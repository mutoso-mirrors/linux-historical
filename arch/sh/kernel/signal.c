/* $Id: signal.c,v 1.19 2003/10/13 07:21:19 lethal Exp $
 *
 *  linux/arch/sh/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *
 *  SuperH version:  Copyright (C) 1999, 2000  Niibe Yutaka & Kaz Kojima
 *
 */

#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/stddef.h>
#include <linux/tty.h>
#include <linux/personality.h>
#include <linux/binfmts.h>
#include <linux/suspend.h>

#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/cacheflush.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage int do_signal(struct pt_regs *regs, sigset_t *oldset);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int
sys_sigsuspend(old_sigset_t mask,
	       unsigned long r5, unsigned long r6, unsigned long r7,
	       struct pt_regs regs)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs.regs[0] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int
sys_rt_sigsuspend(sigset_t *unewset, size_t sigsetsize,
		  unsigned long r6, unsigned long r7,
		  struct pt_regs regs)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs.regs[0] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int 
sys_sigaction(int sig, const struct old_sigaction __user *act,
	      struct old_sigaction __user *oact)
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

asmlinkage int
sys_sigaltstack(const stack_t __user *uss, stack_t __user *uoss,
		unsigned long r6, unsigned long r7,
		struct pt_regs regs)
{
	return do_sigaltstack(uss, uoss, regs.regs[15]);
}


/*
 * Do a signal return; undo the signal stack.
 */

#define MOVW(n)	 (0x9300|((n)-2))	/* Move mem word at PC+n to R3 */
#define TRAP16	 0xc310			/* Syscall w/no args (NR in R3) */
#define OR_R0_R0 0x200b			/* or r0,r0 (insert to avoid hardware bug) */

struct sigframe
{
	struct sigcontext sc;
	unsigned long extramask[_NSIG_WORDS-1];
	u16 retcode[8];
};

struct rt_sigframe
{
	struct siginfo info;
	struct ucontext uc;
	u16 retcode[8];
};

#ifdef CONFIG_CPU_SH4
static inline int restore_sigcontext_fpu(struct sigcontext __user *sc)
{
	struct task_struct *tsk = current;

	if (!(cpu_data->flags & CPU_HAS_FPU))
		return 0;

	tsk->used_math = 1;
	return __copy_from_user(&tsk->thread.fpu.hard, &sc->sc_fpregs[0],
				sizeof(long)*(16*2+2));
}

static inline int save_sigcontext_fpu(struct sigcontext __user *sc)
{
	struct task_struct *tsk = current;

	if (!(cpu_data->flags & CPU_HAS_FPU))
		return 0;

	if (!tsk->used_math) {
		__put_user(0, &sc->sc_ownedfp);
		return 0;
	}

	__put_user(1, &sc->sc_ownedfp);

	/* This will cause a "finit" to be triggered by the next
	   attempted FPU operation by the 'current' process.
	   */
	tsk->used_math = 0;

	unlazy_fpu(tsk);
	return __copy_to_user(&sc->sc_fpregs[0], &tsk->thread.fpu.hard,
			      sizeof(long)*(16*2+2));
}
#endif /* CONFIG_CPU_SH4 */

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext __user *sc, int *r0_p)
{
	unsigned int err = 0;

#define COPY(x)		err |= __get_user(regs->x, &sc->sc_##x)
			COPY(regs[1]);
	COPY(regs[2]);	COPY(regs[3]);
	COPY(regs[4]);	COPY(regs[5]);
	COPY(regs[6]);	COPY(regs[7]);
	COPY(regs[8]);	COPY(regs[9]);
	COPY(regs[10]);	COPY(regs[11]);
	COPY(regs[12]);	COPY(regs[13]);
	COPY(regs[14]);	COPY(regs[15]);
	COPY(gbr);	COPY(mach);
	COPY(macl);	COPY(pr);
	COPY(sr);	COPY(pc);
#undef COPY

#ifdef CONFIG_CPU_SH4
	if (cpu_data->flags & CPU_HAS_FPU) {
		int owned_fp;
		struct task_struct *tsk = current;

		regs->sr |= SR_FD; /* Release FPU */
		clear_fpu(tsk);
		tsk->used_math = 0;
		__get_user (owned_fp, &sc->sc_ownedfp);
		if (owned_fp)
			err |= restore_sigcontext_fpu(sc);
	}
#endif

	regs->tra = -1;		/* disable syscall checks */
	err |= __get_user(*r0_p, &sc->sc_regs[0]);
	return err;
}

