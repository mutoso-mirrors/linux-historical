/*
 *  include/asm-i386/mach-default/mach_timer.h
 *
 *  Machine specific calibrate_tsc() for generic.
 *  Split out from timer_tsc.c by Osamu Tomita <tomita@cinet.co.jp>
 */
/* ------ Calibrate the TSC ------- 
 * Return 2^32 * (1 / (TSC clocks per usec)) for do_fast_gettimeoffset().
 * Too much 64-bit arithmetic here to do this cleanly in C, and for
 * accuracy's sake we want to keep the overhead on the CTC speaker (channel 2)
 * output busy loop as low as possible. We avoid reading the CTC registers
 * directly because of the awkward 8-bit access mechanism of the 82C54
 * device.
 */
#ifndef _MACH_TIMER_H
#define _MACH_TIMER_H

#define CALIBRATE_LATCH	(5 * LATCH)

static inline void mach_prepare_counter(void)
{
       /* Set the Gate high, disable speaker */
	outb((inb(0x61) & ~0x02) | 0x01, 0x61);

	/*
	 * Now let's take care of CTC channel 2
	 *
	 * Set the Gate high, program CTC channel 2 for mode 0,
	 * (interrupt on terminal count mode), binary count,
	 * load 5 * LATCH count, (LSB and MSB) to begin countdown.
	 *
	 * Some devices need a delay here.
	 */
	outb(0xb0, 0x43);			/* binary, mode 0, LSB/MSB, Ch 2 */
	outb_p(CALIBRATE_LATCH & 0xff, 0x42);	/* LSB of count */
	outb_p(CALIBRATE_LATCH >> 8, 0x42);       /* MSB of count */
}

static inline void mach_countup(unsigned long *count)
{
	*count = 0L;
	do {
		*count++;
	} while ((inb_p(0x61) & 0x20) == 0);
}

#endif /* !_MACH_TIMER_H */