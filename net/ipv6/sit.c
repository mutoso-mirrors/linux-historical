/*
 *	IPv6 over IPv4 tunnel device - Simple Internet Transition (SIT)
 *	Linux INET6 implementation
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	$Id: sit.c,v 1.13 1997/03/18 18:24:50 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/sched.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/icmp.h>

#include <net/sock.h>
#include <net/snmp.h>

#include <net/ipv6.h>
#include <net/protocol.h>
#include <net/transp_v6.h>
#include <net/ndisc.h>
#include <net/addrconf.h>
#include <net/ip.h>
#include <net/udp.h>
#include <net/sit.h>


static int sit_init_dev(struct device *dev);

static struct device sit_device = {
	"sit0",
	0, 0, 0, 0,
	0x0, 0,
	0, 0, 0, NULL, sit_init_dev
};

static unsigned long		sit_gc_last_run;
static void			sit_mtu_cache_gc(void);

static int			sit_xmit(struct sk_buff *skb, 
					 struct device *dev);
static int			sit_rcv(struct sk_buff *skb, unsigned short len);
static void 			sit_err(struct sk_buff *skb, unsigned char *dp);

static int			sit_open(struct device *dev);
static int			sit_close(struct device *dev);

static struct net_device_stats *sit_get_stats(struct device *dev);

extern void	udp_err(struct sk_buff *, unsigned char *);

static struct inet_protocol sit_protocol = {
	sit_rcv,
	sit_err,
	0,
	IPPROTO_IPV6,
	0,
	NULL,
	"IPv6"
};

#define SIT_NUM_BUCKETS	16

struct sit_mtu_info *sit_mtu_cache[SIT_NUM_BUCKETS];

static int		vif_num = 0;
static struct sit_vif	*vif_list = NULL;

static __inline__ __u32 sit_addr_hash(__u32 addr)
{
        
        __u32 hash_val;
        
        hash_val = addr;

        hash_val ^= hash_val >> 16;
	hash_val ^= hash_val >> 8;
        
        return (hash_val & (SIT_NUM_BUCKETS - 1));
}

static void sit_cache_insert(__u32 addr, int mtu)
{
	struct sit_mtu_info *minfo;
	int hash;
	
	minfo = kmalloc(sizeof(struct sit_mtu_info), GFP_ATOMIC);
	
	if (minfo == NULL)
		return;

	minfo->addr = addr;
	minfo->tstamp = jiffies;
	minfo->mtu = mtu;

	hash = sit_addr_hash(addr);

	minfo->next = sit_mtu_cache[hash];
	sit_mtu_cache[hash] = minfo;
}

static struct sit_mtu_info * sit_mtu_lookup(__u32 addr)
{
	struct sit_mtu_info *iter;
	int hash;

	hash = sit_addr_hash(addr);

	for(iter = sit_mtu_cache[hash]; iter; iter=iter->next) {
		if (iter->addr == addr) {
			iter->tstamp = jiffies;
			break;
		}
	}

	/*
	 *	run garbage collector
	 */

	if (jiffies - sit_gc_last_run > SIT_GC_FREQUENCY) {
		sit_mtu_cache_gc();
		sit_gc_last_run = jiffies;
	}

	return iter;
}

static void sit_mtu_cache_gc(void)
{
	struct sit_mtu_info *iter, *back;
	unsigned long now = jiffies;
	int i;

	for (i=0; i < SIT_NUM_BUCKETS; i++) {
		back = NULL;
		for (iter = sit_mtu_cache[i]; iter;) {
			if (now - iter->tstamp > SIT_GC_TIMEOUT) {
				struct sit_mtu_info *old;

				old = iter;
				iter = iter->next;

				if (back)
					back->next = iter;
				else
					sit_mtu_cache[i] = iter;

				kfree(old);
				continue;
			}
			back = iter;
			iter = iter->next;
		}
	}
}

