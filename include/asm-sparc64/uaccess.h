/* $Id: uaccess.h,v 1.13 1997/05/29 12:45:04 jj Exp $ */
#ifndef _ASM_UACCESS_H
#define _ASM_UACCESS_H

/*
 * User space memory access functions
 */

#ifdef __KERNEL__
#include <linux/sched.h>
#include <linux/string.h>
#include <asm/a.out.h>
#include <asm/asi.h>
#include <asm/system.h>
#include <asm/spitfire.h>
#endif

#ifndef __ASSEMBLY__

/* Sparc is not segmented, however we need to be able to fool verify_area()
 * when doing system calls from kernel mode legitimately.
 *
 * "For historical reasons, these macros are grossly misnamed." -Linus
 */
#define KERNEL_DS   0
#define USER_DS     -1

#define VERIFY_READ	0
#define VERIFY_WRITE	1

#define get_fs() (current->tss.current_ds)
#define get_ds() (KERNEL_DS)
extern __inline__ void set_fs(int val)
{
	if (val != current->tss.current_ds) {
		if (val == KERNEL_DS) {
			flushw_user ();
			spitfire_set_secondary_context (0);
		} else {
			spitfire_set_secondary_context (current->mm->context);
		}
		current->tss.current_ds = val;
	}
}

#define __user_ok(addr,size) 1
#define __kernel_ok (get_fs() == KERNEL_DS)
#define __access_ok(addr,size) 1
#define access_ok(type,addr,size) 1

extern inline int verify_area(int type, const void * addr, unsigned long size)
{
	return 0;
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
 *
 * There is a special way how to put a range of potentially faulting
 * insns (like twenty ldd/std's with now intervening other instructions)
 * You specify address of first in insn and 0 in fixup and in the next
 * exception_table_entry you specify last potentially faulting insn + 1
 * and in fixup the routine which should handle the fault.
 * That fixup code will get
 * (faulting_insn_address - first_insn_in_the_range_address)/4
 * in %g2 (ie. index of the faulting instruction in the range).
 */

struct exception_table_entry
{
        unsigned long insn, fixup;
};

/* Returns 0 if exception not found and fixup otherwise.  */
extern unsigned long search_exception_table(unsigned long, unsigned long *);

extern void __ret_efault(void);

/* Uh, these should become the main single-value transfer routines..
 * They automatically use the right size if we just have the right
 * pointer type..
 *
 * This gets kind of ugly. We want to return _two_ values in "get_user()"
 * and yet we don't want to do any pointers, because that is too much
 * of a performance impact. Thus we have a few rather ugly macros here,
 * and hide all the uglyness from the user.
 */
#define put_user(x,ptr) ({ \
unsigned long __pu_addr = (unsigned long)(ptr); \
__put_user_nocheck((__typeof__(*(ptr)))(x),__pu_addr,sizeof(*(ptr))); })

#define put_user_ret(x,ptr,retval) ({ \
unsigned long __pu_addr = (unsigned long)(ptr); \
__put_user_nocheck_ret((__typeof__(*(ptr)))(x),__pu_addr,sizeof(*(ptr)),retval); })

#define get_user(x,ptr) ({ \
unsigned long __gu_addr = (unsigned long)(ptr); \
__get_user_nocheck((x),__gu_addr,sizeof(*(ptr)),__typeof__(*(ptr))); })

#define get_user_ret(x,ptr,retval) ({ \
unsigned long __gu_addr = (unsigned long)(ptr); \
__get_user_nocheck_ret((x),__gu_addr,sizeof(*(ptr)),__typeof__(*(ptr)),retval); })

#define __put_user(x,ptr) put_user(x,ptr)
#define __put_user_ret(x,ptr,retval) put_user_ret(x,ptr,retval)
#define __get_user(x,ptr) get_user(x,ptr)
#define __get_user_ret(x,ptr,retval) get_user_ret(x,ptr,retval)

struct __large_struct { unsigned long buf[100]; };
#define __m(x) ((struct __large_struct *)(x))

#define __put_user_nocheck(data,addr,size) ({ \
register int __pu_ret; \
switch (size) { \
case 1: __put_user_asm(data,b,addr,__pu_ret); break; \
case 2: __put_user_asm(data,h,addr,__pu_ret); break; \
case 4: __put_user_asm(data,w,addr,__pu_ret); break; \
case 8: __put_user_asm(data,x,addr,__pu_ret); break; \
default: __pu_ret = __put_user_bad(); break; \
} __pu_ret; })

