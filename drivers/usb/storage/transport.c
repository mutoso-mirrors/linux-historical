/* Driver for USB Mass Storage compliant devices
 *
 * $Id: transport.c,v 1.47 2002/04/22 03:39:43 mdharm Exp $
 *
 * Current development and maintenance by:
 *   (c) 1999-2002 Matthew Dharm (mdharm-usb@one-eyed-alien.net)
 *
 * Developed with the assistance of:
 *   (c) 2000 David L. Brown, Jr. (usb-storage@davidb.org)
 *   (c) 2000 Stephen J. Gowdy (SGowdy@lbl.gov)
 *   (c) 2002 Alan Stern <stern@rowland.org>
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
#include "transport.h"
#include "protocol.h"
#include "usb.h"
#include "debug.h"

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>

/***********************************************************************
 * Helper routines
 ***********************************************************************/

/* Calculate the length of the data transfer (not the command) for any
 * given SCSI command
 */
unsigned int usb_stor_transfer_length(Scsi_Cmnd *srb)
{
      	int i;
	int doDefault = 0;
	unsigned int len = 0;
	unsigned int total = 0;
	struct scatterlist *sg;

	/* This table tells us:
	   X = command not supported
	   L = return length in cmnd[4] (8 bits).
	   M = return length in cmnd[8] (8 bits).
	   G = return length in cmnd[3] and cmnd[4] (16 bits)
	   H = return length in cmnd[7] and cmnd[8] (16 bits)
	   I = return length in cmnd[8] and cmnd[9] (16 bits)
	   C = return length in cmnd[2] to cmnd[5] (32 bits)
	   D = return length in cmnd[6] to cmnd[9] (32 bits)
	   B = return length in blocksize so we use buff_len
	   R = return length in cmnd[2] to cmnd[4] (24 bits)
	   S = return length in cmnd[3] to cmnd[5] (24 bits)
	   T = return length in cmnd[6] to cmnd[8] (24 bits)
	   U = return length in cmnd[7] to cmnd[9] (24 bits)
	   0-9 = fixed return length
	   V = 20 bytes
	   W = 24 bytes
	   Z = return length is mode dependant or not in command, use buff_len
	*/

	static char *lengths =

	      /* 0123456789ABCDEF   0123456789ABCDEF */

		"00XLZ6XZBXBBXXXB" "00LBBLG0R0L0GG0X"  /* 00-1F */
		"XXXXT8XXB4B0BBBB" "ZZZ0B00HCSSZTBHH"  /* 20-3F */
		"M0HHB0X000H0HH0X" "XHH0HHXX0TH0H0XX"  /* 40-5F */
		"XXXXXXXXXXXXXXXX" "XXXXXXXXXXXXXXXX"  /* 60-7F */
		"XXXXXXXXXXXXXXXX" "XXXXXXXXXXXXXXXX"  /* 80-9F */
		"X0XXX00XB0BXBXBB" "ZZZ0XUIDU000XHBX"  /* A0-BF */
		"XXXXXXXXXXXXXXXX" "XXXXXXXXXXXXXXXX"  /* C0-DF */
		"XDXXXXXXXXXXXXXX" "XXW00HXXXXXXXXXX"; /* E0-FF */

	/* Commands checked in table:

	   CHANGE_DEFINITION 40
	   COMPARE 39
	   COPY 18
	   COPY_AND_VERIFY 3a
	   ERASE 19
	   ERASE_10 2c
	   ERASE_12 ac
	   EXCHANGE_MEDIUM a6
	   FORMAT_UNIT 04
	   GET_DATA_BUFFER_STATUS 34
	   GET_MESSAGE_10 28
	   GET_MESSAGE_12 a8
	   GET_WINDOW 25   !!! Has more data than READ_CAPACITY, need to fix table
	   INITIALIZE_ELEMENT_STATUS 07 !!! REASSIGN_BLOCKS luckily uses buff_len
	   INQUIRY 12
	   LOAD_UNLOAD 1b
	   LOCATE 2b
	   LOCK_UNLOCK_CACHE 36
	   LOG_SELECT 4c
	   LOG_SENSE 4d
	   MEDIUM_SCAN 38     !!! This was M
	   MODE_SELECT6 15
	   MODE_SELECT_10 55
	   MODE_SENSE_6 1a
	   MODE_SENSE_10 5a
	   MOVE_MEDIUM a5
	   OBJECT_POSITION 31  !!! Same as SEARCH_DATA_EQUAL
	   PAUSE_RESUME 4b
	   PLAY_AUDIO_10 45
	   PLAY_AUDIO_12 a5
	   PLAY_AUDIO_MSF 47
	   PLAY_AUDIO_TRACK_INDEX 48
   	   PLAY_AUDIO_TRACK_RELATIVE_10 49
	   PLAY_AUDIO_TRACK_RELATIVE_12 a9
	   POSITION_TO_ELEMENT 2b
      	   PRE-FETCH 34
	   PREVENT_ALLOW_MEDIUM_REMOVAL 1e
	   PRINT 0a             !!! Same as WRITE_6 but is always in bytes
	   READ_6 08
	   READ_10 28
	   READ_12 a8
	   READ_BLOCK_LIMITS 05
	   READ_BUFFER 3c
	   READ_CAPACITY 25
	   READ_CDROM_CAPACITY 25
	   READ_DEFECT_DATA 37
	   READ_DEFECT_DATA_12 b7
	   READ_ELEMENT_STATUS b8 !!! Think this is in bytes
	   READ_GENERATION 29 !!! Could also be M?
	   READ_HEADER 44     !!! This was L
	   READ_LONG 3e
	   READ_POSITION 34   !!! This should be V but conflicts with PRE-FETCH
	   READ_REVERSE 0f
	   READ_SUB-CHANNEL 42 !!! Is this in bytes?
	   READ_TOC 43         !!! Is this in bytes?
	   READ_UPDATED_BLOCK 2d
	   REASSIGN_BLOCKS 07
	   RECEIVE 08        !!! Same as READ_6 probably in bytes though
	   RECEIVE_DIAGNOSTIC_RESULTS 1c
	   RECOVER_BUFFERED_DATA 14 !!! For PRINTERs this is bytes
	   RELEASE_UNIT 17
	   REQUEST_SENSE 03
	   REQUEST_VOLUME_ELEMENT_ADDRESS b5 !!! Think this is in bytes
	   RESERVE_UNIT 16
	   REWIND 01
	   REZERO_UNIT 01
	   SCAN 1b          !!! Conflicts with various commands, should be L
	   SEARCH_DATA_EQUAL 31
	   SEARCH_DATA_EQUAL_12 b1
	   SEARCH_DATA_LOW 30
	   SEARCH_DATA_LOW_12 b0
	   SEARCH_DATA_HIGH 32
	   SEARCH_DATA_HIGH_12 b2
	   SEEK_6 0b         !!! Conflicts with SLEW_AND_PRINT
	   SEEK_10 2b
	   SEND 0a           !!! Same as WRITE_6, probably in bytes though
	   SEND 2a           !!! Similar to WRITE_10 but for scanners
	   SEND_DIAGNOSTIC 1d
	   SEND_MESSAGE_6 0a   !!! Same as WRITE_6 - is in bytes
	   SEND_MESSAGE_10 2a  !!! Same as WRITE_10 - is in bytes
	   SEND_MESSAGE_12 aa  !!! Same as WRITE_12 - is in bytes
	   SEND_OPC 54
	   SEND_VOLUME_TAG b6 !!! Think this is in bytes
	   SET_LIMITS 33
	   SET_LIMITS_12 b3
	   SET_WINDOW 24
	   SLEW_AND_PRINT 0b !!! Conflicts with SEEK_6
	   SPACE 11
	   START_STOP_UNIT 1b
	   STOP_PRINT 1b
	   SYNCHRONIZE_BUFFER 10
	   SYNCHRONIZE_CACHE 35
	   TEST_UNIT_READY 00
	   UPDATE_BLOCK 3d
	   VERIFY 13
	   VERIFY 2f
	   VERIFY_12 af
	   WRITE_6 0a
	   WRITE_10 2a
	   WRITE_12 aa
	   WRITE_AND_VERIFY 2e
	   WRITE_AND_VERIFY_12 ae
	   WRITE_BUFFER 3b
	   WRITE_FILEMARKS 10
	   WRITE_LONG 3f
	   WRITE_SAME 41
	*/

	if (srb->sc_data_direction == SCSI_DATA_WRITE) {
		doDefault = 1;
	}
	else
		switch (lengths[srb->cmnd[0]]) {
			case 'L':
				len = srb->cmnd[4];
				break;

			case 'M':
				len = srb->cmnd[8];
				break;

			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
			case '8':
			case '9':
				len = lengths[srb->cmnd[0]]-'0';
				break;

			case 'G':
				len = (((unsigned int)srb->cmnd[3])<<8) |
					srb->cmnd[4];
				break;

			case 'H':
				len = (((unsigned int)srb->cmnd[7])<<8) |
					srb->cmnd[8];
				break;

			case 'I':
				len = (((unsigned int)srb->cmnd[8])<<8) |
					srb->cmnd[9];
				break;

			case 'R':
				len = (((unsigned int)srb->cmnd[2])<<16) |
					(((unsigned int)srb->cmnd[3])<<8) |
					srb->cmnd[4];
				break;

			case 'S':
				len = (((unsigned int)srb->cmnd[3])<<16) |
					(((unsigned int)srb->cmnd[4])<<8) |
					srb->cmnd[5];
				break;

			case 'T':
				len = (((unsigned int)srb->cmnd[6])<<16) |
					(((unsigned int)srb->cmnd[7])<<8) |
					srb->cmnd[8];
				break;

			case 'U':
				len = (((unsigned int)srb->cmnd[7])<<16) |
					(((unsigned int)srb->cmnd[8])<<8) |
					srb->cmnd[9];
				break;

			case 'C':
				len = (((unsigned int)srb->cmnd[2])<<24) |
					(((unsigned int)srb->cmnd[3])<<16) |
					(((unsigned int)srb->cmnd[4])<<8) |
					srb->cmnd[5];
				break;

			case 'D':
				len = (((unsigned int)srb->cmnd[6])<<24) |
					(((unsigned int)srb->cmnd[7])<<16) |
					(((unsigned int)srb->cmnd[8])<<8) |
					srb->cmnd[9];
				break;

			case 'V':
				len = 20;
				break;

			case 'W':
				len = 24;
				break;

			case 'B':
				/* Use buffer size due to different block sizes */
				doDefault = 1;
				break;

			case 'X':
				US_DEBUGP("Error: UNSUPPORTED COMMAND %02X\n",
						srb->cmnd[0]);
				doDefault = 1;
				break;

			case 'Z':
				/* Use buffer size due to mode dependence */
				doDefault = 1;
				break;

			default:
				US_DEBUGP("Error: COMMAND %02X out of range or table inconsistent (%c).\n",
						srb->cmnd[0], lengths[srb->cmnd[0]] );
				doDefault = 1;
		}
	   
	   if ( doDefault == 1 ) {
		   /* Are we going to scatter gather? */
		   if (srb->use_sg) {
			   /* Add up the sizes of all the sg segments */
			   sg = (struct scatterlist *) srb->request_buffer;
			   for (i = 0; i < srb->use_sg; i++)
				   total += sg[i].length;
			   len = total;

			   /* Double-check to see if the advertised buffer
			    * length less than the actual buffer length --
			    * in other words, we should tend towards the
			    * conservative side for data transfers.
			    */
			   if (len > srb->request_bufflen)
				   len = srb->request_bufflen;
		   }
		   else
			   /* Just return the length of the buffer */
			   len = srb->request_bufflen;
	   }

	/* According to the linux-scsi people, any command sent which
	 * violates this invariant is a bug.  In the hopes of removing
	 * all the complex logic above, let's find them and eliminate them.
	 */
	if (len != srb->request_bufflen) {
		printk(KERN_ERR "USB len=%d, request_bufflen=%d\n", len, srb->request_bufflen);
		dump_stack();
	}

	return len;
}

