/* $Id: conv.h,v 1.2 1997/09/03 12:29:13 jj Exp $
 * conv.h: Utility macros for Solaris emulation
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
 
/* #define DEBUG_SOLARIS */

#ifndef __ASSEMBLY__

#include <asm/unistd.h>

/* As gcc will warn about casting u32 to some ptr, we have to cast it to
 * unsigned long first, and that's what is A() for.
 * You just do (void *)A(x), instead of having to
 * type (void *)((unsigned long)x) or instead of just (void *)x, which will
 * produce warnings.
 */
#define A(x) ((unsigned long)x)

extern unsigned sys_call_table[];
extern unsigned sys_call_table32[];
extern unsigned sunos_sys_table[];

#define SYS(name) ((long)sys_call_table[__NR_##name])
#define SUNOS(x) ((long)sunos_sys_table[x])

#endif /* __ASSEMBLY__ */
