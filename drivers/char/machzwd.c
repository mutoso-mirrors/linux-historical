/*
 *  MachZ ZF-Logic Watchdog Timer driver for Linux
 *  
 * 
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *  The author does NOT admit liability nor provide warranty for
 *  any of this software. This material is provided "AS-IS" in
 *  the hope that it may be useful for others.
 *
 *  Author: Fernando Fuganti <fuganti@conectiva.com.br>
 *
 *  Based on sbc60xxwdt.c by Jakob Oestergaard
 * 
 *
 *  We have two timers (wd#1, wd#2) driven by a 32 KHz clock with the 
 *  following periods:
 *      wd#1 - 2 seconds;
 *      wd#2 - 7.2 ms;
 *  After the expiration of wd#1, it can generate a NMI, SCI, SMI, or 
 *  a system RESET and it starts wd#2 that unconditionaly will RESET 
 *  the system when the counter reaches zero.
 *
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#include <linux/fcntl.h>
#include <linux/smp_lock.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>


/* ports */
#define ZF_IOBASE	0x218
#define INDEX		0x218
#define DATA_B		0x219
#define DATA_W		0x21A
#define DATA_D		0x21A

/* indexes */			/* size */
#define ZFL_VERSION	0x02	/* 16   */
#define CONTROL 	0x10	/* 16   */	
#define STATUS		0x12	/* 8    */
#define COUNTER_1	0x0C	/* 16   */
#define COUNTER_2	0x0E	/* 8    */
#define PULSE_LEN	0x0F	/* 8    */

/* controls */
#define ENABLE_WD1	0x0001
#define ENABLE_WD2	0x0002
#define RESET_WD1	0x0010
#define RESET_WD2	0x0020
#define GEN_SCI		0x0100
#define GEN_NMI		0x0200
#define GEN_SMI		0x0400
#define GEN_RESET	0x0800


/* utilities */

#define WD1	0
#define WD2	1

#define zf_writew(port, data)  { outb(port, INDEX); outw(data, DATA_W); }
#define zf_writeb(port, data)  { outb(port, INDEX); outb(data, DATA_B); }
#define zf_get_ZFL_version()   zf_readw(ZFL_VERSION)


static unsigned short zf_readw(unsigned char port)
{
	outb(port, INDEX);
	return inw(DATA_W);
}

static unsigned short zf_readb(unsigned char port)
{
	outb(port, INDEX);
	return inb(DATA_B);
}


MODULE_AUTHOR("Fernando Fuganti <fuganti@conectiva.com.br>");
MODULE_DESCRIPTION("MachZ ZF-Logic Watchdog driver");
MODULE_PARM(action, "i");
MODULE_PARM_DESC(action, "after watchdog resets, generate: 0 = RESET(*)  1 = SMI  2 = NMI  3 = SCI");

#define PFX "machzwd"

static struct watchdog_info zf_info = {
	options:		WDIOF_KEEPALIVEPING, 
	firmware_version:	1, 
	identity:		"ZF-Logic watchdog"
};


/*
 * action refers to action taken when watchdog resets
 * 0 = GEN_RESET
 * 1 = GEN_SMI
 * 2 = GEN_NMI
 * 3 = GEN_SCI
 * defaults to GEN_RESET (0)
 */
static int action = 0;
static int zf_action = GEN_RESET;
static int zf_is_open = 0;
static int zf_expect_close = 0;
static spinlock_t zf_lock;
static struct timer_list zf_timer;
static unsigned long next_heartbeat = 0;


/* timeout for user land heart beat (10 seconds) */
#define ZF_USER_TIMEO (HZ*10)

/* timeout for hardware watchdog (~500ms) */
#define ZF_HW_TIMEO (HZ/2)

/* number of ticks on WD#1 (driven by a 32KHz clock, 2s) */
#define ZF_CTIMEOUT 0xffff

#ifndef ZF_DEBUG
#	define dprintk(format, args...)
#else
#	define dprintk(format, args...) printk(KERN_DEBUG PFX ":" __FUNCTION__ ":%d: " format, __LINE__ , ## args)
#endif


/* STATUS register functions */

static inline unsigned char zf_get_status(void)
{
	return zf_readb(STATUS);
}

static inline void zf_set_status(unsigned char new)
{
	zf_writeb(STATUS, new);
}


/* CONTROL register functions */

static inline unsigned short zf_get_control(void)
{
	return zf_readw(CONTROL);
}

