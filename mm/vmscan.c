/*
 *  linux/mm/vmscan.c
 *
 *  Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Swap reorganised 29.12.95, Stephen Tweedie.
 *  kswapd added: 7.1.96  sct
 *  Removed kswapd_ctl limits, and swap out as many pages as needed
 *  to bring the system back to freepages.high: 2.4.97, Rik van Riel.
 *  Version: $Id: vmscan.c,v 1.5 1998/02/23 22:14:28 sct Exp $
 *  Zone aware kswapd started 02/00, Kanoj Sarcar (kanoj@sgi.com).
 *  Multiqueue VM started 5.8.00, Rik van Riel.
 */

#include <linux/slab.h>
#include <linux/kernel_stat.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/pagemap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/file.h>

#include <asm/pgalloc.h>

/*
 * The swap-out functions return 1 if they successfully
 * threw something out, and we got a free page. It returns
 * zero if it couldn't do anything, and any other value
 * indicates it decreased rss, but the page was shared.
 *
 * NOTE! If it sleeps, it *must* return 1 to make sure we
 * don't continue with the swap-out. Otherwise we may be
 * using a process that no longer actually exists (it might
 * have died while we slept).
 */
static int try_to_swap_out(struct mm_struct * mm, struct vm_area_struct* vma, unsigned long address, pte_t * page_table, int gfp_mask)
{
	pte_t pte;
	swp_entry_t entry;
	struct page * page;
	int (*swapout)(struct page *, struct file *);
	int onlist;

	pte = *page_table;
	if (!pte_present(pte))
		goto out_failed;
	page = pte_page(pte);
	if ((!VALID_PAGE(page)) || PageReserved(page))
		goto out_failed;

	if (mm->swap_cnt)
		mm->swap_cnt--;

	onlist = PageActive(page);
	/* Don't look at this pte if it's been accessed recently. */
	if (pte_young(pte)) {
		set_pte(page_table, pte_mkold(pte));
		if (onlist) {
			/*
			 * Transfer the "accessed" bit from the page
			 * tables to the global page map. Page aging
			 * will be done by refill_inactive_scan().
			 */
                	SetPageReferenced(page);
		} else {
			/*
			 * The page is not on the active list, so
			 * we have to do the page aging ourselves.
			 */
			age_page_up(page);
		}
		goto out_failed;
	}
	if (!onlist)
		age_page_down(page);

	/*
	 * If the page is in active use by us, or if the page
	 * is in active use by others, don't unmap it or
	 * (worse) start unneeded IO.
	 */
	if (page->age > 0)
		goto out_failed;

	if (TryLockPage(page))
		goto out_failed;

	/*
	 * Is the page already in the swap cache? If so, then
	 * we can just drop our reference to it without doing
	 * any IO - it's already up-to-date on disk.
	 *
	 * Return 0, as we didn't actually free any real
	 * memory, and we should just continue our scan.
	 */
	if (PageSwapCache(page)) {
		entry.val = page->index;
		swap_duplicate(entry);
		set_pte(page_table, swp_entry_to_pte(entry));
drop_pte:
		UnlockPage(page);
		vma->vm_mm->rss--;
		flush_tlb_page(vma, address);
		deactivate_page(page);
		page_cache_release(page);
		goto out_failed;
	}

	/*
	 * Is it a clean page? Then it must be recoverable
	 * by just paging it in again, and we can just drop
	 * it..
	 *
	 * However, this won't actually free any real
	 * memory, as the page will just be in the page cache
	 * somewhere, and as such we should just continue
	 * our scan.
	 *
	 * Basically, this just makes it possible for us to do
	 * some real work in the future in "refill_inactive()".
	 */
	if (!pte_dirty(pte)) {
		flush_cache_page(vma, address);
		pte_clear(page_table);
		goto drop_pte;
	}

	/*
	 * Don't go down into the swap-out stuff if
	 * we cannot do I/O! Avoid recursing on FS
	 * locks etc.
	 */
	if (!(gfp_mask & __GFP_IO))
		goto out_unlock;

	/*
	 * Don't do any of the expensive stuff if
	 * we're not really interested in this zone.
	 */
	if (page->zone->free_pages + page->zone->inactive_clean_pages
					+ page->zone->inactive_dirty_pages
		      	> page->zone->pages_high + inactive_target)
		goto out_unlock;

	/*
	 * Ok, it's really dirty. That means that
	 * we should either create a new swap cache
	 * entry for it, or we should write it back
	 * to its own backing store.
	 *
	 * Note that in neither case do we actually
	 * know that we make a page available, but
	 * as we potentially sleep we can no longer
	 * continue scanning, so we migth as well
	 * assume we free'd something.
	 *
	 * NOTE NOTE NOTE! This should just set a
	 * dirty bit in 'page', and just drop the
	 * pte. All the hard work would be done by
	 * refill_inactive().
	 *
	 * That would get rid of a lot of problems.
	 */
	flush_cache_page(vma, address);
	if (vma->vm_ops && (swapout = vma->vm_ops->swapout)) {
		int error;
		struct file *file = vma->vm_file;
		if (file) get_file(file);
		pte_clear(page_table);
		vma->vm_mm->rss--;
		flush_tlb_page(vma, address);
		vmlist_access_unlock(vma->vm_mm);
		error = swapout(page, file);
		UnlockPage(page);
		if (file) fput(file);
		if (!error)
			goto out_free_success;
		deactivate_page(page);
		page_cache_release(page);
		return error;
	}

	/*
	 * This is a dirty, swappable page.  First of all,
	 * get a suitable swap entry for it, and make sure
	 * we have the swap cache set up to associate the
	 * page with that swap entry.
	 */
	entry = get_swap_page();
	if (!entry.val)
		goto out_unlock; /* No swap space left */

	if (!(page = prepare_highmem_swapout(page)))
		goto out_swap_free;

	swap_duplicate(entry);	/* One for the process, one for the swap cache */

	/* Add it to the swap cache */
	add_to_swap_cache(page, entry);

	/* Put the swap entry into the pte after the page is in swapcache */
	vma->vm_mm->rss--;
	set_pte(page_table, swp_entry_to_pte(entry));
	flush_tlb_page(vma, address);
	vmlist_access_unlock(vma->vm_mm);

	/* OK, do a physical asynchronous write to swap.  */
	rw_swap_page(WRITE, page, 0);
	deactivate_page(page);

out_free_success:
	page_cache_release(page);
	return 1;
out_swap_free:
	swap_free(entry);
out_failed:
	return 0;
out_unlock:
	UnlockPage(page);
	return 0;
}

