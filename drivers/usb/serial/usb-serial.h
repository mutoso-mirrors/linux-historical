/*
 * USB Serial Converter driver
 *
 *	Copyright (C) 1999 - 2002
 *	    Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 * See Documentation/usb/usb-serial.txt for more information on using this driver
 *
 * (03/26/2002) gkh
 *	removed the port->tty check from port_paranoia_check() due to serial
 *	consoles not having a tty device assigned to them.
 *
 * (12/03/2001) gkh
 *	removed active from the port structure.
 *	added documentation to the usb_serial_device_type structure
 *
 * (10/10/2001) gkh
 *	added vendor and product to serial structure.  Needed to determine device
 *	owner when the device is disconnected.
 *
 * (05/30/2001) gkh
 *	added sem to port structure and removed port_lock
 *
 * (10/05/2000) gkh
 *	Added interrupt_in_endpointAddress and bulk_in_endpointAddress to help
 *	fix bug with urb->dev not being set properly, now that the usb core
 *	needs it.
 * 
 * (09/11/2000) gkh
 *	Added usb_serial_debug_data function to help get rid of #DEBUG in the
 *	drivers.
 *
 * (08/28/2000) gkh
 *	Added port_lock to port structure.
 *
 * (08/08/2000) gkh
 *	Added open_count to port structure.
 *
 * (07/23/2000) gkh
 *	Added bulk_out_endpointAddress to port structure.
 *
 * (07/19/2000) gkh, pberger, and borchers
 *	Modifications to allow usb-serial drivers to be modules.
 *
 * 
 */


#ifndef __LINUX_USB_SERIAL_H
#define __LINUX_USB_SERIAL_H

#include <linux/config.h>

#define SERIAL_TTY_MAJOR	188	/* Nice legal number now */
#define SERIAL_TTY_MINORS	255	/* loads of devices :) */

#define MAX_NUM_PORTS		8	/* The maximum number of ports one device can grab at once */

#define USB_SERIAL_MAGIC	0x6702	/* magic number for usb_serial struct */
#define USB_SERIAL_PORT_MAGIC	0x7301	/* magic number for usb_serial_port struct */

/* parity check flag */
#define RELEVANT_IFLAG(iflag)	(iflag & (IGNBRK|BRKINT|IGNPAR|PARMRK|INPCK))


struct usb_serial_port {
	int			magic;
	struct usb_serial	*serial;	/* pointer back to the owner of this port */
	struct tty_struct *	tty;		/* the coresponding tty for this port */
	unsigned char		number;

	unsigned char *		interrupt_in_buffer;
	struct urb *		interrupt_in_urb;
	__u8			interrupt_in_endpointAddress;

	unsigned char *		bulk_in_buffer;
	struct urb *		read_urb;
	__u8			bulk_in_endpointAddress;

	unsigned char *		bulk_out_buffer;
	int			bulk_out_size;
	struct urb *		write_urb;
	__u8			bulk_out_endpointAddress;

	wait_queue_head_t	write_wait;

	struct tq_struct	tqueue;		/* task queue for line discipline waking up */
	int			open_count;	/* number of times this port has been opened */
	struct semaphore	sem;		/* locks this structure */
	
	void *			private;	/* data private to the specific port */
};

struct usb_serial {
	int				magic;
	struct usb_device *		dev;
	struct usb_serial_device_type *	type;			/* the type of usb serial device this is */
	struct usb_interface *		interface;		/* the interface for this device */
	struct tty_driver *		tty_driver;		/* the tty_driver for this device */
	unsigned char			minor;			/* the starting minor number for this device */
	unsigned char			num_ports;		/* the number of ports this device has */
	char				num_interrupt_in;	/* number of interrupt in endpoints we have */
	char				num_bulk_in;		/* number of bulk in endpoints we have */
	char				num_bulk_out;		/* number of bulk out endpoints we have */
	__u16				vendor;			/* vendor id of this device */
	__u16				product;		/* product id of this device */
	struct usb_serial_port		port[MAX_NUM_PORTS];

	void *			private;		/* data private to the specific driver */
};


#define NUM_DONT_CARE	(-1)


/**
 * usb_serial_device_type - a structure that defines a usb serial device
 * @owner: pointer to the module that owns this device.
 * @name: pointer to a string that describes this device.  This string used
 *	in the syslog messages when a device is inserted or removed.
 * @id_table: pointer to a list of usb_device_id structures that define all
 *	of the devices this structure can support.
 * @num_interrupt_in: the number of interrupt in endpoints this device will
 *	have.
 * @num_bulk_in: the number of bulk in endpoints this device will have.
 * @num_bulk_out: the number of bulk out endpoints this device will have.
 * @num_ports: the number of different ports this device will have.
 * @calc_num_ports: pointer to a function to determine how many ports this
 *	device has dynamically.  It will be called after the probe()
 *	callback is called, but before attach()
 * @probe: pointer to the driver's probe function.
 *	This will be called when the device is inserted into the system,
 *	but before the device has been fully initialized by the usb_serial
 *	subsystem.  Use this function to download any firmware to the device,
 *	or any other early initialization that might be needed.
 *	Return 0 to continue on with the initialization sequence.  Anything 
 *	else will abort it.
 * @attach: pointer to the driver's attach function.
 *	This will be called when the struct usb_serial structure is fully set
 *	set up.  Do any local initialization of the device, or any private
 *	memory structure allocation at this point in time.
 * @shutdown: pointer to the driver's shutdown function.  This will be
 *	called when the device is removed from the system.
 *
 * This structure is defines a USB Serial device.  It provides all of
 * the information that the USB serial core code needs.  If the function
 * pointers are defined, then the USB serial core code will call them when
 * the corresponding tty port functions are called.  If they are not
 * called, the generic serial function will be used instead.
 */
