/*
 * net/sched/sch_csz.c	Clark-Shenker-Zhang scheduler.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 */

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/pkt_sched.h>


/*	Clark-Shenker-Zhang algorithm.
	=======================================

	SOURCE.

	David D. Clark, Scott Shenker and Lixia Zhang
	"Supporting Real-Time Applications in an Integrated Services Packet
	Network: Architecture and Mechanism".

	CBQ presents a flexible universal algorithm for packet scheduling,
	but it has pretty poor delay characteristics.
	Round-robin scheduling and link-sharing goals
	apparently contradict to minimization of network delay and jitter.
	Moreover, correct handling of predicted flows seems to be
	impossible in CBQ.

	CSZ presents more precise but less flexible and less efficient
	approach. As I understand, the main idea is to create
	WFQ flows for each guaranteed service and to allocate
	the rest of bandwith to dummy flow-0. Flow-0 comprises
	the predicted services and the best effort traffic;
	it is handled by a priority scheduler with the highest
	priority band allocated	for predicted services, and the rest ---
	to the best effort packets.

	Note, that in CSZ flows are NOT limited to their bandwidth.
	It is supposed, that flow passed admission control at the edge
	of QoS network and it more need no shaping. Any attempt to improve
	the flow or to shape it to a token bucket at intermediate hops
	will introduce undesired delays and raise jitter.

	At the moment CSZ is the only scheduler that provides
	real guaranteed service. Another schemes (including CBQ)
	do not provide guaranteed delay and randomize jitter.
	There exists the statement (Sally Floyd), that delay
	can be estimated by a IntServ compliant formulae.
	This result is true formally, but it is wrong in principle.
	At first, it ignores delays introduced by link sharing.
	And the second (and main) it limits bandwidth,
	it is fatal flaw.

        ALGORITHM.

	--- Notations.

	$B$ is link bandwidth (bits/sec).

	$I$ is set of all flows, including flow $0$.
	Every flow $a \in I$ has associated bandwidth slice $r_a < 1$ and
	$\sum_{a \in I} r_a = 1$.

	--- Flow model.

	Let $m_a$ is number of backlogged bits in flow $a$.
	The flow is {\em active }, if $m_a > 0$.
	This number is discontinuous function of time;
	when a packet $i$ arrives:
	\[
	m_a(t_i+0) - m_a(t_i-0) = L^i,
	\]
	where $L^i$ is the length of arrived packet.
	The flow queue is drained continuously until $m_a == 0$:
	\[
	{d m_a \over dt} = - { B r_a \over \sum_{b \in A} r_b}.
	\]
	I.e. flow rates are their allocated rates proportionally
	scaled to take all available link bandwidth. Apparently,
	it is not the only possible policy. F.e. CBQ classes
	without borrowing would be modelled by:
	\[
	{d m_a \over dt} = - B r_a .
	\]
	More complicated hierarchical bandwidth allocation
	policies are possible, but, unfortunately, basic
	flows equation have simple solution only for proportional
	scaling.

	--- Departure times.

	We calculate time until the last bit of packet will be sent:
	\[
	E_a^i(t) = { m_a(t_i) - \delta_a(t) \over r_a },
	\]
	where $\delta_a(t)$ is number of bits drained since $t_i$.
	We have to evaluate $E_a^i$ for all queued packets,
	then find packet with minimal $E_a^i$ and send it.

	It sounds good, but direct implementation of the algorithm
	is absolutely infeasible. Luckily, if flow rates
	are scaled proportionally, the equations have simple solution.
	
	The differential equation for $E_a^i$ is
	\[
	{d E_a^i (t) \over dt } = - { d \delta_a(t) \over dt} { 1 \over r_a} =
	{ B \over \sum_{b \in A} r_b}
	\]
	with initial condition
	\[
	E_a^i (t_i) = { m_a(t_i) \over r_a } .
	\]

	Let's introduce an auxiliary function $R(t)$:

	--- Round number.

	Consider the following model: we rotate over active flows,
	sending $r_a B$ bits from every flow, so that we send
	$B \sum_{a \in A} r_a$ bits per round, that takes
	$\sum_{a \in A} r_a$ seconds.
	
	Hence, $R(t)$ (round number) is monotonically increasing
	linear function	of time when $A$ is not changed
	\[
	{ d R(t) \over dt } = { 1 \over \sum_{a \in A} r_a }
	\]
	and it is continuous when $A$ changes.

	The central observation is that the quantity
	$F_a^i = R(t) + E_a^i(t)/B$ does not depend on time at all!
	$R(t)$ does not depend on flow, so that $F_a^i$ can be
	calculated only once on packet arrival, and we need not
	recalculation of $E$ numbers and resorting queues.
	Number $F_a^i$ is called finish number of the packet.
	It is just value of $R(t)$, when the last bit of packet
	will be sent out.

	Maximal finish number on flow is called finish number of flow
	and minimal one is "start number of flow".
	Apparently, flow is active if and only if $F_a \leq R$.

	When packet of length $L_i$ bit arrives to flow $a$ at time $t_i$,
	we calculate number $F_a^i$ as:

	If flow was inactive ($F_a < R$):
	$F_a^i = R(t) + {L_i \over B r_a}$
	otherwise
	$F_a^i = F_a + {L_i \over B r_a}$

	These equations complete the algorithm specification.

	It looks pretty hairy, but there exists a simple
	procedure for solving these equations.
	See procedure csz_update(), that is a generalization of
	algorithm from S. Keshav's thesis Chapter 3
	"Efficient Implementation of Fair Queeing".

	NOTES.

	* We implement only the simplest variant of CSZ,
	when flow-0 is explicit 4band priority fifo.
	It is bad, but we need "peek" operation in addition
	to "dequeue" to implement complete CSZ.
	I do not want to make it, until it is not absolutely
	necessary.
	
	* A primitive support for token bucket filtering
	presents too. It directly contradicts to CSZ, but
	though the Internet is on the globe ... :-)
	yet "the edges of the network" really exist.
	
	BUGS.

	* Fixed point arithmetic is overcomplicated, suboptimal and even
	wrong. Check it later.
*/


