/*
 * IA32 Architecture-specific signal handling support.
 *
 * Copyright (C) 1999 Hewlett-Packard Co
 * Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 Arun Sharma <arun.sharma@intel.com>
 * Copyright (C) 2000 VA Linux Co
 * Copyright (C) 2000 Don Dugger <n0ano@valinux.com>
 *
 * Derived from i386 and Alpha versions.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/wait.h>

#include <asm/uaccess.h>
#include <asm/rse.h>
#include <asm/sigcontext.h>
#include <asm/segment.h>
#include <asm/ia32.h>

#define DEBUG_SIG	0
#define _BLOCKABLE	(~(sigmask(SIGKILL) | sigmask(SIGSTOP)))


struct sigframe_ia32
{
       int pretcode;
       int sig;
       struct sigcontext_ia32 sc;
       struct _fpstate_ia32 fpstate;
       unsigned int extramask[_IA32_NSIG_WORDS-1];
       char retcode[8];
};

struct rt_sigframe_ia32
{
       int pretcode;
       int sig;
       int pinfo;
       int puc;
       struct siginfo info;
       struct ucontext_ia32 uc;
       struct _fpstate_ia32 fpstate;
       char retcode[8];
};

static int
setup_sigcontext_ia32(struct sigcontext_ia32 *sc, struct _fpstate_ia32 *fpstate,
                struct pt_regs *regs, unsigned long mask)
{
       int  err = 0;

       err |= __put_user((regs->r16 >> 32) & 0xffff , (unsigned int *)&sc->fs);
       err |= __put_user((regs->r16 >> 48) & 0xffff , (unsigned int *)&sc->gs);

       err |= __put_user((regs->r16 >> 56) & 0xffff, (unsigned int *)&sc->es);
       err |= __put_user(regs->r16 & 0xffff, (unsigned int *)&sc->ds);
       err |= __put_user(regs->r15, &sc->edi);
       err |= __put_user(regs->r14, &sc->esi);
       err |= __put_user(regs->r13, &sc->ebp);
       err |= __put_user(regs->r12, &sc->esp);
       err |= __put_user(regs->r11, &sc->ebx);
       err |= __put_user(regs->r10, &sc->edx);
       err |= __put_user(regs->r9, &sc->ecx);
       err |= __put_user(regs->r8, &sc->eax);
#if 0
       err |= __put_user(current->tss.trap_no, &sc->trapno);
       err |= __put_user(current->tss.error_code, &sc->err);
#endif
       err |= __put_user(regs->cr_iip, &sc->eip);
       err |= __put_user(regs->r17 & 0xffff, (unsigned int *)&sc->cs);
#if 0
       err |= __put_user(regs->eflags, &sc->eflags);
#endif
       
       err |= __put_user(regs->r12, &sc->esp_at_signal);
       err |= __put_user((regs->r17 >> 16) & 0xffff, (unsigned int *)&sc->ss);

#if 0
       tmp = save_i387(fpstate);
       if (tmp < 0)
         err = 1;
       else
         err |= __put_user(tmp ? fpstate : NULL, &sc->fpstate);

       /* non-iBCS2 extensions.. */
       err |= __put_user(mask, &sc->oldmask);
       err |= __put_user(current->tss.cr2, &sc->cr2);
#endif
       
       return err;
}

