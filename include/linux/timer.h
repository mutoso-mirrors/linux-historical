#ifndef _LINUX_TIMER_H
#define _LINUX_TIMER_H

/*
 * DON'T CHANGE THESE!! Most of them are hardcoded into some assembly language
 * as well as being defined here.
 */

/*
 * The timers are:
 *
 * BLANK_TIMER		console screen-saver timer
 *
 * BEEP_TIMER		console beep timer
 *
 * RS_TIMER		timer for the RS-232 ports
 * 
 * HD_TIMER		harddisk timer
 *
 * HD_TIMER2		(atdisk2 patches)
 *
 * FLOPPY_TIMER		floppy disk timer (not used right now)
 * 
 * SCSI_TIMER		scsi.c timeout timer
 *
 * NET_TIMER		tcp/ip timeout timer
 *
 * COPRO_TIMER		387 timeout for buggy hardware..
 *
 * QIC02_TAPE_TIMER	timer for QIC-02 tape driver (it's not hardcoded)
 *
 * MCD_TIMER		Mitsumi CD-ROM Timer
 *
 * GSCD_TIMER		Goldstar CD-ROM Timer
 *
 * OPTCD_TIMER		Optics Storage CD-ROM Timer
 *
 */

#define BLANK_TIMER	0
#define BEEP_TIMER	1
#define RS_TIMER	2

#define HD_TIMER	16
#define FLOPPY_TIMER	17
#define SCSI_TIMER 	18
#define NET_TIMER	19
#define SOUND_TIMER	20
#define COPRO_TIMER	21

#define QIC02_TAPE_TIMER	22	/* hhb */
#define MCD_TIMER	23

#define HD_TIMER2	24
#define GSCD_TIMER	25
#define OPTCD_TIMER	26

struct timer_struct {
	unsigned long expires;
	void (*fn)(void);
};

extern unsigned long timer_active;
extern struct timer_struct timer_table[32];

/*
 * This is completely separate from the above, and is the
 * "new and improved" way of handling timers more dynamically.
 * Hopefully efficient and general enough for most things.
 *
 * The "hardcoded" timers above are still useful for well-
 * defined problems, but the timer-list is probably better
 * when you need multiple outstanding timers or similar.
 *
 * The "data" field is in case you want to use the same
 * timeout function for several timeouts. You can use this
 * to distinguish between the different invocations.
 */
struct timer_list {
	struct timer_list *next;
	struct timer_list *prev;
	unsigned long expires;
	unsigned long data;
	void (*function)(unsigned long);
};

extern void add_timer(struct timer_list * timer);
extern int  del_timer(struct timer_list * timer);

extern inline void init_timer(struct timer_list * timer)
{
	timer->next = NULL;
	timer->prev = NULL;
}

#endif
