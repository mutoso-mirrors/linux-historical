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
 *	History
 *	AX.25 006	Alan(GW4PTS)		Nearly died of shock - it's working 8-)
 *	AX.25 007	Alan(GW4PTS)		Removed the silliest bugs
 *	AX.25 008	Alan(GW4PTS)		Cleaned up, fixed a few state machine problems, added callbacks
 *	AX.25 009	Alan(GW4PTS)		Emergency patch kit to fix memory corruption
 * 	AX.25 010	Alan(GW4PTS)		Added RAW sockets/Digipeat.
 *	AX.25 011	Alan(GW4PTS)		RAW socket and datagram fixes (thanks) - Raw sendto now gets PID right
 *						datagram sendto uses correct target address.
 *	AX.25 012	Alan(GW4PTS)		Correct incoming connection handling, send DM to failed connects.
 *						Use skb->data not skb+1. Support sk->priority correctly.
 *						Correct receive on SOCK_DGRAM.
 *	AX.25 013	Alan(GW4PTS)		Send DM to all unknown frames, missing initialiser fixed
 *						Leave spare SSID bits set (DAMA etc) - thanks for bug report,
 *						removed device registration (it's not used or needed). Clean up for
 *						gcc 2.5.8. PID to AX25_P_
 *	AX.25 014	Alan(GW4PTS)		Cleanup and NET3 merge
 *	AX.25 015	Alan(GW4PTS)		Internal test version.
 *	AX.25 016	Alan(GW4PTS)		Semi Internal version for PI card
 *						work.
 *	AX.25 017	Alan(GW4PTS)		Fixed some small bugs reported by
 *						G4KLX
 *	AX.25 018	Alan(GW4PTS)		Fixed a small error in SOCK_DGRAM
 *	AX.25 019	Alan(GW4PTS)		Clean ups for the non INET kernel and device ioctls in AX.25
 *	AX.25 020	Jonathan(G4KLX)		/proc support and other changes.
 *	AX.25 021	Alan(GW4PTS)		Added AX25_T1, AX25_N2, AX25_T3 as requested.
 *	AX.25 022	Jonathan(G4KLX)		More work on the ax25 auto router and /proc improved (again)!
 *			Alan(GW4PTS)		Added TIOCINQ/OUTQ
 *	AX.25 023	Alan(GW4PTS)		Fixed shutdown bug
 *	AX.25 023	Alan(GW4PTS)		Linus changed timers
 *	AX.25 024	Alan(GW4PTS)		Small bug fixes
 *	AX.25 025	Alan(GW4PTS)		More fixes, Linux 1.1.51 compatibility stuff, timers again!
 *	AX.25 026	Alan(GW4PTS)		Small state fix.
 *	AX.25 027	Alan(GW4PTS)		Socket close crash fixes.
 *	AX.25 028	Alan(GW4PTS)		Callsign control including settings per uid.
 *						Small bug fixes.
 *						Protocol set by sockets only.
 *						Small changes to allow for start of NET/ROM layer.
 *	AX.25 028a	Jonathan(G4KLX)		Changes to state machine.
 *	AX.25 028b	Jonathan(G4KLX)		Extracted ax25 control block
 *						from sock structure.
 *	AX.25 029	Alan(GW4PTS)		Combined 028b and some KA9Q code
 *			Jonathan(G4KLX)		and removed all the old Berkeley, added IP mode registration.
 *			Darryl(G7LED)		stuff. Cross-port digipeating. Minor fixes and enhancements.
 *			Alan(GW4PTS)		Missed suser() on axassociate checks
 *	AX.25 030	Alan(GW4PTS)		Added variable length headers.
 *			Jonathan(G4KLX)		Added BPQ Ethernet interface.
 *			Steven(GW7RRM)		Added digi-peating control ioctl.
 *						Added extended AX.25 support.
 *						Added AX.25 frame segmentation.
 *			Darryl(G7LED)		Changed connect(), recvfrom(), sendto() sockaddr/addrlen to
 *						fall inline with bind() and new policy.
 *						Moved digipeating ctl to new ax25_dev structs.
 *						Fixed ax25_release(), set TCP_CLOSE, wakeup app
 *						context, THEN make the sock dead.
 *			Alan(GW4PTS)		Cleaned up for single recvmsg methods.
 *			Alan(GW4PTS)		Fixed not clearing error on connect failure.
 *	AX.25 031	Jonathan(G4KLX)		Added binding to any device.
 *			Joerg(DL1BKE)		Added DAMA support, fixed (?) digipeating, fixed buffer locking
 *						for "virtual connect" mode... Result: Probably the
 *						"Most Buggiest Code You've Ever Seen" (TM)
 *			HaJo(DD8NE)		Implementation of a T5 (idle) timer
 *			Joerg(DL1BKE)		Renamed T5 to IDLE and changed behaviour:
 *						the timer gets reloaded on every received or transmitted
 *						I frame for IP or NETROM. The idle timer is not active
 *						on "vanilla AX.25" connections. Furthermore added PACLEN
 *						to provide AX.25-layer based fragmentation (like WAMPES)
 *      AX.25 032	Joerg(DL1BKE)		Fixed DAMA timeout error.
 *						ax25_send_frame() limits the number of enqueued
 *						datagrams per socket.
 *	AX.25 033	Jonathan(G4KLX)		Removed auto-router.
 *			Hans(PE1AYX)		Converted to Module.
 *			Joerg(DL1BKE)		Moved BPQ Ethernet to seperate driver.
 *	AX.25 034	Jonathan(G4KLX)		2.1 changes
 *			Alan(GW4PTS)		Small POSIXisations
 *	AX.25 035	Alan(GW4PTS)		Started fixing to the new
 *						format.
 *			Hans(PE1AYX)		Fixed interface to IP layer.
 *			Alan(GW4PTS)		Added asynchronous support.
 *			Frederic(F1OAT)		Support for pseudo-digipeating.
 *			Jonathan(G4KLX)		Support for packet forwarding.
 *	AX.25 036	Jonathan(G4KLX)		Major restructuring.
 *			Joerg(DL1BKE)		Fixed DAMA Slave.
 */

#include <linux/config.h>
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
#include <linux/module.h>
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
#include <linux/if_arp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/termios.h>	/* For TIOCINQ/OUTQ */
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/firewall.h>
#include <linux/sysctl.h>
#include <net/ip.h>
#include <net/arp.h>

ax25_cb *volatile ax25_list = NULL;

static struct proto_ops ax25_proto_ops;

/*
 *	Free an allocated ax25 control block. This is done to centralise
 *	the MOD count code.
 */
void ax25_free_cb(ax25_cb *ax25)
{
	if (ax25->digipeat != NULL) {
		kfree_s(ax25->digipeat, sizeof(ax25_digi));
		ax25->digipeat = NULL;
	}

	kfree_s(ax25, sizeof(ax25_cb));

	MOD_DEC_USE_COUNT;
}

/*
 *	Socket removal during an interrupt is now safe.
 */
static void ax25_remove_socket(ax25_cb *ax25)
{
	ax25_cb *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	if ((s = ax25_list) == ax25) {
		ax25_list = s->next;
		restore_flags(flags);
		return;
	}

	while (s != NULL && s->next != NULL) {
		if (s->next == ax25) {
			s->next = ax25->next;
			restore_flags(flags);
			return;
		}

		s = s->next;
	}

	restore_flags(flags);
}

/*
 *	Kill all bound sockets on a dropped device.
 */
static void ax25_kill_by_device(struct device *dev)
{
	ax25_dev *ax25_dev;
	ax25_cb *s;

	if ((ax25_dev = ax25_dev_ax25dev(dev)) == NULL)
		return;

	for (s = ax25_list; s != NULL; s = s->next) {
		if (s->ax25_dev == ax25_dev) {
			s->state    = AX25_STATE_0;
			s->ax25_dev = NULL;
			if (s->sk != NULL) {
				s->sk->state     = TCP_CLOSE;
				s->sk->err       = ENETUNREACH;
				s->sk->shutdown |= SEND_SHUTDOWN;
				if (!s->sk->dead)
					s->sk->state_change(s->sk);
				s->sk->dead  = 1;
			}
		}
	}
}

/*
 *	Handle device status changes.
 */
static int ax25_device_event(struct notifier_block *this,unsigned long event, void *ptr)
{
	struct device *dev = (struct device *)ptr;

	/* Reject non AX.25 devices */
	if (dev->type != ARPHRD_AX25)
		return NOTIFY_DONE;

	switch (event) {
		case NETDEV_UP:
			ax25_dev_device_up(dev);
			break;
		case NETDEV_DOWN:
			ax25_kill_by_device(dev);
			ax25_rt_device_down(dev);
			ax25_dev_device_down(dev);
			break;
		default:
			break;
	}

	return NOTIFY_DONE;
}

/*
 *	Add a socket to the bound sockets list.
 */
void ax25_insert_socket(ax25_cb *ax25)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	ax25->next = ax25_list;
	ax25_list  = ax25;

	restore_flags(flags);
}

