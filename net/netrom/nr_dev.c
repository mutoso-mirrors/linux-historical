/*
 *	NET/ROM release 006
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
 *	NET/ROM 001	Jonathan(G4KLX)	Cloned from loopback.c
 *	NET/ROM 002	Steve Whitehouse(GW7RRM) fixed the set_mac_address
 *	NET/ROM 003	Jonathan(G4KLX)	Put nr_rebuild_header into line with
 *					ax25_rebuild_header
 *	NET/ROM 004	Jonathan(G4KLX)	Callsign registration with AX.25.
 *	NET/ROM 006	Hans(PE1AYX)	Fixed interface to IP layer.
 */

#include <linux/config.h>
#if defined(CONFIG_NETROM) || defined(CONFIG_NETROM_MODULE)
#include <linux/proc_fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/sysctl.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/in.h>
#include <linux/if_ether.h>	/* For the statistics structure. */

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>

#include <net/ip.h>
#include <net/arp.h>

#include <net/ax25.h>
#include <net/netrom.h>

/*
 *	Only allow IP over NET/ROM frames through if the netrom device is up.
 */

int nr_rx_ip(struct sk_buff *skb, struct device *dev)
{
	struct enet_statistics *stats = (struct enet_statistics *)dev->priv;

	if (!dev->start) {
		stats->rx_errors++;
		return 0;
	}

	stats->rx_packets++;
	skb->protocol = htons(ETH_P_IP);

	/* Spoof incoming device */
	skb->dev      = dev;
	skb->h.raw    = skb->data;
	skb->nh.raw   = skb->data;
	skb->pkt_type = PACKET_HOST;

	ip_rcv(skb, skb->dev, NULL);

	return 1;
}

static int nr_header(struct sk_buff *skb, struct device *dev, unsigned short type,
	void *daddr, void *saddr, unsigned len)
{
	unsigned char *buff = skb_push(skb, NR_NETWORK_LEN + NR_TRANSPORT_LEN);

	memcpy(buff, (saddr != NULL) ? saddr : dev->dev_addr, dev->addr_len);
	buff[6] &= ~AX25_CBIT;
	buff[6] &= ~AX25_EBIT;
	buff[6] |= AX25_SSSID_SPARE;
	buff    += AX25_ADDR_LEN;

	if (daddr != NULL)
		memcpy(buff, daddr, dev->addr_len);
	buff[6] &= ~AX25_CBIT;
	buff[6] |= AX25_EBIT;
	buff[6] |= AX25_SSSID_SPARE;
	buff    += AX25_ADDR_LEN;

	*buff++ = sysctl_netrom_network_ttl_initialiser;

	*buff++ = NR_PROTO_IP;
	*buff++ = NR_PROTO_IP;
	*buff++ = 0;
	*buff++ = 0;
	*buff++ = NR_PROTOEXT;

	if (daddr != NULL)
		return 37;

	return -37;
}

static int nr_rebuild_header(struct sk_buff *skb)
{
	struct device *dev = skb->dev;
	struct enet_statistics *stats = (struct enet_statistics *)dev->priv;
	struct sk_buff *skbn;
	unsigned char *bp = skb->data;

	if (!arp_find(bp + 7, skb)) {
		kfree_skb(skb, FREE_WRITE);
		return 1;
	}

	bp[6] &= ~AX25_CBIT;
	bp[6] &= ~AX25_EBIT;
	bp[6] |= AX25_SSSID_SPARE;
	bp    += AX25_ADDR_LEN;

	bp[6] &= ~AX25_CBIT;
	bp[6] |= AX25_EBIT;
	bp[6] |= AX25_SSSID_SPARE;

	if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
		kfree_skb(skb, FREE_WRITE);
		return 1;
	}

	if (skb->sk != NULL)
		skb_set_owner_w(skbn, skb->sk);

	kfree_skb(skb, FREE_WRITE);

	if (!nr_route_frame(skbn, NULL)) {
		kfree_skb(skbn, FREE_WRITE);
		stats->tx_errors++;
	}

	stats->tx_packets++;

	return 1;
}

static int nr_set_mac_address(struct device *dev, void *addr)
{
	struct sockaddr *sa = addr;

	ax25_listen_release((ax25_address *)dev->dev_addr, NULL);

	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);

	ax25_listen_register((ax25_address *)dev->dev_addr, NULL);

	return 0;
}

static int nr_open(struct device *dev)
{
	dev->tbusy = 0;
	dev->start = 1;

	ax25_listen_register((ax25_address *)dev->dev_addr, NULL);

	return 0;
}

static int nr_close(struct device *dev)
{
	dev->tbusy = 1;
	dev->start = 0;

	ax25_listen_release((ax25_address *)dev->dev_addr, NULL);

	return 0;
}

static int nr_xmit(struct sk_buff *skb, struct device *dev)
{
	struct enet_statistics *stats = (struct enet_statistics *)dev->priv;

	if (skb == NULL || dev == NULL)
		return 0;

	if (!dev->start) {
		printk(KERN_ERR "netrom: xmit call when iface is down\n");
		return 1;
	}

	cli();

	if (dev->tbusy != 0) {
		sti();
		stats->tx_errors++;
		return 1;
	}

	dev->tbusy = 1;

	sti();

	kfree_skb(skb, FREE_WRITE);

	stats->tx_errors++;

	dev->tbusy = 0;

	mark_bh(NET_BH);

	return 0;
}

static struct enet_statistics *nr_get_stats(struct device *dev)
{
	return (struct enet_statistics *)dev->priv;
}

int nr_init(struct device *dev)
{
	int i;

	dev->mtu		= 236;		/* MTU			*/
	dev->tbusy		= 0;
	dev->hard_start_xmit	= nr_xmit;
	dev->open		= nr_open;
	dev->stop		= nr_close;

	dev->hard_header	= nr_header;
	dev->hard_header_len	= AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + NR_NETWORK_LEN + NR_TRANSPORT_LEN;
	dev->addr_len		= AX25_ADDR_LEN;
	dev->type		= ARPHRD_NETROM;
	dev->rebuild_header	= nr_rebuild_header;
	dev->set_mac_address    = nr_set_mac_address;

	/* New-style flags. */
	dev->flags		= 0;
	dev->family		= AF_INET;

	dev->pa_addr		= 0;
	dev->pa_brdaddr		= 0;
	dev->pa_mask		= 0;
	dev->pa_alen		= sizeof(unsigned long);

	if ((dev->priv = kmalloc(sizeof(struct enet_statistics), GFP_KERNEL)) == NULL)
		return -ENOMEM;

	memset(dev->priv, 0, sizeof(struct enet_statistics));

	dev->get_stats = nr_get_stats;

	/* Fill in the generic fields of the device structure. */
	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);

	return 0;
};

#endif