struct usb_serial_device_type {
	struct module *owner;
	char	*name;
	const struct usb_device_id *id_table;
	char	num_interrupt_in;
	char	num_bulk_in;
	char	num_bulk_out;
	char	num_ports;

	struct list_head	driver_list;
	
	int (*probe) (struct usb_serial *serial);
	int (*attach) (struct usb_serial *serial);
	int (*calc_num_ports) (struct usb_serial *serial);

	void (*shutdown) (struct usb_serial *serial);

	/* serial function calls */
	int  (*open)		(struct usb_serial_port *port, struct file * filp);
	void (*close)		(struct usb_serial_port *port, struct file * filp);
	int  (*write)		(struct usb_serial_port *port, int from_user, const unsigned char *buf, int count);
	int  (*write_room)	(struct usb_serial_port *port);
	int  (*ioctl)		(struct usb_serial_port *port, struct file * file, unsigned int cmd, unsigned long arg);
	void (*set_termios)	(struct usb_serial_port *port, struct termios * old);
	void (*break_ctl)	(struct usb_serial_port *port, int break_state);
	int  (*chars_in_buffer)	(struct usb_serial_port *port);
	void (*throttle)	(struct usb_serial_port *port);
	void (*unthrottle)	(struct usb_serial_port *port);

	void (*read_int_callback)(struct urb *urb);
	void (*read_bulk_callback)(struct urb *urb);
	void (*write_bulk_callback)(struct urb *urb);
};

extern int  usb_serial_register(struct usb_serial_device_type *new_device);
extern void usb_serial_deregister(struct usb_serial_device_type *device);

/* determine if we should include the EzUSB loader functions */
#undef USES_EZUSB_FUNCTIONS
#if defined(CONFIG_USB_SERIAL_KEYSPAN_PDA) || defined(CONFIG_USB_SERIAL_KEYSPAN_PDA_MODULE)
	#define USES_EZUSB_FUNCTIONS
#endif
#if defined(CONFIG_USB_SERIAL_XIRCOM) || defined(CONFIG_USB_SERIAL_XIRCOM_MODULE)
	#define USES_EZUSB_FUNCTIONS
#endif
#if defined(CONFIG_USB_SERIAL_KEYSPAN) || defined(CONFIG_USB_SERIAL_KEYSPAN_MODULE)
	#define USES_EZUSB_FUNCTIONS
#endif
#if defined(CONFIG_USB_SERIAL_WHITEHEAT) || defined(CONFIG_USB_SERIAL_WHITEHEAT_MODULE)
	#define USES_EZUSB_FUNCTIONS
#endif
#ifdef USES_EZUSB_FUNCTIONS
extern int ezusb_writememory (struct usb_serial *serial, int address, unsigned char *data, int length, __u8 bRequest);
extern int ezusb_set_reset (struct usb_serial *serial, unsigned char reset_bit);
#endif


/* Inline functions to check the sanity of a pointer that is passed to us */
static inline int serial_paranoia_check (struct usb_serial *serial, const char *function)
{
	if (!serial) {
		dbg("%s - serial == NULL", function);
		return -1;
	}
	if (serial->magic != USB_SERIAL_MAGIC) {
		dbg("%s - bad magic number for serial", function);
		return -1;
	}
	if (!serial->type) {
		dbg("%s - serial->type == NULL!", function);
		return -1;
	}

	return 0;
}


static inline int port_paranoia_check (struct usb_serial_port *port, const char *function)
{
	if (!port) {
		dbg("%s - port == NULL", function);
		return -1;
	}
	if (port->magic != USB_SERIAL_PORT_MAGIC) {
		dbg("%s - bad magic number for port", function);
		return -1;
	}
	if (!port->serial) {
		dbg("%s - port->serial == NULL", function);
		return -1;
	}

	return 0;
}


static inline struct usb_serial* get_usb_serial (struct usb_serial_port *port, const char *function) 
{ 
	/* if no port was specified, or it fails a paranoia check */
	if (!port || 
		port_paranoia_check (port, function) ||
		serial_paranoia_check (port->serial, function)) {
		/* then say that we dont have a valid usb_serial thing, which will
		 * end up genrating -ENODEV return values */ 
		return NULL;
	}

	return port->serial;
}


static inline void usb_serial_debug_data (const char *file, const char *function, int size, const unsigned char *data)
{
	int i;

	if (!debug)
		return;
	
	printk (KERN_DEBUG "%s: %s - length = %d, data = ", file, function, size);
	for (i = 0; i < size; ++i) {
		printk ("%.2x ", data[i]);
	}
	printk ("\n");
}


/* Use our own dbg macro */
#undef dbg
#define dbg(format, arg...) do { if (debug) printk(KERN_DEBUG __FILE__ ": " format "\n" , ## arg); } while (0)



#endif	/* ifdef __LINUX_USB_SERIAL_H */

