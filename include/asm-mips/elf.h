#ifndef __ASM_MIPS_ELF_H
#define __ASM_MIPS_ELF_H

/*
 * ELF register definitions
 * This is "make it compile" stuff!
 */
#define ELF_NGREG	32
#define ELF_NFPREG	32

typedef unsigned long elf_greg_t;
typedef elf_greg_t elf_gregset_t[ELF_NGREG];

typedef double elf_fpreg_t;
typedef elf_fpreg_t elf_fpregset_t[ELF_NFPREG];

#endif /* __ASM_MIPS_ELF_H */
