/*
 * Copyright (C) 2000, 2001, 2002 Jeff Dike (jdike@karaya.com)
 * Copyright 2003 PathScale, Inc.
 * Derived from include/asm-i386/pgtable.h
 * Licensed under the GPL
 */

#ifndef __UM_PGTABLE_2LEVEL_H
#define __UM_PGTABLE_2LEVEL_H

#include <asm-generic/pgtable-nopmd.h>

/* PGDIR_SHIFT determines what a third-level page table entry can map */

#define PGDIR_SHIFT	22
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE-1))

/*
 * entries per page directory level: the i386 is two-level, so
 * we don't really have any PMD directory physically.
 */
#define PTRS_PER_PTE	1024
#define USER_PTRS_PER_PGD ((TASK_SIZE + (PGDIR_SIZE - 1)) / PGDIR_SIZE)
#define PTRS_PER_PGD	1024
#define FIRST_USER_PGD_NR       0

#define pte_ERROR(e) \
        printk("%s:%d: bad pte %p(%08lx).\n", __FILE__, __LINE__, &(e), \
	       pte_val(e))
#define pgd_ERROR(e) \
        printk("%s:%d: bad pgd %p(%08lx).\n", __FILE__, __LINE__, &(e), \
	       pgd_val(e))

static inline int pgd_newpage(pgd_t pgd)	{ return 0; }
static inline void pgd_mkuptodate(pgd_t pgd)	{ }

#define pte_present(x)	(pte_val(x) & (_PAGE_PRESENT | _PAGE_PROTNONE))

static inline pte_t pte_mknewprot(pte_t pte)
{
 	pte_val(pte) |= _PAGE_NEWPROT;
	return(pte);
}

static inline pte_t pte_mknewpage(pte_t pte)
{
	pte_val(pte) |= _PAGE_NEWPAGE;
	return(pte);
}

static inline void set_pte(pte_t *pteptr, pte_t pteval)
{
	/* If it's a swap entry, it needs to be marked _PAGE_NEWPAGE so
	 * fix_range knows to unmap it.  _PAGE_NEWPROT is specific to
	 * mapped pages.
	 */
	*pteptr = pte_mknewpage(pteval);
	if(pte_present(*pteptr)) *pteptr = pte_mknewprot(*pteptr);
}
#define set_pte_at(mm,addr,ptep,pteval) set_pte(ptep,pteval)

#define set_pmd(pmdptr, pmdval) (*(pmdptr) = (pmdval))

#define pte_page(x) pfn_to_page(pte_pfn(x))
#define pte_none(x) !(pte_val(x) & ~_PAGE_NEWPAGE)
#define pte_pfn(x) phys_to_pfn(pte_val(x))
#define pfn_pte(pfn, prot) __pte(pfn_to_phys(pfn) | pgprot_val(prot))
#define pfn_pmd(pfn, prot) __pmd(pfn_to_phys(pfn) | pgprot_val(prot))

#define pmd_page_kernel(pmd) \
	((unsigned long) __va(pmd_val(pmd) & PAGE_MASK))

/*
 * Bits 0 through 3 are taken
 */
#define PTE_FILE_MAX_BITS	28

#define pte_to_pgoff(pte) (pte_val(pte) >> 4)

#define pgoff_to_pte(off) ((pte_t) { ((off) << 4) + _PAGE_FILE })

#endif
