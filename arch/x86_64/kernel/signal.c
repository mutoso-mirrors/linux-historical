/*
 *  linux/arch/x86_64/kernel/signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 2000, 2001, 2002 Andi Kleen SuSE Labs
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 *  2000-2002   x86-64 support by Andi Kleen
 * 
 *  $Id: signal.c,v 1.18 2001/10/17 22:30:37 ak Exp $
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
#include <linux/compiler.h>
#include <linux/binfmts.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/i387.h>

/* #define DEBUG_SIG 1 */

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

void ia32_setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
               sigset_t *set, struct pt_regs * regs); 
void ia32_setup_frame(int sig, struct k_sigaction *ka,
            sigset_t *set, struct pt_regs * regs); 

asmlinkage long
sys_rt_sigsuspend(sigset_t *unewset, size_t sigsetsize, struct pt_regs regs)
{
	sigset_t saveset, newset;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&newset, unewset, sizeof(newset)))
		return -EFAULT;
	sigdelsetmask(&newset, ~_BLOCKABLE);

	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	current->blocked = newset;
	recalc_sigpending();
	spin_unlock_irq(&current->sigmask_lock);
#if DEBUG_SIG
	printk("rt_sigsuspend savset(%lx) newset(%lx) regs(%p) rip(%lx)\n",
		saveset, newset, &regs, regs.rip);
#endif 
	regs.rax = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&regs, &saveset))
			return -EINTR;
	}
}

asmlinkage long
sys_sigaltstack(const stack_t *uss, stack_t *uoss, struct pt_regs regs)
{
	return do_sigaltstack(uss, uoss, regs.rsp);
}


/*
 * Do a signal return; undo the signal stack.
 */

struct rt_sigframe
{
	char *pretcode;
	struct ucontext uc;
	struct siginfo info;
	struct _fpstate fpstate __attribute__((aligned(8)));
};

static int
restore_sigcontext(struct pt_regs *regs, struct sigcontext *sc, unsigned long *prax)
{
	unsigned int err = 0;


#define COPY(x)		err |= __get_user(regs->x, &sc->x)

	{ 
		unsigned int seg; 
		err |= __get_user(seg, &sc->gs); 
		load_gs_index(seg); 
		err |= __get_user(seg, &sc->fs);
		loadsegment(fs,seg);
	}

	COPY(rdi); COPY(rsi); COPY(rbp); COPY(rsp); COPY(rbx);
	COPY(rdx); COPY(rcx); COPY(rip);
	COPY(r8);
	COPY(r9);
	COPY(r10);
	COPY(r11);
	COPY(r12);
	COPY(r13);
	COPY(r14);
	COPY(r15);

	{
		unsigned int tmpflags;
		err |= __get_user(tmpflags, &sc->eflags);
		regs->eflags = (regs->eflags & ~0x40DD5) | (tmpflags & 0x40DD5);
		regs->orig_rax = -1;		/* disable syscall checks */
	}

	{
		struct _fpstate * buf;
		err |= __get_user(buf, &sc->fpstate);
		if (buf) {
			if (verify_area(VERIFY_READ, buf, sizeof(*buf)))
				goto badframe;
			err |= restore_i387(buf);
		}
	}

	err |= __get_user(*prax, &sc->rax);
	return err;

badframe:
	return 1;
}

asmlinkage long sys_rt_sigreturn(struct pt_regs regs)
{
	struct rt_sigframe *frame = (struct rt_sigframe *)(regs.rsp - 8);
	sigset_t set;
	stack_t st;
	long eax;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sigmask_lock);
	
	if (restore_sigcontext(&regs, &frame->uc.uc_mcontext, &eax))
		goto badframe;

#if DEBUG_SIG
	printk("%d sigreturn rip:%lx rsp:%lx frame:%p rax:%lx\n",current->pid,regs.rip,regs.rsp,frame,eax);
#endif

	if (__copy_from_user(&st, &frame->uc.uc_stack, sizeof(st)))
		goto badframe;
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, regs.rsp);

	return eax;

badframe:
#if DEBUG_SIG
	printk("%d bad frame %p\n",current->pid,frame);
#endif
	force_sig(SIGSEGV, current);
	return 0;
}	

/*
 * Set up a signal frame.
 */

static int
setup_sigcontext(struct sigcontext *sc, struct _fpstate *fpstate,
		 struct pt_regs *regs, unsigned long mask)
{
	int tmp, err = 0;
	struct task_struct *me = current;

