/*
 * ip_vs_xmit.c: various packet transmitters for IPVS
 *
 * Version:     $Id: ip_vs_xmit.c,v 1.2 2002/11/30 01:50:35 wensong Exp $
 *
 * Authors:     Wensong Zhang <wensong@linuxvirtualserver.org>
 *              Julian Anastasov <ja@ssi.bg>
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Changes:
 *
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/compiler.h>
#include <linux/ip.h>
#include <linux/tcp.h>                  /* for tcphdr */
#include <net/tcp.h>                    /* for csum_tcpudp_magic */
#include <net/udp.h>
#include <net/icmp.h>                   /* for icmp_send */
#include <net/route.h>                  /* for ip_route_output */
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>

#include <net/ip_vs.h>


/*
 *      Destination cache to speed up outgoing route lookup
 */
static inline void
__ip_vs_dst_set(struct ip_vs_dest *dest, u32 rtos, struct dst_entry *dst)
{
	struct dst_entry *old_dst;

	old_dst = dest->dst_cache;
	dest->dst_cache = dst;
	dest->dst_rtos = rtos;
	dst_release(old_dst);
}

static inline struct dst_entry *
__ip_vs_dst_check(struct ip_vs_dest *dest, u32 rtos, u32 cookie)
{
	struct dst_entry *dst = dest->dst_cache;

	if (!dst)
		return NULL;
	if ((dst->obsolete || rtos != dest->dst_rtos) &&
	    dst->ops->check(dst, cookie) == NULL) {
		dest->dst_cache = 0;
		return NULL;
	}
	dst_hold(dst);
	return dst;
}

static inline struct rtable *
__ip_vs_get_out_rt(struct ip_vs_conn *cp, u32 rtos)
{
	struct rtable *rt;			/* Route to the other host */
	struct ip_vs_dest *dest = cp->dest;

	if (dest) {
		spin_lock(&dest->dst_lock);
		if (!(rt = (struct rtable *)
		      __ip_vs_dst_check(dest, rtos, 0))) {
			struct flowi fl = {
				.oif = 0,
				.nl_u = {
					.ip4_u = {
						.daddr = dest->addr,
						.saddr = 0,
						.tos = rtos, } },
				.proto = cp->protocol,
			};

			if (ip_route_output_key(&rt, &fl)) {
				spin_unlock(&dest->dst_lock);
				IP_VS_DBG_RL("ip_route_output error, "
					     "dest: %u.%u.%u.%u\n",
					     NIPQUAD(dest->addr));
				return NULL;
			}
			__ip_vs_dst_set(dest, rtos, dst_clone(&rt->u.dst));
			IP_VS_DBG(10, "new dst %u.%u.%u.%u, refcnt=%d, rtos=%X\n",
				  NIPQUAD(dest->addr),
				  atomic_read(&rt->u.dst.__refcnt), rtos);
		}
		spin_unlock(&dest->dst_lock);
	} else {
		struct flowi fl = {
			.oif = 0,
			.nl_u = {
				.ip4_u = {
					.daddr = dest->addr,
					.saddr = 0,
					.tos = rtos, } },
			.proto = cp->protocol,
		};

		if (ip_route_output_key(&rt, &fl)) {
			IP_VS_DBG_RL("ip_route_output error, dest: "
				     "%u.%u.%u.%u\n", NIPQUAD(cp->daddr));
			return NULL;
		}
	}

	return rt;
}


/*
 *	Release dest->dst_cache before a dest is removed
 */
void
ip_vs_dst_reset(struct ip_vs_dest *dest)
{
	struct dst_entry *old_dst;

	old_dst = dest->dst_cache;
	dest->dst_cache = NULL;
	dst_release(old_dst);
}


static inline int
ip_vs_skb_cow(struct sk_buff *skb, unsigned int headroom,
	      struct iphdr **iph_p, unsigned char **t_p)
{
	int delta = (headroom > 16 ? headroom : 16) - skb_headroom(skb);

	if (delta < 0)
		delta = 0;

