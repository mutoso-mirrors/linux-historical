/*
 * 	NET3	Protocol independent device support routines.
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
 *		David Hinds <dhinds@allegro.stanford.edu>
 *
 *	Changes:
 *		Alan Cox	:	device private ioctl copies fields back.
 *		Alan Cox	:	Transmit queue code does relevant stunts to
 *					keep the queue safe.
 *		Alan Cox	:	Fixed double lock.
 *		Alan Cox	:	Fixed promisc NULL pointer trap
 *		????????	:	Support the full private ioctl range
 *		Alan Cox	:	Moved ioctl permission check into drivers
 *		Tim Kordas	:	SIOCADDMULTI/SIOCDELMULTI
 *		Alan Cox	:	100 backlog just doesn't cut it when
 *					you start doing multicast video 8)
 *		Alan Cox	:	Rewrote net_bh and list manager.
 *		Alan Cox	: 	Fix ETH_P_ALL echoback lengths.
 *		Alan Cox	:	Took out transmit every packet pass
 *					Saved a few bytes in the ioctl handler
 *		Alan Cox	:	Network driver sets packet type before calling netif_rx. Saves
 *					a function call a packet.
 *		Alan Cox	:	Hashed net_bh()
 *		Richard Kooijman:	Timestamp fixes.
 *		Alan Cox	:	Wrong field in SIOCGIFDSTADDR
 *		Alan Cox	:	Device lock protection.
 *		Alan Cox	: 	Fixed nasty side effect of device close changes.
 *		Rudi Cilibrasi	:	Pass the right thing to set_mac_address()
 *		Dave Miller	:	32bit quantity for the device lock to make it work out
 *					on a Sparc.
 *		Bjorn Ekwall	:	Added KERNELD hack.
 *		Alan Cox	:	Cleaned up the backlog initialise.
 *		Craig Metz	:	SIOCGIFCONF fix if space for under
 *					1 device.
 *	    Thomas Bogendoerfer :	Return ENODEV for dev_open, if there
 *					is no device open function.
 *
 */

#include <asm/uaccess.h>
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
#include <linux/notifier.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/arp.h>
#include <net/slhc.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <net/br.h>
#include <linux/net_alias.h>
#ifdef CONFIG_KERNELD
#include <linux/kerneld.h>
#endif
#ifdef CONFIG_NET_RADIO
#include <linux/wireless.h>
#endif	/* CONFIG_NET_RADIO */
#ifdef CONFIG_PLIP
extern int plip_init(void);
#endif

/*
 *	The list of devices, that are able to output.
 */

static struct device *dev_up_base;

/*
 *	The list of packet types we will receive (as opposed to discard)
 *	and the routines to invoke.
 *
 *	Why 16. Because with 16 the only overlap we get on a hash of the
 *	low nibble of the protocol value is RARP/SNAP/X.25. 
 *
 *		0800	IP
 *		0001	802.3
 *		0002	AX.25
 *		0004	802.2
 *		8035	RARP
 *		0005	SNAP
 *		0805	X.25
 *		0806	ARP
 *		8137	IPX
 *		0009	Localtalk
 *		86DD	IPv6
 */

struct packet_type *ptype_base[16];		/* 16 way hashed list */
struct packet_type *ptype_all = NULL;		/* Taps */

/*
 *	Device list lock
 */
 
atomic_t dev_lockct = ATOMIC_INIT;
 
/*
 *	Our notifier list
 */
 
struct notifier_block *netdev_chain=NULL;

/*
 *	Device drivers call our routines to queue packets here. We empty the
 *	queue in the bottom half handler.
 */

static struct sk_buff_head backlog;

/* 
 *	We don't overdo the queue or we will thrash memory badly.
 */
 
static int backlog_size = 0;



/******************************************************************************************

		Protocol management and registration routines

*******************************************************************************************/

/*
 *	For efficiency
 */

static int dev_nit=0;

/*
 *	Add a protocol ID to the list. Now that the input handler is
 *	smarter we can dispense with all the messy stuff that used to be
 *	here.
 */
 
void dev_add_pack(struct packet_type *pt)
{
	int hash;
	if(pt->type==htons(ETH_P_ALL))
	{
		dev_nit++;
		pt->next=ptype_all;
		ptype_all=pt;
	}
	else
	{	
		hash=ntohs(pt->type)&15;
		pt->next = ptype_base[hash];
		ptype_base[hash] = pt;
	}
}


/*
 *	Remove a protocol ID from the list.
 */
 
void dev_remove_pack(struct packet_type *pt)
{
	struct packet_type **pt1;
	if(pt->type==htons(ETH_P_ALL))
	{
		dev_nit--;
		pt1=&ptype_all;
	}
	else
		pt1=&ptype_base[ntohs(pt->type)&15];
	for(; (*pt1)!=NULL; pt1=&((*pt1)->next))
	{
		if(pt==(*pt1))
		{
			*pt1=pt->next;
			return;
		}
	}
	printk(KERN_WARNING "dev_remove_pack: %p not found.\n", pt);
}

/*****************************************************************************************

			    Device Interface Subroutines

******************************************************************************************/

/* 
 *	Find an interface by name.
 */
 
struct device *dev_get(const char *name)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		if (strcmp(dev->name, name) == 0)
			return(dev);
	}
	return NULL;
}

