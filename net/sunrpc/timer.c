#include <linux/version.h>
#include <linux/types.h>
#include <linux/unistd.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/timer.h>

#define RPC_RTO_MAX (60*HZ)
#define RPC_RTO_INIT (HZ/5)
#define RPC_RTO_MIN (2)

void
rpc_init_rtt(struct rpc_rtt *rt, unsigned long timeo)
{
	unsigned long init = 0;
	unsigned i;
	rt->timeo = timeo;
	if (timeo > RPC_RTO_INIT)
		init = (timeo - RPC_RTO_INIT) << 3;
	for (i = 0; i < 5; i++) {
		rt->srtt[i] = init;
		rt->sdrtt[i] = RPC_RTO_INIT;
	}
	atomic_set(&rt->ntimeouts, 0);
}

void
rpc_update_rtt(struct rpc_rtt *rt, int timer, long m)
{
	unsigned long *srtt, *sdrtt;

	if (timer-- == 0)
		return;

	/* jiffies wrapped; ignore this one */
	if (m < 0)
		return;
	if (m == 0)
		m = 1;
	srtt = &rt->srtt[timer];
	m -= *srtt >> 3;
	*srtt += m;
	if (m < 0)
		m = -m;
	sdrtt = &rt->sdrtt[timer];
	m -= *sdrtt >> 2;
	*sdrtt += m;
	/* Set lower bound on the variance */
	if (*sdrtt < RPC_RTO_MIN)
		*sdrtt = RPC_RTO_MIN;
}

/*
 * Estimate rto for an nfs rpc sent via. an unreliable datagram.
 * Use the mean and mean deviation of rtt for the appropriate type of rpc
 * for the frequent rpcs and a default for the others.
 * The justification for doing "other" this way is that these rpcs
 * happen so infrequently that timer est. would probably be stale.
 * Also, since many of these rpcs are
 * non-idempotent, a conservative timeout is desired.
 * getattr, lookup,
 * read, write, commit     - A+4D
 * other                   - timeo
 */

unsigned long
rpc_calc_rto(struct rpc_rtt *rt, int timer)
{
	unsigned long res;
	if (timer-- == 0)
		return rt->timeo;
	res = (rt->srtt[timer] >> 3) + rt->sdrtt[timer];
	if (res > RPC_RTO_MAX)
		res = RPC_RTO_MAX;
	return res;
}
