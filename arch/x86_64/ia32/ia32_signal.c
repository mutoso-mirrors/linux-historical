/*
 *  linux/arch/x86_64/ia32/ia32_signal.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  1997-11-28  Modified for POSIX.1b signals by Richard Henderson
 *  2000-06-20  Pentium III FXSR, SSE support by Gareth Hughes
 *  2000-12-*   x86-64 compatibility mode signal handling by Andi Kleen
 * 
 *  $Id: ia32_signal.c,v 1.22 2002/07/29 10:34:03 ak Exp $
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
#include <linux/personality.h>
#include <linux/compat.h>
#include <asm/ucontext.h>
#include <asm/uaccess.h>
#include <asm/i387.h>
#include <asm/ia32.h>
#include <asm/ptrace.h>
#include <asm/ia32_unistd.h>
#include <asm/user32.h>
#include <asm/sigcontext32.h>
#include <asm/fpu32.h>
#include <asm/proto.h>
#include <asm/vsyscall32.h>

#define ptr_to_u32(x) ((u32)(u64)(x))	/* avoid gcc warning */ 

#define DEBUG_SIG 0

#define _BLOCKABLE (~(sigmask(SIGKILL) | sigmask(SIGSTOP)))

asmlinkage int do_signal(struct pt_regs *regs, sigset_t *oldset);
void signal_fault(struct pt_regs *regs, void *frame, char *where);

static int ia32_copy_siginfo_to_user(siginfo_t32 *to, siginfo_t *from)
{
	if (!access_ok (VERIFY_WRITE, to, sizeof(siginfo_t)))
		return -EFAULT;
	if (from->si_code < 0) { 
		/* the only field that's different is the alignment
		   of the pointer in sigval_t. Move that 4 bytes down including
		   padding. */
		memmove(&((siginfo_t32 *)&from)->si_int,
			&from->si_int, 
			sizeof(siginfo_t) - offsetof(siginfo_t, si_int));
		/* last 4 bytes stay the same */
		return __copy_to_user(to, from, sizeof(siginfo_t32));
	} else {
		int err;

		/* If you change siginfo_t structure, please be sure
		   this code is fixed accordingly.
		   It should never copy any pad contained in the structure
		   to avoid security leaks, but must copy the generic
		   3 ints plus the relevant union member.  */
		err = __put_user(from->si_signo, &to->si_signo);
		err |= __put_user(from->si_errno, &to->si_errno);
		err |= __put_user(from->si_code, &to->si_code);
		/* First 32bits of unions are always present.  */
		err |= __put_user(from->si_pid, &to->si_pid);
		switch (from->si_code >> 16) {
		case __SI_FAULT >> 16:
			break;
		case __SI_CHLD >> 16:
			err |= __put_user(from->si_utime, &to->si_utime);
			err |= __put_user(from->si_stime, &to->si_stime);
			err |= __put_user(from->si_status, &to->si_status);
		default:
			err |= __put_user(from->si_uid, &to->si_uid);
			break;
		case __SI_POLL >> 16:
			err |= __put_user(from->si_band, &to->si_band); 
			err |= __put_user(from->si_fd, &to->si_fd); 
			break;
		/* case __SI_RT: This is not generated by the kernel as of now.  */
		}
		return err;
	}
}

asmlinkage long
sys32_sigsuspend(int history0, int history1, old_sigset_t mask, struct pt_regs regs)
{
	sigset_t saveset;

	mask &= _BLOCKABLE;
	spin_lock_irq(&current->sighand->siglock);
	saveset = current->blocked;
	siginitset(&current->blocked, mask);
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);

	regs.rax = -EINTR;
	while (1) {
		current->state = TASK_INTERRUPTIBLE;
		schedule();
		if (do_signal(&regs, &saveset))
			return -EINTR;
	}
}

asmlinkage long
sys32_sigaltstack(const stack_ia32_t *uss_ptr, stack_ia32_t *uoss_ptr, 
				  struct pt_regs regs)
{
	stack_t uss,uoss; 
	int ret;
	mm_segment_t seg; 
	if (uss_ptr) { 
		u32 ptr;
		memset(&uss,0,sizeof(stack_t));
	if (!access_ok(VERIFY_READ,uss_ptr,sizeof(stack_ia32_t)) ||
		    __get_user(ptr, &uss_ptr->ss_sp) ||
		    __get_user(uss.ss_flags, &uss_ptr->ss_flags) ||
		    __get_user(uss.ss_size, &uss_ptr->ss_size))
		return -EFAULT;
		uss.ss_sp = (void *)(u64)ptr;
	}
	seg = get_fs(); 
	set_fs(KERNEL_DS); 
	ret = do_sigaltstack(uss_ptr ? &uss : NULL, &uoss, regs.rsp);
	set_fs(seg); 
	if (ret >= 0 && uoss_ptr)  {
		if (!access_ok(VERIFY_WRITE,uoss_ptr,sizeof(stack_ia32_t)) ||
		    __put_user((u32)(u64)uss.ss_sp, &uoss_ptr->ss_sp) ||
		    __put_user(uss.ss_flags, &uoss_ptr->ss_flags) ||
		    __put_user(uss.ss_size, &uoss_ptr->ss_size))
			ret = -EFAULT;
	} 	
	return ret;	
}

