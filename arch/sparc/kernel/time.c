/* $Id: time.c,v 1.27 1997/04/14 05:38:31 davem Exp $
 * linux/arch/sparc/kernel/time.c
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 *
 * This file handles the Sparc specific time handling details.
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/init.h>

#include <asm/oplib.h>
#include <asm/segment.h>
#include <asm/timer.h>
#include <asm/mostek.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>

enum sparc_clock_type sp_clock_typ;
struct mostek48t02 *mstk48t02_regs = 0;
struct mostek48t08 *mstk48t08_regs = 0;
static int set_rtc_mmss(unsigned long);

__volatile__ unsigned int *master_l10_counter;
__volatile__ unsigned int *master_l10_limit;

/*
 * timer_interrupt() needs to keep up the real-time clock,
 * as well as call the "do_timer()" routine every clocktick
 */
void timer_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	/* last time the cmos clock got updated */
	static long last_rtc_update=0;

	clear_clock_irq();

	do_timer(regs);

	/* Determine when to update the Mostek clock. */
	if (time_state != TIME_BAD && xtime.tv_sec > last_rtc_update + 660 &&
	    xtime.tv_usec > 500000 - (tick >> 1) &&
	    xtime.tv_usec < 500000 + (tick >> 1))
	  if (set_rtc_mmss(xtime.tv_sec) == 0)
	    last_rtc_update = xtime.tv_sec;
	  else
	    last_rtc_update = xtime.tv_sec - 600; /* do it again in 60 s */

#ifdef __SMP__
	/* I really think it should not be done this way... -DaveM */
	smp_message_pass(MSG_ALL_BUT_SELF, MSG_RESCHEDULE, 0L, 0);
#endif
}

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
static inline unsigned long mktime(unsigned int year, unsigned int mon,
	unsigned int day, unsigned int hour,
	unsigned int min, unsigned int sec)
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

/* Kick start a stopped clock (procedure from the Sun NVRAM/hostid FAQ). */
static void kick_start_clock(void)
{
	register struct mostek48t02 *regs = mstk48t02_regs;
	unsigned char sec;
	int i, count;

	prom_printf("CLOCK: Clock was stopped. Kick start ");

	/* Turn on the kick start bit to start the oscillator. */
	regs->creg |= MSTK_CREG_WRITE;
	regs->sec &= ~MSTK_STOP;
	regs->hour |= MSTK_KICK_START;
	regs->creg &= ~MSTK_CREG_WRITE;

	/* Delay to allow the clock oscillator to start. */
	sec = MSTK_REG_SEC(regs);
	for (i = 0; i < 3; i++) {
		while (sec == MSTK_REG_SEC(regs))
			for (count = 0; count < 100000; count++)
				/* nothing */ ;
		prom_printf(".");
		sec = regs->sec;
	}
	prom_printf("\n");

	/* Turn off kick start and set a "valid" time and date. */
	regs->creg |= MSTK_CREG_WRITE;
	regs->hour &= ~MSTK_KICK_START;
	MSTK_SET_REG_SEC(regs,0);
	MSTK_SET_REG_MIN(regs,0);
	MSTK_SET_REG_HOUR(regs,0);
	MSTK_SET_REG_DOW(regs,5);
	MSTK_SET_REG_DOM(regs,1);
	MSTK_SET_REG_MONTH(regs,8);
	MSTK_SET_REG_YEAR(regs,1996 - MSTK_YEAR_ZERO);
	regs->creg &= ~MSTK_CREG_WRITE;

	/* Ensure the kick start bit is off. If it isn't, turn it off. */
	while (regs->hour & MSTK_KICK_START) {
		prom_printf("CLOCK: Kick start still on!\n");
		regs->creg |= MSTK_CREG_WRITE;
		regs->hour &= ~MSTK_KICK_START;
		regs->creg &= ~MSTK_CREG_WRITE;
	}

	prom_printf("CLOCK: Kick start procedure successful.\n");
}

/* Return nonzero if the clock chip battery is low. */
static int has_low_battery(void)
{
	register struct mostek48t02 *regs = mstk48t02_regs;
	unsigned char data1, data2;

	data1 = regs->eeprom[0];	/* Read some data. */
	regs->eeprom[0] = ~data1;	/* Write back the complement. */
	data2 = regs->eeprom[0];	/* Read back the complement. */
	regs->eeprom[0] = data1;	/* Restore the original value. */

	return (data1 == data2);	/* Was the write blocked? */
}

