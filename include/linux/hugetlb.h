#ifndef _LINUX_HUGETLB_H
#define _LINUX_HUGETLB_H

#ifdef CONFIG_HUGETLB_PAGE

struct ctl_table;
struct hugetlb_key;

static inline int is_vm_hugetlb_page(struct vm_area_struct *vma)
{
	return vma->vm_flags & VM_HUGETLB;
}

int hugetlb_sysctl_handler(struct ctl_table *, int, struct file *, void *, size_t *);
int copy_hugetlb_page_range(struct mm_struct *, struct mm_struct *, struct vm_area_struct *);
int follow_hugetlb_page(struct mm_struct *, struct vm_area_struct *, struct page **, struct vm_area_struct **, unsigned long *, int *, int);
void zap_hugepage_range(struct vm_area_struct *, unsigned long, unsigned long);
void unmap_hugepage_range(struct vm_area_struct *, unsigned long, unsigned long);
int hugetlb_prefault(struct address_space *, struct vm_area_struct *);
void huge_page_release(struct page *);
int hugetlb_report_meminfo(char *);
int is_hugepage_mem_enough(size_t);
struct page *follow_huge_addr(struct mm_struct *mm, struct vm_area_struct *vma,
			unsigned long address, int write);
struct vm_area_struct *hugepage_vma(struct mm_struct *mm,
					unsigned long address);
struct page *follow_huge_pmd(struct mm_struct *mm, unsigned long address,
				pmd_t *pmd, int write);
int pmd_huge(pmd_t pmd);

extern int htlbpage_max;

static inline void
mark_mm_hugetlb(struct mm_struct *mm, struct vm_area_struct *vma)
{
	if (is_vm_hugetlb_page(vma))
		mm->used_hugetlb = 1;
}

#else /* !CONFIG_HUGETLB_PAGE */

static inline int is_vm_hugetlb_page(struct vm_area_struct *vma)
{
	return 0;
}

#define follow_hugetlb_page(m,v,p,vs,a,b,i)	({ BUG(); 0; })
#define follow_huge_addr(mm, vma, addr, write)	0
#define copy_hugetlb_page_range(src, dst, vma)	({ BUG(); 0; })
#define hugetlb_prefault(mapping, vma)		({ BUG(); 0; })
#define zap_hugepage_range(vma, start, len)	BUG()
#define unmap_hugepage_range(vma, start, end)	BUG()
#define huge_page_release(page)			BUG()
#define is_hugepage_mem_enough(size)		0
#define hugetlb_report_meminfo(buf)		0
#define hugepage_vma(mm, addr)			0
#define mark_mm_hugetlb(mm, vma)		do { } while (0)
#define follow_huge_pmd(mm, addr, pmd, write)	0
#define pmd_huge(x)	0

#ifndef HPAGE_MASK
#define HPAGE_MASK	0		/* Keep the compiler happy */
#endif

#endif /* !CONFIG_HUGETLB_PAGE */

#ifdef CONFIG_HUGETLBFS
extern struct file_operations hugetlbfs_file_operations;
extern struct vm_operations_struct hugetlb_vm_ops;
struct file *hugetlb_zero_setup(size_t);

static inline int is_file_hugepages(struct file *file)
{
	return file->f_op == &hugetlbfs_file_operations;
}

static inline void set_file_hugepages(struct file *file)
{
	file->f_op = &hugetlbfs_file_operations;
}
#else /* !CONFIG_HUGETLBFS */

#define is_file_hugepages(file)		0
#define set_file_hugepages(file)	BUG()
#define hugetlb_zero_setup(size)	ERR_PTR(-ENOSYS)

#endif /* !CONFIG_HUGETLBFS */

#endif /* _LINUX_HUGETLB_H */
