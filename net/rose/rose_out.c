/*
 *	Rose release 001
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.0 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	Rose 001	Jonathan(G4KLX)	Cloned from nr_out.c
 */

#include <linux/config.h>
#if defined(CONFIG_ROSE) || defined(CONFIG_ROSE_MODULE)
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
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/rose.h>

/*
 *	This is where all Rose frames pass;
 */
void rose_output(struct sock *sk, struct sk_buff *skb)
{
	struct sk_buff *skbn;
	unsigned char header[ROSE_MIN_LEN];
	int err, frontlen, len;

	if (skb->len - ROSE_MIN_LEN > ROSE_PACLEN) {
		/* Save a copy of the Header */
		memcpy(header, skb->data, ROSE_MIN_LEN);
		skb_pull(skb, ROSE_MIN_LEN);

		frontlen = skb_headroom(skb);

		while (skb->len > 0) {
			if ((skbn = sock_alloc_send_skb(sk, frontlen + ROSE_PACLEN, 0, 0, &err)) == NULL)
				return;

			skbn->sk   = sk;
			skbn->arp  = 1;

			skb_reserve(skbn, frontlen);

			len = (ROSE_PACLEN > skb->len) ? skb->len : ROSE_PACLEN;

			/* Copy the user data */
			memcpy(skb_put(skbn, len), skb->data, len);
			skb_pull(skb, len);

			/* Duplicate the Header */
			skb_push(skbn, ROSE_MIN_LEN);
			memcpy(skbn->data, header, ROSE_MIN_LEN);

			if (skb->len > 0)
				skbn->data[2] |= M_BIT;
		
			skb_queue_tail(&sk->write_queue, skbn); /* Throw it on the queue */
		}
		
		kfree_skb(skb, FREE_WRITE);
	} else {
		skb_queue_tail(&sk->write_queue, skb);		/* Throw it on the queue */
	}

	if (sk->protinfo.rose->state == ROSE_STATE_3)
		rose_kick(sk);
}

/* 
 *	This procedure is passed a buffer descriptor for an iframe. It builds
 *	the rest of the control part of the frame and then writes it out.
 */
static void rose_send_iframe(struct sock *sk, struct sk_buff *skb)
{
	if (skb == NULL)
		return;

	skb->data[2] |= (sk->protinfo.rose->vr << 5) & 0xE0;
	skb->data[2] |= (sk->protinfo.rose->vs << 1) & 0x0E;

	rose_transmit_link(skb, sk->protinfo.rose->neighbour);	
}

void rose_kick(struct sock *sk)
{
	struct sk_buff *skb, *skbn;
	int last = 1;
	unsigned short start, end, next;

	del_timer(&sk->timer);

	start = (skb_peek(&sk->protinfo.rose->ack_queue) == NULL) ? sk->protinfo.rose->va : sk->protinfo.rose->vs;
	end   = (sk->protinfo.rose->va + ROSE_DEFAULT_WINDOW) % ROSE_MODULUS;

	if (!(sk->protinfo.rose->condition & PEER_RX_BUSY_CONDITION) &&
	    start != end                                  &&
	    skb_peek(&sk->write_queue) != NULL) {

		sk->protinfo.rose->vs = start;

		/*
		 * Transmit data until either we're out of data to send or
		 * the window is full.
		 */

		/*
		 * Dequeue the frame and copy it.
		 */
		skb  = skb_dequeue(&sk->write_queue);

		do {
			if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
				skb_queue_head(&sk->write_queue, skb);
				break;
			}

			next = (sk->protinfo.rose->vs + 1) % ROSE_MODULUS;
			last = (next == end);

			/*
			 * Transmit the frame copy.
			 */
			rose_send_iframe(sk, skbn);

			sk->protinfo.rose->vs = next;

			/*
			 * Requeue the original data frame.
			 */
			skb_queue_tail(&sk->protinfo.rose->ack_queue, skb);

		} while (!last && (skb = skb_dequeue(&sk->write_queue)) != NULL);

		sk->protinfo.rose->vl = sk->protinfo.rose->vr;
	}

	rose_set_timer(sk);
}

/*
 * The following routines are taken from page 170 of the 7th ARRL Computer
 * Networking Conference paper, as is the whole state machine.
 */

void rose_enquiry_response(struct sock *sk)
{
	if (sk->protinfo.rose->condition & OWN_RX_BUSY_CONDITION) {
		rose_write_internal(sk, ROSE_RNR);
	} else {
		rose_write_internal(sk, ROSE_RR);
	}

	sk->protinfo.rose->vl = sk->protinfo.rose->vr;
}

void rose_check_iframes_acked(struct sock *sk, unsigned short nr)
{
	if (sk->protinfo.rose->vs == nr) {
		rose_frames_acked(sk, nr);
	} else {
		if (sk->protinfo.rose->va != nr) {
			rose_frames_acked(sk, nr);
		}
	}
}

#endif
