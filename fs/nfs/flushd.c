/*
 * linux/fs/nfs/flushd.c
 *
 * For each NFS mount, there is a separate cache object that contains
 * a hash table of all clusters. With this cache, an async RPC task
 * (`flushd') is associated, which wakes up occasionally to inspect
 * its list of dirty buffers.
 * (Note that RPC tasks aren't kernel threads. Take a look at the
 * rpciod code to understand what they are).
 *
 * Inside the cache object, we also maintain a count of the current number
 * of dirty pages, which may not exceed a certain threshold.
 * (FIXME: This threshold should be configurable).
 *
 * The code is streamlined for what I think is the prevalent case for
 * NFS traffic, which is sequential write access without concurrent
 * access by different processes.
 *
 * Copyright (C) 1996, 1997, Olaf Kirch <okir@monad.swb.de>
 *
 * Rewritten 6/3/2000 by Trond Myklebust
 * Copyright (C) 1999, 2000, Trond Myklebust <trond.myklebust@fys.uio.no>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/malloc.h>
#include <linux/pagemap.h>
#include <linux/file.h>

#include <linux/sched.h>

#include <linux/sunrpc/auth.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/sched.h>

#include <linux/smp_lock.h>

#include <linux/nfs.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_fs_sb.h>
#include <linux/nfs_flushd.h>
#include <linux/nfs_mount.h>

/*
 * Various constants
 */
#define NFSDBG_FACILITY         NFSDBG_PAGECACHE

/*
 * This is the wait queue all cluster daemons sleep on
 */
static struct rpc_wait_queue    flushd_queue = RPC_INIT_WAITQ("nfs_flushd");

/*
 * Local function declarations.
 */
static void	nfs_flushd(struct rpc_task *);
static void	nfs_flushd_exit(struct rpc_task *);


int nfs_reqlist_init(struct nfs_server *server)
{
	struct nfs_reqlist	*cache;
	struct rpc_task		*task;
	int			status;

	dprintk("NFS: writecache_init\n");

	lock_kernel();
	status = -ENOMEM;
	/* Create the RPC task */
	if (!(task = rpc_new_task(server->client, NULL, RPC_TASK_ASYNC)))
		goto out_unlock;

	cache = server->rw_requests;

	status = 0;
	if (cache->task)
		goto out_unlock;

	task->tk_calldata = server;

	cache->task = task;

	/* Run the task */
	cache->runat = jiffies;

	cache->auth = server->client->cl_auth;
	task->tk_action   = nfs_flushd;
	task->tk_exit   = nfs_flushd_exit;

	rpc_execute(task);
	unlock_kernel();
	return 0;
 out_unlock:
	if (task)
		rpc_release_task(task);
	unlock_kernel();
	return status;
}

void nfs_reqlist_exit(struct nfs_server *server)
{
	struct nfs_reqlist      *cache;

	lock_kernel();
	cache = server->rw_requests;
	if (!cache)
		goto out;

	dprintk("NFS: reqlist_exit (ptr %p rpc %p)\n", cache, cache->task);

	while (cache->task || cache->inodes) {
		if (!cache->task) {
			nfs_reqlist_init(server);
		} else {
			cache->task->tk_status = -ENOMEM;
			rpc_wake_up_task(cache->task);
		}
		interruptible_sleep_on_timeout(&cache->request_wait, 1 * HZ);
	}
 out:
	unlock_kernel();
}

int nfs_reqlist_alloc(struct nfs_server *server)
{
	struct nfs_reqlist	*cache;
	if (server->rw_requests)
		return 0;

	cache = (struct nfs_reqlist *)kmalloc(sizeof(*cache), GFP_KERNEL);
	if (!cache)
		return -ENOMEM;

	memset(cache, 0, sizeof(*cache));
	atomic_set(&cache->nr_requests, 0);
	init_waitqueue_head(&cache->request_wait);
	server->rw_requests = cache;

	return 0;
}

void nfs_reqlist_free(struct nfs_server *server)
{
	if (server->rw_requests) {
		kfree(server->rw_requests);
		server->rw_requests = NULL;
	}
}

void nfs_wake_flushd()
{
	rpc_wake_up_status(&flushd_queue, -ENOMEM);
}

