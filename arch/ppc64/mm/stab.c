/*
 * PowerPC64 Segment Translation Support.
 *
 * Dave Engebretsen and Mike Corrigan {engebret|mikejc}@us.ibm.com
 *    Copyright (c) 2001 Dave Engebretsen
 *
 * Copyright (C) 2002 Anton Blanchard <anton@au.ibm.com>, IBM
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/paca.h>
#include <asm/naca.h>
#include <asm/cputable.h>

/* Both the segment table and SLB code uses the following cache */
#define NR_STAB_CACHE_ENTRIES 8
DEFINE_PER_CPU(long, stab_cache_ptr);
DEFINE_PER_CPU(long, stab_cache[NR_STAB_CACHE_ENTRIES]);

/*
 * Create a segment table entry for the given esid/vsid pair.
 */
static int make_ste(unsigned long stab, unsigned long esid, unsigned long vsid)
{
	unsigned long esid_data, vsid_data;
	unsigned long entry, group, old_esid, castout_entry, i;
	unsigned int global_entry;
	struct stab_entry *ste, *castout_ste;
	unsigned long kernel_segment = (esid << SID_SHIFT) >= KERNELBASE;

	vsid_data = vsid << STE_VSID_SHIFT;
	esid_data = esid << SID_SHIFT | STE_ESID_KP | STE_ESID_V;
	if (! kernel_segment)
		esid_data |= STE_ESID_KS;

	/* Search the primary group first. */
	global_entry = (esid & 0x1f) << 3;
	ste = (struct stab_entry *)(stab | ((esid & 0x1f) << 7));

	/* Find an empty entry, if one exists. */
	for (group = 0; group < 2; group++) {
		for (entry = 0; entry < 8; entry++, ste++) {
			if (!(ste->esid_data & STE_ESID_V)) {
				ste->vsid_data = vsid_data;
				asm volatile("eieio":::"memory");
				ste->esid_data = esid_data;
				return (global_entry | entry);
			}
		}
		/* Now search the secondary group. */
		global_entry = ((~esid) & 0x1f) << 3;
		ste = (struct stab_entry *)(stab | (((~esid) & 0x1f) << 7));
	}

	/*
	 * Could not find empty entry, pick one with a round robin selection.
	 * Search all entries in the two groups.
	 */
	castout_entry = get_paca()->stab_rr;
	for (i = 0; i < 16; i++) {
		if (castout_entry < 8) {
			global_entry = (esid & 0x1f) << 3;
			ste = (struct stab_entry *)(stab | ((esid & 0x1f) << 7));
			castout_ste = ste + castout_entry;
		} else {
			global_entry = ((~esid) & 0x1f) << 3;
			ste = (struct stab_entry *)(stab | (((~esid) & 0x1f) << 7));
			castout_ste = ste + (castout_entry - 8);
		}

		/* Dont cast out the first kernel segment */
		if ((castout_ste->esid_data & ESID_MASK) != KERNELBASE)
			break;

		castout_entry = (castout_entry + 1) & 0xf;
	}

	get_paca()->stab_rr = (castout_entry + 1) & 0xf;

	/* Modify the old entry to the new value. */

	/* Force previous translations to complete. DRENG */
	asm volatile("isync" : : : "memory");

	old_esid = castout_ste->esid_data >> SID_SHIFT;
	castout_ste->esid_data = 0;		/* Invalidate old entry */

	asm volatile("sync" : : : "memory");    /* Order update */

	castout_ste->vsid_data = vsid_data;
	asm volatile("eieio" : : : "memory");   /* Order update */
	castout_ste->esid_data = esid_data;

	asm volatile("slbie  %0" : : "r" (old_esid << SID_SHIFT));
	/* Ensure completion of slbie */
	asm volatile("sync" : : : "memory");

	return (global_entry | (castout_entry & 0x7));
}

/*
 * Allocate a segment table entry for the given ea and mm
 */
