/*
 * linux/arch/arm/mach-h720x/h7201-eval.c
 *
 * Copyright (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *               2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *               2004 Sascha Hauer    <s.hauer@pengutronix.de>
 *
 * Architecture specific stuff for Hynix GMS30C7201 development board
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/device.h>

#include <asm/setup.h>
#include <asm/types.h>
#include <asm/mach-types.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mach/arch.h>
#include <asm/hardware.h>

extern void h720x_init_irq (void);
extern void h7201_init_time(void);
extern void __init h720x_map_io(void);

MACHINE_START(H7201, "Hynix GMS30C7201")
	MAINTAINER("Robert Schwebel, Pengutronix")
	BOOT_MEM(0x40000000, 0x80000000, 0xf0000000)
	BOOT_PARAMS(0xc0001000)
	MAPIO(h720x_map_io)
	INITIRQ(h720x_init_irq)
	INITTIME(h7201_init_time)
MACHINE_END
