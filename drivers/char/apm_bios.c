/*
 * APM BIOS driver for Linux
 * Copyright 1994, 1995 Stephen Rothwell (Stephen.Rothwell@pd.necisa.oz.au)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * $Id: apm_bios.c,v 0.22 1995/03/09 14:12:02 sfr Exp $
 *
 * October 1995, Rik Faith (faith@cs.unc.edu):
 *    Minor enhancements and updates (to the patch set) for 1.3.x
 *
 * Reference:
 *
 *   Intel Corporation, Microsoft Corporation. Advanced Power Management
 *   (APM) BIOS Interface Specification, Revision 1.1, September 1993.
 *   Intel Order Number 241704-001.  Microsoft Part Number 781-110-X01.
 *
 * [This document is available free from Intel by calling 800.628.8686 (fax
 * 916.356.6100) or 800.548.4725.  It is also available from Microsoft by
 * calling 206.882.8080; and is ftpable from
 * ftp://ftp.intel.com/pub/IAL/software_specs/apmv11.doc]
 *
 */

#include <linux/config.h>
#include <linux/module.h>

#include <asm/system.h>
#include <asm/segment.h>

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/timer.h>
#include <linux/fcntl.h>
#include <linux/malloc.h>
#include <linux/linkage.h>
#include <linux/apm_bios.h>

static struct symbol_table	apm_syms = {
#include <linux/symtab_begin.h>
	X(apm_register_callback),
	X(apm_unregister_callback),
#include <linux/symtab_end.h>
};

extern unsigned long get_cmos_time(void);

/* Configurable options:
 *  
 * CONFIG_APM_IGNORE_USER_SUSPEND: define to ignore USER SUSPEND requests.
 * This is necessary on the NEC Versa M series, which generates these when
 * resuming from SYSTEM SUSPEND.  However, enabling this on other laptops
 * will cause the laptop to generate a CRITICAL SUSPEND when an appropriate
 * USER SUSPEND is ignored -- this may prevent the APM driver from updating
 * the system time on a RESUME.
 *
 * CONFIG_APM_DO_ENABLE: enable APM features at boot time.  From page 36 of
 * the specification: "When disabled, the APM BIOS does not automatically
 * power manage devices, enter the Standby State, enter the Suspend State,
 * or take power saving steps in response to CPU Idle calls."  This driver
 * will make CPU Idle calls when Linux is idle (unless this feature is
 * turned off -- see below).  This should always save battery power, but
 * more complicated APM features will be dependent on your BIOS
 * implementation.  You may need to turn this option off if your computer
 * hangs at boot time when using APM support, or if it beeps continuously
 * instead of suspending.  Turn this off if you have a NEC UltraLite Versa
 * 33/C or a Toshiba T400CDT.  This is off by default since most machines
 * do fine without this feature.
 *
 * CONFIG_APM_CPU_IDLE: enable calls to APM CPU Idle/CPU Busy inside the
 * idle loop.  On some machines, this can activate improved power savings,
 * such as a slowed CPU clock rate, when the machine is idle.  These idle
 * call is made after the idle loop has run for some length of time (e.g.,
 * 333 mS).  On some machines, this will cause a hang at boot time or
 * whenever the CPU becomes idle.
 *
 * CONFIG_APM_DISPLAY_BLANK: enable console blanking using the APM.  Some
 * laptops can use this to turn of the LCD backlight when the VC screen
 * blanker blanks the screen.  Note that this is only used by the VC screen
 * blanker, and probably won't turn off the backlight when using X11.
 *
 * If you are debugging the APM support for your laptop, note that code for
 * all of these options is contained in this file, so you can #define or
 * #undef these on the next line to avoid recompiling the whole kernel.
 *
 */

/* KNOWN PROBLEM MACHINES:
 *
 * U: TI 4000M TravelMate: BIOS is *NOT* APM compliant
 *                         [Confirmed by TI representative]
 * U: ACER 486DX4/75: uses dseg 0040, in violation of APM specification
 *                    [Confirmed by BIOS disassembly]
 * P: Toshiba 1950S: battery life information only gets updated after resume
 *
 * Legend: U = unusable with APM patches
 *         P = partially usable with APM patches
 */

/*
 * define to have debug messages
 */
#undef APM_DEBUG