/*
 * A new implementation of swap_out().  We do not swap complete processes,
 * but only a small number of blocks, before we continue with the next
 * process.  The number of blocks actually swapped is determined on the
 * number of page faults, that this process actually had in the last time,
 * so we won't swap heavily used processes all the time ...
 *
 * Note: the priority argument is a hint on much CPU to waste with the
 *       swap block search, not a hint, of how much blocks to swap with
 *       each process.
 *
 * (C) 1993 Kai Petzke, wpp@marie.physik.tu-berlin.de
 */

static inline int swap_out_pmd(struct mm_struct * mm, struct vm_area_struct * vma, pmd_t *dir, unsigned long address, unsigned long end, int gfp_mask)
{
	pte_t * pte;
	unsigned long pmd_end;

	if (pmd_none(*dir))
		return 0;
	if (pmd_bad(*dir)) {
		pmd_ERROR(*dir);
		pmd_clear(dir);
		return 0;
	}
	
	pte = pte_offset(dir, address);
	
	pmd_end = (address + PMD_SIZE) & PMD_MASK;
	if (end > pmd_end)
		end = pmd_end;

	do {
		int result;
		vma->vm_mm->swap_address = address + PAGE_SIZE;
		result = try_to_swap_out(mm, vma, address, pte, gfp_mask);
		if (result)
			return result;
		if (!mm->swap_cnt)
			return 0;
		address += PAGE_SIZE;
		pte++;
	} while (address && (address < end));
	return 0;
}

static inline int swap_out_pgd(struct mm_struct * mm, struct vm_area_struct * vma, pgd_t *dir, unsigned long address, unsigned long end, int gfp_mask)
{
	pmd_t * pmd;
	unsigned long pgd_end;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		pgd_ERROR(*dir);
		pgd_clear(dir);
		return 0;
	}

	pmd = pmd_offset(dir, address);

	pgd_end = (address + PGDIR_SIZE) & PGDIR_MASK;	
	if (pgd_end && (end > pgd_end))
		end = pgd_end;
	
	do {
		int result = swap_out_pmd(mm, vma, pmd, address, end, gfp_mask);
		if (result)
			return result;
		if (!mm->swap_cnt)
			return 0;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address && (address < end));
	return 0;
}

