/* SCTP kernel reference Implementation
 * Copyright (c) 2001 Nokia, Inc.
 * Copyright (c) 2001 La Monte H.P. Yarroll
 * Copyright (c) 2002 International Business Machines, Corp.
 *
 * This file is part of the SCTP kernel reference Implementation
 *
 * SCTP over IPv6.
 *
 * The SCTP reference implementation is free software;
 * you can redistribute it and/or modify it under the terms of
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The SCTP reference implementation is distributed in the hope that it
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 *
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by:
 *    Le Yanqun             <yanqun.le@nokia.com>
 *    Hui Huang		    <hui.huang@nokia.com>
 *    La Monte H.P. Yarroll <piggy@acm.org>
 *    Sridhar Samudrala	    <sri@us.ibm.com>
 *    Jon Grimm             <jgrimm@us.ibm.com>
 *
 * Based on:
 *      linux/net/ipv6/tcp_ipv6.c
 *
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/sched.h>
#include <linux/in.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/init.h>
#include <linux/ipsec.h>

#include <linux/ipv6.h>
#include <linux/icmpv6.h>
#include <linux/random.h>

#include <net/protocol.h>
#include <net/tcp.h>
#include <net/ndisc.h>
#include <net/ipv6.h>
#include <net/transp_v6.h>
#include <net/addrconf.h>
#include <net/ip6_route.h>
#include <net/inet_common.h>
#include <net/inet_ecn.h>
#include <net/sctp/sctp.h>

#include <asm/uaccess.h>

extern struct notifier_block sctp_inetaddr_notifier;

/* FIXME: This macro needs to be moved to a common header file. */
#define NIP6(addr) \
        ntohs((addr)->s6_addr16[0]), \
        ntohs((addr)->s6_addr16[1]), \
        ntohs((addr)->s6_addr16[2]), \
        ntohs((addr)->s6_addr16[3]), \
        ntohs((addr)->s6_addr16[4]), \
        ntohs((addr)->s6_addr16[5]), \
        ntohs((addr)->s6_addr16[6]), \
        ntohs((addr)->s6_addr16[7])

/* FIXME: Comments. */
static inline void sctp_v6_err(struct sk_buff *skb,
			       struct inet6_skb_parm *opt,
			       int type, int code, int offset, __u32 info)
{
	/* BUG.  WRITE ME.  */
}

/* Based on tcp_v6_xmit() in tcp_ipv6.c. */
static inline int sctp_v6_xmit(struct sk_buff *skb,
			       struct sctp_transport *transport, int ipfragok)
{
	struct sock *sk = skb->sk;
	struct ipv6_pinfo *np = inet6_sk(sk);
	struct flowi fl;
	struct dst_entry *dst = skb->dst;
	struct rt6_info *rt6 = (struct rt6_info *)dst;

	fl.proto = sk->protocol;

	/* Fill in the dest address from the route entry passed with the skb
	 * and the source address from the transport.
	 */ 
	fl.fl6_dst = &rt6->rt6i_dst.addr;
	fl.fl6_src = &transport->saddr.v6.sin6_addr; 

	fl.fl6_flowlabel = np->flow_label;
	IP6_ECN_flow_xmit(sk, fl.fl6_flowlabel);
	fl.oif = sk->bound_dev_if;
	fl.uli_u.ports.sport = inet_sk(sk)->sport;
	fl.uli_u.ports.dport = inet_sk(sk)->dport;

	if (np->opt && np->opt->srcrt) {
		struct rt0_hdr *rt0 = (struct rt0_hdr *) np->opt->srcrt;
		fl.nl_u.ip6_u.daddr = rt0->addr;
	}

	SCTP_DEBUG_PRINTK("%s: skb:%p, len:%d, "
			  "src:%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x "
			  "dst:%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
			  __FUNCTION__, skb, skb->len, NIP6(fl.fl6_src),
			  NIP6(fl.fl6_dst));

	SCTP_INC_STATS(SctpOutSCTPPacks);

	return ip6_xmit(sk, skb, &fl, np->opt);
}

/* Returns the dst cache entry for the given source and destination ip
 * addresses.
 */
struct dst_entry *sctp_v6_get_dst(sctp_association_t *asoc,
				  union sctp_addr *daddr,
				  union sctp_addr *saddr)
{
	struct dst_entry *dst;
	struct flowi fl = {
		.nl_u = { .ip6_u = { .daddr = &daddr->v6.sin6_addr, } } };

	SCTP_DEBUG_PRINTK("%s: DST=%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x ",
			  __FUNCTION__, NIP6(fl.fl6_dst));