/*
 * define to always call the APM BIOS busy routine even if the clock was
 * not slowed by the idle routine
 */
#define ALWAYS_CALL_BUSY

/*
 * define to disable interrupts in APM BIOS calls (the CPU Idle BIOS call
 * should turn interrupts on before it does a 'hlt')
 */
#define APM_NOINTS

/*
 * define to make the APM BIOS calls zero all data segment registers (do
 * that an incorrect BIOS implementation will cause a kernel panic if it
 * tries to write to arbitrary memory)
 */
#define APM_ZERO_SEGS

/*
 * Need to poll the APM BIOS every second
 */
#define APM_CHECK_TIMEOUT	(HZ)

/*
 * These are the actual BIOS calls in assembler.  Depending on
 * APM_ZERO_SEGS and APM_NOINTS, we are being really paranoid here!  Not
 * only are interrupts disabled, but all the segment registers (except SS)
 * are saved and zeroed this means that if the BIOS tries to reference any
 * data without explicitly loading the segment registers, the kernel will
 * fault immediately rather than have some unforeseen circumstances for the
 * rest of the kernel.  And it will be very obvious!  :-) Doing this
 * depends on CS referring to the same physical memory as DS so that DS can
 * be zeroed before the call. Unfortunately, we can't do anything about the
 * stack segment/pointer.  Also, we tell the compiler that everything could
 * change.
 */
#ifdef APM_NOINTS
#	define APM_DO_CLI	"cli\n\t"
#else
#	define APM_DO_CLI
#endif
#ifdef APM_ZERO_SEGS
#	define APM_DO_ZERO_SEGS	\
		"pushl %%ds\n\t" \
		"pushl %%es\n\t" \
		"pushl %%fs\n\t" \
		"pushl %%gs\n\t" \
		"xorl %%edx, %%edx\n\t" \
		"mov %%dx, %%ds\n\t" \
		"mov %%dx, %%es\n\t" \
		"mov %%dx, %%fs\n\t" \
		"mov %%dx, %%gs\n\t"
#	define APM_DO_RESTORE_SEGS	\
		"popl %%gs\n\t" \
		"popl %%fs\n\t" \
		"popl %%es\n\t" \
		"popl %%ds\n\t"
#else
#	define APM_DO_ZERO_SEGS
#	define APM_DO_RESTORE_SEGS
#endif

#define APM_BIOS_CALL(error_reg) \
	__asm__ __volatile__( \
		APM_DO_ZERO_SEGS \
		"pushfl\n\t" \
		APM_DO_CLI \
		"lcall %%cs:" SYMBOL_NAME_STR(apm_bios_entry) "\n\t" \
		"setc %%" # error_reg "\n\t" \
		"popfl\n\t" \
		APM_DO_RESTORE_SEGS
#define APM_BIOS_CALL_END \
		: "ax", "bx", "cx", "dx", "si", "di", "bp", "memory")

#ifdef CONFIG_APM_CPU_IDLE
#define APM_SET_CPU_IDLE(error) \
	APM_BIOS_CALL(al) \
	: "=a" (error) \
	: "0" (0x5305) \
	APM_BIOS_CALL_END
#endif

#define APM_SET_CPU_BUSY(error) \
	APM_BIOS_CALL(al) \
	: "=a" (error) \
	: "0" (0x5306) \
	APM_BIOS_CALL_END

#define APM_SET_POWER_STATE(state, error) \
	APM_BIOS_CALL(al) \
	: "=a" (error) \
	: "0" (0x5307), "b" (0x0001), "c" (state) \
	APM_BIOS_CALL_END

#ifdef CONFIG_APM_DISPLAY_BLANK
#define APM_SET_DISPLAY_POWER_STATE(state, error) \
	APM_BIOS_CALL(al) \
	: "=a" (error) \
	: "0" (0x5307), "b" (0x01ff), "c" (state) \
	APM_BIOS_CALL_END
#endif

#ifdef CONFIG_APM_DO_ENABLE
#define APM_ENABLE_POWER_MANAGEMENT(device, error) \
	APM_BIOS_CALL(al) \
	: "=a" (error) \
	: "0" (0x5308), "b" (device), "c" (1) \
	APM_BIOS_CALL_END
#endif

