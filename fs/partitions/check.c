/*
 *  fs/partitions/check.c
 *
 *  Code extracted from drivers/block/genhd.c
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 *
 *  We now have independent partition support from the
 *  block drivers, which allows all the partition code to
 *  be grouped in one location, and it to be mostly self
 *  contained.
 *
 *  Added needed MAJORS for new pairs, {hdi,hdj}, {hdk,hdl}
 */

#include <linux/init.h>
#include <linux/fs.h>
#include <linux/blk.h>
#include <linux/kmod.h>
#include <linux/ctype.h>

#include "check.h"

#include "acorn.h"
#include "amiga.h"
#include "atari.h"
#include "ldm.h"
#include "mac.h"
#include "msdos.h"
#include "osf.h"
#include "sgi.h"
#include "sun.h"
#include "ibm.h"
#include "ultrix.h"
#include "efi.h"

#if CONFIG_BLK_DEV_MD
extern void md_autodetect_dev(dev_t dev);
#endif

int warn_no_part = 1; /*This is ugly: should make genhd removable media aware*/

static int (*check_part[])(struct parsed_partitions *, struct block_device *) = {
#ifdef CONFIG_ACORN_PARTITION
	acorn_partition,
#endif
#ifdef CONFIG_EFI_PARTITION
	efi_partition,		/* this must come before msdos */
#endif
#ifdef CONFIG_LDM_PARTITION
	ldm_partition,		/* this must come before msdos */
#endif
#ifdef CONFIG_MSDOS_PARTITION
	msdos_partition,
#endif
#ifdef CONFIG_OSF_PARTITION
	osf_partition,
#endif
#ifdef CONFIG_SUN_PARTITION
	sun_partition,
#endif
#ifdef CONFIG_AMIGA_PARTITION
	amiga_partition,
#endif
#ifdef CONFIG_ATARI_PARTITION
	atari_partition,
#endif
#ifdef CONFIG_MAC_PARTITION
	mac_partition,
#endif
#ifdef CONFIG_SGI_PARTITION
	sgi_partition,
#endif
#ifdef CONFIG_ULTRIX_PARTITION
	ultrix_partition,
#endif
#ifdef CONFIG_IBM_PARTITION
	ibm_partition,
#endif
	NULL
};
 
/*
 * disk_name() is used by partition check code and the md driver.
 * It formats the devicename of the indicated disk into
 * the supplied buffer (of size at least 32), and returns
 * a pointer to that same buffer (for convenience).
 */

char *disk_name(struct gendisk *hd, int part, char *buf)
{
	int pos;
	if (!part) {
		if (hd->disk_de) {
			pos = devfs_generate_path(hd->disk_de, buf, 64);
			if (pos >= 0)
				return buf + pos;
		}
		sprintf(buf, "%s", hd->disk_name);
	} else {
		if (hd->part[part-1].de) {
			pos = devfs_generate_path(hd->part[part-1].de, buf, 64);
			if (pos >= 0)
				return buf + pos;
		}
		if (isdigit(hd->disk_name[strlen(hd->disk_name)-1]))
			sprintf(buf, "%sp%d", hd->disk_name, part);
		else
			sprintf(buf, "%s%d", hd->disk_name, part);
	}
	return buf;
}

/* Driverfs file support */
static ssize_t partition_device_kdev_read(struct device *driverfs_dev, 
			char *page, size_t count, loff_t off)
{
	kdev_t kdev; 
	kdev.value=(int)(long)driverfs_dev->driver_data;
	return off ? 0 : sprintf (page, "%x\n",kdev.value);
}
static DEVICE_ATTR(kdev,S_IRUGO,partition_device_kdev_read,NULL);

static ssize_t partition_device_type_read(struct device *driverfs_dev, 
			char *page, size_t count, loff_t off) 
{
	return off ? 0 : sprintf (page, "BLK\n");
}
static DEVICE_ATTR(type,S_IRUGO,partition_device_type_read,NULL);

static void driverfs_create_partitions(struct gendisk *hd)
{
	struct device *parent = hd->driverfs_dev;
	struct device *dev = &hd->disk_dev;

	/* if driverfs not supported by subsystem, skip partitions */
	if (!(hd->flags & GENHD_FL_DRIVERFS))
		return;

	if (parent)  {
		sprintf(dev->name, "%sdisc", parent->name);
		sprintf(dev->bus_id, "%sdisc", parent->bus_id);
		dev->parent = parent;
		dev->bus = parent->bus;
	} else {
		sprintf(dev->name, "disc");
		sprintf(dev->bus_id, "disc");
	}
	dev->driver_data = (void *)(long)__mkdev(hd->major, hd->first_minor);
	device_register(dev);
	device_create_file(dev, &dev_attr_type);
	device_create_file(dev, &dev_attr_kdev);
}