#define __put_user_nocheck_ret(data,addr,size,retval) ({ \
register int __foo __asm__ ("l1"); \
switch (size) { \
case 1: __put_user_asm_ret(data,b,addr,retval,__foo); break; \
case 2: __put_user_asm_ret(data,h,addr,retval,__foo); break; \
case 4: __put_user_asm_ret(data,w,addr,retval,__foo); break; \
case 8: __put_user_asm_ret(data,x,addr,retval,__foo); break; \
default: if (__put_user_bad()) return retval; break; \
} })

#define __put_user_asm(x,size,addr,ret)					\
__asm__ __volatile__(							\
	"/* Put user asm, inline. */\n"					\
"1:\t"	"st"#size "a %1, [%2] %4\n\t"					\
	"clr	%0\n"							\
"2:\n\n\t"								\
	".section .fixup,#alloc,#execinstr\n\t"				\
	".align	4\n"							\
"3:\n\t"								\
	"b	2b\n\t"							\
	" mov	%3, %0\n\n\t"						\
	".previous\n\t"							\
	".section __ex_table,#alloc\n\t"				\
	".align	8\n\t"							\
	".xword	1b, 3b\n\t"						\
	".previous\n\n\t"						\
       : "=r" (ret) : "r" (x), "r" (__m(addr)),				\
	 "i" (-EFAULT), "i" (ASI_S))

#define __put_user_asm_ret(x,size,addr,ret,foo)				\
if (__builtin_constant_p(ret) && ret == -EFAULT)			\
__asm__ __volatile__(							\
	"/* Put user asm ret, inline. */\n"				\
"1:\t"	"st"#size "a %1, [%2] %3\n\n\t"					\
	".section __ex_table,#alloc\n\t"				\
	".align	8\n\t"							\
	".xword	1b, __ret_efault\n\n\t"					\
	".previous\n\n\t"						\
       : "=r" (foo) : "r" (x), "r" (__m(addr)), "i" (ASI_S));		\
else									\
__asm__ __volatile(							\
	"/* Put user asm ret, inline. */\n"				\
"1:\t"	"st"#size "a %1, [%2] %4\n\n\t"					\
	".section .fixup,#alloc,#execinstr\n\t"				\
	".align	4\n"							\
"3:\n\t"								\
	"ret\n\t"							\
	" restore %%g0, %3, %%o0\n\n\t"					\
	".previous\n\t"							\
	".section __ex_table,#alloc\n\t"				\
	".align	8\n\t"							\
	".xword	1b, 3b\n\n\t"						\
	".previous\n\n\t"						\
       : "=r" (foo) : "r" (x), "r" (__m(addr)),				\
         "i" (ret), "i" (ASI_S))

extern int __put_user_bad(void);

#define __get_user_nocheck(data,addr,size,type) ({ \
register int __gu_ret; \
register unsigned long __gu_val; \
switch (size) { \
case 1: __get_user_asm(__gu_val,ub,addr,__gu_ret); break; \
case 2: __get_user_asm(__gu_val,uh,addr,__gu_ret); break; \
case 4: __get_user_asm(__gu_val,uw,addr,__gu_ret); break; \
case 8: __get_user_asm(__gu_val,x,addr,__gu_ret); break; \
default: __gu_val = 0; __gu_ret = __get_user_bad(); break; \
} data = (type) __gu_val; __gu_ret; })

#define __get_user_nocheck_ret(data,addr,size,type,retval) ({ \
register unsigned long __gu_val __asm__ ("l1"); \
switch (size) { \
case 1: __get_user_asm_ret(__gu_val,ub,addr,retval); break; \
case 2: __get_user_asm_ret(__gu_val,uh,addr,retval); break; \
case 4: __get_user_asm_ret(__gu_val,uw,addr,retval); break; \
case 8: __get_user_asm_ret(__gu_val,x,addr,retval); break; \
default: if (__get_user_bad()) return retval; \
} data = (type) __gu_val; })

