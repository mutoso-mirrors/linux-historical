#include <linux/module.h>
#include <linux/smp.h>
#include <linux/user.h>
#include <linux/elfcore.h>

#include <asm/semaphore.h>
#include <asm/io.h>

extern void dump_thread(struct pt_regs *, struct user *);
extern int dump_fpu(elf_fpregset_t *);

static struct symbol_table arch_symbol_table = {
#include <linux/symtab_begin.h>
	/* platform dependent support */
	X(dump_thread),
	X(dump_fpu),
	X(ioremap),
	XNOVERS(__down_failed),
	XNOVERS(__up_wakeup),
#ifdef __SMP__
	X(apic_reg),		/* Needed internally for the I386 inlines */
	X(cpu_data),
	X(syscall_count),
#endif
#include <linux/symtab_end.h>
};

void arch_syms_export(void)
{
	register_symtab(&arch_symbol_table);
}