static void driverfs_remove_partitions(struct gendisk *hd)
{
	struct device *dev = &hd->disk_dev;
	if (!(hd->flags & GENHD_FL_DRIVERFS))
		return;
	device_remove_file(dev, &dev_attr_type);
	device_remove_file(dev, &dev_attr_kdev);
	put_device(dev);	
}

static struct parsed_partitions *
check_partition(struct gendisk *hd, struct block_device *bdev)
{
	struct parsed_partitions *state;
	devfs_handle_t de = NULL;
	char buf[64];
	int i, res;

	state = kmalloc(sizeof(struct parsed_partitions), GFP_KERNEL);
	if (!state)
		return NULL;

	if (hd->flags & GENHD_FL_DEVFS)
		de = hd->de;
	i = devfs_generate_path (de, buf, sizeof buf);
	if (i >= 0) {
		printk(KERN_INFO " /dev/%s:", buf + i);
		sprintf(state->name, "p");
	} else {
		disk_name(hd, 0, state->name);
		printk(KERN_INFO " %s:", state->name);
		if (isdigit(state->name[strlen(state->name)-1]))
			sprintf(state->name, "p");
	}
	state->limit = hd->minors;
	i = res = 0;
	while (!res && check_part[i]) {
		memset(&state->parts, 0, sizeof(state->parts));
		res = check_part[i++](state, bdev);
	}
	if (res > 0)
		return state;
	if (!res)
		printk(" unknown partition table\n");
	else if (warn_no_part)
		printk(" unable to read partition table\n");
	kfree(state);
	return NULL;
}

static void devfs_register_partition(struct gendisk *dev, int part)
{
#ifdef CONFIG_DEVFS_FS
	devfs_handle_t dir;
	unsigned int devfs_flags = DEVFS_FL_DEFAULT;
	struct hd_struct *p = dev->part;
	char devname[16];

	if (p[part-1].de)
		return;
	dir = devfs_get_parent(dev->disk_de);
	if (!dir)
		return;
	if (dev->flags & GENHD_FL_REMOVABLE)
		devfs_flags |= DEVFS_FL_REMOVABLE;
	sprintf(devname, "part%d", part);
	p[part-1].de = devfs_register (dir, devname, devfs_flags,
				    dev->major, dev->first_minor + part,
				    S_IFBLK | S_IRUSR | S_IWUSR,
				    dev->fops, NULL);
#endif
}

#ifdef CONFIG_DEVFS_FS
static struct unique_numspace disc_numspace = UNIQUE_NUMBERSPACE_INITIALISER;
static devfs_handle_t cdroms;
static struct unique_numspace cdrom_numspace = UNIQUE_NUMBERSPACE_INITIALISER;
#endif

static void devfs_create_partitions(struct gendisk *dev)
{
#ifdef CONFIG_DEVFS_FS
	int pos = 0;
	devfs_handle_t dir, slave;
	unsigned int devfs_flags = DEVFS_FL_DEFAULT;
	char dirname[64], symlink[16];
	static devfs_handle_t devfs_handle;
	int part, max_p = dev->minors;
	struct hd_struct *p = dev->part;

	if (dev->flags & GENHD_FL_REMOVABLE)
		devfs_flags |= DEVFS_FL_REMOVABLE;
	if (dev->flags & GENHD_FL_DEVFS) {
		dir = dev->de;
		if (!dir)  /*  Aware driver wants to block disc management  */
			return;
		pos = devfs_generate_path(dir, dirname + 3, sizeof dirname-3);
		if (pos < 0)
			return;
		strncpy(dirname + pos, "../", 3);
	} else {
		/*  Unaware driver: construct "real" directory  */
		sprintf(dirname, "../%s/disc%d", dev->disk_name,
			dev->first_minor >> dev->minor_shift);
		dir = devfs_mk_dir(NULL, dirname + 3, NULL);
	}
	if (!devfs_handle)
		devfs_handle = devfs_mk_dir(NULL, "discs", NULL);
	dev->number = devfs_alloc_unique_number (&disc_numspace);
	sprintf(symlink, "disc%d", dev->number);
	devfs_mk_symlink (devfs_handle, symlink, DEVFS_FL_DEFAULT,
			  dirname + pos, &slave, NULL);
	dev->disk_de = devfs_register(dir, "disc", devfs_flags,
			    dev->major, dev->first_minor,
			    S_IFBLK | S_IRUSR | S_IWUSR, dev->fops, NULL);
	devfs_auto_unregister(dev->disk_de, slave);
	if (!(dev->flags & GENHD_FL_DEVFS))
		devfs_auto_unregister (slave, dir);
#endif
}

