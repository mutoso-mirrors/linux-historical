/*
 * linux/drivers/char/ppdev.c
 *
 * This is the code behind /dev/parport* -- it allows a user-space
 * application to use the parport subsystem.
 *
 * Copyright (C) 1998-9 Tim Waugh <tim@cyberelk.demon.co.uk>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * A /dev/parportxy device node represents an arbitrary device ('y')
 * on port 'x'.  The following operations are possible:
 *
 * open		do nothing, set up default IEEE 1284 protocol to be COMPAT
 * close	release port and unregister device (if necessary)
 * ioctl
 *   EXCL	register device exclusively (may fail)
 *   CLAIM	(register device first time) parport_claim_or_block
 *   RELEASE	parport_release
 *   SETMODE	set the IEEE 1284 protocol to use for read/write
 *   DATADIR	data_forward / data_reverse
 *   WDATA	write_data
 *   RDATA	read_data
 *   WCONTROL	write_control
 *   RCONTROL	read_control
 *   FCONTROL	frob_control
 *   RSTATUS	read_status
 *   NEGOT	parport_negotiate
 *   YIELD	parport_yield_blocking
 * read/write	read or write in current IEEE 1284 protocol
 * select	wait for interrupt (in readfds)
 */

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/ioctl.h>
#include <linux/parport.h>
#include <linux/ctype.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include "ppdev.h"

#define PP_VERSION "ppdev: user-space parallel port driver"
#define CHRDEV "ppdev"

#ifndef min
#define min(a,b) ((a) < (b) ? (a) : (b))
#endif

/* The device minor encodes the parport number and (arbitrary) 
 * pardevice number as (port << 4) | dev. */
#define PP_PORT(minor) ((minor >> 4) & 0xf)

struct pp_struct {
	struct pardevice * pdev;
	wait_queue_head_t irq_wait;
	int got_irq;
	int mode;
	unsigned int flags;
};

/* pp_struct.flags bitfields */
#define PP_CLAIMED    (1<<0)
#define PP_EXCL       (1<<1)

/* Other constants */
#define PP_INTERRUPT_TIMEOUT (10 * HZ) /* 10s */
#define PP_BUFFER_SIZE 256
#define PARDEVICE_MAX 8

static inline void enable_irq (struct pp_struct *pp)
{
	struct parport *port = pp->pdev->port;
	port->ops->enable_irq (port);
}

static loff_t pp_lseek (struct file * file, long long offset, int origin)
{
	return -ESPIPE;
}

/* This looks a bit like parport_read.  The difference is that we don't
 * determine the mode to use from the port data, but rather from the
 * mode the driver told us to use. */
static ssize_t do_read (struct pp_struct *pp, void *buf, size_t len)
{
	size_t (*fn) (struct parport *, void *, size_t, int);
	struct parport *port = pp->pdev->port;

	switch (pp->mode) {
	case IEEE1284_MODE_COMPAT:
		/* This is a write-only mode. */
		return -EIO;

	case IEEE1284_MODE_NIBBLE:
		fn = port->ops->nibble_read_data;
		break;

	case IEEE1284_MODE_BYTE:
		fn = port->ops->byte_read_data;
		break;

	case IEEE1284_MODE_EPP:
		fn = port->ops->epp_read_data;
		break;

	case IEEE1284_MODE_ECP:
	case IEEE1284_MODE_ECPRLE:
		fn = port->ops->ecp_read_data;
		break;

	case IEEE1284_MODE_ECPSWE:
		fn = parport_ieee1284_ecp_read_data;
		break;

	default:
		printk (KERN_DEBUG "%s: unknown mode 0x%02x\n",
			pp->pdev->name, pp->mode);
		return -EINVAL;
	}

	return (*fn) (port, buf, len, 0);
}

/* This looks a bit like parport_write.  The difference is that we don't
 * determine the mode to use from the port data, but rather from the
 * mode the driver told us to use. */
