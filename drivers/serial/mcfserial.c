/*
 * mcfserial.c -- serial driver for ColdFire internal UARTS.
 *
 * Copyright (C) 1999-2003 Greg Ungerer <gerg@snapgear.com>
 * Copyright (c) 2000-2001 Lineo, Inc. <www.lineo.com> 
 * Copyright (C) 2001-2002 SnapGear Inc. <www.snapgear.com> 
 *
 * Based on code from 68332serial.c which was:
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998 TSHG
 * Copyright (c) 1999 Rt-Control Inc. <jeff@uclinux.org>
 */
 
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/config.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/serial.h>
#include <linux/serialP.h>
#ifdef CONFIG_LEDMAN
#include <linux/ledman.h>
#endif
#include <linux/console.h>
#include <linux/version.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/semaphore.h>
#include <asm/bitops.h>
#include <asm/delay.h>
#include <asm/coldfire.h>
#include <asm/mcfsim.h>
#include <asm/mcfuart.h>
#include <asm/nettel.h>
#include <asm/uaccess.h>
#include "mcfserial.h"

struct timer_list mcfrs_timer_struct;

/*
 *	Default console baud rate,  we use this as the default
 *	for all ports so init can just open /dev/console and
 *	keep going.  Perhaps one day the cflag settings for the
 *	console can be used instead.
 */
#if defined(CONFIG_ARNEWSH) || defined(CONFIG_MOTOROLA) || defined(CONFIG_senTec)
#define	CONSOLE_BAUD_RATE	19200
#define	DEFAULT_CBAUD		B19200
#endif

#ifndef CONSOLE_BAUD_RATE
#define	CONSOLE_BAUD_RATE	9600
#define	DEFAULT_CBAUD		B9600
#endif

int mcfrs_console_inited = 0;
int mcfrs_console_port = -1;
int mcfrs_console_baud = CONSOLE_BAUD_RATE;
int mcfrs_console_cbaud = DEFAULT_CBAUD;

/*
 *	Driver data structures.
 */
struct tty_driver	mcfrs_serial_driver, mcfrs_callout_driver;
static int		mcfrs_serial_refcount;

/* serial subtype definitions */
#define SERIAL_TYPE_NORMAL	1
#define SERIAL_TYPE_CALLOUT	2
  
/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

/* Debugging...
 */
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW

#define _INLINE_ inline

#define	IRQBASE	73

/*
 *	Configuration table, UARTs to look for at startup.
 */
static struct mcf_serial mcfrs_table[] = {
  { 0, (MCF_MBAR+MCFUART_BASE1), IRQBASE,   ASYNC_BOOT_AUTOCONF },  /* ttyS0 */
  { 0, (MCF_MBAR+MCFUART_BASE2), IRQBASE+1, ASYNC_BOOT_AUTOCONF },  /* ttyS1 */
};


#define	NR_PORTS	(sizeof(mcfrs_table) / sizeof(struct mcf_serial))

static struct tty_struct	*mcfrs_serial_table[NR_PORTS];
static struct termios		*mcfrs_serial_termios[NR_PORTS];
static struct termios		*mcfrs_serial_termios_locked[NR_PORTS];

/*
 * This is used to figure out the divisor speeds and the timeouts.
 */
static int mcfrs_baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400, 460800, 0
};
#define MCFRS_BAUD_TABLE_SIZE \
			(sizeof(mcfrs_baud_table)/sizeof(mcfrs_baud_table[0]))


#ifdef CONFIG_MAGIC_SYSRQ
/*
 *	Magic system request keys. Used for debugging...
 */
extern int	magic_sysrq_key(int ch);
#endif


/*
 * tmp_buf is used as a temporary buffer by serial_write.  We need to
 * lock it in case the copy_from_user blocks while swapping in a page,
 * and some other program tries to do a serial write at the same time.
 * Since the lock will only come under contention when the system is
 * swapping and available memory is low, it makes sense to share one
 * buffer across all the serial ports, since it significantly saves
 * memory if large numbers of serial ports are open.
 */
static unsigned char mcfrs_tmp_buf[4096]; /* This is cheating */
static DECLARE_MUTEX(mcfrs_tmp_buf_sem);

/*
 *	Forware declarations...
 */
static void	mcfrs_change_speed(struct mcf_serial *info);


static inline int serial_paranoia_check(struct mcf_serial *info,
					kdev_t device, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"MCFRS(warning): bad magic number for serial struct (%d, %d) in %s\n";
	static const char *badinfo =
		"MCFRS(warning): null mcf_serial for (%d, %d) in %s\n";

	if (!info) {
		printk(badinfo, MAJOR(device), MINOR(device), routine);
		return 1;
	}
	if (info->magic != SERIAL_MAGIC) {
		printk(badmagic, MAJOR(device), MINOR(device), routine);
		return 1;
	}
#endif
	return 0;
}

/*
 *	Sets or clears DTR and RTS on the requested line.
 */
static void mcfrs_setsignals(struct mcf_serial *info, int dtr, int rts)
{
	volatile unsigned char	*uartp;
	unsigned long		flags;
	
#if 0
	printk("%s(%d): mcfrs_setsignals(info=%x,dtr=%d,rts=%d)\n",
		__FILE__, __LINE__, info, dtr, rts);
#endif

	local_irq_save(flags);
	if (dtr >= 0) {
#ifdef MCFPP_DTR0
		if (info->line)
			mcf_setppdata(MCFPP_DTR1, (dtr ? 0 : MCFPP_DTR1));
		else
			mcf_setppdata(MCFPP_DTR0, (dtr ? 0 : MCFPP_DTR0));
#endif
	}
	if (rts >= 0) {
		uartp = (volatile unsigned char *) info->addr;
		if (rts) {
			info->sigs |= TIOCM_RTS;
			uartp[MCFUART_UOP1] = MCFUART_UOP_RTS;
		} else {
			info->sigs &= ~TIOCM_RTS;
			uartp[MCFUART_UOP0] = MCFUART_UOP_RTS;
		}
	}
	local_irq_restore(flags);
	return;
}

/*
 *	Gets values of serial signals.
 */
