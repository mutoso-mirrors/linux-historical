/*
 *  linux/drivers/block/ide-probe.c	Version 1.0  Oct  31, 1996
 *
 *  Copyright (C) 1994-1996  Linus Torvalds & authors (see below)
 */

/*
 *  Maintained by Mark Lord  <mlord@pobox.com>
 *            and Gadi Oxman <gadio@netvision.net.il>
 *
 * This is the IDE probe module, as evolved from hd.c and ide.c.
 *
 *  From hd.c:
 *  |
 *  | It traverses the request-list, using interrupts to jump between functions.
 *  | As nearly all functions can be called within interrupts, we may not sleep.
 *  | Special care is recommended.  Have Fun!
 *  |
 *  | modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *  |
 *  | Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  | in the early extended-partition checks and added DM partitions.
 *  |
 *  | Early work on error handling by Mika Liljeberg (liljeber@cs.Helsinki.FI).
 *  |
 *  | IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  | and general streamlining by Mark Lord (mlord@pobox.com).
 *
 *  October, 1994 -- Complete line-by-line overhaul for linux 1.1.x, by:
 *
 *	Mark Lord	(mlord@pobox.com)		(IDE Perf.Pkg)
 *	Delman Lee	(delman@mipg.upenn.edu)		("Mr. atdisk2")
 *	Scott Snyder	(snyder@fnald0.fnal.gov)	(ATAPI IDE cd-rom)
 *
 *  This was a rewrite of just about everything from hd.c, though some original
 *  code is still sprinkled about.  Think of it as a major evolution, with
 *  inspiration from lots of linux users, esp.  hamish@zot.apana.org.au
 *
 * Version 1.0		move drive probing code from ide.c to ide-probe.c
 */

#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/malloc.h>

#include <asm/byteorder.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include "ide.h"

static inline void do_identify (ide_drive_t *drive, byte cmd)
{
	int bswap = 1;
	struct hd_driveid *id;

	id = drive->id = kmalloc (SECTOR_WORDS*4, GFP_KERNEL);
	ide_input_data(drive, id, SECTOR_WORDS);	/* read 512 bytes of id info */
	sti();
	ide_fix_driveid(id);

#if defined (CONFIG_SCSI_EATA_DMA) || defined (CONFIG_SCSI_EATA_PIO) || defined (CONFIG_SCSI_EATA)
	/*
	 * EATA SCSI controllers do a hardware ATA emulation:
	 * Ignore them if there is a driver for them available.
	 */
	if ((id->model[0] == 'P' && id->model[1] == 'M')
	 || (id->model[0] == 'S' && id->model[1] == 'K')) {
		printk("%s: EATA SCSI HBA %.10s\n", drive->name, id->model);
		drive->present = 0;
		return;
	}
#endif /* CONFIG_SCSI_EATA_DMA || CONFIG_SCSI_EATA_PIO */

	/*
	 *  WIN_IDENTIFY returns little-endian info,
	 *  WIN_PIDENTIFY *usually* returns little-endian info.
	 */
	if (cmd == WIN_PIDENTIFY) {
		if ((id->model[0] == 'N' && id->model[1] == 'E') /* NEC */
		 || (id->model[0] == 'F' && id->model[1] == 'X') /* Mitsumi */
		 || (id->model[0] == 'P' && id->model[1] == 'i'))/* Pioneer */
			bswap ^= 1;	/* Vertos drives may still be weird */
	}
	ide_fixstring (id->model,     sizeof(id->model),     bswap);
	ide_fixstring (id->fw_rev,    sizeof(id->fw_rev),    bswap);
	ide_fixstring (id->serial_no, sizeof(id->serial_no), bswap);

	drive->present = 1;
	printk("%s: %s, ", drive->name, id->model);

	/*
	 * Check for an ATAPI device
	 */
	if (cmd == WIN_PIDENTIFY) {
		byte type = (id->config >> 8) & 0x1f;
		printk("ATAPI ");
#ifdef CONFIG_BLK_DEV_PROMISE
		if (HWIF(drive)->is_promise2) {
			printk(" -- not supported on 2nd Promise port\n");
			drive->present = 0;
			return;
		}
#endif /* CONFIG_BLK_DEV_PROMISE */
		switch (type) {
			case ide_floppy:
				if (strstr (id->model, "oppy") || strstr (id->model, "poyp")) {
					printk ("FLOPPY");
					break;
				}
				printk ("cdrom or floppy?, assuming ");
				type = ide_cdrom;	/* Early cdrom models used zero */
			case ide_cdrom:
				printk ("CDROM");
				drive->removable = 1;
				break;
			case ide_tape:
				printk ("TAPE");
				break;
			default:
				printk("UNKNOWN (type %d)", type);
				break;
		}
		printk (" drive\n");
		drive->media = type;
		return;
	}

	drive->media = ide_disk;
	printk("ATA DISK drive\n");
	return;
}

