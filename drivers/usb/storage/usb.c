/* Driver for USB Mass Storage compliant devices
 *
 * $Id: usb.c,v 1.16 2000/08/01 22:01:19 mdharm Exp $
 *
 * Current development and maintainance by:
 *   (c) 1999, 2000 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
 *
 * This driver is based on the 'USB Mass Storage Class' document. This
 * describes in detail the protocol used to communicate with such
 * devices.  Clearly, the designers had SCSI and ATAPI commands in
 * mind when they created this document.  The commands are all very
 * similar to commands in the SCSI-II and ATAPI specifications.
 *
 * It is important to note that in a number of cases this class
 * exhibits class-specific exemptions from the USB specification.
 * Notably the usage of NAK, STALL and ACK differs from the norm, in
 * that they are used to communicate wait, failed and OK on commands.
 *
 * Also, for certain devices, the interrupt endpoint is used to convey
 * status of a command.
 *
 * Please see http://www.one-eyed-alien.net/~mdharm/linux-usb for more
 * information about this driver.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>

#include "usb.h"
#include "scsiglue.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"
#ifdef CONFIG_USB_STORAGE_HP8200e
#include "shuttle_usbat.h"
#endif
#ifdef CONFIG_USB_STORAGE_SDDR09
#include "sddr09.h"
#endif

#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/malloc.h>

/*
 * Per device data
 */

static int my_host_number;

/*
 * kernel thread actions
 */

#define US_ACT_COMMAND		1
#define US_ACT_DEVICE_RESET	2
#define US_ACT_BUS_RESET	3
#define US_ACT_HOST_RESET	4
#define US_ACT_EXIT		5

/* The list of structures and the protective lock for them */
struct us_data *us_list;
struct semaphore us_list_semaphore;

static void * storage_probe(struct usb_device *dev, unsigned int ifnum);
static void storage_disconnect(struct usb_device *dev, void *ptr);
static struct usb_driver storage_driver = {
	name:		"usb-storage",
	probe:		storage_probe,
	disconnect:	storage_disconnect,
};

