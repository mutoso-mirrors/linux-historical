/* rwsem-xadd.h: R/W semaphores implemented using XADD/CMPXCHG
 *
 * Written by David Howells (dhowells@redhat.com), 2001.
 * Derived from asm-i386/semaphore.h
 *
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

#ifndef _I386_RWSEM_XADD_H
#define _I386_RWSEM_XADD_H

#ifdef __KERNEL__

/*
 * the semaphore definition
 */
struct rw_semaphore {
	atomic_t		count;
#define RWSEM_UNLOCKED_VALUE		0x00000000
#define RWSEM_ACTIVE_BIAS		0x00000001
#define RWSEM_ACTIVE_MASK		0x0000ffff
#define RWSEM_WAITING_BIAS		(-0x00010000)
#define RWSEM_ACTIVE_READ_BIAS		RWSEM_ACTIVE_BIAS
#define RWSEM_ACTIVE_WRITE_BIAS		(RWSEM_WAITING_BIAS + RWSEM_ACTIVE_BIAS)
	wait_queue_head_t	wait;
#define RWSEM_WAITING_FOR_READ	WQ_FLAG_CONTEXT_0	/* bits to use in wait_queue_t.flags */
#define RWSEM_WAITING_FOR_WRITE	WQ_FLAG_CONTEXT_1
#if RWSEM_DEBUG
	int			debug;
#endif
#if RWSEM_DEBUG_MAGIC
	long			__magic;
	atomic_t		readers;
	atomic_t		writers;
#endif
};

/*
 * initialisation
 */
#if RWSEM_DEBUG
#define __RWSEM_DEBUG_INIT      , 0
#else
#define __RWSEM_DEBUG_INIT	/* */
#endif
#if RWSEM_DEBUG_MAGIC
#define __RWSEM_DEBUG_MINIT(name)	, (int)&(name).__magic, ATOMIC_INIT(0), ATOMIC_INIT(0)
#else
#define __RWSEM_DEBUG_MINIT(name)	/* */
#endif

#define __RWSEM_INITIALIZER(name,count) \
{ ATOMIC_INIT(RWSEM_UNLOCKED_VALUE), __WAIT_QUEUE_HEAD_INITIALIZER((name).wait) \
	__RWSEM_DEBUG_INIT __RWSEM_DEBUG_MINIT(name) }

#define __DECLARE_RWSEM_GENERIC(name,count) \
	struct rw_semaphore name = __RWSEM_INITIALIZER(name,count)

#define DECLARE_RWSEM(name) __DECLARE_RWSEM_GENERIC(name,RW_LOCK_BIAS)
#define DECLARE_RWSEM_READ_LOCKED(name) __DECLARE_RWSEM_GENERIC(name,RW_LOCK_BIAS-1)
#define DECLARE_RWSEM_WRITE_LOCKED(name) __DECLARE_RWSEM_GENERIC(name,0)

static inline void init_rwsem(struct rw_semaphore *sem)
{
	atomic_set(&sem->count, RWSEM_UNLOCKED_VALUE);
	init_waitqueue_head(&sem->wait);
#if RWSEM_DEBUG
	sem->debug = 0;
#endif
#if RWSEM_DEBUG_MAGIC
	sem->__magic = (long)&sem->__magic;
	atomic_set(&sem->readers, 0);
	atomic_set(&sem->writers, 0);
#endif
}

/*
 * lock for reading
 */
static inline void __down_read(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"# beginning down_read\n\t"
LOCK_PREFIX	"  incl      (%%eax)\n\t" /* adds 0x00000001, returns the old value */
		"  js        2f\n\t" /* jump if we weren't granted the lock */
		"1:\n\t"
		".section .text.lock,\"ax\"\n"
		"2:\n\t"
		"  call      __down_read_failed\n\t"
		"  jmp       1b\n"
		".previous"
		"# ending down_read\n\t"
		: "=m"(sem->count)
		: "a"(sem), "m"(sem->count)
		: "memory");
}

/*
 * lock for writing
 */
static inline void __down_write(struct rw_semaphore *sem)
{
	int tmp;

	tmp = RWSEM_ACTIVE_WRITE_BIAS;
	__asm__ __volatile__(
		"# beginning down_write\n\t"
LOCK_PREFIX	"  xadd      %0,(%%eax)\n\t" /* subtract 0x00010001, returns the old value */
		"  testl     %0,%0\n\t" /* was the count 0 before? */
		"  jnz       2f\n\t" /* jump if we weren't granted the lock */
		"1:\n\t"
		".section .text.lock,\"ax\"\n"
		"2:\n\t"
		"  call      __down_write_failed\n\t"
		"  jmp       1b\n"
		".previous\n"
		"# ending down_write"
		: "+r"(tmp), "=m"(sem->count)
		: "a"(sem), "m"(sem->count)
		: "memory");
}

/*
 * unlock after reading
 */
static inline void __up_read(struct rw_semaphore *sem)
{
	int tmp;

	tmp = -RWSEM_ACTIVE_READ_BIAS;
	__asm__ __volatile__(
		"# beginning __up_read\n\t"
LOCK_PREFIX	"  xadd      %0,(%%eax)\n\t" /* subtracts 1, returns the old value */
		"  js        2f\n\t" /* jump if the lock is being waited upon */
		"1:\n\t"
		".section .text.lock,\"ax\"\n"
		"2:\n\t"
		"  decl      %0\n\t" /* xadd gave us the old count */
		"  testl     %3,%0\n\t" /* do nothing if still outstanding active readers */
		"  jnz       1b\n\t"
		"  call      __rwsem_wake\n\t"
		"  jmp       1b\n"
		".previous\n"
		"# ending __up_read\n"
		: "+r"(tmp), "=m"(sem->count)
		: "a"(sem), "i"(RWSEM_ACTIVE_MASK), "m"(sem->count)
		: "memory");
}

/*
 * unlock after writing
 */
static inline void __up_write(struct rw_semaphore *sem)
{
	__asm__ __volatile__(
		"# beginning __up_write\n\t"
LOCK_PREFIX	"  addl      %2,(%%eax)\n\t" /* adds 0x0000ffff */
		"  js        2f\n\t" /* jump if the lock is being waited upon */
		"1:\n\t"
		".section .text.lock,\"ax\"\n"
		"2:\n\t"
		"  call      __rwsem_wake\n\t"
		"  jmp       1b\n"
		".previous\n"
		"# ending __up_write\n"
		: "=m"(sem->count)
		: "a"(sem), "i"(-RWSEM_ACTIVE_WRITE_BIAS), "m"(sem->count)
		: "memory");
}

#endif /* __KERNEL__ */
#endif /* _I386_RWSEM_XADD_H */
