/* $Id: pgtable.h,v 1.25 1995/11/25 02:32:22 davem Exp $ */
#ifndef _SPARC_PGTABLE_H
#define _SPARC_PGTABLE_H

/*  asm-sparc/pgtable.h:  Defines and functions used to work
 *                        with Sparc page tables.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/mm.h>
#include <asm/asi.h>
#include <asm/pgtsun4c.h>
#include <asm/pgtsrmmu.h>
#include <asm/vac-ops.h>
#include <asm/oplib.h>

extern void load_mmu(void);

/* mmu-specific process creation/cloning/etc hooks. */
extern void (*mmu_exit_hook)(void *);
extern void (*mmu_fork_hook)(void *, unsigned long);
extern void (*mmu_release_hook)(void *);
extern void (*mmu_flush_hook)(void *);
extern void (*mmu_task_cacheflush)(void *);

/* Routines for data transfer buffers. */
extern char *(*mmu_lockarea)(char *, unsigned long);
extern void  (*mmu_unlockarea)(char *, unsigned long);

/* Routines for getting a dvma scsi buffer. */
extern char *(*mmu_get_scsi_buffer)(char *, unsigned long);
extern void  (*mmu_release_scsi_buffer)(char *, unsigned long);

extern unsigned int pmd_shift;
extern unsigned int pmd_size;
extern unsigned int pmd_mask;
extern unsigned int (*pmd_align)(unsigned int);

extern unsigned int pgdir_shift;
extern unsigned int pgdir_size;
extern unsigned int pgdir_mask;
extern unsigned int (*pgdir_align)(unsigned int);

extern unsigned int ptrs_per_pte;
extern unsigned int ptrs_per_pmd;
extern unsigned int ptrs_per_pgd;

extern unsigned int ptrs_per_page;

extern unsigned long (*(vmalloc_start))(void);

#define VMALLOC_VMADDR(x) ((unsigned long)(x))
#define VMALLOC_START vmalloc_start()

extern pgprot_t page_none;
extern pgprot_t page_shared;
extern pgprot_t page_copy;
extern pgprot_t page_readonly;
extern pgprot_t page_kernel;
extern pgprot_t page_invalid;

#define PMD_SHIFT      (pmd_shift)
#define PMD_SIZE       (pmd_size)
#define PMD_MASK       (pmd_mask)
#define PMD_ALIGN      (pmd_align)
#define PGDIR_SHIFT    (pgdir_shift)
#define PGDIR_SIZE     (pgdir_size)
#define PGDIR_MASK     (pgdir_mask)
#define PGDIR_ALIGN    (pgdir_align)
#define PTRS_PER_PTE   (ptrs_per_pte)
#define PTRS_PER_PMD   (ptrs_per_pmd)
#define PTRS_PER_PGD   (ptrs_per_pgd)

#define PAGE_NONE      (page_none)
#define PAGE_SHARED    (page_shared)
#define PAGE_COPY      (page_copy)
#define PAGE_READONLY  (page_readonly)
#define PAGE_KERNEL    (page_kernel)
#define PAGE_INVALID   (page_invalid)

/* Top-level page directory */
extern pgd_t swapper_pg_dir[1024];

/* Page table for 0-4MB for everybody, on the Sparc this
 * holds the same as on the i386.
 */
extern pte_t pg0[1024];

extern unsigned long ptr_in_current_pgd;

/* the no. of pointers that fit on a page: this will go away */
#define PTRS_PER_PAGE   (PAGE_SIZE/sizeof(void*))

/* Here is a trick, since mmap.c need the initializer elements for
 * protection_map[] to be constant at compile time, I set the following
 * to all zeros.  I set it to the real values after I link in the
 * appropriate MMU page table routines at boot time.
 */
#define __P000  __pgprot(0)
#define __P001  __pgprot(0)
#define __P010  __pgprot(0)
#define __P011  __pgprot(0)
#define __P100  __pgprot(0)
#define __P101  __pgprot(0)
#define __P110  __pgprot(0)
#define __P111  __pgprot(0)

#define __S000	__pgprot(0)
#define __S001	__pgprot(0)
#define __S010	__pgprot(0)
#define __S011	__pgprot(0)
#define __S100	__pgprot(0)
#define __S101	__pgprot(0)
#define __S110	__pgprot(0)
#define __S111	__pgprot(0)

extern int num_contexts;

/*
 * BAD_PAGETABLE is used when we need a bogus page-table, while
 * BAD_PAGE is used for a bogus page.
 *
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern pte_t __bad_page(void);
extern pte_t * __bad_pagetable(void);

extern unsigned long __zero_page(void);

#define BAD_PAGETABLE __bad_pagetable()
#define BAD_PAGE __bad_page()
#define ZERO_PAGE __zero_page()

/* number of bits that fit into a memory pointer */
#define BITS_PER_PTR      (8*sizeof(unsigned long))

/* to align the pointer to a pointer address */
#define PTR_MASK          (~(sizeof(void*)-1))

#define SIZEOF_PTR_LOG2   2

extern unsigned long (*pte_page)(pte_t);
extern unsigned long (*pmd_page)(pmd_t);
extern unsigned long (*pgd_page)(pgd_t);

/* to set the page-dir
 *
 * On the Sparc the page segments hold 64 pte's which means 256k/segment.
 * Therefore there is no global idea of 'the' page directory, although we
 * make a virtual one in kernel memory so that we can keep the stats on
 * all the pages since not all can be loaded at once in the mmu.
 *
 * Actually on the SRMMU things do work exactly like the i386, the
 * page tables live in real physical ram, no funky TLB buisness.
 */

extern void (*sparc_update_rootmmu_dir)(struct task_struct *, pgd_t *pgdir);

