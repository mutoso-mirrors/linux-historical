#ifndef _PPC64_PGALLOC_H
#define _PPC64_PGALLOC_H

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/percpu.h>
#include <asm/processor.h>
#include <asm/tlb.h>

extern kmem_cache_t *zero_cache;

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

static inline pgd_t *
pgd_alloc(struct mm_struct *mm)
{
	return kmem_cache_alloc(zero_cache, GFP_KERNEL);
}

static inline void
pgd_free(pgd_t *pgd)
{
	kmem_cache_free(zero_cache, pgd);
}

#define pgd_populate(MM, PGD, PMD)	pgd_set(PGD, PMD)

static inline pmd_t *
pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(zero_cache, GFP_KERNEL|__GFP_REPEAT);
}

static inline void
pmd_free(pmd_t *pmd)
{
	kmem_cache_free(zero_cache, pmd);
}

#define pmd_populate_kernel(mm, pmd, pte) pmd_set(pmd, pte)
#define pmd_populate(mm, pmd, pte_page) \
	pmd_populate_kernel(mm, pmd, page_address(pte_page))

static inline pte_t *
pte_alloc_one_kernel(struct mm_struct *mm, unsigned long addr)
{
	return kmem_cache_alloc(zero_cache, GFP_KERNEL|__GFP_REPEAT);
}

static inline struct page *
pte_alloc_one(struct mm_struct *mm, unsigned long address)
{
	pte_t *pte = pte_alloc_one_kernel(mm, address);

	if (pte)
		return virt_to_page(pte);

	return NULL;
}
		
static inline void pte_free_kernel(pte_t *pte)
{
	kmem_cache_free(zero_cache, pte);
}

#define pte_free(pte_page)	pte_free_kernel(page_address(pte_page))

struct pte_freelist_batch
{
	struct rcu_head	rcu;
	unsigned int	index;
	struct page *	pages[0];
};

#define PTE_FREELIST_SIZE	((PAGE_SIZE - sizeof(struct pte_freelist_batch)) / \
				  sizeof(struct page *))

extern void pte_free_now(struct page *ptepage);
extern void pte_free_submit(struct pte_freelist_batch *batch);

DECLARE_PER_CPU(struct pte_freelist_batch *, pte_freelist_cur);

static inline void __pte_free_tlb(struct mmu_gather *tlb, struct page *ptepage)
{
	/* This is safe as we are holding page_table_lock */
        cpumask_t local_cpumask = cpumask_of_cpu(smp_processor_id());
	struct pte_freelist_batch **batchp = &__get_cpu_var(pte_freelist_cur);

	if (atomic_read(&tlb->mm->mm_users) < 2 ||
	    cpus_equal(tlb->mm->cpu_vm_mask, local_cpumask)) {
		pte_free(ptepage);
		return;
	}

	if (*batchp == NULL) {
		*batchp = (struct pte_freelist_batch *)__get_free_page(GFP_ATOMIC);
		if (*batchp == NULL) {
			pte_free_now(ptepage);
			return;
		}
		(*batchp)->index = 0;
	}
	(*batchp)->pages[(*batchp)->index++] = ptepage;
	if ((*batchp)->index == PTE_FREELIST_SIZE) {
		pte_free_submit(*batchp);
		*batchp = NULL;
	}
}

#define __pmd_free_tlb(tlb, pmd)	__pte_free_tlb(tlb, virt_to_page(pmd))

#define check_pgt_cache()	do { } while (0)

#endif /* _PPC64_PGALLOC_H */
