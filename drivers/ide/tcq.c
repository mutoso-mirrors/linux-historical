/*
 * Copyright (C) 2001, 2002 Jens Axboe <axboe@suse.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Support for the DMA queued protocol, which enables ATA disk drives to
 * use tagged command queueing.
 */
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ide.h>

#include <asm/delay.h>

/*
 * warning: it will be _very_ verbose if defined
 */
#undef IDE_TCQ_DEBUG

#ifdef IDE_TCQ_DEBUG
#define TCQ_PRINTK printk
#else
#define TCQ_PRINTK(x...)
#endif

/*
 * use nIEN or not
 */
#undef IDE_TCQ_NIEN

/*
 * We are leaving the SERVICE interrupt alone, IBM drives have it
 * on per default and it can't be turned off. Doesn't matter, this
 * is the sane config.
 */
#undef IDE_TCQ_FIDDLE_SI

static ide_startstop_t ide_dmaq_intr(struct ata_device *drive, struct request *rq);
static ide_startstop_t service(struct ata_device *drive, struct request *rq);

static inline void drive_ctl_nien(struct ata_device *drive, int set)
{
#ifdef IDE_TCQ_NIEN
	if (IDE_CONTROL_REG) {
		int mask = set ? 0x02 : 0x00;

		OUT_BYTE(drive->ctl | mask, IDE_CONTROL_REG);
	}
#endif
}

static ide_startstop_t tcq_nop_handler(struct ata_device *drive, struct request *rq)
{
	struct ata_taskfile *args = rq->special;

	ide__sti();
	ide_end_drive_cmd(drive, rq, GET_STAT(), GET_ERR());
	kfree(args);
	return ide_stopped;
}

/*
 * If we encounter _any_ error doing I/O to one of the tags, we must
 * invalidate the pending queue. Clear the software busy queue and requeue
 * on the request queue for restart. Issue a WIN_NOP to clear hardware queue.
 */
static void tcq_invalidate_queue(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	ide_hwgroup_t *hwgroup = ch->hwgroup;
	request_queue_t *q = &drive->queue;
	struct ata_taskfile *args;
	struct request *rq;
	unsigned long flags;

	printk(KERN_INFO "ATA: %s: invalidating pending queue (%d)\n", drive->name, ata_pending_commands(drive));

	spin_lock_irqsave(&ide_lock, flags);

	del_timer(&ch->timer);

	if (test_bit(IDE_DMA, &ch->active))
		udma_stop(drive);

	blk_queue_invalidate_tags(q);

	drive->using_tcq = 0;
	drive->queue_depth = 1;
	clear_bit(IDE_BUSY, &ch->active);
	clear_bit(IDE_DMA, &ch->active);
	hwgroup->handler = NULL;

	/*
	 * Do some internal stuff -- we really need this command to be
	 * executed before any new commands are started. issue a NOP
	 * to clear internal queue on drive.
	 */
	args = kmalloc(sizeof(*args), GFP_ATOMIC);
	if (!args) {
		printk(KERN_ERR "ATA: %s: failed to issue NOP\n", drive->name);
		goto out;
	}

	rq = blk_get_request(&drive->queue, READ, GFP_ATOMIC);
	if (!rq)
		rq = blk_get_request(&drive->queue, WRITE, GFP_ATOMIC);

	/*
	 * blk_queue_invalidate_tags() just added back at least one command
	 * to the free list, so there _must_ be at least one free.
	 */
	BUG_ON(!rq);

	rq->special = args;
	args->taskfile.command = WIN_NOP;
	args->handler = tcq_nop_handler;
	args->command_type = IDE_DRIVE_TASK_NO_DATA;

	rq->rq_dev = mk_kdev(drive->channel->major, (drive->select.b.unit)<<PARTN_BITS);
	_elv_add_request(q, rq, 0, 0);

	/*
	 * make sure that nIEN is cleared
	 */
out:
	drive_ctl_nien(drive, 0);

	/*
	 * start doing stuff again
	 */
	q->request_fn(q);
	spin_unlock_irqrestore(&ide_lock, flags);
	printk(KERN_DEBUG "ATA: tcq_invalidate_queue: done\n");
}

static void ata_tcq_irq_timeout(unsigned long data)
{
	struct ata_device *drive = (struct ata_device *) data;
	struct ata_channel *ch = drive->channel;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	unsigned long flags;

	printk(KERN_ERR "ATA: %s: timeout waiting for interrupt...\n", __FUNCTION__);

	spin_lock_irqsave(&ide_lock, flags);

	if (test_and_set_bit(IDE_BUSY, &ch->active))
		printk(KERN_ERR "ATA: %s: hwgroup not busy\n", __FUNCTION__);
	if (hwgroup->handler == NULL)
		printk(KERN_ERR "ATA: %s: missing isr!\n", __FUNCTION__);

	spin_unlock_irqrestore(&ide_lock, flags);

	/*
	 * if pending commands, try service before giving up
	 */
	if (ata_pending_commands(drive) && (GET_STAT() & SERVICE_STAT))
		if (service(drive, drive->rq) == ide_started)
			return;

	if (drive)
		tcq_invalidate_queue(drive);
}

