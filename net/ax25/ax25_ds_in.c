/*
 *	AX.25 release 036
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
 *	Most of this code is based on the SDL diagrams published in the 7th
 *	ARRL Computer Networking Conference papers. The diagrams have mistakes
 *	in them, but are mostly correct. Before you modify the code could you
 *	read the SDL diagrams as the code is not obvious and probably very
 *	easy to break;
 *
 *	History
 *	AX.25 036	Jonathan(G4KLX)	Cloned from ax25_in.c
 *			Joerg(DL1BKE)	Fixed it.
 */

#include <linux/config.h>
#if defined(CONFIG_AX25_DAMA_SLAVE)
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
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

/*
 *	State machine for state 1, Awaiting Connection State.
 *	The handling of the timer(s) is in file ax25_timer.c.
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_ds_state1_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int pf, int type)
{
	switch (frametype) {
		case AX25_SABM:
			ax25->modulus = AX25_MODULUS;
			ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
			ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
			break;

		case AX25_SABME:
			ax25->modulus = AX25_EMODULUS;
			ax25->window  = ax25->ax25_dev->values[AX25_VALUES_EWINDOW];
			ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
			break;

		case AX25_DISC:
			ax25_send_control(ax25, AX25_DM, pf, AX25_RESPONSE);
			break;

		case AX25_UA:
			ax25_calculate_rtt(ax25);
			ax25->t1timer = 0;
			ax25->t3timer = ax25->t3;
			ax25->idletimer = ax25->idle;
			ax25->vs      = 0;
			ax25->va      = 0;
			ax25->vr      = 0;
			ax25->state   = AX25_STATE_3;
			ax25->n2count = 0;
			if (ax25->sk != NULL) {
				ax25->sk->state = TCP_ESTABLISHED;
				/* For WAIT_SABM connections we will produce an accept ready socket here */
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
			}
			ax25_dama_on(ax25);

			/* according to DK4EG�s spec we are required to
			 * send a RR RESPONSE FINAL NR=0. Please mail
			 * <jr@lykos.oche.de> if this causes problems
			 * with the TheNetNode DAMA Master implementation.
			 */ 

			ax25_std_enquiry_response(ax25);
			break;

		case AX25_DM:
			if (pf) {
				if (ax25->modulus == AX25_MODULUS) {
					ax25_clear_queues(ax25);
					ax25->state = AX25_STATE_0;
					if (ax25->sk != NULL) {
						ax25->sk->state     = TCP_CLOSE;
						ax25->sk->err       = ECONNREFUSED;
						ax25->sk->shutdown |= SEND_SHUTDOWN;
						if (!ax25->sk->dead)
							ax25->sk->state_change(ax25->sk);
						ax25->sk->dead      = 1;
					}
				} else {
					ax25->modulus = AX25_MODULUS;
					ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
				}
			}
			break;

		default:
			if (pf)
				ax25_send_control(ax25, AX25_SABM, AX25_POLLON, AX25_COMMAND);
			break;
	}

	return 0;
}

