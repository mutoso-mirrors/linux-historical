/*  $Id: signal32.c,v 1.60 2000/02/25 06:02:37 jj Exp $
 *  arch/sparc64/kernel/signal32.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1996 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1997 Eddie C. Dost   (ecd@skynet.be)
 *  Copyright (C) 1997,1998 Jakub Jelinek   (jj@sunsite.mff.cuni.cz)
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/wait.h>
#include <linux/ptrace.h>
#include <linux/unistd.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/bitops.h>
#include <asm/ptrace.h>
#include <asm/svr4.h>
#include <asm/pgtable.h>
#include <asm/psrcompat.h>
#include <asm/fpumacro.h>
#include <asm/visasm.h>

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage int sys_wait4(pid_t pid, unsigned long *stat_addr,
			 int options, unsigned long *ru);

asmlinkage int do_signal32(sigset_t *oldset, struct pt_regs *regs,
			 unsigned long orig_o0, int ret_from_syscall);

/* This turned off for production... */
/* #define DEBUG_SIGNALS 1 */
/* #define DEBUG_SIGNALS_TRACE 1 */
/* #define DEBUG_SIGNALS_MAPS 1 */
/* #define DEBUG_SIGNALS_TLB 1 */

/* Signal frames: the original one (compatible with SunOS):
 *
 * Set up a signal frame... Make the stack look the way SunOS
 * expects it to look which is basically:
 *
 * ---------------------------------- <-- %sp at signal time
 * Struct sigcontext
 * Signal address
 * Ptr to sigcontext area above
 * Signal code
 * The signal number itself
 * One register window
 * ---------------------------------- <-- New %sp
 */
struct signal_sframe32 {
	struct reg_window32 sig_window;
	int sig_num;
	int sig_code;
	/* struct sigcontext32 * */ u32 sig_scptr;
	int sig_address;
	struct sigcontext32 sig_context;
	unsigned extramask[_NSIG_WORDS32 - 1];
};

/* 
 * And the new one, intended to be used for Linux applications only
 * (we have enough in there to work with clone).
 * All the interesting bits are in the info field.
 */
struct new_signal_frame32 {
	struct sparc_stackf32	ss;
	__siginfo32_t		info;
	/* __siginfo_fpu32_t * */ u32 fpu_save;
	unsigned int		insns [2];
	unsigned		extramask[_NSIG_WORDS32 - 1];
	unsigned		extra_size; /* Should be sizeof(siginfo_extra_v8plus_t) */
	/* Only valid if (info.si_regs.psr & (PSR_VERS|PSR_IMPL)) == PSR_V8PLUS */
	siginfo_extra_v8plus_t	v8plus;
	__siginfo_fpu_t		fpu_state;
};

struct rt_signal_frame32 {
	struct sparc_stackf32	ss;
	siginfo_t32		info;
	struct pt_regs32	regs;
	sigset_t32		mask;
	/* __siginfo_fpu32_t * */ u32 fpu_save;
	unsigned int		insns [2];
	stack_t32		stack;
	unsigned		extra_size; /* Should be sizeof(siginfo_extra_v8plus_t) */
	/* Only valid if (regs.psr & (PSR_VERS|PSR_IMPL)) == PSR_V8PLUS */
	siginfo_extra_v8plus_t	v8plus;
	__siginfo_fpu_t		fpu_state;
};

/* Align macros */
#define SF_ALIGNEDSZ  (((sizeof(struct signal_sframe32) + 7) & (~7)))
#define NF_ALIGNEDSZ  (((sizeof(struct new_signal_frame32) + 7) & (~7)))
#define RT_ALIGNEDSZ  (((sizeof(struct rt_signal_frame32) + 7) & (~7)))

int copy_siginfo_to_user32(siginfo_t32 *to, siginfo_t *from)
{
	int err;

	if (!access_ok (VERIFY_WRITE, to, sizeof(siginfo_t32)))
		return -EFAULT;

	err = __put_user(from->si_signo, &to->si_signo);
	err |= __put_user(from->si_errno, &to->si_errno);
	err |= __put_user(from->si_code, &to->si_code);
	if (from->si_code < 0)
		err |= __copy_to_user(&to->_sifields._pad, &from->_sifields._pad, SI_PAD_SIZE);
	else {
		int signo = from->si_signo;
		if (from->si_code == SI_USER || from->si_code == SI_KERNEL)
			signo = SIGRTMIN;
		switch (signo) {
		case SIGCHLD:
			err |= __put_user(from->si_utime, &to->si_utime);
			err |= __put_user(from->si_stime, &to->si_stime);
			err |= __put_user(from->si_status, &to->si_status);
		default:
			err |= __put_user(from->si_pid, &to->si_pid);
			err |= __put_user(from->si_uid, &to->si_uid);
			break;
		case SIGURG:
		case SIGIO:
		case SIGSEGV:
		case SIGILL:
		case SIGFPE:
		case SIGBUS:
		case SIGEMT:
			err |= __put_user(from->si_trapno, &to->si_trapno);
			err |= __put_user((long)from->si_addr, &to->si_addr);
			break;
		}
	}
	return err;
}

/*
 * atomically swap in the new signal mask, and wait for a signal.
 * This is really tricky on the Sparc, watch out...
 */
asmlinkage void _sigpause32_common(old_sigset_t32 set, struct pt_regs *regs)
{
	sigset_t saveset;

	set &= _BLOCKABLE;
	spin_lock_irq(&current->sigmask_lock);
	saveset = current->blocked;
	siginitset(&current->blocked, set);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;

	/* Condition codes and return value where set here for sigpause,
	 * and so got used by setup_frame, which again causes sigreturn()
	 * to return -EINTR.
	 */
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		/*
		 * Return -EINTR and set condition code here,
		 * so the interrupted system call actually returns
		 * these.
		 */
		regs->tstate |= TSTATE_ICARRY;
		regs->u_regs[UREG_I0] = EINTR;
		if (do_signal32(&saveset, regs, 0, 0))
			return;
	}
}

asmlinkage void do_rt_sigsuspend32(u32 uset, size_t sigsetsize, struct pt_regs *regs)
{
	sigset_t oldset, set;
	sigset_t32 set32;
        
	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (((__kernel_size_t32)sigsetsize) != sizeof(sigset_t)) {
		regs->tstate |= TSTATE_ICARRY;
		regs->u_regs[UREG_I0] = EINVAL;
		return;
	}
	if (copy_from_user(&set32, (void *)(long)uset, sizeof(set32))) {
		regs->tstate |= TSTATE_ICARRY;
		regs->u_regs[UREG_I0] = EFAULT;
		return;
	}
	switch (_NSIG_WORDS) {
	case 4: set.sig[3] = set32.sig[6] + (((long)set32.sig[7]) << 32);
	case 3: set.sig[2] = set32.sig[4] + (((long)set32.sig[5]) << 32);
	case 2: set.sig[1] = set32.sig[2] + (((long)set32.sig[3]) << 32);
	case 1: set.sig[0] = set32.sig[0] + (((long)set32.sig[1]) << 32);
	}
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	oldset = current->blocked;
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	
	regs->tpc = regs->tnpc;
	regs->tnpc += 4;

	/* Condition codes and return value where set here for sigpause,
	 * and so got used by setup_frame, which again causes sigreturn()
	 * to return -EINTR.
	 */
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		/*
		 * Return -EINTR and set condition code here,
		 * so the interrupted system call actually returns
		 * these.
		 */
		regs->tstate |= TSTATE_ICARRY;
		regs->u_regs[UREG_I0] = EINTR;
		if (do_signal32(&oldset, regs, 0, 0))
			return;
	}
}

