/* -*- linux-c -*-
 * USB ViCAM driver
 *
 * Copyright (c) 2001 Christopher L Cheney (ccheney@cheney.cx)
 * Copyright (c) 2001 Pavel Machek (pavel@suse.cz) sponsored by SuSE
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License as
 *      published by the Free Software Foundation; either version 2 of
 *      the License, or (at your option) any later version.
 *
 * This driver is for the Vista Imaging ViCAM and 3Com HomeConnect USB
 *
 * Thanks to Greg Kroah-Hartman for the USB Skeleton driver
 *
 * TODO:
 *	- find out the ids for the Vista Imaging ViCAM
 *
 * History:
 *
 * 2001_07_07 - 0.1 - christopher: first version
 * 2001_08_28 - 0.2 - pavel: messed it up, but for some fun, try 
 			while true; do dd if=/dev/video of=/dev/fb0 bs=$[0x1e480] count=1 2> /dev/null; done
		      yep, moving pictures.
 * 2001_08_29 - 0.3 - pavel: played a little bit more. Experimental mmap support. For some fun,
 			get gqcam-0.9, compile it and run. Better than dd ;-).
 * 2001_08_29 - 0.4 - pavel: added shutter speed control (not much functional)
 			kill update_params if it does not seem to work for you.
 * 2001_08_30 - 0.5 - pavel: fixed stupid bug with update_params & vicam_bulk

 *
 * FIXME: It crashes on rmmod with camera plugged.
 */
#define DEBUG 1

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/poll.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/fcntl.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/smp_lock.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/usb.h>

#include <asm/io.h>
#include <linux/wrapper.h>
#include <linux/vmalloc.h>

#include <linux/videodev.h>

#include "vicam.h"
#include "vicamurbs.h"

/* Version Information */
#define DRIVER_VERSION "v0"
#define DRIVER_AUTHOR "Christopher L Cheney <ccheney@cheney.cx>, Pavel Machek <pavel@suse.cz>"
#define DRIVER_DESC "USB ViCAM Driver"

/* Define these values to match your device */
#define USB_VICAM_VENDOR_ID	0x04C1
#define USB_VICAM_PRODUCT_ID	0x009D

/* table of devices that work with this driver */
static struct usb_device_id vicam_table [] = {
	{ USB_DEVICE(USB_VICAM_VENDOR_ID, USB_VICAM_PRODUCT_ID) },
	{ }					/* Terminating entry */
};

MODULE_DEVICE_TABLE (usb, vicam_table);

static int video_nr = -1; 		/* next avail video device */
static struct usb_driver vicam_driver;

static char *buf, *buf2;
static volatile int change_pending = 0; 

static int vicam_parameters(struct usb_vicam *vicam);

/******************************************************************************
 *
 *  Memory management functions
 *
 *  Taken from bttv-drivers.c 2.4.7-pre3
 *
 ******************************************************************************/

/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the area.
 */
static inline unsigned long kvirt_to_pa(unsigned long adr)
{
	unsigned long kva, ret;

	kva = (unsigned long) page_address(vmalloc_to_page((void *)adr));
	kva |= adr & (PAGE_SIZE-1); /* restore the offset */
	ret = __pa(kva);
	return ret;
}