static ssize_t do_write (struct pp_struct *pp, const void *buf, size_t len)
{
	size_t (*fn) (struct parport *, const void *, size_t, int);
	struct parport *port = pp->pdev->port;

	switch (pp->mode) {
	case IEEE1284_MODE_NIBBLE:
	case IEEE1284_MODE_BYTE:
		/* Read-only modes. */
		return -EIO;

	case IEEE1284_MODE_COMPAT:
		fn = port->ops->compat_write_data;
		break;

	case IEEE1284_MODE_EPP:
		fn = port->ops->epp_write_data;
		break;

	case IEEE1284_MODE_ECP:
	case IEEE1284_MODE_ECPRLE:
		fn = port->ops->ecp_write_data;
		break;

	case IEEE1284_MODE_ECPSWE:
		fn = parport_ieee1284_ecp_write_data;
		break;

	default:
		printk (KERN_DEBUG "%s: unknown mode 0x%02x\n",
			pp->pdev->name, pp->mode);
		return -EINVAL;
	}

	return (*fn) (port, buf, len, 0);
}

static ssize_t pp_read (struct file * file, char * buf, size_t count,
			loff_t * ppos)
{
	unsigned int minor = MINOR (file->f_dentry->d_inode->i_rdev);
	struct pp_struct *pp = file->private_data;
	char * kbuffer;
	ssize_t bytes_read = 0;
	ssize_t got = 0;

	if (!(pp->flags & PP_CLAIMED)) {
		/* Don't have the port claimed */
		printk (KERN_DEBUG CHRDEV "%02x: claim the port first\n",
			minor);
		return -EINVAL;
	}

	kbuffer = kmalloc (min (count, PP_BUFFER_SIZE), GFP_KERNEL);
	if (!kbuffer)
		return -ENOMEM;

	while (bytes_read < count) {
		ssize_t need = min(count - bytes_read, PP_BUFFER_SIZE);

		got = do_read (pp, kbuffer, need);

		if (got <= 0) {
			if (!bytes_read)
				bytes_read = got;

			break;
		}

		if (copy_to_user (buf + bytes_read, kbuffer, got)) {
			bytes_read = -EFAULT;
			break;
		}

		bytes_read += got;

		if (signal_pending (current)) {
			if (!bytes_read)
				bytes_read = -EINTR;
			break;
		}

		if (current->need_resched)
			schedule ();
	}

	kfree (kbuffer);
	enable_irq (pp);
	return bytes_read;
}

static ssize_t pp_write (struct file * file, const char * buf, size_t count,
			 loff_t * ppos)
{
	unsigned int minor = MINOR (file->f_dentry->d_inode->i_rdev);
	struct pp_struct *pp = file->private_data;
	char * kbuffer;
	ssize_t bytes_written = 0;
	ssize_t wrote;

	if (!(pp->flags & PP_CLAIMED)) {
		/* Don't have the port claimed */
		printk (KERN_DEBUG CHRDEV "%02x: claim the port first\n",
			minor);
		return -EINVAL;
	}

	kbuffer = kmalloc (min (count, PP_BUFFER_SIZE), GFP_KERNEL);
	if (!kbuffer)
		return -ENOMEM;

	while (bytes_written < count) {
		ssize_t n = min(count - bytes_written, PP_BUFFER_SIZE);

		if (copy_from_user (kbuffer, buf + bytes_written, n)) {
			bytes_written = -EFAULT;
			break;
		}

		wrote = do_write (pp, kbuffer, n);

		if (wrote < 0) {
			if (!bytes_written)
				bytes_written = wrote;
			break;
		}

		bytes_written += wrote;

		if (signal_pending (current)) {
			if (!bytes_written)
				bytes_written = -EINTR;
			break;
		}

		if (current->need_resched)
			schedule ();
	}

	kfree (kbuffer);
	enable_irq (pp);
	return bytes_written;
}

static void pp_irq (int irq, void * private, struct pt_regs * unused)
{
	struct pp_struct * pp = (struct pp_struct *) private;
	pp->got_irq = 1;
	wake_up_interruptible (&pp->irq_wait);
}