#define APM_GET_POWER_STATUS(bx, cx, dx, error) \
	APM_BIOS_CALL(al) \
	: "=a" (error), "=b" (bx), "=c" (cx), "=d" (dx) \
	: "0" (0x530a), "1" (1) \
	APM_BIOS_CALL_END

#define APM_GET_EVENT(event, error)	\
	APM_BIOS_CALL(al) \
	: "=a" (error), "=b" (event) \
	: "0" (0x530b) \
	APM_BIOS_CALL_END

#define APM_DRIVER_VERSION(ver, ax, error) \
	APM_BIOS_CALL(bl) \
	: "=a" (ax), "=b" (error) \
	: "0" (0x530e), "1" (0), "c" (ver) \
	APM_BIOS_CALL_END

#define APM_ENGAGE_POWER_MANAGEMENT(device, error) \
	APM_BIOS_CALL(al) \
	: "=a" (error) \
	: "0" (0x530f), "b" (device), "c" (1) \
	APM_BIOS_CALL_END

/*
 * Forward declarations
 */
static void	suspend(void);
static void	standby(void);
static void	set_time(void);

static void	check_events(void);
static void	do_apm_timer(unsigned long);

static int	do_open(struct inode *, struct file *);
static void	do_release(struct inode *, struct file *);
static int	do_read(struct inode *, struct file *, char *, int);
static int	do_select(struct inode *, struct file *, int,
			  select_table *);
static int	do_ioctl(struct inode *, struct file *, u_int, u_long);

extern int	apm_register_callback(int (*)(apm_event_t));
extern void	apm_unregister_callback(int (*)(apm_event_t));

/*
 * Local variables
 */
static asmlinkage struct {
	unsigned long	offset;
	unsigned short	segment;
}				apm_bios_entry;
static int			apm_enabled = 0;
#ifdef CONFIG_APM_CPU_IDLE
static int			clock_slowed = 0;
#endif
static int			apm_major;
static int			suspends_pending = 0;
static int			standbys_pending = 0;

static long			clock_cmos_diff;
static int			got_clock_diff = 0;

static struct wait_queue *	process_list = NULL;
static struct apm_bios_struct *	user_list = NULL;

static struct timer_list	apm_timer;

static char			driver_version[] = "0.6b";

#ifdef APM_DEBUG
static char *	apm_event_name[] = {
	"system standby",
	"system suspend",
	"normal resume",
	"critical resume",
	"low battery",
	"power status change",
	"update time",
	"critical suspend",
	"user standby",
	"user suspend",
	"system standby resume"
};
#define NR_APM_EVENT_NAME	\
		(sizeof(apm_event_name) / sizeof(apm_event_name[0]))
#endif

static struct file_operations apm_bios_fops = {
	NULL,		/* lseek */
	do_read,
	NULL,		/* write */
	NULL,		/* readdir */
	do_select,
	do_ioctl,
	NULL,		/* mmap */
	do_open,
	do_release,
	NULL,		/* fsync */
	NULL		/* fasync */
};

typedef struct callback_list_t {
	int (*				callback)(apm_event_t);
	struct callback_list_t *	next;
} callback_list_t;

static callback_list_t *	callback_list = NULL;

typedef struct lookup_t {
	int	key;
	char *	msg;
} lookup_t;

static const lookup_t error_table[] = {
/* N/A	{ APM_SUCCESS,		"Operation succeeded" }, */
	{ APM_DISABLED,		"Power management disabled" },
	{ APM_CONNECTED,	"Real mode interface already connected" },
	{ APM_NOT_CONNECTED,	"Interface not connected" },
	{ APM_16_CONNECTED,	"16 bit interface already connected" },
/* N/A	{ APM_16_UNSUPPORTED,	"16 bit interface not supported" }, */
	{ APM_32_CONNECTED,	"32 bit interface already connected" },
	{ APM_32_UNSUPPORTED,	"32 bit interface not supported" },
	{ APM_BAD_DEVICE,	"Unrecognized device ID" },
	{ APM_BAD_PARAM,	"Parameter out of range" },
	{ APM_NOT_ENGAGED,	"Interface not engaged" },
	{ APM_BAD_STATE,	"Unable to enter requested state" },
/* N/A	{ APM_NO_EVENTS,	"No events pending" }, */
	{ APM_NOT_PRESENT,	"No APM present" }
};
#define ERROR_COUNT	(sizeof(error_table)/sizeof(lookup_t))

