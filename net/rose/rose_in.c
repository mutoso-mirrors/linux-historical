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
 *	Most of this code is based on the SDL diagrams published in the 7th
 *	ARRL Computer Networking Conference papers. The diagrams have mistakes
 *	in them, but are mostly correct. Before you modify the code could you
 *	read the SDL diagrams as the code is not obvious and probably very
 *	easy to break;
 *
 *	History
 *	Rose 001	Jonathan(G4KLX)	Cloned from nr_in.c
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
#include <net/ip.h>			/* For ip_rcv */
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <net/rose.h>

static int rose_queue_rx_frame(struct sock *sk, struct sk_buff *skb, int more)
{
	struct sk_buff *skbo, *skbn = skb;

	if (more) {
		sk->protinfo.rose->fraglen += skb->len;
		skb_queue_tail(&sk->protinfo.rose->frag_queue, skb);
		return 0;
	}
	
	if (!more && sk->protinfo.rose->fraglen > 0) {	/* End of fragment */
		sk->protinfo.rose->fraglen += skb->len;
		skb_queue_tail(&sk->protinfo.rose->frag_queue, skb);

		if ((skbn = alloc_skb(sk->protinfo.rose->fraglen, GFP_ATOMIC)) == NULL)
			return 1;

		skbn->free = 1;
		skbn->arp  = 1;
		skbn->sk   = sk;
		sk->rmem_alloc += skbn->truesize;
		skbn->h.raw = skbn->data;

		skbo = skb_dequeue(&sk->protinfo.rose->frag_queue);
		memcpy(skb_put(skbn, skbo->len), skbo->data, skbo->len);
		kfree_skb(skbo, FREE_READ);

		while ((skbo = skb_dequeue(&sk->protinfo.rose->frag_queue)) != NULL) {
			skb_pull(skbo, ROSE_MIN_LEN);
			memcpy(skb_put(skbn, skbo->len), skbo->data, skbo->len);
			kfree_skb(skbo, FREE_READ);
		}

		sk->protinfo.rose->fraglen = 0;		
	}

	return sock_queue_rcv_skb(sk, skbn);
}

/*
 * State machine for state 1, Awaiting Call Accepted State.
 * The handling of the timer(s) is in file rose_timer.c.
 * Handling of state 0 and connection release is in af_rose.c.
 */
static int rose_state1_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	switch (frametype) {

		case ROSE_CALL_ACCEPTED:
			sk->protinfo.rose->timer = 0;
			sk->protinfo.rose->vs    = 0;
			sk->protinfo.rose->va    = 0;
			sk->protinfo.rose->vr    = 0;
			sk->protinfo.rose->vl    = 0;
			sk->protinfo.rose->state = ROSE_STATE_3;
			sk->state                = TCP_ESTABLISHED;
			if (!sk->dead)
				sk->state_change(sk);
			break;

		case ROSE_CLEAR_REQUEST:
			rose_clear_queues(sk);
			sk->protinfo.rose->state = ROSE_STATE_0;
			sk->state                = TCP_CLOSE;
			sk->err                  = ECONNREFUSED;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                 = 1;
			break;

		default:		/* XXX */
			printk(KERN_WARNING "rose: unknown %02X in state 1\n", frametype);
			break;
	}

	return 0;
}

/*
 * State machine for state 2, Awaiting Clear Confirmation State.
 * The handling of the timer(s) is in file rose_timer.c
 * Handling of state 0 and connection release is in af_rose.c.
 */
static int rose_state2_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	switch (frametype) {

		case ROSE_CLEAR_REQUEST:
		case ROSE_CLEAR_CONFIRMATION:
			sk->protinfo.rose->state = ROSE_STATE_0;
			sk->state                = TCP_CLOSE;
			sk->err                  = 0;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                 = 1;
			break;

		default:		/* XXX */
			printk(KERN_WARNING "rose: unknown %02X in state 2\n", frametype);
			break;
	}

	return 0;
}

/*
 * State machine for state 3, Connected State.
 * The handling of the timer(s) is in file rose_timer.c
 * Handling of state 0 and connection release is in af_rose.c.
 */
