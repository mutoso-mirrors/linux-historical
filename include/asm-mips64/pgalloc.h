/* $Id: pgalloc.h,v 1.3 2000/02/24 00:13:20 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 - 2000 by Ralf Baechle at alii
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 */
#ifndef _ASM_PGALLOC_H
#define _ASM_PGALLOC_H

#include <linux/config.h>

/* TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLB entries
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB entries
 *  - flush_tlb_page(mm, vmaddr) flushes a single page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 */
extern void (*_flush_tlb_all)(void);
extern void (*_flush_tlb_mm)(struct mm_struct *mm);
extern void (*_flush_tlb_range)(struct mm_struct *mm, unsigned long start,
			       unsigned long end);
extern void (*_flush_tlb_page)(struct vm_area_struct *vma, unsigned long page);

#define flush_tlb_all()			_flush_tlb_all()
#define flush_tlb_mm(mm)		_flush_tlb_mm(mm)
#define flush_tlb_range(mm,vmaddr,end)	_flush_tlb_range(mm, vmaddr, end)
#define flush_tlb_page(vma,page)	_flush_tlb_page(vma, page)

extern inline void flush_tlb_pgtables(struct mm_struct *mm,
                                      unsigned long start, unsigned long end)
{
	/* Nothing to do on MIPS.  */
}


/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any.
 */

#define pgd_quicklist (current_cpu_data.pgd_quick)
#define pmd_quicklist (current_cpu_data.pmd_quick)
#define pte_quicklist (current_cpu_data.pte_quick)
#define pgtable_cache_size (current_cpu_data.pgtable_cache_sz)

extern pgd_t *get_pgd_slow(void);

extern inline pgd_t *get_pgd_fast(void)
{
	unsigned long *ret;

	if((ret = pgd_quicklist) != NULL) {
		pgd_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
		return (pgd_t *)ret;
	}

	ret = (unsigned long *) get_pgd_slow();
	return (pgd_t *)ret;
}

extern inline void free_pgd_fast(pgd_t *pgd)
{
	*(unsigned long *)pgd = (unsigned long) pgd_quicklist;
	pgd_quicklist = (unsigned long *) pgd;
	pgtable_cache_size++;
}

extern inline void free_pgd_slow(pgd_t *pgd)
{
	free_pages((unsigned long)pgd, 1);
}

extern pte_t *get_pte_slow(pmd_t *pmd, unsigned long address_preadjusted);
extern pte_t *get_pte_kernel_slow(pmd_t *pmd, unsigned long address_preadjusted);

extern inline pte_t *get_pte_fast(void)
{
	unsigned long *ret;

	if((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
	}
	return (pte_t *)ret;
}

extern inline void free_pte_fast(pte_t *pte)
{
	*(unsigned long *)pte = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pte;
	pgtable_cache_size++;
}

extern inline void free_pte_slow(pte_t *pte)
{
	free_pages((unsigned long)pte, 0);
}

extern pmd_t *get_pmd_slow(pgd_t *pgd, unsigned long address_preadjusted);
extern pmd_t *get_pmd_kernel_slow(pgd_t *pgd, unsigned long address_preadjusted);

extern inline pmd_t *get_pmd_fast(void)
{
	unsigned long *ret;

	if ((ret = (unsigned long *)pte_quicklist) != NULL) {
		pte_quicklist = (unsigned long *)(*ret);
		ret[0] = ret[1];
		pgtable_cache_size--;
		return (pmd_t *)ret;
	}

	return (pmd_t *)ret;
}

extern inline void free_pmd_fast(pmd_t *pmd)
{
	*(unsigned long *)pmd = (unsigned long) pte_quicklist;
	pte_quicklist = (unsigned long *) pmd;
	pgtable_cache_size++;
}

extern inline void free_pmd_slow(pmd_t *pmd)
{
	free_pages((unsigned long)pmd, 1);
}

extern void __bad_pte(pmd_t *pmd);
extern void __bad_pte_kernel(pmd_t *pmd);
extern void __bad_pmd(pgd_t *pgd);

#define pte_free_kernel(pte)    free_pte_fast(pte)
#define pte_free(pte)           free_pte_fast(pte)
#define pmd_free_kernel(pte)    free_pmd_fast(pte)
#define pmd_free(pte)           free_pmd_fast(pte)
#define pgd_free(pgd)           free_pgd_fast(pgd)
#define pgd_alloc()             get_pgd_fast()

extern inline pte_t * pte_alloc_kernel(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);

	if (pmd_none(*pmd)) {
		pte_t *page = get_pte_fast();
		if (page) {
			pmd_val(*pmd) = (unsigned long) page;
			return page + address;
		}
		return get_pte_kernel_slow(pmd, address);
	}
	if (pmd_bad(*pmd)) {
		__bad_pte_kernel(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline pte_t * pte_alloc(pmd_t * pmd, unsigned long address)
{
	address = (address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1);

	if (pmd_none(*pmd)) {
		pte_t *page = get_pte_fast();
		if (page) {
			pmd_val(*pmd) = (unsigned long) page;
			return page + address;
		}
		return get_pte_slow(pmd, address);
	}
	if (pmd_bad(*pmd)) {
		__bad_pte(pmd);
		return NULL;
	}
	return (pte_t *) pmd_page(*pmd) + address;
}

extern inline pmd_t *pmd_alloc(pgd_t * pgd, unsigned long address)
{
	address = (address >> PMD_SHIFT) & (PTRS_PER_PMD - 1);
	if (pgd_none(*pgd)) {
		pmd_t *page = get_pmd_fast();

		if (!page)
			return get_pmd_slow(pgd, address);
		pgd_set(pgd, page);
		return page + address;
	}
	if (pgd_bad(*pgd)) {
		__bad_pmd(pgd);
		return NULL;
	}
	return (pmd_t *) pgd_page(*pgd) + address;
}

#define pmd_alloc_kernel	pmd_alloc

extern int do_check_pgt_cache(int, int);

extern inline void set_pgdir(unsigned long address, pgd_t entry)
{
	struct task_struct * p;
	pgd_t *pgd;
#ifdef CONFIG_SMP
	int i;
#endif	
        
	read_lock(&tasklist_lock);
	for_each_task(p) {
		if (!p->mm)
			continue;
		*pgd_offset(p->mm, address) = entry;
	}
	read_unlock(&tasklist_lock);
#ifndef CONFIG_SMP
	for (pgd = (pgd_t *)pgd_quicklist; pgd; pgd = (pgd_t *)*(unsigned long *)pgd)
		pgd[address >> PGDIR_SHIFT] = entry;
#else
	/* To pgd_alloc/pgd_free, one holds master kernel lock and so does our
	   callee, so we can modify pgd caches of other CPUs as well. -jj */
	for (i = 0; i < NR_CPUS; i++)
		for (pgd = (pgd_t *)cpu_data[i].pgd_quick; pgd; pgd = (pgd_t *)*(unsigned long *)pgd)
			pgd[address >> PGDIR_SHIFT] = entry;
#endif
}

#endif /* _ASM_PGALLOC_H */