/*
 * Delay for *at least* 50ms.  As we don't know how much time is left
 * until the next tick occurs, we wait an extra tick to be safe.
 * This is used only during the probing/polling for drives at boot time.
 */
static void delay_50ms (void)
{
	unsigned long timer = jiffies + ((HZ + 19)/20) + 1;
	while (timer > jiffies);
}

/*
 * try_to_identify() sends an ATA(PI) IDENTIFY request to a drive
 * and waits for a response.  It also monitors irqs while this is
 * happening, in hope of automatically determining which one is
 * being used by the interface.
 *
 * Returns:	0  device was identified
 *		1  device timed-out (no response to identify request)
 *		2  device aborted the command (refused to identify itself)
 */
static int try_to_identify (ide_drive_t *drive, byte cmd)
{
	int rc;
	ide_ioreg_t hd_status;
	unsigned long timeout;
	int irqs = 0;

	if (!HWIF(drive)->irq) {		/* already got an IRQ? */
		probe_irq_off(probe_irq_on());	/* clear dangling irqs */
		irqs = probe_irq_on();		/* start monitoring irqs */
		OUT_BYTE(drive->ctl,IDE_CONTROL_REG);	/* enable device irq */
	}

	delay_50ms();				/* take a deep breath */
	if ((IN_BYTE(IDE_ALTSTATUS_REG) ^ IN_BYTE(IDE_STATUS_REG)) & ~INDEX_STAT) {
		printk("%s: probing with STATUS instead of ALTSTATUS\n", drive->name);
		hd_status = IDE_STATUS_REG;	/* ancient Seagate drives */
	} else
		hd_status = IDE_ALTSTATUS_REG;	/* use non-intrusive polling */

#if CONFIG_BLK_DEV_PROMISE
	if (IS_PROMISE_DRIVE) {
		if (promise_cmd(drive,PROMISE_IDENTIFY)) {
			if (irqs)
				(void) probe_irq_off(irqs);
			return 1;
		}
	} else
#endif /* CONFIG_BLK_DEV_PROMISE */
		OUT_BYTE(cmd,IDE_COMMAND_REG);		/* ask drive for ID */
	timeout = ((cmd == WIN_IDENTIFY) ? WAIT_WORSTCASE : WAIT_PIDENTIFY) / 2;
	timeout += jiffies;
	do {
		if (jiffies > timeout) {
			if (irqs)
				(void) probe_irq_off(irqs);
			return 1;	/* drive timed-out */
		}
		delay_50ms();		/* give drive a breather */
	} while (IN_BYTE(hd_status) & BUSY_STAT);

	delay_50ms();		/* wait for IRQ and DRQ_STAT */
	if (OK_STAT(GET_STAT(),DRQ_STAT,BAD_R_STAT)) {
		unsigned long flags;
		save_flags(flags);
		cli();			/* some systems need this */
		do_identify(drive, cmd); /* drive returned ID */
		rc = 0;			/* drive responded with ID */
		(void) GET_STAT();	/* clear drive IRQ */
		restore_flags(flags);
	} else
		rc = 2;			/* drive refused ID */
	if (!HWIF(drive)->irq) {
		irqs = probe_irq_off(irqs);	/* get our irq number */
		if (irqs > 0) {
			HWIF(drive)->irq = irqs; /* save it for later */
			irqs = probe_irq_on();
			OUT_BYTE(drive->ctl|2,IDE_CONTROL_REG); /* mask device irq */
			udelay(5);
			(void) probe_irq_off(irqs);
			(void) probe_irq_off(probe_irq_on()); /* clear self-inflicted irq */
			(void) GET_STAT();	/* clear drive IRQ */

		} else {	/* Mmmm.. multiple IRQs.. don't know which was ours */
			printk("%s: IRQ probe failed (%d)\n", drive->name, irqs);
#ifdef CONFIG_BLK_DEV_CMD640
#ifdef CMD640_DUMP_REGS
			if (HWIF(drive)->chipset == ide_cmd640) {
				printk("%s: Hmmm.. probably a driver problem.\n", drive->name);
				CMD640_DUMP_REGS;
			}
#endif /* CMD640_DUMP_REGS */
#endif /* CONFIG_BLK_DEV_CMD640 */
		}
	}
	return rc;
}