static void devfs_create_cdrom(struct gendisk *dev)
{
#ifdef CONFIG_DEVFS_FS
	int pos = 0;
	devfs_handle_t dir, slave;
	unsigned int devfs_flags = DEVFS_FL_DEFAULT;
	char dirname[64], symlink[16];
	char vname[23];

	if (!cdroms)
		cdroms = devfs_mk_dir (NULL, "cdroms", NULL);

	dev->number = devfs_alloc_unique_number(&cdrom_numspace);
	sprintf(vname, "cdrom%d", dev->number);
	if (dev->de) {
		int pos;
		devfs_handle_t slave;
		char rname[64];

		dev->disk_de = devfs_register(dev->de, "cd", DEVFS_FL_DEFAULT,
				     dev->major, dev->first_minor,
				     S_IFBLK | S_IRUGO | S_IWUGO,
				     dev->fops, NULL);

		pos = devfs_generate_path(dev->disk_de, rname+3, sizeof(rname)-3);
		if (pos >= 0) {
			strncpy(rname + pos, "../", 3);
			devfs_mk_symlink(cdroms, vname,
					 DEVFS_FL_DEFAULT,
					 rname + pos, &slave, NULL);
			devfs_auto_unregister(dev->de, slave);
		}
	} else {
		dev->disk_de = devfs_register (NULL, vname, DEVFS_FL_DEFAULT,
				    dev->major, dev->first_minor,
				    S_IFBLK | S_IRUGO | S_IWUGO,
				    dev->fops, NULL);
	}
#endif
}

static void devfs_remove_partitions(struct gendisk *dev)
{
#ifdef CONFIG_DEVFS_FS
	devfs_unregister(dev->disk_de);
	dev->disk_de = NULL;
	if (dev->flags & GENHD_FL_CD)
		devfs_dealloc_unique_number(&cdrom_numspace, dev->number);
	else
		devfs_dealloc_unique_number(&disc_numspace, dev->number);
#endif
}

void delete_partition(struct gendisk *disk, int part)
{
	struct hd_struct *p = disk->part + part - 1;
	struct device *dev;
	if (!p->nr_sects)
		return;
	p->start_sect = 0;
	p->nr_sects = 0;
	devfs_unregister(p->de);
	dev = p->hd_driverfs_dev;
	p->hd_driverfs_dev = NULL;
	if (dev) {
		device_remove_file(dev, &dev_attr_type);
		device_remove_file(dev, &dev_attr_kdev);
		device_unregister(dev);	
	}
}

static void part_release(struct device *dev)
{
	kfree(dev);
}

void add_partition(struct gendisk *disk, int part, sector_t start, sector_t len)
{
	struct hd_struct *p = disk->part + part - 1;
	struct device *parent = disk->disk_dev.parent;
	struct device *dev;

	p->start_sect = start;
	p->nr_sects = len;
	devfs_register_partition(disk, part);
	if (!(disk->flags & GENHD_FL_DRIVERFS))
		return;
	dev = kmalloc(sizeof(struct device), GFP_KERNEL);
	if (!dev)
		return;
	memset(dev, 0, sizeof(struct device));
	if (parent)  {
		sprintf(dev->name, "%spart%d", parent->name, part);
		sprintf(dev->bus_id, "%s:p%d", parent->bus_id, part);
		dev->parent = parent;
		dev->bus = parent->bus;
	} else {
		sprintf(dev->name, "part%d", part);
		sprintf(dev->bus_id, "p%d", part);
	}
	dev->release = part_release;
	dev->driver_data =
		(void *)(long)__mkdev(disk->major, disk->first_minor+part);
	device_register(dev);
	device_create_file(dev, &dev_attr_type);
	device_create_file(dev, &dev_attr_kdev);
	p->hd_driverfs_dev = dev;
}