/*
 *	Find a socket that wants to accept the SABM we have just
 *	received.
 */
struct sock *ax25_find_listener(ax25_address *addr, int digi, struct device *dev, int type)
{
	unsigned long flags;
	ax25_cb *s;

	save_flags(flags);
	cli();

	for (s = ax25_list; s != NULL; s = s->next) {
		if ((s->iamdigi && !digi) || (!s->iamdigi && digi))
			continue;
		if (s->sk != NULL && ax25cmp(&s->source_addr, addr) == 0 && s->sk->type == type && s->sk->state == TCP_LISTEN) {
			/* If device is null we match any device */
			if (s->ax25_dev == NULL || s->ax25_dev->dev == dev) {
				restore_flags(flags);
				return s->sk;
			}
		}
	}

	restore_flags(flags);
	return NULL;
}

/*
 *	Find an AX.25 socket given both ends.
 */
struct sock *ax25_find_socket(ax25_address *my_addr, ax25_address *dest_addr, int type)
{
	ax25_cb *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = ax25_list; s != NULL; s = s->next) {
		if (s->sk != NULL && ax25cmp(&s->source_addr, my_addr) == 0 && ax25cmp(&s->dest_addr, dest_addr) == 0 && s->sk->type == type) {
			restore_flags(flags);
			return s->sk;
		}
	}

	restore_flags(flags);

	return NULL;
}

/*
 *	Find an AX.25 control block given both ends. It will only pick up
 *	floating AX.25 control blocks or non Raw socket bound control blocks.
 */
ax25_cb *ax25_find_cb(ax25_address *my_addr, ax25_address *dest_addr, ax25_digi *digi, struct device *dev)
{
	ax25_cb *s;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (s = ax25_list; s != NULL; s = s->next) {
		if (s->sk != NULL && s->sk->type != SOCK_SEQPACKET)
			continue;
		if (ax25cmp(&s->source_addr, my_addr) == 0 && ax25cmp(&s->dest_addr, dest_addr) == 0 && s->ax25_dev->dev == dev) {
			if (digi != NULL) {
				if (s->digipeat == NULL && digi->ndigi != 0)
					continue;
				if (s->digipeat != NULL && ax25digicmp(s->digipeat, digi) != 0)
					continue;
			}
			restore_flags(flags);
			return s;
		}
	}

	restore_flags(flags);

	return NULL;
}

/*
 *	Look for any matching address - RAW sockets can bind to arbitrary names
 */
struct sock *ax25_addr_match(ax25_address *addr)
{
	unsigned long flags;
	ax25_cb *s;

	save_flags(flags);
	cli();

	for (s = ax25_list; s != NULL; s = s->next) {
		if (s->sk != NULL && ax25cmp(&s->source_addr, addr) == 0 && s->sk->type == SOCK_RAW) {
			restore_flags(flags);
			return s->sk;
		}
	}

	restore_flags(flags);

	return NULL;
}

void ax25_send_to_raw(struct sock *sk, struct sk_buff *skb, int proto)
{
	struct sk_buff *copy;

	while (sk != NULL) {
		if (sk->type == SOCK_RAW &&
		    sk->protocol == proto &&
		    atomic_read(&sk->rmem_alloc) <= sk->rcvbuf) {
			if ((copy = skb_clone(skb, GFP_ATOMIC)) == NULL)
				return;

			if (sock_queue_rcv_skb(sk, copy) != 0)
				kfree_skb(copy, FREE_READ);
		}

		sk = sk->next;
	}
}

/*
 *	Deferred destroy.
 */
void ax25_destroy_socket(ax25_cb *);

/*
 *	Handler for deferred kills.
 */
static void ax25_destroy_timer(unsigned long data)
{
	ax25_destroy_socket((ax25_cb *)data);
}

/*
 *	This is called from user mode and the timers. Thus it protects itself against
 *	interrupt users but doesn't worry about being called during work.
 *	Once it is removed from the queue no interrupt or bottom half will
 *	touch it and we are (fairly 8-) ) safe.
 */
void ax25_destroy_socket(ax25_cb *ax25)	/* Not static as it's used by the timer */
{
	struct sk_buff *skb;
	unsigned long flags;

	save_flags(flags);
	cli();

	del_timer(&ax25->timer);

	ax25_remove_socket(ax25);
	ax25_clear_queues(ax25);	/* Flush the queues */

	if (ax25->sk != NULL) {
		while ((skb = skb_dequeue(&ax25->sk->receive_queue)) != NULL) {
			if (skb->sk != ax25->sk) {			/* A pending connection */
				skb->sk->dead = 1;	/* Queue the unaccepted socket for death */
				ax25_set_timer(skb->sk->protinfo.ax25);
				skb->sk->protinfo.ax25->state = AX25_STATE_0;
			}

			kfree_skb(skb, FREE_READ);
		}
	}

	if (ax25->sk != NULL) {
		if (atomic_read(&ax25->sk->wmem_alloc) != 0 ||
		    atomic_read(&ax25->sk->rmem_alloc) != 0) {
			/* Defer: outstanding buffers */
			init_timer(&ax25->timer);
			ax25->timer.expires  = jiffies + 10 * HZ;
			ax25->timer.function = ax25_destroy_timer;
			ax25->timer.data     = (unsigned long)ax25;
			add_timer(&ax25->timer);
		} else {
			sk_free(ax25->sk);
			ax25_free_cb(ax25);
		}
	} else {
		ax25_free_cb(ax25);
	}

	restore_flags(flags);
}

/*
 * dl1bke 960311: set parameters for existing AX.25 connections,
 *		  includes a KILL command to abort any connection.
 *		  VERY useful for debugging ;-)
 */
