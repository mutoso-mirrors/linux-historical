/*
 *  linux/fs/open.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/vfs.h>
#include <linux/types.h>
#include <linux/utime.h>
#include <linux/errno.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/tty.h>
#include <asm/segment.h>

struct file_operations * chrdev_fops[MAX_CHRDEV] = {
	NULL,
};

struct file_operations * blkdev_fops[MAX_BLKDEV] = {
	NULL,
};

int sys_ustat(int dev, struct ustat * ubuf)
{
	return -ENOSYS;
}

int sys_statfs(const char * path, struct statfs * buf)
{
	struct inode * inode;

	verify_area(buf, sizeof(struct statfs));
	if (!(inode = namei(path)))
		return -ENOENT;
	if (!inode->i_sb->s_op->statfs) {
		iput(inode);
		return -ENOSYS;
	}
	inode->i_sb->s_op->statfs(inode->i_sb, buf);
	iput(inode);
	return 0;
}

int sys_fstatfs(unsigned int fd, struct statfs * buf)
{
	struct inode * inode;
	struct file * file;

	verify_area(buf, sizeof(struct statfs));
	if (fd >= NR_OPEN || !(file = current->filp[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (!inode->i_sb->s_op->statfs)
		return -ENOSYS;
	inode->i_sb->s_op->statfs(inode->i_sb, buf);
	return 0;
}

int sys_truncate(const char * path, unsigned int length)
{
	struct inode * inode;

	if (!(inode = namei(path)))
		return -ENOENT;
	if (S_ISDIR(inode->i_mode) || !permission(inode,MAY_WRITE)) {
		iput(inode);
		return -EACCES;
	}
	if (IS_RDONLY(inode)) {
		iput(inode);
		return -EROFS;
	}
	inode->i_size = length;
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	inode->i_atime = inode->i_mtime = CURRENT_TIME;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

int sys_ftruncate(unsigned int fd, unsigned int length)
{
	struct inode * inode;
	struct file * file;

	if (fd >= NR_OPEN || !(file = current->filp[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (S_ISDIR(inode->i_mode) || !(file->f_mode & 2))
		return -EACCES;
	inode->i_size = length;
	if (inode->i_op && inode->i_op->truncate)
		inode->i_op->truncate(inode);
	inode->i_atime = inode->i_mtime = CURRENT_TIME;
	inode->i_dirt = 1;
	return 0;
}

/* If times==NULL, set access and modification to current time,
 * must be owner or have write permission.
 * Else, update from *times, must be owner or super user.
 */
