/*
 * 	NET3	Protocol independant device support routines.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Derived from the non IP parts of dev.c 1.0.19
 * 		Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *				Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *				Mark Evans, <evansmp@uhura.aston.ac.uk>
 *
 *	Additional Authors:
 *		Florian la Roche <rzsfl@rz.uni-sb.de>
 *		Alan Cox <gw4pts@gw4pts.ampr.org>
 *
 *	Cleaned up and recommented by Alan Cox 2nd April 1994. I hope to have
 *	the rest as well commented in the end.
 */

/*
 *	A lot of these includes will be going walkies very soon 
 */
 
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include "ip.h"
#include "route.h"
#include <linux/skbuff.h>
#include "sock.h"
#include "arp.h"


/*
 *	The list of packet types we will receive (as opposed to discard)
 *	and the routines to invoke.
 */

struct packet_type *ptype_base = NULL;

/*
 *	Device drivers call our routines to queue packets here. We empty the
 *	queue in the bottom half handler.
 */

static struct sk_buff_head backlog = 
{
	(struct sk_buff *)&backlog, (struct sk_buff *)&backlog
#ifdef CONFIG_SKB_CHECK
	,SK_HEAD_SKB
#endif
};

/* 
 *	We don't overdo the queue or we will thrash memory badly.
 */
 
static int backlog_size = 0;

/*
 *	The number of sockets open for 'all' protocol use. We have to
 *	know this to copy a buffer the correct number of times.
 */
 
static int dev_nit=0;

/*
 *	Return the lesser of the two values. 
 */
 
static __inline__ unsigned long min(unsigned long a, unsigned long b)
{
  return (a < b)? a : b;
}


/******************************************************************************************

		Protocol management and registration routines

*******************************************************************************************/


/*
 *	Add a protocol ID to the list.
 */
 
void dev_add_pack(struct packet_type *pt)
{
	struct packet_type *p1;
	pt->next = ptype_base;

	/* 
	 *	Don't use copy counts on ETH_P_ALL. Instead keep a global
 	 *	count of number of these and use it and pt->copy to decide
	 *	copies 
	 */
	 
	pt->copy=0;	/* Assume we will not be copying the buffer before 
			 * this routine gets it
			 */
			 
	if(pt->type == htons(ETH_P_ALL))
  		dev_nit++;	/* I'd like a /dev/nit too one day 8) */
	else
	{
  		/*
  		 *	See if we need to copy it - that is another process also
  		 *	wishes to receive this type of packet.
  		 */
		for (p1 = ptype_base; p1 != NULL; p1 = p1->next) 
		{
			if (p1->type == pt->type) 
			{
				pt->copy = 1;	/* We will need to copy */
				break;
			}
	  	}
	}
  
  /*
   *	NIT taps must go at the end or inet_bh will leak!
   */
   
	if (pt->type == htons(ETH_P_ALL))
	{
  		pt->next=NULL;
  		if(ptype_base==NULL)
		  	ptype_base=pt;
		else
		{
			/* 
			 *	Move to the end of the list
			 */
			for(p1=ptype_base;p1->next!=NULL;p1=p1->next);
			/*
			 *	Hook on the end
			 */
			p1->next=pt;
		}
	 }
	else
/*
 *	It goes on the start 
 */
		ptype_base = pt;
}


/*
 *	Remove a protocol ID from the list.
 */
 
void dev_remove_pack(struct packet_type *pt)
{
	struct packet_type *lpt, *pt1;

	/*
	 *	Keep the count of nit (Network Interface Tap) sockets correct.
	 */
	 
	if (pt->type == htons(ETH_P_ALL))
	  	dev_nit--;
	  	
	/*
	 *	If we are first, just unhook us.
	 */
	 
	if (pt == ptype_base) 
	{
		ptype_base = pt->next;
		return;
	}

	lpt = NULL;
	
	/*
	 *	This is harder. What we do is to walk the list of sockets 
	 *	for this type. We unhook the entry, and if there is a previous
	 *	entry that is copying _and_ we are not copying, (ie we are the
	 *	last entry for this type) then the previous one is set to
	 *	non-copying as it is now the last.
	 */
	for (pt1 = ptype_base; pt1->next != NULL; pt1 = pt1->next) 
	{
		if (pt1->next == pt ) 
		{
			cli();
			if (!pt->copy && lpt) 
				lpt->copy = 0;
			pt1->next = pt->next;
			sti();
			return;
		}
		if (pt1->next->type == pt->type && pt->type != htons(ETH_P_ALL))
			lpt = pt1->next;
	}
}

