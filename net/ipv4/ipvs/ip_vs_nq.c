/*
 * IPVS:        Never Queue scheduling module
 *
 * Version:     $Id: ip_vs_nq.c,v 1.2 2003/06/08 09:31:19 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *
 */

/*
 * The NQ algorithm adopts a two-speed model. When there is an idle server
 * available, the job will be sent to the idle server, instead of waiting
 * for a fast one. When there is no idle server available, the job will be
 * sent to the server that minimize its expected delay (The Shortest
 * Expected Delay scheduling algorithm).
 *
 * See the following paper for more information:
 * A. Weinrib and S. Shenker, Greed is not enough: Adaptive load sharing
 * in large heterogeneous systems. In Proceedings IEEE INFOCOM'88,
 * pages 986-994, 1988.
 *
 * Thanks must go to Marko Buuri <marko@buuri.name> for talking NQ to me.
 *
 * The difference between NQ and SED is that NQ can improve overall
 * system utilization.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>

#include <net/ip_vs.h>


static int
ip_vs_nq_init_svc(struct ip_vs_service *svc)
{
	return 0;
}


static int
ip_vs_nq_done_svc(struct ip_vs_service *svc)
{
	return 0;
}


static int
ip_vs_nq_update_svc(struct ip_vs_service *svc)
{
	return 0;
}


static inline unsigned int
ip_vs_nq_dest_overhead(struct ip_vs_dest *dest)
{
	/*
	 * We only use the active connection number in the cost
	 * calculation here.
	 */
	return atomic_read(&dest->activeconns) + 1;
}


/*
 *	Weighted Least Connection scheduling
 */
static struct ip_vs_dest *
ip_vs_nq_schedule(struct ip_vs_service *svc, const struct sk_buff *skb)
{
	struct ip_vs_dest *dest, *least = NULL;
	unsigned int loh = 0, doh;

	IP_VS_DBG(6, "ip_vs_nq_schedule(): Scheduling...\n");

	/*
	 * We calculate the load of each dest server as follows:
	 *	(server expected overhead) / dest->weight
	 *
	 * Remember -- no floats in kernel mode!!!
	 * The comparison of h1*w2 > h2*w1 is equivalent to that of
	 *		  h1/w1 > h2/w2
	 * if every weight is larger than zero.
	 *
	 * The server with weight=0 is quiesced and will not receive any
	 * new connections.
	 */

	list_for_each_entry(dest, &svc->destinations, n_list) {

		if (dest->flags & IP_VS_DEST_F_OVERLOAD ||
		    !atomic_read(&dest->weight))
			continue;

		doh = ip_vs_nq_dest_overhead(dest);

		/* return the server directly if it is idle */
		if (atomic_read(&dest->activeconns) == 0) {
			least = dest;
			loh = doh;
			goto out;
		}

		if (!least ||
		    (loh * atomic_read(&dest->weight) >
		     doh * atomic_read(&least->weight))) {
			least = dest;
			loh = doh;
		}
	}

	if (!least)
		return NULL;

  out:
	IP_VS_DBG(6, "NQ: server %u.%u.%u.%u:%u "
		  "activeconns %d refcnt %d weight %d overhead %d\n",
		  NIPQUAD(least->addr), ntohs(least->port),
		  atomic_read(&least->activeconns),
		  atomic_read(&least->refcnt),
		  atomic_read(&least->weight), loh);

	return least;
}


static struct ip_vs_scheduler ip_vs_nq_scheduler =
{
	.name =			"nq",
	.refcnt =		ATOMIC_INIT(0),
	.module =		THIS_MODULE,
	.init_service =		ip_vs_nq_init_svc,
	.done_service =		ip_vs_nq_done_svc,
	.update_service =	ip_vs_nq_update_svc,
	.schedule =		ip_vs_nq_schedule,
};


static int __init ip_vs_nq_init(void)
{
	INIT_LIST_HEAD(&ip_vs_nq_scheduler.n_list);
	return register_ip_vs_scheduler(&ip_vs_nq_scheduler);
}

static void __exit ip_vs_nq_cleanup(void)
{
	unregister_ip_vs_scheduler(&ip_vs_nq_scheduler);
}

module_init(ip_vs_nq_init);
module_exit(ip_vs_nq_cleanup);
MODULE_LICENSE("GPL");
