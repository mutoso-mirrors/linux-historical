/*
 * linux/fs/msdos/vfatfs_syms.c
 *
 * Exported kernel symbols for the VFAT filesystem.
 * These symbols are used by dmsdos.
 */

#define ASC_LINUX_VERSION(V, P, S)	(((V) * 65536) + ((P) * 256) + (S))
#include <linux/version.h>
#include <linux/module.h>

#include <linux/mm.h>
#include <linux/msdos_fs.h>

DECLARE_FSTYPE_DEV(vfat_fs_type, "vfat", vfat_read_super);

EXPORT_SYMBOL(vfat_create);
EXPORT_SYMBOL(vfat_unlink);
EXPORT_SYMBOL(vfat_mkdir);
EXPORT_SYMBOL(vfat_rmdir);
EXPORT_SYMBOL(vfat_rename);
EXPORT_SYMBOL(vfat_read_super);
EXPORT_SYMBOL(vfat_lookup);

int init_vfat_fs(void)
{
	return register_filesystem(&vfat_fs_type);
}

