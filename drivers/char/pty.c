/*
 *  linux/drivers/char/pty.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/major.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>

struct pty_struct {
	int	magic;
	struct wait_queue * open_wait;
};

#define PTY_MAGIC 0x5001

static struct tty_driver pty_driver, pty_slave_driver;
static struct tty_driver old_pty_driver, old_pty_slave_driver;
static int pty_refcount;

static struct tty_struct *pty_table[NR_PTYS];
static struct termios *pty_termios[NR_PTYS];
static struct termios *pty_termios_locked[NR_PTYS];
static struct tty_struct *ttyp_table[NR_PTYS];
static struct termios *ttyp_termios[NR_PTYS];
static struct termios *ttyp_termios_locked[NR_PTYS];
static struct pty_struct pty_state[NR_PTYS];

#define MIN(a,b)	((a) < (b) ? (a) : (b))

static void pty_close(struct tty_struct * tty, struct file * filp)
{
	if (!tty)
		return;
	if (tty->driver.subtype == PTY_TYPE_MASTER) {
		if (tty->count > 1)
			printk("master pty_close: count = %d!!\n", tty->count);
	} else {
		if (tty->count > 2)
			return;
	}
	wake_up_interruptible(&tty->read_wait);
	wake_up_interruptible(&tty->write_wait);
	tty->packet = 0;
	if (!tty->link)
		return;
	tty->link->packet = 0;
	wake_up_interruptible(&tty->link->read_wait);
	wake_up_interruptible(&tty->link->write_wait);
	set_bit(TTY_OTHER_CLOSED, &tty->link->flags);
	if (tty->driver.subtype == PTY_TYPE_MASTER) {
		tty_hangup(tty->link);
		set_bit(TTY_OTHER_CLOSED, &tty->flags);
	}
}

/*
 * The unthrottle routine is called by the line discipline to signal
 * that it can receive more characters.  For PTY's, the TTY_THROTTLED
 * flag is always set, to force the line discipline to always call the
 * unthrottle routine when there are fewer than TTY_THRESHOLD_UNTHROTTLE 
 * characters in the queue.  This is necessary since each time this
 * happens, we need to wake up any sleeping processes that could be
 * (1) trying to send data to the pty, or (2) waiting in wait_until_sent()
 * for the pty buffer to be drained.
 */
static void pty_unthrottle(struct tty_struct * tty)
{
	struct tty_struct *o_tty = tty->link;

	if (!o_tty)
		return;

	if ((o_tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    o_tty->ldisc.write_wakeup)
		(o_tty->ldisc.write_wakeup)(o_tty);
	wake_up_interruptible(&o_tty->write_wait);
	set_bit(TTY_THROTTLED, &tty->flags);
}

/*
 * WSH 05/24/97: modified to 
 *   (1) use space in tty->flip instead of a shared temp buffer
 *	 The flip buffers aren't being used for a pty, so there's lots
 *	 of space available.  The buffer is protected by a per-pty
 *	 semaphore that should almost never come under contention.
 *   (2) avoid redundant copying for cases where count >> receive_room
 * N.B. Calls from user space may now return an error code instead of
 * a count.
 */
static int pty_write(struct tty_struct * tty, int from_user,
		       const unsigned char *buf, int count)
{
	struct tty_struct *to = tty->link;
	int	c=0, n;
	char	*temp_buffer;

	if (!to || tty->stopped)
		return 0;

	if (from_user) {
		down(&tty->flip.pty_sem);
		temp_buffer = &tty->flip.char_buf[0];
		while (count > 0) {
			/* check space so we don't copy needlessly */ 
			n = MIN(count, to->ldisc.receive_room(to));
			if (!n) break;

			n  = MIN(n, PTY_BUF_SIZE);
			n -= copy_from_user(temp_buffer, buf, n);
			if (!n) {
				if (!c)
					c = -EFAULT;
				break;
			}

			/* check again in case the buffer filled up */
			n = MIN(n, to->ldisc.receive_room(to));
			if (!n) break;
			buf   += n; 
			c     += n;
			count -= n;
			to->ldisc.receive_buf(to, temp_buffer, 0, n);
		}
		up(&tty->flip.pty_sem);
	} else {
		c = MIN(count, to->ldisc.receive_room(to));
		to->ldisc.receive_buf(to, buf, 0, c);
	}
	
	return c;
}

static int pty_write_room(struct tty_struct *tty)
{
	struct tty_struct *to = tty->link;

	if (!to || tty->stopped)
		return 0;

	return to->ldisc.receive_room(to);
}

/*
 *	WSH 05/24/97:  Modified for asymmetric MASTER/SLAVE behavior
 *	The chars_in_buffer() value is used by the ldisc select() function 
 *	to hold off writing when chars_in_buffer > WAKEUP_CHARS (== 256).
 *	The pty driver chars_in_buffer() Master/Slave must behave differently:
 *
 *      The Master side needs to allow typed-ahead commands to accumulate
 *      while being canonicalized, so we report "our buffer" as empty until
 *	some threshold is reached, and then report the count. (Any count >
 *	WAKEUP_CHARS is regarded by select() as "full".)  To avoid deadlock 
 *	the count returned must be 0 if no canonical data is available to be 
 *	read. (The N_TTY ldisc.chars_in_buffer now knows this.)
 *  
 *	The Slave side passes all characters in raw mode to the Master side's
 *	buffer where they can be read immediately, so in this case we can
 *	return the true count in the buffer.
 */
static int pty_chars_in_buffer(struct tty_struct *tty)
{
	struct tty_struct *to = tty->link;
	int count;

	if (!to || !to->ldisc.chars_in_buffer)
		return 0;

	/* The ldisc must report 0 if no characters available to be read */
	count = to->ldisc.chars_in_buffer(to);

	if (tty->driver.subtype == PTY_TYPE_SLAVE) return count;

	/* Master side driver ... if the other side's read buffer is less than 
	 * half full, return 0 to allow writers to proceed; otherwise return
	 * the count.  This leaves a comfortable margin to avoid overflow, 
	 * and still allows half a buffer's worth of typed-ahead commands.
	 */
	return ((count < N_TTY_BUF_SIZE/2) ? 0 : count);
}

static void pty_flush_buffer(struct tty_struct *tty)
{
	struct tty_struct *to = tty->link;
	
	if (!to)
		return;
	
	if (to->ldisc.flush_buffer)
		to->ldisc.flush_buffer(to);
	
	if (to->packet) {
		tty->ctrl_status |= TIOCPKT_FLUSHWRITE;
		wake_up_interruptible(&to->read_wait);
	}
}

static int pty_open(struct tty_struct *tty, struct file * filp)
{
	int	retval;
	int	line;
	struct	pty_struct *pty;

	retval = -ENODEV;
	if (!tty || !tty->link)
		goto out;
	line = MINOR(tty->device) - tty->driver.minor_start;
	if ((line < 0) || (line >= NR_PTYS))
		goto out;
	pty = pty_state + line;
	tty->driver_data = pty;

	retval = -EIO;
	if (test_bit(TTY_OTHER_CLOSED, &tty->flags))
		goto out;
	if (tty->link->count != 1)
		goto out;

	clear_bit(TTY_OTHER_CLOSED, &tty->link->flags);
	wake_up_interruptible(&pty->open_wait);
	set_bit(TTY_THROTTLED, &tty->flags);
	retval = 0;
out:
	return retval;
}

static void pty_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
        tty->termios->c_cflag &= ~(CSIZE | PARENB);
        tty->termios->c_cflag |= (CS8 | CREAD);
}

