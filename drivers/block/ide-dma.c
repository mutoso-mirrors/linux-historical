/*
 *  linux/drivers/block/ide-dma.c	Version 4.07  December 5, 1997
 *
 *  Copyright (c) 1995-1998  Mark Lord
 *  May be copied or modified under the terms of the GNU General Public License
 */

/*
 * This module provides support for the bus-master IDE DMA functions
 * of various PCI chipsets, including the Intel PIIX (i82371FB for
 * the 430 FX chipset), the PIIX3 (i82371SB for the 430 HX/VX and 
 * 440 chipsets), and the PIIX4 (i82371AB for the 430 TX chipset)
 * ("PIIX" stands for "PCI ISA IDE Xcellerator").
 *
 * Pretty much the same code works for other IDE PCI bus-mastering chipsets.
 *
 * DMA is supported for all IDE devices (disk drives, cdroms, tapes, floppies).
 *
 * By default, DMA support is prepared for use, but is currently enabled only
 * for drives which already have DMA enabled (UltraDMA or mode 2 multi/single),
 * or which are recognized as "good" (see table below).  Drives with only mode0
 * or mode1 (multi/single) DMA should also work with this chipset/driver
 * (eg. MC2112A) but are not enabled by default.
 *
 * Use "hdparm -i" to view modes supported by a given drive.
 *
 * The hdparm-2.4 (or later) utility can be used for manually enabling/disabling
 * DMA support, but must be (re-)compiled against this kernel version or later.
 *
 * To enable DMA, use "hdparm -d1 /dev/hd?" on a per-drive basis after booting.
 * If problems arise, ide.c will disable DMA operation after a few retries.
 * This error recovery mechanism works and has been extremely well exercised.
 *
 * IDE drives, depending on their vintage, may support several different modes
 * of DMA operation.  The boot-time modes are indicated with a "*" in
 * the "hdparm -i" listing, and can be changed with *knowledgeable* use of
 * the "hdparm -X" feature.  There is seldom a need to do this, as drives
 * normally power-up with their "best" PIO/DMA modes enabled.
 *
 * Testing has been done with a rather extensive number of drives,
 * with Quantum & Western Digital models generally outperforming the pack,
 * and Fujitsu & Conner (and some Seagate which are really Conner) drives
 * showing more lackluster throughput.
 *
 * Keep an eye on /var/adm/messages for "DMA disabled" messages.
 *
 * Some people have reported trouble with Intel Zappa motherboards.
 * This can be fixed by upgrading the AMI BIOS to version 1.00.04.BS0,
 * available from ftp://ftp.intel.com/pub/bios/10004bs0.exe
 * (thanks to Glen Morrell <glen@spin.Stanford.edu> for researching this).
 *
 * Thanks to "Christopher J. Reimer" <reimer@doe.carleton.ca> for
 * fixing the problem with the BIOS on some Acer motherboards.
 *
 * Thanks to "Benoit Poulot-Cazajous" <poulot@chorus.fr> for testing
 * "TX" chipset compatibility and for providing patches for the "TX" chipset.
 *
 * Thanks to Christian Brunner <chb@muc.de> for taking a good first crack
 * at generic DMA -- his patches were referred to when preparing this code.
 *
 * Most importantly, thanks to Robert Bringman <rob@mars.trion.com>
 * for supplying a Promise UDMA board & WD UDMA drive for this work!
 *
 * And, yes, Intel Zappa boards really *do* use both PIIX IDE ports.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/bios32.h>

#include <asm/io.h>
#include <asm/irq.h>

#include "ide.h"

/*
 * good_dma_drives() lists the model names (from "hdparm -i")
 * of drives which do not support mode2 DMA but which are
 * known to work fine with this interface under Linux.
 */
const char *good_dma_drives[] = {"Micropolis 2112A",
				 "CONNER CTMA 4000",
				 NULL};

