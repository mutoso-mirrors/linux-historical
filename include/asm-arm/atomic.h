/*
 *  linux/include/asm-arm/atomic.h
 *
 *  Copyright (c) 1996 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Changelog:
 *   27-06-1996	RMK	Created
 *   13-04-1997	RMK	Made functions atomic!
 *   07-12-1997	RMK	Upgraded for v2.1.
 *   26-08-1998	PJB	Added #ifdef __KERNEL__
 */
#ifndef __ASM_ARM_ATOMIC_H
#define __ASM_ARM_ATOMIC_H

#include <linux/config.h>

#ifdef CONFIG_SMP
#error SMP not supported
#endif

typedef struct { volatile int counter; } atomic_t;

#define ATOMIC_INIT(i)	{ (i) }

#ifdef __KERNEL__
#include <asm/proc/system.h>

#define atomic_read(v)	((v)->counter)
#define atomic_set(v,i)	(((v)->counter) = (i))

static __inline__ void atomic_add(int i, volatile atomic_t *v)
{
	unsigned long flags;

	__save_flags_cli(flags);
	v->counter += i;
	__restore_flags(flags);
}

static __inline__ void atomic_sub(int i, volatile atomic_t *v)
{
	unsigned long flags;

	__save_flags_cli(flags);
	v->counter -= i;
	__restore_flags(flags);
}

static __inline__ void atomic_inc(volatile atomic_t *v)
{
	unsigned long flags;

	__save_flags_cli(flags);
	v->counter += 1;
	__restore_flags(flags);
}

static __inline__ void atomic_dec(volatile atomic_t *v)
{
	unsigned long flags;

	__save_flags_cli(flags);
	v->counter -= 1;
	__restore_flags(flags);
}

static __inline__ int atomic_dec_and_test(volatile atomic_t *v)
{
	unsigned long flags;
	int val;

	__save_flags_cli(flags);
	val = v->counter;
	v->counter = val -= 1;
	__restore_flags(flags);

	return val == 0;
}

static inline int atomic_add_negative(int i, volatile atomic_t *v)
{
	unsigned long flags;
	int val;

	__save_flags_cli(flags);
	val = v->counter;
	v->counter = val += i;
	__restore_flags(flags);

	return val < 0;
}

static __inline__ void atomic_clear_mask(unsigned long mask, unsigned long *addr)
{
	unsigned long flags;

	__save_flags_cli(flags);
	*addr &= ~mask;
	__restore_flags(flags);
}

/* Atomic operations are already serializing on ARM */
#define smp_mb__before_atomic_dec()	barrier()
#define smp_mb__after_atomic_dec()	barrier()
#define smp_mb__before_atomic_inc()	barrier()
#define smp_mb__after_atomic_inc()	barrier()

#endif
#endif