static void * rvmalloc(unsigned long size)
{
	void * mem;
	unsigned long adr;

	size=PAGE_ALIGN(size);
	mem=vmalloc_32(size);
	if (mem)
	{
		memset(mem, 0, size); /* Clear the ram out, no junk to the user */
		adr=(unsigned long) mem;
		while (size > 0)
		{
			mem_map_reserve(vmalloc_to_page((void *)adr));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
	}
	return mem;
}

static void rvfree(void * mem, unsigned long size)
{
	unsigned long adr;

	if (mem)
	{
		adr=(unsigned long) mem;
		while ((long) size > 0)
		{
			mem_map_unreserve(vmalloc_to_page((void *)adr));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
}

/******************************************************************************
 *
 *  Foo Bar
 *
 ******************************************************************************/

/**
 *	usb_vicam_debug_data
 */
static inline void usb_vicam_debug_data (const char *function, int size, const unsigned char *data)
{
	int i;

	if (!debug)
		return;

	printk (KERN_DEBUG __FILE__": %s - length = %d, data = ",
		function, size);
	for (i = 0; i < size; ++i) {
		printk ("%.2x ", data[i]);
	}
	printk ("\n");
}

/*****************************************************************************
 *
 *  Send command to vicam
 *
 *****************************************************************************/

static int vicam_sndctrl(int set, struct usb_vicam *vicam, unsigned short req,
	unsigned short value, unsigned char *cp, int size)
{
	int ret;
	unsigned char *transfer_buffer = kmalloc (size, GFP_KERNEL);

	/* Needs to return data I think, works for sending though */
	memcpy(transfer_buffer, cp, size);
	
	ret = usb_control_msg ( vicam->udev, set ? usb_sndctrlpipe(vicam->udev, 0) : usb_rcvctrlpipe(vicam->udev, 0), req, (set ? USB_DIR_OUT : USB_DIR_IN) | USB_TYPE_VENDOR | USB_RECIP_DEVICE, value, 0, transfer_buffer, size, HZ);

	kfree(transfer_buffer);
	if (ret)
		printk("vicam: error: %d\n", ret);
	mdelay(100);
	return ret;
}


/*****************************************************************************
 *
 *  Video4Linux Helpers
 * 
 *****************************************************************************/

static int vicam_get_capability(struct usb_vicam *vicam, struct video_capability *b)
{
	dbg("vicam_get_capability");

	strcpy(b->name, vicam->camera_name);
	b->type = VID_TYPE_CAPTURE | VID_TYPE_MONOCHROME;
	b->channels = 1;
	b->audios = 0;

	b->maxwidth = vicam->width[vicam->sizes-1];
	b->maxheight = vicam->height[vicam->sizes-1];
	b->minwidth = vicam->width[0];
	b->minheight = vicam->height[0];

	return 0;
}
		
static int vicam_get_channel(struct usb_vicam *vicam, struct video_channel *v)
{
	dbg("vicam_get_channel");

	if (v->channel != 0)
		return -EINVAL;
 
	v->flags = 0;
	v->tuners = 0;
	v->type = VIDEO_TYPE_CAMERA;
	strcpy(v->name, "Camera");

	return 0;
} 
		
static int vicam_set_channel(struct usb_vicam *vicam, struct video_channel *v)
{
	dbg("vicam_set_channel");

	if (v->channel != 0)
		return -EINVAL;
	
	return 0;
}
		
static int vicam_get_mmapbuffer(struct usb_vicam *vicam, struct video_mbuf *vm)
{
	int i;

	dbg("vicam_get_mmapbuffer");

	memset(vm, 0, sizeof(vm));
	vm->size = VICAM_NUMFRAMES * vicam->maxframesize;
	vm->frames = VICAM_NUMFRAMES;

	for (i=0; i<VICAM_NUMFRAMES; i++)
		vm->offsets[i] = vicam->maxframesize * i;

	return 0;
}

static int vicam_get_picture(struct usb_vicam *vicam, struct video_picture *p)
{
	dbg("vicam_get_picture");

	/* This is probably where that weird 0x56 call goes */
	p->brightness = vicam->win.brightness;
	p->hue = vicam->win.hue;
	p->colour = vicam->win.colour;
	p->contrast = vicam->win.contrast;
	p->whiteness = vicam->win.whiteness;
	p->depth = vicam->win.depth;
	p->palette = vicam->win.palette;

	return 0;
}

static void synchronize(struct usb_vicam *vicam)
{
	DECLARE_WAITQUEUE(wait, current);
	change_pending = 1;
	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&vicam->wait, &wait);
	if (change_pending)
		schedule();
	remove_wait_queue(&vicam->wait, &wait);
	set_current_state(TASK_RUNNING);
	vicam_sndctrl(1, vicam, VICAM_REQ_CAMERA_POWER, 0x00, NULL, 0);
	mdelay(10);
	vicam_sndctrl(1, vicam, VICAM_REQ_LED_CONTROL, 0x00, NULL, 0);
	mdelay(10);
}

static void params_changed(struct usb_vicam *vicam)
{
#if 1
	synchronize(vicam);
	mdelay(10);
	vicam_parameters(vicam);
	printk(KERN_DEBUG "Submiting urb: %d\n", usb_submit_urb(vicam->readurb, GFP_KERNEL));
#endif
}

static int vicam_set_picture(struct usb_vicam *vicam, struct video_picture *p)
{
	int changed = 0;
	info("vicam_set_picture (%d)", p->brightness);


#define SET(x) \
	if (vicam->win.x != p->x) \
		vicam->win.x = p->x, changed = 1;
	SET(brightness);
	SET(hue);
	SET(colour);
	SET(contrast);
	SET(whiteness);
	SET(depth);
	SET(palette);
	if (changed)
		params_changed(vicam);

	return 0;
	/* Investigate what should be done maybe 0x56 type call */
	if (p->depth != 8) return 1;
	if (p->palette != VIDEO_PALETTE_GREY) return 1;

	return 0;
}

/* FIXME - vicam_sync_frame - important */
static int vicam_sync_frame(struct usb_vicam *vicam, int frame)
{
	dbg("vicam_sync_frame");

	if(frame <0 || frame >= VICAM_NUMFRAMES)
		return -EINVAL;

	/* Probably need to handle various cases */
/*	ret=vicam_newframe(vicam, frame);
	vicam->frame[frame].grabstate=FRAME_UNUSED;
*/
	return 0;
}
	
static int vicam_get_window(struct usb_vicam *vicam, struct video_window *vw)
{
	dbg("vicam_get_window");

	vw->x = 0;
	vw->y = 0;
	vw->chromakey = 0;
	vw->flags = 0;
	vw->clipcount = 0;
	vw->width = vicam->win.width;
	vw->height = vicam->win.height;

	return 0;
}

static int vicam_set_window(struct usb_vicam *vicam, struct video_window *vw)
{
	info("vicam_set_window");
		
	if (vw->flags)
		return -EINVAL;
	if (vw->clipcount)
		return -EINVAL;

	if (vicam->win.width == vw->width && vicam->win.height == vw->height)
		return 0;

	/* Pick largest mode that is smaller than specified res */
	/* If specified res is too small reject                 */

	/* Add urb send to device... */

	vicam->win.width = vw->width;
	vicam->win.height = vw->height;
	params_changed(vicam);

	return 0;
}

/* FIXME - vicam_mmap_capture - important */
static int vicam_mmap_capture(struct usb_vicam *vicam, struct video_mmap *vm)
{
	dbg("vicam_mmap_capture");

	/* usbvideo.c looks good for using here */

	/* 
	if (vm->frame >= VICAM_NUMFRAMES)
		return -EINVAL;
	if (vicam->frame[vm->frame].grabstate != FRAME_UNUSED)
		return -EBUSY;
	vicam->frame[vm->frame].grabstate=FRAME_READY;
	*/

	/* No need to vicam_set_window here according to Alan */

	/*
	if (!vicam->streaming)
		vicam_start_stream(vicam);
	*/

	/* set frame as ready */

	return 0;
}

/*****************************************************************************
 *
 *  Video4Linux
 * 
 *****************************************************************************/

static int vicam_v4l_open(struct inode *inode, struct file *file)
{
	struct video_device *vdev = video_devdata(file);
	struct usb_vicam *vicam = (struct usb_vicam *)vdev;
	int err = 0;
	
	dbg("vicam_v4l_open");
	down(&vicam->sem);

	vicam->fbuf = rvmalloc(vicam->maxframesize * VICAM_NUMFRAMES);
	if (vicam->open_count) {
		err = -EBUSY;
	} else if (!vicam->fbuf) {
		err =- ENOMEM;
	} else {
#ifdef BLINKING
		vicam_sndctrl(1, vicam, VICAM_REQ_CAMERA_POWER, 0x01, NULL, 0);
		info ("led on");
		vicam_sndctrl(1, vicam, VICAM_REQ_LED_CONTROL, 0x01, NULL, 0);
#endif
		vicam->open_count++;
		file->private_data = vdev;
	}

	up(&vicam->sem);
	return err;
}

static int vicam_v4l_close(struct inode *inode, struct file *file)
{
	struct video_device *vdev = file->private_data;
	struct usb_vicam *vicam = (struct usb_vicam *)vdev;

	dbg("vicam_v4l_close");
	
	down(&vicam->sem);

#ifdef BLINKING
	info ("led off");
	vicam_sndctrl(1, vicam, VICAM_REQ_LED_CONTROL, 0x00, NULL, 0);
//	vicam_sndctrl(1, vicam, VICAM_REQ_CAMERA_POWER, 0x00, NULL, 0); Leave it on
#endif

	rvfree(vicam->fbuf, vicam->maxframesize * VICAM_NUMFRAMES);
	vicam->fbuf = 0;
	vicam->open_count=0;
	file->private_data = NULL;

	up(&vicam->sem);
	/* Why does se401.c have a usbdevice check here? */
	/* If device is unplugged while open, I guess we only may unregister now */
	return 0;
}

static int vicam_v4l_read(struct file *file, char *user_buf,
			  size_t buflen, loff_t *ppos)
{
	struct video_device *vdev = file->private_data;
	//struct usb_vicam *vicam = (struct usb_vicam *)vdev;

	dbg("vicam_v4l_read(%d)", buflen);

	if (!vdev || !buf)
		return -EFAULT;

	if (copy_to_user(user_buf, buf2, buflen))
		return -EFAULT;
	return buflen;
}

static int vicam_v4l_do_ioctl(struct inode *inode, struct file *file,
			      unsigned int cmd, void *arg)
{
	struct video_device *vdev = file->private_data;
	struct usb_vicam *vicam = (struct usb_vicam *)vdev;
	int ret = -EL3RST;

	if (!vicam->udev)
		return -EIO;

	down(&vicam->sem);

	switch (cmd) {
	case VIDIOCGCAP:
	{
		struct video_capability *b = arg;
		ret = vicam_get_capability(vicam,b);
		dbg("name %s",b->name);
		break;
	}
	case VIDIOCGFBUF:
	{
		struct video_buffer *vb = arg;
		info("vicam_v4l_ioctl - VIDIOCGBUF - query frame buffer param");
		/* frame buffer not supported, not used */
		memset(vb, 0, sizeof(*vb));
		ret = 0;
		break;
	}
	case VIDIOCGWIN:
	{
		struct video_window *vw = arg;
		ret = vicam_get_window(vicam, vw);
		break;
	}
	case VIDIOCSWIN:
	{
		struct video_window *vw = arg;
		ret = vicam_set_window(vicam, vw);
		break;
	}
	case VIDIOCGCHAN:
	{
		struct video_channel *v = arg;

		ret = vicam_get_channel(vicam,v);
		break;
	}
	case VIDIOCSCHAN:
	{
		struct video_channel *v = arg;
		ret = vicam_set_channel(vicam,v);
		break;
 	}
	case VIDIOCGPICT:
	{
		struct video_picture *p = arg;
		ret = vicam_get_picture(vicam,p);
		break;
	}
	case VIDIOCSPICT:
	{
		struct video_picture *p = arg;
		ret = vicam_set_picture(vicam,p);
		break;
	}
	case VIDIOCGMBUF:
	{
		struct video_mbuf *vm = arg;
		ret = vicam_get_mmapbuffer(vicam,vm);
		break;
	}
	case VIDIOCMCAPTURE:
	{
		struct video_mmap *vm = arg;
		ret = vicam_mmap_capture(vicam,vm);
		break;
	}
	case VIDIOCSYNC:
	{
		int *frame = arg;
		ret = vicam_sync_frame(vicam,*frame);
		break;
	}

	case VIDIOCKEY:
		ret = 0;
 
	case VIDIOCCAPTURE:
	case VIDIOCSFBUF:
	case VIDIOCGTUNER:
	case VIDIOCSTUNER:
	case VIDIOCGFREQ:
	case VIDIOCSFREQ:
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
	case VIDIOCGUNIT:
		ret = -EINVAL;

	default:
	{
		info("vicam_v4l_ioctl - %ui",cmd);
		ret = -ENOIOCTLCMD;
	}
	} /* end switch */

	up(&vicam->sem);
        return ret;
}

static int vicam_v4l_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	return video_usercopy(inode, file, cmd, arg, vicam_v4l_do_ioctl);
}

static int vicam_v4l_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_device *vdev = file->private_data;
	struct usb_vicam *vicam = (struct usb_vicam *)vdev;
	unsigned long start = vma->vm_start;
	unsigned long size  = vma->vm_end-vma->vm_start;
	unsigned long page, pos;

	down(&vicam->sem);
	
	if (vicam->udev == NULL) {
		up(&vicam->sem);
		return -EIO;
	}
#if 0
	if (size > (((VICAM_NUMFRAMES * vicam->maxframesize) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))) {
		up(&vicam->sem);
		return -EINVAL;
	}
#endif
	pos = (unsigned long)vicam->fbuf;
	while (size > 0) {
		page = kvirt_to_pa(pos);
		if (remap_page_range(vma, start, page, PAGE_SIZE, PAGE_SHARED)) {
			up(&vicam->sem);
			return -EAGAIN;
		}
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}
	up(&vicam->sem);

        return 0;
}