	if (saddr) {
		fl.fl6_src = &saddr->v6.sin6_addr;
		SCTP_DEBUG_PRINTK(
			"SRC=%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x - ",
			NIP6(fl.fl6_src));
	}

	dst = ip6_route_output(NULL, &fl);
	if (dst) {
		struct rt6_info *rt;
		rt = (struct rt6_info *)dst;
		SCTP_DEBUG_PRINTK(
			"rt6_dst:%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x "
			"rt6_src:%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
			NIP6(&rt->rt6i_dst.addr), NIP6(&rt->rt6i_src.addr));
	} else {
		SCTP_DEBUG_PRINTK("NO ROUTE\n");
	}

	return dst;
}

/* Returns the number of consecutive initial bits that match in the 2 ipv6
 * addresses.
 */ 
static inline int sctp_v6_addr_match_len(union sctp_addr *s1,
					 union sctp_addr *s2)
{
	struct in6_addr *a1 = &s1->v6.sin6_addr;
	struct in6_addr *a2 = &s2->v6.sin6_addr;
	int i, j;

	for (i = 0; i < 4 ; i++) {
		__u32 a1xora2;

		a1xora2 = a1->s6_addr32[i] ^ a2->s6_addr32[i];
		
		if ((j = fls(ntohl(a1xora2))))
			return (i * 32 + 32 - j);
	}

	return (i*32);
}

/* Fills in the source address(saddr) based on the destination address(daddr)
 * and asoc's bind address list.
 */   
void sctp_v6_get_saddr(sctp_association_t *asoc, struct dst_entry *dst,
		       union sctp_addr *daddr, union sctp_addr *saddr)
{
	sctp_bind_addr_t *bp;
	rwlock_t *addr_lock;
	struct sockaddr_storage_list *laddr;
	struct list_head *pos;
	sctp_scope_t scope;
	union sctp_addr *baddr = NULL;
	__u8 matchlen = 0;
	__u8 bmatchlen;

	SCTP_DEBUG_PRINTK("%s: asoc:%p dst:%p "
			  "daddr:%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x ",
			  __FUNCTION__, asoc, dst, NIP6(&daddr->v6.sin6_addr));

	if (!asoc) {
		ipv6_get_saddr(dst, &daddr->v6.sin6_addr, &saddr->v6.sin6_addr);
		SCTP_DEBUG_PRINTK("saddr from ipv6_get_saddr: "
				  "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
				  NIP6(&saddr->v6.sin6_addr));
		return;
	}

	scope = sctp_scope(daddr);

	bp = &asoc->base.bind_addr;
	addr_lock = &asoc->base.addr_lock;

	/* Go through the bind address list and find the best source address
	 * that matches the scope of the destination address.
	 */
	sctp_read_lock(addr_lock);
	list_for_each(pos, &bp->address_list) {
		laddr = list_entry(pos, struct sockaddr_storage_list, list);
		if ((laddr->a.sa.sa_family == AF_INET6) &&
		    (scope <= sctp_scope(&laddr->a))) {
			bmatchlen = sctp_v6_addr_match_len(daddr, &laddr->a);
			if (!baddr || (matchlen < bmatchlen)) {
				baddr = &laddr->a;
				matchlen = bmatchlen;
			}
		}
	}

	if (baddr) {
		memcpy(saddr, baddr, sizeof(union sctp_addr));
		SCTP_DEBUG_PRINTK("saddr: "
				  "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
				  NIP6(&saddr->v6.sin6_addr));
	} else {
		printk(KERN_ERR "%s: asoc:%p Could not find a valid source "
		       "address for the "
		       "dest:%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
		       __FUNCTION__, asoc, NIP6(&daddr->v6.sin6_addr));
	}

	sctp_read_unlock(addr_lock);
}

/* Make a copy of all potential local addresses. */
static void sctp_v6_copy_addrlist(struct list_head *addrlist,
				  struct net_device *dev)
{
	struct inet6_dev *in6_dev;
	struct inet6_ifaddr *ifp;
	struct sockaddr_storage_list *addr;

	read_lock(&addrconf_lock);
	if ((in6_dev = __in6_dev_get(dev)) == NULL) {
		read_unlock(&addrconf_lock);
		return;
	}

	read_lock(&in6_dev->lock);
	for (ifp = in6_dev->addr_list; ifp; ifp = ifp->if_next) {
		/* Add the address to the local list.  */
		addr = t_new(struct sockaddr_storage_list, GFP_ATOMIC);
		if (addr) {
			addr->a.v6.sin6_family = AF_INET6;
			addr->a.v6.sin6_port = 0;
			addr->a.v6.sin6_addr = ifp->addr;
			INIT_LIST_HEAD(&addr->list);
			list_add_tail(&addr->list, addrlist);
		}
	}

