/*
 * Video capture interface for Linux
 *
 *		A generic video device interface for the LINUX operating system
 *		using a set of device structures/vectors for low level operations.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Author:	Alan Cox, <alan@cymru.net>
 *
 * Fixes:
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/videodev.h>

#include <asm/uaccess.h>
#include <asm/system.h>


#define VIDEO_NUM_DEVICES	256 

/*
 *	Active devices 
 */
 
static struct video_device *video_device[VIDEO_NUM_DEVICES];

/*
 *	Initialiser list
 */
 
struct video_init
{
	char *name;
	int (*init)(struct video_init *);
};

extern int init_bttv_cards(struct video_init *);

static struct video_init video_init_list[]={
#ifdef CONFIG_VIDEO_BT848
	{"bttv", init_bttv_cards},
#endif	
#ifdef CONFIG_VIDEO_CQCAM
	{"c-qcam", init_colour_qcams},
#endif	
#ifdef CONFIG_VIDEO_BWQCAM
	{"bw-qcam", init_bw_qcams},
#endif	
#ifdef CONFIG_VIDEO_PMS
	{"PMS", init_pms_cards},
#endif	
	{"end", NULL}
};


/*
 *	Read will do some smarts later on. Buffer pin etc.
 */
 
static ssize_t video_read(struct file *file,
	char *buf, size_t count, loff_t *ppos)
{
	int err;
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
	return vfl->read(vfl, buf, count, file->f_flags&O_NONBLOCK);
}

/*
 *	Write for now does nothing. No reason it shouldnt do overlay setting
 *	for some boards I guess..
 */

static ssize_t video_write(struct file *file, const char *buf, 
	size_t count, loff_t *ppos)
{
	int err;
	struct video_device *vfl=video_device[MINOR(file->f_dentry->d_inode->i_rdev)];
	return vfl->write(vfl, buf, count, file->f_flags&O_NONBLOCK);
}

/*
 *	Open a video device.
 */

static int video_open(struct inode *inode, struct file *file)
{
	unsigned int minor = MINOR(inode->i_rdev);
	int err;
	struct video_device *vfl;
	
	if(minor>=VIDEO_NUM_DEVICES)
		return -ENODEV;
		
	vfl=video_device[minor];
	if(vfl==NULL)
		return -ENODEV;
	if(vfl->busy)
		return -EBUSY;
	vfl->busy=1;		/* In case vfl->open sleeps */
	
	if(vfl->open)
	{
		err=vfl->open(vfl,0);	/* Tell the device it is open */
		if(err)
		{
			vfl->busy=0;
			return err;
		}
	}
	return 0;
}

/*
 *	Last close of a video for Linux device
 */
	
static int video_release(struct inode *inode, struct file *file)
{
	struct video_device *vfl=video_device[MINOR(inode->i_rdev)];
	if(vfl->close)
		vfl->close(vfl);
	vfl->busy=0;
	return 0;
}

/*
 *	Question: Should we be able to capture and then seek around the
 *	image ?
 */
 
static long long video_lseek(struct file * file,
			  long long offset, int origin)
{
	return -ESPIPE;
}


static int video_ioctl(struct inode *inode, struct file *file,
	unsigned int cmd, unsigned long arg)
{
	struct video_device *vfl=video_device[MINOR(inode->i_rdev)];
	int err=vfl->ioctl(vfl, cmd, (void *)arg);

	if(err!=-ENOIOCTLCMD)
		return err;
	
	switch(cmd)
	{
		default:
			return -EINVAL;
	}
}

/*
 *	We need to do MMAP support
 */

/*
 *	Video For Linux device drivers request registration here.
 */
 
int video_register_device(struct video_device *vfd)
{
	int i=0;
	int base=0;
	int err;
	
	for(i=base;i<base+VIDEO_NUM_DEVICES;i++)
	{
		if(video_device[i]==NULL)
		{
			video_device[i]=vfd;
			vfd->minor=i;
			/* The init call may sleep so we book the slot out
			   then call */
			MOD_INC_USE_COUNT;
			err=vfd->initialize(vfd);
			if(err<0)
			{
				video_device[i]=NULL;
				MOD_DEC_USE_COUNT;
				return err;
			}
			return 0;
		}
	}
	return -ENFILE;
}

/*
 *	Unregister an unused video for linux device
 */
 
void video_unregister_device(struct video_device *vfd)
{
	if(video_device[vfd->minor]!=vfd)
		panic("vfd: bad unregister");
	video_device[vfd->minor]=NULL;
	MOD_DEC_USE_COUNT;
}


static struct file_operations video_fops=
{
	video_lseek,
	video_read,
	video_write,
	NULL,	/* readdir */
	NULL,	/* poll */
	video_ioctl,
	NULL,	/* mmap */
	video_open,
	video_release
};

/*
 *	Initialise video for linux
 */
 
int videodev_init(void)
{
	struct video_init *vfli = video_init_list;
	
	printk(KERN_INFO "Linux video capture interface: v0.01 ALPHA\n");
	if(register_chrdev(VIDEO_MAJOR,"video_capture", &video_fops))
	{
		printk("video_dev: unable to get major %d\n", VIDEO_MAJOR);
		return -EIO;
	}

	/*
	 *	Init kernel installed video drivers
	 */
	 	
	while(vfli->init!=NULL)
	{
		vfli->init(vfli);
		vfli++;
	}
	return 0;
}

#ifdef MODULE		
int init_module(void)
{
	return videodev_init();
}

void cleanup_module(void)
{
	unregister_chrdev(VIDEO_MAJOR, "video_capture");
}

#endif

EXPORT_SYMBOL(video_register_device);
EXPORT_SYMBOL(video_unregister_device);