/* FIXME - vicam_template - important */
static struct file_operations vicam_fops = {
	owner:		THIS_MODULE,
	open:		vicam_v4l_open,
	release:       	vicam_v4l_close,
	read:		vicam_v4l_read,
	mmap:		vicam_v4l_mmap,
	ioctl:		vicam_v4l_ioctl,
	llseek:         no_llseek,
};
static struct video_device vicam_template = {
	owner:		THIS_MODULE,
	name:		"vicam USB camera",
	type:		VID_TYPE_CAPTURE,
	hardware:	VID_HARDWARE_SE401, /* need to ask for own id */
	fops:           &vicam_fops,
};

/******************************************************************************
 *
 *  Some Routines
 *
 ******************************************************************************/

/*
Flash the led
vicam_sndctrl(1, vicam, VICAM_REQ_CAMERA_POWER, 0x01, NULL, 0);
info ("led on");
vicam_sndctrl(1, vicam, VICAM_REQ_LED_CONTROL, 0x01, NULL, 0);
info ("led off");
vicam_sndctrl(1, vicam, VICAM_REQ_LED_CONTROL, 0x00, NULL, 0);
vicam_sndctrl(1, vicam, VICAM_REQ_CAMERA_POWER, 0x00, NULL, 0);
*/

static void vicam_bulk(struct urb *urb)
{
	struct usb_vicam *vicam = urb->context;

	/*	if (!vicam || !vicam->dev || !vicam->used)
		return;
	*/

	if (urb->status)
		printk("vicam%d: nonzero read/write bulk status received: %d",
			0, urb->status);

	urb->actual_length = 0;
	urb->dev = vicam->udev;

	memcpy(buf2, buf+64, 0x1e480);
	if (vicam->fbuf)
		memcpy(vicam->fbuf, buf+64, 0x1e480);

	if (!change_pending) {
		if (usb_submit_urb(urb, GFP_ATOMIC))
			dbg("failed resubmitting read urb");
	} else {
		change_pending = 0;
		wake_up_interruptible(&vicam->wait);
	}
}