static int register_device (int minor, struct pp_struct *pp)
{
	unsigned int portnum = PP_PORT (minor);
	struct parport * port;
	struct pardevice * pdev = NULL;
	char *name;
	int fl;

	name = kmalloc (strlen (CHRDEV) + 3, GFP_KERNEL);
	if (name == NULL)
		return -ENOMEM;

	sprintf (name, CHRDEV "%02x", minor);
	port = parport_enumerate (); /* FIXME: use attach/detach */

	while (port && port->number != portnum)
		port = port->next;

	if (!port) {
		printk (KERN_WARNING "%s: no associated port!\n", name);
		kfree (name);
		return -ENXIO;
	}

	fl = (pp->flags & PP_EXCL) ? PARPORT_FLAG_EXCL : 0;
	pdev = parport_register_device (port, name, NULL, NULL, pp_irq, fl,
					pp);

	if (!pdev) {
		printk (KERN_WARNING "%s: failed to register device!\n", name);
		kfree (name);
		return -ENXIO;
	}

	pp->pdev = pdev;
	printk (KERN_DEBUG "%s: registered pardevice\n", name);
	return 0;
}

static int pp_ioctl(struct inode *inode, struct file *file,
		    unsigned int cmd, unsigned long arg)
{
	unsigned int minor = MINOR(inode->i_rdev);
	struct pp_struct *pp = file->private_data;
	struct parport * port;

	/* First handle the cases that don't take arguments. */
	if (cmd == PPCLAIM) {
		if (pp->flags & PP_CLAIMED) {
			printk (KERN_DEBUG CHRDEV
				"%02x: you've already got it!\n", minor);
			return -EINVAL;
		}

		/* Deferred device registration. */
		if (!pp->pdev) {
			int err = register_device (minor, pp);
			if (err)
				return err;
		}

		parport_claim_or_block (pp->pdev);
		pp->flags |= PP_CLAIMED;

		/* For interrupt-reporting to work, we need to be
		 * informed of each interrupt. */
		enable_irq (pp);
		return 0;
	}

	if (cmd == PPEXCL) {
		if (pp->pdev) {
			printk (KERN_DEBUG CHRDEV "%02x: too late for PPEXCL; "
				"already registered\n", minor);
			if (pp->flags & PP_EXCL)
				/* But it's not really an error. */
				return 0;
			/* There's no chance of making the driver happy. */
			return -EINVAL;
		}

		/* Just remember to register the device exclusively
		 * when we finally do the registration. */
		pp->flags |= PP_EXCL;
		return 0;
	}

	if (cmd == PPSETMODE) {
		int mode;
		if (copy_from_user (&mode, (int *) arg, sizeof (mode)))
			return -EFAULT;
		/* FIXME: validate mode */
		pp->mode = mode;
		return 0;
	}

	/* Everything else requires the port to be claimed, so check
	 * that now. */
	if ((pp->flags & PP_CLAIMED) == 0) {
		printk (KERN_DEBUG CHRDEV "%02x: claim the port first\n",
			minor);
		return -EINVAL;
	}

	port = pp->pdev->port;
	switch (cmd) {
		unsigned char reg;
		unsigned char mask;
		int mode;
		int ret;

	case PPRSTATUS:
		reg = parport_read_status (port);
		return copy_to_user ((unsigned char *) arg, &reg,
				     sizeof (reg));

	case PPRDATA:
		reg = parport_read_data (port);
		return copy_to_user ((unsigned char *) arg, &reg,
				     sizeof (reg));

	case PPRCONTROL:
		reg = parport_read_control (port);
		return copy_to_user ((unsigned char *) arg, &reg,
				     sizeof (reg));

	case PPYIELD:
		parport_yield_blocking (pp->pdev);
		return 0;

	case PPRELEASE:
		parport_release (pp->pdev);
		pp->flags &= ~PP_CLAIMED;
		return 0;

	case PPWCONTROL:
		if (copy_from_user (&reg, (unsigned char *) arg, sizeof (reg)))
			return -EFAULT;
		parport_write_control (port, reg);
		return 0;

	case PPWDATA:
		if (copy_from_user (&reg, (unsigned char *) arg, sizeof (reg)))
			return -EFAULT;
		parport_write_data (port, reg);
		return 0;

	case PPFCONTROL:
		if (copy_from_user (&mask, (unsigned char *) arg,
				    sizeof (mask)))
			return -EFAULT;
		if (copy_from_user (&reg, 1 + (unsigned char *) arg,
				    sizeof (reg)))
			return -EFAULT;
		parport_frob_control (port, mask, reg);
		return 0;

	case PPDATADIR:
		if (copy_from_user (&mode, (int *) arg, sizeof (mode)))
			return -EFAULT;
		if (mode)
			port->ops->data_reverse (port);
		else
			port->ops->data_forward (port);
		return 0;

	case PPNEGOT:
		if (copy_from_user (&mode, (int *) arg, sizeof (mode)))
			return -EFAULT;
		/* FIXME: validate mode */
		ret = parport_negotiate (port, mode);
		enable_irq (pp);
		return ret;

	default:
		printk (KERN_DEBUG CHRDEV "%02x: What? (cmd=0x%x)\n", minor,
			cmd);
		return -EINVAL;
	}

	/* Keep the compiler happy */
	return 0;
}

