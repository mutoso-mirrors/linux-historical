/*
 * Switch a MMU context.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1996, 1997, 1998, 1999 by Ralf Baechle
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#ifndef _ASM_MMU_CONTEXT_H
#define _ASM_MMU_CONTEXT_H

#include <linux/config.h>
#include <linux/slab.h>
#include <asm/pgalloc.h>

/* Fuck.  The f-word is here so you can grep for it :-)  */
extern unsigned long asid_cache;
extern pgd_t *current_pgd[];

#if defined(CONFIG_CPU_R3000)

#define ASID_INC	0x40
#define ASID_MASK	0xfc0

#else /* FIXME: not correct for R6000, R8000 */

#define ASID_INC	0x1
#define ASID_MASK	0xff

#endif

static inline void enter_lazy_tlb(struct mm_struct *mm, struct task_struct *tsk, unsigned cpu)
{
}

/*
 *  All unused by hardware upper bits will be considered
 *  as a software asid extension.
 */
#define ASID_VERSION_MASK  ((unsigned long)~(ASID_MASK|(ASID_MASK-1)))
#define ASID_FIRST_VERSION ((unsigned long)(~ASID_VERSION_MASK) + 1)

extern inline void
get_new_mmu_context(struct mm_struct *mm, unsigned long asid)
{
	if (! ((asid += ASID_INC) & ASID_MASK) ) {
		flush_tlb_all(); /* start new asid cycle */
		if (!asid)      /* fix version if needed */
			asid = ASID_FIRST_VERSION;
	}
	mm->context = asid_cache = asid;
}

/*
 * Initialize the context related info for a new mm_struct
 * instance.
 */
extern inline int
init_new_context(struct task_struct *tsk, struct mm_struct *mm)
{
#ifndef CONFIG_SMP
	mm->context = 0;
#else
	mm->context = (unsigned long)kmalloc(smp_num_cpus * 
				sizeof(unsigned long), GFP_KERNEL);
	/*
 	 * Init the "context" values so that a tlbpid allocation 
	 * happens on the first switch.
 	 */
	if (mm->context == 0)
		return -ENOMEM;
	memset((void *)mm->context, 0, smp_num_cpus * sizeof(unsigned long));
#endif
	return 0;
}

extern inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
                             struct task_struct *tsk, unsigned cpu)
{
	unsigned long asid = asid_cache;

	/* Check if our ASID is of an older version and thus invalid */
	if ((next->context ^ asid) & ASID_VERSION_MASK)
		get_new_mmu_context(next, asid);

	current_pgd[cpu] = next->pgd;
	set_entryhi(next->context);
}

/*
 * Destroy context related info for an mm_struct that is about
 * to be put to rest.
 */
extern inline void destroy_context(struct mm_struct *mm)
{
	/* Nothing to do.  */
}

#define deactivate_mm(tsk,mm)	do { } while (0)

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
extern inline void
activate_mm(struct mm_struct *prev, struct mm_struct *next)
{
	/* Unconditionally get a new ASID.  */
	get_new_mmu_context(next, asid_cache);

	current_pgd[smp_processor_id()] = next->pgd;
	set_entryhi(next->context);
}

#endif /* _ASM_MMU_CONTEXT_H */
