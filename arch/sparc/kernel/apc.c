/* $Id$
 *
 * apc - Driver implementation for power management functions
 * of Aurora Personality Chip (APC) on SPARCstation-4/5 and
 * derivatives.
 *
 * Copyright (c) 2002 Eric Brower (ebrower@usa.net)
 */

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/pm.h>

#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/oplib.h>
#include <asm/uaccess.h>
#include <asm/auxio.h>
#include <asm/apc.h>

/* Debugging
 * 
 * #define APC_DEBUG_LED
 * #define APC_NO_IDLE
 */

#define APC_MINOR	MISC_DYNAMIC_MINOR
#define APC_OBPNAME	"power-management"
#define APC_DEVNAME "apc"

volatile static u8 *regs; 
static int apc_regsize;

#define apc_readb(offs)			(sbus_readb(regs+offs))
#define apc_writeb(val, offs) 	(sbus_writeb(val, regs+offs))

/* 
 * CPU idle callback function
 * See .../arch/sparc/kernel/process.c
 */
void apc_swift_idle(void)
{
#ifdef APC_DEBUG_LED
	set_auxio(0x00, AUXIO_LED); 
#endif

	apc_writeb(apc_readb(APC_IDLE_REG) | APC_IDLE_ON, APC_IDLE_REG);

#ifdef APC_DEBUG_LED
	set_auxio(AUXIO_LED, 0x00); 
#endif
} 

static inline void apc_free(void)
{
	sbus_iounmap((unsigned long)regs, apc_regsize);
}

static int apc_open(struct inode *inode, struct file *f)
{
	return 0;
}

static int apc_release(struct inode *inode, struct file *f)
{
	return 0;
}

static int apc_ioctl(struct inode *inode, struct file *f, 
		     unsigned int cmd, unsigned long arg)
{
	__u8 inarg;

	switch (cmd) {
		case APCIOCGFANCTL:
			if(put_user(apc_readb(APC_FANCTL_REG) & APC_REGMASK, (__u8*) arg)) {
				return -EFAULT;
			}
			break;
		case APCIOCGCPWR:
			if(put_user(apc_readb(APC_CPOWER_REG) & APC_REGMASK, (__u8*) arg)) {
				return -EFAULT;
			}
			break;
		case APCIOCGBPORT:
			if(put_user(apc_readb(APC_BPORT_REG) & APC_BPMASK, (__u8*) arg)) {
				return -EFAULT;
			}
			break;

		case APCIOCSFANCTL:
			if(get_user(inarg, (__u8*) arg)) {
				return -EFAULT;
			}
			apc_writeb(inarg & APC_REGMASK, APC_FANCTL_REG);
			break;
		case APCIOCSCPWR:
			if(get_user(inarg, (__u8*) arg)) {
				return -EFAULT;
			}
			apc_writeb(inarg & APC_REGMASK, APC_CPOWER_REG);
			break;
		case APCIOCSBPORT:
			if(get_user(inarg, (__u8*) arg)) {
				return -EFAULT;
			}
			apc_writeb(inarg & APC_BPMASK, APC_BPORT_REG);
			break;
		default:
			return -EINVAL;
	};

	return 0;
}

static struct file_operations apc_fops = {
	ioctl:		apc_ioctl,
	open:		apc_open,
	release:	apc_release,
};

static struct miscdevice apc_miscdev = { APC_MINOR, APC_DEVNAME, &apc_fops };

static int __init apc_probe(void)
{
	struct sbus_bus *sbus = NULL;
	struct sbus_dev *sdev = NULL;
	int iTmp = 0;
	for_each_sbus(sbus) {
		for_each_sbusdev(sdev, sbus) {
			if (!strcmp(sdev->prom_name, APC_OBPNAME)) {
				goto sbus_done;
			}
		}
	}

sbus_done:
	if (!sdev) {
		return -ENODEV;
	}

	apc_regsize = sdev->reg_addrs[0].reg_size;
	regs = (u8*) sbus_ioremap(&sdev->resource[0], 0, 
				   apc_regsize, APC_OBPNAME);
	if(NULL == regs) {
		printk(KERN_ERR "%s: unable to map registers\n", APC_DEVNAME);
		return -ENODEV;
	}

	iTmp = misc_register(&apc_miscdev);
	if (iTmp != 0) {
		printk(KERN_ERR "%s: unable to register device\n", APC_DEVNAME);
		apc_free();
		return -ENODEV;
	}

#ifndef APC_NO_IDLE	
	/* Assign power management IDLE handler */
	pm_idle = apc_swift_idle;	
#endif

	printk(KERN_INFO "%s: power management initialized\n", APC_DEVNAME);
	return 0;
}

/* This driver is not critical to the boot process
 * and is easiest to ioremap when SBus is already
 * initialized, so we install ourselves thusly:
 */
__initcall(apc_probe);