static int swap_out_vma(struct mm_struct * mm, struct vm_area_struct * vma, unsigned long address, int gfp_mask)
{
	pgd_t *pgdir;
	unsigned long end;

	/* Don't swap out areas which are locked down */
	if (vma->vm_flags & VM_LOCKED)
		return 0;

	pgdir = pgd_offset(vma->vm_mm, address);

	end = vma->vm_end;
	if (address >= end)
		BUG();
	do {
		int result = swap_out_pgd(mm, vma, pgdir, address, end, gfp_mask);
		if (result)
			return result;
		if (!mm->swap_cnt)
			return 0;
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	} while (address && (address < end));
	return 0;
}

static int swap_out_mm(struct mm_struct * mm, int gfp_mask)
{
	unsigned long address;
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	address = mm->swap_address;

	/*
	 * Find the proper vm-area after freezing the vma chain 
	 * and ptes.
	 */
	vmlist_access_lock(mm);
	vma = find_vma(mm, address);
	if (vma) {
		if (address < vma->vm_start)
			address = vma->vm_start;

		for (;;) {
			int result = swap_out_vma(mm, vma, address, gfp_mask);
			if (result)
				return result;
			if (!mm->swap_cnt)
				goto out_unlock;
			vma = vma->vm_next;
			if (!vma)
				break;
			address = vma->vm_start;
		}
	}
	/* Reset to 0 when we reach the end of address space */
	mm->swap_address = 0;
	mm->swap_cnt = 0;

out_unlock:
	vmlist_access_unlock(mm);

	/* We didn't find anything for the process */
	return 0;
}

/*
 * Select the task with maximal swap_cnt and try to swap out a page.
 * N.B. This function returns only 0 or 1.  Return values != 1 from
 * the lower level routines result in continued processing.
 */
#define SWAP_SHIFT 5
#define SWAP_MIN 8

static int swap_out(unsigned int priority, int gfp_mask, unsigned long idle_time)
{
	struct task_struct * p;
	int counter;
	int __ret = 0;

	lock_kernel();
	/* 
	 * We make one or two passes through the task list, indexed by 
	 * assign = {0, 1}:
	 *   Pass 1: select the swappable task with maximal RSS that has
	 *         not yet been swapped out. 
	 *   Pass 2: re-assign rss swap_cnt values, then select as above.
	 *
	 * With this approach, there's no need to remember the last task
	 * swapped out.  If the swap-out fails, we clear swap_cnt so the 
	 * task won't be selected again until all others have been tried.
	 *
	 * Think of swap_cnt as a "shadow rss" - it tells us which process
	 * we want to page out (always try largest first).
	 */
	counter = (nr_threads << SWAP_SHIFT) >> priority;
	if (counter < 1)
		counter = 1;

	for (; counter >= 0; counter--) {
		unsigned long max_cnt = 0;
		struct mm_struct *best = NULL;
		int pid = 0;
		int assign = 0;
		int found_task = 0;
	select:
		read_lock(&tasklist_lock);
		p = init_task.next_task;
		for (; p != &init_task; p = p->next_task) {
			struct mm_struct *mm = p->mm;
			if (!p->swappable || !mm)
				continue;
	 		if (mm->rss <= 0)
				continue;
			/* Skip tasks which haven't slept long enough yet when idle-swapping. */
			if (idle_time && !assign && (!(p->state & TASK_INTERRUPTIBLE) ||
					time_before(p->sleep_time + idle_time * HZ, jiffies)))
				continue;
			found_task++;
			/* Refresh swap_cnt? */
			if (assign == 1) {
				mm->swap_cnt = (mm->rss >> SWAP_SHIFT);
				if (mm->swap_cnt < SWAP_MIN)
					mm->swap_cnt = SWAP_MIN;
			}
			if (mm->swap_cnt > max_cnt) {
				max_cnt = mm->swap_cnt;
				best = mm;
				pid = p->pid;
			}
		}
		read_unlock(&tasklist_lock);
		if (!best) {
			if (!assign && found_task > 0) {
				assign = 1;
				goto select;
			}
			goto out;
		} else {
			int ret;

			atomic_inc(&best->mm_count);
			ret = swap_out_mm(best, gfp_mask);
			mmdrop(best);

			if (!ret)
				continue;

			if (ret < 0)
				kill_proc(pid, SIGBUS, 1);
			__ret = 1;
			goto out;
		}
	}
out:
	unlock_kernel();
	return __ret;
}


