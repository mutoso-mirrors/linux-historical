/*
 *  linux/fs/ufs/ufs_dir.c
 *
 * Copyright (C) 1996
 * Adrian Rodriguez (adrian@franklins-tower.rutgers.edu)
 * Laboratory for Computer Science Research Computing Facility
 * Rutgers, The State University of New Jersey
 *
 * $Id: ufs_dir.c,v 1.10 1997/06/05 01:29:06 davem Exp $
 *
 */

#include <linux/fs.h>
#include <linux/ufs_fs.h>

/*
 * This is blatantly stolen from ext2fs
 */
static int
ufs_readdir (struct inode * inode, struct file * filp, void * dirent,
	     filldir_t filldir)
{
	int error = 0;
	unsigned long offset, lblk, blk;
	int i, stored;
	struct buffer_head * bh;
	struct ufs_direct * de;
	struct super_block * sb;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
	sb = inode->i_sb;

	if (inode->i_sb->u.ufs_sb.s_flags & UFS_DEBUG) {
	        printk("ufs_readdir: ino %lu  f_pos %lu\n",
	               inode->i_ino, (unsigned long) filp->f_pos);
	        ufs_print_inode(inode);
	}

	stored = 0;
	bh = NULL;
	offset = filp->f_pos & (sb->s_blocksize - 1);

	while (!error && !stored && filp->f_pos < inode->i_size) {
		lblk = (filp->f_pos) >> sb->s_blocksize_bits;
	        /* XXX - ufs_bmap() call needs error checking */
	        blk = ufs_bmap(inode, lblk);
		bh = bread (sb->s_dev, blk, sb->s_blocksize);
		if (!bh) {
	                /* XXX - error - skip to the next block */
	                printk("ufs_readdir: dir inode %lu has a hole at offset %lu\n",
	                       inode->i_ino, (unsigned long int)filp->f_pos);
			filp->f_pos += sb->s_blocksize - offset;
			continue;
		}

revalidate:
		/* If the dir block has changed since the last call to
		 * readdir(2), then we might be pointing to an invalid
		 * dirent right now.  Scan from the start of the block
		 * to make sure. */
		if (filp->f_version != inode->i_version) {
			for (i = 0; i < sb->s_blocksize && i < offset; ) {
				de = (struct ufs_direct *) 
					(bh->b_data + i);
				/* It's too expensive to do a full
				 * dirent test each time round this
				 * loop, but we do have to test at
				 * least that it is non-zero.  A
				 * failure will be detected in the
				 * dirent test below. */
				if (ufs_swab16(de->d_reclen) < 1)
					break;
				i += ufs_swab16(de->d_reclen);
			}
			offset = i;
			filp->f_pos = (filp->f_pos & ~(sb->s_blocksize - 1))
				| offset;
			filp->f_version = inode->i_version;
		}
		
		while (!error && filp->f_pos < inode->i_size 
		       && offset < sb->s_blocksize) {
			de = (struct ufs_direct *) (bh->b_data + offset);
	                /* XXX - put in a real ufs_check_dir_entry() */
	                if ((ufs_swab16(de->d_reclen) == 0)
                             || (ufs_swab16(de->d_namlen) == 0)) {
	                        filp->f_pos = (filp->f_pos & (sb->s_blocksize - 1)) + sb->s_blocksize;
	                        brelse(bh);
	                        return stored;
	                }
#if 0
			if (!ext2_check_dir_entry ("ext2_readdir", inode, de,
						   bh, offset)) {
				/* On error, skip the f_pos to the
	                           next block. */
				filp->f_pos = (filp->f_pos & (sb->s_blocksize - 1))
					      + sb->s_blocksize;
				brelse (bh);
				return stored;
			}
#endif /* XXX */
			offset += ufs_swab16(de->d_reclen);
			if (ufs_swab32(de->d_ino)) {
				/* We might block in the next section
				 * if the data destination is
				 * currently swapped out.  So, use a
				 * version stamp to detect whether or
				 * not the directory has been modified
				 * during the copy operation. */
				unsigned long version = inode->i_version;

	                        if (inode->i_sb->u.ufs_sb.s_flags & UFS_DEBUG) {
	                                printk("ufs_readdir: filldir(%s,%u)\n",
	                                       de->d_name, ufs_swab32(de->d_ino));
	                        }
				error = filldir(dirent, de->d_name, ufs_swab16(de->d_namlen), filp->f_pos, ufs_swab32(de->d_ino));
				if (error)
					break;
				if (version != inode->i_version)
					goto revalidate;
				stored ++;
			}
			filp->f_pos += ufs_swab16(de->d_reclen);
		}
		offset = 0;
		brelse (bh);
	}
#if 0 /* XXX */
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
#endif /* XXX */
	return 0;
}

static struct file_operations ufs_dir_operations = {
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	ufs_readdir,		/* readdir */
	NULL,			/* poll */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* release */
	file_fsync,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
};

struct inode_operations ufs_dir_inode_operations = {
	&ufs_dir_operations,	/* default directory file operations */
	NULL,			/* create */
	ufs_lookup,		/* lookup */
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
	NULL,			/* permission */
	NULL,			/* smap */
};
