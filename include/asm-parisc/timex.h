/*
 * linux/include/asm-parisc/timex.h
 *
 * PARISC architecture timex specifications
 */
#ifndef _ASMPARISC_TIMEX_H
#define _ASMPARISC_TIMEX_H

#include <asm/system.h>
#include <linux/time.h>

#define CLOCK_TICK_RATE	1193180 /* Underlying HZ */

typedef unsigned long cycles_t;

extern cycles_t cacheflush_time;

static inline cycles_t get_cycles (void)
{
	return mfctl(16);
}

#endif