static int mcfrs_getsignals(struct mcf_serial *info)
{
	volatile unsigned char	*uartp;
	unsigned long		flags;
	int			sigs;
#if defined(CONFIG_NETtel) && defined(CONFIG_M5307)
	unsigned short		ppdata;
#endif

#if 0
	printk("%s(%d): mcfrs_getsignals(info=%x)\n", __FILE__, __LINE__);
#endif

	local_irq_save(flags);
	uartp = (volatile unsigned char *) info->addr;
	sigs = (uartp[MCFUART_UIPR] & MCFUART_UIPR_CTS) ? 0 : TIOCM_CTS;
	sigs |= (info->sigs & TIOCM_RTS);

#ifdef MCFPP_DCD0
{
	unsigned int ppdata;
	ppdata = mcf_getppdata();
	if (info->line == 0) {
		sigs |= (ppdata & MCFPP_DCD0) ? 0 : TIOCM_CD;
		sigs |= (ppdata & MCFPP_DTR0) ? 0 : TIOCM_DTR;
	} else if (info->line == 1) {
		sigs |= (ppdata & MCFPP_DCD1) ? 0 : TIOCM_CD;
		sigs |= (ppdata & MCFPP_DTR1) ? 0 : TIOCM_DTR;
	}
}
#endif

	local_irq_restore(flags);
	return(sigs);
}

/*
 * ------------------------------------------------------------
 * mcfrs_stop() and mcfrs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * They enable or disable transmitter interrupts, as necessary.
 * ------------------------------------------------------------
 */
static void mcfrs_stop(struct tty_struct *tty)
{
	volatile unsigned char	*uartp;
	struct mcf_serial	*info = (struct mcf_serial *)tty->driver_data;
	unsigned long		flags;

	if (serial_paranoia_check(info, tty->device, "mcfrs_stop"))
		return;
	
	local_irq_save(flags);
	uartp = (volatile unsigned char *) info->addr;
	info->imr &= ~MCFUART_UIR_TXREADY;
	uartp[MCFUART_UIMR] = info->imr;
	local_irq_restore(flags);
}

static void mcfrs_start(struct tty_struct *tty)
{
	volatile unsigned char	*uartp;
	struct mcf_serial	*info = (struct mcf_serial *)tty->driver_data;
	unsigned long		flags;
	
	if (serial_paranoia_check(info, tty->device, "mcfrs_start"))
		return;

	local_irq_save(flags);
	if (info->xmit_cnt && info->xmit_buf) {
		uartp = (volatile unsigned char *) info->addr;
		info->imr |= MCFUART_UIR_TXREADY;
		uartp[MCFUART_UIMR] = info->imr;
	}
	local_irq_restore(flags);
}

/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * mcfrs_interrupt().  They were separated out for readability's sake.
 *
 * Note: mcfrs_interrupt() is a "fast" interrupt, which means that it
 * runs with interrupts turned off.  People who may want to modify
 * mcfrs_interrupt() should try to keep the interrupt handler as fast as
 * possible.  After you are done making modifications, it is not a bad
 * idea to do:
 * 
 * gcc -S -DKERNEL -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer serial.c
 *
 * and look at the resulting assemble code in serial.s.
 *
 * 				- Ted Ts'o (tytso@mit.edu), 7-Mar-93
 * -----------------------------------------------------------------------
 */

static _INLINE_ void receive_chars(struct mcf_serial *info, struct pt_regs *regs, unsigned short rx)
{
	volatile unsigned char	*uartp;
	struct tty_struct	*tty = info->tty;
	unsigned char		status, ch;

	if (!tty)
		return;

#if defined(CONFIG_LEDMAN)
	ledman_cmd(LEDMAN_CMD_SET, info->line ? LEDMAN_COM2_RX : LEDMAN_COM1_RX);
#endif

	uartp = (volatile unsigned char *) info->addr;

	while ((status = uartp[MCFUART_USR]) & MCFUART_USR_RXREADY) {

		if (tty->flip.count >= TTY_FLIPBUF_SIZE)
			break;

		ch = uartp[MCFUART_URB];
		info->stats.rx++;

#ifdef CONFIG_MAGIC_SYSRQ
		if (mcfrs_console_inited && (info->line == mcfrs_console_port)) {
			if (magic_sysrq_key(ch))
				continue;
		}
#endif

		tty->flip.count++;
		if (status & MCFUART_USR_RXERR)
			uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETERR;
		if (status & MCFUART_USR_RXBREAK) {
			info->stats.rxbreak++;
			*tty->flip.flag_buf_ptr++ = TTY_BREAK;
		} else if (status & MCFUART_USR_RXPARITY) {
			info->stats.rxparity++;
			*tty->flip.flag_buf_ptr++ = TTY_PARITY;
		} else if (status & MCFUART_USR_RXOVERRUN) {
			info->stats.rxoverrun++;
			*tty->flip.flag_buf_ptr++ = TTY_OVERRUN;
		} else if (status & MCFUART_USR_RXFRAMING) {
			info->stats.rxframing++;
			*tty->flip.flag_buf_ptr++ = TTY_FRAME;
		} else {
			*tty->flip.flag_buf_ptr++ = 0;
		}
		*tty->flip.char_buf_ptr++ = ch;
	}

	schedule_work(&tty->flip.work);
	return;
}

static _INLINE_ void transmit_chars(struct mcf_serial *info)
{
	volatile unsigned char	*uartp;

#if defined(CONFIG_LEDMAN)
	ledman_cmd(LEDMAN_CMD_SET, info->line ? LEDMAN_COM2_TX : LEDMAN_COM1_TX);
#endif

	uartp = (volatile unsigned char *) info->addr;

	if (info->x_char) {
		/* Send special char - probably flow control */
		uartp[MCFUART_UTB] = info->x_char;
		info->x_char = 0;
		info->stats.tx++;
	}

	if ((info->xmit_cnt <= 0) || info->tty->stopped) {
		info->imr &= ~MCFUART_UIR_TXREADY;
		uartp[MCFUART_UIMR] = info->imr;
		return;
	}

	while (uartp[MCFUART_USR] & MCFUART_USR_TXREADY) {
		uartp[MCFUART_UTB] = info->xmit_buf[info->xmit_tail++];
		info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
		info->stats.tx++;
		if (--info->xmit_cnt <= 0)
			break;
	}

	if (info->xmit_cnt < WAKEUP_CHARS)
		schedule_work(&info->tqueue);
	return;
}

/*
 * This is the serial driver's generic interrupt routine
 */
void mcfrs_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct mcf_serial	*info;
	unsigned char		isr;

	info = &mcfrs_table[(irq - IRQBASE)];
	isr = (((volatile unsigned char *)info->addr)[MCFUART_UISR]) & info->imr;

	if (isr & MCFUART_UIR_RXREADY)
		receive_chars(info, regs, isr);
	if (isr & MCFUART_UIR_TXREADY)
		transmit_chars(info);
#if 0
	if (isr & MCFUART_UIR_DELTABREAK) {
		printk("%s(%d): delta break!\n", __FILE__, __LINE__);
		receive_chars(info, regs, isr);
	}
