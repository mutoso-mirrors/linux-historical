/*
 *      sd.c Copyright (C) 1992 Drew Eckhardt
 *           Copyright (C) 1993, 1994, 1995, 1999 Eric Youngdale
 *
 *      Linux scsi disk driver
 *              Initial versions: Drew Eckhardt
 *              Subsequent revisions: Eric Youngdale
 *
 *      <drew@colorado.edu>
 *
 *       Modified by Eric Youngdale ericy@andante.org to
 *       add scatter-gather, multiple outstanding request, and other
 *       enhancements.
 *
 *       Modified by Eric Youngdale eric@andante.org to support loadable
 *       low-level scsi drivers.
 *
 *       Modified by Jirka Hanika geo@ff.cuni.cz to support more
 *       scsi disks using eight major numbers.
 *
 *       Modified by Richard Gooch rgooch@atnf.csiro.au to support devfs.
 *	
 *	 Modified by Torben Mathiasen tmm@image.dk
 *       Resource allocation fixes in sd_init and cleanups.
 *	
 *	 Modified by Alex Davis <letmein@erols.com>
 *       Fix problem where partition info not being read in sd_open.
 *	
 *	 Modified by Alex Davis <letmein@erols.com>
 *       Fix problem where removable media could be ejected after sd_open.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/hdreg.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <linux/smp.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/io.h>

#define MAJOR_NR SCSI_DISK0_MAJOR
#include <linux/blk.h>
#include <linux/blkpg.h>
#include "scsi.h"
#include "hosts.h"
#include "sd.h"
#include <scsi/scsi_ioctl.h>
#include "constants.h"
#include <scsi/scsicam.h>	/* must follow "hosts.h" */

#include <linux/genhd.h>

#define SD_MAJOR(i) (!(i) ? SCSI_DISK0_MAJOR : SCSI_DISK1_MAJOR-1+(i))

#define SCSI_DISKS_PER_MAJOR	16
#define SD_MAJOR_NUMBER(i)	SD_MAJOR((i) >> 8)
#define SD_MINOR_NUMBER(i)	((i) & 255)
#define MKDEV_SD_PARTITION(i)	mk_kdev(SD_MAJOR_NUMBER(i), (i) & 255)
#define MKDEV_SD(index)		MKDEV_SD_PARTITION((index) << 4)
#define N_USED_SD_MAJORS	(1 + ((sd_template.dev_max - 1) >> 4))

#define MAX_RETRIES 5

/*
 *  Time out in seconds for disks and Magneto-opticals (which are slower).
 */

#define SD_TIMEOUT (30 * HZ)
#define SD_MOD_TIMEOUT (75 * HZ)

struct hd_struct *sd;

static Scsi_Disk *rscsi_disks;
static int *sd_sizes;
static int *sd_blocksizes;
static int *sd_max_sectors;

static int check_scsidisk_media_change(kdev_t);
static int fop_revalidate_scsidisk(kdev_t);

static int sd_init_onedisk(int);

static int sd_init(void);
static void sd_finish(void);
static int sd_attach(Scsi_Device *);
static int sd_detect(Scsi_Device *);
static void sd_detach(Scsi_Device *);
static int sd_init_command(Scsi_Cmnd *);

static struct Scsi_Device_Template sd_template = {
	module:THIS_MODULE,
	name:"disk",
	tag:"sd",
	scsi_type:TYPE_DISK,
	major:SCSI_DISK0_MAJOR,
        /*
         * Secondary range of majors that this driver handles.
         */
	min_major:SCSI_DISK1_MAJOR,
	max_major:SCSI_DISK7_MAJOR,
	blk:1,
	detect:sd_detect,
	init:sd_init,
	finish:sd_finish,
	attach:sd_attach,
	detach:sd_detach,
	init_command:sd_init_command,
};

static void rw_intr(Scsi_Cmnd * SCpnt);

#if defined(CONFIG_PPC)
/*
 * Moved from arch/ppc/pmac_setup.c.  This is where it really belongs.
 */
kdev_t __init
sd_find_target(void *host, int tgt)
{
    Scsi_Disk *dp;
    int i;
    for (dp = rscsi_disks, i = 0; i < sd_template.dev_max; ++i, ++dp)
        if (dp->device != NULL && dp->device->host == host
            && dp->device->id == tgt)
            return MKDEV_SD(i);
    return NODEV;
}
#endif

