/*
 * include/asm-arm/leds.h
 *
 * Copyright (C) 1998 Russell King
 *
 * Event-driven interface for LEDs on machines
 *
 * Added led_start and led_stop- Alex Holden, 28th Dec 1998.
 */
#ifndef ASM_ARM_LEDS_H
#define ASM_ARM_LEDS_H

#include <linux/config.h>

typedef enum {
	led_idle_start,
	led_idle_end,
	led_timer,
	led_start,
	led_stop,
	led_claim,		/* override idle & timer leds */
	led_release,		/* restore idle & timer leds */
	led_green_on,
	led_green_off,
	led_amber_on,
	led_amber_off,
	led_red_on,
	led_red_off,
	/*
	 * I want this between led_timer and led_start, but
	 * someone has decided to export this to user space
	 */
	led_halted
} led_event_t;

/* Use this routine to handle LEDs */

#ifdef CONFIG_LEDS
extern void (*leds_event)(led_event_t);
#else
#define leds_event(e)
#endif

#endif