/*
 * Our Physical Region Descriptor (PRD) table should be large enough
 * to handle the biggest I/O request we are likely to see.  Since requests
 * can have no more than 256 sectors, and since the typical blocksize is
 * two or more sectors, we could get by with a limit of 128 entries here for
 * the usual worst case.  Most requests seem to include some contiguous blocks,
 * further reducing the number of table entries required.
 *
 * The driver reverts to PIO mode for individual requests that exceed
 * this limit (possible with 512 byte blocksizes, eg. MSDOS f/s), so handling
 * 100% of all crazy scenarios here is not necessary.
 *
 * As it turns out though, we must allocate a full 4KB page for this,
 * so the two PRD tables (ide0 & ide1) will each get half of that,
 * allowing each to have about 256 entries (8 bytes each) from this.
 */
#define PRD_BYTES	8
#define PRD_ENTRIES	(PAGE_SIZE / (2 * PRD_BYTES))

/*
 * dma_intr() is the handler for disk read/write DMA interrupts
 */
void ide_dma_intr (ide_drive_t *drive)
{
	int i;
	byte stat, dma_stat;

	dma_stat = HWIF(drive)->dmaproc(ide_dma_end, drive);
	stat = GET_STAT();			/* get drive status */
	if (OK_STAT(stat,DRIVE_READY,drive->bad_wstat|DRQ_STAT)) {
		if (!dma_stat) {
			struct request *rq = HWGROUP(drive)->rq;
			rq = HWGROUP(drive)->rq;
			for (i = rq->nr_sectors; i > 0;) {
				i -= rq->current_nr_sectors;
				ide_end_request(1, HWGROUP(drive));
			}
			return;
		}
		printk("%s: dma_intr: bad DMA status\n", drive->name);
	}
	sti();
	ide_error(drive, "dma_intr", stat);
}

/*
 * ide_build_dmatable() prepares a dma request.
 * Returns 0 if all went okay, returns 1 otherwise.
 * May also be invoked from trm290.c
 */
int ide_build_dmatable (ide_drive_t *drive)
{
	struct request *rq = HWGROUP(drive)->rq;
	struct buffer_head *bh = rq->bh;
	unsigned long size, addr, *table = HWIF(drive)->dmatable;
#ifdef CONFIG_BLK_DEV_TRM290
	unsigned int is_trm290_chipset = (HWIF(drive)->chipset == ide_trm290);
#else
	const int is_trm290_chipset = 0;
#endif
	unsigned int count = 0;

	do {
		/*
		 * Determine addr and size of next buffer area.  We assume that
		 * individual virtual buffers are always composed linearly in
		 * physical memory.  For example, we assume that any 8kB buffer
		 * is always composed of two adjacent physical 4kB pages rather
		 * than two possibly non-adjacent physical 4kB pages.
		 */
		if (bh == NULL) {  /* paging requests have (rq->bh == NULL) */
			addr = virt_to_bus (rq->buffer);
			size = rq->nr_sectors << 9;
		} else {
			/* group sequential buffers into one large buffer */
			addr = virt_to_bus (bh->b_data);
			size = bh->b_size;
			while ((bh = bh->b_reqnext) != NULL) {
				if ((addr + size) != virt_to_bus (bh->b_data))
					break;
				size += bh->b_size;
			}
		}
		/*
		 * Fill in the dma table, without crossing any 64kB boundaries.
		 * The hardware requires 16-bit alignment of all blocks
		 * (trm290 requires 32-bit alignment).
		 */
		if ((addr & 3)) {
			printk("%s: misaligned DMA buffer\n", drive->name);
			return 0;
		}
		while (size) {
			if (++count >= PRD_ENTRIES) {
				printk("%s: DMA table too small\n", drive->name);
				return 0; /* revert to PIO for this request */
			} else {
				unsigned long xcount, bcount = 0x10000 - (addr & 0xffff);
				if (bcount > size)
					bcount = size;
				*table++ = cpu_to_le32(addr);
				xcount = bcount & 0xffff;
				if (is_trm290_chipset)
					xcount = ((xcount >> 2) - 1) << 16;
				*table++ = cpu_to_le32(xcount);
				addr += bcount;
				size -= bcount;
			}
		}
	} while (bh != NULL);
	if (!count)
		printk("%s: empty DMA table?\n", drive->name);
	else if (!is_trm290_chipset)
		*--table |= cpu_to_le32(0x80000000);	/* set End-Of-Table (EOT) bit */
	return count;
}