/*
 * do_probe() has the difficult job of finding a drive if it exists,
 * without getting hung up if it doesn't exist, without trampling on
 * ethernet cards, and without leaving any IRQs dangling to haunt us later.
 *
 * If a drive is "known" to exist (from CMOS or kernel parameters),
 * but does not respond right away, the probe will "hang in there"
 * for the maximum wait time (about 30 seconds), otherwise it will
 * exit much more quickly.
 *
 * Returns:	0  device was identified
 *		1  device timed-out (no response to identify request)
 *		2  device aborted the command (refused to identify itself)
 *		3  bad status from device (possible for ATAPI drives)
 *		4  probe was not attempted because failure was obvious
 */
static int do_probe (ide_drive_t *drive, byte cmd)
{
	int rc;
	ide_hwif_t *hwif = HWIF(drive);
	if (drive->present) {	/* avoid waiting for inappropriate probes */
		if ((drive->media != ide_disk) && (cmd == WIN_IDENTIFY))
			return 4;
	}
#ifdef DEBUG
	printk("probing for %s: present=%d, media=%d, probetype=%s\n",
		drive->name, drive->present, drive->media,
		(cmd == WIN_IDENTIFY) ? "ATA" : "ATAPI");
#endif
	SELECT_DRIVE(hwif,drive);
	delay_50ms();
	if (IN_BYTE(IDE_SELECT_REG) != drive->select.all && !drive->present) {
		OUT_BYTE(0xa0,IDE_SELECT_REG);	/* exit with drive0 selected */
		delay_50ms();		/* allow BUSY_STAT to assert & clear */
		return 3;    /* no i/f present: avoid killing ethernet cards */
	}

	if (OK_STAT(GET_STAT(),READY_STAT,BUSY_STAT)
	 || drive->present || cmd == WIN_PIDENTIFY)
	{
		if ((rc = try_to_identify(drive,cmd)))   /* send cmd and wait */
			rc = try_to_identify(drive,cmd); /* failed: try again */
		if (rc == 1)
			printk("%s: no response (status = 0x%02x)\n", drive->name, GET_STAT());
		(void) GET_STAT();		/* ensure drive irq is clear */
	} else {
		rc = 3;				/* not present or maybe ATAPI */
	}
	if (drive->select.b.unit != 0) {
		OUT_BYTE(0xa0,IDE_SELECT_REG);	/* exit with drive0 selected */
		delay_50ms();
		(void) GET_STAT();		/* ensure drive irq is clear */
	}
	return rc;
}

/*
 * probe_for_drive() tests for existence of a given drive using do_probe().
 *
 * Returns:	0  no device was found
 *		1  device was found (note: drive->present might still be 0)
 */
static inline byte probe_for_drive (ide_drive_t *drive)
{
	if (drive->noprobe)			/* skip probing? */
		return drive->present;
	if (do_probe(drive, WIN_IDENTIFY) >= 2) { /* if !(success||timed-out) */
		(void) do_probe(drive, WIN_PIDENTIFY); /* look for ATAPI device */
	}
	if (!drive->present)
		return 0;			/* drive not found */
	if (drive->id == NULL) {		/* identification failed? */
		if (drive->media == ide_disk) {
			printk ("%s: non-IDE drive, CHS=%d/%d/%d\n",
			 drive->name, drive->cyl, drive->head, drive->sect);
		} else if (drive->media == ide_cdrom) {
			printk("%s: ATAPI cdrom (?)\n", drive->name);
		} else {
			drive->present = 0;	/* nuke it */
		}
	}
	return 1;	/* drive was found */
}

