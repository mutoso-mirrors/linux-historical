/* $Id: uaccess.h,v 1.9 2003/05/06 23:28:51 lethal Exp $
 *
 * User space memory access functions
 *
 * Copyright (C) 1999, 2002  Niibe Yutaka
 * Copyright (C) 2003  Paul Mundt
 *
 *  Based on:
 *     MIPS implementation version 1.15 by
 *              Copyright (C) 1996, 1997, 1998 by Ralf Baechle
 *     and i386 version.
 */
#ifndef __ASM_SH_UACCESS_H
#define __ASM_SH_UACCESS_H

#include <linux/errno.h>
#include <linux/sched.h>

/*
 * NOTE: Macro/functions in this file depends on threads_info.h implementation.
 * Assumes:
 * TI_FLAGS == 8
 * TIF_USERSPACE == 31
 * USER_ADDR_LIMIT == 0x80000000
 */

#define VERIFY_READ    0
#define VERIFY_WRITE   1

typedef struct {
	unsigned int is_user_space;
} mm_segment_t;

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons (Data Segment Register?), these macros are misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })
#define segment_eq(a,b)	((a).is_user_space == (b).is_user_space)

#define USER_ADDR_LIMIT	0x80000000

#define KERNEL_DS	MAKE_MM_SEG(0)
#define USER_DS		MAKE_MM_SEG(1)

#define get_ds()	(KERNEL_DS)

#if !defined(CONFIG_MMU)
static inline mm_segment_t get_fs(void)
{
	return USER_DS;
}

static inline void set_fs(mm_segment_t s)
{
}

/*
 * __access_ok: Check if address with size is OK or not.
 *
 * If we don't have an MMU (or if its disabled) the only thing we really have
 * to look out for is if the address resides somewhere outside of what
 * available RAM we have.
 *
 * TODO: This check could probably also stand to be restricted somewhat more..
 * though it still does the Right Thing(tm) for the time being.
 */
static inline int __access_ok(unsigned long addr, unsigned long size)
{
	extern unsigned long memory_start, memory_end;

	return ((addr >= memory_start) && ((addr + size) < memory_end));
}
#else /* CONFIG_MMU */
static inline mm_segment_t get_fs(void)
{
	return MAKE_MM_SEG(test_thread_flag(TIF_USERSPACE));
}

static inline void set_fs(mm_segment_t s)
{
	unsigned long ti, flag;
	__asm__ __volatile__(
		"stc	r7_bank, %0\n\t"
		"mov.l	@(8,%0), %1\n\t"
		"shal	%1\n\t"
		"cmp/pl	%2\n\t"
		"rotcr	%1\n\t"
		"mov.l	%1, @(8,%0)"
		: "=&r" (ti), "=&r" (flag)
		: "r" (s.is_user_space)
		: "t");
/****
	if (s.is_user_space)
		set_thread_flag(TIF_USERSPACE);
	else
		clear_thread_flag(TIF_USERSPACE);
****/
}

/*
 * __access_ok: Check if address with size is OK or not.
 *
 * We do three checks:
 * (1) is it user space? 
 * (2) addr + size --> carry?
 * (3) addr + size >= 0x80000000  (USER_ADDR_LIMIT)
 *
 * (1) (2) (3) | RESULT
 *  0   0   0  |  ok
 *  0   0   1  |  ok
 *  0   1   0  |  bad
 *  0   1   1  |  bad
 *  1   0   0  |  ok
 *  1   0   1  |  bad
 *  1   1   0  |  bad
 *  1   1   1  |  bad
 */
static inline int __access_ok(unsigned long addr, unsigned long size)
{
	unsigned long flag, tmp;

	__asm__("stc	r7_bank, %0\n\t"
		"mov.l	@(8,%0), %0\n\t"
		"clrt\n\t"
		"addc	%2, %1\n\t"
		"and	%1, %0\n\t"
		"rotcl	%0\n\t"
		"rotcl	%0\n\t"
		"and	#3, %0"
		: "=&z" (flag), "=r" (tmp)
		: "r" (addr), "1" (size)
		: "t");

	return flag == 0;
}
#endif /* CONFIG_MMU */

static inline int access_ok(int type, const void *p, unsigned long size)
{
	unsigned long addr = (unsigned long)p;
	return __access_ok(addr, size);
}

static inline int verify_area(int type, const void * addr, unsigned long size)
{
	return access_ok(type,addr,size) ? 0 : -EFAULT;
}