struct device * dev_get_by_index(int ifindex)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		if (dev->ifindex == ifindex)
			return(dev);
	}
	return NULL;
}

struct device *dev_getbyhwaddr(unsigned short type, char *ha)
{
	struct device *dev;

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		if (dev->type == type &&
		    !(dev->flags&(IFF_LOOPBACK|IFF_NOARP)) &&
		    memcmp(dev->dev_addr, ha, dev->addr_len) == 0)
			return(dev);
	}
	return(NULL);
}

/*
 *	Passed a format string - eg "lt%d" it will try and find a suitable
 *	id. Not efficient for many devices, not called a lot..
 */

int dev_alloc_name(struct device *dev, const char *name)
{
	int i;
	/*
	 *	If you need over 100 please also fix the algorithm...
	 */
	for(i=0;i<100;i++)
	{
		sprintf(dev->name,name,i);
		if(dev_get(dev->name)==NULL)
			return i;
	}
	return -ENFILE;	/* Over 100 of the things .. bail out! */
}
 
struct device *dev_alloc(const char *name, int *err)
{
	struct device *dev=kmalloc(sizeof(struct device)+16, GFP_KERNEL);
	if(dev==NULL)
	{
		*err=-ENOBUFS;
		return NULL;
	}
	dev->name=(char *)(dev+1);	/* Name string space */
	*err=dev_alloc_name(dev,name);
	if(*err<0)
	{
		kfree(dev);
		return NULL;
	}
	return dev;
}


/*
 *	Find and possibly load an interface.
 */
 
#ifdef CONFIG_KERNELD

void dev_load(const char *name)
{
	if(!dev_get(name)) {
#ifdef CONFIG_NET_ALIAS
		const char *sptr;
 
		for (sptr=name ; *sptr ; sptr++) if(*sptr==':') break;
		if (!(*sptr && *(sptr+1)))
#endif
		request_module(name);
	}
}

#endif
 
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
	{
		dev->flags |= (IFF_UP | IFF_RUNNING);
		/*
		 *	Initialise multicasting status 
		 */
		dev_mc_upload(dev);
		notifier_call_chain(&netdev_chain, NETDEV_UP, dev);
		
		/*
		 *	Passive non transmitting devices (including
		 *	aliases) need not be on this chain.
		 */
		if (!net_alias_is(dev) && dev->tx_queue_len)
		{
			cli();
			dev->next_up = dev_up_base;
			dev_up_base = dev;
			sti();
		}
	}
	return(ret);
}


/*
 *	Completely shutdown an interface.
 */
 
int dev_close(struct device *dev)
{
	int ct=0;
	struct device **devp;

	/*
	 *	Call the device specific close. This cannot fail.
	 *	Only if device is UP
	 */
	 
	if ((dev->flags & IFF_UP) && dev->stop)
		dev->stop(dev);

	/*
	 *	Device is now down.
	 */
	 
	dev->flags&=~(IFF_UP|IFF_RUNNING);

	/*
	 *	Tell people we are going down
	 */
	 
	notifier_call_chain(&netdev_chain, NETDEV_DOWN, dev);
	/*
	 *	Flush the multicast chain
	 */
	dev_mc_discard(dev);

	/*
	 *	Purge any queued packets when we down the link 
	 */
	while(ct<DEV_NUMBUFFS)
	{
		struct sk_buff *skb;
		while((skb=skb_dequeue(&dev->buffs[ct]))!=NULL)
			kfree_skb(skb,FREE_WRITE);
		ct++;
	}

	/*
	 *	The device is no longer up. Drop it from the list.
	 */
	 
	devp = &dev_up_base;
	while (*devp)
	{
		if (*devp == dev)
		{
			*devp = dev->next_up;
			break;
		}
		devp = &(*devp)->next_up;
	}
	return(0);
}


/*
 *	Device change register/unregister. These are not inline or static
 *	as we export them to the world.
 */

int register_netdevice_notifier(struct notifier_block *nb)
{
	return notifier_chain_register(&netdev_chain, nb);
}

int unregister_netdevice_notifier(struct notifier_block *nb)
{
	return notifier_chain_unregister(&netdev_chain,nb);
}

/*
 *	Support routine. Sends outgoing frames to any network
 *	taps currently in use.
 */

static void queue_xmit_nit(struct sk_buff *skb, struct device *dev)
{
	struct packet_type *ptype;
	get_fast_time(&skb->stamp);

	for (ptype = ptype_all; ptype!=NULL; ptype = ptype->next) 
	{
		/* Never send packets back to the socket
		 * they originated from - MvS (miquels@drinkel.ow.org)
		 */
		if ((ptype->dev == dev || !ptype->dev) &&
			((struct sock *)ptype->data != skb->sk))
		{
			struct sk_buff *skb2;
			if ((skb2 = skb_clone(skb, GFP_ATOMIC)) == NULL)
				break;
			skb2->mac.raw = skb2->data;
			skb2->nh.raw = skb2->h.raw = skb2->data + dev->hard_header_len;
			ptype->func(skb2, skb->dev, ptype);
		}
	}
}
 
/*
 *	Send (or queue for sending) a packet. 
 *
 *	IMPORTANT: When this is called to resend frames. The caller MUST
 *	already have locked the sk_buff. Apart from that we do the
 *	rest of the magic.
 */

