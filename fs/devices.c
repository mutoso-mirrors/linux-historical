/*
 *  linux/fs/devices.c
 *
 * (C) 1993 Matthias Urlichs -- collected common code and tables.
 * 
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/fs.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/ext_fs.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/errno.h>

/*
 * Ugly. We'll fix this once all the drivers use the f_ops->check_media_change()
 * stuff instead..
 */
#ifdef CONFIG_SCSI
#ifdef CONFIG_BLK_DEV_SR
extern int check_cdrom_media_change(int, int);
#endif
#ifdef CONFIG_BLK_DEV_SD
extern int check_scsidisk_media_change(int, int);
extern int revalidate_scsidisk(int, int);
#endif
#endif
#ifdef CONFIG_CDU31A
extern int check_cdu31a_media_change(int, int);
#endif
#ifdef CONFIG_MCD
extern int check_mcd_media_change(int, int);
#endif
#ifdef CONFIG_SBPCD
extern int check_sbpcd_media_change(int, int);
#endif

struct device_struct {
	const char * name;
	struct file_operations * fops;
};

static struct device_struct chrdevs[MAX_CHRDEV] = {
	{ NULL, NULL },
};

static struct device_struct blkdevs[MAX_BLKDEV] = {
	{ NULL, NULL },
};

int get_device_list(char * page)
{
	int i;
	int len;

	len = sprintf(page, "Character devices:\n");
	for (i = 0; i < MAX_CHRDEV ; i++) {
		if (chrdevs[i].fops) {
			len += sprintf(page+len, "%2d %s\n", i, chrdevs[i].name);
		}
	}
	len += sprintf(page+len, "\nBlock devices:\n");
	for (i = 0; i < MAX_BLKDEV ; i++) {
		if (blkdevs[i].fops) {
			len += sprintf(page+len, "%2d %s\n", i, blkdevs[i].name);
		}
	}
	return len;
}

struct file_operations * get_blkfops(unsigned int major)
{
	if (major >= MAX_BLKDEV)
		return NULL;
	return blkdevs[major].fops;
}

struct file_operations * get_chrfops(unsigned int major)
{
	if (major >= MAX_CHRDEV)
		return NULL;
	return chrdevs[major].fops;
}

int register_chrdev(unsigned int major, const char * name, struct file_operations *fops)
{
	if (major >= MAX_CHRDEV)
		return -EINVAL;
	if (chrdevs[major].fops)
		return -EBUSY;
	chrdevs[major].name = name;
	chrdevs[major].fops = fops;
	return 0;
}

int register_blkdev(unsigned int major, const char * name, struct file_operations *fops)
{
	if (major >= MAX_BLKDEV)
		return -EINVAL;
	if (blkdevs[major].fops)
		return -EBUSY;
	blkdevs[major].name = name;
	blkdevs[major].fops = fops;
	return 0;
}

int unregister_chrdev(unsigned int major, const char * name)
{
	if (major >= MAX_CHRDEV)
		return -EINVAL;
	if (!chrdevs[major].fops)
		return -EINVAL;
	if (strcmp(chrdevs[major].name, name))
		return -EINVAL;
	chrdevs[major].name = NULL;
	chrdevs[major].fops = NULL;
	return 0;
}

int unregister_blkdev(unsigned int major, const char * name)
{
	if (major >= MAX_BLKDEV)
		return -EINVAL;
	if (!blkdevs[major].fops)
		return -EINVAL;
	if (strcmp(blkdevs[major].name, name))
		return -EINVAL;
	blkdevs[major].name = NULL;
	blkdevs[major].fops = NULL;
	return 0;
}

/*
 * This routine checks whether a removable media has been changed,
 * and invalidates all buffer-cache-entries in that case. This
 * is a relatively slow routine, so we have to try to minimize using
 * it. Thus it is called only upon a 'mount' or 'open'. This
 * is the best way of combining speed and utility, I think.
 * People changing diskettes in the middle of an operation deserve
 * to loose :-)
 */
void check_disk_change(dev_t dev)
{
	int i;
	struct file_operations * fops;

	i = MAJOR(dev);
	if (i >= MAX_BLKDEV || (fops = blkdevs[i].fops) == NULL)
		return;
	if (fops->check_media_change != NULL) {
		if (!fops->check_media_change(dev))
			return;
	} 
#if 1 /* this will go soon.. */
	else switch(MAJOR(dev)){
#if defined(CONFIG_BLK_DEV_SD) && defined(CONFIG_SCSI)
         case SCSI_DISK_MAJOR:
		if (!check_scsidisk_media_change(dev, 0))
			return;
		break;
#endif

#if defined(CONFIG_BLK_DEV_SR) && defined(CONFIG_SCSI)
	 case SCSI_CDROM_MAJOR:
		if (!check_cdrom_media_change(dev, 0))
			return;
		break;
#endif

#if defined(CONFIG_CDU31A)
         case CDU31A_CDROM_MAJOR:
		if (!check_cdu31a_media_change(dev, 0))
			return;
		break;
#endif

#if defined(CONFIG_MCD)
         case MITSUMI_CDROM_MAJOR:
		if (!check_mcd_media_change(dev, 0))
			return;
		break;
#endif

#if defined(CONFIG_SBPCD)
         case MATSUSHITA_CDROM_MAJOR:
		if (!check_sbpcd_media_change(dev, 0))
			return;
		break;
#endif

         default:
		return;
	}
#endif	/* will go away */

	printk("VFS: Disk change detected on device %d/%d\n",
					MAJOR(dev), MINOR(dev));
	for (i=0 ; i<NR_SUPER ; i++)
		if (super_blocks[i].s_dev == dev)
			put_super(super_blocks[i].s_dev);
	invalidate_inodes(dev);
	invalidate_buffers(dev);

	if (fops->revalidate)
		fops->revalidate(dev);

#if defined(CONFIG_BLK_DEV_SD) && defined(CONFIG_SCSI)
/* This is trickier for a removable hardisk, because we have to invalidate
   all of the partitions that lie on the disk. */
	if (MAJOR(dev) == SCSI_DISK_MAJOR)
		revalidate_scsidisk(dev, 0);
#endif
}

/*
 * Called every time a block special file is opened
 */
int blkdev_open(struct inode * inode, struct file * filp)
{
	int i;

	i = MAJOR(inode->i_rdev);
	if (i >= MAX_BLKDEV || !blkdevs[i].fops)
		return -ENODEV;
	filp->f_op = blkdevs[i].fops;
	if (filp->f_op->open)
		return filp->f_op->open(inode,filp);
	return 0;
}	

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the special file...
 */
struct file_operations def_blk_fops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* select */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	blkdev_open,	/* open */
	NULL,		/* release */
};

struct inode_operations blkdev_inode_operations = {
	&def_blk_fops,		/* default file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

/*
 * Called every time a character special file is opened
 */
int chrdev_open(struct inode * inode, struct file * filp)
{
	int i;

	i = MAJOR(inode->i_rdev);
	if (i >= MAX_CHRDEV || !chrdevs[i].fops)
		return -ENODEV;
	filp->f_op = chrdevs[i].fops;
	if (filp->f_op->open)
		return filp->f_op->open(inode,filp);
	return 0;
}

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the special file...
 */
struct file_operations def_chr_fops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* select */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	chrdev_open,	/* open */
	NULL,		/* release */
};

struct inode_operations chrdev_inode_operations = {
	&def_chr_fops,		/* default file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};