static int apm_driver_version(u_short *val)
{
	u_short	error;

	APM_DRIVER_VERSION(*val, *val, error);

	if (error & 0xff)
		return (*val >> 8);
	return APM_SUCCESS;
}

static int apm_get_event(apm_event_t *event)
{
	u_short	error;

	APM_GET_EVENT(*event, error);
	if (error & 0xff)
		return (error >> 8);
	return APM_SUCCESS;
}

static int apm_set_power_state(u_short state)
{
	u_short	error;

	APM_SET_POWER_STATE(state, error);
	if (error & 0xff)
		return (error >> 8);
	return APM_SUCCESS;
}

#ifdef CONFIG_APM_DISPLAY_BLANK
static int apm_set_display_power_state(u_short state)
{
	u_short	error;

	APM_SET_DISPLAY_POWER_STATE(state, error);
	if (error & 0xff)
		return (error >> 8);
	return APM_SUCCESS;
}
#endif

#ifdef CONFIG_APM_DO_ENABLE
static int apm_enable_power_management(void)
{
	u_short	error;

	APM_ENABLE_POWER_MANAGEMENT((apm_bios_info.version > 0x100)
				    ? 0x0001 : 0xffff,
				    error);
	if (error & 0xff)
		return (error >> 8);
	return APM_SUCCESS;
}
#endif

static int apm_get_power_status(u_short *status, u_short *bat, u_short *life)
{
	u_short	error;

	APM_GET_POWER_STATUS(*status, *bat, *life, error);
	if (error & 0xff)
		return (error >> 8);
	return APM_SUCCESS;
}

static int apm_engage_power_management(u_short device)
{
	u_short	error;

	APM_ENGAGE_POWER_MANAGEMENT(device, error);
	if (error & 0xff)
		return (error >> 8);
	return APM_SUCCESS;
}

static void apm_error(char *str, int err)
{
	int	i;

	for (i = 0; i < ERROR_COUNT; i++)
		if (error_table[i].key == err) break;
	if (i < ERROR_COUNT)
		printk("apm_bios: %s: %s\n", str, error_table[i].msg);
	else
		printk("apm_bios: %s: unknown error code %#2.2x\n", str, err);
}

int apm_display_blank(void)
{
#ifdef CONFIG_APM_DISPLAY_BLANK
	int	error;

	if (apm_bios_info.version == 0)
		return 0;
	error = apm_set_display_power_state(APM_STATE_STANDBY);
	if (error == APM_SUCCESS)
		return 1;
	apm_error("set display standby", error);
#endif
	return 0;
}

int apm_display_unblank(void)
{
#ifdef CONFIG_APM_DISPLAY_BLANK
	int error;

	if (apm_bios_info.version == 0)
		return 0;
	error = apm_set_display_power_state(APM_STATE_READY);
	if (error == APM_SUCCESS)
		return 1;
	apm_error("set display ready", error);
#endif
	return 0;
}

int apm_register_callback(int (*callback)(apm_event_t))
{
	callback_list_t *	new;

	new = kmalloc(sizeof(callback_list_t), GFP_KERNEL);
	if (new == NULL)
		return -ENOMEM;
	new->callback = callback;
	new->next = callback_list;
	callback_list = new;
	return 0;
}

void apm_unregister_callback(int (*callback)(apm_event_t))
{
	callback_list_t **	ptr;
	callback_list_t *	old;

	ptr = &callback_list;
	for (ptr = &callback_list; *ptr != NULL; ptr = &(*ptr)->next)
		if ((*ptr)->callback == callback)
			break;
	old = *ptr;
	*ptr = old->next;
	kfree_s(old, sizeof(callback_list_t));
}
	
static int queue_empty(struct apm_bios_struct * as)
{
	return as->event_head == as->event_tail;
}

static apm_event_t get_queued_event(struct apm_bios_struct * as)
{
	as->event_tail = (as->event_tail + 1) % APM_MAX_EVENTS;
	return as->events[as->event_tail];
}

