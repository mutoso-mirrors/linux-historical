/* ptrace.c */
/* By Ross Biro 1/23/92 */
/* edited by Linus Torvalds */
/* mangled further by Bob Manson (manson@santafe.edu) */
/* more mutilation by David Mosberger (davidm@azstarnet.com) */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/user.h>
#include <linux/slab.h>
#include <linux/security.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/fpu.h>

#include "proto.h"

#define DEBUG	DBG_MEM
#undef DEBUG

#ifdef DEBUG
enum {
	DBG_MEM		= (1<<0),
	DBG_BPT		= (1<<1),
	DBG_MEM_ALL	= (1<<2)
};
#define DBG(fac,args)	{if ((fac) & DEBUG) printk args;}
#else
#define DBG(fac,args)
#endif

#define BREAKINST	0x00000080	/* call_pal bpt */

/*
 * does not yet catch signals sent when the child dies.
 * in exit.c or in signal.c.
 */

/*
 * Processes always block with the following stack-layout:
 *
 *  +================================+ <---- task + 2*PAGE_SIZE
 *  | PALcode saved frame (ps, pc,   | ^
 *  | gp, a0, a1, a2)		     | |
 *  +================================+ | struct pt_regs
 *  |	        		     | |
 *  | frame generated by SAVE_ALL    | |
 *  |	        		     | v
 *  +================================+
 *  |	        		     | ^
 *  | frame saved by do_switch_stack | | struct switch_stack
 *  |	        		     | v
 *  +================================+
 */

/* 
 * The following table maps a register index into the stack offset at
 * which the register is saved.  Register indices are 0-31 for integer
 * regs, 32-63 for fp regs, and 64 for the pc.  Notice that sp and
 * zero have no stack-slot and need to be treated specially (see
 * get_reg/put_reg below).
 */
enum {
	REG_R0 = 0, REG_F0 = 32, REG_FPCR = 63, REG_PC = 64
};

static int regoff[] = {
	PT_REG(	   r0), PT_REG(	   r1), PT_REG(	   r2), PT_REG(	  r3),
	PT_REG(	   r4), PT_REG(	   r5), PT_REG(	   r6), PT_REG(	  r7),
	PT_REG(	   r8), SW_REG(	   r9), SW_REG(	  r10), SW_REG(	 r11),
	SW_REG(	  r12), SW_REG(	  r13), SW_REG(	  r14), SW_REG(	 r15),
	PT_REG(	  r16), PT_REG(	  r17), PT_REG(	  r18), PT_REG(	 r19),
	PT_REG(	  r20), PT_REG(	  r21), PT_REG(	  r22), PT_REG(	 r23),
	PT_REG(	  r24), PT_REG(	  r25), PT_REG(	  r26), PT_REG(	 r27),
	PT_REG(	  r28), PT_REG(	   gp),		   -1,		   -1,
	SW_REG(fp[ 0]), SW_REG(fp[ 1]), SW_REG(fp[ 2]), SW_REG(fp[ 3]),
	SW_REG(fp[ 4]), SW_REG(fp[ 5]), SW_REG(fp[ 6]), SW_REG(fp[ 7]),
	SW_REG(fp[ 8]), SW_REG(fp[ 9]), SW_REG(fp[10]), SW_REG(fp[11]),
	SW_REG(fp[12]), SW_REG(fp[13]), SW_REG(fp[14]), SW_REG(fp[15]),
	SW_REG(fp[16]), SW_REG(fp[17]), SW_REG(fp[18]), SW_REG(fp[19]),
	SW_REG(fp[20]), SW_REG(fp[21]), SW_REG(fp[22]), SW_REG(fp[23]),
	SW_REG(fp[24]), SW_REG(fp[25]), SW_REG(fp[26]), SW_REG(fp[27]),
	SW_REG(fp[28]), SW_REG(fp[29]), SW_REG(fp[30]), SW_REG(fp[31]),
	PT_REG(	   pc)
};

static long zero;

/*
 * Get address of register REGNO in task TASK.
 */