/* This number is arbitrary */

#define CSZ_MAX_GUARANTEED 16

#define CSZ_FLOW_ID(skb)   (CSZ_MAX_GUARANTEED)

struct csz_head
{
	struct csz_head		*snext;
	struct csz_head		*sprev;
	struct csz_head		*fnext;
	struct csz_head		*fprev;
};

struct csz_flow
{
	struct csz_head		*snext;
	struct csz_head		*sprev;
	struct csz_head		*fnext;
	struct csz_head		*fprev;

/* Parameters */
	unsigned long		rate;	/* Flow rate. Fixed point is at rate_log */
	unsigned long		*L_tab;	/* Lookup table for L/(B*r_a) values */
	unsigned long		max_bytes;	/* Maximal length of queue */
#ifdef CSZ_PLUS_TBF
	unsigned long		depth;	/* Depth of token bucket, normalized
					   as L/(B*r_a) */
#endif

/* Variables */
#ifdef CSZ_PLUS_TBF
	unsigned long		tokens; /* Tokens number: usecs */
	psched_time_t		t_tbf;
	unsigned long		R_tbf;
	int			throttled;
#endif
	unsigned		peeked;
	unsigned long		start;	/* Finish number of the first skb */
	unsigned long		finish;	/* Finish number of the flow */

	struct sk_buff_head	q;	/* FIFO queue */
};

#define L2R(q,f,L) ((f)->L_tab[(L)>>(q)->cell_log])