	tmp = 0;
	__asm__("movl %%gs,%0" : "=r"(tmp): "0"(tmp));
	err |= __put_user(tmp, (unsigned int *)&sc->gs);
	__asm__("movl %%fs,%0" : "=r"(tmp): "0"(tmp));
	err |= __put_user(tmp, (unsigned int *)&sc->fs);

	err |= __put_user(regs->rdi, &sc->rdi);
	err |= __put_user(regs->rsi, &sc->rsi);
	err |= __put_user(regs->rbp, &sc->rbp);
	err |= __put_user(regs->rsp, &sc->rsp);
	err |= __put_user(regs->rbx, &sc->rbx);
	err |= __put_user(regs->rdx, &sc->rdx);
	err |= __put_user(regs->rcx, &sc->rcx);
	err |= __put_user(regs->rax, &sc->rax);
	err |= __put_user(regs->r8, &sc->r8);
	err |= __put_user(regs->r9, &sc->r9);
	err |= __put_user(regs->r10, &sc->r10);
	err |= __put_user(regs->r11, &sc->r11);
	err |= __put_user(regs->r12, &sc->r12);
	err |= __put_user(regs->r13, &sc->r13);
	err |= __put_user(regs->r14, &sc->r14);
	err |= __put_user(regs->r15, &sc->r15);
	err |= __put_user(me->thread.trap_no, &sc->trapno);
	err |= __put_user(me->thread.error_code, &sc->err);
	err |= __put_user(regs->rip, &sc->rip);
	err |= __put_user(regs->eflags, &sc->eflags);
	err |= __put_user(mask, &sc->oldmask);
	err |= __put_user(me->thread.cr2, &sc->cr2);

	tmp = save_i387(fpstate);
	if (tmp < 0)
	  err = 1;
	else
	  err |= __put_user(tmp ? fpstate : NULL, &sc->fpstate);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs * regs, size_t frame_size)
{
	unsigned long rsp;

	/* Default to using normal stack - redzone*/
	rsp = regs->rsp - 128;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (! sas_ss_flags(rsp) == 0)
			rsp = current->sas_ss_sp + current->sas_ss_size;
	}

	{ 
		extern void bad_sigframe(void); 
		/* beginning of sigframe is 8 bytes misaligned, but fpstate
		   must end up on a 16byte boundary */ 
		if ((offsetof(struct rt_sigframe, fpstate) & 16) != 0) 
			bad_sigframe(); 		
	} 

	return (void *)((rsp - frame_size) & ~(15UL)) - 8;
}

