/*
 * macserial.c: Serial port driver for Power Macintoshes.
 *
 * Derived from drivers/sbus/char/sunserial.c by Paul Mackerras.
 *
 * Copyright (C) 1996 Paul Mackerras (Paul.Mackerras@cs.anu.edu.au)
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/prom.h>
#include <asm/system.h>
#include <asm/segment.h>
#include <asm/bitops.h>

#include "macserial.h"

/*
 * It would be nice to dynamically allocate everything that
 * depends on NUM_SERIAL, so we could support any number of
 * Z8530s, but for now...
 */
#define NUM_SERIAL	2		/* Max number of ZS chips supported */
#define NUM_CHANNELS	(NUM_SERIAL * 2)	/* 2 channels per chip */

/* On PowerMacs, the hardware takes care of the SCC recovery time,
   but we need the eieio to make sure that the accesses occur
   in the order we want. */
#define RECOVERY_DELAY	eieio()

struct mac_zschannel *zs_kgdbchan;
struct mac_zschannel zs_channels[NUM_CHANNELS];

struct mac_serial zs_soft[NUM_CHANNELS];
int zs_channels_found;
struct mac_serial *zs_chain;	/* list of all channels */

struct tty_struct zs_ttys[NUM_CHANNELS];
/** struct tty_struct *zs_constty; **/

/* Console hooks... */
static int zs_cons_chan = 0;
struct mac_serial *zs_consinfo = 0;
struct mac_zschannel *zs_conschan;

/*
 * Initialization values for when a channel is used for
 * kernel gdb support.
 */
static unsigned char kgdb_regs[16] = {
	0, 0, 0,		/* write 0, 1, 2 */
	(Rx8 | RxENABLE),	/* write 3 */
	(X16CLK | SB1),		/* write 4 */
	(Tx8 | TxENAB | RTS),	/* write 5 */
	0, 0, 0,		/* write 6, 7, 8 */
	(NV),			/* write 9 */
	(NRZ),			/* write 10 */
	(TCBR | RCBR),		/* write 11 */
	1, 0,			/* 38400 baud divisor, write 12 + 13 */
	(BRENABL),		/* write 14 */
	(DCDIE)			/* write 15 */
};

#define ZS_CLOCK         3686400 	/* Z8530 RTxC input clock rate */

DECLARE_TASK_QUEUE(tq_serial);

struct tty_driver serial_driver, callout_driver;
static int serial_refcount;

/* serial subtype definitions */
#define SERIAL_TYPE_NORMAL	1
#define SERIAL_TYPE_CALLOUT	2

/* number of characters left in xmit buffer before we ask for more */
#define WAKEUP_CHARS 256

/* Debugging... DEBUG_INTR is bad to use when one of the zs
 * lines is your console ;(
 */
#undef SERIAL_DEBUG_INTR
#undef SERIAL_DEBUG_OPEN
#undef SERIAL_DEBUG_FLOW

#define RS_STROBE_TIME 10
#define RS_ISR_PASS_LIMIT 256

#define _INLINE_ inline

static void probe_sccs(void);
static void change_speed(struct mac_serial *info);

static struct tty_struct *serial_table[NUM_CHANNELS];
static struct termios *serial_termios[NUM_CHANNELS];
static struct termios *serial_termios_locked[NUM_CHANNELS];

#ifndef MIN
#define MIN(a,b)	((a) < (b) ? (a) : (b))
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
static unsigned char tmp_buf[4096]; /* This is cheating */
static struct semaphore tmp_buf_sem = MUTEX;