static int usb_stor_control_thread(void * __us)
{
	wait_queue_t wait;
	struct us_data *us = (struct us_data *)__us;
	int action;

	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources..
	 */
	exit_files(current);
	current->files = init_task.files;
	atomic_inc(&current->files->count);
	daemonize();

	/* set our name for identification purposes */
	sprintf(current->comm, "usb-storage-%d", us->host_number);

	unlock_kernel();

	/* set up for wakeups by new commands */
	init_waitqueue_entry(&wait, current);
	init_waitqueue_head(&(us->wqh));
	add_wait_queue(&(us->wqh), &wait);

	/* signal that we've started the thread */
	up(&(us->notify));
	set_current_state(TASK_INTERRUPTIBLE);

	for(;;) {
		US_DEBUGP("*** thread sleeping.\n");
		schedule();
		US_DEBUGP("*** thread awakened.\n");

		/* lock access to the queue element */
		down(&(us->queue_exclusion));

		/* take the command off the queue */
		action = us->action;
		us->action = 0;
		us->srb = us->queue_srb;
	
		/* release the queue lock as fast as possible */
		up(&(us->queue_exclusion));

		switch (action) {
		case US_ACT_COMMAND:
			/* reject the command if the direction indicator 
			 * is UNKNOWN
			 */
			if (us->srb->sc_data_direction == SCSI_DATA_UNKNOWN) {
				US_DEBUGP("UNKNOWN data direction\n");
				us->srb->result = DID_ERROR;
				set_current_state(TASK_INTERRUPTIBLE);
				us->srb->scsi_done(us->srb);
				us->srb = NULL;
				break;
			}
			
			/* reject if target != 0 or if LUN is higher than
			 * the maximum known LUN
			 */
			if (us->srb->target || (us->srb->lun > us->max_lun)) {
				US_DEBUGP("Bad device number (%d/%d)\n",
					  us->srb->target, us->srb->lun);
				us->srb->result = DID_BAD_TARGET << 16;

				set_current_state(TASK_INTERRUPTIBLE);
				us->srb->scsi_done(us->srb);
				us->srb = NULL;
				break;
			}

			/* handle those devices which can't do a START_STOP */
			if ((us->srb->cmnd[0] == START_STOP) &&
			    (us->flags & US_FL_START_STOP)) {
				us->srb->result = GOOD;

				set_current_state(TASK_INTERRUPTIBLE);
				us->srb->scsi_done(us->srb);
				us->srb = NULL;
				break;
			}
				     
			/* lock the device pointers */
			down(&(us->dev_semaphore));

			/* our device has gone - pretend not ready */
			if (!us->pusb_dev) {
				US_DEBUGP("Request is for removed device\n");
				/* For REQUEST_SENSE, it's the data.  But
				 * for anything else, it should look like
				 * we auto-sensed for it.
				 */
				if (us->srb->cmnd[0] == REQUEST_SENSE) {
					memcpy(us->srb->request_buffer, 
					       usb_stor_sense_notready, 
					       sizeof(usb_stor_sense_notready));
					us->srb->result = GOOD;
				} else {
					memcpy(us->srb->sense_buffer, 
					       usb_stor_sense_notready, 
					       sizeof(usb_stor_sense_notready));
					us->srb->result = CHECK_CONDITION;
				}
			} else { /* !us->pusb_dev */
				/* we've got a command, let's do it! */
				US_DEBUG(usb_stor_show_command(us->srb));
				us->proto_handler(us->srb, us);
			}

			/* unlock the device pointers */
			up(&(us->dev_semaphore));

			/* indicate that the command is done */
			if (us->srb->result != DID_ABORT << 16) {
				US_DEBUGP("scsi cmd done, result=0x%x\n", 
					   us->srb->result);
				set_current_state(TASK_INTERRUPTIBLE);
				us->srb->scsi_done(us->srb);
			} else {
				US_DEBUGP("scsi command aborted\n");
				set_current_state(TASK_INTERRUPTIBLE);
				up(&(us->notify));
			}
			us->srb = NULL;
			break;

		case US_ACT_DEVICE_RESET:
			break;

		case US_ACT_BUS_RESET:
			break;

		case US_ACT_HOST_RESET:
			break;

		} /* end switch on action */

		/* exit if we get a signal to exit */
		if (action == US_ACT_EXIT) {
			US_DEBUGP("-- US_ACT_EXIT command recieved\n");
			break;
		}
	} /* for (;;) */

	/* notify the exit routine that we're actually exiting now */
	up(&(us->notify));

	return 0;
}	