static void do_dev_queue_xmit(struct sk_buff *skb, struct device *dev, int pri)
{
	unsigned long flags;
	struct sk_buff_head *list;
	int retransmission = 0;	/* used to say if the packet should go	*/
				/* at the front or the back of the	*/
				/* queue - front is a retransmit try	*/

#if CONFIG_SKB_CHECK 
	IS_SKB(skb);
#endif    

	/*
	 *	Negative priority is used to flag a frame that is being pulled from the
	 *	queue front as a retransmit attempt. It therefore goes back on the queue
	 *	start on a failure.
	 */
	 
  	if (pri < 0) 
  	{
		pri = -pri-1;
		retransmission = 1;
  	}

#ifdef CONFIG_NET_DEBUG
	if (pri >= DEV_NUMBUFFS) 
	{
		printk(KERN_WARNING "bad priority in do_dev_queue_xmit.\n");
		pri = 1;
	}
#endif

	/*
	 *	If we are bridging and this is directly generated output
	 *	pass the frame via the bridge.
	 */

#ifdef CONFIG_BRIDGE
	if(skb->pkt_bridged!=IS_BRIDGED && br_stats.flags & BR_UP)
	{
		if(br_tx_frame(skb))
			return;
	}
#endif

	list = dev->buffs + pri;

	save_flags(flags);

	/*
	 *	If this isn't a retransmission, use the first packet instead.
	 *	Note: We don't do strict priority ordering here. We will in
	 *	fact kick the queue that is our priority. The dev_tint reload
	 *	does strict priority queueing. In effect what we are doing here
	 *	is to add some random jitter to the queues and to do so by
	 *	saving clocks. Doing a perfect priority queue isn't a good idea
	 *	as you get some fascinating timing interactions.
	 */

	if (!retransmission) 
	{
		/* avoid overrunning the device queue.. */
		if (skb_queue_len(list) > dev->tx_queue_len) 
		{
			dev_kfree_skb(skb, FREE_WRITE);
			return;
		}

		/* copy outgoing packets to any sniffer packet handlers */
		if (dev_nit) 
			queue_xmit_nit(skb,dev);

		if (skb_queue_len(list)) {
			cli();
			__skb_queue_tail(list, skb);
			skb = __skb_dequeue(list);
			restore_flags(flags);
		}
	}
	if (dev->hard_start_xmit(skb, dev) == 0) {
		/*
		 *	Packet is now solely the responsibility of the driver
		 */
		return;
	}

	/*
	 *	Transmission failed, put skb back into a list. Once on the list it's safe and
	 *	no longer device locked (it can be freed safely from the device queue)
	 */
	cli();
	__skb_queue_head(list,skb);
	restore_flags(flags);
}

/*
 *	Entry point for transmitting frames.
 */
 
int dev_queue_xmit(struct sk_buff *skb)
{
	struct device *dev = skb->dev;

	start_bh_atomic();

#if CONFIG_SKB_CHECK 
	IS_SKB(skb);
#endif    

	/*
	 *	If the address has not been resolved. Call the device header rebuilder.
	 *	This can cover all protocols and technically not just ARP either.
	 */
	 
	if (!skb->arp) 
	{
		/*
		 *	FIXME: we should make the printk for no rebuild
		 *	header a default rebuild_header routine and drop
		 *	this call. Similarly we should make hard_header
		 *	have a default NULL operation not check conditions.
		 */
		if (dev->rebuild_header) 
		{
			if (dev->rebuild_header(skb)) 
			{
				end_bh_atomic();
				return 0;
			}
		} 
		else
			printk(KERN_DEBUG "%s: !skb->arp & !rebuild_header!\n", dev->name);
	}

	/*
	 *
	 * 	If dev is an alias, switch to its main device.
	 *	"arp" resolution has been made with alias device, so
	 *	arp entries refer to alias, not main.
	 *
	 */

	if (net_alias_is(dev))
	  	skb->dev = dev = net_alias_main_dev(dev);

	do_dev_queue_xmit(skb, dev, skb->priority);
	end_bh_atomic();
	return 0;
}

/*
 *	Fast path for loopback frames.
 */
 
void dev_loopback_xmit(struct sk_buff *skb)
{
	struct sk_buff *newskb=skb_clone(skb, GFP_ATOMIC);
	if (newskb==NULL)
		return;

	skb_pull(newskb, newskb->nh.raw - newskb->data);
	newskb->ip_summed = CHECKSUM_UNNECESSARY;
	if (newskb->dst==NULL)
		printk(KERN_DEBUG "BUG: packet without dst looped back 1\n");
	netif_rx(newskb);
}


/*
 *	Receive a packet from a device driver and queue it for the upper
 *	(protocol) levels.  It always succeeds. 
 */

void netif_rx(struct sk_buff *skb)
{
	static int dropping = 0;

	/*
	 *	Any received buffers are un-owned and should be discarded
	 *	when freed. These will be updated later as the frames get
	 *	owners.
	 */

	skb->sk = NULL;
	if(skb->stamp.tv_sec==0)
		get_fast_time(&skb->stamp);

	/*
	 *	Check that we aren't overdoing things.
	 */

	if (!backlog_size)
  		dropping = 0;
	else if (backlog_size > 300)
		dropping = 1;

	if (dropping) 
	{
		kfree_skb(skb, FREE_READ);
		return;
	}

	/*
	 *	Add it to the "backlog" queue. 
	 */
#if CONFIG_SKB_CHECK
	IS_SKB(skb);
#endif	
	skb_queue_tail(&backlog,skb);
	backlog_size++;
  
	/*
	 *	If any packet arrived, mark it for processing after the
	 *	hardware interrupt returns.
	 */

	mark_bh(NET_BH);
	return;
}