	if (delta ||skb_cloned(skb)) {
		if (pskb_expand_head(skb, (delta+15)&~15, 0, GFP_ATOMIC))
			return -ENOMEM;

		/* skb data changed, update pointers */
		*iph_p = skb->nh.iph;
		*t_p = (char*) (*iph_p) + (*iph_p)->ihl * 4;
	}
	return 0;
}


#define IP_VS_XMIT(skb, rt)				\
do {							\
	skb->nfcache |= NFC_IPVS_PROPERTY;		\
	NF_HOOK(PF_INET, NF_IP_LOCAL_OUT, skb, NULL,	\
		rt->u.dst.dev, dst_output);		\
} while (0)


/*
 *      NULL transmitter (do nothing except return NF_ACCEPT)
 */
int
ip_vs_null_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		struct ip_vs_protocol *pp)
{
	return NF_ACCEPT;
}


/*
 *      Bypass transmitter
 *      Let packets bypass the destination when the destination is not
 *      available, it may be only used in transparent cache cluster.
 */
int
ip_vs_bypass_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		  struct ip_vs_protocol *pp)
{
	struct rtable *rt;			/* Route to the other host */
	struct iphdr  *iph = skb->nh.iph;
	u8     tos = iph->tos;
	int    mtu;
	struct flowi fl = {
		.oif = 0,
		.nl_u = {
			.ip4_u = {
				.daddr = iph->daddr,
				.saddr = 0,
				.tos = RT_TOS(tos), } },
		.proto = iph->protocol,
	};

	EnterFunction(10);

	if (ip_route_output_key(&rt, &fl)) {
		IP_VS_DBG_RL("ip_vs_bypass_xmit(): ip_route_output error, "
			     "dest: %u.%u.%u.%u\n", NIPQUAD(iph->daddr));
		goto tx_error_icmp;
	}

	/* MTU checking */
	mtu = dst_pmtu(&rt->u.dst);
	if ((skb->len > mtu) && (iph->frag_off&__constant_htons(IP_DF))) {
		ip_rt_put(rt);
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		IP_VS_DBG_RL("ip_vs_bypass_xmit(): frag needed\n");
		goto tx_error;
	}

	if (skb_is_nonlinear(skb) && skb->len <= mtu)
		ip_send_check(iph);

	if (unlikely(skb_headroom(skb) < rt->u.dst.dev->hard_header_len)) {
		if (skb_cow(skb, rt->u.dst.dev->hard_header_len)) {
			ip_rt_put(rt);
			IP_VS_ERR_RL("ip_vs_bypass_xmit(): no memory\n");
			goto tx_error;
		}
	}

	/* drop old route */
	dst_release(skb->dst);
	skb->dst = &rt->u.dst;

#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug = 0;
#endif /* CONFIG_NETFILTER_DEBUG */
	IP_VS_XMIT(skb, rt);

	LeaveFunction(10);
	return NF_STOLEN;

 tx_error_icmp:
	dst_link_failure(skb);
 tx_error:
	kfree_skb(skb);
	return NF_STOLEN;
}


/*
 *      NAT transmitter (only for outside-to-inside nat forwarding)
 *      Not used for related ICMP
 */
int
ip_vs_nat_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
	       struct ip_vs_protocol *pp)
{
	struct rtable *rt;		/* Route to the other host */
	struct iphdr  *iph;
	union ip_vs_tphdr h;
	int ihl;
	unsigned short size;
	int mtu;

	EnterFunction(10);

	/*
	 * If it has ip_vs_app helper, the helper may change the payload,
	 * so it needs full checksum checking and checksum calculation.
	 * If not, only the header (such as IP address and port number)
	 * will be changed, so it is fast to do incremental checksum update,
	 * and let the destination host do final checksum checking.
	 */

	if (unlikely(cp->app && !pp->slave)) {
		if (skb_is_nonlinear(skb) &&
		    skb_linearize(skb, GFP_ATOMIC) != 0)
			return NF_DROP;
	}

