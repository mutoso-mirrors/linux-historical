/*
 * Kernel support for the ptrace() and syscall tracing interfaces.
 *
 * Copyright (C) 1999-2001 Hewlett-Packard Co
 * Copyright (C) 1999-2001 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * Derived from the x86 and Alpha versions.  Most of the code in here
 * could actually be factored into a common set of routines.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp_lock.h>
#include <linux/user.h>

#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/ptrace_offsets.h>
#include <asm/rse.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/unwind.h>

/*
 * Bits in the PSR that we allow ptrace() to change:
 *	be, up, ac, mfl, mfh (the user mask; five bits total)
 *	db (debug breakpoint fault; one bit)
 *	id (instruction debug fault disable; one bit)
 *	dd (data debug fault disable; one bit)
 *	ri (restart instruction; two bits)
 *	is (instruction set; one bit)
 */
#define IPSR_WRITE_MASK \
	(IA64_PSR_UM | IA64_PSR_DB | IA64_PSR_IS | IA64_PSR_ID | IA64_PSR_DD | IA64_PSR_RI)
#define IPSR_READ_MASK	IPSR_WRITE_MASK

#define PTRACE_DEBUG	1

#if PTRACE_DEBUG
# define dprintk(format...)	printk(format)
# define inline
#else
# define dprintk(format...)
#endif

/*
 * Collect the NaT bits for r1-r31 from scratch_unat and return a NaT
 * bitset where bit i is set iff the NaT bit of register i is set.
 */
