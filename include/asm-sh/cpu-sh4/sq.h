/*
 * include/asm-sh/cpu-sh4/sq.h
 *
 * Copyright (C) 2001, 2002, 2003  Paul Mundt
 * Copyright (C) 2001, 2002  M. R. Brown
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_CPU_SH4_SQ_H
#define __ASM_CPU_SH4_SQ_H

#include <asm/addrspace.h>

/*
 * Store queues range from e0000000-e3fffffc, allowing approx. 64MB to be
 * mapped to any physical address space. Since data is written (and aligned)
 * to 32-byte boundaries, we need to be sure that all allocations are aligned.
 */ 
#define SQ_SIZE                 32
#define SQ_ALIGN_MASK           (~(SQ_SIZE - 1))
#define SQ_ALIGN(addr)          (((addr)+SQ_SIZE-1) & SQ_ALIGN_MASK)

#define SQ_QACR0		(P4SEG_REG_BASE  + 0x38)
#define SQ_QACR1		(P4SEG_REG_BASE  + 0x3c)
#define SQ_ADDRMAX              (P4SEG_STORE_QUE + 0x04000000)

struct sq_mapping {
	const char *name;

	unsigned long sq_addr;
	unsigned long addr;
	unsigned int size;

	struct list_head list;
};

/* arch/sh/kernel/cpu/sh4/sq.c */
extern struct sq_mapping *sq_remap(unsigned long phys, unsigned int size, const char *name);
extern void sq_unmap(struct sq_mapping *map);

extern void sq_clear(unsigned long addr, unsigned int len);
extern void sq_flush(void *addr);
extern void sq_flush_range(unsigned long start, unsigned int len);

#endif /* __ASM_CPU_SH4_SQ_H */

