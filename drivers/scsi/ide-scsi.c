/*
 * linux/drivers/scsi/ide-scsi.c	Version 0.2 - ALPHA	Jan  26, 1997
 *
 * Copyright (C) 1996, 1997 Gadi Oxman <gadio@netvision.net.il>
 */

/*
 * Emulation of a SCSI host adapter for IDE ATAPI devices.
 *
 * With this driver, one can use the Linux SCSI drivers instead of the
 * native IDE ATAPI drivers.
 *
 * Ver 0.1   Dec  3 96   Initial version.
 * Ver 0.2   Jan 26 97   Fixed bug in cleanup_module() and added emulation
 *                        of MODE_SENSE_6/MODE_SELECT_6 for cdroms. Thanks
 *                        to Janos Farkas for pointing this out.
 *                       Avoid using bitfields in structures for m68k.
 *                       Added Scather/Gather and DMA support.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/errno.h>
#include <linux/hdreg.h>
#include <linux/malloc.h>

#include <asm/io.h>
#include <asm/bitops.h>

#include "../block/ide.h"

#include "scsi.h"
#include "hosts.h"
#include "sd.h"
#include "ide-scsi.h"

#define IDESCSI_DEBUG_LOG		0

typedef struct idescsi_pc_s {
	u8 c[12];				/* Actual packet bytes */
	int request_transfer;			/* Bytes to transfer */
	int actually_transferred;		/* Bytes actually transferred */
	int buffer_size;			/* Size of our data buffer */
	struct request *rq;			/* The corresponding request */
	byte *buffer;				/* Data buffer */
	byte *current_position;			/* Pointer into the above buffer */
	struct scatterlist *sg;			/* Scather gather table */
	int b_count;				/* Bytes transferred from current entry */
	Scsi_Cmnd *scsi_cmd;			/* SCSI command */
	void (*done)(Scsi_Cmnd *);		/* Scsi completion routine */
	unsigned int flags;			/* Status/Action flags */
} idescsi_pc_t;

/*
 *	Packet command status bits.
 */
#define PC_DMA_IN_PROGRESS		0	/* 1 while DMA in progress */
#define PC_WRITING			1	/* Data direction */

typedef struct {
	ide_drive_t *drive;
	idescsi_pc_t *pc;			/* Current packet command */
	unsigned int flags;			/* Status/Action flags */
} idescsi_scsi_t;

/*
 *	Per ATAPI device status bits.
 */
#define IDESCSI_DRQ_INTERRUPT		0	/* DRQ interrupt device */

/*
 *	ide-scsi requests.
 */
#define IDESCSI_PC_RQ			90

/*
 *	Bits of the interrupt reason register.
 */
#define IDESCSI_IREASON_COD	0x1		/* Information transferred is command */
#define IDESCSI_IREASON_IO	0x2		/* The device requests us to read */

static void idescsi_discard_data (ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		IN_BYTE (IDE_DATA_REG);
}

static void idescsi_output_zeros (ide_drive_t *drive, unsigned int bcount)
{
	while (bcount--)
		OUT_BYTE (0, IDE_DATA_REG);
}

/*
 *	PIO data transfer routines using the scather gather table.
 */
static void idescsi_input_buffers (ide_drive_t *drive, idescsi_pc_t *pc, unsigned int bcount)
{
	int count;

	while (bcount) {
		if (pc->sg - (struct scatterlist *) pc->scsi_cmd->request_buffer > pc->scsi_cmd->use_sg) {
			printk (KERN_ERR "ide-scsi: scather gather table too small, discarding data\n");
			idescsi_discard_data (drive, bcount);
			return;
		}
		count = IDE_MIN (pc->sg->length - pc->b_count, bcount);
		atapi_input_bytes (drive, pc->sg->address + pc->b_count, count);
		bcount -= count; pc->b_count += count;
		if (pc->b_count == pc->sg->length) {
			pc->sg++;
			pc->b_count = 0;
		}
	}
}

static void idescsi_output_buffers (ide_drive_t *drive, idescsi_pc_t *pc, unsigned int bcount)
{
	int count;

	while (bcount) {
		if (pc->sg - (struct scatterlist *) pc->scsi_cmd->request_buffer > pc->scsi_cmd->use_sg) {
			printk (KERN_ERR "ide-scsi: scather gather table too small, padding with zeros\n");
			idescsi_output_zeros (drive, bcount);
			return;
		}
		count = IDE_MIN (pc->sg->length - pc->b_count, bcount);
		atapi_output_bytes (drive, pc->sg->address + pc->b_count, count);
		bcount -= count; pc->b_count += count;
		if (pc->b_count == pc->sg->length) {
			pc->sg++;
			pc->b_count = 0;
		}
	}
}

