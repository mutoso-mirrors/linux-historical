/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Ross Biro
 * Copyright (C) Linus Torvalds
 * Copyright (C) 1994, 95, 96, 97, 98, 2000 Ralf Baechle
 * Copyright (C) 1996 David S. Miller
 * Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 1999 MIPS Technologies, Inc.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/user.h>

#include <asm/fp.h>
#include <asm/mipsregs.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/bootinfo.h>
#include <asm/cpu.h>

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	int res;
	extern void save_fp(void*);

	lock_kernel();
#if 0
	printk("ptrace(r=%d,pid=%d,addr=%08lx,data=%08lx)\n",
	       (int) request, (int) pid, (unsigned long) addr,
	       (unsigned long) data);
#endif
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->ptrace & PT_PTRACED) {
			res = -EPERM;
			goto out;
		}
		/* set the ptrace bit in the process flags. */
		current->ptrace |= PT_PTRACED;
		res = 0;
		goto out;
	}
	res = -ESRCH;
	read_lock(&tasklist_lock);
	child = find_task_by_pid(pid);
	if (child)
		get_task_struct(child);
	read_unlock(&tasklist_lock);
	if (!child)
		goto out;

	res = -EPERM;
	if (pid == 1)		/* you may not mess with init */
		goto out;

	if (request == PTRACE_ATTACH) {
		res = ptrace_attach(child);
		goto out_tsk;
	}
	res = -ESRCH;
	if (!(child->ptrace & PT_PTRACED))
		goto out_tsk;
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL)
			goto out_tsk;
	}
	if (child->p_pptr != current)
		goto out_tsk;
	switch (request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		res = -EIO;
		if (copied != sizeof(tmp))
			break;
		res = put_user(tmp,(unsigned long *) data);

		goto out;
		}

	/* Read the word at location addr in the USER area.  */
	case PTRACE_PEEKUSR: {
		struct pt_regs *regs;
		unsigned long tmp;

		regs = (struct pt_regs *) ((unsigned long) child +
		       KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));
		tmp = 0;  /* Default return value. */

		switch(addr) {
		case 0 ... 31:
			tmp = regs->regs[addr];
			break;
		case FPR_BASE ... FPR_BASE + 31:
			if (child->used_math) {
			        unsigned long long *fregs
					= (unsigned long long *)
					    &child->thread.fpu.hard.fp_regs[0];
#ifdef CONFIG_MIPS_FPU_EMULATOR
			    if(!(mips_cpu.options & MIPS_CPU_FPU)) {
				    fregs = (unsigned long long *)
					&child->thread.fpu.soft.regs[0];
			    } else 
#endif
				if (last_task_used_math == child) {
					enable_cp1();
					save_fp(child);
					disable_cp1();
					last_task_used_math = NULL;
				}
				tmp = (unsigned long) fregs[(addr - 32)];
			} else {
				tmp = -1;	/* FP not yet used  */
			}
			break;
		case PC:
			tmp = regs->cp0_epc;
			break;
		case CAUSE:
			tmp = regs->cp0_cause;
			break;
		case BADVADDR:
			tmp = regs->cp0_badvaddr;
			break;
		case MMHI:
			tmp = regs->hi;
			break;
		case MMLO:
			tmp = regs->lo;
			break;
		case FPC_CSR:
#ifdef CONFIG_MIPS_FPU_EMULATOR
			if(!(mips_cpu.options & MIPS_CPU_FPU))
				tmp = child->thread.fpu.soft.sr;
			else
#endif
			tmp = child->thread.fpu.hard.control;
			break;
		case FPC_EIR: {	/* implementation / version register */
			unsigned int flags;

			__save_flags(flags);
			enable_cp1();
			__asm__ __volatile__("cfc1\t%0,$0": "=r" (tmp));
			__restore_flags(flags);
			break;
		}
		default:
			tmp = 0;
			res = -EIO;
			goto out;
		}
		res = put_user(tmp, (unsigned long *) data);
		goto out;
		}

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		res = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1)
		    == sizeof(data))
			break;
		res = -EIO;
		goto out;

	case PTRACE_POKEUSR: {
		struct pt_regs *regs;
		res = 0;
		regs = (struct pt_regs *) ((unsigned long) child +
		       KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));

		switch (addr) {
		case 0 ... 31:
			regs->regs[addr] = data;
			break;
		case FPR_BASE ... FPR_BASE + 31: {
			unsigned long long *fregs;
			fregs = (unsigned long long *)&child->thread.fpu.hard.fp_regs[0];
			if (child->used_math) {
				if (last_task_used_math == child)
#ifdef CONFIG_MIPS_FPU_EMULATOR
				    if(!(mips_cpu.options & MIPS_CPU_FPU)) {
					fregs = (unsigned long long *)
					    &child->thread.fpu.soft.regs[0];
				    } else
#endif
				{
					enable_cp1();
					save_fp(child);
					disable_cp1();
					last_task_used_math = NULL;
					regs->cp0_status &= ~ST0_CU1;
				}
			} else {
				/* FP not yet used  */
				memset(&child->thread.fpu.hard, ~0,
				       sizeof(child->thread.fpu.hard));
				child->thread.fpu.hard.control = 0;
			}
			fregs[addr - FPR_BASE] = data;
			break;
		}
		case PC:
			regs->cp0_epc = data;
			break;
		case MMHI:
			regs->hi = data;
			break;
		case MMLO:
			regs->lo = data;
			break;
		case FPC_CSR:
#ifdef CONFIG_MIPS_FPU_EMULATOR
			if(!(mips_cpu.options & MIPS_CPU_FPU)) 
				child->thread.fpu.soft.sr = data;
			else
#endif
			child->thread.fpu.hard.control = data;
			break;
		default:
			/* The rest are not allowed. */
			res = -EIO;
			break;
		}
		break;
		}

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* restart after signal. */
		res = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		if (request == PTRACE_SYSCALL)
			child->ptrace |= PT_TRACESYS;
		else
			child->ptrace &= ~PT_TRACESYS;
		child->exit_code = data;
		wake_up_process(child);
		res = 0;
		break;
		}

	/*
	 * make the child exit.  Best I can do is send it a sigkill. 
	 * perhaps it should be put in the status that it wants to 
	 * exit.
	 */
	case PTRACE_KILL:
		res = 0;
		if (child->state == TASK_ZOMBIE)	/* already dead */
			break;
		child->exit_code = SIGKILL;
		wake_up_process(child);
		break;

	case PTRACE_DETACH: /* detach a process that was attached. */
		res = -EIO;
		if ((unsigned long) data > _NSIG)
			break;
		child->ptrace = 0;
		child->exit_code = data;
		write_lock_irq(&tasklist_lock);
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		write_unlock_irq(&tasklist_lock);
		wake_up_process(child);
		res = 0;
		break;

	case PTRACE_SETOPTIONS:
		if (data & PTRACE_O_TRACESYSGOOD)
			child->ptrace |= PT_TRACESYSGOOD;
		else
			child->ptrace &= ~PT_TRACESYSGOOD;
		res = 0;
		break;

	default:
		res = -EIO;
		goto out;
	}
out_tsk:
	free_task_struct(child);
out:
	unlock_kernel();
	return res;
}

asmlinkage void syscall_trace(void)
{
	if ((current->ptrace & (PT_PTRACED|PT_TRACESYS))
			!= (PT_PTRACED|PT_TRACESYS))
		return;
	/* The 0x80 provides a way for the tracing parent to distinguish
	   between a syscall stop and SIGTRAP delivery */
	current->exit_code = SIGTRAP | ((current->ptrace & PT_TRACESYSGOOD)
	                                ? 0x80 : 0);
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