static inline int restore_fpu_state32(struct pt_regs *regs, __siginfo_fpu_t *fpu)
{
	unsigned long *fpregs = (unsigned long *)(((char *)current) + AOFF_task_fpregs);
	unsigned long fprs;
	int err;
	
	err = __get_user(fprs, &fpu->si_fprs);
	fprs_write(0);
	regs->tstate &= ~TSTATE_PEF;
	if (fprs & FPRS_DL)
		err |= copy_from_user(fpregs, &fpu->si_float_regs[0], (sizeof(unsigned int) * 32));
	if (fprs & FPRS_DU)
		err |= copy_from_user(fpregs+16, &fpu->si_float_regs[32], (sizeof(unsigned int) * 32));
	err |= __get_user(current->thread.xfsr[0], &fpu->si_fsr);
	err |= __get_user(current->thread.gsr[0], &fpu->si_gsr);
	current->thread.fpsaved[0] |= fprs;
	return err;
}

void do_new_sigreturn32(struct pt_regs *regs)
{
	struct new_signal_frame32 *sf;
	unsigned int psr;
	unsigned pc, npc, fpu_save;
	sigset_t set;
	unsigned seta[_NSIG_WORDS32];
	int err, i;
	
	regs->u_regs[UREG_FP] &= 0x00000000ffffffffUL;
	sf = (struct new_signal_frame32 *) regs->u_regs [UREG_FP];

	/* 1. Make sure we are not getting garbage from the user */
	if (verify_area (VERIFY_READ, sf, sizeof (*sf))	||
	    (((unsigned long) sf) & 3))
		goto segv;

	get_user(pc, &sf->info.si_regs.pc);
	__get_user(npc, &sf->info.si_regs.npc);

	if ((pc | npc) & 3)
		goto segv;

	regs->tpc = pc;
	regs->tnpc = npc;

	/* 2. Restore the state */
	err = __get_user(regs->y, &sf->info.si_regs.y);
	err |= __get_user(psr, &sf->info.si_regs.psr);

	for (i = UREG_G1; i <= UREG_I7; i++)
		err |= __get_user(regs->u_regs[i], &sf->info.si_regs.u_regs[i]);
	if ((psr & (PSR_VERS|PSR_IMPL)) == PSR_V8PLUS) {
		err |= __get_user(i, &sf->v8plus.g_upper[0]);
		if (i == SIGINFO_EXTRA_V8PLUS_MAGIC) {
			for (i = UREG_G1; i <= UREG_I7; i++)
				err |= __get_user(((u32 *)regs->u_regs)[2*i], &sf->v8plus.g_upper[i]);
		}
	}

	/* User can only change condition codes in %tstate. */
	regs->tstate &= ~(TSTATE_ICC|TSTATE_XCC);
	regs->tstate |= psr_to_tstate_icc(psr);

	err |= __get_user(fpu_save, &sf->fpu_save);
	if (fpu_save)
		err |= restore_fpu_state32(regs, &sf->fpu_state);
	err |= __get_user(seta[0], &sf->info.si_mask);
	err |= copy_from_user(seta+1, &sf->extramask, (_NSIG_WORDS32 - 1) * sizeof(unsigned));
	if (err)
	    	goto segv;
	switch (_NSIG_WORDS) {
		case 4: set.sig[3] = seta[6] + (((long)seta[7]) << 32);
		case 3: set.sig[2] = seta[4] + (((long)seta[5]) << 32);
		case 2: set.sig[1] = seta[2] + (((long)seta[3]) << 32);
		case 1: set.sig[0] = seta[0] + (((long)seta[1]) << 32);
	}
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	return;

segv:
	do_exit(SIGSEGV);
}

asmlinkage void do_sigreturn32(struct pt_regs *regs)
{
	struct sigcontext32 *scptr;
	unsigned pc, npc, psr;
	sigset_t set;
	unsigned seta[_NSIG_WORDS32];
	int err;

	synchronize_user_stack();
	if (current->thread.flags & SPARC_FLAG_NEWSIGNALS)
		return do_new_sigreturn32(regs);

	scptr = (struct sigcontext32 *)
		(regs->u_regs[UREG_I0] & 0x00000000ffffffffUL);
	/* Check sanity of the user arg. */
	if(verify_area(VERIFY_READ, scptr, sizeof(struct sigcontext32)) ||
	   (((unsigned long) scptr) & 3))
		goto segv;

	err = __get_user(pc, &scptr->sigc_pc);
	err |= __get_user(npc, &scptr->sigc_npc);

	if((pc | npc) & 3)
		goto segv; /* Nice try. */

	err |= __get_user(seta[0], &scptr->sigc_mask);
	/* Note that scptr + 1 points to extramask */
	err |= copy_from_user(seta+1, scptr + 1, (_NSIG_WORDS32 - 1) * sizeof(unsigned));
	if (err)
	    	goto segv;
	switch (_NSIG_WORDS) {
		case 4: set.sig[3] = seta[6] + (((long)seta[7]) << 32);
		case 3: set.sig[2] = seta[4] + (((long)seta[5]) << 32);
		case 2: set.sig[1] = seta[2] + (((long)seta[3]) << 32);
		case 1: set.sig[0] = seta[0] + (((long)seta[1]) << 32);
	}
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	
	regs->tpc = pc;
	regs->tnpc = npc;
	err = __get_user(regs->u_regs[UREG_FP], &scptr->sigc_sp);
	err |= __get_user(regs->u_regs[UREG_I0], &scptr->sigc_o0);
	err |= __get_user(regs->u_regs[UREG_G1], &scptr->sigc_g1);

	/* User can only change condition codes in %tstate. */
	err |= __get_user(psr, &scptr->sigc_psr);
	if (err)
		goto segv;
	regs->tstate &= ~(TSTATE_ICC|TSTATE_XCC);
	regs->tstate |= psr_to_tstate_icc(psr);
	return;

segv:
	do_exit(SIGSEGV);
}