/*
 * Uh, these should become the main single-value transfer routines ...
 * They automatically use the right size if we just have the right
 * pointer type ...
 *
 * As SuperH uses the same address space for kernel and user data, we
 * can just do these as direct assignments.
 *
 * Careful to not
 * (a) re-use the arguments for side effects (sizeof is ok)
 * (b) require any knowledge of processes at this stage
 */
#define put_user(x,ptr)	__put_user_check((x),(ptr),sizeof(*(ptr)))
#define get_user(x,ptr) __get_user_check((x),(ptr),sizeof(*(ptr)))

/*
 * The "__xxx" versions do not do address space checking, useful when
 * doing multiple accesses to the same area (the user has to do the
 * checks by hand with "access_ok()")
 */
#define __put_user(x,ptr) __put_user_nocheck((x),(ptr),sizeof(*(ptr)))
#define __get_user(x,ptr) __get_user_nocheck((x),(ptr),sizeof(*(ptr)))

struct __large_struct { unsigned long buf[100]; };
#define __m(x) (*(struct __large_struct *)(x))

#define __get_user_nocheck(x,ptr,size) ({ \
long __gu_err; \
__typeof(*(ptr)) __gu_val; \
long __gu_addr; \
__asm__("":"=r" (__gu_val)); \
__gu_addr = (long) (ptr); \
__asm__("":"=r" (__gu_err)); \
switch (size) { \
case 1: __get_user_asm("b"); break; \
case 2: __get_user_asm("w"); break; \
case 4: __get_user_asm("l"); break; \
default: __get_user_unknown(); break; \
} x = (__typeof__(*(ptr))) __gu_val; __gu_err; })

#define __get_user_check(x,ptr,size)			\
({ __typeof__(*(ptr)) __val; long __err;		\
 switch(size) {						\
 case 1: __err = __get_user_1(__val, ptr); break;	\
 case 2: __err = __get_user_2(__val, ptr); break;	\
 case 4: __err = __get_user_4(__val, ptr); break;	\
 default: __get_user_unknown(); break;			\
 }							\
 (x) = __val; __err; })

#define __get_user_1(x,ptr) ({			\
long __gu_err;					\
__typeof__(*(ptr)) __gu_val;			\
long __gu_addr = (long) (ptr);			\
__asm__("stc	r7_bank, %1\n\t"		\
	"mov.l	@(8,%1), %1\n\t"		\
	"and	%2, %1\n\t"			\
	"cmp/pz	%1\n\t"				\
	"bt/s	1f\n\t"				\
	" mov	#0, %0\n\t"			\
	"0:\n"					\
	"mov	#-14, %0\n\t"			\
	"bra	2f\n\t"				\
	" mov	#0, %1\n"			\
	"1:\n\t"				\
	"mov.b	@%2, %1\n\t"			\
	"extu.b	%1, %1\n"			\
	"2:\n"					\
	".section	__ex_table,\"a\"\n\t"	\
	".long	1b, 0b\n\t"			\
	".previous"				\
	: "=&r" (__gu_err), "=&r" (__gu_val)	\
	: "r" (__gu_addr)			\
	: "t");					\
x = (__typeof__(*(ptr))) __gu_val; __gu_err; })

#define __get_user_2(x,ptr) ({			\
long __gu_err;					\
__typeof__(*(ptr)) __gu_val;			\
long __gu_addr = (long) (ptr);			\
__asm__("stc	r7_bank, %1\n\t"		\
	"mov.l	@(8,%1), %1\n\t"		\
	"and	%2, %1\n\t"			\
	"cmp/pz	%1\n\t"				\
	"bt/s	1f\n\t"				\
	" mov	#0, %0\n\t"			\
	"0:\n"					\
	"mov	#-14, %0\n\t"			\
	"bra	2f\n\t"				\
	" mov	#0, %1\n"			\
	"1:\n\t"				\
	"mov.w	@%2, %1\n\t"			\
	"extu.w	%1, %1\n"			\
	"2:\n"					\
	".section	__ex_table,\"a\"\n\t"	\
	".long	1b, 0b\n\t"			\
	".previous"				\
	: "=&r" (__gu_err), "=&r" (__gu_val)	\
	: "r" (__gu_addr)			\
	: "t");					\
x = (__typeof__(*(ptr))) __gu_val; __gu_err; })

