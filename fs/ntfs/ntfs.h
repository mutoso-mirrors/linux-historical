/*
 * ntfs.h - Defines for NTFS Linux kernel driver. Part of the Linux-NTFS
 *	    project.
 *
 * Copyright (c) 2001-2004 Anton Altaparmakov
 * Copyright (C) 2002 Richard Russon
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _LINUX_NTFS_H
#define _LINUX_NTFS_H

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/compiler.h>
#include <linux/fs.h>
#include <linux/nls.h>
#include <linux/smp.h>

#include "types.h"
#include "volume.h"
#include "layout.h"

typedef enum {
	NTFS_BLOCK_SIZE		= 512,
	NTFS_BLOCK_SIZE_BITS	= 9,
	NTFS_SB_MAGIC		= 0x5346544e,	/* 'NTFS' */
	NTFS_MAX_NAME_LEN	= 255,
} NTFS_CONSTANTS;

/* Global variables. */

/* Slab caches (from super.c). */
extern kmem_cache_t *ntfs_name_cache;
extern kmem_cache_t *ntfs_inode_cache;
extern kmem_cache_t *ntfs_big_inode_cache;
extern kmem_cache_t *ntfs_attr_ctx_cache;
extern kmem_cache_t *ntfs_index_ctx_cache;

/* The various operations structs defined throughout the driver files. */
extern struct super_operations ntfs_sops;
extern struct address_space_operations ntfs_aops;
extern struct address_space_operations ntfs_mst_aops;
extern struct address_space_operations ntfs_mft_aops;

extern struct  file_operations ntfs_file_ops;
extern struct inode_operations ntfs_file_inode_ops;

extern struct  file_operations ntfs_dir_ops;
extern struct inode_operations ntfs_dir_inode_ops;

extern struct  file_operations ntfs_empty_file_ops;
extern struct inode_operations ntfs_empty_inode_ops;

/**
 * NTFS_SB - return the ntfs volume given a vfs super block
 * @sb:		VFS super block
 *
 * NTFS_SB() returns the ntfs volume associated with the VFS super block @sb.
 */
static inline ntfs_volume *NTFS_SB(struct super_block *sb)
{
	return sb->s_fs_info;
}

/* Declarations of functions and global variables. */

/* From fs/ntfs/compress.c */
extern int ntfs_read_compressed_block(struct page *page);
extern int allocate_compression_buffers(void);
extern void free_compression_buffers(void);

/* From fs/ntfs/super.c */
#define default_upcase_len 0x10000
extern ntfschar *default_upcase;
extern unsigned long ntfs_nr_upcase_users;
extern struct semaphore ntfs_lock;

typedef struct {
	int val;
	char *str;
} option_t;
extern const option_t on_errors_arr[];

/* From fs/ntfs/mst.c */
extern int post_read_mst_fixup(NTFS_RECORD *b, const u32 size);
extern int pre_write_mst_fixup(NTFS_RECORD *b, const u32 size);
extern void post_write_mst_fixup(NTFS_RECORD *b);

/* From fs/ntfs/unistr.c */
extern BOOL ntfs_are_names_equal(const ntfschar *s1, size_t s1_len,
		const ntfschar *s2, size_t s2_len,
		const IGNORE_CASE_BOOL ic,
		const ntfschar *upcase, const u32 upcase_size);
extern int ntfs_collate_names(const ntfschar *name1, const u32 name1_len,
		const ntfschar *name2, const u32 name2_len,
		const int err_val, const IGNORE_CASE_BOOL ic,
		const ntfschar *upcase, const u32 upcase_len);
extern int ntfs_ucsncmp(const ntfschar *s1, const ntfschar *s2, size_t n);
extern int ntfs_ucsncasecmp(const ntfschar *s1, const ntfschar *s2, size_t n,
		const ntfschar *upcase, const u32 upcase_size);
extern void ntfs_upcase_name(ntfschar *name, u32 name_len,
		const ntfschar *upcase, const u32 upcase_len);
extern void ntfs_file_upcase_value(FILE_NAME_ATTR *file_name_attr,
		const ntfschar *upcase, const u32 upcase_len);
extern int ntfs_file_compare_values(FILE_NAME_ATTR *file_name_attr1,
		FILE_NAME_ATTR *file_name_attr2,
		const int err_val, const IGNORE_CASE_BOOL ic,
		const ntfschar *upcase, const u32 upcase_len);
extern int ntfs_nlstoucs(const ntfs_volume *vol, const char *ins,
		const int ins_len, ntfschar **outs);
extern int ntfs_ucstonls(const ntfs_volume *vol, const ntfschar *ins,
		const int ins_len, unsigned char **outs, int outs_len);

/* From fs/ntfs/upcase.c */
extern ntfschar *generate_default_upcase(void);

#endif /* _LINUX_NTFS_H */