/*
 * Do a signal return; undo the signal stack.
 */

struct sigframe
{
	u32 pretcode;
	int sig;
	struct sigcontext_ia32 sc;
	struct _fpstate_ia32 fpstate;
	unsigned int extramask[_COMPAT_NSIG_WORDS-1];
	char retcode[8];
};

struct rt_sigframe
{
	u32 pretcode;
	int sig;
	u32 pinfo;
	u32 puc;
	struct siginfo32 info;
	struct ucontext_ia32 uc;
	struct _fpstate_ia32 fpstate;
	char retcode[8];
};

static int
ia32_restore_sigcontext(struct pt_regs *regs, struct sigcontext_ia32 *sc, unsigned int *peax)
{
	unsigned int err = 0;
	
	/* Always make any pending restarted system calls return -EINTR */
	current_thread_info()->restart_block.fn = do_no_restart_syscall;

#if DEBUG_SIG
	printk("SIG restore_sigcontext: sc=%p err(%x) eip(%x) cs(%x) flg(%x)\n",
		sc, sc->err, sc->eip, sc->cs, sc->eflags);
#endif
#define COPY(x)		{ \
	unsigned int reg;	\
	err |= __get_user(reg, &sc->e ##x);	\
	regs->r ## x = reg;			\
}

#define RELOAD_SEG(seg,mask)						\
	{ unsigned int cur; 				\
	  unsigned short pre;				\
	  err |= __get_user(pre, &sc->seg);				\
    	  asm volatile("movl %%" #seg ",%0" : "=r" (cur));		\
	  pre |= mask; 							\
	  if (pre != cur) loadsegment(seg,pre); }

	/* Reload fs and gs if they have changed in the signal handler.
	   This does not handle long fs/gs base changes in the handler, but 
	   does not clobber them at least in the normal case. */ 
	
	{
		unsigned gs, oldgs; 
		err |= __get_user(gs, &sc->gs);
		gs |= 3; 
		asm("movl %%gs,%0" : "=r" (oldgs));
		if (gs != oldgs)
		load_gs_index(gs); 
	} 
	RELOAD_SEG(fs,3);
	RELOAD_SEG(ds,3);
	RELOAD_SEG(es,3);

	COPY(di); COPY(si); COPY(bp); COPY(sp); COPY(bx);
	COPY(dx); COPY(cx); COPY(ip);
	/* Don't touch extended registers */ 
	
	err |= __get_user(regs->cs, &sc->cs); 
	regs->cs |= 3;  
	err |= __get_user(regs->ss, &sc->ss); 
	regs->ss |= 3; 

	{
		unsigned int tmpflags;
		err |= __get_user(tmpflags, &sc->eflags);
		regs->eflags = (regs->eflags & ~0x40DD5) | (tmpflags & 0x40DD5);
		regs->orig_rax = -1;		/* disable syscall checks */
	}

	{
		u32 tmp;
		struct _fpstate_ia32 * buf;
		err |= __get_user(tmp, &sc->fpstate);
		buf = (struct _fpstate_ia32 *) (u64)tmp;
		if (buf) {
			if (verify_area(VERIFY_READ, buf, sizeof(*buf)))
				goto badframe;
			err |= restore_i387_ia32(current, buf, 0);
		}
	}

	{ 
		u32 tmp;
		err |= __get_user(tmp, &sc->eax);
		*peax = tmp;
	}
	return err;

badframe:
	return 1;
}

asmlinkage long sys32_sigreturn(struct pt_regs regs)
{
	struct sigframe *frame = (struct sigframe *)(regs.rsp - 8);
	sigset_t set;
	unsigned int eax;

	set_thread_flag(TIF_IRET);
	
	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__get_user(set.sig[0], &frame->sc.oldmask)
	    || (_COMPAT_NSIG_WORDS > 1
		&& __copy_from_user((((char *) &set.sig) + 4), &frame->extramask,
				    sizeof(frame->extramask))))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	if (ia32_restore_sigcontext(&regs, &frame->sc, &eax))
		goto badframe;
	return eax;

badframe:
	signal_fault(&regs, frame, "32bit sigreturn"); 
	return 0;
}	