/**
 * reclaim_page -	reclaims one page from the inactive_clean list
 * @zone: reclaim a page from this zone
 *
 * The pages on the inactive_clean can be instantly reclaimed.
 * The tests look impressive, but most of the time we'll grab
 * the first page of the list and exit successfully.
 */
struct page * reclaim_page(zone_t * zone)
{
	struct page * page = NULL;
	struct list_head * page_lru;
	int maxscan;

	/*
	 * We only need the pagemap_lru_lock if we don't reclaim the page,
	 * but we have to grab the pagecache_lock before the pagemap_lru_lock
	 * to avoid deadlocks and most of the time we'll succeed anyway.
	 */
	spin_lock(&pagecache_lock);
	spin_lock(&pagemap_lru_lock);
	maxscan = zone->inactive_clean_pages;
	while ((page_lru = zone->inactive_clean_list.prev) !=
			&zone->inactive_clean_list && maxscan--) {
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		if (!PageInactiveClean(page)) {
			printk("VM: reclaim_page, wrong page on list.\n");
			list_del(page_lru);
			page->zone->inactive_clean_pages--;
			continue;
		}

		/* Page is or was in use?  Move it to the active list. */
		if (PageTestandClearReferenced(page) || page->age > 0 ||
				(!page->buffers && page_count(page) > 1)) {
			del_page_from_inactive_clean_list(page);
			add_page_to_active_list(page);
			continue;
		}

		/* The page is dirty, or locked, move to inactive_diry list. */
		if (page->buffers || TryLockPage(page)) {
			del_page_from_inactive_clean_list(page);
			add_page_to_inactive_dirty_list(page);
			continue;
		}

		/* OK, remove the page from the caches. */
                if (PageSwapCache(page)) {
			__delete_from_swap_cache(page);
			goto found_page;
		}

		if (page->mapping) {
			__remove_inode_page(page);
			goto found_page;
		}

		/* We should never ever get here. */
		printk(KERN_ERR "VM: reclaim_page, found unknown page\n");
		list_del(page_lru);
		zone->inactive_clean_pages--;
		UnlockPage(page);
	}
	/* Reset page pointer, maybe we encountered an unfreeable page. */
	page = NULL;
	goto out;

found_page:
	del_page_from_inactive_clean_list(page);
	UnlockPage(page);
	if (page_count(page) != 1)
		printk("VM: reclaim_page, found page with count %d!\n",
				page_count(page));
out:
	spin_unlock(&pagemap_lru_lock);
	spin_unlock(&pagecache_lock);
	memory_pressure++;
	return page;
}

/**
 * page_launder - clean dirty inactive pages, move to inactive_clean list
 * @gfp_mask: what operations we are allowed to do
 * @sync: should we wait synchronously for the cleaning of pages
 *
 * When this function is called, we are most likely low on free +
 * inactive_clean pages. Since we want to refill those pages as
 * soon as possible, we'll make two loops over the inactive list,
 * one to move the already cleaned pages to the inactive_clean lists
 * and one to (often asynchronously) clean the dirty inactive pages.
 *
 * In situations where kswapd cannot keep up, user processes will
 * end up calling this function. Since the user process needs to
 * have a page before it can continue with its allocation, we'll
 * do synchronous page flushing in that case.
 *
 * This code is heavily inspired by the FreeBSD source code. Thanks
 * go out to Matthew Dillon.
 */