/*****************************************************************************************

			    Device Inteface Subroutines

******************************************************************************************/

/* 
 *	Find an interface by name.
 */
 
struct device *dev_get(char *name)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		if (strcmp(dev->name, name) == 0)
			return(dev);
	}
	return(NULL);
}


/*
 *	Prepare an interface for use. 
 */
 
int dev_open(struct device *dev)
{
	int ret = 0;

	/*
	 *	Call device private open method
	 */
	if (dev->open) 
  		ret = dev->open(dev);

	/*
	 *	If it went open OK then set the flags
	 */
	 
	if (ret == 0) 
		dev->flags |= (IFF_UP | IFF_RUNNING);
	
	return(ret);
}


/*
 *	Completely shutdown an interface.
 *
 *	WARNING: Both because of the way the upper layers work (that can be fixed)
 *	and because of races during a close (that can't be fixed any other way)
 *	a device may be given things to transmit EVEN WHEN IT IS DOWN. The driver
 *	MUST cope with this (eg by freeing and dumping the frame).
 */
 
int dev_close(struct device *dev)
{
	/*
	 *	Only close a device if it is up.
	 */
	 
	if (dev->flags != 0) 
	{
  		int ct=0;
		dev->flags = 0;
		/*
		 *	Call the device specific close. This cannot fail.
		 */
		if (dev->stop) 
			dev->stop(dev);
		/*
		 *	Delete the route to the device.
		 */
		ip_rt_flush(dev);
		/*
		 *	Blank the IP addresses
		 */
		dev->pa_addr = 0;
		dev->pa_dstaddr = 0;
		dev->pa_brdaddr = 0;
		dev->pa_mask = 0;
		/*
		 *	Purge any queued packets when we down the link 
		 */
		while(ct<DEV_NUMBUFFS)
		{
			struct sk_buff *skb;
			while((skb=skb_dequeue(&dev->buffs[ct]))!=NULL)
				if(skb->free)
					kfree_skb(skb,FREE_WRITE);
			ct++;
		}
	}
	return(0);
}


/*
 *	Send (or queue for sending) a packet. 
 */

void dev_queue_xmit(struct sk_buff *skb, struct device *dev, int pri)
{
	int where = 0;		/* used to say if the packet should go	*/
				/* at the front or the back of the	*/
				/* queue.				*/

	DPRINTF((DBG_DEV, "dev_queue_xmit(skb=%X, dev=%X, pri = %d)\n",
							skb, dev, pri));

	if (dev == NULL) 
	{
		printk("dev.c: dev_queue_xmit: dev = NULL\n");
		return;
	}
 
	IS_SKB(skb);
    
	skb->dev = dev;
	start_bh_atomic();

	/*
	 *	This just eliminates some race conditions, but not all... 
	 */

	if (skb->next != NULL) 
	{
		/*
		 *	Make sure we haven't missed an interrupt. 
		 */
		printk("dev_queue_xmit: worked around a missed interrupt\n");
		dev->hard_start_xmit(NULL, dev);
		end_bh_atomic();
		return;
  	}

	/*
	 *	Negative priority is used to flag a frame that is being pulled from the
	 *	queue front as a retransmit attempt. It therefore goes back on the queue
	 *	start on a failure.
	 */
	 
  	if (pri < 0) 
  	{
		pri = -pri-1;
		where = 1;
  	}

	if (pri >= DEV_NUMBUFFS) 
	{
		printk("bad priority in dev_queue_xmit.\n");
		pri = 1;
	}

	/*
	 *	If the address has not been resolved called the device header rebuilder.
	 *	This can cover all protocols and technically no just ARP either.
	 */
	 
	if (!skb->arp && dev->rebuild_header(skb->data, dev, skb->raddr, skb)) {
		end_bh_atomic();
		return;
	}

	/*
	 *	This is vitally important. We _MUST_ keep packets in order. While tcp/ip
	 *	suffers only a slow down some IPX apps, and all the AX.25 code will break
	 *	if it occurs out of order. 
	 *
	 *	This is commented out while I fix a few 'side effects'
	 */

	if ((where==1 || skb_peek(&dev->buffs[pri])==NULL) && dev->hard_start_xmit(skb, dev) == 0) 
	{
		end_bh_atomic();
		return;
	}

	/*
	 *	Transmission failed, put skb back into a list. 
	 */

	if(where)
		skb_queue_head(&dev->buffs[pri],skb);
	else
		skb_queue_tail(&dev->buffs[pri],skb);
	end_bh_atomic();
}

