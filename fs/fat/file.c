/*
 *  linux/fs/fat/file.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  regular file handling primitives for fat-based filesystems
 */

#include <linux/module.h>
#include <linux/time.h>
#include <linux/msdos_fs.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>

static ssize_t fat_file_aio_write(struct kiocb *iocb, const char __user *buf,
				  size_t count, loff_t pos)
{
	struct inode *inode = iocb->ki_filp->f_dentry->d_inode;
	int retval;

	retval = generic_file_aio_write(iocb, buf, count, pos);
	if (retval > 0) {
		inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
		MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
		mark_inode_dirty(inode);
	}
	return retval;
}

static ssize_t fat_file_writev(struct file *filp, const struct iovec *iov,
			       unsigned long nr_segs, loff_t *ppos)
{
	struct inode *inode = filp->f_dentry->d_inode;
	int retval;

	retval = generic_file_writev(filp, iov, nr_segs, ppos);
	if (retval > 0) {
		inode->i_mtime = inode->i_ctime = CURRENT_TIME_SEC;
		MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
		mark_inode_dirty(inode);
	}
	return retval;
}

int fat_generic_ioctl(struct inode *inode, struct file *filp,
		      unsigned int cmd, unsigned long arg)
{
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	u32 __user *user_attr = (u32 __user *)arg;

	switch (cmd) {
	case FAT_IOCTL_GET_ATTRIBUTES:
	{
		u32 attr;

		if (inode->i_ino == MSDOS_ROOT_INO)
			attr = ATTR_DIR;
		else
			attr = fat_attr(inode);

		return put_user(attr, user_attr);
	}
	case FAT_IOCTL_SET_ATTRIBUTES:
	{
		u32 attr, oldattr;
		int err, is_dir = S_ISDIR(inode->i_mode);
		struct iattr ia;

		err = get_user(attr, user_attr);
		if (err)
			return err;

		down(&inode->i_sem);

		if (IS_RDONLY(inode)) {
			err = -EROFS;
			goto up;
		}

		/*
		 * ATTR_VOLUME and ATTR_DIR cannot be changed; this also
		 * prevents the user from turning us into a VFAT
		 * longname entry.  Also, we obviously can't set
		 * any of the NTFS attributes in the high 24 bits.
		 */
		attr &= 0xff & ~(ATTR_VOLUME | ATTR_DIR);
		/* Merge in ATTR_VOLUME and ATTR_DIR */
		attr |= (MSDOS_I(inode)->i_attrs & ATTR_VOLUME) |
			(is_dir ? ATTR_DIR : 0);
		oldattr = fat_attr(inode);

		/* Equivalent to a chmod() */
		ia.ia_valid = ATTR_MODE | ATTR_CTIME;
		if (is_dir) {
			ia.ia_mode = MSDOS_MKMODE(attr,
				S_IRWXUGO & ~sbi->options.fs_dmask)
				| S_IFDIR;
		} else {
			ia.ia_mode = MSDOS_MKMODE(attr,
				(S_IRUGO | S_IWUGO | (inode->i_mode & S_IXUGO))
				& ~sbi->options.fs_fmask)
				| S_IFREG;
		}

		/* The root directory has no attributes */
		if (inode->i_ino == MSDOS_ROOT_INO && attr != ATTR_DIR) {
			err = -EINVAL;
			goto up;
		}

		if (sbi->options.sys_immutable) {
			if ((attr | oldattr) & ATTR_SYS) {
				if (!capable(CAP_LINUX_IMMUTABLE)) {
					err = -EPERM;
					goto up;
				}
			}
		}

		/* This MUST be done before doing anything irreversible... */
		err = notify_change(filp->f_dentry, &ia);
		if (err)
			goto up;

		if (sbi->options.sys_immutable) {
			if (attr & ATTR_SYS)
				inode->i_flags |= S_IMMUTABLE;
			else
				inode->i_flags &= S_IMMUTABLE;
		}

		MSDOS_I(inode)->i_attrs = attr & ATTR_UNUSED;
		mark_inode_dirty(inode);
	up:
		up(&inode->i_sem);
		return err;
	}
	default:
		return -ENOTTY;	/* Inappropriate ioctl for device */
	}
}

struct file_operations fat_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.write		= do_sync_write,
	.readv		= generic_file_readv,
	.writev		= fat_file_writev,
	.aio_read	= generic_file_aio_read,
	.aio_write	= fat_file_aio_write,
	.mmap		= generic_file_mmap,
	.ioctl		= fat_generic_ioctl,
	.fsync		= file_fsync,
	.sendfile	= generic_file_sendfile,
};

