/*
 * Code to handle DECstation IRQs plus some generic interrupt stuff.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994, 1995, 1996, 1997, 2000 Ralf Baechle
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/seq_file.h>

#include <asm/bitops.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

#include <asm/dec/interrupts.h>

extern void dec_init_kn01(void);
extern void dec_init_kn230(void);
extern void dec_init_kn02(void);
extern void dec_init_kn02ba(void);
extern void dec_init_kn02ca(void);
extern void dec_init_kn03(void);

extern asmlinkage void decstation_handle_int(void);

unsigned long spurious_count = 0;

static inline void mask_irq(unsigned int irq_nr)
{
    unsigned int dummy;

    if (dec_interrupt[irq_nr].iemask) {		/* This is an ASIC interrupt    */
	*imr &= ~dec_interrupt[irq_nr].iemask;
	dummy = *imr;
	dummy = *imr;
    } else			/* This is a cpu interrupt        */
	change_cp0_status(ST0_IM, read_32bit_cp0_register(CP0_STATUS) & ~dec_interrupt[irq_nr].cpu_mask);
}

static inline void unmask_irq(unsigned int irq_nr)
{
    unsigned int dummy;

    if (dec_interrupt[irq_nr].iemask) {		/* This is an ASIC interrupt    */
	*imr |= dec_interrupt[irq_nr].iemask;
	dummy = *imr;
	dummy = *imr;
    }
    change_cp0_status(ST0_IM, read_32bit_cp0_register(CP0_STATUS) | dec_interrupt[irq_nr].cpu_mask);
}

void disable_irq(unsigned int irq_nr)
{
    unsigned long flags;

    save_and_cli(flags);
    mask_irq(irq_nr);
    restore_flags(flags);
}

void enable_irq(unsigned int irq_nr)
{
    unsigned long flags;

    save_and_cli(flags);
    unmask_irq(irq_nr);
    restore_flags(flags);
}

/*
 * Pointers to the low-level handlers: first the general ones, then the
 * fast ones, then the bad ones.
 */
extern void interrupt(void);

static struct irqaction *irq_action[32] =
{
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

int show_interrupts(struct seq_file *p, void *v)
{
	int i;
	struct irqaction *action;

	for (i = 0; i < 32; i++) {
		action = irq_action[i];
		if (!action)
			continue;
		seq_printf(p, "%2d: %8d %c %s",
				i, kstat.irqs[0][i],
				(action->flags & SA_INTERRUPT) ? '+' : ' ',
				action->name);
		for (action = action->next; action; action = action->next) {
			seq_printf(p, ",%s %s",
				(action->flags & SA_INTERRUPT) ? " +" : "",
				action->name);
		}
		seq_putc(p, '\n');
	}
	return 0;
}

/*
 * do_IRQ handles IRQ's that have been installed without the
 * SA_INTERRUPT flag: it uses the full signal-handling return
 * and runs with other interrupts enabled. All relatively slow
 * IRQ's should use this format: notably the keyboard/timer
 * routines.
 */
asmlinkage void do_IRQ(int irq, struct pt_regs *regs)
{
    struct irqaction *action;
    int do_random, cpu;

    cpu = smp_processor_id();
    irq_enter(cpu, irq);
    kstat.irqs[cpu][irq]++;

    mask_irq(irq);
    action = *(irq + irq_action);
    if (action) {
	if (!(action->flags & SA_INTERRUPT))
	    __sti();
	action = *(irq + irq_action);
	do_random = 0;
	do {
	    do_random |= action->flags;
	    action->handler(irq, action->dev_id, regs);
	    action = action->next;
	} while (action);
	if (do_random & SA_SAMPLE_RANDOM)
	    add_interrupt_randomness(irq);
	__cli();
	unmask_irq(irq);
    }
    irq_exit(cpu, irq);

    /* unmasking and bottom half handling is done magically for us. */
}

/*
 * Idea is to put all interrupts
 * in a single table and differenciate them just by number.
 */
int setup_dec_irq(int irq, struct irqaction *new)
{
    int shared = 0;
    struct irqaction *old, **p;
    unsigned long flags;

    p = irq_action + irq;
    if ((old = *p) != NULL) {
	/* Can't share interrupts unless both agree to */
	if (!(old->flags & new->flags & SA_SHIRQ))
	    return -EBUSY;

	/* Can't share interrupts unless both are same type */
	if ((old->flags ^ new->flags) & SA_INTERRUPT)
	    return -EBUSY;

	/* add new interrupt at end of irq queue */
	do {
	    p = &old->next;
	    old = *p;
	} while (old);
	shared = 1;
    }
    if (new->flags & SA_SAMPLE_RANDOM)
	rand_initialize_irq(irq);

    save_and_cli(flags);
    *p = new;

    if (!shared) {
	unmask_irq(irq);
    }
    restore_flags(flags);
    return 0;
}

int request_irq(unsigned int irq,
		void (*handler) (int, void *, struct pt_regs *),
		unsigned long irqflags,
		const char *devname,
		void *dev_id)
{
    int retval;
    struct irqaction *action;

    if (irq >= 32)
	return -EINVAL;
    if (!handler)
	return -EINVAL;

    action = (struct irqaction *) kmalloc(sizeof(struct irqaction), GFP_KERNEL);
    if (!action)
	return -ENOMEM;

    action->handler = handler;
    action->flags = irqflags;
    action->mask = 0;
    action->name = devname;
    action->next = NULL;
    action->dev_id = dev_id;

    retval = setup_dec_irq(irq, action);

    if (retval)
	kfree(action);
    return retval;
}

void free_irq(unsigned int irq, void *dev_id)
{
    struct irqaction *action, **p;
    unsigned long flags;

    if (irq > 39) {
	printk("Trying to free IRQ%d\n", irq);
	return;
    }
    for (p = irq + irq_action; (action = *p) != NULL; p = &action->next) {
	if (action->dev_id != dev_id)
	    continue;

	/* Found it - now free it */
	save_and_cli(flags);
	*p = action->next;
	if (!irq[irq_action])
	    mask_irq(irq);
	restore_flags(flags);
	kfree(action);
	return;
    }
    printk("Trying to free free IRQ%d\n", irq);
}

unsigned long probe_irq_on(void)
{
    /* TODO */
    return 0;
}

int probe_irq_off(unsigned long irqs)
{
    /* TODO */
    return 0;
}

void __init init_IRQ(void)
{
    switch (mips_machtype) {
    case MACH_DS23100:
	dec_init_kn01();
	break;
    case MACH_DS5100:		/*  DS5100 MIPSMATE */
	dec_init_kn230();
	break;
    case MACH_DS5000_200:	/* DS5000 3max */
	dec_init_kn02();
	break;
    case MACH_DS5000_1XX:	/* DS5000/100 3min */
	dec_init_kn02ba();
	break;
    case MACH_DS5000_2X0:	/* DS5000/240 3max+ */
	dec_init_kn03();
	break;
    case MACH_DS5000_XX:	/* Personal DS5000/2x */
	dec_init_kn02ca();
	break;
    case MACH_DS5800:		/* DS5800 Isis */
	panic("Don't know how to set this up!");
	break;
    case MACH_DS5400:		/* DS5400 MIPSfair */
	panic("Don't know how to set this up!");
	break;
    case MACH_DS5500:		/* DS5500 MIPSfair-2 */
	panic("Don't know how to set this up!");
	break;
    }
    set_except_vector(0, decstation_handle_int);
}
