/*
   ataraid.c  Copyright (C) 2001 Red Hat, Inc. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
   
   You should have received a copy of the GNU General Public License
   (for example /usr/src/linux/COPYING); if not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.  
   
   Authors: 	Arjan van de Ven <arjanv@redhat.com>
   		
   
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/semaphore.h>
#include <linux/sched.h>
#include <linux/smp_lock.h>
#include <linux/blkdev.h>
#include <linux/blkpg.h>
#include <linux/genhd.h>
#include <linux/ioctl.h>
#include <linux/kdev_t.h>
#include <linux/swap.h>
#include <linux/buffer_head.h>

#include <linux/ide.h>
#include <asm/uaccess.h>

#include "ataraid.h"

static struct raid_device_operations *ataraid_ops[16];

static int ataraid_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg);
static int ataraid_open(struct inode *inode, struct file *filp);
static int ataraid_release(struct inode *inode, struct file *filp);
static void ataraid_split_request(request_queue_t * q, int rw,
				  struct buffer_head *bh);


static struct gendisk ataraid_gendisk[16];
static struct hd_struct *ataraid_part;
static char *ataraid_names;
static int ataraid_readahead[256];

static struct block_device_operations ataraid_fops = {
	.owner = THIS_MODULE,
	.open = ataraid_open,
	.release = ataraid_release,
	.ioctl = ataraid_ioctl,
};



static DECLARE_MUTEX(ataraid_sem);

/* Bitmap for the devices currently in use */
static unsigned int ataraiduse;


/* stub fops functions */

static int ataraid_ioctl(struct inode *inode, struct file *file,
			 unsigned int cmd, unsigned long arg)
{
	int minor;
	minor = minor(inode->i_rdev) >> SHIFT;

	if ((ataraid_ops[minor]) && (ataraid_ops[minor]->ioctl))
		return (ataraid_ops[minor]->ioctl) (inode, file, cmd, arg);
	return -EINVAL;
}

static int ataraid_open(struct inode *inode, struct file *filp)
{
	int minor;
	minor = minor(inode->i_rdev) >> SHIFT;

	if ((ataraid_ops[minor]) && (ataraid_ops[minor]->open))
		return (ataraid_ops[minor]->open) (inode, filp);
	return -EINVAL;
}


static int ataraid_release(struct inode *inode, struct file *filp)
{
	int minor;
	minor = minor(inode->i_rdev) >> SHIFT;

	if ((ataraid_ops[minor]) && (ataraid_ops[minor]->release))
		return (ataraid_ops[minor]->release) (inode, filp);
	return -EINVAL;
}

static int ataraid_make_request(request_queue_t * q, int rw,
				struct buffer_head *bh)
{
	int minor;
	int retval;
	minor = minor(bh->b_rdev) >> SHIFT;

	if ((ataraid_ops[minor]) && (ataraid_ops[minor]->make_request)) {

		retval = (ataraid_ops[minor]->make_request) (q, rw, bh);
		if (retval == -1) {
			ataraid_split_request(q, rw, bh);
			return 0;
		} else
			return retval;
	}
	return -EINVAL;
}

struct buffer_head *ataraid_get_bhead(void)
{
	void *ptr = NULL;
	while (!ptr) {
		ptr = kmalloc(sizeof(struct buffer_head), GFP_NOIO);
		if (!ptr)
			yield();
	}
	return ptr;
}

EXPORT_SYMBOL(ataraid_get_bhead);

struct ataraid_bh_private *ataraid_get_private(void)
{
	void *ptr = NULL;
	while (!ptr) {
		ptr = kmalloc(sizeof(struct ataraid_bh_private), GFP_NOIO);
		if (!ptr)
			yield();
	}
	return ptr;
}

EXPORT_SYMBOL(ataraid_get_private);

void ataraid_end_request(struct buffer_head *bh, int uptodate)
{
	struct ataraid_bh_private *private = bh->b_private;

	if (private == NULL)
		BUG();

	if (atomic_dec_and_test(&private->count)) {
		private->parent->b_end_io(private->parent, uptodate);
		private->parent = NULL;
		kfree(private);
	}
	kfree(bh);
}

EXPORT_SYMBOL(ataraid_end_request);