	iph = skb->nh.iph;
	ihl = iph->ihl << 2;
	h.raw = (char*) iph + ihl;
	size = ntohs(iph->tot_len) - ihl;

	/* do TCP/UDP checksum checking if it has application helper */
	if (unlikely(cp->app && pp->csum_check && !pp->slave)) {
		if (!pp->csum_check(skb, pp, iph, h, size))
			goto tx_error;
	}

	/*
	 *  Check if it is no clinet port connection ...
	 */
	if (unlikely(cp->flags & IP_VS_CONN_F_NO_CPORT)) {
		ip_vs_conn_fill_cport(cp, h.portp[0]);
		IP_VS_DBG(10, "filled cport=%d\n", ntohs(cp->dport));
	}

	if (!(rt = __ip_vs_get_out_rt(cp, RT_TOS(iph->tos))))
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_pmtu(&rt->u.dst);
	if ((skb->len > mtu) && (iph->frag_off&__constant_htons(IP_DF))) {
		ip_rt_put(rt);
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		IP_VS_DBG_RL_PKT(0, pp, iph, "ip_vs_nat_xmit(): frag needed for");
		goto tx_error;
	}

	/* drop old route */
	dst_release(skb->dst);
	skb->dst = &rt->u.dst;

	/* copy-on-write the packet before mangling it */
	if (ip_vs_skb_cow(skb, rt->u.dst.dev->hard_header_len, &iph, &h.raw))
		return NF_DROP;

	/* mangle the packet */
	iph->daddr = cp->daddr;
	if (pp->dnat_handler) {
		pp->dnat_handler(skb, pp, cp, iph, h, size);
		iph = skb->nh.iph;
		h.raw = (char*) iph + ihl;
	}
	ip_send_check(iph);

	IP_VS_DBG_PKT(10, pp, iph, "After DNAT");

	/* FIXME: when application helper enlarges the packet and the length
	   is larger than the MTU of outgoing device, there will be still
	   MTU problem. */

#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug = 0;
#endif /* CONFIG_NETFILTER_DEBUG */
	IP_VS_XMIT(skb, rt);

	LeaveFunction(10);
	return NF_STOLEN;

  tx_error_icmp:
	dst_link_failure(skb);
  tx_error:
	kfree_skb(skb);
	return NF_STOLEN;
}


/*
 *   IP Tunneling transmitter
 *
 *   This function encapsulates the packet in a new IP packet, its
 *   destination will be set to cp->daddr. Most code of this function
 *   is taken from ipip.c.
 *
 *   It is used in VS/TUN cluster. The load balancer selects a real
 *   server from a cluster based on a scheduling algorithm,
 *   encapsulates the request packet and forwards it to the selected
 *   server. For example, all real servers are configured with
 *   "ifconfig tunl0 <Virtual IP Address> up". When the server receives
 *   the encapsulated packet, it will decapsulate the packet, processe
 *   the request and return the response packets directly to the client
 *   without passing the load balancer. This can greatly increase the
 *   scalability of virtual server.
 *
 *   Used for ANY protocol
 */