static int sd_ioctl(struct inode * inode, struct file * file, unsigned int cmd, unsigned long arg)
{
	kdev_t dev = inode->i_rdev;
	struct Scsi_Host * host;
	Scsi_Device * SDev;
	int diskinfo[4];
    
	SDev = rscsi_disks[DEVICE_NR(dev)].device;
	if (!SDev)
		return -ENODEV;

	/*
	 * If we are in the middle of error recovery, don't let anyone
	 * else try and use this device.  Also, if error recovery fails, it
	 * may try and take the device offline, in which case all further
	 * access to the device is prohibited.
	 */

	if( !scsi_block_when_processing_errors(SDev) )
	{
		return -ENODEV;
	}

	switch (cmd) 
	{
		case HDIO_GETGEO:   /* Return BIOS disk parameters */
		{
			struct hd_geometry *loc = (struct hd_geometry *) arg;
			if(!loc)
				return -EINVAL;

			host = rscsi_disks[DEVICE_NR(dev)].device->host;
	
			/* default to most commonly used values */
	
		        diskinfo[0] = 0x40;
	        	diskinfo[1] = 0x20;
	        	diskinfo[2] = rscsi_disks[DEVICE_NR(dev)].capacity >> 11;
	
			/* override with calculated, extended default, or driver values */
	
			if(host->hostt->bios_param != NULL)
				host->hostt->bios_param(&rscsi_disks[DEVICE_NR(dev)],
					    dev,
					    &diskinfo[0]);
			else scsicam_bios_param(&rscsi_disks[DEVICE_NR(dev)],
					dev, &diskinfo[0]);
			if (put_user(diskinfo[0], &loc->heads) ||
				put_user(diskinfo[1], &loc->sectors) ||
				put_user(diskinfo[2], &loc->cylinders) ||
				put_user((unsigned) get_start_sect(inode->i_rdev),
					 (unsigned long *) &loc->start))
				return -EFAULT;
			return 0;
		}
		case HDIO_GETGEO_BIG:
		{
			struct hd_big_geometry *loc = (struct hd_big_geometry *) arg;

			if(!loc)
				return -EINVAL;

			host = rscsi_disks[DEVICE_NR(dev)].device->host;

			/* default to most commonly used values */

			diskinfo[0] = 0x40;
			diskinfo[1] = 0x20;
			diskinfo[2] = rscsi_disks[DEVICE_NR(dev)].capacity >> 11;

			/* override with calculated, extended default, or driver values */

			if(host->hostt->bios_param != NULL)
				host->hostt->bios_param(&rscsi_disks[DEVICE_NR(dev)],
					    dev,
					    &diskinfo[0]);
			else scsicam_bios_param(&rscsi_disks[DEVICE_NR(dev)],
					dev, &diskinfo[0]);

			if (put_user(diskinfo[0], &loc->heads) ||
				put_user(diskinfo[1], &loc->sectors) ||
				put_user(diskinfo[2], (unsigned int *) &loc->cylinders) ||
				put_user((unsigned)get_start_sect(inode->i_rdev),
					 (unsigned long *)&loc->start))
				return -EFAULT;
			return 0;
		}
		case BLKGETSIZE:
		case BLKGETSIZE64:
		case BLKROSET:
		case BLKROGET:
		case BLKFLSBUF:
		case BLKSSZGET:
		case BLKPG:
                case BLKELVGET:
                case BLKELVSET:
		case BLKBSZGET:
		case BLKBSZSET:
			return blk_ioctl(inode->i_bdev, cmd, arg);

		case BLKRRPART: /* Re-read partition tables */
		        if (!capable(CAP_SYS_ADMIN))
		                return -EACCES;
			return revalidate_scsidisk(dev, 1);

		default:
			return scsi_ioctl(rscsi_disks[DEVICE_NR(dev)].device,
					  cmd, (void *) arg);
	}
}

static void sd_devname(unsigned int disknum, char *buffer)
{
	if (disknum < 26)
		sprintf(buffer, "sd%c", 'a' + disknum);
	else {
		unsigned int min1;
		unsigned int min2;
		/*
		 * For larger numbers of disks, we need to go to a new
		 * naming scheme.
		 */
		min1 = disknum / 26;
		min2 = disknum % 26;
		sprintf(buffer, "sd%c%c", 'a' + min1 - 1, 'a' + min2);
	}
}

static request_queue_t *sd_find_queue(kdev_t dev)
{
	Scsi_Disk *dpnt;
 	int target;
 	target = DEVICE_NR(dev);

	dpnt = &rscsi_disks[target];
	if (!dpnt)
		return NULL;	/* No such device */
	return &dpnt->device->request_queue;
}

