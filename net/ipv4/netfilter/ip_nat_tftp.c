/* (C) 2001-2002 Magnus Boden <mb@ozaba.mine.nu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Version: 0.0.7
 *
 * Thu 21 Mar 2002 Harald Welte <laforge@gnumonks.org>
 * 	- Port to newnat API
 *
 * This module currently supports DNAT:
 * iptables -t nat -A PREROUTING -d x.x.x.x -j DNAT --to-dest x.x.x.y
 *
 * and SNAT:
 * iptables -t nat -A POSTROUTING { -j MASQUERADE , -j SNAT --to-source x.x.x.x }
 *
 * It has not been tested with
 * -j SNAT --to-source x.x.x.x-x.x.x.y since I only have one external ip
 * If you do test this please let me know if it works or not.
 *
 */

#include <linux/module.h>
#include <linux/netfilter_ipv4.h>
#include <linux/ip.h>
#include <linux/udp.h>

#include <linux/netfilter.h>
#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/ip_conntrack_tftp.h>
#include <linux/netfilter_ipv4/ip_nat_helper.h>
#include <linux/netfilter_ipv4/ip_nat_rule.h>
#include <linux/moduleparam.h>

MODULE_AUTHOR("Magnus Boden <mb@ozaba.mine.nu>");
MODULE_DESCRIPTION("tftp NAT helper");
MODULE_LICENSE("GPL");

#define MAX_PORTS 8

static int ports[MAX_PORTS];
static int ports_c = 0;
module_param_array(ports, int, &ports_c, 0400);
MODULE_PARM_DESC(ports, "port numbers of tftp servers");

#if 0
#define DEBUGP(format, args...) printk("%s:%s:" format, \
                                       __FILE__, __FUNCTION__ , ## args)
#else
#define DEBUGP(format, args...)
#endif
static unsigned int 
tftp_nat_help(struct ip_conntrack *ct,
	      struct ip_conntrack_expect *exp,
	      struct ip_nat_info *info,
	      enum ip_conntrack_info ctinfo,
	      unsigned int hooknum,
	      struct sk_buff **pskb)
{
	int dir = CTINFO2DIR(ctinfo);
	struct tftphdr _tftph, *tfh;
	struct ip_conntrack_tuple repl;

	if (!((hooknum == NF_IP_POST_ROUTING && dir == IP_CT_DIR_ORIGINAL)
	      || (hooknum == NF_IP_PRE_ROUTING && dir == IP_CT_DIR_REPLY))) 
		return NF_ACCEPT;

	if (!exp) {
		DEBUGP("no conntrack expectation to modify\n");
		return NF_ACCEPT;
	}

	tfh = skb_header_pointer(*pskb,
				 (*pskb)->nh.iph->ihl*4+sizeof(struct udphdr),
				 sizeof(_tftph), &_tftph);
	if (tfh == NULL)
		return NF_DROP;

	switch (ntohs(tfh->opcode)) {
	/* RRQ and WRQ works the same way */
	case TFTP_OPCODE_READ:
	case TFTP_OPCODE_WRITE:
		repl = ct->tuplehash[IP_CT_DIR_REPLY].tuple;
		DEBUGP("");
		DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_ORIGINAL].tuple);
		DUMP_TUPLE(&ct->tuplehash[IP_CT_DIR_REPLY].tuple);
		DEBUGP("expecting: ");
		DUMP_TUPLE(&repl);
		DUMP_TUPLE(&exp->mask);
		ip_conntrack_change_expect(exp, &repl);
		break;
	default:
		DEBUGP("Unknown opcode\n");
	}               

	return NF_ACCEPT;
}