int
ip_vs_tunnel_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		  struct ip_vs_protocol *pp)
{
	struct rtable *rt;			/* Route to the other host */
	struct net_device *tdev;		/* Device to other host */
	struct iphdr  *old_iph = skb->nh.iph;
	u8     tos = old_iph->tos;
	u16    df = old_iph->frag_off;
	struct iphdr  *iph;			/* Our new IP header */
	int    max_headroom;			/* The extra header space needed */
	int    mtu;

	EnterFunction(10);

	if (skb->protocol != __constant_htons(ETH_P_IP)) {
		IP_VS_DBG_RL("ip_vs_tunnel_xmit(): protocol error, "
			     "ETH_P_IP: %d, skb protocol: %d\n",
			     __constant_htons(ETH_P_IP), skb->protocol);
		goto tx_error;
	}

	if (!(rt = __ip_vs_get_out_rt(cp, RT_TOS(tos))))
		goto tx_error_icmp;

	tdev = rt->u.dst.dev;

	mtu = dst_pmtu(&rt->u.dst) - sizeof(struct iphdr);
	if (mtu < 68) {
		ip_rt_put(rt);
		IP_VS_DBG_RL("ip_vs_tunnel_xmit(): mtu less than 68\n");
		goto tx_error;
	}
	if (skb->dst)
		skb->dst->ops->update_pmtu(skb->dst, mtu);

	df |= (old_iph->frag_off&__constant_htons(IP_DF));

	if ((old_iph->frag_off&__constant_htons(IP_DF))
	    && mtu < ntohs(old_iph->tot_len)) {
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		ip_rt_put(rt);
		IP_VS_DBG_RL("ip_vs_tunnel_xmit(): frag needed\n");
		goto tx_error;
	}

	if (skb_is_nonlinear(skb))
		ip_send_check(old_iph);

	skb->h.raw = skb->nh.raw;

	/*
	 * Okay, now see if we can stuff it in the buffer as-is.
	 */
	max_headroom = (((tdev->hard_header_len+15)&~15)+sizeof(struct iphdr));

	if (skb_headroom(skb) < max_headroom
	    || skb_cloned(skb) || skb_shared(skb)) {
		struct sk_buff *new_skb =
			skb_realloc_headroom(skb, max_headroom);
		if (!new_skb) {
			ip_rt_put(rt);
			kfree_skb(skb);
			IP_VS_ERR_RL("ip_vs_tunnel_xmit(): no memory\n");
			return -EINVAL;
		}
		kfree_skb(skb);
		skb = new_skb;
	}

	skb->nh.raw = skb_push(skb, sizeof(struct iphdr));
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));

	/* drop old route */
	dst_release(skb->dst);
	skb->dst = &rt->u.dst;

	/*
	 *	Push down and install the IPIP header.
	 */
	iph			=	skb->nh.iph;
	iph->version		=	4;
	iph->ihl		=	sizeof(struct iphdr)>>2;
	iph->frag_off		=	df;
	iph->protocol		=	IPPROTO_IPIP;
	iph->tos		=	tos;
	iph->daddr		=	rt->rt_dst;
	iph->saddr		=	rt->rt_src;
	iph->ttl		=	old_iph->ttl;
	iph->tot_len		=	htons(skb->len);
	ip_select_ident(iph, &rt->u.dst, NULL);
	ip_send_check(iph);

	skb->ip_summed = CHECKSUM_NONE;
#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug = 0;
#endif /* CONFIG_NETFILTER_DEBUG */
	IP_VS_XMIT(skb, rt);

	LeaveFunction(10);

	return NF_STOLEN;

  tx_error_icmp:
	dst_link_failure(skb);
  tx_error:
	kfree_skb(skb);
	return NF_STOLEN;
}


/*
 *      Direct Routing transmitter
 *      Used for ANY protocol
 */
int
ip_vs_dr_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
	      struct ip_vs_protocol *pp)
{
	struct rtable *rt;			/* Route to the other host */
	struct iphdr  *iph = skb->nh.iph;
	int    mtu;

	EnterFunction(10);

	if (!(rt = __ip_vs_get_out_rt(cp, RT_TOS(iph->tos))))
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_pmtu(&rt->u.dst);
	if ((iph->frag_off&__constant_htons(IP_DF)) && skb->len > mtu) {
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		ip_rt_put(rt);
		IP_VS_DBG_RL("ip_vs_dr_xmit(): frag needed\n");
		goto tx_error;
	}

	if (skb_is_nonlinear(skb) && skb->len <= mtu)
		ip_send_check(iph);

	if (unlikely(skb_headroom(skb) < rt->u.dst.dev->hard_header_len)) {
		if (skb_cow(skb, rt->u.dst.dev->hard_header_len)) {
			ip_rt_put(rt);
			IP_VS_ERR_RL("ip_vs_dr_xmit(): no memory\n");
			goto tx_error;
		}
	}

