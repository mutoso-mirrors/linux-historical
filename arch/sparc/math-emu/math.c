/*
 * arch/sparc/math-emu/math.c
 *
 * Copyright (C) 1998 Peter Maydell (pmaydell@chiark.greenend.org.uk)
 * Copyright (C) 1997, 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 * This is a good place to start if you're trying to understand the
 * emulation code, because it's pretty simple. What we do is
 * essentially analyse the instruction to work out what the operation
 * is and which registers are involved. We then execute the appropriate
 * FXXXX function. [The floating point queue introduces a minor wrinkle;
 * see below...]
 * The fxxxxx.c files each emulate a single insn. They look relatively
 * simple because the complexity is hidden away in an unholy tangle
 * of preprocessor macros.
 *
 * The first layer of macros is single.h, double.h, quad.h. Generally
 * these files define macros for working with floating point numbers
 * of the three IEEE formats. FP_ADD_D(R,A,B) is for adding doubles,
 * for instance. These macros are usually defined as calls to more
 * generic macros (in this case _FP_ADD(D,2,R,X,Y) where the number
 * of machine words required to store the given IEEE format is passed
 * as a parameter. [double.h and co check the number of bits in a word
 * and define FP_ADD_D & co appropriately].
 * The generic macros are defined in op-common.h. This is where all
 * the grotty stuff like handling NaNs is coded. To handle the possible
 * word sizes macros in op-common.h use macros like _FP_FRAC_SLL_##wc()
 * where wc is the 'number of machine words' parameter (here 2).
 * These are defined in the third layer of macros: op-1.h, op-2.h
 * and op-4.h. These handle operations on floating point numbers composed
 * of 1,2 and 4 machine words respectively. [For example, on sparc64
 * doubles are one machine word so macros in double.h eventually use
 * constructs in op-1.h, but on sparc32 they use op-2.h definitions.]
 * soft-fp.h is on the same level as op-common.h, and defines some
 * macros which are independent of both word size and FP format.
 * Finally, sfp-machine.h is the machine dependent part of the
 * code: it defines the word size and what type a word is. It also
 * defines how _FP_MUL_MEAT_t() maps to _FP_MUL_MEAT_n_* : op-n.h
 * provide several possible flavours of multiply algorithm, most
 * of which require that you supply some form of asm or C primitive to
 * do the actual multiply. (such asm primitives should be defined
 * in sfp-machine.h too). udivmodti4.c is the same sort of thing.
 *
 * There may be some errors here because I'm working from a
 * SPARC architecture manual V9, and what I really want is V8...
 * Also, the insns which can generate exceptions seem to be a
 * greater subset of the FPops than for V9 (for example, FCMPED
 * has to be emulated on V8). So I think I'm going to have
 * to emulate them all just to be on the safe side...
 *
 * Emulation routines originate from soft-fp package, which is
 * part of glibc and has appropriate copyrights in it (allegedly).
 *
 * NB: on sparc int == long == 4 bytes, long long == 8 bytes.
 * Most bits of the kernel seem to go for long rather than int,
 * so we follow that practice...
 */

/* TODO:
 * fpsave() saves the FP queue but fpload() doesn't reload it.
 * Therefore when we context switch or change FPU ownership
 * we have to check to see if the queue had anything in it and
 * emulate it if it did. This is going to be a pain.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <asm/uaccess.h>

#include "sfp-util.h"
#include "soft-fp.h"

#define FLOATFUNC(x) extern int x(void *,void *,void *)

/* The Vn labels indicate what version of the SPARC architecture gas thinks
 * each insn is. This is from the binutils source :->
 */
