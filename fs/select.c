/*
 * This file contains the procedures for the handling of select and poll
 *
 * Created for Linux based loosely upon Mathius Lattner's minix
 * patches by Peter MacDonald. Heavily edited by Linus.
 *
 *  4 February 1994
 *     COFF/ELF binary emulation. If the process has the STICKY_TIMEOUTS
 *     flag set in its personality we do *not* modify the given timeout
 *     parameter to reflect time remaining.
 */

#include <linux/types.h>
#include <linux/time.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/personality.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/poll.h>

#define ROUND_UP(x,y) (((x)+(y)-1)/(y))
#define DEFAULT_POLLMASK (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM)

/*
 * Ok, Peter made a complicated, but straightforward multiple_wait() function.
 * I have rewritten this, taking some shortcuts: This code may not be easy to
 * follow, but it should be free of race-conditions, and it's practical. If you
 * understand what I'm doing here, then you understand how the linux
 * sleep/wakeup mechanism works.
 *
 * Two very simple procedures, poll_wait() and free_wait() make all the work.
 * poll_wait() is an inline-function defined in <linux/sched.h>, as all select/poll
 * functions have to call it to add an entry to the poll table.
 */

/*
 * I rewrote this again to make the poll_table size variable, take some
 * more shortcuts, improve responsiveness, and remove another race that
 * Linus noticed.  -- jrs
 */

static void free_wait(poll_table * p)
{
	struct poll_table_entry * entry = p->entry + p->nr;

	while (p->nr > 0) {
		p->nr--;
		entry--;
		remove_wait_queue(entry->wait_address,&entry->wait);
	}
}

/*
 * For the kernel fd_set we use a fixed set-size for allocation purposes.
 * This set-size doesn't necessarily bear any relation to the size the user
 * uses, but should preferably obviously be larger than any possible user
 * size (NR_OPEN bits).
 *
 * We need 6 bitmaps (in/out/ex for both incoming and outgoing), and we
 * allocate one page for all the bitmaps. Thus we have 8*PAGE_SIZE bits,
 * to be divided by 6. And we'd better make sure we round to a full
 * long-word (in fact, we'll round to 64 bytes).
 */
#define KFDS_64BLOCK ((PAGE_SIZE/(6*64))*64)
#define KFDS_NR (KFDS_64BLOCK*8 > NR_OPEN ? NR_OPEN : KFDS_64BLOCK*8)
typedef unsigned long kernel_fd_set[KFDS_NR/(8*sizeof(unsigned long))];

typedef struct {
	kernel_fd_set in, out, ex;
	kernel_fd_set res_in, res_out, res_ex;
} fd_set_buffer;

#define __IN(in)	(in)
#define __OUT(in)	(in + sizeof(kernel_fd_set)/sizeof(unsigned long))
#define __EX(in)	(in + 2*sizeof(kernel_fd_set)/sizeof(unsigned long))
#define __RES_IN(in)	(in + 3*sizeof(kernel_fd_set)/sizeof(unsigned long))
#define __RES_OUT(in)	(in + 4*sizeof(kernel_fd_set)/sizeof(unsigned long))
#define __RES_EX(in)	(in + 5*sizeof(kernel_fd_set)/sizeof(unsigned long))

#define BITS(in)	(*__IN(in)|*__OUT(in)|*__EX(in))

static int max_select_fd(unsigned long n, fd_set_buffer *fds)
{
	unsigned long *open_fds, *in;
	unsigned long set;
	int max;

	/* handle last in-complete long-word first */
	set = ~(~0UL << (n & (__NFDBITS-1)));
	n /= __NFDBITS;
	open_fds = current->files->open_fds.fds_bits+n;
	in = fds->in+n;
	max = 0;
	if (set) {
		set &= BITS(in);
		if (set) {
			if (!(set & ~*open_fds))
				goto get_max;
			return -EBADF;
		}
	}
	while (n) {
		in--;
		open_fds--;
		n--;
		set = BITS(in);
		if (!set)
			continue;
		if (set & ~*open_fds)
			return -EBADF;
		if (max)
			continue;
get_max:
		do {
			max++;
			set >>= 1;
		} while (set);
		max += n * __NFDBITS;
	}

	return max;
}

#define BIT(i)		(1UL << ((i)&(__NFDBITS-1)))
#define MEM(i,m)	((m)+(unsigned)(i)/__NFDBITS)
#define ISSET(i,m)	(((i)&*(m)) != 0)
#define SET(i,m)	(*(m) |= (i))

#define POLLIN_SET (POLLRDNORM | POLLRDBAND | POLLIN | POLLHUP | POLLERR)
#define POLLOUT_SET (POLLWRBAND | POLLWRNORM | POLLOUT | POLLERR)
#define POLLEX_SET (POLLPRI)

