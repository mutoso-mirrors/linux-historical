/*
 * Linux/MIPS specific definitions for signals.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997 by Ralf Baechle
 *
 * $Id: signal.h,v 1.8 1998/05/01 01:36:11 ralf Exp $
 */
#ifndef __ASM_MIPS_SIGNAL_H
#define __ASM_MIPS_SIGNAL_H

#include <linux/types.h>

#define _NSIG		128
#define _NSIG_BPW	32
#define _NSIG_WORDS	(_NSIG / _NSIG_BPW)

typedef struct {
	unsigned long sig[_NSIG_WORDS];
} sigset_t;

typedef unsigned long old_sigset_t;		/* at least 32 bits */

#define SIGHUP		 1	/* Hangup (POSIX).  */
#define SIGINT		 2	/* Interrupt (ANSI).  */
#define SIGQUIT		 3	/* Quit (POSIX).  */
#define SIGILL		 4	/* Illegal instruction (ANSI).  */
#define SIGTRAP		 5	/* Trace trap (POSIX).  */
#define SIGIOT		 6	/* IOT trap (4.2 BSD).  */
#define SIGABRT		 SIGIOT	/* Abort (ANSI).  */
#define SIGEMT		 7
#define SIGFPE		 8	/* Floating-point exception (ANSI).  */
#define SIGKILL		 9	/* Kill, unblockable (POSIX).  */
#define SIGBUS		10	/* BUS error (4.2 BSD).  */
#define SIGSEGV		11	/* Segmentation violation (ANSI).  */
#define SIGSYS		12
#define SIGPIPE		13	/* Broken pipe (POSIX).  */
#define SIGALRM		14	/* Alarm clock (POSIX).  */
#define SIGTERM		15	/* Termination (ANSI).  */
#define SIGUSR1		16	/* User-defined signal 1 (POSIX).  */
#define SIGUSR2		17	/* User-defined signal 2 (POSIX).  */
#define SIGCHLD		18	/* Child status has changed (POSIX).  */
#define SIGCLD		SIGCHLD	/* Same as SIGCHLD (System V).  */
#define SIGPWR		19	/* Power failure restart (System V).  */
#define SIGWINCH	20	/* Window size change (4.3 BSD, Sun).  */
#define SIGURG		21	/* Urgent condition on socket (4.2 BSD).  */
#define SIGIO		22	/* I/O now possible (4.2 BSD).  */
#define SIGPOLL		SIGIO	/* Pollable event occurred (System V).  */
#define SIGSTOP		23	/* Stop, unblockable (POSIX).  */
#define SIGTSTP		24	/* Keyboard stop (POSIX).  */
#define SIGCONT		25	/* Continue (POSIX).  */
#define SIGTTIN		26	/* Background read from tty (POSIX).  */
#define SIGTTOU		27	/* Background write to tty (POSIX).  */
#define SIGVTALRM	28	/* Virtual alarm clock (4.2 BSD).  */
#define SIGPROF		29	/* Profiling alarm clock (4.2 BSD).  */
#define SIGXCPU		30	/* CPU limit exceeded (4.2 BSD).  */
#define SIGXFSZ		31	/* File size limit exceeded (4.2 BSD).  */

/* These should not be considered constants from userland.  */
#define SIGRTMIN	32
#define SIGRTMAX	(_NSIG-1)

/*
 * SA_FLAGS values:
 *
 * SA_ONSTACK is not currently supported, but will allow sigaltstack(2).
 * SA_INTERRUPT is a no-op, but left due to historical reasons. Use the
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_NOCLDSTOP flag to turn off SIGCHLD when children stop.
 * SA_RESETHAND clears the handler when the signal is delivered.
 * SA_NOCLDWAIT flag on SIGCHLD to inhibit zombies.
 * SA_NODEFER prevents the current signal from being masked in the handler.
 *
 * SA_ONESHOT and SA_NOMASK are the historical Linux names for the Single
 * Unix names RESETHAND and NODEFER respectively.
 */
#define SA_STACK	0x00000001
#define SA_RESETHAND	0x00000002
#define SA_RESTART	0x00000004
#define SA_SIGINFO	0x00000008
#define SA_NODEFER	0x00000010
#define SA_NOCLDWAIT	0x00010000	/* Not supported yet */
#define SA_NOCLDSTOP	0x00020000

#define SA_NOMASK	SA_NODEFER	/* DANGER: was 0x02000000 */
#define SA_ONESHOT	SA_RESETHAND	/* DANGER: was 0x04000000 */

#ifdef __KERNEL__
/*
 * These values of sa_flags are used only by the kernel as part of the
 * irq handling routines.
 *
 * SA_INTERRUPT is a no-op, but left due to historical reasons. Use the
 * SA_RESTART flag to get restarting signals (which were the default long ago)
 * SA_SHIRQ flag is for shared interrupt support on PCI and EISA.
 */
#define SA_INTERRUPT		0x01000000	/* interrupt handling */
#define SA_SHIRQ		0x08000000
#define SA_PROBE		SA_ONESHOT
#define SA_SAMPLE_RANDOM	SA_RESTART
#endif /* __KERNEL__ */

#define SIG_BLOCK	1	/* for blocking signals */
#define SIG_UNBLOCK	2	/* for unblocking signals */
#define SIG_SETMASK	3	/* for setting the signal mask */
#define SIG_SETMASK32	256	/* Goodie from SGI for BSD compatibility:
				   set only the low 32 bit of the sigset.  */

/* Type of a signal handler.  */
typedef void (*__sighandler_t)(int);

/* Fake signal functions */
#define SIG_DFL	((__sighandler_t)0)	/* default signal handling */
#define SIG_IGN	((__sighandler_t)1)	/* ignore signal */
#define SIG_ERR	((__sighandler_t)-1)	/* error return from signal */

struct sigaction {
	unsigned int	sa_flags;
	__sighandler_t	sa_handler;
	sigset_t	sa_mask;
	int		sa_resv[2];	/* reserved */
};

/* XXX use sa_rev for storing ka_restorer */
struct k_sigaction {
	struct sigaction sa;
	void (*ka_restorer)(void);
};

#ifdef __KERNEL__
#include <asm/sigcontext.h>
#endif

#if defined (__KERNEL__) || defined (__USE_MISC)
/*
 * The following break codes are or were in use for specific purposes in
 * other MIPS operating systems.  Linux/MIPS doesn't use all of them.  The
 * unused ones are here as placeholders; we might encounter them in
 * non-Linux/MIPS object files or make use of them in the future.
 */
#define BRK_USERBP	0	/* User bp (used by debuggers) */
#define BRK_KERNELBP	1	/* Break in the kernel */
#define BRK_ABORT	2	/* Sometimes used by abort(3) to SIGIOT */
#define BRK_BD_TAKEN	3	/* For bd slot emulation - not implemented */
#define BRK_BD_NOTTAKEN	4	/* For bd slot emulation - not implemented */
#define BRK_SSTEPBP	5	/* User bp (used by debuggers) */
#define BRK_OVERFLOW	6	/* Overflow check */
#define BRK_DIVZERO	7	/* Divide by zero check */
#define BRK_RANGE	8	/* Range error check */
#define BRK_STACKOVERFLOW 9	/* For Ada stackchecking */
#define BRK_NORLD	10	/* No rld found - not used by Linux/MIPS */
#define _BRK_THREADBP	11	/* For threads, user bp (used by debuggers) */
#define BRK_MULOVF	1023	/* Multiply overflow */
#endif /* defined (__KERNEL__) || defined (__USE_MISC) */

#endif /* !defined (__ASM_MIPS_SIGNAL_H) */