static inline int serial_paranoia_check(struct mac_serial *info,
					dev_t device, const char *routine)
{
#ifdef SERIAL_PARANOIA_CHECK
	static const char *badmagic =
		"Warning: bad magic number for serial struct (%d, %d) in %s\n";
	static const char *badinfo =
		"Warning: null mac_serial for (%d, %d) in %s\n";

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
 * This is used to figure out the divisor speeds and the timeouts
 */
static int baud_table[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400, 0 };

/* 
 * Reading and writing Z8530 registers.
 */
static inline unsigned char read_zsreg(struct mac_zschannel *channel,
				       unsigned char reg)
{
	unsigned char retval;

	if (reg != 0) {
		*channel->control = reg;
		RECOVERY_DELAY;
	}
	retval = *channel->control;
	RECOVERY_DELAY;
	return retval;
}

static inline void write_zsreg(struct mac_zschannel *channel,
			       unsigned char reg, unsigned char value)
{
	if (reg != 0) {
		*channel->control = reg;
		RECOVERY_DELAY;
	}
	*channel->control = value;
	RECOVERY_DELAY;
	return;
}

static inline unsigned char read_zsdata(struct mac_zschannel *channel)
{
	unsigned char retval;

	retval = *channel->data;
	RECOVERY_DELAY;
	return retval;
}

static inline void write_zsdata(struct mac_zschannel *channel,
				unsigned char value)
{
	*channel->data = value;
	RECOVERY_DELAY;
	return;
}

static inline void load_zsregs(struct mac_zschannel *channel,
			       unsigned char *regs)
{
	ZS_CLEARERR(channel);
	ZS_CLEARFIFO(channel);
	/* Load 'em up */
	write_zsreg(channel, R4, regs[R4]);
	write_zsreg(channel, R10, regs[R10]);
	write_zsreg(channel, R3, regs[R3] & ~RxENABLE);
	write_zsreg(channel, R5, regs[R5] & ~TxENAB);
	write_zsreg(channel, R1, regs[R1]);
	write_zsreg(channel, R9, regs[R9]);
	write_zsreg(channel, R11, regs[R11]);
	write_zsreg(channel, R12, regs[R12]);
	write_zsreg(channel, R13, regs[R13]);
	write_zsreg(channel, R14, regs[R14]);
	write_zsreg(channel, R15, regs[R15]);
	write_zsreg(channel, R3, regs[R3]);
	write_zsreg(channel, R5, regs[R5]);
	return;
}

/* Sets or clears DTR/RTS on the requested line */
static inline void zs_rtsdtr(struct mac_serial *ss, int set)
{
	if (set)
		ss->curregs[5] |= (RTS | DTR);
	else
		ss->curregs[5] &= ~(RTS | DTR);
	write_zsreg(ss->zs_channel, 5, ss->curregs[5]);
	return;
}

static inline void kgdb_chaninit(struct mac_serial *ss, int intson, int bps)
{
	int brg;

	if (intson) {
		kgdb_regs[R1] = INT_ALL_Rx;
		kgdb_regs[R9] |= MIE;
	} else {
		kgdb_regs[R1] = 0;
		kgdb_regs[R9] &= ~MIE;
	}
	brg = BPS_TO_BRG(bps, ZS_CLOCK/16);
	kgdb_regs[R12] = brg;
	kgdb_regs[R13] = brg >> 8;
	load_zsregs(ss->zs_channel, kgdb_regs);
}

/* Utility routines for the Zilog */
static inline int get_zsbaud(struct mac_serial *ss)
{
	struct mac_zschannel *channel = ss->zs_channel;
	int brg;

	if ((ss->curregs[R11] & TCBR) == 0) {
		/* higher rates don't use the baud rate generator */
		return (ss->curregs[R4] & X32CLK)? ZS_CLOCK/32: ZS_CLOCK/16;
	}
	/* The baud rate is split up between two 8-bit registers in
	 * what is termed 'BRG time constant' format in my docs for
	 * the chip, it is a function of the clk rate the chip is
	 * receiving which happens to be constant.
	 */
	brg = (read_zsreg(channel, 13) << 8);
	brg |= read_zsreg(channel, 12);
	return BRG_TO_BPS(brg, (ZS_CLOCK/(ss->clk_divisor)));
}

/* On receive, this clears errors and the receiver interrupts */
static inline void rs_recv_clear(struct mac_zschannel *zsc)
{
	write_zsreg(zsc, 0, ERR_RES);
	write_zsreg(zsc, 0, RES_H_IUS); /* XXX this is unnecessary */
}

/*
 * ----------------------------------------------------------------------
 *
 * Here starts the interrupt handling routines.  All of the following
 * subroutines are declared as inline and are folded into
 * rs_interrupt().  They were separated out for readability's sake.
 *
 * 				- Ted Ts'o (tytso@mit.edu), 7-Mar-93
 * -----------------------------------------------------------------------
 */

/*
 * This routine is used by the interrupt handler to schedule
 * processing in the software interrupt portion of the driver.
 */
static _INLINE_ void rs_sched_event(struct mac_serial *info,
				  int event)
{
	info->event |= 1 << event;
	queue_task(&info->tqueue, &tq_serial);
	mark_bh(SERIAL_BH);
}

extern void breakpoint(void);  /* For the KGDB frame character */

static _INLINE_ void receive_chars(struct mac_serial *info,
				   struct pt_regs *regs)
{
	struct tty_struct *tty = info->tty;
	unsigned char ch, stat, flag;

	while ((read_zsreg(info->zs_channel, 0) & Rx_CH_AV) != 0) {

		stat = read_zsreg(info->zs_channel, R1);
		ch = read_zsdata(info->zs_channel);

#if 0	/* KGDB not yet supported */
		/* Look for kgdb 'stop' character, consult the gdb documentation
		 * for remote target debugging and arch/sparc/kernel/sparc-stub.c
		 * to see how all this works.
		 */
		if ((info->kgdb_channel) && (ch =='\003')) {
			breakpoint();
			continue;
		}
#endif

		if (!tty)
			continue;
		queue_task(&tty->flip.tqueue, &tq_timer);

		if (tty->flip.count >= TTY_FLIPBUF_SIZE) {
			static int flip_buf_ovf;
			++flip_buf_ovf;
			continue;
		}
		tty->flip.count++;
		{
			static int flip_max_cnt;
			if (flip_max_cnt < tty->flip.count)
				flip_max_cnt = tty->flip.count;
		}
		if (stat & Rx_OVR) {
			flag = TTY_OVERRUN;
			/* reset the error indication */
			write_zsreg(info->zs_channel, 0, ERR_RES);
		} else if (stat & FRM_ERR) {
			/* this error is not sticky */
			flag = TTY_FRAME;
		} else if (stat & PAR_ERR) {
			flag = TTY_PARITY;
			/* reset the error indication */
			write_zsreg(info->zs_channel, 0, ERR_RES);
		} else
			flag = 0;
		*tty->flip.flag_buf_ptr++ = flag;
		*tty->flip.char_buf_ptr++ = ch;
	}
}

static void transmit_chars(struct mac_serial *info)
{
	if ((read_zsreg(info->zs_channel, 0) & Tx_BUF_EMP) == 0)
		return;
	info->tx_active = 0;

	if (info->x_char) {
		/* Send next char */
		write_zsdata(info->zs_channel, info->x_char);
		info->x_char = 0;
		info->tx_active = 1;
		return;
	}

	if ((info->xmit_cnt <= 0) || info->tty->stopped || info->tx_stopped) {
		write_zsreg(info->zs_channel, 0, RES_Tx_P);
		return;
	}

	/* Send char */
	write_zsdata(info->zs_channel, info->xmit_buf[info->xmit_tail++]);
	info->xmit_tail = info->xmit_tail & (SERIAL_XMIT_SIZE-1);
	info->xmit_cnt--;
	info->tx_active = 1;

	if (info->xmit_cnt < WAKEUP_CHARS)
		rs_sched_event(info, RS_EVENT_WRITE_WAKEUP);
}

static _INLINE_ void status_handle(struct mac_serial *info)
{
	unsigned char status;

	/* Get status from Read Register 0 */
	status = read_zsreg(info->zs_channel, 0);

	/* Check for DCD transitions */
	if (((status ^ info->read_reg_zero) & DCD) != 0
	    && info->tty && !C_CLOCAL(info->tty)) {
		if (status & DCD) {
			wake_up_interruptible(&info->open_wait);
		} else if (!(info->flags & ZILOG_CALLOUT_ACTIVE)) {
			queue_task(&info->tqueue_hangup, &tq_scheduler);
		}
	}

	/* Check for CTS transitions */
	if (info->tty && C_CRTSCTS(info->tty)) {
		/*
		 * For some reason, on the Power Macintosh,
		 * it seems that the CTS bit is 1 when CTS is
		 * *negated* and 0 when it is asserted.
		 * The DCD bit doesn't seem to be inverted
		 * like this.
		 */
		if ((status & CTS) == 0) {
			if (info->tx_stopped) {
				info->tx_stopped = 0;
				if (!info->tx_active)
					transmit_chars(info);
			}
		} else {
			info->tx_stopped = 1;
		}
	}

	/* Clear status condition... */
	write_zsreg(info->zs_channel, 0, RES_EXT_INT);
	info->read_reg_zero = status;
}

/*
 * This is the serial driver's generic interrupt routine
 */
void rs_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
	struct mac_serial *info = (struct mac_serial *) dev_id;
	unsigned char zs_intreg;
	int shift;

	/* NOTE: The read register 3, which holds the irq status,
	 *       does so for both channels on each chip.  Although
	 *       the status value itself must be read from the A
	 *       channel and is only valid when read from channel A.
	 *       Yes... broken hardware...
	 */
#define CHAN_IRQMASK (CHBRxIP | CHBTxIP | CHBEXT)

	if (info->zs_chan_a == info->zs_channel)
		shift = 3;	/* Channel A */
	else
		shift = 0;	/* Channel B */

	for (;;) {
		zs_intreg = read_zsreg(info->zs_chan_a, 3) >> shift;
		if ((zs_intreg & CHAN_IRQMASK) == 0)
			break;

		if (zs_intreg & CHBRxIP)
			receive_chars(info, regs);
		if (zs_intreg & CHBTxIP)
			transmit_chars(info);
		if (zs_intreg & CHBEXT)
			status_handle(info);
	}
}