#define SET_PAGE_DIR(tsk,pgdir) sparc_update_rootmmu_dir(tsk, pgdir)
       
/* to find an entry in a page-table */
#define PAGE_PTR(address) \
((unsigned long)(address)>>(PAGE_SHIFT-SIZEOF_PTR_LOG2)&PTR_MASK&~PAGE_MASK)

extern unsigned long high_memory;

extern int (*pte_none)(pte_t);
extern int (*pte_present)(pte_t);
extern int (*pte_inuse)(pte_t *);
extern void (*pte_clear)(pte_t *);
extern void (*pte_reuse)(pte_t *);

extern int (*pmd_none)(pmd_t);
extern int (*pmd_bad)(pmd_t);
extern int (*pmd_present)(pmd_t);
extern int (*pmd_inuse)(pmd_t *);
extern void (*pmd_clear)(pmd_t *);
extern void (*pmd_reuse)(pmd_t *);

extern int (*pgd_none)(pgd_t);
extern int (*pgd_bad)(pgd_t);
extern int (*pgd_present)(pgd_t);
extern int (*pgd_inuse)(pgd_t *);
extern void (*pgd_clear)(pgd_t *);
extern void (*pgd_reuse)(pgd_t *);

/*
 * The following only work if pte_present() is true.
 * Undefined behaviour if not..
 */
extern int (*pte_read)(pte_t);
extern int (*pte_write)(pte_t);
extern int (*pte_exec)(pte_t);
extern int (*pte_dirty)(pte_t);
extern int (*pte_young)(pte_t);
extern int (*pte_cow)(pte_t);

extern pte_t (*pte_wrprotect)(pte_t);
extern pte_t (*pte_rdprotect)(pte_t);
extern pte_t (*pte_exprotect)(pte_t);
extern pte_t (*pte_mkclean)(pte_t);
extern pte_t (*pte_mkold)(pte_t);
extern pte_t (*pte_uncow)(pte_t);
extern pte_t (*pte_mkwrite)(pte_t);
extern pte_t (*pte_mkread)(pte_t);
extern pte_t (*pte_mkexec)(pte_t);
extern pte_t (*pte_mkdirty)(pte_t);
extern pte_t (*pte_mkyoung)(pte_t);
extern pte_t (*pte_mkcow)(pte_t);

/*
 * Conversion functions: convert a page and protection to a page entry,
 * and a page entry and page directory to the page they refer to.
 */
extern pte_t (*mk_pte)(unsigned long, pgprot_t);

extern void (*pgd_set)(pgd_t *, pmd_t *);

extern pte_t (*pte_modify)(pte_t, pgprot_t);

/* to find an entry in a page-table-directory */
extern pgd_t * (*pgd_offset)(struct mm_struct *, unsigned long);

/* Find an entry in the second-level page table.. */
extern pmd_t * (*pmd_offset)(pgd_t *, unsigned long);

/* Find an entry in the third-level page table.. */ 
extern pte_t * (*pte_offset)(pmd_t *, unsigned long);

/*
 * Allocate and free page tables. The xxx_kernel() versions are
 * used to allocate a kernel page table - this turns on ASN bits
 * if any, and marks the page tables reserved.
 */
extern void (*pte_free_kernel)(pte_t *);

extern pte_t * (*pte_alloc_kernel)(pmd_t *, unsigned long);

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern void (*pmd_free_kernel)(pmd_t *);

extern pmd_t * (*pmd_alloc_kernel)(pgd_t *, unsigned long);

extern void (*pte_free)(pte_t *);

extern pte_t * (*pte_alloc)(pmd_t *, unsigned long);

/*
 * allocating and freeing a pmd is trivial: the 1-entry pmd is
 * inside the pgd, so has no extra memory associated with it.
 */
extern void (*pmd_free)(pmd_t *);

extern pmd_t * (*pmd_alloc)(pgd_t *, unsigned long);

extern void (*pgd_free)(pgd_t *);

extern pgd_t * (*pgd_alloc)(void);

/* Fault handler stuff... */
#define FAULT_CODE_PROT     0x1
#define FAULT_CODE_WRITE    0x2
#define FAULT_CODE_USER     0x4
extern int (*get_fault_info)(unsigned long *, unsigned long *, unsigned long);
extern void (*update_mmu_cache)(struct vm_area_struct *vma, unsigned long address, pte_t pte);

extern int invalid_segment;

#define SWP_TYPE(entry) (((entry) >> 1) & 0x7f)
#define SWP_OFFSET(entry) ((entry) >> 8)
#define SWP_ENTRY(type,offset) (((type) << 1) | ((offset) << 8))

struct ctx_list {
	struct ctx_list *next;
	struct ctx_list *prev;
	unsigned char ctx_number;
	struct task_struct *ctx_task; /* Who has it now, if not free */
};

extern struct ctx_list *ctx_list_pool;  /* Dynamically allocated */
extern struct ctx_list ctx_free;        /* Head of free list */
extern struct ctx_list ctx_used;        /* Head of used contexts list */

extern inline void remove_from_ctx_list(struct ctx_list *entry)
{
	entry->next->prev = entry->prev;
	entry->prev->next = entry->next;
}

extern inline void add_to_ctx_list(struct ctx_list *head, struct ctx_list *entry)
{
	entry->next = head;
	(entry->prev = head->prev)->next = entry;
	head->prev = entry;
}
#define add_to_free_ctxlist(entry) add_to_ctx_list(&ctx_free, entry)
#define add_to_used_ctxlist(entry) add_to_ctx_list(&ctx_used, entry)

#endif /* !(_SPARC_PGTABLE_H) */