static void set_irq(struct ata_device *drive, ata_handler_t *handler)
{
	struct ata_channel *ch = drive->channel;
	ide_hwgroup_t *hwgroup = HWGROUP(drive);
	unsigned long flags;

	spin_lock_irqsave(&ide_lock, flags);

	/*
	 * always just bump the timer for now, the timeout handling will
	 * have to be changed to be per-command
	 *
	 * FIXME: Jens - this is broken it will interfere with
	 * the normal timer function on serialized drives!
	 */

	ch->timer.function = ata_tcq_irq_timeout;
	ch->timer.data = (unsigned long) ch->drive;
	mod_timer(&ch->timer, jiffies + 5 * HZ);

	hwgroup->handler = handler;
	spin_unlock_irqrestore(&ide_lock, flags);
}

/*
 * wait 400ns, then poll for busy_mask to clear from alt status
 */
#define IDE_TCQ_WAIT	(10000)
static int wait_altstat(struct ata_device *drive, u8 *stat, u8 busy_mask)
{
	int i = 0;

	udelay(1);

	while ((*stat = GET_ALTSTAT()) & busy_mask) {
		if (unlikely(i++ > IDE_TCQ_WAIT))
			return 1;

		udelay(10);
	}

	return 0;
}

static ide_startstop_t udma_tcq_start(struct ata_device *drive, struct request *rq);

/*
 * issue SERVICE command to drive -- drive must have been selected first,
 * and it must have reported a need for service (status has SERVICE_STAT set)
 *
 * Also, nIEN must be set as not to need protection against ide_dmaq_intr
 */
static ide_startstop_t service(struct ata_device *drive, struct request *rq)
{
	u8 feat;
	u8 stat;
	int tag;

	TCQ_PRINTK("%s: started service\n", drive->name);

	/*
	 * Could be called with IDE_DMA in-progress from invalidate
	 * handler, refuse to do anything.
	 */
	if (test_bit(IDE_DMA, &drive->channel->active))
		return ide_stopped;

	/*
	 * need to select the right drive first...
	 */
	if (drive != drive->channel->drive) {
		SELECT_DRIVE(drive->channel, drive);
		udelay(10);
	}

	drive_ctl_nien(drive, 1);

	/*
	 * send SERVICE, wait 400ns, wait for BUSY_STAT to clear
	 */
	OUT_BYTE(WIN_QUEUED_SERVICE, IDE_COMMAND_REG);

	if (wait_altstat(drive, &stat, BUSY_STAT)) {
		printk(KERN_ERR"%s: BUSY clear took too long\n", __FUNCTION__);
		ide_dump_status(drive, rq, __FUNCTION__, stat);
		tcq_invalidate_queue(drive);

		return ide_stopped;
	}

	drive_ctl_nien(drive, 0);

	/*
	 * FIXME, invalidate queue
	 */
	if (stat & ERR_STAT) {
		ide_dump_status(drive, rq, __FUNCTION__, stat);
		tcq_invalidate_queue(drive);

		return ide_stopped;
	}

	/*
	 * should not happen, a buggy device could introduce loop
	 */
	if ((feat = GET_FEAT()) & NSEC_REL) {
		drive->rq = NULL;
		printk("%s: release in service\n", drive->name);
		return ide_stopped;
	}

	tag = feat >> 3;

	TCQ_PRINTK("%s: stat %x, feat %x\n", __FUNCTION__, stat, feat);

	rq = blk_queue_tag_request(&drive->queue, tag);
	if (!rq) {
		printk(KERN_ERR"%s: missing request for tag %d\n", __FUNCTION__, tag);
		return ide_stopped;
	}

	drive->rq = rq;

	/*
	 * we'll start a dma read or write, device will trigger
	 * interrupt to indicate end of transfer, release is not allowed
	 */
	TCQ_PRINTK("%s: starting command %x\n", __FUNCTION__, stat);
	return udma_tcq_start(drive, rq);
}

static ide_startstop_t check_service(struct ata_device *drive, struct request *rq)
{
	u8 stat;

	TCQ_PRINTK("%s: %s\n", drive->name, __FUNCTION__);

	if (!ata_pending_commands(drive))
		return ide_stopped;

	if ((stat = GET_STAT()) & SERVICE_STAT)
		return service(drive, rq);

	/*
	 * we have pending commands, wait for interrupt
	 */
	set_irq(drive, ide_dmaq_intr);

	return ide_started;
}