static int vicam_parameters(struct usb_vicam *vicam)
{
	unsigned char req[0x10];
	unsigned int shutter;
	shutter = 10;

	switch (vicam->win.width) {
	case 512:
	default:
		memcpy(req, s512x242bw, 0x10);
		break;
	case 256:
		memcpy(req, s256x242bw, 0x10);
		break;
	case 128:
		memcpy(req, s128x122bw, 0x10);
		break;
	}


	mdelay(10);
	vicam_sndctrl(1, vicam, VICAM_REQ_CAMERA_POWER, 0x01, NULL, 0);
	info ("led on");
	vicam_sndctrl(1, vicam, VICAM_REQ_LED_CONTROL, 0x01, NULL, 0);

	mdelay(10);

	shutter = vicam->win.contrast / 256;
	if (shutter == 0)
		shutter = 1;
	printk("vicam_parameters: brightness %d, shutter %d\n", vicam->win.brightness, shutter );
	req[0] = vicam->win.brightness /256;
	shutter = 15600/shutter - 1;
	req[6] = shutter & 0xff;
	req[7] = (shutter >> 8) & 0xff;
	vicam_sndctrl(1, vicam, VICAM_REQ_CAPTURE, 0x80, req, 0x10);
	mdelay(10);
	vicam_sndctrl(0, vicam, VICAM_REQ_GET_SOMETHIN, 0, buf, 0x10);
	mdelay(10);

	return 0;
}

