/*
 *	SoftDog	0.06:	A Software Watchdog Device
 *
 *	(c) Copyright 1996 Alan Cox <alan@redhat.com>, All Rights Reserved.
 *				http://www.redhat.com
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *	
 *	Neither Alan Cox nor CymruNet Ltd. admit liability nor provide 
 *	warranty for any of this software. This material is provided 
 *	"AS-IS" and at no charge.	
 *
 *	(c) Copyright 1995    Alan Cox <alan@lxorguk.ukuu.org.uk>
 *
 *	Software only watchdog driver. Unlike its big brother the WDT501P
 *	driver this won't always recover a failed machine.
 *
 *  03/96: Angelo Haritsis <ah@doc.ic.ac.uk> :
 *	Modularised.
 *	Added soft_margin; use upon insmod to change the timer delay.
 *	NB: uses same minor as wdt (WATCHDOG_MINOR); we could use separate
 *	    minors.
 *
 *  19980911 Alan Cox
 *	Made SMP safe for 2.3.x
 *
 *  20011214 Matt Domsch <Matt_Domsch@dell.com>
 *      Added nowayout module option to override CONFIG_WATCHDOG_NOWAYOUT
 *      Didn't add timeout option, as soft_margin option already exists.
 */
 
#include <linux/module.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/reboot.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <asm/uaccess.h>

#define TIMER_MARGIN	60		/* (secs) Default is 1 minute */

static int soft_margin = TIMER_MARGIN;	/* in seconds */

MODULE_PARM(soft_margin,"i");

#ifdef CONFIG_WATCHDOG_NOWAYOUT
static int nowayout = 1;
#else
static int nowayout = 0;
#endif

MODULE_PARM(nowayout,"i");
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default=CONFIG_WATCHDOG_NOWAYOUT)");
MODULE_LICENSE("GPL");

/*
 *	Our timer
 */
 
static void watchdog_fire(unsigned long);

static struct timer_list watchdog_ticktock =
		TIMER_INITIALIZER(watchdog_fire, 0, 0);
static int timer_alive;


/*
 *	If the timer expires..
 */
 
static void watchdog_fire(unsigned long data)
{
#ifdef ONLY_TESTING
		printk(KERN_CRIT "SOFTDOG: Would Reboot.\n");
#else
	printk(KERN_CRIT "SOFTDOG: Initiating system reboot.\n");
	machine_restart(NULL);
	printk("WATCHDOG: Reboot didn't ?????\n");
#endif
}

/*
 *	Allow only one person to hold it open
 */
 
static int softdog_open(struct inode *inode, struct file *file)
{
	if(timer_alive)
		return -EBUSY;
	if (nowayout) {
		MOD_INC_USE_COUNT;
	}
	/*
	 *	Activate timer
	 */
	mod_timer(&watchdog_ticktock, jiffies+(soft_margin*HZ));
	timer_alive=1;
	return 0;
}

static int softdog_release(struct inode *inode, struct file *file)
{
	/*
	 *	Shut off the timer.
	 * 	Lock it in if it's a module and we set nowayout
	 */
	if(!nowayout) {
		del_timer(&watchdog_ticktock);
	}
	timer_alive=0;
	return 0;
}

static ssize_t softdog_write(struct file *file, const char *data, size_t len, loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	/*
	 *	Refresh the timer.
	 */
	if(len) {
		mod_timer(&watchdog_ticktock, jiffies+(soft_margin*HZ));
		return 1;
	}
	return 0;
}

static int softdog_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	static struct watchdog_info ident = {
		identity: "Software Watchdog",
	};
	switch (cmd) {
		default:
			return -ENOTTY;
		case WDIOC_GETSUPPORT:
			if(copy_to_user((struct watchdog_info *)arg, &ident, sizeof(ident)))
				return -EFAULT;
			return 0;
		case WDIOC_GETSTATUS:
		case WDIOC_GETBOOTSTATUS:
			return put_user(0,(int *)arg);
		case WDIOC_KEEPALIVE:
			mod_timer(&watchdog_ticktock, jiffies+(soft_margin*HZ));
			return 0;
	}
}

static struct file_operations softdog_fops = {
	owner:		THIS_MODULE,
	write:		softdog_write,
	ioctl:		softdog_ioctl,
	open:		softdog_open,
	release:	softdog_release,
};

static struct miscdevice softdog_miscdev = {
	minor:		WATCHDOG_MINOR,
	name:		"watchdog",
	fops:		&softdog_fops,
};

static char banner[] __initdata = KERN_INFO "Software Watchdog Timer: 0.06, soft_margin: %d sec, nowayout: %d\n";

static int __init watchdog_init(void)
{
	int ret;

	ret = misc_register(&softdog_miscdev);

	if (ret)
		return ret;

	printk(banner, soft_margin, nowayout);

	return 0;
}

static void __exit watchdog_exit(void)
{
	misc_deregister(&softdog_miscdev);
}

module_init(watchdog_init);
module_exit(watchdog_exit);