#endif

	return;
}

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

static void mcfrs_offintr(void *private)
{
	struct mcf_serial	*info = (struct mcf_serial *) private;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;

	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
	wake_up_interruptible(&tty->write_wait);
}


/*
 *	Change of state on a DCD line.
 */
void mcfrs_modem_change(struct mcf_serial *info, int dcd)
{
	if (info->count == 0)
		return;

	if (info->flags & ASYNC_CHECK_CD) {
		if (dcd) {
			wake_up_interruptible(&info->open_wait);
		} else if (!((info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ASYNC_CALLOUT_NOHUP))) {
			schedule_work(&info->tqueue_hangup);
		}
	}
}


#ifdef MCFPP_DCD0

unsigned short	mcfrs_ppstatus;

/*
 * This subroutine is called when the RS_TIMER goes off. It is used
 * to monitor the state of the DCD lines - since they have no edge
 * sensors and interrupt generators.
 */
static void mcfrs_timer(void)
{
	unsigned int	ppstatus, dcdval, i;

	ppstatus = mcf_getppdata() & (MCFPP_DCD0 | MCFPP_DCD1);

	if (ppstatus != mcfrs_ppstatus) {
		for (i = 0; (i < 2); i++) {
			dcdval = (i ? MCFPP_DCD1 : MCFPP_DCD0);
			if ((ppstatus & dcdval) != (mcfrs_ppstatus & dcdval)) {
				mcfrs_modem_change(&mcfrs_table[i],
					((ppstatus & dcdval) ? 0 : 1));
			}
		}
	}
	mcfrs_ppstatus = ppstatus;

	/* Re-arm timer */
	mcfrs_timer_struct.expires = jiffies + HZ/25;
	add_timer(&mcfrs_timer_struct);
}

#endif	/* MCFPP_DCD0 */


/*
 * This routine is called from the scheduler tqueue when the interrupt
 * routine has signalled that a hangup has occurred. The path of
 * hangup processing is:
 *
 * 	serial interrupt routine -> (scheduler tqueue) ->
 * 	do_serial_hangup() -> tty->hangup() -> mcfrs_hangup()
 * 
 */
static void do_serial_hangup(void *private)
{
	struct mcf_serial	*info = (struct mcf_serial *) private;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;

	tty_hangup(tty);
}

static int startup(struct mcf_serial * info)
{
	volatile unsigned char	*uartp;
	unsigned long		flags;
	
	if (info->flags & ASYNC_INITIALIZED)
		return 0;

	if (!info->xmit_buf) {
		info->xmit_buf = (unsigned char *) __get_free_page(GFP_KERNEL);
		if (!info->xmit_buf)
			return -ENOMEM;
	}

	local_irq_save(flags);

#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttyS%d (irq %d)...\n", info->line, info->irq);
#endif

	/*
	 *	Reset UART, get it into known state...
	 */
	uartp = (volatile unsigned char *) info->addr;
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETRX;  /* reset RX */
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETTX;  /* reset TX */
	mcfrs_setsignals(info, 1, 1);

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/*
	 * and set the speed of the serial port
	 */
	mcfrs_change_speed(info);

	/*
	 * Lastly enable the UART transmitter and receiver, and
	 * interrupt enables.
	 */
	info->imr = MCFUART_UIR_RXREADY;
	uartp[MCFUART_UCR] = MCFUART_UCR_RXENABLE | MCFUART_UCR_TXENABLE;
	uartp[MCFUART_UIMR] = info->imr;

	info->flags |= ASYNC_INITIALIZED;
	local_irq_restore(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct mcf_serial * info)
{
	volatile unsigned char	*uartp;
	unsigned long		flags;

	if (!(info->flags & ASYNC_INITIALIZED))
		return;

#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d (irq %d)....\n", info->line,
	       info->irq);
#endif
	
	local_irq_save(flags);

	uartp = (volatile unsigned char *) info->addr;
	uartp[MCFUART_UIMR] = 0;  /* mask all interrupts */
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETRX;  /* reset RX */
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETTX;  /* reset TX */

	if (!info->tty || (info->tty->termios->c_cflag & HUPCL))
		mcfrs_setsignals(info, 0, 0);

	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);
	
	info->flags &= ~ASYNC_INITIALIZED;
	local_irq_restore(flags);
}


/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void mcfrs_change_speed(struct mcf_serial *info)
{
	volatile unsigned char	*uartp;
	unsigned int		baudclk, cflag;
	unsigned long		flags;
	unsigned char		mr1, mr2;
	int			i;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;
	if (info->addr == 0)
		return;

#if 0
	printk("%s(%d): mcfrs_change_speed()\n", __FILE__, __LINE__);
#endif

	i = cflag & CBAUD;
	if (i & CBAUDEX) {
		i &= ~CBAUDEX;
		if (i < 1 || i > 4)
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			i += 15;
	}
	if (i == 0) {
		mcfrs_setsignals(info, 0, -1);
		return;
	}
	baudclk = ((MCF_BUSCLK / mcfrs_baud_table[i]) + 16) / 32;
	info->baud = mcfrs_baud_table[i];

	mr1 = MCFUART_MR1_RXIRQRDY | MCFUART_MR1_RXERRCHAR;
	mr2 = 0;

	switch (cflag & CSIZE) {
	case CS5:	mr1 |= MCFUART_MR1_CS5; break;
	case CS6:	mr1 |= MCFUART_MR1_CS6; break;
	case CS7:	mr1 |= MCFUART_MR1_CS7; break;
	case CS8:
	default:	mr1 |= MCFUART_MR1_CS8; break;
	}

	if (cflag & PARENB) {
		if (cflag & PARODD)
			mr1 |= MCFUART_MR1_PARITYODD;
		else
			mr1 |= MCFUART_MR1_PARITYEVEN;
	} else {
		mr1 |= MCFUART_MR1_PARITYNONE;
	}

	if (cflag & CSTOPB)
		mr2 |= MCFUART_MR2_STOP2;
	else
		mr2 |= MCFUART_MR2_STOP1;

	if (cflag & CRTSCTS) {
		mr1 |= MCFUART_MR1_RXRTS;
		mr2 |= MCFUART_MR2_TXCTS;
	}

	if (cflag & CLOCAL)
		info->flags &= ~ASYNC_CHECK_CD;
	else
		info->flags |= ASYNC_CHECK_CD;

	uartp = (volatile unsigned char *) info->addr;

	local_irq_save(flags);
#if 0
	printk("%s(%d): mr1=%x mr2=%x baudclk=%x\n", __FILE__, __LINE__,
		mr1, mr2, baudclk);
#endif
	/*
	  Note: pg 12-16 of MCF5206e User's Manual states that a
	  software reset should be performed prior to changing
	  UMR1,2, UCSR, UACR, bit 7
	*/
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETRX;    /* reset RX */
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETTX;    /* reset TX */
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETMRPTR;	/* reset MR pointer */
	uartp[MCFUART_UMR] = mr1;
	uartp[MCFUART_UMR] = mr2;
	uartp[MCFUART_UBG1] = (baudclk & 0xff00) >> 8;	/* set msb byte */
	uartp[MCFUART_UBG2] = (baudclk & 0xff);		/* set lsb byte */
	uartp[MCFUART_UCSR] = MCFUART_UCSR_RXCLKTIMER | MCFUART_UCSR_TXCLKTIMER;
	uartp[MCFUART_UCR] = MCFUART_UCR_RXENABLE | MCFUART_UCR_TXENABLE;
	mcfrs_setsignals(info, 1, -1);
	local_irq_restore(flags);
	return;
}