asmlinkage void do_rt_sigreturn32(struct pt_regs *regs)
{
	struct rt_signal_frame32 *sf;
	unsigned int psr;
	unsigned pc, npc, fpu_save;
	sigset_t set;
	sigset_t32 seta;
	stack_t st;
	int err, i;
	
	synchronize_user_stack();
	regs->u_regs[UREG_FP] &= 0x00000000ffffffffUL;
	sf = (struct rt_signal_frame32 *) regs->u_regs [UREG_FP];

	/* 1. Make sure we are not getting garbage from the user */
	if (verify_area (VERIFY_READ, sf, sizeof (*sf))	||
	    (((unsigned long) sf) & 3))
		goto segv;

	get_user(pc, &sf->regs.pc);
	__get_user(npc, &sf->regs.npc);

	if ((pc | npc) & 3)
		goto segv;

	regs->tpc = pc;
	regs->tnpc = npc;

	/* 2. Restore the state */
	err = __get_user(regs->y, &sf->regs.y);
	err |= __get_user(psr, &sf->regs.psr);
	
	for (i = UREG_G1; i <= UREG_I7; i++)
		err |= __get_user(regs->u_regs[i], &sf->regs.u_regs[i]);
	if ((psr & (PSR_VERS|PSR_IMPL)) == PSR_V8PLUS) {
		err |= __get_user(i, &sf->v8plus.g_upper[0]);
		if (i == SIGINFO_EXTRA_V8PLUS_MAGIC) {
			for (i = UREG_G1; i <= UREG_I7; i++)
				err |= __get_user(((u32 *)regs->u_regs)[2*i], &sf->v8plus.g_upper[i]);
		}
	}

	/* User can only change condition codes in %tstate. */
	regs->tstate &= ~(TSTATE_ICC|TSTATE_XCC);
	regs->tstate |= psr_to_tstate_icc(psr);

	err |= __get_user(fpu_save, &sf->fpu_save);
	if (fpu_save)
		err |= restore_fpu_state32(regs, &sf->fpu_state);
	err |= copy_from_user(&seta, &sf->mask, sizeof(sigset_t32));
	err |= __get_user((long)st.ss_sp, &sf->stack.ss_sp);
	err |= __get_user(st.ss_flags, &sf->stack.ss_flags);
	err |= __get_user(st.ss_size, &sf->stack.ss_size);
	if (err)
		goto segv;
		
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, (unsigned long)sf);
	
	switch (_NSIG_WORDS) {
		case 4: set.sig[3] = seta.sig[6] + (((long)seta.sig[7]) << 32);
		case 3: set.sig[2] = seta.sig[4] + (((long)seta.sig[5]) << 32);
		case 2: set.sig[1] = seta.sig[2] + (((long)seta.sig[3]) << 32);
		case 1: set.sig[0] = seta.sig[0] + (((long)seta.sig[1]) << 32);
	}
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	return;
segv:
	do_exit(SIGSEGV);
}

/* Checks if the fp is valid */
static int invalid_frame_pointer(void *fp, int fplen)
{
	if ((((unsigned long) fp) & 7) || ((unsigned long)fp) > 0x100000000ULL - fplen)
		return 1;
	return 0;
}

static inline void *get_sigframe(struct sigaction *sa, struct pt_regs *regs, unsigned long framesize)
{
	unsigned long sp;
	
	regs->u_regs[UREG_FP] &= 0x00000000ffffffffUL;
	sp = regs->u_regs[UREG_FP];
	
	/* This is the X/Open sanctioned signal stack switching.  */
	if (sa->sa_flags & SA_ONSTACK) {
		if (!on_sig_stack(sp) && !((current->sas_ss_sp + current->sas_ss_size) & 7))
			sp = current->sas_ss_sp + current->sas_ss_size;
	}
	return (void *)(sp - framesize);
}

static void
setup_frame32(struct sigaction *sa, struct pt_regs *regs, int signr, sigset_t *oldset, siginfo_t *info)
{
	struct signal_sframe32 *sframep;
	struct sigcontext32 *sc;
	unsigned seta[_NSIG_WORDS32];
	int err = 0;
	void *sig_address;
	int sig_code;
	unsigned long pc = regs->tpc;
	unsigned long npc = regs->tnpc;
	
#if 0	
	int window = 0;
#endif	
	unsigned psr;

	synchronize_user_stack();
	save_and_clear_fpu();

	sframep = (struct signal_sframe32 *)get_sigframe(sa, regs, SF_ALIGNEDSZ);
	if (invalid_frame_pointer (sframep, sizeof(*sframep))){
#ifdef DEBUG_SIGNALS /* fills up the console logs during crashme runs, yuck... */
		printk("%s [%d]: User has trashed signal stack\n",
		       current->comm, current->pid);
		printk("Sigstack ptr %p handler at pc<%016lx> for sig<%d>\n",
		       sframep, pc, signr);
#endif
		/* Don't change signal code and address, so that
		 * post mortem debuggers can have a look.
		 */
		do_exit(SIGILL);
	}

	sc = &sframep->sig_context;

	/* We've already made sure frame pointer isn't in kernel space... */
	err = __put_user((sas_ss_flags(regs->u_regs[UREG_FP]) == SS_ONSTACK),
			 &sc->sigc_onstack);
	
	switch (_NSIG_WORDS) {
	case 4: seta[7] = (oldset->sig[3] >> 32);
	        seta[6] = oldset->sig[3];
	case 3: seta[5] = (oldset->sig[2] >> 32);
	        seta[4] = oldset->sig[2];
	case 2: seta[3] = (oldset->sig[1] >> 32);
	        seta[2] = oldset->sig[1];
	case 1: seta[1] = (oldset->sig[0] >> 32);
	        seta[0] = oldset->sig[0];
	}
	err |= __put_user(seta[0], &sc->sigc_mask);
	err |= __copy_to_user(sframep->extramask, seta + 1,
			      (_NSIG_WORDS32 - 1) * sizeof(unsigned));
	err |= __put_user(regs->u_regs[UREG_FP], &sc->sigc_sp);
	err |= __put_user(pc, &sc->sigc_pc);
	err |= __put_user(npc, &sc->sigc_npc);
	psr = tstate_to_psr (regs->tstate);
	if(current->thread.fpsaved[0] & FPRS_FEF)
		psr |= PSR_EF;
	err |= __put_user(psr, &sc->sigc_psr);
	err |= __put_user(regs->u_regs[UREG_G1], &sc->sigc_g1);
	err |= __put_user(regs->u_regs[UREG_I0], &sc->sigc_o0);
	err |= __put_user(current->thread.w_saved, &sc->sigc_oswins);
#if 0
/* w_saved is not currently used... */
	if(current->thread.w_saved)
		for(window = 0; window < current->thread.w_saved; window++) {
			sc->sigc_spbuf[window] =
				(char *)current->thread.rwbuf_stkptrs[window];
			err |= copy_to_user(&sc->sigc_wbuf[window],
					    &current->thread.reg_window[window],
					    sizeof(struct reg_window));
		}
	else
#endif	
		err |= copy_in_user((u32 *)sframep,
				    (u32 *)(regs->u_regs[UREG_FP]),
				    sizeof(struct reg_window32));
		       
	current->thread.w_saved = 0; /* So process is allowed to execute. */
	err |= __put_user(signr, &sframep->sig_num);
	sig_address = NULL;
	sig_code = 0;
	if (SI_FROMKERNEL (info) && (info->si_code & __SI_MASK) == __SI_FAULT) {
		sig_address = info->si_addr;
		switch (signr) {
		case SIGSEGV:
			switch (info->si_code) {
			case SEGV_MAPERR: sig_code = SUBSIG_NOMAPPING; break;
			default: sig_code = SUBSIG_PROTECTION; break;
			}
			break;
		case SIGILL:
			switch (info->si_code) {
			case ILL_ILLOPC: sig_code = SUBSIG_ILLINST; break;
			case ILL_PRVOPC: sig_code = SUBSIG_PRIVINST; break;
			case ILL_ILLTRP: sig_code = SUBSIG_BADTRAP (info->si_trapno); break;
			default: sig_code = SUBSIG_STACK; break;
			}
			break;
		case SIGFPE:
			switch (info->si_code) {
			case FPE_INTDIV: sig_code = SUBSIG_IDIVZERO; break;
			case FPE_INTOVF: sig_code = SUBSIG_FPINTOVFL; break;
			case FPE_FLTDIV: sig_code = SUBSIG_FPDIVZERO; break;
			case FPE_FLTOVF: sig_code = SUBSIG_FPOVFLOW; break;
			case FPE_FLTUND: sig_code = SUBSIG_FPUNFLOW; break;
			case FPE_FLTRES: sig_code = SUBSIG_FPINEXACT; break;
			case FPE_FLTINV: sig_code = SUBSIG_FPOPERROR; break;
			default: sig_code = SUBSIG_FPERROR; break;
			}
			break;
		case SIGBUS:
			switch (info->si_code) {
			case BUS_ADRALN: sig_code = SUBSIG_ALIGNMENT; break;
			case BUS_ADRERR: sig_code = SUBSIG_MISCERROR; break;
			default: sig_code = SUBSIG_BUSTIMEOUT; break;
			}
			break;
		case SIGEMT:
			switch (info->si_code) {
			case EMT_TAGOVF: sig_code = SUBSIG_TAG; break;
			}
			break;
		case SIGSYS:
			if (info->si_code == (__SI_FAULT|0x100)) {
				/* See sys_sunos32.c */
				sig_code = info->si_trapno;
				break;
			}
		default:
			sig_address = NULL;
		}
	}
	err |= __put_user((long)sig_address, &sframep->sig_address);
	err |= __put_user(sig_code, &sframep->sig_code);
	err |= __put_user((u64)sc, &sframep->sig_scptr);
	if (err)
		goto sigsegv;

	regs->u_regs[UREG_FP] = (unsigned long) sframep;
	regs->tpc = (unsigned long) sa->sa_handler;
	regs->tnpc = (regs->tpc + 4);
	return;

sigsegv:
	do_exit(SIGSEGV);
}