int fat_notify_change(struct dentry *dentry, struct iattr *attr)
{
	struct msdos_sb_info *sbi = MSDOS_SB(dentry->d_sb);
	struct inode *inode = dentry->d_inode;
	int mask, error = 0;

	lock_kernel();

	/* FAT cannot truncate to a longer file */
	if (attr->ia_valid & ATTR_SIZE) {
		if (attr->ia_size > inode->i_size) {
			error = -EPERM;
			goto out;
		}
	}

	error = inode_change_ok(inode, attr);
	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}
	if (((attr->ia_valid & ATTR_UID) &&
	     (attr->ia_uid != sbi->options.fs_uid)) ||
	    ((attr->ia_valid & ATTR_GID) &&
	     (attr->ia_gid != sbi->options.fs_gid)) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode & ~MSDOS_VALID_MODE)))
		error = -EPERM;

	if (error) {
		if (sbi->options.quiet)
			error = 0;
		goto out;
	}
	error = inode_setattr(inode, attr);
	if (error)
		goto out;

	if (S_ISDIR(inode->i_mode))
		mask = sbi->options.fs_dmask;
	else
		mask = sbi->options.fs_fmask;
	inode->i_mode &= S_IFMT | (S_IRWXUGO & ~mask);
out:
	unlock_kernel();
	return error;
}

EXPORT_SYMBOL(fat_notify_change);

/* Free all clusters after the skip'th cluster. */
static int fat_free(struct inode *inode, int skip)
{
	struct super_block *sb = inode->i_sb;
	int nr, ret, fclus, dclus;

	if (MSDOS_I(inode)->i_start == 0)
		return 0;

	if (skip) {
		ret = fat_get_cluster(inode, skip - 1, &fclus, &dclus);
		if (ret < 0)
			return ret;
		else if (ret == FAT_ENT_EOF)
			return 0;

		nr = fat_access(sb, dclus, -1);
		if (nr == FAT_ENT_EOF)
			return 0;
		else if (nr > 0) {
			/*
			 * write a new EOF, and get the remaining cluster
			 * chain for freeing.
			 */
			nr = fat_access(sb, dclus, FAT_ENT_EOF);
		}
		if (nr < 0)
			return nr;

		fat_cache_inval_inode(inode);
	} else {
		fat_cache_inval_inode(inode);

		nr = MSDOS_I(inode)->i_start;
		MSDOS_I(inode)->i_start = 0;
		MSDOS_I(inode)->i_logstart = 0;
		mark_inode_dirty(inode);
	}

	lock_fat(sb);
	do {
		nr = fat_access(sb, nr, FAT_ENT_FREE);
		if (nr < 0)
			goto error;
		else if (nr == FAT_ENT_FREE) {
			fat_fs_panic(sb, "%s: deleting beyond EOF (i_pos %lld)",
				     __FUNCTION__, MSDOS_I(inode)->i_pos);
			nr = -EIO;
			goto error;
		}
		if (MSDOS_SB(sb)->free_clusters != -1)
			MSDOS_SB(sb)->free_clusters++;
		inode->i_blocks -= MSDOS_SB(sb)->cluster_size >> 9;
	} while (nr != FAT_ENT_EOF);
	fat_clusters_flush(sb);
	nr = 0;
error:
	unlock_fat(sb);

	return nr;
}

void fat_truncate(struct inode *inode)
{
	struct msdos_sb_info *sbi = MSDOS_SB(inode->i_sb);
	const unsigned int cluster_size = sbi->cluster_size;
	int nr_clusters;

	/*
	 * This protects against truncating a file bigger than it was then
	 * trying to write into the hole.
	 */
	if (MSDOS_I(inode)->mmu_private > inode->i_size)
		MSDOS_I(inode)->mmu_private = inode->i_size;

	nr_clusters = (inode->i_size + (cluster_size - 1)) >> sbi->cluster_bits;

	lock_kernel();
	fat_free(inode, nr_clusters);
	MSDOS_I(inode)->i_attrs |= ATTR_ARCH;
	unlock_kernel();
	inode->i_ctime = inode->i_mtime = CURRENT_TIME_SEC;
	mark_inode_dirty(inode);
}

struct inode_operations fat_file_inode_operations = {
	.truncate	= fat_truncate,
	.setattr	= fat_notify_change,
};
