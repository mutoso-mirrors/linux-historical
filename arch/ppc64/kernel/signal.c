/*
 *  linux/arch/ppc64/kernel/signal.c
 *
 *  
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/kernel/signal.c"
 *    Copyright (C) 1991, 1992 Linus Torvalds
 *    1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
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
#include <linux/elf.h>
#include <linux/tty.h>
#include <linux/binfmts.h>
#include <asm/ppc32.h>
#include <asm/sigcontext.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/ppcdebug.h>
#include <asm/unistd.h>
#include <asm/cacheflush.h>

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define GP_REGS_SIZE	MIN(sizeof(elf_gregset_t), sizeof(struct pt_regs))

/* 
 * These are the flags in the MSR that the user is allowed to change
 * by modifying the saved value of the MSR on the stack.  SE and BE
 * should not be in this list since gdb may want to change these.  I.e,
 * you should be able to step out of a signal handler to see what
 * instruction executes next after the signal handler completes.
 * Alternately, if you stepped into a signal handler, you should be
 * able to continue 'til the next breakpoint from within the signal
 * handler, even if the handler returns.
 */
#define MSR_USERCHANGE	(MSR_FE0 | MSR_FE1)

int do_signal(sigset_t *oldset, struct pt_regs *regs);
extern long sys_wait4(pid_t pid, unsigned int *stat_addr,
		     int options, /*unsigned long*/ struct rusage *ru);

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
long sys_sigsuspend(old_sigset_t mask, int p2, int p3, int p4, int p6, int p7,
	       struct pt_regs *regs)
{
	sigset_t saveset;
  
        PPCDBG(PPCDBG_SYS64X, "sys_sigsuspend - running - pid=%ld current=%lx comm=%s \n",
                current->pid, current, current->comm);



	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sigmask_lock);

	regs->gpr[3] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			/*
			 * If a signal handler needs to be called,
			 * do_signal() has set R3 to the signal number (the
			 * first argument of the signal handler), so don't
			 * overwrite that with EINTR !
			 * In the other cases, do_signal() doesn't touch 
			 * R3, so it's still set to -EINTR (see above).
			 */
			return regs->gpr[3];
	}
}

long sys_rt_sigsuspend(sigset_t *unewset, size_t sigsetsize, int p3, int p4, int p6,
		  int p7, struct pt_regs *regs)
{
	sigset_t saveset, newset;

  
        PPCDBG(PPCDBG_SYS64X, "sys_rt_sigsuspend - running - pid=%ld current=%lx comm=%s \n",
                current->pid, current, current->comm);
	
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

	regs->gpr[3] = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&saveset, regs))
			return regs->gpr[3];
	}
}



asmlinkage long sys_sigaltstack(const stack_t *uss, stack_t *uoss)
{
  struct pt_regs *regs = (struct pt_regs *) &uss;
  
  PPCDBG(PPCDBG_SYS64X, "sys_sigaltstack - running - pid=%ld current=%lx comm=%s \n",
          current->pid, current, current->comm);

  return do_sigaltstack(uss, uoss, regs->gpr[1]);
}

long sys_sigaction(int sig, const struct old_sigaction *act,
	      struct old_sigaction *oact)
{
	struct k_sigaction new_ka, old_ka;
	int ret;

        PPCDBG(PPCDBG_SYS64X, "sys_sigaction - running - pid=%ld current=%lx comm=%s \n",
                current->pid, current, current->comm);

 

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
 * When we have signals to deliver, we set up on the
 * user stack, going down from the original stack pointer:
 *	a sigregs struct
 *	one or more sigcontext structs
 *	a gap of __SIGNAL_FRAMESIZE bytes
 *
 * Each of these things must be a multiple of 16 bytes in size.
 *
 * XXX ultimately we will have to stack up a siginfo and ucontext
 * for each rt signal.
 */
struct sigregs {
	elf_gregset_t	gp_regs;
	double		fp_regs[ELF_NFPREG];
	unsigned int	tramp[2];
	/* 64 bit API allows for 288 bytes below sp before 
	   decrementing it. */
	int		abigap[72];
};



struct rt_sigframe
{
	unsigned long	_unused[2];
	struct siginfo *pinfo;
	void *puc;
	struct siginfo info;
	struct ucontext uc;
};


/*
 *  When we have rt signals to deliver, we set up on the
 *  user stack, going down from the original stack pointer:
 *	   a sigregs struct
 *	   one rt_sigframe struct (siginfo + ucontext)
 *	   a gap of __SIGNAL_FRAMESIZE bytes
 *
 *  Each of these things must be a multiple of 16 bytes in size.
 *
 */

int sys_rt_sigreturn(unsigned long r3, unsigned long r4, unsigned long r5,
		     unsigned long r6, unsigned long r7, unsigned long r8,
		     struct pt_regs *regs)
{
	struct rt_sigframe *rt_sf;
	struct sigcontext_struct sigctx;
	struct sigregs *sr;
	int ret;
	elf_gregset_t saved_regs;  /* an array of ELF_NGREG unsigned longs */
	sigset_t set;
	stack_t st;
	unsigned long prevsp;

