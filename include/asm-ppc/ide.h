/*
 *  linux/include/asm-ppc/ide.h
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors
 */

/*
 *  This file contains the ppc architecture specific IDE code.
 */

#ifndef __ASMPPC_IDE_H
#define __ASMPPC_IDE_H

#include <linux/sched.h>
#include <asm/processor.h>

#ifndef MAX_HWIFS
#define MAX_HWIFS	4
#endif

#include <asm/hdreg.h>

#ifdef __KERNEL__

#include <linux/hdreg.h>
#include <linux/ioport.h>
#include <asm/io.h>

extern int pmac_ide_ports_known;
extern ide_ioreg_t pmac_ide_regbase[MAX_HWIFS];
extern int pmac_ide_irq[MAX_HWIFS];
extern void pmac_ide_probe(void);

extern int chrp_ide_ports_known;
extern ide_ioreg_t chrp_ide_regbase[MAX_HWIFS];
extern ide_ioreg_t chrp_idedma_regbase; /* one for both channels */
extern unsigned int chrp_ide_irq;
extern void chrp_ide_probe(void);

struct ide_machdep_calls {
        void        (*insw)(ide_ioreg_t port, void *buf, int ns);
        void        (*outsw)(ide_ioreg_t port, void *buf, int ns);
        int         (*default_irq)(ide_ioreg_t base);
        ide_ioreg_t (*default_io_base)(int index);
        int         (*check_region)(ide_ioreg_t from, unsigned int extent);
        void        (*request_region)(ide_ioreg_t from,
                                      unsigned int extent,
                                      const char *name);
        void        (*release_region)(ide_ioreg_t from,
                                      unsigned int extent);
        void        (*fix_driveid)(struct hd_driveid *id);
        void        (*ide_init_hwif)(hw_regs_t *hw,
                                     ide_ioreg_t data_port,
                                     ide_ioreg_t ctrl_port,
                                     int *irq);

        int io_base;
};

extern struct ide_machdep_calls ppc_ide_md;

void ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq);
void ide_insw(ide_ioreg_t port, void *buf, int ns);
void ide_outsw(ide_ioreg_t port, void *buf, int ns);
void ppc_generic_ide_fix_driveid(struct hd_driveid *id);

#undef insw
#define insw(port, buf, ns) 	do {				\
	ppc_ide_md.insw((port), (buf), (ns));			\
} while (0)
     
#undef outsw
#define outsw(port, buf, ns) 	do {				\
	ppc_ide_md.outsw((port), (buf), (ns));			\
} while (0)

#undef	SUPPORT_SLOW_DATA_PORTS
#define	SUPPORT_SLOW_DATA_PORTS	0
#undef	SUPPORT_VLB_SYNC
#define SUPPORT_VLB_SYNC	0

#define ide__sti()	__sti()

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	if ( ppc_ide_md.default_irq )
		return ppc_ide_md.default_irq(base);
	else
		return -1;
}

static __inline__ ide_ioreg_t ide_default_io_base(int index)
{
	if ( ppc_ide_md.default_io_base )
		return ppc_ide_md.default_io_base(index);
	else
		return -1;
}

static __inline__ void ide_init_default_hwifs(void)
{
#ifdef __DO_I_NEED_THIS
	hw_regs_t hw;
	int index;

	for(index = 0; index < MAX_HWIFS; index++) {
		ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, NULL);
		hw.irq = ide_default_irq(ide_default_io_base(index));
		ide_register_hw(&hw, NULL);
	}
#endif /* __DO_I_NEED_THIS */
}

static __inline__ int ide_check_region (ide_ioreg_t from, unsigned int extent)
{
	if ( ppc_ide_md.check_region )
		return ppc_ide_md.check_region(from, extent);
	else
		return -1;
}

static __inline__ void ide_request_region (ide_ioreg_t from, unsigned int extent, const char *name)
{
	if ( ppc_ide_md.request_region )
		ppc_ide_md.request_region(from, extent, name);
}

static __inline__ void ide_release_region (ide_ioreg_t from, unsigned int extent)
{
	if ( ppc_ide_md.release_region )
		ppc_ide_md.release_region(from, extent);
}

static __inline__ void ide_fix_driveid (struct hd_driveid *id)
{
        if ( ppc_ide_md.fix_driveid )
		ppc_ide_md.fix_driveid(id);
}

#undef inb
#define inb(port)	in_8((unsigned char *)((port) + ppc_ide_md.io_base))
#undef inb_p
#define inb_p(port)	inb(port)

#undef outb
#define outb(val, port)	\
	out_8((unsigned char *)((port) + ppc_ide_md.io_base), (val) )
#undef outb_p
#define outb_p(val, port)	outb(val, port)

typedef union {
	unsigned all			: 8;	/* all of the bits together */
	struct {
		unsigned bit7		: 1;	/* always 1 */
		unsigned lba		: 1;	/* using LBA instead of CHS */
		unsigned bit5		: 1;	/* always 1 */
		unsigned unit		: 1;	/* drive select number, 0/1 */
		unsigned head		: 4;	/* always zeros here */
	} b;
} select_t;

#define ide_request_irq(irq,hand,flg,dev,id)	request_irq((irq),(hand),(flg),(dev),(id))
#define ide_free_irq(irq,dev_id)		free_irq((irq), (dev_id))

/*
 * The following are not needed for the non-m68k ports
 */
#define ide_ack_intr(hwif)		(1)
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

#endif /* __KERNEL__ */

#endif /* __ASMPPC_IDE_H */
