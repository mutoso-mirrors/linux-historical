/*
 *  linux/fs/stat.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/file.h>
#include <linux/smp_lock.h>
#include <linux/highuid.h>
#include <linux/fs.h>
#include <linux/namei.h>
#include <linux/security.h>

#include <asm/uaccess.h>

void generic_fillattr(struct inode *inode, struct kstat *stat)
{
	stat->dev = inode->i_dev;
	stat->ino = inode->i_ino;
	stat->mode = inode->i_mode;
	stat->nlink = inode->i_nlink;
	stat->uid = inode->i_uid;
	stat->gid = inode->i_gid;
	stat->rdev = kdev_t_to_nr(inode->i_rdev);
	stat->atime = inode->i_atime;
	stat->mtime = inode->i_mtime;
	stat->ctime = inode->i_ctime;
	stat->size = inode->i_size;
	stat->blocks = inode->i_blocks;
	stat->blksize = inode->i_blksize;
}

int vfs_getattr(struct vfsmount *mnt, struct dentry *dentry, struct kstat *stat)
{
	struct inode *inode = dentry->d_inode;
	int retval;

	retval = security_ops->inode_getattr(mnt, dentry);
	if (retval)
		return retval;

	if (inode->i_op->getattr)
		return inode->i_op->getattr(mnt, dentry, stat);

	generic_fillattr(inode, stat);
	if (!stat->blksize) {
		struct super_block *s = inode->i_sb;
		unsigned blocks;
		blocks = (stat->size+s->s_blocksize-1) >> s->s_blocksize_bits;
		stat->blocks = (s->s_blocksize / 512) * blocks;
		stat->blksize = s->s_blocksize;
	}
	return 0;
}

int vfs_stat(char *name, struct kstat *stat)
{
	struct nameidata nd;
	int error;

	error = user_path_walk(name, &nd);
	if (!error) {
		error = vfs_getattr(nd.mnt, nd.dentry, stat);
		path_release(&nd);
	}
	return error;
}

int vfs_lstat(char *name, struct kstat *stat)
{
	struct nameidata nd;
	int error;

	error = user_path_walk_link(name, &nd);
	if (!error) {
		error = vfs_getattr(nd.mnt, nd.dentry, stat);
		path_release(&nd);
	}
	return error;
}

int vfs_fstat(unsigned int fd, struct kstat *stat)
{
	struct file *f = fget(fd);
	int error = -EBADF;

	if (f) {
		error = vfs_getattr(f->f_vfsmnt, f->f_dentry, stat);
		fput(f);
	}
	return error;
}

#if !defined(__alpha__) && !defined(__sparc__) && !defined(__ia64__) \
  && !defined(CONFIG_ARCH_S390) && !defined(__hppa__) && !defined(__x86_64__) \
  && !defined(__arm__) && !defined(CONFIG_V850)

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
static int cp_old_stat(struct kstat *stat, struct __old_kernel_stat * statbuf)
{
	static int warncount = 5;
	struct __old_kernel_stat tmp;

	if (warncount > 0) {
		warncount--;
		printk(KERN_WARNING "VFS: Warning: %s using old stat() call. Recompile your binary.\n",
			current->comm);
	} else if (warncount < 0) {
		/* it's laughable, but... */
		warncount = 0;
	}

	tmp.st_dev = stat->dev;
	tmp.st_ino = stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	SET_OLDSTAT_UID(tmp, stat->uid);
	SET_OLDSTAT_GID(tmp, stat->gid);
	tmp.st_rdev = stat->rdev;
#if BITS_PER_LONG == 32
	if (stat->size > MAX_NON_LFS)
		return -EOVERFLOW;
#endif	
	tmp.st_size = stat->size;
	tmp.st_atime = stat->atime;
	tmp.st_mtime = stat->mtime;
	tmp.st_ctime = stat->ctime;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

