/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 Forwarding Information Base: policy rules.
 *
 * Version:	$Id: fib_rules.c,v 1.4 1998/03/21 07:27:58 davem Exp $
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/init.h>

#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/ip_fib.h>

#define FRprintk(a...)

#ifndef CONFIG_RTNL_OLD_IFINFO
#define RTA_IFNAME RTA_IIF
#endif

struct fib_rule
{
	struct fib_rule *r_next;
	u32		r_preference;
	unsigned char	r_table;
	unsigned char	r_action;
	unsigned char	r_dst_len;
	unsigned char	r_src_len;
	u32		r_src;
	u32		r_srcmask;
	u32		r_dst;
	u32		r_dstmask;
	u32		r_srcmap;
	u8		r_flags;
	u8		r_tos;
	int		r_ifindex;
	char		r_ifname[IFNAMSIZ];
};

static struct fib_rule default_rule = { NULL, 0x7FFF, RT_TABLE_DEFAULT, RTN_UNICAST, };
static struct fib_rule main_rule = { &default_rule, 0x7FFE, RT_TABLE_MAIN, RTN_UNICAST, };
static struct fib_rule local_rule = { &main_rule, 0, RT_TABLE_LOCAL, RTN_UNICAST, };

static struct fib_rule *fib_rules = &local_rule;

int inet_rtm_delrule(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct rtattr **rta = arg;
	struct rtmsg *rtm = NLMSG_DATA(nlh);
	struct fib_rule *r, **rp;

	for (rp=&fib_rules; (r=*rp) != NULL; rp=&r->r_next) {
		if ((!rta[RTA_SRC-1] || memcmp(RTA_DATA(rta[RTA_SRC-1]), &r->r_src, 4) == 0) &&
		    rtm->rtm_src_len == r->r_src_len &&
		    rtm->rtm_dst_len == r->r_dst_len &&
		    (!rta[RTA_DST-1] || memcmp(RTA_DATA(rta[RTA_DST-1]), &r->r_dst, 4) == 0) &&
		    rtm->rtm_tos == r->r_tos &&
		    (!rtm->rtm_type || rtm->rtm_type == r->r_action) &&
		    (!rta[RTA_PRIORITY-1] || memcmp(RTA_DATA(rta[RTA_PRIORITY-1]), &r->r_preference, 4) == 0) &&
		    (!rta[RTA_IFNAME-1] || strcmp(RTA_DATA(rta[RTA_IFNAME-1]), r->r_ifname) == 0) &&
		    (!rtm->rtm_table || (r && rtm->rtm_table == r->r_table))) {
			*rp = r->r_next;
			if (r != &default_rule && r != &main_rule && r != &local_rule)
				kfree(r);
			return 0;
		}
	}
	return -ESRCH;
}

/* Allocate new unique table id */

static struct fib_table *fib_empty_table(void)
{
	int id;

	for (id = 1; id <= RT_TABLE_MAX; id++)
		if (fib_tables[id] == NULL)
			return __fib_new_table(id);
	return NULL;
}


