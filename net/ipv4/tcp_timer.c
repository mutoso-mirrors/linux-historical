/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	@(#)tcp.c	1.0.16	05/25/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Charles Hedrick, <hedrick@klinzhai.rutgers.edu>
 *		Linus Torvalds, <torvalds@cs.helsinki.fi>
 *		Alan Cox, <gw4pts@gw4pts.ampr.org>
 *		Matthew Dillon, <dillon@apollo.west.oic.com>
 *		Arnt Gulbrandsen, <agulbra@nvg.unit.no>
 *		Jorge Cwik, <jorge@laser.satlink.net>
 */

#include <net/tcp.h>

static void tcp_sltimer_handler(unsigned long);
static void tcp_syn_recv_timer(unsigned long);
static void tcp_keepalive(unsigned long data);

struct timer_list	tcp_slow_timer = {
	NULL, NULL,
	0, 0,
	tcp_sltimer_handler,
};


struct tcp_sl_timer tcp_slt_array[TCP_SLT_MAX] = {
	{0, TCP_SYNACK_PERIOD, 0, tcp_syn_recv_timer},		/* SYNACK	*/
	{0, TCP_KEEPALIVE_PERIOD, 0, tcp_keepalive}		/* KEEPALIVE	*/
};

/*
 * Using different timers for retransmit, delayed acks and probes
 * We may wish use just one timer maintaining a list of expire jiffies 
 * to optimize.
 */

void tcp_init_xmit_timers(struct sock *sk)
{
	init_timer(&sk->tp_pinfo.af_tcp.retransmit_timer);
	sk->tp_pinfo.af_tcp.retransmit_timer.function=&tcp_retransmit_timer;
	sk->tp_pinfo.af_tcp.retransmit_timer.data = (unsigned long) sk;
	
	init_timer(&sk->tp_pinfo.af_tcp.delack_timer);
	sk->tp_pinfo.af_tcp.delack_timer.function=&tcp_delack_timer;
	sk->tp_pinfo.af_tcp.delack_timer.data = (unsigned long) sk;

	init_timer(&sk->tp_pinfo.af_tcp.probe_timer);
	sk->tp_pinfo.af_tcp.probe_timer.function=&tcp_probe_timer;
	sk->tp_pinfo.af_tcp.probe_timer.data = (unsigned long) sk;
}

/*
 *	Reset the retransmission timer
 */
 
void tcp_reset_xmit_timer(struct sock *sk, int what, unsigned long when)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	if((long)when <= 0)
	{		
		printk("xmit_timer <= 0 - timer:%d when:%lx\n", what, when);
		when=HZ/50;
	}

	switch (what) {
	case TIME_RETRANS:
		/*
		 * When seting the transmit timer the probe timer 
		 * should not be set.
		 * The delayed ack timer can be set if we are changing the
		 * retransmit timer when removing acked frames.
		 */
		del_timer(&tp->probe_timer);
		del_timer(&tp->retransmit_timer);
		tp->retransmit_timer.expires=jiffies+when;
		add_timer(&tp->retransmit_timer);
		break;

	case TIME_DACK:
		del_timer(&tp->delack_timer);
		tp->delack_timer.expires=jiffies+when;
		add_timer(&tp->delack_timer);
		break;

	case TIME_PROBE0:
		del_timer(&tp->probe_timer);
		tp->probe_timer.expires=jiffies+when;
		add_timer(&tp->probe_timer);
		break;	

	case TIME_WRITE:
		printk("bug: tcp_reset_xmit_timer TIME_WRITE\n");
		break;

	default:
		printk("bug: unknown timer value\n");
	}
}

void tcp_clear_xmit_timer(struct sock *sk, int what)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	switch (what) {
	case TIME_RETRANS:
		del_timer(&tp->retransmit_timer);
		break;
	case TIME_DACK:
		del_timer(&tp->delack_timer);
		break;
	case TIME_PROBE0:
		del_timer(&tp->probe_timer);
		break;	
	default:
		printk("bug: unknown timer value\n");
	}
}