static int __ste_allocate(unsigned long ea, struct mm_struct *mm)
{
	unsigned long vsid;
	unsigned char stab_entry;
	unsigned long offset;

	/* Check for invalid effective addresses. */
	if (!IS_VALID_EA(ea))
		return 1;

	/* Kernel or user address? */
	if (ea >= KERNELBASE) {
		vsid = get_kernel_vsid(ea);
	} else {
		if (! mm)
			return 1;

		vsid = get_vsid(mm->context.id, ea);
	}

	stab_entry = make_ste(get_paca()->stab_addr, GET_ESID(ea), vsid);

	if (ea < KERNELBASE) {
		offset = __get_cpu_var(stab_cache_ptr);
		if (offset < NR_STAB_CACHE_ENTRIES)
			__get_cpu_var(stab_cache[offset++]) = stab_entry;
		else
			offset = NR_STAB_CACHE_ENTRIES+1;
		__get_cpu_var(stab_cache_ptr) = offset;

		/* Order update */
		asm volatile("sync":::"memory");
	}

	return 0;
}

int ste_allocate(unsigned long ea)
{
	return __ste_allocate(ea, current->mm);
}

/*
 * Do the segment table work for a context switch: flush all user
 * entries from the table, then preload some probably useful entries
 * for the new task
 */
void switch_stab(struct task_struct *tsk, struct mm_struct *mm)
{
	struct stab_entry *stab = (struct stab_entry *) get_paca()->stab_addr;
	struct stab_entry *ste;
	unsigned long offset = __get_cpu_var(stab_cache_ptr);
	unsigned long pc = KSTK_EIP(tsk);
	unsigned long stack = KSTK_ESP(tsk);
	unsigned long unmapped_base;

	/* Force previous translations to complete. DRENG */
	asm volatile("isync" : : : "memory");

	if (offset <= NR_STAB_CACHE_ENTRIES) {
		int i;

		for (i = 0; i < offset; i++) {
			ste = stab + __get_cpu_var(stab_cache[i]);
			ste->esid_data = 0; /* invalidate entry */
		}
	} else {
		unsigned long entry;

		/* Invalidate all entries. */
		ste = stab;

		/* Never flush the first entry. */
		ste += 1;
		for (entry = 1;
		     entry < (PAGE_SIZE / sizeof(struct stab_entry));
		     entry++, ste++) {
			unsigned long ea;
			ea = ste->esid_data & ESID_MASK;
			if (ea < KERNELBASE) {
				ste->esid_data = 0;
			}
		}
	}

	asm volatile("sync; slbia; sync":::"memory");

	__get_cpu_var(stab_cache_ptr) = 0;

	/* Now preload some entries for the new task */
	if (test_tsk_thread_flag(tsk, TIF_32BIT))
		unmapped_base = TASK_UNMAPPED_BASE_USER32;
	else
		unmapped_base = TASK_UNMAPPED_BASE_USER64;

	__ste_allocate(pc, mm);

	if (GET_ESID(pc) == GET_ESID(stack))
		return;

	__ste_allocate(stack, mm);

	if ((GET_ESID(pc) == GET_ESID(unmapped_base))
	    || (GET_ESID(stack) == GET_ESID(unmapped_base)))
		return;

	__ste_allocate(unmapped_base, mm);

	/* Order update */
	asm volatile("sync" : : : "memory");
}

extern void slb_initialize(void);

/*
 * Build an entry for the base kernel segment and put it into
 * the segment table or SLB.  All other segment table or SLB
 * entries are faulted in.
 */
void stab_initialize(unsigned long stab)
{
	unsigned long vsid = get_kernel_vsid(KERNELBASE);

	if (cur_cpu_spec->cpu_features & CPU_FTR_SLB) {
		slb_initialize();
	} else {
		asm volatile("isync; slbia; isync":::"memory");
		make_ste(stab, GET_ESID(KERNELBASE), vsid);

		/* Order update */
		asm volatile("sync":::"memory");
	}
}