static int sit_init_dev(struct device *dev)
{
	int i;

	dev->open	= sit_open;
	dev->stop	= sit_close;

	dev->hard_start_xmit	= sit_xmit;
	dev->get_stats		= sit_get_stats;

	dev->priv = kmalloc(sizeof(struct net_device_stats), GFP_KERNEL);

	if (dev->priv == NULL)
		return -ENOMEM;

	memset(dev->priv, 0, sizeof(struct net_device_stats));


	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);

	dev->hard_header	= NULL;
	dev->rebuild_header 	= NULL;
	dev->set_mac_address 	= NULL;
	dev->hard_header_cache 	= NULL;
	dev->header_cache_update= NULL;

	dev->type		= ARPHRD_SIT;

	dev->hard_header_len 	= MAX_HEADER;
	dev->mtu		= 1500 - sizeof(struct iphdr);
	dev->addr_len		= 0;
	dev->tx_queue_len	= 0;

	memset(dev->broadcast, 0, MAX_ADDR_LEN);
	memset(dev->dev_addr,  0, MAX_ADDR_LEN);

	dev->flags		= IFF_NOARP;	

	dev->family		= AF_INET6;
	dev->pa_addr		= 0;
	dev->pa_brdaddr 	= 0;
	dev->pa_dstaddr		= 0;
	dev->pa_mask		= 0;
	dev->pa_alen		= 4;

	return 0;
}

static int sit_init_vif(struct device *dev)
{
	int i;

	dev->flags = IFF_NOARP|IFF_POINTOPOINT|IFF_MULTICAST;
	dev->priv = kmalloc(sizeof(struct net_device_stats), GFP_KERNEL);

	if (dev->priv == NULL)
		return -ENOMEM;

	memset(dev->priv, 0, sizeof(struct net_device_stats));

	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);
		
	return 0;
}

static int sit_open(struct device *dev)
{
	return 0;
}

static int sit_close(struct device *dev)
{
	return 0;
}

int sit_init(void)
{
	int i;

	/* register device */

	if (register_netdev(&sit_device) != 0)
		return -EIO;

	inet_add_protocol(&sit_protocol);

	for (i=0; i < SIT_NUM_BUCKETS; i++)
		sit_mtu_cache[i] = NULL;

	sit_gc_last_run = jiffies;

	return 0;
}

struct device *sit_add_tunnel(__u32 dstaddr)
{
	struct sit_vif *vif;
	struct device *dev;

	if ((sit_device.flags & IFF_UP) == 0)
		return NULL;
	
	vif = kmalloc(sizeof(struct sit_vif), GFP_KERNEL);
	if (vif == NULL)
		return NULL;
	
	/*
	 *	Create PtoP configured tunnel
	 */
	
	dev = kmalloc(sizeof(struct device), GFP_KERNEL);
	if (dev == NULL)
		return NULL;

	memcpy(dev, &sit_device, sizeof(struct device));
	dev->init = sit_init_vif;
	dev->pa_dstaddr = dstaddr;

	dev->name = vif->name;
	sprintf(vif->name, "sit%d", ++vif_num);

	register_netdev(dev);

	vif->dev = dev;
	vif->next = vif_list;
	vif_list = vif;

	return dev;
}

void sit_cleanup(void)
{
	struct sit_vif *vif;

	for (vif = vif_list; vif;) {
		struct device *dev = vif->dev;
		struct sit_vif *cur;

		unregister_netdev(dev);
		kfree(dev->priv);
		kfree(dev);
		
		cur = vif;
		vif = vif->next;
	}

	vif_list = NULL;

	unregister_netdev(&sit_device);
	inet_del_protocol(&sit_protocol);
	
}

/*
 *	receive IPv4 ICMP messages
 */

static void sit_err(struct sk_buff *skb, unsigned char *dp)
{
	struct iphdr *iph = (struct iphdr*)dp;
	int type = skb->h.icmph->type;
	int code = skb->h.icmph->code;

	if (type == ICMP_DEST_UNREACH && code == ICMP_FRAG_NEEDED) {
		struct sit_mtu_info *minfo;
		unsigned short info = skb->h.icmph->un.frag.mtu - sizeof(struct iphdr);

		minfo = sit_mtu_lookup(iph->daddr);

		printk(KERN_DEBUG "sit: %08lx pmtu = %ul\n", ntohl(iph->saddr),
		       info);

		if (minfo == NULL) {
			minfo = kmalloc(sizeof(struct sit_mtu_info),
					GFP_ATOMIC);

			if (minfo == NULL)
				return;

			start_bh_atomic();
			sit_cache_insert(iph->daddr, info);
			end_bh_atomic();
		} else {
			minfo->mtu = info;
		}
	}
}

