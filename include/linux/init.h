#ifndef _LINUX_INIT_H
#define _LINUX_INIT_H

/* These macros are used to mark some functions or 
 * initialized data (doesn't apply to uninitialized data)
 * as `initialization' functions. The kernel can take this
 * as hint that the function is used only during the initialization
 * phase and free up used memory resources after
 *
 * Usage:
 * For functions:
 * 
 * You should add __init immediately before the function name, like:
 *
 * static void __init initme(int x, int y)
 * {
 *    extern int z; z = x * y;
 * }
 *
 * Depricated: you can surround the whole function declaration 
 * just before function body into __initfunc() macro, like:
 *
 * __initfunc (static void initme(int x, int y))
 * {
 *    extern int z; z = x * y;
 * }
 *
 * If the function has a prototype somewhere, you can also add
 * __init between closing brace of the prototype and semicolon:
 *
 * extern int initialize_foobar_device(int, int, int) __init;
 *
 * For initialized data:
 * You should insert __initdata between the variable name and equal
 * sign followed by value, e.g.:
 *
 * static int init_variable __initdata = 0;
 * static char linux_logo[] __initdata = { 0x32, 0x36, ... };
 *
 * For initialized data not at file scope, i.e. within a function,
 * you should use __initlocaldata instead, due to a bug in GCC 2.7.
 */

/*
 * Disable the __initfunc macros if a file that is a part of a
 * module attempts to use them. We do not want to interfere
 * with module linking.
 */

#ifndef MODULE

/*
 * Used for initialization calls..
 */
typedef int (*initcall_t)(void);

extern initcall_t __initcall_start, __initcall_end;

#define __initcall(fn)								\
	static __attribute__ ((unused,__section__ (".initcall.init")))		\
		initcall_t __initcall_##fn = fn

/*
 * Used for kernel command line parameter setup
 */
struct kernel_param {
	const char *str;
	int (*setup_func)(char *);
};

extern struct kernel_param __setup_start, __setup_end;

#define __setup(str, fn)							\
	static __attribute__ ((__section__ (".data.init")))			\
		char __setup_str_##fn[] = str;					\
	static __attribute__ ((unused,__section__ (".setup.init")))		\
		struct kernel_param __setup_##fn = { __setup_str_##fn, fn }

/*
 * Mark functions and data as being only used at initialization
 * or exit time.
 */
#define __init __attribute__ ((__section__ (".text.init")))
#define __exit __attribute__ ((unused, __section__(".text.init")))
#define __initdata __attribute__ ((__section__ (".data.init")))
#define __exitdata __attribute__ ((unused, __section__ (".data.init")))

#define __initfunc(__arginit) \
	__arginit __init; \
	__arginit

/* For assembly routines */
#define __INIT		.section	".text.init","ax"
#define __FINIT		.previous
#define __INITDATA	.section	".data.init","aw"

#define module_init(x)	__initcall(x);
#define module_exit(x)	/* nothing */

#else

#define __init
#define __exit
#define __initdata
#define __exitdata
#define __initfunc(__arginit) __arginit
#define __initcall
/* For assembly routines */
#define __INIT
#define __FINIT
#define __INITDATA

/* Not sure what version aliases were introduced in, but certainly in 2.95.  */
#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 95)
#define module_init(x)	int init_module(void) __attribute__((alias(#x)));
#define module_exit(x)	void cleanup_module(void) __attribute__((alias(#x)));
#else
#define module_init(x)	int init_module(void) { return x(); }
#define module_exit(x)	void cleanup_module(void) { x(); }
#endif

#endif

#if __GNUC__ > 2 || (__GNUC__ == 2 && __GNUC_MINOR__ >= 8)
#define __initlocaldata  __initdata
#else
#define __initlocaldata
#endif

#endif