#define __get_user_4(x,ptr) ({			\
long __gu_err;					\
__typeof__(*(ptr)) __gu_val;			\
long __gu_addr = (long) (ptr);			\
__asm__("stc	r7_bank, %1\n\t"		\
	"mov.l	@(8,%1), %1\n\t"		\
	"and	%2, %1\n\t"			\
	"cmp/pz	%1\n\t"				\
	"bt/s	1f\n\t"				\
	" mov	#0, %0\n\t"			\
	"0:\n"					\
	"mov	#-14, %0\n\t"			\
	"bra	2f\n\t"				\
	" mov	#0, %1\n"			\
	"1:\n\t"				\
	"mov.l	@%2, %1\n\t"			\
	"2:\n"					\
	".section	__ex_table,\"a\"\n\t"	\
	".long	1b, 0b\n\t"			\
	".previous"				\
	: "=&r" (__gu_err), "=&r" (__gu_val)	\
	: "r" (__gu_addr)			\
	: "t");					\
x = (__typeof__(*(ptr))) __gu_val; __gu_err; })

#define __get_user_asm(insn) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov." insn "	%2, %1\n\t" \
	"mov	#0, %0\n" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"mov	#0, %1\n\t" \
	"mov.l	4f, %0\n\t" \
	"jmp	@%0\n\t" \
	" mov	%3, %0\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	:"=&r" (__gu_err), "=&r" (__gu_val) \
	:"m" (__m(__gu_addr)), "i" (-EFAULT)); })

extern void __get_user_unknown(void);

#define __put_user_nocheck(x,ptr,size) ({ \
long __pu_err; \
__typeof__(*(ptr)) __pu_val; \
long __pu_addr; \
__pu_val = (x); \
__pu_addr = (long) (ptr); \
__asm__("":"=r" (__pu_err)); \
switch (size) { \
case 1: __put_user_asm("b"); break; \
case 2: __put_user_asm("w"); break; \
case 4: __put_user_asm("l"); break; \
case 8: __put_user_u64(__pu_val,__pu_addr,__pu_err); break; \
default: __put_user_unknown(); break; \
} __pu_err; })

#define __put_user_check(x,ptr,size) ({ \
long __pu_err; \
__typeof__(*(ptr)) __pu_val; \
long __pu_addr; \
__pu_val = (x); \
__pu_addr = (long) (ptr); \
__asm__("":"=r" (__pu_err)); \
if (__access_ok(__pu_addr,size)) { \
switch (size) { \
case 1: __put_user_asm("b"); break; \
case 2: __put_user_asm("w"); break; \
case 4: __put_user_asm("l"); break; \
case 8: __put_user_u64(__pu_val,__pu_addr,__pu_err); break; \
default: __put_user_unknown(); break; \
} } __pu_err; })

#define __put_user_asm(insn) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov." insn "	%1, %2\n\t" \
	"mov	#0, %0\n" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"nop\n\t" \
	"mov.l	4f, %0\n\t" \
	"jmp	@%0\n\t" \
	"mov	%3, %0\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	:"=&r" (__pu_err) \
	:"r" (__pu_val), "m" (__m(__pu_addr)), "i" (-EFAULT) \
        :"memory"); })

#if defined(__LITTLE_ENDIAN__)
#define __put_user_u64(val,addr,retval) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov.l	%R1,%2\n\t" \
	"mov.l	%S1,%T2\n\t" \
	"mov	#0,%0\n" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"nop\n\t" \
	"mov.l	4f,%0\n\t" \
	"jmp	@%0\n\t" \
	" mov	%3,%0\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	: "=r" (retval) \
	: "r" (val), "m" (__m(addr)), "i" (-EFAULT) \
        : "memory"); })
#else
#define __put_user_u64(val,addr,retval) \
({ \
__asm__ __volatile__( \
	"1:\n\t" \
	"mov.l	%S1,%2\n\t" \
	"mov.l	%R1,%T2\n\t" \
	"mov	#0,%0\n" \
	"2:\n" \
	".section	.fixup,\"ax\"\n" \
	"3:\n\t" \
	"nop\n\t" \
	"mov.l	4f,%0\n\t" \
	"jmp	@%0\n\t" \
	" mov	%3,%0\n" \
	"4:	.long	2b\n\t" \
	".previous\n" \
	".section	__ex_table,\"a\"\n\t" \
	".long	1b, 3b\n\t" \
	".previous" \
	: "=r" (retval) \
	: "r" (val), "m" (__m(addr)), "i" (-EFAULT) \
        : "memory"); })
#endif

extern void __put_user_unknown(void);

/* Generic arbitrary sized copy.  */
/* Return the number of bytes NOT copied */
extern __kernel_size_t __copy_user(void *to, const void *from, __kernel_size_t n);