	rt_sf = (struct rt_sigframe *)(regs->gpr[1] + __SIGNAL_FRAMESIZE);
	if (copy_from_user(&sigctx, &rt_sf->uc.uc_mcontext, sizeof(sigctx))
	    || copy_from_user(&set, &rt_sf->uc.uc_sigmask, sizeof(set))
	    || copy_from_user(&st, &rt_sf->uc.uc_stack, sizeof(st)))
		goto badframe;
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sigmask_lock);

	rt_sf++;			/* Look at next rt_sigframe */
	if (rt_sf == (struct rt_sigframe *)(sigctx.regs)) {
		/* Last stacked signal - restore registers -
		 * sigctx is initialized to point to the 
		 * preamble frame (where registers are stored) 
		 * see handle_signal()
		 */
		sr = (struct sigregs *) sigctx.regs;
		if (regs->msr & MSR_FP )
			giveup_fpu(current);
		if (copy_from_user(saved_regs, &sr->gp_regs,
				   sizeof(sr->gp_regs)))
			goto badframe;
		saved_regs[PT_MSR] = (regs->msr & ~MSR_USERCHANGE)
			| (saved_regs[PT_MSR] & MSR_USERCHANGE);
		saved_regs[PT_SOFTE] = regs->softe;
		memcpy(regs, saved_regs, GP_REGS_SIZE);
		if (copy_from_user(current->thread.fpr, &sr->fp_regs,
				   sizeof(sr->fp_regs)))
			goto badframe;
		/* This function sets back the stack flags into
		   the current task structure.  */
		sys_sigaltstack(&st, NULL);

		ret = regs->result;
	} else {
		/* More signals to go */
		/* Set up registers for next signal handler */
		regs->gpr[1] = (unsigned long)rt_sf - __SIGNAL_FRAMESIZE;
		if (copy_from_user(&sigctx, &rt_sf->uc.uc_mcontext, sizeof(sigctx)))
			goto badframe;
		sr = (struct sigregs *) sigctx.regs;
		regs->gpr[3] = ret = sigctx.signal;
		/* Get the siginfo   */
		get_user(regs->gpr[4], (unsigned long *)&rt_sf->pinfo);
		/* Get the ucontext */
		get_user(regs->gpr[5], (unsigned long *)&rt_sf->puc);
		regs->gpr[6] = (unsigned long) rt_sf;

		regs->link = (unsigned long) &sr->tramp;
		regs->nip = sigctx.handler;
		if (get_user(prevsp, &sr->gp_regs[PT_R1])
		    || put_user(prevsp, (unsigned long *) regs->gpr[1]))
			goto badframe;
	}
	return ret;

badframe:
	do_exit(SIGSEGV);
}