	read_unlock(&in6_dev->lock);
	read_unlock(&addrconf_lock);
}

/* Initialize a sockaddr_storage from in incoming skb. */
static void sctp_v6_from_skb(union sctp_addr *addr,struct sk_buff *skb,
			     int is_saddr)
{
	void *from;
	__u16 *port;
	struct sctphdr *sh;

	port = &addr->v6.sin6_port;
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_flowinfo = 0; /* FIXME */
	addr->v6.sin6_scope_id = 0; /* FIXME */

	sh = (struct sctphdr *) skb->h.raw;
	if (is_saddr) {
		*port  = ntohs(sh->source);
		from = &skb->nh.ipv6h->saddr;
	} else {
		*port = ntohs(sh->dest);
		from = &skb->nh.ipv6h->daddr;
	}
	ipv6_addr_copy(&addr->v6.sin6_addr, from);
}

/* Initialize an sctp_addr from a socket. */
static void sctp_v6_from_sk(union sctp_addr *addr, struct sock *sk)
{
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_port = inet_sk(sk)->num;
	addr->v6.sin6_addr = inet6_sk(sk)->rcv_saddr;
}

/* Initialize sk->rcv_saddr from sctp_addr. */
static void sctp_v6_to_sk(union sctp_addr *addr, struct sock *sk)
{
	inet6_sk(sk)->rcv_saddr = addr->v6.sin6_addr;
}

/* Initialize a sctp_addr from a dst_entry. */
static void sctp_v6_dst_saddr(union sctp_addr *addr, struct dst_entry *dst,
			      unsigned short port)
{
	struct rt6_info *rt = (struct rt6_info *)dst;
	addr->sa.sa_family = AF_INET6;
	addr->v6.sin6_port = port;
	ipv6_addr_copy(&addr->v6.sin6_addr, &rt->rt6i_src.addr);
}

/* Compare addresses exactly.  Well.. almost exactly; ignore scope_id
 * for now.  FIXME: v4-mapped-v6.
 */
static int sctp_v6_cmp_addr(const union sctp_addr *addr1,
			    const union sctp_addr *addr2)
{
	int match;
	if (addr1->sa.sa_family != addr2->sa.sa_family)
		return 0;
	match = !ipv6_addr_cmp((struct in6_addr *)&addr1->v6.sin6_addr,
			       (struct in6_addr *)&addr2->v6.sin6_addr);

	return match;
}

/* Initialize addr struct to INADDR_ANY. */
static void sctp_v6_inaddr_any(union sctp_addr *addr, unsigned short port)
{
	memset(addr, 0x00, sizeof(union sctp_addr));
	addr->v6.sin6_family = AF_INET6;
	addr->v6.sin6_port = port;
}

/* Is this a wildcard address? */
static int sctp_v6_is_any(const union sctp_addr *addr)
{
	int type;
	type = ipv6_addr_type((struct in6_addr *)&addr->v6.sin6_addr);
	return IPV6_ADDR_ANY == type;
}

/* Should this be available for binding?   */
static int sctp_v6_available(const union sctp_addr *addr)
{
	int type;
	struct in6_addr *in6 = (struct in6_addr *)&addr->v6.sin6_addr;

	type = ipv6_addr_type(in6);
	if (IPV6_ADDR_ANY == type)
		return 1;
	if (!(type & IPV6_ADDR_UNICAST))
		return 0;

	return ipv6_chk_addr(in6, NULL);
}


/* This function checks if the address is a valid address to be used for
 * SCTP.
 *
 * Output:
 * Return 0 - If the address is a non-unicast or an illegal address.
 * Return 1 - If the address is a unicast.
 */
static int sctp_v6_addr_valid(union sctp_addr *addr)
{
	int ret = ipv6_addr_type(&addr->v6.sin6_addr);

	/* FIXME:  v4-mapped-v6 address support. */

	/* Is this a non-unicast address */
	if (!(ret & IPV6_ADDR_UNICAST))
		return 0;

	return 1;
}

/* What is the scope of 'addr'?  */
static sctp_scope_t sctp_v6_scope(union sctp_addr *addr)
{
	int v6scope;
	sctp_scope_t retval;

	/* The IPv6 scope is really a set of bit fields.
	 * See IFA_* in <net/if_inet6.h>.  Map to a generic SCTP scope.
	 */

	v6scope = ipv6_addr_scope(&addr->v6.sin6_addr);
	switch (v6scope) {
	case IFA_HOST:
		retval = SCTP_SCOPE_LOOPBACK;
		break;
	case IFA_LINK:
		retval = SCTP_SCOPE_LINK;
		break;
	case IFA_SITE:
		retval = SCTP_SCOPE_PRIVATE;
		break;
	default:
		retval = SCTP_SCOPE_GLOBAL;
		break;
	};

	return retval;
}