static int vicam_init(struct usb_vicam *vicam)
{
	int width[] = {128, 256, 512};
	int height[] = {122, 242, 242};

	dbg("vicam_init");
	buf = kmalloc(0x1e480, GFP_KERNEL);
	buf2 = kmalloc(0x1e480, GFP_KERNEL);
	if ((!buf) || (!buf2)) {
		printk("Not enough memory for vicam!\n");
		goto error;
	}

	/* do we do aspect correction in kernel or not? */
	vicam->sizes = 3;
	vicam->width = kmalloc(vicam->sizes*sizeof(int), GFP_KERNEL);
	vicam->height = kmalloc(vicam->sizes*sizeof(int), GFP_KERNEL);
	memcpy(vicam->width, &width, sizeof(width));
	memcpy(vicam->height, &height, sizeof(height));
	vicam->maxframesize = vicam->width[vicam->sizes-1] * vicam->height[vicam->sizes-1];

	/* Download firmware to camera */
	vicam_sndctrl(1, vicam, VICAM_REQ_VENDOR, 0, firmware1, sizeof(firmware1));
	vicam_sndctrl(1, vicam, VICAM_REQ_VENDOR, 0, findex1, sizeof(findex1));
	vicam_sndctrl(1, vicam, VICAM_REQ_VENDOR, 0, fsetup, sizeof(fsetup));
	vicam_sndctrl(1, vicam, VICAM_REQ_VENDOR, 0, firmware2, sizeof(firmware2));
	vicam_sndctrl(1, vicam, VICAM_REQ_VENDOR, 0, findex2, sizeof(findex2));
	vicam_sndctrl(1, vicam, VICAM_REQ_VENDOR, 0, fsetup, sizeof(fsetup));

	vicam_parameters(vicam);

	FILL_BULK_URB(vicam->readurb, vicam->udev, usb_rcvbulkpipe(vicam->udev, 0x81),
		      buf, 0x1e480, vicam_bulk, vicam);
	printk(KERN_DEBUG "Submiting urb: %d\n", usb_submit_urb(vicam->readurb, GFP_KERNEL));

	return 0;
error:
	if (buf)
		kfree(buf);
	if (buf2)
		kfree(buf2);
	return 1;
}