/*
 *	Most of the SCSI commands are supported directly by ATAPI devices.
 *	idescsi_transform_pc handles the few exceptions.
 */
static inline void idescsi_transform_pc1 (ide_drive_t *drive, idescsi_pc_t *pc)
{
	u8 *c = pc->c, *buf = pc->buffer, *sc = pc->scsi_cmd->cmnd;
	int i;

	if (drive->media == ide_cdrom) {
		if (c[0] == READ_6) {
			c[8] = c[4];		c[5] = c[3];		c[4] = c[2];
			c[3] = c[1] & 0x1f;	c[2] = 0;		c[1] &= 0xe0;
			c[0] = READ_10;
		}
		if (c[0] == MODE_SENSE || (c[0] == MODE_SELECT && buf[3] == 8)) {
			pc->request_transfer -= 4;
			memset (c, 0, 12);
			c[0] = sc[0] | 0x40;	c[2] = sc[2];		c[8] = sc[4] - 4;
			if (c[0] == MODE_SENSE_10) return;
			for (i = 0; i <= 7; i++) buf[i] = 0;
			for (i = 8; i < pc->buffer_size - 4; i++) buf[i] = buf[i + 4];
		}
	}
}

static inline void idescsi_transform_pc2 (ide_drive_t *drive, idescsi_pc_t *pc)
{
	u8 *buf = pc->buffer;
	int i;

	if (drive->media == ide_cdrom) {
		if (pc->c[0] == MODE_SENSE_10 && pc->scsi_cmd->cmnd[0] == MODE_SENSE) {
			buf[0] = buf[1];	buf[1] = buf[2];
			buf[2] = 0;		buf[3] = 8;
			for (i = pc->buffer_size - 1; i >= 12; i--) buf[i] = buf[i - 4];
			for (i = 11; i >= 4; i--) buf[i] = 0;
		}
	}
}

static inline void idescsi_free_bh (struct buffer_head *bh)
{
	struct buffer_head *bhp;

	while (bh) {
		bhp = bh;
		bh = bh->b_reqnext;
		kfree (bhp);
	}
}

static void idescsi_end_request (byte uptodate, ide_hwgroup_t *hwgroup)
{
	ide_drive_t *drive = hwgroup->drive;
	idescsi_scsi_t *scsi = drive->driver_data;
	struct request *rq = hwgroup->rq;
	idescsi_pc_t *pc = (idescsi_pc_t *) rq->buffer;

	if (rq->cmd != IDESCSI_PC_RQ) {
		ide_end_request (uptodate, hwgroup);
		return;
	}
	ide_end_drive_cmd (drive, 0, 0);
	if (rq->errors >= ERROR_MAX) {
#if IDESCSI_DEBUG_LOG
		printk ("ide-scsi: %s: I/O error for %lu\n", drive->name, pc->scsi_cmd->serial_number);
#endif /* IDESCSI_DEBUG_LOG */
		pc->scsi_cmd->result = DID_ERROR << 16;
	} else if (rq->errors) {
#if IDESCSI_DEBUG_LOG
		printk ("ide-scsi: %s: check condition for %lu\n", drive->name, pc->scsi_cmd->serial_number);
#endif /* IDESCSI_DEBUG_LOG */
		pc->scsi_cmd->result = (CHECK_CONDITION << 1) | (DID_OK << 16);
	} else {
#if IDESCSI_DEBUG_LOG
		printk ("ide-scsi: %s: success for %lu\n", drive->name, pc->scsi_cmd->serial_number);
#endif /* IDESCSI_DEBUG_LOG */
		pc->scsi_cmd->result = DID_OK << 16;
		idescsi_transform_pc2 (drive, pc);
	}
	pc->done(pc->scsi_cmd);
	idescsi_free_bh (rq->bh);
	kfree(pc); kfree(rq);
	scsi->pc = NULL;
}

/*
 *	Our interrupt handler.
 */
