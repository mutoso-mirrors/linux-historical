/*
 *  linux/include/asm-arm/proc-armv/tlbflush.h
 *
 *  Copyright (C) 1999-2003 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <asm/glue.h>

#define TLB_V3_PAGE	(1 << 0)
#define TLB_V4_U_PAGE	(1 << 1)
#define TLB_V4_D_PAGE	(1 << 2)
#define TLB_V4_I_PAGE	(1 << 3)

#define TLB_V3_FULL	(1 << 8)
#define TLB_V4_U_FULL	(1 << 9)
#define TLB_V4_D_FULL	(1 << 10)
#define TLB_V4_I_FULL	(1 << 11)

#define TLB_WB		(1 << 31)

/*
 *	MMU TLB Model
 *	=============
 *
 *	We have the following to choose from:
 *	  v3    - ARMv3
 *	  v4    - ARMv4 without write buffer
 *	  v4wb  - ARMv4 with write buffer without I TLB flush entry instruction
 *	  v4wbi - ARMv4 with write buffer with I TLB flush entry instruction
 */
#undef _TLB
#undef MULTI_TLB

#define v3_tlb_flags	(TLB_V3_FULL | TLB_V3_PAGE)

#if defined(CONFIG_CPU_ARM610) || defined(CONFIG_CPU_ARM710)
# define v3_possible_flags	v3_tlb_flags
# define v3_always_flags	v3_tlb_flags
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v3
# endif
#else
# define v3_possible_flags	0
# define v3_always_flags	(-1UL)
#endif

#define v4_tlb_flags	(TLB_V4_U_FULL | TLB_V4_U_PAGE)

#if defined(CONFIG_CPU_ARM720T)
# define v4_possible_flags	v4_tlb_flags
# define v4_always_flags	v4_tlb_flags
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v4
# endif
#else
# define v4_possible_flags	0
# define v4_always_flags	(-1UL)
#endif

#define v4wbi_tlb_flags	(TLB_WB | \
			 TLB_V4_I_FULL | TLB_V4_D_FULL | \
			 TLB_V4_I_PAGE | TLB_V4_D_PAGE)

#if defined(CONFIG_CPU_ARM920T) || defined(CONFIG_CPU_ARM922T) || \
    defined(CONFIG_CPU_ARM926T) || defined(CONFIG_CPU_ARM1020) || \
    defined(CONFIG_CPU_XSCALE)
# define v4wbi_possible_flags	v4wbi_tlb_flags
# define v4wbi_always_flags	v4wbi_tlb_flags
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v4wbi
# endif
#else
# define v4wbi_possible_flags	0
# define v4wbi_always_flags	(-1UL)
#endif

#define v4wb_tlb_flags	(TLB_WB | \
			 TLB_V4_I_FULL | TLB_V4_D_FULL | \
			 TLB_V4_D_PAGE)

#if defined(CONFIG_CPU_SA110) || defined(CONFIG_CPU_SA1100)
# define v4wb_possible_flags	v4wb_tlb_flags
# define v4wb_always_flags	v4wb_tlb_flags
# ifdef _TLB
#  define MULTI_TLB 1
# else
#  define _TLB v4wb
# endif
#else
# define v4wb_possible_flags	0
# define v4wb_always_flags	(-1UL)
#endif

#ifndef _TLB
#error Unknown TLB model
#endif

#ifndef __ASSEMBLY__

struct cpu_tlb_fns {
	void (*flush_user_range)(unsigned long, unsigned long, struct vm_area_struct *);
	void (*flush_kern_range)(unsigned long, unsigned long);
	unsigned long tlb_flags;
};

/*
 * Select the calling method
 */
#ifdef MULTI_TLB

#define __cpu_flush_user_tlb_range	cpu_tlb.flush_user_range
#define __cpu_flush_kern_tlb_range	cpu_tlb.flush_kern_range

#else

#define __cpu_flush_user_tlb_range	__glue(_TLB,_flush_user_tlb_range)
#define __cpu_flush_kern_tlb_range	__glue(_TLB,_flush_kern_tlb_range)

