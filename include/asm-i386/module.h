#ifndef _ASM_I386_MODULE_H
#define _ASM_I386_MODULE_H
/* x86 is simple */
struct mod_arch_specific
{
};

#define Elf_Shdr Elf32_Shdr
#define Elf_Sym Elf32_Sym
#define Elf_Ehdr Elf32_Ehdr
#endif /* _ASM_I386_MODULE_H */
