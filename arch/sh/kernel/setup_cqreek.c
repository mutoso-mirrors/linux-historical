/* $Id: setup_cqreek.c,v 1.1 2000/08/05 06:25:23 gniibe Exp $
 *
 * arch/sh/kernel/setup_cqreek.c
 *
 * Copyright (C) 2000  Niibe Yutaka
 *
 * CqREEK IDE/ISA Bridge Support.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>

#include <asm/io.h>
#include <asm/io_generic.h>
#include <asm/irq.h>
#include <asm/machvec.h>
#include <asm/machvec_init.h>

#define BRIDGE_FEATURE		0x0002

#define BRIDGE_IDE_CTRL		0x0018
#define BRIDGE_IDE_INTR_LVL    	0x001A
#define BRIDGE_IDE_INTR_MASK	0x001C
#define BRIDGE_IDE_INTR_STAT	0x001E

#define BRIDGE_ISA_CTRL		0x0028
#define BRIDGE_ISA_INTR_LVL    	0x002A
#define BRIDGE_ISA_INTR_MASK	0x002C
#define BRIDGE_ISA_INTR_STAT	0x002E

#define IDE_OFFSET 0xA4000000UL
#define ISA_OFFSET 0xA4A00000UL

static unsigned long cqreek_port2addr(unsigned long port)
{
	if (0x0000<=port && port<=0x0040)
		return IDE_OFFSET + port;
	if ((0x01f0<=port && port<=0x01f7) || port == 0x03f6)
		return IDE_OFFSET + port;

	return ISA_OFFSET + port;
}

static void disable_cqreek_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned short mask;

	save_and_cli(flags);
	/* Disable IRQ */
	mask = inw(BRIDGE_ISA_INTR_MASK) & ~(1 << irq);
	outw_p(mask, BRIDGE_ISA_INTR_MASK);
	restore_flags(flags);
}

static void enable_cqreek_irq(unsigned int irq)
{
	unsigned long flags;
	unsigned short mask;

	save_and_cli(flags);
	/* Enable IRQ */
	mask = inw(BRIDGE_ISA_INTR_MASK) | (1 << irq);
	outw_p(mask, BRIDGE_ISA_INTR_MASK);
	restore_flags(flags);
}

#define CLEAR_AT_ACCEPT

static void mask_and_ack_cqreek(unsigned int irq)
{
	inw(BRIDGE_ISA_INTR_STAT);
	disable_cqreek_irq(irq);
#ifdef CLEAR_AT_ACCEPT
	/* Clear IRQ (it might be edge IRQ) */
	outw_p((1<<irq), BRIDGE_ISA_INTR_STAT);
#endif
}

static void end_cqreek_irq(unsigned int irq)
{
#ifndef CLEAR_AT_ACCEPT
	/* Clear IRQ (it might be edge IRQ) */
	outw_p((1<<irq), BRIDGE_ISA_INTR_STAT);
#endif
	enable_cqreek_irq(irq);
}

static unsigned int startup_cqreek_irq(unsigned int irq)
{ 
	enable_cqreek_irq(irq);
	return 0;
}

static void shutdown_cqreek_irq(unsigned int irq)
{
	disable_cqreek_irq(irq);
}

static struct hw_interrupt_type cqreek_irq_type = {
	"CQREEK-IRQ",
	startup_cqreek_irq,
	shutdown_cqreek_irq,
	enable_cqreek_irq,
	disable_cqreek_irq,
	mask_and_ack_cqreek,
	end_cqreek_irq
};

static int has_ide, has_isa;

/* XXX: This is just for test for my NE2000 ISA board
   What we really need is virtualized IRQ and demultiplexer like HP600 port */