extern void __cpu_flush_user_tlb_range(unsigned long, unsigned long, struct vm_area_struct *);
extern void __cpu_flush_kern_tlb_range(unsigned long, unsigned long);

#endif

extern struct cpu_tlb_fns cpu_tlb;

#define __cpu_tlb_flags			cpu_tlb.tlb_flags

/*
 *	TLB Management
 *	==============
 *
 *	The arch/arm/mm/tlb-*.S files implement these methods.
 *
 *	The TLB specific code is expected to perform whatever tests it
 *	needs to determine if it should invalidate the TLB for each
 *	call.  Start addresses are inclusive and end addresses are
 *	exclusive; it is safe to round these addresses down.
 *
 *	flush_tlb_all()
 *
 *		Invalidate the entire TLB.
 *
 *	flush_tlb_mm(mm)
 *
 *		Invalidate all TLB entries in a particular address
 *		space.
 *		- mm	- mm_struct describing address space
 *
 *	flush_tlb_range(mm,start,end)
 *
 *		Invalidate a range of TLB entries in the specified
 *		address space.
 *		- mm	- mm_struct describing address space
 *		- start - start address (may not be aligned)
 *		- end	- end address (exclusive, may not be aligned)
 *
 *	flush_tlb_page(vaddr,vma)
 *
 *		Invalidate the specified page in the specified address range.
 *		- vaddr - virtual address (may not be aligned)
 *		- vma	- vma_struct describing address range
 *
 *	flush_kern_tlb_page(kaddr)
 *
 *		Invalidate the TLB entry for the specified page.  The address
 *		will be in the kernels virtual memory space.  Current uses
 *		only require the D-TLB to be invalidated.
 *		- kaddr - Kernel virtual memory address
 */

/*
 * We optimise the code below by:
 *  - building a set of TLB flags that might be set in __cpu_tlb_flags
 *  - building a set of TLB flags that will always be set in __cpu_tlb_flags
 *  - if we're going to need __cpu_tlb_flags, access it once and only once
 *
 * This allows us to build optimal assembly for the single-CPU type case,
 * and as close to optimal given the compiler constrants for multi-CPU
 * case.  We could do better for the multi-CPU case if the compiler
 * implemented the "%?" method, but this has been discontinued due to too
 * many people getting it wrong.
 */
#define possible_tlb_flags	(v3_possible_flags | \
				 v4_possible_flags | \
				 v4wbi_possible_flags | \
				 v4wb_possible_flags)

#define always_tlb_flags	(v3_always_flags & \
				 v4_always_flags & \
				 v4wbi_always_flags & \
				 v4wb_always_flags)

#define tlb_flag(f)	((always_tlb_flags & (f)) || (__tlb_flag & possible_tlb_flags & (f)))

static inline void flush_tlb_all(void)
{
	const int zero = 0;
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	if (tlb_flag(TLB_WB))
		asm("mcr%? p15, 0, %0, c7, c10, 4" : : "r" (zero));

	if (tlb_flag(TLB_V3_FULL))
		asm("mcr%? p15, 0, %0, c6, c0, 0" : : "r" (zero));
	if (tlb_flag(TLB_V4_U_FULL))
		asm("mcr%? p15, 0, %0, c8, c7, 0" : : "r" (zero));
	if (tlb_flag(TLB_V4_D_FULL))
		asm("mcr%? p15, 0, %0, c8, c6, 0" : : "r" (zero));
	if (tlb_flag(TLB_V4_I_FULL))
		asm("mcr%? p15, 0, %0, c8, c5, 0" : : "r" (zero));
}

static inline void flush_tlb_mm(struct mm_struct *mm)
{
	const int zero = 0;
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	if (tlb_flag(TLB_WB))
		asm("mcr%? p15, 0, %0, c7, c10, 4" : : "r" (zero));

	if (mm == current->active_mm) {
		if (tlb_flag(TLB_V3_FULL))
			asm("mcr%? p15, 0, %0, c6, c0, 0" : : "r" (zero));
		if (tlb_flag(TLB_V4_U_FULL))
			asm("mcr%? p15, 0, %0, c8, c7, 0" : : "r" (zero));
		if (tlb_flag(TLB_V4_D_FULL))
			asm("mcr%? p15, 0, %0, c8, c6, 0" : : "r" (zero));
		if (tlb_flag(TLB_V4_I_FULL))
			asm("mcr%? p15, 0, %0, c8, c5, 0" : : "r" (zero));
	}
}