/*
 *	Receive a packet from a device driver and queue it for the upper
 *	(protocol) levels.  It always succeeds. This is the recommended 
 *	interface to use.
 */

void netif_rx(struct sk_buff *skb)
{
	static int dropping = 0;
	extern struct timeval xtime;

	/*
	 *	Any received buffers are un-owned and should be discarded
	 *	when freed. These will be updated later as the frames get
	 *	owners.
	 */
	skb->sk = NULL;
	skb->free = 1;
	if(skb->stamp.tv_sec==0)
		skb->stamp = xtime;

	/*
	 *	Check that we aren't oevrdoing things.
	 */

	if (!backlog_size)
  		dropping = 0;
	else if (backlog_size > 100)
		dropping = 1;

	if (dropping) 
	{
		kfree_skb(skb, FREE_READ);
		return;
	}

	/*
	 *	Add it to the "backlog" queue. 
	 */

	IS_SKB(skb);
	skb_queue_tail(&backlog,skb);
	backlog_size++;
  
	/*
	 *	If any packet arrived, mark it for processing after the
	 *	hardware interrupt returns.
	 */

	mark_bh(INET_BH);
	return;
}


/*
 *	The old interface to fetch a packet from a device driver.
 *	This function is the base level entry point for all drivers that
 *	want to send a packet to the upper (protocol) levels.  It takes
 *	care of de-multiplexing the packet to the various modules based
 *	on their protocol ID.
 *
 *	Return values:	1 <- exit I can't do any more
 *			0 <- feed me more (i.e. "done", "OK"). 
 *
 *	This function is OBSOLETE and should not be used by any new
 *	device.
 */

int dev_rint(unsigned char *buff, long len, int flags, struct device *dev)
{
	static int dropping = 0;
	struct sk_buff *skb = NULL;
	unsigned char *to;
	int amount, left;
	int len2;

	if (dev == NULL || buff == NULL || len <= 0) 
		return(1);

	if (flags & IN_SKBUFF) 
	{
		skb = (struct sk_buff *) buff;
	}
	else
	{
		if (dropping) 
		{
			if (skb_peek(&backlog) != NULL)
				return(1);
			printk("INET: dev_rint: no longer dropping packets.\n");
			dropping = 0;
		}

		skb = alloc_skb(len, GFP_ATOMIC);
		if (skb == NULL) 
		{
			printk("dev_rint: packet dropped on %s (no memory) !\n",
			       dev->name);
			dropping = 1;
			return(1);
		}

		/* 
		 *	First we copy the packet into a buffer, and save it for later. We
		 *	in effect handle the incoming data as if it were from a circular buffer
		 */

		to = skb->data;
		left = len;

		len2 = len;
		while (len2 > 0) 
		{
			amount = min(len2, (unsigned long) dev->rmem_end -
						(unsigned long) buff);
			memcpy(to, buff, amount);
			len2 -= amount;
			left -= amount;
			buff += amount;
			to += amount;
			if ((unsigned long) buff == dev->rmem_end)
				buff = (unsigned char *) dev->rmem_start;
		}
	}

	/*
	 *	Tag the frame and kick it to the proper receive routine
	 */
	 
	skb->len = len;
	skb->dev = dev;
	skb->free = 1;

	netif_rx(skb);
	/*
	 *	OK, all done. 
	 */
	return(0);
}


/*
 *	This routine causes all interfaces to try to send some data. 
 */
 
void dev_transmit(void)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		if (!dev->tbusy) {
			/*
			 *	Kick the device
			 */
			dev_tint(dev);
		}
	}
}