static int queue_event(apm_event_t event)
{
	struct apm_bios_struct *	as;
	
	if (user_list == NULL)
		return 0;
	for (as = user_list; as != NULL; as = as->next) {
		as->event_head = (as->event_head + 1) % APM_MAX_EVENTS;
		if (as->event_head == as->event_tail)
			as->event_tail = (as->event_tail + 1) % APM_MAX_EVENTS;
		as->events[as->event_head] = event;
		if (!as->suser)
			continue;
		switch (event) {
		case APM_SYS_SUSPEND:
		case APM_USER_SUSPEND:
			as->suspends_pending++;
			suspends_pending++;
			break;

		case APM_SYS_STANDBY:
		case APM_USER_STANDBY:
			as->standbys_pending++;
			standbys_pending++;
			break;
		}
	}
	wake_up_interruptible(&process_list);
	return 1;
}

static void set_time(void)
{
	unsigned long	flags;

	if (!got_clock_diff)	/* Don't know time zone, can't set clock */
		return;

	save_flags(flags);
	cli();
	CURRENT_TIME = get_cmos_time() + clock_cmos_diff;
	restore_flags(flags);
}

static void suspend(void)
{
	unsigned long	flags;
	int		err;

				/* Estimate time zone so that set_time can
                                   update the clock */
	save_flags(flags);
	cli();
	clock_cmos_diff = CURRENT_TIME - get_cmos_time();
	got_clock_diff = 1;
	restore_flags(flags);
	
	err = apm_set_power_state(APM_STATE_SUSPEND);
	if (err)
		apm_error("suspend", err);
	set_time();
}

static void standby(void)
{
	int	err;

	err = apm_set_power_state(APM_STATE_STANDBY);
	if (err)
		apm_error("standby", err);
}

static apm_event_t get_event(void)
{
	int		error;
	apm_event_t	event;

	static int notified = 0;

	error = apm_get_event(&event);
	if (error == APM_SUCCESS)
		return event;

	if ((error != APM_NO_EVENTS) && (notified++ == 0))
		apm_error("get_event", error);

	return 0;
}

static void send_event(apm_event_t event, apm_event_t undo)
{
	callback_list_t *	call;
	callback_list_t *	fix;
    
	for (call = callback_list; call != NULL; call = call->next) {
		if (call->callback(event) && undo) {
			for (fix = callback_list; fix != call; fix = fix->next)
				fix->callback(undo);
			if (apm_bios_info.version > 0x100)
				apm_set_power_state(APM_STATE_REJECT);
			return;
		}
	}

	queue_event(event);
}

static void check_events(void)
{
	apm_event_t	event;

	while ((event = get_event()) != 0) {
		switch (event) {
		case APM_SYS_STANDBY:
		case APM_USER_STANDBY:
			send_event(event, APM_STANDBY_RESUME);
			if (standbys_pending <= 0)
				standby();
			break;

		case APM_USER_SUSPEND:
#ifdef CONFIG_APM_IGNORE_USER_SUSPEND
			apm_set_power_state(APM_STATE_REJECT);
			break;
#endif
		case APM_SYS_SUSPEND:
			send_event(event, APM_NORMAL_RESUME);
			if (suspends_pending <= 0)
				suspend();
			break;

		case APM_NORMAL_RESUME:
		case APM_CRITICAL_RESUME:
		case APM_STANDBY_RESUME:
			set_time();
			send_event(event, 0);
			break;

		case APM_LOW_BATTERY:
		case APM_POWER_STATUS_CHANGE:
			send_event(event, 0);
			break;

		case APM_UPDATE_TIME:
			set_time();
			break;

		case APM_CRITICAL_SUSPEND:
			suspend();
			break;
		}
#ifdef APM_DEBUG
		if (event <= NR_APM_EVENT_NAME)
			printk("APM BIOS received %s notify\n",
			       apm_event_name[event - 1]);
		else
			printk("APM BIOS received unknown event 0x%02x\n",
			       event);
#endif
	}
}

static void do_apm_timer(unsigned long unused)
{
	int	err;

	static int	pending_count = 0;

	if (((standbys_pending > 0) || (suspends_pending > 0))
	    && (apm_bios_info.version > 0x100)
	    && (pending_count-- <= 0)) {
		pending_count = 4;

		err = apm_set_power_state(APM_STATE_BUSY);
		if (err)
			apm_error("busy", err);
	}
	check_events();

	init_timer(&apm_timer);
	apm_timer.expires = APM_CHECK_TIMEOUT + jiffies;
	add_timer(&apm_timer);
}