static int sd_init_command(Scsi_Cmnd * SCpnt)
{
	int dev, devm, block, this_count;
	Scsi_Disk *dpnt;
#if CONFIG_SCSI_LOGGING
	char nbuff[6];
#endif

	/*
	 * don't support specials for nwo
	 */
	if (!(SCpnt->request.flags & REQ_CMD))
		return 0;

	devm = SD_PARTITION(SCpnt->request.rq_dev);
	dev = DEVICE_NR(SCpnt->request.rq_dev);

	block = SCpnt->request.sector;
	this_count = SCpnt->request_bufflen >> 9;

	SCSI_LOG_HLQUEUE(1, printk("Doing sd request, dev = %d, block = %d\n", devm, block));

	dpnt = &rscsi_disks[dev];
	if (devm >= (sd_template.dev_max << 4) || (devm & 0xf) ||
	    !dpnt ||
	    !dpnt->device->online ||
 	    block + SCpnt->request.nr_sectors > sd[devm].nr_sects) {
		SCSI_LOG_HLQUEUE(2, printk("Finishing %ld sectors\n", SCpnt->request.nr_sectors));
		SCSI_LOG_HLQUEUE(2, printk("Retry with 0x%p\n", SCpnt));
		return 0;
	}

	if (dpnt->device->changed) {
		/*
		 * quietly refuse to do anything to a changed disc until the changed
		 * bit has been reset
		 */
		/* printk("SCSI disk has been changed. Prohibiting further I/O.\n"); */
		return 0;
	}
	SCSI_LOG_HLQUEUE(2, sd_devname(devm, nbuff));
	SCSI_LOG_HLQUEUE(2, printk("%s : real dev = /dev/%d, block = %d\n",
				   nbuff, dev, block));

	/*
	 * If we have a 1K hardware sectorsize, prevent access to single
	 * 512 byte sectors.  In theory we could handle this - in fact
	 * the scsi cdrom driver must be able to handle this because
	 * we typically use 1K blocksizes, and cdroms typically have
	 * 2K hardware sectorsizes.  Of course, things are simpler
	 * with the cdrom, since it is read-only.  For performance
	 * reasons, the filesystems should be able to handle this
	 * and not force the scsi disk driver to use bounce buffers
	 * for this.
	 */
	if (dpnt->device->sector_size == 1024) {
		if ((block & 1) || (SCpnt->request.nr_sectors & 1)) {
			printk("sd.c:Bad block number requested");
			return 0;
		} else {
			block = block >> 1;
			this_count = this_count >> 1;
		}
	}
	if (dpnt->device->sector_size == 2048) {
		if ((block & 3) || (SCpnt->request.nr_sectors & 3)) {
			printk("sd.c:Bad block number requested");
			return 0;
		} else {
			block = block >> 2;
			this_count = this_count >> 2;
		}
	}
	if (dpnt->device->sector_size == 4096) {
		if ((block & 7) || (SCpnt->request.nr_sectors & 7)) {
			printk("sd.c:Bad block number requested");
			return 0;
		} else {
			block = block >> 3;
			this_count = this_count >> 3;
		}
	}
	if (rq_data_dir(&SCpnt->request) == WRITE) {
		if (!dpnt->device->writeable) {
			return 0;
		}
		SCpnt->cmnd[0] = WRITE_6;
		SCpnt->sc_data_direction = SCSI_DATA_WRITE;
	} else if (rq_data_dir(&SCpnt->request) == READ) {
		SCpnt->cmnd[0] = READ_6;
		SCpnt->sc_data_direction = SCSI_DATA_READ;
	} else
		panic("Unknown sd command %lx\n", SCpnt->request.flags);

	SCSI_LOG_HLQUEUE(2, printk("%s : %s %d/%ld 512 byte blocks.\n",
				   nbuff,
		   (rq_data_dir(&SCpnt->request) == WRITE) ? "writing" : "reading",
				 this_count, SCpnt->request.nr_sectors));

	SCpnt->cmnd[1] = (SCpnt->device->scsi_level <= SCSI_2) ?
			 ((SCpnt->lun << 5) & 0xe0) : 0;

	if (((this_count > 0xff) || (block > 0x1fffff)) || SCpnt->device->ten) {
		if (this_count > 0xffff)
			this_count = 0xffff;

		SCpnt->cmnd[0] += READ_10 - READ_6;
		SCpnt->cmnd[2] = (unsigned char) (block >> 24) & 0xff;
		SCpnt->cmnd[3] = (unsigned char) (block >> 16) & 0xff;
		SCpnt->cmnd[4] = (unsigned char) (block >> 8) & 0xff;
		SCpnt->cmnd[5] = (unsigned char) block & 0xff;
		SCpnt->cmnd[6] = SCpnt->cmnd[9] = 0;
		SCpnt->cmnd[7] = (unsigned char) (this_count >> 8) & 0xff;
		SCpnt->cmnd[8] = (unsigned char) this_count & 0xff;
	} else {
		if (this_count > 0xff)
			this_count = 0xff;

		SCpnt->cmnd[1] |= (unsigned char) ((block >> 16) & 0x1f);
		SCpnt->cmnd[2] = (unsigned char) ((block >> 8) & 0xff);
		SCpnt->cmnd[3] = (unsigned char) block & 0xff;
		SCpnt->cmnd[4] = (unsigned char) this_count;
		SCpnt->cmnd[5] = 0;
	}

	/*
	 * We shouldn't disconnect in the middle of a sector, so with a dumb
	 * host adapter, it's safe to assume that we can at least transfer
	 * this many bytes between each connect / disconnect.
	 */
	SCpnt->transfersize = dpnt->device->sector_size;
	SCpnt->underflow = this_count << 9;

	SCpnt->allowed = MAX_RETRIES;
	SCpnt->timeout_per_command = (SCpnt->device->type == TYPE_DISK ?
				      SD_TIMEOUT : SD_MOD_TIMEOUT);

	/*
	 * This is the completion routine we use.  This is matched in terms
	 * of capability to this function.
	 */
	SCpnt->done = rw_intr;

	/*
	 * This indicates that the command is ready from our end to be
	 * queued.
	 */
	return 1;
}

static int sd_open(struct inode *inode, struct file *filp)
{
	int target, retval = -ENXIO;
	Scsi_Device * SDev;
	target = DEVICE_NR(inode->i_rdev);

	SCSI_LOG_HLQUEUE(1, printk("target=%d, max=%d\n", target, sd_template.dev_max));

	if (target >= sd_template.dev_max || !rscsi_disks[target].device)
		return -ENXIO;	/* No such device */

	/*
	 * If the device is in error recovery, wait until it is done.
	 * If the device is offline, then disallow any access to it.
	 */
	if (!scsi_block_when_processing_errors(rscsi_disks[target].device)) {
		return -ENXIO;
	}
	/*
	 * Make sure that only one process can do a check_change_disk at one time.
	 * This is also used to lock out further access when the partition table
	 * is being re-read.
	 */

	while (rscsi_disks[target].device->busy) {
		barrier();
		cpu_relax();
	}
	/*
	 * The following code can sleep.
	 * Module unloading must be prevented
	 */
	SDev = rscsi_disks[target].device;
	if (SDev->host->hostt->module)
		__MOD_INC_USE_COUNT(SDev->host->hostt->module);
	if (sd_template.module)
		__MOD_INC_USE_COUNT(sd_template.module);
	SDev->access_count++;

	if (rscsi_disks[target].device->removable) {
		SDev->allow_revalidate = 1;
		check_disk_change(inode->i_rdev);
		SDev->allow_revalidate = 0;

		/*
		 * If the drive is empty, just let the open fail.
		 */
		if ((!rscsi_disks[target].ready) && !(filp->f_flags & O_NDELAY)) {
			retval = -ENOMEDIUM;
			goto error_out;
		}

		/*
		 * Similarly, if the device has the write protect tab set,
		 * have the open fail if the user expects to be able to write
		 * to the thing.
		 */
		if ((rscsi_disks[target].write_prot) && (filp->f_mode & 2)) {
			retval = -EROFS;
			goto error_out;
		}
	}
	/*
	 * It is possible that the disk changing stuff resulted in the device
	 * being taken offline.  If this is the case, report this to the user,
	 * and don't pretend that
	 * the open actually succeeded.
	 */
	if (!SDev->online) {
		goto error_out;
	}
	/*
	 * See if we are requesting a non-existent partition.  Do this
	 * after checking for disk change.
	 */
	if (sd_sizes[SD_PARTITION(inode->i_rdev)] == 0) {
		goto error_out;
	}

	if (SDev->removable)
		if (SDev->access_count==1)
			if (scsi_block_when_processing_errors(SDev))
				scsi_ioctl(SDev, SCSI_IOCTL_DOORLOCK, NULL);

	
	return 0;

error_out:
	SDev->access_count--;
	if (SDev->host->hostt->module)
		__MOD_DEC_USE_COUNT(SDev->host->hostt->module);
	if (sd_template.module)
		__MOD_DEC_USE_COUNT(sd_template.module);
	return retval;	
}

