/*
 *  linux/include/asm-m68k/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */
 
/* Copyright(c) 1996 Kars de Jong */
/* Based on the ide driver from 1.2.13pl8 */

/*
 * Credits (alphabetical):
 *
 *  - Bjoern Brauel
 *  - Kars de Jong
 *  - Torsten Ebeling
 *  - Dwight Engen
 *  - Thorsten Floeck
 *  - Roman Hodek
 *  - Guenther Kelleter
 *  - Chris Lawrence
 *  - Michael Rausch
 *  - Christian Sauer
 *  - Michael Schmitz
 *  - Jes Soerensen
 *  - Michael Thurm
 *  - Geert Uytterhoeven
 */

#ifndef _M68K_IDE_H
#define _M68K_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>

#ifdef CONFIG_AMIGA
#include <asm/amigahw.h>
#include <asm/amihdreg.h>
#include <asm/amigaints.h>
#endif /* CONFIG_AMIGA */

#ifdef CONFIG_ATARI
#include <asm/atarihw.h>
#include <asm/atarihdreg.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>
#endif /* CONFIG_ATARI */

#include <asm/setup.h>
#include <asm/io.h>
#include <asm/irq.h>

typedef unsigned char * ide_ioreg_t;

#ifndef MAX_HWIFS
#define MAX_HWIFS	1
#endif

static __inline int ide_default_irq (ide_ioreg_t base)
{
	return 0;
}

static __inline__ ide_ioreg_t ide_default_io_base (int index)
{
	if (index)
		return NULL;
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA) {
		if (AMIGAHW_PRESENT(A4000_IDE)) {
			printk("Gayle IDE interface (A%d style)\n", 4000);
			return ((ide_ioreg_t)ZTWO_VADDR(HD_BASE_A4000));
		}
		if (AMIGAHW_PRESENT(A1200_IDE)) {
			printk("Gayle IDE interface (A%d style)\n", 1200);
			return ((ide_ioreg_t)ZTWO_VADDR(HD_BASE_A1200));
		}
	}
#endif /* CONFIG_AMIGA */
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		if (ATARIHW_PRESENT(IDE)) {
			printk("Falcon IDE interface\n");
			return ((ide_ioreg_t) ATA_HD_BASE);
		}
	}
#endif /* CONFIG_ATARI */
	return NULL;
}

static __inline__ void ide_init_hwif_ports (ide_ioreg_t *p, ide_ioreg_t base, int *irq)
{
	*p++ = base;
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA) {
		*p++ = base + AMI_HD_ERROR;
		*p++ = base + AMI_HD_NSECTOR;
		*p++ = base + AMI_HD_SECTOR;
		*p++ = base + AMI_HD_LCYL;
		*p++ = base + AMI_HD_HCYL;
		*p++ = base + AMI_HD_SELECT;
		*p++ = base + AMI_HD_STATUS;
		*p++ = base + AMI_HD_CMD;
		if (AMIGAHW_PRESENT(A4000_IDE))
			*p++ = (ide_ioreg_t) ZTWO_VADDR(HD_A4000_IRQ);
		else if (AMIGAHW_PRESENT(A1200_IDE))
			*p++ = (ide_ioreg_t) ZTWO_VADDR(HD_A1200_IRQ);
		if (irq != NULL)
			*irq = IRQ_AMIGA_PORTS;
	}
#endif /* CONFIG_AMIGA */
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		*p++ = base + ATA_HD_ERROR;
		*p++ = base + ATA_HD_NSECTOR;
		*p++ = base + ATA_HD_SECTOR;
		*p++ = base + ATA_HD_LCYL;
		*p++ = base + ATA_HD_HCYL;
		*p++ = base + ATA_HD_CURRENT;
		*p++ = base + ATA_HD_STATUS;
		*p++ = base + ATA_HD_CMD;
		if (irq != NULL)
			*irq = IRQ_MFP_IDE;
	}
#endif /* CONFIG_ATARI */
}

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned bit7		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit5		: 1;	/* always 1 */
		unsigned unit		: 1;	/* drive select number, 0 or 1 */
		unsigned head		: 4;	/* always zeros here */
	} b;
	} select_t;

