/*
 * include/asm-ppc/cache.h
 */
#ifndef __ARCH_PPC_CACHE_H
#define __ARCH_PPC_CACHE_H

/* bytes per L1 cache line */
#define        L1_CACHE_BYTES  32      
#define        L1_CACHE_ALIGN(x)       (((x)+(L1_CACHE_BYTES-1))&~(L1_CACHE_BYTES-1))
#define L1_CACHE_PAGES		8

#endif
