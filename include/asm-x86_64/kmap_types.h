#ifndef _ASM_KMAP_TYPES_H
#define _ASM_KMAP_TYPES_H

enum km_type {
	KM_BOUNCE_READ,
	KM_SKB_SUNRPC_DATA,
	KM_SKB_DATA_SOFTIRQ,
	KM_USER0,
	KM_USER1,
	KM_BIO_SRC_IRQ,
	KM_BIO_DST_IRQ,
	KM_IRQ0,
	KM_IRQ1,
	KM_CRYPTO_USER,
	KM_CRYPTO_SOFTIRQ,
	KM_TYPE_NR
};

#endif
