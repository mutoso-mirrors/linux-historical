/*
 * linux/include/asm-arm/arch-ebsa110/irqs.h
 *
 * Copyright (C) 1996 Russell King
 */

#define IRQ_PRINTER		0
#define IRQ_COM1		1
#define IRQ_COM2		2
#define IRQ_ETHERNET		3
#define IRQ_TIMER0		4
#define IRQ_TIMER1		5
#define IRQ_PCMCIA		6
#define IRQ_IMMEDIATE		7

#define IRQ_TIMER		IRQ_TIMER0

#define irq_cannonicalize(i)	(i)

