#ifndef __ARCH_M68K_ATOMIC__
#define __ARCH_M68K_ATOMIC__

/*
 * Atomic operations that C can't guarantee us.  Useful for
 * resource counting etc..
 */

/*
 * We do not have SMP m68k systems, so we don't have to deal with that.
 */

typedef struct { int counter; } atomic_t;
#define ATOMIC_INIT	{ 0 }

#define atomic_read(v)		((v)->counter)
#define atomic_set(v)		(((v)->counter) = i)

static __inline__ void atomic_add(int i, atomic_t *v)
{
	__asm__ __volatile__("addl %1,%0" : : "m" (*v), "id" (i));
}

static __inline__ void atomic_sub(int i, atomic_t *v)
{
	__asm__ __volatile__("subl %1,%0" : : "m" (*v), "id" (i));
}

static __inline__ void atomic_inc(atomic_t *v)
{
	__asm__ __volatile__("addql #1,%0" : : "m" (*v));
}

static __inline__ void atomic_dec(atomic_t *v)
{
	__asm__ __volatile__("subql #1,%0" : : "m" (*v));
}

static __inline__ int atomic_dec_and_test(atomic_t *v)
{
	char c;
	__asm__ __volatile__("subql #1,%1; seq %0" : "=d" (c) : "m" (*v));
	return c != 0;
}

#endif /* __ARCH_M68K_ATOMIC __ */