static int pp_open (struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR (inode->i_rdev);
	unsigned int portnum = PP_PORT (minor);
	struct pp_struct *pp;

	if (portnum >= PARPORT_MAX)
		return -ENXIO;

	pp = kmalloc (GFP_KERNEL, sizeof (struct pp_struct));
	if (!pp)
		return -ENOMEM;

	pp->mode = IEEE1284_MODE_COMPAT;
	pp->flags = 0;
	pp->got_irq = 0;
	init_waitqueue_head (&pp->irq_wait);

	/* Defer the actual device registration until the first claim.
	 * That way, we know whether or not the driver wants to have
	 * exclusive access to the port (PPEXCL).
	 */
	pp->pdev = NULL;
	file->private_data = pp;

	MOD_INC_USE_COUNT;
	return 0;
}

static int pp_release (struct inode * inode, struct file * file)
{
	unsigned int minor = MINOR (inode->i_rdev);
	struct pp_struct *pp = file->private_data;

	if (pp->flags & PP_CLAIMED) {
		parport_release (pp->pdev);
		printk (KERN_DEBUG CHRDEV "%02x: released pardevice because "
			"user-space forgot\n", minor);
	}

	if (pp->pdev) {
		kfree (pp->pdev->name);
		parport_unregister_device (pp->pdev);
		pp->pdev = NULL;
		printk (KERN_DEBUG CHRDEV "%02x: unregistered pardevice\n",
			minor);
	}

	kfree (pp);

	MOD_DEC_USE_COUNT;
	return 0;
}

static unsigned int pp_poll (struct file * file, poll_table * wait)
{
	struct pp_struct *pp = file->private_data;
	unsigned int mask = 0;
	if (pp->got_irq) {
		pp->got_irq = 0;
		mask |= POLLIN | POLLRDNORM;
	}
	poll_wait (file, &pp->irq_wait, wait);
	return mask;
}

static struct file_operations pp_fops = {
	pp_lseek,
	pp_read,
	pp_write,
	NULL,	/* pp_readdir */
	pp_poll,
	pp_ioctl,
	NULL,	/* pp_mmap */
	pp_open,
	NULL,   /* pp_flush */
	pp_release
};

#ifdef MODULE
#define pp_init init_module
#endif

int pp_init (void)
{
	if (register_chrdev (PP_MAJOR, CHRDEV, &pp_fops)) {
		printk (KERN_WARNING CHRDEV ": unable to get major %d\n",
			PP_MAJOR);
		return -EIO;
	}

	printk (KERN_INFO PP_VERSION "\n");
	return 0;
}

#ifdef MODULE
void cleanup_module (void)
{
	/* Clean up all parport stuff */
	unregister_chrdev (PP_MAJOR, CHRDEV);
}
#endif /* MODULE */
