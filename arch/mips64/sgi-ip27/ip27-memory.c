/* $Id: ip27-memory.c,v 1.9 2000/02/10 09:07:31 kanoj Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000 by Ralf Baechle
 * Copyright (C) 2000 by Silicon Graphics, Inc.
 *
 * On SGI IP27 the ARC memory configuration data is completly bogus but
 * alternate easier to use mechanisms are available.
 */
#include <linux/init.h>
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/swap.h>

#include <asm/page.h>
#include <asm/bootinfo.h>
#include <asm/addrspace.h>
#include <asm/pgtable.h>
#include <asm/sn/types.h>
#include <asm/sn/addrs.h>
#include <asm/sn/klconfig.h>
#include <asm/sn/arch.h>
#include <asm/mmzone.h>

typedef unsigned long pfn_t;			/* into <asm/sn/types.h> */
#define KDM_TO_PHYS(x)	((x) & TO_PHYS_MASK)	/* into asm/addrspace.h */

extern char _end;

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define SLOT_IGNORED	0xffff

short slot_lastfilled_cache[MAX_COMPACT_NODES];
unsigned short slot_psize_cache[MAX_COMPACT_NODES][MAX_MEM_SLOTS];
static pfn_t numpages;
static pfn_t pagenr = 0;

plat_pg_data_t *plat_node_data[MAX_COMPACT_NODES];
bootmem_data_t plat_node_bdata[MAX_COMPACT_NODES];

int numa_debug(void)
{
	printk("NUMA debug\n");
	*(int *)0 = 0;
	return(0);
}

/*
 * Return pfn of first free page of memory on a node. PROM may allocate
 * data structures on the first couple of pages of the first slot of each 
 * node. If this is the case, getfirstfree(node) > getslotstart(node, 0).
 */
pfn_t node_getfirstfree(cnodeid_t cnode)
{
#ifdef LATER
	nasid_t nasid = COMPACT_TO_NASID_NODEID(cnode);

	if (cnode == 0)
		return KDM_TO_PHYS((unsigned long)(&_end));
	return KDM_TO_PHYS(SYMMON_STK_ADDR(nasid, 0));
#endif
	if (cnode == 0)
		return (KDM_TO_PHYS(PAGE_ALIGN((unsigned long)(&_end)) - 
					(CKSEG0 - K0BASE)) >> PAGE_SHIFT);
	return slot_getbasepfn(cnode, 0);
}

/*
 * Return the number of pages of memory provided by the given slot
 * on the specified node.
 */
pfn_t slot_getsize(cnodeid_t node, int slot)
{
	return (pfn_t) slot_psize_cache[node][slot];
}

/*
 * Return highest slot filled
 */
int node_getlastslot(cnodeid_t node)
{
	return (int) slot_lastfilled_cache[node];
}

/*
 * Return the pfn of the last free page of memory on a node.
 */
pfn_t node_getmaxclick(cnodeid_t node)
{
	pfn_t	slot_psize;
	int	slot;

	/*
	 * Start at the top slot. When we find a slot with memory in it,
	 * that's the winner.
	 */
	for (slot = (node_getnumslots(node) - 1); slot >= 0; slot--) {
		if ((slot_psize = slot_getsize(node, slot))) {
			if (slot_psize == SLOT_IGNORED)
				continue;
			/* Return the basepfn + the slot size, minus 1. */
			return slot_getbasepfn(node, slot) + slot_psize - 1;
		}
	}

	/*
	 * If there's no memory on the node, return 0. This is likely
	 * to cause problems.
	 */
	return (pfn_t)0;
}

static pfn_t slot_psize_compute(cnodeid_t node, int slot)
{
	nasid_t nasid;
	lboard_t *brd;
	klmembnk_t *banks;
	unsigned long size;

	nasid = COMPACT_TO_NASID_NODEID(node);
	/* Find the node board */
	brd = find_lboard_real((lboard_t *)KL_CONFIG_INFO(nasid), KLTYPE_IP27);
	if (!brd)
		return 0;

	/* Get the memory bank structure */
	banks = (klmembnk_t *)find_first_component(brd, KLSTRUCT_MEMBNK);
	if (!banks)
		return 0;

	/* Size in _Megabytes_ */
	size = (unsigned long)banks->membnk_bnksz[slot/4];

	/* hack for 128 dimm banks */
	if (size <= 128) {
		if (slot%4 == 0) {
			size <<= 20;		/* size in bytes */
			return(size >> PAGE_SHIFT);
		} else {
			return 0;
		}
	} else {
		size /= 4;
		size <<= 20;
		return(size >> PAGE_SHIFT);
	}
}

