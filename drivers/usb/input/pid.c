/*
 *  PID Force feedback support for hid devices.
 *
 *  Copyright (c) 2002 Rodrigo Damazio.
 *  Portions by Johann Deneux and Bjorn Augustson
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so by
 * e-mail - mail your message to <rdamazio@lsi.usp.br>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>
#include <linux/input.h>
#include <linux/usb.h>
#include "hid.h"
#include "pid.h"

#define DEBUG

MODULE_AUTHOR("Rodrigo Damazio <rdamazio@lsi.usp.br>");
MODULE_DESCRIPTION("USB PID(Physical Interface Device) Driver");
MODULE_LICENSE("GPL");

#define CHECK_OWNERSHIP(i, hid_pid)	\
	((i) < FF_EFFECTS_MAX && i >= 0 && \
	test_bit(FF_PID_FLAGS_USED, &hid_pid->effects[(i)].flags) && \
	(current->pid == 0 || \
	(hid_pid)->effects[(i)].owner == current->pid))

/* Called when a transfer is completed */
static void hid_pid_ctrl_out(struct urb *u)
{
#ifdef DEBUG
    printk("hid_pid_ctrl_out - Transfer Completed\n");
#endif
}

static void hid_pid_exit(struct hid_device* hid)
{
    struct hid_ff_pid *private = hid->ff_private;
    
    if (private->urbffout) {
	usb_unlink_urb(private->urbffout);
	usb_free_urb(private->urbffout);
    }
}

static int pid_upload_periodic(struct hid_ff_pid *pid, struct ff_effect *effect, int is_update) {
    
    printk("Requested periodic force upload\n");
    return 0;
}

static int pid_upload_constant(struct hid_ff_pid *pid, struct ff_effect *effect, int is_update) {
    printk("Requested constant force upload\n");
    return 0;
}

static int pid_upload_condition(struct hid_ff_pid *pid, struct ff_effect *effect, int is_update) {
    printk("Requested Condition force upload\n");
    return 0;
}

static int pid_upload_ramp(struct hid_ff_pid *pid, struct ff_effect *effect, int is_update) {
    printk("Request ramp force upload\n");
    return 0;
}

static int hid_pid_event(struct hid_device *hid, struct input_dev *input,
			  unsigned int type, unsigned int code, int value)
{
#ifdef DEBUG
    printk ("PID event received: type=%d,code=%d,value=%d.\n",type,code,value);
#endif

    if (type != EV_FF)
	return -1;

    

    return 0;
}

/* Lock must be held by caller */
static void hid_pid_ctrl_playback(struct hid_device *hid,
				   struct hid_pid_effect *effect, int play)
{
	if (play) {
		set_bit(FF_PID_FLAGS_PLAYING, &effect->flags);

	} else {
		clear_bit(FF_PID_FLAGS_PLAYING, &effect->flags);
	}
}


static int hid_pid_erase(struct input_dev *dev, int id)
{
	struct hid_device *hid = dev->private;
	struct hid_field* field;
	struct hid_report* report;
	struct hid_ff_pid *pid = hid->ff_private;
	unsigned long flags;
	unsigned wanted_report = HID_UP_PID | FF_PID_USAGE_BLOCK_FREE;  /*  PID Block Free Report */
	int ret;

	if (!CHECK_OWNERSHIP(id, pid)) return -EACCES;

	/* Find report */
	ret =  hid_find_report_by_usage(hid, wanted_report, &report, HID_OUTPUT_REPORT);
	if(!ret) {
		printk("Couldn't find report\n");
		return ret;
	}

	/* Find field */
	field = (struct hid_field *) kmalloc(sizeof(struct hid_field), GFP_KERNEL);
	ret = hid_set_field(field, ret, pid->effects[id].device_id);
	if(!ret) {
		printk("Couldn't set field\n");
		return ret;
	}

	hid_submit_report(hid, report, USB_DIR_OUT);

	spin_lock_irqsave(&pid->lock, flags);
	hid_pid_ctrl_playback(hid, pid->effects + id, 0);
	pid->effects[id].flags = 0;
	spin_unlock_irqrestore(&pid->lock, flags);

	return ret;
}

/* Erase all effects this process owns */
static int hid_pid_flush(struct input_dev *dev, struct file *file)
{
	struct hid_device *hid = dev->private;
	struct hid_ff_pid *pid = hid->ff_private;
	int i;

	/*NOTE: no need to lock here. The only times EFFECT_USED is
	  modified is when effects are uploaded or when an effect is
	  erased. But a process cannot close its dev/input/eventX fd
	  and perform ioctls on the same fd all at the same time */
	for (i=0; i<dev->ff_effects_max; ++i)
		if ( current->pid == pid->effects[i].owner
		     && test_bit(FF_PID_FLAGS_USED, &pid->effects[i].flags))
			if (hid_pid_erase(dev, i))
				warn("erase effect %d failed", i);

	return 0;
}