asmlinkage long sys_stat(char * filename, struct __old_kernel_stat * statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_old_stat(&stat, statbuf);

	return error;
}
asmlinkage long sys_lstat(char * filename, struct __old_kernel_stat * statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_old_stat(&stat, statbuf);

	return error;
}
asmlinkage long sys_fstat(unsigned int fd, struct __old_kernel_stat * statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_old_stat(&stat, statbuf);

	return error;
}

#endif

static int cp_new_stat(struct kstat *stat, struct stat *statbuf)
{
	struct stat tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = stat->dev;
	tmp.st_ino = stat->ino;
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	SET_STAT_UID(tmp, stat->uid);
	SET_STAT_GID(tmp, stat->gid);
	tmp.st_rdev = stat->rdev;
#if BITS_PER_LONG == 32
	if (stat->size > MAX_NON_LFS)
		return -EOVERFLOW;
#endif	
	tmp.st_size = stat->size;
	tmp.st_atime = stat->atime;
	tmp.st_mtime = stat->mtime;
	tmp.st_ctime = stat->ctime;
	tmp.st_blocks = stat->blocks;
	tmp.st_blksize = stat->blksize;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

asmlinkage long sys_newstat(char * filename, struct stat * statbuf)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_new_stat(&stat, statbuf);

	return error;
}
asmlinkage long sys_newlstat(char * filename, struct stat * statbuf)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_new_stat(&stat, statbuf);

	return error;
}
asmlinkage long sys_newfstat(unsigned int fd, struct stat * statbuf)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_new_stat(&stat, statbuf);

	return error;
}

asmlinkage long sys_readlink(const char * path, char * buf, int bufsiz)
{
	struct nameidata nd;
	int error;

	if (bufsiz <= 0)
		return -EINVAL;

	error = user_path_walk_link(path, &nd);
	if (!error) {
		struct inode * inode = nd.dentry->d_inode;

		error = -EINVAL;
		if (inode->i_op && inode->i_op->readlink) {
			error = security_ops->inode_readlink(nd.dentry);
			if (!error) {
				UPDATE_ATIME(inode);
				error = inode->i_op->readlink(nd.dentry, buf, bufsiz);
			}
		}
		path_release(&nd);
	}
	return error;
}


/* ---------- LFS-64 ----------- */
#if !defined(__alpha__) && !defined(__ia64__) && !defined(__mips64) && !defined(__x86_64__) && !defined(CONFIG_ARCH_S390X)

static long cp_new_stat64(struct kstat *stat, struct stat64 *statbuf)
{
	struct stat64 tmp;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = stat->dev;
	tmp.st_ino = stat->ino;
#ifdef STAT64_HAS_BROKEN_ST_INO
	tmp.__st_ino = stat->ino;
#endif
	tmp.st_mode = stat->mode;
	tmp.st_nlink = stat->nlink;
	tmp.st_uid = stat->uid;
	tmp.st_gid = stat->gid;
	tmp.st_rdev = stat->rdev;
	tmp.st_atime = stat->atime;
	tmp.st_mtime = stat->mtime;
	tmp.st_ctime = stat->ctime;
	tmp.st_size = stat->size;
	tmp.st_blocks = stat->blocks;
	tmp.st_blksize = stat->blksize;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

asmlinkage long sys_stat64(char * filename, struct stat64 * statbuf, long flags)
{
	struct kstat stat;
	int error = vfs_stat(filename, &stat);

	if (!error)
		error = cp_new_stat64(&stat, statbuf);

	return error;
}
asmlinkage long sys_lstat64(char * filename, struct stat64 * statbuf, long flags)
{
	struct kstat stat;
	int error = vfs_lstat(filename, &stat);

	if (!error)
		error = cp_new_stat64(&stat, statbuf);

	return error;
}
asmlinkage long sys_fstat64(unsigned long fd, struct stat64 * statbuf, long flags)
{
	struct kstat stat;
	int error = vfs_fstat(fd, &stat);

	if (!error)
		error = cp_new_stat64(&stat, statbuf);

	return error;
}

#endif /* LFS-64 */