/*
 * -------------------------------------------------------------------
 * Here ends the serial interrupt routines.
 * -------------------------------------------------------------------
 */

/*
 * ------------------------------------------------------------
 * rs_stop() and rs_start()
 *
 * This routines are called before setting or resetting tty->stopped.
 * ------------------------------------------------------------
 */
static void rs_stop(struct tty_struct *tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->device, "rs_stop"))
		return;
	
#if 0
	save_flags(flags); cli();
	if (info->curregs[5] & TxENAB) {
		info->curregs[5] &= ~TxENAB;
		info->pendregs[5] &= ~TxENAB;
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
	}
	restore_flags(flags);
#endif
}

static void rs_start(struct tty_struct *tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;
	
	if (serial_paranoia_check(info, tty->device, "rs_start"))
		return;
	
	save_flags(flags); cli();
#if 0
	if (info->xmit_cnt && info->xmit_buf && !(info->curregs[5] & TxENAB)) {
		info->curregs[5] |= TxENAB;
		info->pendregs[5] = info->curregs[5];
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
	}
#else
	if (info->xmit_cnt && info->xmit_buf && !info->tx_active) {
		transmit_chars(info);
	}
#endif
	restore_flags(flags);
}

/*
 * This routine is used to handle the "bottom half" processing for the
 * serial driver, known also the "software interrupt" processing.
 * This processing is done at the kernel interrupt level, after the
 * rs_interrupt() has returned, BUT WITH INTERRUPTS TURNED ON.  This
 * is where time-consuming activities which can not be done in the
 * interrupt driver proper are done; the interrupt driver schedules
 * them using rs_sched_event(), and they get done here.
 */
static void do_serial_bh(void)
{
	run_task_queue(&tq_serial);
}

static void do_softint(void *private_)
{
	struct mac_serial	*info = (struct mac_serial *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;

	if (test_and_clear_bit(RS_EVENT_WRITE_WAKEUP, &info->event)) {
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup)(tty);
		wake_up_interruptible(&tty->write_wait);
	}
}

/*
 * This routine is called from the scheduler tqueue when the interrupt
 * routine has signalled that a hangup has occurred.  The path of
 * hangup processing is:
 *
 * 	serial interrupt routine -> (scheduler tqueue) ->
 * 	do_serial_hangup() -> tty->hangup() -> rs_hangup()
 * 
 */
static void do_serial_hangup(void *private_)
{
	struct mac_serial	*info = (struct mac_serial *) private_;
	struct tty_struct	*tty;
	
	tty = info->tty;
	if (!tty)
		return;

	tty_hangup(tty);
}

static void rs_timer(void)
{
}

