/*
 * BK Id: %F% %I% %G% %U% %#%
 */
/*
 * Common time routines among all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 * MPC8xx/MBX changes by Dan Malek (dmalek@jlc.net).
 *
 * First round of bugfixes by Gabriel Paubert (paubert@iram.es)
 * to make clock more stable (2.4.0-test5). The only thing
 * that this code assumes is that the timebases have been synchronized
 * by firmware on SMP and are never stopped (never do sleep
 * on SMP then, nap and doze are OK).
 *
 * TODO (not necessarily in this file):
 * - improve precision and reproducibility of timebase frequency
 * measurement at boot time.
 * - get rid of xtime_lock for gettimeofday (generic kernel problem
 * to be implemented on all architectures for SMP scalability and
 * eventually implementing gettimeofday without entering the kernel).
 * - put all time/clock related variables in a single structure
 * to minimize number of cache lines touched by gettimeofday()
 * - for astronomical applications: add a new function to get
 * non ambiguous timestamps even around leap seconds. This needs
 * a new timestamp format and a good name.
 *
 *
 * The following comment is partially obsolete (at least the long wait
 * is no more a valid reason):
 * Since the MPC8xx has a programmable interrupt timer, I decided to
 * use that rather than the decrementer.  Two reasons: 1.) the clock
 * frequency is low, causing 2.) a long wait in the timer interrupt
 *		while ((d = get_dec()) == dval)
 * loop.  The MPC8xx can be driven from a variety of input clocks,
 * so a number of assumptions have been made here because the kernel
 * parameter HZ is a constant.  We assume (correctly, today :-) that
 * the MPC8xx on the MBX board is driven from a 32.768 kHz crystal.
 * This is then divided by 4, providing a 8192 Hz clock into the PIT.
 * Since it is not possible to get a nice 100 Hz clock out of this, without
 * creating a software PLL, I have set HZ to 128.  -- Dan
 *
 * 1997-09-10  Updated NTP code according to technical memorandum Jan '96
 *             "A Kernel Model for Precision Timekeeping" by Dave Mills
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/timex.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/time.h>
#include <linux/init.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/processor.h>
#include <asm/nvram.h>
#include <asm/cache.h>
#include <asm/8xx_immap.h>
#include <asm/machdep.h>

#include <asm/time.h>

/* XXX false sharing with below? */
u64 jiffies_64;

unsigned long disarm_decr[NR_CPUS];

extern struct timezone sys_tz;

/* keep track of when we need to update the rtc */
time_t last_rtc_update;
extern rwlock_t xtime_lock;

/* The decrementer counts down by 128 every 128ns on a 601. */
#define DECREMENTER_COUNT_601	(1000000000 / HZ)

unsigned tb_ticks_per_jiffy;
unsigned tb_to_us;
unsigned tb_last_stamp;

extern unsigned long wall_jiffies;

#ifdef CONFIG_PPC_ISERIES
extern u64 get_tb64(void);
extern u64 next_jiffy_update_tb[];
#endif

static long time_offset;

spinlock_t rtc_lock = SPIN_LOCK_UNLOCKED;

EXPORT_SYMBOL(rtc_lock);

/* Timer interrupt helper function */
static inline int tb_delta(unsigned *jiffy_stamp) {
	int delta;
	if (__USE_RTC()) {
		delta = get_rtcl();
		if (delta < *jiffy_stamp) *jiffy_stamp -= 1000000000;
		delta -= *jiffy_stamp;
	} else {
		delta = get_tbl() - *jiffy_stamp;
	}
	return delta;
}

#ifndef CONFIG_PPC_ISERIES	/* iSeries version is in iSeries_time.c */

extern unsigned long prof_cpu_mask;
extern unsigned int * prof_buffer;
extern unsigned long prof_len;
extern unsigned long prof_shift;
extern char _stext;