struct csz_sched_data
{
/* Parameters */
	unsigned char	cell_log;	/* 1<<cell_log is quantum of packet size */
	unsigned char	rate_log;	/* fixed point position for rate;
					 * really we need not it */
	unsigned char	R_log;		/* fixed point position for round number */
	unsigned char	delta_log;	/* 1<<delta_log is maximal timeout in usecs;
					 * 21 <-> 2.1sec is MAXIMAL value */

/* Variables */
#ifdef CSZ_PLUS_TBF
	struct timer_list wd_timer;
	long		wd_expires;
#endif
	psched_time_t	t_c;		/* Time check-point */
	unsigned long	R_c;		/* R-number check-point	*/
	unsigned long	rate;		/* Current sum of rates of active flows */
	struct csz_head	s;		/* Flows sorted by "start" */
	struct csz_head	f;		/* Flows sorted by "finish"	*/

	struct sk_buff_head	other[4];/* Predicted (0) and the best efforts
					     classes (1,2,3) */
	struct csz_flow	flow[CSZ_MAX_GUARANTEED]; /* Array of flows */
};

/* These routines (csz_insert_finish and csz_insert_start) are
   the most time consuming part of all the algorithm.

   We insert to sorted list, so that time
   is linear with respect to number of active flows in the worst case.
   Note that we have not very large number of guaranteed flows,
   so that logarithmic algorithms (heap etc.) are useless,
   they are slower than linear one when length of list <= 32.

   Heap would take sence if we used WFQ for best efforts
   flows, but SFQ is better choice in this case.
 */


/* Insert flow "this" to the list "b" before
   flow with greater finish number.
 */

#if 0
/* Scan forward */
extern __inline__ void csz_insert_finish(struct csz_head *b,
					 struct csz_flow *this)
{
	struct csz_head *f = b->fnext;
	unsigned long finish = this->finish;

	while (f != b) {
		if (((struct csz_flow*)f)->finish - finish > 0)
			break;
		f = f->fnext;
	}
	this->fnext = f;
	this->fprev = f->fprev;
	this->fnext->fprev = this->fprev->fnext = (struct csz_head*)this;
}
#else
/* Scan backward */
extern __inline__ void csz_insert_finish(struct csz_head *b,
					 struct csz_flow *this)
{
	struct csz_head *f = b->fprev;
	unsigned long finish = this->finish;

	while (f != b) {
		if (((struct csz_flow*)f)->finish - finish <= 0)
			break;
		f = f->fprev;
	}
	this->fnext = f->fnext;
	this->fprev = f;
	this->fnext->fprev = this->fprev->fnext = (struct csz_head*)this;
}
#endif

/* Insert flow "this" to the list "b" before
   flow with greater start number.
 */

extern __inline__ void csz_insert_start(struct csz_head *b,
					struct csz_flow *this)
{
	struct csz_head *f = b->snext;
	unsigned long start = this->start;

	while (f != b) {
		if (((struct csz_flow*)f)->start - start > 0)
			break;
		f = f->snext;
	}
	this->snext = f;
	this->sprev = f->sprev;
	this->snext->sprev = this->sprev->snext = (struct csz_head*)this;
}


/* Calculate and return current round number.
   It is another time consuming part, but
   it is impossible to avoid it.

   Fixed point arithmetic is not ... does not ... Well, it is just CRAP.
 */