static inline int save_fpu_state32(struct pt_regs *regs, __siginfo_fpu_t *fpu)
{
	unsigned long *fpregs = (unsigned long *)(((char *)current) + AOFF_task_fpregs);
	unsigned long fprs;
	int err = 0;
	
	fprs = current->thread.fpsaved[0];
	if (fprs & FPRS_DL)
		err |= copy_to_user(&fpu->si_float_regs[0], fpregs,
				    (sizeof(unsigned int) * 32));
	if (fprs & FPRS_DU)
		err |= copy_to_user(&fpu->si_float_regs[32], fpregs+16,
				    (sizeof(unsigned int) * 32));
	err |= __put_user(current->thread.xfsr[0], &fpu->si_fsr);
	err |= __put_user(current->thread.gsr[0], &fpu->si_gsr);
	err |= __put_user(fprs, &fpu->si_fprs);

	return err;
}

static inline void new_setup_frame32(struct k_sigaction *ka, struct pt_regs *regs,
				     int signo, sigset_t *oldset)
{
	struct new_signal_frame32 *sf;
	int sigframe_size;
	u32 psr;
	int i, err;
	unsigned seta[_NSIG_WORDS32];

	/* 1. Make sure everything is clean */
	synchronize_user_stack();
	save_and_clear_fpu();
	
	sigframe_size = NF_ALIGNEDSZ;
	if (!(current->thread.fpsaved[0] & FPRS_FEF))
		sigframe_size -= sizeof(__siginfo_fpu_t);

	sf = (struct new_signal_frame32 *)get_sigframe(&ka->sa, regs, sigframe_size);
	
	if (invalid_frame_pointer (sf, sigframe_size)) {
#ifdef DEBUG_SIGNALS
		printk("new_setup_frame32(%s:%d): invalid_frame_pointer(%p, %d)\n",
		       current->comm, current->pid, sf, sigframe_size);
#endif
		goto sigill;
	}

	if (current->thread.w_saved != 0) {
#ifdef DEBUG_SIGNALS
		printk ("%s[%d]: Invalid user stack frame for "
			"signal delivery.\n", current->comm, current->pid);
#endif
		goto sigill;
	}

	/* 2. Save the current process state */
	err  = put_user(regs->tpc, &sf->info.si_regs.pc);
	err |= __put_user(regs->tnpc, &sf->info.si_regs.npc);
	err |= __put_user(regs->y, &sf->info.si_regs.y);
	psr = tstate_to_psr (regs->tstate);
	if(current->thread.fpsaved[0] & FPRS_FEF)
		psr |= PSR_EF;
	err |= __put_user(psr, &sf->info.si_regs.psr);
	for (i = 0; i < 16; i++)
		err |= __put_user(regs->u_regs[i], &sf->info.si_regs.u_regs[i]);
	err |= __put_user(sizeof(siginfo_extra_v8plus_t), &sf->extra_size);
	err |= __put_user(SIGINFO_EXTRA_V8PLUS_MAGIC, &sf->v8plus.g_upper[0]);
	for (i = 1; i < 16; i++)
		err |= __put_user(((u32 *)regs->u_regs)[2*i], &sf->v8plus.g_upper[i]);

	if (psr & PSR_EF) {
		err |= save_fpu_state32(regs, &sf->fpu_state);
		err |= __put_user((u64)&sf->fpu_state, &sf->fpu_save);
	} else {
		err |= __put_user(0, &sf->fpu_save);
	}

	switch (_NSIG_WORDS) {
	case 4: seta[7] = (oldset->sig[3] >> 32);
	        seta[6] = oldset->sig[3];
	case 3: seta[5] = (oldset->sig[2] >> 32);
	        seta[4] = oldset->sig[2];
	case 2: seta[3] = (oldset->sig[1] >> 32);
	        seta[2] = oldset->sig[1];
	case 1: seta[1] = (oldset->sig[0] >> 32);
	        seta[0] = oldset->sig[0];
	}
	err |= __put_user(seta[0], &sf->info.si_mask);
	err |= __copy_to_user(sf->extramask, seta + 1,
			      (_NSIG_WORDS32 - 1) * sizeof(unsigned));

	err |= copy_in_user((u32 *)sf,
			    (u32 *)(regs->u_regs[UREG_FP]),
			    sizeof(struct reg_window32));
	
	if (err)
		goto sigsegv;

	/* 3. signal handler back-trampoline and parameters */
	regs->u_regs[UREG_FP] = (unsigned long) sf;
	regs->u_regs[UREG_I0] = signo;
	regs->u_regs[UREG_I1] = (unsigned long) &sf->info;

	/* 4. signal handler */
	regs->tpc = (unsigned long) ka->sa.sa_handler;
	regs->tnpc = (regs->tpc + 4);

	/* 5. return to kernel instructions */
	if (ka->ka_restorer)
		regs->u_regs[UREG_I7] = (unsigned long)ka->ka_restorer;
	else {
		/* Flush instruction space. */
		unsigned long address = ((unsigned long)&(sf->insns[0]));
		pgd_t *pgdp = pgd_offset(current->mm, address);
		pmd_t *pmdp = pmd_offset(pgdp, address);
		pte_t *ptep = pte_offset(pmdp, address);

		regs->u_regs[UREG_I7] = (unsigned long) (&(sf->insns[0]) - 2);
	
		err  = __put_user(0x821020d8, &sf->insns[0]); /*mov __NR_sigreturn, %g1*/
		err |= __put_user(0x91d02010, &sf->insns[1]); /*t 0x10*/
		if(err)
			goto sigsegv;

		if(pte_present(*ptep)) {
			unsigned long page = (unsigned long)
				__va(pte_pagenr(*ptep) << PAGE_SHIFT);

			__asm__ __volatile__("
			membar	#StoreStore
			flush	%0 + %1"
			: : "r" (page), "r" (address & (PAGE_SIZE - 1))
			: "memory");
		}
	}
	return;

sigill:
	do_exit(SIGILL);
sigsegv:
	do_exit(SIGSEGV);
}

