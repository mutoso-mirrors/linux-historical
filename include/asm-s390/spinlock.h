/*
 *  include/asm-s390/spinlock.h
 *
 *  S390 version
 *    Copyright (C) 1999 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com)
 *
 *  Derived from "include/asm-i386/spinlock.h"
 */

#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

typedef struct {
	volatile unsigned long lock;
} spinlock_t;

#define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 }
#define spin_lock_init(lp) do { (lp)->lock = 0; } while(0)
#define spin_unlock_wait(lp)	do { barrier(); } while(((volatile spinlock_t *)(lp))->lock)
#define spin_is_locked(x) ((x)->lock != 0)

extern inline void spin_lock(spinlock_t *lp)
{
        __asm__ __volatile("    bras  1,1f\n"
                           "0:  diag  0,0,68\n"
                           "1:  slr   0,0\n"
                           "    cs    0,1,%1\n"
                           "    jl    0b\n"
                           : "=m" (lp->lock)
                           : "0" (lp->lock) : "0", "1", "cc" );
}

extern inline int spin_trylock(spinlock_t *lp)
{
	unsigned long result;
	__asm__ __volatile("    slr   %1,%1\n"
			   "    basr  1,0\n"
			   "0:  cs    %1,1,%0"
			   : "=m" (lp->lock), "=&d" (result)
			   : "0" (lp->lock) : "1", "cc" );
	return !result;
}

extern inline void spin_unlock(spinlock_t *lp)
{
	__asm__ __volatile("    xc 0(4,%0),0(%0)\n"
                           "    bcr 15,0"
			   : /* no output */ : "a" (lp) : "memory", "cc" );
}
		
/*
 * Read-write spinlocks, allowing multiple readers
 * but only one writer.
 *
 * NOTE! it is quite common to have readers in interrupts
 * but no interrupt writers. For those circumstances we
 * can "mix" irq-safe locks - any writer needs to get a
 * irq-safe write-lock, but readers can get non-irqsafe
 * read-locks.
 */
typedef struct {
	volatile unsigned long lock;
	volatile unsigned long owner_pc;
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t) { 0, 0 }

#define rwlock_init(x)	do { *(x) = RW_LOCK_UNLOCKED; } while(0)

#define read_lock(rw)   \
        asm volatile("   l     2,%0\n"   \
                     "   j     1f\n"     \
                     "0: diag  0,0,68\n" \
                     "1: la    2,0(2)\n"  /* clear high (=write) bit */ \
                     "   la    3,1(2)\n"  /* one more reader */ \
                     "   cs    2,3,%0\n"  /* try to write new value */ \
                     "   jl    0b"       \
                     : "+m" ((rw)->lock) : : "2", "3", "cc" );

#define read_unlock(rw) \
        asm volatile("   l     2,%0\n"   \
                     "   j     1f\n"     \
                     "0: diag  0,0,68\n" \
                     "1: lr    3,2\n"    \
                     "   ahi   3,-1\n"    /* one less reader */ \
                     "   cs    2,3,%0\n" \
                     "   jl    0b"       \
                     : "+m" ((rw)->lock) : : "2", "3", "cc" );

#define write_lock(rw) \
        asm volatile("   lhi   3,1\n"    \
                     "   sll   3,31\n"    /* new lock value = 0x80000000 */ \
                     "   j     1f\n"     \
                     "0: diag  0,0,68\n" \
                     "1: slr   2,2\n"     /* old lock value must be 0 */ \
                     "   cs    2,3,%0\n" \
                     "   jl    0b"       \
                     : "+m" ((rw)->lock) : : "2", "3", "cc" );

#define write_unlock(rw) \
        asm volatile("   slr   3,3\n"     /* new lock value = 0 */ \
                     "   j     1f\n"     \
                     "0: diag  0,0,68\n" \
                     "1: lhi   2,1\n"    \
                     "   sll   2,31\n"    /* old lock value must be 0x80000000 */ \
                     "   cs    2,3,%0\n" \
                     "   jl    0b"       \
                     : "+m" ((rw)->lock) : : "2", "3", "cc" );

#endif /* __ASM_SPINLOCK_H */
