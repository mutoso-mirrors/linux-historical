/*
 *	Rose release 001
 *
 *	This is ALPHA test software. This code may break your machine, randomly fail to work with new 
 *	releases, misbehave and/or generally screw up. It might even work. 
 *
 *	This code REQUIRES 2.1.0 or higher/ NET3.029
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	History
 *	Rose 001	Jonathan(G4KLX)	Cloned from nr_dev.c.
 */

#include <linux/config.h>
#if defined(CONFIG_ROSE) || defined(CONFIG_ROSE_MODULE)
#include <linux/module.h>
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
#include <asm/segment.h>
#include <asm/io.h>

#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/if_arp.h>
#include <linux/skbuff.h>

#include <net/ip.h>
#include <net/arp.h>

#include <net/ax25.h>
#include <net/rose.h>

/*
 *	Only allow IP over Rose frames through if the netrom device is up.
 */

int rose_rx_ip(struct sk_buff *skb, struct device *dev)
{
	struct enet_statistics *stats = (struct enet_statistics *)dev->priv;

	if (!dev->start) {
		stats->rx_errors++;
		return 0;
	}

	stats->rx_packets++;
	skb->protocol = htons(ETH_P_IP);

	/* Spoof incoming device */
	skb->dev = dev;

	skb->h.raw = skb->data;
	ip_rcv(skb, skb->dev, NULL);

	return 1;
}

static int rose_header(struct sk_buff *skb, struct device *dev, unsigned short type,
	void *daddr, void *saddr, unsigned len)
{
	unsigned char *buff = skb_push(skb, ROSE_MIN_LEN + 2);

	*buff++ = GFI | Q_BIT;
	*buff++ = 0x00;
	*buff++ = ROSE_DATA;
	*buff++ = 0x7F;
	*buff++ = AX25_P_IP;

	if (daddr != NULL)
		return 37;
	
	return -37;	
}

static int rose_rebuild_header(void *buff, struct device *dev,
	unsigned long raddr, struct sk_buff *skb)
{
	struct enet_statistics *stats = (struct enet_statistics *)dev->priv;
	unsigned char *bp = (unsigned char *)buff;
	struct sk_buff *skbn;

	if (!arp_query(bp + 7, raddr, dev)) {
		dev_kfree_skb(skb, FREE_WRITE);
		return 1;
	}

	if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
		dev_kfree_skb(skb, FREE_WRITE);
		return 1;
	}

	skbn->sk = skb->sk;
	
	if (skbn->sk != NULL)
		atomic_add(skbn->truesize, &skbn->sk->wmem_alloc);

	dev_kfree_skb(skb, FREE_WRITE);

	if (!rose_route_frame(skbn, NULL)) {
		dev_kfree_skb(skbn, FREE_WRITE);
		stats->tx_errors++;
	}

	stats->tx_packets++;

	return 1;
}

static int rose_set_mac_address(struct device *dev, void *addr)
{
	struct sockaddr *sa = addr;

	ax25_listen_release((ax25_address *)dev->dev_addr, NULL);

	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);
	
	ax25_listen_register((ax25_address *)dev->dev_addr, NULL);

	return 0;
}

static int rose_open(struct device *dev)
{
	dev->tbusy = 0;
	dev->start = 1;

	MOD_INC_USE_COUNT;

	ax25_listen_register((ax25_address *)dev->dev_addr, NULL);

	return 0;
}

static int rose_close(struct device *dev)
{
	dev->tbusy = 1;
	dev->start = 0;

	ax25_listen_release((ax25_address *)dev->dev_addr, NULL);

	MOD_DEC_USE_COUNT;

	return 0;
}

static int rose_xmit(struct sk_buff *skb, struct device *dev)
{
	struct enet_statistics *stats = (struct enet_statistics *)dev->priv;

	if (skb == NULL || dev == NULL)
		return 0;

	if (!dev->start) {
		printk(KERN_ERR "rose: xmit call when iface is down\n");
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

	dev_kfree_skb(skb, FREE_WRITE);

	stats->tx_errors++;

	dev->tbusy = 0;

	mark_bh(NET_BH);

	return 0;
}

static struct enet_statistics *rose_get_stats(struct device *dev)
{
	return (struct enet_statistics *)dev->priv;
}

int rose_init(struct device *dev)
{
	int i;

	dev->mtu		= ROSE_PACLEN - 2;
	dev->tbusy		= 0;
	dev->hard_start_xmit	= rose_xmit;
	dev->open		= rose_open;
	dev->stop		= rose_close;

	dev->hard_header	= rose_header;
	dev->hard_header_len	= AX25_BPQ_HEADER_LEN + AX25_MAX_HEADER_LEN + ROSE_MIN_LEN;
	dev->addr_len		= ROSE_ADDR_LEN;
	dev->type		= ARPHRD_ROSE;
	dev->rebuild_header	= rose_rebuild_header;
	dev->set_mac_address    = rose_set_mac_address;

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

	dev->get_stats = rose_get_stats;

	/* Fill in the generic fields of the device structure. */
	for (i = 0; i < DEV_NUMBUFFS; i++)
		skb_queue_head_init(&dev->buffs[i]);

	return 0;
};

#ifdef MODULE
extern struct proto_ops rose_proto_ops;
extern struct notifier_block rose_dev_notifier;

static struct device dev_rose[] = {
	{"rose0", 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, rose_init},
	{"rose1", 0, 0, 0, 0, 0, 0, 0, 0, 0, NULL, rose_init}
};

int init_module(void)
{
	register_netdev(&dev_rose[0]);
	register_netdev(&dev_rose[1]);

	register_symtab(NULL);

	rose_proto_init(NULL);
	
	return 0;
}

void cleanup_module(void)
{
	int i;

#ifdef CONFIG_PROC_FS
	proc_net_unregister(PROC_NET_RS);
	proc_net_unregister(PROC_NET_RS_NEIGH);
	proc_net_unregister(PROC_NET_RS_NODES);
	proc_net_unregister(PROC_NET_RS_ROUTES);
#endif
	rose_rt_free();

	ax25_protocol_release(AX25_P_ROSE);
	ax25_linkfail_release(rose_link_failed);

	rose_unregister_sysctl();

	unregister_netdevice_notifier(&rose_dev_notifier);

	sock_unregister(rose_proto_ops.family);
	
	for (i = 0; i < 2; i++) {
		if (dev_rose[i].priv != NULL) {
			kfree(dev_rose[i].priv);
			dev_rose[i].priv = NULL;
			unregister_netdev(&dev_rose[i]);
		}
	}
}

#endif

#endif