/**********************************************************************************

			Receive Queue Processor
			
***********************************************************************************/

/*
 *	This is a single non-rentrant routine which takes the received packet
 *	queue and throws it at the networking layers in the hope that something
 *	useful will emerge.
 */
 
volatile char in_bh = 0;	/* Non-rentrant remember */

int in_inet_bh()	/* Used by timer.c */
{
	return(in_bh==0?0:1);
}

/*
 *	When we are called the queue is ready to grab, the interrupts are
 *	on and hardware can interrupt and queue to the receive queue a we
 *	run with no problems.
 *	This is run as a bottom half after an interrupt handler that does
 *	mark_bh(INET_BH);
 */
 
void inet_bh(void *tmp)
{
	struct sk_buff *skb;
	struct packet_type *ptype;
	unsigned short type;
	unsigned char flag = 0;
	int nitcount;

	/*
	 *	Atomically check and mark our BUSY state. 
	 */

	if (set_bit(1, (void*)&in_bh))
		return;

	/*
	 *	Can we send anything now? We want to clear the
	 *	decks for any more sends that get done as we
	 *	process the input.
	 */

	dev_transmit();
  
	/*
	 *	Any data left to process. This may occur because a
	 *	mark_bh() is done after we empty the queue including
	 *	that from the device which does a mark_bh() just after
	 */

	cli();
	
	/*
	 *	While the queue is not empty
	 */
	 
	while((skb=skb_dequeue(&backlog))!=NULL)
	{
		/*
		 *	We have a packet. Therefore the queue has shrunk
		 */
  		backlog_size--;

	  	nitcount=dev_nit;
		flag=0;
		sti();
		
	       /*
		*	Bump the pointer to the next structure.
		*	This assumes that the basic 'skb' pointer points to
		*	the MAC header, if any (as indicated by its "length"
		*	field).  Take care now!
		*/

		skb->h.raw = skb->data + skb->dev->hard_header_len;
		skb->len -= skb->dev->hard_header_len;

	       /*
		* 	Fetch the packet protocol ID.  This is also quite ugly, as
		* 	it depends on the protocol driver (the interface itself) to
		* 	know what the type is, or where to get it from.  The Ethernet
		* 	interfaces fetch the ID from the two bytes in the Ethernet MAC
		*	header (the h_proto field in struct ethhdr), but other drivers
		*	may either use the ethernet ID's or extra ones that do not
		*	clash (eg ETH_P_AX25). We could set this before we queue the
		*	frame. In fact I may change this when I have time.
		*/
		
		type = skb->dev->type_trans(skb, skb->dev);

		/*
		 *	We got a packet ID.  Now loop over the "known protocols"
		 *	table (which is actually a linked list, but this will
		 *	change soon if I get my way- FvK), and forward the packet
		 *	to anyone who wants it.
		 *
		 *	[FvK didn't get his way but he is right this ought to be
		 *	hashed so we typically get a single hit. The speed cost
		 *	here is minimal but no doubt adds up at the 4,000+ pkts/second
		 *	rate we can hit flat out]
		 */
		 
		for (ptype = ptype_base; ptype != NULL; ptype = ptype->next) 
		{
			if (ptype->type == type || ptype->type == htons(ETH_P_ALL)) 
			{
				struct sk_buff *skb2;

				if (ptype->type == htons(ETH_P_ALL))
					nitcount--;
				if (ptype->copy || nitcount) 
				{	
					/*
					 *	copy if we need to
					 */
#ifdef OLD
					skb2 = alloc_skb(skb->len, GFP_ATOMIC);
					if (skb2 == NULL) 
						continue;
					memcpy(skb2, skb, skb2->mem_len);
					skb2->mem_addr = skb2;
					skb2->h.raw = (unsigned char *)(
					    (unsigned long) skb2 +
					    (unsigned long) skb->h.raw -
					    (unsigned long) skb
					);
					skb2->free = 1;
#else
					skb2=skb_clone(skb, GFP_ATOMIC);
					if(skb2==NULL)
						continue;
#endif				
				} 
				else 
				{
					skb2 = skb;
				}

				/*
				 *	Protocol located. 
				 */
				 
				flag = 1;

				/*
				 *	Kick the protocol handler. This should be fast
				 *	and efficient code.
				 */

				ptype->func(skb2, skb->dev, ptype);
			}
		} /* End of protocol list loop */

		/*
		 * 	Has an unknown packet has been received ?
		 */
	 
		if (!flag) 
		{
			DPRINTF((DBG_DEV,"INET: unknown packet type 0x%04X (ignored)\n", type));
			kfree_skb(skb, FREE_WRITE);
		}

		/*
		 *	Again, see if we can transmit anything now. 
		 */

		dev_transmit();
		cli();
  	}	/* End of queue loop */
  	
  	/*
  	 *	We have emptied the queue
  	 */
  	 
  	in_bh = 0;
	sti();
	
	/*
	 *	One last output flush.
	 */
	 
	dev_transmit();
}


