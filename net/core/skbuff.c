/*
 *	Routines having to do with the 'struct sk_buff' memory handlers.
 *
 *	Authors:	Alan Cox <iiitac@pyr.swan.ac.uk>
 *			Florian La Roche <rzsfl@rz.uni-sb.de>
 *
 *	Version:	$Id: skbuff.c,v 1.69 2000/03/06 03:47:58 davem Exp $
 *
 *	Fixes:	
 *		Alan Cox	:	Fixed the worst of the load balancer bugs.
 *		Dave Platt	:	Interrupt stacking fix.
 *	Richard Kooijman	:	Timestamp fixes.
 *		Alan Cox	:	Changed buffer format.
 *		Alan Cox	:	destructor hook for AF_UNIX etc.
 *		Linus Torvalds	:	Better skb_clone.
 *		Alan Cox	:	Added skb_copy.
 *		Alan Cox	:	Added all the changed routines Linus
 *					only put in the headers
 *		Ray VanTassle	:	Fixed --skb->lock in free
 *		Alan Cox	:	skb_copy copy arp field
 *		Andi Kleen	:	slabified it.
 *
 *	NOTE:
 *		The __skb_ routines should be called with interrupts 
 *	disabled, or you better be *real* sure that the operation is atomic 
 *	with respect to whatever list is being frobbed (e.g. via lock_sock()
 *	or via disabling bottom half handlers, etc).
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

/*
 *	The functions in this file will not compile correctly with gcc 2.4.x
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/malloc.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/cache.h>
#include <linux/init.h>

#include <net/ip.h>
#include <net/protocol.h>
#include <net/dst.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/sock.h>

#include <asm/uaccess.h>
#include <asm/system.h>

int sysctl_hot_list_len = 128;

static kmem_cache_t *skbuff_head_cache;

static union {
	struct sk_buff_head	list;
	char			pad[SMP_CACHE_BYTES];
} skb_head_pool[NR_CPUS];

/*
 *	Keep out-of-line to prevent kernel bloat.
 *	__builtin_return_address is not used because it is not always
 *	reliable. 
 */

void skb_over_panic(struct sk_buff *skb, int sz, void *here)
{
	printk("skput:over: %p:%d put:%d dev:%s", 
		here, skb->len, sz, skb->dev ? skb->dev->name : "<NULL>");
	*(int*)0 = 0;
}

void skb_under_panic(struct sk_buff *skb, int sz, void *here)
{
        printk("skput:under: %p:%d put:%d dev:%s",
                here, skb->len, sz, skb->dev ? skb->dev->name : "<NULL>");
	*(int*)0 = 0;
}

static __inline__ struct sk_buff *skb_head_from_pool(void)
{
	struct sk_buff_head *list = &skb_head_pool[smp_processor_id()].list;

	if (skb_queue_len(list)) {
		struct sk_buff *skb;
		unsigned long flags;

		local_irq_save(flags);
		skb = __skb_dequeue(list);
		local_irq_restore(flags);
		return skb;
	}
	return NULL;
}

static __inline__ void skb_head_to_pool(struct sk_buff *skb)
{
	struct sk_buff_head *list = &skb_head_pool[smp_processor_id()].list;

	if (skb_queue_len(list) < sysctl_hot_list_len) {
		unsigned long flags;

		local_irq_save(flags);
		__skb_queue_head(list, skb);
		local_irq_restore(flags);

		return;
	}
	kmem_cache_free(skbuff_head_cache, skb);
}


/* 	Allocate a new skbuff. We do this ourselves so we can fill in a few
 *	'private' fields and also do memory statistics to find all the
 *	[BEEP] leaks.
 * 
 */

struct sk_buff *alloc_skb(unsigned int size,int gfp_mask)
{
	struct sk_buff *skb;
	u8 *data;

	if (in_interrupt() && (gfp_mask & __GFP_WAIT)) {
		static int count = 0;
		if (++count < 5) {
			printk(KERN_ERR "alloc_skb called nonatomically "
			       "from interrupt %p\n", NET_CALLER(size));
 			*(int*)0 = 0;
		}
		gfp_mask &= ~__GFP_WAIT;
	}

