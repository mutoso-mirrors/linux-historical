/*
 *  arch/s390/kernel/signal32.c
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com)
 *               Gerhard Tonn (ton@de.ibm.com)                  
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 */

#include <linux/config.h>
#include <linux/compat.h>
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
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/lowcore.h>
#include "compat_linux.h"
#include "compat_ptrace.h"

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

typedef struct 
{
	__u8 callee_used_stack[__SIGNAL_FRAMESIZE32];
	struct sigcontext32 sc;
	_sigregs32 sregs;
	int signo;
	__u8 retcode[S390_SYSCALL_SIZE];
} sigframe32;

typedef struct 
{
	__u8 callee_used_stack[__SIGNAL_FRAMESIZE32];
	__u8 retcode[S390_SYSCALL_SIZE];
	struct siginfo32 info;
	struct ucontext32 uc;
} rt_sigframe32;

asmlinkage int FASTCALL(do_signal(struct pt_regs *regs, sigset_t *oldset));

int copy_siginfo_to_user32(siginfo_t32 __user *to, siginfo_t *from)
{
	int err;

	if (!access_ok (VERIFY_WRITE, to, sizeof(siginfo_t32)))
		return -EFAULT;

	/* If you change siginfo_t structure, please be sure
	   this code is fixed accordingly.
	   It should never copy any pad contained in the structure
	   to avoid security leaks, but must copy the generic
	   3 ints plus the relevant union member.  
	   This routine must convert siginfo from 64bit to 32bit as well
	   at the same time.  */
	err = __put_user(from->si_signo, &to->si_signo);
	err |= __put_user(from->si_errno, &to->si_errno);
	err |= __put_user((short)from->si_code, &to->si_code);
	if (from->si_code < 0)
		err |= __copy_to_user(&to->_sifields._pad, &from->_sifields._pad, SI_PAD_SIZE);
	else {
		switch (from->si_code >> 16) {
		case __SI_RT >> 16: /* This is not generated by the kernel as of now.  */
		case __SI_MESGQ >> 16:
			err |= __put_user(from->si_int, &to->si_int);
			/* fallthrough */
		case __SI_KILL >> 16:
			err |= __put_user(from->si_pid, &to->si_pid);
			err |= __put_user(from->si_uid, &to->si_uid);
			break;
		case __SI_CHLD >> 16:
			err |= __put_user(from->si_pid, &to->si_pid);
			err |= __put_user(from->si_uid, &to->si_uid);
			err |= __put_user(from->si_utime, &to->si_utime);
			err |= __put_user(from->si_stime, &to->si_stime);
			err |= __put_user(from->si_status, &to->si_status);
			break;
		case __SI_FAULT >> 16:
			err |= __put_user((unsigned long) from->si_addr,
					  &to->si_addr);
			break;
		case __SI_POLL >> 16:
		case __SI_TIMER >> 16:
			err |= __put_user(from->si_band, &to->si_band);
			err |= __put_user(from->si_fd, &to->si_fd);
			break;
		default:
			break;
		}
	}
	return err;
}

int copy_siginfo_from_user32(siginfo_t *to, siginfo_t32 __user *from)
{
	int err;
	u32 tmp;

	if (!access_ok (VERIFY_READ, from, sizeof(siginfo_t32)))
		return -EFAULT;

	err = __get_user(to->si_signo, &from->si_signo);
	err |= __get_user(to->si_errno, &from->si_errno);
	err |= __get_user(to->si_code, &from->si_code);

	if (from->si_code < 0)
		err |= __copy_from_user(&to->_sifields._pad, &from->_sifields._pad, SI_PAD_SIZE);
	else {
		switch (from->si_code >> 16) {
		case __SI_RT >> 16: /* This is not generated by the kernel as of now.  */
		case __SI_MESGQ >> 16:
			err |= __get_user(to->si_int, &from->si_int);
			/* fallthrough */
		case __SI_KILL >> 16:
			err |= __get_user(to->si_pid, &from->si_pid);
			err |= __get_user(to->si_uid, &from->si_uid);
			break;
		case __SI_CHLD >> 16:
			err |= __get_user(to->si_pid, &from->si_pid);
			err |= __get_user(to->si_uid, &from->si_uid);
			err |= __get_user(to->si_utime, &from->si_utime);
			err |= __get_user(to->si_stime, &from->si_stime);
			err |= __get_user(to->si_status, &from->si_status);
			break;
		case __SI_FAULT >> 16:
			err |= __get_user(tmp, &from->si_addr);
			to->si_addr = (void *)(u64) (tmp & PSW32_ADDR_INSN);
			break;
		case __SI_POLL >> 16:
		case __SI_TIMER >> 16:
			err |= __get_user(to->si_band, &from->si_band);
			err |= __get_user(to->si_fd, &from->si_fd);
			break;
		default:
			break;
		}
	}
	return err;
}