static unsigned long csz_update(struct Qdisc *sch)
{
	struct csz_sched_data	*q = (struct csz_sched_data*)sch->data;
	struct csz_flow 	*a;
	unsigned long		F;
	unsigned long		tmp;
	psched_time_t		now;
	unsigned long		delay;
	unsigned long		R_c;

	PSCHED_GET_TIME(now);
	delay = PSCHED_TDIFF_SAFE(now, q->t_c, 0, goto do_reset);

	if (delay>>q->delta_log) {
do_reset:
		/* Delta is too large.
		   It is possible if MTU/BW > 1<<q->delta_log
		   (i.e. configuration error) or because of hardware
		   fault. We have no choice...
		 */
		qdisc_reset(sch);
		return 0;
	}

	q->t_c = now;

	for (;;) {
		a = (struct csz_flow*)q->f.fnext;

		/* No more active flows. Reset R and exit. */
		if (a == (struct csz_flow*)&q->f) {
#ifdef CSZ_DEBUG
			if (q->rate) {
				printk("csz_update: rate!=0 on inactive csz\n");
				q->rate = 0;
			}
#endif
			q->R_c = 0;
			return 0;
		}

		F = a->finish;

#ifdef CSZ_DEBUG
		if (q->rate == 0) {
			printk("csz_update: rate=0 on active csz\n");
			goto do_reset;
		}
#endif

		/*
		 *           tmp = (t - q->t_c)/q->rate;
		 */

		tmp = ((delay<<(31-q->delta_log))/q->rate)>>(31-q->delta_log+q->R_log);

		tmp += q->R_c;

		/* OK, this flow (and all flows with greater
		   finish numbers) is still active */
		if (F - tmp > 0)
			break;

		/* It is more not active */

		a->fprev->fnext = a->fnext;
		a->fnext->fprev = a->fprev;

		/*
		 * q->t_c += (F - q->R_c)*q->rate
		 */

		tmp = ((F-q->R_c)*q->rate)<<q->R_log;
		R_c = F;
		q->rate -= a->rate;

		if (delay - tmp >= 0) {
			delay -= tmp;
			continue;
		}
		delay = 0;
	}

	q->R_c = tmp;
	return tmp;
}

static int
csz_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct csz_sched_data *q = (struct csz_sched_data *)sch->data;
	unsigned flow_id = CSZ_FLOW_ID(skb);
	unsigned long R;
	int prio;
	struct csz_flow *this;

	if (flow_id >= CSZ_MAX_GUARANTEED) {
		prio = flow_id - CSZ_MAX_GUARANTEED;
		flow_id = 0;
	}

	this = &q->flow[flow_id];
	if (this->q.qlen >= this->max_bytes || this->L_tab == NULL) {
		kfree_skb(skb, FREE_WRITE);
		return 0;
	}

	R = csz_update(sch);

	if (this->finish - R >= 0) {
		/* It was active */
		this->finish += L2R(q,this,skb->len);
	} else {
		/* It is inactive; activate it */
		this->finish = R + L2R(q,this,skb->len);
		q->rate += this->rate;
		csz_insert_finish(&q->f, this);
	}

	/* If this flow was empty, remember start number
	   and insert it into start queue */
	if (this->q.qlen == 0) {
		this->start = this->finish;
		csz_insert_start(&q->s, this);
	}
	if (flow_id)
		skb_queue_tail(&this->q, skb);
	else
		skb_queue_tail(&q->other[prio], skb);
	sch->q.qlen++;
	return 1;
}

static __inline__ struct sk_buff *
skb_dequeue_best(struct csz_sched_data * q)
{
	int i;
	struct sk_buff *skb;

	for (i=0; i<4; i++) {
		skb = skb_dequeue(&q->other[i]);
		if (skb) {
			q->flow[0].q.qlen--;
			return skb;
		}
	}
	return NULL;
}

static __inline__ struct sk_buff *
skb_peek_best(struct csz_sched_data * q)
{
	int i;
	struct sk_buff *skb;

	for (i=0; i<4; i++) {
		skb = skb_peek(&q->other[i]);
		if (skb)
			return skb;
	}
	return NULL;
}

#ifdef CSZ_PLUS_TBF

static void csz_watchdog(unsigned long arg)
{
	struct Qdisc *sch = (struct Qdisc*)arg;
	struct csz_sched_data *q = (struct csz_sched_data*)sch->data;

	q->wd_timer.expires = 0;
	q->wd_timer.function = NULL;

	qdisc_wakeup(sch->dev);
}

static __inline__ void
csz_move_queue(struct csz_flow *this, long delta)
{
	this->fprev->fnext = this->fnext;
	this->fnext->fprev = this->fprev;

	this->start += delta;
	this->finish += delta;

	csz_insert_finish(this);
}