pfn_t szmem(pfn_t fpage, pfn_t maxpmem)
{
	cnodeid_t node;
	int slot, numslots;
	pfn_t num_pages = 0, slot_psize;
	pfn_t slot0sz = 0, nodebytes;	/* Hack to detect problem configs */
	int ignore;

	for (node = 0; node < numnodes; node++) {
		numslots = node_getnumslots(node);
		ignore = nodebytes = 0;
		for (slot = 0; slot < numslots; slot++) {
			slot_psize = slot_psize_compute(node, slot);
			if (slot == 0) slot0sz = slot_psize;
			/* 
			 * We need to refine the hack when we have replicated
			 * kernel text.
			 */
			nodebytes += SLOT_SIZE;
			if ((nodebytes >> PAGE_SHIFT) * (sizeof(struct page)) >
						(slot0sz << PAGE_SHIFT))
				ignore = 1;
			if (ignore && slot_psize) {
				printk("Ignoring slot %d onwards on node %d\n",
								slot, node);
				slot_psize_cache[node][slot] = SLOT_IGNORED;
				slot = numslots;
				continue;
			}
			num_pages += slot_psize;
			slot_psize_cache[node][slot] = 
					(unsigned short) slot_psize;
			if (slot_psize)
				slot_lastfilled_cache[node] = slot;
		}
	}
	if (maxpmem)
		return((maxpmem > num_pages) ? num_pages : maxpmem);
	else
		return num_pages;
}

/*
 * HACK ALERT - Things do not work if this is not here. Maybe this is
 * acting as a pseudocacheflush operation. The write pattern seems to
 * be important, writing a 0 does not help.
 */
void setup_test(cnodeid_t node, pfn_t start, pfn_t end)
{
	unsigned long *ptr = __va(start << PAGE_SHIFT);
	unsigned long size = 4 * 1024 * 1024;	/* 4M L2 caches */

	while (size) {
		size -= sizeof(unsigned long);
		*ptr = (0xdeadbeefbabeb000UL|node);
		/* *ptr = 0; */
		ptr++;
	}
}

/*
 * Currently, the intranode memory hole support assumes that each slot
 * contains at least 32 MBytes of memory. We assume all bootmem data
 * fits on the first slot.
 */
void __init prom_meminit(void)
{
	extern void mlreset(void);
	cnodeid_t node;
	pfn_t slot_firstpfn, slot_lastpfn, slot_freepfn;
	unsigned long bootmap_size;
	int node_datasz;

	node_datasz = PFN_UP(sizeof(plat_pg_data_t));
	mlreset();
	numpages = szmem(0, 0);
	for (node = (numnodes - 1); node >= 0; node--) {
		slot_firstpfn = slot_getbasepfn(node, 0);
		slot_lastpfn = slot_firstpfn + slot_getsize(node, 0);
		slot_freepfn = node_getfirstfree(node);
		/* Foll line hack for non discontigmem; remove once discontigmem
		 * becomes the default. */
		max_low_pfn = (slot_lastpfn - slot_firstpfn);
		if (node != 0) 
			setup_test(node, slot_freepfn, slot_lastpfn);
		/*
		 * Allocate the node data structure on the node first.
		 */
		plat_node_data[node] = (plat_pg_data_t *)(__va(slot_freepfn \
							<< PAGE_SHIFT));
		NODE_DATA(node)->bdata = plat_node_bdata + node;
		slot_freepfn += node_datasz;
	  	bootmap_size = init_bootmem_node(node, slot_freepfn, 
						slot_firstpfn, slot_lastpfn);
		free_bootmem_node(node, slot_firstpfn << PAGE_SHIFT, 
				(slot_lastpfn - slot_firstpfn) << PAGE_SHIFT);
		reserve_bootmem_node(node, slot_firstpfn << PAGE_SHIFT,
		  ((slot_freepfn - slot_firstpfn) << PAGE_SHIFT) + bootmap_size);
	}
	printk("Total memory probed : 0x%lx pages\n", numpages);
}

int __init page_is_ram(unsigned long pagenr)
{
        return 1;
}

void __init
prom_free_prom_memory (void)
{
	/* We got nothing to free here ...  */
}

#ifdef CONFIG_DISCONTIGMEM