static void mcfrs_flush_chars(struct tty_struct *tty)
{
	volatile unsigned char	*uartp;
	struct mcf_serial	*info = (struct mcf_serial *)tty->driver_data;
	unsigned long		flags;

	if (serial_paranoia_check(info, tty->device, "mcfrs_flush_chars"))
		return;

	if (info->xmit_cnt <= 0 || tty->stopped || tty->hw_stopped ||
	    !info->xmit_buf)
		return;

	/* Enable transmitter */
	local_irq_save(flags);
	uartp = (volatile unsigned char *) info->addr;
	info->imr |= MCFUART_UIR_TXREADY;
	uartp[MCFUART_UIMR] = info->imr;
	local_irq_restore(flags);
}

static int mcfrs_write(struct tty_struct * tty, int from_user,
		    const unsigned char *buf, int count)
{
	volatile unsigned char	*uartp;
	struct mcf_serial	*info = (struct mcf_serial *)tty->driver_data;
	unsigned long		flags;
	int			c, total = 0;

#if 0
	printk("%s(%d): mcfrs_write(tty=%x,from_user=%d,buf=%x,count=%d)\n",
		__FILE__, __LINE__, tty, from_user, buf, count);
#endif

	if (serial_paranoia_check(info, tty->device, "mcfrs_write"))
		return 0;

	if (!tty || !info->xmit_buf)
		return 0;
	
	local_save_flags(flags);
	while (1) {
		local_irq_disable();		
		c = min(count, min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				   SERIAL_XMIT_SIZE - info->xmit_head));

		if (c <= 0) {
			local_irq_restore(flags);
			break;
		}

		if (from_user) {
			down(&mcfrs_tmp_buf_sem);
			copy_from_user(mcfrs_tmp_buf, buf, c);
			local_irq_restore(flags);
			local_irq_disable();
			c = min(c, min(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				       SERIAL_XMIT_SIZE - info->xmit_head));
			memcpy(info->xmit_buf + info->xmit_head, mcfrs_tmp_buf, c);
			up(&mcfrs_tmp_buf_sem);
		} else
			memcpy(info->xmit_buf + info->xmit_head, buf, c);

		info->xmit_head = (info->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt += c;
		local_irq_restore(flags);

		buf += c;
		count -= c;
		total += c;
	}

	local_irq_disable();
	uartp = (volatile unsigned char *) info->addr;
	info->imr |= MCFUART_UIR_TXREADY;
	uartp[MCFUART_UIMR] = info->imr;
	local_irq_restore(flags);

	return total;
}

static int mcfrs_write_room(struct tty_struct *tty)
{
	struct mcf_serial *info = (struct mcf_serial *)tty->driver_data;
	int	ret;
				
	if (serial_paranoia_check(info, tty->device, "mcfrs_write_room"))
		return 0;
	ret = SERIAL_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}

static int mcfrs_chars_in_buffer(struct tty_struct *tty)
{
	struct mcf_serial *info = (struct mcf_serial *)tty->driver_data;
				
	if (serial_paranoia_check(info, tty->device, "mcfrs_chars_in_buffer"))
		return 0;
	return info->xmit_cnt;
}

