#ifndef _ALPHA_TYPES_H
#define _ALPHA_TYPES_H

/*
 * These aren't exported outside the kernel to avoid name space clashes
 */
#ifdef __KERNEL__

typedef signed char s8;
typedef unsigned char u8;

typedef signed short s16;
typedef unsigned short u16;

typedef signed int s32;
typedef unsigned int u32;

/*
 * There are 32-bit compilers for the alpha out there..
 */
#if ((~0UL) == 0xffffffff)

typedef signed long long s64;
typedef unsigned long long u64;

#else

typedef signed long s64;
typedef unsigned long u64;

#endif

#endif /* __KERNEL__ */

#endif