static void *vicam_probe(struct usb_device *udev, unsigned int ifnum,
	const struct usb_device_id *id)
{
	struct usb_vicam *vicam;
	char *camera_name=NULL;

	dbg("vicam_probe");

	/* See if the device offered us matches what we can accept */
	if ((udev->descriptor.idVendor != USB_VICAM_VENDOR_ID) ||
	    (udev->descriptor.idProduct != USB_VICAM_PRODUCT_ID)) {
		return NULL;
	}
	
	camera_name="3Com HomeConnect USB";
	info("ViCAM camera found: %s", camera_name);
	
	vicam = kmalloc (sizeof(struct usb_vicam), GFP_KERNEL);
	if (vicam == NULL) {
		err ("couldn't kmalloc vicam struct");
		return NULL;
	}
	memset(vicam, 0, sizeof(*vicam));

	vicam->readurb = usb_alloc_urb(0, GFP_KERNEL);
	if (!vicam->readurb) {
		kfree(vicam);
		return NULL;
	}

	vicam->udev = udev;
	vicam->camera_name = camera_name;
	vicam->win.brightness = 128;
	vicam->win.contrast = 10;

	/* FIXME */
	if (vicam_init(vicam)) {
		usb_free_urb(vicam->readurb);
		kfree(vicam);
		return NULL;
	}
	memcpy(&vicam->vdev, &vicam_template, sizeof(vicam_template));
	memcpy(vicam->vdev.name, vicam->camera_name, strlen(vicam->camera_name));
	
	if (video_register_device(&vicam->vdev, VFL_TYPE_GRABBER, video_nr) == -1) {
		err("video_register_device");
		usb_free_urb(vicam->readurb);
		kfree(vicam);
		return NULL;
	}

	info("registered new video device: video%d", vicam->vdev.minor);
	
	init_MUTEX (&vicam->sem);
	init_waitqueue_head(&vicam->wait);
	
	return vicam;
}