static void mcfrs_flush_buffer(struct tty_struct *tty)
{
	struct mcf_serial	*info = (struct mcf_serial *)tty->driver_data;
	unsigned long		flags;
				
	if (serial_paranoia_check(info, tty->device, "mcfrs_flush_buffer"))
		return;

	local_irq_save(flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	local_irq_restore(flags);

	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*
 * ------------------------------------------------------------
 * mcfrs_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void mcfrs_throttle(struct tty_struct * tty)
{
	struct mcf_serial *info = (struct mcf_serial *)tty->driver_data;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("throttle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "mcfrs_throttle"))
		return;
	
	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);

	/* Turn off RTS line (do this atomic) */
}

static void mcfrs_unthrottle(struct tty_struct * tty)
{
	struct mcf_serial *info = (struct mcf_serial *)tty->driver_data;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("unthrottle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "mcfrs_unthrottle"))
		return;
	
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			info->x_char = START_CHAR(tty);
	}

	/* Assert RTS line (do this atomic) */
}

/*
 * ------------------------------------------------------------
 * mcfrs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int get_serial_info(struct mcf_serial * info,
			   struct serial_struct * retinfo)
{
	struct serial_struct tmp;
  
	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = info->addr;
	tmp.irq = info->irq;
	tmp.flags = info->flags;
	tmp.baud_base = info->baud_base;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	tmp.custom_divisor = info->custom_divisor;
	copy_to_user(retinfo,&tmp,sizeof(*retinfo));
	return 0;
}

static int set_serial_info(struct mcf_serial * info,
			   struct serial_struct * new_info)
{
	struct serial_struct new_serial;
	struct mcf_serial old_info;
	int 	retval = 0;

	if (!new_info)
		return -EFAULT;
	copy_from_user(&new_serial,new_info,sizeof(new_serial));
	old_info = *info;

	if (!capable(CAP_SYS_ADMIN)) {
		if ((new_serial.baud_base != info->baud_base) ||
		    (new_serial.type != info->type) ||
		    (new_serial.close_delay != info->close_delay) ||
		    ((new_serial.flags & ~ASYNC_USR_MASK) !=
		     (info->flags & ~ASYNC_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~ASYNC_USR_MASK) |
			       (new_serial.flags & ASYNC_USR_MASK));
		info->custom_divisor = new_serial.custom_divisor;
		goto check_and_exit;
	}

	if (info->count > 1)
		return -EBUSY;

	/*
	 * OK, past this point, all the error checking has been done.
	 * At this point, we start making changes.....
	 */

	info->baud_base = new_serial.baud_base;
	info->flags = ((info->flags & ~ASYNC_FLAGS) |
			(new_serial.flags & ASYNC_FLAGS));
	info->type = new_serial.type;
	info->close_delay = new_serial.close_delay;
	info->closing_wait = new_serial.closing_wait;

check_and_exit:
	retval = startup(info);
	return retval;
}

/*
 * get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 * 	    is emptied.  On bus types like RS485, the transmitter must
 * 	    release the bus after transmitting. This must be done when
 * 	    the transmit shift register is empty, not be done when the
 * 	    transmit holding register is empty.  This functionality
 * 	    allows an RS485 driver to be written in user space. 
 */
static int get_lsr_info(struct mcf_serial * info, unsigned int *value)
{
	volatile unsigned char	*uartp;
	unsigned long		flags;
	unsigned char		status;

	local_irq_save(flags);
	uartp = (volatile unsigned char *) info->addr;
	status = (uartp[MCFUART_USR] & MCFUART_USR_TXEMPTY) ? TIOCSER_TEMT : 0;
	local_irq_restore(flags);

	put_user(status,value);
	return 0;
}

/*
 * This routine sends a break character out the serial port.
 */
static void send_break(	struct mcf_serial * info, int duration)
{
	volatile unsigned char	*uartp;
	unsigned long		flags;

	if (!info->addr)
		return;
	current->state = TASK_INTERRUPTIBLE;
	uartp = (volatile unsigned char *) info->addr;

	local_irq_save(flags);
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDBREAKSTART;
	schedule_timeout(jiffies + duration);
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDBREAKSTOP;
	local_irq_restore(flags);
}

static int mcfrs_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	struct mcf_serial * info = (struct mcf_serial *)tty->driver_data;
	unsigned int val;
	int retval, error;
	int dtr, rts;

	if (serial_paranoia_check(info, tty->device, "mcfrs_ioctl"))
		return -ENODEV;

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
	    (cmd != TIOCSERCONFIG) && (cmd != TIOCSERGWILD)  &&
	    (cmd != TIOCSERSWILD) && (cmd != TIOCSERGSTRUCT)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
		    return -EIO;
	}
	
	switch (cmd) {
		case TCSBRK:	/* SVID version: non-zero arg --> no break */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			if (!arg)
				send_break(info, HZ/4);	/* 1/4 second */
			return 0;
		case TCSBRKP:	/* support for POSIX tcsendbreak() */
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			send_break(info, arg ? arg*(HZ/10) : HZ/4);
			return 0;
		case TIOCGSOFTCAR:
			error = verify_area(VERIFY_WRITE, (void *) arg,sizeof(long));
			if (error)
				return error;
			put_user(C_CLOCAL(tty) ? 1 : 0,
				    (unsigned long *) arg);
			return 0;
		case TIOCSSOFTCAR:
			get_user(arg, (unsigned long *) arg);
			tty->termios->c_cflag =
				((tty->termios->c_cflag & ~CLOCAL) |
				 (arg ? CLOCAL : 0));
			return 0;
		case TIOCGSERIAL:
			error = verify_area(VERIFY_WRITE, (void *) arg,
						sizeof(struct serial_struct));
			if (error)
				return error;
			return get_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSSERIAL:
			return set_serial_info(info,
					       (struct serial_struct *) arg);
		case TIOCSERGETLSR: /* Get line status register */
			error = verify_area(VERIFY_WRITE, (void *) arg,
				sizeof(unsigned int));
			if (error)
				return error;
			else
			    return get_lsr_info(info, (unsigned int *) arg);

		case TIOCSERGSTRUCT:
			error = verify_area(VERIFY_WRITE, (void *) arg,
						sizeof(struct mcf_serial));
			if (error)
				return error;
			copy_to_user((struct mcf_serial *) arg,
				    info, sizeof(struct mcf_serial));
			return 0;
			
		case TIOCMGET:
			if ((error = verify_area(VERIFY_WRITE, (void *) arg,
                            sizeof(unsigned int))))
                                return(error);
			val = mcfrs_getsignals(info);
			put_user(val, (unsigned int *) arg);
			break;

                case TIOCMBIS:
			if ((error = verify_area(VERIFY_WRITE, (void *) arg,
                            sizeof(unsigned int))))
				return(error);

			get_user(val, (unsigned int *) arg);
			rts = (val & TIOCM_RTS) ? 1 : -1;
			dtr = (val & TIOCM_DTR) ? 1 : -1;
			mcfrs_setsignals(info, dtr, rts);
			break;

                case TIOCMBIC:
			if ((error = verify_area(VERIFY_WRITE, (void *) arg,
                            sizeof(unsigned int))))
				return(error);
			get_user(val, (unsigned int *) arg);
			rts = (val & TIOCM_RTS) ? 0 : -1;
			dtr = (val & TIOCM_DTR) ? 0 : -1;
			mcfrs_setsignals(info, dtr, rts);
			break;

                case TIOCMSET:
			if ((error = verify_area(VERIFY_WRITE, (void *) arg,
                            sizeof(unsigned int))))
				return(error);
			get_user(val, (unsigned int *) arg);
			rts = (val & TIOCM_RTS) ? 1 : 0;
			dtr = (val & TIOCM_DTR) ? 1 : 0;
			mcfrs_setsignals(info, dtr, rts);
			break;

#ifdef TIOCSET422
		case TIOCSET422:
			get_user(val, (unsigned int *) arg);
			mcf_setpa(MCFPP_PA11, (val ? 0 : MCFPP_PA11));
			break;
		case TIOCGET422:
			val = (mcf_getpa() & MCFPP_PA11) ? 0 : 1;
			put_user(val, (unsigned int *) arg);
			break;
#endif

		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

static void mcfrs_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct mcf_serial *info = (struct mcf_serial *)tty->driver_data;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;

	mcfrs_change_speed(info);

	if ((old_termios->c_cflag & CRTSCTS) &&
	    !(tty->termios->c_cflag & CRTSCTS)) {
		tty->hw_stopped = 0;
		mcfrs_setsignals(info, -1, 1);
#if 0
		mcfrs_start(tty);
#endif
	}
}

/*
 * ------------------------------------------------------------
 * mcfrs_close()
 * 
 * This routine is called when the serial port gets closed.  First, we
 * wait for the last remaining data to be sent.  Then, we unlink its
 * S structure from the interrupt chain if necessary, and we free
 * that IRQ if nothing is left in the chain.
 * ------------------------------------------------------------
 */