/* This is the list of devices we recognize, along with their flag data */
static struct us_unusual_dev us_unusual_dev_list[] = {
	{ 0x03f0, 0x0107, 0x0200, 0x0200, "HP USB CD-Writer Plus",
		US_SC_8070, US_PR_CB, 0}, 
#ifdef CONFIG_USB_STORAGE_HP8200e
	{ 0x03f0, 0x0207, 0x0001, 0x0001, "HP USB CD-Writer Plus 8200e",
		US_SC_8070, US_PR_SCM_ATAPI, 
		US_FL_ALT_LENGTH | US_FL_NEED_INIT | US_FL_SINGLE_LUN}, 
#endif
	{ 0x04e6, 0x0001, 0x0200, 0x0200, "Matshita LS-120",
		US_SC_8020, US_PR_CB, US_FL_SINGLE_LUN},
	{ 0x04e6, 0x0002, 0x0100, 0x0100, "Shuttle eUSCSI Bridge",
		US_SC_SCSI, US_PR_BULK, US_FL_ALT_LENGTH}, 
	{ 0x04e6, 0x0006, 0x0100, 0x0100, "Shuttle eUSB MMC Adapter",
		US_SC_SCSI, US_PR_CB, US_FL_SINGLE_LUN}, 
	{ 0x054c, 0x0010, 0x0210, 0x0210, "Sony DSC-S30/S70", 
		US_SC_SCSI, US_PR_CB, US_FL_SINGLE_LUN | US_FL_START_STOP |
		US_FL_MODE_XLATE | US_FL_ALT_LENGTH | US_FL_ALT_LENGTH},
	{ 0x054c, 0x002d, 0x0100, 0x0100, "Sony Memorystick MSAC-US1",
		US_SC_SCSI, US_PR_CB, US_FL_SINGLE_LUN | US_FL_START_STOP |
		US_FL_MODE_XLATE | US_FL_ALT_LENGTH},
	{ 0x057b, 0x0000, 0x0000, 0x0299, "Y-E Data Flashbuster-U",
		US_SC_UFI,  US_PR_CB, US_FL_SINGLE_LUN},
	{ 0x057b, 0x0000, 0x0300, 0x9999, "Y-E Data Flashbuster-U",
		US_SC_UFI,  US_PR_CBI, US_FL_SINGLE_LUN},
	{ 0x0693, 0x0002, 0x0100, 0x0100, "Hagiwara FlashGate SmartMedia",
		US_SC_SCSI, US_PR_BULK, US_FL_ALT_LENGTH},
	{ 0x0781, 0x0001, 0x0200, 0x0200, "Sandisk ImageMate (SDDR-05a)",
		US_SC_SCSI, US_PR_CB, US_FL_SINGLE_LUN | US_FL_START_STOP},
#ifdef CONFIG_USB_STORAGE_SDDR09
	{ 0x0781, 0x0200, 0x0100, 0x0100, "Sandisk ImageMate (SDDR-09)",
		US_SC_SCSI, US_PR_EUSB_SDDR09,
		US_FL_SINGLE_LUN | US_FL_START_STOP},
#endif
	{ 0x0781, 0x0002, 0x0009, 0x0009, "Sandisk Imagemate (SDDR-31)",
		US_SC_SCSI, US_PR_BULK, US_FL_IGNORE_SER},
	{ 0x07af, 0x0004, 0x0100, 0x0100, "Microtech USB-SCSI-DB25",
		US_SC_SCSI, US_PR_BULK, US_FL_ALT_LENGTH}, 
	{ 0x07af, 0x0005, 0x0100, 0x0100, "Microtech USB-SCSI-HD50",
		US_SC_SCSI, US_PR_BULK, US_FL_ALT_LENGTH}, 
	{ 0x05ab, 0x0031, 0x0100, 0x0100, "In-System USB/IDE Bridge",
		US_SC_8070, US_PR_BULK, US_FL_ALT_LENGTH}, 
	{ 0x0693, 0x0005, 0x0100, 0x0100, "Hagiwara Flashgate",
		US_SC_SCSI, US_PR_BULK, US_FL_ALT_LENGTH}, 
	{ 0 }};

/* Search our ususual device list, based on vendor/product combinations
 * to see if we can support this device.  Returns a pointer to a structure
 * defining how we should support this device, or NULL if it's not in the
 * list
 */
static struct us_unusual_dev* us_find_dev(u16 idVendor, u16 idProduct, 
					  u16 bcdDevice)
{
	struct us_unusual_dev* ptr;

	US_DEBUGP("Searching unusual device list for (0x%x, 0x%x, 0x%x)...\n",
		  idVendor, idProduct, bcdDevice);

	ptr = us_unusual_dev_list;
	while ((ptr->idVendor != 0x0000) && 
	       !((ptr->idVendor == idVendor) && 
		 (ptr->idProduct == idProduct) &&
		 (ptr->bcdDeviceMin <= bcdDevice) &&
		 (ptr->bcdDeviceMax >= bcdDevice)))
		ptr++;
	
	/* if the search ended because we hit the end record, we failed */
	if (ptr->idVendor == 0x0000) {
		US_DEBUGP("-- did not find a matching device\n");
		return NULL;
	}

	/* otherwise, we found one! */
	US_DEBUGP("-- found matching device: %s\n", ptr->name);
	return ptr;
}

