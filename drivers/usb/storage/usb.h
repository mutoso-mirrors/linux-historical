/* Driver for USB Mass Storage compliant devices
 * Main Header File
 *
 * $Id: usb.h,v 1.21 2002/04/21 02:57:59 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
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

#ifndef _USB_H_
#define _USB_H_

#include <linux/usb.h>
#include <linux/blk.h>
#include <linux/smp_lock.h>
#include <linux/completion.h>
#include <linux/version.h>
#include "scsi.h"
#include "hosts.h"

/* 
 * GUID definitions
 */

#define GUID(x) __u32 x[3]
#define GUID_EQUAL(x, y) (x[0] == y[0] && x[1] == y[1] && x[2] == y[2])
#define GUID_CLEAR(x) x[0] = x[1] = x[2] = 0;
#define GUID_NONE(x) (!x[0] && !x[1] && !x[2])
#define GUID_FORMAT "%08x%08x%08x"
#define GUID_ARGS(x) x[0], x[1], x[2]

static inline void make_guid( __u32 *pg, __u16 vendor, __u16 product, char *serial)
{
	pg[0] = (vendor << 16) | product;
	pg[1] = pg[2] = 0;
	while (*serial) {
		pg[1] <<= 4;
		pg[1] |= pg[2] >> 28;
		pg[2] <<= 4;
		if (*serial >= 'a')
			*serial -= 'a' - 'A';
		pg[2] |= (*serial <= '9' && *serial >= '0') ? *serial - '0'
			: *serial - 'A' + 10;
		serial++;
	}
}

struct us_data;

/*
 * Unusual device list definitions 
 */

struct us_unusual_dev {
	const char* vendorName;
	const char* productName;
	__u8  useProtocol;
	__u8  useTransport;
	int (*initFunction)(struct us_data *);
	unsigned int flags;
};

/* Flag definitions */
#define US_FL_SINGLE_LUN      0x00000001 /* allow access to only LUN 0	    */
#define US_FL_MODE_XLATE      0x00000002 /* translate _6 to _10 commands for
						    Win/MacOS compatibility */
#define US_FL_START_STOP      0x00000004 /* ignore START_STOP commands	    */
#define US_FL_IGNORE_SER      0x00000010 /* Ignore the serial number given  */
#define US_FL_SCM_MULT_TARG   0x00000020 /* supports multiple targets	    */
#define US_FL_FIX_INQUIRY     0x00000040 /* INQUIRY response needs fixing   */
#define US_FL_FIX_CAPACITY    0x00000080 /* READ CAPACITY response too big  */

#define US_FL_DEV_ATTACHED    0x00010000 /* is the device attached?	    */
#define US_FLIDX_IP_WANTED   17  /* 0x00020000	is an IRQ expected?	    */
#define US_FLIDX_CAN_CANCEL  18  /* 0x00040000  okay to cancel current_urb? */
#define US_FLIDX_CANCEL_SG   19  /* 0x00080000	okay to cancel current_sg?  */


/* processing state machine states */
#define US_STATE_IDLE		1
#define US_STATE_RUNNING	2
#define US_STATE_RESETTING	3
#define US_STATE_ABORTING	4

#define USB_STOR_STRING_LEN 32

typedef int (*trans_cmnd)(Scsi_Cmnd*, struct us_data*);
typedef int (*trans_reset)(struct us_data*);
typedef void (*proto_cmnd)(Scsi_Cmnd*, struct us_data*);
typedef void (*extra_data_destructor)(void *);	 /* extra data destructor   */

/* we allocate one of these for every device that we remember */
struct us_data {
	struct us_data		*next;		 /* next device */

	/* The device we're working with
	 * It's important to note:
	 *    (o) you must hold dev_semaphore to change pusb_dev
	 *    (o) DEV_ATTACHED in flags should change whenever pusb_dev does
	 */
	struct semaphore	dev_semaphore;	 /* protect pusb_dev */
	struct usb_device	*pusb_dev;	 /* this usb_device */
	unsigned long		flags;		 /* from filter initially */
	unsigned int		send_bulk_pipe;	 /* cached pipe values */
	unsigned int		recv_bulk_pipe;
	unsigned int		send_ctrl_pipe;
	unsigned int		recv_ctrl_pipe;

	/* information about the device -- always good */
	char			vendor[USB_STOR_STRING_LEN];
	char			product[USB_STOR_STRING_LEN];
	char			serial[USB_STOR_STRING_LEN];
	char			*transport_name;
	char			*protocol_name;
	u8			subclass;
	u8			protocol;
	u8			max_lun;

	/* information about the device -- only good if device is attached */
	u8			ifnum;		 /* interface number   */
	u8			ep_in;		 /* bulk in endpoint   */
	u8			ep_out;		 /* bulk out endpoint  */
	struct usb_endpoint_descriptor *ep_int;	 /* interrupt endpoint */ 

	/* function pointers for this device */
	trans_cmnd		transport;	 /* transport function	   */
	trans_reset		transport_reset; /* transport device reset */
	proto_cmnd		proto_handler;	 /* protocol handler	   */

	/* SCSI interfaces */
	GUID(guid);				 /* unique dev id	*/
	struct Scsi_Host	*host;		 /* our dummy host data */
	Scsi_Host_Template	htmplt;		 /* own host template	*/
	int			host_number;	 /* to find us		*/
	int			host_no;	 /* allocated by scsi	*/
	Scsi_Cmnd		*srb;		 /* current srb		*/

	/* thread information */
	int			pid;		 /* control thread	 */
	atomic_t		sm_state;	 /* what we are doing	 */

	/* interrupt info for CBI devices -- only good if attached */
	struct semaphore	ip_waitq;	 /* for CBI interrupts	 */

	/* interrupt communications data */
	struct semaphore	irq_urb_sem;	 /* to protect irq_urb	 */
	struct urb		*irq_urb;	 /* for USB int requests */
	unsigned char		irqbuf[2];	 /* buffer for USB IRQ	 */
	unsigned char		irqdata[2];	 /* data from USB IRQ	 */

	/* control and bulk communications data */
	struct urb		*current_urb;	 /* non-int USB requests */
	struct usb_ctrlrequest	*dr;		 /* control requests	 */
	struct usb_sg_request	*current_sg;	 /* scatter-gather USB   */

	/* the semaphore for sleeping the control thread */
	struct semaphore	sema;		 /* to sleep thread on   */

	/* mutual exclusion structures */
	struct completion	notify;		 /* thread begin/end	    */
	struct us_unusual_dev   *unusual_dev;	 /* If unusual device       */
	void			*extra;		 /* Any extra data          */
	extra_data_destructor	extra_destructor;/* extra data destructor   */
};

/* The list of structures and the protective lock for them */
extern struct us_data *us_list;
extern struct semaphore us_list_semaphore;

/* The structure which defines our driver */
extern struct usb_driver usb_storage_driver;

/* Function to fill an inquiry response. See usb.c for details */
extern void fill_inquiry_response(struct us_data *us,
	unsigned char *data, unsigned int data_len);

/* The scsi_lock() and scsi_unlock() macros protect the sm_state and the
 * single queue element srb for write access */
#define scsi_unlock(host)	spin_unlock_irq(host->host_lock)
#define scsi_lock(host)		spin_lock_irq(host->host_lock)
#define sg_address(psg)		(page_address((psg).page) + (psg).offset)

#endif