int tcp_timer_is_set(struct sock *sk, int what)
{
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	switch (what) {
	case TIME_RETRANS:
		return tp->retransmit_timer.next != NULL;
		break;
	case TIME_DACK:
		return tp->delack_timer.next != NULL;
		break;
	case TIME_PROBE0:
		return tp->probe_timer.next != NULL;
		break;	
	default:
		printk("bug: unknown timer value\n");
	}
	return 0;
}

void tcp_clear_xmit_timers(struct sock *sk)
{	
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	del_timer(&tp->retransmit_timer);
	del_timer(&tp->delack_timer);
	del_timer(&tp->probe_timer);
}

/*
 *	A write timeout has occurred. Process the after effects. BROKEN (badly)
 */

static int tcp_write_timeout(struct sock *sk)
{
	/*
	 *	Look for a 'soft' timeout.
	 */
	if ((sk->state == TCP_ESTABLISHED && sk->retransmits && !(sk->retransmits & 7))
		|| (sk->state != TCP_ESTABLISHED && sk->retransmits > TCP_RETR1)) 
	{
		/*
		 *	Attempt to recover if arp has changed (unlikely!) or
		 *	a route has shifted (not supported prior to 1.3).
		 */
		ip_rt_advice(&sk->ip_route_cache, 0);
	}
	
	/*
	 *	Have we tried to SYN too many times (repent repent 8))
	 */
	 
	if(sk->retransmits > TCP_SYN_RETRIES && sk->state==TCP_SYN_SENT)
	{
		if(sk->err_soft)
			sk->err=sk->err_soft;
		else
			sk->err=ETIMEDOUT;
#ifdef TCP_DEBUG
		printk(KERN_DEBUG "syn timeout\n");
#endif

		sk->error_report(sk);
		tcp_clear_xmit_timers(sk);
		tcp_statistics.TcpAttemptFails++;	/* Is this right ??? - FIXME - */
		tcp_set_state(sk,TCP_CLOSE);
		/* Don't FIN, we got nothing back */
		return 0;
	}
	/*
	 *	Has it gone just too far ?
	 */
	if (sk->retransmits > TCP_RETR2) 
	{
		if(sk->err_soft)
			sk->err = sk->err_soft;
		else
			sk->err = ETIMEDOUT;
		sk->error_report(sk);

		tcp_clear_xmit_timers(sk);

		/*
		 *	Time wait the socket 
		 */
		if (sk->state == TCP_FIN_WAIT1 || sk->state == TCP_FIN_WAIT2 || sk->state == TCP_CLOSING ) 
		{
			tcp_set_state(sk,TCP_TIME_WAIT);
			tcp_reset_msl_timer (sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
		}
		else
		{
			/*
			 *	Clean up time.
			 */
			tcp_set_state(sk, TCP_CLOSE);
			return 0;
		}
	}
	return 1;
}


void tcp_delack_timer(unsigned long data) {

	struct sock *sk = (struct sock*)data;

	if(sk->zapped)
	{
		return;
	}
	
	if (sk->delayed_acks)
	{
		tcp_read_wakeup(sk); 		
	}
}

void tcp_probe_timer(unsigned long data) {

	struct sock *sk = (struct sock*)data;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	if(sk->zapped) 
	{		
		return;
	}
	
	if (sk->users) 
	{
		/* 
		 * Try again in second 
		 */

		tcp_reset_xmit_timer(sk, TIME_PROBE0, HZ);
		return;
	}

	/*
	 *	*WARNING* RFC 1122 forbids this
	 *	FIXME: We ought not to do it, Solaris 2.5 actually has fixing
	 *	this behaviour in Solaris down as a bug fix. [AC]
	 */
	if (tp->probes_out > TCP_RETR2) 
	{
		if(sk->err_soft)
			sk->err = sk->err_soft;
		else
			sk->err = ETIMEDOUT;
		sk->error_report(sk);

		/*
		 *	Time wait the socket 
		 */
		if (sk->state == TCP_FIN_WAIT1 || sk->state == TCP_FIN_WAIT2 
		    || sk->state == TCP_CLOSING ) 
		{
			tcp_set_state(sk, TCP_TIME_WAIT);
			tcp_reset_msl_timer (sk, TIME_CLOSE, TCP_TIMEWAIT_LEN);
		}
		else
		{
			/*
			 *	Clean up time.
			 */
			tcp_set_state(sk, TCP_CLOSE);
		}
	}
	
	tcp_send_probe0(sk);
}

static __inline__ int tcp_keepopen_proc(struct sock *sk)
{
	int res = 0;

	if (sk->state == TCP_ESTABLISHED || sk->state == TCP_CLOSE_WAIT)
	{
		struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
		__u32 elapsed = jiffies - tp->rcv_tstamp;

		if (elapsed >= TCP_KEEPALIVE_TIME)
		{
			if (tp->probes_out > TCP_KEEPALIVE_PROBES)
			{
				if(sk->err_soft)
					sk->err = sk->err_soft;
				else
					sk->err = ETIMEDOUT;

				tcp_set_state(sk, TCP_CLOSE);
			}
			else
			{
				tp->probes_out++;
				tp->pending = TIME_KEEPOPEN;
				tcp_write_wakeup(sk);
				res = 1;
			}
		}
	}
	return res;
}

/*
 *	Check all sockets for keepalive timer
 *	Called every 75 seconds
 *	This timer is started by af_inet init routine and is constantly
 *	running.
 *
 *	It might be better to maintain a count of sockets that need it using
 *	setsockopt/tcp_destroy_sk and only set the timer when needed.
 */

/*
 *	don't send over 5 keepopens at a time to avoid burstiness 
 *	on big servers [AC]
 */
#define MAX_KA_PROBES	5

static void tcp_keepalive(unsigned long data)
{
	struct sock *sk;
	int count = 0;
	int i;
	
	for(i=0; i < SOCK_ARRAY_SIZE; i++)
	{
		sk = tcp_prot.sock_array[i];
		while (sk)
		{
			if (sk->keepopen)
			{
				count += tcp_keepopen_proc(sk);
			}

			if (count == MAX_KA_PROBES)
				return;
			
			sk = sk->next;	    
		}
	}
}

/*
 *	The TCP retransmit timer. This lacks a few small details.
 *
 *	1. 	An initial rtt timeout on the probe0 should cause what we can
 *		of the first write queue buffer to be split and sent.
 *	2.	On a 'major timeout' as defined by RFC1122 we shouldn't report
 *		ETIMEDOUT if we know an additional 'soft' error caused this.
 *		tcp_err should save a 'soft error' for us.
 *	[Unless someone has broken it then it does, except for one 2.0 
 *	broken case of a send when the route/device is directly unreachable,
 *	and we error but should retry! - FIXME] [AC]
 */

void tcp_retransmit_timer(unsigned long data)
{
	struct sock *sk = (struct sock*)data;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	/*
	 *	We are reset. We will send no more retransmits.
	 */

	if(sk->zapped)
	{
		tcp_clear_xmit_timer(sk, TIME_RETRANS);
		return;
	}

	/*
	 * Clear delay ack timer
	 */

	tcp_clear_xmit_timer(sk, TIME_DACK);

	/*
	 *	Retransmission
	 */

	tp->retrans_head = NULL;
	

	if (sk->retransmits == 0)
	{
		/* 
		 * remember window where we lost 
		 * "one half of the current window but at least 2 segments"
		 */
		
		sk->ssthresh = max(sk->cong_window >> 1, 2); 
		sk->cong_count = 0;
		sk->cong_window = 1;
	}

	atomic_inc(&sk->retransmits);

	tcp_do_retransmit(sk, 0);

	/*
	 * Increase the timeout each time we retransmit.  Note that
	 * we do not increase the rtt estimate.  rto is initialized
	 * from rtt, but increases here.  Jacobson (SIGCOMM 88) suggests
	 * that doubling rto each time is the least we can get away with.
	 * In KA9Q, Karn uses this for the first few times, and then
	 * goes to quadratic.  netBSD doubles, but only goes up to *64,
	 * and clamps at 1 to 64 sec afterwards.  Note that 120 sec is
	 * defined in the protocol as the maximum possible RTT.  I guess
	 * we'll have to use something other than TCP to talk to the
	 * University of Mars.
	 *
	 * PAWS allows us longer timeouts and large windows, so once
	 * implemented ftp to mars will work nicely. We will have to fix
	 * the 120 second clamps though!
	 */

	tp->backoff++;
	tp->rto = min(tp->rto << 1, 120*HZ);
	tcp_reset_xmit_timer(sk, TIME_RETRANS, tp->rto);

	tcp_write_timeout(sk);
}

/*
 *	Slow timer for SYN-RECV sockets
 */

static void tcp_syn_recv_timer(unsigned long data)
{
	struct sock *sk;
	unsigned long now = jiffies;
	int i;

	for(i=0; i < SOCK_ARRAY_SIZE; i++)
	{
		sk = tcp_prot.sock_array[i];
		while (sk)
		{
			struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;
			
			if (sk->state == TCP_LISTEN && !sk->users &&
			    tp->syn_wait_queue)
			{
				struct open_request *req;

				req = tp->syn_wait_queue;

				do {
					struct open_request *conn;
				  
					conn = req;
					req = req->dl_next;

					if (conn->sk)
					{
						continue;
					}
					
					if ((long)(now - conn->expires) <= 0)
						break;

					tcp_synq_unlink(tp, conn);
					
					if (conn->retrans >= TCP_RETR1)
					{
#ifdef TCP_DEBUG
						printk(KERN_DEBUG "syn_recv: "
						       "too many retransmits\n");
#endif
						(*conn->class->destructor)(conn);
						tcp_dec_slow_timer(TCP_SLT_SYNACK);
						kfree(conn);

						if (!tp->syn_wait_queue)
							break;
					}
					else
					{
						__u32 timeo;
						
						(*conn->class->rtx_syn_ack)(sk, conn);

						conn->retrans++;
#ifdef TCP_DEBUG
						printk(KERN_DEBUG "syn_ack rtx %d\n", conn->retrans);
#endif
						timeo = min((TCP_TIMEOUT_INIT 
							     << conn->retrans),
							    120*HZ);
						conn->expires = now + timeo;
						tcp_synq_queue(tp, conn);
					}
				} while (req != tp->syn_wait_queue);
			}
			
			sk = sk->next;
		}
	}
}

void tcp_sltimer_handler(unsigned long data)
{
	struct tcp_sl_timer *slt = tcp_slt_array;
	unsigned long next = ~0UL;
	unsigned long now = jiffies;
	int i;

	for (i=0; i < TCP_SLT_MAX; i++, slt++)
	{
		if (slt->count)
		{
			long trigger;

			trigger = slt->period - ((long)(now - slt->last));

			if (trigger <= 0)
			{
				(*slt->handler)((unsigned long) slt);
				slt->last = now;
				trigger = slt->period;
			}
			next = min(next, trigger);
		}
	}

	if (next != ~0UL)
	{
		tcp_slow_timer.expires = now + next;
		add_timer(&tcp_slow_timer);
	}
}

void __tcp_inc_slow_timer(struct tcp_sl_timer *slt)
{
	unsigned long now = jiffies;
	unsigned long next = 0;
	unsigned long when;

	slt->last = now;
		
	when = now + slt->period;
	if (del_timer(&tcp_slow_timer))
	{
		next = tcp_slow_timer.expires;
	}
	if (next && ((long)(next - when) < 0))
	{
		when = next;
	}
		
	tcp_slow_timer.expires = when;
	add_timer(&tcp_slow_timer);
}
