/*
 *  linux/mm/swap_state.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *  Swap reorganised 29.12.95, Stephen Tweedie
 *
 *  Rewritten to use page cache, (C) 1998 Stephen Tweedie
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <linux/swapctl.h>
#include <linux/init.h>
#include <linux/pagemap.h>

#include <asm/bitops.h>
#include <asm/pgtable.h>

#ifdef SWAP_CACHE_INFO
unsigned long swap_cache_add_total = 0;
unsigned long swap_cache_add_success = 0;
unsigned long swap_cache_del_total = 0;
unsigned long swap_cache_del_success = 0;
unsigned long swap_cache_find_total = 0;
unsigned long swap_cache_find_success = 0;

/* 
 * Keep a reserved false inode which we will use to mark pages in the
 * page cache are acting as swap cache instead of file cache. 
 *
 * We only need a unique pointer to satisfy the page cache, but we'll
 * reserve an entire zeroed inode structure for the purpose just to
 * ensure that any mistaken dereferences of this structure cause a
 * kernel oops.
 */
struct inode swapper_inode;


void show_swap_cache_info(void)
{
	printk("Swap cache: add %ld/%ld, delete %ld/%ld, find %ld/%ld\n",
		swap_cache_add_total, swap_cache_add_success, 
		swap_cache_del_total, swap_cache_del_success,
		swap_cache_find_total, swap_cache_find_success);
}
#endif

int add_to_swap_cache(struct page *page, unsigned long entry)
{
	struct swap_info_struct * p = &swap_info[SWP_TYPE(entry)];

#ifdef SWAP_CACHE_INFO
	swap_cache_add_total++;
#endif
	if (PageLocked(page))
		panic("Adding page cache to locked page");
	if ((p->flags & SWP_WRITEOK) == SWP_WRITEOK) {
		if (PageTestandSetSwapCache(page))
			panic("swap_cache: replacing non-empty entry");
		if (page->inode)
			panic("swap_cache: replacing page-cached entry");
		atomic_inc(&page->count);
		page->inode = &swapper_inode;
		page->offset = entry;
		add_page_to_hash_queue(page, &swapper_inode, entry);
		add_page_to_inode_queue(&swapper_inode, page);
#ifdef SWAP_CACHE_INFO
		swap_cache_add_success++;
#endif
		return 1;
	}
	return 0;
}

/*
 * If swap_map[] reaches 127, the entries are treated as "permanent".
 */
void swap_duplicate(unsigned long entry)
{
	struct swap_info_struct * p;
	unsigned long offset, type;

	if (!entry)
		goto out;
	type = SWP_TYPE(entry);
	if (type & SHM_SWP_TYPE)
		goto out;
	if (type >= nr_swapfiles)
		goto bad_file;
	p = type + swap_info;
	offset = SWP_OFFSET(entry);
	if (offset >= p->max)
		goto bad_offset;
	if (!p->swap_map[offset])
		goto bad_unused;
	if (p->swap_map[offset] < 126)
		p->swap_map[offset]++;
	else {
		static int overflow = 0;
		if (overflow++ < 5)
			printk("swap_duplicate: entry %08lx map count=%d\n",
				entry, p->swap_map[offset]);
		p->swap_map[offset] = 127;
	}
out:
	return;

bad_file:
	printk("swap_duplicate: Trying to duplicate nonexistent swap-page\n");
	goto out;
bad_offset:
	printk("swap_duplicate: offset exceeds max\n");
	goto out;
bad_unused:
	printk("swap_duplicate: unused page\n");
	goto out;
}


void remove_from_swap_cache(struct page *page)
{
	if (!page->inode)
		panic ("Removing swap cache page with zero inode hash");
	if (page->inode != &swapper_inode)
		panic ("Removing swap cache page with wrong inode hash");
	if (PageLocked(page))
		panic ("Removing swap cache from locked page");
	/*
	 * This will be a legal case once we have a more mature swap cache.
	 */
	if (atomic_read(&page->count) == 1)
		panic ("Removing page cache on unshared page");
	
	remove_page_from_hash_queue (page);
	remove_page_from_inode_queue (page);
	PageClearSwapCache (page);
	__free_page (page);
}


long find_in_swap_cache(struct page *page)
{
#ifdef SWAP_CACHE_INFO
	swap_cache_find_total++;
#endif
	if (PageSwapCache (page))  {
		long entry = page->offset;
#ifdef SWAP_CACHE_INFO
		swap_cache_find_success++;
#endif	
		remove_from_swap_cache (page);
		return entry;
	}
	return 0;
}

int delete_from_swap_cache(struct page *page)
{
#ifdef SWAP_CACHE_INFO
	swap_cache_del_total++;
#endif	
	if (PageSwapCache (page))  {
		long entry = page->offset;
#ifdef SWAP_CACHE_INFO
		swap_cache_del_success++;
#endif
		remove_from_swap_cache (page);
		swap_free (entry);
		return 1;
	}
	return 0;
}

/* 
 * Perform a free_page(), also freeing any swap cache associated with
 * this page if it is the last user of the page. 
 */

void free_page_and_swap_cache(unsigned long addr)
{
	struct page *page = mem_map + MAP_NR(addr);
	/* 
	 * If we are the only user, then free up the swap cache. 
	 */
	if (PageSwapCache(page) && !is_page_shared(page))
		delete_from_swap_cache(page);
	
	free_page(addr);
}
