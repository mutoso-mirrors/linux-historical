#ifndef _S390_RWSEM_H
#define _S390_RWSEM_H

/*
 *  include/asm-s390/rwsem.h
 *
 *  S390 version
 *    Copyright (C) 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Based on asm-alpha/semaphore.h and asm-i386/rwsem.h
 */

/*
 *
 * The MSW of the count is the negated number of active writers and waiting
 * lockers, and the LSW is the total number of active locks
 *
 * The lock count is initialized to 0 (no active and no waiting lockers).
 *
 * When a writer subtracts WRITE_BIAS, it'll get 0xffff0001 for the case of an
 * uncontended lock. This can be determined because XADD returns the old value.
 * Readers increment by 1 and see a positive value when uncontended, negative
 * if there are writers (and maybe) readers waiting (in which case it goes to
 * sleep).
 *
 * The value of WAITING_BIAS supports up to 32766 waiting processes. This can
 * be extended to 65534 by manually checking the whole MSW rather than relying
 * on the S flag.
 *
 * The value of ACTIVE_BIAS supports up to 65535 active processes.
 *
 * This should be totally fair - if anything is waiting, a process that wants a
 * lock will go to the back of the queue. When the currently active lock is
 * released, if there's a writer at the front of the queue, then that and only
 * that will be woken up; if there's a bunch of consequtive readers at the
 * front, then they'll all be woken up, but no other readers will be.
 */

#ifndef _LINUX_RWSEM_H
#error please dont include asm/rwsem.h directly, use linux/rwsem.h instead
#endif

#ifdef __KERNEL__

#include <linux/list.h>
#include <linux/spinlock.h>

struct rwsem_waiter;

extern struct rw_semaphore *rwsem_down_read_failed(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_down_write_failed(struct rw_semaphore *sem);
extern struct rw_semaphore *rwsem_wake(struct rw_semaphore *);

/*
 * the semaphore definition
 */
struct rw_semaphore {
	signed long		count;
	spinlock_t		wait_lock;
	struct list_head	wait_list;
};

#define RWSEM_UNLOCKED_VALUE	0x00000000
#define RWSEM_ACTIVE_BIAS	0x00000001
#define RWSEM_ACTIVE_MASK	0x0000ffff
#define RWSEM_WAITING_BIAS	(-0x00010000)
#define RWSEM_ACTIVE_READ_BIAS	RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS	(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)

/*
 * initialisation
 */
#define __RWSEM_INITIALIZER(name) \
{ RWSEM_UNLOCKED_VALUE, SPIN_LOCK_UNLOCKED, LIST_HEAD_INIT((name).wait_list) }

#define DECLARE_RWSEM(name) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name)

static inline void init_rwsem(struct rw_semaphore *sem)
{
	sem->count = RWSEM_UNLOCKED_VALUE;
	spin_lock_init(&sem->wait_lock);
	INIT_LIST_HEAD(&sem->wait_list);
}

/*
 * lock for reading
 */
static inline void __down_read(struct rw_semaphore *sem)
{
	signed long old, new;

	__asm__ __volatile__(
		"   l    %0,0(%2)\n"
		"0: lr   %1,%0\n"
		"   ahi  %1,%3\n"
		"   cs   %0,%1,0(%2)\n"
		"   jl   0b"
                : "=&d" (old), "=&d" (new)
		: "a" (&sem->count), "i" (RWSEM_ACTIVE_READ_BIAS)
		: "cc", "memory" );
	if (old < 0)
		rwsem_down_read_failed(sem);
}

/*
 * lock for writing
 */
static inline void __down_write(struct rw_semaphore *sem)
{
	signed long old, new, tmp;

	tmp = RWSEM_ACTIVE_WRITE_BIAS;
	__asm__ __volatile__(
		"   l    %0,0(%2)\n"
		"0: lr   %1,%0\n"
		"   a    %1,%3\n"
		"   cs   %0,%1,0(%2)\n"
		"   jl   0b"
                : "=&d" (old), "=&d" (new)
		: "a" (&sem->count), "m" (tmp)
		: "cc", "memory" );
	if (old != 0)
		rwsem_down_write_failed(sem);
}

/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	signed long old, new;

	__asm__ __volatile__(
		"   l    %0,0(%2)\n"
		"0: lr   %1,%0\n"
		"   ahi  %1,%3\n"
		"   cs   %0,%1,0(%2)\n"
		"   jl   0b"
                : "=&d" (old), "=&d" (new)
		: "a" (&sem->count), "i" (-RWSEM_ACTIVE_READ_BIAS)
		: "cc", "memory" );
	if (new < 0)
		if ((new & RWSEM_ACTIVE_MASK) == 0)
			rwsem_wake(sem);
}

/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	signed long old, new, tmp;

	tmp = -RWSEM_ACTIVE_WRITE_BIAS;
	__asm__ __volatile__(
		"   l    %0,0(%2)\n"
		"0: lr   %1,%0\n"
		"   a    %1,%3\n"
		"   cs   %0,%1,0(%2)\n"
		"   jl   0b"
                : "=&d" (old), "=&d" (new)
		: "a" (&sem->count), "m" (tmp)
		: "cc", "memory" );
	if (new < 0)
		if ((new & RWSEM_ACTIVE_MASK) == 0)
			rwsem_wake(sem);
}

/*
 * implement atomic add functionality
 */
static inline void rwsem_atomic_add(long delta, struct rw_semaphore *sem)
{
	signed long old, new;

	__asm__ __volatile__(
		"   l    %0,0(%2)\n"
		"0: lr   %1,%0\n"
		"   ar   %1,%3\n"
		"   cs   %0,%1,0(%2)\n"
		"   jl   0b"
                : "=&d" (old), "=&d" (new)
		: "a" (&sem->count), "d" (delta)
		: "cc", "memory" );
}

/*
 * implement exchange and add functionality
 */
static inline long rwsem_atomic_update(long delta, struct rw_semaphore *sem)
{
	signed long old, new;

	__asm__ __volatile__(
		"   l    %0,0(%2)\n"
		"0: lr   %1,%0\n"
		"   ar   %1,%3\n"
		"   cs   %0,%1,0(%2)\n"
		"   jl   0b"
                : "=&d" (old), "=&d" (new)
		: "a" (&sem->count), "d" (delta)
		: "cc", "memory" );
	return new;
}

#endif /* __KERNEL__ */
#endif /* _S390_RWSEM_H */