static void idescsi_pc_intr (ide_drive_t *drive)
{
	idescsi_scsi_t *scsi = drive->driver_data;
	byte status, ireason;
	int bcount;
	idescsi_pc_t *pc=scsi->pc;
	struct request *rq = pc->rq;
	unsigned int temp;

#if IDESCSI_DEBUG_LOG
	printk (KERN_INFO "ide-scsi: Reached idescsi_pc_intr interrupt handler\n");
#endif /* IDESCSI_DEBUG_LOG */

	if (test_and_clear_bit (PC_DMA_IN_PROGRESS, &pc->flags)) {
#if IDESCSI_DEBUG_LOG
		printk ("ide-scsi: %s: DMA complete\n", drive->name);
#endif /* IDESCSI_DEBUG_LOG */
		pc->actually_transferred=pc->request_transfer;
		(void) (HWIF(drive)->dmaproc(ide_dma_abort, drive));
	}

	status = GET_STAT();						/* Clear the interrupt */

	if ((status & DRQ_STAT) == 0) {					/* No more interrupts */
#if IDESCSI_DEBUG_LOG
		printk (KERN_INFO "Packet command completed, %d bytes transferred\n", pc->actually_transferred);
#endif /* IDESCSI_DEBUG_LOG */
		ide_sti();
		if (status & ERR_STAT)
			rq->errors++;
		idescsi_end_request (1, HWGROUP(drive));
		return;
	}
	bcount = IN_BYTE (IDE_BCOUNTH_REG) << 8 | IN_BYTE (IDE_BCOUNTL_REG);
	ireason = IN_BYTE (IDE_IREASON_REG);

	if (ireason & IDESCSI_IREASON_COD) {
		printk (KERN_ERR "ide-scsi: CoD != 0 in idescsi_pc_intr\n");
		ide_do_reset (drive);
		return;
	}
	if (ireason & IDESCSI_IREASON_IO) {
		temp = pc->actually_transferred + bcount;
		if ( temp > pc->request_transfer) {
			if (temp > pc->buffer_size) {
				printk (KERN_ERR "ide-scsi: The scsi wants to send us more data than expected - discarding data\n");
				idescsi_discard_data (drive,bcount);
				ide_set_handler (drive,&idescsi_pc_intr,WAIT_CMD);
				return;
			}
#if IDESCSI_DEBUG_LOG
			printk (KERN_NOTICE "ide-scsi: The scsi wants to send us more data than expected - allowing transfer\n");
#endif /* IDESCSI_DEBUG_LOG */
		}
	}
	if (ireason & IDESCSI_IREASON_IO) {
		if (pc->sg)
			idescsi_input_buffers (drive, pc, bcount);
		else
			atapi_input_bytes (drive,pc->current_position,bcount);
	} else {
		if (pc->sg)
			idescsi_output_buffers (drive, pc, bcount);
		else
			atapi_output_bytes (drive,pc->current_position,bcount);
	}
	pc->actually_transferred+=bcount;				/* Update the current position */
	pc->current_position+=bcount;

	ide_set_handler (drive,&idescsi_pc_intr,WAIT_CMD);		/* And set the interrupt handler again */
}

static void idescsi_transfer_pc (ide_drive_t *drive)
{
	idescsi_scsi_t *scsi = drive->driver_data;
	byte ireason;

	if (ide_wait_stat (drive,DRQ_STAT,BUSY_STAT,WAIT_READY)) {
		printk (KERN_ERR "ide-scsi: Strange, packet command initiated yet DRQ isn't asserted\n");
		return;
	}
	ireason = IN_BYTE (IDE_IREASON_REG);
	if ((ireason & (IDESCSI_IREASON_IO | IDESCSI_IREASON_COD)) != IDESCSI_IREASON_COD) {
		printk (KERN_ERR "ide-scsi: (IO,CoD) != (0,1) while issuing a packet command\n");
		ide_do_reset (drive);
		return;
	}
	ide_set_handler (drive, &idescsi_pc_intr, WAIT_CMD);	/* Set the interrupt routine */
	atapi_output_bytes (drive, scsi->pc->c, 12);		/* Send the actual packet */
}

/*
 *	Issue a packet command
 */