/***********************************************************************
 * Data transfer routines
 ***********************************************************************/

/*
 * This is subtle, so pay attention:
 * ---------------------------------
 * We're very concerned about races with a command abort.  Hanging this code
 * is a sure fire way to hang the kernel.  (Note that this discussion applies
 * only to transactions resulting from a scsi queued-command, since only
 * these transactions are subject to a scsi abort.  Other transactions, such
 * as those occurring during device-specific initialization, must be handled
 * by a separate code path.)
 *
 * The abort function first sets the machine state, then atomically
 * tests-and-clears the CAN_CANCEL bit in us->flags to see if the current_urb
 * needs to be aborted.
 *
 * The submit function first verifies that the submission completed without
 * errors, and only then sets the CAN_CANCEL bit.  This prevents the abort
 * function from trying to cancel the URB while the submit call is underway.
 * Next, the submit function must test the state to see if we got aborted
 * before the submission or before setting the CAN_CANCEL bit.  If so, it's
 * essential to abort the URB if it hasn't been cancelled already (i.e.,
 * if the CAN_CANCEL bit is still set).  Either way, the function must then
 * wait for the URB to finish.  Note that because the URB_ASYNC_UNLINK flag
 * is set, the URB can still be in progress even after a call to
 * usb_unlink_urb() returns.
 *
 * (It's also permissible, but not necessary, to test the state -before-
 * submitting the URB.  Doing so would prevent an unnecessary submission if
 * the transaction had already been aborted, but this is very unlikely to
 * happen, because the abort would have to have been requested during actual
 * kernel processing rather than during an I/O delay.)
 *
 * The idea is that (1) once the state is changed to ABORTING, either the
 * aborting function or the submitting function is guaranteed to call
 * usb_unlink_urb() for an active URB, and (2) test_and_clear_bit() prevents
 * usb_unlink_urb() from being called more than once or from being called
 * during usb_submit_urb().
 */