static int ax25_ctl_ioctl(const unsigned int cmd, void *arg)
{
	struct ax25_ctl_struct ax25_ctl;
	ax25_dev *ax25_dev;
	ax25_cb *ax25;
	unsigned long flags;
	int err;

	if ((err = verify_area(VERIFY_READ, arg, sizeof(ax25_ctl))) != 0)
		return err;

	copy_from_user(&ax25_ctl, arg, sizeof(ax25_ctl));

	if ((ax25_dev = ax25_addr_ax25dev(&ax25_ctl.port_addr)) == NULL)
		return -ENODEV;

	if ((ax25 = ax25_find_cb(&ax25_ctl.source_addr, &ax25_ctl.dest_addr, NULL, ax25_dev->dev)) == NULL)
		return -ENOTCONN;

	switch (ax25_ctl.cmd) {
		case AX25_KILL:
			ax25_clear_queues(ax25);
			ax25_send_control(ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
			ax25->state = AX25_STATE_0;
#ifdef CONFIG_AX25_DAMA_SLAVE
			if (ax25_dev->dama.slave && ax25->ax25_dev->values[AX25_VALUES_PROTOCOL] == AX25_PROTO_DAMA_SLAVE)
				ax25_dama_off(ax25);
#endif

			if (ax25->sk != NULL) {
				ax25->sk->state     = TCP_CLOSE;
				ax25->sk->err       = ENETRESET;
				ax25->sk->shutdown |= SEND_SHUTDOWN;
				if (!ax25->sk->dead)
					ax25->sk->state_change(ax25->sk);
				ax25->sk->dead  = 1;
			}

			ax25_set_timer(ax25);
	  		break;

	  	case AX25_WINDOW:
	  		if (ax25->modulus == AX25_MODULUS) {
	  			if (ax25_ctl.arg < 1 || ax25_ctl.arg > 7)
	  				return -EINVAL;
	  		} else {
	  			if (ax25_ctl.arg < 1 || ax25_ctl.arg > 63)
	  				return -EINVAL;
	  		}
	  		ax25->window = ax25_ctl.arg;
	  		break;

	  	case AX25_T1:
  			if (ax25_ctl.arg < 1)
  				return -EINVAL;
  			ax25->rtt = (ax25_ctl.arg * AX25_SLOWHZ) / 2;
  			ax25->t1 = ax25_ctl.arg * AX25_SLOWHZ;
  			save_flags(flags); cli();
  			if (ax25->t1timer > ax25->t1)
  				ax25->t1timer = ax25->t1;
  			restore_flags(flags);
  			break;

	  	case AX25_T2:
	  		if (ax25_ctl.arg < 1)
	  			return -EINVAL;
	  		save_flags(flags); cli();
	  		ax25->t2 = ax25_ctl.arg * AX25_SLOWHZ;
	  		if (ax25->t2timer > ax25->t2)
	  			ax25->t2timer = ax25->t2;
	  		restore_flags(flags);
	  		break;

	  	case AX25_N2:
	  		if (ax25_ctl.arg < 1 || ax25_ctl.arg > 31)
	  			return -EINVAL;
	  		ax25->n2count = 0;
	  		ax25->n2 = ax25_ctl.arg;
	  		break;

	  	case AX25_T3:
	  		if (ax25_ctl.arg < 0)
	  			return -EINVAL;
	  		save_flags(flags); cli();
	  		ax25->t3 = ax25_ctl.arg * AX25_SLOWHZ;
	  		if (ax25->t3timer != 0)
	  			ax25->t3timer = ax25->t3;
	  		restore_flags(flags);
	  		break;

	  	case AX25_IDLE:
	  		if (ax25_ctl.arg < 0)
	  			return -EINVAL;
			save_flags(flags); cli();
	  		ax25->idle = ax25_ctl.arg * AX25_SLOWHZ * 60;
	  		if (ax25->idletimer != 0)
	  			ax25->idletimer = ax25->idle;
	  		restore_flags(flags);
	  		break;

	  	case AX25_PACLEN:
	  		if (ax25_ctl.arg < 16 || ax25_ctl.arg > 65535)
	  			return -EINVAL;
	  		ax25->paclen = ax25_ctl.arg;
	  		break;

	  	default:
	  		return -EINVAL;
	  }

	  return 0;
}

/*
 * Create an empty AX.25 control block.
 */
ax25_cb *ax25_create_cb(void)
{
	ax25_cb *ax25;

	if ((ax25 = kmalloc(sizeof(*ax25), GFP_ATOMIC)) == NULL)
		return NULL;

	MOD_INC_USE_COUNT;

	memset(ax25, 0x00, sizeof(*ax25));

	skb_queue_head_init(&ax25->write_queue);
	skb_queue_head_init(&ax25->frag_queue);
	skb_queue_head_init(&ax25->ack_queue);
	skb_queue_head_init(&ax25->reseq_queue);

	init_timer(&ax25->timer);

	ax25->rtt     = AX25_DEF_T1 / 2;
	ax25->t1      = AX25_DEF_T1;
	ax25->t2      = AX25_DEF_T2;
	ax25->t3      = AX25_DEF_T3;
	ax25->n2      = AX25_DEF_N2;
	ax25->paclen  = AX25_DEF_PACLEN;
	ax25->idle    = AX25_DEF_IDLE;

	if (AX25_DEF_AXDEFMODE) {
		ax25->modulus = AX25_EMODULUS;
		ax25->window  = AX25_DEF_EWINDOW;
	} else {
		ax25->modulus = AX25_MODULUS;
		ax25->window  = AX25_DEF_WINDOW;
	}

	ax25->backoff = AX25_DEF_BACKOFF;
	ax25->state   = AX25_STATE_0;

	return ax25;
}

/*
 *	Fill in a created AX.25 created control block with the default
 *	values for a particular device.
 */
void ax25_fillin_cb(ax25_cb *ax25, ax25_dev *ax25_dev)
{
	ax25->ax25_dev = ax25_dev;

	ax25->rtt      = ax25_dev->values[AX25_VALUES_T1];
	ax25->t1       = ax25_dev->values[AX25_VALUES_T1];
	ax25->t2       = ax25_dev->values[AX25_VALUES_T2];
	ax25->t3       = ax25_dev->values[AX25_VALUES_T3];
	ax25->n2       = ax25_dev->values[AX25_VALUES_N2];
	ax25->paclen   = ax25_dev->values[AX25_VALUES_PACLEN];
	ax25->idle     = ax25_dev->values[AX25_VALUES_IDLE];

	if (ax25_dev->values[AX25_VALUES_AXDEFMODE]) {
		ax25->modulus = AX25_EMODULUS;
		ax25->window  = ax25_dev->values[AX25_VALUES_EWINDOW];
	} else {
		ax25->modulus = AX25_MODULUS;
		ax25->window  = ax25_dev->values[AX25_VALUES_WINDOW];
	}

	ax25->backoff = ax25_dev->values[AX25_VALUES_BACKOFF];
}

/*
 *	Handling for system calls applied via the various interfaces to an
 *	AX25 socket object
 */

static int ax25_setsockopt(struct socket *sock, int level, int optname, char *optval, int optlen)
{
	struct sock *sk = sock->sk;
	int opt;

	if (level != SOL_AX25)
		return -ENOPROTOOPT;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(opt, (int *)optval))
		return -EFAULT;

	switch (optname) {
		case AX25_WINDOW:
			if (sk->protinfo.ax25->modulus == AX25_MODULUS) {
				if (opt < 1 || opt > 7)
					return -EINVAL;
			} else {
				if (opt < 1 || opt > 63)
					return -EINVAL;
			}
			sk->protinfo.ax25->window = opt;
			return 0;

		case AX25_T1:
			if (opt < 1)
				return -EINVAL;
			sk->protinfo.ax25->rtt = (opt * AX25_SLOWHZ) / 2;
			return 0;

		case AX25_T2:
			if (opt < 1)
				return -EINVAL;
			sk->protinfo.ax25->t2 = opt * AX25_SLOWHZ;
			return 0;

		case AX25_N2:
			if (opt < 1 || opt > 31)
				return -EINVAL;
			sk->protinfo.ax25->n2 = opt;
			return 0;

		case AX25_T3:
			if (opt < 1)
				return -EINVAL;
			sk->protinfo.ax25->t3 = opt * AX25_SLOWHZ;
			return 0;

		case AX25_IDLE:
			if (opt < 0)
				return -EINVAL;
			sk->protinfo.ax25->idle = opt * AX25_SLOWHZ * 60;
			return 0;

		case AX25_BACKOFF:
			if (opt < 0 || opt > 2)
				return -EINVAL;
			sk->protinfo.ax25->backoff = opt;
			return 0;

		case AX25_EXTSEQ:
			sk->protinfo.ax25->modulus = opt ? AX25_EMODULUS : AX25_MODULUS;
			return 0;

		case AX25_HDRINCL:
			sk->protinfo.ax25->hdrincl = opt ? 1 : 0;
			return 0;

		case AX25_IAMDIGI:
			sk->protinfo.ax25->iamdigi = opt ? 1 : 0;
			return 0;

		case AX25_PACLEN:
			if (opt < 16 || opt > 65535)
				return -EINVAL;
			sk->protinfo.ax25->paclen = opt;
			return 0;

		default:
			return -ENOPROTOOPT;
	}
}