static void mcfrs_close(struct tty_struct *tty, struct file * filp)
{
	volatile unsigned char	*uartp;
	struct mcf_serial	*info = (struct mcf_serial *)tty->driver_data;
	unsigned long		flags;

	if (!info || serial_paranoia_check(info, tty->device, "mcfrs_close"))
		return;
	
	local_irq_save(flags);
	
	if (tty_hung_up_p(filp)) {
		local_irq_restore(flags);
		return;
	}
	
#ifdef SERIAL_DEBUG_OPEN
	printk("mcfrs_close ttyS%d, count = %d\n", info->line, info->count);
#endif
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("MCFRS: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk("MCFRS: bad serial port count for ttyS%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		local_irq_restore(flags);
		return;
	}
	info->flags |= ASYNC_CLOSING;

	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (info->flags & ASYNC_NORMAL_ACTIVE)
		info->normal_termios = *tty->termios;
	if (info->flags & ASYNC_CALLOUT_ACTIVE)
		info->callout_termios = *tty->termios;

	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);

	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	info->imr &= ~MCFUART_UIR_RXREADY;
	uartp = (volatile unsigned char *) info->addr;
	uartp[MCFUART_UIMR] = info->imr;

#if 0
	/* FIXME: do we need to keep this enabled for console?? */
	if (mcfrs_console_inited && (mcfrs_console_port == info->line)) {
		/* Do not disable the UART */ ;
	} else
#endif
	shutdown(info);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	tty->closing = 0;
	info->event = 0;
	info->tty = 0;
	if (tty->ldisc.num != ldiscs[N_TTY].num) {
		if (tty->ldisc.close)
			(tty->ldisc.close)(tty);
		tty->ldisc = ldiscs[N_TTY];
		tty->termios->c_line = N_TTY;
		if (tty->ldisc.open)
			(tty->ldisc.open)(tty);
	}
	if (info->blocked_open) {
		if (info->close_delay) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(info->close_delay);
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE|
			 ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
	local_irq_restore(flags);
}

/*
 * mcfrs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
void mcfrs_hangup(struct tty_struct *tty)
{
	struct mcf_serial * info = (struct mcf_serial *)tty->driver_data;
	
	if (serial_paranoia_check(info, tty->device, "mcfrs_hangup"))
		return;
	
	mcfrs_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~(ASYNC_NORMAL_ACTIVE|ASYNC_CALLOUT_ACTIVE);
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * mcfrs_open() and friends
 * ------------------------------------------------------------
 */
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct mcf_serial *info)
{
	DECLARE_WAITQUEUE(wait, current);
	int	retval;
	int	do_clocal = 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (info->flags & ASYNC_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		if (info->flags & ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}

	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */
	if (tty->driver.subtype == SERIAL_TYPE_CALLOUT) {
		if (info->flags & ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ASYNC_SESSION_LOCKOUT) &&
		    (info->session != current->session))
		    return -EBUSY;
		if ((info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ASYNC_PGRP_LOCKOUT) &&
		    (info->pgrp != current->pgrp))
		    return -EBUSY;
		info->flags |= ASYNC_CALLOUT_ACTIVE;
		return 0;
	}
	
	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		if (info->flags & ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ASYNC_NORMAL_ACTIVE;
		return 0;
	}

	if (info->flags & ASYNC_CALLOUT_ACTIVE) {
		if (info->normal_termios.c_cflag & CLOCAL)
			do_clocal = 1;
	} else {
		if (tty->termios->c_cflag & CLOCAL)
			do_clocal = 1;
	}
	
	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * mcfrs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: ttyS%d, count = %d\n",
	       info->line, info->count);
#endif
	info->count--;
	info->blocked_open++;
	while (1) {
		local_irq_disable();
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE))
			mcfrs_setsignals(info, 1, 1);
		local_irq_enable();
		current->state = TASK_INTERRUPTIBLE;
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ASYNC_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & ASYNC_CALLOUT_ACTIVE) &&
		    !(info->flags & ASYNC_CLOSING) &&
		    (do_clocal || (mcfrs_getsignals(info) & TIOCM_CD)))
			break;
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef SERIAL_DEBUG_OPEN
		printk("block_til_ready blocking: ttyS%d, count = %d\n",
		       info->line, info->count);
#endif
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready after blocking: ttyS%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ASYNC_NORMAL_ACTIVE;
	return 0;
}	

/*
 * This routine is called whenever a serial port is opened. It
 * enables interrupts for a serial port, linking in its structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
int mcfrs_open(struct tty_struct *tty, struct file * filp)
{
	struct mcf_serial	*info;
	int 			retval, line;

	line = minor(tty->device) - tty->driver.minor_start;
	if ((line < 0) || (line >= NR_PORTS))
		return -ENODEV;
	info = mcfrs_table + line;
	if (serial_paranoia_check(info, tty->device, "mcfrs_open"))
		return -ENODEV;
#ifdef SERIAL_DEBUG_OPEN
	printk("mcfrs_open %s%d, count = %d\n", tty->driver.name, info->line,
	       info->count);
#endif
	info->count++;
	tty->driver_data = info;
	info->tty = tty;

	/*
	 * Start up serial port
	 */
	retval = startup(info);
	if (retval)
		return retval;

	retval = block_til_ready(tty, filp, info);
	if (retval) {
#ifdef SERIAL_DEBUG_OPEN
		printk("mcfrs_open returning after block_til_ready with %d\n",
		       retval);
#endif
		return retval;
	}

	if ((info->count == 1) && (info->flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = info->normal_termios;
		else 
			*tty->termios = info->callout_termios;
		mcfrs_change_speed(info);
	}

	info->session = current->session;
	info->pgrp = current->pgrp;

#ifdef SERIAL_DEBUG_OPEN
	printk("mcfrs_open ttyS%d successful...\n", info->line);
#endif
	return 0;
}

/*
 *	Based on the line number set up the internal interrupt stuff.
 */
static void mcfrs_irqinit(struct mcf_serial *info)
{
#ifdef CONFIG_M5272
	volatile unsigned long	*icrp;
	volatile unsigned long	*portp;
	volatile unsigned char	*uartp;

	uartp = (volatile unsigned char *) info->addr;
	icrp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_ICR2);

	switch (info->line) {
	case 0:
		*icrp = 0xe0000000;
		break;
	case 1:
		*icrp = 0x0e000000;
		break;
	default:
		printk("MCFRS: don't know how to handle UART %d interrupt?\n",
			info->line);
		return;
	}

	/* Enable the output lines for the serial ports */
	portp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_PBCNT);
	*portp = (*portp & ~0x000000ff) | 0x00000055;
	portp = (volatile unsigned long *) (MCF_MBAR + MCFSIM_PDCNT);
	*portp = (*portp & ~0x000003fc) | 0x000002a8;
