/*
 * Copyright 2001-2002 Pavel Machek <pavel@suse.cz>
 * Based on code
 * Copyright 2001 Patrick Mochel <mochel@osdl.org>
 */
#include <asm/desc.h>
#include <asm/i387.h>

static inline void
arch_prepare_suspend(void)
{
	if (!cpu_has_pse)
		panic("pse required");
}

/* image of the saved processor state */
struct saved_context {
	u32 eax, ebx, ecx, edx;
	u32 esp, ebp, esi, edi;
	u16 es, fs, gs, ss;
	u32 cr0, cr2, cr3, cr4;
	u16 gdt_pad;
	u16 gdt_limit;
	u32 gdt_base;
	u16 idt_pad;
	u16 idt_limit;
	u32 idt_base;
	u16 ldt;
	u16 tss;
	u32 tr;
	u32 safety;
	u32 return_address;
	u32 eflags;
} __attribute__((packed));

#define loaddebug(thread,register) \
               __asm__("movl %0,%%db" #register  \
                       : /* no output */ \
                       :"r" ((thread)->debugreg[register]))

extern void do_fpu_end(void);
extern void fix_processor_context(void);
extern void do_magic(int resume);

#ifdef CONFIG_ACPI_SLEEP
extern unsigned long saved_eip;
extern unsigned long saved_esp;
extern unsigned long saved_ebp;
extern unsigned long saved_ebx;
extern unsigned long saved_esi;
extern unsigned long saved_edi;

static inline void acpi_save_register_state(unsigned long return_point)
{
	saved_eip = return_point;
	asm volatile ("movl %%esp,(%0)" : "=m" (saved_esp));
	asm volatile ("movl %%ebp,(%0)" : "=m" (saved_ebp));
	asm volatile ("movl %%ebx,(%0)" : "=m" (saved_ebx));
	asm volatile ("movl %%edi,(%0)" : "=m" (saved_edi));
	asm volatile ("movl %%esi,(%0)" : "=m" (saved_esi));
}

#define acpi_restore_register_state()  do {} while (0)

/* routines for saving/restoring kernel state */
extern int acpi_save_state_mem(void);
extern int acpi_save_state_disk(void);
#endif