/* Initialize a PF_INET6 socket msg_name. */
static void sctp_inet6_msgname(char *msgname, int *addr_len)
{
	struct sockaddr_in6 *sin6;

	sin6 = (struct sockaddr_in6 *)msgname;
	sin6->sin6_family = AF_INET6;
	sin6->sin6_flowinfo = 0;
	sin6->sin6_scope_id = 0;
	*addr_len = sizeof(struct sockaddr_in6);
}

/* Initialize a PF_INET msgname from a ulpevent. */
static void sctp_inet6_event_msgname(struct sctp_ulpevent *event, char *msgname,
				     int *addrlen)
{
	struct sockaddr_in6 *sin6, *sin6from;

	if (msgname) {
		union sctp_addr *addr;

		sctp_inet6_msgname(msgname, addrlen);
		sin6 = (struct sockaddr_in6 *)msgname;
		sin6->sin6_port = htons(event->asoc->peer.port);
		addr = &event->asoc->peer.primary_addr;

		/* Note: If we go to a common v6 format, this code
		 * will change.
		 */

		/* Map ipv4 address into v4-mapped-on-v6 address.  */
		if (AF_INET == addr->sa.sa_family) {
			/* FIXME: Easy, but there was no way to test this
			 * yet.
			 */
			return;
		}

		sin6from = &event->asoc->peer.primary_addr.v6;
		ipv6_addr_copy(&sin6->sin6_addr, &sin6from->sin6_addr);
	}
}

/* Initialize a msg_name from an inbound skb. */
static void sctp_inet6_skb_msgname(struct sk_buff *skb, char *msgname,
				   int *addr_len)
{
	struct sctphdr *sh;
	struct sockaddr_in6 *sin6;

	if (msgname) {
		sctp_inet6_msgname(msgname, addr_len);
		sin6 = (struct sockaddr_in6 *)msgname;
		sh = (struct sctphdr *)skb->h.raw;
		sin6->sin6_port = sh->source;

		/* FIXME: Map ipv4 address into v4-mapped-on-v6 address. */
		if (__constant_htons(ETH_P_IP) == skb->protocol) {
			/* FIXME: Easy, but there was no way to test this
			 * yet.
			 */
			return;
		}

		/* Otherwise, just copy the v6 address. */

		ipv6_addr_copy(&sin6->sin6_addr, &skb->nh.ipv6h->saddr);
		if (ipv6_addr_type(&sin6->sin6_addr) & IPV6_ADDR_LINKLOCAL) {
			struct inet6_skb_parm *opt =
				(struct inet6_skb_parm *) skb->cb;
			sin6->sin6_scope_id = opt->iif;
		}
	}
}

/* Do we support this AF? */
static int sctp_inet6_af_supported(sa_family_t family)
{
	/* FIXME:  v4-mapped-v6 addresses.  The I-D is still waffling
	 * on what to do with sockaddr formats for PF_INET6 sockets.
	 * For now assume we'll support both.
	 */
	switch (family) {
	case AF_INET6:
	case AF_INET:
		return 1;
	default:
		return 0;
	}
}

/* Address matching with wildcards allowed.  This extra level
 * of indirection lets us choose whether a PF_INET6 should
 * disallow any v4 addresses if we so choose.
 */
static int sctp_inet6_cmp_addr(const union sctp_addr *addr1,
			       const union sctp_addr *addr2,
			       struct sctp_opt *opt)
{
	struct sctp_af *af1, *af2;

	af1 = sctp_get_af_specific(addr1->sa.sa_family);
	af2 = sctp_get_af_specific(addr2->sa.sa_family);

	if (!af1 || !af2)
		return 0;
	/* Today, wildcard AF_INET/AF_INET6. */
	if (sctp_is_any(addr1) || sctp_is_any(addr2))
		return 1;

	if (addr1->sa.sa_family != addr2->sa.sa_family)
		return 0;

	return af1->cmp_addr(addr1, addr2);
}

/* Verify that the provided sockaddr looks bindable.   Common verification,
 * has already been taken care of.
 */
static int sctp_inet6_bind_verify(struct sctp_opt *opt, union sctp_addr *addr)
{
	struct sctp_af *af;

	/* ASSERT: address family has already been verified. */
	if (addr->sa.sa_family != AF_INET6) {
		af = sctp_get_af_specific(addr->sa.sa_family);
	} else
		af = opt->pf->af;

	return af->available(addr);
}

