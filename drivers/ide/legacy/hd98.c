/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This is the low-level hd interrupt support. It traverses the
 * request-list, using interrupts to jump between functions. As
 * all the functions are called within interrupts, we may not
 * sleep. Special care is recommended.
 *
 *  modified by Drew Eckhardt to check nr of hd's from the CMOS.
 *
 *  Thanks to Branko Lankester, lankeste@fwi.uva.nl, who found a bug
 *  in the early extended-partition checks and added DM partitions
 *
 *  IRQ-unmask, drive-id, multiple-mode, support for ">16 heads",
 *  and general streamlining by Mark Lord.
 *
 *  Removed 99% of above. Use Mark's ide driver for those options.
 *  This is now a lightweight ST-506 driver. (Paul Gortmaker)
 *
 *  Modified 1995 Russell King for ARM processor.
 *
 *  Bugfix: max_sectors must be <= 255 or the wheels tend to come
 *  off in a hurry once you queue things up - Paul G. 02/2001
 */

/* Uncomment the following if you want verbose error reports. */
/* #define VERBOSE_ERRORS */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/genhd.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/mc146818rtc.h> /* CMOS defines */
#include <linux/init.h>
#include <linux/blkpg.h>
#include <linux/hdreg.h>

#define REALLY_SLOW_IO
#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/blk.h>

#include "io_ports.h"

#ifdef __arm__
#undef  HD_IRQ
#endif
#include <asm/irq.h>
#ifdef __arm__
#define HD_IRQ IRQ_HARDDISK
#endif

/* Hd controller regster ports */

#define HD_DATA		0x640	/* _CTL when writing */
#define HD_ERROR	0x642	/* see err-bits */
#define HD_NSECTOR	0x644	/* nr of sectors to read/write */
#define HD_SECTOR	0x646	/* starting sector */
#define HD_LCYL		0x648	/* starting cylinder */
#define HD_HCYL		0x64a	/* high byte of starting cyl */
#define HD_CURRENT	0x64c	/* 101dhhhh , d=drive, hhhh=head */
#define HD_STATUS	0x64e	/* see status-bits */
#define HD_FEATURE	HD_ERROR	/* same io address, read=error, write=feature */
#define HD_PRECOMP	HD_FEATURE	/* obsolete use of this port - predates IDE */
#define HD_COMMAND	HD_STATUS	/* same io address, read=status, write=cmd */

#define HD_CMD		0x74c	/* used for resets */
#define HD_ALTSTATUS	0x74c	/* same as HD_STATUS but doesn't clear irq */

/* Bits of HD_STATUS */
#define ERR_STAT		0x01
#define INDEX_STAT		0x02
#define ECC_STAT		0x04	/* Corrected error */
#define DRQ_STAT		0x08
#define SEEK_STAT		0x10
#define SERVICE_STAT		SEEK_STAT
#define WRERR_STAT		0x20
#define READY_STAT		0x40
#define BUSY_STAT		0x80

/* Bits for HD_ERROR */
#define MARK_ERR		0x01	/* Bad address mark */
#define TRK0_ERR		0x02	/* couldn't find track 0 */
#define ABRT_ERR		0x04	/* Command aborted */
#define MCR_ERR			0x08	/* media change request */
#define ID_ERR			0x10	/* ID field not found */
#define MC_ERR			0x20	/* media changed */
#define ECC_ERR			0x40	/* Uncorrectable ECC error */
#define BBD_ERR			0x80	/* pre-EIDE meaning:  block marked bad */
#define ICRC_ERR		0x80	/* new meaning:  CRC error during transfer */

static spinlock_t hd_lock = SPIN_LOCK_UNLOCKED;
static struct request_queue hd_queue;

#define CURRENT elv_next_request(&hd_queue)

#define TIMEOUT_VALUE	(6*HZ)
#define	HD_DELAY	0

#define MAX_ERRORS     16	/* Max read/write errors/sector */
#define RESET_FREQ      8	/* Reset controller every 8th retry */
#define RECAL_FREQ      4	/* Recalibrate every 4th retry */
#define MAX_HD		2

#define STAT_OK		(READY_STAT|SEEK_STAT)
#define OK_STATUS(s)	(((s)&(STAT_OK|(BUSY_STAT|WRERR_STAT|ERR_STAT)))==STAT_OK)

static void recal_intr(void);
static void bad_rw_intr(void);

static int reset;
static int hd_error;

/*
 *  This struct defines the HD's and their types.
 */