/*
 * Atomically swap in the new signal mask, and wait for a signal.
 */
asmlinkage int
sys32_sigsuspend(struct pt_regs * regs,int history0, int history1, old_sigset_t mask)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	regs->gprs[2] = -EINTR;

	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule();
		if (do_signal(regs, &saveset))
			return -EINTR;
	}
}

asmlinkage int
sys32_rt_sigsuspend(struct pt_regs * regs, compat_sigset_t __user *unewset,
								size_t sigsetsize)
{
	sigset_t saveset, newset;
	compat_sigset_t set32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(sigset_t))
		return -EINVAL;

	if (copy_from_user(&set32, unewset, sizeof(set32)))
		return -EFAULT;
	switch (_NSIG_WORDS) {
	case 4: newset.sig[3] = set32.sig[6] + (((long)set32.sig[7]) << 32);
	case 3: newset.sig[2] = set32.sig[4] + (((long)set32.sig[5]) << 32);
	case 2: newset.sig[1] = set32.sig[2] + (((long)set32.sig[3]) << 32);
	case 1: newset.sig[0] = set32.sig[0] + (((long)set32.sig[1]) << 32);
	}
        sigdelsetmask(&newset, ~_BLOCKABLE);

        spin_lock_irq(&current->sighand->siglock);
        saveset = current->blocked;
        current->blocked = newset;
        recalc_sigpending();
        spin_unlock_irq(&current->sighand->siglock);
        regs->gprs[2] = -EINTR;

        while (1) {
                set_current_state(TASK_INTERRUPTIBLE);
                schedule();
                if (do_signal(regs, &saveset))
                        return -EINTR;
        }
}

asmlinkage long
sys32_sigaction(int sig, const struct old_sigaction32 __user *act,
		 struct old_sigaction32 __user *oact)
{
        struct k_sigaction new_ka, old_ka;
        int ret;

        if (act) {
		compat_old_sigset_t mask;
		if (verify_area(VERIFY_READ, act, sizeof(*act)) ||
		    __get_user((unsigned long)new_ka.sa.sa_handler, &act->sa_handler) ||
		    __get_user((unsigned long)new_ka.sa.sa_restorer, &act->sa_restorer))
			return -EFAULT;
		__get_user(new_ka.sa.sa_flags, &act->sa_flags);
		__get_user(mask, &act->sa_mask);
		siginitset(&new_ka.sa.sa_mask, mask);
        }

        ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		if (verify_area(VERIFY_WRITE, oact, sizeof(*oact)) ||
		    __put_user((unsigned long)old_ka.sa.sa_handler, &oact->sa_handler) ||
		    __put_user((unsigned long)old_ka.sa.sa_restorer, &oact->sa_restorer))
			return -EFAULT;
		__put_user(old_ka.sa.sa_flags, &oact->sa_flags);
		__put_user(old_ka.sa.sa_mask.sig[0], &oact->sa_mask);
        }

	return ret;
}

int
do_sigaction(int sig, const struct k_sigaction *act, struct k_sigaction *oact);

asmlinkage long
sys32_rt_sigaction(int sig, const struct sigaction32 __user *act,
	   struct sigaction32 __user *oact,  size_t sigsetsize)
{
	struct k_sigaction new_ka, old_ka;
	int ret;
	compat_sigset_t set32;