#define MAX_SYNC_LAUNDER	(1 << page_cluster)
#define MAX_LAUNDER 		(MAX_SYNC_LAUNDER * 4)
int page_launder(int gfp_mask, int sync)
{
	int synclaunder, launder_loop, maxscan, cleaned_pages, maxlaunder;
	struct list_head * page_lru;
	struct page * page;

	launder_loop = 0;
	synclaunder = 0;
	maxlaunder = 0;
	cleaned_pages = 0;

	if (!(gfp_mask & __GFP_IO))
		return 0;

dirty_page_rescan:
	spin_lock(&pagemap_lru_lock);
	maxscan = nr_inactive_dirty_pages;
	while ((page_lru = inactive_dirty_list.prev) != &inactive_dirty_list &&
				maxscan-- > 0) {
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		if (!PageInactiveDirty(page)) {
			printk("VM: page_launder, wrong page on list.\n");
			list_del(page_lru);
			nr_inactive_dirty_pages--;
			page->zone->inactive_dirty_pages--;
			continue;
		}

		/* Page is or was in use?  Move it to the active list. */
		if (PageTestandClearReferenced(page) || page->age > 0 ||
				(!page->buffers && page_count(page) > 1) ||
				page_ramdisk(page)) {
			del_page_from_inactive_dirty_list(page);
			add_page_to_active_list(page);
			continue;
		}

		/*
		 * The page is locked. IO in progress?
		 * Move it to the back of the list.
		 */
		if (TryLockPage(page)) {
			list_del(page_lru);
			list_add(page_lru, &inactive_dirty_list);
			continue;
		}

		/*
		 * If the page has buffers, try to free the buffer mappings
		 * associated with this page. If we succeed we either free
		 * the page (in case it was a buffercache only page) or we
		 * move the page to the inactive_clean list.
		 *
		 * On the first round, we should free all previously cleaned
		 * buffer pages
		 */
		if (page->buffers) {
			int wait, clearedbuf;
			int freed_page = 0;
			/*
			 * Since we might be doing disk IO, we have to
			 * drop the spinlock and take an extra reference
			 * on the page so it doesn't go away from under us.
			 */
			del_page_from_inactive_dirty_list(page);
			page_cache_get(page);
			spin_unlock(&pagemap_lru_lock);

			/* Will we do (asynchronous) IO? */
			if (launder_loop && synclaunder-- > 0)
				wait = 2;	/* Synchrounous IO */
			else if (launder_loop && maxlaunder-- > 0)
				wait = 1;	/* Async IO */
			else
				wait = 0;	/* No IO */

			/* Try to free the page buffers. */
			clearedbuf = try_to_free_buffers(page, wait);

			/*
			 * Re-take the spinlock. Note that we cannot
			 * unlock the page yet since we're still
			 * accessing the page_struct here...
			 */
			spin_lock(&pagemap_lru_lock);

			/* The buffers were not freed. */
			if (!clearedbuf) {
				add_page_to_inactive_dirty_list(page);

			/* The page was only in the buffer cache. */
			} else if (!page->mapping) {
				atomic_dec(&buffermem_pages);
				freed_page = 1;
				cleaned_pages++;

			/* The page has more users besides the cache and us. */
			} else if (page_count(page) > 2) {
				add_page_to_active_list(page);

			/* OK, we "created" a freeable page. */
			} else /* page->mapping && page_count(page) == 2 */ {
				add_page_to_inactive_clean_list(page);
				cleaned_pages++;
			}

			/*
			 * Unlock the page and drop the extra reference.
			 * We can only do it here because we ar accessing
			 * the page struct above.
			 */
			UnlockPage(page);
			page_cache_release(page);

			/* 
			 * If we're freeing buffer cache pages, stop when
			 * we've got enough free memory.
			 */
			if (freed_page && !free_shortage())
				break;
			continue;
		} else if (page->mapping && !PageDirty(page)) {
			/*
			 * If a page had an extra reference in
			 * deactivate_page(), we will find it here.
			 * Now the page is really freeable, so we
			 * move it to the inactive_clean list.
			 */
			del_page_from_inactive_dirty_list(page);
			add_page_to_inactive_clean_list(page);
			UnlockPage(page);
			cleaned_pages++;
		} else {
			/*
			 * OK, we don't know what to do with the page.
			 * It's no use keeping it here, so we move it to
			 * the active list.
			 */
			del_page_from_inactive_dirty_list(page);
			add_page_to_active_list(page);
			UnlockPage(page);
		}
	}
	spin_unlock(&pagemap_lru_lock);

	/*
	 * If we don't have enough free pages, we loop back once
	 * to queue the dirty pages for writeout. When we were called
	 * by a user process (that /needs/ a free page) and we didn't
	 * free anything yet, we wait synchronously on the writeout of
	 * MAX_SYNC_LAUNDER pages.
	 *
	 * We also wake up bdflush, since bdflush should, under most
	 * loads, flush out the dirty pages before we have to wait on
	 * IO.
	 */
	if (!launder_loop && free_shortage()) {
		launder_loop = 1;
		if (sync && !cleaned_pages)
			synclaunder = MAX_SYNC_LAUNDER;
		/* We only do a few "out of order" flushes. */
		maxlaunder = MAX_LAUNDER;
		/* Kflushd takes care of the rest. */
		wakeup_bdflush(0);
		goto dirty_page_rescan;
	}

	/* Return the number of pages moved to the inactive_clean list. */
	return cleaned_pages;
}

/**
 * refill_inactive_scan - scan the active list and find pages to deactivate
 * @priority: the priority at which to scan
 * @oneshot: exit after deactivating one page
 *
 * This function will scan a portion of the active list to find
 * unused pages, those pages will then be moved to the inactive list.
 */