static inline void zf_set_control(unsigned short new)
{
	zf_writew(CONTROL, new);
}


/* WD#? counter functions */
/*
 *	Just get current counter value
 */

inline unsigned short zf_get_timer(unsigned char n)
{
	switch(n){
		case WD1:
			return zf_readw(COUNTER_1);
		case WD2:
			return zf_readb(COUNTER_2);
		default:
			return 0;
	}
}

/*
 *	Just set counter value
 */

static inline void zf_set_timer(unsigned short new, unsigned char n)
{
	switch(n){
		case WD1:
			zf_writew(COUNTER_1, new);
		case WD2:
			zf_writeb(COUNTER_2, new > 0xff ? 0xff : new);
		default:
			return;
	}
}

/*
 * stop hardware timer
 */
static void zf_timer_off(void)
{
	unsigned int ctrl_reg = 0;

	/* stop internal ping */
	del_timer(&zf_timer);

	/* stop watchdog timer */	
	ctrl_reg = zf_get_control();
	ctrl_reg |= (ENABLE_WD1|ENABLE_WD2);	/* disable wd1 and wd2 */
	ctrl_reg &= ~(ENABLE_WD1|ENABLE_WD2);
	zf_set_control(ctrl_reg);

	printk(PFX ": Watchdog timer is now disabled\n");
}


/*
 * start hardware timer 
 */
static void zf_timer_on(void)
{
	unsigned int ctrl_reg = 0;

	zf_writeb(PULSE_LEN, 0xff);

	zf_set_timer(ZF_CTIMEOUT, WD1);

	/* user land ping */
	next_heartbeat = jiffies + ZF_USER_TIMEO;

	/* start the timer for internal ping */
	zf_timer.expires = jiffies + ZF_HW_TIMEO;

	add_timer(&zf_timer);

	/* start watchdog timer */
	ctrl_reg = zf_get_control();
	ctrl_reg |= (ENABLE_WD1|zf_action);
	zf_set_control(ctrl_reg);

	printk(PFX ": Watchdog timer is now enabled\n");
}


static void zf_ping(unsigned long data)
{
	unsigned int ctrl_reg = 0;

	zf_writeb(COUNTER_2, 0xff);

	if(time_before(jiffies, next_heartbeat)){

		dprintk("time_before: %ld\n", next_heartbeat - jiffies);
		
		/* 
		 * reset event is activated by transition from 0 to 1 on
		 * RESET_WD1 bit and we assume that it is already zero...
		 */
		ctrl_reg = zf_get_control();    
		ctrl_reg |= RESET_WD1;		
		zf_set_control(ctrl_reg);	
		
		/* ...and nothing changes until here */
		ctrl_reg &= ~(RESET_WD1);
		zf_set_control(ctrl_reg);		

		zf_timer.expires = jiffies + ZF_HW_TIMEO;
		add_timer(&zf_timer);
	}else{
		printk(PFX ": I will reset your machine\n");
	}
}

static ssize_t zf_write(struct file *file, const char *buf, size_t count, 
								loff_t *ppos)
{
	/*  Can't seek (pwrite) on this device  */
	if (ppos != &file->f_pos)
		return -ESPIPE;

	/* See if we got the magic character */
	if(count){

/*
 * no need to check for close confirmation
 * no way to disable watchdog ;)
 */
#ifndef CONFIG_WATCHDOG_NOWAYOUT
		size_t ofs;

		/* 
		 * note: just in case someone wrote the magic character
		 * five months ago...
		 */
		zf_expect_close = 0;

		/* now scan */
		for(ofs = 0; ofs != count; ofs++){
			if(buf[ofs] == 'V'){
				zf_expect_close = 1;
				dprintk("zf_expect_close 1\n");
			}
		}
#endif
		/*
		 * Well, anyhow someone wrote to us,
		 * we should return that favour
		 */
		next_heartbeat = jiffies + ZF_USER_TIMEO;
		dprintk("user ping at %ld\n", jiffies);
		
		return 1;
	}

	return 0;
}

static ssize_t zf_read(struct file *file, char *buf, size_t count, 
							loff_t *ppos)
{
	return -EINVAL;
}



static int zf_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	int ret;
		
	switch(cmd){
		case WDIOC_GETSUPPORT:
			ret = copy_to_user((struct watchdog_info *)arg, 
						&zf_info, sizeof(zf_info));
			if(ret)
				return -EFAULT;
			break;
	  
		case WDIOC_GETSTATUS:
			ret = copy_to_user((int *)arg, &zf_is_open,
								sizeof(int));
			if(ret)
				return -EFAULT;
			break;

		case WDIOC_KEEPALIVE:
			zf_ping(0);
			break;

		default:
			return -ENOIOCTLCMD;
	}

	return 0;
}