static int sit_rcv(struct sk_buff *skb, unsigned short len)
{
	struct net_device_stats *stats;
	struct device *dev = NULL;
	struct sit_vif *vif;	
	__u32  saddr = skb->nh.iph->saddr;
       
	skb->h.raw = skb->nh.raw = skb_pull(skb, skb->h.raw - skb->data);

	skb->protocol = __constant_htons(ETH_P_IPV6);

	for (vif = vif_list; vif; vif = vif->next) {
		if (saddr == vif->dev->pa_dstaddr) {
			dev = vif->dev;
			break;
		}
	}

	if (dev == NULL)
		dev = &sit_device;

	skb->dev = dev;
	skb->ip_summed = CHECKSUM_NONE;

	stats = (struct net_device_stats *)dev->priv;
	stats->rx_bytes += len;
	stats->rx_packets++;

	ipv6_rcv(skb, dev, NULL);
	return 0;
}

static int sit_xmit(struct sk_buff *skb, struct device *dev)
{
	struct net_device_stats *stats;
	struct sit_mtu_info *minfo;
	struct in6_addr *addr6;	
	struct rtable *rt;
	struct iphdr *iph;
	__u32 saddr;
	__u32 daddr;
	int addr_type;
	int mtu;
	int headroom;

	/* 
	 *	Make sure we are not busy (check lock variable) 
	 */

	stats = (struct net_device_stats *)dev->priv;

	daddr = dev->pa_dstaddr;
	if (daddr == 0) {
		struct nd_neigh *neigh = NULL;

		if (skb->dst)
			neigh = (struct nd_neigh *) skb->dst->neighbour;

		if (neigh == NULL) {
			printk(KERN_DEBUG "sit: nexthop == NULL\n");
			goto on_error;
		}
		
		addr6 = &neigh->ndn_addr;
		addr_type = ipv6_addr_type(addr6);

		if (addr_type == IPV6_ADDR_ANY) {
			addr6 = &skb->nh.ipv6h->daddr;
			addr_type = ipv6_addr_type(addr6);
		}

		if ((addr_type & IPV6_ADDR_COMPATv4) == 0) {
			printk(KERN_DEBUG "sit_xmit: non v4 address\n");
			goto on_error;
		}
		daddr = addr6->s6_addr32[3];
	}

	if (ip_route_output(&rt, daddr, 0, 0, NULL)) {
		printk(KERN_DEBUG "sit: no route to host\n");
		goto on_error;
	}

	minfo = sit_mtu_lookup(daddr);

	/* IP should calculate pmtu correctly,
	 * let's check it...
	 */
#if 0
	if (minfo)
		mtu = minfo->mtu;
	else
#endif
		mtu = rt->u.dst.pmtu;

	if (mtu > 576 && skb->tail - (skb->data + sizeof(struct ipv6hdr)) > mtu) {
		icmpv6_send(skb, ICMPV6_PKT_TOOBIG, 0, mtu, dev);
		ip_rt_put(rt);
		goto on_error;
	}

	headroom = ((rt->u.dst.dev->hard_header_len+15)&~15)+sizeof(struct iphdr);

	if (skb_headroom(skb) < headroom || skb_shared(skb)) {
		struct sk_buff *new_skb = skb_realloc_headroom(skb, headroom);
		if (!new_skb) {
			ip_rt_put(rt);
			goto on_error;
		}
		dev_kfree_skb(skb, FREE_WRITE);
		skb = new_skb;
	}
		
	memset(&(IPCB(skb)->opt), 0, sizeof(IPCB(skb)->opt));

	iph = (struct iphdr *) skb_push(skb, sizeof(struct iphdr));
	skb->nh.iph   = iph;

	saddr = rt->rt_src;
	dst_release(skb->dst);
	skb->dst = &rt->u.dst;

	iph->version  = 4;
	iph->ihl      = 5;
	iph->tos      = 0;				/* tos set to 0... */

	if (mtu > 576)
		iph->frag_off = htons(IP_DF);
	else
		iph->frag_off = 0;

	iph->ttl      = 64;
	iph->saddr    = saddr;
	iph->daddr    = daddr;
	iph->protocol = IPPROTO_IPV6;
	iph->tot_len  =	htons(skb->len);
	iph->id	      =	htons(ip_id_count++);
	ip_send_check(iph);

	ip_send(skb);

	stats->tx_bytes += skb->len;
	stats->tx_packets++;

	return 0;

on_error:
	dev_kfree_skb(skb, FREE_WRITE);
	stats->tx_errors++;
	return 0;	
}

static struct net_device_stats *sit_get_stats(struct device *dev)
{
	return((struct net_device_stats *) dev->priv);
}
