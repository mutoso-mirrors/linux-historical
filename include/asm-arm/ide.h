/*
 *  linux/include/asm-arm/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the i386 architecture specific IDE code.
 */

#ifndef __ASMARM_IDE_H
#define __ASMARM_IDE_H

#ifdef __KERNEL__

#ifndef MAX_HWIFS
#define MAX_HWIFS	4
#endif

#define ide__sti()	__sti()

#include <asm/arch/ide.h>

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned head		: 4;	/* always zeros here */
		unsigned unit		: 1;	/* drive select number, 0 or 1 */
		unsigned bit5		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit7		: 1;	/* always 1 */
	} b;
} select_t;

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned bit0		: 1;
		unsigned nIEN		: 1;	/* device INTRQ to host */
		unsigned SRST		: 1;	/* host soft reset bit */
		unsigned bit3		: 1;	/* ATA-2 thingy */
		unsigned reserved456	: 3;
		unsigned HOB		: 1;	/* 48-bit address ordering */
	} b;
} control_t;

/*
 * The following are not needed for the non-m68k ports
 */
#define ide_ack_intr(hwif)		(1)
#define ide_fix_driveid(id)		do {} while (0)
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

/*
 * We always use the new IDE port registering,
 * so these are fixed here.
 */
#define ide_default_io_base(i)		((ide_ioreg_t)0)
#define ide_default_irq(b)		(0)

#endif /* __KERNEL__ */

#endif /* __ASMARM_IDE_H */