int refill_inactive_scan(unsigned int priority, int oneshot)
{
	struct list_head * page_lru;
	struct page * page;
	int maxscan, page_active = 0;
	int ret = 0;

	/* Take the lock while messing with the list... */
	spin_lock(&pagemap_lru_lock);
	maxscan = nr_active_pages >> priority;
	while (maxscan-- > 0 && (page_lru = active_list.prev) != &active_list) {
		page = list_entry(page_lru, struct page, lru);

		/* Wrong page on list?! (list corruption, should not happen) */
		if (!PageActive(page)) {
			printk("VM: refill_inactive, wrong page on list.\n");
			list_del(page_lru);
			nr_active_pages--;
			continue;
		}

		/* Do aging on the pages. */
		if (PageTestandClearReferenced(page)) {
			age_page_up_nolock(page);
			page_active = 1;
		} else {
			age_page_down_nolock(page);
			page_active = 0;
		}
		/*
		 * If the page is still on the active list, move it
		 * to the other end of the list. Otherwise it was
		 * deactivated by age_page_down and we exit successfully.
		 */
		if (page_active || PageActive(page)) {
			list_del(page_lru);
			list_add(page_lru, &active_list);
		} else {
			ret = 1;
			if (oneshot)
				break;
		}
	}
	spin_unlock(&pagemap_lru_lock);

	return ret;
}

/*
 * Check if there are zones with a severe shortage of free pages,
 * or if all zones have a minor shortage.
 */
int free_shortage(void)
{
	pg_data_t *pgdat = pgdat_list;
	int sum = 0;
	int freeable = nr_free_pages() + nr_inactive_clean_pages();

	/* Are we low on truly free pages? */
	if (nr_free_pages() < freepages.min)
		return freepages.high - nr_free_pages();

	/* Are we low on free pages over-all? */
	if (freeable < freepages.high)
		return freepages.high - freeable;

	/* If not, are we very low on any particular zone? */
	do {
		int i;
		for(i = 0; i < MAX_NR_ZONES; i++) {
			zone_t *zone = pgdat->node_zones+ i;
			if (zone->size && (zone->inactive_clean_pages +
					zone->free_pages < zone->pages_min)) {
				sum += zone->pages_min;
				sum -= zone->free_pages;
				sum -= zone->inactive_clean_pages;
			}
		}
		pgdat = pgdat->node_next;
	} while (pgdat);

	return sum;
}

/*
 * How many inactive pages are we short?
 */
int inactive_shortage(void)
{
	int shortage = 0;

	shortage += freepages.high;
	shortage += inactive_target;
	shortage -= nr_free_pages();
	shortage -= nr_inactive_clean_pages();
	shortage -= nr_inactive_dirty_pages;

	if (shortage > 0)
		return shortage;

	return 0;
}

/*
 * We need to make the locks finer granularity, but right
 * now we need this so that we can do page allocations
 * without holding the kernel lock etc.
 *
 * We want to try to free "count" pages, and we want to 
 * cluster them so that we get good swap-out behaviour.
 *
 * OTOH, if we're a user process (and not kswapd), we
 * really care about latency. In that case we don't try
 * to free too many pages.
 */