static int startup(struct mac_serial * info)
{
	unsigned long flags;

	if (info->flags & ZILOG_INITIALIZED)
		return 0;

	if (!info->xmit_buf) {
		info->xmit_buf = (unsigned char *) get_free_page(GFP_KERNEL);
		if (!info->xmit_buf)
			return -ENOMEM;
	}

	save_flags(flags); cli();

#ifdef SERIAL_DEBUG_OPEN
	printk("starting up ttyS%d (irq %d)...", info->line, info->irq);
#endif

	/*
	 * Clear the receive FIFO.
	 */
	ZS_CLEARFIFO(info->zs_channel);
	info->xmit_fifo_size = 1;

	/*
	 * Clear the interrupt registers.
	 */
	write_zsreg(info->zs_channel, 0, ERR_RES);
	write_zsreg(info->zs_channel, 0, RES_H_IUS);

	/*
	 * Turn on RTS and DTR.
	 */
	zs_rtsdtr(info, 1);

	/*
	 * Finally, enable sequencing and interrupts
	 */
	info->curregs[1] = (info->curregs[1] & ~0x18) | (EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB);
	info->pendregs[1] = info->curregs[1];
	info->curregs[3] |= (RxENABLE | Rx8);
	info->pendregs[3] = info->curregs[3];
	info->curregs[5] |= (TxENAB | Tx8);
	info->pendregs[5] = info->curregs[5];
	info->curregs[9] |= (NV | MIE);
	info->pendregs[9] = info->curregs[9];
	write_zsreg(info->zs_channel, 3, info->curregs[3]);
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	write_zsreg(info->zs_channel, 9, info->curregs[9]);

	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;

	/*
	 * Set the speed of the serial port
	 */
	change_speed(info);

	/* Save the current value of RR0 */
	info->read_reg_zero = read_zsreg(info->zs_channel, 0);

	info->flags |= ZILOG_INITIALIZED;
	restore_flags(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void shutdown(struct mac_serial * info)
{
	unsigned long	flags;

	if (!(info->flags & ZILOG_INITIALIZED))
		return;

#ifdef SERIAL_DEBUG_OPEN
	printk("Shutting down serial port %d (irq %d)....", info->line,
	       info->irq);
#endif
	
	save_flags(flags); cli(); /* Disable interrupts */
	
	if (info->xmit_buf) {
		free_page((unsigned long) info->xmit_buf);
		info->xmit_buf = 0;
	}

	info->pendregs[1] = info->curregs[1] = 0;
	write_zsreg(info->zs_channel, 1, 0);	/* no interrupts */

	info->curregs[3] &= ~RxENABLE;
	info->pendregs[3] = info->curregs[3];
	write_zsreg(info->zs_channel, 3, info->curregs[3]);

	info->curregs[5] &= ~TxENAB;
	if (!info->tty || C_HUPCL(info->tty))
		info->curregs[5] &= ~(DTR | RTS);
	info->pendregs[5] = info->curregs[5];
	write_zsreg(info->zs_channel, 5, info->curregs[5]);

	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	info->flags &= ~ZILOG_INITIALIZED;
	restore_flags(flags);
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void change_speed(struct mac_serial *info)
{
	unsigned short port;
	unsigned cflag;
	int	i;
	int	brg;
	unsigned long flags;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;
	if (!(port = info->port))
		return;
	i = cflag & CBAUD;

	save_flags(flags); cli();
	info->zs_baud = baud_table[i];
	info->clk_divisor = 16;

	switch (info->zs_baud) {
	case ZS_CLOCK/16:	/* 230400 */
		info->curregs[4] = X16CLK;
		info->curregs[11] = 0;
		break;
	case ZS_CLOCK/32:	/* 115200 */
		info->curregs[4] = X32CLK;
		info->curregs[11] = 0;
		break;
	default:
		info->curregs[4] = X16CLK;
		info->curregs[11] = TCBR | RCBR;
		brg = BPS_TO_BRG(info->zs_baud, ZS_CLOCK/info->clk_divisor);
		info->curregs[12] = (brg & 255);
		info->curregs[13] = ((brg >> 8) & 255);
		info->curregs[14] = BRENABL;
	}

	/* byte size and parity */
	info->curregs[3] &= ~RxNBITS_MASK;
	info->curregs[5] &= ~TxNBITS_MASK;
	switch (cflag & CSIZE) {
	case CS5:
		info->curregs[3] |= Rx5;
		info->curregs[5] |= Tx5;
		break;
	case CS6:
		info->curregs[3] |= Rx6;
		info->curregs[5] |= Tx6;
		break;
	case CS7:
		info->curregs[3] |= Rx7;
		info->curregs[5] |= Tx7;
		break;
	case CS8:
	default: /* defaults to 8 bits */
		info->curregs[3] |= Rx8;
		info->curregs[5] |= Tx8;
		break;
	}
	info->pendregs[3] = info->curregs[3];
	info->pendregs[5] = info->curregs[5];

	info->curregs[4] &= ~(SB_MASK | PAR_ENA | PAR_EVEN);
	if (cflag & CSTOPB) {
		info->curregs[4] |= SB2;
	} else {
		info->curregs[4] |= SB1;
	}
	if (cflag & PARENB) {
		info->curregs[4] |= PAR_ENA;
	}
	if (!(cflag & PARODD)) {
		info->curregs[4] |= PAR_EVEN;
	}
	info->pendregs[4] = info->curregs[4];

	if (!(cflag & CLOCAL)) {
		if (!(info->curregs[15] & DCDIE))
			info->read_reg_zero = read_zsreg(info->zs_channel, 0);
		info->curregs[15] |= DCDIE;
	} else
		info->curregs[15] &= ~DCDIE;
	if (cflag & CRTSCTS) {
		info->curregs[15] |= CTSIE;
		if ((read_zsreg(info->zs_channel, 0) & CTS) != 0)
			info->tx_stopped = 1;
	} else {
		info->curregs[15] &= ~CTSIE;
		info->tx_stopped = 0;
	}
	info->pendregs[15] = info->curregs[15];

	/* Load up the new values */
	load_zsregs(info->zs_channel, info->curregs);

	restore_flags(flags);
}

/* This is for console output over ttya/ttyb */
static void rs_put_char(char ch)
{
	struct mac_zschannel *chan = zs_conschan;
	int loops = 0;
	unsigned long flags;

	if(!chan)
		return;

	save_flags(flags); cli();
	while ((read_zsreg(chan, 0) & Tx_BUF_EMP) == 0)
		if (++loops >= 1000000)
			break;
	write_zsdata(chan, ch);
	restore_flags(flags);
}

/* These are for receiving and sending characters under the kgdb
 * source level kernel debugger.
 */
void putDebugChar(char kgdb_char)
{
	struct mac_zschannel *chan = zs_kgdbchan;

	while ((read_zsreg(chan, 0) & Tx_BUF_EMP) == 0)
		udelay(5);
	write_zsdata(chan, kgdb_char);
}

char getDebugChar(void)
{
	struct mac_zschannel *chan = zs_kgdbchan;

	while ((read_zsreg(chan, 0) & Rx_CH_AV) == 0)
		udelay(5);
	return read_zsdata(chan);
}

/*
 * Fair output driver allows a process to speak.
 */
static void rs_fair_output(void)
{
	int left;		/* Output no more than that */
	unsigned long flags;
	struct mac_serial *info = zs_consinfo;
	char c;

	if (info == 0) return;
	if (info->xmit_buf == 0) return;

	save_flags(flags);  cli();
	left = info->xmit_cnt;
	while (left != 0) {
		c = info->xmit_buf[info->xmit_tail];
		info->xmit_tail = (info->xmit_tail+1) & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt--;
		restore_flags(flags);

		rs_put_char(c);

		save_flags(flags);  cli();
		left = MIN(info->xmit_cnt, left-1);
	}

	restore_flags(flags);
	return;
}

/*
 * zs_console_print is registered for printk.
 */
static void zs_console_print(const char *p)
{
	char c;

	while ((c = *(p++)) != 0) {
		if (c == '\n')
			rs_put_char('\r');
		rs_put_char(c);
	}

	/* Comment this if you want to have a strict interrupt-driven output */
	rs_fair_output();
}

static void rs_flush_chars(struct tty_struct *tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "rs_flush_chars"))
		return;

	if (info->xmit_cnt <= 0 || tty->stopped || info->tx_stopped ||
	    !info->xmit_buf)
		return;

	/* Enable transmitter */
	save_flags(flags); cli();
	transmit_chars(info);
	restore_flags(flags);
}

static int rs_write(struct tty_struct * tty, int from_user,
		    const unsigned char *buf, int count)
{
	int	c, total = 0;
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;

	if (serial_paranoia_check(info, tty->device, "rs_write"))
		return 0;

	if (!tty || !info->xmit_buf)
		return 0;

	save_flags(flags);
	while (1) {
		cli();		
		c = MIN(count, MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				   SERIAL_XMIT_SIZE - info->xmit_head));
		if (c <= 0)
			break;

		if (from_user) {
			down(&tmp_buf_sem);
			copy_from_user(tmp_buf, buf, c);
			c = MIN(c, MIN(SERIAL_XMIT_SIZE - info->xmit_cnt - 1,
				       SERIAL_XMIT_SIZE - info->xmit_head));
			memcpy(info->xmit_buf + info->xmit_head, tmp_buf, c);
			up(&tmp_buf_sem);
		} else
			memcpy(info->xmit_buf + info->xmit_head, buf, c);
		info->xmit_head = (info->xmit_head + c) & (SERIAL_XMIT_SIZE-1);
		info->xmit_cnt += c;
		restore_flags(flags);
		buf += c;
		count -= c;
		total += c;
	}
	if (info->xmit_cnt && !tty->stopped && !info->tx_stopped
	    && !info->tx_active)
		transmit_chars(info);
	restore_flags(flags);
	return total;
}