void __init paging_init(void)
{
	cnodeid_t node;
	unsigned int zones_size[MAX_NR_ZONES] = {0, 0, 0};

	/* Initialize the entire pgd.  */
	pgd_init((unsigned long)swapper_pg_dir);
	pgd_init((unsigned long)swapper_pg_dir + PAGE_SIZE / 2);
	pmd_init((unsigned long)invalid_pmd_table);

	for (node = 0; node < numnodes; node++) {
		pfn_t start_pfn = slot_getbasepfn(node, 0);
		pfn_t end_pfn = node_getmaxclick(node);

		zones_size[ZONE_DMA] = end_pfn + 1 - start_pfn;
		PLAT_NODE_DATA(node)->physstart = (start_pfn << PAGE_SHIFT);
		PLAT_NODE_DATA(node)->size = (zones_size[ZONE_DMA] << PAGE_SHIFT);
		free_area_init_node(node, NODE_DATA(node), zones_size, 
						start_pfn << PAGE_SHIFT, 0);
		PLAT_NODE_DATA(node)->start_mapnr = 
					(NODE_DATA(node)->node_mem_map - mem_map);
		if ((PLAT_NODE_DATA(node)->start_mapnr + 
					PLAT_NODE_DATA(node)->size) > pagenr)
			pagenr = PLAT_NODE_DATA(node)->start_mapnr +
					PLAT_NODE_DATA(node)->size;
	}
}

void __init mem_init(void)
{
	extern char _ftext, _etext, _fdata, _edata;
	extern char __init_begin, __init_end;
	extern unsigned long totalram_pages;
	extern unsigned long setup_zero_pages(void);
	cnodeid_t nid;
	unsigned long tmp, ram;
	unsigned long codesize, reservedpages, datasize, initsize;
	int slot, numslots;
	struct page *pg, *pslot;
	pfn_t pgnr;

	num_physpages = numpages;	/* memory already sized by szmem */
	max_mapnr = pagenr;		/* already found during paging_init */
	high_memory = (void *) __va(max_mapnr << PAGE_SHIFT);

	for (nid = 0; nid < numnodes; nid++) {

		/*
	 	 * This will free up the bootmem, ie, slot 0 memory.
	 	 */
		totalram_pages += free_all_bootmem_node(nid);

		/*
		 * We need to manually do the other slots.
		 */
		pg = NODE_DATA(nid)->node_mem_map + slot_getsize(nid, 0);
		pgnr = PLAT_NODE_DATA(nid)->start_mapnr + slot_getsize(nid, 0);
		numslots = node_getlastslot(nid);
		for (slot = 1; slot <= numslots; slot++) {
			pslot = NODE_DATA(nid)->node_mem_map + 
			   slot_getbasepfn(nid, slot) - slot_getbasepfn(nid, 0);

			/*
			 * Mark holes in previous slot. May also want to
			 * free up the pages that hold the memmap entries.
			 */
			while (pg < pslot) {
				pg->flags |= (1<<PG_skip);
				pg++; pgnr++;
			}

			/*
			 * Free valid memory in current slot.
			 */
			pslot += slot_getsize(nid, slot);
			while (pg < pslot) {
				if (!page_is_ram(pgnr))
					continue;
				ClearPageReserved(pg);
				atomic_set(&pg->count, 1);
				__free_page(pg);
				totalram_pages++;
				pg++; pgnr++;
			}
		}
	}

	totalram_pages -= setup_zero_pages();	/* This comes from node 0 */

	reservedpages = ram = 0;
	for (nid = 0; nid < numnodes; nid++) {
		for (tmp = PLAT_NODE_DATA(nid)->start_mapnr; tmp < 
			((PLAT_NODE_DATA(nid)->start_mapnr) + 
			(PLAT_NODE_DATA(nid)->size >> PAGE_SHIFT)); tmp++) {
			/* Ignore holes */
			if (PageSkip(mem_map+tmp))
				continue;
			if (page_is_ram(tmp)) {
				ram++;
				if (PageReserved(mem_map+tmp))
					reservedpages++;
			}
		}
	}

	codesize =  (unsigned long) &_etext - (unsigned long) &_ftext;
	datasize =  (unsigned long) &_edata - (unsigned long) &_fdata;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk("Memory: %luk/%luk available (%ldk kernel code, %ldk reserved, "
		"%ldk data, %ldk init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		ram << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		initsize >> 10);
}

#endif /* CONFIG_DISCONTIGMEM */
