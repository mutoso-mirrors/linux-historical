/*
 * time.c: Generic SGI handler for (spurious) 8254 interrupts
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <asm/sgialib.h>

void indy_8254timer_irq(void)
{
	int cpu = smp_processor_id();
	int irq = 4;

	irq_enter(cpu, irq);
	kstat_cpu(0).irqs[irq]++;
	printk("indy_8254timer_irq: Whoops, should not have gotten this IRQ\n");
	prom_getchar();
	ArcEnterInteractiveMode();
	irq_exit(cpu, irq);
}