static void setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs * regs)
{
	struct rt_sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	if (ka->sa.sa_flags & SA_SIGINFO) { 
		err |= copy_siginfo_to_user(&frame->info, info);
		if (err)
			goto give_sigsegv;
	}
		
	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->rsp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= setup_sigcontext(&frame->uc.uc_mcontext, &frame->fpstate,
			        regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;


	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	/* x86-64 should always use SA_RESTORER. */
	if (ka->sa.sa_flags & SA_RESTORER) {
		err |= __put_user(ka->sa.sa_restorer, &frame->pretcode);
	} else {
		printk("%s forgot to set SA_RESTORER for signal %d.\n", current->comm, sig); 
		goto give_sigsegv; 
	}

	if (err)
		goto give_sigsegv;

#if DEBUG_SIG
	printk("%d old rip %lx old rsp %lx old rax %lx\n", current->pid,regs->rip,regs->rsp,regs->rax);
#endif


	/* Set up registers for signal handler */
	{ 
		struct exec_domain *ed = current_thread_info()->exec_domain;
		if (unlikely(ed && ed->signal_invmap && sig < 32))
			sig = ed->signal_invmap[sig];
	} 
	regs->rdi = sig;
	/* In case the signal handler was declared without prototypes */ 
	regs->rax = 0;	

	/* This also works for non SA_SIGINFO handlers because they expect the
	   next argument after the signal number on the stack. */
	regs->rsi = (unsigned long)&frame->info; 
	regs->rdx = (unsigned long)&frame->uc; 
	regs->rsp = (unsigned long) frame;
	regs->rip = (unsigned long) ka->sa.sa_handler;

	set_fs(USER_DS);
	regs->eflags &= ~TF_MASK;

#if DEBUG_SIG
	printk("SIG deliver (%s:%d): sp=%p pc=%p ra=%p\n",
		current->comm, current->pid, frame, regs->rip, frame->pretcode);
#endif

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
handle_signal(unsigned long sig, struct k_sigaction *ka,
	      siginfo_t *info, sigset_t *oldset, struct pt_regs * regs)
{
#if DEBUG_SIG
	printk("handle_signal pid:%d sig:%lu rip:%lx rsp:%lx regs=%p\n", current->pid, sig, 
		regs->rip, regs->rsp, regs);
#endif

	/* Are we from a system call? */
	if (regs->orig_rax >= 0) {
		/* If so, check system call restarting.. */
		switch (regs->rax) {
			case -ERESTARTNOHAND:
				regs->rax = -EINTR;
				break;

			case -ERESTARTSYS:
				if (!(ka->sa.sa_flags & SA_RESTART)) {
					regs->rax = -EINTR;
					break;
				}
			/* fallthrough */
			case -ERESTARTNOINTR:
				regs->rax = regs->orig_rax;
				regs->rip -= 2;
		}
	}

#ifdef CONFIG_IA32_EMULATION
	if (test_thread_flag(TIF_IA32)) {
		if (ka->sa.sa_flags & SA_SIGINFO)
			ia32_setup_rt_frame(sig, ka, info, oldset, regs);
		else
			ia32_setup_frame(sig, ka, oldset, regs);
	} else 
#endif
	setup_rt_frame(sig, ka, info, oldset, regs);

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sigmask_lock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sigmask_lock);
	}
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
int do_signal(struct pt_regs *regs, sigset_t *oldset)
{
	siginfo_t info;
	struct k_sigaction *ka;

	/*
	 * We want the common case to go fast, which
	 * is why we may in certain cases get here from
	 * kernel mode. Just return without doing anything
	 * if so.
	 */
	if ((regs->cs & 3) != 3) {
		return 1;
	} 	

	if (!oldset)
		oldset = &current->blocked;

	for (;;) {
		unsigned long signr;

		spin_lock_irq(&current->sigmask_lock);
		signr = dequeue_signal(&current->blocked, &info);
		spin_unlock_irq(&current->sigmask_lock);

		if (!signr) { 
			break;
		}

		if ((current->ptrace & PT_PTRACED) && signr != SIGKILL) {
			/* Let the debugger run.  */
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current, SIGCHLD);
			schedule();

			/* We're back.  Did the debugger cancel the sig?  */
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;

			/* The debugger continued.  Ignore SIGSTOP.  */
			if (signr == SIGSTOP)
				continue;

			/* Update the siginfo structure.  Is this good?  */
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
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					continue;
				/* FALLTHRU */

			case SIGSTOP: {
				struct signal_struct *sig;
				preempt_disable(); 
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				sig = current->parent->sig;
				if (sig && !(sig->action[SIGCHLD-1].sa.sa_flags & SA_NOCLDSTOP))
					notify_parent(current, SIGCHLD);
				schedule();
				preempt_enable();
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

		/* Reenable any watchpoints before delivering the
		 * signal to user space. The processor register will
		 * have been cleared if the watchpoint triggered
		 * inside the kernel.
		 */
		__asm__("movq %0,%%db7"	: : "r" (current->thread.debugreg[7]));

		/* Whee!  Actually deliver the signal.  */
		handle_signal(signr, ka, &info, oldset, regs);
		return 1;
	}

	/* Did we come from a system call? */
	if (regs->orig_rax >= 0) {
		/* Restart the system call - no handlers present */
		if (regs->rax == -ERESTARTNOHAND ||
		    regs->rax == -ERESTARTSYS ||
		    regs->rax == -ERESTARTNOINTR) {
			regs->rax = regs->orig_rax;
			regs->rip -= 2;
		}
	}
	return 0;
}

void do_notify_resume(struct pt_regs *regs, sigset_t *oldset, __u32 thread_info_flags)
{
#if DEBUG_SIG
	printk("do_notify_resume flags:%x rip:%lx rsp:%lx caller:%lx pending:%lx\n",
	       thread_info_flags, regs->rip, regs->rsp, __builtin_return_address(0),signal_pending(current)); 
#endif
	       

	/* deal with pending signal delivery */
	if (thread_info_flags & _TIF_SIGPENDING)
		do_signal(regs,oldset);
}