static int sd_release(struct inode *inode, struct file *file)
{
	int target;
	Scsi_Device * SDev;

	target = DEVICE_NR(inode->i_rdev);
	SDev = rscsi_disks[target].device;
	if (!SDev)
		return -ENODEV;

	SDev->access_count--;

	if (SDev->removable) {
		if (!SDev->access_count)
			if (scsi_block_when_processing_errors(SDev))
				scsi_ioctl(SDev, SCSI_IOCTL_DOORUNLOCK, NULL);
	}
	if (SDev->host->hostt->module)
		__MOD_DEC_USE_COUNT(SDev->host->hostt->module);
	if (sd_template.module)
		__MOD_DEC_USE_COUNT(sd_template.module);
	return 0;
}

static struct block_device_operations sd_fops =
{
	owner:			THIS_MODULE,
	open:			sd_open,
	release:		sd_release,
	ioctl:			sd_ioctl,
	check_media_change:	check_scsidisk_media_change,
	revalidate:		fop_revalidate_scsidisk
};

/*
 *    If we need more than one SCSI disk major (i.e. more than
 *      16 SCSI disks), we'll have to kmalloc() more gendisks later.
 */

static struct gendisk sd_gendisk =
{
	major:		SCSI_DISK0_MAJOR,
	major_name:	"sd",
	minor_shift:	4,
	fops:		&sd_fops,
};

static struct gendisk *sd_gendisks = &sd_gendisk;

#define SD_GENDISK(i)    sd_gendisks[(i) / SCSI_DISKS_PER_MAJOR]

/*
 * rw_intr is the interrupt routine for the device driver.
 * It will be notified on the end of a SCSI read / write, and
 * will take one of several actions based on success or failure.
 */

static void rw_intr(Scsi_Cmnd * SCpnt)
{
	int result = SCpnt->result;
#if CONFIG_SCSI_LOGGING
	char nbuff[6];
#endif
	int this_count = SCpnt->bufflen >> 9;
	int good_sectors = (result == 0 ? this_count : 0);
	int block_sectors = 1;

	SCSI_LOG_HLCOMPLETE(1, sd_devname(DEVICE_NR(SCpnt->request.rq_dev), nbuff));

	SCSI_LOG_HLCOMPLETE(1, printk("%s : rw_intr(%d, %x [%x %x])\n", nbuff,
				      SCpnt->host->host_no,
				      result,
				      SCpnt->sense_buffer[0],
				      SCpnt->sense_buffer[2]));

	/*
	   Handle MEDIUM ERRORs that indicate partial success.  Since this is a
	   relatively rare error condition, no care is taken to avoid
	   unnecessary additional work such as memcpy's that could be avoided.
	 */

	/* An error occurred */
	if (driver_byte(result) != 0) {
		/* Sense data is valid */
		if (SCpnt->sense_buffer[0] == 0xF0 && SCpnt->sense_buffer[2] == MEDIUM_ERROR) {
			long error_sector = (SCpnt->sense_buffer[3] << 24) |
			(SCpnt->sense_buffer[4] << 16) |
			(SCpnt->sense_buffer[5] << 8) |
			SCpnt->sense_buffer[6];
			if (SCpnt->request.bio != NULL)
				block_sectors = bio_sectors(SCpnt->request.bio);
			switch (SCpnt->device->sector_size) {
			case 1024:
				error_sector <<= 1;
				if (block_sectors < 2)
					block_sectors = 2;
				break;
			case 2048:
				error_sector <<= 2;
				if (block_sectors < 4)
					block_sectors = 4;
				break;
			case 4096:
				error_sector <<=3;
				if (block_sectors < 8)
					block_sectors = 8;
				break;
			case 256:
				error_sector >>= 1;
				break;
			default:
				break;
			}

			error_sector &= ~(block_sectors - 1);
			good_sectors = error_sector - SCpnt->request.sector;
			if (good_sectors < 0 || good_sectors >= this_count)
				good_sectors = 0;
		}
		if (SCpnt->sense_buffer[2] == ILLEGAL_REQUEST) {
			if (SCpnt->device->ten == 1) {
				if (SCpnt->cmnd[0] == READ_10 ||
				    SCpnt->cmnd[0] == WRITE_10)
					SCpnt->device->ten = 0;
			}
		}
	}
	/*
	 * This calls the generic completion function, now that we know
	 * how many actual sectors finished, and how many sectors we need
	 * to say have failed.
	 */
	scsi_io_completion(SCpnt, good_sectors, block_sectors);
}
/*
 * requeue_sd_request() is the request handler function for the sd driver.
 * Its function in life is to take block device requests, and translate
 * them to SCSI commands.
 */