static int refill_inactive(unsigned int gfp_mask, int user)
{
	int priority, count, start_count, made_progress;
	unsigned long idle_time;

	count = inactive_shortage() + free_shortage();
	if (user)
		count = (1 << page_cluster);
	start_count = count;

	/* Always trim SLAB caches when memory gets low. */
	kmem_cache_reap(gfp_mask);

	/*
	 * Calculate the minimum time (in seconds) a process must
	 * have slept before we consider it for idle swapping.
	 * This must be the number of seconds it takes to go through
	 * all of the cache. Doing this idle swapping makes the VM
	 * smoother once we start hitting swap.
	 */
	idle_time = atomic_read(&page_cache_size);
	idle_time += atomic_read(&buffermem_pages);
	idle_time /= (inactive_target + 1);

	priority = 6;
	do {
		made_progress = 0;

		if (current->need_resched) {
			__set_current_state(TASK_RUNNING);
			schedule();
		}

		while (refill_inactive_scan(priority, 1) ||
				swap_out(priority, gfp_mask, idle_time)) {
			made_progress = 1;
			if (--count <= 0)
				goto done;
		}

		/*
		 * don't be too light against the d/i cache since
	   	 * refill_inactive() almost never fail when there's
	   	 * really plenty of memory free. 
		 */
		count -= shrink_dcache_memory(priority, gfp_mask);
		count -= shrink_icache_memory(priority, gfp_mask);
		/*
		 * Not currently working, see fixme in shrink_?cache_memory
		 * In the inner funtions there is a comment:
		 * "To help debugging, a zero exit status indicates
		 *  all slabs were released." (-arca?)
		 * lets handle it in a primitive but working way...
		 *	if (count <= 0)
		 *		goto done;
		 */

		/* Try to get rid of some shared memory pages.. */
		while (shm_swap(priority, gfp_mask)) {
			made_progress = 1;
			if (--count <= 0)
				goto done;
		}

		/*
		 * Then, try to page stuff out..
		 */
		while (swap_out(priority, gfp_mask, 0)) {
			made_progress = 1;
			if (--count <= 0)
				goto done;
		}

		/*
		 * If we either have enough free memory, or if
		 * page_launder() will be able to make enough
		 * free memory, then stop.
		 */
		if (!inactive_shortage() || !free_shortage())
			goto done;

		/*
		 * Only switch to a lower "priority" if we
		 * didn't make any useful progress in the
		 * last loop.
		 */
		if (!made_progress)
			priority--;
	} while (priority >= 0);

	/* Always end on a refill_inactive.., may sleep... */
	while (refill_inactive_scan(0, 1)) {
		if (--count <= 0)
			goto done;
	}

done:
	return (count < start_count);
}

static int do_try_to_free_pages(unsigned int gfp_mask, int user)
{
	int ret = 0;

	/*
	 * If we're low on free pages, move pages from the
	 * inactive_dirty list to the inactive_clean list.
	 *
	 * Usually bdflush will have pre-cleaned the pages
	 * before we get around to moving them to the other
	 * list, so this is a relatively cheap operation.
	 */
	if (free_shortage() || nr_inactive_dirty_pages > nr_free_pages() +
			nr_inactive_clean_pages())
		ret += page_launder(gfp_mask, user);

	/*
	 * If needed, we move pages from the active list
	 * to the inactive list. We also "eat" pages from
	 * the inode and dentry cache whenever we do this.
	 */
	if (free_shortage() || inactive_shortage()) {
		ret += shrink_dcache_memory(6, gfp_mask);
		ret += shrink_icache_memory(6, gfp_mask);
		ret += refill_inactive(gfp_mask, user);
	} else {
		/*
		 * Reclaim unused slab cache memory.
		 */
		kmem_cache_reap(gfp_mask);
		ret = 1;
	}

	return ret;
}

DECLARE_WAIT_QUEUE_HEAD(kswapd_wait);
DECLARE_WAIT_QUEUE_HEAD(kswapd_done);
struct task_struct *kswapd_task;

/*
 * The background pageout daemon, started as a kernel thread
 * from the init process. 
 *
 * This basically trickles out pages so that we have _some_
 * free memory available even if there is no other activity
 * that frees anything up. This is needed for things like routing
 * etc, where we otherwise might have all activity going on in
 * asynchronous contexts that cannot page things out.
 *
 * If there are applications that are active memory-allocators
 * (most normal use), this basically shouldn't matter.
 */