/* quadword instructions */
FLOATFUNC(FSQRTQ);                                /* v8 */
FLOATFUNC(FADDQ);                                 /* v8 */
FLOATFUNC(FSUBQ);                                 /* v8 */
FLOATFUNC(FMULQ);                                 /* v8 */
FLOATFUNC(FDIVQ);                                 /* v8 */
FLOATFUNC(FDMULQ);                                /* v8 */
FLOATFUNC(FQTOS);                                 /* v8 */
FLOATFUNC(FQTOD);                                 /* v8 */
FLOATFUNC(FITOQ);                                 /* v8 */
FLOATFUNC(FSTOQ);                                 /* v8 */
FLOATFUNC(FDTOQ);                                 /* v8 */
FLOATFUNC(FQTOI);                                 /* v8 */
FLOATFUNC(FCMPQ);                                 /* v8 */
FLOATFUNC(FCMPEQ);                                /* v8 */
/* single/double instructions (subnormal): should all work */
FLOATFUNC(FSQRTS);                                /* v7 */
FLOATFUNC(FSQRTD);                                /* v7 */
FLOATFUNC(FADDS);                                 /* v6 */
FLOATFUNC(FADDD);                                 /* v6 */
FLOATFUNC(FSUBS);                                 /* v6 */
FLOATFUNC(FSUBD);                                 /* v6 */
FLOATFUNC(FMULS);                                 /* v6 */
FLOATFUNC(FMULD);                                 /* v6 */
FLOATFUNC(FDIVS);                                 /* v6 */
FLOATFUNC(FDIVD);                                 /* v6 */
FLOATFUNC(FSMULD);                                /* v8 */
FLOATFUNC(FDTOS);                                 /* v6 */
FLOATFUNC(FSTOD);                                 /* v6 */
FLOATFUNC(FSTOI);                                 /* v6 */
FLOATFUNC(FDTOI);                                 /* v6 */
FLOATFUNC(FABSS);                                 /* v6 */
FLOATFUNC(FCMPS);                                 /* v6 */
FLOATFUNC(FCMPES);                                /* v6 */
FLOATFUNC(FCMPD);                                 /* v6 */
FLOATFUNC(FCMPED);                                /* v6 */
FLOATFUNC(FMOVS);                                 /* v6 */
FLOATFUNC(FNEGS);                                 /* v6 */
FLOATFUNC(FITOS);                                 /* v6 */
FLOATFUNC(FITOD);                                 /* v6 */

#define FSR_TEM_SHIFT	23UL
#define FSR_TEM_MASK	(0x1fUL << FSR_TEM_SHIFT)
#define FSR_AEXC_SHIFT	5UL
#define FSR_AEXC_MASK	(0x1fUL << FSR_AEXC_SHIFT)
#define FSR_CEXC_SHIFT	0UL
#define FSR_CEXC_MASK	(0x1fUL << FSR_CEXC_SHIFT)

static int do_one_mathemu(u32 insn, unsigned long *fsr, unsigned long *fregs);

/* Unlike the Sparc64 version (which has a struct fpustate), we
 * pass the taskstruct corresponding to the task which currently owns the
 * FPU. This is partly because we don't have the fpustate struct and
 * partly because the task owning the FPU isn't always current (as is
 * the case for the Sparc64 port). This is probably SMP-related...
 * This function returns 1 if all queued insns were emulated successfully.
 * The test for unimplemented FPop in kernel mode has been moved into
 * kernel/traps.c for simplicity.
 */
int do_mathemu(struct pt_regs *regs, struct task_struct *fpt)
{
	/* regs->pc isn't necessarily the PC at which the offending insn is sitting.
	 * The FPU maintains a queue of FPops which cause traps.
	 * When it hits an instruction that requires that the trapped op succeeded
	 * (usually because it reads a reg. that the trapped op wrote) then it
	 * causes this exception. We need to emulate all the insns on the queue
	 * and then allow the op to proceed.
	 * This code should also handle the case where the trap was precise,
	 * in which case the queue length is zero and regs->pc points at the
	 * single FPop to be emulated. (this case is untested, though :->)
	 * You'll need this case if you want to be able to emulate all FPops
	 * because the FPU either doesn't exist or has been software-disabled.
	 * [The UltraSPARC makes FP a precise trap; this isn't as stupid as it
	 * might sound because the Ultra does funky things with a superscalar
	 * architecture.]
	 */

	/* You wouldn't believe how often I typed 'ftp' when I meant 'fpt' :-> */

	int i;
	int retcode = 0;                               /* assume all succeed */
	unsigned long insn;

#ifdef DEBUG_MATHEMU
	printk("In do_mathemu()... pc is %08lx\n", regs->pc);
	printk("fpqdepth is %ld\n", fpt->tss.fpqdepth);
	for (i = 0; i < fpt->tss.fpqdepth; i++)
		printk("%d: %08lx at %08lx\n", i, fpt->tss.fpqueue[i].insn,
		       (unsigned long)fpt->tss.fpqueue[i].insn_addr);
#endif

	if (fpt->tss.fpqdepth == 0) {                   /* no queue, guilty insn is at regs->pc */
#ifdef DEBUG_MATHEMU
		printk("precise trap at %08lx\n", regs->pc);
#endif
		if (!get_user(insn, (u32 *)regs->pc)) {
			retcode = do_one_mathemu(insn, &fpt->tss.fsr, fpt->tss.float_regs);
			if (retcode) {
				/* in this case we need to fix up PC & nPC */
				regs->pc = regs->npc;
				regs->npc += 4;
			}
		}
		return retcode;
	}

	/* Normal case: need to empty the queue... */
	for (i = 0; i < fpt->tss.fpqdepth; i++) {
		retcode = do_one_mathemu(fpt->tss.fpqueue[i].insn, &(fpt->tss.fsr), fpt->tss.float_regs);
		if (!retcode)                               /* insn failed, no point doing any more */
			break;
	}
	/* Now empty the queue and clear the queue_not_empty flag */
	if(retcode)
		fpt->tss.fsr &= ~(0x3000 | FSR_CEXC_MASK);
	else
		fpt->tss.fsr &= ~0x3000;
	fpt->tss.fpqdepth = 0;

	return retcode;
}

