#ifndef _ASM_KMAP_TYPES_H
#define _ASM_KMAP_TYPES_H

#include <linux/config.h>

#if CONFIG_DEBUG_HIGHMEM
# define D(n) __KM_FENCE_##n ,
#else
# define D(n)
#endif

enum km_type {
D(0)	KM_BOUNCE_READ,
D(1)	KM_SKB_SUNRPC_DATA,
D(2)	KM_SKB_DATA_SOFTIRQ,
D(3)	KM_USER0,
D(4)	KM_USER1,
D(5)	KM_BIO_IRQ,
D(6)	KM_PTE0,
D(7)	KM_PTE1,
D(8)	KM_TYPE_NR
};

#undef D

#endif