static void idescsi_issue_pc (ide_drive_t *drive, idescsi_pc_t *pc)
{
	idescsi_scsi_t *scsi = drive->driver_data;
	int bcount;
	struct request *rq = pc->rq;
	int dma_ok = 0;

	scsi->pc=pc;							/* Set the current packet command */
	pc->actually_transferred=0;					/* We haven't transferred any data yet */
	pc->current_position=pc->buffer;
	bcount = IDE_MIN (pc->request_transfer, 63 * 1024);		/* Request to transfer the entire buffer at once */

	if (drive->using_dma && rq->bh)
		dma_ok=!HWIF(drive)->dmaproc(test_bit (PC_WRITING, &pc->flags) ? ide_dma_write : ide_dma_read, drive);

	OUT_BYTE (drive->ctl,IDE_CONTROL_REG);
	OUT_BYTE (dma_ok,IDE_FEATURE_REG);
	OUT_BYTE (bcount >> 8,IDE_BCOUNTH_REG);
	OUT_BYTE (bcount & 0xff,IDE_BCOUNTL_REG);
	OUT_BYTE (drive->select.all,IDE_SELECT_REG);

	if (dma_ok) {
		set_bit (PC_DMA_IN_PROGRESS, &pc->flags);
		(void) (HWIF(drive)->dmaproc(ide_dma_begin, drive));
	}
	if (test_bit (IDESCSI_DRQ_INTERRUPT, &scsi->flags)) {
		ide_set_handler (drive, &idescsi_transfer_pc, WAIT_CMD);
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG);		/* Issue the packet command */
	} else {
		OUT_BYTE (WIN_PACKETCMD, IDE_COMMAND_REG);
		idescsi_transfer_pc (drive);
	}
}

/*
 *	idescsi_do_request is our request handling function.
 */
static void idescsi_do_request (ide_drive_t *drive, struct request *rq, unsigned long block)
{
#if IDESCSI_DEBUG_LOG
	printk (KERN_INFO "rq_status: %d, rq_dev: %u, cmd: %d, errors: %d\n",rq->rq_status,(unsigned int) rq->rq_dev,rq->cmd,rq->errors);
	printk (KERN_INFO "sector: %ld, nr_sectors: %ld, current_nr_sectors: %ld\n",rq->sector,rq->nr_sectors,rq->current_nr_sectors);
#endif /* IDESCSI_DEBUG_LOG */

	if (rq->cmd == IDESCSI_PC_RQ) {
		idescsi_issue_pc (drive, (idescsi_pc_t *) rq->buffer);
		return;
	}
	printk (KERN_ERR "ide-scsi: %s: unsupported command in request queue (%x)\n", drive->name, rq->cmd);
	idescsi_end_request (0,HWGROUP (drive));
}

static int idescsi_open (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	MOD_INC_USE_COUNT;
	return 0;
}

static void idescsi_ide_release (struct inode *inode, struct file *filp, ide_drive_t *drive)
{
	MOD_DEC_USE_COUNT;
}

static ide_drive_t *idescsi_drives[MAX_HWIFS * MAX_DRIVES];
static int idescsi_initialized = 0;

/*
 *	Driver initialization.
 */
static void idescsi_setup (ide_drive_t *drive, idescsi_scsi_t *scsi, int id)
{
	DRIVER(drive)->busy++;
	idescsi_drives[id] = drive;
	drive->driver_data = scsi;
	drive->ready_stat = 0;
	memset (scsi, 0, sizeof (idescsi_scsi_t));
	scsi->drive = drive;
	if (drive->id && (drive->id->config & 0x0060) == 0x20)
		set_bit (IDESCSI_DRQ_INTERRUPT, &scsi->flags);
}

static int idescsi_cleanup (ide_drive_t *drive)
{
	idescsi_scsi_t *scsi = drive->driver_data;

	if (ide_unregister_subdriver (drive))
		return 1;
	drive->driver_data = NULL;
	kfree (scsi);
	return 0;
}

int idescsi_init (void);
static ide_module_t idescsi_module = {
	IDE_DRIVER_MODULE,
	idescsi_init,
	NULL
};

/*
 *	IDE subdriver functions, registered with ide.c
 */
static ide_driver_t idescsi_driver = {
	ide_scsi,		/* media */
	0,			/* busy */
	1,			/* supports_dma */
	0,			/* supports_dsc_overlap */
	idescsi_cleanup,	/* cleanup */
	idescsi_do_request,	/* do_request */
	idescsi_end_request,	/* end_request */
	NULL,			/* ioctl */
	idescsi_open,		/* open */
	idescsi_ide_release,	/* release */
	NULL,			/* media_change */
	NULL,			/* pre_reset */
	NULL,			/* capacity */
	NULL			/* special */
};