static void inode_append_flushd(struct inode *inode)
{
	struct nfs_reqlist	*cache = NFS_REQUESTLIST(inode);
	struct inode		**q;

	if (NFS_FLAGS(inode) & NFS_INO_FLUSH)
		goto out;
	inode->u.nfs_i.hash_next = NULL;

	q = &cache->inodes;
	while (*q)
		q = &(*q)->u.nfs_i.hash_next;
	*q = inode;

	/* Note: we increase the inode i_count in order to prevent
	 *	 it from disappearing when on the flush list
	 */
	NFS_FLAGS(inode) |= NFS_INO_FLUSH;
	atomic_inc(&inode->i_count);
 out:
}

void inode_remove_flushd(struct inode *inode)
{
	struct nfs_reqlist	*cache = NFS_REQUESTLIST(inode);
	struct inode		**q;

	lock_kernel();
	if (!(NFS_FLAGS(inode) & NFS_INO_FLUSH))
		goto out;

	q = &cache->inodes;
	while (*q && *q != inode)
		q = &(*q)->u.nfs_i.hash_next;
	if (*q) {
		*q = inode->u.nfs_i.hash_next;
		NFS_FLAGS(inode) &= ~NFS_INO_FLUSH;
		iput(inode);
	}
 out:
	unlock_kernel();
}

void inode_schedule_scan(struct inode *inode, unsigned long time)
{
	struct nfs_reqlist	*cache = NFS_REQUESTLIST(inode);
	struct rpc_task		*task;
	unsigned long		mintimeout;

	lock_kernel();
	if (time_after(NFS_NEXTSCAN(inode), time))
		NFS_NEXTSCAN(inode) = time;
	mintimeout = jiffies + 1 * HZ;
	if (time_before(mintimeout, NFS_NEXTSCAN(inode)))
		mintimeout = NFS_NEXTSCAN(inode);
	inode_append_flushd(inode);

	task = cache->task;
	if (!task) {
		nfs_reqlist_init(NFS_SERVER(inode));
	} else {
		if (time_after(cache->runat, mintimeout))
			rpc_wake_up_task(task);
	}
	unlock_kernel();
}


static void
nfs_flushd(struct rpc_task *task)
{
	struct nfs_server	*server;
	struct nfs_reqlist	*cache;
	struct inode		*inode, *next;
	unsigned long		delay = jiffies + NFS_WRITEBACK_LOCKDELAY;
	int			flush = (task->tk_status == -ENOMEM);

        dprintk("NFS: %4d flushd starting\n", task->tk_pid);
	server = (struct nfs_server *) task->tk_calldata;
        cache = server->rw_requests;

	next = cache->inodes;
	cache->inodes = NULL;

	while ((inode = next) != NULL) {
		next = next->u.nfs_i.hash_next;
		inode->u.nfs_i.hash_next = NULL;
		NFS_FLAGS(inode) &= ~NFS_INO_FLUSH;

		if (flush) {
			nfs_pagein_inode(inode, 0, 0);
			nfs_sync_file(inode, NULL, 0, 0, FLUSH_AGING);
		} else if (time_after(jiffies, NFS_NEXTSCAN(inode))) {
			NFS_NEXTSCAN(inode) = jiffies + NFS_WRITEBACK_LOCKDELAY;
			nfs_pagein_timeout(inode);
			nfs_flush_timeout(inode, FLUSH_AGING);
#ifdef CONFIG_NFS_V3
			nfs_commit_timeout(inode, FLUSH_AGING);
#endif
		}

		if (nfs_have_writebacks(inode) || nfs_have_read(inode)) {
			inode_append_flushd(inode);
			if (time_after(delay, NFS_NEXTSCAN(inode)))
				delay = NFS_NEXTSCAN(inode);
		}
		iput(inode);
	}

	dprintk("NFS: %4d flushd back to sleep\n", task->tk_pid);
	if (time_after(jiffies + 1 * HZ, delay))
		delay = 1 * HZ;
	else
		delay = delay - jiffies;
	task->tk_status = 0;
	task->tk_action = nfs_flushd;
	task->tk_timeout = delay;
	cache->runat = jiffies + task->tk_timeout;

	if (!atomic_read(&cache->nr_requests) && !cache->inodes) {
		cache->task = NULL;
		task->tk_action = NULL;
	} else
		rpc_sleep_on(&flushd_queue, task, NULL, NULL);
}

static void
nfs_flushd_exit(struct rpc_task *task)
{
	struct nfs_server	*server;
	struct nfs_reqlist	*cache;
	server = (struct nfs_server *) task->tk_calldata;
	cache = server->rw_requests;

	if (cache->task == task)
		cache->task = NULL;
	wake_up(&cache->request_wait);
}