static void
setup_rt_frame(struct pt_regs *regs, struct sigregs *frame,
	       signed long newsp)
{
	struct rt_sigframe *rt_sf = (struct rt_sigframe *) newsp;
	/* Handler is *really* a pointer to the function descriptor for
	 * the signal routine.  The first entry in the function
	 * descriptor is the entry address of signal and the second
	 * entry is the TOC value we need to use.
	 */
        struct funct_descr_entry {
                     unsigned long entry;
                     unsigned long toc;
	};
  
	struct funct_descr_entry * funct_desc_ptr;
	unsigned long temp_ptr;

	/* Set up preamble frame */
	if (verify_area(VERIFY_WRITE, frame, sizeof(*frame)))
		goto badframe;
	if (regs->msr & MSR_FP)
		giveup_fpu(current);
	if (__copy_to_user(&frame->gp_regs, regs, GP_REGS_SIZE)
	    || __copy_to_user(&frame->fp_regs, current->thread.fpr,
			      ELF_NFPREG * sizeof(double))
	    || __put_user(0x38000000UL + __NR_rt_sigreturn, &frame->tramp[0])	/* li r0, __NR_rt_sigreturn */
	    || __put_user(0x44000002UL, &frame->tramp[1]))	/* sc */
		goto badframe;
	flush_icache_range((unsigned long) &frame->tramp[0],
			   (unsigned long) &frame->tramp[2]);

	/* Retrieve rt_sigframe from stack and
	   set up registers for signal handler
	*/
	newsp -= __SIGNAL_FRAMESIZE;

        if ( get_user(temp_ptr, &rt_sf->uc.uc_mcontext.handler)) {
		goto badframe;
	}

        funct_desc_ptr = ( struct funct_descr_entry *) temp_ptr;
        
	if (put_user(regs->gpr[1], (unsigned long *)newsp)
	    || get_user(regs->nip, &funct_desc_ptr->entry)
            || get_user(regs->gpr[2], &funct_desc_ptr->toc)
	    || get_user(regs->gpr[3], &rt_sf->uc.uc_mcontext.signal)
	    || get_user(regs->gpr[4], (unsigned long *)&rt_sf->pinfo)
	    || get_user(regs->gpr[5], (unsigned long *)&rt_sf->puc))
		goto badframe;

	regs->gpr[1] = newsp;
	regs->gpr[6] = (unsigned long) rt_sf;
	regs->link = (unsigned long) frame->tramp;

	
	return;

badframe:
#if DEBUG_SIG
	printk("badframe in setup_rt_frame, regs=%p frame=%p newsp=%lx\n",
	       regs, frame, newsp);
#endif
	do_exit(SIGSEGV);
}

/*
 * Do a signal return; undo the signal stack.
 */
long sys_sigreturn(unsigned long r3, unsigned long r4, unsigned long r5,
		   unsigned long r6, unsigned long r7, unsigned long r8,
		   struct pt_regs *regs)
{
	struct sigcontext_struct *sc, sigctx;
	struct sigregs *sr;
	long ret;
	elf_gregset_t saved_regs;  /* an array of ELF_NGREG unsigned longs */
	sigset_t set;
	unsigned long prevsp;

        sc = (struct sigcontext_struct *)(regs->gpr[1] + __SIGNAL_FRAMESIZE);
	if (copy_from_user(&sigctx, sc, sizeof(sigctx)))
		goto badframe;

	set.sig[0] = sigctx.oldmask;
#if _NSIG_WORDS > 1
	set.sig[1] = sigctx._unused[3];
#endif
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sigmask_lock);

	sc++;			/* Look at next sigcontext */
	if (sc == (struct sigcontext_struct *)(sigctx.regs)) {
		/* Last stacked signal - restore registers */
		sr = (struct sigregs *) sigctx.regs;
		if (regs->msr & MSR_FP )
			giveup_fpu(current);
		if (copy_from_user(saved_regs, &sr->gp_regs,
				   sizeof(sr->gp_regs)))
			goto badframe;
		saved_regs[PT_MSR] = (regs->msr & ~MSR_USERCHANGE)
			| (saved_regs[PT_MSR] & MSR_USERCHANGE);
		saved_regs[PT_SOFTE] = regs->softe;
		memcpy(regs, saved_regs, GP_REGS_SIZE);

		if (copy_from_user(current->thread.fpr, &sr->fp_regs,
				   sizeof(sr->fp_regs)))
			goto badframe;

		ret = regs->result;

	} else {
		/* More signals to go */
		regs->gpr[1] = (unsigned long)sc - __SIGNAL_FRAMESIZE;
		if (copy_from_user(&sigctx, sc, sizeof(sigctx)))
			goto badframe;
		sr = (struct sigregs *) sigctx.regs;
		regs->gpr[3] = ret = sigctx.signal;
		regs->gpr[4] = (unsigned long) sc;
		regs->link = (unsigned long) &sr->tramp;
		regs->nip = sigctx.handler;

		if (get_user(prevsp, &sr->gp_regs[PT_R1])
		    || put_user(prevsp, (unsigned long *) regs->gpr[1]))
			goto badframe;
	}
	return ret;

badframe:
	do_exit(SIGSEGV);
}	

/*
 * Set up a signal frame.
 */
