/* Driver for USB Mass Storage compliant devices
 *
 * $Id: usb.c,v 1.75 2002/04/22 03:39:43 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
 *
 * Initial work by:
 *   (c) 1999 Michael Gee (michael@linuxspecific.com)
 *
 * usb_device_id support by Adam J. Richter (adam@yggdrasil.com):
 *   (c) 2000 Yggdrasil Computing, Inc.
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
#include <linux/sched.h>
#include <linux/errno.h>
#include "usb.h"
#include "scsiglue.h"
#include "transport.h"
#include "protocol.h"
#include "debug.h"
#include "initializers.h"

#ifdef CONFIG_USB_STORAGE_HP8200e
#include "shuttle_usbat.h"
#endif
#ifdef CONFIG_USB_STORAGE_SDDR09
#include "sddr09.h"
#endif
#ifdef CONFIG_USB_STORAGE_SDDR55
#include "sddr55.h"
#endif
#ifdef CONFIG_USB_STORAGE_DPCM
#include "dpcm.h"
#endif
#ifdef CONFIG_USB_STORAGE_FREECOM
#include "freecom.h"
#endif
#ifdef CONFIG_USB_STORAGE_ISD200
#include "isd200.h"
#endif
#ifdef CONFIG_USB_STORAGE_DATAFAB
#include "datafab.h"
#endif
#ifdef CONFIG_USB_STORAGE_JUMPSHOT
#include "jumpshot.h"
#endif


#include <linux/module.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>

/* Some informational data */
MODULE_AUTHOR("Matthew Dharm <mdharm-usb@one-eyed-alien.net>");
MODULE_DESCRIPTION("USB Mass Storage driver for Linux");
MODULE_LICENSE("GPL");

/*
 * Per device data
 */

static int my_host_number;

/* The list of structures and the protective lock for them */
struct us_data *us_list;
struct semaphore us_list_semaphore;

static int storage_probe(struct usb_interface *iface,
			 const struct usb_device_id *id);

static void storage_disconnect(struct usb_interface *iface);

/* The entries in this table, except for final ones here
 * (USB_MASS_STORAGE_CLASS and the empty entry), correspond,
 * line for line with the entries of us_unsuaul_dev_list[].
 * For now, we duplicate idVendor and idProduct in us_unsual_dev_list,
 * just to avoid alignment bugs.
 */

#define UNUSUAL_DEV(id_vendor, id_product, bcdDeviceMin, bcdDeviceMax, \
		    vendorName, productName,useProtocol, useTransport, \
		    initFunction, flags) \
{ USB_DEVICE_VER(id_vendor, id_product, bcdDeviceMin,bcdDeviceMax) }

static struct usb_device_id storage_usb_ids [] = {

#	include "unusual_devs.h"
#undef UNUSUAL_DEV
	/* Control/Bulk transport for all SubClass values */
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_RBC, US_PR_CB) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8020, US_PR_CB) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_QIC, US_PR_CB) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_UFI, US_PR_CB) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8070, US_PR_CB) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_SCSI, US_PR_CB) },

	/* Control/Bulk/Interrupt transport for all SubClass values */
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_RBC, US_PR_CBI) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8020, US_PR_CBI) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_QIC, US_PR_CBI) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_UFI, US_PR_CBI) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8070, US_PR_CBI) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_SCSI, US_PR_CBI) },

	/* Bulk-only transport for all SubClass values */
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_RBC, US_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8020, US_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_QIC, US_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_UFI, US_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_8070, US_PR_BULK) },
	{ USB_INTERFACE_INFO(USB_CLASS_MASS_STORAGE, US_SC_SCSI, US_PR_BULK) },

	/* Terminating entry */
	{ }
};

MODULE_DEVICE_TABLE (usb, storage_usb_ids);

/* This is the list of devices we recognize, along with their flag data */

/* The vendor name should be kept at eight characters or less, and
 * the product name should be kept at 16 characters or less. If a device
 * has the US_FL_FIX_INQUIRY flag, then the vendor and product names
 * normally generated by a device thorugh the INQUIRY response will be
 * taken from this list, and this is the reason for the above size
 * restriction. However, if the flag is not present, then you
 * are free to use as many characters as you like.
 */