static int check_scsidisk_media_change(kdev_t full_dev)
{
	int retval;
	int target;
	int flag = 0;
	Scsi_Device * SDev;

	target = DEVICE_NR(full_dev);
	SDev = rscsi_disks[target].device;

	if (target >= sd_template.dev_max || !SDev) {
		printk("SCSI disk request error: invalid device.\n");
		return 0;
	}
	if (!SDev->removable)
		return 0;

	/*
	 * If the device is offline, don't send any commands - just pretend as
	 * if the command failed.  If the device ever comes back online, we
	 * can deal with it then.  It is only because of unrecoverable errors
	 * that we would ever take a device offline in the first place.
	 */
	if (SDev->online == FALSE) {
		rscsi_disks[target].ready = 0;
		SDev->changed = 1;
		return 1;	/* This will force a flush, if called from
				 * check_disk_change */
	}

	/* Using Start/Stop enables differentiation between drive with
	 * no cartridge loaded - NOT READY, drive with changed cartridge -
	 * UNIT ATTENTION, or with same cartridge - GOOD STATUS.
	 * This also handles drives that auto spin down. eg iomega jaz 1GB
	 * as this will spin up the drive.
	 */
	retval = -ENODEV;
	if (scsi_block_when_processing_errors(SDev))
		retval = scsi_ioctl(SDev, SCSI_IOCTL_START_UNIT, NULL);

	if (retval) {		/* Unable to test, unit probably not ready.
				 * This usually means there is no disc in the
				 * drive.  Mark as changed, and we will figure
				 * it out later once the drive is available
				 * again.  */

		rscsi_disks[target].ready = 0;
		SDev->changed = 1;
		return 1;	/* This will force a flush, if called from
				 * check_disk_change */
	}
	/*
	 * for removable scsi disk ( FLOPTICAL ) we have to recognise the
	 * presence of disk in the drive. This is kept in the Scsi_Disk
	 * struct and tested at open !  Daniel Roche ( dan@lectra.fr )
	 */

	rscsi_disks[target].ready = 1;	/* FLOPTICAL */

	retval = SDev->changed;
	if (!flag)
		SDev->changed = 0;
	return retval;
}