asmlinkage int sys_sigreturn(unsigned long r4, unsigned long r5,
			     unsigned long r6, unsigned long r7,
			     struct pt_regs regs)
{
	struct sigframe __user *frame = (struct sigframe __user *)regs.regs[15];
	sigset_t set;
	int r0;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || (_NSIG_WORDS > 1
		&& __copy_from_user(&set.sig[1], &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);

	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(&regs, &frame->sc, &r0))
		goto badframe;
	return r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage int sys_rt_sigreturn(unsigned long r4, unsigned long r5,
				unsigned long r6, unsigned long r7,
				struct pt_regs regs)
{
	struct rt_sigframe __user *frame = (struct rt_sigframe __user *)regs.regs[15];
	sigset_t set;
	stack_t st;
	int r0;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;

	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigcontext(&regs, &frame->uc.uc_mcontext, &r0))
		goto badframe;

	if (__copy_from_user(&st, &frame->uc.uc_stack, sizeof(st)))
		goto badframe;
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, regs.regs[15]);

	return r0;

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}	

/*
 * Set up a signal frame.
 */

static int
setup_sigcontext(struct sigcontext __user *sc, struct pt_regs *regs,
		 unsigned long mask)
{
	int err = 0;

#define COPY(x)		err |= __put_user(regs->x, &sc->sc_##x)
	COPY(regs[0]);	COPY(regs[1]);
	COPY(regs[2]);	COPY(regs[3]);
	COPY(regs[4]);	COPY(regs[5]);
	COPY(regs[6]);	COPY(regs[7]);
	COPY(regs[8]);	COPY(regs[9]);
	COPY(regs[10]);	COPY(regs[11]);
	COPY(regs[12]);	COPY(regs[13]);
	COPY(regs[14]);	COPY(regs[15]);
	COPY(gbr);	COPY(mach);
	COPY(macl);	COPY(pr);
	COPY(sr);	COPY(pc);
#undef COPY

#ifdef CONFIG_CPU_SH4
	err |= save_sigcontext_fpu(sc);
#endif

	/* non-iBCS2 extensions.. */
	err |= __put_user(mask, &sc->oldmask);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void __user *
get_sigframe(struct k_sigaction *ka, unsigned long sp, size_t frame_size)
{
	if ((ka->sa.sa_flags & SA_ONSTACK) != 0 && ! on_sig_stack(sp))
		sp = current->sas_ss_sp + current->sas_ss_size;

	return (void __user *)((sp - frame_size) & -8ul);
}

static void setup_frame(int sig, struct k_sigaction *ka,
			sigset_t *set, struct pt_regs *regs)
{
	struct sigframe __user *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs->regs[15], sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err |= setup_sigcontext(&frame->sc, regs, set->sig[0]);

	if (_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		regs->pr = (unsigned long) ka->sa.sa_restorer;
	} else {
		/* Generate return code (system call to sigreturn) */
		err |= __put_user(MOVW(7), &frame->retcode[0]);
		err |= __put_user(TRAP16, &frame->retcode[1]);
		err |= __put_user(OR_R0_R0, &frame->retcode[2]);
		err |= __put_user(OR_R0_R0, &frame->retcode[3]);
		err |= __put_user(OR_R0_R0, &frame->retcode[4]);
		err |= __put_user(OR_R0_R0, &frame->retcode[5]);
		err |= __put_user(OR_R0_R0, &frame->retcode[6]);
		err |= __put_user((__NR_sigreturn), &frame->retcode[7]);
		regs->pr = (unsigned long) frame->retcode;
	}

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->regs[15] = (unsigned long) frame;
	regs->regs[4] = signal; /* Arg for signal handler */
	regs->regs[5] = 0;
	regs->regs[6] = (unsigned long) &frame->sc;
	regs->pc = (unsigned long) ka->sa.sa_handler;

	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%08lx pr=%08lx\n",
		current->comm, current->pid, frame, regs->pc, regs->pr);
#endif

	flush_cache_sigtramp(regs->pr);
	if ((-regs->pr & (L1_CACHE_BYTES-1)) < sizeof(frame->retcode))
		flush_cache_sigtramp(regs->pr + L1_CACHE_BYTES);
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs *regs)
{
	struct rt_sigframe __user *frame;
	int err = 0;
	int signal;

	frame = get_sigframe(ka, regs->regs[15], sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	signal = current_thread_info()->exec_domain
		&& current_thread_info()->exec_domain->signal_invmap
		&& sig < 32
		? current_thread_info()->exec_domain->signal_invmap[sig]
		: sig;

	err |= copy_siginfo_to_user(&frame->info, info);

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user((void *)current->sas_ss_sp,
			  &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->regs[15]),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext,
			        regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		regs->pr = (unsigned long) ka->sa.sa_restorer;
	} else {
		/* Generate return code (system call to rt_sigreturn) */
		err |= __put_user(MOVW(7), &frame->retcode[0]);
		err |= __put_user(TRAP16, &frame->retcode[1]);
		err |= __put_user(OR_R0_R0, &frame->retcode[2]);
		err |= __put_user(OR_R0_R0, &frame->retcode[3]);
		err |= __put_user(OR_R0_R0, &frame->retcode[4]);
		err |= __put_user(OR_R0_R0, &frame->retcode[5]);
		err |= __put_user(OR_R0_R0, &frame->retcode[6]);
		err |= __put_user((__NR_rt_sigreturn), &frame->retcode[7]);
		regs->pr = (unsigned long) frame->retcode;
	}

	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->regs[15] = (unsigned long) frame;
	regs->regs[4] = signal; /* Arg for signal handler */
	regs->regs[5] = (unsigned long) &frame->info;
	regs->regs[6] = (unsigned long) &frame->uc;
	regs->pc = (unsigned long) ka->sa.sa_handler;

	set_fs(USER_DS);

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%08lx pr=%08lx\n",
		current->comm, current->pid, frame, regs->pc, regs->pr);