/* Fill in Supported Address Type information for INIT and INIT-ACK
 * chunks.   Note: In the future, we may want to look at sock options
 * to determine whether a PF_INET6 socket really wants to have IPV4
 * addresses.  
 * Returns number of addresses supported.
 */
static int sctp_inet6_supported_addrs(const struct sctp_opt *opt,
				      __u16 *types) 
{
	types[0] = SCTP_PARAM_IPV4_ADDRESS;
	types[1] = SCTP_PARAM_IPV6_ADDRESS;
	return 2;
}

static struct proto_ops inet6_seqpacket_ops = {
	.family     = PF_INET6,
	.release    = inet6_release,
	.bind       = inet6_bind,
	.connect    = inet_dgram_connect,
	.socketpair = sock_no_socketpair,
	.accept     = inet_accept,
	.getname    = inet6_getname,
	.poll       = sctp_poll,
	.ioctl      = inet6_ioctl,
	.listen     = sctp_inet_listen,
	.shutdown   = inet_shutdown,
	.setsockopt = inet_setsockopt,
	.getsockopt = inet_getsockopt,
	.sendmsg    = inet_sendmsg,
	.recvmsg    = inet_recvmsg,
	.mmap       = sock_no_mmap,
};

static struct inet_protosw sctpv6_protosw = {
	.type          = SOCK_SEQPACKET,
	.protocol      = IPPROTO_SCTP,
	.prot 	       = &sctp_prot,
	.ops           = &inet6_seqpacket_ops,
	.capability    = -1,
	.no_check      = 0,
	.flags         = SCTP_PROTOSW_FLAG
};

static struct inet6_protocol sctpv6_protocol = {
	.handler      = sctp_rcv,
	.err_handler  = sctp_v6_err,
};

static struct sctp_af sctp_ipv6_specific = {
	.sctp_xmit       = sctp_v6_xmit,
	.setsockopt      = ipv6_setsockopt,
	.getsockopt      = ipv6_getsockopt,
	.get_dst	 = sctp_v6_get_dst,
	.get_saddr	 = sctp_v6_get_saddr,
	.copy_addrlist   = sctp_v6_copy_addrlist,
	.from_skb        = sctp_v6_from_skb,
	.from_sk         = sctp_v6_from_sk,
	.to_sk           = sctp_v6_to_sk,
	.dst_saddr       = sctp_v6_dst_saddr,
	.cmp_addr        = sctp_v6_cmp_addr,
	.scope           = sctp_v6_scope,
	.addr_valid      = sctp_v6_addr_valid,
	.inaddr_any      = sctp_v6_inaddr_any,
	.is_any          = sctp_v6_is_any,
	.available       = sctp_v6_available,
	.net_header_len  = sizeof(struct ipv6hdr),
	.sockaddr_len    = sizeof(struct sockaddr_in6),
	.sa_family       = AF_INET6,
};

static struct sctp_pf sctp_pf_inet6_specific = {
	.event_msgname = sctp_inet6_event_msgname,
	.skb_msgname   = sctp_inet6_skb_msgname,
	.af_supported  = sctp_inet6_af_supported,
	.cmp_addr      = sctp_inet6_cmp_addr,
	.bind_verify   = sctp_inet6_bind_verify,
	.supported_addrs = sctp_inet6_supported_addrs,
	.af            = &sctp_ipv6_specific,
};

/* Initialize IPv6 support and register with inet6 stack.  */
int sctp_v6_init(void)
{
	/* Register inet6 protocol. */
	if (inet6_add_protocol(&sctpv6_protocol, IPPROTO_SCTP) < 0)
		return -EAGAIN;

	/* Add SCTPv6 to inetsw6 linked list. */
	inet6_register_protosw(&sctpv6_protosw);

	/* Register the SCTP specfic PF_INET6 functions. */
	sctp_register_pf(&sctp_pf_inet6_specific, PF_INET6);

	/* Register the SCTP specfic AF_INET6 functions. */
	sctp_register_af(&sctp_ipv6_specific);

	/* Register notifier for inet6 address additions/deletions. */
	register_inet6addr_notifier(&sctp_inetaddr_notifier);

	return 0;
}

/* IPv6 specific exit support. */
void sctp_v6_exit(void)
{
	list_del(&sctp_ipv6_specific.list);
	inet6_del_protocol(&sctpv6_protocol, IPPROTO_SCTP);
	inet6_unregister_protosw(&sctpv6_protosw);
	unregister_inet6addr_notifier(&sctp_inetaddr_notifier);
}