static __inline__ int csz_enough_tokens(struct csz_sched_data *q,
					struct csz_flow *this,
					struct sk_buff *skb)
{
	long toks;
	long shift;
	psched_time_t now;

	PSCHED_GET_TIME(now);

	toks = PSCHED_TDIFF(now, t_tbf) + this->tokens - L2R(q,this,skb->len);

	shift = 0;
	if (this->throttled) {
		/* Remember aposteriory delay */

		unsigned long R = csz_update(q);
		shift = R - this->R_tbf;
		this->R_tbf = R;
	}

	if (toks >= 0) {
		/* Now we have enough tokens to proceed */

		this->tokens = toks <= this->depth ? toks ? this->depth;
		this->t_tbf = now;
	
		if (!this->throttled)
			return 1;

		/* Flow was throttled. Update its start&finish numbers
		   with delay calculated aposteriori.
		 */

		this->throttled = 0;
		if (shift > 0)
			csz_move_queue(this, shift);
		return 1;
	}

	if (!this->throttled) {
		/* Flow has just been throttled; remember
		   current round number to calculate aposteriori delay
		 */
		this->throttled = 1;
		this->R_tbf = csz_update(q);
	}

	/* Move all the queue to the time when it will be allowed to send.
	   We should translate time to round number, but it is impossible,
	   so that we made the most conservative estimate i.e. we suppose
	   that only this flow is active and, hence, R = t.
	   Really toks <= R <= toks/r_a.

	   This apriory shift in R will be adjusted later to reflect
	   real delay. We cannot avoid it because of:
	   - throttled flow continues to be active from the viewpoint
	     of CSZ, so that it would acquire highest priority,
	     if you not adjusted start numbers.
	   - Eventually, finish number would become less than round
	     number and flow were declared inactive.
	 */

	toks = -toks;

	/* Remeber, that we should start watchdog */
	if (toks < q->wd_expires)
		q->wd_expires = toks;

	toks >>= q->R_log;
	shift += toks;
	if (shift > 0) {
		this->R_tbf += toks;
		csz_move_queue(this, shift);
	}
	csz_insert_start(this);
	return 0;
}
#endif


static struct sk_buff *
csz_dequeue(struct Qdisc* sch)
{
	struct csz_sched_data *q = (struct csz_sched_data *)sch->data;
	struct sk_buff *skb;
	struct csz_flow *this;

#ifdef CSZ_PLUS_TBF
	q->wd_expires = 0;
#endif
	this = (struct csz_flow*)q->s.snext;

	while (this != (struct csz_flow*)&q->s) {

		/* First of all: unlink from start list */
		this->sprev->snext = this->snext;
		this->snext->sprev = this->sprev;

		if (this != &q->flow[0]) {	/* Guaranteed flow */
			skb = __skb_dequeue(&this->q);
			if (skb) {
#ifdef CSZ_PLUS_TBF
				if (this->depth) {
					if (!csz_enough_tokens(q, this, skb))
						continue;
				}
#endif
				if (this->q.qlen) {
					struct sk_buff *nskb = skb_peek(&this->q);
					this->start += L2R(q,this,nskb->len);
					csz_insert_start(&q->s, this);
				}
				sch->q.qlen--;
				return skb;
			}
		} else {	/* Predicted or best effort flow */
			skb = skb_dequeue_best(q);
			if (skb) {
				unsigned peeked = this->peeked;
				this->peeked = 0;

				if (--this->q.qlen) {
					struct sk_buff *nskb;
					unsigned dequeued = L2R(q,this,skb->len);

					/* We got not the same thing that
					   peeked earlier; adjust start number
					   */
					if (peeked != dequeued && peeked)
						this->start += dequeued - peeked;

					nskb = skb_peek_best(q);
					peeked = L2R(q,this,nskb->len);
					this->start += peeked;
					this->peeked = peeked;
					csz_insert_start(&q->s, this);
				}
				sch->q.qlen--;
				return skb;
			}
		}
	}
#ifdef CSZ_PLUS_TBF
	/* We are about to return no skb.
	   Schedule watchdog timer, if it occured because of shaping.
	 */
	if (q->wd_expires) {
		if (q->wd_timer.function)
			del_timer(&q->wd_timer);
		q->wd_timer.function = csz_watchdog;
		q->wd_timer.expires = jiffies + PSCHED_US2JIFFIE(q->wd_expires);
		add_timer(&q->wd_timer);
	}
#endif
	return NULL;
}