ide_startstop_t ide_dmaq_complete(struct ata_device *drive, struct request *rq, u8 stat)
{
	u8 dma_stat;

	/*
	 * transfer was in progress, stop DMA engine
	 */
	dma_stat = udma_stop(drive);

	/*
	 * must be end of I/O, check status and complete as necessary
	 */
	if (unlikely(!OK_STAT(stat, READY_STAT, drive->bad_wstat | DRQ_STAT))) {
		printk(KERN_ERR "%s: %s: error status %x\n", __FUNCTION__, drive->name,stat);
		ide_dump_status(drive, rq, __FUNCTION__, stat);
		tcq_invalidate_queue(drive);

		return ide_stopped;
	}

	if (dma_stat)
		printk("%s: bad DMA status (dma_stat=%x)\n", drive->name, dma_stat);

	TCQ_PRINTK("%s: ending %p, tag %d\n", __FUNCTION__, rq, rq->tag);
	__ide_end_request(drive, rq, !dma_stat, rq->nr_sectors);

	/*
	 * we completed this command, check if we can service a new command
	 */
	return check_service(drive, rq);
}

/*
 * intr handler for queued dma operations. this can be entered for two
 * reasons:
 *
 * 1) device has completed dma transfer
 * 2) service request to start a command
 *
 * if the drive has an active tag, we first complete that request before
 * processing any pending SERVICE.
 */
static ide_startstop_t ide_dmaq_intr(struct ata_device *drive, struct request *rq)
{
	u8 stat = GET_STAT();

	TCQ_PRINTK("%s: stat=%x\n", __FUNCTION__, stat);

	/*
	 * if a command completion interrupt is pending, do that first and
	 * check service afterwards
	 */
	if (rq)
		return ide_dmaq_complete(drive, rq, stat);

	/*
	 * service interrupt
	 */
	if (stat & SERVICE_STAT) {
		TCQ_PRINTK("%s: SERV (stat=%x)\n", __FUNCTION__, stat);
		return service(drive, rq);
	}

	printk("%s: stat=%x, not expected\n", __FUNCTION__, stat);
	return check_service(drive, rq);
}

/*
 * Check if the ata adapter this drive is attached to supports the
 * NOP auto-poll for multiple tcq enabled drives on one channel.
 */
static int check_autopoll(struct ata_device *drive)
{
	struct ata_channel *ch = drive->channel;
	struct ata_taskfile args;
	int drives = 0, i;

	/*
	 * only need to probe if both drives on a channel support tcq
	 */
	for (i = 0; i < MAX_DRIVES; i++)
		if (drive->channel->drives[i].present &&drive->type == ATA_DISK)
			drives++;

	if (drives <= 1)
		return 0;

	memset(&args, 0, sizeof(args));

	args.taskfile.feature = 0x01;
	args.taskfile.command = WIN_NOP;
	ide_cmd_type_parser(&args);

	/*
	 * do taskfile and check ABRT bit -- intelligent adapters will not
	 * pass NOP with sub-code 0x01 to device, so the command will not
	 * fail there
	 */
	ide_raw_taskfile(drive, &args);
	if (args.taskfile.feature & ABRT_ERR)
		return 1;

	ch->auto_poll = 1;
	printk("%s: NOP Auto-poll enabled\n", ch->name);
	return 0;
}

/*
 * configure the drive for tcq
 */
static int configure_tcq(struct ata_device *drive)
{
	int tcq_mask = 1 << 1 | 1 << 14;
	int tcq_bits = tcq_mask | 1 << 15;
	struct ata_taskfile args;

	/*
	 * bit 14 and 1 must be set in word 83 of the device id to indicate
	 * support for dma queued protocol, and bit 15 must be cleared
	 */
	if ((drive->id->command_set_2 & tcq_bits) ^ tcq_mask)
		return -EIO;

	memset(&args, 0, sizeof(args));
	args.taskfile.feature = SETFEATURES_EN_WCACHE;
	args.taskfile.command = WIN_SETFEATURES;
	ide_cmd_type_parser(&args);

	if (ide_raw_taskfile(drive, &args)) {
		printk("%s: failed to enable write cache\n", drive->name);
		return 1;
	}

	/*
	 * disable RELease interrupt, it's quicker to poll this after
	 * having sent the command opcode
	 */
	memset(&args, 0, sizeof(args));
	args.taskfile.feature = SETFEATURES_DIS_RI;
	args.taskfile.command = WIN_SETFEATURES;
	ide_cmd_type_parser(&args);

	if (ide_raw_taskfile(drive, &args)) {
		printk("%s: disabling release interrupt fail\n", drive->name);
		return 1;
	}

#ifdef IDE_TCQ_FIDDLE_SI
	/*
	 * enable SERVICE interrupt
	 */
	memset(&args, 0, sizeof(args));
	args.taskfile.feature = SETFEATURES_EN_SI;
	args.taskfile.command = WIN_SETFEATURES;
	ide_cmd_type_parser(&args);

	if (ide_raw_taskfile(drive, &args)) {
		printk("%s: enabling service interrupt fail\n", drive->name);
		return 1;
	}
#endif

	return 0;
}

