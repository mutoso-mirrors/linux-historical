/*
 *  linux/fs/ext2/symlink.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/symlink.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 symlink handling code
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/stat.h>

static int ext2_readlink (struct inode *, char *, int);
static struct dentry *ext2_follow_link(struct inode *, struct dentry *);

/*
 * symlinks can't do much...
 */
struct inode_operations ext2_symlink_inode_operations = {
	NULL,			/* no file-operations */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	ext2_readlink,		/* readlink */
	ext2_follow_link,	/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* smap */
};

static struct dentry * ext2_follow_link(struct inode * inode, struct dentry *base)
{
	int error;
	struct buffer_head * bh = NULL;
	char * link;

	link = (char *) inode->u.ext2_i.i_data;
	if (inode->i_blocks) {
		if (!(bh = ext2_bread (inode, 0, 0, &error))) {
			dput(base);
			return ERR_PTR(-EIO);
		}
		link = bh->b_data;
	}
	if (!IS_RDONLY(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	base = lookup_dentry(link, base, 1);
	if (bh)
		brelse(bh);
	return base;
}

static int ext2_readlink (struct inode * inode, char * buffer, int buflen)
{
	struct buffer_head * bh = NULL;
	char * link;
	int i;

	if (buflen > inode->i_sb->s_blocksize - 1)
		buflen = inode->i_sb->s_blocksize - 1;

	link = (char *) inode->u.ext2_i.i_data;
	if (inode->i_blocks) {
		int err;
		bh = ext2_bread (inode, 0, 0, &err);
		if (!bh) {
			if(err < 0) /* indicate type of error */
				return err;
			return 0;
		}
		link = bh->b_data;
	}

	i = 0;
	while (i < buflen && link[i])
		i++;
	if (copy_to_user(buffer, link, i))
		i = -EFAULT;
 	if (DO_UPDATE_ATIME(inode)) {
		inode->i_atime = CURRENT_TIME;
		inode->i_dirt = 1;
	}
	if (bh)
		brelse (bh);
	return i;
}