/* Setup a Solaris stack frame */
static inline void
setup_svr4_frame32(struct sigaction *sa, unsigned long pc, unsigned long npc,
		   struct pt_regs *regs, int signr, sigset_t *oldset)
{
	svr4_signal_frame_t *sfp;
	svr4_gregset_t  *gr;
	svr4_siginfo_t  *si;
	svr4_mcontext_t *mc;
	svr4_gwindows_t *gw;
	svr4_ucontext_t *uc;
	svr4_sigset_t setv;
#if 0	
	int window = 0;
#endif	
	unsigned psr;
	int i, err;

	synchronize_user_stack();
	save_and_clear_fpu();
	
	regs->u_regs[UREG_FP] &= 0x00000000ffffffffUL;
	sfp = (svr4_signal_frame_t *) get_sigframe(sa, regs, REGWIN_SZ + SVR4_SF_ALIGNED);

	if (invalid_frame_pointer (sfp, sizeof (*sfp))){
#ifdef DEBUG_SIGNALS
		printk ("Invalid stack frame\n");
#endif
		do_exit(SIGILL);
	}

	/* Start with a clean frame pointer and fill it */
	err = clear_user(sfp, sizeof (*sfp));

	/* Setup convenience variables */
	si = &sfp->si;
	uc = &sfp->uc;
	gw = &sfp->gw;
	mc = &uc->mcontext;
	gr = &mc->greg;
	
	/* FIXME: where am I supposed to put this?
	 * sc->sigc_onstack = old_status;
	 * anyways, it does not look like it is used for anything at all.
	 */
	setv.sigbits[0] = oldset->sig[0];
	setv.sigbits[1] = (oldset->sig[0] >> 32);
	if (_NSIG_WORDS >= 2) {
		setv.sigbits[2] = oldset->sig[1];
		setv.sigbits[3] = (oldset->sig[1] >> 32);
		err |= __copy_to_user(&uc->sigmask, &setv, sizeof(svr4_sigset_t));
	} else
		err |= __copy_to_user(&uc->sigmask, &setv, 2 * sizeof(unsigned));
	
	/* Store registers */
	err |= __put_user(regs->tpc, &((*gr) [SVR4_PC]));
	err |= __put_user(regs->tnpc, &((*gr) [SVR4_NPC]));
	psr = tstate_to_psr (regs->tstate);
	if(current->thread.fpsaved[0] & FPRS_FEF)
		psr |= PSR_EF;
	err |= __put_user(psr, &((*gr) [SVR4_PSR]));
	err |= __put_user(regs->y, &((*gr) [SVR4_Y]));
	
	/* Copy g [1..7] and o [0..7] registers */
	for (i = 0; i < 7; i++)
		err |= __put_user(regs->u_regs[UREG_G1+i], (&(*gr)[SVR4_G1])+i);
	for (i = 0; i < 8; i++)
		err |= __put_user(regs->u_regs[UREG_I0+i], (&(*gr)[SVR4_O0])+i);

	/* Setup sigaltstack */
	err |= __put_user(current->sas_ss_sp, &uc->stack.sp);
	err |= __put_user(sas_ss_flags(regs->u_regs[UREG_FP]), &uc->stack.flags);
	err |= __put_user(current->sas_ss_size, &uc->stack.size);

	/* Save the currently window file: */

	/* 1. Link sfp->uc->gwins to our windows */
	err |= __put_user((u32)(long)gw, &mc->gwin);
	    
	/* 2. Number of windows to restore at setcontext (): */
	err |= __put_user(current->thread.w_saved, &gw->count);

	/* 3. Save each valid window
	 *    Currently, it makes a copy of the windows from the kernel copy.
	 *    David's code for SunOS, makes the copy but keeps the pointer to
	 *    the kernel.  My version makes the pointer point to a userland 
	 *    copy of those.  Mhm, I wonder if I shouldn't just ignore those
	 *    on setcontext and use those that are on the kernel, the signal
	 *    handler should not be modyfing those, mhm.
	 *
	 *    These windows are just used in case synchronize_user_stack failed
	 *    to flush the user windows.
	 */
#if 0	 
	for(window = 0; window < current->thread.w_saved; window++) {
		err |= __put_user((int *) &(gw->win [window]),
				  (int **)gw->winptr +window );
		err |= copy_to_user(&gw->win [window],
				    &current->thread.reg_window [window],
				    sizeof (svr4_rwindow_t));
		err |= __put_user(0, (int *)gw->winptr + window);
	}
#endif	

	/* 4. We just pay attention to the gw->count field on setcontext */
	current->thread.w_saved = 0; /* So process is allowed to execute. */

	/* Setup the signal information.  Solaris expects a bunch of
	 * information to be passed to the signal handler, we don't provide
	 * that much currently, should use siginfo.
	 */
	err |= __put_user(signr, &si->siginfo.signo);
	err |= __put_user(SVR4_SINOINFO, &si->siginfo.code);
	if (err)
		goto sigsegv;

	regs->u_regs[UREG_FP] = (unsigned long) sfp;
	regs->tpc = (unsigned long) sa->sa_handler;
	regs->tnpc = (regs->tpc + 4);

#ifdef DEBUG_SIGNALS
	printk ("Solaris-frame: %x %x\n", (int) regs->tpc, (int) regs->tnpc);
#endif
	/* Arguments passed to signal handler */
	if (regs->u_regs [14]){
		struct reg_window32 *rw = (struct reg_window32 *)
			(regs->u_regs [14] & 0x00000000ffffffffUL);

		err |= __put_user(signr, &rw->ins [0]);
		err |= __put_user((u64)si, &rw->ins [1]);
		err |= __put_user((u64)uc, &rw->ins [2]);
		err |= __put_user((u64)sfp, &rw->ins [6]);	/* frame pointer */
		if (err)
			goto sigsegv;

		regs->u_regs[UREG_I0] = signr;
		regs->u_regs[UREG_I1] = (u32)(u64) si;
		regs->u_regs[UREG_I2] = (u32)(u64) uc;
	}
	return;

sigsegv:
	do_exit(SIGSEGV);
}