static int rose_state3_machine(struct sock *sk, struct sk_buff *skb, int frametype, int ns, int nr, int q, int d, int m)
{
	int queued = 0;

	switch (frametype) {

		case ROSE_RESET_REQUEST:
			rose_clear_queues(sk);
			rose_write_internal(sk, ROSE_RESET_CONFIRMATION);
			sk->protinfo.rose->condition = 0x00;
			sk->protinfo.rose->vs        = 0;
			sk->protinfo.rose->vr        = 0;
			sk->protinfo.rose->va        = 0;
			sk->protinfo.rose->vl        = 0;
			break;

		case ROSE_CLEAR_REQUEST:
			rose_clear_queues(sk);
			rose_write_internal(sk, ROSE_CLEAR_CONFIRMATION);
			sk->protinfo.rose->state = ROSE_STATE_0;
			sk->state                = TCP_CLOSE;
			sk->err                  = 0;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                 = 1;
			break;

		case ROSE_RR:
		case ROSE_RNR:
			if (frametype == ROSE_RNR) {
				sk->protinfo.rose->condition |= PEER_RX_BUSY_CONDITION;
			} else {
				sk->protinfo.rose->condition &= ~PEER_RX_BUSY_CONDITION;
			}
			if (!rose_validate_nr(sk, nr)) {
				rose_clear_queues(sk);
				rose_write_internal(sk, ROSE_RESET_REQUEST);
				sk->protinfo.rose->condition = 0x00;
				sk->protinfo.rose->vs        = 0;
				sk->protinfo.rose->vr        = 0;
				sk->protinfo.rose->va        = 0;
				sk->protinfo.rose->vl        = 0;
				sk->protinfo.rose->state     = ROSE_STATE_4;
				sk->protinfo.rose->timer     = sk->protinfo.rose->t2;
			} else {
				if (sk->protinfo.rose->condition & PEER_RX_BUSY_CONDITION) {
					rose_frames_acked(sk, nr);
				} else {
					rose_check_iframes_acked(sk, nr);
				}
			}
			break;
			
		case ROSE_DATA:	/* XXX */
			sk->protinfo.rose->condition &= ~PEER_RX_BUSY_CONDITION;
			if (!rose_validate_nr(sk, nr)) {
				rose_clear_queues(sk);
				rose_write_internal(sk, ROSE_RESET_REQUEST);
				sk->protinfo.rose->condition = 0x00;
				sk->protinfo.rose->vs        = 0;
				sk->protinfo.rose->vr        = 0;
				sk->protinfo.rose->va        = 0;
				sk->protinfo.rose->vl        = 0;
				sk->protinfo.rose->state     = ROSE_STATE_4;
				sk->protinfo.rose->timer     = sk->protinfo.rose->t2;
				break;
			}
			if (sk->protinfo.rose->condition & PEER_RX_BUSY_CONDITION) {
				rose_frames_acked(sk, nr);
			} else {
				rose_check_iframes_acked(sk, nr);
			}
			if (sk->protinfo.rose->condition & OWN_RX_BUSY_CONDITION)
				break;
			if (ns == sk->protinfo.rose->vr) {
				if (rose_queue_rx_frame(sk, skb, m) == 0) {
					sk->protinfo.rose->vr = (sk->protinfo.rose->vr + 1) % ROSE_MODULUS;
					queued = 1;
				} else {
					sk->protinfo.rose->condition |= OWN_RX_BUSY_CONDITION;
				}
			}
			/*
			 * If the window is full, ack the frame.
			 */
			if (((sk->protinfo.rose->vl + sk->window) % ROSE_MODULUS) == sk->protinfo.rose->vr)
				rose_enquiry_response(sk);
			break;

		default:
			printk(KERN_WARNING "rose: unknown %02X in state 3\n", frametype);
			break;
	}

	return queued;
}

/*
 * State machine for state 4, Awaiting Reset Confirmation State.
 * The handling of the timer(s) is in file rose_timer.c
 * Handling of state 0 and connection release is in af_rose.c.
 */
static int rose_state4_machine(struct sock *sk, struct sk_buff *skb, int frametype)
{
	switch (frametype) {

		case ROSE_RESET_CONFIRMATION:
		case ROSE_RESET_REQUEST:
			sk->protinfo.rose->timer     = 0;
			sk->protinfo.rose->condition = 0x00;
			sk->protinfo.rose->va        = 0;
			sk->protinfo.rose->vr        = 0;
			sk->protinfo.rose->vs        = 0;
			sk->protinfo.rose->vl        = 0;
			sk->protinfo.rose->state     = ROSE_STATE_3;
			break;

		case ROSE_CLEAR_REQUEST:
			rose_clear_queues(sk);
			rose_write_internal(sk, ROSE_CLEAR_CONFIRMATION);
			sk->protinfo.rose->timer = 0;
			sk->protinfo.rose->state = ROSE_STATE_0;
			sk->state                = TCP_CLOSE;
			sk->err                  = 0;
			if (!sk->dead)
				sk->state_change(sk);
			sk->dead                 = 1;
			break;

		default:		/* XXX */
			printk(KERN_WARNING "rose: unknown %02X in state 4\n", frametype);
			break;
	}

	return 0;
}

/* Higher level upcall for a LAPB frame */
int rose_process_rx_frame(struct sock *sk, struct sk_buff *skb)
{
	int queued = 0, frametype, ns, nr, q, d, m;
	
	if (sk->protinfo.rose->state == ROSE_STATE_0 && sk->dead)
		return queued;

	if (sk->protinfo.rose->state != ROSE_STATE_1 && sk->protinfo.rose->state != ROSE_STATE_2 &&
	    sk->protinfo.rose->state != ROSE_STATE_3 && sk->protinfo.rose->state != ROSE_STATE_4) {
		printk(KERN_ERR "rose_process_rx_frame: frame received - state: %d\n", sk->protinfo.rose->state);
		return queued;
	}

	del_timer(&sk->timer);

	frametype = rose_decode(skb, &ns, &nr, &q, &d, &m);

	switch (sk->protinfo.rose->state) {
		case ROSE_STATE_1:
			queued = rose_state1_machine(sk, skb, frametype);
			break;
		case ROSE_STATE_2:
			queued = rose_state2_machine(sk, skb, frametype);
			break;
		case ROSE_STATE_3:
			queued = rose_state3_machine(sk, skb, frametype, ns, nr, q, d, m);
			break;
		case ROSE_STATE_4:
			queued = rose_state4_machine(sk, skb, frametype);
			break;
	}

	rose_set_timer(sk);

	return queued;
}

#endif
