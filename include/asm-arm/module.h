#ifndef _ASM_ARM_MODULE_H
#define _ASM_ARM_MODULE_H

struct mod_arch_specific
{
	int foo;
};

#define Elf_Shdr	Elf32_Shdr
#define Elf_Sym		Elf32_Sym
#define Elf_Ehdr	Elf32_Ehdr

#endif /* _ASM_ARM_MODULE_H */