asmlinkage int
svr4_getcontext(svr4_ucontext_t *uc, struct pt_regs *regs)
{
	svr4_gregset_t  *gr;
	svr4_mcontext_t *mc;
	svr4_sigset_t setv;
	int i, err;

	synchronize_user_stack();
	save_and_clear_fpu();
	
	if (current->thread.w_saved){
		printk ("Uh oh, w_saved is not zero (%d)\n", (int) current->thread.w_saved);
		do_exit (SIGSEGV);
	}
	err = clear_user(uc, sizeof (*uc));

	/* Setup convenience variables */
	mc = &uc->mcontext;
	gr = &mc->greg;

	setv.sigbits[0] = current->blocked.sig[0];
	setv.sigbits[1] = (current->blocked.sig[0] >> 32);
	if (_NSIG_WORDS >= 2) {
		setv.sigbits[2] = current->blocked.sig[1];
		setv.sigbits[3] = (current->blocked.sig[1] >> 32);
		err |= __copy_to_user(&uc->sigmask, &setv, sizeof(svr4_sigset_t));
	} else
		err |= __copy_to_user(&uc->sigmask, &setv, 2 * sizeof(unsigned));

	/* Store registers */
	err |= __put_user(regs->tpc, &uc->mcontext.greg [SVR4_PC]);
	err |= __put_user(regs->tnpc, &uc->mcontext.greg [SVR4_NPC]);
#if 1
	err |= __put_user(0, &uc->mcontext.greg [SVR4_PSR]);
#else
	i = tstate_to_psr(regs->tstate) & ~PSR_EF;		   
	if (current->thread.fpsaved[0] & FPRS_FEF)
		i |= PSR_EF;
	err |= __put_user(i, &uc->mcontext.greg [SVR4_PSR]);
#endif
	err |= __put_user(regs->y, &uc->mcontext.greg [SVR4_Y]);
	
	/* Copy g [1..7] and o [0..7] registers */
	for (i = 0; i < 7; i++)
		err |= __put_user(regs->u_regs[UREG_G1+i], (&(*gr)[SVR4_G1])+i);
	for (i = 0; i < 8; i++)
		err |= __put_user(regs->u_regs[UREG_I0+i], (&(*gr)[SVR4_O0])+i);

	/* Setup sigaltstack */
	err |= __put_user(current->sas_ss_sp, &uc->stack.sp);
	err |= __put_user(sas_ss_flags(regs->u_regs[UREG_FP]), &uc->stack.flags);
	err |= __put_user(current->sas_ss_size, &uc->stack.size);

	/* The register file is not saved
	 * we have already stuffed all of it with sync_user_stack
	 */
	return (err ? -EFAULT : 0);
}


/* Set the context for a svr4 application, this is Solaris way to sigreturn */
asmlinkage int svr4_setcontext(svr4_ucontext_t *c, struct pt_regs *regs)
{
	struct thread_struct *tp = &current->thread;
	svr4_gregset_t  *gr;
	u32 pc, npc, psr;
	sigset_t set;
	svr4_sigset_t setv;
	int i, err;
	stack_t st;
	
	/* Fixme: restore windows, or is this already taken care of in
	 * svr4_setup_frame when sync_user_windows is done?
	 */
	flush_user_windows();
	
	if (tp->w_saved){
		printk ("Uh oh, w_saved is: 0x%x\n", tp->w_saved);
		goto sigsegv;
	}
	if (((unsigned long) c) & 3){
		printk ("Unaligned structure passed\n");
		goto sigsegv;
	}

	if(!__access_ok((unsigned long)c, sizeof(*c))) {
		/* Miguel, add nice debugging msg _here_. ;-) */
		goto sigsegv;
	}

	/* Check for valid PC and nPC */
	gr = &c->mcontext.greg;
	err = __get_user(pc, &((*gr)[SVR4_PC]));
	err |= __get_user(npc, &((*gr)[SVR4_NPC]));
	if((pc | npc) & 3) {
#ifdef DEBUG_SIGNALS	
	        printk ("setcontext, PC or nPC were bogus\n");
#endif
		goto sigsegv;
	}
	
	/* Retrieve information from passed ucontext */
	/* note that nPC is ored a 1, this is used to inform entry.S */
	/* that we don't want it to mess with our PC and nPC */
	
	err |= copy_from_user (&setv, &c->sigmask, sizeof(svr4_sigset_t));
	set.sig[0] = setv.sigbits[0] | (((long)setv.sigbits[1]) << 32);
	if (_NSIG_WORDS >= 2)
		set.sig[1] = setv.sigbits[2] | (((long)setv.sigbits[3]) << 32);
	
	err |= __get_user((long)st.ss_sp, &c->stack.sp);
	err |= __get_user(st.ss_flags, &c->stack.flags);
	err |= __get_user(st.ss_size, &c->stack.size);
	if (err)
		goto sigsegv;
		
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	do_sigaltstack(&st, NULL, regs->u_regs[UREG_I6]);
	
	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sigmask_lock);
	current->blocked = set;
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);
	regs->tpc = pc;
	regs->tnpc = npc | 1;
	err |= __get_user(regs->y, &((*gr) [SVR4_Y]));
	err |= __get_user(psr, &((*gr) [SVR4_PSR]));
	regs->tstate &= ~(TSTATE_ICC|TSTATE_XCC);
	regs->tstate |= psr_to_tstate_icc(psr);
#if 0	
	if(psr & PSR_EF)
		regs->tstate |= TSTATE_PEF;
#endif
	/* Restore g[1..7] and o[0..7] registers */
	for (i = 0; i < 7; i++)
		err |= __get_user(regs->u_regs[UREG_G1+i], (&(*gr)[SVR4_G1])+i);
	for (i = 0; i < 8; i++)
		err |= __get_user(regs->u_regs[UREG_I0+i], (&(*gr)[SVR4_O0])+i);
	if(err)
		goto sigsegv;

	return -EINTR;
sigsegv:
	do_exit(SIGSEGV);
}