struct hd_i_struct {
	unsigned int head,sect,cyl,wpcom,lzone,ctl;
	int unit;
	int recalibrate;
	int special_op;
};
	
#ifdef HD_TYPE
static struct hd_i_struct hd_info[] = { HD_TYPE };
static int NR_HD = ((sizeof (hd_info))/(sizeof (struct hd_i_struct)));
#else
static struct hd_i_struct hd_info[MAX_HD];
static int NR_HD;
#endif

static struct gendisk *hd_gendisk[MAX_HD];

static struct timer_list device_timer;

#define TIMEOUT_VALUE (6*HZ)

#define SET_TIMER							\
	do {								\
		mod_timer(&device_timer, jiffies + TIMEOUT_VALUE);	\
	} while (0)

static void (*do_hd)(void) = NULL;
#define SET_HANDLER(x) \
if ((do_hd = (x)) != NULL) \
	SET_TIMER; \
else \
	del_timer(&device_timer);


#if (HD_DELAY > 0)
unsigned long last_req;

unsigned long read_timer(void)
{
        extern spinlock_t i8253_lock;
	unsigned long t, flags;
	int i;

	spin_lock_irqsave(&i8253_lock, flags);
	t = jiffies * 11932;
    	outb_p(0, PIT_MODE);
	i = inb_p(PIT_CH0);
	i |= inb(PIT_CH0) << 8;
	spin_unlock_irqrestore(&i8253_lock, flags);
	return(t - i);
}
#endif

void __init hd_setup(char *str, int *ints)
{
	int hdind = 0;

	if (ints[0] != 3)
		return;
	if (hd_info[0].head != 0)
		hdind=1;
	hd_info[hdind].head = ints[2];
	hd_info[hdind].sect = ints[3];
	hd_info[hdind].cyl = ints[1];
	hd_info[hdind].wpcom = 0;
	hd_info[hdind].lzone = ints[1];
	hd_info[hdind].ctl = (ints[2] > 8 ? 8 : 0);
	NR_HD = hdind+1;
}

static void dump_status (const char *msg, unsigned int stat)
{
	char *name = CURRENT ?
			CURRENT->rq_dev->bd_disk->disk_name :
			"hd?";
#ifdef VERBOSE_ERRORS
	printk("%s: %s: status=0x%02x { ", name, msg, stat & 0xff);
	if (stat & BUSY_STAT)	printk("Busy ");
	if (stat & READY_STAT)	printk("DriveReady ");
	if (stat & WRERR_STAT)	printk("WriteFault ");
	if (stat & SEEK_STAT)	printk("SeekComplete ");
	if (stat & DRQ_STAT)	printk("DataRequest ");
	if (stat & ECC_STAT)	printk("CorrectedError ");
	if (stat & INDEX_STAT)	printk("Index ");
	if (stat & ERR_STAT)	printk("Error ");
	printk("}\n");
	if ((stat & ERR_STAT) == 0) {
		hd_error = 0;
	} else {
		hd_error = inb(HD_ERROR);
		printk("%s: %s: error=0x%02x { ", name, msg, hd_error & 0xff);
		if (hd_error & BBD_ERR)		printk("BadSector ");
		if (hd_error & ECC_ERR)		printk("UncorrectableError ");
		if (hd_error & ID_ERR)		printk("SectorIdNotFound ");
		if (hd_error & ABRT_ERR)	printk("DriveStatusError ");
		if (hd_error & TRK0_ERR)	printk("TrackZeroNotFound ");
		if (hd_error & MARK_ERR)	printk("AddrMarkNotFound ");
		printk("}");
		if (hd_error & (BBD_ERR|ECC_ERR|ID_ERR|MARK_ERR)) {
			printk(", CHS=%d/%d/%d", (inb(HD_HCYL)<<8) + inb(HD_LCYL),
				inb(HD_CURRENT) & 0xf, inb(HD_SECTOR));
			if (CURRENT)
				printk(", sector=%ld", CURRENT->sector);
		}
		printk("\n");
	}
#else
	printk("%s: %s: status=0x%02x.\n", name, msg, stat & 0xff);
	if ((stat & ERR_STAT) == 0) {
		hd_error = 0;
	} else {
		hd_error = inb(HD_ERROR);
		printk("%s: %s: error=0x%02x.\n", name, msg, hd_error & 0xff);
	}
#endif
}

void check_status(void)
{
	int i = inb(HD_STATUS);

	if (!OK_STATUS(i)) {
		dump_status("check_status", i);
		bad_rw_intr();
	}
}