/*
 * We query CMOS about hard disks : it could be that we have a SCSI/ESDI/etc
 * controller that is BIOS compatible with ST-506, and thus showing up in our
 * BIOS table, but not register compatible, and therefore not present in CMOS.
 *
 * Furthermore, we will assume that our ST-506 drives <if any> are the primary
 * drives in the system -- the ones reflected as drive 1 or 2.  The first
 * drive is stored in the high nibble of CMOS byte 0x12, the second in the low
 * nibble.  This will be either a 4 bit drive type or 0xf indicating use byte
 * 0x19 for an 8 bit type, drive 1, 0x1a for drive 2 in CMOS.  A non-zero value
 * means we have an AT controller hard disk for that drive.
 *
 * Of course, there is no guarantee that either drive is actually on the
 * "primary" IDE interface, but we don't bother trying to sort that out here.
 * If a drive is not actually on the primary interface, then these parameters
 * will be ignored.  This results in the user having to supply the logical
 * drive geometry as a boot parameter for each drive not on the primary i/f.
 *
 * The only "perfect" way to handle this would be to modify the setup.[cS] code
 * to do BIOS calls Int13h/Fn08h and Int13h/Fn48h to get all of the drive info
 * for us during initialization.  I have the necessary docs -- any takers?  -ml
 */
static void probe_cmos_for_drives (ide_hwif_t *hwif)
{
#ifdef __i386__
	extern struct drive_info_struct drive_info;
	byte cmos_disks, *BIOS = (byte *) &drive_info;
	int unit;

#ifdef CONFIG_BLK_DEV_PROMISE
	if (hwif->is_promise2)
		return;
#endif /* CONFIG_BLK_DEV_PROMISE */
	outb_p(0x12,0x70);		/* specify CMOS address 0x12 */
	cmos_disks = inb_p(0x71);	/* read the data from 0x12 */
	/* Extract drive geometry from CMOS+BIOS if not already setup */
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];
		if ((cmos_disks & (0xf0 >> (unit*4))) && !drive->present && !drive->nobios) {
			drive->cyl   = drive->bios_cyl  = *(unsigned short *)BIOS;
			drive->head  = drive->bios_head = *(BIOS+2);
			drive->sect  = drive->bios_sect = *(BIOS+14);
			drive->ctl   = *(BIOS+8);
			drive->present = 1;
		}
		BIOS += 16;
	}
#endif
}

/*
 * This routine only knows how to look for drive units 0 and 1
 * on an interface, so any setting of MAX_DRIVES > 2 won't work here.
 */