#undef UNUSUAL_DEV
#define UNUSUAL_DEV(idVendor, idProduct, bcdDeviceMin, bcdDeviceMax, \
		    vendor_name, product_name, use_protocol, use_transport, \
		    init_function, Flags) \
{ \
	.vendorName = vendor_name,	\
	.productName = product_name,	\
	.useProtocol = use_protocol,	\
	.useTransport = use_transport,	\
	.initFunction = init_function,	\
	.flags = Flags, \
}

static struct us_unusual_dev us_unusual_dev_list[] = {
#	include "unusual_devs.h" 
#	undef UNUSUAL_DEV
	/* Control/Bulk transport for all SubClass values */
	{ .useProtocol = US_SC_RBC,
	  .useTransport = US_PR_CB},
	{ .useProtocol = US_SC_8020,
	  .useTransport = US_PR_CB},
	{ .useProtocol = US_SC_QIC,
	  .useTransport = US_PR_CB},
	{ .useProtocol = US_SC_UFI,
	  .useTransport = US_PR_CB},
	{ .useProtocol = US_SC_8070,
	  .useTransport = US_PR_CB},
	{ .useProtocol = US_SC_SCSI,
	  .useTransport = US_PR_CB},

	/* Control/Bulk/Interrupt transport for all SubClass values */
	{ .useProtocol = US_SC_RBC,
	  .useTransport = US_PR_CBI},
	{ .useProtocol = US_SC_8020,
	  .useTransport = US_PR_CBI},
	{ .useProtocol = US_SC_QIC,
	  .useTransport = US_PR_CBI},
	{ .useProtocol = US_SC_UFI,
	  .useTransport = US_PR_CBI},
	{ .useProtocol = US_SC_8070,
	  .useTransport = US_PR_CBI},
	{ .useProtocol = US_SC_SCSI,
	  .useTransport = US_PR_CBI},

	/* Bulk-only transport for all SubClass values */
	{ .useProtocol = US_SC_RBC,
	  .useTransport = US_PR_BULK},
	{ .useProtocol = US_SC_8020,
	  .useTransport = US_PR_BULK},
	{ .useProtocol = US_SC_QIC,
	  .useTransport = US_PR_BULK},
	{ .useProtocol = US_SC_UFI,
	  .useTransport = US_PR_BULK},
	{ .useProtocol = US_SC_8070,
	  .useTransport = US_PR_BULK},
	{ .useProtocol = US_SC_SCSI,
	  .useTransport = US_PR_BULK},

	/* Terminating entry */
	{ 0 }
};

struct usb_driver usb_storage_driver = {
	.name =		"usb-storage",
	.probe =	storage_probe,
	.disconnect =	storage_disconnect,
	.id_table =	storage_usb_ids,
};

/*
 * fill_inquiry_response takes an unsigned char array (which must
 * be at least 36 characters) and populates the vendor name,
 * product name, and revision fields. Then the array is copied
 * into the SCSI command's response buffer (oddly enough
 * called request_buffer). data_len contains the length of the
 * data array, which again must be at least 36.
 */

void fill_inquiry_response(struct us_data *us, unsigned char *data,
		unsigned int data_len) {

	int i;
	struct scatterlist *sg;
	int len =
		us->srb->request_bufflen > data_len ? data_len :
		us->srb->request_bufflen;
	int transferred;
	int amt;

	if (data_len<36) // You lose.
		return;

	if(data[0]&0x20) { /* USB device currently not connected. Return
			      peripheral qualifier 001b ("...however, the
			      physical device is not currently connected
			      to this logical unit") and leave vendor and
			      product identification empty. ("If the target
			      does store some of the INQUIRY data on the
			      device, it may return zeros or ASCII spaces 
			      (20h) in those fields until the data is
			      available from the device."). */
		memset(data+8,0,28);
	} else {
		memcpy(data+8, us->unusual_dev->vendorName, 
			strlen(us->unusual_dev->vendorName) > 8 ? 8 :
			strlen(us->unusual_dev->vendorName));
		memcpy(data+16, us->unusual_dev->productName, 
			strlen(us->unusual_dev->productName) > 16 ? 16 :
			strlen(us->unusual_dev->productName));
		data[32] = 0x30 + ((us->pusb_dev->descriptor.bcdDevice>>12) & 0x0F);
		data[33] = 0x30 + ((us->pusb_dev->descriptor.bcdDevice>>8) & 0x0F);
		data[34] = 0x30 + ((us->pusb_dev->descriptor.bcdDevice>>4) & 0x0F);
		data[35] = 0x30 + ((us->pusb_dev->descriptor.bcdDevice) & 0x0F);
	}

	if (us->srb->use_sg) {
		sg = (struct scatterlist *)us->srb->request_buffer;
		for (i=0; i<us->srb->use_sg; i++)
			memset(sg_address(sg[i]), 0, sg[i].length);
		for (i=0, transferred=0; 
				i<us->srb->use_sg && transferred < len;
				i++) {
			amt = sg[i].length > len-transferred ? 
					len-transferred : sg[i].length;
			memcpy(sg_address(sg[i]), data+transferred, amt);
			transferred -= amt;
		}
	} else {
		memset(us->srb->request_buffer, 0, us->srb->request_bufflen);
		memcpy(us->srb->request_buffer, data, len);
	}
}