/* This is the completion handler which will wake us up when an URB
 * completes.
 */
static void usb_stor_blocking_completion(struct urb *urb, struct pt_regs *regs)
{
	struct completion *urb_done_ptr = (struct completion *)urb->context;

	complete(urb_done_ptr);
}

/* This is the common part of the URB message submission code
 *
 * All URBs from the usb-storage driver involved in handling a queued scsi
 * command _must_ pass through this function (or something like it) for the
 * abort mechanisms to work properly.
 */
static int usb_stor_msg_common(struct us_data *us)
{
	struct completion urb_done;
	int status;

	/* set up data structures for the wakeup system */
	init_completion(&urb_done);

	/* fill the common fields in the URB */
	us->current_urb->context = &urb_done;
	us->current_urb->actual_length = 0;
	us->current_urb->error_count = 0;
	us->current_urb->transfer_flags = URB_ASYNC_UNLINK;

	/* submit the URB */
	status = usb_submit_urb(us->current_urb, GFP_NOIO);
	if (status) {
		/* something went wrong */
		return status;
	}

	/* since the URB has been submitted successfully, it's now okay
	 * to cancel it */
	set_bit(US_FLIDX_CAN_CANCEL, &us->flags);

	/* has the current command been aborted? */
	if (atomic_read(&us->sm_state) == US_STATE_ABORTING) {

		/* cancel the URB, if it hasn't been cancelled already */
		if (test_and_clear_bit(US_FLIDX_CAN_CANCEL, &us->flags)) {
			US_DEBUGP("-- cancelling URB\n");
			usb_unlink_urb(us->current_urb);
		}
	}

	/* wait for the completion of the URB */
	wait_for_completion(&urb_done);
	clear_bit(US_FLIDX_CAN_CANCEL, &us->flags);

	/* return the URB status */
	return us->current_urb->status;
}

/* This is our function to emulate usb_control_msg() with enough control
 * to make aborts/resets/timeouts work
 */
int usb_stor_control_msg(struct us_data *us, unsigned int pipe,
			 u8 request, u8 requesttype, u16 value, u16 index, 
			 void *data, u16 size)
{
	int status;

	/* fill in the devrequest structure */
	us->dr->bRequestType = requesttype;
	us->dr->bRequest = request;
	us->dr->wValue = cpu_to_le16(value);
	us->dr->wIndex = cpu_to_le16(index);
	us->dr->wLength = cpu_to_le16(size);

	/* fill and submit the URB */
	usb_fill_control_urb(us->current_urb, us->pusb_dev, pipe, 
			 (unsigned char*) us->dr, data, size, 
			 usb_stor_blocking_completion, NULL);
	status = usb_stor_msg_common(us);

	/* return the actual length of the data transferred if no error */
	if (status == 0)
		status = us->current_urb->actual_length;
	return status;
}

/* This is our function to emulate usb_bulk_msg() with enough control
 * to make aborts/resets/timeouts work
 */
int usb_stor_bulk_msg(struct us_data *us, void *data, unsigned int pipe,
		      unsigned int len, unsigned int *act_len)
{
	int status;

	/* fill and submit the URB */
	usb_fill_bulk_urb(us->current_urb, us->pusb_dev, pipe, data, len,
		      usb_stor_blocking_completion, NULL);
	status = usb_stor_msg_common(us);

	/* store the actual length of the data transferred */
	*act_len = us->current_urb->actual_length;
	return status;
}

/* This is our function to submit interrupt URBs with enough control
 * to make aborts/resets/timeouts work
 *
 * This routine always uses us->recv_intr_pipe as the pipe and
 * us->ep_bInterval as the interrupt interval.
 */
