/*
 *  linux/fs/ext2/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  ext2 fs regular file handling primitives
 *
 *  64-bit file support on 64-bit platforms by Jakub Jelinek
 * 	(jj@sunsite.ms.mff.cuni.cz)
 */

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/ext2_fs.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/locks.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/smp_lock.h>

#define	NBUF	32

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

static int ext2_writepage (struct file * file, struct page * page);
static long long ext2_file_lseek(struct file *, long long, int);
#if BITS_PER_LONG < 64
static int ext2_open_file (struct inode *, struct file *);

#else

#define EXT2_MAX_SIZE(bits)							\
	(((EXT2_NDIR_BLOCKS + (1LL << (bits - 2)) + 				\
	   (1LL << (bits - 2)) * (1LL << (bits - 2)) + 				\
	   (1LL << (bits - 2)) * (1LL << (bits - 2)) * (1LL << (bits - 2))) * 	\
	  (1LL << bits)) - 1)

static long long ext2_max_sizes[] = {
0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 
EXT2_MAX_SIZE(10), EXT2_MAX_SIZE(11), EXT2_MAX_SIZE(12), EXT2_MAX_SIZE(13)
};

#endif


/*
 * Make sure the offset never goes beyond the 32-bit mark..
 */
static long long ext2_file_lseek(
	struct file *file,
	long long offset,
	int origin)
{
	struct inode *inode = file->f_dentry->d_inode;

	switch (origin) {
		case 2:
			offset += inode->i_size;
			break;
		case 1:
			offset += file->f_pos;
	}
	if (((unsigned long long) offset >> 32) != 0) {
#if BITS_PER_LONG < 64
		return -EINVAL;
#else
		if (offset > ext2_max_sizes[EXT2_BLOCK_SIZE_BITS(inode->i_sb)])
			return -EINVAL;
#endif
	} 
	if (offset != file->f_pos) {
		file->f_pos = offset;
		file->f_reada = 0;
		file->f_version = ++event;
	}
	return offset;
}

static inline void remove_suid(struct inode *inode)
{
	unsigned int mode;

	/* set S_IGID if S_IXGRP is set, and always set S_ISUID */
	mode = (inode->i_mode & S_IXGRP)*(S_ISGID/S_IXGRP) | S_ISUID;

	/* was any of the uid bits set? */
	mode &= inode->i_mode;
	if (mode && !capable(CAP_FSETID)) {
		inode->i_mode &= ~mode;
		mark_inode_dirty(inode);
	}
}

static int ext2_writepage (struct file * file, struct page * page)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	unsigned long block;
	int *p, nr[PAGE_SIZE/512];
	int i, err, created;
	struct buffer_head *bh;

	i = PAGE_SIZE >> inode->i_sb->s_blocksize_bits;
	block = page->offset >> inode->i_sb->s_blocksize_bits;
	p = nr;
	bh = page->buffers;
	do {
		if (bh && bh->b_blocknr)
			*p = bh->b_blocknr;
		else
			*p = ext2_getblk_block (inode, block, 1, &err, &created);
		if (!*p)
			return -EIO;
		i--;
		block++;
		p++;
		if (bh)
			bh = bh->b_this_page;
	} while (i > 0);

	/* IO start */
	brw_page(WRITE, page, inode->i_dev, nr, inode->i_sb->s_blocksize, 1);
	return 0;
}

static long ext2_write_one_page (struct file *file, struct page *page, unsigned long offset, unsigned long bytes, const char * buf)
{
	return block_write_one_page(file, page, offset, bytes, buf, ext2_getblk_block);
}

/*
 * Write to a file (through the page cache).
 */
static ssize_t
ext2_file_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	return generic_file_write(file, buf, count, ppos, ext2_write_one_page);
}

/*
 * Called when an inode is released. Note that this is different
 * from ext2_file_open: open gets called at every open, but release
 * gets called only when /all/ the files are closed.
 */
static int ext2_release_file (struct inode * inode, struct file * filp)
{
	if (filp->f_mode & FMODE_WRITE)
		ext2_discard_prealloc (inode);
	return 0;
}

#if BITS_PER_LONG < 64
/*
 * Called when an inode is about to be open.
 * We use this to disallow opening RW large files on 32bit systems.
 */
static int ext2_open_file (struct inode * inode, struct file * filp)
{
	if (inode->u.ext2_i.i_high_size && (filp->f_mode & FMODE_WRITE))
		return -EFBIG;
	return 0;
}
#endif

/*
 * We have mostly NULL's here: the current defaults are ok for
 * the ext2 filesystem.
 */
static struct file_operations ext2_file_operations = {
	ext2_file_lseek,	/* lseek */
	generic_file_read,	/* read */
	ext2_file_write,	/* write */
	NULL,			/* readdir - bad */
	NULL,			/* poll - default */
	ext2_ioctl,		/* ioctl */
	generic_file_mmap,	/* mmap */
#if BITS_PER_LONG == 64	
	NULL,			/* no special open is needed */
#else
	ext2_open_file,
#endif
	NULL,			/* flush */
	ext2_release_file,	/* release */
	ext2_sync_file,		/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

struct inode_operations ext2_file_inode_operations = {
	&ext2_file_operations,/* default file operations */
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
	NULL,			/* follow_link */
	generic_readpage,	/* readpage */
	ext2_writepage,		/* writepage */
	ext2_bmap,		/* bmap */
	ext2_truncate,		/* truncate */
	ext2_permission,	/* permission */
	NULL,			/* smap */
	NULL,			/* updatepage */
	NULL,			/* revalidate */
	generic_block_flushpage,/* flushpage */
};
