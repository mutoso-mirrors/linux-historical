/*
 *  linux/arch/arm/mm/proc-syms.c
 *
 *  Copyright (C) 2000-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/mm.h>

#include <asm/cacheflush.h>
#include <asm/proc-fns.h>
#include <asm/tlbflush.h>

EXPORT_SYMBOL(__flush_dcache_page);

#ifndef MULTI_CPU
EXPORT_SYMBOL(cpu_dcache_clean_area);
EXPORT_SYMBOL(cpu_set_pte);
#else
EXPORT_SYMBOL(processor);
#endif

#ifndef MULTI_CACHE
EXPORT_SYMBOL_NOVERS(__cpuc_flush_kern_all);
EXPORT_SYMBOL_NOVERS(__cpuc_flush_user_all);
EXPORT_SYMBOL_NOVERS(__cpuc_flush_user_range);
EXPORT_SYMBOL_NOVERS(__cpuc_coherent_kern_range);
EXPORT_SYMBOL_NOVERS(__cpuc_flush_dcache_page);
#else
EXPORT_SYMBOL(cpu_cache);
#endif

/*
 * No module should need to touch the TLB (and currently
 * no modules do.  We export this for "loadkernel" support
 * (booting a new kernel from within a running kernel.)
 */
#ifdef MULTI_TLB
EXPORT_SYMBOL(cpu_tlb);
#endif