int usb_stor_interrupt_msg(struct us_data *us, void *data,
			unsigned int len, unsigned int *act_len)
{
	unsigned int pipe = us->recv_intr_pipe;
	unsigned int maxp;
	int status;

	/* calculate the max packet size */
	maxp = usb_maxpacket(us->pusb_dev, pipe, usb_pipeout(pipe));
	if (maxp > len)
		maxp = len;

	/* fill and submit the URB */
	usb_fill_int_urb(us->current_urb, us->pusb_dev, pipe, data,
			maxp, usb_stor_blocking_completion, NULL,
			us->ep_bInterval);
	status = usb_stor_msg_common(us);

	/* store the actual length of the data transferred */
	*act_len = us->current_urb->actual_length;
	return status;
}

/* This is a version of usb_clear_halt() that doesn't read the status from
 * the device -- this is because some devices crash their internal firmware
 * when the status is requested after a halt.
 *
 * A definitive list of these 'bad' devices is too difficult to maintain or
 * make complete enough to be useful.  This problem was first observed on the
 * Hagiwara FlashGate DUAL unit.  However, bus traces reveal that neither
 * MacOS nor Windows checks the status after clearing a halt.
 *
 * Since many vendors in this space limit their testing to interoperability
 * with these two OSes, specification violations like this one are common.
 */
int usb_stor_clear_halt(struct us_data *us, unsigned int pipe)
{
	int result;
	int endp = usb_pipeendpoint(pipe);

	if (usb_pipein (pipe))
		endp |= USB_DIR_IN;

	result = usb_stor_control_msg(us, us->send_ctrl_pipe,
		USB_REQ_CLEAR_FEATURE, USB_RECIP_ENDPOINT, 0,
		endp, NULL, 0);		/* note: no 3*HZ timeout */
	US_DEBUGP("usb_stor_clear_halt: result=%d\n", result);

	/* this is a failure case */
	if (result < 0)
		return result;

	/* reset the toggles and endpoint flags */
	usb_endpoint_running(us->pusb_dev, usb_pipeendpoint(pipe),
		usb_pipeout(pipe));
	usb_settoggle(us->pusb_dev, usb_pipeendpoint(pipe),
		usb_pipeout(pipe), 0);

	return 0;
}


/*
 * Interpret the results of a URB transfer
 *
 * This function prints appropriate debugging messages, clears halts on
 * bulk endpoints, and translates the status to the corresponding
 * USB_STOR_XFER_xxx return code.
 */
static int interpret_urb_result(struct us_data *us, unsigned int pipe,
		unsigned int length, int result, unsigned int partial) {

	US_DEBUGP("Status code %d; transferred %u/%u\n",
			result, partial, length);
	switch (result) {

	/* no error code; did we send all the data? */
	case 0:
		if (partial != length) {
			US_DEBUGP("-- short transfer\n");
			return USB_STOR_XFER_SHORT;
		}

		US_DEBUGP("-- transfer complete\n");
		return USB_STOR_XFER_GOOD;

	/* stalled */
	case -EPIPE:
		/* for control endpoints, a stall indicates a protocol error */
		if (usb_pipecontrol(pipe)) {
			US_DEBUGP("-- stall on control pipe\n");
			return USB_STOR_XFER_ERROR;
		}

		/* for other sorts of endpoint, clear the stall */
		US_DEBUGP("clearing endpoint halt for pipe 0x%x\n", pipe);
		if (usb_stor_clear_halt(us, pipe) < 0)
			return USB_STOR_XFER_ERROR;
		return USB_STOR_XFER_STALLED;

	/* NAK - that means we've retried this a few times already */
	case -ETIMEDOUT:
		US_DEBUGP("-- device NAKed\n");
		return USB_STOR_XFER_ERROR;

	/* the transfer was cancelled, presumably by an abort */
	case -ENODEV:
		US_DEBUGP("-- transfer cancelled\n");
		return USB_STOR_XFER_ERROR;

	/* short scatter-gather read transfer */
	case -EREMOTEIO:
		US_DEBUGP("-- short read transfer\n");
		return USB_STOR_XFER_SHORT;

	/* the catch-all error case */
	default:
		US_DEBUGP("-- unknown error\n");
		return USB_STOR_XFER_ERROR;
	}
}

/*
 * Transfer one control message
 *
 * This function does basically the same thing as usb_stor_control_msg()
 * above, except that return codes are USB_STOR_XFER_xxx rather than the
 * urb status or transfer length.
 */
int usb_stor_ctrl_transfer(struct us_data *us, unsigned int pipe,
		u8 request, u8 requesttype, u16 value, u16 index,
		void *data, u16 size) {
	int result;
	unsigned int partial = 0;

	US_DEBUGP("usb_stor_ctrl_transfer(): rq=%02x rqtype=%02x "
			"value=%04x index=%02x len=%u\n",
			request, requesttype, value, index, size);
	result = usb_stor_control_msg(us, pipe, request, requesttype,
			value, index, data, size);

	if (result > 0) {	/* Separate out the amount transferred */
		partial = result;
		result = 0;
	}
	return interpret_urb_result(us, pipe, size, result, partial);
}

/*
 * Receive one buffer via interrupt transfer
 *
 * This function does basically the same thing as usb_stor_interrupt_msg()
 * above, except that return codes are USB_STOR_XFER_xxx rather than the
 * urb status.
 */
int usb_stor_intr_transfer(struct us_data *us, void *buf,
		unsigned int length, unsigned int *act_len)
{
	int result;
	unsigned int partial;

	/* transfer the data */
	US_DEBUGP("usb_stor_intr_transfer(): xfer %u bytes\n", length);
	result = usb_stor_interrupt_msg(us, buf, length, &partial);
	if (act_len)
		*act_len = partial;

	return interpret_urb_result(us, us->recv_intr_pipe,
			length, result, partial);
}

/*
 * Transfer one buffer via bulk transfer
 *
 * This function does basically the same thing as usb_stor_bulk_msg()
 * above, except that:
 *
 *	1.  If the bulk pipe stalls during the transfer, the halt is
 *	    automatically cleared;
 *	2.  Return codes are USB_STOR_XFER_xxx rather than the
 *	    urb status or transfer length.
 */
