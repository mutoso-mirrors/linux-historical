#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

/*
 * 'kernel.h' contains some often-used function prototypes etc
 */

void verify_area(void * addr,int count);
volatile void panic(const char * str);
volatile void do_exit(long error_code);
unsigned long simple_strtoul(const char *,char **,unsigned int);
int printk(const char * fmt, ...);

void * kmalloc(unsigned int size, int priority);
void kfree_s(void * obj, int size);

#define kfree(x) kfree_s((x), 0)

/*
 * This is defined as a macro, but at some point this might become a
 * real subroutine that sets a flag if it returns true (to do
 * BSD-style accounting where the process is flagged if it uses root
 * privs).  The implication of this is that you should do normal
 * permissions checks first, and check suser() last.
 */
#define suser() (current->euid == 0)

#endif
