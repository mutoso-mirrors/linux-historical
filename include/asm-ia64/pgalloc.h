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

	if (__builtin_expect(ret != NULL, 1)) {
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

	if (__builtin_expect(pgd == NULL, 0)) {
		pgd = (pgd_t *)__get_free_page(GFP_KERNEL);
		if (__builtin_expect(pgd != NULL, 1))
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

	if (__builtin_expect(ret != NULL, 1)) {
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

	if (__builtin_expect(pmd != NULL, 1))
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

	if (__builtin_expect(pte != NULL, 1))
		clear_page(page_address(pte));
	return pte;
}

static inline pte_t *
pte_alloc_one_kernel (struct mm_struct *mm, unsigned long addr)
{
	pte_t *pte = (pte_t *) __get_free_page(GFP_KERNEL);

	if (__builtin_expect(pte != NULL, 1))
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

extern int do_check_pgt_cache (int, int);

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

/*
 * Now for some TLB flushing routines.  This is the kind of stuff that
 * can be very expensive, so try to avoid them whenever possible.
 */

/*
 * Flush everything (kernel mapping may also have changed due to
 * vmalloc/vfree).
 */
extern void __flush_tlb_all (void);

#ifdef CONFIG_SMP
  extern void smp_flush_tlb_all (void);
# define flush_tlb_all()	smp_flush_tlb_all()
#else
# define flush_tlb_all()	__flush_tlb_all()
#endif

/*
 * Flush a specified user mapping
 */
static inline void
flush_tlb_mm (struct mm_struct *mm)
{
	if (mm) {
		mm->context = 0;
		if (mm == current->active_mm) {
			/* This is called, e.g., as a result of exec().  */
			get_new_mmu_context(mm);
			reload_context(mm);
		}
	}
}

extern void flush_tlb_range (struct vm_area_struct *vma, unsigned long start, unsigned long end);

/*
 * Page-granular tlb flush.
 */
static inline void
flush_tlb_page (struct vm_area_struct *vma, unsigned long addr)
{
#ifdef CONFIG_SMP
	flush_tlb_range(vma, (addr & PAGE_MASK), (addr & PAGE_MASK) + PAGE_SIZE);
#else
	if (vma->vm_mm == current->active_mm)
		asm volatile ("ptc.l %0,%1" :: "r"(addr), "r"(PAGE_SHIFT << 2) : "memory");
#endif
}

/*
 * Flush the TLB entries mapping the virtually mapped linear page
 * table corresponding to address range [START-END).
 */
static inline void
flush_tlb_pgtables (struct mm_struct *mm, unsigned long start, unsigned long end)
{
	struct vm_area_struct vma;

	if (rgn_index(start) != rgn_index(end))
		printk("flush_tlb_pgtables: can't flush across regions!!\n");
	vma.vm_mm = mm;
	flush_tlb_range(&vma, ia64_thash(start), ia64_thash(end));
}

#endif /* _ASM_IA64_PGALLOC_H */