static void
csz_reset(struct Qdisc* sch)
{
	struct csz_sched_data *q = (struct csz_sched_data *)sch->data;
	struct sk_buff *skb;
	int    i;

	for (i=0; i<4; i++)
		while ((skb=skb_dequeue(&q->other[i])) != NULL)
			kfree_skb(skb, 0);

	for (i=0; i<CSZ_MAX_GUARANTEED; i++) {
		struct csz_flow *this = q->flow + i;
		while ((skb = skb_dequeue(&this->q)) != NULL)
			kfree_skb(skb, FREE_WRITE);
		this->snext = this->sprev =
		this->fnext = this->fprev = (struct csz_head*)this;
		this->start = this->finish = 0;
	}
	q->s.snext = q->s.sprev = &q->s;
	q->f.fnext = q->f.fprev = &q->f;
	q->R_c = 0;
#ifdef CSZ_PLUS_TBF
	PSCHED_GET_TIME(&q->t_tbf);
	q->tokens = q->depth;
	if (q->wd_timer.function) {
		del_timer(&q->wd_timer);
		q->wd_timer.function = NULL;
	}
#endif
	sch->q.qlen = 0;
}

static void
csz_destroy(struct Qdisc* sch)
{
/*
	struct csz_sched_data *q = (struct csz_sched_data *)sch->data;
	int	i;

	for (i=0; i<4; i++)
		qdisc_destroy(q->other[i]);
 */
}

static int csz_init(struct Qdisc *sch, void *arg)
{
	struct csz_sched_data *q = (struct csz_sched_data *)sch->data;
	struct cszinitctl *ctl = (struct cszinitctl*)arg;
	int    i;

	for (i=0; i<4; i++)
		skb_queue_head_init(&q->other[i]);

	for (i=0; i<CSZ_MAX_GUARANTEED; i++) {
		struct csz_flow *this = q->flow + i;
		skb_queue_head_init(&this->q);
		this->snext = this->sprev =
		this->fnext = this->fprev = (struct csz_head*)this;
		this->start = this->finish = 0;
	}
	q->s.snext = q->s.sprev = &q->s;
	q->f.fnext = q->f.fprev = &q->f;
	q->R_c = 0;
#ifdef CSZ_PLUS_TBF
	init_timer(&q->wd_timer);
	q->wd_timer.data = (unsigned long)sch;
#endif
	if (ctl) {
		if (ctl->flows != CSZ_MAX_GUARANTEED)
			return -EINVAL;
		q->cell_log = ctl->cell_log;
	}
	return 0;
}

static int csz_control(struct Qdisc *sch, struct pschedctl *gctl)
{
/*
	struct csz_sched_data *q = (struct csz_sched_data *)sch->data;
	struct cszctl *ctl = (struct cszctl*)gctl->arg;
	struct sk_buff *skb;
	int    i;

	if (op == PSCHED_TC_ATTACH) {
		
	}
*/
	return 0;
}




struct Qdisc_ops csz_ops =
{
	NULL,
	"csz",
	0,
	sizeof(struct csz_sched_data),
	csz_enqueue,
	csz_dequeue,
	csz_reset,
	csz_destroy,
	csz_init,
	csz_control,
};


#ifdef MODULE
#include <linux/module.h>
int init_module(void)
{
	int err;

	/* Load once and never free it. */
	MOD_INC_USE_COUNT;

	err = register_qdisc(&csz_ops);
	if (err)
		MOD_DEC_USE_COUNT;
	return err;
}

void cleanup_module(void) 
{
}
#endif