int apm_do_idle(void)
{
#ifdef CONFIG_APM_CPU_IDLE
	unsigned short	error;

	if (!apm_enabled)
		return 0;

	APM_SET_CPU_IDLE(error);
	if (error & 0xff)
		return 0;

	clock_slowed = (apm_bios_info.flags & APM_IDLE_SLOWS_CLOCK) != 0;
	return 1;
#else
	return 0;
#endif
}

void apm_do_busy(void)
{
#ifdef CONFIG_APM_CPU_IDLE
	unsigned short	error;

#ifndef ALWAYS_CALL_BUSY
	if (!clock_slowed)
		return;
#endif

	APM_SET_CPU_BUSY(error);

	clock_slowed = 0;
#endif
}

static int check_apm_bios_struct(struct apm_bios_struct *as, const char *func)
{
	if ((as == NULL) || (as->magic != APM_BIOS_MAGIC)) {
		printk("apm_bios: %s passed bad filp", func);
		return 1;
	}
	return 0;
}

static int do_read(struct inode *inode, struct file *fp, char *buf, int count)
{
	struct apm_bios_struct *	as;
	int			i;
	apm_event_t		event;
	struct wait_queue	wait = { current,	NULL };

	as = fp->private_data;
	if (check_apm_bios_struct(as, "read"))
		return -EIO;
	if (count < sizeof(apm_event_t))
		return -EINVAL;
	if (queue_empty(as)) {
		if (fp->f_flags & O_NONBLOCK)
			return -EAGAIN;
		add_wait_queue(&process_list, &wait);
repeat:
		current->state = TASK_INTERRUPTIBLE;
		if (queue_empty(as)
		    && !(current->signal & ~current->blocked)) {
			schedule();
			goto repeat;
		}
		current->state = TASK_RUNNING;
		remove_wait_queue(&process_list, &wait);
	}
	i = count;
	while ((i >= sizeof(event)) && !queue_empty(as)) {
		event = get_queued_event(as);
		memcpy_tofs(buf, &event, sizeof(event));
		buf += sizeof(event);
		i -= sizeof(event);
	}
	if (i < count)
		return count - i;
	if (current->signal & ~current->blocked)
		return -ERESTARTSYS;
	return 0;
}

static int do_select(struct inode *inode, struct file *fp, int sel_type,
		     select_table * wait)
{
	struct apm_bios_struct *	as;

	as = fp->private_data;
	if (check_apm_bios_struct(as, "select"))
		return 0;
	if (sel_type != SEL_IN)
		return 0;
	if (!queue_empty(as))
		return 1;
	select_wait(&process_list, wait);
	return 0;
}