int kswapd(void *unused)
{
	struct task_struct *tsk = current;

	tsk->session = 1;
	tsk->pgrp = 1;
	strcpy(tsk->comm, "kswapd");
	sigfillset(&tsk->blocked);
	kswapd_task = tsk;
	
	/*
	 * Tell the memory management that we're a "memory allocator",
	 * and that if we need more memory we should get access to it
	 * regardless (see "__alloc_pages()"). "kswapd" should
	 * never get caught in the normal page freeing logic.
	 *
	 * (Kswapd normally doesn't need memory anyway, but sometimes
	 * you need a small amount of memory in order to be able to
	 * page out something else, and this flag essentially protects
	 * us from recursively trying to free more memory as we're
	 * trying to free the first piece of memory in the first place).
	 */
	tsk->flags |= PF_MEMALLOC;

	/*
	 * Kswapd main loop.
	 */
	for (;;) {
		static int recalc = 0;

		/* If needed, try to free some memory. */
		if (inactive_shortage() || free_shortage()) {
			int wait = 0;
			/* Do we need to do some synchronous flushing? */
			if (waitqueue_active(&kswapd_done))
				wait = 1;
			if (!do_try_to_free_pages(GFP_KSWAPD, wait)) {
				/*
				 * if (out_of_memory()) {
				 * 	try again a few times;
				 * 	oom_kill();
				 * }
				 */
			}
		}

		/*
		 * Do some (very minimal) background scanning. This
		 * will scan all pages on the active list once
		 * every minute. This clears old referenced bits
		 * and moves unused pages to the inactive list.
		 */
		refill_inactive_scan(6, 0);

		/* Once a second, recalculate some VM stats. */
		if (time_after(jiffies, recalc + HZ)) {
			recalc = jiffies;
			recalculate_vm_stats();
		}

		/*
		 * Wake up everybody waiting for free memory
		 * and unplug the disk queue.
		 */
		wake_up_all(&kswapd_done);
		run_task_queue(&tq_disk);

		/* 
		 * We go to sleep if either the free page shortage
		 * or the inactive page shortage is gone. We do this
		 * because:
		 * 1) we need no more free pages   or
		 * 2) the inactive pages need to be flushed to disk,
		 *    it wouldn't help to eat CPU time now ...
		 *
		 * We go to sleep for one second, but if it's needed
		 * we'll be woken up earlier...
		 */
		if (!free_shortage() || !inactive_shortage())
			interruptible_sleep_on_timeout(&kswapd_wait, HZ);
	}
}

void wakeup_kswapd(int block)
{
	DECLARE_WAITQUEUE(wait, current);

	if (current == kswapd_task)
		return;

	if (!block) {
		if (waitqueue_active(&kswapd_wait))
			wake_up(&kswapd_wait);
		return;
	}

	/*
	 * Kswapd could wake us up before we get a chance
	 * to sleep, so we have to be very careful here to
	 * prevent SMP races...
	 */
	__set_current_state(TASK_UNINTERRUPTIBLE);
	add_wait_queue(&kswapd_done, &wait);

	if (waitqueue_active(&kswapd_wait))
		wake_up(&kswapd_wait);
	schedule();

	remove_wait_queue(&kswapd_done, &wait);
	__set_current_state(TASK_RUNNING);
}

/*
 * Called by non-kswapd processes when they want more
 * memory.
 *
 * In a perfect world, this should just wake up kswapd
 * and return. We don't actually want to swap stuff out
 * from user processes, because the locking issues are
 * nasty to the extreme (file write locks, and MM locking)
 *
 * One option might be to let kswapd do all the page-out
 * and VM page table scanning that needs locking, and this
 * process thread could do just the mmap shrink stage that
 * can be done by just dropping cached pages without having
 * any deadlock issues.
 */
int try_to_free_pages(unsigned int gfp_mask)
{
	int ret = 1;

	if (gfp_mask & __GFP_WAIT)
		ret = do_try_to_free_pages(gfp_mask, 1);

	return ret;
}

DECLARE_WAIT_QUEUE_HEAD(kreclaimd_wait);
/*
 * Kreclaimd will move pages from the inactive_clean list to the
 * free list, in order to keep atomic allocations possible under
 * all circumstances. Even when kswapd is blocked on IO.
 */
int kreclaimd(void *unused)
{
	struct task_struct *tsk = current;
	pg_data_t *pgdat;

	tsk->session = 1;
	tsk->pgrp = 1;
	strcpy(tsk->comm, "kreclaimd");
	sigfillset(&tsk->blocked);
	current->flags |= PF_MEMALLOC;

	while (1) {

		/*
		 * We sleep until someone wakes us up from
		 * page_alloc.c::__alloc_pages().
		 */
		interruptible_sleep_on(&kreclaimd_wait);

		/*
		 * Move some pages from the inactive_clean lists to
		 * the free lists, if it is needed.
		 */
		pgdat = pgdat_list;
		do {
			int i;
			for(i = 0; i < MAX_NR_ZONES; i++) {
				zone_t *zone = pgdat->node_zones + i;
				if (!zone->size)
					continue;

				while (zone->free_pages < zone->pages_low) {
					struct page * page;
					page = reclaim_page(zone);
					if (!page)
						break;
					__free_page(page);
				}
			}
			pgdat = pgdat->node_next;
		} while (pgdat);
	}
}


static int __init kswapd_init(void)
{
	printk("Starting kswapd v1.8\n");
	swap_setup();
	kernel_thread(kswapd, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
	kernel_thread(kreclaimd, NULL, CLONE_FS | CLONE_FILES | CLONE_SIGNAL);
	return 0;
}

module_init(kswapd_init)
