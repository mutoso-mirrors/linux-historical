/*
 * $Id: mtdcore.c,v 1.13 2000/07/13 14:27:37 dwmw2 Exp $
 *
 * Core registration and callback routines for MTD 
 * drivers and users.
 *
 */

#ifdef MTD_DEBUG
#define DEBUGLVL debug
#endif

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/major.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <stdarg.h>
#include <linux/mtd/compatmac.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif

#include <linux/mtd/mtd.h>

#ifdef MTD_DEBUG
static int debug = MTD_DEBUG;
MODULE_PARM(debug, "i");
#endif

/* Init code required for 2.2 kernels */

#if LINUX_VERSION_CODE < 0x20300

#ifdef CONFIG_MTD_DOC1000
extern int init_doc1000(void);
#endif
#ifdef CONFIG_MTD_DOCPROBE
extern int init_doc(void);
#endif
#ifdef CONFIG_MTD_PHYSMAP
extern int init_physmap(void);
#endif
#ifdef CONFIG_MTD_RPXLITE
extern int init_rpxlite(void);
#endif
#ifdef CONFIG_MTD_OCTAGON
extern int init_octagon5066(void);
#endif
#ifdef CONFIG_MTD_PNC2000
extern int init_pnc2000(void);
#endif
#ifdef CONFIG_MTD_VMAX
extern int init_vmax301(void);
#endif
#ifdef CONFIG_MTD_MIXMEM
extern int init_mixmem(void);
#endif
#ifdef CONFIG_MTD_PMC551
extern int init_pmc551(void);
#endif
#ifdef CONFIG_MTD_NORA
extern int init_nora(void);
#endif
#ifdef CONFIG_FTL
extern int init_ftl(void);
#endif
#ifdef CONFIG_NFTL
extern int init_nftl(void);
#endif
#ifdef CONFIG_MTD_BLOCK
extern int init_mtdblock(void);
#endif
#ifdef CONFIG_MTD_CHAR
extern int init_mtdchar(void);
#endif

#endif /* LINUX_VERSION_CODE < 0x20300 */


static DECLARE_MUTEX(mtd_table_mutex);
static struct mtd_info *mtd_table[MAX_MTD_DEVICES];
static struct mtd_notifier *mtd_notifiers = NULL;

/**
 *	add_mtd_device - register an MTD device 
 *	@mtd: pointer to new MTD device info structure
 *
 *	Add a device to the list of MTD devices present in the system, and
 *	notify each currently active MTD 'user' of its arrival. Returns
 *	zero on success or 1 on failure, which currently will only happen
 *	if the number of present devices exceeds MAX_MTD_DEVICES (i.e. 16)
 */

int add_mtd_device(struct mtd_info *mtd)
{
	int i;

	down(&mtd_table_mutex);

	for (i=0; i< MAX_MTD_DEVICES; i++)
		if (!mtd_table[i])
		{
			struct mtd_notifier *not=mtd_notifiers;

			mtd_table[i] = mtd;
			DEBUG(0, "mtd: Giving out device %d to %s\n",i, mtd->name);
			while (not)
			{
				(*(not->add))(mtd);
				not = not->next;
			}
			up(&mtd_table_mutex);
			MOD_INC_USE_COUNT;
			return 0;
		}
	
	up(&mtd_table_mutex);
	return 1;
}

/**
 *	del_mtd_device - unregister an MTD device 
 *	@mtd: pointer to MTD device info structure
 *
 *	Remove a device from the list of MTD devices present in the system,
 *	and notify each currently active MTD 'user' of its departure.
 *	Returns zero on success or 1 on failure, which currently will happen
 *	if the requested device does not appear to be present in the list.
 */

int del_mtd_device (struct mtd_info *mtd)
{
	struct mtd_notifier *not=mtd_notifiers;
	int i;
	
	down(&mtd_table_mutex);

	for (i=0; i < MAX_MTD_DEVICES; i++)
	{
		if (mtd_table[i] == mtd)
		{
			while (not)
			{
				(*(not->remove))(mtd);
				not = not->next;
			}
			mtd_table[i] = NULL;
			up (&mtd_table_mutex);
			MOD_DEC_USE_COUNT;
			return 0;
		}
	}

	up(&mtd_table_mutex);
	return 1;
}

/**
 *	register_mtd_user - register a 'user' of MTD devices.
 *	@new: pointer to notifier info structure
 *
 *	Registers a pair of callbacks function to be called upon addition
 *	or removal of MTD devices. Causes the 'add' callback to be immediately
 *	invoked for each MTD device currently present in the system.
 */