	/* Get the HEAD */
	skb = skb_head_from_pool();
	if (skb == NULL) {
		skb = kmem_cache_alloc(skbuff_head_cache, gfp_mask);
		if (skb == NULL)
			goto nohead;
	}

	/* Get the DATA. Size must match skb_add_mtu(). */
	size = ((size + 15) & ~15); 
	data = kmalloc(size + sizeof(atomic_t), gfp_mask);
	if (data == NULL)
		goto nodata;

	/* XXX: does not include slab overhead */ 
	skb->truesize = size + sizeof(struct sk_buff);

	/* Load the data pointers. */
	skb->head = data;
	skb->data = data;
	skb->tail = data;
	skb->end = data + size;

	/* Set up other state */
	skb->len = 0;
	skb->is_clone = 0;
	skb->cloned = 0;

	atomic_set(&skb->users, 1); 
	atomic_set(skb_datarefp(skb), 1);
	return skb;

nodata:
	skb_head_to_pool(skb);
nohead:
	return NULL;
}


/*
 *	Slab constructor for a skb head. 
 */ 
static inline void skb_headerinit(void *p, kmem_cache_t *cache, 
				  unsigned long flags)
{
	struct sk_buff *skb = p;

	skb->destructor = NULL;
	skb->pkt_type = PACKET_HOST;	/* Default type */
	skb->prev = skb->next = NULL;
	skb->list = NULL;
	skb->sk = NULL;
	skb->stamp.tv_sec=0;	/* No idea about time */
	skb->ip_summed = 0;
	skb->security = 0;	/* By default packets are insecure */
	skb->dst = NULL;
	skb->rx_dev = NULL;
#ifdef CONFIG_NETFILTER
	skb->nfmark = skb->nfreason = skb->nfcache = 0;
	skb->nfct = NULL;
#ifdef CONFIG_NETFILTER_DEBUG
	skb->nf_debug = 0;
#endif
#endif
#ifdef CONFIG_NET_SCHED
	skb->tc_index = 0;
#endif
	memset(skb->cb, 0, sizeof(skb->cb));
	skb->priority = 0;
}

/*
 *	Free an skbuff by memory without cleaning the state. 
 */
void kfree_skbmem(struct sk_buff *skb)
{
	if (!skb->cloned || atomic_dec_and_test(skb_datarefp(skb)))  
		kfree(skb->head);

	skb_head_to_pool(skb);
}

/*
 *	Free an sk_buff. Release anything attached to the buffer. Clean the state.
 */

void __kfree_skb(struct sk_buff *skb)
{
	if (skb->list) {
	 	printk(KERN_WARNING "Warning: kfree_skb passed an skb still "
		       "on a list (from %p).\n", NET_CALLER(skb));
		*(int*)0 = 0;
	}

	dst_release(skb->dst);
	if(skb->destructor) {
		if (in_irq()) {
			printk(KERN_WARNING "Warning: kfree_skb on hard IRQ %p\n",
				NET_CALLER(skb));
		}
		skb->destructor(skb);
	}
#ifdef CONFIG_NETFILTER
	nf_conntrack_put(skb->nfct);
#endif
#ifdef CONFIG_NET		
	if(skb->rx_dev)
		dev_put(skb->rx_dev);
#endif		
	skb_headerinit(skb, NULL, 0);  /* clean state */
	kfree_skbmem(skb);
}

/*
 *	Duplicate an sk_buff. The new one is not owned by a socket.
 */

struct sk_buff *skb_clone(struct sk_buff *skb, int gfp_mask)
{
	struct sk_buff *n;

	n = skb_head_from_pool();
	if (!n) {
		n = kmem_cache_alloc(skbuff_head_cache, gfp_mask);
		if (!n)
			return NULL;
	}