/*
 *	This routine causes all interfaces to try to send some data. 
 */
 
static void dev_transmit(void)
{
	struct device *dev;

	for (dev = dev_up_base; dev != NULL; dev = dev->next_up)
	{
		if (dev->flags != 0 && !dev->tbusy) 
		{
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
 *	When we are called the queue is ready to grab, the interrupts are
 *	on and hardware can interrupt and queue to the receive queue as we
 *	run with no problems.
 *	This is run as a bottom half after an interrupt handler that does
 *	mark_bh(NET_BH);
 */
 
void net_bh(void)
{
	struct packet_type *ptype;
	struct packet_type *pt_prev;
	unsigned short type;
	int nit = 301;

	/*
	 *	Can we send anything now? We want to clear the
	 *	decks for any more sends that get done as we
	 *	process the input. This also minimises the
	 *	latency on a transmit interrupt bh.
	 */

	dev_transmit();
  
	/*
	 *	Any data left to process. This may occur because a
	 *	mark_bh() is done after we empty the queue including
	 *	that from the device which does a mark_bh() just after
	 */

	/*
	 *	While the queue is not empty..
	 *
	 *	Note that the queue never shrinks due to
	 *	an interrupt, so we can do this test without
	 *	disabling interrupts.
	 */

	while (!skb_queue_empty(&backlog)) 
	{
		struct sk_buff * skb = backlog.next;

		/*
		 *	We have a packet. Therefore the queue has shrunk
		 */
		cli();
		__skb_unlink(skb, &backlog);
  		backlog_size--;
		sti();

		/*
		 *	We do not want to spin in net_bh infinitely. --ANK
		 */
		if (--nit <= 0)
		{
			if (nit == 0)
				printk(KERN_WARNING "net_bh: too many loops, dropping...\n");
			kfree_skb(skb, FREE_WRITE);
			continue;
		}
		
#ifdef CONFIG_BRIDGE

		/*
		 *	If we are bridging then pass the frame up to the
		 *	bridging code (if this protocol is to be bridged).
		 *      If it is bridged then move on
		 */
		 
		if (br_stats.flags & BR_UP && br_protocol_ok(ntohs(skb->protocol)))
		{
			/*
			 *	We pass the bridge a complete frame. This means
			 *	recovering the MAC header first.
			 */
			 
			int offset=skb->data-skb->mac.raw;
			cli();
			skb_push(skb,offset);	/* Put header back on for bridge */
			if(br_receive_frame(skb))
			{
				sti();
				continue;
			}
			/*
			 *	Pull the MAC header off for the copy going to
			 *	the upper layers.
			 */
			skb_pull(skb,offset);
			sti();
		}
#endif
		
		/*
	 	 *	Bump the pointer to the next structure.
		 * 
		 *	On entry to the protocol layer. skb->data and
		 *	skb->nh.raw point to the MAC and encapsulated data
		 */

		/* XXX until we figure out every place to modify.. */
		skb->h.raw = skb->nh.raw = skb->data;

		/*
		 * 	Fetch the packet protocol ID. 
		 */
		
		type = skb->protocol;

		/*
		 *	We got a packet ID.  Now loop over the "known protocols"
		 * 	list. There are two lists. The ptype_all list of taps (normally empty)
		 *	and the main protocol list which is hashed perfectly for normal protocols.
		 */
		
		pt_prev = NULL;
		for (ptype = ptype_all; ptype!=NULL; ptype=ptype->next)
		{
			if(pt_prev)
			{
				struct sk_buff *skb2=skb_clone(skb, GFP_ATOMIC);
				if(skb2)
					pt_prev->func(skb2,skb->dev, pt_prev);
			}
			pt_prev=ptype;
		}
		
		for (ptype = ptype_base[ntohs(type)&15]; ptype != NULL; ptype = ptype->next) 
		{
			if (ptype->type == type && (!ptype->dev || ptype->dev==skb->dev))
			{
				/*
				 *	We already have a match queued. Deliver
				 *	to it and then remember the new match
				 */
				if(pt_prev)
				{
					struct sk_buff *skb2;

					skb2=skb_clone(skb, GFP_ATOMIC);

					/*
					 *	Kick the protocol handler. This should be fast
					 *	and efficient code.
					 */

					if(skb2)
						pt_prev->func(skb2, skb->dev, pt_prev);
				}
				/* Remember the current last to do */
				pt_prev=ptype;
			}
		} /* End of protocol list loop */
		
		/*
		 *	Is there a last item to send to ?
		 */

		if(pt_prev)
			pt_prev->func(skb, skb->dev, pt_prev);
		/*
		 * 	Has an unknown packet has been received ?
		 */
	 
		else
			kfree_skb(skb, FREE_WRITE);
		/*
		 *	Again, see if we can transmit anything now. 
		 *	[Ought to take this out judging by tests it slows
		 *	 us down not speeds us up]
		 */
#ifdef XMIT_EVERY
		dev_transmit();
#endif		
  	}	/* End of queue loop */
  	
  	/*
  	 *	We have emptied the queue
  	 */
	
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
	unsigned long flags;
	struct sk_buff_head * head;
	
	/*
	 * aliases do not transmit (for now :) )
	 */

	if (net_alias_is(dev)) {
		printk(KERN_DEBUG "net alias %s transmits\n", dev->name);
		return;
	}

	head = dev->buffs;
	save_flags(flags);
	cli();

	/*
	 *	Work the queues in priority order
	 */	 
	for(i = 0;i < DEV_NUMBUFFS; i++,head++)
	{

		while (!skb_queue_empty(head)) {
			struct sk_buff *skb;

			skb = head->next;
			__skb_unlink(skb, head);
			/*
			 *	Stop anyone freeing the buffer while we retransmit it
			 */
			restore_flags(flags);
			/*
			 *	Feed them to the output stage and if it fails
			 *	indicate they re-queue at the front.
			 */
			do_dev_queue_xmit(skb,dev,-i - 1);
			/*
			 *	If we can take no more then stop here.
			 */
			if (dev->tbusy)
				return;
			cli();
		}
	}
	restore_flags(flags);
}


/*
 *	Perform a SIOCGIFCONF call. This structure will change
 *	size eventually, and there is nothing I can do about it.
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
	
	err = copy_from_user(&ifc, arg, sizeof(struct ifconf));
	if (err)
		return -EFAULT; 
	len = ifc.ifc_len;
	pos = ifc.ifc_buf;

	/*
	 *	We now walk the device list filling each active device
	 *	into the array.
	 */
	
	/*
	 *	Loop over the interfaces, and write an info block for each. 
	 */

	for (dev = dev_base; dev != NULL; dev = dev->next) 
	{
		/*
		 *	Have we run out of space here ?
		 */
	
		if (len < sizeof(struct ifreq)) 
			break;

		memset(&ifr, 0, sizeof(struct ifreq));
		strcpy(ifr.ifr_name, dev->name);
		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_family = dev->family;
		(*(struct sockaddr_in *) &ifr.ifr_addr).sin_addr.s_addr = dev->pa_addr;


		/*
		 *	Write this block to the caller's space. 
		 */
		 
		err = copy_to_user(pos, &ifr, sizeof(struct ifreq));
		if (err)
			return -EFAULT; 
		pos += sizeof(struct ifreq);
		len -= sizeof(struct ifreq);		
  	}

	/*
	 *	All done.  Write the updated control block back to the caller. 
	 */
	 
	ifc.ifc_len = (pos - ifc.ifc_buf);
	ifc.ifc_req = (struct ifreq *) ifc.ifc_buf;
	err = copy_to_user(arg, &ifc, sizeof(struct ifconf));
	if (err)
		return -EFAULT; 

	/*
	 *	Report how much was filled in
	 */
	 
	return(pos - arg);
}


/*
 *	This is invoked by the /proc filesystem handler to display a device
 *	in detail.
 */

#ifdef CONFIG_PROC_FS
static int sprintf_stats(char *buffer, struct device *dev)
{
	struct net_device_stats *stats = (dev->get_stats ? dev->get_stats(dev): NULL);
	int size;
	
	if (stats)
		size = sprintf(buffer, "%6s:%8lu %7lu %4lu %4lu %4lu %4lu %8lu %8lu %4lu %4lu %4lu %5lu %4lu\n",
		   dev->name,
		   stats->rx_bytes,
		   stats->rx_packets, stats->rx_errors,
		   stats->rx_dropped + stats->rx_missed_errors,
		   stats->rx_fifo_errors,
		   stats->rx_length_errors + stats->rx_over_errors
		   + stats->rx_crc_errors + stats->rx_frame_errors,
		   stats->tx_bytes,
		   stats->tx_packets, stats->tx_errors, stats->tx_dropped,
		   stats->tx_fifo_errors, stats->collisions,
		   stats->tx_carrier_errors + stats->tx_aborted_errors
		   + stats->tx_window_errors + stats->tx_heartbeat_errors);
	else
		size = sprintf(buffer, "%6s: No statistics available.\n", dev->name);

	return size;
}

/*
 *	Called from the PROCfs module. This now uses the new arbitrary sized /proc/net interface
 *	to create /proc/net/dev
 */
 
int dev_get_info(char *buffer, char **start, off_t offset, int length, int dummy)
{
	int len=0;
	off_t begin=0;
	off_t pos=0;
	int size;
	
	struct device *dev;


	size = sprintf(buffer, "Inter-|   Receive                  |  Transmit\n"
			    " face |bytes    packets errs drop fifo frame|bytes    packets errs drop fifo colls carrier\n");
	
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
#endif	/* CONFIG_PROC_FS */


#ifdef CONFIG_NET_RADIO
#ifdef CONFIG_PROC_FS

/*
 * Print one entry of /proc/net/wireless
 * This is a clone of /proc/net/dev (just above)
 */
static int sprintf_wireless_stats(char *buffer, struct device *dev)
{
	/* Get stats from the driver */
	struct iw_statistics *stats = (dev->get_wireless_stats ?
				       dev->get_wireless_stats(dev) :
				       (struct iw_statistics *) NULL);
	int size;

	if(stats != (struct iw_statistics *) NULL)
		size = sprintf(buffer,
			       "%6s: %02x  %3d%c %3d%c  %3d%c %5d %5d %5d\n",
			       dev->name,
			       stats->status,
			       stats->qual.qual,
			       stats->qual.updated & 1 ? '.' : ' ',
			       stats->qual.level,
			       stats->qual.updated & 2 ? '.' : ' ',
			       stats->qual.noise,
			       stats->qual.updated & 3 ? '.' : ' ',
			       stats->discard.nwid,
			       stats->discard.code,
			       stats->discard.misc);
	else
		size = 0;

	return size;
}

/*
 * Print info for /proc/net/wireless (print all entries)
 * This is a clone of /proc/net/dev (just above)
 */
int dev_get_wireless_info(char * buffer, char **start, off_t offset,
			  int length, int dummy)
{
	int		len = 0;
	off_t		begin = 0;
	off_t		pos = 0;
	int		size;
	
	struct device *	dev;

	size = sprintf(buffer,
		       "Inter-|sta|  Quality       |  Discarded packets\n"
		       " face |tus|link level noise| nwid crypt  misc\n");
	
	pos+=size;
	len+=size;

	for(dev = dev_base; dev != NULL; dev = dev->next) 
	{
		size = sprintf_wireless_stats(buffer+len, dev);
		len+=size;
		pos=begin+len;

		if(pos < offset)
		{
			len=0;
			begin=pos;
		}
		if(pos > offset + length)
			break;
	}

	*start = buffer + (offset - begin);	/* Start of wanted data */
	len -= (offset - begin);		/* Start slop */
	if(len > length)
		len = length;		/* Ending slop */

	return len;
}
#endif	/* CONFIG_PROC_FS */
#endif	/* CONFIG_NET_RADIO */


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
	int ret, err;

	/*
	 *	Fetch the caller's info block into kernel space
	 */
	
	err = copy_from_user(&ifr, arg, sizeof(struct ifreq));
	if (err)
		return -EFAULT; 

	/*
	 *	See which interface the caller is talking about. 
	 */
	 
	/*
	 *
	 *	net_alias_dev_get(): dev_get() with added alias naming magic.
	 *	only allow alias creation/deletion if (getset==SIOCSIFADDR)
	 *
	 */
	 
#ifdef CONFIG_KERNELD
	dev_load(ifr.ifr_name);
#endif	

#ifdef CONFIG_NET_ALIAS
	if ((dev = net_alias_dev_get(ifr.ifr_name, getset == SIOCSIFADDR, &err, NULL, NULL)) == NULL)
		return(err);
#else
	if ((dev = dev_get(ifr.ifr_name)) == NULL) 	
		return(-ENODEV);
#endif
	switch(getset) 
	{
		case SIOCGIFFLAGS:	/* Get interface flags */
			ifr.ifr_flags = dev->flags;
			goto rarok;

		case SIOCSIFFLAGS:	/* Set interface flags */
			{
				int old_flags = dev->flags;
				
				/*
				 *	We are not allowed to potentially close/unload
				 *	a device until we get this lock.
				 */
				
				dev_lock_wait();
				dev_lock_list();
				
				/*
				 *	Set the flags on our device.
				 */
				 
				dev->flags = (ifr.ifr_flags & (
					IFF_BROADCAST | IFF_DEBUG | IFF_LOOPBACK |
					IFF_POINTOPOINT | IFF_NOTRAILERS | IFF_RUNNING |
					IFF_NOARP | IFF_PROMISC | IFF_ALLMULTI | IFF_SLAVE | IFF_MASTER
					| IFF_MULTICAST)) | (dev->flags & IFF_UP);
				/*
				 *	Load in the correct multicast list now the flags have changed.
				 */				

				dev_mc_upload(dev);

			  	/*
			  	 *	Have we downed the interface. We handle IFF_UP ourselves
			  	 *	according to user attempts to set it, rather than blindly
			  	 *	setting it.
			  	 */
			  	 
			  	if ((old_flags^ifr.ifr_flags)&IFF_UP)	/* Bit is different  ? */
			  	{
					if(old_flags&IFF_UP)		/* Gone down */
						ret=dev_close(dev); 		
					else				/* Come up */
					{
						ret=dev_open(dev);
						if(ret<0)
							dev->flags&=~IFF_UP;	/* Open failed */
					}	
			  	}
			  	else
			  		ret=0;
				/*
				 *	Load in the correct multicast list now the flags have changed.
				 */				

				dev_mc_upload(dev);
				if ((dev->flags&IFF_UP) && ((old_flags^dev->flags)&~(IFF_UP|IFF_RUNNING|IFF_PROMISC)))
				{
					printk(KERN_DEBUG "SIFFL %s(%s)\n", dev->name, current->comm);
					notifier_call_chain(&netdev_chain, NETDEV_CHANGE, dev);
				}
				if ((dev->flags^old_flags)&IFF_PROMISC) {
					if (dev->flags&IFF_PROMISC)
						printk(KERN_INFO "%s enters promiscuous mode.\n", dev->name);
					else
						printk(KERN_INFO "%s leave promiscuous mode.\n", dev->name);
				}
				dev_unlock_list();
			}
			break;
		
		case SIOCGIFMETRIC:	/* Get the metric on the interface (currently unused) */
			
			ifr.ifr_metric = dev->metric;
			goto  rarok;
			
		case SIOCSIFMETRIC:	/* Set the metric on the interface (currently unused) */
			dev->metric = ifr.ifr_metric;
			ret=0;
			break;
	
		case SIOCGIFMTU:	/* Get the MTU of a device */
			ifr.ifr_mtu = dev->mtu;
			goto rarok;
	
		case SIOCSIFMTU:	/* Set the MTU of a device */
		
			if (ifr.ifr_mtu == dev->mtu) {
				ret = 0;
				break;
			}

			/*
			 *	MTU must be positive.
			 */
			 
			if(ifr.ifr_mtu<68)
				return -EINVAL;

			if (dev->change_mtu)
				ret = dev->change_mtu(dev, ifr.ifr_mtu);
			else
			{
				dev->mtu = ifr.ifr_mtu;
				ret = 0;
			}
			if (!ret && dev->flags&IFF_UP) {
				printk(KERN_DEBUG "SIFMTU %s(%s)\n", dev->name, current->comm);
				notifier_call_chain(&netdev_chain, NETDEV_CHANGEMTU, dev);
			}
			break;
	
		case SIOCGIFMEM:	/* Get the per device memory space. We can add this but currently
					   do not support it */
			ret = -EINVAL;
			break;
		
		case SIOCSIFMEM:	/* Set the per device memory buffer space. Not applicable in our case */
			ret = -EINVAL;
			break;

		case SIOCGIFHWADDR:
			memcpy(ifr.ifr_hwaddr.sa_data,dev->dev_addr, MAX_ADDR_LEN);
			ifr.ifr_hwaddr.sa_family=dev->type;			
			goto rarok;
				
		case SIOCSIFHWADDR:
			if(dev->set_mac_address==NULL)
				return -EOPNOTSUPP;
			if(ifr.ifr_hwaddr.sa_family!=dev->type)
				return -EINVAL;
			ret=dev->set_mac_address(dev,&ifr.ifr_hwaddr);
			if (!ret)
				notifier_call_chain(&netdev_chain, NETDEV_CHANGEADDR, dev);
			break;
			
		case SIOCGIFMAP:
			ifr.ifr_map.mem_start=dev->mem_start;
			ifr.ifr_map.mem_end=dev->mem_end;
			ifr.ifr_map.base_addr=dev->base_addr;
			ifr.ifr_map.irq=dev->irq;
			ifr.ifr_map.dma=dev->dma;
			ifr.ifr_map.port=dev->if_port;
			goto rarok;
			
		case SIOCSIFMAP:
			if(dev->set_config==NULL)
				return -EOPNOTSUPP;
			return dev->set_config(dev,&ifr.ifr_map);
			
		case SIOCADDMULTI:
			if(dev->set_multicast_list==NULL)
				return -EINVAL;
			if(ifr.ifr_hwaddr.sa_family!=AF_UNSPEC)
				return -EINVAL;
			dev_mc_add(dev,ifr.ifr_hwaddr.sa_data, dev->addr_len, 1);
			return 0;

		case SIOCDELMULTI:
			if(dev->set_multicast_list==NULL)
				return -EINVAL;
			if(ifr.ifr_hwaddr.sa_family!=AF_UNSPEC)
				return -EINVAL;
			dev_mc_delete(dev,ifr.ifr_hwaddr.sa_data,dev->addr_len, 1);
			return 0;

		case SIOGIFINDEX:
			ifr.ifr_ifindex = dev->ifindex;
			goto rarok;
			

		/*
		 *	Unknown or private ioctl
		 */

		default:
			if((getset >= SIOCDEVPRIVATE) &&
			   (getset <= (SIOCDEVPRIVATE + 15))) {
				if(dev->do_ioctl==NULL)
					return -EOPNOTSUPP;
				ret = dev->do_ioctl(dev, &ifr, getset);
				if (!ret)
				{
					err = copy_to_user(arg,&ifr,sizeof(struct ifreq));
					if (err)
						ret = -EFAULT;
				}
				break;
			}

#ifdef CONFIG_NET_RADIO
			if((getset >= SIOCIWFIRST) && (getset <= SIOCIWLAST))
			{
				if(dev->do_ioctl==NULL)
					return -EOPNOTSUPP;
				/* Perform the ioctl */
				ret=dev->do_ioctl(dev, &ifr, getset);
				/* If return args... */
				if(IW_IS_GET(getset))
				{
					if (copy_to_user(arg, &ifr,
							 sizeof(struct ifreq)))
					{
						ret = -EFAULT;
					}
				}
				break;
			}
#endif	/* CONFIG_NET_RADIO */

			ret = -EINVAL;
	}
	return(ret);
/*
 *	The load of calls that return an ifreq and ok (saves memory).
 */
rarok:
	err = copy_to_user(arg, &ifr, sizeof(struct ifreq));
	if (err)
		err = -EFAULT;
	return err;
}


/*
 *	This function handles all "interface"-type I/O control requests. The actual
 *	'doing' part of this is dev_ifsioc above.
 */

int dev_ioctl(unsigned int cmd, void *arg)
{
	switch(cmd) 
	{
		case SIOCGIFCONF:
			(void) dev_ifconf((char *) arg);
			return 0;

		/*
		 *	Ioctl calls that can be done by all.
		 */
		 
		case SIOCGIFFLAGS:
		case SIOCGIFMETRIC:
		case SIOCGIFMTU:
		case SIOCGIFMEM:
		case SIOCGIFHWADDR:
		case SIOCSIFHWADDR:
		case SIOCGIFSLAVE:
		case SIOCGIFMAP:
		case SIOGIFINDEX:
			return dev_ifsioc(arg, cmd);

		/*
		 *	Ioctl calls requiring the power of a superuser
		 */
		 
		case SIOCSIFFLAGS:
		case SIOCSIFMETRIC:
		case SIOCSIFMTU:
		case SIOCSIFMEM:
		case SIOCSIFMAP:
		case SIOCSIFSLAVE:
		case SIOCADDMULTI:
		case SIOCDELMULTI:
			if (!suser())
				return -EPERM;
			return dev_ifsioc(arg, cmd);
	
		case SIOCSIFLINK:
			return -EINVAL;

		/*
		 *	Unknown or private ioctl.
		 */	
		 
		default:
			if((cmd >= SIOCDEVPRIVATE) &&
			   (cmd <= (SIOCDEVPRIVATE + 15))) {
				return dev_ifsioc(arg, cmd);
			}
#ifdef CONFIG_NET_RADIO
			if((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST))
			{
				if((IW_IS_SET(cmd)) && (!suser()))
					return -EPERM;
				return dev_ifsioc(arg, cmd);
			}
#endif	/* CONFIG_NET_RADIO */
			return -EINVAL;
	}
}

int dev_new_index()
{
	static int ifindex;
	return ++ifindex;
}

/*
 *	Initialize the DEV module. At boot time this walks the device list and
 *	unhooks any devices that fail to initialise (normally hardware not 
 *	present) and leaves us with a valid list of present and active devices.
 *
 */
extern int lance_init(void);
extern int pi_init(void);
extern int bpq_init(void);
extern int scc_init(void);
extern void sdla_setup(void);
extern void dlci_setup(void);
extern int pt_init(void);
extern int sm_init(void);
extern int baycom_init(void);
extern int lapbeth_init(void);

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_net_dev = {
	PROC_NET_DEV, 3, "dev",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	dev_get_info
};
#endif

#ifdef CONFIG_NET_RADIO
#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_net_wireless = {
	PROC_NET_WIRELESS, 8, "wireless",
	S_IFREG | S_IRUGO, 1, 0, 0,
	0, &proc_net_inode_operations,
	dev_get_wireless_info
};
#endif	/* CONFIG_PROC_FS */
#endif	/* CONFIG_NET_RADIO */

int net_dev_init(void)
{
	struct device *dev, **dp;

	/*
	 *	Initialise the packet receive queue.
	 */
	 
	skb_queue_head_init(&backlog);
	
	/*
	 *	The bridge has to be up before the devices
	 */

#ifdef CONFIG_BRIDGE	 
	br_init();
#endif	
	
	/*
	 * This is Very Ugly(tm).
	 *
	 * Some devices want to be initialized early..
	 */
#if defined(CONFIG_LANCE)
	lance_init();
#endif
#if defined(CONFIG_PI)
	pi_init();
#endif	
#if defined(CONFIG_SCC)
	scc_init();
#endif
#if defined(CONFIG_PT)
	pt_init();
#endif
#if defined(CONFIG_BPQETHER)
	bpq_init();
#endif
#if defined(CONFIG_DLCI)
	dlci_setup();
#endif
#if defined(CONFIG_SDLA)
	sdla_setup();
#endif
#if defined(CONFIG_BAYCOM)
	baycom_init();
#endif
#if defined(CONFIG_SOUNDMODEM)
	sm_init();
#endif
#if defined(CONFIG_LAPBETHER)
	lapbeth_init();
#endif
#if defined(CONFIG_PLIP)
	plip_init();
#endif
	/*
	 *	SLHC if present needs attaching so other people see it
	 *	even if not opened.
	 */
#if (defined(CONFIG_SLIP) && defined(CONFIG_SLIP_COMPRESSED)) \
	 || defined(CONFIG_PPP) \
    || (defined(CONFIG_ISDN) && defined(CONFIG_ISDN_PPP))
	slhc_install();
#endif	

	/*
	 *	Add the devices.
	 *	If the call to dev->init fails, the dev is removed
	 *	from the chain disconnecting the device until the
	 *	next reboot.
	 */

	dp = &dev_base;
	while ((dev = *dp) != NULL)
	{
		int i;
		for (i = 0; i < DEV_NUMBUFFS; i++)  {
			skb_queue_head_init(dev->buffs + i);
		}

		if (dev->init && dev->init(dev)) 
		{
			/*
			 *	It failed to come up. Unhook it.
			 */
			*dp = dev->next;
		} 
		else
		{
			dp = &dev->next;
			dev->ifindex = dev_new_index();
		}
	}

#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_dev);
#endif

#ifdef CONFIG_NET_RADIO
#ifdef CONFIG_PROC_FS
	proc_net_register(&proc_net_wireless);
#endif	/* CONFIG_PROC_FS */
#endif	/* CONFIG_NET_RADIO */

	/*	
	 *	Initialise net_alias engine 
	 *
	 *		- register net_alias device notifier
	 *		- register proc entries:	/proc/net/alias_types
	 *									/proc/net/aliases
	 */

#ifdef CONFIG_NET_ALIAS
	net_alias_init();
#endif

	init_bh(NET_BH, net_bh);
	return 0;
}