static inline void setup_rt_frame32(struct k_sigaction *ka, struct pt_regs *regs,
				        unsigned long signr, sigset_t *oldset,
				        siginfo_t *info)
{
	struct rt_signal_frame32 *sf;
	int sigframe_size;
	u32 psr;
	int i, err;
	sigset_t32 seta;

	/* 1. Make sure everything is clean */
	synchronize_user_stack();
	save_and_clear_fpu();
	
	sigframe_size = RT_ALIGNEDSZ;
	if (!(current->thread.fpsaved[0] & FPRS_FEF))
		sigframe_size -= sizeof(__siginfo_fpu_t);

	sf = (struct rt_signal_frame32 *)get_sigframe(&ka->sa, regs, sigframe_size);
	
	if (invalid_frame_pointer (sf, sigframe_size)) {
#ifdef DEBUG_SIGNALS
		printk("rt_setup_frame32(%s:%d): invalid_frame_pointer(%p, %d)\n",
		       current->comm, current->pid, sf, sigframe_size);
#endif
		goto sigill;
	}

	if (current->thread.w_saved != 0) {
#ifdef DEBUG_SIGNALS
		printk ("%s[%d]: Invalid user stack frame for "
			"signal delivery.\n", current->comm, current->pid);
#endif
		goto sigill;
	}

	/* 2. Save the current process state */
	err  = put_user(regs->tpc, &sf->regs.pc);
	err |= __put_user(regs->tnpc, &sf->regs.npc);
	err |= __put_user(regs->y, &sf->regs.y);
	psr = tstate_to_psr (regs->tstate);
	if(current->thread.fpsaved[0] & FPRS_FEF)
		psr |= PSR_EF;
	err |= __put_user(psr, &sf->regs.psr);
	for (i = 0; i < 16; i++)
		err |= __put_user(regs->u_regs[i], &sf->regs.u_regs[i]);
	err |= __put_user(sizeof(siginfo_extra_v8plus_t), &sf->extra_size);
	err |= __put_user(SIGINFO_EXTRA_V8PLUS_MAGIC, &sf->v8plus.g_upper[0]);
	for (i = 1; i < 16; i++)
		err |= __put_user(((u32 *)regs->u_regs)[2*i], &sf->v8plus.g_upper[i]);

	if (psr & PSR_EF) {
		err |= save_fpu_state32(regs, &sf->fpu_state);
		err |= __put_user((u64)&sf->fpu_state, &sf->fpu_save);
	} else {
		err |= __put_user(0, &sf->fpu_save);
	}

	/* Update the siginfo structure.  */
	err |= copy_siginfo_to_user32(&sf->info, info);
	
	/* Setup sigaltstack */
	err |= __put_user(current->sas_ss_sp, &sf->stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->u_regs[UREG_FP]), &sf->stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &sf->stack.ss_size);

	switch (_NSIG_WORDS) {
	case 4: seta.sig[7] = (oldset->sig[3] >> 32);
		seta.sig[6] = oldset->sig[3];
	case 3: seta.sig[5] = (oldset->sig[2] >> 32);
		seta.sig[4] = oldset->sig[2];
	case 2: seta.sig[3] = (oldset->sig[1] >> 32);
		seta.sig[2] = oldset->sig[1];
	case 1: seta.sig[1] = (oldset->sig[0] >> 32);
		seta.sig[0] = oldset->sig[0];
	}
	err |= __copy_to_user(&sf->mask, &seta, sizeof(sigset_t32));

	err |= copy_in_user((u32 *)sf,
			    (u32 *)(regs->u_regs[UREG_FP]),
			    sizeof(struct reg_window32));
	if (err)
		goto sigsegv;
	
	/* 3. signal handler back-trampoline and parameters */
	regs->u_regs[UREG_FP] = (unsigned long) sf;
	regs->u_regs[UREG_I0] = signr;
	regs->u_regs[UREG_I1] = (unsigned long) &sf->info;

	/* 4. signal handler */
	regs->tpc = (unsigned long) ka->sa.sa_handler;
	regs->tnpc = (regs->tpc + 4);

	/* 5. return to kernel instructions */
	if (ka->ka_restorer)
		regs->u_regs[UREG_I7] = (unsigned long)ka->ka_restorer;
	else {
		/* Flush instruction space. */
		unsigned long address = ((unsigned long)&(sf->insns[0]));
		pgd_t *pgdp = pgd_offset(current->mm, address);
		pmd_t *pmdp = pmd_offset(pgdp, address);
		pte_t *ptep = pte_offset(pmdp, address);

		regs->u_regs[UREG_I7] = (unsigned long) (&(sf->insns[0]) - 2);
	
		/* mov __NR_rt_sigreturn, %g1 */
		err |= __put_user(0x82102065, &sf->insns[0]);

		/* t 0x10 */
		err |= __put_user(0x91d02010, &sf->insns[1]);
		if (err)
			goto sigsegv;

		if(pte_present(*ptep)) {
			unsigned long page = (unsigned long)
				__va(pte_pagenr(*ptep) << PAGE_SHIFT);

			__asm__ __volatile__("
			membar	#StoreStore
			flush	%0 + %1"
			: : "r" (page), "r" (address & (PAGE_SIZE - 1))
			: "memory");
		}
	}
	return;

sigill:
	do_exit(SIGILL);
sigsegv:
	do_exit(SIGSEGV);
}

static inline void handle_signal32(unsigned long signr, struct k_sigaction *ka,
				   siginfo_t *info,
				   sigset_t *oldset, struct pt_regs *regs,
				   int svr4_signal)
{
	if(svr4_signal)
		setup_svr4_frame32(&ka->sa, regs->tpc, regs->tnpc, regs, signr, oldset);
	else {
		if (ka->sa.sa_flags & SA_SIGINFO)
			setup_rt_frame32(ka, regs, signr, oldset, info);
		else if (current->thread.flags & SPARC_FLAG_NEWSIGNALS)
			new_setup_frame32(ka, regs, signr, oldset);
		else
			setup_frame32(&ka->sa, regs, signr, oldset, info);
	}
	if(ka->sa.sa_flags & SA_ONESHOT)
		ka->sa.sa_handler = SIG_DFL;
	if(!(ka->sa.sa_flags & SA_NOMASK)) {
		spin_lock_irq(&current->sigmask_lock);
		sigorsets(&current->blocked,&current->blocked,&ka->sa.sa_mask);
		sigaddset(&current->blocked,signr);
		recalc_sigpending(current);
		spin_unlock_irq(&current->sigmask_lock);
	}
}

static inline void syscall_restart32(unsigned long orig_i0, struct pt_regs *regs,
				     struct sigaction *sa)
{
	switch(regs->u_regs[UREG_I0]) {
		case ERESTARTNOHAND:
		no_system_call_restart:
			regs->u_regs[UREG_I0] = EINTR;
			regs->tstate |= TSTATE_ICARRY;
			break;
		case ERESTARTSYS:
			if(!(sa->sa_flags & SA_RESTART))
				goto no_system_call_restart;
		/* fallthrough */
		case ERESTARTNOINTR:
			regs->u_regs[UREG_I0] = orig_i0;
			regs->tpc -= 4;
			regs->tnpc -= 4;
	}
}

#ifdef DEBUG_SIGNALS_MAPS

#define MAPS_LINE_FORMAT	  "%016lx-%016lx %s %016lx %s %lu "

static inline void read_maps (void)
{
	struct vm_area_struct * map, * next;
	char * buffer;
	ssize_t i;

	buffer = (char*)__get_free_page(GFP_KERNEL);
	if (!buffer)
		return;

	for (map = current->mm->mmap ; map ; map = next ) {
		/* produce the next line */
		char *line;
		char str[5], *cp = str;
		int flags;
		kdev_t dev;
		unsigned long ino;

		/*
		 * Get the next vma now (but it won't be used if we sleep).
		 */
		next = map->vm_next;
		flags = map->vm_flags;

		*cp++ = flags & VM_READ ? 'r' : '-';
		*cp++ = flags & VM_WRITE ? 'w' : '-';
		*cp++ = flags & VM_EXEC ? 'x' : '-';
		*cp++ = flags & VM_MAYSHARE ? 's' : 'p';
		*cp++ = 0;

		dev = 0;
		ino = 0;
		if (map->vm_file != NULL) {
			dev = map->vm_file->f_dentry->d_inode->i_dev;
			ino = map->vm_file->f_dentry->d_inode->i_ino;
			line = d_path(map->vm_file->f_dentry,
				      map->vm_file->f_vfsmnt,
				      buffer, PAGE_SIZE);
		}
		printk(MAPS_LINE_FORMAT, map->vm_start, map->vm_end, str, map->vm_pgoff << PAGE_SHIFT,
			      kdevname(dev), ino);
		if (map->vm_file != NULL)
			printk("%s\n", line);
		else
			printk("\n");
	}
	free_page((unsigned long)buffer);
	return;
}

#endif

/* Note that 'init' is a special process: it doesn't get signals it doesn't
 * want to handle. Thus you cannot kill init even with a SIGKILL even by
 * mistake.
 */