static void ataraid_split_request(request_queue_t * q, int rw,
				  struct buffer_head *bh)
{
	struct buffer_head *bh1, *bh2;
	struct ataraid_bh_private *private;
	bh1 = ataraid_get_bhead();
	bh2 = ataraid_get_bhead();

	/* If either of those ever fails we're doomed */
	if ((!bh1) || (!bh2))
		BUG();
	private = ataraid_get_private();
	if (private == NULL)
		BUG();

	memcpy(bh1, bh, sizeof(*bh));
	memcpy(bh2, bh, sizeof(*bh));

	bh1->b_end_io = ataraid_end_request;
	bh2->b_end_io = ataraid_end_request;

	bh2->b_rsector += bh->b_size >> 10;
	bh1->b_size /= 2;
	bh2->b_size /= 2;
	private->parent = bh;

	bh1->b_private = private;
	bh2->b_private = private;
	atomic_set(&private->count, 2);

	bh2->b_data += bh->b_size / 2;

	generic_make_request(rw, bh1);
	generic_make_request(rw, bh2);
}




/* device register / release functions */


int ataraid_get_device(struct raid_device_operations *fops)
{
	int bit;
	down(&ataraid_sem);
	if (ataraiduse == ~0U) {
		up(&ataraid_sem);
		return -ENODEV;
	}
	bit = ffz(ataraiduse);
	ataraiduse |= 1 << bit;
	ataraid_ops[bit] = fops;
	up(&ataraid_sem);
	return bit;
}

void ataraid_release_device(int device)
{
	down(&ataraid_sem);

	if ((ataraiduse & (1 << device)) == 0)
		BUG();		/* device wasn't registered at all */

	ataraiduse &= ~(1 << device);
	ataraid_ops[device] = NULL;
	up(&ataraid_sem);
}

void ataraid_register_disk(int device, long size)
{
	struct gendisk *disk = ataraid_gendisk + device;
	char *name = ataraid_names + 12 * device;

	sprintf(name, "ataraid/d%d", device);
	disk->part = ataraid_part + 16 * device;
	disk->major = ATAMAJOR;
	disk->first_minor = 16 * device;
	disk->major_name = name;
	disk->minor_shift = 4;
	disk->nr_real = 1;
	disk->fops = &ataraid_fops;

	add_gendisk(disk);
	register_disk(disk,
		      mk_kdev(disk->major, disk->first_minor),
		      1 << disk->minor_shift,
		      disk->fops, size);
}

void ataraid_unregister_disk(int device)
{
	del_gendisk(&ataraid_gendisk[device]);
}

static __init int ataraid_init(void)
{
	int i;
	for (i = 0; i < 256; i++)
		ataraid_readahead[i] = 1023;

	/* setup the gendisk structure */
	ataraid_part = kmalloc(256 * sizeof(struct hd_struct), GFP_KERNEL);
	ataraid_names = kmalloc(16 * 12, GFP_KERNEL);
	if (!ataraid_part || !ataraid_names) {
		kfree(ataraid_part);
		kfree(ataraid_names);
		printk(KERN_ERR
		       "ataraid: Couldn't allocate memory, aborting \n");
		return -1;
	}

	memset(ataraid_part, 0, 256 * sizeof(struct hd_struct));

	if (register_blkdev(ATAMAJOR, "ataraid", &ataraid_fops)) {
		kfree(ataraid_part);
		kfree(ataraid_names);
		printk(KERN_ERR "ataraid: Could not get major %d \n",
		       ATAMAJOR);
		return -1;
	}

	blk_queue_make_request(BLK_DEFAULT_QUEUE(ATAMAJOR),
			       ataraid_make_request);

	return 0;
}

static void __exit ataraid_exit(void)
{
	unregister_blkdev(ATAMAJOR, "ataraid");
	kfree(ataraid_part);
	kfree(ataraid_names);
}

module_init(ataraid_init);
module_exit(ataraid_exit);

EXPORT_SYMBOL(ataraid_get_device);
EXPORT_SYMBOL(ataraid_release_device);
EXPORT_SYMBOL(ataraid_register_disk);
EXPORT_SYMBOL(ataraid_unregister_disk);
MODULE_LICENSE("GPL");
