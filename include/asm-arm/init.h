#ifndef _ASMARM_INIT_H
#define _ASMARM_INIT_H

#if 0
#define __init __attribute__ ((__section__ (".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
#define __initfunc(__arginit) \
	__arginit __init; \
	__arginit
/* For assembly routines */
#define __INIT		.section	".text.init",@alloc,@execinstr
#define __FINIT	.previous
#define __INITDATA	.section	".data.init",@alloc,@write
#else
#define __init
#define __initdata
#define __initfunc(__arginit) __arginit
/* For assembly routines */
#define __INIT
#define __FINIT
#define __INITDATA
#endif

#endif
