#ifndef _ASM_GENERIC_PGTABLE_H
#define _ASM_GENERIC_PGTABLE_H

#ifndef __HAVE_ARCH_PTEP_ESTABLISH
/*
 * Establish a new mapping:
 *  - flush the old one
 *  - update the page tables
 *  - inform the TLB about the new one
 *
 * We hold the mm semaphore for reading and vma->vm_mm->page_table_lock.
 *
 * Note: the old pte is known to not be writable, so we don't need to
 * worry about dirty bits etc getting lost.
 */
#ifndef __HAVE_ARCH_SET_PTE_ATOMIC
#define ptep_establish(__vma, __address, __ptep, __entry)		\
do {				  					\
	set_pte_at((__vma)->vm_mm, (__address), __ptep, __entry);	\
	flush_tlb_page(__vma, __address);				\
} while (0)
#else /* __HAVE_ARCH_SET_PTE_ATOMIC */
#define ptep_establish(__vma, __address, __ptep, __entry)		\
do {				  					\
	set_pte_atomic(__ptep, __entry);				\
	flush_tlb_page(__vma, __address);				\
} while (0)
#endif /* __HAVE_ARCH_SET_PTE_ATOMIC */
#endif

#ifndef __HAVE_ARCH_PTEP_SET_ACCESS_FLAGS
/*
 * Largely same as above, but only sets the access flags (dirty,
 * accessed, and writable). Furthermore, we know it always gets set
 * to a "more permissive" setting, which allows most architectures
 * to optimize this.
 */
#define ptep_set_access_flags(__vma, __address, __ptep, __entry, __dirty) \
do {				  					  \
	set_pte_at((__vma)->vm_mm, (__address), __ptep, __entry);	  \
	flush_tlb_page(__vma, __address);				  \
} while (0)
#endif

#ifndef __HAVE_ARCH_PTEP_TEST_AND_CLEAR_YOUNG
#define ptep_test_and_clear_young(__vma, __address, __ptep)		\
({									\
	pte_t __pte = *(__ptep);					\
	int r = 1;							\
	if (!pte_young(__pte))						\
		r = 0;							\
	else								\
		set_pte_at((__vma)->vm_mm, (__address),			\
			   (__ptep), pte_mkold(__pte));			\
	r;								\
})
#endif

#ifndef __HAVE_ARCH_PTEP_CLEAR_YOUNG_FLUSH
#define ptep_clear_flush_young(__vma, __address, __ptep)		\
({									\
	int __young;							\
	__young = ptep_test_and_clear_young(__vma, __address, __ptep);	\
	if (__young)							\
		flush_tlb_page(__vma, __address);			\
	__young;							\
})
#endif

#ifndef __HAVE_ARCH_PTEP_TEST_AND_CLEAR_DIRTY
#define ptep_test_and_clear_dirty(__vma, __address, __ptep)		\
({									\
	pte_t __pte = *__ptep;						\
	int r = 1;							\
	if (!pte_dirty(__pte))						\
		r = 0;							\
	else								\
		set_pte_at((__vma)->vm_mm, (__address), (__ptep),	\
			   pte_mkclean(__pte));				\
	r;								\
})
#endif

#ifndef __HAVE_ARCH_PTEP_CLEAR_DIRTY_FLUSH
#define ptep_clear_flush_dirty(__vma, __address, __ptep)		\
({									\
	int __dirty;							\
	__dirty = ptep_test_and_clear_dirty(__vma, __address, __ptep);	\
	if (__dirty)							\
		flush_tlb_page(__vma, __address);			\
	__dirty;							\
})
#endif

#ifndef __HAVE_ARCH_PTEP_GET_AND_CLEAR
#define ptep_get_and_clear(__mm, __address, __ptep)			\
({									\
	pte_t __pte = *(__ptep);					\
	pte_clear((__mm), (__address), (__ptep));			\
	__pte;								\
})
#endif

#ifndef __HAVE_ARCH_PTEP_CLEAR_FLUSH
#define ptep_clear_flush(__vma, __address, __ptep)			\
({									\
	pte_t __pte;							\
	__pte = ptep_get_and_clear((__vma)->vm_mm, __address, __ptep);	\
	flush_tlb_page(__vma, __address);				\
	__pte;								\
})
#endif

#ifndef __HAVE_ARCH_PTEP_SET_WRPROTECT
static inline void ptep_set_wrprotect(struct mm_struct *mm, unsigned long address, pte_t *ptep)
{
	pte_t old_pte = *ptep;
	set_pte_at(mm, address, ptep, pte_wrprotect(old_pte));
}
#endif

#ifndef __HAVE_ARCH_PTE_SAME
#define pte_same(A,B)	(pte_val(A) == pte_val(B))
#endif

#ifndef __HAVE_ARCH_PAGE_TEST_AND_CLEAR_DIRTY
#define page_test_and_clear_dirty(page) (0)
#endif

#ifndef __HAVE_ARCH_PAGE_TEST_AND_CLEAR_YOUNG
#define page_test_and_clear_young(page) (0)
#endif

#ifndef __HAVE_ARCH_PGD_OFFSET_GATE
#define pgd_offset_gate(mm, addr)	pgd_offset(mm, addr)
#endif

#endif /* _ASM_GENERIC_PGTABLE_H */