#else
	volatile unsigned char	*icrp, *uartp;

	switch (info->line) {
	case 0:
		icrp = (volatile unsigned char *) (MCF_MBAR + MCFSIM_UART1ICR);
		*icrp = /*MCFSIM_ICR_AUTOVEC |*/ MCFSIM_ICR_LEVEL6 |
			MCFSIM_ICR_PRI1;
		mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_UART1);
		break;
	case 1:
		icrp = (volatile unsigned char *) (MCF_MBAR + MCFSIM_UART2ICR);
		*icrp = /*MCFSIM_ICR_AUTOVEC |*/ MCFSIM_ICR_LEVEL6 |
			MCFSIM_ICR_PRI2;
		mcf_setimr(mcf_getimr() & ~MCFSIM_IMR_UART2);
		break;
	default:
		printk("MCFRS: don't know how to handle UART %d interrupt?\n",
			info->line);
		return;
	}

	uartp = (volatile unsigned char *) info->addr;
	uartp[MCFUART_UIVR] = info->irq;
#endif

	/* Clear mask, so no surprise interrupts. */
	uartp[MCFUART_UIMR] = 0;

	if (request_irq(info->irq, mcfrs_interrupt, SA_INTERRUPT,
	    "ColdFire UART", NULL)) {
		printk("MCFRS: Unable to attach ColdFire UART %d interrupt "
			"vector=%d\n", info->line, info->irq);
	}

	return;
}


char *mcfrs_drivername = "ColdFire internal UART serial driver version 1.00\n";


/*
 * Serial stats reporting...
 */
int mcfrs_readproc(char *page, char **start, off_t off, int count,
		         int *eof, void *data)
{
	struct mcf_serial	*info;
	char			str[20];
	int			len, sigs, i;

	len = sprintf(page, mcfrs_drivername);
	for (i = 0; (i < NR_PORTS); i++) {
		info = &mcfrs_table[i];
		len += sprintf((page + len), "%d: port:%x irq=%d baud:%d ",
			i, info->addr, info->irq, info->baud);
		if (info->stats.rx || info->stats.tx)
			len += sprintf((page + len), "tx:%d rx:%d ",
			info->stats.tx, info->stats.rx);
		if (info->stats.rxframing)
			len += sprintf((page + len), "fe:%d ",
			info->stats.rxframing);
		if (info->stats.rxparity)
			len += sprintf((page + len), "pe:%d ",
			info->stats.rxparity);
		if (info->stats.rxbreak)
			len += sprintf((page + len), "brk:%d ",
			info->stats.rxbreak);
		if (info->stats.rxoverrun)
			len += sprintf((page + len), "oe:%d ",
			info->stats.rxoverrun);

		str[0] = str[1] = 0;
		if ((sigs = mcfrs_getsignals(info))) {
			if (sigs & TIOCM_RTS)
				strcat(str, "|RTS");
			if (sigs & TIOCM_CTS)
				strcat(str, "|CTS");
			if (sigs & TIOCM_DTR)
				strcat(str, "|DTR");
			if (sigs & TIOCM_CD)
				strcat(str, "|CD");
		}

		len += sprintf((page + len), "%s\n", &str[1]);
	}

	return(len);
}


/* Finally, routines used to initialize the serial driver. */

static void show_serial_version(void)
{
	printk(mcfrs_drivername);
}

/* mcfrs_init inits the driver */
static int __init
mcfrs_init(void)
{
	struct mcf_serial	*info;
	unsigned long		flags;
	int			i;

	/* Setup base handler, and timer table. */
#ifdef MCFPP_DCD0
	init_timer(&mcfrs_timer_struct);
	mcfrs_timer_struct.function = mcfrs_timer;
	mcfrs_timer_struct.data = 0;
	mcfrs_timer_struct.expires = jiffies + HZ/25;
	add_timer(&mcfrs_timer_struct);
	mcfrs_ppstatus = mcf_getppdata() & (MCFPP_DCD0 | MCFPP_DCD1);
#endif

	show_serial_version();

	/* Initialize the tty_driver structure */
	memset(&mcfrs_serial_driver, 0, sizeof(struct tty_driver));
	mcfrs_serial_driver.magic = TTY_DRIVER_MAGIC;
	mcfrs_serial_driver.name = "ttyS";
	mcfrs_serial_driver.major = TTY_MAJOR;
	mcfrs_serial_driver.minor_start = 64;
	mcfrs_serial_driver.num = NR_PORTS;
	mcfrs_serial_driver.type = TTY_DRIVER_TYPE_SERIAL;
	mcfrs_serial_driver.subtype = SERIAL_TYPE_NORMAL;
	mcfrs_serial_driver.init_termios = tty_std_termios;

	mcfrs_serial_driver.init_termios.c_cflag =
		mcfrs_console_cbaud | CS8 | CREAD | HUPCL | CLOCAL;
	mcfrs_serial_driver.flags = TTY_DRIVER_REAL_RAW;
	mcfrs_serial_driver.refcount = &mcfrs_serial_refcount;
	mcfrs_serial_driver.table = mcfrs_serial_table;
	mcfrs_serial_driver.termios = mcfrs_serial_termios;
	mcfrs_serial_driver.termios_locked = mcfrs_serial_termios_locked;

	mcfrs_serial_driver.open = mcfrs_open;
	mcfrs_serial_driver.close = mcfrs_close;
	mcfrs_serial_driver.write = mcfrs_write;
	mcfrs_serial_driver.flush_chars = mcfrs_flush_chars;
	mcfrs_serial_driver.write_room = mcfrs_write_room;
	mcfrs_serial_driver.chars_in_buffer = mcfrs_chars_in_buffer;
	mcfrs_serial_driver.flush_buffer = mcfrs_flush_buffer;
	mcfrs_serial_driver.ioctl = mcfrs_ioctl;
	mcfrs_serial_driver.throttle = mcfrs_throttle;
	mcfrs_serial_driver.unthrottle = mcfrs_unthrottle;
	mcfrs_serial_driver.set_termios = mcfrs_set_termios;
	mcfrs_serial_driver.stop = mcfrs_stop;
	mcfrs_serial_driver.start = mcfrs_start;
	mcfrs_serial_driver.hangup = mcfrs_hangup;
	mcfrs_serial_driver.read_proc = mcfrs_readproc;
	mcfrs_serial_driver.driver_name = "serial";

	/*
	 * The callout device is just like normal device except for
	 * major number and the subtype code.
	 */
	mcfrs_callout_driver = mcfrs_serial_driver;
	mcfrs_callout_driver.name = "cua";
	mcfrs_callout_driver.major = TTYAUX_MAJOR;
	mcfrs_callout_driver.subtype = SERIAL_TYPE_CALLOUT;
	mcfrs_callout_driver.read_proc = 0;
	mcfrs_callout_driver.proc_entry = 0;

	if (tty_register_driver(&mcfrs_serial_driver)) {
		printk("MCFRS: Couldn't register serial driver\n");
		return(-EBUSY);
	}
	if (tty_register_driver(&mcfrs_callout_driver)) {
		printk("MCFRS: Couldn't register callout driver\n");
		return(-EBUSY);
	}
	
	local_irq_save(flags);

	/*
	 *	Configure all the attached serial ports.
	 */
	for (i = 0, info = mcfrs_table; (i < NR_PORTS); i++, info++) {
		info->magic = SERIAL_MAGIC;
		info->line = i;
		info->tty = 0;
		info->custom_divisor = 16;
		info->close_delay = 50;
		info->closing_wait = 3000;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
		INIT_WORK(&info->tqueue, mcfrs_offintr, info);
		INIT_WORK(&info->tqueue_hangup, do_serial_hangup, info);
		info->callout_termios = mcfrs_callout_driver.init_termios;
		info->normal_termios = mcfrs_serial_driver.init_termios;
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);

		info->imr = 0;
		mcfrs_setsignals(info, 0, 0);
		mcfrs_irqinit(info);

		printk("%s%d at 0x%04x (irq = %d)", mcfrs_serial_driver.name,
			info->line, info->addr, info->irq);
		printk(" is a builtin ColdFire UART\n");
	}

	local_irq_restore(flags);
	return 0;
}