/* Not exported, helper to add_disk(). */
void register_disk(struct gendisk *disk)
{
	struct parsed_partitions *state;
	struct block_device *bdev;
	int j;

	if (disk->flags & GENHD_FL_CD)
		devfs_create_cdrom(disk);

	/* No minors to use for partitions */
	if (disk->minors == 1)
		return;

	/* No such device (e.g., media were just removed) */
	if (!get_capacity(disk))
		return;

	bdev = bdget(MKDEV(disk->major, disk->first_minor));
	if (blkdev_get(bdev, FMODE_READ, 0, BDEV_RAW) < 0)
		return;
	state = check_partition(disk, bdev);
	driverfs_create_partitions(disk);
	devfs_create_partitions(disk);
	if (state) {
		for (j = 1; j < state->limit; j++) {
			sector_t size = state->parts[j].size;
			sector_t from = state->parts[j].from;
			if (!size)
				continue;
			add_partition(disk, j, from, size);
#if CONFIG_BLK_DEV_MD
			if (!state->parts[j].flags)
				continue;
			md_autodetect_dev(bdev->bd_dev+j);
#endif
		}
		kfree(state);
	}
	blkdev_put(bdev, BDEV_RAW);
}

int rescan_partitions(struct gendisk *disk, struct block_device *bdev)
{
	kdev_t dev = to_kdev_t(bdev->bd_dev);
	struct parsed_partitions *state;
	int p, res;

	if (!bdev->bd_invalidated)
		return 0;
	if (bdev->bd_part_count)
		return -EBUSY;
	res = invalidate_device(dev, 1);
	if (res)
		return res;
	bdev->bd_invalidated = 0;
	for (p = 1; p < disk->minors; p++)
		delete_partition(disk, p);
	if (bdev->bd_op->revalidate)
		bdev->bd_op->revalidate(dev);
	if (!get_capacity(disk) || !(state = check_partition(disk, bdev)))
		return res;
	for (p = 1; p < state->limit; p++) {
		sector_t size = state->parts[p].size;
		sector_t from = state->parts[p].from;
		if (!size)
			continue;
		add_partition(disk, p, from, size);
#if CONFIG_BLK_DEV_MD
		if (!state->parts[j].flags)
			continue;
		md_autodetect_dev(bdev->bd_dev+p);
#endif
	}
	kfree(state);
	return res;
}

unsigned char *read_dev_sector(struct block_device *bdev, sector_t n, Sector *p)
{
	struct address_space *mapping = bdev->bd_inode->i_mapping;
	struct page *page;

	page = read_cache_page(mapping, (pgoff_t)(n >> (PAGE_CACHE_SHIFT-9)),
			(filler_t *)mapping->a_ops->readpage, NULL);
	if (!IS_ERR(page)) {
		wait_on_page_locked(page);
		if (!PageUptodate(page))
			goto fail;
		if (PageError(page))
			goto fail;
		p->v = page;
		return (unsigned char *)page_address(page) +  ((n & ((1 << (PAGE_CACHE_SHIFT - 9)) - 1)) << 9);
fail:
		page_cache_release(page);
	}
	p->v = NULL;
	return NULL;
}

void del_gendisk(struct gendisk *disk)
{
	int max_p = disk->minors;
	kdev_t devp;
	int p;

	/* invalidate stuff */
	for (p = max_p - 1; p > 0; p--) {
		devp = mk_kdev(disk->major,disk->first_minor + p);
		invalidate_device(devp, 1);
		delete_partition(disk, p);
	}
	devp = mk_kdev(disk->major,disk->first_minor);
	invalidate_device(devp, 1);
	disk->capacity = 0;
	disk->flags &= ~GENHD_FL_UP;
	unlink_gendisk(disk);
	driverfs_remove_partitions(disk);
	devfs_remove_partitions(disk);
}

struct dev_name {
	struct list_head list;
	dev_t dev;
	char namebuf[64];
	char *name;
};

static LIST_HEAD(device_names);

char *partition_name(dev_t dev)
{
	struct gendisk *hd;
	static char nomem [] = "<nomem>";
	struct dev_name *dname;
	struct list_head *tmp;
	int part;

	list_for_each(tmp, &device_names) {
		dname = list_entry(tmp, struct dev_name, list);
		if (dname->dev == dev)
			return dname->name;
	}

	dname = kmalloc(sizeof(*dname), GFP_KERNEL);

	if (!dname)
		return nomem;
	/*
	 * ok, add this new device name to the list
	 */
	hd = get_gendisk(dev, &part);
	dname->name = NULL;
	if (hd)
		dname->name = disk_name(hd, part, dname->namebuf);
	if (!dname->name) {
		sprintf(dname->namebuf, "[dev %s]", kdevname(to_kdev_t(dev)));
		dname->name = dname->namebuf;
	}

	dname->dev = dev;
	list_add(&dname->list, &device_names);

	return dname->name;
}