/* Probe for the real time clock chip. */
__initfunc(static void clock_probe(void))
{
	struct linux_prom_registers clk_reg[2];
	char model[128];
	register int node, cpuunit, bootbus;

	cpuunit = bootbus = 0;

	/* Determine the correct starting PROM node for the probe. */
	node = prom_getchild(prom_root_node);
	switch (sparc_cpu_model) {
	case sun4c:
		break;
	case sun4m:
		node = prom_getchild(prom_searchsiblings(node, "obio"));
		break;
	case sun4d:
		node = prom_getchild(bootbus = prom_searchsiblings(prom_getchild(cpuunit = prom_searchsiblings(node, "cpu-unit")), "bootbus"));
		break;
	default:
		prom_printf("CLOCK: Unsupported architecture!\n");
		prom_halt();
	}

	/* Find the PROM node describing the real time clock. */
	sp_clock_typ = MSTK_INVALID;
	node = prom_searchsiblings(node,"eeprom");
	if (!node) {
		prom_printf("CLOCK: No clock found!\n");
		prom_halt();
	}

	/* Get the model name and setup everything up. */
	model[0] = '\0';
	prom_getstring(node, "model", model, sizeof(model));
	if (strcmp(model, "mk48t02") == 0) {
		sp_clock_typ = MSTK48T02;
		if (prom_getproperty(node, "reg", (char *) clk_reg, sizeof(clk_reg)) == -1) {
			prom_printf("clock_probe: FAILED!\n");
			prom_halt();
		}
		if (sparc_cpu_model == sun4d)
			prom_apply_generic_ranges (bootbus, cpuunit, clk_reg, 1);
		else
			prom_apply_obio_ranges(clk_reg, 1);
		/* Map the clock register io area read-only */
		mstk48t02_regs = (struct mostek48t02 *) 
			sparc_alloc_io(clk_reg[0].phys_addr,
				       (void *) 0, sizeof(*mstk48t02_regs),
				       "clock", clk_reg[0].which_io, 0x0);
		mstk48t08_regs = 0;  /* To catch weirdness */
	} else if (strcmp(model, "mk48t08") == 0) {
		sp_clock_typ = MSTK48T08;
		if(prom_getproperty(node, "reg", (char *) clk_reg,
				    sizeof(clk_reg)) == -1) {
			prom_printf("clock_probe: FAILED!\n");
			prom_halt();
		}
		if (sparc_cpu_model == sun4d)
			prom_apply_generic_ranges (bootbus, cpuunit, clk_reg, 1);
		else
			prom_apply_obio_ranges(clk_reg, 1);
		/* Map the clock register io area read-only */
		mstk48t08_regs = (struct mostek48t08 *)
			sparc_alloc_io(clk_reg[0].phys_addr,
				       (void *) 0, sizeof(*mstk48t08_regs),
				       "clock", clk_reg[0].which_io, 0x0);

		mstk48t02_regs = &mstk48t08_regs->regs;
	} else {
		prom_printf("CLOCK: Unknown model name '%s'\n",model);
		prom_halt();
	}

	/* Report a low battery voltage condition. */
	if (has_low_battery())
		printk(KERN_CRIT "NVRAM: Low battery voltage!\n");

	/* Kick start the clock if it is completely stopped. */
	if (mstk48t02_regs->sec & MSTK_STOP)
		kick_start_clock();
}

__initfunc(void time_init(void))
{
	unsigned int year, mon, day, hour, min, sec;
	struct mostek48t02 *mregs;

	do_get_fast_time = do_gettimeofday;

#if CONFIG_AP1000
	init_timers(timer_interrupt);
	ap_init_time(&xtime);
        return;
#endif

	clock_probe();
	init_timers(timer_interrupt);

	mregs = mstk48t02_regs;
	if(!mregs) {
		prom_printf("Something wrong, clock regs not mapped yet.\n");
		prom_halt();
	}		
	mregs->creg |= MSTK_CREG_READ;
	sec = MSTK_REG_SEC(mregs);
	min = MSTK_REG_MIN(mregs);
	hour = MSTK_REG_HOUR(mregs);
	day = MSTK_REG_DOM(mregs);
	mon = MSTK_REG_MONTH(mregs);
	year = MSTK_CVT_YEAR( MSTK_REG_YEAR(mregs) );
	xtime.tv_sec = mktime(year, mon, day, hour, min, sec);
	xtime.tv_usec = 0;
	mregs->creg &= ~MSTK_CREG_READ;

	/* Now that OBP ticker has been silenced, it is safe to enable IRQ. */
	__sti();
}