__initfunc(int pty_init(void))
{
	memset(&pty_state, 0, sizeof(pty_state));
	memset(&pty_driver, 0, sizeof(struct tty_driver));
	pty_driver.magic = TTY_DRIVER_MAGIC;
	pty_driver.driver_name = "pty_master";
	pty_driver.name = "pty";
	pty_driver.major = PTY_MASTER_MAJOR;
	pty_driver.minor_start = 0;
	pty_driver.num = NR_PTYS;
	pty_driver.type = TTY_DRIVER_TYPE_PTY;
	pty_driver.subtype = PTY_TYPE_MASTER;
	pty_driver.init_termios = tty_std_termios;
	pty_driver.init_termios.c_iflag = 0;
	pty_driver.init_termios.c_oflag = 0;
	pty_driver.init_termios.c_cflag = B38400 | CS8 | CREAD;
	pty_driver.init_termios.c_lflag = 0;
	pty_driver.flags = TTY_DRIVER_RESET_TERMIOS | TTY_DRIVER_REAL_RAW;
	pty_driver.refcount = &pty_refcount;
	pty_driver.table = pty_table;
	pty_driver.termios = pty_termios;
	pty_driver.termios_locked = pty_termios_locked;
	pty_driver.other = &pty_slave_driver;

	pty_driver.open = pty_open;
	pty_driver.close = pty_close;
	pty_driver.write = pty_write;
	pty_driver.write_room = pty_write_room;
	pty_driver.flush_buffer = pty_flush_buffer;
	pty_driver.chars_in_buffer = pty_chars_in_buffer;
	pty_driver.unthrottle = pty_unthrottle;
	pty_driver.set_termios = pty_set_termios;

	pty_slave_driver = pty_driver;
	pty_slave_driver.driver_name = "pty_slave";
	pty_slave_driver.proc_entry = 0;
	pty_slave_driver.name = "ttyp";
	pty_slave_driver.subtype = PTY_TYPE_SLAVE;
	pty_slave_driver.major = PTY_SLAVE_MAJOR;
	pty_slave_driver.minor_start = 0;
	pty_slave_driver.init_termios = tty_std_termios;
	pty_slave_driver.init_termios.c_cflag = B38400 | CS8 | CREAD;
	pty_slave_driver.table = ttyp_table;
	pty_slave_driver.termios = ttyp_termios;
	pty_slave_driver.termios_locked = ttyp_termios_locked;
	pty_slave_driver.other = &pty_driver;

	old_pty_driver = pty_driver;
	old_pty_driver.driver_name = "compat_pty_master";
	old_pty_driver.proc_entry = 0;
	old_pty_driver.major = TTY_MAJOR;
	old_pty_driver.minor_start = 128;
	old_pty_driver.num = (NR_PTYS > 64) ? 64 : NR_PTYS;
	old_pty_driver.other = &old_pty_slave_driver;
	
	old_pty_slave_driver = pty_slave_driver;
	old_pty_slave_driver.driver_name = "compat_pty_slave";
	old_pty_slave_driver.proc_entry = 0;
	old_pty_slave_driver.major = TTY_MAJOR;
	old_pty_slave_driver.minor_start = 192;
	old_pty_slave_driver.num = (NR_PTYS > 64) ? 64 : NR_PTYS;
	old_pty_slave_driver.other = &old_pty_driver;

	if (tty_register_driver(&pty_driver))
		panic("Couldn't register pty driver");
	if (tty_register_driver(&pty_slave_driver))
		panic("Couldn't register pty slave driver");
	if (tty_register_driver(&old_pty_driver))
		panic("Couldn't register compat pty driver");
	if (tty_register_driver(&old_pty_slave_driver))
		panic("Couldn't register compat pty slave driver");
	
	return 0;
}