#endif

	flush_cache_sigtramp(regs->pr);
	if ((-regs->pr & (L1_CACHE_BYTES-1)) < sizeof(frame->retcode))
		flush_cache_sigtramp(regs->pr + L1_CACHE_BYTES);
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

/*
 * OK, we're invoking a handler
 */	

static void
handle_signal(unsigned long sig, siginfo_t *info, sigset_t *oldset,
	struct pt_regs * regs)
{
	struct k_sigaction *ka = &current->sighand->action[sig-1];

	/* Are we from a system call? */
	if (regs->tra >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->regs[0]) {
			case -ERESTARTNOHAND:
				regs->regs[0] = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->regs[0] = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->pc -= 2;
		}
#ifndef CONFIG_PREEMPT
	} else {
		/* gUSA handling */
		if (regs->regs[15] >= 0xc0000000) {
			int offset = (int)regs->regs[15];

			/* Reset stack pointer: clear critical region mark */
			regs->regs[15] = regs->regs[1];
			if (regs->pc < regs->regs[0])
				/* Go to rewind point #1 */
				regs->pc = regs->regs[0] + offset - 2;
		}
#endif
	}

	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame(sig, ka, info, oldset, regs);
	else
		setup_frame(sig, ka, oldset, regs);

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sighand->siglock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sighand->siglock);
	}
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
int do_signal(struct pt_regs *regs, sigset_t *oldset)
{
	siginfo_t info;
	int signr;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if (!user_mode(regs))
		return 1;

	if (current->flags & PF_FREEZE) {
		refrigerator(0);
		goto no_signal;
	}

	if (!oldset)
		oldset = &current->blocked;

	signr = get_signal_to_deliver(&info, regs, NULL);
	if (signr > 0) {
		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, &info, oldset, regs);
		return 1;
	}

 no_signal:
	/* Did we come from a system call? */
	if (regs->tra >= 0) {
		/* Restart the system call - no handlers present */
		if (regs->regs[0] == -ERESTARTNOHAND ||
		    regs->regs[0] == -ERESTARTSYS ||
		    regs->regs[0] == -ERESTARTNOINTR ||
		    regs->regs[0] == -ERESTART_RESTARTBLOCK) {
			regs->pc -= 2;
		}
	}
	return 0;
}