static unsigned int 
tftp_nat_expected(struct sk_buff **pskb,
		  unsigned int hooknum,
		  struct ip_conntrack *ct, 
		  struct ip_nat_info *info) 
{
	const struct ip_conntrack *master = ct->master->expectant;
	const struct ip_conntrack_tuple *orig = 
			&master->tuplehash[IP_CT_DIR_ORIGINAL].tuple;
	struct ip_nat_range range;
#if 0
	const struct ip_conntrack_tuple *repl =
			&master->tuplehash[IP_CT_DIR_REPLY].tuple;
	struct udphdr _udph, *uh;

	uh = skb_header_pointer(*pskb,
				(*pskb)->nh.iph->ihl*4,
				sizeof(_udph), &_udph);
	if (uh == NULL)
		return NF_DROP;
#endif

	IP_NF_ASSERT(info);
	IP_NF_ASSERT(master);
	IP_NF_ASSERT(!(info->initialized & (1 << HOOK2MANIP(hooknum))));

	range.flags = IP_NAT_RANGE_MAP_IPS;

	if (HOOK2MANIP(hooknum) == IP_NAT_MANIP_SRC) {
		range.min_ip = range.max_ip = orig->dst.ip; 
		DEBUGP("orig: %u.%u.%u.%u:%u <-> %u.%u.%u.%u:%u "
			"newsrc: %u.%u.%u.%u\n",
                        NIPQUAD((*pskb)->nh.iph->saddr), ntohs(uh->source),
 			NIPQUAD((*pskb)->nh.iph->daddr), ntohs(uh->dest),
			NIPQUAD(orig->dst.ip));
	} else {
		range.min_ip = range.max_ip = orig->src.ip;
		range.min.udp.port = range.max.udp.port = orig->src.u.udp.port;
		range.flags |= IP_NAT_RANGE_PROTO_SPECIFIED;

		DEBUGP("orig: %u.%u.%u.%u:%u <-> %u.%u.%u.%u:%u "
			"newdst: %u.%u.%u.%u:%u\n",
                        NIPQUAD((*pskb)->nh.iph->saddr), ntohs(uh->source),
                        NIPQUAD((*pskb)->nh.iph->daddr), ntohs(uh->dest),
                        NIPQUAD(orig->src.ip), ntohs(orig->src.u.udp.port));
	}

	return ip_nat_setup_info(ct, &range, hooknum);
}

static struct ip_nat_helper tftp[MAX_PORTS];
static char tftp_names[MAX_PORTS][10];

static void fini(void)
{
	int i;

	for (i = 0 ; i < ports_c; i++) {
		DEBUGP("unregistering helper for port %d\n", ports[i]);
		ip_nat_helper_unregister(&tftp[i]);
	}
}

static int __init init(void)
{
	int i, ret = 0;
	char *tmpname;

	if (ports_c == 0)
		ports[ports_c++] = TFTP_PORT;

	for (i = 0; i < ports_c; i++) {
		memset(&tftp[i], 0, sizeof(struct ip_nat_helper));

		tftp[i].tuple.dst.protonum = IPPROTO_UDP;
		tftp[i].tuple.src.u.udp.port = htons(ports[i]);
		tftp[i].mask.dst.protonum = 0xFFFF;
		tftp[i].mask.src.u.udp.port = 0xFFFF;
		tftp[i].help = tftp_nat_help;
		tftp[i].flags = 0;
		tftp[i].me = THIS_MODULE;
		tftp[i].expect = tftp_nat_expected;

		tmpname = &tftp_names[i][0];
		if (ports[i] == TFTP_PORT)
			sprintf(tmpname, "tftp");
		else
			sprintf(tmpname, "tftp-%d", i);
		tftp[i].name = tmpname;
		
		DEBUGP("ip_nat_tftp: registering for port %d: name %s\n",
			ports[i], tftp[i].name);
		ret = ip_nat_helper_register(&tftp[i]);

		if (ret) {
			printk("ip_nat_tftp: unable to register for port %d\n",
				ports[i]);
			fini();
			return ret;
		}
	}
	return ret;
}

module_init(init);
module_exit(fini);
