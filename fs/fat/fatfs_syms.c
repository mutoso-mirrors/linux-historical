/*
 * linux/fs/fat/fatfs_syms.c
 *
 * Exported kernel symbols for the low-level FAT-based fs support.
 *
 */
#include <linux/module.h>

#include <linux/mm.h>
#include <linux/msdos_fs.h>

#include "msbuffer.h"
#include "tables.h"

extern struct file_operations fat_dir_operations;

EXPORT_SYMBOL(fat_a2alias);
EXPORT_SYMBOL(fat_a2uni);
EXPORT_SYMBOL(fat_add_cluster);
EXPORT_SYMBOL(fat_bmap);
EXPORT_SYMBOL(fat_brelse);
EXPORT_SYMBOL(fat_cache_inval_inode);
EXPORT_SYMBOL(fat_code2uni);
EXPORT_SYMBOL(fat_date_unix2dos);
EXPORT_SYMBOL(fat_dir_operations);
EXPORT_SYMBOL(fat_file_read);
EXPORT_SYMBOL(fat_file_write);
EXPORT_SYMBOL(fat_fs_panic);
EXPORT_SYMBOL(fat_get_entry);
EXPORT_SYMBOL(fat_lock_creation);
EXPORT_SYMBOL(fat_mark_buffer_dirty);
EXPORT_SYMBOL(fat_mmap);
EXPORT_SYMBOL(fat_notify_change);
EXPORT_SYMBOL(fat_parent_ino);
EXPORT_SYMBOL(fat_put_inode);
EXPORT_SYMBOL(fat_put_super);
EXPORT_SYMBOL(fat_read_inode);
EXPORT_SYMBOL(fat_read_super);
EXPORT_SYMBOL(fat_readdirx);
EXPORT_SYMBOL(fat_readdir);
EXPORT_SYMBOL(fat_scan);
EXPORT_SYMBOL(fat_smap);
EXPORT_SYMBOL(fat_statfs);
EXPORT_SYMBOL(fat_truncate);
EXPORT_SYMBOL(fat_uni2asc_pg);
EXPORT_SYMBOL(fat_uni2code);
EXPORT_SYMBOL(fat_unlock_creation);
EXPORT_SYMBOL(fat_write_inode);

int init_fat_fs(void)
{
	return 0;
}
