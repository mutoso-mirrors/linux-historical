/*
 * NET3:	Handling for token ring frames that are not IP. IP is hooked
 *		early in the token ring support code.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <net/datalink.h>
#include <linux/mm.h>
#include <linux/in.h>
#include <linux/init.h>
#include <net/p8022tr.h>

#define SNAP_HEADER_LEN	8

static struct datalink_proto *p8022tr_list = NULL;

/*
 *	We don't handle the loopback SAP stuff, the extended
 *	802.2 command set, multicast SAP identifiers and non UI
 *	frames. We have the absolute minimum needed for IPX,
 *	IP and Appletalk phase 2. See the llc_* routines for support
 *	to handle the fun stuff.
 *
 *	We assume the list will be very short (at the moment its normally
 *	one or two entries).
 */

static struct datalink_proto *find_8022tr_client(unsigned char type)
{
	struct datalink_proto	*proto;

	for (proto = p8022tr_list;
		((proto != NULL) && (*(proto->type) != type));
		proto = proto->next)
		;

	return proto;
}

int p8022tr_rcv(struct sk_buff *skb, struct device *dev, struct packet_type *pt)
{
	struct datalink_proto	*proto;

	proto = find_8022tr_client(*(skb->h.raw));
	if (proto != NULL) {
		skb->h.raw += 3;
		skb_pull(skb,3);
		return proto->rcvfunc(skb, dev, pt);
	}

	skb->sk = NULL;
	kfree_skb(skb, FREE_READ);
	return 0;
}

static void p8022tr_datalink_header(struct datalink_proto *dl,
		struct sk_buff *skb, unsigned char *dest_node)
{
	struct device	*dev = skb->dev;
	unsigned char	*rawp;
	unsigned char	*olddata;
	unsigned char	*newdata;

	rawp = skb_push(skb,3);
	*rawp++ = dl->type[0];
	*rawp++ = dl->type[0];
	*rawp = 0x03;	/* UI */
	dev->hard_header(skb, dev, ETH_P_802_3, dest_node, NULL, skb->len);
	olddata = skb->data;
	newdata = skb_pull(skb, SNAP_HEADER_LEN);
	memmove(newdata, olddata, dev->hard_header_len - SNAP_HEADER_LEN);
}

static struct packet_type p8022tr_packet_type =
{
	0,
	NULL,		/* All devices */
	p8022tr_rcv,
	NULL,
	NULL,
};


EXPORT_SYMBOL(register_8022tr_client);
EXPORT_SYMBOL(unregister_8022tr_client);

__initfunc(void p8022tr_proto_init(struct net_proto *pro))
{
	p8022tr_packet_type.type=htons(ETH_P_TR_802_2);
	dev_add_pack(&p8022tr_packet_type);
}

struct datalink_proto *register_8022tr_client(unsigned char type, 
	int (*rcvfunc)(struct sk_buff *, struct device *, struct packet_type *))
{
	struct datalink_proto	*proto;

	if (find_8022tr_client(type) != NULL)
		return NULL;

	proto = (struct datalink_proto *) kmalloc(sizeof(*proto), GFP_ATOMIC);
	if (proto != NULL) {
		proto->type[0] = type;
		proto->type_len = 1;
		proto->rcvfunc = rcvfunc;
		proto->header_length = 3;
		proto->datalink_header = p8022tr_datalink_header;
		proto->string_name = "802.2TR";
		proto->next = p8022tr_list;
		p8022tr_list = proto;
	}

	return proto;
}

void unregister_8022tr_client(unsigned char type)
{
	struct datalink_proto *tmp, **clients = &p8022tr_list;
	unsigned long flags;

	save_flags(flags);
	cli();

	while ((tmp = *clients) != NULL)
	{
		if (tmp->type[0] == type) {
			*clients = tmp->next;
			kfree_s(tmp, sizeof(struct datalink_proto));
			break;
		} else {
			clients = &tmp->next;
		}
	}

	restore_flags(flags);
}