static int do_select(int n, fd_set_buffer *fds)
{
	int retval;
	poll_table wait_table, *wait;
	struct poll_table_entry *entry;
	int i;

	retval = max_select_fd(n, fds);
	if (retval < 0)
		goto out;
	n = retval;
	retval = -ENOMEM;
	entry = (struct poll_table_entry *) __get_free_page(GFP_KERNEL);
	if (!entry)
		goto out;
	retval = 0;
	wait_table.nr = 0;
	wait_table.entry = entry;
	wait = &wait_table;
	for (;;) {
		struct file ** fd = current->files->fd;
		current->state = TASK_INTERRUPTIBLE;
		for (i = 0 ; i < n ; i++,fd++) {
			unsigned long bit = BIT(i);
			unsigned long *in = MEM(i,fds->in);

			if (bit & BITS(in)) {
				struct file * file = *fd;
				unsigned int mask = POLLNVAL;
				if (file) {
					mask = DEFAULT_POLLMASK;
					if (file->f_op && file->f_op->poll)
						mask = file->f_op->poll(file, wait);
				}
				if ((mask & POLLIN_SET) && ISSET(bit, __IN(in))) {
					SET(bit, __RES_IN(in));
					retval++;
					wait = NULL;
				}
				if ((mask & POLLOUT_SET) && ISSET(bit, __OUT(in))) {
					SET(bit, __RES_OUT(in));
					retval++;
					wait = NULL;
				}
				if ((mask & POLLEX_SET) && ISSET(bit, __EX(in))) {
					SET(bit, __RES_EX(in));
					retval++;
					wait = NULL;
				}
			}
		}
		wait = NULL;
		if (retval || !current->timeout || (current->signal & ~current->blocked))
			break;
		schedule();
	}
	free_wait(&wait_table);
	free_page((unsigned long) entry);
	current->state = TASK_RUNNING;
out:
	return retval;
}

/*
 * We do a VERIFY_WRITE here even though we are only reading this time:
 * we'll write to it eventually..
 *
 * Use "unsigned long" accesses to let user-mode fd_set's be long-aligned.
 */
static int __get_fd_set(unsigned long nr, unsigned long * fs_pointer, unsigned long * fdset)
{
	/* round up nr to nearest "unsigned long" */
	nr = (nr + 8*sizeof(unsigned long)-1) / (8*sizeof(unsigned long));
	if (fs_pointer) {
		int error = verify_area(VERIFY_WRITE,fs_pointer,
					nr*sizeof(unsigned long));
		if (!error) {
			while (nr) {
				__get_user(*fdset, fs_pointer);
				nr--;
				fs_pointer++;
				fdset++;
			}
		}
		return error;
	}
	while (nr) {
		*fdset = 0;
		nr--;
		fdset++;
	}
	return 0;
}

static void __set_fd_set(long nr, unsigned long * fs_pointer, unsigned long * fdset)
{
	if (!fs_pointer)
		return;
	while (nr >= 0) {
		__put_user(*fdset, fs_pointer);
		nr -= 8 * sizeof(unsigned long);
		fdset++;
		fs_pointer++;
	}
}

/* We can do long accesses here, kernel fdsets are always long-aligned */
static inline void __zero_fd_set(long nr, unsigned long * fdset)
{
	while (nr >= 0) {
		*fdset = 0;
		nr -= 8 * sizeof(unsigned long);
		fdset++;
	}
}		

/*
 * Note a few subtleties: we use "long" for the dummy, not int, and we do a
 * subtract by 1 on the nr of file descriptors. The former is better for
 * machines with long > int, and the latter allows us to test the bit count
 * against "zero or positive", which can mostly be just a sign bit test..
 *
 * Unfortunately this scheme falls apart on big endian machines where
 * sizeof(long) > sizeof(int) (ie. V9 Sparc). -DaveM
 */

#define get_fd_set(nr,fsp,fdp) \
__get_fd_set(nr, (unsigned long *) (fsp), (unsigned long *) (fdp))

#define set_fd_set(nr,fsp,fdp) \
__set_fd_set((nr)-1, (unsigned long *) (fsp), (unsigned long *) (fdp))

#define zero_fd_set(nr,fdp) \
__zero_fd_set((nr)-1, (unsigned long *) (fdp))

/*
 * We can actually return ERESTARTSYS instead of EINTR, but I'd
 * like to be certain this leads to no problems. So I return
 * EINTR just for safety.
 *
 * Update: ERESTARTSYS breaks at least the xview clock binary, so
 * I'm trying ERESTARTNOHAND which restart only when you want to.
 */
