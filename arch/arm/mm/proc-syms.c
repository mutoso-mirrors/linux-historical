/*
 *  linux/arch/arm/mm/proc-syms.c
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/mm.h>

#include <asm/pgalloc.h>
#include <asm/proc-fns.h>

#ifndef MULTI_CPU
EXPORT_SYMBOL(cpu_cache_clean_invalidate_all);
EXPORT_SYMBOL(cpu_cache_clean_invalidate_range);
EXPORT_SYMBOL(cpu_dcache_clean_page);
EXPORT_SYMBOL(cpu_dcache_clean_entry);
EXPORT_SYMBOL(cpu_dcache_clean_range);
EXPORT_SYMBOL(cpu_dcache_invalidate_range);
EXPORT_SYMBOL(cpu_icache_invalidate_range);
EXPORT_SYMBOL(cpu_icache_invalidate_page);
EXPORT_SYMBOL(cpu_set_pgd);
EXPORT_SYMBOL(cpu_set_pmd);
EXPORT_SYMBOL(cpu_set_pte);
#else
EXPORT_SYMBOL(processor);
#endif

#ifndef MULTI_TLB
EXPORT_SYMBOL_NOVERS(__cpu_flush_kern_tlb_all);
EXPORT_SYMBOL_NOVERS(__cpu_flush_user_tlb_mm);
EXPORT_SYMBOL_NOVERS(__cpu_flush_user_tlb_range);
EXPORT_SYMBOL_NOVERS(__cpu_flush_user_tlb_page);
#else
EXPORT_SYMBOL(cpu_tlb);
#endif