static int config_drive_for_dma (ide_drive_t *drive)
{
	const char **list;
	struct hd_driveid *id = drive->id;
	ide_hwif_t *hwif = HWIF(drive);

	if (id && (id->capability & 1) && !HWIF(drive)->no_autodma) {
		/* Enable DMA on any drive that has UltraDMA (mode 0/1/2) enabled */
		if (id->field_valid & 4)	/* UltraDMA */
			if  ((id->dma_ultra & (id->dma_ultra >> 8) & 7))
				return hwif->dmaproc(ide_dma_on, drive);
		/* Enable DMA on any drive that has mode2 DMA (multi or single) enabled */
		if (id->field_valid & 2)	/* regular DMA */
			if  ((id->dma_mword & 0x404) == 0x404 || (id->dma_1word & 0x404) == 0x404)
				return hwif->dmaproc(ide_dma_on, drive);
		/* Consult the list of known "good" drives */
		list = good_dma_drives;
		while (*list) {
			if (!strcmp(*list++,id->model))
				return hwif->dmaproc(ide_dma_on, drive);
		}
	}
	return hwif->dmaproc(ide_dma_off_quietly, drive);
}

/*
 * ide_dmaproc() initiates/aborts DMA read/write operations on a drive.
 *
 * The caller is assumed to have selected the drive and programmed the drive's
 * sector address using CHS or LBA.  All that remains is to prepare for DMA
 * and then issue the actual read/write DMA/PIO command to the drive.
 *
 * For ATAPI devices, we just prepare for DMA and return. The caller should
 * then issue the packet command to the drive and call us again with
 * ide_dma_begin afterwards.
 *
 * Returns 0 if all went well.
 * Returns 1 if DMA read/write could not be started, in which case
 * the caller should revert to PIO for the current request.
 * May also be invoked from trm290.c
 */
int ide_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	ide_hwif_t *hwif = HWIF(drive);
	unsigned int dma_base = hwif->dma_base;
	unsigned int count, reading = 0;

	switch (func) {
		case ide_dma_off:
			printk("%s: DMA disabled\n", drive->name);
		case ide_dma_off_quietly:
		case ide_dma_on:
			drive->using_dma = (func == ide_dma_on);
			return 0;
		case ide_dma_check:
			return config_drive_for_dma (drive);
		case ide_dma_read:
			reading = 1 << 3;
		case ide_dma_write:
			if (!(count = ide_build_dmatable(drive)))
				return 1;	/* try PIO instead of DMA */
			outl(virt_to_bus(hwif->dmatable), dma_base + 4); /* PRD table */
			outb(reading, dma_base);			/* specify r/w */
			outb(inb(dma_base+2)|0x06, dma_base+2);		/* clear status bits */
			if (drive->media != ide_disk)
				return 0;
			ide_set_handler(drive, &ide_dma_intr, WAIT_CMD);/* issue cmd to drive */
			OUT_BYTE(reading ? WIN_READDMA : WIN_WRITEDMA, IDE_COMMAND_REG);
		case ide_dma_begin:
			outb(inb(dma_base)|1, dma_base);		/* start DMA */
			return 0;
		case ide_dma_end: /* returns 1 on error, 0 otherwise */
		{
			byte dma_stat = inb(dma_base+2);
			int rc = (dma_stat & 7) != 4;
			outb(inb(dma_base)&~1, dma_base);		/* stop DMA */
			outb(dma_stat|6, dma_base+2);	/* clear the INTR & ERROR bits */
			return rc;	/* verify good DMA status */
		}
		default:
			printk("ide_dmaproc: unsupported func: %d\n", func);
			return 1;
	}
}