void register_mtd_user (struct mtd_notifier *new)
{
	int i;

	down(&mtd_table_mutex);

	new->next = mtd_notifiers;
	mtd_notifiers = new;

 	MOD_INC_USE_COUNT;
	
	for (i=0; i< MAX_MTD_DEVICES; i++)
		if (mtd_table[i])
			new->add(mtd_table[i]);

	up(&mtd_table_mutex);
}

/**
 *	register_mtd_user - unregister a 'user' of MTD devices.
 *	@new: pointer to notifier info structure
 *
 *	Removes a callback function pair from the list of 'users' to be
 *	notified upon addition or removal of MTD devices. Causes the 
 *	'remove' callback to be immediately invoked for each MTD device
 *	currently present in the system.
 */

int unregister_mtd_user (struct mtd_notifier *old)
{
	struct mtd_notifier **prev = &mtd_notifiers;
	struct mtd_notifier *cur;
	int i;

	down(&mtd_table_mutex);

	while ((cur = *prev)) {
		if (cur == old) {
			*prev = cur->next;

			MOD_DEC_USE_COUNT;

			for (i=0; i< MAX_MTD_DEVICES; i++)
				if (mtd_table[i])
					old->remove(mtd_table[i]);
			
			up(&mtd_table_mutex);
			return 0;
		}
		prev = &cur->next;
	}
	up(&mtd_table_mutex);
	return 1;
}


/**
 *	__get_mtd_device - obtain a validated handle for an MTD device
 *	@mtd: last known address of the required MTD device
 *	@num: internal device number of the required MTD device
 *
 *	Given a number and NULL address, return the num'th entry in the device 
 *	table, if any.	Given an address and num == -1, search the device table
 *	for a device with that address and return if it's still present. Given
 *	both, return the num'th driver only if its address matches. Return NULL
 *	if not. get_mtd_device() increases the use count, but
 *	__get_mtd_device() doesn't - you should generally use get_mtd_device().
 */
	
struct mtd_info *__get_mtd_device(struct mtd_info *mtd, int num)
{
	struct mtd_info *ret = NULL;
	int i;

	down(&mtd_table_mutex);

	if (num == -1) {
		for (i=0; i< MAX_MTD_DEVICES; i++)
			if (mtd_table[i] == mtd)
				ret = mtd_table[i];
	} else if (num < MAX_MTD_DEVICES) {
		ret = mtd_table[num];
		if (mtd && mtd != ret)
			ret = NULL;
	}
	
	up(&mtd_table_mutex);
	return ret;
}

EXPORT_SYMBOL(add_mtd_device);
EXPORT_SYMBOL(del_mtd_device);
EXPORT_SYMBOL(__get_mtd_device);
EXPORT_SYMBOL(register_mtd_user);
EXPORT_SYMBOL(unregister_mtd_user);

/*====================================================================*/
/* Power management code */

#ifdef CONFIG_PM

#include <linux/pm.h>

static struct pm_dev *mtd_pm_dev = NULL;

static int mtd_pm_callback(struct pm_dev *dev, pm_request_t rqst, void *data)
{
	int ret = 0, i;

	if (down_trylock(&mtd_table_mutex))
		return -EAGAIN;
	if (rqst == PM_SUSPEND) {
		for (i = 0; ret == 0 && i < MAX_MTD_DEVICES; i++) {
			if (mtd_table[i] && mtd_table[i]->suspend)
				ret = mtd_table[i]->suspend(mtd_table[i]);
		}
	} else i = MAX_MTD_DEVICES-1;

	if (rqst == PM_RESUME || ret) {
		for ( ; i >= 0; i--) {
			if (mtd_table[i] && mtd_table[i]->resume)
				mtd_table[i]->resume(mtd_table[i]);
		}
	}
	up(&mtd_table_mutex);
	return ret;
}
#endif

/*====================================================================*/
/* Support for /proc/mtd */

#ifdef CONFIG_PROC_FS

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
static struct proc_dir_entry *proc_mtd;
#endif

static inline int mtd_proc_info (char *buf, int i)
{
	struct mtd_info *this = mtd_table[i];

	if (!this) 
		return 0;

	return sprintf(buf, "mtd%d: %8.8lx \"%s\"\n", i, this->size, 
		       this->name);
}

