/* $Id: types.h,v 1.9 1996/04/04 12:51:34 davem Exp $ */
#ifndef _SPARC_TYPES_H
#define _SPARC_TYPES_H

/*
 * _xx is ok: it doesn't pollute the POSIX namespace. Use these in the
 * header files exported to user space.
 */

/*
 * This file is never included by application software unless
 * explicitly requested (e.g., via linux/types.h) in which case the
 * application is Linux specific so (user-) name space pollution is
 * not a major issue.  However, for interoperability, libraries still
 * need to be careful to avoid a name clashes.
 */

typedef unsigned short umode_t;

typedef signed char __s8;
typedef unsigned char __u8;

typedef signed short __s16;
typedef unsigned short __u16;

typedef signed int __s32;
typedef unsigned int __u32;

typedef signed long long __s64;
typedef unsigned long long __u64;

#ifdef __KERNEL__

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

typedef signed long long s64;
typedef unsigned long long u64;

#endif /* __KERNEL__ */

#endif /* defined(_SPARC_TYPES_H) */