asmlinkage long sys32_rt_sigreturn(struct pt_regs regs)
{
	struct rt_sigframe *frame = (struct rt_sigframe *)(regs.rsp - 4);
	sigset_t set;
	stack_t st;
	unsigned int eax;

	set_thread_flag(TIF_IRET);

	if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
		goto badframe;
	if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
		goto badframe;

	sigdelsetmask(&set, ~_BLOCKABLE);
	spin_lock_irq(&current->sighand->siglock);
	current->blocked = set;
	recalc_sigpending();
	spin_unlock_irq(&current->sighand->siglock);
	
	if (ia32_restore_sigcontext(&regs, &frame->uc.uc_mcontext, &eax))
		goto badframe;

	if (__copy_from_user(&st, &frame->uc.uc_stack, sizeof(st)))
		goto badframe;
	/* It is more difficult to avoid calling this function than to
	   call it and ignore errors.  */
	{
		mm_segment_t oldds = get_fs(); 
		set_fs(KERNEL_DS); 
		do_sigaltstack(&st, NULL, regs.rsp);
		set_fs(oldds);  
	}

	return eax;

badframe:
	signal_fault(&regs,frame,"32bit rt sigreturn");
	return 0;
}	

/*
 * Set up a signal frame.
 */

static int
ia32_setup_sigcontext(struct sigcontext_ia32 *sc, struct _fpstate_ia32 *fpstate,
		 struct pt_regs *regs, unsigned int mask)
{
	int tmp, err = 0;

	tmp = 0;
	__asm__("movl %%gs,%0" : "=r"(tmp): "0"(tmp));
	err |= __put_user(tmp, (unsigned int *)&sc->gs);
	__asm__("movl %%fs,%0" : "=r"(tmp): "0"(tmp));
	err |= __put_user(tmp, (unsigned int *)&sc->fs);
	__asm__("movl %%ds,%0" : "=r"(tmp): "0"(tmp));
	err |= __put_user(tmp, (unsigned int *)&sc->ds);
	__asm__("movl %%es,%0" : "=r"(tmp): "0"(tmp));
	err |= __put_user(tmp, (unsigned int *)&sc->es);

	err |= __put_user((u32)regs->rdi, &sc->edi);
	err |= __put_user((u32)regs->rsi, &sc->esi);
	err |= __put_user((u32)regs->rbp, &sc->ebp);
	err |= __put_user((u32)regs->rsp, &sc->esp);
	err |= __put_user((u32)regs->rbx, &sc->ebx);
	err |= __put_user((u32)regs->rdx, &sc->edx);
	err |= __put_user((u32)regs->rcx, &sc->ecx);
	err |= __put_user((u32)regs->rax, &sc->eax);
	err |= __put_user((u32)regs->cs, &sc->cs);
	err |= __put_user((u32)regs->ss, &sc->ss);
	err |= __put_user(current->thread.trap_no, &sc->trapno);
	err |= __put_user(current->thread.error_code, &sc->err);
	err |= __put_user((u32)regs->rip, &sc->eip);
	err |= __put_user((u32)regs->eflags, &sc->eflags);
	err |= __put_user((u32)regs->rsp, &sc->esp_at_signal);

	tmp = save_i387_ia32(current, fpstate, regs, 0);
	if (tmp < 0)
	  err = -EFAULT;
	else { 
		current->used_math = 0;
		stts();
	  err |= __put_user((u32)(u64)(tmp ? fpstate : NULL), &sc->fpstate);
	}

	/* non-iBCS2 extensions.. */
	err |= __put_user(mask, &sc->oldmask);
	err |= __put_user(current->thread.cr2, &sc->cr2);

	return err;
}

/*
 * Determine which stack to use..
 */
static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs * regs, size_t frame_size)
{
	unsigned long rsp;

	/* Default to using normal stack */
	rsp = regs->rsp;

	/* This is the X/Open sanctioned signal stack switching.  */
	if (ka->sa.sa_flags & SA_ONSTACK) {
		if (! on_sig_stack(rsp))
			rsp = current->sas_ss_sp + current->sas_ss_size;
	}

	/* This is the legacy signal stack switching. */
	else if ((regs->ss & 0xffff) != __USER_DS &&
		!(ka->sa.sa_flags & SA_RESTORER) &&
		 ka->sa.sa_restorer) {
		rsp = (unsigned long) ka->sa.sa_restorer;
	}

	return (void *)((rsp - frame_size) & -8UL);
}

