/*
 *	Spanning tree protocol; BPDU handling
 *	Linux ethernet bridge
 *
 *	Authors:
 *	Lennert Buytenhek		<buytenh@gnu.org>
 *
 *	$Id: br_stp_bpdu.c,v 1.3 2001/11/10 02:35:25 davem Exp $
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/if_ether.h>
#include <linux/if_bridge.h>
#include "br_private.h"
#include "br_private_stp.h"

#define JIFFIES_TO_TICKS(j) (((j) << 8) / HZ)
#define TICKS_TO_JIFFIES(j) (((j) * HZ) >> 8)

static void br_send_bpdu(struct net_bridge_port *p, unsigned char *data, int length)
{
	struct net_device *dev;
	struct sk_buff *skb;
	int size;

	if (!p->br->stp_enabled)
		return;

	size = length + 2*ETH_ALEN + 2;
	if (size < 60)
		size = 60;

	dev = p->dev;

	if ((skb = dev_alloc_skb(size)) == NULL) {
		printk(KERN_INFO "br: memory squeeze!\n");
		return;
	}

	skb->dev = dev;
	skb->protocol = htons(ETH_P_802_2);
	skb->mac.raw = skb_put(skb, size);
	memcpy(skb->mac.raw, bridge_ula, ETH_ALEN);
	memcpy(skb->mac.raw+ETH_ALEN, dev->dev_addr, ETH_ALEN);
	skb->mac.raw[2*ETH_ALEN] = 0;
	skb->mac.raw[2*ETH_ALEN+1] = length;
	skb->nh.raw = skb->mac.raw + 2*ETH_ALEN + 2;
	memcpy(skb->nh.raw, data, length);
	memset(skb->nh.raw + length, 0xa5, size - length - 2*ETH_ALEN - 2);

	dev_queue_xmit(skb);
}

static __inline__ void br_set_ticks(unsigned char *dest, int jiff)
{
	__u16 ticks;

	ticks = JIFFIES_TO_TICKS(jiff);
	dest[0] = (ticks >> 8) & 0xFF;
	dest[1] = ticks & 0xFF;
}

static __inline__ int br_get_ticks(unsigned char *dest)
{
	return TICKS_TO_JIFFIES((dest[0] << 8) | dest[1]);
}

/* called under bridge lock */
void br_send_config_bpdu(struct net_bridge_port *p, struct br_config_bpdu *bpdu)
{
	unsigned char buf[38];

	buf[0] = 0x42;
	buf[1] = 0x42;
	buf[2] = 0x03;
	buf[3] = 0;
	buf[4] = 0;
	buf[5] = 0;
	buf[6] = BPDU_TYPE_CONFIG;
	buf[7] = (bpdu->topology_change ? 0x01 : 0) |
		(bpdu->topology_change_ack ? 0x80 : 0);
	buf[8] = bpdu->root.prio[0];
	buf[9] = bpdu->root.prio[1];
	buf[10] = bpdu->root.addr[0];
	buf[11] = bpdu->root.addr[1];
	buf[12] = bpdu->root.addr[2];
	buf[13] = bpdu->root.addr[3];
	buf[14] = bpdu->root.addr[4];
	buf[15] = bpdu->root.addr[5];
	buf[16] = (bpdu->root_path_cost >> 24) & 0xFF;
	buf[17] = (bpdu->root_path_cost >> 16) & 0xFF;
	buf[18] = (bpdu->root_path_cost >> 8) & 0xFF;
	buf[19] = bpdu->root_path_cost & 0xFF;
	buf[20] = bpdu->bridge_id.prio[0];
	buf[21] = bpdu->bridge_id.prio[1];
	buf[22] = bpdu->bridge_id.addr[0];
	buf[23] = bpdu->bridge_id.addr[1];
	buf[24] = bpdu->bridge_id.addr[2];
	buf[25] = bpdu->bridge_id.addr[3];
	buf[26] = bpdu->bridge_id.addr[4];
	buf[27] = bpdu->bridge_id.addr[5];
	buf[28] = (bpdu->port_id >> 8) & 0xFF;
	buf[29] = bpdu->port_id & 0xFF;

	br_set_ticks(buf+30, bpdu->message_age);
	br_set_ticks(buf+32, bpdu->max_age);
	br_set_ticks(buf+34, bpdu->hello_time);
	br_set_ticks(buf+36, bpdu->forward_delay);

	br_send_bpdu(p, buf, 38);
}

/* called under bridge lock */
void br_send_tcn_bpdu(struct net_bridge_port *p)
{
	unsigned char buf[7];

	buf[0] = 0x42;
	buf[1] = 0x42;
	buf[2] = 0x03;
	buf[3] = 0;
	buf[4] = 0;
	buf[5] = 0;
	buf[6] = BPDU_TYPE_TCN;
	br_send_bpdu(p, buf, 7);
}

static unsigned char header[6] = {0x42, 0x42, 0x03, 0x00, 0x00, 0x00};

/* called under bridge lock */
void br_stp_handle_bpdu(struct sk_buff *skb)
{
	unsigned char *buf;
	struct net_bridge_port *p;

	buf = skb->mac.raw + 14;
	p = skb->dev->br_port;
	if (!p->br->stp_enabled || memcmp(buf, header, 6)) {
		kfree_skb(skb);
		return;
	}

	if (buf[6] == BPDU_TYPE_CONFIG) {
		struct br_config_bpdu bpdu;

		bpdu.topology_change = (buf[7] & 0x01) ? 1 : 0;
		bpdu.topology_change_ack = (buf[7] & 0x80) ? 1 : 0;
		bpdu.root.prio[0] = buf[8];
		bpdu.root.prio[1] = buf[9];
		bpdu.root.addr[0] = buf[10];
		bpdu.root.addr[1] = buf[11];
		bpdu.root.addr[2] = buf[12];
		bpdu.root.addr[3] = buf[13];
		bpdu.root.addr[4] = buf[14];
		bpdu.root.addr[5] = buf[15];
		bpdu.root_path_cost =
			(buf[16] << 24) |
			(buf[17] << 16) |
			(buf[18] << 8) |
			buf[19];
		bpdu.bridge_id.prio[0] = buf[20];
		bpdu.bridge_id.prio[1] = buf[21];
		bpdu.bridge_id.addr[0] = buf[22];
		bpdu.bridge_id.addr[1] = buf[23];
		bpdu.bridge_id.addr[2] = buf[24];
		bpdu.bridge_id.addr[3] = buf[25];
		bpdu.bridge_id.addr[4] = buf[26];
		bpdu.bridge_id.addr[5] = buf[27];
		bpdu.port_id = (buf[28] << 8) | buf[29];

		bpdu.message_age = br_get_ticks(buf+30);
		bpdu.max_age = br_get_ticks(buf+32);
		bpdu.hello_time = br_get_ticks(buf+34);
		bpdu.forward_delay = br_get_ticks(buf+36);

		kfree_skb(skb);
		br_received_config_bpdu(p, &bpdu);
		return;
	}

	if (buf[6] == BPDU_TYPE_TCN) {
		br_received_tcn_bpdu(p);
		kfree_skb(skb);
		return;
	}

	kfree_skb(skb);
}
