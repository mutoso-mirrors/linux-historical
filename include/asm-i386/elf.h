#ifndef __ASMi386_ELF_H
#define __ASMi386_ELF_H

/*
 * ELF register definitions..
 */

#include <asm/ptrace.h>
#include <asm/user.h>

typedef unsigned long elf_greg_t;

#define ELF_NGREG (sizeof (struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef struct user_i387_struct elf_fpregset_t;

/*
 * This is used to ensure we don't load something for the wrong architecture.
 */
#define elf_check_arch(x) ( ((x) == EM_386) || ((x) == EM_486) )

/*
 * These are used to set parameters in the core dumps.
 */
#define ELF_CLASS	ELFCLASS32
#define ELF_DATA	ELFDATA2LSB;
#define ELF_ARCH	EM_386

	/* SVR4/i386 ABI (pages 3-31, 3-32) says that when the program
	   starts %edx contains a pointer to a function which might be
	   registered using `atexit'.  This provides a mean for the
	   dynamic linker to call DT_FINI functions for shared libraries
	   that have been loaded before the code runs.

	   A value of 0 tells we have no such handler.  */
#define ELF_PLAT_INIT(_r)	_r->edx = 0

#define USE_ELF_CORE_DUMP
#define ELF_EXEC_PAGESIZE	4096

/* Wow, the "main" arch needs arch dependent functions too.. :) */

/* regs is struct pt_regs, pr_reg is elf_gregset_t (which is
   now struct_user_regs, they are different) */

#define ELF_CORE_COPY_REGS(pr_reg, regs)		\
	pr_reg[0] = regs->ebx;				\
	pr_reg[1] = regs->ecx;				\
	pr_reg[2] = regs->edx;				\
	pr_reg[3] = regs->esi;				\
	pr_reg[4] = regs->edi;				\
	pr_reg[5] = regs->ebp;				\
	pr_reg[6] = regs->eax;				\
	pr_reg[7] = regs->xds;				\
	pr_reg[8] = regs->xes;				\
	/* fake once used fs and gs selectors? */	\
	pr_reg[9] = regs->xds;	/* was fs and __fs */	\
	pr_reg[10] = regs->xds;	/* was gs and __gs */	\
	pr_reg[11] = regs->orig_eax;			\
	pr_reg[12] = regs->eip;				\
	pr_reg[13] = regs->xcs;				\
	pr_reg[14] = regs->eflags;			\
	pr_reg[15] = regs->esp;				\
	pr_reg[16] = regs->xss;

#endif