/*
 *	This routine is called when an device driver (i.e. an
 *	interface) is ready to transmit a packet.
 */
 
void dev_tint(struct device *dev)
{
	int i;
	struct sk_buff *skb;
	
	/*
	 *	Work the queues in priority order
	 */
	 
	for(i = 0;i < DEV_NUMBUFFS; i++) 
	{
		/*
		 *	Pull packets from the queue
		 */
		 
		while((skb=skb_dequeue(&dev->buffs[i]))!=NULL)
		{
			/*
			 *	Feed them to the output stage and if it fails
			 *	indicate they re-queue at the front.
			 */
			dev_queue_xmit(skb,dev,-i - 1);
			/*
			 *	If we can take no more then stop here.
			 */
			if (dev->tbusy)
				return;
		}
	}
}


/*
 *	Perform a SIOCGIFCONF call. This structure will change
 *	size shortly, and there is nothing I can do about it.
 *	Thus we will need a 'compatibility mode'.
 */

static int dev_ifconf(char *arg)
{
	struct ifconf ifc;
	struct ifreq ifr;
	struct device *dev;
	char *pos;
	int len;
	int err;

	/*
	 *	Fetch the caller's info block. 
	 */
	 
	err=verify_area(VERIFY_WRITE, arg, sizeof(struct ifconf));
	if(err)
	  	return err;
	memcpy_fromfs(&ifc, arg, sizeof(struct ifconf));
	len = ifc.ifc_len;
	pos = ifc.ifc_buf;

	/*
	 *	We now walk the device list filling each active device
	 *	into the array.
	 */
	 
	err=verify_area(VERIFY_WRITE,pos,len);
	if(err)
	  	return err;
  	
	/*
	 *	Loop over the interfaces, and write an info block for each. 
	 */

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
        	if(!(dev->flags & IFF_UP))	/* Downed devices don't count */
	        	continue;
		memset(&ifr, 0, sizeof(struct ifreq));
		strcpy(ifr.ifr_name, dev->name);
		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_family = dev->family;
		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_addr.s_addr = dev->pa_addr;

		/*
		 *	Write this block to the caller's space. 
		 */
		 
		memcpy_tofs(pos, &ifr, sizeof(struct ifreq));
		pos += sizeof(struct ifreq);
		len -= sizeof(struct ifreq);
		
		/*
		 *	Have we run out of space here ?
		 */
	
		if (len < sizeof(struct ifreq)) 
			break;
  	}

	/*
	 *	All done.  Write the updated control block back to the caller. 
	 */
	 
	ifc.ifc_len = (pos - ifc.ifc_buf);
	ifc.ifc_req = (struct ifreq *) ifc.ifc_buf;
	memcpy_tofs(arg, &ifc, sizeof(struct ifconf));
	
	/*
	 *	Report how much was filled in
	 */
	 
	return(pos - arg);
}


/*
 *	This is invoked by the /proc filesystem handler to display a device
 *	in detail.
 */

