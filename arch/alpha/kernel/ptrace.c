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
#include <linux/malloc.h>

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>

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
#define PT_REG(reg)	(PAGE_SIZE*2 - sizeof(struct pt_regs)		\
			 + (long)&((struct pt_regs *)0)->reg)

#define SW_REG(reg)	(PAGE_SIZE*2 - sizeof(struct pt_regs)		\
			 - sizeof(struct switch_stack)			\
			 + (long)&((struct switch_stack *)0)->reg)

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
		addr = &task->tss.usp;
	} else if (regno == 31 || regno > 64) {
		zero = 0;
		addr = &zero;
	} else {
		addr = (long *)((long)task + regoff[regno]);
	}
	return addr;
}

/*
 * Get contents of register REGNO in task TASK.
 */
static inline long
get_reg(struct task_struct * task, unsigned long regno)
{
	return *get_reg_addr(task, regno);
}

/*
 * Write contents of register REGNO in task TASK.
 */
static inline int
put_reg(struct task_struct *task, unsigned long regno, long data)
{
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
		child->tss.bpt_addr[nsaved++] = pc + 4;
		if (displ)		/* guard against unoptimized code */
			child->tss.bpt_addr[nsaved++] = pc + 4 + displ;
		DBG(DBG_BPT, ("execing branch\n"));
	} else if (op_code == 0x1a) {
		reg_b = (insn >> 16) & 0x1f;
		child->tss.bpt_addr[nsaved++] = get_reg(child, reg_b);
		DBG(DBG_BPT, ("execing jump\n"));
	} else {
		child->tss.bpt_addr[nsaved++] = pc + 4;
		DBG(DBG_BPT, ("execing normal insn\n"));
	}

	/* install breakpoints: */
	for (i = 0; i < nsaved; ++i) {
		res = read_int(child, child->tss.bpt_addr[i], &insn);
		if (res < 0)
			return res;
		child->tss.bpt_insn[i] = insn;
		DBG(DBG_BPT, ("    -> next_pc=%lx\n", child->tss.bpt_addr[i]));
		res = write_int(child, child->tss.bpt_addr[i], BREAKINST);
		if (res < 0)
			return res;
	}
	child->tss.bpt_nsaved = nsaved;
	return 0;
}

/*
 * Ensure no single-step breakpoint is pending.  Returns non-zero
 * value if child was being single-stepped.
 */
int
ptrace_cancel_bpt(struct task_struct * child)
{
	int i, nsaved = child->tss.bpt_nsaved;

	child->tss.bpt_nsaved = 0;

	if (nsaved > 2) {
		printk("ptrace_cancel_bpt: bogus nsaved: %d!\n", nsaved);
		nsaved = 2;
	}

	for (i = 0; i < nsaved; ++i) {
		write_int(child, child->tss.bpt_addr[i],
			  child->tss.bpt_insn[i]);
	}
	return (nsaved != 0);
}

asmlinkage long
sys_ptrace(long request, long pid, long addr, long data,
	   int a4, int a5, struct pt_regs regs)
{
	struct task_struct *child;
	long ret;

	lock_kernel();
	DBG(DBG_MEM, ("request=%ld pid=%ld addr=0x%lx data=0x%lx\n",
		      request, pid, addr, data));
	ret = -EPERM;
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED)
			goto out;
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		ret = 0;
		goto out;
	}
	if (pid == 1)		/* you may not mess with init */
		goto out;
	ret = -ESRCH;
	if (!(child = find_task_by_pid(pid)))
		goto out;
	if (request == PTRACE_ATTACH) {
		ret = -EPERM;
		if (child == current)
			goto out;
		if ((!child->dumpable ||
		     (current->uid != child->euid) ||
		     (current->uid != child->suid) ||
		     (current->uid != child->uid) ||
		     (current->gid != child->egid) ||
		     (current->gid != child->sgid) ||
		     (current->gid != child->gid) ||
		     (!cap_issubset(child->cap_permitted, current->cap_permitted)))
		    && !capable(CAP_SYS_PTRACE))
			goto out;
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			goto out;
		child->flags |= PF_PTRACED;
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		send_sig(SIGSTOP, child, 1);
		ret = 0;
		goto out;
	}
	ret = -ESRCH;
	if (!(child->flags & PF_PTRACED)) {
		DBG(DBG_MEM, ("child not traced\n"));
		goto out;
	}
	if (child->state != TASK_STOPPED) {
		DBG(DBG_MEM, ("child process not stopped\n"));
		if (request != PTRACE_KILL)
			goto out;
	}
	if (child->p_pptr != current) {
		DBG(DBG_MEM, ("child not parent of this process\n"));
		goto out;
	}

	switch (request) {
	/* When I and D space are separate, these will need to be fixed.  */
	case PTRACE_PEEKTEXT: /* read word at location addr. */
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		ret = -EIO;
		if (copied != sizeof(tmp))
			goto out;
		
		regs.r0 = 0;	/* special return: no errors */
		ret = tmp;
		goto out;
	}

	/* Read register number ADDR. */
	case PTRACE_PEEKUSR:
		regs.r0 = 0;	/* special return: no errors */
		ret = get_reg(child, addr);
		DBG(DBG_MEM, ("peek $%ld->%#lx\n", addr, ret));
		goto out;

	/* When I and D space are separate, this will have to be fixed.  */
	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA: {
		unsigned long tmp = data;
		int copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 1);
		ret = (copied == sizeof(tmp)) ? 0 : -EIO;
		goto out;
	}

	case PTRACE_POKEUSR: /* write the specified register */
		DBG(DBG_MEM, ("poke $%ld<-%#lx\n", addr, data));
		ret = put_reg(child, addr, data);
		goto out;

	case PTRACE_SYSCALL: /* continue and stop at next
				(return from) syscall */
	case PTRACE_CONT:    /* restart after signal. */
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			goto out;
		if (request == PTRACE_SYSCALL)
			child->flags |= PF_TRACESYS;
		else
			child->flags &= ~PF_TRACESYS;
		child->exit_code = data;
		wake_up_process(child);
		/* make sure single-step breakpoint is gone. */
		ptrace_cancel_bpt(child);
		ret = data;
		goto out;

	/*
	 * Make the child exit.  Best I can do is send it a sigkill.
	 * perhaps it should be put in the status that it wants to
	 * exit.
	 */
	case PTRACE_KILL:
		if (child->state != TASK_ZOMBIE) {
			wake_up_process(child);
			child->exit_code = SIGKILL;
		}
		/* make sure single-step breakpoint is gone. */
		ptrace_cancel_bpt(child);
		ret = 0;
		goto out;

	case PTRACE_SINGLESTEP:  /* execute single instruction. */
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			goto out;
		child->tss.bpt_nsaved = -1;	/* mark single-stepping */
		child->flags &= ~PF_TRACESYS;
		wake_up_process(child);
		child->exit_code = data;
		/* give it a chance to run. */
		ret = 0;
		goto out;

	case PTRACE_DETACH: /* detach a process that was attached. */
		ret = -EIO;
		if ((unsigned long) data > _NSIG)
			goto out;
		child->flags &= ~(PF_PTRACED|PF_TRACESYS);
		wake_up_process(child);
		child->exit_code = data;
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		/* make sure single-step breakpoint is gone. */
		ptrace_cancel_bpt(child);
		ret = 0;
		goto out;

	default:
		ret = -EIO;
		goto out;
	}
 out:
	unlock_kernel();
	return ret;
}

asmlinkage void
syscall_trace(void)
{
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
	    != (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
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
