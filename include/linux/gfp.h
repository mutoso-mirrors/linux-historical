#ifndef __LINUX_GFP_H
#define __LINUX_GFP_H

#include <linux/mmzone.h>
#include <linux/stddef.h>
#include <linux/linkage.h>
/*
 * GFP bitmasks..
 */
/* Zone modifiers in GFP_ZONEMASK (see linux/mmzone.h - low four bits) */
#define __GFP_DMA	0x01
#define __GFP_HIGHMEM	0x02

/* Action modifiers - doesn't change the zoning */
#define __GFP_WAIT	0x10	/* Can wait and reschedule? */
#define __GFP_HIGH	0x20	/* Should access emergency pools? */
#define __GFP_IO	0x40	/* Can start low memory physical IO? */
#define __GFP_HIGHIO	0x80	/* Can start high mem physical IO? */
#define __GFP_FS	0x100	/* Can call down to low-level FS? */

#define GFP_NOHIGHIO	(             __GFP_WAIT | __GFP_IO)
#define GFP_NOIO	(             __GFP_WAIT)
#define GFP_NOFS	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO)
#define GFP_ATOMIC	(__GFP_HIGH)
#define GFP_USER	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)
#define GFP_HIGHUSER	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS | __GFP_HIGHMEM)
#define GFP_KERNEL	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)
#define GFP_KSWAPD	(             __GFP_WAIT | __GFP_IO | __GFP_HIGHIO | __GFP_FS)

/* Flag - indicates that the buffer will be suitable for DMA.  Ignored on some
   platforms, used as appropriate on others */

#define GFP_DMA		__GFP_DMA

/*
 * There is only one page-allocator function, and two main namespaces to
 * it. The alloc_page*() variants return 'struct page *' and as such
 * can allocate highmem pages, the *get*page*() variants return
 * virtual kernel addresses to the allocated page(s).
 */

/*
 * We get the zone list from the current node and the gfp_mask.
 * This zone list contains a maximum of MAXNODES*MAX_NR_ZONES zones.
 *
 * For the normal case of non-DISCONTIGMEM systems the NODE_DATA() gets
 * optimized to &contig_page_data at compile-time.
 */
extern struct page * FASTCALL(__alloc_pages(unsigned int, unsigned int, struct zonelist *));
static inline struct page * alloc_pages_node(int nid, unsigned int gfp_mask, unsigned int order)
{
	struct pglist_data *pgdat = NODE_DATA(nid);
	unsigned int idx = (gfp_mask & GFP_ZONEMASK);

	if (unlikely(order >= MAX_ORDER))
		return NULL;
	return __alloc_pages(gfp_mask, order, pgdat->node_zonelists + idx);
}
static inline struct page * alloc_pages(unsigned int gfp_mask, unsigned int order)
{
	struct pglist_data *pgdat = NODE_DATA(numa_node_id());
	unsigned int idx = (gfp_mask & GFP_ZONEMASK);

	if (unlikely(order >= MAX_ORDER))
		return NULL;
	return __alloc_pages(gfp_mask, order, pgdat->node_zonelists + idx);
}

#define alloc_page(gfp_mask) alloc_pages(gfp_mask, 0)

extern unsigned long FASTCALL(__get_free_pages(unsigned int gfp_mask, unsigned int order));
extern unsigned long FASTCALL(get_zeroed_page(unsigned int gfp_mask));

#define __get_free_page(gfp_mask) \
		__get_free_pages((gfp_mask),0)

#define __get_dma_pages(gfp_mask, order) \
		__get_free_pages((gfp_mask) | GFP_DMA,(order))

/*
 * There is only one 'core' page-freeing function.
 */
extern void FASTCALL(__free_pages(struct page *page, unsigned int order));
extern void FASTCALL(free_pages(unsigned long addr, unsigned int order));

#define __free_page(page) __free_pages((page), 0)
#define free_page(addr) free_pages((addr),0)

#endif /* __LINUX_GFP_H */