/* FIXME - vicam_disconnect - important */
static void vicam_disconnect(struct usb_device *udev, void *ptr)
{
	struct usb_vicam *vicam;

	vicam = (struct usb_vicam *) ptr;

	video_unregister_device(&vicam->vdev);
	vicam->udev = NULL;
/*
	vicam->frame[0].grabstate = FRAME_ERROR;
	vicam->frame[1].grabstate = FRAME_ERROR;
*/

	/* Free buffers and shit */

	info("%s disconnected", vicam->camera_name);
	synchronize(vicam);

	if (!vicam->open_count) {
		/* Other random junk */
		usb_free_urb(vicam->readurb);
		kfree(vicam);
		vicam = NULL;
	}
}

/* usb specific object needed to register this driver with the usb subsystem */
static struct usb_driver vicam_driver = {
	owner:		THIS_MODULE,
	name:		"vicam",
	probe:		vicam_probe,
	disconnect:	vicam_disconnect,
	id_table:	vicam_table,
};

/******************************************************************************
 *
 *  Module Routines
 *
 ******************************************************************************/

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/* Module paramaters */
MODULE_PARM(debug, "i");
MODULE_PARM_DESC(debug, "Debug enabled or not");

static int __init usb_vicam_init(void)
{
	int result;

	printk("VICAM: initializing\n");
	/* register this driver with the USB subsystem */
	result = usb_register(&vicam_driver);
	if (result < 0) {
		err("usb_register failed for the "__FILE__" driver. Error number %d",
		    result);
		return -1;
	}

	info(DRIVER_VERSION " " DRIVER_AUTHOR);
	info(DRIVER_DESC);
	return 0;
}

static void __exit usb_vicam_exit(void)
{
	/* deregister this driver with the USB subsystem */
	usb_deregister(&vicam_driver);
}

module_init(usb_vicam_init);
module_exit(usb_vicam_exit);