/* All routines returning an exception to raise should detect
 * such exceptions _before_ rounding to be consistant with
 * the behavior of the hardware in the implemented cases
 * (and thus with the recommendations in the V9 architecture
 * manual).
 *
 * We return 0 if a SIGFPE should be sent, 1 otherwise.
 */
static int record_exception(unsigned long *pfsr, int eflag)
{
	unsigned long fsr = *pfsr;
	int would_trap;

	/* Determine if this exception would have generated a trap. */
	would_trap = (fsr & ((long)eflag << FSR_TEM_SHIFT)) != 0UL;

	/* If trapping, we only want to signal one bit. */
	if(would_trap != 0) {
		eflag &= ((fsr & FSR_TEM_MASK) >> FSR_TEM_SHIFT);
		if((eflag & (eflag - 1)) != 0) {
			if(eflag & FP_EX_INVALID)
				eflag = FP_EX_INVALID;
			else if(eflag & FP_EX_OVERFLOW)
				eflag = FP_EX_OVERFLOW;
			else if(eflag & FP_EX_UNDERFLOW)
				eflag = FP_EX_UNDERFLOW;
			else if(eflag & FP_EX_DIVZERO)
				eflag = FP_EX_DIVZERO;
			else if(eflag & FP_EX_INEXACT)
				eflag = FP_EX_INEXACT;
		}
	}

	/* Set CEXC, here is the rule:
	 *
	 *    In general all FPU ops will set one and only one
	 *    bit in the CEXC field, this is always the case
	 *    when the IEEE exception trap is enabled in TEM.
	 */
	fsr &= ~(FSR_CEXC_MASK);
	fsr |= ((long)eflag << FSR_CEXC_SHIFT);

	/* Set the AEXC field, rule is:
	 *
	 *    If a trap would not be generated, the
	 *    CEXC just generated is OR'd into the
	 *    existing value of AEXC.
	 */
	if(would_trap == 0)
		fsr |= ((long)eflag << FSR_AEXC_SHIFT);

	/* If trapping, indicate fault trap type IEEE. */
	if(would_trap != 0)
		fsr |= (1UL << 14);

	*pfsr = fsr;

	return (would_trap ? 0 : 1);
}