static inline void
flush_tlb_page(struct vm_area_struct *vma, unsigned long uaddr)
{
	const int zero = 0;
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	uaddr &= PAGE_MASK;

	if (tlb_flag(TLB_WB))
		asm("mcr%? p15, 0, %0, c7, c10, 4" : : "r" (zero));

	if (vma->vm_mm == current->active_mm) {
		if (tlb_flag(TLB_V3_PAGE))
			asm("mcr%? p15, 0, %0, c6, c0, 0" : : "r" (uaddr));
		if (tlb_flag(TLB_V4_U_PAGE))
			asm("mcr%? p15, 0, %0, c8, c7, 1" : : "r" (uaddr));
		if (tlb_flag(TLB_V4_D_PAGE))
			asm("mcr%? p15, 0, %0, c8, c6, 1" : : "r" (uaddr));
		if (tlb_flag(TLB_V4_I_PAGE))
			asm("mcr%? p15, 0, %0, c8, c5, 1" : : "r" (uaddr));
		if (!tlb_flag(TLB_V4_I_PAGE) && tlb_flag(TLB_V4_I_FULL))
			asm("mcr%? p15, 0, %0, c8, c5, 0" : : "r" (zero));
	}
}

static inline void flush_tlb_kernel_page(unsigned long kaddr)
{
	const int zero = 0;
	const unsigned int __tlb_flag = __cpu_tlb_flags;

	kaddr &= PAGE_MASK;

	if (tlb_flag(TLB_WB))
		asm("mcr%? p15, 0, %0, c7, c10, 4" : : "r" (zero));

	if (tlb_flag(TLB_V3_PAGE))
		asm("mcr%? p15, 0, %0, c6, c0, 0" : : "r" (kaddr));
	if (tlb_flag(TLB_V4_U_PAGE))
		asm("mcr%? p15, 0, %0, c8, c7, 1" : : "r" (kaddr));
	if (tlb_flag(TLB_V4_D_PAGE))
		asm("mcr%? p15, 0, %0, c8, c6, 1" : : "r" (kaddr));
	if (tlb_flag(TLB_V4_I_PAGE))
		asm("mcr%? p15, 0, %0, c8, c5, 1" : : "r" (kaddr));
	if (!tlb_flag(TLB_V4_I_PAGE) && tlb_flag(TLB_V4_I_FULL))
		asm("mcr%? p15, 0, %0, c8, c5, 0" : : "r" (zero));
}

#undef tlb_flag
#undef always_tlb_flags
#undef possible_tlb_flags

/*
 * Convert calls to our calling convention.
 */
#define flush_tlb_range(vma,start,end)	__cpu_flush_user_tlb_range(start,end,vma)
#define flush_tlb_kernel_range(s,e)	__cpu_flush_kern_tlb_range(s,e)

/*
 * if PG_dcache_dirty is set for the page, we need to ensure that any
 * cache entries for the kernels virtual memory range are written
 * back to the page.
 */
extern void update_mmu_cache(struct vm_area_struct *vma, unsigned long addr, pte_t pte);

/*
 * ARM processors do not cache TLB tables in RAM.
 */
#define flush_tlb_pgtables(mm,start,end)	do { } while (0)

/*
 * Old ARM MEMC stuff.  This supports the reversed mapping handling that
 * we have on the older 26-bit machines.  We don't have a MEMC chip, so...
 */
#define memc_update_all()		do { } while (0)
#define memc_update_mm(mm)		do { } while (0)
#define memc_update_addr(mm,pte,log)	do { } while (0)
#define memc_clear(mm,physaddr)		do { } while (0)

#endif
