/*
 *  linux/fs/readdir.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/time.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/file.h>
#include <linux/smp_lock.h>
#include <linux/fs.h>
#include <linux/dirent.h>
#include <linux/security.h>

#include <asm/uaccess.h>

int vfs_readdir(struct file *file, filldir_t filler, void *buf)
{
	struct inode *inode = file->f_dentry->d_inode;
	int res = -ENOTDIR;
	if (!file->f_op || !file->f_op->readdir)
		goto out;

	res = security_file_permission(file, MAY_READ);
	if (res)
		goto out;

	down(&inode->i_sem);
	res = -ENOENT;
	if (!IS_DEADDIR(inode)) {
		res = file->f_op->readdir(file, buf, filler);
	}
	up(&inode->i_sem);
out:
	return res;
}

/*
 * Traditional linux readdir() handling..
 *
 * "count=1" is a special case, meaning that the buffer is one
 * dirent-structure in size and that the code can't handle more
 * anyway. Thus the special "fillonedir()" function for that
 * case (the low-level handlers don't need to care about this).
 */
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+sizeof(long)-1) & ~(sizeof(long)-1))

#ifndef __ia64__

struct old_linux_dirent {
	unsigned long	d_ino;
	unsigned long	d_offset;
	unsigned short	d_namlen;
	char		d_name[1];
};

struct readdir_callback {
	struct old_linux_dirent * dirent;
	int count;
};

static int fillonedir(void * __buf, const char * name, int namlen, loff_t offset,
		      ino_t ino, unsigned int d_type)
{
	struct readdir_callback * buf = (struct readdir_callback *) __buf;
	struct old_linux_dirent * dirent;

	if (buf->count)
		return -EINVAL;
	buf->count++;
	dirent = buf->dirent;
	if (!access_ok(VERIFY_WRITE, (unsigned long)dirent,
			(unsigned long)(dirent->d_name + namlen + 1) -
				(unsigned long)dirent))
		return -EFAULT;
	if (	__put_user(ino, &dirent->d_ino) ||
		__put_user(offset, &dirent->d_offset) ||
		__put_user(namlen, &dirent->d_namlen) ||
		__copy_to_user(dirent->d_name, name, namlen) ||
		__put_user(0, dirent->d_name + namlen))
		return -EFAULT;
	return 0;
}

asmlinkage long old_readdir(unsigned int fd, void * dirent, unsigned int count)
{
	int error;
	struct file * file;
	struct readdir_callback buf;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.count = 0;
	buf.dirent = dirent;

	error = vfs_readdir(file, fillonedir, &buf);
	if (error >= 0)
		error = buf.count;

	fput(file);
out:
	return error;
}

#endif /* !__ia64__ */

/*
 * New, all-improved, singing, dancing, iBCS2-compliant getdents()
 * interface. 
 */
struct linux_dirent {
	unsigned long	d_ino;
	unsigned long	d_off;
	unsigned short	d_reclen;
	char		d_name[1];
};

struct getdents_callback {
	struct linux_dirent * current_dir;
	struct linux_dirent * previous;
	int count;
	int error;
};

static int filldir(void * __buf, const char * name, int namlen, loff_t offset,
		   ino_t ino, unsigned int d_type)
{
	struct linux_dirent * dirent;
	struct getdents_callback * buf = (struct getdents_callback *) __buf;
	int reclen = ROUND_UP(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent) {
		if (__put_user(offset, &dirent->d_off))
			goto efault;
	}
	dirent = buf->current_dir;
	buf->previous = dirent;
	if (__put_user(ino, &dirent->d_ino))
		goto efault;
	if (__put_user(reclen, &dirent->d_reclen))
		goto efault;
	if (copy_to_user(dirent->d_name, name, namlen))
		goto efault;
	if (__put_user(0, dirent->d_name + namlen))
		goto efault;
	((char *) dirent) += reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
efault:
	return -EFAULT;
}

asmlinkage long sys_getdents(unsigned int fd, void * dirent, unsigned int count)
{
	struct file * file;
	struct linux_dirent * lastdirent;
	struct getdents_callback buf;
	int error;

	error = -EFAULT;
	if (!access_ok(VERIFY_WRITE, dirent, count))
		goto out;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = (struct linux_dirent *) dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, filldir, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		if (put_user(file->f_pos, &lastdirent->d_off))
			error = -EFAULT;
		else
			error = count - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}

#define ROUND_UP64(x) (((x)+sizeof(u64)-1) & ~(sizeof(u64)-1))

struct getdents_callback64 {
	struct linux_dirent64 * current_dir;
	struct linux_dirent64 * previous;
	int count;
	int error;
};

static int filldir64(void * __buf, const char * name, int namlen, loff_t offset,
		     ino_t ino, unsigned int d_type)
{
	struct linux_dirent64 *dirent;
	struct getdents_callback64 * buf = (struct getdents_callback64 *) __buf;
	int reclen = ROUND_UP64(NAME_OFFSET(dirent) + namlen + 1);

	buf->error = -EINVAL;	/* only used if we fail.. */
	if (reclen > buf->count)
		return -EINVAL;
	dirent = buf->previous;
	if (dirent) {
		if (__put_user(offset, &dirent->d_off))
			goto efault;
	}
	dirent = buf->current_dir;
	buf->previous = dirent;
	if (__put_user(ino, &dirent->d_ino))
		goto efault;
	if (__put_user(0, &dirent->d_off))
		goto efault;
	if (__put_user(reclen, &dirent->d_reclen))
		goto efault;
	if (__put_user(d_type, &dirent->d_type))
		goto efault;
	if (copy_to_user(dirent->d_name, name, namlen))
		goto efault;
	if (__put_user(0, dirent->d_name + namlen))
		goto efault;
	((char *) dirent) += reclen;
	buf->current_dir = dirent;
	buf->count -= reclen;
	return 0;
efault:
	return -EFAULT;
}

asmlinkage long sys_getdents64(unsigned int fd, void * dirent, unsigned int count)
{
	struct file * file;
	struct linux_dirent64 * lastdirent;
	struct getdents_callback64 buf;
	int error;

	error = -EFAULT;
	if (!access_ok(VERIFY_WRITE, dirent, count))
		goto out;

	error = -EBADF;
	file = fget(fd);
	if (!file)
		goto out;

	buf.current_dir = (struct linux_dirent64 *) dirent;
	buf.previous = NULL;
	buf.count = count;
	buf.error = 0;

	error = vfs_readdir(file, filldir64, &buf);
	if (error < 0)
		goto out_putf;
	error = buf.error;
	lastdirent = buf.previous;
	if (lastdirent) {
		struct linux_dirent64 d;
		d.d_off = file->f_pos;
		__put_user(d.d_off, &lastdirent->d_off);
		error = count - buf.count;
	}

out_putf:
	fput(file);
out:
	return error;
}