static int ax25_getsockopt(struct socket *sock, int level, int optname, char *optval, int *optlen)
{
	struct sock *sk = sock->sk;
	int val = 0;
	int len;

	if (level != SOL_AX25)
		return -ENOPROTOOPT;

	if (get_user(len, optlen))
		return -EFAULT;

	switch (optname) {
		case AX25_WINDOW:
			val = sk->protinfo.ax25->window;
			break;

		case AX25_T1:
			val = (sk->protinfo.ax25->t1 * 2) / AX25_SLOWHZ;
			break;

		case AX25_T2:
			val = sk->protinfo.ax25->t2 / AX25_SLOWHZ;
			break;

		case AX25_N2:
			val = sk->protinfo.ax25->n2;
			break;

		case AX25_T3:
			val = sk->protinfo.ax25->t3 / AX25_SLOWHZ;
			break;

		case AX25_IDLE:
			val = sk->protinfo.ax25->idle / (AX25_SLOWHZ * 60);
			break;

		case AX25_BACKOFF:
			val = sk->protinfo.ax25->backoff;
			break;

		case AX25_EXTSEQ:
			val = (sk->protinfo.ax25->modulus == AX25_EMODULUS);
			break;

		case AX25_HDRINCL:
			val = sk->protinfo.ax25->hdrincl;
			break;

		case AX25_IAMDIGI:
			val = sk->protinfo.ax25->iamdigi;
			break;

		case AX25_PACLEN:
			val = sk->protinfo.ax25->paclen;
			break;

		default:
			return -ENOPROTOOPT;
	}

	len = min(len, sizeof(int));

	if (put_user(len, optlen))
		return -EFAULT;

	if (copy_to_user(optval, &val, len))
		return -EFAULT;

	return 0;
}

static int ax25_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;

	if (sk->type == SOCK_SEQPACKET && sk->state != TCP_LISTEN) {
		sk->max_ack_backlog = backlog;
		sk->state           = TCP_LISTEN;
		return 0;
	}

	return -EOPNOTSUPP;
}

int ax25_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	ax25_cb *ax25;

	switch (sock->type) {
		case SOCK_DGRAM:
			if (protocol == 0 || protocol == AF_AX25)
				protocol = AX25_P_TEXT;
			break;
		case SOCK_SEQPACKET:
			switch (protocol) {
				case 0:
				case AF_AX25:	/* For CLX */
					protocol = AX25_P_TEXT;
					break;
				case AX25_P_SEGMENT:
#ifdef CONFIG_INET
				case AX25_P_ARP:
				case AX25_P_IP:
#endif
#ifdef CONFIG_NETROM
				case AX25_P_NETROM:
#endif
#ifdef CONFIG_ROSE
				case AX25_P_ROSE:
#endif
					return -ESOCKTNOSUPPORT;
#ifdef CONFIG_NETROM_MODULE
				case AX25_P_NETROM:
					if (ax25_protocol_is_registered(AX25_P_NETROM))
						return -ESOCKTNOSUPPORT;
#endif
#ifdef CONFIG_ROSE_MODULE
				case AX25_P_ROSE:
					if (ax25_protocol_is_registered(AX25_P_ROSE))
						return -ESOCKTNOSUPPORT;
#endif
				default:
					break;
			}
			break;
		case SOCK_RAW:
			break;
		default:
			return -ESOCKTNOSUPPORT;
	}

	if ((sk = sk_alloc(GFP_ATOMIC)) == NULL)
		return -ENOMEM;

	if ((ax25 = ax25_create_cb()) == NULL) {
		sk_free(sk);
		return -ENOMEM;
	}

	sock_init_data(sock, sk);
	
	sock->ops    = &ax25_proto_ops;
	sk->protocol = protocol;
	sk->mtu      = AX25_MTU;	/* 256 */

	ax25->sk          = sk;
	sk->protinfo.ax25 = ax25;

	return 0;
}

struct sock *ax25_make_new(struct sock *osk, struct device *dev)
{
	struct sock *sk;
	ax25_cb *ax25;

	if ((sk = sk_alloc(GFP_ATOMIC)) == NULL)
		return NULL;

	if ((ax25 = ax25_create_cb()) == NULL) {
		sk_free(sk);
		return NULL;
	}

	switch (osk->type) {
		case SOCK_DGRAM:
			break;
		case SOCK_SEQPACKET:
			break;
		default:
			sk_free(sk);
			ax25_free_cb(ax25);
			return NULL;
	}

	sock_init_data(NULL, sk);
	
	sk->type     = osk->type;
	sk->socket   = osk->socket;
	sk->priority = osk->priority;
	sk->protocol = osk->protocol;
	sk->rcvbuf   = osk->rcvbuf;
	sk->sndbuf   = osk->sndbuf;
	sk->debug    = osk->debug;
	sk->state    = TCP_ESTABLISHED;
	sk->mtu      = osk->mtu;
	sk->sleep    = osk->sleep;
	sk->zapped   = osk->zapped;

	ax25->modulus = osk->protinfo.ax25->modulus;
	ax25->backoff = osk->protinfo.ax25->backoff;
	ax25->hdrincl = osk->protinfo.ax25->hdrincl;
	ax25->iamdigi = osk->protinfo.ax25->iamdigi;
	ax25->rtt     = osk->protinfo.ax25->rtt;
	ax25->t1      = osk->protinfo.ax25->t1;
	ax25->t2      = osk->protinfo.ax25->t2;
	ax25->t3      = osk->protinfo.ax25->t3;
	ax25->n2      = osk->protinfo.ax25->n2;
	ax25->idle    = osk->protinfo.ax25->idle;
	ax25->paclen  = osk->protinfo.ax25->paclen;
	ax25->window  = osk->protinfo.ax25->window;

	ax25->ax25_dev    = osk->protinfo.ax25->ax25_dev;
	ax25->source_addr = osk->protinfo.ax25->source_addr;

