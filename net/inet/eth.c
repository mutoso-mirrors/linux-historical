/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Ethernet-type device handling.
 *
 * Version:	@(#)eth.c	1.28	20/12/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Mark Evans, <evansmp@uhura.aston.ac.uk>
 * 
 * Fixes:
 *		Mr Linux	: Arp problems.
 *		Alan Cox	: Generic queue tidyup (very tiny here).
 *		Alan Cox	: eth_header ntohs should be htons.
 *		Alan Cox	: eth_rebuild_header missing an htons and
 *				  minor other things.
 *		Tegge		: Arp bug fixes.
 *		Alan Cox	: Tidy up ready for the big day.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/in.h>
#include "inet.h"
#include "devinet.h"
#include "eth.h"
#include "ip.h"
#include "route.h"
#include "protocol.h"
#include "tcp.h"
#include "skbuff.h"
#include "sockinet.h"
#include <linux/errno.h>
#include "arp.h"


#ifdef ETH_DEBUG
/* Display an Ethernet address in readable format. */
char *eth_print(unsigned char *ptr)
{
  static char buff[64];

  if (ptr == NULL) return("[NONE]");
  sprintf(buff, "%02X:%02X:%02X:%02X:%02X:%02X",
	(ptr[0] & 255), (ptr[1] & 255), (ptr[2] & 255),
	(ptr[3] & 255), (ptr[4] & 255), (ptr[5] & 255)
  );
  return(buff);
}
#endif


#ifdef ETH_DEBUG

/*
 *	Display the contents of the Ethernet MAC header. 
 */

void eth_dump(struct ethhdr *eth)
{
  if (inet_debug != DBG_ETH) return;

  printk("eth: SRC = %s ", eth_print(eth->h_source));
  printk("DST = %s ", eth_print(eth->h_dest));
  printk("TYPE = %04X\n", ntohs(eth->h_proto));
}

#endif


/*
 * 	Create the Ethernet MAC header. 
 *
 *	ARP might prevent this from working all in one go. See also
 *	the rebuild header function.
 */
 
int eth_header(unsigned char *buff, struct device *dev, unsigned short type,
	   unsigned long daddr, unsigned long saddr, unsigned len)
{
  	struct ethhdr *eth;

  	DPRINTF((DBG_DEV, "ETH: header(%s, ", in_ntoa(saddr)));
  	DPRINTF((DBG_DEV, "%s, 0x%X)\n", in_ntoa(daddr), type));

  	/* Fill in the basic Ethernet MAC header. */
 	eth = (struct ethhdr *) buff;
  	eth->h_proto = htons(type);

  	/* We don't ARP for the LOOPBACK device... */
  	if (dev->flags & IFF_LOOPBACK) 
  	{
		DPRINTF((DBG_DEV, "ETH: No header for loopback\n"));
		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
		memset(eth->h_dest, 0, dev->addr_len);
		return(dev->hard_header_len);
  	}

  	/* Check if we can use the MAC BROADCAST address. */
  	if (chk_addr(daddr) == IS_BROADCAST) 
  	{
		DPRINTF((DBG_DEV, "ETH: Using MAC Broadcast\n"));
		memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
		memcpy(eth->h_dest, dev->broadcast, dev->addr_len);
		return(dev->hard_header_len);
  	}
  	/*
  	 *	We disable interrupts here to avoid a race if the ARP
  	 *	reply is too quick.
  	 */
 	cli();
  	memcpy(eth->h_source, &saddr, 4);
  	/* No. Ask ARP to resolve the Ethernet address. */
  	if (arp_find(eth->h_dest, daddr, dev, dev->pa_addr/* saddr */)) 
  	{
       		sti();
        	if(type!=ETH_P_IP)
        		printk("Erk: protocol %X got into an arp request state!\n",type);
		return(-dev->hard_header_len);
  	} 
  	else
  	{
  		memcpy(eth->h_source,dev->dev_addr,dev->addr_len);	/* This was missing causing chaos if the
  									   header built correctly! */
  		sti();
  		return(dev->hard_header_len);
  	}
}


/*
 *	Rebuild the Ethernet MAC header.
 *
 *	We've got a 'stuck' packet that failed to go out before. See if
 *	the arp is resolved and we can finally shift it.
 */
 
int eth_rebuild_header(void *buff, struct device *dev)
{
  	struct ethhdr *eth;
  	unsigned long src, dst;

  	DPRINTF((DBG_DEV, "ETH: Using MAC Broadcast\n"));
  	eth = (struct ethhdr *) buff;
  	src = *(unsigned long *) eth->h_source;
  	dst = *(unsigned long *) eth->h_dest;
  	DPRINTF((DBG_DEV, "ETH: RebuildHeader: SRC=%s ", in_ntoa(src)));
  	DPRINTF((DBG_DEV, "DST=%s\n", in_ntoa(dst)));
  	if(eth->h_proto!=htons(ETH_P_ARP))	/* This ntohs kind of helps a bit! */
		if (arp_find(eth->h_dest, dst, dev, dev->pa_addr /* src */)) 
			/* Still not known */
			return(1);
  	memcpy(eth->h_source, dev->dev_addr, dev->addr_len);
	return(0);
}


/* 
 *	Add an ARP entry for a host on this interface.
 */
 
void eth_add_arp(unsigned long addr, struct sk_buff *skb, struct device *dev)
{
  	struct ethhdr *eth;

  	eth = (struct ethhdr *) (skb + 1);
  	arp_add(addr, eth->h_source, dev);
}


/*
 * 	Determine the packet's protocol ID. 
 *
 * Ethernet comes in two 'species' DIX (Digitial Intel Xerox) and IEE802.3
 * needless to say they are different. Fortunately there is a way of telling
 * them apart. All 'normal' modern DIX service ID's are >1536.
 * All IEE802.3 frames have a length at this position and that cannot be 
 * >=1536. Note IEE802.3 frames have a second 802.2 header normally. We don't
 * deal with this bit in the current kernel, but a user using SOCK_PACKET
 * for 802.3 frames can do so.
 */
 
unsigned short eth_type_trans(struct sk_buff *skb, struct device *dev)
{
  	struct ethhdr *eth;

  	eth = (struct ethhdr *) (skb + 1);

  	if(ntohs(eth->h_proto)<1536)
  		return(htons(ETH_P_802_3));
  	return(eth->h_proto);
}
