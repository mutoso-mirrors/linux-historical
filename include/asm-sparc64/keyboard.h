/* $Id: keyboard.h,v 1.1 1997/09/04 05:50:39 ecd Exp $
 * linux/include/asm-sparc64/keyboard.h
 *
 * Created Aug 29 1997 by Eddie C. Dost (ecd@skynet.be)
 */

/*
 *  This file contains the Ultra/PCI architecture specific keyboard definitions
 */

#ifndef _SPARC64_KEYBOARD_H
#define _SPARC64_KEYBOARD_H 1

#ifdef __KERNEL__

#define KEYBOARD_IRQ			1
#define DISABLE_KBD_DURING_INTERRUPTS	0

extern int pcikbd_setkeycode(unsigned int scancode, unsigned int keycode);
extern int pcikbd_getkeycode(unsigned int scancode);
extern int pcikbd_pretranslate(unsigned char scancode, char raw_mode);
extern int pcikbd_translate(unsigned char scancode, unsigned char *keycode,
			    char raw_mode);
extern char pcikbd_unexpected_up(unsigned char keycode);
extern void pcikbd_leds(unsigned char leds);
extern void pcikbd_init_hw(void);
extern unsigned char pcikbd_sysrq_xlate[128];

#define kbd_setkeycode		pcikbd_setkeycode
#define kbd_getkeycode		pcikbd_getkeycode
#define kbd_pretranslate	pcikbd_pretranslate
#define kbd_translate		pcikbd_translate
#define kbd_unexpected_up	pcikbd_unexpected_up
#define kbd_leds		pcikbd_leds
#define kbd_init_hw		pcikbd_init_hw
#define kbd_sysrq_xlate		pcikbd_sysrq_xlate
#define kbd_init		pcikbd_init

#define SYSRQ_KEY 0x54

#endif /* __KERNEL__ */

#endif /* !(_SPARC64_KEYBOARD_H) */
