/*
 *  include/asm-s390/types.h
 *
 *  S390 version
 *
 *  Derived from "include/asm-i386/types.h"
 */

#ifndef _S390_TYPES_H
#define _S390_TYPES_H

#ifndef __ASSEMBLY__

typedef unsigned short umode_t;

/*
 * __xx is ok: it doesn't pollute the POSIX namespace. Use these in the
 * header files exported to user space
 */

typedef __signed__ char __s8;
typedef unsigned char __u8;

typedef __signed__ short __s16;
typedef unsigned short __u16;

typedef __signed__ int __s32;
typedef unsigned int __u32;

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)
typedef __signed__ long long __s64;
typedef unsigned long long __u64;
#endif
/* A address type so that arithmetic can be done on it & it can be upgraded to
   64 bit when neccessary 
*/
typedef __u32  addr_t; 
typedef __s32  saddr_t;

#endif /* __ASSEMBLY__ */

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__

#define BITS_PER_LONG 32

#ifndef __ASSEMBLY__

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed long long s64;
typedef unsigned long long u64;

typedef u32 dma_addr_t;

typedef union {
	unsigned long long pair;
	struct {
		unsigned long even;
		unsigned long odd;
	} subreg;
} register_pair;

#ifdef CONFIG_LBD
typedef u64 sector_t;
#define HAVE_SECTOR_T
#endif

#endif /* __ASSEMBLY__ */

#endif                                 /* __KERNEL__                       */
#endif
