#ifndef __ASM_SPINLOCK_H
#define __ASM_SPINLOCK_H

#include <asm/atomic.h>
#include <asm/rwlock.h>
#include <asm/page.h>

extern int printk(const char * fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

/* It seems that people are forgetting to
 * initialize their spinlocks properly, tsk tsk.
 * Remember to turn this off in 2.4. -ben
 */
#define SPINLOCK_DEBUG	1

/*
 * Your basic SMP spinlocks, allowing only a single CPU anywhere
 */

typedef struct {
	volatile unsigned int lock;
#if SPINLOCK_DEBUG
	unsigned magic;
#endif
} spinlock_t;

#define SPINLOCK_MAGIC	0xdead4ead

#if SPINLOCK_DEBUG
#define SPINLOCK_MAGIC_INIT	, SPINLOCK_MAGIC
#else
#define SPINLOCK_MAGIC_INIT	/* */
#endif

#define SPIN_LOCK_UNLOCKED (spinlock_t) { 0 SPINLOCK_MAGIC_INIT }

#define spin_lock_init(x)	do { *(x) = SPIN_LOCK_UNLOCKED; } while(0)
/*
 * Simple spin lock operations.  There are two variants, one clears IRQ's
 * on the local processor, one does not.
 *
 * We make no fairness assumptions. They have a cost.
 */

#define spin_unlock_wait(x)	do { barrier(); } while(((volatile spinlock_t *)(x))->lock)

#define spin_lock_string \
	"\n1:\t" \
	"lock ; btsl $0,%0\n\t" \
	"jc 2f\n" \
	".section .text.lock,\"ax\"\n" \
	"2:\t" \
	"testb $1,%0\n\t" \
	"jne 2b\n\t" \
	"jmp 1b\n" \
	".previous"

#define spin_unlock_string \
	"movb $0,%0"

extern inline void spin_lock(spinlock_t *lock)
{
#if SPINLOCK_DEBUG
	__label__ here;
here:
	if (lock->magic != SPINLOCK_MAGIC) {
printk("eip: %p\n", &&here);
		BUG();
	}
#endif
	__asm__ __volatile__(
		spin_lock_string
		:"=m" (__dummy_lock(lock)));
}

extern inline void spin_unlock(spinlock_t *lock)
{
#if SPINLOCK_DEBUG
	if (lock->magic != SPINLOCK_MAGIC)
		BUG();
#endif
	__asm__ __volatile__(
		spin_unlock_string
		:"=m" (__dummy_lock(lock)));
}

#define spin_trylock(lock) (!test_and_set_bit(0,(lock)))

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
	volatile unsigned int lock;
#if SPINLOCK_DEBUG
	unsigned magic;
#endif
} rwlock_t;

#define RWLOCK_MAGIC	0xdeaf1eed

#if SPINLOCK_DEBUG
#define RWLOCK_MAGIC_INIT	, RWLOCK_MAGIC
#else
#define RWLOCK_MAGIC_INIT	/* */
#endif

#define RW_LOCK_UNLOCKED (rwlock_t) { RW_LOCK_BIAS RWLOCK_MAGIC_INIT }

/*
 * On x86, we implement read-write locks as a 32-bit counter
 * with the high bit (sign) being the "contended" bit.
 *
 * The inline assembly is non-obvious. Think about it.
 *
 * Changed to use the same technique as rw semaphores.  See
 * semaphore.h for details.  -ben
 */
/* the spinlock helpers are in arch/i386/kernel/semaphore.S */

extern inline void read_lock(rwlock_t *rw)
{
#if SPINLOCK_DEBUG
	if (rw->magic != RWLOCK_MAGIC)
		BUG();
#endif
	__build_read_lock(rw, "__read_lock_failed");
}

extern inline void write_lock(rwlock_t *rw)
{
#if SPINLOCK_DEBUG
	if (rw->magic != RWLOCK_MAGIC)
		BUG();
#endif
	__build_write_lock(rw, "__write_lock_failed");
}

#define read_unlock(rw)		asm volatile("lock ; incl %0" :"=m" (__dummy_lock(&(rw)->lock)))
#define write_unlock(rw)	asm volatile("lock ; addl $" RW_LOCK_BIAS_STR ",%0":"=m" (__dummy_lock(&(rw)->lock)))

extern inline int write_trylock(rwlock_t *lock)
{
	atomic_t *count = (atomic_t *)lock;
	if (atomic_sub_and_test(RW_LOCK_BIAS, count))
		return 1;
	atomic_add(RW_LOCK_BIAS, count);
	return 0;
}

#endif /* __ASM_SPINLOCK_H */