	if (osk->protinfo.ax25->digipeat != NULL) {
		if ((ax25->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
			sk_free(sk);
			ax25_free_cb(ax25);
			return NULL;
		}

		*ax25->digipeat = *osk->protinfo.ax25->digipeat;
	}

	sk->protinfo.ax25 = ax25;
	ax25->sk          = sk;

	return sk;
}

static int ax25_dup(struct socket *newsock, struct socket *oldsock)
{
	struct sock *sk = oldsock->sk;

	if (sk == NULL || newsock == NULL)
		return -EINVAL;

	return ax25_create(newsock, sk->protocol);
}

static int ax25_release(struct socket *sock, struct socket *peer)
{
	struct sock *sk = sock->sk;

	if (sk == NULL) return 0;

	if (sk->type == SOCK_SEQPACKET) {
		switch (sk->protinfo.ax25->state) {
			case AX25_STATE_0:
				sk->state       = TCP_CLOSE;
				sk->shutdown   |= SEND_SHUTDOWN;
				sk->state_change(sk);
				sk->dead        = 1;
				ax25_destroy_socket(sk->protinfo.ax25);
				break;

			case AX25_STATE_1:
			case AX25_STATE_2:
				ax25_clear_queues(sk->protinfo.ax25);
				ax25_send_control(sk->protinfo.ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
				sk->protinfo.ax25->state = AX25_STATE_0;
				sk->state                = TCP_CLOSE;
				sk->shutdown            |= SEND_SHUTDOWN;
				sk->state_change(sk);
				sk->dead                 = 1;
				ax25_destroy_socket(sk->protinfo.ax25);
				break;

			case AX25_STATE_3:
			case AX25_STATE_4:
				ax25_clear_queues(sk->protinfo.ax25);
				sk->protinfo.ax25->n2count = 0;
				switch (sk->protinfo.ax25->ax25_dev->values[AX25_VALUES_PROTOCOL]) {
					case AX25_PROTO_STD_SIMPLEX:
					case AX25_PROTO_STD_DUPLEX:
						ax25_send_control(sk->protinfo.ax25, AX25_DISC, AX25_POLLON, AX25_COMMAND);
						sk->protinfo.ax25->t3timer = 0;
						break;
#ifdef AX25_CONFIG_DAMA_SLAVE
					case AX25_PROTO_DAMA_SLAVE:
						sk->protinfo.ax25->t3timer = 0;
						break;
#endif
				}
				sk->protinfo.ax25->t1timer = sk->protinfo.ax25->t1 = ax25_calculate_t1(sk->protinfo.ax25);
				sk->protinfo.ax25->state   = AX25_STATE_2;
				sk->state                  = TCP_CLOSE;
				sk->shutdown              |= SEND_SHUTDOWN;
				sk->state_change(sk);
				sk->dead                   = 1;
				sk->destroy                = 1;
				break;

			default:
				break;
		}
	} else {
		sk->state       = TCP_CLOSE;
		sk->shutdown   |= SEND_SHUTDOWN;
		sk->state_change(sk);
		sk->dead        = 1;
		ax25_destroy_socket(sk->protinfo.ax25);
	}

	sock->sk   = NULL;	
	sk->socket = NULL;	/* Not used, but we should do this */

	return 0;
}

/*
 *	We support a funny extension here so you can (as root) give any callsign
 *	digipeated via a local address as source. This is a hack until we add
 *	BSD 4.4 ADDIFADDR type support. It is however small and trivially backward
 *	compatible 8)
 */
static int ax25_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sock *sk = sock->sk;
	struct full_sockaddr_ax25 *addr = (struct full_sockaddr_ax25 *)uaddr;
	ax25_address *call;
	ax25_dev *ax25_dev = NULL;

	if (sk->zapped == 0)
		return -EINVAL;

	if (addr_len != sizeof(struct sockaddr_ax25) && addr_len != sizeof(struct full_sockaddr_ax25))
		return -EINVAL;

	if (addr->fsa_ax25.sax25_family != AF_AX25)
		return -EINVAL;

	call = ax25_findbyuid(current->euid);
	if (call == NULL && ax25_uid_policy && !suser())
		return -EACCES;

	if (call == NULL)
		sk->protinfo.ax25->source_addr = addr->fsa_ax25.sax25_call;
	else
		sk->protinfo.ax25->source_addr = *call;

	SOCK_DEBUG(sk, "AX25: source address set to %s\n", ax2asc(&sk->protinfo.ax25->source_addr));

	if (addr_len == sizeof(struct full_sockaddr_ax25) && addr->fsa_ax25.sax25_ndigis == 1) {
		if (ax25cmp(&addr->fsa_digipeater[0], &null_ax25_address) == 0) {
			ax25_dev = NULL;
			SOCK_DEBUG(sk, "AX25: bound to any device\n");
		} else {
			if ((ax25_dev = ax25_addr_ax25dev(&addr->fsa_digipeater[0])) == NULL) {
				SOCK_DEBUG(sk, "AX25: bind failed - no device\n");
				return -EADDRNOTAVAIL;
			}
			SOCK_DEBUG(sk, "AX25: bound to device %s\n", ax25_dev->dev->name);
		}
	} else {
		if ((ax25_dev = ax25_addr_ax25dev(&addr->fsa_ax25.sax25_call)) == NULL) {
			SOCK_DEBUG(sk, "AX25: bind failed - no device\n");
			return -EADDRNOTAVAIL;
		}
		SOCK_DEBUG(sk, "AX25: bound to device %s\n", ax25_dev->dev->name);
	}

	if (ax25_dev != NULL)
		ax25_fillin_cb(sk->protinfo.ax25, ax25_dev);

	ax25_insert_socket(sk->protinfo.ax25);

	sk->zapped = 0;
	SOCK_DEBUG(sk, "AX25: socket is bound\n");
	return 0;
}

/*
 *	FIXME: nonblock behaviour looks like it may have a bug.
 */
static int ax25_connect(struct socket *sock, struct sockaddr *uaddr, int addr_len, int flags)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ax25 *addr = (struct sockaddr_ax25 *)uaddr;
	int err;

	if (sk->state == TCP_ESTABLISHED && sock->state == SS_CONNECTING) {
		sock->state = SS_CONNECTED;
		return 0;	/* Connect completed during a ERESTARTSYS event */
	}

	if (sk->state == TCP_CLOSE && sock->state == SS_CONNECTING) {
		sock->state = SS_UNCONNECTED;
		return -ECONNREFUSED;
	}

	if (sk->state == TCP_ESTABLISHED && sk->type == SOCK_SEQPACKET)
		return -EISCONN;	/* No reconnect on a seqpacket socket */

	sk->state   = TCP_CLOSE;
	sock->state = SS_UNCONNECTED;

	if (addr_len != sizeof(struct sockaddr_ax25) && addr_len != sizeof(struct full_sockaddr_ax25))
		return -EINVAL;

	if (addr->sax25_family != AF_AX25)
		return -EINVAL;

	/*
	 *	Handle digi-peaters to be used.
	 */
	if (addr_len == sizeof(struct full_sockaddr_ax25) && addr->sax25_ndigis != 0) {
		int ct           = 0;
		struct full_sockaddr_ax25 *fsa = (struct full_sockaddr_ax25 *)addr;

		/* Valid number of digipeaters ? */
		if (addr->sax25_ndigis < 1 || addr->sax25_ndigis > AX25_MAX_DIGIS)
			return -EINVAL;

		if (sk->protinfo.ax25->digipeat == NULL) {
			if ((sk->protinfo.ax25->digipeat = kmalloc(sizeof(ax25_digi), GFP_KERNEL)) == NULL)
				return -ENOBUFS;
		}

		sk->protinfo.ax25->digipeat->ndigi      = addr->sax25_ndigis;
		sk->protinfo.ax25->digipeat->lastrepeat = -1;

		while (ct < addr->sax25_ndigis) {
			if ((fsa->fsa_digipeater[ct].ax25_call[6] & AX25_HBIT) && sk->protinfo.ax25->iamdigi) {
				sk->protinfo.ax25->digipeat->repeated[ct] = 1;
				sk->protinfo.ax25->digipeat->lastrepeat   = ct;
			} else {
				sk->protinfo.ax25->digipeat->repeated[ct] = 0;
			}
			sk->protinfo.ax25->digipeat->calls[ct] = fsa->fsa_digipeater[ct];
			ct++;
		}
	}