void ia32_setup_frame(int sig, struct k_sigaction *ka,
			compat_sigset_t *set, struct pt_regs * regs)
{
	struct sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;

	{
		struct exec_domain *ed = current_thread_info()->exec_domain;
	err |= __put_user((ed
		           && ed->signal_invmap
		           && sig < 32
		           ? ed->signal_invmap[sig]
		           : sig),
		          &frame->sig);
	}
	if (err)
		goto give_sigsegv;

	err |= ia32_setup_sigcontext(&frame->sc, &frame->fpstate, regs, set->sig[0]);
	if (err)
		goto give_sigsegv;

	if (_COMPAT_NSIG_WORDS > 1) {
		err |= __copy_to_user(frame->extramask, &set->sig[1],
				      sizeof(frame->extramask));
	}
	if (err)
		goto give_sigsegv;

	/* Return stub is in 32bit vsyscall page */
	{ 
		void *restorer = VSYSCALL32_SIGRETURN; 
		if (ka->sa.sa_flags & SA_RESTORER)
			restorer = ka->sa.sa_restorer;       
		err |= __put_user(ptr_to_u32(restorer), &frame->pretcode);
	}
	/* These are actually not used anymore, but left because some 
	   gdb versions depend on them as a marker. */
	{ 
		/* copy_to_user optimizes that into a single 8 byte store */
		static const struct { 
			u16 poplmovl;
			u32 val;
			u16 int80;    
			u16 pad; 
		} __attribute__((packed)) code = { 
			0xb858,		 /* popl %eax ; movl $...,%eax */
			__NR_ia32_sigreturn,   
			0x80cd,		/* int $0x80 */
			0,
		}; 
		err |= __copy_to_user(frame->retcode, &code, 8); 
	}
	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->rsp = (unsigned long) frame;
	regs->rip = (unsigned long) ka->sa.sa_handler;

	asm volatile("movl %0,%%ds" :: "r" (__USER32_DS)); 
	asm volatile("movl %0,%%es" :: "r" (__USER32_DS)); 

	regs->cs = __USER32_CS; 
	regs->ss = __USER32_DS; 

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
	signal_fault(regs,frame,"32bit signal deliver");
}

void ia32_setup_rt_frame(int sig, struct k_sigaction *ka, siginfo_t *info,
			   compat_sigset_t *set, struct pt_regs * regs)
{
	struct rt_sigframe *frame;
	int err = 0;

	frame = get_sigframe(ka, regs, sizeof(*frame));

	if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
		goto give_sigsegv;


	{
		struct exec_domain *ed = current_thread_info()->exec_domain;
	err |= __put_user((ed
		    	   && ed->signal_invmap
		    	   && sig < 32
		    	   ? ed->signal_invmap[sig]
			   : sig),
			  &frame->sig);
	}
	err |= __put_user((u32)(u64)&frame->info, &frame->pinfo);
	err |= __put_user((u32)(u64)&frame->uc, &frame->puc);
	err |= ia32_copy_siginfo_to_user(&frame->info, info);
	if (err)
		goto give_sigsegv;

	/* Create the ucontext.  */
	err |= __put_user(0, &frame->uc.uc_flags);
	err |= __put_user(0, &frame->uc.uc_link);
	err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
	err |= __put_user(sas_ss_flags(regs->rsp),
			  &frame->uc.uc_stack.ss_flags);
	err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
	err |= ia32_setup_sigcontext(&frame->uc.uc_mcontext, &frame->fpstate,
			        regs, set->sig[0]);
	err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
	if (err)
		goto give_sigsegv;

	
	{ 
		void *restorer = VSYSCALL32_RTSIGRETURN; 
		if (ka->sa.sa_flags & SA_RESTORER)
			restorer = ka->sa.sa_restorer;       
		err |= __put_user(ptr_to_u32(restorer), &frame->pretcode);
	}

	/* This is movl $,%eax ; int $0x80 */
	/* Not actually used anymore, but left because some gdb versions
	   need it. */ 
	{ 
		/* __copy_to_user optimizes that into a single 8 byte store */
		static const struct { 
			u8 movl; 
			u32 val; 
			u16 int80; 
			u16 pad;
			u8  pad2;				
		} __attribute__((packed)) code = { 
			0xb8,
			__NR_ia32_rt_sigreturn,
			0x80cd,
			0,
		}; 
		err |= __copy_to_user(frame->retcode, &code, 8); 
	} 
	if (err)
		goto give_sigsegv;

	/* Set up registers for signal handler */
	regs->rsp = (unsigned long) frame;
	regs->rip = (unsigned long) ka->sa.sa_handler;

	asm volatile("movl %0,%%ds" :: "r" (__USER32_DS)); 
	asm volatile("movl %0,%%es" :: "r" (__USER32_DS)); 
	
	regs->cs = __USER32_CS; 
	regs->ss = __USER32_DS; 

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
	signal_fault(regs, frame, "32bit rt signal setup"); 
}