static __inline__ int ide_request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
			unsigned long flags, const char *device, void *dev_id)
{
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA)
		return request_irq(irq, handler, 0, device, dev_id);
#endif /* CONFIG_AMIGA */
	return 0;
}

static __inline__ void ide_free_irq(unsigned int irq, void *dev_id)
{
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA)
		free_irq(irq, dev_id);
#endif /* CONFIG_AMIGA */
}

/*
 * We should really implement those some day.
 */
static __inline__ int ide_check_region (ide_ioreg_t from, unsigned int extent)
{
	return 0;
}

static __inline__ void ide_request_region (ide_ioreg_t from, unsigned int extent, const char *name)
{
}

static __inline__ void ide_release_region (ide_ioreg_t from, unsigned int extent)
{
}

#undef SUPPORT_SLOW_DATA_PORTS
#define SUPPORT_SLOW_DATA_PORTS 0

#undef SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC 0

#undef HD_DATA
#define HD_DATA NULL

#define insl(data_reg, buffer, wcount) insw(data_reg, buffer, (wcount)<<1)
#define outsl(data_reg, buffer, wcount) outsw(data_reg, buffer, (wcount)<<1)

#define insw(port, buf, nr) \
    if ((nr) % 16) \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a0@,%/a1@+; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "a0", "a1", "d6"); \
    else \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 lsrl  #4,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 movew %/a0@,%/a1@+; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "a0", "a1", "d6")

#define outsw(port, buf, nr) \
    if ((nr) % 16) \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a1@+,%/a0@; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "a0", "a1", "d6"); \
    else \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 lsrl  #4,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 movew %/a1@+,%/a0@; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "a0", "a1", "d6")

#ifdef CONFIG_ATARI
#define insl_swapw(data_reg, buffer, wcount) \
    insw_swapw(data_reg, buffer, (wcount)<<1)
#define outsl_swapw(data_reg, buffer, wcount) \
    outsw_swapw(data_reg, buffer, (wcount)<<1)

#define insw_swapw(port, buf, nr) \
    if ((nr) % 8) \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "d0", "a0", "a1", "d6"); \
    else \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 lsrl  #3,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 movew %/a0@,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a1@+; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "d0", "a0", "a1", "d6")

#define outsw_swapw(port, buf, nr) \
    if ((nr) % 8) \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "d0", "a0", "a1", "d6"); \
    else \
	__asm__ __volatile__ \
	       ("movel %0,%/a0; \
		 movel %1,%/a1; \
		 movel %2,%/d6; \
		 lsrl  #3,%/d6; \
		 subql #1,%/d6; \
	       1:movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 movew %/a1@+,%/d0; \
		 rolw  #8,%/d0; \
		 movew %/d0,%/a0@; \
		 dbra %/d6,1b" : \
		: "g" (port), "g" (buf), "g" (nr) \
		: "d0", "a0", "a1", "d6")

#endif /* CONFIG_ATARI */

static __inline__ int ide_ack_intr (ide_ioreg_t status_port, ide_ioreg_t irq_port)
{
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA) {
		unsigned char ch;
		ch = inb(irq_port);
		if (!(ch & 0x80))
			return(0);
		if (AMIGAHW_PRESENT(A1200_IDE)) {
			(void) inb(status_port);
			outb(0x7c | (ch & 0x03), irq_port);
		}
	}
#endif /* CONFIG_AMIGA */
	return(1);
}

#define T_CHAR          (0x0000)        /* char:  don't touch  */
#define T_SHORT         (0x4000)        /* short: 12 -> 21     */
#define T_INT           (0x8000)        /* int:   1234 -> 4321 */
#define T_TEXT          (0xc000)        /* text:  12 -> 21     */

#define T_MASK_TYPE     (0xc000)
#define T_MASK_COUNT    (0x3fff)

#define D_CHAR(cnt)     (T_CHAR  | (cnt))
#define D_SHORT(cnt)    (T_SHORT | (cnt))
#define D_INT(cnt)      (T_INT   | (cnt))
#define D_TEXT(cnt)     (T_TEXT  | (cnt))

