#ifndef _PPC_PTRACE_H
#define _PPC_PTRACE_H

/*
 * This struct defines the way the registers are stored on the
 * kernel stack during a system call or other kernel entry.
 *
 * this should only contain volatile regs
 * since we can keep non-volatile in the thread_struct
 * should set this up when only volatiles are saved
 * by intr code.
 *
 * Since this is going on the stack, *CARE MUST BE TAKEN* to insure
 * that the overall structure is a multiple of 16 bytes in length.
 *
 * Note that the offsets of the fields in this struct correspond with
 * the PT_* values below.  This simplifies arch/ppc/kernel/ptrace.c.
 */

#ifndef __ASSEMBLY__
struct pt_regs {
	unsigned long gpr[32];
	unsigned long nip;
	unsigned long msr;
	unsigned long orig_gpr3;	/* Used for restarting system calls */
	unsigned long ctr;
	unsigned long link;
	unsigned long xer;
	unsigned long ccr;
	unsigned long mq;		/* 601 only (not used at present) */
					/* Used on APUS to hold IPL value. */
	unsigned long trap;		/* Reason for being here */
	/* N.B. for critical exceptions on 4xx, the dar and dsisr
	   fields are overloaded to hold srr0 and srr1. */
	unsigned long dar;		/* Fault registers */
	unsigned long dsisr;		/* on 4xx/Book-E used for ESR */
	unsigned long result; 		/* Result of a system call */
};

#endif /* __ASSEMBLY__ */

#ifdef __KERNEL__
#define STACK_FRAME_OVERHEAD	16	/* size of minimum stack frame */

/* Size of stack frame allocated when calling signal handler. */
#define __SIGNAL_FRAMESIZE	64

#ifndef __ASSEMBLY__
#define instruction_pointer(regs) ((regs)->nip)
#define profile_pc(regs) instruction_pointer(regs)
#define user_mode(regs) (((regs)->msr & MSR_PR) != 0)

#define force_successful_syscall_return()   \
	do { \
		current_thread_info()->local_flags |= _TIFL_FORCE_NOERROR; \
	} while(0)

/*
 * We use the least-significant bit of the trap field to indicate
 * whether we have saved the full set of registers, or only a
 * partial set.  A 1 there means the partial set.
 * On 4xx we use the next bit to indicate whether the exception
 * is a critical exception (1 means it is).
 */
#define FULL_REGS(regs)		(((regs)->trap & 1) == 0)
#define IS_CRITICAL_EXC(regs)	(((regs)->trap & 2) == 0)
#define TRAP(regs)		((regs)->trap & ~0xF)

#define CHECK_FULL_REGS(regs)						      \
do {									      \
	if ((regs)->trap & 1)						      \
		printk(KERN_CRIT "%s: partial register set\n", __FUNCTION__); \
} while (0)
#endif /* __ASSEMBLY__ */

#endif /* __KERNEL__ */

/*
 * Offsets used by 'ptrace' system call interface.
 * These can't be changed without breaking binary compatibility
 * with MkLinux, etc.
 */
#define PT_R0	0
#define PT_R1	1
#define PT_R2	2
#define PT_R3	3
#define PT_R4	4
#define PT_R5	5
#define PT_R6	6
#define PT_R7	7
#define PT_R8	8
#define PT_R9	9
#define PT_R10	10
#define PT_R11	11
#define PT_R12	12
#define PT_R13	13
#define PT_R14	14
#define PT_R15	15
#define PT_R16	16
#define PT_R17	17
#define PT_R18	18
#define PT_R19	19
#define PT_R20	20
#define PT_R21	21
#define PT_R22	22
#define PT_R23	23
#define PT_R24	24
#define PT_R25	25
#define PT_R26	26
#define PT_R27	27
#define PT_R28	28
#define PT_R29	29
#define PT_R30	30
#define PT_R31	31

#define PT_NIP	32
#define PT_MSR	33
#ifdef __KERNEL__
#define PT_ORIG_R3 34
#endif
#define PT_CTR	35
#define PT_LNK	36
#define PT_XER	37
#define PT_CCR	38
#define PT_MQ	39

#define PT_FPR0	48	/* each FP reg occupies 2 slots in this space */
#define PT_FPR31 (PT_FPR0 + 2*31)
#define PT_FPSCR (PT_FPR0 + 2*32 + 1)

/* Get/set all the altivec registers vr0..vr31, vscr, vrsave, in one go */
#define PTRACE_GETVRREGS	18
#define PTRACE_SETVRREGS	19

/* Get/set all the upper 32-bits of the SPE registers, accumulator, and
 * spefscr, in one go */
#define PTRACE_GETEVRREGS	20
#define PTRACE_SETEVRREGS	21

#endif
