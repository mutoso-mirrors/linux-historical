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
 * or the like.  Any license provided herein, whether implied or
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

static void sync_timeout(unsigned long __data)
{
	struct task_struct * p = (struct task_struct *) __data;

	wake_up_process(p);
}

#define SYNCD_FLAGS	(SYNC_FSDATA|SYNC_BDFLUSH|SYNC_ATTR)

int syncd(void *arg)
{
	vfs_t			*vfsp = (vfs_t *) arg;
	int			error;
	struct timer_list	timer;

	daemonize("xfs_syncd");

	vfsp->vfs_sync_task = current;
	wmb();
	wake_up(&vfsp->vfs_wait_sync_task);

	init_timer(&timer);
	timer.data = (unsigned long)current;
	timer.function = sync_timeout;

	do {
		mod_timer(&timer, jiffies + xfs_params.sync_interval);
		interruptible_sleep_on(&vfsp->vfs_sync);

		if (!(vfsp->vfs_flag & VFS_RDONLY))
			VFS_SYNC(vfsp, SYNCD_FLAGS, NULL, error);
	} while (!(vfsp->vfs_flag & VFS_UMOUNT));

	del_timer_sync(&timer);
	vfsp->vfs_sync_task = NULL;
	wmb();
	wake_up(&vfsp->vfs_wait_sync_task);
	return 0;
}

int
linvfs_start_syncd(vfs_t *vfsp)
{
	int pid;

	pid = kernel_thread(syncd, (void *) vfsp,
			CLONE_VM | CLONE_FS | CLONE_FILES);
	if (pid < 0)
		return pid;
	wait_event(vfsp->vfs_wait_sync_task, vfsp->vfs_sync_task);
	return 0;
}

void
linvfs_stop_syncd(vfs_t *vfsp)
{
	vfsp->vfs_flag |= VFS_UMOUNT;
	wmb();

	wake_up(&vfsp->vfs_sync);
	wait_event(vfsp->vfs_wait_sync_task, !vfsp->vfs_sync_task);
}