int inet_rtm_newrule(struct sk_buff *skb, struct nlmsghdr* nlh, void *arg)
{
	struct rtattr **rta = arg;
	struct rtmsg *rtm = NLMSG_DATA(nlh);
	struct fib_rule *r, *new_r, **rp;
	unsigned char table_id;

	if (rtm->rtm_src_len > 32 || rtm->rtm_dst_len > 32 ||
	    (rtm->rtm_tos & ~IPTOS_TOS_MASK))
		return -EINVAL;

	if (rta[RTA_IFNAME-1] && RTA_PAYLOAD(rta[RTA_IFNAME-1]) > IFNAMSIZ)
		return -EINVAL;

	table_id = rtm->rtm_table;
	if (table_id == RT_TABLE_UNSPEC) {
		struct fib_table *table;
		if (rtm->rtm_type == RTN_UNICAST || rtm->rtm_type == RTN_NAT) {
			if ((table = fib_empty_table()) == NULL)
				return -ENOBUFS;
			table_id = table->tb_id;
		}
	}

	new_r = kmalloc(sizeof(*new_r), GFP_KERNEL);
	if (!new_r)
		return -ENOMEM;
	memset(new_r, 0, sizeof(*new_r));
	if (rta[RTA_SRC-1])
		memcpy(&new_r->r_src, RTA_DATA(rta[RTA_SRC-1]), 4);
	if (rta[RTA_DST-1])
		memcpy(&new_r->r_dst, RTA_DATA(rta[RTA_DST-1]), 4);
	if (rta[RTA_GATEWAY-1])
		memcpy(&new_r->r_srcmap, RTA_DATA(rta[RTA_GATEWAY-1]), 4);
	new_r->r_src_len = rtm->rtm_src_len;
	new_r->r_dst_len = rtm->rtm_dst_len;
	new_r->r_srcmask = inet_make_mask(rtm->rtm_src_len);
	new_r->r_dstmask = inet_make_mask(rtm->rtm_dst_len);
	new_r->r_tos = rtm->rtm_tos;
	new_r->r_action = rtm->rtm_type;
	new_r->r_flags = rtm->rtm_flags;
	if (rta[RTA_PRIORITY-1])
		memcpy(&new_r->r_preference, RTA_DATA(rta[RTA_PRIORITY-1]), 4);
	new_r->r_table = table_id;
	if (rta[RTA_IFNAME-1]) {
		struct device *dev;
		memcpy(new_r->r_ifname, RTA_DATA(rta[RTA_IFNAME-1]), IFNAMSIZ);
		new_r->r_ifname[IFNAMSIZ-1] = 0;
		new_r->r_ifindex = -1;
		dev = dev_get(new_r->r_ifname);
		if (dev)
			new_r->r_ifindex = dev->ifindex;
	}

	rp = &fib_rules;
	if (!new_r->r_preference) {
		r = fib_rules;
		if (r && (r = r->r_next) != NULL) {
			rp = &fib_rules->r_next;
			if (r->r_preference)
				new_r->r_preference = r->r_preference - 1;
		}
	}

	while ( (r = *rp) != NULL ) {
		if (r->r_preference > new_r->r_preference)
			break;
		rp = &r->r_next;
	}

	new_r->r_next = r;
	*rp = new_r;
	return 0;
}

u32 fib_rules_map_destination(u32 daddr, struct fib_result *res)
{
	u32 mask = inet_make_mask(res->prefixlen);
	return (daddr&~mask)|res->fi->fib_nh->nh_gw;
}

u32 fib_rules_policy(u32 saddr, struct fib_result *res, unsigned *flags)
{
	struct fib_rule *r = res->r;

	if (r->r_action == RTN_NAT) {
		int addrtype = inet_addr_type(r->r_srcmap);

		if (addrtype == RTN_NAT) {
			/* Packet is from  translated source; remember it */
			saddr = (saddr&~r->r_srcmask)|r->r_srcmap;
			*flags |= RTCF_SNAT;
		} else if (addrtype == RTN_LOCAL || r->r_srcmap == 0) {
			/* Packet is from masqueraded source; remember it */
			saddr = r->r_srcmap;
			*flags |= RTCF_MASQ;
		}
	}
	return saddr;
}

static void fib_rules_detach(struct device *dev)
{
	struct fib_rule *r;

	for (r=fib_rules; r; r=r->r_next) {
		if (r->r_ifindex == dev->ifindex)
			r->r_ifindex = -1;
	}
}

static void fib_rules_attach(struct device *dev)
{
	struct fib_rule *r;

	for (r=fib_rules; r; r=r->r_next) {
		if (r->r_ifindex == -1 && strcmp(dev->name, r->r_ifname) == 0)
			r->r_ifindex = dev->ifindex;
	}
}