static int controller_busy(void)
{
	int retries = 100000;
	unsigned char status;

	do {
		status = inb(HD_STATUS);
	} while ((status & BUSY_STAT) && --retries);
	return status;
}

static int status_ok(void)
{
	unsigned char status = inb(HD_STATUS);

	if (status & BUSY_STAT)
		return 1;	/* Ancient, but does it make sense??? */
	if (status & WRERR_STAT)
		return 0;
	if (!(status & READY_STAT))
		return 0;
	if (!(status & SEEK_STAT))
		return 0;
	return 1;
}

static int controller_ready(unsigned int drive, unsigned int head)
{
	int retry = 100;

	do {
		if (controller_busy() & BUSY_STAT)
			return 0;
		outb(0xA0 | (drive<<4) | head, HD_CURRENT);
		if (status_ok())
			return 1;
	} while (--retry);
	return 0;
}

static void hd_out(struct hd_i_struct *disk,
		   unsigned int nsect,
		   unsigned int sect,
		   unsigned int head,
		   unsigned int cyl,
		   unsigned int cmd,
		   void (*intr_addr)(void))
{
	unsigned short port;

#if (HD_DELAY > 0)
	while (read_timer() - last_req < HD_DELAY)
		/* nothing */;
#endif
	if (reset)
		return;
	if (!controller_ready(disk->unit, head)) {
		reset = 1;
		return;
	}
	SET_HANDLER(intr_addr);
	outb(disk->ctl,HD_CMD);
	port=HD_DATA + 2;
	outb(disk->wpcom>>2, port); port += 2;
	outb(nsect, port); port += 2;
	outb(sect, port); port += 2;
	outb(cyl, port); port += 2;
	outb(cyl>>8, port); port += 2;
	outb(0xA0|(disk->unit<<4)|head, port); port += 2;
	outb(cmd, port);
}

static void hd_request (void);

static int drive_busy(void)
{
	unsigned int i;
	unsigned char c;

	for (i = 0; i < 500000 ; i++) {
		c = inb(HD_STATUS);
		if ((c & (BUSY_STAT | READY_STAT | SEEK_STAT)) == STAT_OK)
			return 0;
	}
	dump_status("reset timed out", c);
	return 1;
}

static void reset_controller(void)
{
	int	i;

	outb(4,HD_CMD);
	for(i = 0; i < 1000; i++) barrier();
	outb(hd_info[0].ctl & 0x0f,HD_CMD);
	for(i = 0; i < 1000; i++) barrier();
	if (drive_busy())
		printk("hd: controller still busy\n");
	else if ((hd_error = inb(HD_ERROR)) != 1)
		printk("hd: controller reset failed: %02x\n",hd_error);
}

static void reset_hd(void)
{
	static int i;

repeat:
	if (reset) {
		reset = 0;
		i = -1;
		reset_controller();
	} else {
		check_status();
		if (reset)
			goto repeat;
	}
	if (++i < NR_HD) {
		struct hd_i_struct *disk = &hd_info[i];
		disk->special_op = disk->recalibrate = 1;
		hd_out(disk, disk->sect, disk->sect, disk->head-1,
			disk->cyl, WIN_SPECIFY, &reset_hd);
		if (reset)
			goto repeat;
	} else
		hd_request();
}

/*
 * Ok, don't know what to do with the unexpected interrupts: on some machines
 * doing a reset and a retry seems to result in an eternal loop. Right now I
 * ignore it, and just set the timeout.
 *
 * On laptops (and "green" PCs), an unexpected interrupt occurs whenever the
 * drive enters "idle", "standby", or "sleep" mode, so if the status looks
 * "good", we just ignore the interrupt completely.
 */
void unexpected_hd_interrupt(void)
{
	unsigned int stat = inb(HD_STATUS);

	if (stat & (BUSY_STAT|DRQ_STAT|ECC_STAT|ERR_STAT)) {
		dump_status ("unexpected interrupt", stat);
		SET_TIMER;
	}
}

/*
 * bad_rw_intr() now tries to be a bit smarter and does things
 * according to the error returned by the controller.
 * -Mika Liljeberg (liljeber@cs.Helsinki.FI)
 */