static int mtd_read_proc ( char *page, char **start, off_t off,int count
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
                       ,int *eof, void *data_unused
#else
                        ,int unused
#endif
			)
{
	int len = 0, l, i;
        off_t   begin = 0;
      
	down(&mtd_table_mutex);

        for (i=0; i< MAX_MTD_DEVICES; i++) {

                l = mtd_proc_info(page + len, i);
                len += l;
                if (len+begin > off+count)
                        goto done;
                if (len+begin < off) {
                        begin += len;
                        len = 0;
                }
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
        *eof = 1;
#endif

done:
	up(&mtd_table_mutex);
        if (off >= len+begin)
                return 0;
        *start = page + (begin-off);
        return ((count < begin+len-off) ? count : begin+len-off);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,2,0)
struct proc_dir_entry mtd_proc_entry = {
        0,                 /* low_ino: the inode -- dynamic */
        3, "mtd",     /* len of name and name */
        S_IFREG | S_IRUGO, /* mode */
        1, 0, 0,           /* nlinks, owner, group */
        0, NULL,           /* size - unused; operations -- use default */
        &mtd_read_proc,   /* function used to read data */
        /* nothing more */
    };
#endif

#endif /* CONFIG_PROC_FS */

/*====================================================================*/
/* Init code */

#if LINUX_VERSION_CODE < 0x20300

static inline  void init_others(void) 
{
	/* Shedloads of calls to init functions of all the
	 * other drivers and users of MTD, which we can
	 * ditch in 2.3 because of the sexy new way of 
	 * finding init routines.
	 */
#ifdef CONFIG_MTD_DOC1000
	init_doc1000();
#endif
#ifdef CONFIG_MTD_DOCPROBE
	init_doc(); /* This covers both the DiskOnChip 2000 
		     * and the DiskOnChip Millennium. 
		     * Theoretically all other DiskOnChip
		     * devices too. */
#endif
#ifdef CONFIG_MTD_PHYSMAP
	init_physmap();
#endif
#ifdef CONFIG_MTD_RPXLITE
	init_rpxlite();
#endif
#ifdef CONFIG_MTD_OCTAGON
	init_octagon5066();
#endif
#ifdef CONFIG_MTD_PNC2000
	init_pnc2000();
#endif
#ifdef CONFIG_MTD_VMAX
	init_vmax301();
#endif
#ifdef CONFIGF_MTD_MIXMEM
	init_mixmem();
#endif
#ifdef CONFIG_MTD_PMC551
	init_pmc551();
#endif
#ifdef CONFIG_MTD_NORA
	init_nora();
#endif
#ifdef CONFIG_MTD_MTDRAM
	init_mtdram();
#endif
#ifdef CONFIG_FTL
	init_ftl();
#endif
#ifdef CONFIG_NFTL
	init_nftl();
#endif
#ifdef CONFIG_MTD_BLOCK
	init_mtdblock();
#endif
#ifdef CONFIG_MTD_CHAR
	init_mtdchar();
#endif
}

#ifdef MODULE
#define init_mtd init_module
#define cleanup_mtd cleanup_module
#endif

#endif /* LINUX_VERSION_CODE < 0x20300 */

mod_init_t init_mtd(void)
{
	int i;
	DEBUG(1, "INIT_MTD:\n");	
	for (i=0; i<MAX_MTD_DEVICES; i++)
		mtd_table[i]=NULL;

#ifdef CONFIG_PROC_FS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
	if ((proc_mtd = create_proc_entry( "mtd", 0, 0 )))
	  proc_mtd->read_proc = mtd_read_proc;
#else
        proc_register_dynamic(&proc_root,&mtd_proc_entry);
#endif

#endif

#if LINUX_VERSION_CODE < 0x20300
	init_others();
#endif
#ifdef CONFIG_PM
	mtd_pm_dev = pm_register(PM_UNKNOWN_DEV, 0, mtd_pm_callback);
#endif
	return 0;
}

mod_exit_t cleanup_mtd(void)
{
	unregister_chrdev(MTD_CHAR_MAJOR, "mtd");
#ifdef CONFIG_PM
	if (mtd_pm_dev) {
		pm_unregister(mtd_pm_dev);
		mtd_pm_dev = NULL;
	}
#endif
#ifdef CONFIG_PROC_FS
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
        if (proc_mtd)
          remove_proc_entry( "mtd", 0);
#else
        proc_unregister(&proc_root,mtd_proc_entry.low_ino);
#endif
#endif
}
      
#if LINUX_VERSION_CODE > 0x20300
module_init(init_mtd);
module_exit(cleanup_mtd);
#endif