static int sd_init_onedisk(int i)
{
	unsigned char cmd[10];
	char nbuff[6];
	unsigned char *buffer;
	unsigned long spintime_value = 0;
	int the_result, retries, spintime;
	int sector_size;
	Scsi_Request *SRpnt;

	/*
	 * Get the name of the disk, in case we need to log it somewhere.
	 */
	sd_devname(i, nbuff);

	/*
	 * If the device is offline, don't try and read capacity or any
	 * of the other niceties.
	 */
	if (rscsi_disks[i].device->online == FALSE)
		return i;

	/*
	 * We need to retry the READ_CAPACITY because a UNIT_ATTENTION is
	 * considered a fatal error, and many devices report such an error
	 * just after a scsi bus reset.
	 */

	SRpnt = scsi_allocate_request(rscsi_disks[i].device);
	if (!SRpnt) {
		printk(KERN_WARNING "(sd_init_onedisk:) Request allocation failure.\n");
		return i;
	}

	buffer = kmalloc(512, GFP_DMA);
	if (!buffer) {
		printk(KERN_WARNING "(sd_init_onedisk:) Memory allocation failure.\n");
		scsi_release_request(SRpnt);
		return i;
	}

	spintime = 0;

	/* Spin up drives, as required.  Only do this at boot time */
	/* Spinup needs to be done for module loads too. */
	do {
		retries = 0;

		while (retries < 3) {
			cmd[0] = TEST_UNIT_READY;
			cmd[1] = (rscsi_disks[i].device->scsi_level <= SCSI_2) ?
				 ((rscsi_disks[i].device->lun << 5) & 0xe0) : 0;
			memset((void *) &cmd[2], 0, 8);
			SRpnt->sr_cmd_len = 0;
			SRpnt->sr_sense_buffer[0] = 0;
			SRpnt->sr_sense_buffer[2] = 0;
			SRpnt->sr_data_direction = SCSI_DATA_NONE;

			scsi_wait_req (SRpnt, (void *) cmd, (void *) buffer,
				0/*512*/, SD_TIMEOUT, MAX_RETRIES);

			the_result = SRpnt->sr_result;
			retries++;
			if (the_result == 0
			    || SRpnt->sr_sense_buffer[2] != UNIT_ATTENTION)
				break;
		}

		/*
		 * If the drive has indicated to us that it doesn't have
		 * any media in it, don't bother with any of the rest of
		 * this crap.
		 */
		if( the_result != 0
		    && ((driver_byte(the_result) & DRIVER_SENSE) != 0)
		    && SRpnt->sr_sense_buffer[2] == UNIT_ATTENTION
		    && SRpnt->sr_sense_buffer[12] == 0x3A ) {
			rscsi_disks[i].capacity = 0x1fffff;
			sector_size = 512;
			rscsi_disks[i].device->changed = 1;
			rscsi_disks[i].ready = 0;
			break;
		}

		/* Look for non-removable devices that return NOT_READY.
		 * Issue command to spin up drive for these cases. */
		if (the_result && !rscsi_disks[i].device->removable &&
		    SRpnt->sr_sense_buffer[2] == NOT_READY) {
			unsigned long time1;
			if (!spintime) {
				printk("%s: Spinning up disk...", nbuff);
				cmd[0] = START_STOP;
				cmd[1] = (rscsi_disks[i].device->scsi_level <= SCSI_2) ?
				 	 ((rscsi_disks[i].device->lun << 5) & 0xe0) : 0;
				cmd[1] |= 1;	/* Return immediately */
				memset((void *) &cmd[2], 0, 8);
				cmd[4] = 1;	/* Start spin cycle */
				SRpnt->sr_cmd_len = 0;
				SRpnt->sr_sense_buffer[0] = 0;
				SRpnt->sr_sense_buffer[2] = 0;

				SRpnt->sr_data_direction = SCSI_DATA_READ;
				scsi_wait_req(SRpnt, (void *) cmd, (void *) buffer,
					    0/*512*/, SD_TIMEOUT, MAX_RETRIES);
				spintime_value = jiffies;
			}
			spintime = 1;
			time1 = HZ;
			/* Wait 1 second for next try */
			do {
				current->state = TASK_UNINTERRUPTIBLE;
				time1 = schedule_timeout(time1);
			} while(time1);
			printk(".");
		}
	} while (the_result && spintime &&
		 time_after(spintime_value + 100 * HZ, jiffies));
	if (spintime) {
		if (the_result)
			printk("not responding...\n");
		else
			printk("ready\n");
	}
	retries = 3;
	do {
		cmd[0] = READ_CAPACITY;
		cmd[1] = (rscsi_disks[i].device->scsi_level <= SCSI_2) ?
			 ((rscsi_disks[i].device->lun << 5) & 0xe0) : 0;
		memset((void *) &cmd[2], 0, 8);
		memset((void *) buffer, 0, 8);
		SRpnt->sr_cmd_len = 0;
		SRpnt->sr_sense_buffer[0] = 0;
		SRpnt->sr_sense_buffer[2] = 0;

		SRpnt->sr_data_direction = SCSI_DATA_READ;
		scsi_wait_req(SRpnt, (void *) cmd, (void *) buffer,
			    8, SD_TIMEOUT, MAX_RETRIES);

		the_result = SRpnt->sr_result;
		retries--;

	} while (the_result && retries);

	/*
	 * The SCSI standard says:
	 * "READ CAPACITY is necessary for self configuring software"
	 *  While not mandatory, support of READ CAPACITY is strongly
	 *  encouraged.
	 *  We used to die if we couldn't successfully do a READ CAPACITY.
	 *  But, now we go on about our way.  The side effects of this are
	 *
	 *  1. We can't know block size with certainty. I have said
	 *     "512 bytes is it" as this is most common.
	 *
	 *  2. Recovery from when someone attempts to read past the
	 *     end of the raw device will be slower.
	 */

	if (the_result) {
		printk("%s : READ CAPACITY failed.\n"
		       "%s : status = %x, message = %02x, host = %d, driver = %02x \n",
		       nbuff, nbuff,
		       status_byte(the_result),
		       msg_byte(the_result),
		       host_byte(the_result),
		       driver_byte(the_result)
		    );
		if (driver_byte(the_result) & DRIVER_SENSE)
			print_req_sense("sd", SRpnt);
		else
			printk("%s : sense not available. \n", nbuff);

		printk("%s : block size assumed to be 512 bytes, disk size 1GB.  \n",
		       nbuff);
		rscsi_disks[i].capacity = 0x1fffff;
		sector_size = 512;

		/* Set dirty bit for removable devices if not ready -
		 * sometimes drives will not report this properly. */
		if (rscsi_disks[i].device->removable &&
		    SRpnt->sr_sense_buffer[2] == NOT_READY)
			rscsi_disks[i].device->changed = 1;

	} else {
		/*
		 * FLOPTICAL, if read_capa is ok, drive is assumed to be ready
		 */
		rscsi_disks[i].ready = 1;

		rscsi_disks[i].capacity = 1 + ((buffer[0] << 24) |
					       (buffer[1] << 16) |
					       (buffer[2] << 8) |
					       buffer[3]);

		sector_size = (buffer[4] << 24) |
		    (buffer[5] << 16) | (buffer[6] << 8) | buffer[7];

		if (sector_size == 0) {
			sector_size = 512;
			printk("%s : sector size 0 reported, assuming 512.\n",
			       nbuff);
		}
		if (sector_size != 512 &&
		    sector_size != 1024 &&
		    sector_size != 2048 &&
		    sector_size != 4096 &&
		    sector_size != 256) {
			printk("%s : unsupported sector size %d.\n",
			       nbuff, sector_size);
			/*
			 * The user might want to re-format the drive with
			 * a supported sectorsize.  Once this happens, it
			 * would be relatively trivial to set the thing up.
			 * For this reason, we leave the thing in the table.
			 */
			rscsi_disks[i].capacity = 0;
		}
		if (sector_size > 1024) {
			int m;

			/*
			 * We must fix the sd_blocksizes and sd_hardsizes
			 * to allow us to read the partition tables.
			 * The disk reading code does not allow for reading
			 * of partial sectors.
			 */
			for (m = i << 4; m < ((i + 1) << 4); m++) {
				sd_blocksizes[m] = sector_size;
			}
		} {
			/*
			 * The msdos fs needs to know the hardware sector size
			 * So I have created this table. See ll_rw_blk.c
			 * Jacques Gelinas (Jacques@solucorp.qc.ca)
			 */
			int hard_sector = sector_size;
			int sz = rscsi_disks[i].capacity * (hard_sector/256);
			request_queue_t *queue = &rscsi_disks[i].device->request_queue;

			blk_queue_hardsect_size(queue, hard_sector);
			printk("SCSI device %s: "
			       "%d %d-byte hdwr sectors (%d MB)\n",
			       nbuff, rscsi_disks[i].capacity,
			       hard_sector, (sz/2 - sz/1250 + 974)/1950);
		}

		/* Rescale capacity to 512-byte units */
		if (sector_size == 4096)
			rscsi_disks[i].capacity <<= 3;
		if (sector_size == 2048)
			rscsi_disks[i].capacity <<= 2;
		if (sector_size == 1024)
			rscsi_disks[i].capacity <<= 1;
		if (sector_size == 256)
			rscsi_disks[i].capacity >>= 1;
	}


	/*
	 * Unless otherwise specified, this is not write protected.
	 */
	rscsi_disks[i].write_prot = 0;
	if (rscsi_disks[i].device->removable && rscsi_disks[i].ready) {
		/* FLOPTICAL */

		/*
		 * For removable scsi disk ( FLOPTICAL ) we have to recognise
		 * the Write Protect Flag. This flag is kept in the Scsi_Disk
		 * struct and tested at open !
		 * Daniel Roche ( dan@lectra.fr )
		 *
		 * Changed to get all pages (0x3f) rather than page 1 to
		 * get around devices which do not have a page 1.  Since
		 * we're only interested in the header anyway, this should
		 * be fine.
		 *   -- Matthew Dharm (mdharm-scsi@one-eyed-alien.net)
		 *
		 * As it turns out, some devices return an error for
		 * every MODE_SENSE request except one for page 0.
		 * So, we should also try that. --aeb
		 */

		memset((void *) &cmd[0], 0, 8);
		cmd[0] = MODE_SENSE;
		cmd[1] = (rscsi_disks[i].device->scsi_level <= SCSI_2) ?
			 ((rscsi_disks[i].device->lun << 5) & 0xe0) : 0;
		cmd[2] = 0x3f;	/* Get all pages */
		cmd[4] = 255;   /* Ask for 255 bytes, even tho we want just the first 8 */
		SRpnt->sr_cmd_len = 0;
		SRpnt->sr_sense_buffer[0] = 0;
		SRpnt->sr_sense_buffer[2] = 0;

		/* same code as READCAPA !! */
		SRpnt->sr_data_direction = SCSI_DATA_READ;
		scsi_wait_req(SRpnt, (void *) cmd, (void *) buffer,
			    512, SD_TIMEOUT, MAX_RETRIES);

		the_result = SRpnt->sr_result;

		if (the_result) {
			printk("%s: test WP failed, assume Write Enabled\n",
			       nbuff);
			/* alternatively, try page 0 */
		} else {
			rscsi_disks[i].write_prot = ((buffer[2] & 0x80) != 0);
			printk("%s: Write Protect is %s\n", nbuff,
			       rscsi_disks[i].write_prot ? "on" : "off");
		}

	}			/* check for write protect */
	SRpnt->sr_device->ten = 1;
	SRpnt->sr_device->remap = 1;
	SRpnt->sr_device->sector_size = sector_size;
	/* Wake up a process waiting for device */
	scsi_release_request(SRpnt);
	SRpnt = NULL;

	kfree(buffer);
	return i;
}