static __inline__ unsigned long do_gettimeoffset(void)
{
	unsigned long offset = 0;
	unsigned int count;

	count = (*master_l10_counter >> 10) & 0x1fffff;

	if(test_bit(TIMER_BH, &bh_active))
		offset = 1000000;

	return offset + count;
}

void do_gettimeofday(struct timeval *tv)
{
#if CONFIG_AP1000
	unsigned long flags;

	save_and_cli(flags);
	ap_gettimeofday(&xtime);
	*tv = xtime;
	restore_flags(flags);
#else /* !(CONFIG_AP1000) */
	/* Load doubles must be used on xtime so that what we get
	 * is guarenteed to be atomic, this is why we can run this
	 * with interrupts on full blast.  Don't touch this... -DaveM
	 */
	__asm__ __volatile__("
	sethi	%hi(master_l10_counter), %o1
	ld	[%o1 + %lo(master_l10_counter)], %g3
	sethi	%hi(xtime), %g2
1:	ldd	[%g2 + %lo(xtime)], %o4
	ld	[%g3], %o1
	ldd	[%g2 + %lo(xtime)], %o2
	xor	%o4, %o2, %o2
	xor	%o5, %o3, %o3
	orcc	%o2, %o3, %g0
	bne	1b
	 subcc	%o1, 0x0, %g0
	bpos	1f
	 srl	%o1, 0xa, %o1
	sethi	%hi(0x2710), %o3
	or	%o3, %lo(0x2710), %o3
	sethi	%hi(0x1fffff), %o2
	or	%o2, %lo(0x1fffff), %o2
	add	%o5, %o3, %o5
	and	%o1, %o2, %o1
1:	add	%o5, %o1, %o5
	sethi	%hi(1000000), %o2
	or	%o2, %lo(1000000), %o2
	cmp	%o5, %o2
	bl,a	1f
	 st	%o4, [%o0 + 0x0]
	add	%o4, 0x1, %o4
	sub	%o5, %o2, %o5
	st	%o4, [%o0 + 0x0]
1:	st	%o5, [%o0 + 0x4]");
#endif
}

void do_settimeofday(struct timeval *tv)
{
	cli();
#if !CONFIG_AP1000
	tv->tv_usec -= do_gettimeoffset();
	if(tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}
#endif
	xtime = *tv;
	time_state = TIME_BAD;
	time_maxerror = 0x70000000;
	time_esterror = 0x70000000;
	sti();
}

static int set_rtc_mmss(unsigned long nowtime)
{
	int real_seconds, real_minutes, mostek_minutes;
	struct mostek48t02 *regs = mstk48t02_regs;

	/* Not having a register set can lead to trouble. */
	if (!regs) 
		return -1;

	/* Read the current RTC minutes. */
	regs->creg |= MSTK_CREG_READ;
	mostek_minutes = MSTK_REG_MIN(regs);
	regs->creg &= ~MSTK_CREG_READ;

	/*
	 * since we're only adjusting minutes and seconds,
	 * don't interfere with hour overflow. This avoids
	 * messing with unknown time zones but requires your
	 * RTC not to be off by more than 15 minutes
	 */
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;
	if (((abs(real_minutes - mostek_minutes) + 15)/30) & 1)
		real_minutes += 30;	/* correct for half hour time zone */
	real_minutes %= 60;

	if (abs(real_minutes - mostek_minutes) < 30) {
		regs->creg |= MSTK_CREG_WRITE;
		MSTK_SET_REG_SEC(regs,real_seconds);
		MSTK_SET_REG_MIN(regs,real_minutes);
		regs->creg &= ~MSTK_CREG_WRITE;
	} else
		return -1;

	return 0;
}