	/* XXX: Don't preclude handling different sized sigset_t's.  */
	if (sigsetsize != sizeof(compat_sigset_t))
		return -EINVAL;

	if (act) {
		ret = get_user((unsigned long)new_ka.sa.sa_handler, &act->sa_handler);
		ret |= __copy_from_user(&set32, &act->sa_mask,
					sizeof(compat_sigset_t));
		switch (_NSIG_WORDS) {
		case 4: new_ka.sa.sa_mask.sig[3] = set32.sig[6]
				| (((long)set32.sig[7]) << 32);
		case 3: new_ka.sa.sa_mask.sig[2] = set32.sig[4]
				| (((long)set32.sig[5]) << 32);
		case 2: new_ka.sa.sa_mask.sig[1] = set32.sig[2]
				| (((long)set32.sig[3]) << 32);
		case 1: new_ka.sa.sa_mask.sig[0] = set32.sig[0]
				| (((long)set32.sig[1]) << 32);
		}
		ret |= __get_user(new_ka.sa.sa_flags, &act->sa_flags);
		
		if (ret)
			return -EFAULT;
	}

	ret = do_sigaction(sig, act ? &new_ka : NULL, oact ? &old_ka : NULL);

	if (!ret && oact) {
		switch (_NSIG_WORDS) {
		case 4:
			set32.sig[7] = (old_ka.sa.sa_mask.sig[3] >> 32);
			set32.sig[6] = old_ka.sa.sa_mask.sig[3];
		case 3:
			set32.sig[5] = (old_ka.sa.sa_mask.sig[2] >> 32);
			set32.sig[4] = old_ka.sa.sa_mask.sig[2];
		case 2:
			set32.sig[3] = (old_ka.sa.sa_mask.sig[1] >> 32);
			set32.sig[2] = old_ka.sa.sa_mask.sig[1];
		case 1:
			set32.sig[1] = (old_ka.sa.sa_mask.sig[0] >> 32);
			set32.sig[0] = old_ka.sa.sa_mask.sig[0];
		}
		ret = put_user((unsigned long)old_ka.sa.sa_handler, &oact->sa_handler);
		ret |= __copy_to_user(&oact->sa_mask, &set32,
				      sizeof(compat_sigset_t));
		ret |= __put_user(old_ka.sa.sa_flags, &oact->sa_flags);
	}

	return ret;
}

asmlinkage long
sys32_sigaltstack(const stack_t32 __user *uss, stack_t32 __user *uoss,
							struct pt_regs *regs)
{
	stack_t kss, koss;
	int ret, err = 0;
	mm_segment_t old_fs = get_fs();

	if (uss) {
		if (!access_ok(VERIFY_READ, uss, sizeof(*uss)))
			return -EFAULT;
		err |= __get_user((unsigned long) kss.ss_sp, &uss->ss_sp);
		err |= __get_user(kss.ss_size, &uss->ss_size);
		err |= __get_user(kss.ss_flags, &uss->ss_flags);
		if (err)
			return -EFAULT;
	}

	set_fs (KERNEL_DS);
	ret = do_sigaltstack((stack_t __user *) (uss ? &kss : NULL),
			     (stack_t __user *) (uoss ? &koss : NULL),
			     regs->gprs[15]);
	set_fs (old_fs);

	if (!ret && uoss) {
		if (!access_ok(VERIFY_WRITE, uoss, sizeof(*uoss)))
			return -EFAULT;
		err |= __put_user((unsigned long) koss.ss_sp, &uoss->ss_sp);
		err |= __put_user(koss.ss_size, &uoss->ss_size);
		err |= __put_user(koss.ss_flags, &uoss->ss_flags);
		if (err)
			return -EFAULT;
	}
	return ret;
}

