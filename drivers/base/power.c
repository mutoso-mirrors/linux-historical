/*
 * power.c - power management functions for the device tree.
 * 
 * Copyright (c) 2002 Patrick Mochel
 *		 2002 Open Source Development Lab
 * 
 *  Kai Germaschewski contributed to the list walking routines.
 *
 */

#define DEBUG 0

#include <linux/device.h>
#include <linux/module.h>
#include <asm/semaphore.h>
#include "base.h"

#define to_dev(node) container_of(node,struct device,g_list)

/**
 * device_suspend - suspend/remove all devices on the device ree
 * @state:	state we're entering
 * @level:	what stage of the suspend process we're at
 *    (emb: it seems that these two arguments are described backwards of what
 *          they actually mean .. is this correct?)
 *
 * The entries in the global device list are inserted such that they're in a
 * depth-first ordering.  So, simply interate over the list, and call the 
 * driver's suspend or remove callback for each device.
 */
int device_suspend(u32 state, u32 level)
{
	struct list_head * node;
	int error = 0;

	printk(KERN_EMERG "Suspending devices\n");

	down(&device_sem);
	list_for_each(node,&global_device_list) {
		struct device * dev = to_dev(node);
		if (device_present(dev) && dev->driver && dev->driver->suspend) {
			pr_debug("suspending device %s\n",dev->name);
			error = dev->driver->suspend(dev,state,level);
			if (error)
				printk(KERN_ERR "%s: suspend returned %d\n",dev->name,error);
		}
	}
	up(&device_sem);
	return error;
}

/**
 * device_resume - resume all the devices in the system
 * @level:	stage of resume process we're at 
 * 
 * Similar to device_suspend above, though we want to do a breadth-first
 * walk of the tree to make sure we wake up parents before children.
 * So, we iterate over the list backward. 
 */
void device_resume(u32 level)
{
	struct list_head * node;

	down(&device_sem);
	list_for_each_prev(node,&global_device_list) {
		struct device * dev = to_dev(node);
		if (device_present(dev) && dev->driver && dev->driver->resume) {
			pr_debug("resuming device %s\n",dev->name);
			dev->driver->resume(dev,level);
		}
	}
	up(&device_sem);

	printk(KERN_EMERG "Devices Resumed\n");
}

/**
 * device_shutdown - call ->remove() on each device to shutdown. 
 */
void device_shutdown(void)
{
	struct list_head * entry;
	
	printk(KERN_EMERG "Shutting down devices\n");

	down(&device_sem);
	list_for_each(entry,&global_device_list) {
		struct device * dev = to_dev(entry);
		if (device_present(dev) && dev->driver && dev->driver->shutdown) {
			pr_debug("shutting down %s\n",dev->name);
			dev->driver->shutdown(dev);
		}
	}
	up(&device_sem);
}

EXPORT_SYMBOL(device_suspend);
EXPORT_SYMBOL(device_resume);
EXPORT_SYMBOL(device_shutdown);