static int do_ioctl(struct inode * inode, struct file *filp,
		    u_int cmd, u_long arg)
{
	struct apm_bios_struct *	as;

	as = filp->private_data;
	if (check_apm_bios_struct(as, "ioctl"))
		return -EIO;
	switch (cmd) {
	case APM_IOC_STANDBY:
		if (as->standbys_pending > 0) {
			as->standbys_pending--;
			standbys_pending--;
			if (standbys_pending <= 0)
				standby();
		}
		break;
	case APM_IOC_SUSPEND:
		if (as->suspends_pending > 0) {
			as->suspends_pending--;
			suspends_pending--;
			if (suspends_pending <= 0)
				suspend();
		}
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void do_release(struct inode * inode, struct file * filp)
{
	struct apm_bios_struct *	as;

	as = filp->private_data;
	filp->private_data = NULL;
	if (check_apm_bios_struct(as, "release"))
		return;
	if (as->standbys_pending > 0) {
		standbys_pending -= as->standbys_pending;
		if (standbys_pending <= 0)
			standby();
	}
	if (as->suspends_pending > 0) {
		suspends_pending -= as->suspends_pending;
		if (suspends_pending <= 0)
			suspend();
	}
	if (user_list == as)
		user_list = as->next;
	else {
		struct apm_bios_struct *	as1;

		for (as1 = user_list;
		     (as1 != NULL) && (as1->next != as);
		     as1 = as1->next)
			;
		if (as1 == NULL)
			printk("apm_bios: filp not in user list");
		else
			as1->next = as->next;
	}
	kfree_s(as, sizeof(*as));
}

static int do_open(struct inode * inode, struct file * filp)
{
	struct apm_bios_struct *	as;

	as = (struct apm_bios_struct *)kmalloc(sizeof(*as), GFP_KERNEL);
	if (as == NULL) {
		printk("apm_bios: cannot allocate struct of size %d bytes",
		       sizeof(*as));
		return -ENOMEM;
	}
	as->magic = APM_BIOS_MAGIC;
	as->event_tail = as->event_head = 0;
	as->suspends_pending = as->standbys_pending = 0;
	as->suser = suser();
	as->next = user_list;
	user_list = as;
	filp->private_data = as;
	return 0;
}

int apm_proc(char *buf)
{
	char *		p;
	char *		power_stat;
	char *		bat_stat;
	unsigned short	bx;
	unsigned short	cx;
	unsigned short	dx;
	unsigned short	error;

	if (apm_bios_info.version == 0)
		return 0;
	p = buf;
	p += sprintf(p, "BIOS version: %d.%d\nFlags: 0x%02x\n",
		     (apm_bios_info.version >> 8) & 0xff,
		     apm_bios_info.version & 0xff,
		     apm_bios_info.flags);
	if ((apm_bios_info.flags & APM_32_BIT_SUPPORT) == 0)
		return p - buf;
	p += sprintf(p, "Entry %x:%lx cseg16 %x dseg %x",
		     apm_bios_info.cseg, apm_bios_info.offset,
		     apm_bios_info.cseg_16, apm_bios_info.dseg);
	if (apm_bios_info.version > 0x100)
		p += sprintf(p, " cseg len %x, dseg len %x",
			     apm_bios_info.cseg_len, apm_bios_info.dseg_len);
	*p++ = '\n';
	error = apm_get_power_status(&bx, &cx, &dx);
	if (error) {
		strcpy(p, "Power status not available\n");
		p += strlen(p);
		return p - buf;
	}
	switch ((bx >> 8) & 0xff) {
	case 0: power_stat = "off line"; break;
	case 1: power_stat = "on line"; break;
	case 2: power_stat = "on backup power"; break;
	default: power_stat = "unknown"; break;
	}
	switch (bx & 0xff) {
	case 0: bat_stat = "high"; break;
	case 1: bat_stat = "low"; break;
	case 2: bat_stat = "critical"; break;
	case 3: bat_stat = "charging"; break;
	default: bat_stat = "unknown"; break;
	}
	p += sprintf(p, "AC: %s\nBattery status: %s\nBattery life: ",
		     power_stat, bat_stat);
	if ((cx & 0xff) == 0xff) {
		strcpy(p, "unknown");
		p += strlen(p);
	} else
		p += sprintf(p, "%d%%", cx & 0xff);
	*p++ = '\n';
	if (apm_bios_info.version > 0x100) {
		p += sprintf(p, "Battery flag: 0x%02x\nBattery life: ",
			     (cx >> 8) & 0xff);
		if (dx == 0xffff) {
			strcpy(p, "unknown");
			p += strlen(p);
		}
		else
			p += sprintf(p, "%d %s", dx & 0x7fff,
				     ((dx & 0x8000) == 0)
				     ? "seconds" : "minutes");
		*p++ = '\n';
	}
	return p - buf;
}

static int apm_setup(void)
{
	unsigned short	bx;
	unsigned short	cx;
	unsigned short	dx;
	unsigned short	error;
	char *		power_stat;
	char *		bat_stat;

	if (apm_bios_info.version == 0) {
		printk("APM BIOS not found.\n");
		return -1;
	}
	printk("APM BIOS version %c.%c Flags 0x%02x (Driver version %s)\n",
	       ((apm_bios_info.version >> 8) & 0xff) + '0',
	       (apm_bios_info.version & 0xff) + '0',
	       apm_bios_info.flags,
	       driver_version);
	if ((apm_bios_info.flags & APM_32_BIT_SUPPORT) == 0) {
		printk("    No 32 bit BIOS support\n");
		return -1;
	}

	/*
	 * Fix for the Compaq Contura 3/25c which reports BIOS version 0.1
	 * but is reportedly a 1.0 BIOS.
	 */
	if (apm_bios_info.version == 0x001)
		apm_bios_info.version = 0x100;

	printk("    Entry %x:%lx cseg16 %x dseg %x",
	       apm_bios_info.cseg, apm_bios_info.offset,
	       apm_bios_info.cseg_16, apm_bios_info.dseg);
	if (apm_bios_info.version > 0x100)
		printk(" cseg len %x, dseg len %x",
		       apm_bios_info.cseg_len, apm_bios_info.dseg_len);
	printk("\n");

	apm_bios_entry.offset = apm_bios_info.offset;
	apm_bios_entry.segment = APM_CS;
	set_base(gdt[APM_CS >> 3],
		 0xc0000000 + ((unsigned long)apm_bios_info.cseg << 4));
	set_base(gdt[APM_CS_16 >> 3],
		 0xc0000000 + ((unsigned long)apm_bios_info.cseg_16 << 4));
	set_base(gdt[APM_DS >> 3],
		 0xc0000000 + ((unsigned long)apm_bios_info.dseg << 4));
	if (apm_bios_info.version == 0x100) {
		set_limit(gdt[APM_CS >> 3], 64 * 1024);
		set_limit(gdt[APM_CS_16 >> 3], 64 * 1024);
		set_limit(gdt[APM_DS >> 3], 64 * 1024);
	} else {
		set_limit(gdt[APM_CS >> 3], apm_bios_info.cseg_len);
		/*
		 * This is not clear from the spec, but at least one
		 * machine needs this to be a 64k segment.
		 */
		set_limit(gdt[APM_CS_16 >> 3], 64 * 1024);
		set_limit(gdt[APM_DS >> 3], apm_bios_info.dseg_len);
		apm_bios_info.version = 0x0101;
		error = apm_driver_version(&apm_bios_info.version);
		if (error != 0)
			apm_bios_info.version = 0x100;
		else {
			apm_engage_power_management(0x0001);
			printk( "    Connection version %d.%d\n",
				(apm_bios_info.version >> 8) & 0xff,
				apm_bios_info.version & 0xff );
			apm_bios_info.version = 0x0101;
		}
	}

	error = apm_get_power_status(&bx, &cx, &dx);
	if (error)
		printk("    Power status not available\n");
	else {
		switch ((bx >> 8) & 0xff) {
		case 0: power_stat = "off line"; break;
		case 1: power_stat = "on line"; break;
		case 2: power_stat = "on backup power"; break;
		default: power_stat = "unknown"; break;
		}
		switch (bx & 0xff) {
		case 0: bat_stat = "high"; break;
		case 1: bat_stat = "low"; break;
		case 2: bat_stat = "critical"; break;
		case 3: bat_stat = "charging"; break;
		default: bat_stat = "unknown"; break;
		}
		printk("    AC %s, battery status %s, battery life ",
		       power_stat, bat_stat);
		if ((cx & 0xff) == 0xff)
			printk("unknown\n");
		else
			printk("%d%%\n", cx & 0xff);
		if (apm_bios_info.version > 0x100) {
			printk("    battery flag 0x%02x, battery life ",
			       (cx >> 8) & 0xff);
			if (dx == 0xffff)
				printk("unknown\n");
			else
				printk("%d %s\n", dx & 0x7fff,
				       ((dx & 0x8000) == 0)
				       ? "seconds" : "minutes");
		}
	}

#ifdef CONFIG_APM_DO_ENABLE
	/*
	 * This call causes my NEC UltraLite Versa 33/C to hang if it is
	 * booted with PM disabled but not in the docking station.
	 * Unfortunate ...
	 */
	error = apm_enable_power_management();
	if (error)
		apm_error("enable power management", error);
#endif

	init_timer(&apm_timer);
	apm_timer.function = do_apm_timer;
	apm_timer.expires = APM_CHECK_TIMEOUT + jiffies;
	add_timer(&apm_timer);

	register_symtab(&apm_syms);

	apm_enabled = 1;

	if ((apm_major = register_chrdev(0, "apm_bios", &apm_bios_fops)) < 0)
		printk("APM BIOS: Cannot allocate major device number\n");

	return 0;
}

void apm_bios_init(void)
{
	apm_setup();
}