int usb_stor_bulk_transfer_buf(struct us_data *us, unsigned int pipe,
	void *buf, unsigned int length, unsigned int *act_len)
{
	int result;
	unsigned int partial;

	/* transfer the data */
	US_DEBUGP("usb_stor_bulk_transfer_buf(): xfer %u bytes\n", length);
	result = usb_stor_bulk_msg(us, buf, pipe, length, &partial);
	if (act_len)
		*act_len = partial;
	return interpret_urb_result(us, pipe, length, result, partial);
}

/*
 * Transfer a scatter-gather list via bulk transfer
 *
 * This function does basically the same thing as usb_stor_bulk_transfer_buf()
 * above, but it uses the usbcore scatter-gather primitives
 */
int usb_stor_bulk_transfer_sglist(struct us_data *us, unsigned int pipe,
		struct scatterlist *sg, int num_sg, unsigned int length,
		unsigned int *act_len)
{
	int result;
	unsigned int partial;

	/* initialize the scatter-gather request block */
	US_DEBUGP("usb_stor_bulk_transfer_sglist(): xfer %u bytes, "
			"%d entries\n", length, num_sg);
	result = usb_sg_init(us->current_sg, us->pusb_dev, pipe, 0,
			sg, num_sg, length, SLAB_NOIO);
	if (result) {
		US_DEBUGP("usb_sg_init returned %d\n", result);
		return USB_STOR_XFER_ERROR;
	}

	/* since the block has been initialized successfully, it's now
	 * okay to cancel it */
	set_bit(US_FLIDX_CANCEL_SG, &us->flags);

	/* has the current command been aborted? */
	if (atomic_read(&us->sm_state) == US_STATE_ABORTING) {

		/* cancel the request, if it hasn't been cancelled already */
		if (test_and_clear_bit(US_FLIDX_CANCEL_SG, &us->flags)) {
			US_DEBUGP("-- cancelling sg request\n");
			usb_sg_cancel(us->current_sg);
		}
	}

	/* wait for the completion of the transfer */
	usb_sg_wait(us->current_sg);
	clear_bit(US_FLIDX_CANCEL_SG, &us->flags);

	result = us->current_sg->status;
	partial = us->current_sg->bytes;
	if (act_len)
		*act_len = partial;
	return interpret_urb_result(us, pipe, length, result, partial);
}

/*
 * Transfer an entire SCSI command's worth of data payload over the bulk
 * pipe.
 *
 * Note that this uses usb_stor_bulk_transfer_buf() and
 * usb_stor_bulk_transfer_sglist() to achieve its goals --
 * this function simply determines whether we're going to use
 * scatter-gather or not, and acts appropriately.
 */
int usb_stor_bulk_transfer_sg(struct us_data* us, unsigned int pipe,
		void *buf, unsigned int length_left, int use_sg, int *residual)
{
	int result;
	unsigned int partial;

	/* are we scatter-gathering? */
	if (use_sg) {
		/* use the usb core scatter-gather primitives */
		result = usb_stor_bulk_transfer_sglist(us, pipe,
				(struct scatterlist *) buf, use_sg,
				length_left, &partial);
		length_left -= partial;
	} else {
		/* no scatter-gather, just make the request */
		result = usb_stor_bulk_transfer_buf(us, pipe, buf, 
				length_left, &partial);
		length_left -= partial;
	}

	/* store the residual and return the error code */
	if (residual)
		*residual = length_left;
	return result;
}

/***********************************************************************
 * Transport routines
 ***********************************************************************/

/* Invoke the transport and basic error-handling/recovery methods
 *
 * This is used by the protocol layers to actually send the message to
 * the device and receive the response.
 */