static inline void ppc_do_profile (unsigned long nip)
{
	if (!prof_buffer)
		return;

	/*
	 * Only measure the CPUs specified by /proc/irq/prof_cpu_mask.
	 * (default is all CPUs.)
	 */
	if (!((1<<smp_processor_id()) & prof_cpu_mask))
		return;

	nip -= (unsigned long) &_stext;
	nip >>= prof_shift;
	/*
	 * Don't ignore out-of-bounds EIP values silently,
	 * put them into the last histogram slot, so if
	 * present, they will show up as a sharp peak.
	 */
	if (nip > prof_len-1)
		nip = prof_len-1;
	atomic_inc((atomic_t *)&prof_buffer[nip]);
}

/*
 * timer_interrupt - gets called when the decrementer overflows,
 * with interrupts disabled.
 * We set it up to overflow again in 1/HZ seconds.
 */
void timer_interrupt(struct pt_regs * regs)
{
	int next_dec;
	unsigned long cpu = smp_processor_id();
	unsigned jiffy_stamp = last_jiffy_stamp(cpu);
	extern void do_IRQ(struct pt_regs *);

	if (atomic_read(&ppc_n_lost_interrupts) != 0)
		do_IRQ(regs);

	irq_enter();
	
	while ((next_dec = tb_ticks_per_jiffy - tb_delta(&jiffy_stamp)) < 0) {
		jiffy_stamp += tb_ticks_per_jiffy;
		if (!user_mode(regs))
			ppc_do_profile(instruction_pointer(regs));
	  	if (smp_processor_id())
			continue;

		/* We are in an interrupt, no need to save/restore flags */
		write_lock(&xtime_lock);
		tb_last_stamp = jiffy_stamp;
		do_timer(regs);

		/*
		 * update the rtc when needed, this should be performed on the
		 * right fraction of a second. Half or full second ?
		 * Full second works on mk48t59 clocks, others need testing.
		 * Note that this update is basically only used through
		 * the adjtimex system calls. Setting the HW clock in
		 * any other way is a /dev/rtc and userland business.
		 * This is still wrong by -0.5/+1.5 jiffies because of the
		 * timer interrupt resolution and possible delay, but here we
		 * hit a quantization limit which can only be solved by higher
		 * resolution timers and decoupling time management from timer
		 * interrupts. This is also wrong on the clocks
		 * which require being written at the half second boundary.
		 * We should have an rtc call that only sets the minutes and
		 * seconds like on Intel to avoid problems with non UTC clocks.
		 */
		if ( ppc_md.set_rtc_time && (time_status & STA_UNSYNC) == 0 &&
		     xtime.tv_sec - last_rtc_update >= 659 &&
		     abs((xtime.tv_nsec / 1000) - (1000000-1000000/HZ)) < 500000/HZ &&
		     jiffies - wall_jiffies == 1) {
		  	if (ppc_md.set_rtc_time(xtime.tv_sec+1 + time_offset) == 0)
				last_rtc_update = xtime.tv_sec+1;
			else
				/* Try again one minute later */
				last_rtc_update += 60;
		}
		write_unlock(&xtime_lock);
	}
	if ( !disarm_decr[smp_processor_id()] )
		set_dec(next_dec);
	last_jiffy_stamp(cpu) = jiffy_stamp;

#ifdef CONFIG_SMP
	smp_local_timer_interrupt(regs);
#endif /* CONFIG_SMP */

	if (ppc_md.heartbeat && !ppc_md.heartbeat_count--)
		ppc_md.heartbeat();

	irq_exit();
}
#endif /* CONFIG_PPC_ISERIES */

/*
 * This version of gettimeofday has microsecond resolution.
 */