static void probe_hwif (ide_hwif_t *hwif)
{
	unsigned int unit;
	unsigned long flags;

	if (hwif->noprobe)
		return;
	if (hwif->io_ports[IDE_DATA_OFFSET] == HD_DATA)
		probe_cmos_for_drives (hwif);
#if CONFIG_BLK_DEV_PROMISE
	if (!hwif->is_promise2 &&
	   (ide_check_region(hwif->io_ports[IDE_DATA_OFFSET],8) || ide_check_region(hwif->io_ports[IDE_CONTROL_OFFSET],1))) {
#else
	if (ide_check_region(hwif->io_ports[IDE_DATA_OFFSET],8) || ide_check_region(hwif->io_ports[IDE_CONTROL_OFFSET],1)) {
#endif /* CONFIG_BLK_DEV_PROMISE */
		int msgout = 0;
		for (unit = 0; unit < MAX_DRIVES; ++unit) {
			ide_drive_t *drive = &hwif->drives[unit];
			if (drive->present) {
				drive->present = 0;
				printk("%s: ERROR, PORTS ALREADY IN USE\n", drive->name);
				msgout = 1;
			}
		}
		if (!msgout)
			printk("%s: ports already in use, skipping probe\n", hwif->name);
		return;	
	}

	save_flags(flags);
	sti();	/* needed for jiffies and irq probing */
	/*
	 * Second drive should only exist if first drive was found,
	 * but a lot of cdrom drives are configured as single slaves.
	 */
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];
		(void) probe_for_drive (drive);
		if (drive->present && !hwif->present) {
			hwif->present = 1;
			ide_request_region(hwif->io_ports[IDE_DATA_OFFSET],  8, hwif->name);
			ide_request_region(hwif->io_ports[IDE_CONTROL_OFFSET], 1, hwif->name);
		}
	}
	if (hwif->reset) {
		unsigned long timeout = jiffies + WAIT_WORSTCASE;
		byte stat;

		printk("%s: reset\n", hwif->name);
		OUT_BYTE(12, hwif->io_ports[IDE_CONTROL_OFFSET]);
		udelay(10);
		OUT_BYTE(8, hwif->io_ports[IDE_CONTROL_OFFSET]);
		do {
			delay_50ms();
			stat = IN_BYTE(hwif->io_ports[IDE_STATUS_OFFSET]);
		} while ((stat & BUSY_STAT) && jiffies < timeout);
	}
	restore_flags(flags);
	for (unit = 0; unit < MAX_DRIVES; ++unit) {
		ide_drive_t *drive = &hwif->drives[unit];
		if (drive->present) {
			ide_tuneproc_t *tuneproc = HWIF(drive)->tuneproc;
			if (tuneproc != NULL && drive->autotune == 1)
				tuneproc(drive, 255);	/* auto-tune PIO mode */
		}
	}
}

#if MAX_HWIFS > 1
/*
 * save_match() is used to simplify logic in init_irq() below.
 *
 * A loophole here is that we may not know about a particular
 * hwif's irq until after that hwif is actually probed/initialized..
 * This could be a problem for the case where an hwif is on a
 * dual interface that requires serialization (eg. cmd640) and another
 * hwif using one of the same irqs is initialized beforehand.
 *
 * This routine detects and reports such situations, but does not fix them.
 */
static void save_match (ide_hwif_t *hwif, ide_hwif_t *new, ide_hwif_t **match)
{
	ide_hwif_t *m = *match;

	if (m && m->hwgroup && m->hwgroup != new->hwgroup) {
		if (!new->hwgroup)
			return;
		printk("%s: potential irq problem with %s and %s\n", hwif->name, new->name, m->name);
	}
	if (!m || m->irq != hwif->irq) /* don't undo a prior perfect match */
		*match = new;
}
#endif /* MAX_HWIFS > 1 */

/*
 * This routine sets up the irq for an ide interface, and creates a new
 * hwgroup for the irq/hwif if none was previously assigned.
 *
 * Much of the code is for correctly detecting/handling irq sharing
 * and irq serialization situations.  This is somewhat complex because
 * it handles static as well as dynamic (PCMCIA) IDE interfaces.
 *
 * The SA_INTERRUPT in sa_flags means ide_intr() is always entered with
 * interrupts completely disabled.  This can be bad for interrupt latency,
 * but anything else has led to problems on some machines.  We re-enable
 * interrupts as much as we can safely do in most places.
 */