static int usb_stor_control_thread(void * __us)
{
	struct us_data *us = (struct us_data *)__us;

	lock_kernel();

	/*
	 * This thread doesn't need any user-level access,
	 * so get rid of all our resources..
	 */
	daemonize();

	/* avoid getting signals */
	spin_lock_irq(&current->sig->siglock);
	flush_signals(current);
	current->flags |= PF_IOTHREAD;
	sigfillset(&current->blocked);
	recalc_sigpending();
	spin_unlock_irq(&current->sig->siglock);

	/* set our name for identification purposes */
	sprintf(current->comm, "usb-storage-%d", us->host_number);

	unlock_kernel();

	/* set up for wakeups by new commands */
	init_MUTEX_LOCKED(&us->sema);

	/* signal that we've started the thread */
	complete(&(us->notify));

	for(;;) {
		struct Scsi_Host *host;
		US_DEBUGP("*** thread sleeping.\n");
		if(down_interruptible(&us->sema))
			break;
			
		US_DEBUGP("*** thread awakened.\n");

		/* if us->srb is NULL, we are being asked to exit */
		if (us->srb == NULL) {
			US_DEBUGP("-- exit command received\n");
			break;
		}
		host = us->srb->device->host;

		/* lock access to the state */
		scsi_lock(host);

		/* has the command been aborted *already* ? */
		if (atomic_read(&us->sm_state) == US_STATE_ABORTING) {
			us->srb->result = DID_ABORT << 16;
			goto SkipForAbort;
		}

		/* set the state and release the lock */
		atomic_set(&us->sm_state, US_STATE_RUNNING);
		scsi_unlock(host);

		/* lock the device pointers */
		down(&(us->dev_semaphore));

		/* reject the command if the direction indicator 
		 * is UNKNOWN
		 */
		if (us->srb->sc_data_direction == SCSI_DATA_UNKNOWN) {
			US_DEBUGP("UNKNOWN data direction\n");
			us->srb->result = DID_ERROR << 16;
		}

		/* reject if target != 0 or if LUN is higher than
		 * the maximum known LUN
		 */
		else if (us->srb->device->id && 
				!(us->flags & US_FL_SCM_MULT_TARG)) {
			US_DEBUGP("Bad target number (%d/%d)\n",
				  us->srb->device->id, us->srb->device->lun);
			us->srb->result = DID_BAD_TARGET << 16;
		}

		else if (us->srb->device->lun > us->max_lun) {
			US_DEBUGP("Bad LUN (%d/%d)\n",
				  us->srb->device->id, us->srb->device->lun);
			us->srb->result = DID_BAD_TARGET << 16;
		}

		/* handle requests for EVPD, which most USB devices do
		 * not support */
		else if((us->srb->cmnd[0] == INQUIRY) &&
				(us->srb->cmnd[1] & 0x1)) {
				US_DEBUGP("Faking INQUIRY command for EVPD\n");
				memcpy(us->srb->sense_buffer, 
				       usb_stor_sense_invalidCDB, 
				       sizeof(usb_stor_sense_invalidCDB));
				us->srb->result = CHECK_CONDITION << 1;
		}

		/* our device has gone - pretend not ready */
		else if (!(us->flags & US_FL_DEV_ATTACHED)) {
			US_DEBUGP("Request is for removed device\n");
			/* For REQUEST_SENSE, it's the data.  But
			 * for anything else, it should look like
			 * we auto-sensed for it.
			 */
			if (us->srb->cmnd[0] == REQUEST_SENSE) {
				memcpy(us->srb->request_buffer, 
				       usb_stor_sense_notready, 
				       sizeof(usb_stor_sense_notready));
				us->srb->result = GOOD << 1;
			} else if(us->srb->cmnd[0] == INQUIRY) {
				/* INQUIRY should always work, per spec... */
				unsigned char data_ptr[36] = {
				    0x20, 0x80, 0x02, 0x02,
				    0x1F, 0x00, 0x00, 0x00};
				US_DEBUGP("Faking INQUIRY command for disconnected device\n");
				fill_inquiry_response(us, data_ptr, 36);
				us->srb->result = GOOD << 1;
			} else {
				/* not ready */
				memcpy(us->srb->sense_buffer, 
				       usb_stor_sense_notready, 
				       sizeof(usb_stor_sense_notready));
				us->srb->result = CHECK_CONDITION << 1;
			}
		}  /* !(us->flags & US_FL_DEV_ATTACHED) */

		/* Handle those devices which need us to fake 
		 * their inquiry data */
		else if ((us->srb->cmnd[0] == INQUIRY) &&
			    (us->flags & US_FL_FIX_INQUIRY)) {
			unsigned char data_ptr[36] = {
			    0x00, 0x80, 0x02, 0x02,
			    0x1F, 0x00, 0x00, 0x00};

			US_DEBUGP("Faking INQUIRY command\n");
			fill_inquiry_response(us, data_ptr, 36);
			us->srb->result = GOOD << 1;
		}

		/* we've got a command, let's do it! */
		else {
			US_DEBUG(usb_stor_show_command(us->srb));
			us->proto_handler(us->srb, us);
		}

		/* unlock the device pointers */
		up(&(us->dev_semaphore));

		/* lock access to the state */
		scsi_lock(host);

		/* indicate that the command is done */
		if (us->srb->result != DID_ABORT << 16) {
			US_DEBUGP("scsi cmd done, result=0x%x\n", 
				   us->srb->result);
			us->srb->scsi_done(us->srb);
		} else {
			SkipForAbort:
			US_DEBUGP("scsi command aborted\n");
		}

		/* in case an abort request was received after the command
		 * completed, we must use a separate test to see whether
		 * we need to signal that the abort has finished */
		if (atomic_read(&us->sm_state) == US_STATE_ABORTING)
			complete(&(us->notify));

		/* empty the queue, reset the state, and release the lock */
		us->srb = NULL;
		atomic_set(&us->sm_state, US_STATE_IDLE);
		scsi_unlock(host);
	} /* for (;;) */

	/* notify the exit routine that we're actually exiting now */
	complete(&(us->notify));

	return 0;
}	