	/* drop old route */
	dst_release(skb->dst);
	skb->dst = &rt->u.dst;

#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug = 0;
#endif /* CONFIG_NETFILTER_DEBUG */
	IP_VS_XMIT(skb, rt);

	LeaveFunction(10);
	return NF_STOLEN;

  tx_error_icmp:
	dst_link_failure(skb);
  tx_error:
	kfree_skb(skb);
	return NF_STOLEN;
}


/*
 *	ICMP packet transmitter
 *	called by the ip_vs_in_icmp
 */
int
ip_vs_icmp_xmit(struct sk_buff *skb, struct ip_vs_conn *cp,
		struct ip_vs_protocol *pp)
{
	struct rtable	*rt;	/* Route to the other host */
	struct iphdr	*iph;
	struct icmphdr	*icmph;
	struct iphdr	*ciph;	/* The ip header contained within the ICMP */
	unsigned short	len;
	union ip_vs_tphdr h;
	int mtu;
	int rc;

	EnterFunction(10);

	/* The ICMP packet for VS/TUN, VS/DR and LOCALNODE will be
	   forwarded directly here, because there is no need to
	   translate address/port back */
	if (IP_VS_FWD_METHOD(cp) != IP_VS_CONN_F_MASQ) {
		if (cp->packet_xmit)
			rc = cp->packet_xmit(skb, cp, pp);
		else
			rc = NF_ACCEPT;
		atomic_inc(&cp->in_pkts);
		__ip_vs_conn_put(cp);
		goto out;
	}

	iph = skb->nh.iph;
	icmph = (struct icmphdr *)((char *)iph+(iph->ihl<<2));
	len = ntohs(iph->tot_len) - (iph->ihl<<2);

	/*
	 * mangle and send the packet here (only for VS/NAT)
	 */

	if (!(rt = __ip_vs_get_out_rt(cp, RT_TOS(iph->tos))))
		goto tx_error_icmp;

	/* MTU checking */
	mtu = dst_pmtu(&rt->u.dst);
	if ((skb->len > mtu) && (iph->frag_off&__constant_htons(IP_DF))) {
		ip_rt_put(rt);
		icmp_send(skb, ICMP_DEST_UNREACH,ICMP_FRAG_NEEDED, htonl(mtu));
		IP_VS_DBG_RL("ip_vs_in_icmp(): frag needed\n");
		goto tx_error;
	}

	/* drop old route */
	dst_release(skb->dst);
	skb->dst = &rt->u.dst;

	/* copy-on-write the packet before mangling it */
	if (ip_vs_skb_cow(skb, rt->u.dst.dev->hard_header_len,
			  &iph, (unsigned char**)&icmph)) {
		rc = NF_DROP;
		goto out;
	}
	ciph = (struct iphdr *) (icmph + 1);
	h.raw = (char *) ciph + (ciph->ihl << 2);

	/* The ICMP packet for VS/NAT must be written to correct addresses
	   before being forwarded to the right server */

	/* First change the dest IP address, and recalc checksum */
	iph->daddr = cp->daddr;
	ip_send_check(iph);

	/* Now change the *source* address in the contained IP */
	ciph->saddr = cp->daddr;
	ip_send_check(ciph);

	/* the TCP/UDP source port - cannot redo check */
	if (IPPROTO_TCP == ciph->protocol || IPPROTO_UDP == ciph->protocol)
		h.portp[0] = cp->dport;

	/* And finally the ICMP checksum */
	icmph->checksum = 0;
	icmph->checksum = ip_compute_csum((unsigned char *) icmph, len);
	skb->ip_summed = CHECKSUM_UNNECESSARY;

	IP_VS_DBG_PKT(11, pp, ciph, "Forwarding incoming ICMP");

#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug = 0;
#endif /* CONFIG_NETFILTER_DEBUG */
	IP_VS_XMIT(skb, rt);

	rc = NF_STOLEN;
	goto out;

  tx_error_icmp:
	dst_link_failure(skb);
  tx_error:
	dev_kfree_skb(skb);
	rc = NF_STOLEN;
  out:
	LeaveFunction(10);
	return rc;
}
