/*
 * Copyright (c) 2000-2003 Silicon Graphics, Inc.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Further, this software is distributed without any warranty that it is
 * free of the rightful claim of any third person regarding infringement
 * or the like.	 Any license provided herein, whether implied or
 * otherwise, applies only to this software file.  Patent licenses, if
 * any, provided herein do not apply to combinations of this program with
 * other software, or any other product whatsoever.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Silicon Graphics, Inc., 1600 Amphitheatre Pkwy,
 * Mountain View, CA  94043, or:
 *
 * http://www.sgi.com
 *
 * For further information regarding this notice, see:
 *
 * http://oss.sgi.com/projects/GenInfo/SGIGPLNoticeExplan/
 */
#ifndef __XFS_SUPER_H__
#define __XFS_SUPER_H__

#ifdef CONFIG_XFS_POSIX_ACL
# define XFS_ACL_STRING		"ACLs, "
# define set_posix_acl_flag(sb)	((sb)->s_flags |= MS_POSIXACL)
#else
# define XFS_ACL_STRING
# define set_posix_acl_flag(sb)	do { } while (0)
#endif

#ifdef CONFIG_XFS_DMAPI
# define XFS_DMAPI_STRING	"DMAPI, "
# define vfs_insertdmapi(vfs)	vfs_insertops(vfsp, &xfs_dmops_xfs)
# define vfs_initdmapi()	(0)			/* temporarily */
# define vfs_exitdmapi()	do { } while (0)	/* temporarily */
#else
# define XFS_DMAPI_STRING
# define vfs_insertdmapi(vfs)	do { } while (0)
# define vfs_initdmapi()	(0)
# define vfs_exitdmapi()	do { } while (0)
#endif

#ifdef CONFIG_XFS_QUOTA
# define XFS_QUOTA_STRING	"quota, "
# define vfs_insertquota(vfs)	vfs_insertops(vfsp, &xfs_qmops_xfs)
# define vfs_initquota()	(0)			/* temporarily */
# define vfs_exitquota()	do { } while (0)	/* temporarily */
#else
# define XFS_QUOTA_STRING
# define vfs_insertquota(vfs)	do { } while (0)
# define vfs_initquota()	(0)
# define vfs_exitquota()	do { } while (0)
#endif

#ifdef CONFIG_XFS_RT
# define XFS_RT_STRING		"realtime, "
#else
# define XFS_RT_STRING
#endif

#ifdef CONFIG_XFS_VNODE_TRACING
# define XFS_VNTRACE_STRING	"VN-trace, "
#else
# define XFS_VNTRACE_STRING
#endif

#ifdef XFSDEBUG
# define XFS_DBG_STRING		"debug"
#else
# define XFS_DBG_STRING		"no debug"
#endif

#define XFS_BUILD_OPTIONS	XFS_ACL_STRING XFS_DMAPI_STRING \
				XFS_RT_STRING \
				XFS_QUOTA_STRING XFS_VNTRACE_STRING \
				XFS_DBG_STRING /* DBG must be last */

#define LINVFS_GET_VFS(s) \
	(vfs_t *)((s)->s_fs_info)
#define LINVFS_SET_VFS(s, vfsp) \
	((s)->s_fs_info = vfsp)

struct xfs_mount;
struct pb_target;
struct block_device;

extern int  xfs_parseargs(bhv_desc_t *, char *, struct xfs_mount_args *, int);
extern int  xfs_showargs(bhv_desc_t *, struct seq_file *);
extern void xfs_initialize_vnode(bhv_desc_t *, vnode_t *, bhv_desc_t *, int);

extern int  xfs_blkdev_get(struct xfs_mount *, const char *,
				struct block_device **);
extern void xfs_blkdev_put(struct block_device *);

extern struct pb_target *xfs_alloc_buftarg(struct block_device *);
extern void xfs_relse_buftarg(struct pb_target *);
extern void xfs_free_buftarg(struct pb_target *);

extern void xfs_setsize_buftarg(struct pb_target *, unsigned int, unsigned int);
extern unsigned int xfs_getsize_buftarg(struct pb_target *);

#endif	/* __XFS_SUPER_H__ */
