/*
 *  linux/arch/m68knommu/platform/MC68VZ328/de2/config.c
 *
 *  Copyright (C) 1993 Hamish Macdonald
 *  Copyright (C) 1999 D. Jeff Dionne
 *  Copyright (C) 2001 Georges Menie, Ken Desmet
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <stdarg.h>
#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/kd.h>
#include <linux/netdevice.h>

#include <asm/setup.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/MC68VZ328.h>

#define CLOCK_COMPARE (32768/HZ)

static void dragen2_sched_init(void (*timer_routine)(int, void *, struct pt_regs *))
{
	/* disable timer 1 */
	TCTL = 0;

	/* set ISR */
	request_irq(TMR_IRQ_NUM, timer_routine, IRQ_FLG_LOCK, "timer", NULL);

	/* Restart mode, Enable int, 32KHz */
	TCTL = TCTL_OM | TCTL_IRQEN | TCTL_CLKSOURCE_32KHZ;
	TPRER = 0;
	TCMP = CLOCK_COMPARE-1; 

	/* Enable timer 1 */
	TCTL |= TCTL_TEN;
}

static void dragen2_tick(void)
{
  	/* Reset Timer1 */
	TSTAT &= 0;
}

static unsigned long dragen2_gettimeoffset(void)
{
	unsigned long ticks, offset = 0;

	ticks = TCN;

	if (ticks > (CLOCK_COMPARE>>1)) {
		/* check for pending interrupt */
		if (ISR & (1<<TMR_IRQ_NUM))
			offset = 1000000/HZ;
	}

	ticks = CLOCK_COMPARE - ticks;
	ticks = (1000000/HZ * ticks) / CLOCK_COMPARE;

	return ticks + offset;
}

static void dragen2_gettod(int *year, int *mon, int *day, int *hour, int *min, int *sec)
{
	long now = RTCTIME;

	*year = *mon = *day = 1;
	*hour = (now>>24)%24;
	*min = (now>>16)%60;
	*sec = now%60;
}

static void dragen2_reset(void)
{
	local_irq_disable();
	asm volatile ("
		movel #-1, 0xFFFFF304;
		moveb #0, 0xFFFFF300;
		moveal #0x04000000, %a0;
		moveal 0(%a0), %sp;
		moveal 4(%a0), %a0;
		jmp (%a0);
	");
}

int dragen2_cs8900_setup(struct net_device *dev)
{
	/* Set the ETH hardware address from its flash monitor location */
	memcpy(dev->dev_addr, (void *)0x400fffa, 6);
        dev->irq = INT1_IRQ_NUM;
	return dev->base_addr = 0x08000041;
}

static void init_hardware(void)
{
	/* CSGB Init */
	CSGBB = 0x4000;
	CSB = 0x1a1;

	/* CS8900 init */
	/* PK3: hardware sleep function pin, active low */
	PKSEL |= PK(3);	/* select pin as I/O */
	PKDIR |= PK(3);	/* select pin as output */
	PKDATA |= PK(3);	/* set pin high */

	/* PF5: hardware reset function pin, active high */
	PFSEL |= PF(5);	/* select pin as I/O */
	PFDIR |= PF(5);	/* select pin as output */
	PFDATA &= ~PF(5);	/* set pin low */

	/* cs8900 hardware reset */
	PFDATA |= PF(5);
	{ volatile int i; for (i = 0; i < 32000; ++i); }
	PFDATA &= ~PF(5);

	/* INT1 enable (cs8900 IRQ) */
	PDPOL &= ~PD(1);	/* active high signal */
	PDIQEG &= ~PD(1);
	PDIRQEN |= PD(1);	/* IRQ enabled */

#ifdef CONFIG_68328_SERIAL_UART2
	/* Enable RXD TXD port bits to enable UART2 */
	PJSEL &= ~(PJ(5)|PJ(4));
#endif
}

void config_BSP(char *command, int len)
{
	printk("68VZ328 DragonBallVZ support (c) 2001 Lineo, Inc.\n");
        command[0] = '\0'; /* no specific boot option */

	init_hardware();

	mach_sched_init      = dragen2_sched_init;
	mach_tick            = dragen2_tick;
	mach_gettimeoffset   = dragen2_gettimeoffset;
	mach_reset           = dragen2_reset;
	mach_gettod          = dragen2_gettod;

	config_M68328_irq();
}
