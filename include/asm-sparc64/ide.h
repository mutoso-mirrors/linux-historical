/* $Id: ide.h,v 1.22 2002/01/16 20:58:40 davem Exp $
 * ide.h: Ultra/PCI specific IDE glue.
 *
 * Copyright (C) 1997  David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998  Eddie C. Dost   (ecd@skynet.be)
 */

#ifndef _SPARC64_IDE_H
#define _SPARC64_IDE_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/pgalloc.h>
#include <asm/io.h>
#include <asm/hdreg.h>
#include <asm/page.h>
#include <asm/spitfire.h>

#undef  MAX_HWIFS
#define MAX_HWIFS	2

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	return 0;
}

static __inline__ ide_ioreg_t ide_default_io_base(int index)
{
	return 0;
}

static __inline__ void ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq)
{
	ide_ioreg_t reg =  data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = 0;
	}
	if (irq != NULL)
		*irq = 0;
	hw->io_ports[IDE_IRQ_OFFSET] = 0;
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void ide_init_default_hwifs(void)
{
#ifndef CONFIG_PCI
	hw_regs_t hw;
	int index;

	for (index = 0; index < MAX_HWIFS; index++) {
		ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, NULL);
		hw.irq = ide_default_irq(ide_default_io_base(index));
		ide_register_hw(&hw, NULL);
	}
#endif
}


#define ide_request_irq(irq,hand,flg,dev,id)	request_irq((irq),(hand),(flg),(dev),(id))
#define ide_free_irq(irq,dev_id)		free_irq((irq), (dev_id))
#define ide_check_region(from,extent)		check_region((from), (extent))
#define ide_request_region(from,extent,name)	request_region((from), (extent), (name))
#define ide_release_region(from,extent)		release_region((from), (extent))

/*
 * The following are not needed for the non-m68k ports
 */
#define ide_ack_intr(hwif)		(1)
#define ide_fix_driveid(id)		do {} while (0)
#define ide_release_lock(lock)		do {} while (0)
#define ide_get_lock(lock, hdlr, data)	do {} while (0)

/* From m68k code... */

#ifdef insl
#undef insl
#endif
#ifdef outsl
#undef outsl
#endif
#ifdef insw
#undef insw
#endif
#ifdef outsw
#undef outsw
#endif

#define insl(data_reg, buffer, wcount) insw(data_reg, buffer, (wcount)<<1)
#define outsl(data_reg, buffer, wcount) outsw(data_reg, buffer, (wcount)<<1)

#define insw(port, buf, nr) ide_insw((port), (buf), (nr))
#define outsw(port, buf, nr) ide_outsw((port), (buf), (nr))

static __inline__ unsigned int inw_be(unsigned long addr)
{
	unsigned int ret;

	__asm__ __volatile__("lduha [%1] %2, %0"
			     : "=r" (ret)
			     : "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));

	return ret;
}

static __inline__ void ide_insw(unsigned long port,
				void *dst,
				unsigned long count)
{
#if (L1DCACHE_SIZE > PAGE_SIZE)		/* is there D$ aliasing problem */
	unsigned long end = (unsigned long)dst + (count << 1);
#endif
	u16 *ps = dst;
	u32 *pi;

	if(((u64)ps) & 0x2) {
		*ps++ = inw_be(port);
		count--;
	}
	pi = (u32 *)ps;
	while(count >= 2) {
		u32 w;

		w  = inw_be(port) << 16;
		w |= inw_be(port);
		*pi++ = w;
		count -= 2;
	}
	ps = (u16 *)pi;
	if(count)
		*ps++ = inw_be(port);

#if (L1DCACHE_SIZE > PAGE_SIZE)		/* is there D$ aliasing problem */
	__flush_dcache_range((unsigned long)dst, end);
#endif
}

static __inline__ void outw_be(unsigned short w, unsigned long addr)
{
	__asm__ __volatile__("stha %0, [%1] %2"
			     : /* no outputs */
			     : "r" (w), "r" (addr), "i" (ASI_PHYS_BYPASS_EC_E));
}

static __inline__ void ide_outsw(unsigned long port,
				 const void *src,
				 unsigned long count)
{
#if (L1DCACHE_SIZE > PAGE_SIZE)		/* is there D$ aliasing problem */
	unsigned long end = (unsigned long)src + (count << 1);
#endif
	const u16 *ps = src;
	const u32 *pi;

	if(((u64)src) & 0x2) {
		outw_be(*ps++, port);
		count--;
	}
	pi = (const u32 *)ps;
	while(count >= 2) {
		u32 w;

		w = *pi++;
		outw_be((w >> 16), port);
		outw_be(w, port);
		count -= 2;
	}
	ps = (const u16 *)pi;
	if(count)
		outw_be(*ps, port);

#if (L1DCACHE_SIZE > PAGE_SIZE)		/* is there D$ aliasing problem */
	__flush_dcache_range((unsigned long)src, end);
#endif
}

#endif /* __KERNEL__ */

#endif /* _SPARC64_IDE_H */