void ide_setup_dma (ide_hwif_t *hwif, unsigned int dma_base, unsigned int num_ports) /* __init */
{
	static unsigned long dmatable = 0;
	static unsigned leftover = 0;

	printk("    %s: BM-DMA at 0x%04x-0x%04x", hwif->name, dma_base, dma_base + num_ports - 1);
	if (check_region(dma_base, num_ports)) {
		printk(" -- ERROR, PORT ADDRESSES ALREADY IN USE\n");
		return;
	}
	request_region(dma_base, num_ports, hwif->name);
	hwif->dma_base = dma_base;
	if (leftover < (PRD_ENTRIES * PRD_BYTES)) {
		/*
		 * The BM-DMA uses full 32bit addr, so we can
		 * safely use __get_free_page() here instead
		 * of __get_dma_pages() -- no ISA limitations.
		 */
		dmatable = __get_free_pages(GFP_KERNEL,1,0);
		leftover = dmatable ? PAGE_SIZE : 0;
	}
	if (!dmatable) {
		printk(" -- ERROR, UNABLE TO ALLOCATE PRD TABLE\n");
	} else {
		hwif->dmatable = (unsigned long *) dmatable;
		dmatable += (PRD_ENTRIES * PRD_BYTES);
		leftover -= (PRD_ENTRIES * PRD_BYTES);
		hwif->dmaproc = &ide_dmaproc;

		if (hwif->chipset != ide_trm290) {
			byte dma_stat = inb(dma_base+2);
			printk(", BIOS settings: %s:%s, %s:%s",
		 	 hwif->drives[0].name, (dma_stat & 0x20) ? "DMA" : "pio",
		 	 hwif->drives[1].name, (dma_stat & 0x40) ? "DMA" : "pio");
		}
		printk("\n");
	}
}

/* The next two functions were stolen from cmd640.c, with a few modifications  */

__initfunc(static void write_pcicfg_dword (byte fn, unsigned short reg, long val))
{
  unsigned long flags;

  save_flags(flags);
  cli();
  outl_p((reg & 0xfc) | ((fn * 0x100) + 0x80000000), 0xcf8);
  outl_p(val, (reg & 3) | 0xcfc);
  restore_flags(flags);
}

__initfunc(static long read_pcicfg_dword (byte fn, unsigned short reg))
{
  long b;
  unsigned long flags;

  save_flags(flags);
  cli();
  outl_p((reg & 0xfc) | ((fn * 0x100) + 0x80000000), 0xcf8);
  b = inl_p((reg & 3) | 0xcfc);
  restore_flags(flags);
  return b;
}

/*
 * Fetch the DMA Bus-Master-I/O-Base-Address (BMIBA) from PCI space:
 */
unsigned int ide_get_or_set_dma_base (ide_hwif_t *hwif, int extra, const char *name) /* __init */
{
	unsigned int new, dma_base = 0;
	byte bus = hwif->pci_bus, fn = hwif->pci_fn;

	if (hwif->mate && hwif->mate->dma_base) {
		dma_base = hwif->mate->dma_base - (hwif->channel ? 0 : 8);
	} else if (pcibios_read_config_dword(bus, fn, 0x20, &dma_base)) {
		printk("%s: failed to read dma_base\n", name);
		dma_base = 0;
	} else if ((dma_base &= ~0xf) == 0 || dma_base == ~0xf) {
		printk("%s: dma_base is invalid (0x%04x, BIOS problem)\n", name, dma_base);
		new = ide_find_free_region(16 + extra);
		hwif->no_autodma = 1;	/* default DMA off if we had to configure it here */
		if (new) {
			printk("%s: setting dma_base to 0x%04x\n", name, new);
			new |= 1;
			(void) pcibios_write_config_dword(bus, fn, 0x20, new);
			(void) pcibios_read_config_dword(bus, fn, 0x20, &dma_base);
			if (dma_base != new) {
				if (bus == 0) {
					printk("%s: operation failed, bypassing BIOS to try again\n", name);
					write_pcicfg_dword(fn, 0x20, new);
					dma_base = read_pcicfg_dword(fn, 0x20);
				}
				if (dma_base != new) {
					printk("%s: operation failed, DMA disabled\n", name);
					dma_base = 0;
				}
			}
			dma_base &= ~0xf;
		}
	}
	if (dma_base) {
		if (extra) /* PDC20246 */
			request_region(dma_base+16, extra, name);
		dma_base += hwif->channel ? 8 : 0;
		if (inb(dma_base+2) & 0x80) {
			printk("%s: simplex device:  DMA disabled\n", name);
			dma_base = 0;
		}
	}
	return dma_base;
}

