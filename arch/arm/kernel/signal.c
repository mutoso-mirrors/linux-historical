/*
 *  linux/arch/arm/kernel/signal.c
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/personality.h>
#include <linux/tty.h>
#include <linux/binfmts.h>
#include <linux/elf.h>

#include <asm/pgalloc.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>

#include "ptrace.h"

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

/*
 * For ARM syscalls, we encode the syscall number into the instruction.
 */
#define SWI_SYS_SIGRETURN	(0xef000000|(__NR_sigreturn))
#define SWI_SYS_RT_SIGRETURN	(0xef000000|(__NR_rt_sigreturn))

/*
 * For Thumb syscalls, we pass the syscall number via r7.  We therefore
 * need two 16-bit instructions.
 */
#define SWI_THUMB_SIGRETURN	(0xdf00 << 16 | 0x2700 | (__NR_sigreturn - __NR_SYSCALL_BASE))
#define SWI_THUMB_RT_SIGRETURN	(0xdf00 << 16 | 0x2700 | (__NR_rt_sigreturn - __NR_SYSCALL_BASE))

static const unsigned long retcodes[4] = {
	SWI_SYS_SIGRETURN,	SWI_THUMB_SIGRETURN,
	SWI_SYS_RT_SIGRETURN,	SWI_THUMB_RT_SIGRETURN
};

static int do_signal(sigset_t *oldset, struct pt_regs * regs, int syscall);

/*
 * atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int sys_sigsuspend(int restart, unsigned long oldmask, old_sigset_t mask, struct pt_regs *regs)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sig->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sig->siglock);
	regs->ARM_r0 = -EINTR;

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs, 0))
			return regs->ARM_r0;
	}
}

asmlinkage int
sys_rt_sigsuspend(sigset_t *unewset, size_t sigsetsize, struct pt_regs *regs)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's. */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sig->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sig->siglock);
	regs->ARM_r0 = -EINTR;

	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs, 0))
			return regs->ARM_r0;
	}
}

