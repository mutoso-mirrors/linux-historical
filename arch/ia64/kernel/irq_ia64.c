/*
 * linux/arch/ia64/kernel/irq.c
 *
 * Copyright (C) 1998-2000 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 1999-2000 David Mosberger-Tang <davidm@hpl.hp.com>
 *
 *  6/10/99: Updated to bring in sync with x86 version to facilitate
 *	     support for SMP and different interrupt controllers.
 */

#include <linux/config.h>

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel_stat.h>
#include <linux/malloc.h>
#include <linux/ptrace.h>
#include <linux/random.h>	/* for rand_initialize_irq() */
#include <linux/signal.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/threads.h>

#include <asm/bitops.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/hw_irq.h>
#include <asm/machvec.h>
#include <asm/pgtable.h>
#include <asm/system.h>

#define IRQ_DEBUG	0

#ifdef CONFIG_ITANIUM_A1_SPECIFIC
spinlock_t ivr_read_lock;
#endif

/* default base addr of IPI table */
unsigned long ipi_base_addr = (__IA64_UNCACHED_OFFSET | IPI_DEFAULT_BASE_ADDR);	

/*
 * Legacy IRQ to IA-64 vector translation table.  Any vector not in
 * this table maps to itself (ie: irq 0x30 => IA64 vector 0x30)
 */
__u8 isa_irq_to_vector_map[16] = {
	/* 8259 IRQ translation, first 16 entries */
	0x60, 0x50, 0x10, 0x51, 0x52, 0x53, 0x43, 0x54,
	0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x40, 0x41
};

#ifdef CONFIG_ITANIUM_A1_SPECIFIC

int usbfix;

static int __init
usbfix_option (char *str)
{
	printk("irq: enabling USB workaround\n");
	usbfix = 1;
	return 1;
}

__setup("usbfix", usbfix_option);

#endif /* CONFIG_ITANIUM_A1_SPECIFIC */

/*
 * That's where the IVT branches when we get an external
 * interrupt. This branches to the correct hardware IRQ handler via
 * function ptr.
 */
void
ia64_handle_irq (unsigned long vector, struct pt_regs *regs)
{
	unsigned long saved_tpr;
#ifdef CONFIG_ITANIUM_A1_SPECIFIC
	unsigned long eoi_ptr;
 
# ifdef CONFIG_USB
	extern void reenable_usb (void);
	extern void disable_usb (void);

	if (usbfix)
		disable_usb();
# endif
	/*
	 * Stop IPIs by getting the ivr_read_lock
	 */
	spin_lock(&ivr_read_lock);
	{
		unsigned int tmp;
		/*
		 * Disable PCI writes
		 */
		outl(0x80ff81c0, 0xcf8);
		tmp = inl(0xcfc);
		outl(tmp | 0x400, 0xcfc);
		eoi_ptr = inl(0xcfc);
		vector = ia64_get_ivr();
		/*
		 * Enable PCI writes
		 */
		outl(tmp, 0xcfc);
	}
	spin_unlock(&ivr_read_lock);

# ifdef CONFIG_USB
	if (usbfix)
		reenable_usb();
# endif
#endif /* CONFIG_ITANIUM_A1_SPECIFIC */

#if IRQ_DEBUG
	{
		unsigned long bsp, sp;

		/*
		 * Note: if the interrupt happened while executing in
		 * the context switch routine (ia64_switch_to), we may
		 * get a spurious stack overflow here.  This is
		 * because the register and the memory stack are not
		 * switched atomically.
		 */
		asm ("mov %0=ar.bsp" : "=r"(bsp));
		asm ("mov %0=sp" : "=r"(sp));

		if ((sp - bsp) < 1024) {
			static unsigned char count;
			static long last_time;

			if (count > 5 && jiffies - last_time > 5*HZ)
				count = 0;
			if (++count < 5) {
				last_time = jiffies;
				printk("ia64_handle_irq: DANGER: less than "
				       "1KB of free stack space!!\n"
				       "(bsp=0x%lx, sp=%lx)\n", bsp, sp);
			}
		}
	}
#endif /* IRQ_DEBUG */

	/*
	 * Always set TPR to limit maximum interrupt nesting depth to
	 * 16 (without this, it would be ~240, which could easily lead
	 * to kernel stack overflows).
	 */
	saved_tpr = ia64_get_tpr();
	ia64_srlz_d();
	do {
		if (vector >= NR_IRQS) {
			printk("handle_irq: invalid vector %lu\n", vector);
			ia64_set_tpr(saved_tpr);
			ia64_srlz_d();
			return;
		}
		ia64_set_tpr(vector);
		ia64_srlz_d();

		do_IRQ(vector, regs);

		/*
		 * Disable interrupts and send EOI:
		 */
		local_irq_disable();
		ia64_set_tpr(saved_tpr);
		ia64_eoi();
#ifdef CONFIG_ITANIUM_A1_SPECIFIC
		break;
#endif
		vector = ia64_get_ivr();
	} while (vector != IA64_SPURIOUS_INT);
}

#ifdef CONFIG_SMP

extern void handle_IPI (int irq, void *dev_id, struct pt_regs *regs);

static struct irqaction ipi_irqaction = {
	handler:	handle_IPI,
	flags:		SA_INTERRUPT,
	name:		"IPI"
};
#endif

void __init
init_IRQ (void)
{
	/*
	 * Disable all local interrupts
	 */
	ia64_set_itv(0, 1);
	ia64_set_lrr0(0, 1);	
	ia64_set_lrr1(0, 1);	

	irq_desc[IA64_SPURIOUS_INT].handler = &irq_type_ia64_sapic;
#ifdef CONFIG_SMP
	/* 
	 * Configure the IPI vector and handler
	 */
	irq_desc[IPI_IRQ].status |= IRQ_PER_CPU;
	irq_desc[IPI_IRQ].handler = &irq_type_ia64_sapic;
	setup_irq(IPI_IRQ, &ipi_irqaction);
#endif

	ia64_set_pmv(1 << 16);
	ia64_set_cmcv(CMC_IRQ);			/* XXX fix me */

	platform_irq_init();

	/* clear TPR to enable all interrupt classes: */
	ia64_set_tpr(0);
}

void
ipi_send (int cpu, int vector, int delivery_mode, int redirect)
{
	unsigned long ipi_addr;
	unsigned long ipi_data;
	unsigned long phys_cpu_id;
#ifdef CONFIG_ITANIUM_A1_SPECIFIC
	unsigned long flags;
#endif

#ifdef CONFIG_SMP
	phys_cpu_id = cpu_physical_id(cpu);
#else
	phys_cpu_id = (ia64_get_lid() >> 16) & 0xffff;
#endif

	/*
	 * cpu number is in 8bit ID and 8bit EID
	 */

	ipi_data = (delivery_mode << 8) | (vector & 0xff);
	ipi_addr = ipi_base_addr | (phys_cpu_id << 4) | ((redirect & 1)  << 3);

#ifdef CONFIG_ITANIUM_A1_SPECIFIC
	spin_lock_irqsave(&ivr_read_lock, flags);
#endif

	writeq(ipi_data, ipi_addr);

#ifdef CONFIG_ITANIUM_A1_SPECIFIC
	spin_unlock_irqrestore(&ivr_read_lock, flags);
#endif
}
