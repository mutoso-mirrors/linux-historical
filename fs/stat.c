/*
 *  linux/fs/stat.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>

/*
 * Revalidate the inode. This is required for proper NFS attribute caching.
 */
static __inline__ int
do_revalidate(struct inode *inode)
{
	if (inode->i_op && inode->i_op->revalidate)
		return inode->i_op->revalidate(inode);
	return 0;
}


#if !defined(__alpha__) && !defined(__sparc__)

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
static int cp_old_stat(struct inode * inode, struct __old_kernel_stat * statbuf)
{
	struct __old_kernel_stat tmp;

	printk("VFS: Warning: %s using old stat() call. Recompile your binary.\n",
		current->comm);
	tmp.st_dev = kdev_t_to_nr(inode->i_dev);
	tmp.st_ino = inode->i_ino;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlink;
	tmp.st_uid = inode->i_uid;
	tmp.st_gid = inode->i_gid;
	tmp.st_rdev = kdev_t_to_nr(inode->i_rdev);
	tmp.st_size = inode->i_size;
	if (inode->i_pipe)
		tmp.st_size = PIPE_SIZE(*inode);
	tmp.st_atime = inode->i_atime;
	tmp.st_mtime = inode->i_mtime;
	tmp.st_ctime = inode->i_ctime;
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

#endif

static int cp_new_stat(struct inode * inode, struct stat * statbuf)
{
	struct stat tmp;
	unsigned int blocks, indirect;

	memset(&tmp, 0, sizeof(tmp));
	tmp.st_dev = kdev_t_to_nr(inode->i_dev);
	tmp.st_ino = inode->i_ino;
	tmp.st_mode = inode->i_mode;
	tmp.st_nlink = inode->i_nlink;
	tmp.st_uid = inode->i_uid;
	tmp.st_gid = inode->i_gid;
	tmp.st_rdev = kdev_t_to_nr(inode->i_rdev);
	tmp.st_size = inode->i_size;
	if (inode->i_pipe)
		tmp.st_size = PIPE_SIZE(*inode);
	tmp.st_atime = inode->i_atime;
	tmp.st_mtime = inode->i_mtime;
	tmp.st_ctime = inode->i_ctime;
/*
 * st_blocks and st_blksize are approximated with a simple algorithm if
 * they aren't supported directly by the filesystem. The minix and msdos
 * filesystems don't keep track of blocks, so they would either have to
 * be counted explicitly (by delving into the file itself), or by using
 * this simple algorithm to get a reasonable (although not 100% accurate)
 * value.
 */

/*
 * Use minix fs values for the number of direct and indirect blocks.  The
 * count is now exact for the minix fs except that it counts zero blocks.
 * Everything is in BLOCK_SIZE'd units until the assignment to
 * tmp.st_blksize.
 */
#define D_B   7
#define I_B   (BLOCK_SIZE / sizeof(unsigned short))

	if (!inode->i_blksize) {
		blocks = (tmp.st_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
		if (blocks > D_B) {
			indirect = (blocks - D_B + I_B - 1) / I_B;
			blocks += indirect;
			if (indirect > 1) {
				indirect = (indirect - 1 + I_B - 1) / I_B;
				blocks += indirect;
				if (indirect > 1)
					blocks++;
			}
		}
		tmp.st_blocks = (BLOCK_SIZE / 512) * blocks;
		tmp.st_blksize = BLOCK_SIZE;
	} else {
		tmp.st_blocks = inode->i_blocks;
		tmp.st_blksize = inode->i_blksize;
	}
	return copy_to_user(statbuf,&tmp,sizeof(tmp)) ? -EFAULT : 0;
}

#if !defined(__alpha__) && !defined(__sparc__)
/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
asmlinkage int sys_stat(char * filename, struct __old_kernel_stat * statbuf)
{
	struct inode * inode;
	int error;

	lock_kernel();
	error = namei(filename, &inode);
	if (error)
		goto out;
	if ((error = do_revalidate(inode)) == 0)
		error = cp_old_stat(inode,statbuf);
	iput(inode);
out:
	unlock_kernel();
	return error;
}
#endif

asmlinkage int sys_newstat(char * filename, struct stat * statbuf)
{
	struct inode * inode;
	int error;

	lock_kernel();
	error = namei(filename, &inode);
	if (error)
		goto out;
	if ((error = do_revalidate(inode)) == 0)
		error = cp_new_stat(inode,statbuf);
	iput(inode);
out:
	unlock_kernel();
	return error;
}

#if !defined(__alpha__) && !defined(__sparc__)

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
asmlinkage int sys_lstat(char * filename, struct __old_kernel_stat * statbuf)
{
	struct inode * inode;
	int error;

	lock_kernel();
	error = lnamei(filename, &inode);
	if (error)
		goto out;
	if ((error = do_revalidate(inode)) == 0)
		error = cp_old_stat(inode,statbuf);
	iput(inode);
out:
	unlock_kernel();
	return error;
}

#endif

asmlinkage int sys_newlstat(char * filename, struct stat * statbuf)
{
	struct inode * inode;
	int error;

	lock_kernel();
	error = lnamei(filename, &inode);
	if (error)
		goto out;
	if ((error = do_revalidate(inode)) == 0)
		error = cp_new_stat(inode,statbuf);
	iput(inode);
out:
	unlock_kernel();
	return error;
}

#if !defined(__alpha__) && !defined(__sparc__)

/*
 * For backward compatibility?  Maybe this should be moved
 * into arch/i386 instead?
 */
asmlinkage int sys_fstat(unsigned int fd, struct __old_kernel_stat * statbuf)
{
	struct file * f;
	struct inode * inode;
	int ret = -EBADF;

	lock_kernel();
	if (fd >= NR_OPEN || !(f=current->files->fd[fd]) || !(inode=f->f_inode))
		goto out;
	if ((ret = do_revalidate(inode)) == 0)
		ret = cp_old_stat(inode,statbuf);
out:
	unlock_kernel();
	return ret;
}

#endif

asmlinkage int sys_newfstat(unsigned int fd, struct stat * statbuf)
{
	struct file * f;
	struct inode * inode;
	int err = -EBADF;

	lock_kernel();
	if (fd >= NR_OPEN || !(f=current->files->fd[fd]) || !(inode=f->f_inode))
		goto out;
	if ((err = do_revalidate(inode)) == 0)
		err = cp_new_stat(inode,statbuf);
out:
	unlock_kernel();
	return err;
}

asmlinkage int sys_readlink(const char * path, char * buf, int bufsiz)
{
	struct inode * inode;
	int error;

	if (bufsiz <= 0)
		return -EINVAL;

	lock_kernel();
	error = lnamei(path, &inode);
	if (error)
		goto out;
	error = -EINVAL;
	if (!inode->i_op || !inode->i_op->readlink ||
	    !S_ISLNK(inode->i_mode) || (error = do_revalidate(inode)) < 0) {
		iput(inode);
		goto out;
	}
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	error = inode->i_op->readlink(inode,buf,bufsiz);
	iput(inode);
out:
	unlock_kernel();
	return error;
}