#ifdef CONFIG_AMIGA
static u_short driveid_types[] = {
	D_SHORT(10),	/* config - vendor2 */
	D_TEXT(20),	/* serial_no */
	D_SHORT(3),	/* buf_type - ecc_bytes */
	D_TEXT(48),	/* fw_rev - model */
	D_CHAR(2),	/* max_multsect - vendor3 */
	D_SHORT(1),	/* dword_io */
	D_CHAR(2),	/* vendor4 - capability */
	D_SHORT(1),	/* reserved50 */
	D_CHAR(4),	/* vendor5 - tDMA */
	D_SHORT(4),	/* field_valid - cur_sectors */
	D_INT(1),	/* cur_capacity */
	D_CHAR(2),	/* multsect - multsect_valid */
	D_INT(1),	/* lba_capacity */
	D_SHORT(194)	/* dma_1word - reservedyy */
};

#define num_driveid_types       (sizeof(driveid_types)/sizeof(*driveid_types))
#endif /* CONFIG_AMIGA */

static __inline__ void ide_fix_driveid(struct hd_driveid *id)
{
#ifdef CONFIG_AMIGA
   u_char *p = (u_char *)id;
   int i, j, cnt;
   u_char t;

   if (!MACH_IS_AMIGA)
   	return;
   for (i = 0; i < num_driveid_types; i++) {
      cnt = driveid_types[i] & T_MASK_COUNT;
      switch (driveid_types[i] & T_MASK_TYPE) {
         case T_CHAR:
            p += cnt;
            break;
         case T_SHORT:
            for (j = 0; j < cnt; j++) {
               t = p[0];
               p[0] = p[1];
               p[1] = t;
               p += 2;
            }
            break;
         case T_INT:
            for (j = 0; j < cnt; j++) {
               t = p[0];
               p[0] = p[3];
               p[3] = t;
               t = p[1];
               p[1] = p[2];
               p[2] = t;
               p += 4;
            }
            break;
         case T_TEXT:
            for (j = 0; j < cnt; j += 2) {
               t = p[0];
               p[0] = p[1];
               p[1] = t;
               p += 2;
            }
            break;
      }
   }
#endif /* CONFIG_AMIGA */
}

static __inline__ void ide_release_lock (int *ide_lock)
{
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		if (*ide_lock == 0) {
			printk("ide_release_lock: bug\n");
			return;
		}
		*ide_lock = 0;
		stdma_release();
	}
#endif /* CONFIG_ATARI */
}

static __inline__ void ide_get_lock (int *ide_lock, void (*handler)(int, void *, struct pt_regs *), void *data)
{
#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI) {
		if (*ide_lock == 0) {
			if (intr_count > 0)
				panic( "Falcon IDE hasn't ST-DMA lock in interrupt" );
			stdma_lock(handler, data);
			*ide_lock = 1;
		}
	}
#endif /* CONFIG_ATARI */
}

/*
 * On the Atari, we sometimes can't enable interrupts:
 */

/* MSch: changed sti() to STI() wherever possible in ide.c; moved STI() def. 
 * to asm/ide.h 
 */
/* The Atari interrupt structure strictly requires that the IPL isn't lowered
 * uncontrolled in an interrupt handler. In the concrete case, the IDE
 * interrupt is already a slow int, so the irq is already disabled at the time
 * the handler is called, and the IPL has been lowered to the minimum value
 * possible. To avoid going below that, STI() checks for being called inside
 * an interrupt, and in that case it does nothing. Hope that is reasonable and
 * works. (Roman)
 */
#if defined(CONFIG_ATARI) && !defined(CONFIG_AMIGA)
#define	ide_sti()					\
    do {						\
	if (!intr_count) sti();				\
    } while(0)
#elif defined(CONFIG_ATARI)
#define	ide_sti()					\
    do {						\
	if (!MACH_IS_ATARI || !intr_count) sti();	\
    } while(0)
#else /* !defined(CONFIG_ATARI) */
#define	ide_sti()	sti()
#endif

#endif /* __KERNEL__ */

#endif /* _M68K_IDE_H */