module_init(mcfrs_init);
/* DAVIDM module_exit(mcfrs_fini); */

/****************************************************************************/
/*                          Serial Console                                  */
/****************************************************************************/

/*
 *	Quick and dirty UART initialization, for console output.
 */

void mcfrs_init_console(void)
{
	volatile unsigned char	*uartp;
	unsigned int		clk;

	/*
	 *	Reset UART, get it into known state...
	 */
	uartp = (volatile unsigned char *) (MCF_MBAR +
		(mcfrs_console_port ? MCFUART_BASE2 : MCFUART_BASE1));

	uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETRX;  /* reset RX */
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETTX;  /* reset TX */
	uartp[MCFUART_UCR] = MCFUART_UCR_CMDRESETMRPTR;  /* reset MR pointer */

	/*
	 * Set port for defined baud , 8 data bits, 1 stop bit, no parity.
	 */
	uartp[MCFUART_UMR] = MCFUART_MR1_PARITYNONE | MCFUART_MR1_CS8;
	uartp[MCFUART_UMR] = MCFUART_MR2_STOP1;

	clk = ((MCF_BUSCLK / mcfrs_console_baud) + 16) / 32; /* set baud */
	uartp[MCFUART_UBG1] = (clk & 0xff00) >> 8;  /* set msb baud */
	uartp[MCFUART_UBG2] = (clk & 0xff);  /* set lsb baud */

	uartp[MCFUART_UCSR] = MCFUART_UCSR_RXCLKTIMER | MCFUART_UCSR_TXCLKTIMER;
	uartp[MCFUART_UCR] = MCFUART_UCR_RXENABLE | MCFUART_UCR_TXENABLE;

	mcfrs_console_inited++;
	return;
}


/*
 *	Setup for console. Argument comes from the boot command line.
 */

int mcfrs_console_setup(struct console *cp, char *arg)
{
	int		i, n = CONSOLE_BAUD_RATE;

	if (!cp)
		return(-1);

	if (!strncmp(cp->name, "ttyS", 4))
		mcfrs_console_port = cp->index;
	else if (!strncmp(cp->name, "cua", 3))
		mcfrs_console_port = cp->index;
	else
		return(-1);

	if (arg)
		n = simple_strtoul(arg,NULL,0);
	for (i = 0; i < MCFRS_BAUD_TABLE_SIZE; i++)
		if (mcfrs_baud_table[i] == n)
			break;
	if (i < MCFRS_BAUD_TABLE_SIZE) {
		mcfrs_console_baud = n;
		mcfrs_console_cbaud = 0;
		if (i > 15) {
			mcfrs_console_cbaud |= CBAUDEX;
			i -= 15;
		}
		mcfrs_console_cbaud |= i;
	}
	mcfrs_init_console(); /* make sure baud rate changes */
	return(0);
}


static kdev_t mcfrs_console_device(struct console *c)
{
	return mk_kdev(TTY_MAJOR, 64 + c->index);
}


/*
 *	Output a single character, using UART polled mode.
 *	This is used for console output.
 */

void mcfrs_put_char(char ch)
{
	volatile unsigned char	*uartp;
	unsigned long		flags;
	int			i;

	uartp = (volatile unsigned char *) (MCF_MBAR +
		(mcfrs_console_port ? MCFUART_BASE2 : MCFUART_BASE1));

	local_irq_save(flags);
	for (i = 0; (i < 0x10000); i++) {
		if (uartp[MCFUART_USR] & MCFUART_USR_TXREADY)
			break;
	}
	if (i < 0x10000) {
		uartp[MCFUART_UTB] = ch;
		for (i = 0; (i < 0x10000); i++)
			if (uartp[MCFUART_USR] & MCFUART_USR_TXEMPTY)
				break;
	}
	if (i >= 0x10000)
		mcfrs_init_console(); /* try and get it back */
	local_irq_restore(flags);

	return;
}


/*
 * rs_console_write is registered for printk output.
 */

void mcfrs_console_write(struct console *cp, const char *p, unsigned len)
{
	if (!mcfrs_console_inited)
		mcfrs_init_console();
	while (len-- > 0) {
		if (*p == '\n')
			mcfrs_put_char('\r');
		mcfrs_put_char(*p++);
	}
}

/*
 * declare our consoles
 */

struct console mcfrs_console = {
	.name		= "ttyS",
	.write		= mcfrs_console_write,
	.device		= mcfrs_console_device,
	.setup		= mcfrs_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

void __init mcfrs_console_init(void)
{
	register_console(&mcfrs_console);
}

/****************************************************************************/
