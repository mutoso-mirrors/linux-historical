/*
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 *
 * Multipath.
 */

#ifndef	DM_MPATH_H
#define	DM_MPATH_H

#include <linux/device-mapper.h>

struct path {
	struct dm_dev *dev;	/* Read-only */
	unsigned is_active;	/* Read-only */

	void *pscontext;	/* For path-selector use */
};

#endif
