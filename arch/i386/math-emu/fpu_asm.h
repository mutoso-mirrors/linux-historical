/*---------------------------------------------------------------------------+
 |  fpu_asm.h                                                                |
 |                                                                           |
 | Copyright (C) 1992,1995                                                   |
 |                       W. Metzenthen, 22 Parker St, Ormond, Vic 3163,      |
 |                       Australia.  E-mail billm@jacobi.maths.monash.edu.au |
 |                                                                           |
 +---------------------------------------------------------------------------*/

#ifndef _FPU_ASM_H_
#define _FPU_ASM_H_

#include <linux/linkage.h>

#define	EXCEPTION	SYMBOL_NAME(FPU_exception)


#define PARAM1	8(%ebp)
#define	PARAM2	12(%ebp)
#define	PARAM3	16(%ebp)
#define	PARAM4	20(%ebp)

#define SIGL_OFFSET 8
#define SIGN(x)	(x)
#define	TAG(x)	1(x)
#define	EXP(x)	4(x)
#define SIG(x)	SIGL_OFFSET##(x)
#define	SIGL(x)	SIGL_OFFSET##(x)
#define	SIGH(x)	12(x)

#endif _FPU_ASM_H_