static int tcq_wait_dataphase(struct ata_device *drive)
{
	u8 stat;
	int i;

	while ((stat = GET_STAT()) & BUSY_STAT)
		udelay(10);

	if (OK_STAT(stat, READY_STAT | DRQ_STAT, drive->bad_wstat))
		return 0;

	i = 0;
	udelay(1);
	while (!OK_STAT(GET_STAT(), READY_STAT | DRQ_STAT, drive->bad_wstat)) {
		if (unlikely(i++ > IDE_TCQ_WAIT))
			return 1;

		udelay(10);
	}

	return 0;
}

/****************************************************************************
 * UDMA transfer handling functions.
 */

/*
 * Invoked from a SERVICE interrupt, command etc already known.  Just need to
 * start the dma engine for this tag.
 */
static ide_startstop_t udma_tcq_start(struct ata_device *drive, struct request *rq)
{
	struct ata_channel *ch = drive->channel;

	TCQ_PRINTK("%s: setting up queued %d\n", __FUNCTION__, rq->tag);
	if (!test_bit(IDE_BUSY, &ch->active))
		printk("queued_rw: IDE_BUSY not set\n");

	if (tcq_wait_dataphase(drive))
		return ide_stopped;

	if (ata_start_dma(drive, rq))
		return ide_stopped;

	set_irq(drive, ide_dmaq_intr);
	if (!udma_start(drive, rq))
		return ide_started;

	return ide_stopped;
}

/*
 * Start a queued command from scratch.
 */
ide_startstop_t udma_tcq_taskfile(struct ata_device *drive, struct request *rq)
{
	u8 stat;
	u8 feat;

	struct ata_taskfile *args = rq->special;

	TCQ_PRINTK("%s: start tag %d\n", drive->name, rq->tag);

	/*
	 * set nIEN, tag start operation will enable again when
	 * it is safe
	 */
	drive_ctl_nien(drive, 1);

	OUT_BYTE(args->taskfile.command, IDE_COMMAND_REG);

	if (wait_altstat(drive, &stat, BUSY_STAT)) {
		ide_dump_status(drive, rq, "queued start", stat);
		tcq_invalidate_queue(drive);
		return ide_stopped;
	}

	drive_ctl_nien(drive, 0);

	if (stat & ERR_STAT) {
		ide_dump_status(drive, rq, "tcq_start", stat);
		return ide_stopped;
	}

	/*
	 * drive released the bus, clear active tag and
	 * check for service
	 */
	if ((feat = GET_FEAT()) & NSEC_REL) {
		drive->immed_rel++;
		drive->rq = NULL;
		set_irq(drive, ide_dmaq_intr);

		TCQ_PRINTK("REL in queued_start\n");

		if ((stat = GET_STAT()) & SERVICE_STAT)
			return service(drive, rq);

		return ide_released;
	}

	TCQ_PRINTK("IMMED in queued_start\n");
	drive->immed_comp++;

	return udma_tcq_start(drive, rq);
}

/*
 * For now assume that command list is always as big as we need and don't
 * attempt to shrink it on tcq disable.
 */
int udma_tcq_enable(struct ata_device *drive, int on)
{
	int depth = drive->using_tcq ? drive->queue_depth : 0;

	/*
	 * disable or adjust queue depth
	 */
	if (!on) {
		if (drive->using_tcq)
			printk("%s: TCQ disabled\n", drive->name);
		drive->using_tcq = 0;
		return 0;
	}

	if (configure_tcq(drive)) {
		drive->using_tcq = 0;
		return 1;
	}

	/*
	 * enable block tagging
	 */
	if (!blk_queue_tagged(&drive->queue))
		blk_queue_init_tags(&drive->queue, IDE_MAX_TAG);

	/*
	 * check auto-poll support
	 */
	check_autopoll(drive);

	if (depth != drive->queue_depth)
		printk("%s: tagged command queueing enabled, command queue depth %d\n", drive->name, drive->queue_depth);

	drive->using_tcq = 1;
	return 0;
}
