/*
 *	LAPB release 001
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	LAPB 001	Jonathan Naylor	Started Coding
 */

#include <linux/config.h>
#if defined(CONFIG_LAPB) || defined(CONFIG_LAPB_MODULE)
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/inet.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/lapb.h>

/* 
 *  This procedure is passed a buffer descriptor for an iframe. It builds
 *  the rest of the control part of the frame and then writes it out.
 */
static void lapb_send_iframe(lapb_cb *lapb, struct sk_buff *skb, int poll_bit)
{
	unsigned char *frame;

	if (skb == NULL)
		return;

	if (lapb->mode & LAPB_EXTENDED) {
		frame = skb_push(skb, 2);

		frame[0] = I;
		frame[0] |= (lapb->vs << 1);
		frame[1] = (poll_bit) ? LAPB_EPF : 0;
		frame[1] |= (lapb->vr << 1);
	} else {
		frame = skb_push(skb, 1);

		*frame = I;
		*frame |= (poll_bit) ? LAPB_SPF : 0;
		*frame |= (lapb->vr << 5);
		*frame |= (lapb->vs << 1);
	}

	lapb_transmit_buffer(lapb, skb, LAPB_COMMAND);	
}

void lapb_kick(lapb_cb *lapb)
{
	struct sk_buff *skb, *skbn;
	int modulus, last = 1;
	unsigned short start, end, next;

	del_timer(&lapb->timer);

	modulus = (lapb->mode & LAPB_EXTENDED) ? LAPB_EMODULUS : LAPB_SMODULUS;

	start = (skb_peek(&lapb->ack_queue) == NULL) ? lapb->va : lapb->vs;
	end   = (lapb->va + lapb->window) % modulus;

	if (!(lapb->condition & PEER_RX_BUSY_CONDITION) &&
	    start != end                                &&
	    skb_peek(&lapb->write_queue) != NULL) {

		lapb->vs = start;

		/*
		 * Dequeue the frame and copy it.
		 */
		skb  = skb_dequeue(&lapb->write_queue);

		do {
			if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
				skb_queue_head(&lapb->write_queue, skb);
				break;
			}

			next = (lapb->vs + 1) % modulus;
#ifdef notdef
			last = (next == end) || skb_peek(&lapb->write_queue) == NULL;
#else
			last = (next == end);
#endif
			/*
			 * Transmit the frame copy.
			 */
			lapb_send_iframe(lapb, skbn, POLLOFF);

			lapb->vs = next;

			/*
			 * Requeue the original data frame.
			 */
			skb_queue_tail(&lapb->ack_queue, skb);
#ifdef notdef
		} while (!last);
#else
		} while (!last && (skb = skb_dequeue(&lapb->write_queue)) != NULL);
#endif
		lapb->condition &= ~LAPB_ACK_PENDING_CONDITION;

		if (lapb->t1timer == 0)
			lapb->t1timer = lapb->t1;
	}

	lapb_set_timer(lapb);
}

void lapb_transmit_buffer(lapb_cb *lapb, struct sk_buff *skb, int type)
{
	unsigned char *ptr;

	ptr = skb_push(skb, 1);

	if (lapb->mode & LAPB_MLP) {
		if (lapb->mode & LAPB_DCE) {
			if (type == LAPB_COMMAND)
				*ptr = LAPB_ADDR_C;
			if (type == LAPB_RESPONSE)
				*ptr = LAPB_ADDR_D;
		} else {
			if (type == LAPB_COMMAND)
				*ptr = LAPB_ADDR_D;
			if (type == LAPB_RESPONSE)
				*ptr = LAPB_ADDR_C;
		}
	} else {
		if (lapb->mode & LAPB_DCE) {
			if (type == LAPB_COMMAND)
				*ptr = LAPB_ADDR_A;
			if (type == LAPB_RESPONSE)
				*ptr = LAPB_ADDR_B;
		} else {
			if (type == LAPB_COMMAND)
				*ptr = LAPB_ADDR_B;
			if (type == LAPB_RESPONSE)
				*ptr = LAPB_ADDR_A;
		}
	}

#if LAPB_DEBUG > 2
	printk(KERN_DEBUG "lapb: (%p) S%d TX %02X %02X %02X\n", lapb->token, lapb->state, skb->data[0], skb->data[1], skb->data[2]);
#endif

	if (!lapb_data_transmit(lapb, skb))
		kfree_skb(skb, FREE_WRITE);
}

void lapb_nr_error_recovery(lapb_cb *lapb)
{
	lapb_establish_data_link(lapb);
}

void lapb_establish_data_link(lapb_cb *lapb)
{
	lapb->condition = 0x00;
	lapb->n2count   = 0;

	if (lapb->mode & LAPB_EXTENDED) {
#if LAPB_DEBUG > 1
		printk(KERN_DEBUG "lapb: (%p) S%d TX SABME(1)\n", lapb->token, lapb->state);
#endif
		lapb_send_control(lapb, SABME, POLLON, LAPB_COMMAND);
	} else {
#if LAPB_DEBUG > 1
		printk(KERN_DEBUG "lapb: (%p) S%d TX SABM(1)\n", lapb->token, lapb->state);
#endif
		lapb_send_control(lapb, SABM, POLLON, LAPB_COMMAND);
	}
	
	lapb->t2timer = 0;
	lapb->t1timer = lapb->t1;
}

void lapb_transmit_enquiry(lapb_cb *lapb)
{
#if LAPB_DEBUG > 1
	printk(KERN_DEBUG "lapb: (%p) S%d TX RR(1) R%d\n", lapb->token, lapb->state, lapb->vr);
#endif

	lapb_send_control(lapb, RR, POLLON, C_COMMAND);

	lapb->condition &= ~LAPB_ACK_PENDING_CONDITION;

	lapb->t1timer = lapb->t1;
}
 	
void lapb_enquiry_response(lapb_cb *lapb)
{
#if LAPB_DEBUG > 1
	printk(KERN_DEBUG "lapb: (%p) S%d TX RR(1) R%d\n", lapb->token, lapb->state, lapb->vr);
#endif

	lapb_send_control(lapb, RR, POLLON, LAPB_RESPONSE);

	lapb->condition &= ~LAPB_ACK_PENDING_CONDITION;
}

void lapb_timeout_response(lapb_cb *lapb)
{
#if LAPB_DEBUG > 1
	printk(KERN_DEBUG "lapb: (%p) S%d TX RR(0) R%d\n", lapb->token, lapb->state, lapb->vr);
#endif

	lapb_send_control(lapb, RR, POLLOFF, LAPB_RESPONSE);

	lapb->condition &= ~LAPB_ACK_PENDING_CONDITION;
}

void lapb_check_iframes_acked(lapb_cb *lapb, unsigned short nr)
{
	if (lapb->vs == nr) {
		lapb_frames_acked(lapb, nr);
		lapb->t1timer = 0;
	} else {
		if (lapb->va != nr) {
			lapb_frames_acked(lapb, nr);
			lapb->t1timer = lapb->t1;
		}
	}
}

void lapb_check_need_response(lapb_cb *lapb, int type, int pf)
{
	if (type == LAPB_COMMAND && pf)
		lapb_enquiry_response(lapb);
}

#endif