	/*
	 *	Must bind first - autobinding in this may or may not work. If
	 *	the socket is already bound, check to see if the device has
	 *	been filled in, error if it hasn't.
	 */
	if (sk->zapped) {
		if ((err = ax25_rt_autobind(sk->protinfo.ax25, &addr->sax25_call)) < 0)
			return err;
		ax25_fillin_cb(sk->protinfo.ax25, sk->protinfo.ax25->ax25_dev);
		ax25_insert_socket(sk->protinfo.ax25);
	} else {
		if (sk->protinfo.ax25->ax25_dev == NULL)
			return -EHOSTUNREACH;
	}

	if (sk->type == SOCK_SEQPACKET && ax25_find_cb(&sk->protinfo.ax25->source_addr, &addr->sax25_call, NULL, sk->protinfo.ax25->ax25_dev->dev) != NULL)
		return -EADDRINUSE;			/* Already such a connection */

	sk->protinfo.ax25->dest_addr = addr->sax25_call;

	/* First the easy one */
	if (sk->type != SOCK_SEQPACKET) {
		sock->state = SS_CONNECTED;
		sk->state   = TCP_ESTABLISHED;
		return 0;
	}

	/* Move to connecting socket, ax.25 lapb WAIT_UA.. */
	sock->state        = SS_CONNECTING;
	sk->state          = TCP_SYN_SENT;

	switch (sk->protinfo.ax25->ax25_dev->values[AX25_VALUES_PROTOCOL]) {
		case AX25_PROTO_STD_SIMPLEX:
		case AX25_PROTO_STD_DUPLEX:
			ax25_std_establish_data_link(sk->protinfo.ax25);
			break;

#ifdef CONFIG_AX25_DAMA_SLAVE
		case AX25_PROTO_DAMA_SLAVE:
			if (sk->protinfo.ax25->ax25_dev->dama.slave)
				ax25_ds_establish_data_link(sk->protinfo.ax25);
			else
				ax25_std_establish_data_link(sk->protinfo.ax25);
			break;
#endif
	}

	sk->protinfo.ax25->state = AX25_STATE_1;
	ax25_set_timer(sk->protinfo.ax25);		/* Start going SABM SABM until a UA or a give up and DM */

	/* Now the loop */
	if (sk->state != TCP_ESTABLISHED && (flags & O_NONBLOCK))
		return -EINPROGRESS;

	cli();	/* To avoid races on the sleep */

	/* A DM or timeout will go to closed, a UA will go to ABM */
	while (sk->state == TCP_SYN_SENT) {
		interruptible_sleep_on(sk->sleep);
		if (current->signal & ~current->blocked) {
			sti();
			return -ERESTARTSYS;
		}
	}

	if (sk->state != TCP_ESTABLISHED) {
		/* Not in ABM, not in WAIT_UA -> failed */
		sti();
		sock->state = SS_UNCONNECTED;
		return sock_error(sk);	/* Always set at this point */
	}

	sock->state = SS_CONNECTED;

	sti();

	return 0;
}

static int ax25_socketpair(struct socket *sock1, struct socket *sock2)
{
	return -EOPNOTSUPP;
}

static int ax25_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk;
	struct sock *newsk;
	struct sk_buff *skb;

	if (newsock->sk != NULL)
		ax25_destroy_socket(newsock->sk->protinfo.ax25);

	newsock->sk = NULL;

	if ((sk = sock->sk) == NULL)
		return -EINVAL;

	if (sk->type != SOCK_SEQPACKET)
		return -EOPNOTSUPP;

	if (sk->state != TCP_LISTEN)
		return -EINVAL;

	/*
	 *	The write queue this time is holding sockets ready to use
	 *	hooked into the SABM we saved
	 */
	do {
		cli();
		if ((skb = skb_dequeue(&sk->receive_queue)) == NULL) {
			if (flags & O_NONBLOCK) {
				sti();
				return -EWOULDBLOCK;
			}
			interruptible_sleep_on(sk->sleep);
			if (current->signal & ~current->blocked) {
				sti();
				return -ERESTARTSYS;
			}
		}
	} while (skb == NULL);

	newsk = skb->sk;
	newsk->pair = NULL;
	sti();

	/* Now attach up the new socket */
	skb->sk = NULL;
	kfree_skb(skb, FREE_READ);
	sk->ack_backlog--;
	newsock->sk = newsk;

	return 0;
}

static int ax25_getname(struct socket *sock, struct sockaddr *uaddr, int *uaddr_len, int peer)
{
	struct full_sockaddr_ax25 *sax = (struct full_sockaddr_ax25 *)uaddr;
	struct sock *sk = sock->sk;
	unsigned char ndigi, i;

	if (peer != 0) {
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;

		sax->fsa_ax25.sax25_family = AF_AX25;
		sax->fsa_ax25.sax25_call   = sk->protinfo.ax25->dest_addr;
		sax->fsa_ax25.sax25_ndigis = 0;
		*uaddr_len = sizeof(struct full_sockaddr_ax25);

		if (sk->protinfo.ax25->digipeat != NULL) {
			ndigi = sk->protinfo.ax25->digipeat->ndigi;
			sax->fsa_ax25.sax25_ndigis = ndigi;
			for (i = 0; i < ndigi; i++)
				sax->fsa_digipeater[i] = sk->protinfo.ax25->digipeat->calls[i];
		}
	} else {
		sax->fsa_ax25.sax25_family = AF_AX25;
		sax->fsa_ax25.sax25_call   = sk->protinfo.ax25->source_addr;
		sax->fsa_ax25.sax25_ndigis = 1;
		*uaddr_len = sizeof(struct full_sockaddr_ax25);

		if (sk->protinfo.ax25->ax25_dev != NULL)
			memcpy(&sax->fsa_digipeater[0], sk->protinfo.ax25->ax25_dev->dev->dev_addr, AX25_ADDR_LEN);
		else
			sax->fsa_digipeater[0] = null_ax25_address;
	}

	return 0;
}

