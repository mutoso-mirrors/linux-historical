/* Kernel module help for sparc64.
 *
 * Copyright (C) 2001 Rusty Russell.
 * Copyright (C) 2002 David S. Miller.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/elf.h>
#include <linux/vmalloc.h>
#include <linux/fs.h>
#include <linux/string.h>

static void *alloc_and_zero(unsigned long size)
{
	void *ret;

	/* We handle the zero case fine, unlike vmalloc */
	if (size == 0)
		return NULL;

	ret = vmalloc(size);
	if (!ret)
		ret = ERR_PTR(-ENOMEM);
	else
		memset(ret, 0, size);

	return ret;
}

/* Free memory returned from module_core_alloc/module_init_alloc */
void module_free(struct module *mod, void *module_region)
{
	vfree(module_region);
	/* FIXME: If module_region == mod->init_region, trim exception
           table entries. */
}

void *module_core_alloc(const Elf64_Ehdr *hdr,
			const Elf64_Shdr *sechdrs,
			const char *secstrings,
			struct module *module)
{
	return alloc_and_zero(module->core_size);
}

void *module_init_alloc(const Elf64_Ehdr *hdr,
			const Elf64_Shdr *sechdrs,
			const char *secstrings,
			struct module *module)
{
	return alloc_and_zero(module->init_size);
}

int apply_relocate(Elf64_Shdr *sechdrs,
		   const char *strtab,
		   unsigned int symindex,
		   unsigned int relsec,
		   struct module *me)
{
	printk(KERN_ERR "module %s: non-ADD RELOCATION unsupported\n",
	       me->name);
	return -ENOEXEC;
}

int apply_relocate_add(Elf64_Shdr *sechdrs,
		       const char *strtab,
		       unsigned int symindex,
		       unsigned int relsec,
		       struct module *me)
{
	unsigned int i;
	Elf64_Rela *rel = (void *)sechdrs[relsec].sh_offset;
	Elf64_Sym *sym;
	u8 *location;
	u32 *loc32;

	for (i = 0; i < sechdrs[relsec].sh_size / sizeof(*rel); i++) {
		Elf64_Addr v;

		/* This is where to make the change */
		location = (u8 *)sechdrs[sechdrs[relsec].sh_info].sh_offset
			+ rel[i].r_offset;
		loc32 = (u32 *) location;
		/* This is the symbol it is referring to */
		sym = (Elf64_Sym *)sechdrs[symindex].sh_offset
			+ ELF64_R_SYM(rel[i].r_info);
		if (!(v = sym->st_value)) {
			printk(KERN_WARNING "%s: Unknown symbol %s\n",
			       me->name, strtab + sym->st_name);
			return -ENOENT;
		}
		v += rel->r_addend;

		switch (ELF64_R_TYPE(rel[i].r_info) & 0xff) {
		case R_SPARC_64:
			location[0] = v >> 56;
			location[1] = v >> 48;
			location[2] = v >> 40;
			location[3] = v >> 32;
			location[4] = v >> 24;
			location[5] = v >> 16;
			location[6] = v >>  8;
			location[7] = v >>  0;
			break;

		case R_SPARC_32:
			location[0] = v >> 24;
			location[1] = v >> 16;
			location[2] = v >>  8;
			location[3] = v >>  0;
			break;

		case R_SPARC_WDISP30:
			v -= (Elf64_Addr) location;
			*loc32 = (*loc32 & ~0x3fffffff) |
				((v >> 2) & 0x3fffffff);
			break;

		case R_SPARC_WDISP22:
			v -= (Elf64_Addr) location;
			*loc32 = (*loc32 & ~0x3fffff) |
				((v >> 2) & 0x3fffff);
			break;

		case R_SPARC_LO10:
			*loc32 = (*loc32 & ~0x3ff) | (v & 0x3ff);
			break;

		case R_SPARC_HI22:
			*loc32 = (*loc32 & ~0x3fffff) |
				((v >> 10) & 0x3fffff);
			break;

		default:
			printk(KERN_ERR "module %s: Unknown relocation: %x\n",
			       me->name,
			       (int) (ELF64_R_TYPE(rel[i].r_info) & 0xff));
			return -ENOEXEC;
		}
	}
	return 0;
}

int module_finalize(const Elf_Ehdr *hdr,
		    const Elf_Shdr *sechdrs,
		    struct module *me)
{
	return 0;
}