/*
 *	State machine for state 2, Awaiting Release State.
 *	The handling of the timer(s) is in file ax25_timer.c
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_ds_state2_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int pf, int type)
{
	switch (frametype) {
		case AX25_SABM:
		case AX25_SABME:
			ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
			ax25->dama_slave = 0;
			ax25_dama_off(ax25);
			break;

		case AX25_DISC:
			ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
			ax25->state = AX25_STATE_0;
			ax25_dama_off(ax25);

			if (ax25->sk != NULL) {
				ax25->sk->state     = TCP_CLOSE;
				ax25->sk->err       = 0;
				ax25->sk->shutdown |= SEND_SHUTDOWN;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead      = 1;
			}
			break;

		case AX25_DM:
		case AX25_UA:
			if (pf) {
				ax25->state = AX25_STATE_0;
				ax25_dama_off(ax25);

				if (ax25->sk != NULL) {
					ax25->sk->state     = TCP_CLOSE;
					ax25->sk->err       = 0;
					ax25->sk->shutdown |= SEND_SHUTDOWN;
					if (!ax25->sk->dead)
						ax25->sk->state_change(ax25->sk);
					ax25->sk->dead      = 1;
				}
			}
			break;

		case AX25_I:
		case AX25_REJ:
		case AX25_RNR:
		case AX25_RR:
			if (pf) {
				ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
				ax25->dama_slave = 0;
				ax25_dama_off(ax25);
			}
			break;

		default:
			break;
	}

	return 0;
}

/*
 *	State machine for state 3, Connected State.
 *	The handling of the timer(s) is in file ax25_timer.c
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_ds_state3_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int ns, int nr, int pf, int type)
{
	int queued = 0;

	switch (frametype) {
		case AX25_SABM:
		case AX25_SABME:
			if (frametype == AX25_SABM) {
				ax25->modulus = AX25_MODULUS;
				ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
			} else {
				ax25->modulus = AX25_EMODULUS;
				ax25->window  = ax25->ax25_dev->values[AX25_VALUES_EWINDOW];
			}
			ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
			ax25->condition = 0x00;
			ax25->t1timer   = 0;
			ax25->t3timer   = ax25->t3;
			ax25->idletimer = ax25->idle;
			ax25->vs        = 0;
			ax25->va        = 0;
			ax25->vr        = 0;
			ax25_requeue_frames(ax25);
			ax25_dama_on(ax25);
			break;

		case AX25_DISC:
			ax25_clear_queues(ax25);
			ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
			ax25_dama_off(ax25);

			if (ax25->sk != NULL) {
				ax25->sk->state     = TCP_CLOSE;
				ax25->sk->err       = 0;
				ax25->sk->shutdown |= SEND_SHUTDOWN;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead      = 1;
			}
			break;

		case AX25_DM:
			ax25_clear_queues(ax25);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
			ax25_dama_off(ax25);

			if (ax25->sk != NULL) {
				ax25->sk->state     = TCP_CLOSE;
				ax25->sk->err       = ECONNRESET;
				ax25->sk->shutdown |= SEND_SHUTDOWN;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead      = 1;
			}
			break;

		case AX25_RR:
		case AX25_RNR:
			if (frametype == AX25_RR)
				ax25->condition &= ~AX25_COND_PEER_RX_BUSY;
			else
				ax25->condition |= AX25_COND_PEER_RX_BUSY;
			if (ax25_validate_nr(ax25, nr)) {
				ax25_check_iframes_acked(ax25, nr);
				if (type == AX25_COMMAND && pf)
					ax25_ds_enquiry_response(ax25);
			} else {
				ax25_ds_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;

		case AX25_REJ:
			ax25->condition &= ~AX25_COND_PEER_RX_BUSY;
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
				ax25_calculate_rtt(ax25);
				ax25->n2count = 0;
				ax25->t1timer = 0;
				ax25->t3timer = ax25->t3;
				ax25_requeue_frames(ax25);
				if (type == AX25_COMMAND && pf)
					ax25_ds_enquiry_response(ax25);
			} else {
				ax25_ds_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;

		case AX25_I:
			if (!ax25_validate_nr(ax25, nr)) {
				ax25_ds_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
				break;
			}
			if (ax25->condition & AX25_COND_PEER_RX_BUSY) {
				ax25_frames_acked(ax25, nr);
				ax25->n2count = 0;
			} else {
				ax25_check_iframes_acked(ax25, nr);
			}
			if (ax25->condition & AX25_COND_OWN_RX_BUSY) {
				if (pf) ax25_ds_enquiry_response(ax25);
				break;
			}
			if (ns == ax25->vr) {
				ax25->vr = (ax25->vr + 1) % ax25->modulus;
				queued = ax25_rx_iframe(ax25, skb);
				if (ax25->condition & AX25_COND_OWN_RX_BUSY) {
					ax25->vr = ns;	/* ax25->vr - 1 */
					if (pf) ax25_ds_enquiry_response(ax25);
					break;
				}
				ax25->condition &= ~AX25_COND_REJECT;
				if (pf) {
					ax25_ds_enquiry_response(ax25);
				} else {
					if (!(ax25->condition & AX25_COND_ACK_PENDING)) {
						ax25->t2timer = ax25->t2;
						ax25->condition |= AX25_COND_ACK_PENDING;
					}
				}
			} else {
				if (ax25->condition & AX25_COND_REJECT) {
					if (pf) ax25_ds_enquiry_response(ax25);
				} else {
					ax25->condition |= AX25_COND_REJECT;
					ax25_ds_enquiry_response(ax25);
					ax25->condition &= ~AX25_COND_ACK_PENDING;
				}
			}
			break;

		case AX25_FRMR:
		case AX25_ILLEGAL:
			ax25_ds_establish_data_link(ax25);
			ax25->state = AX25_STATE_1;
			break;

		default:
			break;
	}

	return queued;
}