static struct proc_dir_entry idescsi_proc_dir = {PROC_SCSI_IDESCSI, 8, "ide-scsi", S_IFDIR | S_IRUGO | S_IXUGO, 2};

/*
 *	idescsi_init will register the driver for each scsi.
 */
int idescsi_init (void)
{
	ide_drive_t *drive;
	idescsi_scsi_t *scsi;
	byte media[] = {TYPE_DISK, TYPE_TAPE, TYPE_PROCESSOR, TYPE_WORM, TYPE_ROM, TYPE_SCANNER, TYPE_MOD, 255};
	int i, failed, id;

	if (idescsi_initialized)
		return 0;
	idescsi_initialized = 1;
	for (i = 0; i < MAX_HWIFS * MAX_DRIVES; i++)
		idescsi_drives[i] = NULL;
	MOD_INC_USE_COUNT;
	for (i = 0; media[i] != 255; i++) {
		failed = 0;
		while ((drive = ide_scan_devices (media[i], NULL, failed++)) != NULL) {
			if ((scsi = (idescsi_scsi_t *) kmalloc (sizeof (idescsi_scsi_t), GFP_KERNEL)) == NULL) {
				printk (KERN_ERR "ide-scsi: %s: Can't allocate a scsi structure\n", drive->name);
				continue;
			}
			if (ide_register_subdriver (drive, &idescsi_driver, IDE_SUBDRIVER_VERSION)) {
				printk (KERN_ERR "ide-scsi: %s: Failed to register the driver with ide.c\n", drive->name);
				kfree (scsi);
				continue;
			}
			for (id = 0; id < MAX_HWIFS * MAX_DRIVES && idescsi_drives[id]; id++);
			idescsi_setup (drive, scsi, id);
			failed--;
		}
	}
	ide_register_module(&idescsi_module);
	MOD_DEC_USE_COUNT;
	return 0;
}

int idescsi_detect (Scsi_Host_Template *host_template)
{
	struct Scsi_Host *host;
	int id;

	host_template->proc_dir = &idescsi_proc_dir;
	host = scsi_register(host_template, 0);
	for (id = 0; id < MAX_HWIFS * MAX_DRIVES && idescsi_drives[id]; id++);
	host->max_id = id;
	host->can_queue = host->cmd_per_lun * id;
	return 1;
}

int idescsi_release (struct Scsi_Host *host)
{
	ide_drive_t *drive;
	int id;

	for (id = 0; id < MAX_HWIFS * MAX_DRIVES; id++) {
		drive = idescsi_drives[id];
		if (drive)
			DRIVER(drive)->busy--;
	}
	return 0;
}

const char *idescsi_info (struct Scsi_Host *host)
{
	return "SCSI host adapter emulation for IDE ATAPI devices";
}

static inline struct buffer_head *idescsi_kmalloc_bh (int count)
{
	struct buffer_head *bh, *bhp, *first_bh;

	if ((first_bh = bhp = bh = kmalloc (sizeof(struct buffer_head), GFP_ATOMIC)) == NULL)
		goto abort;
	memset (bh, 0, sizeof (struct buffer_head));
	bh->b_reqnext = NULL;
	while (--count) {
		if ((bh = kmalloc (sizeof(struct buffer_head), GFP_ATOMIC)) == NULL)
			goto abort;
		memset (bh, 0, sizeof (struct buffer_head));
		bhp->b_reqnext = bh;
		bhp = bh;
		bh->b_reqnext = NULL;
	}
	return first_bh;
abort:
	idescsi_free_bh (first_bh);
	return NULL;
}

static inline int idescsi_set_direction (idescsi_pc_t *pc)
{
	switch (pc->c[0]) {
		case READ_6: case READ_10: case READ_12:
			clear_bit (PC_WRITING, &pc->flags);
			return 0;
		case WRITE_6: case WRITE_10: case WRITE_12:
			set_bit (PC_WRITING, &pc->flags);
			return 0;
		default:
			return 1;
	}
}