static int rs_write_room(struct tty_struct *tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	int	ret;
				
	if (serial_paranoia_check(info, tty->device, "rs_write_room"))
		return 0;
	ret = SERIAL_XMIT_SIZE - info->xmit_cnt - 1;
	if (ret < 0)
		ret = 0;
	return ret;
}

static int rs_chars_in_buffer(struct tty_struct *tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
				
	if (serial_paranoia_check(info, tty->device, "rs_chars_in_buffer"))
		return 0;
	return info->xmit_cnt;
}

static void rs_flush_buffer(struct tty_struct *tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
				
	if (serial_paranoia_check(info, tty->device, "rs_flush_buffer"))
		return;
	cli();
	info->xmit_cnt = info->xmit_head = info->xmit_tail = 0;
	sti();
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*
 * ------------------------------------------------------------
 * rs_throttle()
 * 
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void rs_throttle(struct tty_struct * tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("throttle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "rs_throttle"))
		return;
	
	if (I_IXOFF(tty)) {
		save_flags(flags); cli();
		info->x_char = STOP_CHAR(tty);
		if (!info->tx_active)
			transmit_chars(info);
		restore_flags(flags);
	}

	if (C_CRTSCTS(tty)) {
		/*
		 * Here we want to turn off the RTS line.  On Macintoshes,
		 * we only get the DTR line, which goes to both DTR and
		 * RTS on the modem.  RTS doesn't go out to the serial
		 * port socket.  So you should make sure your modem is
		 * set to ignore DTR if you're using CRTSCTS.
		 */
		save_flags(flags); cli();
		info->curregs[5] &= ~(DTR | RTS);
		info->pendregs[5] &= ~(DTR | RTS);
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
		restore_flags(flags);
	}
}

static void rs_unthrottle(struct tty_struct * tty)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;
#ifdef SERIAL_DEBUG_THROTTLE
	char	buf[64];
	
	printk("unthrottle %s: %d....\n", _tty_name(tty, buf),
	       tty->ldisc.chars_in_buffer(tty));
#endif

	if (serial_paranoia_check(info, tty->device, "rs_unthrottle"))
		return;
	
	if (I_IXOFF(tty)) {
		save_flags(flags); cli();
		if (info->x_char)
			info->x_char = 0;
		else {
			info->x_char = START_CHAR(tty);
			if (!info->tx_active)
				transmit_chars(info);
		}
		restore_flags(flags);
	}

	if (C_CRTSCTS(tty)) {
		/* Assert RTS and DTR lines */
		save_flags(flags); cli();
		info->curregs[5] |= DTR | RTS;
		info->pendregs[5] |= DTR | RTS;
		write_zsreg(info->zs_channel, 5, info->curregs[5]);
		restore_flags(flags);
	}
}

/*
 * ------------------------------------------------------------
 * rs_ioctl() and friends
 * ------------------------------------------------------------
 */

static int get_serial_info(struct mac_serial * info,
			   struct serial_struct * retinfo)
{
	struct serial_struct tmp;
  
	if (!retinfo)
		return -EFAULT;
	memset(&tmp, 0, sizeof(tmp));
	tmp.type = info->type;
	tmp.line = info->line;
	tmp.port = info->port;
	tmp.irq = info->irq;
	tmp.flags = info->flags;
	tmp.baud_base = info->baud_base;
	tmp.close_delay = info->close_delay;
	tmp.closing_wait = info->closing_wait;
	tmp.custom_divisor = info->custom_divisor;
	return copy_to_user(retinfo,&tmp,sizeof(*retinfo));
}

static int set_serial_info(struct mac_serial * info,
			   struct serial_struct * new_info)
{
	struct serial_struct new_serial;
	struct mac_serial old_info;
	int 			retval = 0;

	if (!new_info)
		return -EFAULT;
	copy_from_user(&new_serial,new_info,sizeof(new_serial));
	old_info = *info;

	if (!suser()) {
		if ((new_serial.baud_base != info->baud_base) ||
		    (new_serial.type != info->type) ||
		    (new_serial.close_delay != info->close_delay) ||
		    ((new_serial.flags & ~ZILOG_USR_MASK) !=
		     (info->flags & ~ZILOG_USR_MASK)))
			return -EPERM;
		info->flags = ((info->flags & ~ZILOG_USR_MASK) |
			       (new_serial.flags & ZILOG_USR_MASK));
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
	info->flags = ((info->flags & ~ZILOG_FLAGS) |
			(new_serial.flags & ZILOG_FLAGS));
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
static int get_lsr_info(struct mac_serial * info, unsigned int *value)
{
	unsigned char status;

	cli();
	status = read_zsreg(info->zs_channel, 0);
	sti();
	put_user(status,value);
	return 0;
}

static int get_modem_info(struct mac_serial *info, unsigned int *value)
{
	unsigned char control, status;
	unsigned int result;

	cli();
	control = info->curregs[5];
	status = read_zsreg(info->zs_channel, 0);
	sti();
	result =  ((control & RTS) ? TIOCM_RTS: 0)
		| ((control & DTR) ? TIOCM_DTR: 0)
		| ((status  & DCD) ? TIOCM_CAR: 0)
		| ((status  & CTS) ? 0: TIOCM_CTS);
	put_user(result,value);
	return 0;
}

static int set_modem_info(struct mac_serial *info, unsigned int cmd,
			  unsigned int *value)
{
	int error;
	unsigned int arg, bits;

	error = verify_area(VERIFY_READ, value, sizeof(int));
	if (error)
		return error;
	get_user(arg, value);
	bits = (arg & TIOCM_RTS? RTS: 0) + (arg & TIOCM_DTR? DTR: 0);
	cli();
	switch (cmd) {
	case TIOCMBIS:
		info->curregs[5] |= bits;
		break;
	case TIOCMBIC:
		info->curregs[5] &= ~bits;
		break;
	case TIOCMSET:
		info->curregs[5] = (info->curregs[5] & ~(DTR | RTS)) | bits;
		break;
	default:
		sti();
		return -EINVAL;
	}
	info->pendregs[5] = info->curregs[5];
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	sti();
	return 0;
}

/*
 * This routine sends a break character out the serial port.
 */
static void send_break(	struct mac_serial * info, int duration)
{
	if (!info->port)
		return;
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + duration;
	cli();
	info->curregs[5] |= SND_BRK;
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	schedule();
	info->curregs[5] &= ~SND_BRK;
	write_zsreg(info->zs_channel, 5, info->curregs[5]);
	sti();
}

static int rs_ioctl(struct tty_struct *tty, struct file * file,
		    unsigned int cmd, unsigned long arg)
{
	int error;
	struct mac_serial * info = (struct mac_serial *)tty->driver_data;
	int retval;

	if (serial_paranoia_check(info, tty->device, "rs_ioctl"))
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
			return put_user(C_CLOCAL(tty) ? 1 : 0, (int *) arg);
		case TIOCSSOFTCAR:
			error = get_user(arg, (int *) arg);
			if (error)
				return error;
			tty->termios->c_cflag =
				((tty->termios->c_cflag & ~CLOCAL) |
				 (arg ? CLOCAL : 0));
			return 0;
		case TIOCMGET:
			error = verify_area(VERIFY_WRITE, (void *) arg,
				sizeof(unsigned int));
			if (error)
				return error;
			return get_modem_info(info, (unsigned int *) arg);
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			return set_modem_info(info, cmd, (unsigned int *) arg);
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
						sizeof(struct mac_serial));
			if (error)
				return error;
			copy_from_user((struct mac_serial *) arg,
				       info, sizeof(struct mac_serial));
			return 0;
			
		default:
			return -ENOIOCTLCMD;
		}
	return 0;
}

static void rs_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	struct mac_serial *info = (struct mac_serial *)tty->driver_data;
	int was_stopped;

	if (tty->termios->c_cflag == old_termios->c_cflag)
		return;
	was_stopped = info->tx_stopped;

	change_speed(info);

	if (was_stopped && !info->tx_stopped)
		rs_start(tty);
}