static void
setup_frame(struct pt_regs *regs, struct sigregs *frame,
            unsigned long newsp)
{

	/* Handler is *really* a pointer to the function descriptor for
	 * the signal routine.  The first entry in the function
	 * descriptor is the entry address of signal and the second
	 * entry is the TOC value we need to use.
	 */
	struct funct_descr_entry {
		unsigned long entry;
		unsigned long toc;
	};
  
	struct funct_descr_entry * funct_desc_ptr;
	unsigned long temp_ptr;

	struct sigcontext_struct *sc = (struct sigcontext_struct *) newsp;
  
	if (verify_area(VERIFY_WRITE, frame, sizeof(*frame)))
		goto badframe;
	if (regs->msr & MSR_FP)
		giveup_fpu(current);
	if (__copy_to_user(&frame->gp_regs, regs, GP_REGS_SIZE)
	    || __copy_to_user(&frame->fp_regs, current->thread.fpr,
			      ELF_NFPREG * sizeof(double))
	    || __put_user(0x38000000UL + __NR_sigreturn, &frame->tramp[0])    /* li r0, __NR_sigreturn */
	    || __put_user(0x44000002UL, &frame->tramp[1]))   /* sc */
		goto badframe;
	flush_icache_range((unsigned long) &frame->tramp[0],
			   (unsigned long) &frame->tramp[2]);

	newsp -= __SIGNAL_FRAMESIZE;
	if ( get_user(temp_ptr, &sc->handler))
		goto badframe;
  
	funct_desc_ptr = ( struct funct_descr_entry *) temp_ptr;

	if (put_user(regs->gpr[1], (unsigned long *)newsp)
	    || get_user(regs->nip, & funct_desc_ptr ->entry)
	    || get_user(regs->gpr[2],& funct_desc_ptr->toc)
	    || get_user(regs->gpr[3], &sc->signal))
		goto badframe;
	regs->gpr[1] = newsp;
	regs->gpr[4] = (unsigned long) sc;
	regs->link = (unsigned long) frame->tramp;


	PPCDBG(PPCDBG_SIGNAL, "setup_frame - returning - regs->gpr[1]=%lx, regs->gpr[4]=%lx, regs->link=%lx \n",
	       regs->gpr[1], regs->gpr[4], regs->link);

	return;

 badframe:
	PPCDBG(PPCDBG_SIGNAL, "setup_frame - badframe in setup_frame, regs=%p frame=%p newsp=%lx\n", regs, frame, newsp);  PPCDBG_ENTER_DEBUGGER();
#if DEBUG_SIG
	printk("badframe in setup_frame, regs=%p frame=%p newsp=%lx\n",
	       regs, frame, newsp);
#endif
	do_exit(SIGSEGV);
}

/*
 * OK, we're invoking a handler
 */
static void
handle_signal(unsigned long sig, siginfo_t *info, sigset_t *oldset,
	struct pt_regs * regs, unsigned long *newspp, unsigned long frame)
{
	struct sigcontext_struct *sc;
        struct rt_sigframe *rt_sf;
	struct k_sigaction *ka = &current->sig->action[sig-1];

	if (regs->trap == 0x0C00 /* System Call! */
	    && ((int)regs->result == -ERESTARTNOHAND ||
		((int)regs->result == -ERESTARTSYS &&
		 !(ka->sa.sa_flags & SA_RESTART))))
		regs->result = -EINTR;
        /* Set up Signal Frame */
     
