/* 
 * User address space access functions.
 * The non inlined parts of asm-i386/uaccess.h are here.
 *
 * Copyright 1997 Andi Kleen <ak@muc.de>
 * Copyright 1997 Linus Torvalds
 */
#include <asm/uaccess.h>

unsigned long
__generic_copy_to_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__copy_user(to,from,n);
	return n;
}

unsigned long
__generic_copy_from_user(void *to, const void *from, unsigned long n)
{
	if (access_ok(VERIFY_READ, from, n))
		__copy_user_zeroing(to,from,n);
	return n;
}


/*
 * Copy a null terminated string from userspace.
 */

#define __do_strncpy_from_user(dst,src,count,res)			   \
do {									   \
	int __d0, __d1, __d2;						   \
	__asm__ __volatile__(						   \
		"	testl %1,%1\n"					   \
		"	jz 2f\n"					   \
		"0:	lodsb\n"					   \
		"	stosb\n"					   \
		"	testb %%al,%%al\n"				   \
		"	jz 1f\n"					   \
		"	decl %1\n"					   \
		"	jnz 0b\n"					   \
		"1:	subl %1,%0\n"					   \
		"2:\n"							   \
		".section .fixup,\"ax\"\n"				   \
		"3:	movl %5,%0\n"					   \
		"	jmp 2b\n"					   \
		".previous\n"						   \
		".section __ex_table,\"a\"\n"				   \
		"	.align 4\n"					   \
		"	.long 0b,3b\n"					   \
		".previous"						   \
		: "=d"(res), "=c"(count), "=&a" (__d0), "=&S" (__d1),	   \
		  "=&D" (__d2)						   \
		: "i"(-EFAULT), "0"(count), "1"(count), "3"(src), "4"(dst) \
		: "memory");						   \
} while (0)

long
__strncpy_from_user(char *dst, const char *src, long count)
{
	long res;
	__do_strncpy_from_user(dst, src, count, res);
	return res;
}

long
strncpy_from_user(char *dst, const char *src, long count)
{
	long res = -EFAULT;
	if (access_ok(VERIFY_READ, src, 1))
		__do_strncpy_from_user(dst, src, count, res);
	return res;
}


/*
 * Zero Userspace
 */

#define __do_clear_user(addr,size)					\
do {									\
	int __d0;							\
  	__asm__ __volatile__(						\
		"0:	rep; stosl\n"					\
		"	movl %2,%0\n"					\
		"1:	rep; stosb\n"					\
		"2:\n"							\
		".section .fixup,\"ax\"\n"				\
		"3:	lea 0(%2,%0,4),%0\n"				\
		"	jmp 2b\n"					\
		".previous\n"						\
		".section __ex_table,\"a\"\n"				\
		"	.align 4\n"					\
		"	.long 0b,3b\n"					\
		"	.long 1b,2b\n"					\
		".previous"						\
		: "=&c"(size), "=&D" (__d0)				\
		: "r"(size & 3), "0"(size / 4), "1"(addr), "a"(0));	\
} while (0)

unsigned long
clear_user(void *to, unsigned long n)
{
	if (access_ok(VERIFY_WRITE, to, n))
		__do_clear_user(to, n);
	return n;
}

unsigned long
__clear_user(void *to, unsigned long n)
{
	__do_clear_user(to, n);
	return n;
}

/*
 * Return the size of a string (including the ending 0)
 *
 * Return 0 for error
 */

long strlen_user(const char *s)
{
	unsigned long res;

	__asm__ __volatile__(
		"0:	repne; scasb\n"
		"	notl %0\n"
		"1:\n"
		".section .fixup,\"ax\"\n"
		"2:	xorl %0,%0\n"
		"	jmp 1b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 0b,2b\n"
		".previous"
		:"=c" (res), "=D" (s)
		:"1" (s), "a" (0), "0" (-__addr_ok(s)));
	return res & -__addr_ok(s);
}