/* Set up the URB and the usb_ctrlrequest.
 * ss->dev_semaphore must already be locked.
 * Note that this function assumes that all the data in the us_data
 * structure is current.
 * Returns non-zero on failure, zero on success
 */ 
static int usb_stor_allocate_urbs(struct us_data *ss)
{
	/* calculate and store the pipe values */
	ss->send_ctrl_pipe = usb_sndctrlpipe(ss->pusb_dev, 0);
	ss->recv_ctrl_pipe = usb_rcvctrlpipe(ss->pusb_dev, 0);
	ss->send_bulk_pipe = usb_sndbulkpipe(ss->pusb_dev, ss->ep_out);
	ss->recv_bulk_pipe = usb_rcvbulkpipe(ss->pusb_dev, ss->ep_in);
	ss->recv_intr_pipe = usb_rcvintpipe(ss->pusb_dev, ss->ep_int);

	/* allocate the usb_ctrlrequest for control packets */
	US_DEBUGP("Allocating usb_ctrlrequest\n");
	ss->dr = kmalloc(sizeof(struct usb_ctrlrequest), GFP_NOIO);
	if (!ss->dr) {
		US_DEBUGP("allocation failed\n");
		return 1;
	}

	/* allocate the URB we're going to use */
	US_DEBUGP("Allocating URB\n");
	ss->current_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!ss->current_urb) {
		US_DEBUGP("allocation failed\n");
		return 2;
	}

	US_DEBUGP("Allocating scatter-gather request block\n");
	ss->current_sg = kmalloc(sizeof(*ss->current_sg), GFP_KERNEL);
	if (!ss->current_sg) {
		US_DEBUGP("allocation failed\n");
		return 5;
	}

	return 0;	/* success */
}

/* Deallocate the URB, the usb_ctrlrequest, and the IRQ pipe.
 * ss->dev_semaphore must already be locked.
 */
