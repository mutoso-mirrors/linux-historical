#ifndef _ASM_IA64_PGALLOC_H
#define _ASM_IA64_PGALLOC_H

/*
 * This file contains the functions and defines necessary to allocate
 * page tables.
 *
 * This hopefully works with any (fixed) ia-64 page-size, as defined
 * in <asm/page.h> (currently 8192).
 *
 * Copyright (C) 1998-2001 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 2000, Goutham Rao <goutham.rao@intel.com>
 */

#include <linux/config.h>

#include <linux/mm.h>
#include <linux/threads.h>
#include <linux/compiler.h>

#include <asm/mmu_context.h>
#include <asm/processor.h>

/*
 * Very stupidly, we used to get new pgd's and pmd's, init their contents
 * to point to the NULL versions of the next level page table, later on
 * completely re-init them the same way, then free them up.  This wasted
 * a lot of work and caused unnecessary memory traffic.  How broken...
 * We fix this by caching them.
 */
#define pgd_quicklist		(local_cpu_data->pgd_quick)
#define pmd_quicklist		(local_cpu_data->pmd_quick)
#define pgtable_cache_size	(local_cpu_data->pgtable_cache_sz)

static inline pgd_t*
pgd_alloc_one_fast (struct mm_struct *mm)
{
	unsigned long *ret = pgd_quicklist;

	if (likely(ret != NULL)) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		--pgtable_cache_size;
	} else
		ret = NULL;
	return (pgd_t *) ret;
}

static inline pgd_t*
pgd_alloc (struct mm_struct *mm)
{
	/* the VM system never calls pgd_alloc_one_fast(), so we do it here. */
	pgd_t *pgd = pgd_alloc_one_fast(mm);

	if (unlikely(pgd == NULL)) {
		pgd = (pgd_t *)__get_free_page(GFP_KERNEL);
		if (likely(pgd != NULL))
			clear_page(pgd);
	}
	return pgd;
}

static inline void
pgd_free (pgd_t *pgd)
{
	*(unsigned long *)pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	++pgtable_cache_size;
}

static inline void
pgd_populate (struct mm_struct *mm, pgd_t *pgd_entry, pmd_t *pmd)
{
	pgd_val(*pgd_entry) = __pa(pmd);
}


static inline pmd_t*
pmd_alloc_one_fast (struct mm_struct *mm, unsigned long addr)
{
	unsigned long *ret = (unsigned long *)pmd_quicklist;

	if (likely(ret != NULL)) {
		pmd_quicklist = (unsigned long *)(*ret);
		ret[0] = 0;
		--pgtable_cache_size;
	}
	return (pmd_t *)ret;
}

static inline pmd_t*
pmd_alloc_one (struct mm_struct *mm, unsigned long addr)
{
	pmd_t *pmd = (pmd_t *) __get_free_page(GFP_KERNEL);

	if (likely(pmd != NULL))
		clear_page(pmd);
	return pmd;
}

static inline void
pmd_free (pmd_t *pmd)
{
	*(unsigned long *)pmd = (unsigned long) pmd_quicklist;
	pmd_quicklist = (unsigned long *) pmd;
	++pgtable_cache_size;
}

#define pmd_free_tlb(tlb, pmd)	pmd_free(pmd)

static inline void
pmd_populate (struct mm_struct *mm, pmd_t *pmd_entry, struct page *pte)
{
	pmd_val(*pmd_entry) = page_to_phys(pte);
}

static inline void
pmd_populate_kernel (struct mm_struct *mm, pmd_t *pmd_entry, pte_t *pte)
{
	pmd_val(*pmd_entry) = __pa(pte);
}

static inline struct page *
pte_alloc_one (struct mm_struct *mm, unsigned long addr)
{
	struct page *pte = alloc_pages(GFP_KERNEL, 0);

	if (likely(pte != NULL))
		clear_page(page_address(pte));
	return pte;
}

static inline pte_t *
pte_alloc_one_kernel (struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte = (pte_t *) __get_free_page(GFP_KERNEL);

	if (likely(pte != NULL))
		clear_page(pte);
	return pte;
}

static inline void
pte_free (struct page *pte)
{
	__free_page(pte);
}

static inline void
pte_free_kernel (pte_t *pte)
{
	free_page((unsigned long) pte);
}

#define pte_free_tlb(tlb, pte)	tlb_remove_page((tlb), (pte))

extern void check_pgt_cache (void);

/*
 * IA-64 doesn't have any external MMU info: the page tables contain all the necessary
 * information.  However, we use this macro to take care of any (delayed) i-cache flushing
 * that may be necessary.
 */
static inline void
update_mmu_cache (struct vm_area_struct *vma, unsigned long vaddr, pte_t pte)
{
	unsigned long addr;
	struct page *page;

	if (!pte_exec(pte))
		return;				/* not an executable page... */

	page = pte_page(pte);
	/* don't use VADDR: it may not be mapped on this CPU (or may have just been flushed): */
	addr = (unsigned long) page_address(page);

	if (test_bit(PG_arch_1, &page->flags))
		return;				/* i-cache is already coherent with d-cache */

	flush_icache_range(addr, addr + PAGE_SIZE);
	set_bit(PG_arch_1, &page->flags);	/* mark page as clean */
}

#endif /* _ASM_IA64_PGALLOC_H */
