/* $Id: irq.h,v 1.13 1996/04/25 06:13:09 davem Exp $
 * irq.h: IRQ registers on the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_IRQ_H
#define _SPARC_IRQ_H

#include <linux/linkage.h>

#include <asm/system.h>     /* For NCPUS */

#define NR_IRQS    15

/* Dave Redman (djhr@tadpole.co.uk)
 * changed these to function pointers.. it saves cycles and will allow
 * the irq dependencies to be split into different files at a later date
 * sun4c_irq.c, sun4m_irq.c etc so we could reduce the kernel size.
 */
extern void (*disable_irq)(unsigned int);
extern void (*enable_irq)(unsigned int);
extern void (*clear_clock_irq)( void );
extern void (*clear_profile_irq)( void );
extern void (*load_profile_irq)( unsigned int timeout );
extern void (*init_timers)(void (*lvl10_irq)(int, void *, struct pt_regs *));
extern void claim_ticker14(void (*irq_handler)(int, void *, struct pt_regs *),
			   int irq,
			   unsigned int timeout);

#ifdef __SMP__
extern void (*set_cpu_int)(int, int);
extern void (*clear_cpu_int)(int, int);
extern void (*set_irq_udt)(int);
#endif

extern int request_fast_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *), unsigned long flags, const char *devname);

/* On the sun4m, just like the timers, we have both per-cpu and master
 * interrupt registers.
 */

/* These registers are used for sending/receiving irqs from/to
 * different cpu's.
 */
struct sun4m_intreg_percpu {
	unsigned int tbt;        /* Interrupts still pending for this cpu. */

	/* These next two registers are WRITE-ONLY and are only
	 * "on bit" sensitive, "off bits" written have NO affect.
	 */
	unsigned int clear;  /* Clear this cpus irqs here. */
	unsigned int set;    /* Set this cpus irqs here. */
	unsigned char space[PAGE_SIZE - 12];
};

/*
 * djhr
 * Actually the clear and set fields in this struct are misleading..
 * according to the SLAVIO manual (and the same applies for the SEC)
 * the clear field clears bits in the mask which will ENABLE that IRQ
 * the set field sets bits in the mask to DISABLE the IRQ.
 *
 * Also the undirected_xx address in the SLAVIO is defined as
 * RESERVED and write only..
 *
 * DAVEM_NOTE: The SLAVIO only specifies behavior on uniprocessor
 *             sun4m machines, for MP the layout makes more sense.
 */
struct sun4m_intregs {
	struct sun4m_intreg_percpu cpu_intregs[NCPUS];
	unsigned int tbt;                /* IRQ's that are still pending. */
	unsigned int irqs;               /* Master IRQ bits. */

	/* Again, like the above, two these registers are WRITE-ONLY. */
	unsigned int clear;              /* Clear master IRQ's by setting bits here. */
	unsigned int set;                /* Set master IRQ's by setting bits here. */

	/* This register is both READ and WRITE. */
	unsigned int undirected_target;  /* Which cpu gets undirected irqs. */
};

extern struct sun4m_intregs *sun4m_interrupts;

/* 
 * Bit field defines for the interrupt registers on various
 * Sparc machines.
 */

/* The sun4c interrupt register. */
#define SUN4C_INT_ENABLE  0x01     /* Allow interrupts. */
#define SUN4C_INT_E14     0x80     /* Enable level 14 IRQ. */
#define SUN4C_INT_E10     0x20     /* Enable level 10 IRQ. */
#define SUN4C_INT_E8      0x10     /* Enable level 8 IRQ. */
#define SUN4C_INT_E6      0x08     /* Enable level 6 IRQ. */
#define SUN4C_INT_E4      0x04     /* Enable level 4 IRQ. */
#define SUN4C_INT_E1      0x02     /* Enable level 1 IRQ. */

/* Dave Redman (djhr@tadpole.co.uk)
 * The sun4m interrupt registers.
 */
#define SUN4M_INT_ENABLE  	0x80000000
#define SUN4M_INT_E14     	0x00000080
#define SUN4M_INT_E10     	0x00080000

#define SUN4M_HARD_INT(x)	(0x000000001 << (x))
#define SUN4M_SOFT_INT(x)	(0x000010000 << (x))

#define	SUN4M_INT_MASKALL	0x80000000	  /* mask all interrupts */
#define	SUN4M_INT_MODULE_ERR	0x40000000	  /* module error */
#define	SUN4M_INT_M2S_WRITE	0x20000000	  /* write buffer error */
#define	SUN4M_INT_ECC		0x10000000	  /* ecc memory error */
#define	SUN4M_INT_FLOPPY	0x00400000	  /* floppy disk */
#define	SUN4M_INT_MODULE	0x00200000	  /* module interrupt */
#define	SUN4M_INT_VIDEO		0x00100000	  /* onboard video */
#define	SUN4M_INT_REALTIME	0x00080000	  /* system timer */
#define	SUN4M_INT_SCSI		0x00040000	  /* onboard scsi */
#define	SUN4M_INT_AUDIO		0x00020000	  /* audio/isdn */
#define	SUN4M_INT_ETHERNET	0x00010000	  /* onboard ethernet */
#define	SUN4M_INT_SERIAL	0x00008000	  /* serial ports */
#define	SUN4M_INT_KBDMS		0x00004000	  /* keyboard/mouse */
#define	SUN4M_INT_SBUSBITS	0x00003F80	  /* sbus int bits */

#define SUN4M_INT_SBUS(x)	(1 << (x+7))

#endif
