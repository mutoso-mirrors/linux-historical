#ifndef _I386_SEMAPHORE_H
#define _I386_SEMAPHORE_H

#include <linux/linkage.h>

/*
 * SMP- and interrupt-safe semaphores..
 *
 * (C) Copyright 1996 Linus Torvalds
 *
 * Modified 1996-12-23 by Dave Grothe <dave@gcom.com> to fix bugs in
 *                     the original code and to make semaphore waits
 *                     interruptible so that processes waiting on
 *                     semaphores can be killed.
 *
 * If you would like to see an analysis of this implementation, please
 * ftp to gcom.com and download the file
 * /pub/linux/src/semaphore/semaphore-2.0.24.tar.gz.
 *
 */

#include <asm/system.h>
#include <asm/atomic.h>

struct semaphore {
	atomic_t count;
	int waking;
	struct wait_queue * wait;
};

#define MUTEX ((struct semaphore) { ATOMIC_INIT(1), 0, NULL })
#define MUTEX_LOCKED ((struct semaphore) { ATOMIC_INIT(0), 0, NULL })

asmlinkage void __down_failed(void /* special register calling convention */);
asmlinkage int  __down_failed_interruptible(void  /* params in registers */);
asmlinkage void __up_wakeup(void /* special register calling convention */);

extern void __down(struct semaphore * sem);
extern void __up(struct semaphore * sem);

#define sema_init(sem, val)	atomic_set(&((sem)->count), (val))

/*
 * These two _must_ execute atomically wrt each other.
 *
 * This is trivially done with load_locked/store_cond,
 * but on the x86 we need an external synchronizer.
 * Currently this is just the global interrupt lock,
 * bah. Go for a smaller spinlock some day.
 *
 * (On the other hand this shouldn't be in any critical
 * path, so..)
 */
static inline void wake_one_more(struct semaphore * sem)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	sem->waking++;
	restore_flags(flags);
}

static inline int waking_non_zero(struct semaphore *sem)
{
	unsigned long flags;
	int ret = 0;

	save_flags(flags);
	cli();
	if (sem->waking > 0) {
		sem->waking--;
		ret = 1;
	}
	restore_flags(flags);
	return ret;
}

/*
 * This is ugly, but we want the default case to fall through.
 * "down_failed" is a special asm handler that calls the C
 * routine that actually waits. See arch/i386/lib/semaphore.S
 */
extern inline void do_down(struct semaphore * sem, void (*failed)(void))
{
	__asm__ __volatile__(
		"# atomic down operation\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"decl 0(%0)\n\t"
		"js 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tpushl $1b\n\t"
		"jmp %1\n"
		".previous"
		:/* no outputs */
		:"c" (sem), "m" (*(unsigned long *)failed)
		:"memory");
}

#define down(sem) do_down((sem),__down_failed)
#define down_interruptible(sem) do_down((sem),__down_failed_interruptible)

/*
 * Note! This is subtle. We jump to wake people up only if
 * the semaphore was negative (== somebody was waiting on it).
 * The default case (no contention) will result in NO
 * jumps for both down() and up().
 */
extern inline void up(struct semaphore * sem)
{
	__asm__ __volatile__(
		"# atomic up operation\n\t"
#ifdef __SMP__
		"lock ; "
#endif
		"incl 0(%0)\n\t"
		"jle 2f\n"
		"1:\n"
		".section .text.lock,\"ax\"\n"
		"2:\tpushl $1b\n\t"
		"jmp %1\n"
		".previous"
		:/* no outputs */
		:"c" (sem), "m" (*(unsigned long *)__up_wakeup)
		:"memory");
}

#endif