static long *
get_reg_addr(struct task_struct * task, unsigned long regno)
{
	long *addr;

	if (regno == 30) {
		addr = &task->thread_info->pcb.usp;
	} else if (regno == 65) {
		addr = &task->thread_info->pcb.unique;
	} else if (regno == 31 || regno > 65) {
		zero = 0;
		addr = &zero;
	} else {
		addr = (long *)((long)task->thread_info + regoff[regno]);
	}
	return addr;
}

/*
 * Get contents of register REGNO in task TASK.
 */
static long
get_reg(struct task_struct * task, unsigned long regno)
{
	/* Special hack for fpcr -- combine hardware and software bits.  */
	if (regno == 63) {
		unsigned long fpcr = *get_reg_addr(task, regno);
		unsigned long swcr
		  = task->thread_info->ieee_state & IEEE_SW_MASK;
		swcr = swcr_update_status(swcr, fpcr);
		return fpcr | swcr;
	}
	return *get_reg_addr(task, regno);
}

/*
 * Write contents of register REGNO in task TASK.
 */
static int
put_reg(struct task_struct *task, unsigned long regno, long data)
{
	if (regno == 63) {
		task->thread_info->ieee_state
		  = ((task->thread_info->ieee_state & ~IEEE_SW_MASK)
		     | (data & IEEE_SW_MASK));
		data = (data & FPCR_DYN_MASK) | ieee_swcr_to_fpcr(data);
	}
	*get_reg_addr(task, regno) = data;
	return 0;
}

static inline int
read_int(struct task_struct *task, unsigned long addr, int * data)
{
	int copied = access_process_vm(task, addr, data, sizeof(int), 0);
	return (copied == sizeof(int)) ? 0 : -EIO;
}

static inline int
write_int(struct task_struct *task, unsigned long addr, int data)
{
	int copied = access_process_vm(task, addr, &data, sizeof(int), 1);
	return (copied == sizeof(int)) ? 0 : -EIO;
}

/*
 * Set breakpoint.
 */
int
ptrace_set_bpt(struct task_struct * child)
{
	int displ, i, res, reg_b, nsaved = 0;
	u32 insn, op_code;
	unsigned long pc;

	pc  = get_reg(child, REG_PC);
	res = read_int(child, pc, &insn);
	if (res < 0)
		return res;

	op_code = insn >> 26;
	if (op_code >= 0x30) {
		/*
		 * It's a branch: instead of trying to figure out
		 * whether the branch will be taken or not, we'll put
		 * a breakpoint at either location.  This is simpler,
		 * more reliable, and probably not a whole lot slower
		 * than the alternative approach of emulating the
		 * branch (emulation can be tricky for fp branches).
		 */
		displ = ((s32)(insn << 11)) >> 9;
		child->thread_info->bpt_addr[nsaved++] = pc + 4;
		if (displ)		/* guard against unoptimized code */
			child->thread_info->bpt_addr[nsaved++]
			  = pc + 4 + displ;
		DBG(DBG_BPT, ("execing branch\n"));
	} else if (op_code == 0x1a) {
		reg_b = (insn >> 16) & 0x1f;
		child->thread_info->bpt_addr[nsaved++] = get_reg(child, reg_b);
		DBG(DBG_BPT, ("execing jump\n"));
	} else {
		child->thread_info->bpt_addr[nsaved++] = pc + 4;
		DBG(DBG_BPT, ("execing normal insn\n"));
	}

	/* install breakpoints: */
	for (i = 0; i < nsaved; ++i) {
		res = read_int(child, child->thread_info->bpt_addr[i], &insn);
		if (res < 0)
			return res;
		child->thread_info->bpt_insn[i] = insn;
		DBG(DBG_BPT, ("    -> next_pc=%lx\n",
			      child->thread_info->bpt_addr[i]));
		res = write_int(child, child->thread_info->bpt_addr[i],
				BREAKINST);
		if (res < 0)
			return res;
	}
	child->thread_info->bpt_nsaved = nsaved;
	return 0;
}

/*
 * Ensure no single-step breakpoint is pending.  Returns non-zero
 * value if child was being single-stepped.
 */
