#ifndef __i386_UACCESS_H
#define __i386_UACCESS_H

/*
 * User space memory access functions
 */
#include <linux/sched.h>

#define VERIFY_READ 0
#define VERIFY_WRITE 1

/*
 * The fs value determines whether argument validity checking should be
 * performed or not.  If get_fs() == USER_DS, checking is performed, with
 * get_fs() == KERNEL_DS, checking is bypassed.
 *
 * For historical reasons, these macros are grossly misnamed.
 */

#define MAKE_MM_SEG(s)	((mm_segment_t) { (s) })


#define KERNEL_DS	MAKE_MM_SEG(0xFFFFFFFF)
#define USER_DS		MAKE_MM_SEG(0xC0000000)

#define get_ds()	(KERNEL_DS)
#define get_fs()	(current->addr_limit)
#define set_fs(x)	(current->addr_limit = (x))

#define segment_eq(a,b)	((a).seg == (b).seg)

extern int __verify_write(const void *, unsigned long);

#define __addr_ok(addr) ((unsigned long)(addr) < (current->addr_limit.seg))

/*
 * Uhhuh, this needs 33-bit arithmetic. We have a carry..
 */
#define __range_ok(addr,size) ({ \
	unsigned long flag,sum; \
	asm("addl %3,%1 ; sbbl %0,%0; cmpl %1,%4; sbbl $0,%0" \
		:"=&r" (flag), "=r" (sum) \
		:"1" (addr),"g" (size),"g" (current->addr_limit.seg)); \
	flag; })

#if CPU > 386

#define access_ok(type,addr,size) (__range_ok(addr,size) == 0)

#else

#define access_ok(type,addr,size) ( (__range_ok(addr,size) == 0) && \
			 ((type) == VERIFY_READ || boot_cpu_data.wp_works_ok || \
			  __verify_write((void *)(addr),(size))))

#endif /* CPU */

extern inline int verify_area(int type, const void * addr, unsigned long size)
{
	return access_ok(type,addr,size) ? 0 : -EFAULT;
}


/*
 * The exception table consists of pairs of addresses: the first is the
 * address of an instruction that is allowed to fault, and the second is
 * the address at which the program should continue.  No registers are
 * modified, so it is entirely up to the continuation code to figure out
 * what to do.
 *
 * All the routines below use bits of fixup code that are out of line
 * with the main instruction path.  This means when everything is well,
 * we don't even have to jump over them.  Further, they do not intrude
 * on our cache or tlb entries.
 */

struct exception_table_entry
{
	unsigned long insn, fixup;
};

/* Returns 0 if exception not found and fixup otherwise.  */
extern unsigned long search_exception_table(unsigned long);


/*
 * These are the main single-value transfer routines.  They automatically
 * use the right size if we just have the right pointer type.
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the uglyness from the user.
 *
 * The "__xxx" versions of the user access functions are versions that
 * do not verify the address space, that must have been done previously
 * with a separate "access_ok()" call (this is used when we do multiple
 * accesses to the same area of user memory).
 */

extern void __get_user_1(void);
extern void __get_user_2(void);
extern void __get_user_4(void);

#define __get_user_x(size,ret,x,ptr) \
	__asm__ __volatile__("call __get_user_" #size \
		:"=a" (ret),"=d" (x) \
		:"0" (ptr))

#define get_user(x,ptr) \
({	int __ret_gu;    \
	switch(sizeof (*(ptr))) {     \
	case 1:  __get_user_x(1,__ret_gu,x,ptr); break; \
	case 2:  __get_user_x(2,__ret_gu,x,ptr); break; \
	case 4:  __get_user_x(4,__ret_gu,x,ptr); break; \
	default: __get_user_x(X,__ret_gu,x,ptr); break; \
	} \
	__ret_gu; \
})

extern void __put_user_1(void);
extern void __put_user_2(void);
extern void __put_user_4(void);

#define __put_user_x(size,ret,x,ptr) \
	__asm__ __volatile__("call __put_user_" #size \
		:"=a" (ret) \
		:"0" (ptr),"d" (x) \
		:"cx")

#define put_user(x,ptr) \
({	int __ret_pu;    \
	switch(sizeof (*(ptr))) {     \
	case 1:  __put_user_x(1,__ret_pu,(char)(x),ptr); break; \
	case 2:  __put_user_x(2,__ret_pu,(short)(x),ptr); break; \
	case 4:  __put_user_x(4,__ret_pu,(int)(x),ptr); break; \
	default: __put_user_x(X,__ret_pu,x,ptr); break; \
	} \
	__ret_pu; \
})

#define __get_user(x,ptr) get_user(x,ptr)
#define __put_user(x,ptr) put_user(x,ptr)

/*
 * The "xxx_ret" versions return constant specified in third argument, if
 * something bad happens. These macros can be optimized for the
 * case of just returning from the function xxx_ret is used.
 */

#define put_user_ret(x,ptr,ret) ({ if (put_user(x,ptr)) return ret; })

#define get_user_ret(x,ptr,ret) ({ if (get_user(x,ptr)) return ret; })

#define __put_user_ret(x,ptr,ret) ({ if (__put_user(x,ptr)) return ret; })

#define __get_user_ret(x,ptr,ret) ({ if (__get_user(x,ptr)) return ret; })


/*
 * Copy To/From Userspace
 */