asmlinkage int sys_select(int n, fd_set *inp, fd_set *outp, fd_set *exp, struct timeval *tvp)
{
	int error = -EINVAL;
	fd_set_buffer *fds;
	unsigned long timeout;

	lock_kernel();
	fds = (fd_set_buffer *) __get_free_page(GFP_KERNEL);
	if (!fds)
		goto out;
	if (n < 0)
		goto out;
	if (n > KFDS_NR)
		n = KFDS_NR;
	if ((error = get_fd_set(n, inp, &fds->in)) ||
	    (error = get_fd_set(n, outp, &fds->out)) ||
	    (error = get_fd_set(n, exp, &fds->ex))) goto out;
	timeout = ~0UL;
	if (tvp) {
		error = verify_area(VERIFY_WRITE, tvp, sizeof(*tvp));
		if (error)
			goto out;
		__get_user(timeout, &tvp->tv_usec);
		timeout = ROUND_UP(timeout,(1000000/HZ));
		{
			unsigned long tmp;
			__get_user(tmp, &tvp->tv_sec);
			timeout += tmp * (unsigned long) HZ;
		}
		if (timeout)
			timeout += jiffies + 1;
	}
	zero_fd_set(n, &fds->res_in);
	zero_fd_set(n, &fds->res_out);
	zero_fd_set(n, &fds->res_ex);
	current->timeout = timeout;
	error = do_select(n, fds);
	timeout = current->timeout - jiffies - 1;
	current->timeout = 0;
	if ((long) timeout < 0)
		timeout = 0;
	if (tvp && !(current->personality & STICKY_TIMEOUTS)) {
		__put_user(timeout/HZ, &tvp->tv_sec);
		timeout %= HZ;
		timeout *= (1000000/HZ);
		__put_user(timeout, &tvp->tv_usec);
	}
	if (error < 0)
		goto out;
	if (!error) {
		error = -ERESTARTNOHAND;
		if (current->signal & ~current->blocked)
			goto out;
		error = 0;
	}
	set_fd_set(n, inp, &fds->res_in);
	set_fd_set(n, outp, &fds->res_out);
	set_fd_set(n, exp, &fds->res_ex);
out:
	free_page((unsigned long) fds);
	unlock_kernel();
	return error;
}

static int do_poll(unsigned int nfds, struct pollfd *fds, poll_table *wait)
{
	int count;
	struct file ** fd = current->files->fd;

	count = 0;
	for (;;) {
		unsigned int j;
		struct pollfd * fdpnt;

		current->state = TASK_INTERRUPTIBLE;
		for (fdpnt = fds, j = 0; j < nfds; j++, fdpnt++) {
			unsigned int i;
			unsigned int mask;
			struct file * file;

			mask = POLLNVAL;
			i = fdpnt->fd;
			if (i < NR_OPEN && (file = fd[i]) != NULL) {
				mask = DEFAULT_POLLMASK;
				if (file->f_op && file->f_op->poll)
					mask = file->f_op->poll(file, wait);
				mask &= fdpnt->events | POLLERR | POLLHUP;
			}
			if (mask) {
				wait = NULL;
				count++;
			}
			fdpnt->revents = mask;
		}

		wait = NULL;
		if (count || !current->timeout || (current->signal & ~current->blocked))
			break;
		schedule();
	}
	current->state = TASK_RUNNING;
	return count;
}

asmlinkage int sys_poll(struct pollfd * ufds, unsigned int nfds, int timeout)
{
	int i, count, fdcount, err;
	struct pollfd * fds, *fds1;
	poll_table wait_table;
	struct poll_table_entry *entry;

	lock_kernel();
	err = -ENOMEM;
	entry = (struct poll_table_entry *) __get_free_page(GFP_KERNEL);
	if (!entry)
		goto out;
	fds = (struct pollfd *) kmalloc(nfds*sizeof(struct pollfd), GFP_KERNEL);
	if (!fds) {
		free_page((unsigned long) entry);
		goto out;
	}

	err = -EFAULT;
	if (copy_from_user(fds, ufds, nfds*sizeof(struct pollfd))) {
		free_page((unsigned long)entry);
		kfree(fds);
		goto out;
	}

	if (timeout < 0)
		timeout = 0x7fffffff;
	else if (timeout)
		timeout = ((unsigned long)timeout*HZ+999)/1000+jiffies+1;
	current->timeout = timeout;

	count = 0;
	wait_table.nr = 0;
	wait_table.entry = entry;

	fdcount = do_poll(nfds, fds, timeout ? &wait_table : NULL);
	current->timeout = 0;

	free_wait(&wait_table);
	free_page((unsigned long) entry);

	/* OK, now copy the revents fields back to user space. */
	fds1 = fds;
	for(i=0; i < (int)nfds; i++, ufds++, fds++) {
		__put_user(fds->revents, &ufds->revents);
	}
	kfree(fds1);
	if (!fdcount && (current->signal & ~current->blocked))
		err = -EINTR;
	else
		err = fdcount;
out:
	unlock_kernel();
	return err;
}