/*
 * The sd_init() function looks at all SCSI drives present, determines
 * their size, and reads partition table entries for them.
 */

static int sd_registered;

static int sd_init()
{
	int i, maxparts;

	if (sd_template.dev_noticed == 0)
		return 0;

	if (!rscsi_disks)
		sd_template.dev_max = sd_template.dev_noticed + SD_EXTRA_DEVS;

	if (sd_template.dev_max > N_SD_MAJORS * SCSI_DISKS_PER_MAJOR)
		sd_template.dev_max = N_SD_MAJORS * SCSI_DISKS_PER_MAJOR;

	/* At most 16 partitions on each scsi disk. */
	maxparts = (sd_template.dev_max << 4);
	if (maxparts == 0)
		return 0;

	if (!sd_registered) {
		for (i = 0; i < N_USED_SD_MAJORS; i++) {
			if (devfs_register_blkdev(SD_MAJOR(i), "sd",
						  &sd_fops)) {
				printk("Unable to get major %d for SCSI disk\n",
				       SD_MAJOR(i));
				return 1;
			}
		}
		sd_registered++;
	}
	/* We do not support attaching loadable devices yet. */
	if (rscsi_disks)
		return 0;

	/* allocate memory */
#define init_mem_lth(x,n)	x = kmalloc((n) * sizeof(*x), GFP_ATOMIC)
#define zero_mem_lth(x,n)	memset(x, 0, (n) * sizeof(*x))

	init_mem_lth(rscsi_disks, sd_template.dev_max);
	init_mem_lth(sd_sizes, maxparts);
	init_mem_lth(sd_blocksizes, maxparts);
	init_mem_lth(sd, maxparts);
	init_mem_lth(sd_gendisks, N_USED_SD_MAJORS);
	init_mem_lth(sd_max_sectors, sd_template.dev_max << 4);

	if (!rscsi_disks || !sd_sizes || !sd_blocksizes || !sd || !sd_gendisks)
		goto cleanup_mem;

	zero_mem_lth(rscsi_disks, sd_template.dev_max);
	zero_mem_lth(sd_sizes, maxparts);
	zero_mem_lth(sd, maxparts);

	for (i = 0; i < maxparts; i++) {
		sd_blocksizes[i] = 1024;
		/*
		 * Allow lowlevel device drivers to generate 512k large scsi
		 * commands if they know what they're doing and they ask for it
		 * explicitly via the SHpnt->max_sectors API.
		 */
		sd_max_sectors[i] = MAX_PHYS_SEGMENTS*8;
	}

	for (i = 0; i < N_USED_SD_MAJORS; i++) {
		request_queue_t *q = blk_get_queue(mk_kdev(SD_MAJOR(i), 0));
		int parts_per_major = (SCSI_DISKS_PER_MAJOR << 4);

		blksize_size[SD_MAJOR(i)] =
			sd_blocksizes + i * parts_per_major;
		blk_queue_hardsect_size(q, 512);
 	}

	for (i = 0; i < N_USED_SD_MAJORS; i++) {
		int N = SCSI_DISKS_PER_MAJOR;

		sd_gendisks[i] = sd_gendisk;

		init_mem_lth(sd_gendisks[i].de_arr, N);
		init_mem_lth(sd_gendisks[i].flags, N);

		if (!sd_gendisks[i].de_arr || !sd_gendisks[i].flags)
			goto cleanup_gendisks;

		zero_mem_lth(sd_gendisks[i].de_arr, N);
		zero_mem_lth(sd_gendisks[i].flags, N);

		sd_gendisks[i].major = SD_MAJOR(i);
		sd_gendisks[i].major_name = "sd";
		sd_gendisks[i].minor_shift = 4;
		sd_gendisks[i].part = sd + i * (N << 4);
		sd_gendisks[i].sizes = sd_sizes + i * (N << 4);
		sd_gendisks[i].nr_real = 0;
	}

	return 0;

#undef init_mem_lth
#undef zero_mem_lth

cleanup_gendisks:
	/* kfree can handle NULL, so no test is required here */
	for (i = 0; i < N_USED_SD_MAJORS; i++) {
		kfree(sd_gendisks[i].de_arr);
		kfree(sd_gendisks[i].flags);
	}
cleanup_mem:
	kfree(sd_gendisks);
	kfree(sd);
	kfree(sd_blocksizes);
	kfree(sd_sizes);
	kfree(rscsi_disks);
	for (i = 0; i < N_USED_SD_MAJORS; i++) {
		devfs_unregister_blkdev(SD_MAJOR(i), "sd");
	}
	sd_registered--;
	return 1;
}


