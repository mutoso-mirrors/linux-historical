/*
 * linux/include/asm-mips/ptrace.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 1995, 1996 by Ralf Baechle
 *
 * Machine dependent structs and defines to help the user use
 * the ptrace system call.
 */
#ifndef __ASM_MIPS_PTRACE_H
#define __ASM_MIPS_PTRACE_H

#include <linux/types.h>

#ifndef __ASSEMBLY__
/*
 * This struct defines the way the registers are stored on the stack during a
 * system call/exception. As usual the registers k0/k1 aren't being saved.
 */
struct pt_regs {
	/* Pad bytes for argument save space on the stack. */
	unsigned long pad0[6];

	/* Saved main processor registers. */
	unsigned long regs[32];

	/* Other saved registers. */
	unsigned long lo;
	unsigned long hi;
	unsigned long orig_reg2;
	unsigned long orig_reg7;

	/*
	 * saved cp0 registers
	 */
	unsigned long cp0_epc;
	unsigned long cp0_badvaddr;
	unsigned long cp0_status;
	unsigned long cp0_cause;
};

#endif /* !(__ASSEMBLY__) */

#include <asm/offset.h>

#ifdef __KERNEL__

#ifndef __ASSEMBLY__
/*
 * Does the process account for user or for system time?
 */
#define user_mode(regs) ((regs)->cp0_status & 0x10)

#define instruction_pointer(regs) ((regs)->cp0_epc)

extern void (*show_regs)(struct pt_regs *);
#endif /* !(__ASSEMBLY__) */

#endif

#endif /* __ASM_MIPS_PTRACE_H */