/*
 * ------------------------------------------------------------
 * rs_close()
 * 
 * This routine is called when the serial port gets closed.
 * Wait for the last remaining data to be sent.
 * ------------------------------------------------------------
 */
static void rs_close(struct tty_struct *tty, struct file * filp)
{
	struct mac_serial * info = (struct mac_serial *)tty->driver_data;
	unsigned long flags;
	unsigned long timeout;

	if (!info || serial_paranoia_check(info, tty->device, "rs_close"))
		return;
	
	save_flags(flags); cli();
	
	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
		return;
	}
	
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_close ttys%d, count = %d\n", info->line, info->count);
#endif
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk("rs_close: bad serial port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk("rs_close: bad serial port count for ttys%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		restore_flags(flags);
		return;
	}
	info->flags |= ZILOG_CLOSING;
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (info->flags & ZILOG_NORMAL_ACTIVE)
		info->normal_termios = *tty->termios;
	if (info->flags & ZILOG_CALLOUT_ACTIVE)
		info->callout_termios = *tty->termios;
	/*
	 * Now we wait for the transmit buffer to clear; and we notify 
	 * the line discipline to only process XON/XOFF characters.
	 */
	tty->closing = 1;
	if (info->closing_wait != ZILOG_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, info->closing_wait);
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receiver and receive interrupts.
	 */
	/** if (!info->iscons) ... **/
	info->curregs[3] &= ~RxENABLE;
	info->pendregs[3] = info->curregs[3];
	write_zsreg(info->zs_channel, 3, info->curregs[3]);
	info->curregs[1] &= ~(0x18);	/* disable any rx ints */
	info->pendregs[1] = info->curregs[1];
	write_zsreg(info->zs_channel, 1, info->curregs[1]);
	ZS_CLEARFIFO(info->zs_channel);
	if (info->flags & ZILOG_INITIALIZED) {
		/*
		 * Before we drop DTR, make sure the SCC transmitter
		 * has completely drained.
		 */
		timeout = jiffies+HZ;
		while ((read_zsreg(info->zs_channel, 1) & ALL_SNT) == 0) {
			current->state = TASK_INTERRUPTIBLE;
			current->timeout = jiffies + info->timeout;
			schedule();
			if (jiffies > timeout)
				break;
		}
	}

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
			current->timeout = jiffies + info->close_delay;
			schedule();
		}
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ZILOG_NORMAL_ACTIVE|ZILOG_CALLOUT_ACTIVE|
			 ZILOG_CLOSING);
	wake_up_interruptible(&info->close_wait);
	restore_flags(flags);
}

/*
 * rs_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
void rs_hangup(struct tty_struct *tty)
{
	struct mac_serial * info = (struct mac_serial *)tty->driver_data;

	if (serial_paranoia_check(info, tty->device, "rs_hangup"))
		return;

	rs_flush_buffer(tty);
	shutdown(info);
	info->event = 0;
	info->count = 0;
	info->flags &= ~(ZILOG_NORMAL_ACTIVE|ZILOG_CALLOUT_ACTIVE);
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/*
 * ------------------------------------------------------------
 * rs_open() and friends
 * ------------------------------------------------------------
 */
static int block_til_ready(struct tty_struct *tty, struct file * filp,
			   struct mac_serial *info)
{
	struct wait_queue wait = { current, NULL };
	int		retval;
	int		do_clocal = 0;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (info->flags & ZILOG_CLOSING) {
		interruptible_sleep_on(&info->close_wait);
#ifdef SERIAL_DO_RESTART
		if (info->flags & ZILOG_HUP_NOTIFY)
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
		if (info->flags & ZILOG_NORMAL_ACTIVE)
			return -EBUSY;
		if ((info->flags & ZILOG_CALLOUT_ACTIVE) &&
		    (info->flags & ZILOG_SESSION_LOCKOUT) &&
		    (info->session != current->session))
		    return -EBUSY;
		if ((info->flags & ZILOG_CALLOUT_ACTIVE) &&
		    (info->flags & ZILOG_PGRP_LOCKOUT) &&
		    (info->pgrp != current->pgrp))
		    return -EBUSY;
		info->flags |= ZILOG_CALLOUT_ACTIVE;
		return 0;
	}
	