static void sd_finish()
{
	int i;

	for (i = 0; i < N_USED_SD_MAJORS; i++) {
		blk_dev[SD_MAJOR(i)].queue = sd_find_queue;
		add_gendisk(&(sd_gendisks[i]));
	}

	for (i = 0; i < sd_template.dev_max; ++i)
		if (!rscsi_disks[i].capacity && rscsi_disks[i].device) {
			sd_init_onedisk(i);
			if (!rscsi_disks[i].has_part_table) {
				sd_sizes[i << 4] = rscsi_disks[i].capacity;
				register_disk(&SD_GENDISK(i), MKDEV_SD(i),
						1<<4, &sd_fops,
						rscsi_disks[i].capacity);
				rscsi_disks[i].has_part_table = 1;
			}
		}

	return;
}

static int sd_detect(Scsi_Device * SDp)
{
	if (SDp->type != TYPE_DISK && SDp->type != TYPE_MOD)
		return 0;
	sd_template.dev_noticed++;
	return 1;
}

static int sd_attach(Scsi_Device * SDp)
{
        unsigned int devnum;
	Scsi_Disk *dpnt;
	int i;
	char nbuff[6];

	if (SDp->type != TYPE_DISK && SDp->type != TYPE_MOD)
		return 0;

	if (sd_template.nr_dev >= sd_template.dev_max) {
		SDp->attached--;
		return 1;
	}
	for (dpnt = rscsi_disks, i = 0; i < sd_template.dev_max; i++, dpnt++)
		if (!dpnt->device)
			break;

	if (i >= sd_template.dev_max)
		panic("scsi_devices corrupt (sd)");

	rscsi_disks[i].device = SDp;
	rscsi_disks[i].has_part_table = 0;
	sd_template.nr_dev++;
	SD_GENDISK(i).nr_real++;
        devnum = i % SCSI_DISKS_PER_MAJOR;
        SD_GENDISK(i).de_arr[devnum] = SDp->de;
        if (SDp->removable)
		SD_GENDISK(i).flags[devnum] |= GENHD_FL_REMOVABLE;
	sd_devname(i, nbuff);
	printk("Attached scsi %sdisk %s at scsi%d, channel %d, id %d, lun %d\n",
	       SDp->removable ? "removable " : "",
	       nbuff, SDp->host->host_no, SDp->channel, SDp->id, SDp->lun);
	return 0;
}

#define DEVICE_BUSY rscsi_disks[target].device->busy
#define ALLOW_REVALIDATE rscsi_disks[target].device->allow_revalidate
#define USAGE rscsi_disks[target].device->access_count
#define CAPACITY rscsi_disks[target].capacity
#define MAYBE_REINIT  sd_init_onedisk(target)

/* This routine is called to flush all partitions and partition tables
 * for a changed scsi disk, and then re-read the new partition table.
 * If we are revalidating a disk because of a media change, then we
 * enter with usage == 0.  If we are using an ioctl, we automatically have
 * usage == 1 (we need an open channel to use an ioctl :-), so this
 * is our limit.
 */
int revalidate_scsidisk(kdev_t dev, int maxusage)
{
	int target;
	int res;

	target = DEVICE_NR(dev);

	if (DEVICE_BUSY || (ALLOW_REVALIDATE == 0 && USAGE > maxusage)) {
		printk("Device busy for revalidation (usage=%d)\n", USAGE);
		return -EBUSY;
	}
	DEVICE_BUSY = 1;

	res = wipe_partitions(dev);
	if (res)
		goto leave;

#ifdef MAYBE_REINIT
	MAYBE_REINIT;
#endif

	grok_partitions(dev, CAPACITY);
leave:
	DEVICE_BUSY = 0;
	return res;
}

static int fop_revalidate_scsidisk(kdev_t dev)
{
	return revalidate_scsidisk(dev, 0);
}
static void sd_detach(Scsi_Device * SDp)
{
	Scsi_Disk *dpnt;
	kdev_t dev;
	int i, j;
	int max_p;
	int start;

	for (dpnt = rscsi_disks, i = 0; i < sd_template.dev_max; i++, dpnt++)
		if (dpnt->device == SDp) {

			max_p = 1 << sd_gendisk.minor_shift;
			start = i << sd_gendisk.minor_shift;
			dev = MKDEV_SD_PARTITION(start);
			wipe_partitions(dev);
			for (j = max_p - 1; j >= 0; j--)
				sd_sizes[start + j] = 0;

                        devfs_register_partitions (&SD_GENDISK (i),
                                                   SD_MINOR_NUMBER (start), 1);
			/* unregister_disk() */
			dpnt->has_part_table = 0;
			dpnt->device = NULL;
			dpnt->capacity = 0;
			SDp->attached--;
			sd_template.dev_noticed--;
			sd_template.nr_dev--;
			SD_GENDISK(i).nr_real--;
			return;
		}
}

static int __init init_sd(void)
{
	sd_template.module = THIS_MODULE;
	return scsi_register_device(&sd_template);
}

static void __exit exit_sd(void)
{
	int i;

	scsi_unregister_device(&sd_template);

	for (i = 0; i < N_USED_SD_MAJORS; i++)
		devfs_unregister_blkdev(SD_MAJOR(i), "sd");

	sd_registered--;
	if (rscsi_disks != NULL) {
		kfree(rscsi_disks);
		kfree(sd_sizes);
		kfree(sd_blocksizes);
		kfree((char *) sd);
	}
	for (i = 0; i < N_USED_SD_MAJORS; i++) {
		del_gendisk(&(sd_gendisks[i]));
		blk_clear(SD_MAJOR(i));
	}
	sd_template.dev_max = 0;
	if (sd_gendisks != &sd_gendisk)
		kfree(sd_gendisks);
}

module_init(init_sd);
module_exit(exit_sd);
MODULE_LICENSE("GPL");