static int
restore_sigcontext_ia32(struct pt_regs *regs, struct sigcontext_ia32 *sc, int *peax)
{
       unsigned int err = 0;

#define COPY(ia64x, ia32x)             err |= __get_user(regs->ia64x, &sc->ia32x)

#define copyseg_gs(tmp)        (regs->r16 |= (unsigned long) tmp << 48)
#define copyseg_fs(tmp)        (regs->r16 |= (unsigned long) tmp << 32)
#define copyseg_cs(tmp)        (regs->r17 |= tmp)
#define copyseg_ss(tmp)        (regs->r17 |= (unsigned long) tmp << 16)
#define copyseg_es(tmp)        (regs->r16 |= (unsigned long) tmp << 16)
#define copyseg_ds(tmp)        (regs->r16 |= tmp)

#define COPY_SEG(seg)                                          \
       { unsigned short tmp;                                   \
         err |= __get_user(tmp, &sc->seg);                             \
         copyseg_##seg(tmp); }

#define COPY_SEG_STRICT(seg)                                   \
       { unsigned short tmp;                                   \
         err |= __get_user(tmp, &sc->seg);                             \
         copyseg_##seg(tmp|3); }

       /* To make COPY_SEGs easier, we zero r16, r17 */
       regs->r16 = 0;
       regs->r17 = 0;

       COPY_SEG(gs);
       COPY_SEG(fs);
       COPY_SEG(es);
       COPY_SEG(ds);
       COPY(r15, edi);
       COPY(r14, esi);
       COPY(r13, ebp);
       COPY(r12, esp);
       COPY(r11, ebx);
       COPY(r10, edx);
       COPY(r9, ecx);
       COPY(cr_iip, eip);
       COPY_SEG_STRICT(cs);
       COPY_SEG_STRICT(ss);
#if 0
       {
               unsigned int tmpflags;
               err |= __get_user(tmpflags, &sc->eflags);
               /* XXX: Change this to ar.eflags */
               regs->eflags = (regs->eflags & ~0x40DD5) | (tmpflags & 0x40DD5);
               regs->orig_eax = -1;            /* disable syscall checks */
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
#endif

       err |= __get_user(*peax, &sc->eax);
       return err;

#if 0       
badframe:
       return 1;
#endif

}

/*
 * Determine which stack to use..
 */
static inline void *
get_sigframe(struct k_sigaction *ka, struct pt_regs * regs, size_t frame_size)
{
       unsigned long esp;
       unsigned int xss;

       /* Default to using normal stack */
       esp = regs->r12;
       xss = regs->r16 >> 16;

       /* This is the X/Open sanctioned signal stack switching.  */
       if (ka->sa.sa_flags & SA_ONSTACK) {
               if (! on_sig_stack(esp))
                       esp = current->sas_ss_sp + current->sas_ss_size;
       }
       /* Legacy stack switching not supported */
       
       return (void *)((esp - frame_size) & -8ul);
}

static void
setup_frame_ia32(int sig, struct k_sigaction *ka, sigset_t *set,
           struct pt_regs * regs) 
{      
       struct sigframe_ia32 *frame;
       int err = 0;

       frame = get_sigframe(ka, regs, sizeof(*frame));

       if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
               goto give_sigsegv;

       err |= __put_user((current->exec_domain
                          && current->exec_domain->signal_invmap
                          && sig < 32
                          ? (int)(current->exec_domain->signal_invmap[sig])
                          : sig),
                         &frame->sig);

       err |= setup_sigcontext_ia32(&frame->sc, &frame->fpstate, regs, set->sig[0]);

       if (_NSIG_WORDS > 1) {
               err |= __copy_to_user(frame->extramask, &set->sig[1],
                                     sizeof(frame->extramask));
       }

       /* Set up to return from userspace.  If provided, use a stub
          already in userspace.  */
       err |= __put_user(frame->retcode, &frame->pretcode);
       /* This is popl %eax ; movl $,%eax ; int $0x80 */
       err |= __put_user(0xb858, (short *)(frame->retcode+0));
#define __IA32_NR_sigreturn            119
       err |= __put_user(__IA32_NR_sigreturn & 0xffff, (short *)(frame->retcode+2));
       err |= __put_user(__IA32_NR_sigreturn >> 16, (short *)(frame->retcode+4));
       err |= __put_user(0x80cd, (short *)(frame->retcode+6));

       if (err)
               goto give_sigsegv;

       /* Set up registers for signal handler */
       regs->r12 = (unsigned long) frame;
       regs->cr_iip = (unsigned long) ka->sa.sa_handler;

       set_fs(USER_DS);
       regs->r16 = (__USER_DS << 16) |  (__USER_DS); /* ES == DS, GS, FS are zero */
       regs->r17 = (__USER_DS << 16) | __USER_CS;

#if 0
       regs->eflags &= ~TF_MASK;
#endif

#if 1
       printk("SIG deliver (%s:%d): sp=%p pc=%lx ra=%x\n",
               current->comm, current->pid, frame, regs->cr_iip, frame->pretcode);
#endif

       return;

give_sigsegv:
       if (sig == SIGSEGV)
               ka->sa.sa_handler = SIG_DFL;
       force_sig(SIGSEGV, current);
}

static void
setup_rt_frame_ia32(int sig, struct k_sigaction *ka, siginfo_t *info,
              sigset_t *set, struct pt_regs * regs)
{
       struct rt_sigframe_ia32 *frame;
       int err = 0;

       frame = get_sigframe(ka, regs, sizeof(*frame));

       if (!access_ok(VERIFY_WRITE, frame, sizeof(*frame)))
               goto give_sigsegv;

       err |= __put_user((current->exec_domain
                          && current->exec_domain->signal_invmap
                          && sig < 32
                          ? current->exec_domain->signal_invmap[sig]
                          : sig),
                         &frame->sig);
       err |= __put_user(&frame->info, &frame->pinfo);
       err |= __put_user(&frame->uc, &frame->puc);
       err |= __copy_to_user(&frame->info, info, sizeof(*info));

       /* Create the ucontext.  */
       err |= __put_user(0, &frame->uc.uc_flags);
       err |= __put_user(0, &frame->uc.uc_link);
       err |= __put_user(current->sas_ss_sp, &frame->uc.uc_stack.ss_sp);
       err |= __put_user(sas_ss_flags(regs->r12),
                         &frame->uc.uc_stack.ss_flags);
       err |= __put_user(current->sas_ss_size, &frame->uc.uc_stack.ss_size);
       err |= setup_sigcontext_ia32(&frame->uc.uc_mcontext, &frame->fpstate,
                               regs, set->sig[0]);
       err |= __copy_to_user(&frame->uc.uc_sigmask, set, sizeof(*set));
       
       err |= __put_user(frame->retcode, &frame->pretcode);
       /* This is movl $,%eax ; int $0x80 */
       err |= __put_user(0xb8, (char *)(frame->retcode+0));
#define __IA32_NR_rt_sigreturn         173
       err |= __put_user(__IA32_NR_rt_sigreturn, (int *)(frame->retcode+1));
       err |= __put_user(0x80cd, (short *)(frame->retcode+5));

       if (err)
               goto give_sigsegv;

       /* Set up registers for signal handler */
       regs->r12 = (unsigned long) frame;
       regs->cr_iip = (unsigned long) ka->sa.sa_handler;

       set_fs(USER_DS);

       regs->r16 = (__USER_DS << 16) |  (__USER_DS); /* ES == DS, GS, FS are zero */
       regs->r17 = (__USER_DS << 16) | __USER_CS;

#if 0
       regs->eflags &= ~TF_MASK;
#endif

#if 1
       printk("SIG deliver (%s:%d): sp=%p pc=%lx ra=%x\n",
               current->comm, current->pid, frame, regs->cr_iip, frame->pretcode);
#endif

       return;

give_sigsegv:
       if (sig == SIGSEGV)
               ka->sa.sa_handler = SIG_DFL;
       force_sig(SIGSEGV, current);
}

long
ia32_setup_frame1 (int sig, struct k_sigaction *ka, siginfo_t *info,
		   sigset_t *set, struct pt_regs *regs)
{
       /* Set up the stack frame */
       if (ka->sa.sa_flags & SA_SIGINFO)
               setup_rt_frame_ia32(sig, ka, info, set, regs);
       else
               setup_frame_ia32(sig, ka, set, regs);

}

asmlinkage int
sys32_sigreturn(int arg1, int arg2, int arg3, int arg4, int arg5, unsigned long stack)
{
       struct pt_regs *regs = (struct pt_regs *) &stack;
       struct sigframe_ia32 *frame = (struct sigframe_ia32 *)(regs->r12- 8);
       sigset_t set;
       int eax;

       if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
               goto badframe;

       if (__get_user(set.sig[0], &frame->sc.oldmask)
           || (_IA32_NSIG_WORDS > 1
               && __copy_from_user((((char *) &set.sig) + 4),
                                   &frame->extramask,
                                   sizeof(frame->extramask))))
               goto badframe;

       sigdelsetmask(&set, ~_BLOCKABLE);
       spin_lock_irq(&current->sigmask_lock);
       current->blocked = (sigset_t) set;
       recalc_sigpending(current);
       spin_unlock_irq(&current->sigmask_lock);
       
       if (restore_sigcontext_ia32(regs, &frame->sc, &eax))
               goto badframe;
       return eax;

badframe:
       force_sig(SIGSEGV, current);
       return 0;
}      

asmlinkage int
sys32_rt_sigreturn(int arg1, int arg2, int arg3, int arg4, int arg5, unsigned long stack)
{
       struct pt_regs *regs = (struct pt_regs *) &stack;
       struct rt_sigframe_ia32 *frame = (struct rt_sigframe_ia32 *)(regs->r12 - 4);
       sigset_t set;
       stack_t st;
       int eax;

       if (verify_area(VERIFY_READ, frame, sizeof(*frame)))
               goto badframe;
       if (__copy_from_user(&set, &frame->uc.uc_sigmask, sizeof(set)))
               goto badframe;

       sigdelsetmask(&set, ~_BLOCKABLE);
       spin_lock_irq(&current->sigmask_lock);
       current->blocked =  set;
       recalc_sigpending(current);
       spin_unlock_irq(&current->sigmask_lock);
       
       if (restore_sigcontext_ia32(regs, &frame->uc.uc_mcontext, &eax))
               goto badframe;

       if (__copy_from_user(&st, &frame->uc.uc_stack, sizeof(st)))
               goto badframe;
       /* It is more difficult to avoid calling this function than to
          call it and ignore errors.  */
       do_sigaltstack(&st, NULL, regs->r12);

       return eax;

badframe:
       force_sig(SIGSEGV, current);
       return 0;
}      

