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

#include <xfs.h>

int
vfs_mount(bhv_desc_t *bdp, struct xfs_mount_args *args, struct cred *cr)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_mount)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_mount)(next, args, cr));
}

int
vfs_parseargs(bhv_desc_t *bdp, char *s, struct xfs_mount_args *args, int f)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_parseargs)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_parseargs)(next, s, args, f));
}

int
vfs_showargs(bhv_desc_t *bdp, struct seq_file *m)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_showargs)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_showargs)(next, m));
}

int
vfs_unmount(bhv_desc_t *bdp, int fl, struct cred *cr)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_unmount)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_unmount)(next, fl, cr));
}

int
vfs_root(bhv_desc_t *bdp, struct vnode **vpp)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_root)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_root)(next, vpp));
}

int
vfs_statvfs(bhv_desc_t *bdp, struct statfs *sp, struct vnode *vp)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_statvfs)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_statvfs)(next, sp, vp));
}

int
vfs_sync(bhv_desc_t *bdp, int fl, struct cred *cr)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_sync)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_sync)(next, fl, cr));
}

int
vfs_vget(bhv_desc_t *bdp, struct vnode **vpp, struct fid *fidp)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_vget)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_vget)(next, vpp, fidp));
}

int
vfs_dmapiops(bhv_desc_t *bdp, caddr_t addr)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_dmapiops)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_dmapiops)(next, addr));
}

int
vfs_quotactl(bhv_desc_t *bdp, int cmd, int id, caddr_t addr)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_quotactl)
		next = BHV_NEXT(next);
	return ((*bhvtovfsops(next)->vfs_quotactl)(next, cmd, id, addr));
}

void
vfs_init_vnode(bhv_desc_t *bdp, struct vnode *vp, bhv_desc_t *bp, int unlock)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_init_vnode)
		next = BHV_NEXT(next);
	((*bhvtovfsops(next)->vfs_init_vnode)(next, vp, bp, unlock));
}

void
vfs_force_shutdown(bhv_desc_t *bdp, int fl, char *file, int line)
{
	bhv_desc_t *next = bdp;
	ASSERT(next);
	while (! (bhvtovfsops(next))->vfs_force_shutdown)
		next = BHV_NEXT(next);
	((*bhvtovfsops(next)->vfs_force_shutdown)(next, fl, file, line));
}

vfs_t *
vfs_allocate(void)
{
	vfs_t *vfsp = kmem_zalloc(sizeof(vfs_t), KM_SLEEP);
	bhv_head_init(VFS_BHVHEAD(vfsp), "vfs");
	return vfsp;
}

void
vfs_deallocate(vfs_t *vfsp)
{
	bhv_head_destroy(VFS_BHVHEAD(vfsp));
	kmem_free(vfsp, sizeof(vfs_t));
}

void
vfs_insertops(vfs_t *vfsp, vfsops_t *vfsops)
{
	bhv_desc_t *bdp = kmem_alloc(sizeof(bhv_desc_t), KM_SLEEP);
	bhv_desc_init(bdp, NULL, vfsp, vfsops);
	bhv_insert(&vfsp->vfs_bh, bdp);
}

void
vfs_insertbhv(vfs_t *vfsp, bhv_desc_t *bdp, vfsops_t *vfsops, void *mount)
{
	bhv_desc_init(bdp, mount, vfsp, vfsops);
	bhv_insert_initial(&vfsp->vfs_bh, bdp);
}