static int init_irq (ide_hwif_t *hwif)
{
	unsigned long flags;
#if MAX_HWIFS > 1
	unsigned int index;
#endif /* MAX_HWIFS > 1 */
	ide_hwgroup_t *hwgroup;
	ide_hwif_t *match = NULL;

	save_flags(flags);
	cli();

	hwif->hwgroup = NULL;
#if MAX_HWIFS > 1
	/*
	 * Group up with any other hwifs that share our irq(s).
	 */
	for (index = 0; index < MAX_HWIFS; index++) {
		ide_hwif_t *h = &ide_hwifs[index];
		if (h->hwgroup) {  /* scan only initialized hwif's */
			if (hwif->irq == h->irq) {
				hwif->sharing_irq = h->sharing_irq = 1;
				save_match(hwif, h, &match);
			}
			if (hwif->serialized) {
				ide_hwif_t *mate = &ide_hwifs[hwif->index^1];
				if (index == mate->index || h->irq == mate->irq)
					save_match(hwif, h, &match);
			}
			if (h->serialized) {
				ide_hwif_t *mate = &ide_hwifs[h->index^1];
				if (hwif->irq == mate->irq)
					save_match(hwif, h, &match);
			}
		}
	}
#endif /* MAX_HWIFS > 1 */
	/*
	 * If we are still without a hwgroup, then form a new one
	 */
	if (match) {
		hwgroup = match->hwgroup;
	} else {
		hwgroup = kmalloc(sizeof(ide_hwgroup_t), GFP_KERNEL);
		hwgroup->hwif 	 = hwgroup->next_hwif = hwif->next = hwif;
		hwgroup->rq      = NULL;
		hwgroup->handler = NULL;
		if (hwif->drives[0].present)
			hwgroup->drive = &hwif->drives[0];
		else
			hwgroup->drive = &hwif->drives[1];
		hwgroup->poll_timeout = 0;
		init_timer(&hwgroup->timer);
		hwgroup->timer.function = &ide_timer_expiry;
		hwgroup->timer.data = (unsigned long) hwgroup;
	}

	/*
	 * Allocate the irq, if not already obtained for another hwif
	 */
	if (!match || match->irq != hwif->irq) {
		if (ide_request_irq(hwif->irq, &ide_intr, SA_INTERRUPT, hwif->name, hwgroup)) {
			if (!match)
				kfree(hwgroup);
			restore_flags(flags);
			return 1;
		}
	}

	/*
	 * Everything is okay, so link us into the hwgroup
	 */
	hwif->hwgroup = hwgroup;
	hwif->next = hwgroup->hwif->next;
	hwgroup->hwif->next = hwif;

	restore_flags(flags);	/* safe now that hwif->hwgroup is set up */

#ifndef __mc68000__
	printk("%s at 0x%03x-0x%03x,0x%03x on irq %d", hwif->name,
		hwif->io_ports[IDE_DATA_OFFSET], hwif->io_ports[IDE_DATA_OFFSET]+7, hwif->io_ports[IDE_CONTROL_OFFSET], hwif->irq);
#else
	printk("%s at %p on irq 0x%08x", hwif->name, hwif->io_ports[IDE_DATA_OFFSET], hwif->irq);
#endif /* __mc68000__ */
	if (match)
		printk(" (%sed with %s)", hwif->sharing_irq ? "shar" : "serializ", match->name);
	printk("\n");
	return 0;
}

/*
 * init_gendisk() (as opposed to ide_geninit) is called for each major device,
 * after probing for drives, to allocate partition tables and other data
 * structures needed for the routines in genhd.c.  ide_geninit() gets called
 * somewhat later, during the partition check.
 */
static void init_gendisk (ide_hwif_t *hwif)
{
	struct gendisk *gd, **gdp;
	unsigned int unit, units, minors;
	int *bs;

	/* figure out maximum drive number on the interface */
	for (units = MAX_DRIVES; units > 0; --units) {
		if (hwif->drives[units-1].present)
			break;
	}
	minors    = units * (1<<PARTN_BITS);
	gd        = kmalloc (sizeof(struct gendisk), GFP_KERNEL);
	gd->sizes = kmalloc (minors * sizeof(int), GFP_KERNEL);
	gd->part  = kmalloc (minors * sizeof(struct hd_struct), GFP_KERNEL);
	bs        = kmalloc (minors*sizeof(int), GFP_KERNEL);

	memset(gd->part, 0, minors * sizeof(struct hd_struct));

	/* cdroms and msdos f/s are examples of non-1024 blocksizes */
	blksize_size[hwif->major] = bs;
	for (unit = 0; unit < minors; ++unit)
		*bs++ = BLOCK_SIZE;

	for (unit = 0; unit < units; ++unit)
		hwif->drives[unit].part = &gd->part[unit << PARTN_BITS];

	gd->major	= hwif->major;		/* our major device number */
	gd->major_name	= IDE_MAJOR_NAME;	/* treated special in genhd.c */
	gd->minor_shift	= PARTN_BITS;		/* num bits for partitions */
	gd->max_p	= 1<<PARTN_BITS;	/* 1 + max partitions / drive */
	gd->max_nr	= units;		/* max num real drives */
	gd->nr_real	= units;		/* current num real drives */
	gd->init	= &ide_geninit;		/* initialization function */
	gd->real_devices= hwif;			/* ptr to internal data */
	gd->next	= NULL;			/* linked list of major devs */

	for (gdp = &gendisk_head; *gdp; gdp = &((*gdp)->next)) ;
	hwif->gd = *gdp = gd;			/* link onto tail of list */
}