	memcpy(n, skb, sizeof(*n));
	atomic_inc(skb_datarefp(skb));
	skb->cloned = 1;
       
	dst_clone(n->dst);
	n->rx_dev = NULL;
	n->cloned = 1;
	n->next = n->prev = NULL;
	n->list = NULL;
	n->sk = NULL;
	n->is_clone = 1;
	atomic_set(&n->users, 1);
	n->destructor = NULL;
#ifdef CONFIG_NETFILTER
	nf_conntrack_get(skb->nfct);
#endif
	return n;
}

static void copy_skb_header(struct sk_buff *new, const struct sk_buff *old)
{
	/*
	 *	Shift between the two data areas in bytes
	 */
	unsigned long offset = new->data - old->data;

	new->list=NULL;
	new->sk=NULL;
	new->dev=old->dev;
	new->rx_dev=NULL;
	new->priority=old->priority;
	new->protocol=old->protocol;
	new->dst=dst_clone(old->dst);
	new->h.raw=old->h.raw+offset;
	new->nh.raw=old->nh.raw+offset;
	new->mac.raw=old->mac.raw+offset;
	memcpy(new->cb, old->cb, sizeof(old->cb));
	new->used=old->used;
	new->is_clone=0;
	atomic_set(&new->users, 1);
	new->pkt_type=old->pkt_type;
	new->stamp=old->stamp;
	new->destructor = NULL;
	new->security=old->security;
#ifdef CONFIG_NETFILTER
	new->nfmark=old->nfmark;
	new->nfreason=old->nfreason;
	new->nfcache=old->nfcache;
	new->nfct=old->nfct;
	nf_conntrack_get(new->nfct);
#ifdef CONFIG_NETFILTER_DEBUG
	new->nf_debug=old->nf_debug;
#endif
#endif
#ifdef CONFIG_NET_SCHED
	new->tc_index = old->tc_index;
#endif
}

/*
 *	This is slower, and copies the whole data area 
 */
 
struct sk_buff *skb_copy(const struct sk_buff *skb, int gfp_mask)
{
	struct sk_buff *n;

	/*
	 *	Allocate the copy buffer
	 */
	 
	n=alloc_skb(skb->end - skb->head, gfp_mask);
	if(n==NULL)
		return NULL;

	/* Set the data pointer */
	skb_reserve(n,skb->data-skb->head);
	/* Set the tail pointer and length */
	skb_put(n,skb->len);
	/* Copy the bytes */
	memcpy(n->head,skb->head,skb->end-skb->head);
	n->csum = skb->csum;
	copy_skb_header(n, skb);

	return n;
}

struct sk_buff *skb_copy_expand(const struct sk_buff *skb,
				int newheadroom,
				int newtailroom,
				int gfp_mask)
{
	struct sk_buff *n;

	/*
	 *	Allocate the copy buffer
	 */
 	 
	n=alloc_skb(newheadroom + (skb->tail - skb->data) + newtailroom,
		    gfp_mask);
	if(n==NULL)
		return NULL;

	skb_reserve(n,newheadroom);

	/* Set the tail pointer and length */
	skb_put(n,skb->len);

	/* Copy the data only. */
	memcpy(n->data, skb->data, skb->len);

	copy_skb_header(n, skb);
	return n;
}

#if 0
/* 
 * 	Tune the memory allocator for a new MTU size.
 */
void skb_add_mtu(int mtu)
{
	/* Must match allocation in alloc_skb */
	mtu = ((mtu + 15) & ~15) + sizeof(atomic_t);

	kmem_add_cache_size(mtu);
}
#endif

void __init skb_init(void)
{
	int i;

	skbuff_head_cache = kmem_cache_create("skbuff_head_cache",
					      sizeof(struct sk_buff),
					      0,
					      SLAB_HWCACHE_ALIGN,
					      skb_headerinit, NULL);
	if (!skbuff_head_cache)
		panic("cannot create skbuff cache");

	for (i=0; i<NR_CPUS; i++)
		skb_queue_head_init(&skb_head_pool[i].list);
}