void usb_stor_invoke_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	int need_auto_sense;
	int result;

	/* send the command to the transport layer */
	srb->resid = 0;
	result = us->transport(srb, us);

	/* if the command gets aborted by the higher layers, we need to
	 * short-circuit all other processing
	 */
	if (atomic_read(&us->sm_state) == US_STATE_ABORTING) {
		US_DEBUGP("-- transport indicates command was aborted\n");
		srb->result = DID_ABORT << 16;
		return;
	}

	/* if there is a transport error, reset and don't auto-sense */
	/* What if we want to abort during the reset? */
	if (result == USB_STOR_TRANSPORT_ERROR) {
		US_DEBUGP("-- transport indicates error, resetting\n");
		us->transport_reset(us);
		srb->result = DID_ERROR << 16;
		return;
	}

	/* Determine if we need to auto-sense
	 *
	 * I normally don't use a flag like this, but it's almost impossible
	 * to understand what's going on here if I don't.
	 */
	need_auto_sense = 0;

	/*
	 * If we're running the CB transport, which is incapable
	 * of determining status on it's own, we need to auto-sense almost
	 * every time.
	 */
	if (us->protocol == US_PR_CB || us->protocol == US_PR_DPCM_USB) {
		US_DEBUGP("-- CB transport device requiring auto-sense\n");
		need_auto_sense = 1;

		/* There are some exceptions to this.  Notably, if this is
		 * a UFI device and the command is REQUEST_SENSE or INQUIRY,
		 * then it is impossible to truly determine status.
		 */
		if (us->subclass == US_SC_UFI &&
		    ((srb->cmnd[0] == REQUEST_SENSE) ||
		     (srb->cmnd[0] == INQUIRY))) {
			US_DEBUGP("** no auto-sense for a special command\n");
			need_auto_sense = 0;
		}
	}

	/*
	 * If we have a failure, we're going to do a REQUEST_SENSE 
	 * automatically.  Note that we differentiate between a command
	 * "failure" and an "error" in the transport mechanism.
	 */
	if (result == USB_STOR_TRANSPORT_FAILED) {
		US_DEBUGP("-- transport indicates command failure\n");
		need_auto_sense = 1;
	}

	/*
	 * Also, if we have a short transfer on a command that can't have
	 * a short transfer, we're going to do this.
	 */
	if ((srb->resid > 0) &&
	    !((srb->cmnd[0] == REQUEST_SENSE) ||
	      (srb->cmnd[0] == INQUIRY) ||
	      (srb->cmnd[0] == MODE_SENSE) ||
	      (srb->cmnd[0] == LOG_SENSE) ||
	      (srb->cmnd[0] == MODE_SENSE_10))) {
		US_DEBUGP("-- unexpectedly short transfer\n");
		need_auto_sense = 1;
	}

	/* Now, if we need to do the auto-sense, let's do it */
	if (need_auto_sense) {
		int temp_result;
		void* old_request_buffer;
		unsigned short old_sg;
		unsigned old_request_bufflen;
		unsigned char old_sc_data_direction;
		unsigned char old_cmnd[MAX_COMMAND_SIZE];

		US_DEBUGP("Issuing auto-REQUEST_SENSE\n");

		/* save the old command */
		memcpy(old_cmnd, srb->cmnd, MAX_COMMAND_SIZE);

		/* set the command and the LUN */
		srb->cmnd[0] = REQUEST_SENSE;
		srb->cmnd[1] = old_cmnd[1] & 0xE0;
		srb->cmnd[2] = 0;
		srb->cmnd[3] = 0;
		srb->cmnd[4] = 18;
		srb->cmnd[5] = 0;

		/* set the transfer direction */
		old_sc_data_direction = srb->sc_data_direction;
		srb->sc_data_direction = SCSI_DATA_READ;

		/* use the new buffer we have */
		old_request_buffer = srb->request_buffer;
		srb->request_buffer = srb->sense_buffer;

		/* set the buffer length for transfer */
		old_request_bufflen = srb->request_bufflen;
		srb->request_bufflen = 18;

		/* set up for no scatter-gather use */
		old_sg = srb->use_sg;
		srb->use_sg = 0;

		/* issue the auto-sense command */
		temp_result = us->transport(us->srb, us);

		/* let's clean up right away */
		srb->request_buffer = old_request_buffer;
		srb->request_bufflen = old_request_bufflen;
		srb->use_sg = old_sg;
		srb->sc_data_direction = old_sc_data_direction;
		memcpy(srb->cmnd, old_cmnd, MAX_COMMAND_SIZE);

		if (atomic_read(&us->sm_state) == US_STATE_ABORTING) {
			US_DEBUGP("-- auto-sense aborted\n");
			srb->result = DID_ABORT << 16;
			return;
		}
		if (temp_result != USB_STOR_TRANSPORT_GOOD) {
			US_DEBUGP("-- auto-sense failure\n");

			/* we skip the reset if this happens to be a
			 * multi-target device, since failure of an
			 * auto-sense is perfectly valid
			 */
			if (!(us->flags & US_FL_SCM_MULT_TARG)) {
				/* What if we try to abort during the reset? */
				us->transport_reset(us);
			}
			srb->result = DID_ERROR << 16;
			return;
		}

		US_DEBUGP("-- Result from auto-sense is %d\n", temp_result);
		US_DEBUGP("-- code: 0x%x, key: 0x%x, ASC: 0x%x, ASCQ: 0x%x\n",
			  srb->sense_buffer[0],
			  srb->sense_buffer[2] & 0xf,
			  srb->sense_buffer[12], 
			  srb->sense_buffer[13]);
#ifdef CONFIG_USB_STORAGE_DEBUG
		usb_stor_show_sense(
			  srb->sense_buffer[2] & 0xf,
			  srb->sense_buffer[12], 
			  srb->sense_buffer[13]);
#endif

		/* set the result so the higher layers expect this data */
		srb->result = CHECK_CONDITION << 1;

		/* If things are really okay, then let's show that */
		if ((srb->sense_buffer[2] & 0xf) == 0x0)
			srb->result = GOOD << 1;
	} else /* if (need_auto_sense) */
		srb->result = GOOD << 1;

	/* Regardless of auto-sense, if we _know_ we have an error
	 * condition, show that in the result code
	 */
	if (result == USB_STOR_TRANSPORT_FAILED)
		srb->result = CHECK_CONDITION << 1;

	/* If we think we're good, then make sure the sense data shows it.
	 * This is necessary because the auto-sense for some devices always
	 * sets byte 0 == 0x70, even if there is no error
	 */
	if ((us->protocol == US_PR_CB || us->protocol == US_PR_DPCM_USB) && 
	    (result == USB_STOR_TRANSPORT_GOOD) &&
	    ((srb->sense_buffer[2] & 0xf) == 0x0))
		srb->sense_buffer[0] = 0x0;
}

/* Abort the currently running scsi command or device reset.
 * This must be called with scsi_lock(us->srb->host) held */
void usb_stor_abort_transport(struct us_data *us)
{
	struct Scsi_Host *host;
	int state = atomic_read(&us->sm_state);

	US_DEBUGP("usb_stor_abort_transport called\n");

	/* Normally the current state is RUNNING.  If the control thread
	 * hasn't even started processing this command, the state will be
	 * IDLE.  Anything else is a bug. */
	BUG_ON((state != US_STATE_RUNNING && state != US_STATE_IDLE));

	/* set state to abort and release the lock */
	atomic_set(&us->sm_state, US_STATE_ABORTING);
	host = us->srb->host;
	scsi_unlock(host);

	/* If the state machine is blocked waiting for an URB,
	 * let's wake it up */

	/* If we have an URB pending, cancel it.  The test_and_clear_bit()
	 * call guarantees that if a URB has just been submitted, it
	 * won't be cancelled more than once. */
	if (test_and_clear_bit(US_FLIDX_CAN_CANCEL, &us->flags)) {
		US_DEBUGP("-- cancelling URB\n");
		usb_unlink_urb(us->current_urb);
	}

	/* If we are waiting for a scatter-gather operation, cancel it. */
	if (test_and_clear_bit(US_FLIDX_CANCEL_SG, &us->flags)) {
		US_DEBUGP("-- cancelling sg request\n");
		usb_sg_cancel(us->current_sg);
	}

	/* Wait for the aborted command to finish */
	wait_for_completion(&us->notify);

	/* Reacquire the lock: note that us->srb is now NULL */
	scsi_lock(host);
}

/*
 * Control/Bulk/Interrupt transport
 */