static int ax25_sendmsg(struct socket *sock, struct msghdr *msg, int len, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ax25 *usax = (struct sockaddr_ax25 *)msg->msg_name;
	int err;
	struct sockaddr_ax25 sax;
	struct sk_buff *skb;
	unsigned char *asmptr;
	int size;
	ax25_digi *dp;
	ax25_digi dtmp;
	int lv;
	int addr_len = msg->msg_namelen;

	if (msg->msg_flags & ~MSG_DONTWAIT)
		return -EINVAL;

	if (sk->zapped)
		return -EADDRNOTAVAIL;

	if (sk->shutdown & SEND_SHUTDOWN) {
		send_sig(SIGPIPE, current, 0);
		return -EPIPE;
	}

	if (sk->protinfo.ax25->ax25_dev == NULL)
		return -ENETUNREACH;

	if (usax != NULL) {
		if (addr_len != sizeof(struct sockaddr_ax25) && addr_len != sizeof(struct full_sockaddr_ax25))
			return -EINVAL;
		if (usax->sax25_family != AF_AX25)
			return -EINVAL;
		if (addr_len == sizeof(struct full_sockaddr_ax25) && usax->sax25_ndigis != 0) {
			int ct           = 0;
			struct full_sockaddr_ax25 *fsa = (struct full_sockaddr_ax25 *)usax;

			/* Valid number of digipeaters ? */
			if (usax->sax25_ndigis < 1 || usax->sax25_ndigis > AX25_MAX_DIGIS)
				return -EINVAL;

			dtmp.ndigi      = usax->sax25_ndigis;

			while (ct < usax->sax25_ndigis) {
				dtmp.repeated[ct] = 0;
				dtmp.calls[ct]    = fsa->fsa_digipeater[ct];
				ct++;
			}

			dtmp.lastrepeat = 0;
		}

		sax = *usax;
		if (sk->type == SOCK_SEQPACKET && ax25cmp(&sk->protinfo.ax25->dest_addr, &sax.sax25_call) != 0)
			return -EISCONN;
		if (usax->sax25_ndigis == 0)
			dp = NULL;
		else
			dp = &dtmp;
	} else {
		/*
		 *	FIXME: 1003.1g - if the socket is like this because
		 *	it has become closed (not started closed) and is VC
		 *	we ought to SIGPIPE, EPIPE
		 */
		if (sk->state != TCP_ESTABLISHED)
			return -ENOTCONN;
		sax.sax25_family = AF_AX25;
		sax.sax25_call   = sk->protinfo.ax25->dest_addr;
		dp = sk->protinfo.ax25->digipeat;
	}

	SOCK_DEBUG(sk, "AX.25: sendto: Addresses built.\n");

	/* Build a packet */
	SOCK_DEBUG(sk, "AX.25: sendto: building packet.\n");

	/* Assume the worst case */
	size = len + 3 + ax25_addr_size(dp) + AX25_BPQ_HEADER_LEN;

	if ((skb = sock_alloc_send_skb(sk, size, 0, msg->msg_flags & MSG_DONTWAIT, &err)) == NULL)
		return err;

	skb_reserve(skb, size - len);

	SOCK_DEBUG(sk, "AX.25: Appending user data\n");

	/* User data follows immediately after the AX.25 data */
	memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);

	/* Add the PID, usually AX25_TEXT */
	asmptr  = skb_push(skb, 1);
	*asmptr = sk->protocol;

	SOCK_DEBUG(sk, "AX.25: Transmitting buffer\n");

	if (sk->type == SOCK_SEQPACKET) {
		/* Connected mode sockets go via the LAPB machine */
		if (sk->state != TCP_ESTABLISHED) {
			kfree_skb(skb, FREE_WRITE);
			return -ENOTCONN;
		}

		ax25_output(sk->protinfo.ax25, sk->protinfo.ax25->paclen, skb);	/* Shove it onto the queue and kick */

		return len;
	} else {
		asmptr = skb_push(skb, 1 + ax25_addr_size(dp));

		SOCK_DEBUG(sk, "Building AX.25 Header (dp=%p).\n", dp);

		if (dp != NULL)
			SOCK_DEBUG(sk, "Num digipeaters=%d\n", dp->ndigi);

		/* Build an AX.25 header */
		asmptr += (lv = ax25_addr_build(asmptr, &sk->protinfo.ax25->source_addr, &sax.sax25_call, dp, AX25_COMMAND, AX25_MODULUS));

		SOCK_DEBUG(sk, "Built header (%d bytes)\n",lv);

		skb->h.raw = asmptr;

		SOCK_DEBUG(sk, "base=%p pos=%p\n", skb->data, asmptr);

		*asmptr = AX25_UI;

		/* Datagram frames go straight out of the door as UI */
		skb->dev      = sk->protinfo.ax25->ax25_dev->dev;
		skb->priority = SOPRI_NORMAL;

		ax25_queue_xmit(skb);

		return len;
	}
}

static int ax25_recvmsg(struct socket *sock, struct msghdr *msg, int size, int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	struct sockaddr_ax25 *sax = (struct sockaddr_ax25 *)msg->msg_name;
	int copied, length;
	struct sk_buff *skb;
	int er;
	int dama;

	/*
	 * 	This works for seqpacket too. The receiver has ordered the
	 *	queue for us! We do one quick check first though
	 */
	if (sk->type == SOCK_SEQPACKET && sk->state != TCP_ESTABLISHED)
		return -ENOTCONN;

	/* Now we can treat all alike */
	if ((skb = skb_recv_datagram(sk, flags & ~MSG_DONTWAIT, flags & MSG_DONTWAIT, &er)) == NULL)
		return er;

	if (sk->protinfo.ax25->hdrincl) {
		length = skb->len + (skb->data - skb->h.raw);
	} else {
		if (sk->type == SOCK_SEQPACKET)
			skb_pull(skb, 1);		/* Remove PID */
		length     = skb->len;
		skb->h.raw = skb->data;
	}

	copied = length;

	if (copied > size) {
		copied = size;
		msg->msg_flags |= MSG_TRUNC;
	}		

	skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	
	if (sax != NULL) {
		ax25_digi digi;
		ax25_address dest;

		ax25_addr_parse(skb->data, skb->len, NULL, &dest, &digi, NULL, &dama);

		sax->sax25_family = AF_AX25;
		/* We set this correctly, even though we may not let the
		   application know the digi calls further down (because it
		   did NOT ask to know them).  This could get political... **/
		sax->sax25_ndigis = digi.ndigi;
		sax->sax25_call   = dest;

		if (sax->sax25_ndigis != 0) {
			int ct           = 0;
			struct full_sockaddr_ax25 *fsa = (struct full_sockaddr_ax25 *)sax;

			while (ct < digi.ndigi) {
				fsa->fsa_digipeater[ct] = digi.calls[ct];
				ct++;
			}
		}
	}

	msg->msg_namelen = sizeof(struct full_sockaddr_ax25);

	skb_free_datagram(sk, skb);

	return copied;
}

static int ax25_shutdown(struct socket *sk, int how)
{
	/* FIXME - generate DM and RNR states */
	return -EOPNOTSUPP;
}

static int ax25_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	struct ax25_info_struct ax25_info;
	int err;
	long amount = 0;

	switch (cmd) {
		case TIOCOUTQ:
			if ((err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(int))) != 0)
				return err;
			amount = sk->sndbuf - atomic_read(&sk->wmem_alloc);
			if (amount < 0)
				amount = 0;
			put_user(amount, (int *)arg);
			return 0;

		case TIOCINQ: {
			struct sk_buff *skb;
			/* These two are safe on a single CPU system as only user tasks fiddle here */
			if ((skb = skb_peek(&sk->receive_queue)) != NULL)
				amount = skb->len;
			if ((err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(int))) != 0)
				return err;
			put_user(amount, (int *)arg);
			return 0;
		}

		case SIOCGSTAMP:
			if (sk != NULL) {
				if (sk->stamp.tv_sec==0)
					return -ENOENT;
				if ((err = verify_area(VERIFY_WRITE,(void *)arg,sizeof(struct timeval))) != 0)
					return err;
				copy_to_user((void *)arg, &sk->stamp, sizeof(struct timeval));
				return 0;
			}
			return -EINVAL;

		case SIOCAX25ADDUID:	/* Add a uid to the uid/call map table */
		case SIOCAX25DELUID:	/* Delete a uid from the uid/call map table */
		case SIOCAX25GETUID: {
			struct sockaddr_ax25 sax25;
			if ((err = verify_area(VERIFY_READ, (void *)arg, sizeof(struct sockaddr_ax25))) != 0)
				return err;
			copy_from_user(&sax25, (void *)arg, sizeof(sax25));
			return ax25_uid_ioctl(cmd, &sax25);
		}

		case SIOCAX25NOUID:	/* Set the default policy (default/bar) */
			if ((err = verify_area(VERIFY_READ, (void *)arg, sizeof(unsigned long))) != 0)
				return err;
			if (!suser())
				return -EPERM;
			get_user(amount, (long *)arg);
			if (amount > AX25_NOUID_BLOCK)
				return -EINVAL;
			ax25_uid_policy = amount;
			return 0;

		case SIOCADDRT:
		case SIOCDELRT:
		case SIOCAX25OPTRT:
			if (!suser())
				return -EPERM;
			return ax25_rt_ioctl(cmd, (void *)arg);

		case SIOCAX25CTLCON:
			if (!suser())
				return -EPERM;
			return ax25_ctl_ioctl(cmd, (void *)arg);

		case SIOCAX25GETINFO:
			if ((err = verify_area(VERIFY_WRITE, (void *)arg, sizeof(ax25_info))) != 0)
				return err;
			ax25_info.t1        = sk->protinfo.ax25->t1;
			ax25_info.t2        = sk->protinfo.ax25->t2;
			ax25_info.t3        = sk->protinfo.ax25->t3;
			ax25_info.idle      = sk->protinfo.ax25->idle;
			ax25_info.n2        = sk->protinfo.ax25->n2;
			ax25_info.t1timer   = sk->protinfo.ax25->t1timer;
			ax25_info.t2timer   = sk->protinfo.ax25->t2timer;
			ax25_info.t3timer   = sk->protinfo.ax25->t3timer;
			ax25_info.idletimer = sk->protinfo.ax25->idletimer;
			ax25_info.n2count   = sk->protinfo.ax25->n2count;
			ax25_info.state     = sk->protinfo.ax25->state;
			ax25_info.rcv_q     = atomic_read(&sk->rmem_alloc);
			ax25_info.snd_q     = atomic_read(&sk->wmem_alloc);
			copy_to_user((void *)arg, &ax25_info, sizeof(ax25_info));
			return 0;

		case SIOCAX25ADDFWD:
		case SIOCAX25DELFWD: {
			struct ax25_fwd_struct ax25_fwd;
			if (!suser())
				return -EPERM;
			if ((err = verify_area(VERIFY_READ, (void *)arg, sizeof(ax25_fwd))) != 0)
				return err;
			copy_from_user(&ax25_fwd, (void *)arg, sizeof(ax25_fwd));
			return ax25_fwd_ioctl(cmd, &ax25_fwd);
		}

		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
		case SIOCGIFMETRIC:
		case SIOCSIFMETRIC:
			return -EINVAL;

		default:
			return dev_ioctl(cmd, (void *)arg);
	}

	/*NOTREACHED*/
	return 0;
}

