/*
 *  linux/arch/arm/kernel/time.c
 *
 *  Copyright (C) 1991, 1992, 1995  Linus Torvalds
 *  Modifications for ARM (C) 1994, 1995, 1996,1997 Russell King
 *
 * This file contains the ARM-specific time handling details:
 * reading the RTC at bootup, etc...
 *
 * 1994-07-02  Alan Modra
 *             fixed set_rtc_mmss, fixed time.year for >= 2000, new mktime
 * 1998-12-20  Updated NTP code according to technical memorandum Jan '96
 *             "A Kernel Model for Precision Timekeeping" by Dave Mills
 */
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/timex.h>
#include <asm/hardware.h>

extern int setup_arm_irq(int, struct irqaction *);
extern void setup_timer(void);
extern rwlock_t xtime_lock;
extern unsigned long wall_jiffies;

/* change this if you have some constant time drift */
#define USECS_PER_JIFFY	(1000000/HZ)

#ifndef BCD_TO_BIN
#define BCD_TO_BIN(val) ((val)=((val)&15) + ((val)>>4)*10)
#endif

#ifndef BIN_TO_BCD
#define BIN_TO_BCD(val) ((val)=(((val)/10)<<4) + (val)%10)
#endif

static int dummy_set_rtc(void)
{
	return 0;
}

/*
 * hook for setting the RTC's idea of the current time.
 */
int (*set_rtc)(void) = dummy_set_rtc;

static unsigned long dummy_gettimeoffset(void)
{
	return 0;
}

/*
 * hook for getting the time offset.  Note that it is
 * always called with interrupts disabled.
 */
unsigned long (*gettimeoffset)(void) = dummy_gettimeoffset;

/* Converts Gregorian date to seconds since 1970-01-01 00:00:00.
 * Assumes input in normal date format, i.e. 1980-12-31 23:59:59
 * => year=1980, mon=12, day=31, hour=23, min=59, sec=59.
 *
 * [For the Julian calendar (which was used in Russia before 1917,
 * Britain & colonies before 1752, anywhere else before 1582,
 * and is still in use by some communities) leave out the
 * -year/100+year/400 terms, and add 10.]
 *
 * This algorithm was first published by Gauss (I think).
 *
 * WARNING: this function will overflow on 2106-02-07 06:28:16 on
 * machines were long is 32-bit! (However, as time_t is signed, we
 * will already get problems at other places on 2038-01-19 03:14:08)
 */
unsigned long
mktime(unsigned int year, unsigned int mon, unsigned int day,
       unsigned int hour, unsigned int min, unsigned int sec)
{
	if (0 >= (int) (mon -= 2)) {	/* 1..12 -> 11,12,1..10 */
		mon += 12;	/* Puts Feb last since it has leap day */
		year -= 1;
	}
	return (((
		    (unsigned long)(year/4 - year/100 + year/400 + 367*mon/12 + day) +
		      year*365 - 719499
		    )*24 + hour /* now have hours */
		   )*60 + min /* now have minutes */
		  )*60 + sec; /* finally seconds */
}

/*
 * Handle kernel profile stuff...
 */
static inline void do_profile(struct pt_regs *regs)
{
	if (!user_mode(regs) &&
	    prof_buffer &&
	    current->pid) {
		unsigned long pc = instruction_pointer(regs);
		extern int _stext;

		pc -= (unsigned long)&_stext;

		pc >>= prof_shift;

		if (pc >= prof_len)
			pc = prof_len - 1;

		prof_buffer[pc] += 1;
	}
}

static long next_rtc_update;

/*
 * If we have an externally synchronized linux clock, then update
 * CMOS clock accordingly every ~11 minutes.  set_rtc() has to be
 * called as close as possible to 500 ms before the new second
 * starts.
 */
static inline void do_set_rtc(void)
{
	if (time_status & STA_UNSYNC || set_rtc == NULL)
		return;

	if (next_rtc_update &&
	    time_before(xtime.tv_sec, next_rtc_update))
		return;

	if (xtime.tv_usec < 50000 - (tick >> 1) &&
	    xtime.tv_usec >= 50000 + (tick >> 1))
		return;

	if (set_rtc())
		/*
		 * rtc update failed.  Try again in 60s
		 */
		next_rtc_update = xtime.tv_sec + 60;
	else
		next_rtc_update = xtime.tv_sec + 660;
}

#ifdef CONFIG_LEDS

#include <asm/leds.h>

static void do_leds(void)
{
#ifdef CONFIG_LEDS_CPU
	{
		static int last_pid;

		if (current->pid != last_pid) {
			last_pid = current->pid;
			if (last_pid)
				leds_event(led_idle_end);
			else
				leds_event(led_idle_start);
		}
	}
#endif
#ifdef CONFIG_LEDS_TIMER
	{
		static unsigned int count = 50;

		if (--count == 0) {
			count = 50;
			leds_event(led_timer);
		}
	}
#endif
}
#else
#define do_leds()
#endif

void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned long usec, sec;

	read_lock_irqsave(&xtime_lock, flags);
	usec = gettimeoffset();
	{
		unsigned long lost = jiffies - wall_jiffies;

		if (lost)
			usec += lost * USECS_PER_JIFFY;
	}
	sec = xtime.tv_sec;
	usec += xtime.tv_usec;
	read_unlock_irqrestore(&xtime_lock, flags);

	/* usec may have gone up a lot: be safe */
	while (usec >= 1000000) {
		usec -= 1000000;
		sec++;
	}

	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

void do_settimeofday(struct timeval *tv)
{
	write_lock_irq(&xtime_lock);
	/* This is revolting. We need to set the xtime.tv_usec
	 * correctly. However, the value in this location is
	 * is value at the last tick.
	 * Discover what correction gettimeofday
	 * would have done, and then undo it!
	 */
	tv->tv_usec -= gettimeoffset();
	tv->tv_usec -= (jiffies - wall_jiffies) * USECS_PER_JIFFY;

	while (tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}

	xtime = *tv;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_unlock_irq(&xtime_lock);
}

static struct irqaction timer_irq = {
	NULL, 0, 0, "timer", NULL, NULL
};

/*
 * Include architecture specific code
 */
#include <asm/arch/time.h>

/*
 * This must cause the timer to start ticking.
 * It doesn't have to set the current time though
 * from an RTC - it can be done later once we have
 * some buses initialised.
 */
void __init time_init(void)
{
	xtime.tv_usec = 0;
	xtime.tv_sec  = 0;

	setup_timer();
}