void do_gettimeofday(struct timeval *tv)
{
	unsigned long flags;
	unsigned delta, lost_ticks, usec, sec;

	read_lock_irqsave(&xtime_lock, flags);
	sec = xtime.tv_sec;
	usec = (xtime.tv_nsec / 1000);
#ifdef CONFIG_PPC_ISERIES
	delta = tb_ticks_per_jiffy - ( next_jiffy_update_tb[0] - get_tb64() );
#else
	delta = tb_ticks_since(tb_last_stamp);
#endif
#ifdef CONFIG_SMP
	/* As long as timebases are not in sync, gettimeofday can only
	 * have jiffy resolution on SMP.
	 */
	if (!smp_tb_synchronized)
		delta = 0;
#endif /* CONFIG_SMP */
	lost_ticks = jiffies - wall_jiffies;
	read_unlock_irqrestore(&xtime_lock, flags);

	usec += mulhwu(tb_to_us, tb_ticks_per_jiffy * lost_ticks + delta);
	while (usec >= 1000000) {
	  	sec++;
		usec -= 1000000;
	}
	tv->tv_sec = sec;
	tv->tv_usec = usec;
}

void do_settimeofday(struct timeval *tv)
{
	unsigned long flags;
	int tb_delta, new_usec, new_sec;

	write_lock_irqsave(&xtime_lock, flags);
	/* Updating the RTC is not the job of this code. If the time is
	 * stepped under NTP, the RTC will be update after STA_UNSYNC
	 * is cleared. Tool like clock/hwclock either copy the RTC
	 * to the system time, in which case there is no point in writing
	 * to the RTC again, or write to the RTC but then they don't call
	 * settimeofday to perform this operation. Note also that
	 * we don't touch the decrementer since:
	 * a) it would lose timer interrupt synchronization on SMP
	 * (if it is working one day)
	 * b) it could make one jiffy spuriously shorter or longer
	 * which would introduce another source of uncertainty potentially
	 * harmful to relatively short timers.
	 */

	/* This works perfectly on SMP only if the tb are in sync but 
	 * guarantees an error < 1 jiffy even if they are off by eons,
	 * still reasonable when gettimeofday resolution is 1 jiffy.
	 */
	tb_delta = tb_ticks_since(last_jiffy_stamp(smp_processor_id()));
	tb_delta += (jiffies - wall_jiffies) * tb_ticks_per_jiffy;
	new_sec = tv->tv_sec;
	new_usec = tv->tv_usec - mulhwu(tb_to_us, tb_delta);
	while (new_usec <0) {
		new_sec--; 
		new_usec += 1000000;
	}
	xtime.tv_nsec = (new_usec * 1000);
	xtime.tv_sec = new_sec;

	/* In case of a large backwards jump in time with NTP, we want the 
	 * clock to be updated as soon as the PLL is again in lock.
	 */
	last_rtc_update = new_sec - 658;

	time_adjust = 0;                /* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_state = TIME_ERROR;        /* p. 24, (a) */
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	write_unlock_irqrestore(&xtime_lock, flags);
}


