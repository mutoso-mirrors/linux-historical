/*
 *  linux/include/asm-ppc/keyboard.h
 *
 *  Created 3 Nov 1996 by Geert Uytterhoeven
 *  Modified for Power Macintosh by Paul Mackerras
 */

/*
 * This file contains the ppc architecture specific keyboard definitions -
 * like the intel pc for prep systems, different for power macs.
 */

#ifndef __ASMPPC_KEYBOARD_H
#define __ASMPPC_KEYBOARD_H

#ifdef __KERNEL__

#include <linux/config.h>
#include <asm/adb.h>
#include <asm/machdep.h>
#ifdef CONFIG_APUS
#include <asm-m68k/keyboard.h>
#else

#define KEYBOARD_IRQ			1
#define DISABLE_KBD_DURING_INTERRUPTS	0
#define INIT_KBD

static inline int kbd_setkeycode(unsigned int scancode, unsigned int keycode)
{
	if ( ppc_md.kbd_setkeycode )
		return ppc_md.kbd_setkeycode(scancode, keycode);
	else
		return 0;
}
  
static inline int kbd_getkeycode(unsigned int scancode)
{
	if ( ppc_md.kbd_getkeycode )
		return ppc_md.kbd_getkeycode(scancode);
	else
		return 0;
}
  
static inline int kbd_translate(unsigned char keycode, unsigned char *keycodep,
				char raw_mode)
{
	if ( ppc_md.kbd_translate )
		return ppc_md.kbd_translate(keycode, keycodep, raw_mode);
	else
		return 0;
}
  
static inline int kbd_unexpected_up(unsigned char keycode)
{
	if ( ppc_md.kbd_unexpected_up )
		return ppc_md.kbd_unexpected_up(keycode);
	else
		return 0;
}
  
static inline void kbd_leds(unsigned char leds)
{
	if ( ppc_md.kbd_leds )
		ppc_md.kbd_leds(leds);
}
  
static inline void kbd_init_hw(void)
{
	if ( ppc_md.kbd_init_hw )
		ppc_md.kbd_init_hw();
}

#define kbd_sysrq_xlate	(ppc_md.ppc_kbd_sysrq_xlate)

extern unsigned long SYSRQ_KEY;

#endif /* CONFIG_APUS */

#endif /* __KERNEL__ */

#endif /* __ASMPPC_KEYBOARD_H */