static void bad_rw_intr(void)
{
	struct request *req = CURRENT;
	struct hd_i_struct *disk;

	if (!req)
		return;
	disk = req->rq_disk->private_data;
	if (++req->errors >= MAX_ERRORS || (hd_error & BBD_ERR)) {
		end_request(req, 0);
		disk->special_op = disk->recalibrate = 1;
	} else if (req->errors % RESET_FREQ == 0)
		reset = 1;
	else if ((hd_error & TRK0_ERR) || req->errors % RECAL_FREQ == 0)
		disk->special_op = disk->recalibrate = 1;
	/* Otherwise just retry */
}

static inline int wait_DRQ(void)
{
	int retries = 100000, stat;

	while (--retries > 0)
		if ((stat = inb(HD_STATUS)) & DRQ_STAT)
			return 0;
	dump_status("wait_DRQ", stat);
	return -1;
}

static void read_intr(void)
{
	int i, retries = 100000;
	struct request *req;

	do {
		i = (unsigned) inb(HD_STATUS);
		if (i & BUSY_STAT)
			continue;
		if (!OK_STATUS(i))
			break;
		if (i & DRQ_STAT)
			goto ok_to_read;
	} while (--retries > 0);
	dump_status("read_intr", i);
	bad_rw_intr();
	hd_request();
	return;
ok_to_read:
	req = CURRENT;
	insw(HD_DATA,req->buffer,256);
	req->sector++;
	req->buffer += 512;
	req->errors = 0;
	i = --req->nr_sectors;
	--req->current_nr_sectors;
#ifdef DEBUG
	printk("%s: read: sector %ld, remaining = %ld, buffer=%p\n",
		req->rq_disk->disk_name, req->sector, req->nr_sectors,
		req->buffer+512);
#endif
	if (req->current_nr_sectors <= 0)
		end_request(req, 1);
	if (i > 0) {
		SET_HANDLER(&read_intr);
		return;
	}
	(void) inb(HD_STATUS);
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	if (CURRENT)
		hd_request();
	return;
}

static void write_intr(void)
{
	int i;
	int retries = 100000;
	struct request *req = CURRENT;

	do {
		i = (unsigned) inb(HD_STATUS);
		if (i & BUSY_STAT)
			continue;
		if (!OK_STATUS(i))
			break;
		if ((req->nr_sectors <= 1) || (i & DRQ_STAT))
			goto ok_to_write;
	} while (--retries > 0);
	dump_status("write_intr", i);
	bad_rw_intr();
	hd_request();
	return;
ok_to_write:
	req->sector++;
	i = --req->nr_sectors;
	--req->current_nr_sectors;
	req->buffer += 512;
	if (!i || (req->bio && req->current_nr_sectors < 1))
		end_request(req, 1);
	if (i > 0) {
		SET_HANDLER(&write_intr);
		outsw(HD_DATA,req->buffer,256);
		local_irq_enable();
	} else {
#if (HD_DELAY > 0)
		last_req = read_timer();
#endif
		hd_request();
	}
	return;
}

static void recal_intr(void)
{
	check_status();
#if (HD_DELAY > 0)
	last_req = read_timer();
#endif
	hd_request();
}

/*
 * This is another of the error-routines I don't know what to do with. The
 * best idea seems to just set reset, and start all over again.
 */
static void hd_times_out(unsigned long dummy)
{
	do_hd = NULL;

	if (!CURRENT)
		return;

	disable_irq(HD_IRQ);
	local_irq_enable();
	reset = 1;
	printk("%s: timeout\n", CURRENT->rq_disk->disk_name);
	if (++CURRENT->errors >= MAX_ERRORS) {
#ifdef DEBUG
		printk("%s: too many errors\n", CURRENT->rq_disk->disk_name);
#endif
		end_request(CURRENT, 0);
	}
	local_irq_disable();
	hd_request();
	enable_irq(HD_IRQ);
}

int do_special_op(struct hd_i_struct *disk, struct request *req)
{
	if (disk->recalibrate) {
		disk->recalibrate = 0;
		hd_out(disk, disk->sect,0,0,0,WIN_RESTORE,&recal_intr);
		return reset;
	}
	if (disk->head > 16) {
		printk ("%s: cannot handle device with more than 16 heads - giving up\n", req->rq_disk->disk_name);
		end_request(req, 0);
	}
	disk->special_op = 0;
	return 1;
}

/*
 * The driver enables interrupts as much as possible.  In order to do this,
 * (a) the device-interrupt is disabled before entering hd_request(),
 * and (b) the timeout-interrupt is disabled before the sti().
 *
 * Interrupts are still masked (by default) whenever we are exchanging
 * data/cmds with a drive, because some drives seem to have very poor
 * tolerance for latency during I/O. The IDE driver has support to unmask
 * interrupts for non-broken hardware, so use that driver if required.
 */
