/* sun3_pgalloc.h --
 * reorganization around 2.3.39, routines moved from sun3_pgtable.h 
 *
 *
 * 02/27/2002 -- Modified to support "highpte" implementation in 2.5.5 (Sam)
 *
 * moved 1/26/2000 Sam Creasey
 */

#ifndef _SUN3_PGALLOC_H
#define _SUN3_PGALLOC_H

#include <asm/tlb.h>

/* FIXME - when we get this compiling */
/* erm, now that it's compiling, what do we do with it? */
#define _KERNPG_TABLE 0

extern const char bad_pmd_string[];

#define pmd_alloc_one(mm,address)       ({ BUG(); ((pmd_t *)2); })


static inline void pte_free_kernel(pte_t * pte)
{
        free_page((unsigned long) pte);
}

static inline void pte_free(struct page *page)
{
        __free_page(page);
}

static inline void __pte_free_tlb(mmu_gather_t *tlb, struct page *page)
{
	tlb_remove_page(tlb, page);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm, 
					  unsigned long address)
{
	unsigned long page = __get_free_page(GFP_KERNEL);

	if (!page)
		return NULL;
		
	memset((void *)page, 0, PAGE_SIZE);
	return (pte_t *) (page);
}

static inline struct page *pte_alloc_one(struct mm_struct *mm, 
					 unsigned long address)
{
        struct page *page = alloc_pages(GFP_KERNEL, 0);

	if (page == NULL)
		return NULL;
		
	clear_highpage(page);
	return page;

}

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd, pte_t *pte)
{
	pmd_val(*pmd) = __pa((unsigned long)pte);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, struct page *page)
{
	pmd_val(*pmd) = __pa((unsigned long)page_address(page));
}

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
#define pmd_free(x)			do { } while (0)
#define __pmd_free_tlb(tlb, x)		do { } while (0)

static inline void pgd_free(pgd_t * pgd)
{
        free_page((unsigned long) pgd);
}

static inline pgd_t * pgd_alloc(struct mm_struct *mm)
{
     pgd_t *new_pgd;

     new_pgd = (pgd_t *)get_zeroed_page(GFP_KERNEL);
     memcpy(new_pgd, swapper_pg_dir, PAGE_SIZE);
     memset(new_pgd, 0, (PAGE_OFFSET >> PGDIR_SHIFT));
     return new_pgd;
}

#define pgd_populate(mm, pmd, pte) BUG()


/* Reserved PMEGs. */
extern char sun3_reserved_pmeg[SUN3_PMEGS_NUM];
extern unsigned long pmeg_vaddr[SUN3_PMEGS_NUM];
extern unsigned char pmeg_alloc[SUN3_PMEGS_NUM];
extern unsigned char pmeg_ctx[SUN3_PMEGS_NUM];


#define check_pgt_cache()	do { } while (0)

#endif /* SUN3_PGALLOC_H */
