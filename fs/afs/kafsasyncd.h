/* kafsasyncd.h: AFS asynchronous operation daemon
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_KAFSASYNCD_H
#define _LINUX_AFS_KAFSASYNCD_H

#include "types.h"

struct afs_async_op_ops {
	void (*attend)(afs_async_op_t *op);
	void (*discard)(afs_async_op_t *op);
};

/*****************************************************************************/
/*
 * asynchronous operation record
 */
struct afs_async_op
{
	struct list_head		link;
	afs_server_t			*server;	/* server being contacted */
	struct rxrpc_call		*call;		/* RxRPC call performing op */
	wait_queue_t			waiter;		/* wait queue for kafsasyncd */
	const struct afs_async_op_ops	*ops;		/* operations */
};

static inline void afs_async_op_init(afs_async_op_t *op, const struct afs_async_op_ops *ops)
{
	INIT_LIST_HEAD(&op->link);
	op->call = NULL;
	op->ops = ops;
}

extern int afs_kafsasyncd_start(void);
extern void afs_kafsasyncd_stop(void);

extern void afs_kafsasyncd_begin_op(afs_async_op_t *op);
extern void afs_kafsasyncd_attend_op(afs_async_op_t *op);
extern void afs_kafsasyncd_terminate_op(afs_async_op_t *op);

#endif /* _LINUX_AFS_KAFSASYNCD_H */