/*
 *	State machine for state 4, Timer Recovery State.
 *	The handling of the timer(s) is in file ax25_timer.c
 *	Handling of state 0 and connection release is in ax25.c.
 */
static int ax25_ds_state4_machine(ax25_cb *ax25, struct sk_buff *skb, int frametype, int ns, int nr, int pf, int type)
{
	int queued = 0;

	switch (frametype) {
		case AX25_SABM:
		case AX25_SABME:
			if (frametype == AX25_SABM) {
				ax25->modulus = AX25_MODULUS;
				ax25->window  = ax25->ax25_dev->values[AX25_VALUES_WINDOW];
			} else {
				ax25->modulus = AX25_EMODULUS;
				ax25->window  = ax25->ax25_dev->values[AX25_VALUES_EWINDOW];
			}
			ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
			ax25->condition = 0x00;
			ax25->t1timer   = 0;
			ax25->t3timer   = ax25->t3;
			ax25->idletimer = ax25->idle;
			ax25->vs        = 0;
			ax25->va        = 0;
			ax25->vr        = 0;
			ax25->state     = AX25_STATE_3;
			ax25->n2count   = 0;
			ax25_requeue_frames(ax25);
			ax25_dama_on(ax25);
			break;

		case AX25_DISC:
			ax25_clear_queues(ax25);
			ax25_send_control(ax25, AX25_UA, pf, AX25_RESPONSE);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
			ax25_dama_off(ax25);

			if (ax25->sk != NULL) {
				ax25->sk->state     = TCP_CLOSE;
				ax25->sk->err       = 0;
				ax25->sk->shutdown |= SEND_SHUTDOWN;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead      = 1;
			}
			break;

		case AX25_DM:
			ax25_clear_queues(ax25);
			ax25->t3timer = 0;
			ax25->state   = AX25_STATE_0;
			ax25_dama_off(ax25);

			if (ax25->sk != NULL) {
				ax25->sk->state     = TCP_CLOSE;
				ax25->sk->err       = ECONNRESET;
				ax25->sk->shutdown |= SEND_SHUTDOWN;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead      = 1;
			}
			break;

		case AX25_RR:
		case AX25_RNR:
			if (frametype == AX25_RR)
				ax25->condition &= ~AX25_COND_PEER_RX_BUSY;
			else
				ax25->condition |= AX25_COND_PEER_RX_BUSY;
			if (type == AX25_RESPONSE && pf) {
				ax25->t1timer = 0;
				if (ax25_validate_nr(ax25, nr)) {
					ax25_frames_acked(ax25, nr);
					ax25->n2count = 0;
					if (ax25->vs == ax25->va) {
						ax25->t3timer = ax25->t3;
						ax25->state   = AX25_STATE_3;
					} else {
						ax25_requeue_frames(ax25);
					}
				} else {
					ax25_ds_nr_error_recovery(ax25);
					ax25->state = AX25_STATE_1;
				}
				break;
			}
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
				ax25->n2count = 0;
				if (type == AX25_COMMAND && pf)
					ax25_ds_enquiry_response(ax25);
			} else {
				ax25_ds_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;

		case AX25_REJ:
			ax25->condition &= ~AX25_COND_PEER_RX_BUSY;
			if (pf) {
				ax25->t1timer = 0;
				if (ax25_validate_nr(ax25, nr)) {
					ax25_frames_acked(ax25, nr);
					ax25->n2count = 0;
					if (ax25->vs == ax25->va) {
						ax25->t3timer = ax25->t3;
						ax25->state   = AX25_STATE_3;
					} else {
						ax25_requeue_frames(ax25);
					}
					if (type == AX25_COMMAND && pf)
						ax25_ds_enquiry_response(ax25);
				} else {
					ax25_ds_nr_error_recovery(ax25);
					ax25->state = AX25_STATE_1;
				}
				break;
			}
			if (ax25_validate_nr(ax25, nr)) {
				ax25_frames_acked(ax25, nr);
				ax25->n2count = 0;
				ax25_requeue_frames(ax25);
				if (type == AX25_COMMAND && pf)
					ax25_ds_enquiry_response(ax25);
			} else {
				ax25_ds_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
			}
			break;

		case AX25_I:
			if (!ax25_validate_nr(ax25, nr)) {
				ax25_ds_nr_error_recovery(ax25);
				ax25->state = AX25_STATE_1;
				break;
			}
			ax25_frames_acked(ax25, nr);
			ax25->n2count = 0;
			if (ax25->condition & AX25_COND_OWN_RX_BUSY) {
				if (pf) ax25_ds_enquiry_response(ax25);
				break;
			}
			if (ns == ax25->vr) {
				ax25->vr = (ax25->vr + 1) % ax25->modulus;
				queued = ax25_rx_iframe(ax25, skb);
				if (ax25->condition & AX25_COND_OWN_RX_BUSY) {
					ax25->vr = ns;	/* ax25->vr - 1 */
					if (pf) ax25_ds_enquiry_response(ax25);
					break;
				}
				ax25->condition &= ~AX25_COND_REJECT;
				if (pf) {
					ax25_ds_enquiry_response(ax25);
				} else {
					if (!(ax25->condition & AX25_COND_ACK_PENDING)) {
						ax25->t2timer = ax25->t2;
						ax25->condition |= AX25_COND_ACK_PENDING;
					}
				}
			} else {
				if (ax25->condition & AX25_COND_REJECT) {
					if (pf) ax25_ds_enquiry_response(ax25);
				} else {
					ax25->condition |= AX25_COND_REJECT;
					ax25_ds_enquiry_response(ax25);
					ax25->condition &= ~AX25_COND_ACK_PENDING;
				}
			}
			break;

		case AX25_FRMR:
		case AX25_ILLEGAL:
			ax25_ds_establish_data_link(ax25);
			ax25->state = AX25_STATE_1;
			break;

		default:
			break;
	}

	return queued;
}

/*
 *	Higher level upcall for a LAPB frame
 */
int ax25_ds_frame_in(ax25_cb *ax25, struct sk_buff *skb, int type)
{
	int queued = 0, frametype, ns, nr, pf;

	frametype = ax25_decode(ax25, skb, &ns, &nr, &pf);

	switch (ax25->state) {
		case AX25_STATE_1:
			queued = ax25_ds_state1_machine(ax25, skb, frametype, pf, type);
			break;
		case AX25_STATE_2:
			queued = ax25_ds_state2_machine(ax25, skb, frametype, pf, type);
			break;
		case AX25_STATE_3:
			queued = ax25_ds_state3_machine(ax25, skb, frametype, ns, nr, pf, type);
			break;
		case AX25_STATE_4:
			queued = ax25_ds_state4_machine(ax25, skb, frametype, ns, nr, pf, type);
			break;
	}

	return queued;
}

#endif