void __init time_init(void)
{
	time_t sec, old_sec;
	unsigned old_stamp, stamp, elapsed;
	/* This function is only called on the boot processor */
	unsigned long flags;

        if (ppc_md.time_init != NULL)
                time_offset = ppc_md.time_init();

	if (__USE_RTC()) {
		/* 601 processor: dec counts down by 128 every 128ns */
		tb_ticks_per_jiffy = DECREMENTER_COUNT_601;
		/* mulhwu_scale_factor(1000000000, 1000000) is 0x418937 */
		tb_to_us = 0x418937;
        } else {
                ppc_md.calibrate_decr();
	}

	/* Now that the decrementer is calibrated, it can be used in case the 
	 * clock is stuck, but the fact that we have to handle the 601
	 * makes things more complex. Repeatedly read the RTC until the
	 * next second boundary to try to achieve some precision...
	 */
	if (ppc_md.get_rtc_time) {
		stamp = get_native_tbl();
		sec = ppc_md.get_rtc_time();
		elapsed = 0;
		do {
			old_stamp = stamp; 
			old_sec = sec;
			stamp = get_native_tbl();
			if (__USE_RTC() && stamp < old_stamp) old_stamp -= 1000000000;
			elapsed += stamp - old_stamp;
			sec = ppc_md.get_rtc_time();
		} while ( sec == old_sec && elapsed < 2*HZ*tb_ticks_per_jiffy);
		if (sec==old_sec) {
			printk("Warning: real time clock seems stuck!\n");
		}
		write_lock_irqsave(&xtime_lock, flags);
		xtime.tv_sec = sec;
		last_jiffy_stamp(0) = tb_last_stamp = stamp;
		xtime.tv_nsec = 0;
		/* No update now, we just read the time from the RTC ! */
		last_rtc_update = xtime.tv_sec;
		write_unlock_irqrestore(&xtime_lock, flags);
	}

	/* Not exact, but the timer interrupt takes care of this */
	set_dec(tb_ticks_per_jiffy);

	/* If platform provided a timezone (pmac), we correct the time */
        if (time_offset) {
		sys_tz.tz_minuteswest = -time_offset / 60;
		sys_tz.tz_dsttime = 0;
		xtime.tv_sec -= time_offset;
        }
}

#define FEBRUARY		2
#define	STARTOFTIME		1970
#define SECDAY			86400L
#define SECYR			(SECDAY * 365)

/*
 * Note: this is wrong for 2100, but our signed 32-bit time_t will
 * have overflowed long before that, so who cares.  -- paulus
 */
#define	leapyear(year)		((year) % 4 == 0)
#define	days_in_year(a) 	(leapyear(a) ? 366 : 365)
#define	days_in_month(a) 	(month_days[(a) - 1])

static int month_days[12] = {
	31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

void to_tm(int tim, struct rtc_time * tm)
{
	register int i;
	register long hms, day, gday;

	gday = day = tim / SECDAY;
	hms = tim % SECDAY;

	/* Hours, minutes, seconds are easy */
	tm->tm_hour = hms / 3600;
	tm->tm_min = (hms % 3600) / 60;
	tm->tm_sec = (hms % 3600) % 60;

	/* Number of years in days */
	for (i = STARTOFTIME; day >= days_in_year(i); i++)
		day -= days_in_year(i);
	tm->tm_year = i;

	/* Number of months in days left */
	if (leapyear(tm->tm_year))
		days_in_month(FEBRUARY) = 29;
	for (i = 1; day >= days_in_month(i); i++)
		day -= days_in_month(i);
	days_in_month(FEBRUARY) = 28;
	tm->tm_mon = i;

	/* Days are what is left over (+1) from all that. */
	tm->tm_mday = day + 1;

	/*
	 * Determine the day of week. Jan. 1, 1970 was a Thursday.
	 */
	tm->tm_wday = (gday + 4) % 7;
}

/* Auxiliary function to compute scaling factors */
/* Actually the choice of a timebase running at 1/4 the of the bus
 * frequency giving resolution of a few tens of nanoseconds is quite nice.
 * It makes this computation very precise (27-28 bits typically) which
 * is optimistic considering the stability of most processor clock
 * oscillators and the precision with which the timebase frequency
 * is measured but does not harm.
 */
unsigned mulhwu_scale_factor(unsigned inscale, unsigned outscale) {
	unsigned mlt=0, tmp, err;
	/* No concern for performance, it's done once: use a stupid
	 * but safe and compact method to find the multiplier.
	 */
	for (tmp = 1U<<31; tmp != 0; tmp >>= 1) {
		if (mulhwu(inscale, mlt|tmp) < outscale) mlt|=tmp;
	}
	/* We might still be off by 1 for the best approximation.
	 * A side effect of this is that if outscale is too large
	 * the returned value will be zero.
	 * Many corner cases have been checked and seem to work,
	 * some might have been forgotten in the test however.
	 */
	err = inscale*(mlt+1);
	if (err <= inscale/2) mlt++;
	return mlt;
}