static void usb_stor_deallocate_urbs(struct us_data *ss)
{
	int result;

	/* free the scatter-gather request block */
	if (ss->current_sg) {
		kfree(ss->current_sg);
		ss->current_sg = NULL;
	}

	/* free up the main URB for this device */
	if (ss->current_urb) {
		US_DEBUGP("-- releasing main URB\n");
		result = usb_unlink_urb(ss->current_urb);
		US_DEBUGP("-- usb_unlink_urb() returned %d\n", result);
		usb_free_urb(ss->current_urb);
		ss->current_urb = NULL;
	}

	/* free the usb_ctrlrequest buffer */
	if (ss->dr) {
		kfree(ss->dr);
		ss->dr = NULL;
	}

	/* mark the device as gone */
	ss->flags &= ~ US_FL_DEV_ATTACHED;
	usb_put_dev(ss->pusb_dev);
	ss->pusb_dev = NULL;
}

/* Probe to see if a new device is actually a SCSI device */
static int storage_probe(struct usb_interface *intf,
			 const struct usb_device_id *id)
{
	struct usb_device *dev = interface_to_usbdev(intf);
	int ifnum = intf->altsetting->desc.bInterfaceNumber;
	int i;
	const int id_index = id - storage_usb_ids; 
	char mf[USB_STOR_STRING_LEN];		     /* manufacturer */
	char prod[USB_STOR_STRING_LEN];		     /* product */
	char serial[USB_STOR_STRING_LEN];	     /* serial number */
	GUID(guid);			   /* Global Unique Identifier */
	unsigned int flags;
	struct us_unusual_dev *unusual_dev;
	struct us_data *ss = NULL;
	int result;
	int new_device = 0;

	/* these are temporary copies -- we test on these, then put them
	 * in the us-data structure 
	 */
	struct usb_endpoint_descriptor *ep_in = NULL;
	struct usb_endpoint_descriptor *ep_out = NULL;
	struct usb_endpoint_descriptor *ep_int = NULL;
	u8 subclass = 0;
	u8 protocol = 0;

	/* the altsetting on the interface we're probing that matched our
	 * usb_match_id table
	 */
	struct usb_host_interface *altsetting =
		intf[ifnum].altsetting + intf[ifnum].act_altsetting;
	US_DEBUGP("act_altsetting is %d\n", intf[ifnum].act_altsetting);

	/* clear the temporary strings */
	memset(mf, 0, sizeof(mf));
	memset(prod, 0, sizeof(prod));
	memset(serial, 0, sizeof(serial));

	/* 
	 * Can we support this device, either because we know about it
	 * from our unusual device list, or because it advertises that it's
	 * compliant to the specification?
	 *
	 * id_index is calculated in the declaration to be the index number
	 * of the match from the usb_device_id table, so we can find the
	 * corresponding entry in the private table.
	 */
	US_DEBUGP("id_index calculated to be: %d\n", id_index);
	US_DEBUGP("Array length appears to be: %d\n", sizeof(us_unusual_dev_list) / sizeof(us_unusual_dev_list[0]));
	if (id_index <
	    sizeof(us_unusual_dev_list) / sizeof(us_unusual_dev_list[0])) {
		unusual_dev = &us_unusual_dev_list[id_index];
		if (unusual_dev->vendorName)
			US_DEBUGP("Vendor: %s\n", unusual_dev->vendorName);
		if (unusual_dev->productName)
			US_DEBUGP("Product: %s\n", unusual_dev->productName);
	} else
		/* no, we can't support it */
		return -EIO;

	/* At this point, we know we've got a live one */
	US_DEBUGP("USB Mass Storage device detected\n");

	/* Determine subclass and protocol, or copy from the interface */
	subclass = unusual_dev->useProtocol;
	protocol = unusual_dev->useTransport;
	flags = unusual_dev->flags;

	/*
	 * Find the endpoints we need
	 * We are expecting a minimum of 2 endpoints - in and out (bulk).
	 * An optional interrupt is OK (necessary for CBI protocol).
	 * We will ignore any others.
	 */
	for (i = 0; i < altsetting->desc.bNumEndpoints; i++) {
		struct usb_endpoint_descriptor *ep;

		ep = &altsetting->endpoint[i].desc;

		/* is it an BULK endpoint? */
		if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_BULK) {
			/* BULK in or out? */
			if (ep->bEndpointAddress & USB_DIR_IN)
				ep_in = ep;
			else
				ep_out = ep;
		}

		/* is it an interrupt endpoint? */
		else if ((ep->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK)
				== USB_ENDPOINT_XFER_INT) {
			ep_int = ep;
		}
	}
	US_DEBUGP("Endpoints: In: 0x%p Out: 0x%p Int: 0x%p (Period %d)\n",
		  ep_in, ep_out, ep_int, ep_int ? ep_int->bInterval : 0);