#define copy_to_user(to,from,n) ({ \
void *__copy_to = (void *) (to); \
__kernel_size_t __copy_size = (__kernel_size_t) (n); \
__kernel_size_t __copy_res; \
if(__copy_size && __access_ok((unsigned long)__copy_to, __copy_size)) { \
__copy_res = __copy_user(__copy_to, (void *) (from), __copy_size); \
} else __copy_res = __copy_size; \
__copy_res; })

#define __copy_to_user(to,from,n)		\
	__copy_user((void *)(to),		\
		    (void *)(from), n)

#define copy_from_user(to,from,n) ({ \
void *__copy_to = (void *) (to); \
void *__copy_from = (void *) (from); \
__kernel_size_t __copy_size = (__kernel_size_t) (n); \
__kernel_size_t __copy_res; \
if(__copy_size && __access_ok((unsigned long)__copy_from, __copy_size)) { \
__copy_res = __copy_user(__copy_to, __copy_from, __copy_size); \
} else __copy_res = __copy_size; \
__copy_res; })

#define __copy_from_user(to,from,n)		\
	__copy_user((void *)(to),		\
		    (void *)(from), n)

/*
 * Clear the area and return remaining number of bytes
 * (on failure.  Usually it's 0.)
 */
extern __kernel_size_t __clear_user(void *addr, __kernel_size_t size);

#define clear_user(addr,n) ({ \
void * __cl_addr = (addr); \
unsigned long __cl_size = (n); \
if (__cl_size && __access_ok(((unsigned long)(__cl_addr)), __cl_size)) \
__cl_size = __clear_user(__cl_addr, __cl_size); \
__cl_size; })

static __inline__ int
__strncpy_from_user(unsigned long __dest, unsigned long __src, int __count)
{
	__kernel_size_t res;
	unsigned long __dummy, _d, _s;

	__asm__ __volatile__(
		"9:\n"
		"mov.b	@%2+, %1\n\t"
		"cmp/eq	#0, %1\n\t"
		"bt/s	2f\n"
		"1:\n"
		"mov.b	%1, @%3\n\t"
		"dt	%7\n\t"
		"bf/s	9b\n\t"
		" add	#1, %3\n\t"
		"2:\n\t"
		"sub	%7, %0\n"
		"3:\n"
		".section .fixup,\"ax\"\n"
		"4:\n\t"
		"mov.l	5f, %1\n\t"
		"jmp	@%1\n\t"
		" mov	%8, %0\n\t"
		".balign 4\n"
		"5:	.long 3b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.balign 4\n"
		"	.long 9b,4b\n"
		".previous"
		: "=r" (res), "=&z" (__dummy), "=r" (_s), "=r" (_d)
		: "0" (__count), "2" (__src), "3" (__dest), "r" (__count),
		  "i" (-EFAULT)
		: "memory", "t");

	return res;
}

#define strncpy_from_user(dest,src,count) ({ \
unsigned long __sfu_src = (unsigned long) (src); \
int __sfu_count = (int) (count); \
long __sfu_res = -EFAULT; \
if(__access_ok(__sfu_src, __sfu_count)) { \
__sfu_res = __strncpy_from_user((unsigned long) (dest), __sfu_src, __sfu_count); \
} __sfu_res; })

/*
 * Return the size of a string (including the ending 0!)
 */
static __inline__ long __strnlen_user(const char *__s, long __n)
{
	unsigned long res;
	unsigned long __dummy;

	__asm__ __volatile__(
		"9:\n"
		"cmp/eq	%4, %0\n\t"
		"bt	2f\n"
		"1:\t"
		"mov.b	@(%0,%3), %1\n\t"
		"tst	%1, %1\n\t"
		"bf/s	9b\n\t"
		" add	#1, %0\n"
		"2:\n"
		".section .fixup,\"ax\"\n"
		"3:\n\t"
		"mov.l	4f, %1\n\t"
		"jmp	@%1\n\t"
		" mov	%5, %0\n"
		".balign 4\n"
		"4:	.long 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.balign 4\n"
		"	.long 1b,3b\n"
		".previous"
		: "=z" (res), "=&r" (__dummy)
		: "0" (0), "r" (__s), "r" (__n), "i" (-EFAULT)
		: "t");
	return res;
}

static __inline__ long strnlen_user(const char *s, long n)
{
	if (!access_ok(VERIFY_READ, s, n))
		return 0;
	else
		return __strnlen_user(s, n);
}

static __inline__ long strlen_user(const char *s)
{
	if (!access_ok(VERIFY_READ, s, 0))
		return 0;
	else
		return __strnlen_user(s, ~0UL >> 1);
}

struct exception_table_entry
{
	unsigned long insn, fixup;
};

#endif /* __ASM_SH_UACCESS_H */