asmlinkage int do_signal32(sigset_t *oldset, struct pt_regs * regs,
			   unsigned long orig_i0, int restart_syscall)
{
	unsigned long signr;
	struct k_sigaction *ka;
	siginfo_t info;
	
	int svr4_signal = current->personality == PER_SVR4;
	
	for (;;) {
		spin_lock_irq(&current->sigmask_lock);
		signr = dequeue_signal(&current->blocked, &info);
		spin_unlock_irq(&current->sigmask_lock);
		
		if (!signr) break;

		if ((current->flags & PF_PTRACED) && signr != SIGKILL) {
			current->exit_code = signr;
			current->state = TASK_STOPPED;
			notify_parent(current, SIGCHLD);
			schedule();
			if (!(signr = current->exit_code))
				continue;
			current->exit_code = 0;
			if (signr == SIGSTOP)
				continue;

			/* Update the siginfo structure.  Is this good?  */
			if (signr != info.si_signo) {
				info.si_signo = signr;
				info.si_errno = 0;
				info.si_code = SI_USER;
				info.si_pid = current->p_pptr->pid;
				info.si_uid = current->p_pptr->uid;
			}

			/* If the (new) signal is now blocked, requeue it.  */
			if (sigismember(&current->blocked, signr)) {
				send_sig_info(signr, &info, current);
				continue;
			}
		}
		
		ka = &current->sig->action[signr-1];
		
		if(ka->sa.sa_handler == SIG_IGN) {
			if(signr != SIGCHLD)
				continue;

			/* sys_wait4() grabs the master kernel lock, so
			 * we need not do so, that sucker should be
			 * threaded and would not be that difficult to
			 * do anyways.
			 */
			while(sys_wait4(-1, NULL, WNOHANG, NULL) > 0)
				;
			continue;
		}
		if(ka->sa.sa_handler == SIG_DFL) {
			unsigned long exit_code = signr;
			
			if(current->pid == 1)
				continue;
			switch(signr) {
			case SIGCONT: case SIGCHLD: case SIGWINCH:
				continue;

			case SIGTSTP: case SIGTTIN: case SIGTTOU:
				if (is_orphaned_pgrp(current->pgrp))
					continue;

			case SIGSTOP:
				if (current->flags & PF_PTRACED)
					continue;
				current->state = TASK_STOPPED;
				current->exit_code = signr;
				if(!(current->p_pptr->sig->action[SIGCHLD-1].sa.sa_flags &
				     SA_NOCLDSTOP))
					notify_parent(current, SIGCHLD);
				schedule();
				continue;

			case SIGQUIT: case SIGILL: case SIGTRAP:
			case SIGABRT: case SIGFPE: case SIGSEGV:
			case SIGBUS: case SIGSYS: case SIGXCPU: case SIGXFSZ:
				if (do_coredump(signr, regs))
					exit_code |= 0x80;
#ifdef DEBUG_SIGNALS
				/* Very useful to debug dynamic linker problems */
				printk ("Sig %ld going for %s[%d]...\n", signr, current->comm, current->pid);
				/* On SMP we are only interested in the current
				 * CPU's registers.
				 */
				__show_regs (regs);
#ifdef DEBUG_SIGNALS_TLB
				do {
					extern void sparc_ultra_dump_itlb(void);
					extern void sparc_ultra_dump_dtlb(void);
					sparc_ultra_dump_dtlb();
					sparc_ultra_dump_itlb();
				} while(0);
#endif
#ifdef DEBUG_SIGNALS_TRACE
				{
					struct reg_window32 *rw = (struct reg_window32 *)(regs->u_regs[UREG_FP] & 0xffffffff);
					unsigned int ins[8];

					while(rw &&
					      !(((unsigned long) rw) & 0x3)) {
						copy_from_user(ins, &rw->ins[0], sizeof(ins));
						printk("Caller[%08x](%08x,%08x,%08x,%08x,%08x,%08x)\n", ins[7], ins[0], ins[1], ins[2], ins[3], ins[4], ins[5]);
						rw = (struct reg_window32 *)(unsigned long)ins[6];
					}
				}
#endif			
#ifdef DEBUG_SIGNALS_MAPS	
				printk("Maps:\n");
				read_maps();
#endif
#endif
				/* fall through */
			default:
				lock_kernel();
				sigaddset(&current->signal, signr);
				recalc_sigpending(current);
				current->flags |= PF_SIGNALED;
				do_exit(exit_code);
				/* NOT REACHED */
			}
		}
		if(restart_syscall)
			syscall_restart32(orig_i0, regs, &ka->sa);
		handle_signal32(signr, ka, &info, oldset, regs, svr4_signal);
		return 1;
	}
	if(restart_syscall &&
	   (regs->u_regs[UREG_I0] == ERESTARTNOHAND ||
	    regs->u_regs[UREG_I0] == ERESTARTSYS ||
	    regs->u_regs[UREG_I0] == ERESTARTNOINTR)) {
		/* replay the system call when we are done */
		regs->u_regs[UREG_I0] = orig_i0;
		regs->tpc -= 4;
		regs->tnpc -= 4;
	}
	return 0;
}

struct sigstack32 {
	u32 the_stack;
	int cur_status;
};

asmlinkage int do_sys32_sigstack(u32 u_ssptr, u32 u_ossptr, unsigned long sp)
{
	struct sigstack32 *ssptr = (struct sigstack32 *)((unsigned long)(u_ssptr));
	struct sigstack32 *ossptr = (struct sigstack32 *)((unsigned long)(u_ossptr));
	int ret = -EFAULT;

	/* First see if old state is wanted. */
	if (ossptr) {
		if (put_user(current->sas_ss_sp + current->sas_ss_size, &ossptr->the_stack) ||
		    __put_user(on_sig_stack(sp), &ossptr->cur_status))
			goto out;
	}
	
	/* Now see if we want to update the new state. */
	if (ssptr) {
		void *ss_sp;

		if (get_user((long)ss_sp, &ssptr->the_stack))
			goto out;
		/* If the current stack was set with sigaltstack, don't
		   swap stacks while we are on it.  */
		ret = -EPERM;
		if (current->sas_ss_sp && on_sig_stack(sp))
			goto out;
			
		/* Since we don't know the extent of the stack, and we don't
		   track onstack-ness, but rather calculate it, we must
		   presume a size.  Ho hum this interface is lossy.  */
		current->sas_ss_sp = (unsigned long)ss_sp - SIGSTKSZ;
		current->sas_ss_size = SIGSTKSZ;
	}
	
	ret = 0;
out:
	return ret;
}

asmlinkage int do_sys32_sigaltstack(u32 ussa, u32 uossa, unsigned long sp)
{
	stack_t uss, uoss;
	int ret;
	mm_segment_t old_fs;
	
	if (ussa && (get_user((long)uss.ss_sp, &((stack_t32 *)(long)ussa)->ss_sp) ||
		    __get_user(uss.ss_flags, &((stack_t32 *)(long)ussa)->ss_flags) ||
		    __get_user(uss.ss_size, &((stack_t32 *)(long)ussa)->ss_size)))
		return -EFAULT;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	ret = do_sigaltstack(ussa ? &uss : NULL, uossa ? &uoss : NULL, sp);
	set_fs(old_fs);
	if (!ret && uossa && (put_user((long)uoss.ss_sp, &((stack_t32 *)(long)uossa)->ss_sp) ||
		    __put_user(uoss.ss_flags, &((stack_t32 *)(long)uossa)->ss_flags) ||
		    __put_user(uoss.ss_size, &((stack_t32 *)(long)uossa)->ss_size)))
		return -EFAULT;
	return ret;
}
