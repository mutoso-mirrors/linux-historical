/*
 * linux/fs/ext3/xattr_user.c
 * Handler for extended user attributes.
 *
 * Copyright (C) 2001 by Andreas Gruenbacher, <a.gruenbacher@computer.org>
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/smp_lock.h>
#include <linux/ext3_jbd.h>
#include <linux/ext3_fs.h>
#include "xattr.h"

#ifdef CONFIG_EXT3_FS_POSIX_ACL
# include "acl.h"
#endif

#define XATTR_USER_PREFIX "user."

static size_t
ext3_xattr_user_list(char *list, struct inode *inode,
		     const char *name, int name_len, int flags)
{
	const int prefix_len = sizeof(XATTR_USER_PREFIX)-1;

	if (!(flags & XATTR_KERNEL_CONTEXT) &&
	    !test_opt(inode->i_sb, XATTR_USER))
		return 0;

	if (list) {
		memcpy(list, XATTR_USER_PREFIX, prefix_len);
		memcpy(list+prefix_len, name, name_len);
		list[prefix_len + name_len] = '\0';
	}
	return prefix_len + name_len + 1;
}

static int
ext3_xattr_user_get(struct inode *inode, const char *name,
		    void *buffer, size_t size, int flags)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	if (!(flags & XATTR_KERNEL_CONTEXT)) {
		int error;

		if (!test_opt(inode->i_sb, XATTR_USER))
			return -EOPNOTSUPP;
#ifdef CONFIG_EXT3_FS_POSIX_ACL
		error = ext3_permission_locked(inode, MAY_READ);
#else
		error = permission(inode, MAY_READ);
#endif
		if (error)
			return error;
	}
	return ext3_xattr_get(inode, EXT3_XATTR_INDEX_USER, name,
			      buffer, size);
}

static int
ext3_xattr_user_set(struct inode *inode, const char *name,
		    const void *value, size_t size, int flags)
{
	if (strcmp(name, "") == 0)
		return -EINVAL;
	if ( !S_ISREG(inode->i_mode) &&
	    (!S_ISDIR(inode->i_mode) || inode->i_mode & S_ISVTX))
		return -EPERM;
	if (!(flags & XATTR_KERNEL_CONTEXT)) {
		int error;

		if (!test_opt(inode->i_sb, XATTR_USER))
			return -EOPNOTSUPP;
#ifdef CONFIG_EXT3_FS_POSIX_ACL
		error = ext3_permission_locked(inode, MAY_WRITE);
#else
		error = permission(inode, MAY_WRITE);
#endif
		if (error)
			return error;
	}
	return ext3_xattr_set(inode, EXT3_XATTR_INDEX_USER, name,
			      value, size, flags);
}

struct ext3_xattr_handler ext3_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.list	= ext3_xattr_user_list,
	.get	= ext3_xattr_user_get,
	.set	= ext3_xattr_user_set,
};
