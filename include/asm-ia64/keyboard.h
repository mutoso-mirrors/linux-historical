#ifndef _ASM_IA64_KEYBOARD_H
#define _ASM_IA64_KEYBOARD_H

/*
 * This file contains the ia-64 architecture specific keyboard
 * definitions.
 *
 * Copyright (C) 1998, 1999 Hewlett-Packard Co
 * Copyright (C) 1998, 1999 David Mosberger-Tang <davidm@hpl.hp.com>
 */

# ifdef __KERNEL__

#include <linux/irq.h>

#define KEYBOARD_IRQ			isa_irq_to_vector(1)
#define DISABLE_KBD_DURING_INTERRUPTS	0

extern int pckbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pckbd_getkeycode(unsigned int scancode);
extern int pckbd_pretranslate(unsigned char scancode, char raw_mode);
extern int pckbd_translate(unsigned char scancode, unsigned char *keycode,
			   char raw_mode);
extern char pckbd_unexpected_up(unsigned char keycode);
extern void pckbd_leds(unsigned char leds);
extern void pckbd_init_hw(void);
extern unsigned char pckbd_sysrq_xlate[128];

#define kbd_setkeycode		pckbd_setkeycode
#define kbd_getkeycode		pckbd_getkeycode
#define kbd_pretranslate	pckbd_pretranslate
#define kbd_translate		pckbd_translate
#define kbd_unexpected_up	pckbd_unexpected_up
#define kbd_leds		pckbd_leds
#define kbd_init_hw		pckbd_init_hw
#define kbd_sysrq_xlate		pckbd_sysrq_xlate

#define INIT_KBD

#define SYSRQ_KEY		0x54

/* resource allocation */
#define kbd_request_region()
#define kbd_request_irq(handler) request_irq(KEYBOARD_IRQ, handler, 0, "keyboard", NULL)

/* How to access the keyboard macros on this platform.  */
#define kbd_read_input()	inb(KBD_DATA_REG)
#define kbd_read_status()	inb(KBD_STATUS_REG)
#define kbd_write_output(val)	outb(val, KBD_DATA_REG)
#define kbd_write_command(val)	outb(val, KBD_CNTL_REG)

/* Some stoneage hardware needs delays after some operations.  */
#define kbd_pause() do { } while(0)

/*
 * Machine specific bits for the PS/2 driver
 */

#define AUX_IRQ				isa_irq_to_vector(12)

#define aux_request_irq(hand, dev_id)					\
	request_irq(AUX_IRQ, hand, SA_SHIRQ, "PS/2 Mouse", dev_id)

#define aux_free_irq(dev_id) free_irq(AUX_IRQ, dev_id)

# endif /* __KERNEL__ */
#endif /* _ASM_IA64_KEYBOARD_H */