static int do_one_mathemu(u32 insn, unsigned long *fsr, unsigned long *fregs)
{
	/* Emulate the given insn, updating fsr and fregs appropriately. */
	int type = 0;
	/* 01 is single, 10 is double, 11 is quad,
	 * 000011 is rs1, 001100 is rs2, 110000 is rd (00 in rd is fcc)
	 * 111100000000 tells which ftt that may happen in
	 * (this field not used on sparc32 code, as we can't
	 * extract trap type info for ops on the FP queue)
	 */
	int freg, eflag;
	int (*func)(void *,void *,void *) = NULL;
	void *rs1 = NULL, *rs2 = NULL, *rd = NULL;

#ifdef DEBUG_MATHEMU
	printk("In do_mathemu(), emulating %08lx\n", insn);
#endif

	if ((insn & 0xc1f80000) == 0x81a00000)	/* FPOP1 */ {
		switch ((insn >> 5) & 0x1ff) {
		/* QUAD - ftt == 3 */
		case 0x001: type = 0x314; func = FMOVS; break;
		case 0x005: type = 0x314; func = FNEGS; break;
		case 0x009: type = 0x314; func = FABSS; break;
		case 0x02b: type = 0x33c; func = FSQRTQ; break;
		case 0x043: type = 0x33f; func = FADDQ; break;
		case 0x047: type = 0x33f; func = FSUBQ; break;
		case 0x04b: type = 0x33f; func = FMULQ; break;
		case 0x04f: type = 0x33f; func = FDIVQ; break;
		case 0x06e: type = 0x33a; func = FDMULQ; break;
		case 0x0c7: type = 0x31c; func = FQTOS; break;
		case 0x0cb: type = 0x32c; func = FQTOD; break;
		case 0x0cc: type = 0x334; func = FITOQ; break;
		case 0x0cd: type = 0x334; func = FSTOQ; break;
		case 0x0ce: type = 0x338; func = FDTOQ; break;
		case 0x0d3: type = 0x31c; func = FQTOI; break;
		/* SUBNORMAL - ftt == 2 */
		case 0x029: type = 0x214; func = FSQRTS; break;
		case 0x02a: type = 0x228; func = FSQRTD; break;
		case 0x041: type = 0x215; func = FADDS; break;
		case 0x042: type = 0x22a; func = FADDD; break;
		case 0x045: type = 0x215; func = FSUBS; break;
		case 0x046: type = 0x22a; func = FSUBD; break;
		case 0x049: type = 0x215; func = FMULS; break;
		case 0x04a: type = 0x22a; func = FMULD; break;
		case 0x04d: type = 0x215; func = FDIVS; break;
		case 0x04e: type = 0x22a; func = FDIVD; break;
		case 0x069: type = 0x225; func = FSMULD; break;
		case 0x0c6: type = 0x218; func = FDTOS; break;
		case 0x0c9: type = 0x224; func = FSTOD; break;
		case 0x0d1: type = 0x214; func = FSTOI; break;
		case 0x0d2: type = 0x218; func = FDTOI; break;
		default:
#ifdef DEBUG_MATHEMU
			printk("unknown FPop1: %03lx\n",(insn>>5)&0x1ff);
#endif
		}
	} else if ((insn & 0xc1f80000) == 0x81a80000)	/* FPOP2 */ {
		switch ((insn >> 5) & 0x1ff) {
		case 0x051: type = 0x305; func = FCMPS; break;
		case 0x052: type = 0x30a; func = FCMPD; break;
		case 0x053: type = 0x30f; func = FCMPQ; break;
		case 0x055: type = 0x305; func = FCMPES; break;
		case 0x056: type = 0x30a; func = FCMPED; break;
		case 0x057: type = 0x30f; func = FCMPEQ; break;
		default:
#ifdef DEBUG_MATHEMU
			printk("unknown FPop2: %03lx\n",(insn>>5)&0x1ff);
#endif
		}
	}

	if (!type) {	/* oops, didn't recognise that FPop */
		printk("attempt to emulate unrecognised FPop!\n");
		return 0;
	}

	/* Decode the registers to be used */
	freg = (*fsr >> 14) & 0xf;

	*fsr &= ~0x1c000;				/* clear the traptype bits */

	freg = ((insn >> 14) & 0x1f);
	switch (type & 0x3) {				/* is rs1 single, double or quad? */
	case 3:
		if (freg & 3) {				/* quadwords must have bits 4&5 of the */
							/* encoded reg. number set to zero. */
			*fsr |= (6 << 14);
			return 0;			/* simulate invalid_fp_register exception */
		}
	/* fall through */
	case 2:
		if (freg & 1) {				/* doublewords must have bit 5 zeroed */
			*fsr |= (6 << 14);
			return 0;
		}
	}
	rs1 = (void *)&fregs[freg];
	freg = (insn & 0x1f);
	switch ((type >> 2) & 0x3) {			/* same again for rs2 */
	case 3:
		if (freg & 3) {				/* quadwords must have bits 4&5 of the */
							/* encoded reg. number set to zero. */
			*fsr |= (6 << 14);
			return 0;			/* simulate invalid_fp_register exception */
		}
	/* fall through */
	case 2:
		if (freg & 1) {				/* doublewords must have bit 5 zeroed */
			*fsr |= (6 << 14);
			return 0;
		}
	}
	rs2 = (void *)&fregs[freg];
	freg = ((insn >> 25) & 0x1f);
	switch ((type >> 4) & 0x3) {			/* and finally rd. This one's a bit different */
	case 0:						/* dest is fcc. (this must be FCMPQ or FCMPEQ) */
		if (freg) {				/* V8 has only one set of condition codes, so */
							/* anything but 0 in the rd field is an error */
			*fsr |= (6 << 14);		/* (should probably flag as invalid opcode */
			return 0;			/* but SIGFPE will do :-> ) */
		}
		rd = (void *)(fsr);			/* FCMPQ and FCMPEQ are special and only  */
		break;					/* set bits they're supposed to :-> */
	case 3:
		if (freg & 3) {				/* quadwords must have bits 4&5 of the */
							/* encoded reg. number set to zero. */
			*fsr |= (6 << 14);
			return 0;			/* simulate invalid_fp_register exception */
		}
	/* fall through */
	case 2:
		if (freg & 1) {				/* doublewords must have bit 5 zeroed */
			*fsr |= (6 << 14);
			return 0;
		}
	/* fall through */
	case 1:
		rd = (void *)&fregs[freg];
		break;
	}
#ifdef DEBUG_MATHEMU
	printk("executing insn...\n");
#endif
	eflag = func(rd, rs2, rs1);			/* do the Right Thing */
	if(eflag == 0)
		return 1;				/* success! */
	return record_exception(fsr, eflag);
}