static int hwif_init (int h)
{
	ide_hwif_t *hwif = &ide_hwifs[h];
	void (*rfn)(void);
	
	if (!hwif->present)
		return 0;
	if (!hwif->irq) {
		if (!(hwif->irq = ide_default_irq(hwif->io_ports[IDE_DATA_OFFSET]))) {
			printk("%s: DISABLED, NO IRQ\n", hwif->name);
			return (hwif->present = 0);
		}
	}
#ifdef CONFIG_BLK_DEV_HD
	if (hwif->irq == HD_IRQ && hwif->io_ports[IDE_DATA_OFFSET] != HD_DATA) {
		printk("%s: CANNOT SHARE IRQ WITH OLD HARDDISK DRIVER (hd.c)\n", hwif->name);
		return (hwif->present = 0);
	}
#endif /* CONFIG_BLK_DEV_HD */
	
	hwif->present = 0; /* we set it back to 1 if all is ok below */
	switch (hwif->major) {
	case IDE0_MAJOR: rfn = &do_ide0_request; break;
#if MAX_HWIFS > 1
	case IDE1_MAJOR: rfn = &do_ide1_request; break;
#endif
#if MAX_HWIFS > 2
	case IDE2_MAJOR: rfn = &do_ide2_request; break;
#endif
#if MAX_HWIFS > 3
	case IDE3_MAJOR: rfn = &do_ide3_request; break;
#endif
	default:
		printk("%s: request_fn NOT DEFINED\n", hwif->name);
		return (hwif->present = 0);
	}
	if (register_blkdev (hwif->major, hwif->name, ide_fops)) {
		printk("%s: UNABLE TO GET MAJOR NUMBER %d\n", hwif->name, hwif->major);
	} else if (init_irq (hwif)) {
		printk("%s: UNABLE TO GET IRQ %d\n", hwif->name, hwif->irq);
		(void) unregister_blkdev (hwif->major, hwif->name);
	} else {
		init_gendisk(hwif);
		blk_dev[hwif->major].request_fn = rfn;
		read_ahead[hwif->major] = 8;	/* (4kB) */
		hwif->present = 1;	/* success */
	}
	return hwif->present;
}

int ideprobe_init (void);
static ide_module_t ideprobe_module = {
	IDE_PROBE_MODULE,
	ideprobe_init,
	NULL
};

int ideprobe_init (void)
{
	unsigned int index;
	int probe[MAX_HWIFS];
	
	MOD_INC_USE_COUNT;
	memset(probe, 0, MAX_HWIFS * sizeof(int));
	for (index = 0; index < MAX_HWIFS; ++index)
		probe[index] = !ide_hwifs[index].present;

	/*
	 * Probe for drives in the usual way.. CMOS/BIOS, then poke at ports
	 */
	for (index = 0; index < MAX_HWIFS; ++index)
		if (probe[index]) probe_hwif (&ide_hwifs[index]);
	for (index = 0; index < MAX_HWIFS; ++index)
		if (probe[index]) hwif_init (index);
	ide_register_module(&ideprobe_module);
	MOD_DEC_USE_COUNT;
	return 0;
}

#ifdef MODULE
int init_module (void)
{
	unsigned int index;
	
	for (index = 0; index < MAX_HWIFS; ++index)
		ide_unregister(index);
	return ideprobe_init();
}

void cleanup_module (void)
{
	ide_unregister_module(&ideprobe_module);
}
#endif /* MODULE */