int
ptrace_cancel_bpt(struct task_struct * child)
{
	int i, nsaved = child->thread_info->bpt_nsaved;

	child->thread_info->bpt_nsaved = 0;

	if (nsaved > 2) {
		printk("ptrace_cancel_bpt: bogus nsaved: %d!\n", nsaved);
		nsaved = 2;
	}

	for (i = 0; i < nsaved; ++i) {
		write_int(child, child->thread_info->bpt_addr[i],
			  child->thread_info->bpt_insn[i]);
	}
	return (nsaved != 0);
}

/*
 * Called by kernel/ptrace.c when detaching..
 *
 * Make sure the single step bit is not set.
 */
void ptrace_disable(struct task_struct *child)
{ 
	ptrace_cancel_bpt(child);
}

asmlinkage long
do_sys_ptrace(long request, long pid, long addr, long data,
	      struct pt_regs *regs)
{
	struct task_struct *child;
	unsigned long tmp;
	size_t copied;
	long ret;

	lock_kernel();
	DBG(DBG_MEM, ("request=%ld pid=%ld addr=0x%lx data=0x%lx\n",
		      request, pid, addr, data));
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED)
			goto out_notsk;
		ret = security_ptrace(current->parent, current);
		if (ret)
			goto out_notsk;
		/* set the ptrace bit in the process ptrace flags. */
		current->ptrace |= PT_PTRACED;
		ret = 0;
		goto out_notsk;
	}
	if (pid == 1)		/* you may not mess with init */
		goto out_notsk;

	ret = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out_notsk;

	if (request == PTRACE_ATTACH) {
		ret = ptrace_attach(child);
		goto out;
	}

	ret = ptrace_check_attach(child, request == PTRACE_KILL);
	if (ret < 0)
		goto out;

	switch (request) {
	/* When I and D space are separate, these will need to be fixed.  */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA:
		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			break;
		
		regs->r0 = 0;	/* special return: no errors */
		ret = tmp;
		break;

	/* Read register number ADDR. */
	case PTRACE_PEEKUSR:
		regs->r0 = 0;	/* special return: no errors */
		ret = get_reg(child, addr);
		DBG(DBG_MEM, ("peek $%ld->%#lx\n", addr, ret));
		break;

	/* When I and D space are separate, this will have to be fixed.  */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		tmp = data;
		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 1);
		ret = (copied == sizeof(tmp)) ? 0 : -EIO;
		break;

	case PTRACE_POKEUSR: /* write the specified register */
		DBG(DBG_MEM, ("poke $%ld<-%#lx\n", addr, data));
		ret = put_reg(child, addr, data);
		break;

	case PTRACE_SYSCALL:
		/* continue and stop at next (return from) syscall */
	case PTRACE_CONT:    /* restart after signal. */
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		if (request == PTRACE_SYSCALL)
			set_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		else
			clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		child->exit_code = data;
		/* make sure single-step breakpoint is gone. */
		ptrace_cancel_bpt(child);
		wake_up_process(child);
		ret = 0;
		break;

	/*
	 * Make the child exit.  Best I can do is send it a sigkill.
	 * perhaps it should be put in the status that it wants to
	 * exit.
	 */
	case PTRACE_KILL:
		ret = 0;
		if (child->state == TASK_ZOMBIE)
			break;
		child->exit_code = SIGKILL;
		/* make sure single-step breakpoint is gone. */
		ptrace_cancel_bpt(child);
		wake_up_process(child);
		goto out;

	case PTRACE_SINGLESTEP:  /* execute single instruction. */
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		/* Mark single stepping.  */
		child->thread_info->bpt_nsaved = -1;
		clear_tsk_thread_flag(child, TIF_SYSCALL_TRACE);
		wake_up_process(child);
		child->exit_code = data;
		/* give it a chance to run. */
		ret = 0;
		goto out;

	case PTRACE_DETACH:	 /* detach a process that was attached. */
		ret = ptrace_detach(child, data);
		goto out;

	default:
		ret = ptrace_request(child, request, addr, data);
		goto out;
	}
 out:
	put_task_struct(child);
 out_notsk:
	unlock_kernel();
	return ret;
}

asmlinkage void
syscall_trace(void)
{
	if (!test_thread_flag(TIF_SYSCALL_TRACE))
		return;
	if (!(current->ptrace & PT_PTRACED))
		return;
	/* The 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	ptrace_notify(SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
				 ? 0x80 : 0));

	/*
	 * This isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