	/*
	 * If non-blocking mode is set, or the port is not enabled,
	 * then make the check up front and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		if (info->flags & ZILOG_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ZILOG_NORMAL_ACTIVE;
		return 0;
	}

	if (info->flags & ZILOG_CALLOUT_ACTIVE) {
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
	 * rs_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef SERIAL_DEBUG_OPEN
	printk("block_til_ready before block: ttys%d, count = %d\n",
	       info->line, info->count);
#endif
	cli();
	if (!tty_hung_up_p(filp)) 
		info->count--;
	sti();
	info->blocked_open++;
	while (1) {
		cli();
		if (!(info->flags & ZILOG_CALLOUT_ACTIVE) &&
		    (tty->termios->c_cflag & CBAUD))
			zs_rtsdtr(info, 1);
		sti();
		current->state = TASK_INTERRUPTIBLE;
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ZILOG_INITIALIZED)) {
#ifdef SERIAL_DO_RESTART
			if (info->flags & ZILOG_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;	
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & ZILOG_CALLOUT_ACTIVE) &&
		    !(info->flags & ZILOG_CLOSING) &&
		    (do_clocal || (read_zsreg(info->zs_channel, 0) & DCD)))
			break;
		if (current->signal & ~current->blocked) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef SERIAL_DEBUG_OPEN
		printk("block_til_ready blocking: ttys%d, count = %d\n",
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
	printk("block_til_ready after blocking: ttys%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ZILOG_NORMAL_ACTIVE;
	return 0;
}	

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its ZILOG structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
int rs_open(struct tty_struct *tty, struct file * filp)
{
	struct mac_serial	*info;
	int 			retval, line;

	line = MINOR(tty->device) - tty->driver.minor_start;
	if ((line < 0) || (line >= zs_channels_found))
		return -ENODEV;
	info = zs_soft + line;

	/* Is the kgdb running over this line? */
	if (info->kgdb_channel)
		return -ENODEV;
	if (serial_paranoia_check(info, tty->device, "rs_open"))
		return -ENODEV;
#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open %s%d, count = %d\n", tty->driver.name, info->line,
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
		printk("rs_open returning after block_til_ready with %d\n",
		       retval);
#endif
		return retval;
	}

	if ((info->count == 1) && (info->flags & ZILOG_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == SERIAL_TYPE_NORMAL)
			*tty->termios = info->normal_termios;
		else 
			*tty->termios = info->callout_termios;
		change_speed(info);
	}

	info->session = current->session;
	info->pgrp = current->pgrp;

#ifdef SERIAL_DEBUG_OPEN
	printk("rs_open ttys%d successful...", info->line);
#endif
	return 0;
}

/* Finally, routines used to initialize the serial driver. */

static void show_serial_version(void)
{
	printk("PowerMac Z8530 serial driver version 1.00\n");
}

/* Ask the PROM how many Z8530s we have and initialize their zs_channels */
static void
probe_sccs()
{
	struct device_node *dev, *ch;
	struct mac_serial **pp;
	int n;

	n = 0;
	pp = &zs_chain;
	for (dev = find_devices("escc"); dev != 0; dev = dev->next) {
		if (n >= NUM_CHANNELS) {
			printk("Sorry, can't use %s: no more channels\n",
			       dev->full_name);
			continue;
		}
		for (ch = dev->child; ch != 0; ch = ch->sibling) {
			if (ch->n_addrs < 1 || ch ->n_intrs < 1) {
				printk("Can't use %s: %d addrs %d intrs\n",
				      ch->full_name, ch->n_addrs, ch->n_intrs);
				continue;
			}
			zs_channels[n].control = (volatile unsigned char *)
				ch->addrs[0].address;
			zs_channels[n].data = zs_channels[n].control
				+ ch->addrs[0].size / 2;
			zs_soft[n].zs_channel = &zs_channels[n];
			zs_soft[n].irq = ch->intrs[0];
			if (request_irq(ch->intrs[0], rs_interrupt, 0,
					"SCC", &zs_soft[n]))
				panic("macserial: can't get irq %d",
				      ch->intrs[0]);
			/* XXX this assumes the prom puts chan A before B */
			if (n & 1)
				zs_soft[n].zs_chan_a = &zs_channels[n-1];
			else
				zs_soft[n].zs_chan_a = &zs_channels[n];

			*pp = &zs_soft[n];
			pp = &zs_soft[n].zs_next;
			++n;
		}
	}
	*pp = 0;
	zs_channels_found = n;
}