int fib_lookup(const struct rt_key *key, struct fib_result *res)
{
	int err;
	struct fib_rule *r, *policy;
	struct fib_table *tb;

	u32 daddr = key->dst;
	u32 saddr = key->src;

FRprintk("Lookup: %08x <- %08x ", key->dst, key->src);
	for (r = fib_rules; r; r=r->r_next) {
		if (((saddr^r->r_src) & r->r_srcmask) ||
		    ((daddr^r->r_dst) & r->r_dstmask) ||
#ifdef CONFIG_IP_TOS_ROUTING
		    (r->r_tos && r->r_tos != key->tos) ||
#endif
		    (r->r_ifindex && r->r_ifindex != key->iif))
			continue;

FRprintk("tb %d r %d ", r->r_table, r->r_action);
		switch (r->r_action) {
		case RTN_UNICAST:
			policy = NULL;
			break;
		case RTN_NAT:
			policy = r;
			break;
		case RTN_UNREACHABLE:
			return -ENETUNREACH;
		default:
		case RTN_BLACKHOLE:
			return -EINVAL;
		case RTN_PROHIBIT:
			return -EACCES;
		}

		if ((tb = fib_get_table(r->r_table)) == NULL)
			continue;
		err = tb->tb_lookup(tb, key, res);
		if (err == 0) {
FRprintk("ok\n");
			res->r = policy;
			return 0;
		}
		if (err < 0)
			return err;
FRprintk("RCONT ");
	}
FRprintk("FAILURE\n");
	return -ENETUNREACH;
}

static int fib_rules_event(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct device *dev = ptr;

	if (event == NETDEV_UNREGISTER)
		fib_rules_detach(dev);
	else if (event == NETDEV_REGISTER)
		fib_rules_attach(dev);
	return NOTIFY_DONE;
}


struct notifier_block fib_rules_notifier = {
	fib_rules_event,
	NULL,
	0
};

#ifdef CONFIG_RTNETLINK

extern __inline__ int inet_fill_rule(struct sk_buff *skb,
				     struct fib_rule *r,
				     struct netlink_callback *cb)
{
	struct rtmsg *rtm;
	struct nlmsghdr  *nlh;
	unsigned char	 *b = skb->tail;

	nlh = NLMSG_PUT(skb, NETLINK_CREDS(cb->skb)->pid, cb->nlh->nlmsg_seq, RTM_NEWRULE, sizeof(*rtm));
	rtm = NLMSG_DATA(nlh);
	rtm->rtm_family = AF_INET;
	rtm->rtm_dst_len = r->r_dst_len;
	rtm->rtm_src_len = r->r_src_len;
	rtm->rtm_tos = r->r_tos;
	rtm->rtm_table = r->r_table;
	rtm->rtm_protocol = 0;
	rtm->rtm_scope = 0;
#ifdef CONFIG_RTNL_OLD_IFINFO
	rtm->rtm_nhs = 0;
	rtm->rtm_optlen = 0;
#endif
	rtm->rtm_type = r->r_action;
	rtm->rtm_flags = r->r_flags;

	if (r->r_dst_len)
		RTA_PUT(skb, RTA_DST, 4, &r->r_dst);
	if (r->r_src_len)
		RTA_PUT(skb, RTA_SRC, 4, &r->r_src);
	if (r->r_ifname[0])
		RTA_PUT(skb, RTA_IFNAME, IFNAMSIZ, &r->r_ifname);
	if (r->r_preference)
		RTA_PUT(skb, RTA_PRIORITY, 4, &r->r_preference);
	if (r->r_srcmap)
		RTA_PUT(skb, RTA_GATEWAY, 4, &r->r_srcmap);
	nlh->nlmsg_len = skb->tail - b;
	return skb->len;

nlmsg_failure:
rtattr_failure:
	skb_put(skb, b - skb->tail);
	return -1;
}

int inet_dump_rules(struct sk_buff *skb, struct netlink_callback *cb)
{
	int idx;
	int s_idx = cb->args[0];
	struct fib_rule *r;

	for (r=fib_rules, idx=0; r; r = r->r_next, idx++) {
		if (idx < s_idx)
			continue;
		if (inet_fill_rule(skb, r, cb) < 0)
			break;
	}
	cb->args[0] = idx;

	return skb->len;
}

#endif /* CONFIG_RTNETLINK */

__initfunc(void fib_rules_init(void))
{
	register_netdevice_notifier(&fib_rules_notifier);
}