#define __get_user_asm(x,size,addr,ret)					\
__asm__ __volatile__(							\
	"/* Get user asm, inline. */\n"					\
"1:\t"	"ld"#size "a [%2] %4, %1\n\t"					\
	"clr	%0\n"							\
"2:\n\n\t"								\
	".section .fixup,#alloc,#execinstr\n\t"				\
	".align	4\n"							\
"3:\n\t"								\
	"clr	%1\n\t"							\
	"b	2b\n\t"							\
	" mov	%3, %0\n\n\t"						\
	".previous\n\t"							\
	".section __ex_table,#alloc\n\t"				\
	".align	8\n\t"							\
	".xword	1b, 3b\n\n\t"						\
	".previous\n\t"							\
       : "=r" (ret), "=r" (x) : "r" (__m(addr)),			\
	 "i" (-EFAULT), "i" (ASI_S))

#define __get_user_asm_ret(x,size,addr,retval)				\
if (__builtin_constant_p(retval) && retval == -EFAULT)			\
__asm__ __volatile__(							\
	"/* Get user asm ret, inline. */\n"				\
"1:\t"	"ld"#size "a [%1] %2, %0\n\n\t"					\
	".section __ex_table,#alloc\n\t"				\
	".align	8\n\t"							\
	".xword	1b,__ret_efault\n\n\t"					\
	".previous\n\t"							\
       : "=r" (x) : "r" (__m(addr)), "i" (ASI_S));			\
else									\
__asm__ __volatile__(							\
	"/* Get user asm ret, inline. */\n"				\
"1:\t"	"ld"#size "a [%1] %2, %0\n\n\t"					\
	".section .fixup,#alloc,#execinstr\n\t"				\
	".align	4\n"							\
"3:\n\t"								\
	"ret\n\t"							\
	" restore %%g0, %3, %%o0\n\n\t"					\
	".previous\n\t"							\
	".section __ex_table,#alloc\n\t"				\
	".align	8\n\t"							\
	".xword	1b, 3b\n\n\t"						\
	".previous\n\t"							\
       : "=r" (x) : "r" (__m(addr)), "i" (retval), "i" (ASI_S))

extern int __get_user_bad(void);

extern __kernel_size_t __copy_to_user(void *to, void *from, __kernel_size_t size);
extern __kernel_size_t __copy_from_user(void *to, void *from, __kernel_size_t size);

#define copy_to_user(to,from,n) \
	__copy_to_user((void *)(to), \
	(void *) (from), (__kernel_size_t)(n))

#define copy_to_user_ret(to,from,n,retval) ({ \
if (copy_to_user(to,from,n)) \
	return retval; \
})

#define __copy_to_user_ret(to,from,n,retval) ({ \
if (__copy_to_user(to,from,n)) \
	return retval; \
})

#define copy_from_user(to,from,n)		\
	__copy_from_user((void *)(to),	\
		    (void *)(from), (__kernel_size_t)(n))

#define copy_from_user_ret(to,from,n,retval) ({ \
if (copy_from_user(to,from,n)) \
	return retval; \
})

#define __copy_from_user_ret(to,from,n,retval) ({ \
if (__copy_from_user(to,from,n)) \
	return retval; \
})

extern __inline__ __kernel_size_t __clear_user(void *addr, __kernel_size_t size)
{
  __kernel_size_t ret;
  __asm__ __volatile__ ("
	.section __ex_table,#alloc
	.align 8
	.xword 1f,3
	.previous
1:
	wr %%g0, %3, %%asi
	mov %2, %%o1
	call __bzero_noasi
	 mov %1, %%o0
	mov %%o0, %0
	" : "=r" (ret) : "r" (addr), "r" (size), "i" (ASI_S) :
	"cc", "o0", "o1", "o2", "o3", "o4", "o5", "o7", "g1", "g2", "g3", "g5", "g7");
  return ret;
}

#define clear_user(addr,n) \
	__clear_user((void *)(addr), (__kernel_size_t)(n))

#define clear_user_ret(addr,size,retval) ({ \
if (clear_user(addr,size)) \
	return retval; \
})

extern int __strncpy_from_user(unsigned long dest, unsigned long src, int count);

#define strncpy_from_user(dest,src,count) \
	__strncpy_from_user((unsigned long)(dest), (unsigned long)(src), (int)(count))

extern int __strlen_user(const char *);

#define strlen_user __strlen_user

#endif  /* __ASSEMBLY__ */

#endif /* _ASM_UACCESS_H */