int sys_utime(char * filename, struct utimbuf * times)
{
	struct inode * inode;
	long actime,modtime;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (IS_RDONLY(inode)) {
		iput(inode);
		return -EROFS;
	}
	if (times) {
		if ((current->euid != inode->i_uid) && !suser()) {
			iput(inode);
			return -EPERM;
		}
		actime = get_fs_long((unsigned long *) &times->actime);
		modtime = get_fs_long((unsigned long *) &times->modtime);
	} else {
		if ((current->euid != inode->i_uid) &&
		    !permission(inode,MAY_WRITE)) {
			iput(inode);
			return -EACCES;
		}
		actime = modtime = CURRENT_TIME;
	}
	inode->i_atime = actime;
	inode->i_mtime = modtime;
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

/*
 * XXX should we use the real or effective uid?  BSD uses the real uid,
 * so as to make this call useful to setuid programs.
 */
int sys_access(const char * filename,int mode)
{
	struct inode * inode;
	int res, i_mode;

	mode &= 0007;
	if (!(inode=namei(filename)))
		return -EACCES;
	i_mode = res = inode->i_mode & 0777;
	iput(inode);
	if (current->uid == inode->i_uid)
		res >>= 6;
	else if (in_group_p(inode->i_gid))
		res >>= 3;
	if ((res & 0007 & mode) == mode)
		return 0;
	/*
	 * XXX we are doing this test last because we really should be
	 * swapping the effective with the real user id (temporarily),
	 * and then calling suser() routine.  If we do call the
	 * suser() routine, it needs to be called last. 
	 */
	if ((!current->uid) &&
	    (!(mode & 1) || (i_mode & 0111)))
		return 0;
	return -EACCES;
}

int sys_chdir(const char * filename)
{
	struct inode * inode;

	if (!(inode = namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	if (!permission(inode,MAY_EXEC)) {
		iput(inode);
		return -EACCES;
	}
	iput(current->pwd);
	current->pwd = inode;
	return (0);
}

int sys_chroot(const char * filename)
{
	struct inode * inode;

	if (!(inode=namei(filename)))
		return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	if (!suser()) {
		iput(inode);
		return -EPERM;
	}
	iput(current->root);
	current->root = inode;
	return (0);
}

int sys_fchmod(unsigned int fd, mode_t mode)
{
	struct inode * inode;
	struct file * file;

	if (fd >= NR_OPEN || !(file = current->filp[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if ((current->euid != inode->i_uid) && !suser())
		return -EPERM;
	if (IS_RDONLY(inode))
		return -EROFS;
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	return 0;
}

int sys_chmod(const char * filename, mode_t mode)
{
	struct inode * inode;

	if (!(inode = namei(filename)))
		return -ENOENT;
	if ((current->euid != inode->i_uid) && !suser()) {
		iput(inode);
		return -EPERM;
	}
	if (IS_RDONLY(inode)) {
		iput(inode);
		return -EROFS;
	}
	inode->i_mode = (mode & 07777) | (inode->i_mode & ~07777);
	inode->i_dirt = 1;
	iput(inode);
	return 0;
}

int sys_fchown(unsigned int fd, uid_t user, gid_t group)
{
	struct inode * inode;
	struct file * file;

	if (fd >= NR_OPEN || !(file = current->filp[fd]))
		return -EBADF;
	if (!(inode = file->f_inode))
		return -ENOENT;
	if (IS_RDONLY(inode))
		return -EROFS;
	if ((current->euid == inode->i_uid && user == inode->i_uid &&
	     (in_group_p(group) || group == inode->i_gid)) ||
	    suser()) {
		inode->i_uid = user;
		inode->i_gid = group;
		inode->i_dirt=1;
		return 0;
	}
	return -EPERM;
}

int sys_chown(const char * filename, uid_t user, gid_t group)
{
	struct inode * inode;

	if (!(inode = lnamei(filename)))
		return -ENOENT;
	if (IS_RDONLY(inode)) {
		iput(inode);
		return -EROFS;
	}
	if ((current->euid == inode->i_uid && user == inode->i_uid &&
	     (in_group_p(group) || group == inode->i_gid)) ||
	    suser()) {
		inode->i_uid = user;
		inode->i_gid = group;
		inode->i_dirt=1;
		iput(inode);
		return 0;
	}
	iput(inode);
	return -EPERM;
}

int sys_open(const char * filename,int flag,int mode)
{
	struct inode * inode;
	struct file * f;
	int i,fd;

	for(fd=0 ; fd<NR_OPEN ; fd++)
		if (!current->filp[fd])
			break;
	if (fd>=NR_OPEN)
		return -EMFILE;
	current->close_on_exec &= ~(1<<fd);
	f = get_empty_filp();
	if (!f)
		return -ENFILE;
	current->filp[fd] = f;
	if ((i = open_namei(filename,flag,mode,&inode))<0) {
		current->filp[fd]=NULL;
		f->f_count--;
		return i;
	}
	f->f_mode = "\001\002\003\000"[flag & O_ACCMODE];
	f->f_flags = flag;
	f->f_inode = inode;
	f->f_pos = 0;
	f->f_reada = 0;
	f->f_op = NULL;
	if (inode->i_op)
		f->f_op = inode->i_op->default_file_ops;
	if (f->f_op && f->f_op->open)
		if (i = f->f_op->open(inode,f)) {
			iput(inode);
			f->f_count--;
			current->filp[fd]=NULL;
			return i;
		}
	return (fd);
}

int sys_creat(const char * pathname, int mode)
{
	return sys_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

static int
close_fp (struct file *filp)
{
   struct inode *inode;

	if (filp->f_count == 0) {
		printk("Close: file count is 0\n");
		return 0;
	}

	if (filp->f_count > 1) {
		filp->f_count--;
		return 0;
	}
     
	inode = filp->f_inode;
	if (filp->f_op && filp->f_op->release)
		filp->f_op->release(inode,filp);

	filp->f_count--;
	filp->f_inode = NULL;
	iput(inode);
	return 0;
}

int sys_close(unsigned int fd)
{	
	struct file * filp;

	if (fd >= NR_OPEN)
		return -EINVAL;
	current->close_on_exec &= ~(1<<fd);
	if (!(filp = current->filp[fd]))
		return -EINVAL;
	current->filp[fd] = NULL;
	return (close_fp (filp));
}

/* This routine looks through all the process's and closes any
   references to the current processes tty.  To avoid problems with
   process sleeping on an inode which has already been iput, anyprocess
   which is sleeping on the tty is sent a sigkill (It's probably a rogue
   process.)  Also no process should ever have /dev/console as it's
   controlling tty, or have it open for reading.  So we don't have to
   worry about messing with all the daemons abilities to write messages
   to the console.  (Besides they should be using syslog.) */

int
sys_vhangup(void)
{
   int i;
   int j;
   struct file *filep;
   struct tty_struct *tty;
   extern void kill_wait (struct wait_queue **q, int signal);
   extern int kill_pg (int pgrp, int sig, int priv);

   if (!suser()) return (-EPERM);

   /* send the SIGHUP signal. */
   kill_pg (current->pgrp, SIGHUP, 0);

   /* See if there is a controlling tty. */
   if (current->tty < 0) return (0);

   for (i = 0; i < NR_TASKS; i++)
     {
	if (task[i] == NULL) continue;
	for (j = 0; j < NR_OPEN; j++)
	  {
	     filep = task[i]->filp[j];

	     if (filep == NULL) continue;

	     /* now we need to check to see if this file points to the
		device we are trying to close. */

 	     if (!S_ISCHR (filep->f_inode->i_mode)) continue;

	     /* This will catch both /dev/tty and the explicit terminal
		device.  However, we must make sure that f_rdev is
		defined and correct. */

	     if ((MAJOR(filep->f_inode->i_rdev) == 5 ||
		  MAJOR(filep->f_inode->i_rdev) == 4 ) &&
		 (MAJOR(filep->f_rdev) == 4 &&
		  MINOR(filep->f_rdev) == MINOR (current->tty)))
	       {
		  task[i]->filp[j] = NULL;

		  /* so now we have found something to close.  We
		     need to kill every process waiting on the
		     inode. */

		  kill_wait (&filep->f_inode->i_wait, SIGKILL);

		  /* now make sure they are awake before we close the
		     file. */

		  wake_up (&filep->f_inode->i_wait);

		  /* finally close the file. */

		  current->close_on_exec &= ~(1<<j);
		  close_fp (filep);
	       }

	  }

	/* can't let them keep a reference to it around.
	   But we can't touch current->tty until after the
	   loop is complete. */

	if (task[i]->tty == current->tty && task[i] != current)
	  {
	     task[i]->tty = -1;
	  }
     }
   
   /* need to do tty->session = 0 */
   tty = TTY_TABLE(MINOR(current->tty));
   tty->session = 0;
   tty->pgrp = -1;
   current->tty = -1;


   return (0);
}