void __init init_cqreek_IRQ(void)
{
	if (has_ide)
		make_ipr_irq(14, IDE_OFFSET+BRIDGE_IDE_INTR_LVL, 0, 0x0f-14);

	if (has_isa) {
		/* XXX: Err... we may need demultiplexer for ISA irq... */
		irq_desc[10].handler = &cqreek_irq_type;
		irq_desc[10].status = IRQ_DISABLED;
		irq_desc[10].action = 0;
		irq_desc[10].depth = 1;

		disable_cqreek_irq(10);
	}
}

/*
 * Initialize the board
 */
void __init setup_cqreek(void)
{
	int i;
/* udelay is not available at setup time yet... */
#define DELAY() do {for (i=0; i<10000; i++) ctrl_inw(0xa0000000);} while(0)

	if ((inw (BRIDGE_FEATURE) & 1)) { /* We have IDE interface */
		outw_p(0, BRIDGE_IDE_INTR_LVL);
		outw_p(0, BRIDGE_IDE_INTR_MASK);

		outw_p(0, BRIDGE_IDE_CTRL);
		DELAY();

		outw_p(0x8000, BRIDGE_IDE_CTRL);
		DELAY();

		outw_p(0xffff, BRIDGE_IDE_INTR_STAT); /* Clear interrupt status */
		outw_p(0x0f-14, BRIDGE_IDE_INTR_LVL); /* Use 14 IPR */
		outw_p(1, BRIDGE_IDE_INTR_MASK); /* Enable interrupt */
		has_ide=1;
	}

	if ((inw (BRIDGE_FEATURE) & 2)) { /* We have ISA interface */
		outw_p(0, BRIDGE_ISA_INTR_LVL);
		outw_p(0, BRIDGE_ISA_INTR_MASK);

		outw_p(0, BRIDGE_ISA_CTRL);
		DELAY();
		outw_p(0x8000, BRIDGE_ISA_CTRL);
		DELAY();

		outw_p(0xffff, BRIDGE_ISA_INTR_STAT); /* Clear interrupt status */
		outw_p(0x0f-10, BRIDGE_ISA_INTR_LVL); /* Use 10 IPR */
		outw_p(0xfff8, BRIDGE_ISA_INTR_MASK); /* Enable interrupt */
		has_isa=1;
	}

	printk(KERN_INFO "CqREEK Setup (IDE=%d, ISA=%d)...done\n", has_ide, has_isa);
}

/*
 * The Machine Vector
 */

struct sh_machine_vector mv_cqreek __initmv = {
	mv_name:		"CqREEK",

#if defined(__SH4__)
	mv_nr_irqs:		48,
#elif defined(CONFIG_CPU_SUBTYPE_SH7708)
	mv_nr_irqs:		32,
#elif defined(CONFIG_CPU_SUBTYPE_SH7709)
	mv_nr_irqs:		61,
#endif

	mv_inb:			generic_inb,
	mv_inw:			generic_inw,
	mv_inl:			generic_inl,
	mv_outb:		generic_outb,
	mv_outw:		generic_outw,
	mv_outl:		generic_outl,

	mv_inb_p:		generic_inb_p,
	mv_inw_p:		generic_inw_p,
	mv_inl_p:		generic_inl_p,
	mv_outb_p:		generic_outb_p,
	mv_outw_p:		generic_outw_p,
	mv_outl_p:		generic_outl_p,

	mv_insb:		generic_insb,
	mv_insw:		generic_insw,
	mv_insl:		generic_insl,
	mv_outsb:		generic_outsb,
	mv_outsw:		generic_outsw,
	mv_outsl:		generic_outsl,

	mv_readb:		generic_readb,
	mv_readw:		generic_readw,
	mv_readl:		generic_readl,
	mv_writeb:		generic_writeb,
	mv_writew:		generic_writew,
	mv_writel:		generic_writel,

	mv_init_arch:		setup_cqreek,
	mv_init_irq:		init_cqreek_IRQ,

	mv_port2addr:		cqreek_port2addr,
	mv_isa_port2addr:	cqreek_port2addr,
};
ALIAS_MV(cqreek)
