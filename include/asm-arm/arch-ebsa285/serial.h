/*
 * linux/include/asm-arm/arch-ebsa285/serial.h
 *
 * Copyright (c) 1996,1997,1998 Russell King.
 *
 * Changelog:
 *  15-10-1996	RMK	Created
 *  25-05-1998	PJB	CATS support
 */
#ifndef __ASM_ARCH_SERIAL_H
#define __ASM_ARCH_SERIAL_H

#include <linux/config.h>
#include <asm/irq.h>

/*
 * This assumes you have a 1.8432 MHz clock for your UART.
 *
 * It'd be nice if someone built a serial card with a 24.576 MHz
 * clock, since the 16550A is capable of handling a top speed of 1.5
 * megabits/second; but this requires the faster clock.
 */
#define BASE_BAUD (1843200 / 16)

#ifdef CONFIG_CATS
#define _SER_IRQ0	IRQ_ISA(4)
#define _SER_IRQ1	IRQ_ISA(3)
#else
#define _SER_IRQ0	0
#define _SER_IRQ1	0
#endif

#define STD_COM_FLAGS (ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST)

     /* UART CLK        PORT  IRQ     FLAGS        */
#define SERIAL_PORT_DFNS \
	{ 0, BASE_BAUD, 0x3F8, _SER_IRQ0, STD_COM_FLAGS },	/* ttyS0 */	\
	{ 0, BASE_BAUD, 0x2F8, _SER_IRQ1, STD_COM_FLAGS },	/* ttyS1 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS2 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS3 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS }, 	/* ttyS4 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS5 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS6 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS7 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS8 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS9 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS10 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS11 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS12 */	\
	{ 0, BASE_BAUD, 0    , 0        , STD_COM_FLAGS },	/* ttyS13 */

#endif