/* Set up the IRQ pipe and handler
 * Note that this function assumes that all the data in the us_data
 * strucuture is current.  This includes the ep_int field, which gives us
 * the endpoint for the interrupt.
 * Returns non-zero on failure, zero on success
 */ 
static int usb_stor_allocate_irq(struct us_data *ss)
{
	unsigned int pipe;
	int maxp;
	int result;

	US_DEBUGP("Allocating IRQ for CBI transport\n");
	
	/* lock access to the data structure */
	down(&(ss->irq_urb_sem));

	/* allocate the URB */
	ss->irq_urb = usb_alloc_urb(0);
	if (!ss->irq_urb) {
		up(&(ss->irq_urb_sem));
		US_DEBUGP("couldn't allocate interrupt URB");
		return 1;
	}
	
	/* calculate the pipe and max packet size */
	pipe = usb_rcvintpipe(ss->pusb_dev, ss->ep_int->bEndpointAddress & 
			      USB_ENDPOINT_NUMBER_MASK);
	maxp = usb_maxpacket(ss->pusb_dev, pipe, usb_pipeout(pipe));
	if (maxp > sizeof(ss->irqbuf))
		maxp = sizeof(ss->irqbuf);
	
	/* fill in the URB with our data */
	FILL_INT_URB(ss->irq_urb, ss->pusb_dev, pipe, ss->irqbuf, maxp, 
		     usb_stor_CBI_irq, ss, ss->ep_int->bInterval); 
	
	/* submit the URB for processing */
	result = usb_submit_urb(ss->irq_urb);
	US_DEBUGP("usb_submit_urb() returns %d\n", result);
	if (result) {
		usb_free_urb(ss->irq_urb);
		up(&(ss->irq_urb_sem));
		return 2;
	}

	/* unlock the data structure and return success */
	up(&(ss->irq_urb_sem));
	return 0;
}