int usb_stor_CBI_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	unsigned int transfer_length = usb_stor_transfer_length(srb);
	int result;

	/* COMMAND STAGE */
	/* let's send the command via the control pipe */
	result = usb_stor_ctrl_transfer(us, us->send_ctrl_pipe,
				      US_CBI_ADSC, 
				      USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0, 
				      us->ifnum, srb->cmnd, srb->cmd_len);

	/* check the return code for the command */
	US_DEBUGP("Call to usb_stor_ctrl_transfer() returned %d\n", result);
	if (result != USB_STOR_XFER_GOOD) {
		/* Uh oh... serious problem here */
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* DATA STAGE */
	/* transfer the data payload for this command, if one exists*/
	if (transfer_length) {
		unsigned int pipe = srb->sc_data_direction == SCSI_DATA_READ ? 
				us->recv_bulk_pipe : us->send_bulk_pipe;
		result = usb_stor_bulk_transfer_srb(us, pipe, srb,
					transfer_length);
		US_DEBUGP("CBI data stage result is 0x%x\n", result);
		if (result == USB_STOR_XFER_ERROR)
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* STATUS STAGE */
	result = usb_stor_intr_transfer(us, us->irqdata,
					sizeof(us->irqdata), NULL);
	US_DEBUGP("Got interrupt data (0x%x, 0x%x)\n", 
			us->irqdata[0], us->irqdata[1]);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* UFI gives us ASC and ASCQ, like a request sense
	 *
	 * REQUEST_SENSE and INQUIRY don't affect the sense data on UFI
	 * devices, so we ignore the information for those commands.  Note
	 * that this means we could be ignoring a real error on these
	 * commands, but that can't be helped.
	 */
	if (us->subclass == US_SC_UFI) {
		if (srb->cmnd[0] == REQUEST_SENSE ||
		    srb->cmnd[0] == INQUIRY)
			return USB_STOR_TRANSPORT_GOOD;
		else {
			if (us->irqdata[0])
				return USB_STOR_TRANSPORT_FAILED;
			else
				return USB_STOR_TRANSPORT_GOOD;
		}
	}

	/* If not UFI, we interpret the data as a result code 
	 * The first byte should always be a 0x0
	 * The second byte & 0x0F should be 0x0 for good, otherwise error 
	 */
	if (us->irqdata[0]) {
		US_DEBUGP("CBI IRQ data showed reserved bType %d\n",
				us->irqdata[0]);
		return USB_STOR_TRANSPORT_ERROR;
	}

	switch (us->irqdata[1] & 0x0F) {
		case 0x00: 
			return USB_STOR_TRANSPORT_GOOD;
		case 0x01: 
			return USB_STOR_TRANSPORT_FAILED;
		default: 
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* we should never get here, but if we do, we're in trouble */
	return USB_STOR_TRANSPORT_ERROR;
}

/*
 * Control/Bulk transport
 */
int usb_stor_CB_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	unsigned int transfer_length = usb_stor_transfer_length(srb);
	int result;

	/* COMMAND STAGE */
	/* let's send the command via the control pipe */
	result = usb_stor_ctrl_transfer(us, us->send_ctrl_pipe,
				      US_CBI_ADSC, 
				      USB_TYPE_CLASS | USB_RECIP_INTERFACE, 0, 
				      us->ifnum, srb->cmnd, srb->cmd_len);

	/* check the return code for the command */
	US_DEBUGP("Call to usb_stor_ctrl_transfer() returned %d\n", result);
	if (result != USB_STOR_XFER_GOOD) {
		/* Uh oh... serious problem here */
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* DATA STAGE */
	/* transfer the data payload for this command, if one exists*/
	if (transfer_length) {
		unsigned int pipe = srb->sc_data_direction == SCSI_DATA_READ ? 
				us->recv_bulk_pipe : us->send_bulk_pipe;
		result = usb_stor_bulk_transfer_srb(us, pipe, srb,
					transfer_length);
		US_DEBUGP("CB data stage result is 0x%x\n", result);
		if (result == USB_STOR_XFER_ERROR)
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* STATUS STAGE */
	/* NOTE: CB does not have a status stage.  Silly, I know.  So
	 * we have to catch this at a higher level.
	 */
	return USB_STOR_TRANSPORT_GOOD;
}

/*
 * Bulk only transport
 */

/* Determine what the maximum LUN supported is */
int usb_stor_Bulk_max_lun(struct us_data *us)
{
	unsigned char data;
	int result;

	/* Issue the command -- use usb_control_msg() because this is
	 * not a scsi queued-command.  Also note that at this point the
	 * cached pipe values have not yet been stored. */
	result = usb_control_msg(us->pusb_dev,
				 usb_rcvctrlpipe(us->pusb_dev, 0),
				 US_BULK_GET_MAX_LUN, 
				 USB_DIR_IN | USB_TYPE_CLASS | 
				 USB_RECIP_INTERFACE,
				 0, us->ifnum, &data, sizeof(data), HZ);

	US_DEBUGP("GetMaxLUN command result is %d, data is %d\n", 
		  result, data);

	/* if we have a successful request, return the result */
	if (result == 1)
		return data;

	/* return the default -- no LUNs */
	return 0;
}

int usb_stor_Bulk_transport(Scsi_Cmnd *srb, struct us_data *us)
{
	struct bulk_cb_wrap bcb;
	struct bulk_cs_wrap bcs;
	unsigned int transfer_length = usb_stor_transfer_length(srb);
	int result;

	/* set up the command wrapper */
	bcb.Signature = cpu_to_le32(US_BULK_CB_SIGN);
	bcb.DataTransferLength = cpu_to_le32(transfer_length);
	bcb.Flags = srb->sc_data_direction == SCSI_DATA_READ ? 1 << 7 : 0;
	bcb.Tag = srb->serial_number;
	bcb.Lun = srb->cmnd[1] >> 5;
	if (us->flags & US_FL_SCM_MULT_TARG)
		bcb.Lun |= srb->device->id << 4;
	bcb.Length = srb->cmd_len;

	/* copy the command payload */
	memset(bcb.CDB, 0, sizeof(bcb.CDB));
	memcpy(bcb.CDB, srb->cmnd, bcb.Length);

	/* send it to out endpoint */
	US_DEBUGP("Bulk command S 0x%x T 0x%x Trg %d LUN %d L %d F %d CL %d\n",
		  le32_to_cpu(bcb.Signature), bcb.Tag,
		  (bcb.Lun >> 4), (bcb.Lun & 0x0F), 
		  le32_to_cpu(bcb.DataTransferLength), bcb.Flags, bcb.Length);
	result = usb_stor_bulk_transfer_buf(us, us->send_bulk_pipe,
				&bcb, US_BULK_CB_WRAP_LEN, NULL);
	US_DEBUGP("Bulk command transfer result=%d\n", result);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* DATA STAGE */
	/* send/receive data payload, if there is any */
	if (transfer_length) {
		unsigned int pipe = srb->sc_data_direction == SCSI_DATA_READ ? 
				us->recv_bulk_pipe : us->send_bulk_pipe;
		result = usb_stor_bulk_transfer_srb(us, pipe, srb,
					transfer_length);
		US_DEBUGP("Bulk data transfer result 0x%x\n", result);
		if (result == USB_STOR_XFER_ERROR)
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* See flow chart on pg 15 of the Bulk Only Transport spec for
	 * an explanation of how this code works.
	 */

	/* get CSW for device status */
	US_DEBUGP("Attempting to get CSW...\n");
	result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				&bcs, US_BULK_CS_WRAP_LEN, NULL);

	/* did the attempt to read the CSW fail? */
	if (result == USB_STOR_XFER_STALLED) {

		/* get the status again */
		US_DEBUGP("Attempting to get CSW (2nd try)...\n");
		result = usb_stor_bulk_transfer_buf(us, us->recv_bulk_pipe,
				&bcs, US_BULK_CS_WRAP_LEN, NULL);
	}

	/* if we still have a failure at this point, we're in trouble */
	US_DEBUGP("Bulk status result = %d\n", result);
	if (result != USB_STOR_XFER_GOOD)
		return USB_STOR_TRANSPORT_ERROR;

	/* check bulk status */
	US_DEBUGP("Bulk status Sig 0x%x T 0x%x R %d Stat 0x%x\n",
		  le32_to_cpu(bcs.Signature), bcs.Tag, 
		  bcs.Residue, bcs.Status);
	if (bcs.Signature != cpu_to_le32(US_BULK_CS_SIGN) || 
	    bcs.Tag != bcb.Tag || 
	    bcs.Status > US_BULK_STAT_PHASE) {
		US_DEBUGP("Bulk logical error\n");
		return USB_STOR_TRANSPORT_ERROR;
	}

	/* based on the status code, we report good or bad */
	switch (bcs.Status) {
		case US_BULK_STAT_OK:
			/* command good -- note that data could be short */
			return USB_STOR_TRANSPORT_GOOD;

		case US_BULK_STAT_FAIL:
			/* command failed */
			return USB_STOR_TRANSPORT_FAILED;

		case US_BULK_STAT_PHASE:
			/* phase error -- note that a transport reset will be
			 * invoked by the invoke_transport() function
			 */
			return USB_STOR_TRANSPORT_ERROR;
	}

	/* we should never get here, but if we do, we're in trouble */
	return USB_STOR_TRANSPORT_ERROR;
}

/***********************************************************************
 * Reset routines
 ***********************************************************************/

/* This is the common part of the device reset code.
 *
 * It's handy that every transport mechanism uses the control endpoint for
 * resets.
 *
 * Basically, we send a reset with a 20-second timeout, so we don't get
 * jammed attempting to do the reset.
 */
static int usb_stor_reset_common(struct us_data *us,
		u8 request, u8 requesttype,
		u16 value, u16 index, void *data, u16 size)
{
	int result;

	/* A 20-second timeout may seem rather long, but a LaCie
	 *  StudioDrive USB2 device takes 16+ seconds to get going
	 *  following a powerup or USB attach event. */

	/* Use usb_control_msg() because this is not a queued-command */
	result = usb_control_msg(us->pusb_dev, us->send_ctrl_pipe,
			request, requesttype, value, index, data, size,
			20*HZ);
	if (result < 0)
		goto Done;

	/* long wait for reset */
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(HZ*6);
	set_current_state(TASK_RUNNING);

	/* Use usb_clear_halt() because this is not a queued-command */
	US_DEBUGP("Soft reset: clearing bulk-in endpoint halt\n");
	result = usb_clear_halt(us->pusb_dev, us->recv_bulk_pipe);
	if (result < 0)
		goto Done;

	US_DEBUGP("Soft reset: clearing bulk-out endpoint halt\n");
	result = usb_clear_halt(us->pusb_dev, us->send_bulk_pipe);

	Done:

	/* return a result code based on the result of the control message */
	if (result < 0) {
		US_DEBUGP("Soft reset failed: %d\n", result);
		result = FAILED;
	} else {
		US_DEBUGP("Soft reset done\n");
		result = SUCCESS;
	}
	return result;
}

/* This issues a CB[I] Reset to the device in question
 */
int usb_stor_CB_reset(struct us_data *us)
{
	unsigned char cmd[12];

	US_DEBUGP("CB_reset() called\n");

	memset(cmd, 0xFF, sizeof(cmd));
	cmd[0] = SEND_DIAGNOSTIC;
	cmd[1] = 4;
	return usb_stor_reset_common(us, US_CBI_ADSC, 
				 USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 0, us->ifnum, cmd, sizeof(cmd));
}

/* This issues a Bulk-only Reset to the device in question, including
 * clearing the subsequent endpoint halts that may occur.
 */
int usb_stor_Bulk_reset(struct us_data *us)
{
	US_DEBUGP("Bulk reset requested\n");

	return usb_stor_reset_common(us, US_BULK_RESET_REQUEST, 
				 USB_TYPE_CLASS | USB_RECIP_INTERFACE,
				 0, us->ifnum, NULL, 0);
}