static int save_sigregs32(struct pt_regs *regs, _sigregs32 __user *sregs)
{
	_s390_regs_common32 regs32;
	int err, i;

	regs32.psw.mask = PSW32_MASK_MERGE(PSW32_USER_BITS,
					   (__u32)(regs->psw.mask >> 32));
	regs32.psw.addr = PSW32_ADDR_AMODE31 | (__u32) regs->psw.addr;
	for (i = 0; i < NUM_GPRS; i++)
		regs32.gprs[i] = (__u32) regs->gprs[i];
	save_access_regs(current->thread.acrs);
	memcpy(regs32.acrs, current->thread.acrs, sizeof(regs32.acrs));
	err = __copy_to_user(&sregs->regs, &regs32, sizeof(regs32));
	if (err)
		return err;
	save_fp_regs(&current->thread.fp_regs);
	/* s390_fp_regs and _s390_fp_regs32 are the same ! */
	return __copy_to_user(&sregs->fpregs, &current->thread.fp_regs,
			      sizeof(_s390_fp_regs32));
}

static int restore_sigregs32(struct pt_regs *regs,_sigregs32 __user *sregs)
{
	_s390_regs_common32 regs32;
	int err, i;

	/* Alwys make any pending restarted system call return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

	err = __copy_from_user(&regs32, &sregs->regs, sizeof(regs32));
	if (err)
		return err;
	regs->psw.mask = PSW_MASK_MERGE(regs->psw.mask,
				        (__u64)regs32.psw.mask << 32);
	regs->psw.addr = (__u64)(regs32.psw.addr & PSW32_ADDR_INSN);
	for (i = 0; i < NUM_GPRS; i++)
		regs->gprs[i] = (__u64) regs32.gprs[i];
	memcpy(current->thread.acrs, regs32.acrs, sizeof(current->thread.acrs));
	restore_access_regs(current->thread.acrs);

	err = __copy_from_user(&current->thread.fp_regs, &sregs->fpregs,
			       sizeof(_s390_fp_regs32));
	current->thread.fp_regs.fpc &= FPC_VALID_MASK;
	if (err)
		return err;

	restore_fp_regs(&current->thread.fp_regs);
	regs->trap = -1;	/* disable syscall checks */
	return 0;
}

asmlinkage long sys32_sigreturn(struct pt_regs *regs)
{
	sigframe32 __user *frame = (sigframe32 __user *)regs->gprs[15];
	sigset_t set;

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set.sig, &frame->sc.oldmask, _SIGMASK_COPY_SIZE32))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigregs32(regs, &frame->sregs))
		goto badframe;

	return regs->gprs[2];

badframe:
	force_sig(SIGSEGV, current);
	return 0;
}

asmlinkage long sys32_rt_sigreturn(struct pt_regs *regs)
{
	rt_sigframe32 __user *frame = (rt_sigframe32 __user *)regs->gprs[15];
	sigset_t set;
	stack_t st;
	__u32 ss_sp;
	int err;
	mm_segment_t old_fs = get_fs();

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	if (restore_sigregs32(regs, &frame->uc.uc_mcontext))
		goto badframe;

	err = __get_user(ss_sp, &frame->uc.uc_stack.ss_sp);
	st.ss_sp = (void *) A((unsigned long)ss_sp);
	err |= __get_user(st.ss_size, &frame->uc.uc_stack.ss_size);
	err |= __get_user(st.ss_flags, &frame->uc.uc_stack.ss_flags);
	if (err)
		goto badframe; 

	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	set_fs (KERNEL_DS);
	do_sigaltstack((stack_t __user *)&st, NULL, regs->gprs[15]);
	set_fs (old_fs);

	return regs->gprs[2];

badframe:
        force_sig(SIGSEGV, current);
        return 0;
}	

/*
 * Set up a signal frame.
 */


/*
 * Determine which stack to use..
 */
static inline void __user *
get_sigframe(struct k_sigaction *ka, struct pt_regs * regs, size_t frame_size)
{
	unsigned long sp;

	/* Default to using normal stack */
	sp = (unsigned long) A(regs->gprs[15]);

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (! on_sig_stack(sp))
			sp = current->sas_ss_sp + current->sas_ss_size;
	}

	/* This is the legacy signal stack switching. */
	else if (!user_mode(regs) &&
		 !(ka->sa.sa_flags & SA_RESTORER) &&
		 ka->sa.sa_restorer) {
		sp = (unsigned long) ka->sa.sa_restorer;
	}

	return (void __user *)((sp - frame_size) & -8ul);
}

