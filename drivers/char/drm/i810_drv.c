/* i810_drv.c -- I810 driver -*- linux-c -*-
 * Created: Mon Dec 13 01:56:22 1999 by jhartmann@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Rickard E. (Rik) Faith <faith@valinux.com>
 *    Jeff Hartmann <jhartmann@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/config.h>
#include "drmP.h"
#include "drm.h"
#include "i810_drm.h"
#include "i810_drv.h"

#include "drm_pciids.h"

static int postinit( struct drm_device *dev, unsigned long flags )
{
	/* i810 has 4 more counters */
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;
	
	DRM_INFO( "Initialized %s %d.%d.%d %s on minor %d: %s\n",
		DRIVER_NAME,
		DRIVER_MAJOR,
		DRIVER_MINOR,
		DRIVER_PATCHLEVEL,
		DRIVER_DATE,
		dev->minor,
		pci_pretty_name(dev->pdev)
		);
	return 0;
}

static int version( drm_version_t *version )
{
	int len;

	version->version_major = DRIVER_MAJOR;
	version->version_minor = DRIVER_MINOR;
	version->version_patchlevel = DRIVER_PATCHLEVEL;
	DRM_COPY( version->name, DRIVER_NAME );
	DRM_COPY( version->date, DRIVER_DATE );
	DRM_COPY( version->desc, DRIVER_DESC );
	return 0;
}

static struct pci_device_id pciidlist[] = {
	i810_PCI_IDS
};

static drm_ioctl_desc_t ioctls[] = {
	[DRM_IOCTL_NR(DRM_I810_INIT)]    = { i810_dma_init,    1, 1 },
	[DRM_IOCTL_NR(DRM_I810_VERTEX)]  = { i810_dma_vertex,  1, 0 },
	[DRM_IOCTL_NR(DRM_I810_CLEAR)]   = { i810_clear_bufs,  1, 0 },
	[DRM_IOCTL_NR(DRM_I810_FLUSH)]   = { i810_flush_ioctl, 1, 0 },
	[DRM_IOCTL_NR(DRM_I810_GETAGE)]  = { i810_getage,      1, 0 },
	[DRM_IOCTL_NR(DRM_I810_GETBUF)]  = { i810_getbuf,      1, 0 },
	[DRM_IOCTL_NR(DRM_I810_SWAP)]    = { i810_swap_bufs,   1, 0 },
	[DRM_IOCTL_NR(DRM_I810_COPY)]    = { i810_copybuf,     1, 0 },
	[DRM_IOCTL_NR(DRM_I810_DOCOPY)]  = { i810_docopy,      1, 0 },
	[DRM_IOCTL_NR(DRM_I810_OV0INFO)] = { i810_ov0_info,    1, 0 },
	[DRM_IOCTL_NR(DRM_I810_FSTATUS)] = { i810_fstatus,     1, 0 },
	[DRM_IOCTL_NR(DRM_I810_OV0FLIP)] = { i810_ov0_flip,    1, 0 },
	[DRM_IOCTL_NR(DRM_I810_MC)]      = { i810_dma_mc,      1, 1 },
	[DRM_IOCTL_NR(DRM_I810_RSTATUS)] = { i810_rstatus,     1, 0 },
	[DRM_IOCTL_NR(DRM_I810_FLIP)]    = { i810_flip_bufs,   1, 0 }
};

static struct drm_driver_fn driver_fn = {
	.driver_features = DRIVER_USE_AGP | DRIVER_REQUIRE_AGP | DRIVER_USE_MTRR | DRIVER_HAVE_DMA | DRIVER_DMA_QUEUE,
	.dev_priv_size = sizeof(drm_i810_buf_priv_t),
	.pretakedown = i810_driver_pretakedown,
	.release = i810_driver_release,
	.dma_quiescent = i810_driver_dma_quiescent,
	.reclaim_buffers = i810_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.postinit = postinit,
	.version = version,
	.ioctls = ioctls,
	.num_ioctls = DRM_ARRAY_SIZE(ioctls),
	.pci_driver = {
		.name          = DRIVER_NAME,
		.id_table      = pciidlist,
	},
};

static int __init i810_init(void)
{
	return drm_init(&driver_fn);
}

static void __exit i810_exit(void)
{
	drm_exit(&driver_fn);
}

module_init(i810_init);
module_exit(i810_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL and additional rights");
