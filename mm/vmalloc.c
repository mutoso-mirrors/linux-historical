/*
 *  linux/mm/vmalloc.c
 *
 *  Copyright (C) 1993  Linus Torvalds
 */

#include <linux/malloc.h>
#include <linux/vmalloc.h>

#include <asm/segment.h>
#include <asm/system.h>

static struct vm_struct * vmlist = NULL;

static inline void free_area_pte(pmd_t * pmd, unsigned long address, unsigned long size)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*pmd))
		return;
	if (pmd_bad(*pmd)) {
		printk("free_area_pte: bad pmd (%08lx)\n", pmd_val(*pmd));
		pmd_clear(pmd);
		return;
	}
	pte = pte_offset(pmd, address);
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	while (address < end) {
		pte_t page = *pte;
		pte_clear(pte);
		address += PAGE_SIZE;
		pte++;
		if (pte_none(page))
			continue;
		if (pte_present(page)) {
			free_page(pte_page(page));
			continue;
		}
		printk("Whee.. Swapped out page in kernel page table\n");
	}
}

static inline void free_area_pmd(pgd_t * dir, unsigned long address, unsigned long size)
{
	pmd_t * pmd;
	unsigned long end;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		printk("free_area_pmd: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, address);
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	while (address < end) {
		free_area_pte(pmd, address, end - address);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	}
}

static void free_area_pages(unsigned long address, unsigned long size)
{
	pgd_t * dir;
	unsigned long end = address + size;

	dir = pgd_offset(&init_mm, address);
	flush_cache_all();
	while (address < end) {
		free_area_pmd(dir, address, end - address);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_all();
}

static inline int alloc_area_pte(pte_t * pte, unsigned long address, unsigned long size)
{
	unsigned long end;

	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	while (address < end) {
		unsigned long page;
		if (!pte_none(*pte))
			printk("alloc_area_pte: page already exists\n");
		page = __get_free_page(GFP_KERNEL);
		if (!page)
			return -ENOMEM;
		set_pte(pte, mk_pte(page, PAGE_KERNEL));
		address += PAGE_SIZE;
		pte++;
	}
	return 0;
}

static inline int alloc_area_pmd(pmd_t * pmd, unsigned long address, unsigned long size)
{
	unsigned long end;

	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	while (address < end) {
		pte_t * pte = pte_alloc_kernel(pmd, address);
		if (!pte)
			return -ENOMEM;
		if (alloc_area_pte(pte, address, end - address))
			return -ENOMEM;
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	}
	return 0;
}

static int alloc_area_pages(unsigned long address, unsigned long size)
{
	pgd_t * dir;
	unsigned long end = address + size;

	dir = pgd_offset(&init_mm, address);
	flush_cache_all();
	while (address < end) {
		pmd_t *pmd = pmd_alloc_kernel(dir, address);
		if (!pmd)
			return -ENOMEM;
		if (alloc_area_pmd(pmd, address, end - address))
			return -ENOMEM;
		set_pgdir(address, *dir);
		address = (address + PGDIR_SIZE) & PGDIR_MASK;
		dir++;
	}
	flush_tlb_all();
	return 0;
}

struct vm_struct * get_vm_area(unsigned long size)
{
	void *addr;
	struct vm_struct **p, *tmp, *area;

	area = (struct vm_struct *) kmalloc(sizeof(*area), GFP_KERNEL);
	if (!area)
		return NULL;
	addr = (void *) VMALLOC_START;
	area->size = size + PAGE_SIZE;
	area->next = NULL;
	for (p = &vmlist; (tmp = *p) ; p = &tmp->next) {
		if (size + (unsigned long) addr < (unsigned long) tmp->addr)
			break;
		addr = (void *) (tmp->size + (unsigned long) tmp->addr);
	}
	area->addr = addr;
	area->next = *p;
	*p = area;
	return area;
}

void vfree(void * addr)
{
	struct vm_struct **p, *tmp;

	if (!addr)
		return;
	if ((PAGE_SIZE-1) & (unsigned long) addr) {
		printk("Trying to vfree() bad address (%p)\n", addr);
		return;
	}
	for (p = &vmlist ; (tmp = *p) ; p = &tmp->next) {
		if (tmp->addr == addr) {
			*p = tmp->next;
			free_area_pages(VMALLOC_VMADDR(tmp->addr), tmp->size);
			kfree(tmp);
			return;
		}
	}
	printk("Trying to vfree() nonexistent vm area (%p)\n", addr);
}

void * vmalloc(unsigned long size)
{
	void * addr;
	struct vm_struct *area;

	size = PAGE_ALIGN(size);
	if (!size || size > (max_mapnr << PAGE_SHIFT))
		return NULL;
	area = get_vm_area(size);
	if (!area)
		return NULL;
	addr = area->addr;
	if (alloc_area_pages(VMALLOC_VMADDR(addr), size)) {
		vfree(addr);
		return NULL;
	}
	return addr;
}

int vread(char *buf, char *addr, int count)
{
	struct vm_struct **p, *tmp;
	char *vaddr, *buf_start = buf;
	int n;

	for (p = &vmlist; (tmp = *p) ; p = &tmp->next) {
		vaddr = (char *) tmp->addr;
		while (addr < vaddr) {
			if (count == 0)
				goto finished;
			put_user('\0', buf);
			buf++;
			addr++;
			count--;
		}
		n = tmp->size - PAGE_SIZE;
		if (addr > vaddr)
			n -= addr - vaddr;
		while (--n >= 0) {
			if (count == 0)
				goto finished;
			put_user(*addr, buf);
			buf++;
			addr++;
			count--;
		}
	}
finished:
	return buf - buf_start;
}