static int ax25_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	ax25_cb *ax25;
	const char *devname;
	int len = 0;
	off_t pos = 0;
	off_t begin = 0;

	cli();

	len += sprintf(buffer, "dest_addr src_addr  dev  st  vs  vr  va    t1     t2     t3      idle   n2  rtt wnd paclen   Snd-Q Rcv-Q\n");

	for (ax25 = ax25_list; ax25 != NULL; ax25 = ax25->next) {
		if (ax25->ax25_dev == NULL)
			devname = "???";
		else
			devname = ax25->ax25_dev->dev->name;

		len += sprintf(buffer + len, "%-9s ",
			ax2asc(&ax25->dest_addr));
		len += sprintf(buffer + len, "%-9s %-4s %2d %3d %3d %3d %3d/%03d %2d/%02d %3d/%03d %3d/%03d %2d/%02d %3d %3d  %5d",
			ax2asc(&ax25->source_addr), devname,
			ax25->state,
			ax25->vs, ax25->vr, ax25->va,
			ax25->t1timer / AX25_SLOWHZ,
			ax25->t1      / AX25_SLOWHZ,
			ax25->t2timer / AX25_SLOWHZ,
			ax25->t2      / AX25_SLOWHZ,
			ax25->t3timer / AX25_SLOWHZ,
			ax25->t3      / AX25_SLOWHZ,
			ax25->idletimer / (AX25_SLOWHZ * 60),
			ax25->idle      / (AX25_SLOWHZ * 60),
			ax25->n2count, ax25->n2,
			ax25->rtt     / AX25_SLOWHZ,
			ax25->window,
			ax25->paclen);

		if (ax25->sk != NULL) {
			len += sprintf(buffer + len, " %5d %5d\n",
				atomic_read(&ax25->sk->wmem_alloc),
				atomic_read(&ax25->sk->rmem_alloc));
		} else {
			len += sprintf(buffer + len, "\n");
		}

		pos = begin + len;

		if (pos < offset) {
			len   = 0;
			begin = pos;
		}

		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len   -= (offset - begin);

	if (len > length) len = length;

	return(len);
}

static struct net_proto_family ax25_family_ops =
{
	AF_AX25,
	ax25_create
};

static struct proto_ops ax25_proto_ops = {
	AF_AX25,

	ax25_dup,
	ax25_release,
	ax25_bind,
	ax25_connect,
	ax25_socketpair,
	ax25_accept,
	ax25_getname,
	datagram_poll,
	ax25_ioctl,
	ax25_listen,
	ax25_shutdown,
	ax25_setsockopt,
	ax25_getsockopt,
	sock_no_fcntl,
	ax25_sendmsg,
	ax25_recvmsg
};

/*
 *	Called by socket.c on kernel start up
 */
static struct packet_type ax25_packet_type =
{
	0,	/* MUTTER ntohs(ETH_P_AX25),*/
	0,		/* copy */
	ax25_kiss_rcv,
	NULL,
	NULL,
};

static struct notifier_block ax25_dev_notifier = {
	ax25_device_event,
	0
};

EXPORT_SYMBOL(ax25_encapsulate);
EXPORT_SYMBOL(ax25_rebuild_header);
EXPORT_SYMBOL(ax25_findbyuid);
EXPORT_SYMBOL(ax25_link_up);
EXPORT_SYMBOL(ax25_linkfail_register);
EXPORT_SYMBOL(ax25_linkfail_release);
EXPORT_SYMBOL(ax25_listen_register);
EXPORT_SYMBOL(ax25_listen_release);
EXPORT_SYMBOL(ax25_protocol_register);
EXPORT_SYMBOL(ax25_protocol_release);
EXPORT_SYMBOL(ax25_send_frame);
EXPORT_SYMBOL(ax25_uid_policy);
EXPORT_SYMBOL(ax25cmp);
EXPORT_SYMBOL(ax2asc);
EXPORT_SYMBOL(asc2ax);
EXPORT_SYMBOL(null_ax25_address);

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_ax25_route = {
	PROC_NET_AX25_ROUTE, 10, "ax25_route",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	ax25_rt_get_info
};
static struct proc_dir_entry proc_ax25 = {
	PROC_NET_AX25, 4, "ax25",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	ax25_get_info
};
static struct proc_dir_entry proc_ax25_calls = {
	PROC_NET_AX25_CALLS, 10, "ax25_calls",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	ax25_uid_get_info
};
#endif

void ax25_proto_init(struct net_proto *pro)
{
	sock_register(&ax25_family_ops);
	ax25_packet_type.type = htons(ETH_P_AX25);
	dev_add_pack(&ax25_packet_type);
	register_netdevice_notifier(&ax25_dev_notifier);
#ifdef CONFIG_SYSCTL
	ax25_register_sysctl();
#endif

#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_ax25_route);
	proc_net_register(&proc_ax25);
	proc_net_register(&proc_ax25_calls);
#endif

	printk(KERN_INFO "G4KLX/GW4PTS AX.25 for Linux. Version 0.36 for Linux NET3.038 (Linux 2.1)\n");
}

#ifdef MODULE
int init_module(void)
{
	ax25_proto_init(NULL);

	return 0;
}

void cleanup_module(void)
{
#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_AX25_ROUTE);
	proc_net_unregister(PROC_NET_AX25);
	proc_net_unregister(PROC_NET_AX25_CALLS);
	proc_net_unregister(PROC_NET_AX25_ROUTE);
#endif
	ax25_rt_free();
	ax25_uid_free();
	ax25_dev_free();

#ifdef CONFIG_SYSCTL
	ax25_unregister_sysctl();
#endif
	unregister_netdevice_notifier(&ax25_dev_notifier);

	ax25_packet_type.type = htons(ETH_P_AX25);
	dev_remove_pack(&ax25_packet_type);

	sock_unregister(AF_AX25);
}
#endif

#endif