asmlinkage int 
sys_sigaction(int sig, const struct old_sigaction *act,
	      struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

	if (act) {
		old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user(new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user(new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user(old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user(old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
	}

	return ret;
}

/*
 * Do a signal return; undo the signal stack.
 */
struct sigframe
{
	struct sigcontext sc;
	unsigned long extramask[_NSIG_WORDS-1];
	unsigned long retcode;
};

struct rt_sigframe
{
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
	unsigned long retcode;
};

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *sc)
{
	int err = 0;

	__get_user_error(regs->ARM_r0, &sc->arm_r0, err);
	__get_user_error(regs->ARM_r1, &sc->arm_r1, err);
	__get_user_error(regs->ARM_r2, &sc->arm_r2, err);
	__get_user_error(regs->ARM_r3, &sc->arm_r3, err);
	__get_user_error(regs->ARM_r4, &sc->arm_r4, err);
	__get_user_error(regs->ARM_r5, &sc->arm_r5, err);
	__get_user_error(regs->ARM_r6, &sc->arm_r6, err);
	__get_user_error(regs->ARM_r7, &sc->arm_r7, err);
	__get_user_error(regs->ARM_r8, &sc->arm_r8, err);
	__get_user_error(regs->ARM_r9, &sc->arm_r9, err);
	__get_user_error(regs->ARM_r10, &sc->arm_r10, err);
	__get_user_error(regs->ARM_fp, &sc->arm_fp, err);
	__get_user_error(regs->ARM_ip, &sc->arm_ip, err);
	__get_user_error(regs->ARM_sp, &sc->arm_sp, err);
	__get_user_error(regs->ARM_lr, &sc->arm_lr, err);
	__get_user_error(regs->ARM_pc, &sc->arm_pc, err);
#ifdef CONFIG_CPU_32
	__get_user_error(regs->ARM_cpsr, &sc->arm_cpsr, err);
#endif

	err |= !valid_user_regs(regs);

	return err;
}

asmlinkage int sys_sigreturn(struct pt_regs *regs)
{
	struct sigframe *frame;
	sigset_t set;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->ARM_sp & 7)
		goto badframe;

	frame = (struct sigframe *)regs->ARM_sp;

	if (verify_area(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || (_NSIG_WORDS > 1
	        && __copy_from_user(&set.sig[1], &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sig->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sig->siglock);

	if (restore_sigcontext(regs, &frame->sc))
		goto badframe;

	/* Send SIGTRAP if we're single-stepping */
	if (ptrace_cancel_bpt(current))
		send_sig(SIGTRAP, current, 1);

	return regs->ARM_r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(struct pt_regs *regs)
{
	struct rt_sigframe *frame;
	sigset_t set;

	/*
	 * Since we stacked the signal on a 64-bit boundary,
	 * then 'sp' should be word aligned here.  If it's
	 * not, then the user is trying to mess with us.
	 */
	if (regs->ARM_sp & 7)
		goto badframe;

	frame = (struct rt_sigframe *)regs->ARM_sp;

	if (verify_area(VERIFY_READ, frame, sizeof (*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sig->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sig->siglock);

	if (restore_sigcontext(regs, &frame->uc.uc_mcontext))
		goto badframe;

	/* Send SIGTRAP if we're single-stepping */
	if (ptrace_cancel_bpt(current))
		send_sig(SIGTRAP, current, 1);

	return regs->ARM_r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

static int
setup_sigcontext(struct sigcontext *sc, /*struct _fpstate *fpstate,*/
		 struct pt_regs *regs, unsigned long mask)
{
	int err = 0;

	__put_user_error(regs->ARM_r0, &sc->arm_r0, err);
	__put_user_error(regs->ARM_r1, &sc->arm_r1, err);
	__put_user_error(regs->ARM_r2, &sc->arm_r2, err);
	__put_user_error(regs->ARM_r3, &sc->arm_r3, err);
	__put_user_error(regs->ARM_r4, &sc->arm_r4, err);
	__put_user_error(regs->ARM_r5, &sc->arm_r5, err);
	__put_user_error(regs->ARM_r6, &sc->arm_r6, err);
	__put_user_error(regs->ARM_r7, &sc->arm_r7, err);
	__put_user_error(regs->ARM_r8, &sc->arm_r8, err);
	__put_user_error(regs->ARM_r9, &sc->arm_r9, err);
	__put_user_error(regs->ARM_r10, &sc->arm_r10, err);
	__put_user_error(regs->ARM_fp, &sc->arm_fp, err);
	__put_user_error(regs->ARM_ip, &sc->arm_ip, err);
	__put_user_error(regs->ARM_sp, &sc->arm_sp, err);
	__put_user_error(regs->ARM_lr, &sc->arm_lr, err);
	__put_user_error(regs->ARM_pc, &sc->arm_pc, err);
#ifdef CONFIG_CPU_32
	__put_user_error(regs->ARM_cpsr, &sc->arm_cpsr, err);
#endif

	__put_user_error(current->thread.trap_no, &sc->trap_no, err);
	__put_user_error(current->thread.error_code, &sc->error_code, err);
	__put_user_error(current->thread.address, &sc->fault_address, err);
	__put_user_error(mask, &sc->oldmask, err);

	return err;
}

static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs *regs, int framesize)
{
	unsigned long sp = regs->ARM_sp;

	/*
	 * This is the X/Open sanctioned signal stack switching.
	 */
	if ((ka->sa.sa_flags & SA_ONSTACK) && !sas_ss_flags(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	/*
	 * ATPCS B01 mandates 8-byte alignment
	 */
	return (void *)((sp - framesize) & ~7);
}

static int
setup_return(struct pt_regs *regs, struct k_sigaction *ka,
	     unsigned long *rc, void *frame, int usig)
{
	unsigned long handler = (unsigned long)ka->sa.sa_handler;
	unsigned long retcode;
	int thumb = 0;
#ifdef CONFIG_CPU_32
	unsigned long cpsr = regs->ARM_cpsr & ~PSR_f;

	/*
	 * Maybe we need to deliver a 32-bit signal to a 26-bit task.
	 */
	if (ka->sa.sa_flags & SA_THIRTYTWO)
		cpsr = (cpsr & ~MODE_MASK) | USR_MODE;

#ifdef CONFIG_ARM_THUMB
	if (elf_hwcap & HWCAP_THUMB) {
		/*
		 * The LSB of the handler determines if we're going to
		 * be using THUMB or ARM mode for this signal handler.
		 */
		thumb = handler & 1;

		if (thumb)
			cpsr |= PSR_T_BIT;
		else
			cpsr &= ~PSR_T_BIT;
	}
#endif
#endif

	if (ka->sa.sa_flags & SA_RESTORER) {
		retcode = (unsigned long)ka->sa.sa_restorer;
	} else {
		unsigned int idx = thumb;

		if (ka->sa.sa_flags & SA_SIGINFO)
			idx += 2;

		if (__put_user(retcodes[idx], rc))
			return 1;

		/*
		 * Ensure that the instruction cache sees
		 * the return code written onto the stack.
		 */
		cpu_icache_invalidate_range((unsigned long)rc,
					    (unsigned long)(rc + 1));

		retcode = ((unsigned long)rc) + thumb;
	}

	regs->ARM_r0 = usig;
	regs->ARM_sp = (unsigned long)frame;
	regs->ARM_lr = retcode;
	regs->ARM_pc = handler;

#ifdef CONFIG_CPU_32
	regs->ARM_cpsr = cpsr;
#endif

	return 0;
}

static int
setup_frame(int usig, struct k_sigaction *ka, sigset_t *set, struct pt_regs *regs)
{
	struct sigframe *frame = get_sigframe(ka, regs, sizeof(*frame));
	int err = 0;

	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		return 1;

	err |= setup_sigcontext(&frame->sc, /*&frame->fpstate,*/ regs, set->sig[0]);

	if (_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}

	if (err == 0)
		err = setup_return(regs, ka, &frame->retcode, frame, usig);

	return err;
}

static int
setup_rt_frame(int usig, struct k_sigaction *ka, siginfo_t *info,
	       sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe *frame = get_sigframe(ka, regs, sizeof(*frame));
	int err = 0;

	if (!access_ok(VERIFY_WRITE, frame, sizeof (*frame)))
		return 1;

	__put_user_error(&frame->info, &frame->pinfo, err);
	__put_user_error(&frame->uc, &frame->puc, err);
	err |= copy_siginfo_to_user(&frame->info, info);

	/* Clear all the bits of the ucontext we don't use.  */
	err |= __clear_user(&frame->uc, offsetof(struct ucontext, uc_mcontext));

	err |= setup_sigcontext(&frame->uc.uc_mcontext, /*&frame->fpstate,*/
				regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	if (err == 0)
		err = setup_return(regs, ka, &frame->retcode, frame, usig);

	if (err == 0) {
		/*
		 * For realtime signals we must also set the second and third
		 * arguments for the signal handler.
		 *   -- Peter Maydell <pmaydell@chiark.greenend.org.uk> 2000-12-06
		 */
		regs->ARM_r1 = (unsigned long)frame->pinfo;
		regs->ARM_r2 = (unsigned long)frame->puc;
	}

	return err;
}

/*
 * OK, we're invoking a handler
 */	
static void
handle_signal(unsigned long sig, struct k_sigaction *ka,
	      siginfo_t *info, sigset_t *oldset, struct pt_regs * regs)
{
	struct thread_info *thread = current_thread_info();
	struct task_struct *tsk = current;
	int usig = sig;
	int ret;

	/*
	 * translate the signal
	 */
	if (usig < 32 && thread->exec_domain && thread->exec_domain->signal_invmap)
		usig = thread->exec_domain->signal_invmap[usig];

	/*
	 * Set up the stack frame
	 */
	if (ka->sa.sa_flags & SA_SIGINFO)
		ret = setup_rt_frame(usig, ka, info, oldset, regs);
	else
		ret = setup_frame(usig, ka, oldset, regs);

	/*
	 * Check that the resulting registers are actually sane.
	 */
	ret |= !valid_user_regs(regs);

	if (ret == 0) {
		if (ka->sa.sa_flags & SA_ONESHOT)
			ka->sa.sa_handler = SIG_DFL;

		if (!(ka->sa.sa_flags & SA_NODEFER)) {
			spin_lock_irq(&tsk->sig->siglock);
			sigorsets(&tsk->blocked, &tsk->blocked,
				  &ka->sa.sa_mask);
			sigaddset(&tsk->blocked, sig);
			recalc_sigpending();
			spin_unlock_irq(&tsk->sig->siglock);
		}
		return;
	}

	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, tsk);
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 *
 * Note that we go through the signals twice: once to check the signals that
 * the kernel can handle, and then we build all the user-level signal handling
 * stack-frames in one go after that.
 */
static int do_signal(sigset_t *oldset, struct pt_regs *regs, int syscall)
{
	siginfo_t info;
	int single_stepping;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 0;

	single_stepping = ptrace_cancel_bpt(current);

	for (;;) {
		unsigned long signr = 0;
		struct k_sigaction *ka;

		spin_lock_irq(&current->sig->siglock);
		signr = dequeue_signal(&current->blocked, &info);
		spin_unlock_irq(&current->sig->siglock);

		if (!signr)
			break;

		if ((current->ptrace & PT_PTRACED) && signr != SIGKILL) {
			/* Let the debugger run.  */
			current->exit_code = signr;
			set_current_state(TASK_STOPPED);
			notify_parent(current, SIGCHLD);
			schedule();
			single_stepping |= ptrace_cancel_bpt(current);

			/* We're back.  Did the debugger cancel the sig?  */
			signr = current->exit_code;
			if (signr == 0)
				continue;
			current->exit_code = 0;

			/* The debugger continued.  Ignore SIGSTOP.  */
			if (signr == SIGSTOP)
				continue;

			/* Update the siginfo structure.  Is this good? */
			if (signr != info.si_signo) {
				info.si_signo = signr;
				info.si_errno = 0;
				info.si_code = SI_USER;
				info.si_pid = current->parent->pid;
				info.si_uid = current->parent->uid;
			}

			/* If the (new) signal is now blocked, requeue it.  */
			if (sigismember(&current->blocked, signr)) {
				send_sig_info(signr, &info, current);
				continue;
			}
		}

		ka = &current->sig->action[signr-1];
		if (ka->sa.sa_handler == SIG_IGN) {
			if (signr != SIGCHLD)
				continue;
			/* Check for SIGCHLD: it's special.  */
			while (sys_wait4(-1, NULL, WNOHANG, NULL) > 0)
				/* nothing */;
			continue;
		}

		if (ka->sa.sa_handler == SIG_DFL) {
			int exit_code = signr;

			/* Init gets no signals it doesn't want.  */
			if (current->pid == 1)
				continue;

			switch (signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH: case SIGURG:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					continue;
				/* FALLTHRU */

			case SIGSTOP: {
				struct signal_struct *sig;
				set_current_state(TASK_STOPPED);
				current->exit_code = signr;
				sig = current->parent->sig;
				if (sig && !(sig->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDSTOP))
					notify_parent(current, SIGCHLD);
				schedule();
				single_stepping |= ptrace_cancel_bpt(current);
				continue;
			}

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
			case SIGBUS: case SIGSYS: case SIGXCPU: case SIGXFSZ:
				if (do_coredump(signr, regs))
					exit_code |= 0x80;
				/* FALLTHRU */

			default:
				sig_exit(signr, exit_code, &info);
				/* NOTREACHED */
			}
		}

		/* Are we from a system call? */
		if (syscall) {
			switch (regs->ARM_r0) {
			case -ERESTARTNOHAND:
				regs->ARM_r0 = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->ARM_r0 = -EINTR;
					break;
				}
				/* fallthrough */
			case -ERESTARTNOINTR:
				regs->ARM_r0 = regs->ARM_ORIG_r0;
				regs->ARM_pc -= 4;
			}
		}
		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, ka, &info, oldset, regs);
		if (single_stepping)
		    	ptrace_set_bpt(current);
		return 1;
	}

	if (syscall &&
	    (regs->ARM_r0 == -ERESTARTNOHAND ||
	     regs->ARM_r0 == -ERESTARTSYS ||
	     regs->ARM_r0 == -ERESTARTNOINTR)) {
		regs->ARM_r0 = regs->ARM_ORIG_r0;
		regs->ARM_pc -= 4;
	}
	if (single_stepping)
		ptrace_set_bpt(current);
	return 0;
}

asmlinkage void
do_notify_resume(struct pt_regs *regs, unsigned int thread_flags, int syscall)
{
	if (thread_flags & _TIF_SIGPENDING)
		do_signal(&current->blocked, regs, syscall);
}