static void hd_request(void)
{
	unsigned int block, nsect, sec, track, head, cyl;
	struct hd_i_struct *disk;
	struct request *req;

	if (do_hd)
		return;
repeat:
	del_timer(&device_timer);
	local_irq_enable();

	if (!CURRENT) {
		do_hd = NULL;
		return;
	}
	req = CURRENT;

	if (reset) {
		local_irq_disable();
		reset_hd();
		return;
	}
	disk = req->rq_disk->private_data;
	block = req->sector;
	nsect = req->nr_sectors;
	if (block >= get_capacity(req->rq_disk) ||
	    ((block+nsect) > get_capacity(req->rq_disk))) {
		printk("%s: bad access: block=%d, count=%d\n",
			req->rq_disk->disk_name, block, nsect);
		end_request(req, 0);
		goto repeat;
	}

	if (disk->special_op) {
		if (do_special_op(disk, req))
			goto repeat;
		return;
	}
	sec   = block % disk->sect + 1;
	track = block / disk->sect;
	head  = track % disk->head;
	cyl   = track / disk->head;
#ifdef DEBUG
	printk("%s: %sing: CHS=%d/%d/%d, sectors=%d, buffer=%p\n",
		req->rq_disk->disk_name, (req->cmd == READ)?"read":"writ",
		cyl, head, sec, nsect, req->buffer);
#endif
	if (req->flags & REQ_CMD) {
		switch (rq_data_dir(req)) {
		case READ:
			hd_out(disk,nsect,sec,head,cyl,WIN_READ,&read_intr);
			if (reset)
				goto repeat;
			break;
		case WRITE:
			hd_out(disk,nsect,sec,head,cyl,WIN_WRITE,&write_intr);
			if (reset)
				goto repeat;
			if (wait_DRQ()) {
				bad_rw_intr();
				goto repeat;
			}
			outsw(HD_DATA,req->buffer,256);
			break;
		default:
			printk("unknown hd-command\n");
			end_request(req, 0);
			break;
		}
	}
}

static void do_hd_request (request_queue_t * q)
{
	disable_irq(HD_IRQ);
	hd_request();
	enable_irq(HD_IRQ);
}

static int hd_ioctl(struct inode * inode, struct file * file,
	unsigned int cmd, unsigned long arg)
{
	struct hd_i_struct *disk = inode->i_bdev->bd_disk->private_data;
	struct hd_geometry *loc = (struct hd_geometry *) arg;
	struct hd_geometry g; 

	if (cmd != HDIO_GETGEO)
		return -EINVAL;
	if (!loc)
		return -EINVAL;
	g.heads = disk->head;
	g.sectors = disk->sect;
	g.cylinders = disk->cyl;
	g.start = get_start_sect(inode->i_bdev);
	return copy_to_user(loc, &g, sizeof g) ? -EFAULT : 0; 
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */

static void hd_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	void (*handler)(void) = do_hd;

	do_hd = NULL;
	del_timer(&device_timer);
	if (!handler)
		handler = unexpected_hd_interrupt;
	handler();
	local_irq_enable();
}

static struct block_device_operations hd_fops = {
	.ioctl =	hd_ioctl,
};

/*
 * This is the hard disk IRQ description. The SA_INTERRUPT in sa_flags
 * means we run the IRQ-handler with interrupts disabled:  this is bad for
 * interrupt latency, but anything else has led to problems on some
 * machines.
 *
 * We enable interrupts in some of the routines after making sure it's
 * safe.
 */