#ifdef CONFIG_USB_STORAGE_SDDR09
	if (protocol == US_PR_EUSB_SDDR09 || protocol == US_PR_DPCM_USB) {
		/* set the configuration -- STALL is an acceptable response here */
		result = usb_set_configuration(dev, 1);

		US_DEBUGP("Result from usb_set_configuration is %d\n", result);
		if (result == -EPIPE) {
			US_DEBUGP("-- stall on control interface\n");
		} else if (result != 0) {
			/* it's not a stall, but another error -- time to bail */
			US_DEBUGP("-- Unknown error.  Rejecting device\n");
			return -EIO;
		}
	}
#endif

	/* Do some basic sanity checks, and bail if we find a problem */
	if (!ep_in || !ep_out || (protocol == US_PR_CBI && !ep_int)) {
		US_DEBUGP("Endpoint sanity check failed! Rejecting dev.\n");
		return -EIO;
	}

	/* At this point, we've decided to try to use the device */
	usb_get_dev(dev);

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

	/*
	 * Now check if we have seen this GUID before
	 * We're looking for a device with a matching GUID that isn't
	 * already on the system
	 */
	ss = us_list;
	while ((ss != NULL) && 
	           ((ss->flags & US_FL_DEV_ATTACHED) ||
		    !GUID_EQUAL(guid, ss->guid)))
		ss = ss->next;

	if (ss != NULL) {
		/* Existing device -- re-connect */
		US_DEBUGP("Found existing GUID " GUID_FORMAT "\n",
			  GUID_ARGS(guid));

		/* lock the device pointers */
		down(&(ss->dev_semaphore));

		/* establish the connection to the new device upon reconnect */
		ss->ifnum = ifnum;
		ss->pusb_dev = dev;
		ss->flags |= US_FL_DEV_ATTACHED;

		/* copy over the endpoint data */
		ss->ep_in = ep_in->bEndpointAddress & 
			USB_ENDPOINT_NUMBER_MASK;
		ss->ep_out = ep_out->bEndpointAddress & 
			USB_ENDPOINT_NUMBER_MASK;
		if (ep_int) {
			ss->ep_int = ep_int->bEndpointAddress & 
				USB_ENDPOINT_NUMBER_MASK;
			ss->ep_bInterval = ep_int->bInterval;
		}
		else
			ss->ep_int = ss->ep_bInterval = 0;

		/* allocate the URB, the usb_ctrlrequest, and the IRQ URB */
		if (usb_stor_allocate_urbs(ss))
			goto BadDevice;

                /* Re-Initialize the device if it needs it */
		if (unusual_dev && unusual_dev->initFunction)
			(unusual_dev->initFunction)(ss);

		/* unlock the device pointers */
		up(&(ss->dev_semaphore));

	} else { 
		/* New device -- allocate memory and initialize */
		US_DEBUGP("New GUID " GUID_FORMAT "\n", GUID_ARGS(guid));

		if ((ss = (struct us_data *)kmalloc(sizeof(struct us_data), 
						    GFP_KERNEL)) == NULL) {
			printk(KERN_WARNING USB_STORAGE "Out of memory\n");
			usb_put_dev(dev);
			return -ENOMEM;
		}
		memset(ss, 0, sizeof(struct us_data));
		new_device = 1;

		/* Initialize the mutexes only when the struct is new */
		init_completion(&(ss->notify));
		init_MUTEX_LOCKED(&(ss->dev_semaphore));

		/* copy over the subclass and protocol data */
		ss->subclass = subclass;
		ss->protocol = protocol;
		ss->flags = flags | US_FL_DEV_ATTACHED;
		ss->unusual_dev = unusual_dev;

		/* copy over the endpoint data */
		ss->ep_in = ep_in->bEndpointAddress & 
			USB_ENDPOINT_NUMBER_MASK;
		ss->ep_out = ep_out->bEndpointAddress & 
			USB_ENDPOINT_NUMBER_MASK;
		if (ep_int) {
			ss->ep_int = ep_int->bEndpointAddress & 
				USB_ENDPOINT_NUMBER_MASK;
			ss->ep_bInterval = ep_int->bInterval;
		}
		else
			ss->ep_int = ss->ep_bInterval = 0;

		/* establish the connection to the new device */
		ss->ifnum = ifnum;
		ss->pusb_dev = dev;

		/* copy over the identifiying strings */
		strncpy(ss->vendor, mf, USB_STOR_STRING_LEN);
		strncpy(ss->product, prod, USB_STOR_STRING_LEN);
		strncpy(ss->serial, serial, USB_STOR_STRING_LEN);
		if (strlen(ss->vendor) == 0) {
			if (unusual_dev->vendorName)
				strncpy(ss->vendor, unusual_dev->vendorName,
					USB_STOR_STRING_LEN);
			else
				strncpy(ss->vendor, "Unknown",
					USB_STOR_STRING_LEN);
		}
		if (strlen(ss->product) == 0) {
			if (unusual_dev->productName)
				strncpy(ss->product, unusual_dev->productName,
					USB_STOR_STRING_LEN);
			else
				strncpy(ss->product, "Unknown",
					USB_STOR_STRING_LEN);
		}
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
			ss->max_lun = 0;
			break;
#endif

#ifdef CONFIG_USB_STORAGE_SDDR55
		case US_PR_SDDR55:
			ss->transport_name = "SDDR55";
			ss->transport = sddr55_transport;
			ss->transport_reset = sddr55_reset;
			ss->max_lun = 0;
			break;
#endif

#ifdef CONFIG_USB_STORAGE_DPCM
		case US_PR_DPCM_USB:
			ss->transport_name = "Control/Bulk-EUSB/SDDR09";
			ss->transport = dpcm_transport;
			ss->transport_reset = usb_stor_CB_reset;
			ss->max_lun = 1;
			break;
#endif

#ifdef CONFIG_USB_STORAGE_FREECOM
                case US_PR_FREECOM:
                        ss->transport_name = "Freecom";
                        ss->transport = freecom_transport;
                        ss->transport_reset = usb_stor_freecom_reset;
                        ss->max_lun = 0;
                        break;
#endif

#ifdef CONFIG_USB_STORAGE_DATAFAB
                case US_PR_DATAFAB:
                        ss->transport_name  = "Datafab Bulk-Only";
                        ss->transport = datafab_transport;
                        ss->transport_reset = usb_stor_Bulk_reset;
                        ss->max_lun = 1;
                        break;
#endif

#ifdef CONFIG_USB_STORAGE_JUMPSHOT
                case US_PR_JUMPSHOT:
                        ss->transport_name  = "Lexar Jumpshot Control/Bulk";
                        ss->transport = jumpshot_transport;
                        ss->transport_reset = usb_stor_Bulk_reset;
                        ss->max_lun = 1;
                        break;
#endif

		default:
			/* ss->transport_name = "Unknown"; */
			goto BadDevice;
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
			ss->max_lun = 0;
			break;

		case US_SC_QIC:
			ss->protocol_name = "QIC-157";
			ss->proto_handler = usb_stor_qic157_command;
			ss->max_lun = 0;
			break;

		case US_SC_8070:
			ss->protocol_name = "8070i";
			ss->proto_handler = usb_stor_ATAPI_command;
			ss->max_lun = 0;
			break;

		case US_SC_SCSI:
			ss->protocol_name = "Transparent SCSI";
			ss->proto_handler = usb_stor_transparent_scsi_command;
			break;

		case US_SC_UFI:
			ss->protocol_name = "Uniform Floppy Interface (UFI)";
			ss->proto_handler = usb_stor_ufi_command;
			break;

#ifdef CONFIG_USB_STORAGE_ISD200
                case US_SC_ISD200:
                        ss->protocol_name = "ISD200 ATA/ATAPI";
                        ss->proto_handler = isd200_ata_command;
                        break;
#endif

		default:
			/* ss->protocol_name = "Unknown"; */
			goto BadDevice;
		}
		US_DEBUGP("Protocol: %s\n", ss->protocol_name);

		/* allocate the URB, the usb_ctrlrequest, and the IRQ URB */
		if (usb_stor_allocate_urbs(ss))
			goto BadDevice;

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
		 * the host controller thread in us_detect.  But how else are
		 * we to do it?
		 */
		(struct us_data *)ss->htmplt.proc_dir = ss; 

		/* Just before we start our control thread, initialize
		 * the device if it needs initialization */
		if (unusual_dev && unusual_dev->initFunction)
			unusual_dev->initFunction(ss);

		/* start up our control thread */
		atomic_set(&ss->sm_state, US_STATE_IDLE);
		ss->pid = kernel_thread(usb_stor_control_thread, ss,
					CLONE_VM);
		if (ss->pid < 0) {
			printk(KERN_WARNING USB_STORAGE 
			       "Unable to start control thread\n");
			goto BadDevice;
		}

		/* wait for the thread to start */
		wait_for_completion(&(ss->notify));

		/* unlock the device pointers */
		up(&(ss->dev_semaphore));

		/* now register	 - our detect function will be called */
		ss->htmplt.module = THIS_MODULE;
		result = scsi_register_host(&(ss->htmplt));
		if (result) {
			printk(KERN_WARNING USB_STORAGE
				"Unable to register the scsi host\n");

			/* tell the control thread to exit */
			ss->srb = NULL;
			up(&ss->sema);
			wait_for_completion(&ss->notify);

			/* re-lock the device pointers */
			down(&ss->dev_semaphore);
			goto BadDevice;
		}

		/* lock access to the data structures */
		down(&us_list_semaphore);

		/* put us in the list */
		ss->next = us_list;
		us_list = ss;

		/* release the data structure lock */
		up(&us_list_semaphore);
	}

	printk(KERN_DEBUG 
	       "WARNING: USB Mass Storage data integrity not assured\n");
	printk(KERN_DEBUG 
	       "USB Mass Storage device found at %d\n", dev->devnum);

	/* save a pointer to our structure */
	usb_set_intfdata(intf, ss);
	return 0;

	/* we come here if there are any problems */
	/* ss->dev_semaphore must be locked */
	BadDevice:
	US_DEBUGP("storage_probe() failed\n");
	usb_stor_deallocate_urbs(ss);
	up(&ss->dev_semaphore);
	if (new_device)
		kfree(ss);
	return -EIO;
}