static inline struct buffer_head *idescsi_dma_bh (ide_drive_t *drive, idescsi_pc_t *pc)
{
	struct buffer_head *bh = NULL, *first_bh = NULL;
	int segments = pc->scsi_cmd->use_sg;
	struct scatterlist *sg = pc->scsi_cmd->request_buffer;

	if (!drive->using_dma || pc->request_transfer % 1024)
		return NULL;
	if (idescsi_set_direction(pc))
		return NULL;
	if (segments) {
		if ((first_bh = bh = idescsi_kmalloc_bh (segments)) == NULL)
			return NULL;
#if IDESCSI_DEBUG_LOG
		printk ("ide-scsi: %s: building DMA table, %d segments, %dkB total\n", drive->name, segments, pc->request_transfer >> 10);
#endif /* IDESCSI_DEBUG_LOG */
		while (segments--) {
			bh->b_data = sg->address;
			bh->b_size = sg->length;
			bh = bh->b_reqnext;
			sg++;
		}
	} else {
		if ((first_bh = bh = idescsi_kmalloc_bh (1)) == NULL)
			return NULL;
#if IDESCSI_DEBUG_LOG
		printk ("ide-scsi: %s: building DMA table for a single buffer (%dkB)\n", drive->name, pc->request_transfer >> 10);
#endif /* IDESCSI_DEBUG_LOG */
		bh->b_data = pc->scsi_cmd->request_buffer;
		bh->b_size = pc->request_transfer;
	}
	return first_bh;
}

int idescsi_queue (Scsi_Cmnd *cmd, void (*done)(Scsi_Cmnd *))
{
	ide_drive_t *drive = idescsi_drives[cmd->target];
	struct request *rq = NULL;
	idescsi_pc_t *pc = NULL;

#if IDESCSI_DEBUG_LOG
	printk ("idescsi_queue called, serial = %lu, cmd[0] = %x, id = %d\n", cmd->serial_number, cmd->cmnd[0], cmd->target);
#endif	/* IDESCSI_DEBUG_LOG */

	if (!drive) {
		printk (KERN_ERR "ide-scsi: drive id %d not present\n", cmd->target);
		goto abort;
	}
	pc = kmalloc (sizeof (idescsi_pc_t), GFP_ATOMIC);
	rq = kmalloc (sizeof (struct request), GFP_ATOMIC);
	if (rq == NULL || pc == NULL) {
		printk (KERN_ERR "ide-scsi: %s: out of memory\n", drive->name);
		goto abort;
	}

	memset (pc->c, 0, 12);
	pc->flags = 0;
	pc->rq = rq;
	memcpy (pc->c, cmd->cmnd, cmd->cmd_len);
	if (cmd->use_sg) {
		pc->buffer = NULL;
		pc->sg = cmd->request_buffer;
	} else {
		pc->buffer = cmd->request_buffer;
		pc->sg = NULL;
	}
	pc->b_count = 0;
	pc->request_transfer = pc->buffer_size = cmd->request_bufflen;
	pc->scsi_cmd = cmd;
	pc->done = done;
	idescsi_transform_pc1 (drive, pc);

	ide_init_drive_cmd (rq);
	rq->buffer = (char *) pc;
	rq->bh = idescsi_dma_bh (drive, pc);
	rq->cmd = IDESCSI_PC_RQ;
	(void) ide_do_drive_cmd (drive, rq, ide_end);
	return 0;
abort:
	if (pc) kfree (pc);
	if (rq) kfree (rq);
	cmd->result = DID_ERROR << 16;
	done(cmd);
	return 0;
}

int idescsi_abort (Scsi_Cmnd *cmd)
{
	return SCSI_ABORT_SNOOZE;
}

int idescsi_reset (Scsi_Cmnd *cmd, unsigned int resetflags)
{
	return SCSI_RESET_PUNT;
}

#ifdef MODULE
Scsi_Host_Template idescsi_template = IDESCSI;

int init_module (void)
{
	idescsi_init ();
	idescsi_template.module = &__this_module;
	scsi_register_module (MODULE_SCSI_HA, &idescsi_template);
	return 0;
}

void cleanup_module (void)
{
	ide_drive_t *drive;
	byte media[] = {TYPE_DISK, TYPE_TAPE, TYPE_PROCESSOR, TYPE_WORM, TYPE_ROM, TYPE_SCANNER, TYPE_MOD, 255};
	int i, failed;

	scsi_unregister_module (MODULE_SCSI_HA, &idescsi_template);
	for (i = 0; media[i] != 255; i++) {
		failed = 0;
		while ((drive = ide_scan_devices (media[i], &idescsi_driver, failed)) != NULL)
			if (idescsi_cleanup (drive)) {
				printk ("%s: cleanup_module() called while still busy\n", drive->name);
				failed++;
			}
	}
	ide_unregister_module(&idescsi_module);
}
#endif /* MODULE */
