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
 */

#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/swap.h>
#include <linux/swapctl.h>
#include <linux/smp_lock.h>
#include <linux/slab.h>
#include <linux/dcache.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

#include <asm/bitops.h>
#include <asm/pgtable.h>

/* 
 * When are we next due for a page scan? 
 */
static unsigned long next_swap_jiffies = 0;

/* 
 * How often do we do a pageout scan during normal conditions?
 * Default is four times a second.
 */
int swapout_interval = HZ / 4;

/* 
 * The wait queue for waking up the pageout daemon:
 */
static struct wait_queue * kswapd_wait = NULL;

static void init_swap_timer(void);

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
static inline int try_to_swap_out(struct task_struct * tsk, struct vm_area_struct* vma,
	unsigned long address, pte_t * page_table, int gfp_mask)
{
	pte_t pte;
	unsigned long entry;
	unsigned long page;
	struct page * page_map;

	pte = *page_table;
	if (!pte_present(pte))
		return 0;
	page = pte_page(pte);
	if (MAP_NR(page) >= max_mapnr)
		return 0;

	page_map = mem_map + MAP_NR(page);
	if (PageReserved(page_map)
	    || PageLocked(page_map)
	    || ((gfp_mask & __GFP_DMA) && !PageDMA(page_map)))
		return 0;

	/* 
	 * Deal with page aging.  There are several special cases to
	 * consider:
	 * 
	 * Page has been accessed, but is swap cached.  If the page is
	 * getting sufficiently "interesting" --- its age is getting
	 * high --- then if we are sufficiently short of free swap
	 * pages, then delete the swap cache.  We can only do this if
	 * the swap page's reference count is one: ie. there are no
	 * other references to it beyond the swap cache (as there must
	 * still be pte's pointing to it if count > 1).
	 * 
	 * If the page has NOT been touched, and its age reaches zero,
	 * then we are swapping it out:
	 *
	 *   If there is already a swap cache page for this page, then
	 *   another process has already allocated swap space, so just
	 *   dereference the physical page and copy in the swap entry
	 *   from the swap cache.  
	 * 
	 * Note, we rely on all pages read in from swap either having
	 * the swap cache flag set, OR being marked writable in the pte,
	 * but NEVER BOTH.  (It IS legal to be neither cached nor dirty,
	 * however.)
	 *
	 * -- Stephen Tweedie 1998 */

	if (PageSwapCache(page_map)) {
		if (pte_write(pte)) {
			struct page *found;
			printk ("VM: Found a writable swap-cached page!\n");
			/* Try to diagnose the problem ... */
			found = find_page(&swapper_inode, page_map->offset);
			if (found) {
				printk("page=%p@%08lx, found=%p, count=%d\n",
					page_map, page_map->offset,
					found, atomic_read(&found->count));
				__free_page(found);
			} else 
				printk ("Spurious, page not in cache\n");
			return 0;
		}
	}
	
	if (pte_young(pte)) {
		set_pte(page_table, pte_mkold(pte));
		touch_page(page_map);
		/* 
		 * We should test here to see if we want to recover any
		 * swap cache page here.  We do this if the page seeing
		 * enough activity, AND we are sufficiently low on swap
		 *
		 * We need to track both the number of available swap
		 * pages and the total number present before we can do
		 * this...  
		 */
		return 0;
	}

	age_page(page_map);
	if (page_map->age)
		return 0;

	if (pte_dirty(pte)) {
		if (vma->vm_ops && vma->vm_ops->swapout) {
			pid_t pid = tsk->pid;
			vma->vm_mm->rss--;
			if (vma->vm_ops->swapout(vma, address - vma->vm_start + vma->vm_offset, page_table))
				kill_proc(pid, SIGBUS, 1);
		} else {
			/*
			 * This is a dirty, swappable page.  First of all,
			 * get a suitable swap entry for it, and make sure
			 * we have the swap cache set up to associate the
			 * page with that swap entry.
			 */
        		entry = in_swap_cache(page_map);
			if (!entry) {
				entry = get_swap_page();
				if (!entry)
					return 0; /* No swap space left */
			}
			
			vma->vm_mm->rss--;
			tsk->nswap++;
			flush_cache_page(vma, address);
			set_pte(page_table, __pte(entry));
			flush_tlb_page(vma, address);
			swap_duplicate(entry);

			/* Now to write back the page.  We have two
			 * cases: if the page is already part of the
			 * swap cache, then it is already on disk.  Just
			 * free the page and return (we release the swap
			 * cache on the last accessor too).
			 *
			 * If we have made a new swap entry, then we
			 * start the write out to disk.  If the page is
			 * shared, however, we still need to keep the
			 * copy in memory, so we add it to the swap
			 * cache. */
			if (PageSwapCache(page_map)) {
				free_page_and_swap_cache(page);
				return (atomic_read(&page_map->count) == 0);
			}
			add_to_swap_cache(page_map, entry);
			/* We checked we were unlocked way up above, and we
			   have been careful not to stall until here */
			set_bit(PG_locked, &page_map->flags);
			/* OK, do a physical write to swap.  */
			rw_swap_page(WRITE, entry, (char *) page, (gfp_mask & __GFP_WAIT));
		}
		/* Now we can free the current physical page.  We also
		 * free up the swap cache if this is the last use of the
		 * page.  Note that there is a race here: the page may
		 * still be shared COW by another process, but that
		 * process may exit while we are writing out the page
		 * asynchronously.  That's no problem, shrink_mmap() can
		 * correctly clean up the occassional unshared page
		 * which gets left behind in the swap cache. */
		free_page_and_swap_cache(page);
		return 1;	/* we slept: the process may not exist any more */
	}

	/* The page was _not_ dirty, but still has a zero age.  It must
	 * already be uptodate on disk.  If it is in the swap cache,
	 * then we can just unlink the page now.  Remove the swap cache
	 * too if this is the last user.  */
        if ((entry = in_swap_cache(page_map)))  {
		vma->vm_mm->rss--;
		flush_cache_page(vma, address);
		set_pte(page_table, __pte(entry));
		flush_tlb_page(vma, address);
		swap_duplicate(entry);
		free_page_and_swap_cache(page);
		return (atomic_read(&page_map->count) == 0);
	} 
	/* 
	 * A clean page to be discarded?  Must be mmap()ed from
	 * somewhere.  Unlink the pte, and tell the filemap code to
	 * discard any cached backing page if this is the last user.
	 */
	if (PageSwapCache(page_map)) {
		printk ("VM: How can this page _still_ be cached?");
		return 0;
	}
	vma->vm_mm->rss--;
	flush_cache_page(vma, address);
	pte_clear(page_table);
	flush_tlb_page(vma, address);
	entry = page_unuse(page_map);
	__free_page(page_map);
	return entry;
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

static inline int swap_out_pmd(struct task_struct * tsk, struct vm_area_struct * vma,
	pmd_t *dir, unsigned long address, unsigned long end, int gfp_mask)
{
	pte_t * pte;
	unsigned long pmd_end;

	if (pmd_none(*dir))
		return 0;
	if (pmd_bad(*dir)) {
		printk("swap_out_pmd: bad pmd (%08lx)\n", pmd_val(*dir));
		pmd_clear(dir);
		return 0;
	}
	
	pte = pte_offset(dir, address);
	
	pmd_end = (address + PMD_SIZE) & PMD_MASK;
	if (end > pmd_end)
		end = pmd_end;

	do {
		int result;
		tsk->swap_address = address + PAGE_SIZE;
		result = try_to_swap_out(tsk, vma, address, pte, gfp_mask);
		if (result)
			return result;
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
	return 0;
}

static inline int swap_out_pgd(struct task_struct * tsk, struct vm_area_struct * vma,
	pgd_t *dir, unsigned long address, unsigned long end, int gfp_mask)
{
	pmd_t * pmd;
	unsigned long pgd_end;

	if (pgd_none(*dir))
		return 0;
	if (pgd_bad(*dir)) {
		printk("swap_out_pgd: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return 0;
	}

	pmd = pmd_offset(dir, address);

	pgd_end = (address + PGDIR_SIZE) & PGDIR_MASK;	
	if (end > pgd_end)
		end = pgd_end;
	
	do {
		int result = swap_out_pmd(tsk, vma, pmd, address, end, gfp_mask);
		if (result)
			return result;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
	return 0;
}

static int swap_out_vma(struct task_struct * tsk, struct vm_area_struct * vma,
	pgd_t *pgdir, unsigned long start, int gfp_mask)
{
	unsigned long end;

	/* Don't swap out areas like shared memory which have their
	    own separate swapping mechanism or areas which are locked down */
	if (vma->vm_flags & (VM_SHM | VM_LOCKED))
		return 0;

	end = vma->vm_end;
	while (start < end) {
		int result = swap_out_pgd(tsk, vma, pgdir, start, end, gfp_mask);
		if (result)
			return result;
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	}
	return 0;
}

static int swap_out_process(struct task_struct * p, int gfp_mask)
{
	unsigned long address;
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	address = p->swap_address;

	/*
	 * Find the proper vm-area
	 */
	vma = find_vma(p->mm, address);
	if (!vma) {
		p->swap_address = 0;
		return 0;
	}
	if (address < vma->vm_start)
		address = vma->vm_start;

	for (;;) {
		int result = swap_out_vma(p, vma, pgd_offset(p->mm, address), address, gfp_mask);
		if (result)
			return result;
		vma = vma->vm_next;
		if (!vma)
			break;
		address = vma->vm_start;
	}
	p->swap_address = 0;
	return 0;
}

/*
 * Select the task with maximal swap_cnt and try to swap out a page.
 * N.B. This function returns only 0 or 1.  Return values != 1 from
 * the lower level routines result in continued processing.
 */
static int swap_out(unsigned int priority, int gfp_mask)
{
	struct task_struct * p, * pbest;
	int counter, assign, max_cnt;

	/* 
	 * We make one or two passes through the task list, indexed by 
	 * assign = {0, 1}:
	 *   Pass 1: select the swappable task with maximal swap_cnt.
	 *   Pass 2: assign new swap_cnt values, then select as above.
	 * With this approach, there's no need to remember the last task
	 * swapped out.  If the swap-out fails, we clear swap_cnt so the 
	 * task won't be selected again until all others have been tried.
	 */
	counter = ((PAGEOUT_WEIGHT * nr_tasks) >> 10) >> priority;
	for (; counter >= 0; counter--) {
		assign = 0;
		max_cnt = 0;
		pbest = NULL;
	select:
		read_lock(&tasklist_lock);
		p = init_task.next_task;
		for (; p != &init_task; p = p->next_task) {
			if (!p->swappable)
				continue;
	 		if (p->mm->rss <= 0)
				continue;
			if (assign) {
				/* 
				 * If we didn't select a task on pass 1, 
				 * assign each task a new swap_cnt.
				 * Normalise the number of pages swapped
				 * by multiplying by (RSS / 1MB)
				 */
				p->swap_cnt = AGE_CLUSTER_SIZE(p->mm->rss);
			}
			if (p->swap_cnt > max_cnt) {
				max_cnt = p->swap_cnt;
				pbest = p;
			}
		}
		read_unlock(&tasklist_lock);
		if (!pbest) {
			if (!assign) {
				assign = 1;
				goto select;
			}
			goto out;
		}
		pbest->swap_cnt--;

		switch (swap_out_process(pbest, gfp_mask)) {
		case 0:
			/*
			 * Clear swap_cnt so we don't look at this task
			 * again until we've tried all of the others.
			 * (We didn't block, so the task is still here.)
			 */
			pbest->swap_cnt = 0;
			break;
		case 1:
			return 1;
		default:
			break;
		};
	}
out:
	return 0;
}

/*
 * We are much more aggressive about trying to swap out than we used
 * to be.  This works out OK, because we now do proper aging on page
 * contents. 
 */
static inline int do_try_to_free_page(int gfp_mask)
{
	static int state = 0;
	int i=6;
	int stop;

	/* Always trim SLAB caches when memory gets low. */
	kmem_cache_reap(gfp_mask);

	/* We try harder if we are waiting .. */
	stop = 3;
	if (gfp_mask & __GFP_WAIT)
		stop = 0;
	if (((buffermem >> PAGE_SHIFT) * 100 > buffer_mem.borrow_percent * num_physpages)
		   || (page_cache_size * 100 > page_cache.borrow_percent * num_physpages))
		state = 0;

	switch (state) {
		do {
		case 0:
			if (shrink_mmap(i, gfp_mask))
				return 1;
			state = 1;
		case 1:
			if ((gfp_mask & __GFP_IO) && shm_swap(i, gfp_mask))
				return 1;
			state = 2;
		case 2:
			if (swap_out(i, gfp_mask))
				return 1;
			state = 3;
		case 3:
			shrink_dcache_memory(i, gfp_mask);
			state = 0;
		i--;
		} while ((i - stop) >= 0);
	}
	return 0;
}

/*
 * This is REALLY ugly.
 *
 * We need to make the locks finer granularity, but right
 * now we need this so that we can do page allocations
 * without holding the kernel lock etc.
 */
int try_to_free_page(int gfp_mask)
{
	int retval;

	lock_kernel();
	retval = do_try_to_free_page(gfp_mask);
	unlock_kernel();
	return retval;
}

/*
 * Before we start the kernel thread, print out the 
 * kswapd initialization message (otherwise the init message 
 * may be printed in the middle of another driver's init 
 * message).  It looks very bad when that happens.
 */
void kswapd_setup(void)
{
       int i;
       char *revision="$Revision: 1.5 $", *s, *e;

       if ((s = strchr(revision, ':')) &&
           (e = strchr(s, '$')))
               s++, i = e - s;
       else
               s = revision, i = -1;
       printk ("Starting kswapd v%.*s\n", i, s);
}

/*
 * The background pageout daemon.
 * Started as a kernel thread from the init process.
 */
int kswapd(void *unused)
{
	struct wait_queue wait = { current, NULL };
	current->session = 1;
	current->pgrp = 1;
	sprintf(current->comm, "kswapd");
	sigfillset(&current->blocked);
	
	/*
	 *	As a kernel thread we want to tamper with system buffers
	 *	and other internals and thus be subject to the SMP locking
	 *	rules. (On a uniprocessor box this does nothing).
	 */
	lock_kernel();

	/* Give kswapd a realtime priority. */
	current->policy = SCHED_FIFO;
	current->priority = 32;  /* Fixme --- we need to standardise our
				    namings for POSIX.4 realtime scheduling
				    priorities.  */

	init_swap_timer();
	add_wait_queue(&kswapd_wait, &wait);
	while (1) {
		int tries;
		int tried = 0;

		current->state = TASK_INTERRUPTIBLE;
		flush_signals(current);
		run_task_queue(&tq_disk);
		schedule();
		swapstats.wakeups++;

		/*
		 * Do the background pageout: be
		 * more aggressive if we're really
		 * low on free memory.
		 *
		 * We try page_daemon.tries_base times, divided by
		 * an 'urgency factor'. In practice this will mean
		 * a value of pager_daemon.tries_base / 8 or 4 = 64
		 * or 128 pages at a time.
		 * This gives us 64 (or 128) * 4k * 4 (times/sec) =
		 * 1 (or 2) MB/s swapping bandwidth in low-priority
		 * background paging. This number rises to 8 MB/s
		 * when the priority is highest (but then we'll be
		 * woken up more often and the rate will be even
		 * higher).
		 */
		tries = pager_daemon.tries_base >> free_memory_available(3);
	
		while (tries--) {
			int gfp_mask;

			if (++tried > pager_daemon.tries_min && free_memory_available(0))
				break;
			gfp_mask = __GFP_IO;
			try_to_free_page(gfp_mask);
			/*
			 * Syncing large chunks is faster than swapping
			 * synchronously (less head movement). -- Rik.
			 */
			if (atomic_read(&nr_async_pages) >= pager_daemon.swap_cluster)
				run_task_queue(&tq_disk);

		}
	}
	/* As if we could ever get here - maybe we want to make this killable */
	remove_wait_queue(&kswapd_wait, &wait);
	unlock_kernel();
	return 0;
}

/* 
 * The swap_tick function gets called on every clock tick.
 */
void swap_tick(void)
{
	unsigned long now, want;
	int want_wakeup = 0;

	want = next_swap_jiffies;
	now = jiffies;

	/*
	 * Examine the memory queues. Mark memory low
	 * if there is nothing available in the three
	 * highest queues.
	 *
	 * Schedule for wakeup if there isn't lots
	 * of free memory.
	 */
	switch (free_memory_available(3)) {
	case 0:
		want = now;
		/* Fall through */
	case 1 ... 3:
		want_wakeup = 1;
	default:
	}
 
	if ((long) (now - want) >= 0) {
		if (want_wakeup || (num_physpages * buffer_mem.max_percent) < (buffermem >> PAGE_SHIFT) * 100
				|| (num_physpages * page_cache.max_percent < page_cache_size * 100)) {
			/* Set the next wake-up time */
			next_swap_jiffies = now + swapout_interval;
			wake_up(&kswapd_wait);
		}
	}
	timer_active |= (1<<SWAP_TIMER);
}

/* 
 * Initialise the swap timer
 */

void init_swap_timer(void)
{
	timer_table[SWAP_TIMER].expires = 0;
	timer_table[SWAP_TIMER].fn = swap_tick;
	timer_active |= (1<<SWAP_TIMER);
}
