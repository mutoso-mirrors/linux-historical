/* $Id: andes.c,v 1.1 1997/06/06 09:34:31 ralf Exp $
 * andes.c: MMU and cache operations for the R10000 (ANDES).
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/sgialib.h>

extern unsigned long mips_tlb_entries;

/* Cache operations. XXX Write these dave... */
static inline void andes_flush_cache_all(void)
{
	/* XXX */
}

static void andes_flush_cache_mm(struct mm_struct *mm)
{
	/* XXX */
}

static void andes_flush_cache_range(struct mm_struct *mm,
				    unsigned long start,
				    unsigned long end)
{
	/* XXX */
}

static void andes_flush_cache_page(struct vm_area_struct *vma,
				   unsigned long page)
{
	/* XXX */
}

static void andes_flush_page_to_ram(unsigned long page)
{
	/* XXX */
}

static void andes_flush_cache_sigtramp(unsigned long page)
{
	/* XXX */
}

/* TLB operations. XXX Write these dave... */
static inline void andes_flush_tlb_all(void)
{
	/* XXX */
}

static void andes_flush_tlb_mm(struct mm_struct *mm)
{
	/* XXX */
}

static void andes_flush_tlb_range(struct mm_struct *mm, unsigned long start,
				  unsigned long end)
{
	/* XXX */
}

static void andes_flush_tlb_page(struct vm_area_struct *vma, unsigned long page)
{
	/* XXX */
}

static void andes_load_pgd(unsigned long pg_dir)
{
}

static void andes_pgd_init(unsigned long page)
{
}

void ld_mmu_andes(void)
{
	flush_cache_all = andes_flush_cache_all;
	flush_cache_mm = andes_flush_cache_mm;
	flush_cache_range = andes_flush_cache_range;
	flush_cache_page = andes_flush_cache_page;
	flush_cache_sigtramp = andes_flush_cache_sigtramp;
	flush_page_to_ram = andes_flush_page_to_ram;

	flush_tlb_all = andes_flush_tlb_all;
	flush_tlb_mm = andes_flush_tlb_mm;
	flush_tlb_range = andes_flush_tlb_range;
	flush_tlb_page = andes_flush_tlb_page;

	load_pgd = andes_load_pgd;
	pgd_init = andes_pgd_init;

	flush_cache_all();
	flush_tlb_all();
}