static int sprintf_stats(char *buffer, struct device *dev)
{
	struct enet_statistics *stats = (dev->get_stats ? dev->get_stats(dev): NULL);
	int size;
	
	if (stats)
		size = sprintf(buffer, "%6s:%7d %4d %4d %4d %4d %8d %4d %4d %4d %5d %4d\n",
		   dev->name,
		   stats->rx_packets, stats->rx_errors,
		   stats->rx_dropped + stats->rx_missed_errors,
		   stats->rx_fifo_errors,
		   stats->rx_length_errors + stats->rx_over_errors
		   + stats->rx_crc_errors + stats->rx_frame_errors,
		   stats->tx_packets, stats->tx_errors, stats->tx_dropped,
		   stats->tx_fifo_errors, stats->collisions,
		   stats->tx_carrier_errors + stats->tx_aborted_errors
		   + stats->tx_window_errors + stats->tx_heartbeat_errors);
	else
		size = sprintf(buffer, "%6s: No statistics available.\n", dev->name);

	return size;
}

/*
 *	Called from the PROCfs module. This now uses the new arbitary sized /proc/net interface
 *	to create /proc/net/dev
 */
 
int dev_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len=0;
	off_t begin=0;
	off_t pos=0;
	int size;
	
	struct device *dev;


	size = sprintf(buffer, "Inter-|   Receive                  |  Transmit\n"
			    " face |packets errs drop fifo frame|packets errs drop fifo colls carrier\n");
	
	pos+=size;
	len+=size;
	

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		size = sprintf_stats(buffer+len, dev);
		len+=size;
		pos=begin+len;
				
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			break;
	}
	
	*start=buffer+(offset-begin);	/* Start of wanted data */
	len-=(offset-begin);		/* Start slop */
	if(len>length)
		len=length;		/* Ending slop */
	return len;
}


/*
 *	This checks bitmasks for the ioctl calls for devices.
 */
 
static inline int bad_mask(unsigned long mask, unsigned long addr)
{
	if (addr & (mask = ~mask))
		return 1;
	mask = ntohl(mask);
	if (mask & (mask+1))
		return 1;
	return 0;
}

/*
 *	Perform the SIOCxIFxxx calls. 
 *
 *	The socket layer has seen an ioctl the address family thinks is
 *	for the device. At this point we get invoked to make a decision
 */
 