	if (ka->sa.sa_flags & SA_SIGINFO) {
		/* Put a Real Time Context onto stack */
		*newspp -= sizeof(*rt_sf);
		rt_sf = (struct rt_sigframe *) *newspp;
		if (verify_area(VERIFY_WRITE, rt_sf, sizeof(*rt_sf)))
			goto badframe;
                

		if (__put_user((unsigned long) ka->sa.sa_handler, &rt_sf->uc.uc_mcontext.handler)
		    || __put_user(&rt_sf->info, &rt_sf->pinfo)
		    || __put_user(&rt_sf->uc, &rt_sf->puc)
		    /* Put the siginfo */
		    || __copy_to_user(&rt_sf->info, info, sizeof(*info))
		    /* Create the ucontext */
		    || __put_user(0, &rt_sf->uc.uc_flags)
		    || __put_user(0, &rt_sf->uc.uc_link)
		    || __put_user(current->sas_ss_sp, &rt_sf->uc.uc_stack.ss_sp)
		    || __put_user(sas_ss_flags(regs->gpr[1]), 
				  &rt_sf->uc.uc_stack.ss_flags)
		    || __put_user(current->sas_ss_size, &rt_sf->uc.uc_stack.ss_size)
		    || __copy_to_user(&rt_sf->uc.uc_sigmask, oldset, sizeof(*oldset))
		    /* mcontext.regs points to preamble register frame */
		    || __put_user((struct pt_regs *)frame, &rt_sf->uc.uc_mcontext.regs)
		    || __put_user(sig, &rt_sf->uc.uc_mcontext.signal))
			goto badframe;
               
	} else {
	        /* Put another sigcontext on the stack */
                *newspp -= sizeof(*sc);
        	sc = (struct sigcontext_struct *) *newspp;
        	if (verify_area(VERIFY_WRITE, sc, sizeof(*sc)))
	        	goto badframe;

        	if (__put_user((unsigned long) ka->sa.sa_handler, &sc->handler)
	           || __put_user(oldset->sig[0], &sc->oldmask)
#if _NSIG_WORDS > 1
	           || __put_user(oldset->sig[1], &sc->_unused[3])
#endif
	           || __put_user((struct pt_regs *)frame, &sc->regs)
	           || __put_user(sig, &sc->signal))
	        	goto badframe;
	}

	if (ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;

	if (!(ka->sa.sa_flags & SA_NODEFER)) {
		spin_lock_irq(&current->sigmask_lock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,sig);
		recalc_sigpending();
		spin_unlock_irq(&current->sigmask_lock);
	}
	return;

badframe:
#if DEBUG_SIG
	printk("badframe in handle_signal, regs=%p frame=%lx newsp=%lx\n",
	       regs, frame, *newspp);
	printk("sc=%p sig=%d ka=%p info=%p oldset=%p\n", sc, sig, ka, info, oldset);
#endif
	do_exit(SIGSEGV);
}

/*
 * Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
extern int do_signal32(sigset_t *oldset, struct pt_regs *regs);
int do_signal(sigset_t *oldset, struct pt_regs *regs)
{
	siginfo_t info;
	struct k_sigaction *ka;
	unsigned long frame, newsp;
	int signr;

	/*
	 * If the current thread is 32 bit - invoke the
	 * 32 bit signal handling code
	 */
	if (test_thread_flag(TIF_32BIT))
		return do_signal32(oldset, regs);

        if (!oldset)
		oldset = &current->blocked;

	newsp = frame = 0;

	signr = get_signal_to_deliver(&info, regs);
	if (signr > 0) {
		ka = &current->sig->action[signr-1];
		if ( (ka->sa.sa_flags & SA_ONSTACK)
		     && (! on_sig_stack(regs->gpr[1])))
			newsp = (current->sas_ss_sp + current->sas_ss_size);
		else
			newsp = regs->gpr[1];
		newsp = frame = newsp - sizeof(struct sigregs);

		/* Whee!  Actually deliver the signal.  */
  
                PPCDBG(PPCDBG_SIGNAL, "do_signal - GOING TO RUN SIGNAL HANDLER - pid=%ld current=%lx comm=%s \n", current->pid, current, current->comm);
		handle_signal(signr, &info, oldset, regs, &newsp, frame);
                PPCDBG(PPCDBG_SIGNAL, "do_signal - after running signal handler - pid=%ld current=%lx comm=%s \n", current->pid, current, current->comm);
	}

	if (regs->trap == 0x0C00 /* System Call! */ &&
	    ((int)regs->result == -ERESTARTNOHAND ||
	     (int)regs->result == -ERESTARTSYS ||
	     (int)regs->result == -ERESTARTNOINTR)) {
                PPCDBG(PPCDBG_SIGNAL, "do_signal - going to back up & retry system call \n");
		regs->gpr[3] = regs->orig_gpr3;
		regs->nip -= 4;		/* Back up & retry system call */
		regs->result = 0;
	}

	if (newsp == frame)
          {
             PPCDBG(PPCDBG_SIGNAL, "do_signal - returning w/ no signal delivered \n");
             return 0;		/* no signals delivered */
           }
  
     

        if (ka->sa.sa_flags & SA_SIGINFO)
             setup_rt_frame(regs, (struct sigregs *) frame, newsp);
        else        
	    setup_frame(regs, (struct sigregs *) frame, newsp);
        PPCDBG(PPCDBG_SIGNAL, "do_signal - returning a signal was delivered \n");
	return 1;
}