/* Handle a disconnect event from the USB core */
static void storage_disconnect(struct usb_interface *intf)
{
	struct us_data *ss = usb_get_intfdata(intf);

	US_DEBUGP("storage_disconnect() called\n");

	usb_set_intfdata(intf, NULL);

	/* this is the odd case -- we disconnected but weren't using it */
	if (!ss) {
		US_DEBUGP("-- device was not in use\n");
		return;
	}

	down(&(ss->dev_semaphore));
	usb_stor_deallocate_urbs(ss);
	up(&(ss->dev_semaphore));
}

/***********************************************************************
 * Initialization and registration
 ***********************************************************************/

int __init usb_stor_init(void)
{
	printk(KERN_INFO "Initializing USB Mass Storage driver...\n");

	/* initialize internal global data elements */
	us_list = NULL;
	init_MUTEX(&us_list_semaphore);
	my_host_number = 0;

	/* register the driver, return -1 if error */
	if (usb_register(&usb_storage_driver) < 0)
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
	usb_deregister(&usb_storage_driver) ;

	/* While there are still virtual hosts, unregister them
	 * Note that it's important to do this completely before removing
	 * the structures because of possible races with the /proc
	 * interface
	 */
	for (next = us_list; next; next = next->next) {
		US_DEBUGP("-- calling scsi_unregister_host()\n");
		scsi_unregister_host(&(next->htmplt));
	}

	/* While there are still structures, free them.  Note that we are
	 * now race-free, since these structures can no longer be accessed
	 * from either the SCSI command layer or the /proc interface
	 */
	while (us_list) {
		/* keep track of where the next one is */
		next = us_list->next;

		/* If there's extra data in the us_data structure then
		 * free that first */
		if (us_list->extra) {
			/* call the destructor routine, if it exists */
			if (us_list->extra_destructor) {
				US_DEBUGP("-- calling extra_destructor()\n");
				us_list->extra_destructor(us_list->extra);
			}

			/* destroy the extra data */
			US_DEBUGP("-- freeing the data structure\n");
			kfree(us_list->extra);
		}

		/* free the structure itself */
                kfree (us_list);

		/* advance the list pointer */
		us_list = next;
	}
}

module_init(usb_stor_init);
module_exit(usb_stor_exit);
