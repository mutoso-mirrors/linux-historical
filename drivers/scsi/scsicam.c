/*
 * scsicam.c - SCSI CAM support functions, use for HDIO_GETGEO, etc.
 *
 * Copyright 1993, 1994 Drew Eckhardt
 *      Visionary Computing 
 *      (Unix and Linux consulting and custom programming)
 *      drew@Colorado.EDU
 *	+1 (303) 786-7975
 *
 * For more information, please consult the SCSI-CAM draft.
 */

#ifdef MODULE
/*
 * Don't import our own symbols, as this would severely mess up our
 * symbol tables.
 */
#define _SCSI_SYMS_VER_
#include <linux/autoconf.h>
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include "../block/blk.h"
#include "scsi.h"
#include "hosts.h"
#include "sd.h"

static int partsize(struct buffer_head *bh, unsigned long capacity,
    unsigned int  *cyls, unsigned int *hds, unsigned int *secs);
static int setsize(unsigned long capacity,unsigned int *cyls,unsigned int *hds,
    unsigned int *secs);

/*
 * Function : int scsicam_bios_param (Disk *disk, int dev, int *ip)
 *
 * Purpose : to determine the BIOS mapping used for a drive in a 
 *	SCSI-CAM system, storing the results in ip as required
 *	by the HDIO_GETGEO ioctl().
 *
 * Returns : -1 on failure, 0 on success.
 *
 */

int scsicam_bios_param (Disk *disk, /* SCSI disk */
	kdev_t dev,		/* Device major, minor */
    	int *ip			/* Heads, sectors, cylinders in that order */) {

    struct buffer_head *bh;
    int ret_code;
    int size = disk->capacity;

    if (!(bh = bread(MKDEV(MAJOR(dev), MINOR(dev)&~0xf), 0, 1024)))
	return -1;

#ifdef DEBUG
	printk ("scsicam_bios_param : trying existing mapping\n");
#endif
    ret_code = partsize (bh, (unsigned long) size, (unsigned int *) ip + 2, 
	(unsigned int *) ip + 0, (unsigned int *) ip + 1);
    brelse (bh);

    if (ret_code == -1) {
#ifdef DEBUG
	printk ("scsicam_bios_param : trying optimal mapping\n");
#endif
	ret_code = setsize ((unsigned long) size, (unsigned int *) ip + 2, 
    	    (unsigned int *) ip + 0, (unsigned int *) ip + 1);
    }

    return ret_code;
}

/*
 * Function : static int partsize(struct buffer_head *bh, unsigned long 
 *     capacity,unsigned int *cyls, unsigned int *hds, unsigned int *secs);
 *
 * Purpose : to determine the BIOS mapping used to create the partition
 *	table, storing the results in *cyls, *hds, and *secs 
 *
 * Returns : -1 on failure, 0 on success.
 *
 */

static int partsize(struct buffer_head *bh, unsigned long capacity,
    unsigned int  *cyls, unsigned int *hds, unsigned int *secs) {
    struct partition *p, *largest = NULL;
    int i, largest_cyl;
    int cyl, end_head, end_cyl, end_sector;
    unsigned int logical_end, physical_end;
    

    if (*(unsigned short *) (bh->b_data+510) == 0xAA55) {
	for (largest_cyl = -1, p = (struct partition *) 
    	    (0x1BE + bh->b_data), i = 0; i < 4; ++i, ++p) {
    	    if (!p->sys_ind)
    	    	continue;
#ifdef DEBUG
	printk ("scsicam_bios_param : partition %d has system \n",
	    i);
#endif
    	    cyl = p->cyl + ((p->sector & 0xc0) << 2);
    	    if (cyl > largest_cyl) {
    	    	largest_cyl = cyl;
    	    	largest = p;
    	    }
    	}
    }

    if (largest) {
    	end_cyl = largest->end_cyl + ((largest->end_sector & 0xc0) << 2);
    	end_head = largest->end_head;
    	end_sector = largest->end_sector & 0x3f;
#ifdef DEBUG
	printk ("scsicam_bios_param : end at h = %d, c = %d, s = %d\n",
	    end_head, end_cyl, end_sector);
#endif

    	physical_end =  end_cyl * (end_head + 1) * end_sector +
    	    end_head * end_sector + end_sector;

	/* This is the actual _sector_ number at the end */
	logical_end = largest->start_sect + largest->nr_sects;

    	if (logical_end == physical_end) {
    	    *secs = end_sector;
    	    *hds = end_head + 1;
    	    *cyls = capacity / ((end_head + 1) * end_sector);
    	    return 0;
    	}
#ifdef DEBUG
	printk ("scsicam_bios_param : logical (%u) != physical (%u)\n",
	    logical_end, physical_end);
#endif
    }
    return -1;
}

/*
 * Function : static int setsize(unsigned long capacity,unsigned int *cyls,
 *	unsigned int *hds, unsigned int *secs);
 *
 * Purpose : to determine a near-optimal int 0x13 mapping for a
 *	SCSI disk in terms of lost space of size capacity, storing
 *	the results in *cyls, *hds, and *secs.
 *
 * Returns : -1 on failure, 0 on success.
 *
 * Extracted from
 *
 * WORKING                                                    X3T9.2
 * DRAFT                                                        792D
 *
 *
 *                                                        Revision 6
 *                                                         10-MAR-94
 * Information technology -
 * SCSI-2 Common access method
 * transport and SCSI interface module
 * 
 * ANNEX A :
 *
 * setsize() converts a read capacity value to int 13h
 * head-cylinder-sector requirements. It minimizes the value for
 * number of heads and maximizes the number of cylinders. This
 * will support rather large disks before the number of heads
 * will not fit in 4 bits (or 6 bits). This algorithm also
 * minimizes the number of sectors that will be unused at the end
 * of the disk while allowing for very large disks to be
 * accommodated. This algorithm does not use physical geometry. 
 */

static int setsize(unsigned long capacity,unsigned int *cyls,unsigned int *hds,
    unsigned int *secs) { 
    unsigned int rv = 0; 
    unsigned long heads, sectors, cylinders, temp; 

    cylinders = 1024L;			/* Set number of cylinders to max */ 
    sectors = 62L;      		/* Maximize sectors per track */ 

    temp = cylinders * sectors;		/* Compute divisor for heads */ 
    heads = capacity / temp;		/* Compute value for number of heads */
    if (capacity % temp) {		/* If no remainder, done! */ 
    	heads++;                	/* Else, increment number of heads */ 
    	temp = cylinders * heads;	/* Compute divisor for sectors */ 
    	sectors = capacity / temp;	/* Compute value for sectors per
					       track */ 
    	if (capacity % temp) {		/* If no remainder, done! */ 
      	    sectors++;                  /* Else, increment number of sectors */ 
      	    temp = heads * sectors;	/* Compute divisor for cylinders */
      	    cylinders = capacity / temp;/* Compute number of cylinders */ 
      	} 
    } 
    if (cylinders == 0) rv=(unsigned)-1;/* Give error if 0 cylinders */ 

    *cyls = (unsigned int) cylinders;	/* Stuff return values */ 
    *secs = (unsigned int) sectors; 
    *hds  = (unsigned int) heads; 
    return(rv); 
} 