static inline int map_signal(int sig)
{
	if (current_thread_info()->exec_domain
	    && current_thread_info()->exec_domain->signal_invmap
	    && sig < 32)
		return current_thread_info()->exec_domain->signal_invmap[sig];
        else
		return sig;
}

static void setup_frame32(int sig, struct k_sigaction *ka,
			sigset_t *set, struct pt_regs * regs)
{
	sigframe32 __user *frame = get_sigframe(ka, regs, sizeof(sigframe32));
	if (!access_ok(VERIFY_WRITE, frame, sizeof(sigframe32)))
		goto give_sigsegv;

	if (__copy_to_user(&frame->sc.oldmask, &set->sig, _SIGMASK_COPY_SIZE32))
		goto give_sigsegv;

	if (save_sigregs32(regs, &frame->sregs))
		goto give_sigsegv;
	if (__put_user((unsigned long) &frame->sregs, &frame->sc.sregs))
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		regs->gprs[14] = (__u64) ka->sa.sa_restorer;
	} else {
		regs->gprs[14] = (__u64) frame->retcode;
		if (__put_user(S390_SYSCALL_OPCODE | __NR_sigreturn,
		               (u16 __user *)(frame->retcode)))
			goto give_sigsegv;
        }

	/* Set up backchain. */
	if (__put_user(regs->gprs[15], (unsigned int __user *) frame))
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->gprs[15] = (__u64) frame;
	regs->psw.addr = (__u64) ka->sa.sa_handler;

	regs->gprs[2] = map_signal(sig);
	regs->gprs[3] = (__u64) &frame->sc;

	/* We forgot to include these in the sigcontext.
	   To avoid breaking binary compatibility, they are passed as args. */
	regs->gprs[4] = current->thread.trap_no;
	regs->gprs[5] = current->thread.prot_addr;

	/* Place signal number on stack to allow backtrace from handler.  */
	if (__put_user(regs->gprs[2], (int __user *) &frame->signo))
		goto give_sigsegv;
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

static void setup_rt_frame32(int sig, struct k_sigaction *ka, siginfo_t *info,
			   sigset_t *set, struct pt_regs * regs)
{
	int err = 0;
	rt_sigframe32 __user *frame = get_sigframe(ka, regs, sizeof(rt_sigframe32));
	if (!access_ok(VERIFY_WRITE, frame, sizeof(rt_sigframe32)))
		goto give_sigsegv;

	if (copy_siginfo_to_user32(&frame->info, info))
		goto give_sigsegv;

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->gprs[15]),
	                  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= save_sigregs32(regs, &frame->uc.uc_mcontext);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	/* Set up to return from userspace.  If provided, use a stub
	   already in userspace.  */
	if (ka->sa.sa_flags & SA_RESTORER) {
		regs->gprs[14] = (__u64) ka->sa.sa_restorer;
	} else {
		regs->gprs[14] = (__u64) frame->retcode;
		err |= __put_user(S390_SYSCALL_OPCODE | __NR_rt_sigreturn,
		                  (u16 __user *)(frame->retcode));
	}

	/* Set up backchain. */
	if (__put_user(regs->gprs[15], (unsigned int __user *) frame))
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->gprs[15] = (__u64) frame;
	regs->psw.addr = (__u64) ka->sa.sa_handler;

	regs->gprs[2] = map_signal(sig);
	regs->gprs[3] = (__u64) &frame->info;
	regs->gprs[4] = (__u64) &frame->uc;
	return;

give_sigsegv:
	if (sig == SIGSEGV)
		ka->sa.sa_handler = SIG_DFL;
	force_sig(SIGSEGV, current);
}

/*
 * OK, we're invoking a handler
 */	

void
handle_signal32(unsigned long sig, siginfo_t *info, sigset_t *oldset,
	struct pt_regs * regs)
{
	struct k_sigaction *ka = &current->sighand->action[sig-1];

	/* Set up the stack frame */
	if (ka->sa.sa_flags & SA_SIGINFO)
		setup_rt_frame32(sig, ka, info, oldset, regs);
	else
		setup_frame32(sig, ka, oldset, regs);

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