unsigned long
ia64_get_scratch_nat_bits (struct pt_regs *pt, unsigned long scratch_unat)
{
#	define GET_BITS(first, last, unat)						\
	({										\
		unsigned long bit = ia64_unat_pos(&pt->r##first);			\
		unsigned long mask = ((1UL << (last - first + 1)) - 1) << first;	\
		(ia64_rotl(unat, first) >> bit) & mask;					\
	})
	unsigned long val;

	val  = GET_BITS( 1,  3, scratch_unat);
	val |= GET_BITS(12, 15, scratch_unat);
	val |= GET_BITS( 8, 11, scratch_unat);
	val |= GET_BITS(16, 31, scratch_unat);
	return val;

#	undef GET_BITS
}

/*
 * Set the NaT bits for the scratch registers according to NAT and
 * return the resulting unat (assuming the scratch registers are
 * stored in PT).
 */
unsigned long
ia64_put_scratch_nat_bits (struct pt_regs *pt, unsigned long nat)
{
	unsigned long scratch_unat;

#	define PUT_BITS(first, last, nat)					\
	({									\
		unsigned long bit = ia64_unat_pos(&pt->r##first);		\
		unsigned long mask = ((1UL << (last - first + 1)) - 1) << bit;	\
		(ia64_rotr(nat, first) << bit) & mask;				\
	})
	scratch_unat  = PUT_BITS( 1,  3, nat);
	scratch_unat |= PUT_BITS(12, 15, nat);
	scratch_unat |= PUT_BITS( 8, 11, nat);
	scratch_unat |= PUT_BITS(16, 31, nat);

	return scratch_unat;

#	undef PUT_BITS
}

#define IA64_MLX_TEMPLATE	0x2
#define IA64_MOVL_OPCODE	6

void
ia64_increment_ip (struct pt_regs *regs)
{
	unsigned long w0, ri = ia64_psr(regs)->ri + 1;

	if (ri > 2) {
		ri = 0;
		regs->cr_iip += 16;
	} else if (ri == 2) {
		get_user(w0, (char *) regs->cr_iip + 0);
		if (((w0 >> 1) & 0xf) == IA64_MLX_TEMPLATE) {
			/*
			 * rfi'ing to slot 2 of an MLX bundle causes
			 * an illegal operation fault.  We don't want
			 * that to happen...
			 */
			ri = 0;
			regs->cr_iip += 16;
		}
	}
	ia64_psr(regs)->ri = ri;
}

void
ia64_decrement_ip (struct pt_regs *regs)
{
	unsigned long w0, ri = ia64_psr(regs)->ri - 1;

	if (ia64_psr(regs)->ri == 0) {
		regs->cr_iip -= 16;
		ri = 2;
		get_user(w0, (char *) regs->cr_iip + 0);
		if (((w0 >> 1) & 0xf) == IA64_MLX_TEMPLATE) {
			/*
			 * rfi'ing to slot 2 of an MLX bundle causes
			 * an illegal operation fault.  We don't want
			 * that to happen...
			 */
			ri = 1;
		}
	}
	ia64_psr(regs)->ri = ri;
}

/*
 * This routine is used to read an rnat bits that are stored on the
 * kernel backing store.  Since, in general, the alignment of the user
 * and kernel are different, this is not completely trivial.  In
 * essence, we need to construct the user RNAT based on up to two
 * kernel RNAT values and/or the RNAT value saved in the child's
 * pt_regs.
 *
 * user rbs
 *
 * +--------+ <-- lowest address
 * | slot62 |
 * +--------+
 * |  rnat  | 0x....1f8
 * +--------+
 * | slot00 | \
 * +--------+ |
 * | slot01 | > child_regs->ar_rnat
 * +--------+ |
 * | slot02 | /				kernel rbs
 * +--------+				+--------+
 *	    <- child_regs->ar_bspstore	| slot61 | <-- krbs
 * +- - - - +				+--------+
 *					| slot62 |
 * +- - - - +				+--------+
 *					|  rnat	 |
 * +- - - - +				+--------+
 *   vrnat				| slot00 |
 * +- - - - +				+--------+
 *					=	 =
 *					+--------+
 *					| slot00 | \
 *					+--------+ |
 *					| slot01 | > child_stack->ar_rnat
 *					+--------+ |
 *					| slot02 | /
 *					+--------+
 *						  <--- child_stack->ar_bspstore
 *
 * The way to think of this code is as follows: bit 0 in the user rnat
 * corresponds to some bit N (0 <= N <= 62) in one of the kernel rnat
 * value.  The kernel rnat value holding this bit is stored in
 * variable rnat0.  rnat1 is loaded with the kernel rnat value that
 * form the upper bits of the user rnat value.
 *
 * Boundary cases:
 *
 * o when reading the rnat "below" the first rnat slot on the kernel
 *   backing store, rnat0/rnat1 are set to 0 and the low order bits
 *   are merged in from pt->ar_rnat.
 *
 * o when reading the rnat "above" the last rnat slot on the kernel
 *   backing store, rnat0/rnat1 gets its value from sw->ar_rnat.
 */
static unsigned long
get_rnat (struct pt_regs *pt, struct switch_stack *sw,
	  unsigned long *krbs, unsigned long *urnat_addr)
{
	unsigned long rnat0 = 0, rnat1 = 0, urnat = 0, *slot0_kaddr, kmask = ~0UL;
	unsigned long *kbsp, *ubspstore, *rnat0_kaddr, *rnat1_kaddr, shift;
	long num_regs;

	kbsp = (unsigned long *) sw->ar_bspstore;
	ubspstore = (unsigned long *) pt->ar_bspstore;
	/*
	 * First, figure out which bit number slot 0 in user-land maps
	 * to in the kernel rnat.  Do this by figuring out how many
	 * register slots we're beyond the user's backingstore and
	 * then computing the equivalent address in kernel space.
	 */
	num_regs = ia64_rse_num_regs(ubspstore, urnat_addr + 1);
	slot0_kaddr = ia64_rse_skip_regs(krbs, num_regs);
	shift = ia64_rse_slot_num(slot0_kaddr);
	rnat1_kaddr = ia64_rse_rnat_addr(slot0_kaddr);
	rnat0_kaddr = rnat1_kaddr - 64;

	if (ubspstore + 63 > urnat_addr) {
		/* some bits need to be merged in from pt->ar_rnat */
		kmask = ~((1UL << ia64_rse_slot_num(ubspstore)) - 1);
		urnat = (pt->ar_rnat & ~kmask);
	}
	if (rnat0_kaddr >= kbsp) {
		rnat0 = sw->ar_rnat;
	} else if (rnat0_kaddr > krbs) {
		rnat0 = *rnat0_kaddr;
	}
	if (rnat1_kaddr >= kbsp) {
		rnat1 = sw->ar_rnat;
	} else if (rnat1_kaddr > krbs) {
		rnat1 = *rnat1_kaddr;
	}
	urnat |= ((rnat1 << (63 - shift)) | (rnat0 >> shift)) & kmask;
	return urnat;
}

/*
 * The reverse of get_rnat.
 */
static void
put_rnat (struct pt_regs *pt, struct switch_stack *sw,
	  unsigned long *krbs, unsigned long *urnat_addr, unsigned long urnat)
{
	unsigned long rnat0 = 0, rnat1 = 0, rnat = 0, *slot0_kaddr, kmask = ~0UL, mask;
	unsigned long *kbsp, *ubspstore, *rnat0_kaddr, *rnat1_kaddr, shift;
	long num_regs;

	kbsp = (unsigned long *) sw->ar_bspstore;
	ubspstore = (unsigned long *) pt->ar_bspstore;
	/*
	 * First, figure out which bit number slot 0 in user-land maps
	 * to in the kernel rnat.  Do this by figuring out how many
	 * register slots we're beyond the user's backingstore and
	 * then computing the equivalent address in kernel space.
	 */
	num_regs = (long) ia64_rse_num_regs(ubspstore, urnat_addr + 1);
	slot0_kaddr = ia64_rse_skip_regs(krbs, num_regs);
	shift = ia64_rse_slot_num(slot0_kaddr);
	rnat1_kaddr = ia64_rse_rnat_addr(slot0_kaddr);
	rnat0_kaddr = rnat1_kaddr - 64;

	if (ubspstore + 63 > urnat_addr) {
		/* some bits need to be place in pt->ar_rnat: */
		kmask = ~((1UL << ia64_rse_slot_num(ubspstore)) - 1);
		pt->ar_rnat = (pt->ar_rnat & kmask) | (rnat & ~kmask);
	}
	/*
	 * Note: Section 11.1 of the EAS guarantees that bit 63 of an
	 * rnat slot is ignored. so we don't have to clear it here.
	 */
	rnat0 = (urnat << shift);
	mask = ~0UL << shift;
	if (rnat0_kaddr >= kbsp) {
		sw->ar_rnat = (sw->ar_rnat & ~mask) | (rnat0 & mask);
	} else if (rnat0_kaddr > krbs) {
		*rnat0_kaddr = ((*rnat0_kaddr & ~mask) | (rnat0 & mask));
	}

	rnat1 = (urnat >> (63 - shift));
	mask = ~0UL >> (63 - shift);
	if (rnat1_kaddr >= kbsp) {
		sw->ar_rnat = (sw->ar_rnat & ~mask) | (rnat1 & mask);
	} else if (rnat1_kaddr > krbs) {
		*rnat1_kaddr = ((*rnat1_kaddr & ~mask) | (rnat1 & mask));
	}
}

long
ia64_peek (struct task_struct *child, unsigned long user_bsp, unsigned long addr, long *val)
{
	unsigned long *bspstore, *krbs, regnum, *laddr, *ubsp = (long *) user_bsp;
	struct switch_stack *child_stack;
	struct pt_regs *child_regs;
	size_t copied;
	long ret;

	laddr = (unsigned long *) addr;
	child_regs = ia64_task_regs(child);
	child_stack = (struct switch_stack *) (child->thread.ksp + 16);
	bspstore = (unsigned long *) child_regs->ar_bspstore;
	krbs = (unsigned long *) child + IA64_RBS_OFFSET/8;
	if (laddr >= bspstore && laddr <= ia64_rse_rnat_addr(ubsp)) {
		/*
		 * Attempt to read the RBS in an area that's actually on the kernel RBS =>
		 * read the corresponding bits in the kernel RBS.
		 */
		if (ia64_rse_is_rnat_slot(laddr))
			ret = get_rnat(child_regs, child_stack, krbs, laddr);
		else {
			if (laddr >= ubsp)
				ret = 0;
			else {
				regnum = ia64_rse_num_regs(bspstore, laddr);
				ret = *ia64_rse_skip_regs(krbs, regnum);
			}
		}
	} else {
		copied = access_process_vm(child, addr, &ret, sizeof(ret), 0);
		if (copied != sizeof(ret))
			return -EIO;
	}
	*val = ret;
	return 0;
}

long
ia64_poke (struct task_struct *child, unsigned long user_bsp, unsigned long addr, long val)
{
	unsigned long *bspstore, *krbs, regnum, *laddr, *ubsp = (long *) user_bsp;
	struct switch_stack *child_stack;
	struct pt_regs *child_regs;

	laddr = (unsigned long *) addr;
	child_regs = ia64_task_regs(child);
	child_stack = (struct switch_stack *) (child->thread.ksp + 16);
	bspstore = (unsigned long *) child_regs->ar_bspstore;
	krbs = (unsigned long *) child + IA64_RBS_OFFSET/8;
	if (laddr >= bspstore && laddr <= ia64_rse_rnat_addr(ubsp)) {
		/*
		 * Attempt to write the RBS in an area that's actually on the kernel RBS
		 * => write the corresponding bits in the kernel RBS.
		 */
		if (ia64_rse_is_rnat_slot(laddr))
			put_rnat(child_regs, child_stack, krbs, laddr, val);
		else {
			if (laddr < ubsp) {
				regnum = ia64_rse_num_regs(bspstore, laddr);
				*ia64_rse_skip_regs(krbs, regnum) = val;
			}
		}
	} else if (access_process_vm(child, addr, &val, sizeof(val), 1) != sizeof(val)) {
		return -EIO;
	}
	return 0;
}

/*
 * Calculate the user-level address that would have been in ar.bsp had the user executed a
 * "cover" instruction right before entering the kernel.
 */
unsigned long
ia64_get_user_bsp (struct task_struct *child, struct pt_regs *pt)
{
	unsigned long *krbs, *bspstore, cfm;
	struct unw_frame_info info;
	long ndirty;

	krbs = (unsigned long *) child + IA64_RBS_OFFSET/8;
	bspstore = (unsigned long *) pt->ar_bspstore;
	ndirty = ia64_rse_num_regs(krbs, krbs + (pt->loadrs >> 19));

	if ((long) pt->cr_ifs >= 0) {
		/*
		 * If bit 63 of cr.ifs is cleared, the kernel was entered via a system
		 * call and we need to recover the CFM that existed on entry to the
		 * kernel by unwinding the kernel stack.
		 */
		unw_init_from_blocked_task(&info, child);
		if (unw_unwind_to_user(&info) == 0) {
			unw_get_cfm(&info, &cfm);
			ndirty += (cfm & 0x7f);
		}
	}
	return (unsigned long) ia64_rse_skip_regs(bspstore, ndirty);
}

/*
 * Synchronize (i.e, write) the RSE backing store living in kernel space to the VM of the
 * indicated child process.
 *
 * If new_bsp is non-zero, the bsp will (effectively) be updated to the new value upon
 * resumption of the child process.  This is accomplished by setting the loadrs value to
 * zero and the bspstore value to the new bsp value.
 *
 * When new_bsp and flush_user_rbs are both 0, the register backing store in kernel space
 * is written to user space and the loadrs and bspstore values are left alone.
 *
 * When new_bsp is zero and flush_user_rbs is 1 (non-zero), loadrs is set to 0, and the
 * bspstore value is set to the old bsp value.  This will cause the stacked registers (r32
 * and up) to be obtained entirely from the child's memory space rather than from the
 * kernel.  (This makes it easier to write code for modifying the stacked registers in
 * multi-threaded programs.)
 *
 * Note: I had originally written this function without the flush_user_rbs parameter; it
 * was written so that loadrs would always be set to zero.  But I had problems with
 * certain system calls apparently causing a portion of the RBS to be zeroed.  (I still
 * don't understand why this was happening.) Anyway, it'd definitely less intrusive to
 * leave loadrs and bspstore alone if possible.
 */
static long
sync_kernel_register_backing_store (struct task_struct *child, long user_bsp, long new_bsp,
				    int flush_user_rbs)
{
	struct pt_regs *child_regs = ia64_task_regs(child);
	unsigned long addr, val;
	long ret;

	/*
	 * Return early if nothing to do.  Note that new_bsp will be zero if the caller
	 * wants to force synchronization without changing bsp.
	 */
	if (user_bsp == new_bsp)
		return 0;

	/* Write portion of backing store living on kernel stack to the child's VM. */
	for (addr = child_regs->ar_bspstore; addr < user_bsp; addr += 8) {
		ret = ia64_peek(child, user_bsp, addr, &val);
		if (ret != 0)
			return ret;
		if (access_process_vm(child, addr, &val, sizeof(val), 1) != sizeof(val))
			return -EIO;
	}

	if (new_bsp != 0) {
		flush_user_rbs = 1;
		user_bsp = new_bsp;
	}

	if (flush_user_rbs) {
		child_regs->loadrs = 0;
		child_regs->ar_bspstore = user_bsp;
	}
	return 0;
}

static void
sync_thread_rbs (struct task_struct *child, long bsp, struct mm_struct *mm, int make_writable)
{
	struct task_struct *p;
	read_lock(&tasklist_lock);
	{
		for_each_task(p) {
			if (p->mm == mm && p->state != TASK_RUNNING)
				sync_kernel_register_backing_store(p, bsp, 0, make_writable);
		}
	}
	read_unlock(&tasklist_lock);
	child->thread.flags |= IA64_THREAD_KRBS_SYNCED;
}

/*
 * Write f32-f127 back to task->thread.fph if it has been modified.
 */
inline void
ia64_flush_fph (struct task_struct *task)
{
	struct ia64_psr *psr = ia64_psr(ia64_task_regs(task));
#ifdef CONFIG_SMP
	struct task_struct *fpu_owner = current;
#else
	struct task_struct *fpu_owner = ia64_get_fpu_owner();
#endif

	if (task == fpu_owner && psr->mfh) {
		psr->mfh = 0;
		ia64_save_fpu(&task->thread.fph[0]);
		task->thread.flags |= IA64_THREAD_FPH_VALID;
	}
}

/*
 * Sync the fph state of the task so that it can be manipulated
 * through thread.fph.  If necessary, f32-f127 are written back to
 * thread.fph or, if the fph state hasn't been used before, thread.fph
 * is cleared to zeroes.  Also, access to f32-f127 is disabled to
 * ensure that the task picks up the state from thread.fph when it
 * executes again.
 */
void
ia64_sync_fph (struct task_struct *task)
{
	struct ia64_psr *psr = ia64_psr(ia64_task_regs(task));

	ia64_flush_fph(task);
	if (!(task->thread.flags & IA64_THREAD_FPH_VALID)) {
		task->thread.flags |= IA64_THREAD_FPH_VALID;
		memset(&task->thread.fph, 0, sizeof(task->thread.fph));
	}
#ifndef CONFIG_SMP
	if (ia64_get_fpu_owner() == task)
		ia64_set_fpu_owner(0);
#endif
	psr->dfh = 1;
}

static int
access_fr (struct unw_frame_info *info, int regnum, int hi, unsigned long *data, int write_access)
{
	struct ia64_fpreg fpval;
	int ret;

	ret = unw_get_fr(info, regnum, &fpval);
	if (ret < 0)
		return ret;

	if (write_access) {
		fpval.u.bits[hi] = *data;
		ret = unw_set_fr(info, regnum, fpval);
	} else
		*data = fpval.u.bits[hi];
	return ret;
}

static int
access_uarea (struct task_struct *child, unsigned long addr, unsigned long *data, int write_access)
{
	unsigned long *ptr, regnum, bsp, rnat_addr;
	struct switch_stack *sw;
	struct unw_frame_info info;
	struct pt_regs *pt;

	pt = ia64_task_regs(child);
	sw = (struct switch_stack *) (child->thread.ksp + 16);

	if ((addr & 0x7) != 0) {
		dprintk("ptrace: unaligned register address 0x%lx\n", addr);
		return -1;
	}

	if (addr < PT_F127 + 16) {
		/* accessing fph */
		if (write_access)
			ia64_sync_fph(child);
		else
			ia64_flush_fph(child);
		ptr = (unsigned long *) ((unsigned long) &child->thread.fph + addr);
	} else if (addr >= PT_F10 && addr < PT_F15 + 16) {
		/* scratch registers untouched by kernel (saved in switch_stack) */
		ptr = (unsigned long *) ((long) sw + addr - PT_NAT_BITS);
	} else if (addr < PT_AR_LC + 8) {
		/* preserved state: */
		unsigned long nat_bits, scratch_unat, dummy = 0;
		struct unw_frame_info info;
		char nat = 0;
		int ret;

		unw_init_from_blocked_task(&info, child);
		if (unw_unwind_to_user(&info) < 0)
			return -1;

		switch (addr) {
		      case PT_NAT_BITS:
			if (write_access) {
				nat_bits = *data;
				scratch_unat = ia64_put_scratch_nat_bits(pt, nat_bits);
				if (unw_set_ar(&info, UNW_AR_UNAT, scratch_unat) < 0) {
					dprintk("ptrace: failed to set ar.unat\n");
					return -1;
				}
				for (regnum = 4; regnum <= 7; ++regnum) {
					unw_get_gr(&info, regnum, &dummy, &nat);
					unw_set_gr(&info, regnum, dummy, (nat_bits >> regnum) & 1);
				}
			} else {
				if (unw_get_ar(&info, UNW_AR_UNAT, &scratch_unat) < 0) {
					dprintk("ptrace: failed to read ar.unat\n");
					return -1;
				}
				nat_bits = ia64_get_scratch_nat_bits(pt, scratch_unat);
				for (regnum = 4; regnum <= 7; ++regnum) {
					unw_get_gr(&info, regnum, &dummy, &nat);
					nat_bits |= (nat != 0) << regnum;
				}
				*data = nat_bits;
			}
			return 0;

		      case PT_R4: case PT_R5: case PT_R6: case PT_R7:
			if (write_access) {
				/* read NaT bit first: */
				ret = unw_get_gr(&info, (addr - PT_R4)/8 + 4, data, &nat);
				if (ret < 0)
					return ret;
			}
			return unw_access_gr(&info, (addr - PT_R4)/8 + 4, data, &nat,
					     write_access);

		      case PT_B1: case PT_B2: case PT_B3: case PT_B4: case PT_B5:
			return unw_access_br(&info, (addr - PT_B1)/8 + 1, data, write_access);

		      case PT_AR_EC:
			return unw_access_ar(&info, UNW_AR_EC, data, write_access);

		      case PT_AR_LC:
			return unw_access_ar(&info, UNW_AR_LC, data, write_access);

		      default:
			if (addr >= PT_F2 && addr < PT_F5 + 16)
				return access_fr(&info, (addr - PT_F2)/16 + 2, (addr & 8) != 0,
						 data, write_access);
			else if (addr >= PT_F16 && addr < PT_F31 + 16)
				return access_fr(&info, (addr - PT_F16)/16 + 16, (addr & 8) != 0,
						 data, write_access);
			else {
				dprintk("ptrace: rejecting access to register address 0x%lx\n",
					addr);
				return -1;
			}
		}
	} else if (addr < PT_F9+16) {
		/* scratch state */
		switch (addr) {
		      case PT_AR_BSP:
			bsp = ia64_get_user_bsp(child, pt);
			if (write_access)
				return sync_kernel_register_backing_store(child, bsp, *data, 1);
			else {
				*data = bsp;
				return 0;
			}

		      case PT_CFM:
			if ((long) pt->cr_ifs < 0) {
				if (write_access)
					pt->cr_ifs = ((pt->cr_ifs & ~0x3fffffffffUL)
						      | (*data & 0x3fffffffffUL));
				else
					*data = pt->cr_ifs & 0x3fffffffffUL;
			} else {
				/* kernel was entered through a system call */
				unsigned long cfm;

				unw_init_from_blocked_task(&info, child);
				if (unw_unwind_to_user(&info) < 0)
					return -1;

				unw_get_cfm(&info, &cfm);
				if (write_access)
					unw_set_cfm(&info, ((cfm & ~0x3fffffffffU)
							    | (*data & 0x3fffffffffUL)));
				else
					*data = cfm;
			}
			return 0;

		      case PT_CR_IPSR:
			if (write_access)
				pt->cr_ipsr = ((*data & IPSR_WRITE_MASK)
					       | (pt->cr_ipsr & ~IPSR_WRITE_MASK));
			else
				*data = (pt->cr_ipsr & IPSR_READ_MASK);
			return 0;

		      case PT_AR_RNAT:
			bsp = ia64_get_user_bsp(child, pt);
			rnat_addr = (long) ia64_rse_rnat_addr((long *) bsp - 1);
			if (write_access)
				return ia64_poke(child, bsp, rnat_addr, *data);
			else
				return ia64_peek(child, bsp, rnat_addr, data);

				   case PT_R1:  case PT_R2:  case PT_R3:
		      case PT_R8:  case PT_R9:  case PT_R10: case PT_R11:
		      case PT_R12: case PT_R13: case PT_R14: case PT_R15:
		      case PT_R16: case PT_R17: case PT_R18: case PT_R19:
		      case PT_R20: case PT_R21: case PT_R22: case PT_R23:
		      case PT_R24: case PT_R25: case PT_R26: case PT_R27:
		      case PT_R28: case PT_R29: case PT_R30: case PT_R31:
		      case PT_B0:  case PT_B6:  case PT_B7:
		      case PT_F6:  case PT_F6+8: case PT_F7: case PT_F7+8:
		      case PT_F8:  case PT_F8+8: case PT_F9: case PT_F9+8:
		      case PT_AR_BSPSTORE:
		      case PT_AR_RSC: case PT_AR_UNAT: case PT_AR_PFS:
		      case PT_AR_CCV: case PT_AR_FPSR: case PT_CR_IIP: case PT_PR:
			/* scratch register */
			ptr = (unsigned long *) ((long) pt + addr - PT_CR_IPSR);
			break;

		      default:
			/* disallow accessing anything else... */
			dprintk("ptrace: rejecting access to register address 0x%lx\n",
				addr);
			return -1;
		}
	} else {
		/* access debug registers */

		if (!(child->thread.flags & IA64_THREAD_DBG_VALID)) {
			child->thread.flags |= IA64_THREAD_DBG_VALID;
			memset(child->thread.dbr, 0, sizeof(child->thread.dbr));
			memset(child->thread.ibr, 0, sizeof(child->thread.ibr));
		}
		if (addr >= PT_IBR) {
			regnum = (addr - PT_IBR) >> 3;
			ptr = &child->thread.ibr[0];
		} else {
			regnum = (addr - PT_DBR) >> 3;
			ptr = &child->thread.dbr[0];
		}

		if (regnum >= 8) {
			dprintk("ptrace: rejecting access to register address 0x%lx\n", addr);
			return -1;
		}

		ptr += regnum;

		if (write_access)
			/* don't let the user set kernel-level breakpoints... */
			*ptr = *data & ~(7UL << 56);
		else
			*data = *ptr;
		return 0;
	}
	if (write_access)
		*ptr = *data;
	else
		*data = *ptr;
	return 0;
}

asmlinkage long
sys_ptrace (long request, pid_t pid, unsigned long addr, unsigned long data,
	    long arg4, long arg5, long arg6, long arg7, long stack)
{
	struct pt_regs *pt, *regs = (struct pt_regs *) &stack;
	struct task_struct *child;
	unsigned long flags, bsp;
	long ret;

	lock_kernel();
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED)
			goto out;
		current->ptrace |= PT_PTRACED;
		ret = 0;
		goto out;
	}

	ret = -ESRCH;
	read_lock(&tasklist_lock);
	{
		child = find_task_by_pid(pid);
		if (child)
			get_task_struct(child);
	}
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;
	ret = -EPERM;
	if (pid == 1)		/* no messing around with init! */
		goto out_tsk;

	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		goto out_tsk;
	}
	ret = -ESRCH;
	if (!(child->ptrace & PT_PTRACED))
		goto out_tsk;

	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out_tsk;
	}

	if (child->p_pptr != current)
		goto out_tsk;

	pt = ia64_task_regs(child);

	switch (request) {
	      case PTRACE_PEEKTEXT:
	      case PTRACE_PEEKDATA:		/* read word at location addr */
		bsp = ia64_get_user_bsp(child, pt);
		if (!(child->thread.flags & IA64_THREAD_KRBS_SYNCED)) {
			struct mm_struct *mm;
			long do_sync;

			task_lock(child);
			{
				mm = child->mm;
				do_sync = mm && (atomic_read(&mm->mm_users) > 1);
			}
			task_unlock(child);
			if (do_sync)
				sync_thread_rbs(child, bsp, mm, 0);
		}
		ret = ia64_peek(child, bsp, addr, &data);
		if (ret == 0) {
			ret = data;
			regs->r8 = 0;	/* ensure "ret" is not mistaken as an error code */
		}
		goto out_tsk;

	      case PTRACE_POKETEXT:
	      case PTRACE_POKEDATA:		/* write the word at location addr */
		bsp = ia64_get_user_bsp(child, pt);
		if (!(child->thread.flags & IA64_THREAD_KRBS_SYNCED)) {
			struct mm_struct *mm;
			long do_sync;

			task_lock(child);
			{
				mm = child->mm;
				do_sync = mm && (atomic_read(&child->mm->mm_users) > 1);
			}
			task_unlock(child);
			if (do_sync)
				sync_thread_rbs(child, bsp, mm, 1);
		}
		ret = ia64_poke(child, bsp, addr, data);
		goto out_tsk;

	      case PTRACE_PEEKUSR:		/* read the word at addr in the USER area */
		if (access_uarea(child, addr, &data, 0) < 0) {
			ret = -EIO;
			goto out_tsk;
		}
		ret = data;
		regs->r8 = 0;	/* ensure "ret" is not mistaken as an error code */
		goto out_tsk;

	      case PTRACE_POKEUSR:	      /* write the word at addr in the USER area */
		if (access_uarea(child, addr, &data, 1) < 0) {
			ret = -EIO;
			goto out_tsk;
		}
		ret = 0;
		goto out_tsk;

	      case PTRACE_GETSIGINFO:
		ret = -EIO;
		if (!access_ok(VERIFY_WRITE, data, sizeof (siginfo_t)) || !child->thread.siginfo)
			goto out_tsk;
		ret = copy_siginfo_to_user((siginfo_t *) data, child->thread.siginfo);
		goto out_tsk;

	      case PTRACE_SETSIGINFO:
		ret = -EIO;
		if (!access_ok(VERIFY_READ, data, sizeof (siginfo_t))
		    || child->thread.siginfo == 0)
			goto out_tsk;
		ret = copy_siginfo_from_user(child->thread.siginfo, (siginfo_t *) data);
		goto out_tsk;

	      case PTRACE_SYSCALL:	/* continue and stop at next (return from) syscall */
	      case PTRACE_CONT:		/* restart after signal. */
		ret = -EIO;
		if (data > _NSIG)
			goto out_tsk;
		if (request == PTRACE_SYSCALL)
			child->ptrace |= PT_TRACESYS;
		else
			child->ptrace &= ~PT_TRACESYS;
		child->exit_code = data;

		/* make sure the single step/take-branch tra bits are not set: */
		ia64_psr(pt)->ss = 0;
		ia64_psr(pt)->tb = 0;

		/* Turn off flag indicating that the KRBS is sync'd with child's VM: */
		child->thread.flags &= ~IA64_THREAD_KRBS_SYNCED;

		wake_up_process(child);
		ret = 0;
		goto out_tsk;

	      case PTRACE_KILL:
		/*
		 * Make the child exit.  Best I can do is send it a
		 * sigkill.  Perhaps it should be put in the status
		 * that it wants to exit.
		 */
		if (child->state == TASK_ZOMBIE)		/* already dead */
			goto out_tsk;
		child->exit_code = SIGKILL;

		/* make sure the single step/take-branch tra bits are not set: */
		ia64_psr(pt)->ss = 0;
		ia64_psr(pt)->tb = 0;

		/* Turn off flag indicating that the KRBS is sync'd with child's VM: */
		child->thread.flags &= ~IA64_THREAD_KRBS_SYNCED;

		wake_up_process(child);
		ret = 0;
		goto out_tsk;

	      case PTRACE_SINGLESTEP:		/* let child execute for one instruction */
	      case PTRACE_SINGLEBLOCK:
		ret = -EIO;
		if (data > _NSIG)
			goto out_tsk;

		child->ptrace &= ~PT_TRACESYS;
		if (request == PTRACE_SINGLESTEP) {
			ia64_psr(pt)->ss = 1;
		} else {
			ia64_psr(pt)->tb = 1;
		}
		child->exit_code = data;

		/* Turn off flag indicating that the KRBS is sync'd with child's VM: */
		child->thread.flags &= ~IA64_THREAD_KRBS_SYNCED;

		/* give it a chance to run. */
		wake_up_process(child);
		ret = 0;
		goto out_tsk;

	      case PTRACE_DETACH:		/* detach a process that was attached. */
		ret = -EIO;
		if (data > _NSIG)
			goto out_tsk;

		child->ptrace &= ~(PT_PTRACED|PT_TRACESYS);
		child->exit_code = data;
		write_lock_irqsave(&tasklist_lock, flags);
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		write_unlock_irqrestore(&tasklist_lock, flags);

		/* make sure the single step/take-branch tra bits are not set: */
		ia64_psr(pt)->ss = 0;
		ia64_psr(pt)->tb = 0;

		/* Turn off flag indicating that the KRBS is sync'd with child's VM: */
		child->thread.flags &= ~IA64_THREAD_KRBS_SYNCED;

		wake_up_process(child);
		ret = 0;
		goto out_tsk;

	      default:
		ret = -EIO;
		goto out_tsk;
	}
  out_tsk:
	free_task_struct(child);
  out:
	unlock_kernel();
	return ret;
}

void
syscall_trace (void)
{
	if ((current->ptrace & (PT_PTRACED|PT_TRACESYS)) != (PT_PTRACED|PT_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	set_current_state(TASK_STOPPED);
	notify_parent(current, SIGCHLD);
	schedule();
	/*
	 * This isn't the same as continuing with a signal, but it
	 * will do for normal use.  strace only continues with a
	 * signal if the stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