static int zf_open(struct inode *inode, struct file *file)
{
	switch(MINOR(inode->i_rdev)){
		case WATCHDOG_MINOR:
			spin_lock(&zf_lock);
			if(zf_is_open){
				spin_unlock(&zf_lock);
				return -EBUSY;
			}

#ifdef CONFIG_WATCHDOG_NOWAYOUT
			MOD_INC_USE_COUNT;
#endif
			zf_is_open = 1;

			spin_unlock(&zf_lock);

			zf_timer_on();

			return 0;
		default:
			return -ENODEV;
	}
}

static int zf_close(struct inode *inode, struct file *file)
{
	if(MINOR(inode->i_rdev) == WATCHDOG_MINOR){

		if(zf_expect_close){
			zf_timer_off();
		} else {
			del_timer(&zf_timer);
			printk(PFX ": device file closed unexpectedly. Will not stop the WDT!\n");
		}
		
		spin_lock(&zf_lock);
		zf_is_open = 0;
		spin_unlock(&zf_lock);

		zf_expect_close = 0;
	}
	
	return 0;
}

/*
 * Notifier for system down
 */

static int zf_notify_sys(struct notifier_block *this, unsigned long code,
								void *unused)
{
	if(code == SYS_DOWN || code == SYS_HALT){
		zf_timer_off();		
	}
	
	return NOTIFY_DONE;
}




static struct file_operations zf_fops = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,3,34)
	owner:          THIS_MODULE,
#endif
	read:           zf_read,
	write:          zf_write,
	ioctl:          zf_ioctl,
	open:           zf_open,
	release:        zf_close,
};

static struct miscdevice zf_miscdev = {
	WATCHDOG_MINOR,
	"watchdog",
	&zf_fops
};
                                                                        

/*
 * The device needs to learn about soft shutdowns in order to
 * turn the timebomb registers off.
 */
static struct notifier_block zf_notifier = {
	zf_notify_sys,
	NULL,
	0
};

static void __init zf_show_action(int act)
{
	char *str[] = { "RESET", "SMI", "NMI", "SCI" };
	
	printk(PFX ": Watchdog using action = %s\n", str[act]);
}

int __init zf_init(void)
{
	int ret;
	
	printk(PFX ": MachZ ZF-Logic Watchdog driver initializing.\n");

	ret = zf_get_ZFL_version();
	printk("%#x\n", ret);
	if((!ret) || (ret != 0xffff)){
		printk(PFX ": no ZF-Logic found\n");
		return -ENODEV;
	}

	if((action <= 3) && (action >= 0)){
		zf_action = zf_action>>action;
	} else
		action = 0;
	
	zf_show_action(action);

	spin_lock_init(&zf_lock);
	
	ret = misc_register(&zf_miscdev);
	if (ret){
		printk(KERN_ERR "can't misc_register on minor=%d\n",
							WATCHDOG_MINOR);
		goto out;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,3)
	if(check_region(ZF_IOBASE, 3)){
#else
	if(!request_region(ZF_IOBASE, 3, "MachZ ZFL WDT")){
#endif

		printk(KERN_ERR "cannot reserve I/O ports at %d\n",
							ZF_IOBASE);
		ret = -EBUSY;
		goto no_region;
	}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,3)
	request_region(ZF_IOBASE, 3, "MachZ ZFL WDT");
#define __exit
#endif

	ret = register_reboot_notifier(&zf_notifier);
	if(ret){
		printk(KERN_ERR "can't register reboot notifier (err=%d)\n",
									ret);
		goto no_reboot;
	}
	
	zf_set_status(0);
	zf_set_control(0);

	/* this is the timer that will do the hard work */
	init_timer(&zf_timer);
	zf_timer.function = zf_ping;
	zf_timer.data = 0;
	
	return 0;

no_reboot:
	release_region(ZF_IOBASE, 3);
no_region:
	misc_deregister(&zf_miscdev);
out:
	return ret;
}

	
void __exit zf_exit(void)
{
	zf_timer_off();
	
	misc_deregister(&zf_miscdev);
	unregister_reboot_notifier(&zf_notifier);
	release_region(ZF_IOBASE, 3);
}

module_init(zf_init);
module_exit(zf_exit);