static int __init hd_init(void)
{
	int drive;
	if (register_blkdev(HD_MAJOR,"hd")) {
		printk("hd: unable to get major %d for hard disk\n",HD_MAJOR);
		return -1;
	}
	blk_init_queue(&hd_queue, do_hd_request, &hd_lock);
	blk_queue_max_sectors(&hd_queue, 255);
	init_timer(&device_timer);
	device_timer.function = hd_times_out;
	blk_queue_hardsect_size(&hd_queue, 512);

#ifdef __i386__
	if (!NR_HD) {
		extern struct drive_info drive_info;
		unsigned char *BIOS = (unsigned char *) &drive_info;
		unsigned long flags;
#ifndef CONFIG_X86_PC9800
		int cmos_disks;
#endif

		for (drive=0 ; drive<2 ; drive++) {
			hd_info[drive].cyl = *(unsigned short *) BIOS;
			hd_info[drive].head = *(3+BIOS);
			hd_info[drive].sect = *(2+BIOS);
			hd_info[drive].wpcom = 0;
			hd_info[drive].ctl = *(3+BIOS) > 8 ? 8 : 0;
			hd_info[drive].lzone = *(unsigned short *) BIOS;
			if (hd_info[drive].cyl && NR_HD == drive)
				NR_HD++;
			BIOS += 6;
		}

	}
#endif /* __i386__ */
#ifdef __arm__
	if (!NR_HD) {
		/* We don't know anything about the drive.  This means
		 * that you *MUST* specify the drive parameters to the
		 * kernel yourself.
		 */
		printk("hd: no drives specified - use hd=cyl,head,sectors"
			" on kernel command line\n");
	}
#endif
	if (!NR_HD)
		goto out;

	for (drive=0 ; drive < NR_HD ; drive++) {
		struct gendisk *disk = alloc_disk(64);
		struct hd_i_struct *p = &hd_info[drive];
		if (!disk)
			goto Enomem;
		disk->major = HD_MAJOR;
		disk->first_minor = drive << 6;
		disk->fops = &hd_fops;
		sprintf(disk->disk_name, "hd%c", 'a'+drive);
		disk->private_data = p;
		set_capacity(disk, p->head * p->sect * p->cyl);
		disk->queue = &hd_queue;
		p->unit = drive;
		hd_gendisk[drive] = disk;
		printk ("%s: %luMB, CHS=%d/%d/%d\n",
			disk->disk_name, (unsigned long)get_capacity(disk)/2048,
			p->cyl, p->head, p->sect);
	}

	if (request_irq(HD_IRQ, hd_interrupt, SA_INTERRUPT, "hd", NULL)) {
		printk("hd: unable to get IRQ%d for the hard disk driver\n",
			HD_IRQ);
		goto out1;
	}

	if (!request_region(HD_DATA, 2, "hd(data)")) {
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_DATA);
		NR_HD = 0;
		free_irq(HD_IRQ, NULL);
		return;
	}

	if (!request_region(HD_DATA + 2, 1, "hd"))
	{
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_DATA);
		goto out2;
	}

	if (!request_region(HD_DATA + 4, 1, "hd"))
	{
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_DATA);
		goto out3;
	}

	if (!request_region(HD_DATA + 6, 1, "hd"))
	{
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_DATA);
		goto out4;
	}

	if (!request_region(HD_DATA + 8, 1, "hd"))
	{
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_DATA);
		goto out5;
	}

	if (!request_region(HD_DATA + 10, 1, "hd"))
	{
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_DATA);
		goto out6;
	}

	if (!request_region(HD_DATA + 12, 1, "hd"))
	{
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_DATA);
		goto out7;
	}

	if (!request_region(HD_CMD, 1, "hd(cmd)"))
	{
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_CMD);
		goto out8;
	}

	if (!request_region(HD_CMD + 2, 1, "hd(cmd)"))
	{
		printk(KERN_WARNING "hd: port 0x%x busy\n", HD_CMD);
		goto out9;
	}

	for(drive=0; drive < NR_HD; drive++)
		add_disk(hd_gendisk[drive]);
	return 0;

out9:
	release_region(HD_CMD, 1);
out8:
	release_region(HD_DATA + 12, 1);
out7:
	release_region(HD_DATA + 10, 1);
out6:
	release_region(HD_DATA + 8, 1);
out5:
	release_region(HD_DATA + 6, 1);
out4:
	release_region(HD_DATA + 4, 1);
out3:
	release_region(HD_DATA + 2, 1);
out2:
	release_region(HD_DATA, 2);
	free_irq(HD_IRQ, NULL);
out1:
	for (drive = 0; drive < NR_HD; drive++)
		put_disk(hd_gendisk[drive]);
	NR_HD = 0;
out:
	del_timer(&device_timer);
	unregister_blkdev(HD_MAJOR,"hd");
	blk_cleanup_queue(&hd_queue);
	return -1;
Enomem:
	while (drive--)
		put_disk(hd_gendisk[drive]);
	goto out;
}

static int parse_hd_setup (char *line) {
	int ints[6];

	(void) get_options(line, ARRAY_SIZE(ints), ints);
	hd_setup(NULL, ints);

	return 1;
}
__setup("hd=", parse_hd_setup);

module_init(hd_init);