/* Generic arbitrary sized copy.  */
#define __copy_user(to,from,size)					\
	__asm__ __volatile__(						\
		"0:	rep; movsl\n"					\
		"	movl %1,%0\n"					\
		"1:	rep; movsb\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"3:	lea 0(%1,%0,4),%0\n"				\
		"	jmp 2b\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.align 4\n"					\
		"	.long 0b,3b\n"					\
		"	.long 1b,2b\n"					\
		".previous"						\
		: "=c"(size)						\
		: "r"(size & 3), "0"(size / 4), "D"(to), "S"(from)	\
		: "di", "si", "memory")

/* We let the __ versions of copy_from/to_user inline, because they're often
 * used in fast paths and have only a small space overhead.
 */
static inline unsigned long
__generic_copy_from_user_nocheck(void *to, const void *from, unsigned long n)
{
	__copy_user(to,from,n);
	return n;
}

static inline unsigned long
__generic_copy_to_user_nocheck(void *to, const void *from, unsigned long n)
{
	__copy_user(to,from,n);
	return n;
}


/* Optimize just a little bit when we know the size of the move. */
#define __constant_copy_user(to, from, size)			\
do {								\
	switch (size & 3) {					\
	default:						\
		__asm__ __volatile__(				\
			"0:	rep; movsl\n"			\
			"1:\n"					\
			".section .fixup,\"ax\"\n"		\
			"2:	shl $2,%0\n"			\
			"	jmp 1b\n"			\
			".previous\n"				\
			".section __ex_table,\"a\"\n"		\
			"	.align 4\n"			\
			"	.long 0b,2b\n"			\
			".previous"				\
			: "=c"(size)				\
			: "S"(from), "D"(to), "0"(size/4)	\
			: "di", "si", "memory");		\
		break;						\
	case 1:							\
		__asm__ __volatile__(				\
			"0:	rep; movsl\n"			\
			"1:	movsb\n"			\
			"2:\n"					\
			".section .fixup,\"ax\"\n"		\
			"3:	shl $2,%0\n"			\
			"4:	incl %0\n"			\
			"	jmp 2b\n"			\
			".previous\n"				\
			".section __ex_table,\"a\"\n"		\
			"	.align 4\n"			\
			"	.long 0b,3b\n"			\
			"	.long 1b,4b\n"			\
			".previous"				\
			: "=c"(size)				\
			: "S"(from), "D"(to), "0"(size/4)	\
			: "di", "si", "memory");		\
		break;						\
	case 2:							\
		__asm__ __volatile__(				\
			"0:	rep; movsl\n"			\
			"1:	movsw\n"			\
			"2:\n"					\
			".section .fixup,\"ax\"\n"		\
			"3:	shl $2,%0\n"			\
			"4:	addl $2,%0\n"			\
			"	jmp 2b\n"			\
			".previous\n"				\
			".section __ex_table,\"a\"\n"		\
			"	.align 4\n"			\
			"	.long 0b,3b\n"			\
			"	.long 1b,4b\n"			\
			".previous"				\
			: "=c"(size)				\
			: "S"(from), "D"(to), "0"(size/4)	\
			: "di", "si", "memory");		\
		break;						\
	case 3:							\
		__asm__ __volatile__(				\
			"0:	rep; movsl\n"			\
			"1:	movsw\n"			\
			"2:	movsb\n"			\
			"3:\n"					\
			".section .fixup,\"ax\"\n"		\
			"4:	shl $2,%0\n"			\
			"5:	addl $2,%0\n"			\
			"6:	incl %0\n"			\
			"	jmp 3b\n"			\
			".previous\n"				\
			".section __ex_table,\"a\"\n"		\
			"	.align 4\n"			\
			"	.long 0b,4b\n"			\
			"	.long 1b,5b\n"			\
			"	.long 2b,6b\n"			\
			".previous"				\
			: "=c"(size)				\
			: "S"(from), "D"(to), "0"(size/4)	\
			: "di", "si", "memory");		\
		break;						\
	}							\
} while (0)

unsigned long __generic_copy_to_user(void *, const void *, unsigned long);
unsigned long __generic_copy_from_user(void *, const void *, unsigned long);

static inline unsigned long
__constant_copy_to_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__constant_copy_user(to,from,n);
	return n;
}

static inline unsigned long
__constant_copy_from_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		__constant_copy_user(to,from,n);
	return n;
}

static inline unsigned long
__constant_copy_to_user_nocheck(void *to, const void *from, unsigned long n)
{
	__constant_copy_user(to,from,n);
	return n;
}

static inline unsigned long
__constant_copy_from_user_nocheck(void *to, const void *from, unsigned long n)
{
	__constant_copy_user(to,from,n);
	return n;
}

#define copy_to_user(to,from,n)				\
	(__builtin_constant_p(n) ?			\
	 __constant_copy_to_user((to),(from),(n)) :	\
	 __generic_copy_to_user((to),(from),(n)))

#define copy_from_user(to,from,n)			\
	(__builtin_constant_p(n) ?			\
	 __constant_copy_from_user((to),(from),(n)) :	\
	 __generic_copy_from_user((to),(from),(n)))

#define copy_to_user_ret(to,from,n,retval) ({ if (copy_to_user(to,from,n)) return retval; })

#define copy_from_user_ret(to,from,n,retval) ({ if (copy_from_user(to,from,n)) return retval; })

#define __copy_to_user(to,from,n)			\
	(__builtin_constant_p(n) ?			\
	 __constant_copy_to_user_nocheck((to),(from),(n)) :	\
	 __generic_copy_to_user_nocheck((to),(from),(n)))

#define __copy_from_user(to,from,n)			\
	(__builtin_constant_p(n) ?			\
	 __constant_copy_from_user_nocheck((to),(from),(n)) :	\
	 __generic_copy_from_user_nocheck((to),(from),(n)))

long strncpy_from_user(char *dst, const char *src, long count);
long __strncpy_from_user(char *dst, const char *src, long count);
long strlen_user(const char *str);
unsigned long clear_user(void *mem, unsigned long len);
unsigned long __clear_user(void *mem, unsigned long len);

#endif /* __i386_UACCESS_H */