static int hid_pid_upload_effect(struct input_dev *dev,
				  struct ff_effect *effect)
{
	struct hid_ff_pid* pid_private  = (struct hid_ff_pid*)(dev->private);
	int ret;
	int is_update;
	int flags=0;

#ifdef DEBUG
        printk("Upload effect called: effect_type=%x\n",effect->type);
#endif
	/* Check this effect type is supported by this device */
	if (!test_bit(effect->type, dev->ffbit)) {
#ifdef DEBUG
		printk("Invalid kind of effect requested.\n");
#endif
		return -EINVAL;
	}

	/*
	 * If we want to create a new effect, get a free id
	 */
	if (effect->id == -1) {
		int id=0;

		// Spinlock so we don`t get a race condition when choosing IDs
		spin_lock_irqsave(&pid_private->lock,flags);

		while(id < FF_EFFECTS_MAX)
			if (!test_and_set_bit(FF_PID_FLAGS_USED, &pid_private->effects[id++].flags)) 
			    break;

		if ( id == FF_EFFECTS_MAX) {
// TEMP - We need to get ff_effects_max correctly first:  || id >= dev->ff_effects_max) {
#ifdef DEBUG
			printk("Not enough device memory\n");
#endif
			return -ENOMEM;
		}

		effect->id = id;
#ifdef DEBUG
		printk("Effect ID is %d\n.",id);
#endif
		pid_private->effects[id].owner = current->pid;
		pid_private->effects[id].flags = (1<<FF_PID_FLAGS_USED);
		spin_unlock_irqrestore(&pid_private->lock,flags);

		is_update = FF_PID_FALSE;
	}
	else {
		/* We want to update an effect */
		if (!CHECK_OWNERSHIP(effect->id, pid_private)) return -EACCES;

		/* Parameter type cannot be updated */
		if (effect->type != pid_private->effects[effect->id].effect.type)
			return -EINVAL;

		/* Check the effect is not already being updated */
		if (test_bit(FF_PID_FLAGS_UPDATING, &pid_private->effects[effect->id].flags)) {
			return -EAGAIN;
		}

		is_update = FF_PID_TRUE;
	}

	/*
	 * Upload the effect
	 */
	switch (effect->type) {
		case FF_PERIODIC:
			ret = pid_upload_periodic(pid_private, effect, is_update);
			break;

		case FF_CONSTANT:
			ret = pid_upload_constant(pid_private, effect, is_update);
			break;

		case FF_SPRING:
		case FF_FRICTION:
		case FF_DAMPER:
		case FF_INERTIA:
			ret = pid_upload_condition(pid_private, effect, is_update);
			break;

		case FF_RAMP:
			ret = pid_upload_ramp(pid_private, effect, is_update);
			break;

		default:
#ifdef DEBUG
			printk("Invalid type of effect requested - %x.\n", effect->type);
#endif
			return -EINVAL;
	}
	/* If a packet was sent, forbid new updates until we are notified
	 * that the packet was updated
	 */
	if (ret == 0)
		set_bit(FF_PID_FLAGS_UPDATING, &pid_private->effects[effect->id].flags);
	pid_private->effects[effect->id].effect = *effect;
	return ret;
}

int hid_pid_init(struct hid_device *hid)
{
    struct hid_ff_pid *private;
    
    private = hid->ff_private = kmalloc(sizeof(struct hid_ff_pid), GFP_KERNEL);
    if (!private) return -1;
    
    memset(private,0,sizeof(struct hid_ff_pid));

    hid->ff_private = private; /* 'cause memset can move the block away */

    private->hid = hid;
    
    hid->ff_exit = hid_pid_exit;
    hid->ff_event = hid_pid_event;
    
    /* Open output URB */
    if (!(private->urbffout = usb_alloc_urb(0, GFP_KERNEL))) {
	kfree(private);
	return -1;
    }

    usb_fill_control_urb(private->urbffout, hid->dev,0,(void *) &private->ffcr,private->ctrl_buffer,8,hid_pid_ctrl_out,hid);
    hid->input.upload_effect = hid_pid_upload_effect;
    hid->input.flush = hid_pid_flush;
    hid->input.ff_effects_max = 8;  // A random default
    set_bit(EV_FF, hid->input.evbit);
    set_bit(EV_FF_STATUS, hid->input.evbit);

    spin_lock_init(&private->lock);

    printk(KERN_INFO "Force feedback driver for PID devices by Rodrigo Damazio <rdamazio@lsi.usp.br>.\n");
    
    return 0;    
}

static int __init hid_pid_modinit(void)
{
    return 0;
}

static void __exit hid_pid_modexit(void)
{

}

module_init(hid_pid_modinit);
module_exit(hid_pid_modexit);


