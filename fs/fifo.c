/*
 *  linux/fs/fifo.c
 *
 *  written by Paul H. Hargrove
 *
 *  Fixes:
 *	10-06-1999, AV: fixed OOM handling in fifo_open(), moved
 *			initialization there, switched to external
 *			allocation of pipe_inode_info.
 */

#include <linux/mm.h>
#include <linux/malloc.h>

static int fifo_open(struct inode * inode,struct file * filp)
{
	int retval = 0;
	unsigned long page = 0;
	struct pipe_inode_info *info, *tmp = NULL;

	if (inode->i_pipe)
		goto got_it;
	tmp = kmalloc(sizeof(struct pipe_inode_info),GFP_KERNEL);
	if (inode->i_pipe)
		goto got_it;
	if (!tmp)
		goto oom;
	page = __get_free_page(GFP_KERNEL);
	if (inode->i_pipe)
		goto got_it;
	if (!page)
		goto oom;
	inode->i_pipe = tmp;
	PIPE_LOCK(*inode) = 0;
	PIPE_START(*inode) = PIPE_LEN(*inode) = 0;
	PIPE_BASE(*inode) = (char *) page;
	PIPE_RD_OPENERS(*inode) = PIPE_WR_OPENERS(*inode) = 0;
	PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 0;
	init_waitqueue_head(&PIPE_WAIT(*inode));
	tmp = NULL;	/* no need to free it */
	page = 0;

got_it:

	switch( filp->f_mode ) {

	case 1:
	/*
	 *  O_RDONLY
	 *  POSIX.1 says that O_NONBLOCK means return with the FIFO
	 *  opened, even when there is no process writing the FIFO.
	 */
		filp->f_op = &connecting_fifo_fops;
		if (!PIPE_READERS(*inode)++)
			wake_up_interruptible(&PIPE_WAIT(*inode));
		if (!(filp->f_flags & O_NONBLOCK) && !PIPE_WRITERS(*inode)) {
			PIPE_RD_OPENERS(*inode)++;
			while (!PIPE_WRITERS(*inode)) {
				if (signal_pending(current)) {
					retval = -ERESTARTSYS;
					break;
				}
				interruptible_sleep_on(&PIPE_WAIT(*inode));
			}
			if (!--PIPE_RD_OPENERS(*inode))
				wake_up_interruptible(&PIPE_WAIT(*inode));
		}
		while (PIPE_WR_OPENERS(*inode))
			interruptible_sleep_on(&PIPE_WAIT(*inode));
		if (PIPE_WRITERS(*inode))
			filp->f_op = &read_fifo_fops;
		if (retval && !--PIPE_READERS(*inode))
			wake_up_interruptible(&PIPE_WAIT(*inode));
		break;
	
	case 2:
	/*
	 *  O_WRONLY
	 *  POSIX.1 says that O_NONBLOCK means return -1 with
	 *  errno=ENXIO when there is no process reading the FIFO.
	 */
		if ((filp->f_flags & O_NONBLOCK) && !PIPE_READERS(*inode)) {
			retval = -ENXIO;
			break;
		}
		filp->f_op = &write_fifo_fops;
		if (!PIPE_WRITERS(*inode)++)
			wake_up_interruptible(&PIPE_WAIT(*inode));
		if (!PIPE_READERS(*inode)) {
			PIPE_WR_OPENERS(*inode)++;
			while (!PIPE_READERS(*inode)) {
				if (signal_pending(current)) {
					retval = -ERESTARTSYS;
					break;
				}
				interruptible_sleep_on(&PIPE_WAIT(*inode));
			}
			if (!--PIPE_WR_OPENERS(*inode))
				wake_up_interruptible(&PIPE_WAIT(*inode));
		}
		while (PIPE_RD_OPENERS(*inode))
			interruptible_sleep_on(&PIPE_WAIT(*inode));
		if (retval && !--PIPE_WRITERS(*inode))
			wake_up_interruptible(&PIPE_WAIT(*inode));
		break;
	
	case 3:
	/*
	 *  O_RDWR
	 *  POSIX.1 leaves this case "undefined" when O_NONBLOCK is set.
	 *  This implementation will NEVER block on a O_RDWR open, since
	 *  the process can at least talk to itself.
	 */
		filp->f_op = &rdwr_fifo_fops;
		if (!PIPE_READERS(*inode)++)
			wake_up_interruptible(&PIPE_WAIT(*inode));
		while (PIPE_WR_OPENERS(*inode))
			interruptible_sleep_on(&PIPE_WAIT(*inode));
		if (!PIPE_WRITERS(*inode)++)
			wake_up_interruptible(&PIPE_WAIT(*inode));
		while (PIPE_RD_OPENERS(*inode))
			interruptible_sleep_on(&PIPE_WAIT(*inode));
		break;

	default:
		retval = -EINVAL;
	}
	if (retval) 
		goto cleanup;
out:
	if (tmp)
		kfree(tmp);
	if (page)
		free_page(page);
	return retval;

cleanup:
	if (!PIPE_READERS(*inode) && !PIPE_WRITERS(*inode)) {
		info = inode->i_pipe;
		inode->i_pipe = NULL;
		free_page((unsigned long)info->base);
		kfree(info);
	}
	goto out;
oom:
	retval = -ENOMEM;
	goto out;
}

/*
 * Dummy default file-operations: the only thing this does
 * is contain the open that then fills in the correct operations
 * depending on the access mode of the file...
 */
static struct file_operations def_fifo_fops = {
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	fifo_open,		/* will set read or write pipe_fops */
	NULL,
	NULL,
	NULL,
	NULL
};

struct inode_operations fifo_inode_operations = {
	&def_fifo_fops,		/* default file operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};


/* Goner. Filesystems do not use it anymore. */

void init_fifo(struct inode * inode)
{
	inode->i_op = &fifo_inode_operations;
}