/* Probe to see if a new device is actually a SCSI device */
static void * storage_probe(struct usb_device *dev, unsigned int ifnum)
{
	int i;
	char mf[USB_STOR_STRING_LEN];		     /* manufacturer */
	char prod[USB_STOR_STRING_LEN];		     /* product */
	char serial[USB_STOR_STRING_LEN];	     /* serial number */
	GUID(guid);			   /* Global Unique Identifier */
	unsigned int flags;
	struct us_unusual_dev *unusual_dev;
	struct us_data *ss = NULL;
	int result;

	/* these are temporary copies -- we test on these, then put them
	 * in the us-data structure 
	 */
	struct usb_endpoint_descriptor *ep_in = NULL;
	struct usb_endpoint_descriptor *ep_out = NULL;
	struct usb_endpoint_descriptor *ep_int = NULL;
	u8 subclass = 0;
	u8 protocol = 0;

	/* the altsettting 0 on the interface we're probing */
	struct usb_interface_descriptor *altsetting = 
		&(dev->actconfig->interface[ifnum].altsetting[0]); 

	/* clear the temporary strings */
	memset(mf, 0, sizeof(mf));
	memset(prod, 0, sizeof(prod));
	memset(serial, 0, sizeof(serial));

	/* search for this device in our unusual device list */
	unusual_dev = us_find_dev(dev->descriptor.idVendor, 
				  dev->descriptor.idProduct,
				  dev->descriptor.bcdDevice);

	/* 
	 * Can we support this device, either because we know about it
	 * from our unusual device list, or because it advertises that it's
	 * compliant to the specification?
	 */
	if (!unusual_dev &&
	    !(dev->descriptor.bDeviceClass == 0 &&
	      altsetting->bInterfaceClass == USB_CLASS_MASS_STORAGE &&
	      altsetting->bInterfaceSubClass >= US_SC_MIN &&
	      altsetting->bInterfaceSubClass <= US_SC_MAX)) {
		/* if it's not a mass storage, we go no further */
		return NULL;
	}

	/* At this point, we know we've got a live one */
	US_DEBUGP("USB Mass Storage device detected\n");

	/* Determine subclass and protocol, or copy from the interface */
	if (unusual_dev) {
		subclass = unusual_dev->useProtocol;
		protocol = unusual_dev->useTransport;
		flags = unusual_dev->flags;
	} else {
		subclass = altsetting->bInterfaceSubClass;
		protocol = altsetting->bInterfaceProtocol;
		flags = 0;
	}
	
	/*
	 * Find the endpoints we need
	 * We are expecting a minimum of 2 endpoints - in and out (bulk).
	 * An optional interrupt is OK (necessary for CBI protocol).
	 * We will ignore any others.
	 */
	for (i = 0; i < altsetting->bNumEndpoints; i++) {
		/* is it an BULK endpoint? */
		if ((altsetting->endpoint[i].bmAttributes & 
		     USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK) {
			/* BULK in or out? */
			if (altsetting->endpoint[i].bEndpointAddress & 
			    USB_DIR_IN)
				ep_in = &altsetting->endpoint[i];
			else
				ep_out = &altsetting->endpoint[i];
		}

		/* is it an interrupt endpoint? */
		if ((altsetting->endpoint[i].bmAttributes & 
		     USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_INT) {
			ep_int = &altsetting->endpoint[i];
		}
	}
	US_DEBUGP("Endpoints: In: 0x%p Out: 0x%p Int: 0x%p (Period %d)\n",
		  ep_in, ep_out, ep_int, ep_int ? ep_int->bInterval : 0);

	/* set the interface -- STALL is an acceptable response here */
#ifdef CONFIG_USB_STORAGE_SDDR09
	if (protocol != US_PR_EUSB_SDDR09)
		result = usb_set_interface(dev, 
			altsetting->bInterfaceNumber, 0);
	else
		result = usb_set_configuration(dev, 1);
#else
	result = usb_set_interface(dev, altsetting->bInterfaceNumber, 0);
#endif
	US_DEBUGP("Result from usb_set_interface is %d\n", result);
	if (result == -EPIPE) {
		US_DEBUGP("-- clearing stall on control interface\n");
		usb_clear_halt(dev, usb_sndctrlpipe(dev, 0));
	} else if (result != 0) {
		/* it's not a stall, but another error -- time to bail */
		US_DEBUGP("-- Unknown error.  Rejecting device\n");
		return NULL;
	}

	/* Do some basic sanity checks, and bail if we find a problem */
	if (!ep_in || !ep_out || (protocol == US_PR_CBI && !ep_int)) {
		US_DEBUGP("Sanity check failed.	 Rejecting device.\n");
		return NULL;
	}

	/* At this point, we're committed to using the device */

	/* clear the GUID and fetch the strings */
	GUID_CLEAR(guid);
	if (dev->descriptor.iManufacturer)
		usb_string(dev, dev->descriptor.iManufacturer, 
			   mf, sizeof(mf));
	if (dev->descriptor.iProduct)
		usb_string(dev, dev->descriptor.iProduct, 
			   prod, sizeof(prod));
	if (dev->descriptor.iSerialNumber && !(flags & US_FL_IGNORE_SER))
		usb_string(dev, dev->descriptor.iSerialNumber, 
			   serial, sizeof(serial));
	
	/* Create a GUID for this device */
	if (dev->descriptor.iSerialNumber && serial[0]) {
		/* If we have a serial number, and it's a non-NULL string */
		make_guid(guid, dev->descriptor.idVendor, 
			  dev->descriptor.idProduct, serial);
	} else {
		/* We don't have a serial number, so we use 0 */
		make_guid(guid, dev->descriptor.idVendor, 
			  dev->descriptor.idProduct, "0");
	}

	/* lock access to the data structures */
	down(&us_list_semaphore);

	/*
	 * Now check if we have seen this GUID before
	 * We're looking for a device with a matching GUID that isn't
	 * allready on the system
	 */
	ss = us_list;
	while ((ss != NULL) && 
	       ((ss->pusb_dev) || !GUID_EQUAL(guid, ss->guid)))
		ss = ss->next;

	if (ss != NULL) {
		/* Existing device -- re-connect */
		US_DEBUGP("Found existing GUID " GUID_FORMAT "\n",
			  GUID_ARGS(guid));

		/* establish the connection to the new device upon reconnect */
		ss->ifnum = ifnum;
		ss->pusb_dev = dev;
	
		/* copy over the endpoint data */
		if (ep_in)
			ss->ep_in = ep_in->bEndpointAddress & 
				USB_ENDPOINT_NUMBER_MASK;
		if (ep_out)
			ss->ep_out = ep_out->bEndpointAddress & 
				USB_ENDPOINT_NUMBER_MASK;
		ss->ep_int = ep_int;

                /* Reset the device's NEED_INIT flag if it needs to be
                   initialized with a magic sequence */

                if (flags & US_FL_NEED_INIT)
                        ss->flags |= US_FL_NEED_INIT;

		/* allocate an IRQ callback if one is needed */
		if ((ss->protocol == US_PR_CBI) && usb_stor_allocate_irq(ss))
			return NULL;
	} else { 
		/* New device -- allocate memory and initialize */
		US_DEBUGP("New GUID " GUID_FORMAT "\n", GUID_ARGS(guid));
	
		if ((ss = (struct us_data *)kmalloc(sizeof(struct us_data), 
						    GFP_KERNEL)) == NULL) {
			printk(KERN_WARNING USB_STORAGE "Out of memory\n");
			up(&us_list_semaphore);
			return NULL;
		}
		memset(ss, 0, sizeof(struct us_data));

		/* allocate the URB we're going to use */
		ss->current_urb = usb_alloc_urb(0);
		if (!ss->current_urb) {
			kfree(ss);
			return NULL;
		}

		/* Initialize the mutexes only when the struct is new */
		init_MUTEX_LOCKED(&(ss->notify));
		init_MUTEX_LOCKED(&(ss->ip_waitq));
		init_MUTEX(&(ss->queue_exclusion));
		init_MUTEX(&(ss->irq_urb_sem));
		init_MUTEX(&(ss->current_urb_sem));
		init_MUTEX(&(ss->dev_semaphore));

		/* copy over the subclass and protocol data */
		ss->subclass = subclass;
		ss->protocol = protocol;
		ss->flags = flags;

		/* copy over the endpoint data */
		if (ep_in)
			ss->ep_in = ep_in->bEndpointAddress & 
				USB_ENDPOINT_NUMBER_MASK;
		if (ep_out)
			ss->ep_out = ep_out->bEndpointAddress & 
				USB_ENDPOINT_NUMBER_MASK;
		ss->ep_int = ep_int;

		/* establish the connection to the new device */
		ss->ifnum = ifnum;
		ss->pusb_dev = dev;

		/* copy over the identifiying strings */
		strncpy(ss->vendor, mf, USB_STOR_STRING_LEN);
		strncpy(ss->product, prod, USB_STOR_STRING_LEN);
		strncpy(ss->serial, serial, USB_STOR_STRING_LEN);
		if (strlen(ss->vendor) == 0)
			strncpy(ss->vendor, "Unknown", USB_STOR_STRING_LEN);
		if (strlen(ss->product) == 0)
			strncpy(ss->product, "Unknown", USB_STOR_STRING_LEN);
		if (strlen(ss->serial) == 0)
			strncpy(ss->serial, "None", USB_STOR_STRING_LEN);

		/* copy the GUID we created before */
		memcpy(ss->guid, guid, sizeof(guid));
		
		/* 
		 * Set the handler pointers based on the protocol
		 * Again, this data is persistant across reattachments
		 */
		switch (ss->protocol) {
		case US_PR_CB:
			ss->transport_name = "Control/Bulk";
			ss->transport = usb_stor_CB_transport;
			ss->transport_reset = usb_stor_CB_reset;
			ss->max_lun = 7;
			break;
			
		case US_PR_CBI:
			ss->transport_name = "Control/Bulk/Interrupt";
			ss->transport = usb_stor_CBI_transport;
			ss->transport_reset = usb_stor_CB_reset;
			ss->max_lun = 7;
			break;
			
		case US_PR_BULK:
			ss->transport_name = "Bulk";
			ss->transport = usb_stor_Bulk_transport;
			ss->transport_reset = usb_stor_Bulk_reset;
			ss->max_lun = usb_stor_Bulk_max_lun(ss);
			break;
			
#ifdef CONFIG_USB_STORAGE_HP8200e
		case US_PR_SCM_ATAPI:
			ss->transport_name = "SCM/ATAPI";
			ss->transport = hp8200e_transport;
			ss->transport_reset = usb_stor_CB_reset;
			ss->max_lun = 1;
			break;
#endif

#ifdef CONFIG_USB_STORAGE_SDDR09
		case US_PR_EUSB_SDDR09:
			ss->transport_name = "EUSB/SDDR09";
			ss->transport = sddr09_transport;
			ss->transport_reset = usb_stor_CB_reset;
			ss->max_lun = 1;
			break;
#endif
			
		default:
			ss->transport_name = "Unknown";
			up(&us_list_semaphore);
			kfree(ss->current_urb);
			kfree(ss);
			return NULL;
			break;
		}
		US_DEBUGP("Transport: %s\n", ss->transport_name);

		/* fix for single-lun devices */
		if (ss->flags & US_FL_SINGLE_LUN)
			ss->max_lun = 0;

		switch (ss->subclass) {
		case US_SC_RBC:
			ss->protocol_name = "Reduced Block Commands (RBC)";
			ss->proto_handler = usb_stor_transparent_scsi_command;
			break;

		case US_SC_8020:
			ss->protocol_name = "8020i";
			ss->proto_handler = usb_stor_ATAPI_command;
			break;

		case US_SC_QIC:
			ss->protocol_name = "QIC-157";
			ss->proto_handler = usb_stor_qic157_command;
			break;

		case US_SC_8070:
			ss->protocol_name = "8070i";
			ss->proto_handler = usb_stor_ATAPI_command;
			break;

		case US_SC_SCSI:
			ss->protocol_name = "Transparent SCSI";
			ss->proto_handler = usb_stor_transparent_scsi_command;
			break;

		case US_SC_UFI:
			ss->protocol_name = "Uniform Floppy Interface (UFI)";
			ss->proto_handler = usb_stor_ufi_command;
			break;

		default:
			ss->protocol_name = "Unknown";
			up(&us_list_semaphore);
			kfree(ss->current_urb);
			kfree(ss);
			return NULL;
			break;
		}
		US_DEBUGP("Protocol: %s\n", ss->protocol_name);

		/* allocate an IRQ callback if one is needed */
		if ((ss->protocol == US_PR_CBI) && usb_stor_allocate_irq(ss))
			return NULL;
		
		/*
		 * Since this is a new device, we need to generate a scsi 
		 * host definition, and register with the higher SCSI layers
		 */

		/* Initialize the host template based on the default one */
		memcpy(&(ss->htmplt), &usb_stor_host_template, 
		       sizeof(usb_stor_host_template));

		/* Grab the next host number */
		ss->host_number = my_host_number++;
			
		/* We abuse this pointer so we can pass the ss pointer to 
		 * the host controler thread in us_detect.  But how else are
		 * we to do it?
		 */
		(struct us_data *)ss->htmplt.proc_dir = ss; 
		
		/* start up our control thread */
		ss->pid = kernel_thread(usb_stor_control_thread, ss,
					CLONE_VM);
		if (ss->pid < 0) {
			printk(KERN_WARNING USB_STORAGE 
			       "Unable to start control thread\n");
			kfree(ss->current_urb);
			kfree(ss);
			return NULL;
		}
		
		/* wait for the thread to start */
		down(&(ss->notify));
			
		/* now register	 - our detect function will be called */
		ss->htmplt.module = THIS_MODULE;
		scsi_register_module(MODULE_SCSI_HA, &(ss->htmplt));
		
		/* put us in the list */
		ss->next = us_list;
		us_list = ss;
	}

	/* release the data structure lock */
	up(&us_list_semaphore);

	printk(KERN_DEBUG 
	       "WARNING: USB Mass Storage data integrity not assured\n");
	printk(KERN_DEBUG 
	       "USB Mass Storage device found at %d\n", dev->devnum);

	/* return a pointer for the disconnect function */
	return ss;
}

/* Handle a disconnect event from the USB core */
static void storage_disconnect(struct usb_device *dev, void *ptr)
{
	struct us_data *ss = ptr;
	int result;

	US_DEBUGP("storage_disconnect() called\n");

	/* this is the odd case -- we disconnected but weren't using it */
	if (!ss) {
		US_DEBUGP("-- device was not in use\n");
		return;
	}

	/* lock access to the device data structure */
	down(&(ss->dev_semaphore));

	/* release the IRQ, if we have one */
	down(&(ss->irq_urb_sem));
	if (ss->irq_urb) {
		US_DEBUGP("-- releasing irq handle\n");
		result = usb_unlink_urb(ss->irq_urb);
		ss->irq_urb = NULL;
		US_DEBUGP("-- usb_unlink_urb() returned %d\n", result);
		usb_free_urb(ss->irq_urb);
	}
	up(&(ss->irq_urb_sem));

	/* mark the device as gone */
	ss->pusb_dev = NULL;

	/* lock access to the device data structure */
	up(&(ss->dev_semaphore));
}

/***********************************************************************
 * Initialization and registration
 ***********************************************************************/

int __init usb_stor_init(void)
{
	/* initialize internal global data elements */
	us_list = NULL;
	init_MUTEX(&us_list_semaphore);
	my_host_number = 0;

	/* register the driver, return -1 if error */
	if (usb_register(&storage_driver) < 0)
		return -1;

	/* we're all set */
	printk(KERN_INFO "USB Mass Storage support registered.\n");
	return 0;
}

void __exit usb_stor_exit(void)
{
	struct us_data *next;
	
	US_DEBUGP("usb_stor_exit() called\n");

	/* Deregister the driver
	 * This eliminates races with probes and disconnects 
	 */
	US_DEBUGP("-- calling usb_deregister()\n");
	usb_deregister(&storage_driver) ;
	
	/* lock access to the data structures */
	down(&us_list_semaphore);

	/* While there are still virtual hosts, unregister them
	 *
	 * Note that the us_release() routine will destroy the local data
	 * structure.  So we have to peel these off the top of the list
	 * and keep updating the head pointer as we go.
	 */
	while (us_list) {
		/* keep track of where the next one is */
		next = us_list->next;

		US_DEBUGP("-- calling scsi_unregister_module()\n");
		scsi_unregister_module(MODULE_SCSI_HA, &(us_list->htmplt));

                /* Now that scsi_unregister_module is done with the host
                 * template, we can free the us_data structure (the host
                 * template is inline in this structure). */

		/* If there's extra data in the us_data structure then
		 * free that first */

		if (us_list->extra) {
			if (us_list->extra_destructor)
				(*us_list->extra_destructor)(
					us_list->extra);
			kfree(us_list->extra);
		}
                kfree (us_list);

		/* advance the list pointer */
		us_list = next;
	}
	
	/* unlock the data structures */
	up(&us_list_semaphore);
}

module_init(usb_stor_init) ;
module_exit(usb_stor_exit) ;

MODULE_AUTHOR("Michael Gee <michael@linuxspecific.com>, David L. Brown, Jr. <usb-storage@davidb.org>, Matthew Dharm <mdharm-usb@one-eyed-alien.net>");
MODULE_DESCRIPTION("USB Mass Storage driver");