/* rs_init inits the driver */
int rs_init(void)
{
	int channel, i;
	unsigned long flags;
	struct mac_serial *info;

	/* Setup base handler, and timer table. */
	init_bh(SERIAL_BH, do_serial_bh);
	timer_table[RS_TIMER].fn = rs_timer;
	timer_table[RS_TIMER].expires = 0;

	/* Find out how many Z8530 SCCs we have */
	if (zs_chain == 0)
		probe_sccs();

	show_serial_version();

	/* Initialize the tty_driver structure */
	/* Not all of this is exactly right for us. */

	memset(&serial_driver, 0, sizeof(struct tty_driver));
	serial_driver.magic = TTY_DRIVER_MAGIC;
	serial_driver.name = "ttyS";
	serial_driver.major = TTY_MAJOR;
	serial_driver.minor_start = 64;
	serial_driver.num = zs_channels_found;
	serial_driver.type = TTY_DRIVER_TYPE_SERIAL;
	serial_driver.subtype = SERIAL_TYPE_NORMAL;
	serial_driver.init_termios = tty_std_termios;

	serial_driver.init_termios.c_cflag =
		B38400 | CS8 | CREAD | HUPCL | CLOCAL;
	serial_driver.flags = TTY_DRIVER_REAL_RAW;
	serial_driver.refcount = &serial_refcount;
	serial_driver.table = serial_table;
	serial_driver.termios = serial_termios;
	serial_driver.termios_locked = serial_termios_locked;

	serial_driver.open = rs_open;
	serial_driver.close = rs_close;
	serial_driver.write = rs_write;
	serial_driver.flush_chars = rs_flush_chars;
	serial_driver.write_room = rs_write_room;
	serial_driver.chars_in_buffer = rs_chars_in_buffer;
	serial_driver.flush_buffer = rs_flush_buffer;
	serial_driver.ioctl = rs_ioctl;
	serial_driver.throttle = rs_throttle;
	serial_driver.unthrottle = rs_unthrottle;
	serial_driver.set_termios = rs_set_termios;
	serial_driver.stop = rs_stop;
	serial_driver.start = rs_start;
	serial_driver.hangup = rs_hangup;

	/*
	 * The callout device is just like normal device except for
	 * major number and the subtype code.
	 */
	callout_driver = serial_driver;
	callout_driver.name = "cua";
	callout_driver.major = TTYAUX_MAJOR;
	callout_driver.subtype = SERIAL_TYPE_CALLOUT;

	if (tty_register_driver(&serial_driver))
		panic("Couldn't register serial driver\n");
	if (tty_register_driver(&callout_driver))
		panic("Couldn't register callout driver\n");

	save_flags(flags); cli();

	for (channel = 0; channel < zs_channels_found; ++channel) {
		zs_soft[channel].clk_divisor = 16;
		zs_soft[channel].zs_baud = get_zsbaud(&zs_soft[channel]);

		/* If console serial line, then enable interrupts. */
		if (zs_soft[channel].is_cons) {
			write_zsreg(zs_soft[channel].zs_channel, R1,
				    (EXT_INT_ENAB | INT_ALL_Rx | TxINT_ENAB));
			write_zsreg(zs_soft[channel].zs_channel, R9,
				    (NV | MIE));
		}
		/* If this is the kgdb line, enable interrupts because we
		 * now want to receive the 'control-c' character from the
		 * client attached to us asynchronously.
		 */
		if (zs_soft[channel].kgdb_channel)
			kgdb_chaninit(&zs_soft[channel], 1,
				      zs_soft[channel].zs_baud);
	}

	for (info = zs_chain, i = 0; info; info = info->zs_next, i++)
	{
		info->magic = SERIAL_MAGIC;
		info->port = (int) info->zs_channel->control;
		info->line = i;
		info->tty = 0;
		info->custom_divisor = 16;
		info->close_delay = 50;
		info->closing_wait = 3000;
		info->x_char = 0;
		info->event = 0;
		info->count = 0;
		info->blocked_open = 0;
		info->tqueue.routine = do_softint;
		info->tqueue.data = info;
		info->tqueue_hangup.routine = do_serial_hangup;
		info->tqueue_hangup.data = info;
		info->callout_termios =callout_driver.init_termios;
		info->normal_termios = serial_driver.init_termios;
		info->open_wait = 0;
		info->close_wait = 0;
		printk("tty%02d at 0x%08x (irq = %d)", info->line, 
		       info->port, info->irq);
		printk(" is a Z8530 ESCC\n");
	}

	restore_flags(flags);

	return 0;
}

/*
 * register_serial and unregister_serial allows for serial ports to be
 * configured at run-time, to support PCMCIA modems.
 */
/* PowerMac: Unused at this time, just here to make things link. */
int register_serial(struct serial_struct *req)
{
	return -1;
}

void unregister_serial(int line)
{
	return;
}

extern void register_console(void (*proc)(const char *));

/*
 * Initialization values for when a channel is used for
 * a serial console.
 */
static unsigned char cons_init_regs[16] = {
	0, 0, 0,		/* write 0, 1, 2 */
	(Rx8 | RxENABLE),	/* write 3 */
	(X16CLK | SB1),		/* write 4 */
	(Tx8 | TxENAB | RTS),	/* write 5 */
	0, 0, 0,		/* write 6, 7, 8 */
	0,			/* write 9 */
	(NRZ),			/* write 10 */
	(TCBR | RCBR),		/* write 11 */
	1, 0,			/* 38400 baud divisor, write 12 + 13 */
	(BRENABL),		/* write 14 */
	0			/* write 15 */
};

/*
 * Hooks for running a serial console.  con_init() calls this if the
 * console is being run over one of the serial ports.
 * 'channel' is decoded as 1=modem, 2=printer.
 */
void
rs_cons_hook(int chip, int out, int channel)
{
	int brg;

	if (!out)
		return;
	if (zs_consinfo != 0) {
		printk("rs_cons_hook called twice?\n");
		return;
	}
	if (zs_chain == 0)
		probe_sccs();
	--channel;
	if (channel < 0 || channel >= zs_channels_found) {
		printk("rs_cons_hook: channel = %d?\n", channel);
		return;
	}

	zs_cons_chan = channel;
	zs_consinfo = &zs_soft[channel];
	zs_conschan = zs_consinfo->zs_channel;
	zs_consinfo->clk_divisor = 16;
	zs_consinfo->zs_baud = 38400;
	zs_consinfo->is_cons = 1;

	memcpy(zs_consinfo->curregs, cons_init_regs, sizeof(cons_init_regs));
	brg = BPS_TO_BRG(zs_consinfo->zs_baud, ZS_CLOCK/16);
	zs_consinfo->curregs[R12] = brg;
	zs_consinfo->curregs[R13] = brg >> 8;
	load_zsregs(zs_conschan, zs_consinfo->curregs);

	register_console(zs_console_print);
	printk("zs%d: console I/O\n", channel);
}

/* This is called at boot time to prime the kgdb serial debugging
 * serial line.  The 'tty_num' argument is 0 for /dev/ttyS0 and 1
 * for /dev/ttyS1 which is determined in setup_arch() from the
 * boot command line flags.
 */
void
rs_kgdb_hook(int tty_num)
{
	if (zs_chain == 0)
		probe_sccs();
	zs_kgdbchan = zs_soft[tty_num].zs_channel;
	zs_soft[tty_num].clk_divisor = 16;
	zs_soft[tty_num].zs_baud = get_zsbaud(&zs_soft[tty_num]);
	zs_soft[tty_num].kgdb_channel = 1;     /* This runs kgdb */
	zs_soft[tty_num ^ 1].kgdb_channel = 0; /* This does not */
	/* Turn on transmitter/receiver at 8-bits/char */
	kgdb_chaninit(&zs_soft[tty_num], 0, 9600);
	ZS_CLEARERR(zs_kgdbchan);
	ZS_CLEARFIFO(zs_kgdbchan);
}