static int dev_ifsioc(void *arg, unsigned int getset)
{
	struct ifreq ifr;
	struct device *dev;
	int ret;

	/*
	 *	Fetch the caller's info block into kernel space
	 */

	int err=verify_area(VERIFY_WRITE, arg, sizeof(struct ifreq));
	if(err)
		return err;
	
	memcpy_fromfs(&ifr, arg, sizeof(struct ifreq));

	/*
	 *	See which interface the caller is talking about. 
	 */
	 
	if ((dev = dev_get(ifr.ifr_name)) == NULL) 
		return(-ENODEV);

	switch(getset) 
	{
		case SIOCGIFFLAGS:	/* Get interface flags */
			ifr.ifr_flags = dev->flags;
			memcpy_tofs(arg, &ifr, sizeof(struct ifreq));
			ret = 0;
			break;
		case SIOCSIFFLAGS:	/* Set interface flags */
			{
				int old_flags = dev->flags;
				dev->flags = ifr.ifr_flags & (
					IFF_UP | IFF_BROADCAST | IFF_DEBUG | IFF_LOOPBACK |
					IFF_POINTOPOINT | IFF_NOTRAILERS | IFF_RUNNING |
					IFF_NOARP | IFF_PROMISC | IFF_ALLMULTI);
				
				/*
				 *	Has promiscuous mode been turned off
				 */	
				if ( (old_flags & IFF_PROMISC) && ((dev->flags & IFF_PROMISC) == 0))
			 		dev->set_multicast_list(dev,0,NULL);
			 		
			 	/*
			 	 *	Has it been turned on
			 	 */
	
				if ( (dev->flags & IFF_PROMISC) && ((old_flags & IFF_PROMISC) == 0))
			  		dev->set_multicast_list(dev,-1,NULL);
			  		
			  	/*
			  	 *	Have we downed the interface
			  	 */
		
				if ((old_flags & IFF_UP) && ((dev->flags & IFF_UP) == 0)) 
				{
					ret = dev_close(dev);
				}
				else
				{
					/*
					 *	Have we upped the interface 
					 */
					 
			      		ret = (! (old_flags & IFF_UP) && (dev->flags & IFF_UP))
						? dev_open(dev) : 0;
					/* 
					 *	Check the flags.
					 */
					if(ret<0)
						dev->flags&=~IFF_UP;	/* Didnt open so down the if */
			  	}
	        	}
			break;
		
		case SIOCGIFADDR:	/* Get interface address (and family) */
			(*(struct sockaddr_in *)
				  &ifr.ifr_addr).sin_addr.s_addr = dev->pa_addr;
			(*(struct sockaddr_in *)
				  &ifr.ifr_addr).sin_family = dev->family;
			(*(struct sockaddr_in *)
				  &ifr.ifr_addr).sin_port = 0;
			memcpy_tofs(arg, &ifr, sizeof(struct ifreq));
			ret = 0;
			break;
	
		case SIOCSIFADDR:	/* Set interface address (and family) */
			dev->pa_addr = (*(struct sockaddr_in *)
				 &ifr.ifr_addr).sin_addr.s_addr;
			dev->family = ifr.ifr_addr.sa_family;
			dev->pa_mask = ip_get_mask(dev->pa_addr);
			dev->pa_brdaddr = dev->pa_addr | ~dev->pa_mask;
			ret = 0;
			break;
			
		case SIOCGIFBRDADDR:	/* Get the broadcast address */
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_addr.s_addr = dev->pa_brdaddr;
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_family = dev->family;
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_port = 0;
			memcpy_tofs(arg, &ifr, sizeof(struct ifreq));
			ret = 0;
			break;

		case SIOCSIFBRDADDR:	/* Set the broadcast address */
			dev->pa_brdaddr = (*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_addr.s_addr;
			ret = 0;
			break;
			
		case SIOCGIFDSTADDR:	/* Get the destination address (for point-to-point links) */
			(*(struct sockaddr_in *)
				&ifr.ifr_dstaddr).sin_addr.s_addr = dev->pa_dstaddr;
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_family = dev->family;
			(*(struct sockaddr_in *)
				&ifr.ifr_broadaddr).sin_port = 0;
				memcpy_tofs(arg, &ifr, sizeof(struct ifreq));
			ret = 0;
			break;
	
		case SIOCSIFDSTADDR:	/* Set the destination address (for point-to-point links) */
			dev->pa_dstaddr = (*(struct sockaddr_in *)
				&ifr.ifr_dstaddr).sin_addr.s_addr;
			ret = 0;
			break;
			
		case SIOCGIFNETMASK:	/* Get the netmask for the interface */
			(*(struct sockaddr_in *)
				&ifr.ifr_netmask).sin_addr.s_addr = dev->pa_mask;
			(*(struct sockaddr_in *)
				&ifr.ifr_netmask).sin_family = dev->family;
			(*(struct sockaddr_in *)
				&ifr.ifr_netmask).sin_port = 0;
			memcpy_tofs(arg, &ifr, sizeof(struct ifreq));
			ret = 0;
			break;

		case SIOCSIFNETMASK: 	/* Set the netmask for the interface */
			{
				unsigned long mask = (*(struct sockaddr_in *)
					&ifr.ifr_netmask).sin_addr.s_addr;
				ret = -EINVAL;
				/*
				 *	The mask we set must be legal.
				 */
				if (bad_mask(mask,0))
					break;
				dev->pa_mask = mask;
				ret = 0;
			}
			break;
			
		case SIOCGIFMETRIC:	/* Get the metric on the inteface (currently unused) */
			
			ifr.ifr_metric = dev->metric;
			memcpy_tofs(arg, &ifr, sizeof(struct ifreq));
			ret = 0;
			break;
			
		case SIOCSIFMETRIC:	/* Set the metric on the interface (currently unused) */
			dev->metric = ifr.ifr_metric;
			ret = 0;
			break;
	
		case SIOCGIFMTU:	/* Get the MTU of a device */
			ifr.ifr_mtu = dev->mtu;
			memcpy_tofs(arg, &ifr, sizeof(struct ifreq));
			ret = 0;
			break;
	
		case SIOCSIFMTU:	/* Set the MTU of a device */
		
			/*
			 *	MTU must be positive and under the page size problem
			 */
			 
			if(ifr.ifr_mtu<1 || ifr.ifr_mtu>3800)
				return -EINVAL;
			dev->mtu = ifr.ifr_mtu;
			ret = 0;
			break;
	
		case SIOCGIFMEM:	/* Get the per device memory space. We can add this but currently
					   do not support it */
			printk("NET: ioctl(SIOCGIFMEM, 0x%08X)\n", (int)arg);
			ret = -EINVAL;
			break;
		
		case SIOCSIFMEM:	/* Set the per device memory buffer space. Not applicable in our case */
			printk("NET: ioctl(SIOCSIFMEM, 0x%08X)\n", (int)arg);
			ret = -EINVAL;
			break;

		case OLD_SIOCGIFHWADDR:	/* Get the hardware address. This will change and SIFHWADDR will be added */
			memcpy(ifr.old_ifr_hwaddr,dev->dev_addr, MAX_ADDR_LEN);
			memcpy_tofs(arg,&ifr,sizeof(struct ifreq));
			ret=0;
			break;

		case SIOCGIFHWADDR:
			memcpy(ifr.ifr_hwaddr.sa_data,dev->dev_addr, MAX_ADDR_LEN);
			ifr.ifr_hwaddr.sa_family=dev->type;			
			memcpy_tofs(arg,&ifr,sizeof(struct ifreq));
			ret=0;
			break;
			
		case SIOCSIFHWADDR:
			if(dev->set_mac_address==NULL)
				return -EOPNOTSUPP;
			if(ifr.ifr_hwaddr.sa_family!=dev->type)
				return -EINVAL;
			ret=dev->set_mac_address(dev,ifr.ifr_hwaddr.sa_data);
			break;
			
		/*
		 *	Unknown ioctl
		 */

		default:
			ret = -EINVAL;
	}
	return(ret);
}


/*
 *	This function handles all "interface"-type I/O control requests. The actual
 *	'doing' part of this is dev_ifsioc above.
 */

int dev_ioctl(unsigned int cmd, void *arg)
{
	switch(cmd) 
	{
		/*
		 *	The old old setup ioctl. Even its name and this entry will soon be
		 *	just so much ionization on a backup tape.
		 */

		case SIOCGIFCONF:
			(void) dev_ifconf((char *) arg);
			return 0;

		/*
		 *	Ioctl calls that can be done by all.
		 */
		 
		case SIOCGIFFLAGS:
		case SIOCGIFADDR:
		case SIOCGIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFMEM:
		case SIOCGIFHWADDR:
		case SIOCSIFHWADDR:
		case OLD_SIOCGIFHWADDR:
			return dev_ifsioc(arg, cmd);

		/*
		 *	Ioctl calls requiring the power of a superuser
		 */
		 
		case SIOCSIFFLAGS:
		case SIOCSIFADDR:
		case SIOCSIFDSTADDR:
		case SIOCSIFBRDADDR:
		case SIOCSIFNETMASK:
		case SIOCSIFMETRIC:
		case SIOCSIFMTU:
		case SIOCSIFMEM:
			if (!suser())
				return -EPERM;
			return dev_ifsioc(arg, cmd);
	
		case SIOCSIFLINK:
			return -EINVAL;

		/*
		 *	Unknown ioctl.
		 */	
		 
		default:
			return -EINVAL;
	}
}


/*
 *	Initialize the DEV module. At boot time this walks the device list and
 *	unhooks any devices that fail to initialise (normally hardware not 
 *	present) and leaves us with a valid list of present and active devices.
 *
 *	The PCMICA code may need to change this a little, and add a pair
 *	of register_inet_device() unregister_inet_device() calls. This will be
 *	needed for ethernet as modules support.
 */
 
void dev_init(void)
{
	struct device *dev, *dev2;

	/*
	 *	Add the devices.
	 *	If the call to dev->init fails, the dev is removed
	 *	from the chain disconnecting the device until the
	 *	next reboot.
	 */
	 
	dev2 = NULL;
	for (dev = dev_base; dev != NULL; dev=dev->next) 
	{
		if (dev->init && dev->init(dev)) 
		{
			/*
			 *	It failed to come up. Unhook it.
			 */
			 
			if (dev2 == NULL) 
				dev_base = dev->next;
			else 
				dev2->next = dev->next;
		} 
		else
		{
			dev2 = dev;
		}
	}
}
