/* r128_drv.c -- ATI Rage 128 driver -*- linux-c -*-
 * Created: Mon Dec 13 09:47:27 1999 by faith@precisioninsight.com
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
 *    Gareth Hughes <gareth@valinux.com>
 */

#include <linux/config.h>
#include "drmP.h"
#include "drm.h"
#include "r128_drm.h"
#include "r128_drv.h"
#include "ati_pcigart.h"

#include "drm_pciids.h"

static int postinit( struct drm_device *dev, unsigned long flags )
{
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
	r128_PCI_IDS
};

/* Interface history:
 *
 * ??  - ??
 * 2.4 - Add support for ycbcr textures (no new ioctls)
 * 2.5 - Add FLIP ioctl, disable FULLSCREEN.
 */
static drm_ioctl_desc_t ioctls[] = {
   [DRM_IOCTL_NR(DRM_R128_INIT)]       = { r128_cce_init,     1, 1 },
   [DRM_IOCTL_NR(DRM_R128_CCE_START)]  = { r128_cce_start,    1, 1 },
   [DRM_IOCTL_NR(DRM_R128_CCE_STOP)]   = { r128_cce_stop,     1, 1 },
   [DRM_IOCTL_NR(DRM_R128_CCE_RESET)]  = { r128_cce_reset,    1, 1 },
   [DRM_IOCTL_NR(DRM_R128_CCE_IDLE)]   = { r128_cce_idle,     1, 0 },
   [DRM_IOCTL_NR(DRM_R128_RESET)]      = { r128_engine_reset, 1, 0 },
   [DRM_IOCTL_NR(DRM_R128_FULLSCREEN)] = { r128_fullscreen,   1, 0 },
   [DRM_IOCTL_NR(DRM_R128_SWAP)]       = { r128_cce_swap,     1, 0 },
   [DRM_IOCTL_NR(DRM_R128_FLIP)]       = { r128_cce_flip,     1, 0 },
   [DRM_IOCTL_NR(DRM_R128_CLEAR)]      = { r128_cce_clear,    1, 0 },
   [DRM_IOCTL_NR(DRM_R128_VERTEX)]     = { r128_cce_vertex,   1, 0 },
   [DRM_IOCTL_NR(DRM_R128_INDICES)]    = { r128_cce_indices,  1, 0 },
   [DRM_IOCTL_NR(DRM_R128_BLIT)]       = { r128_cce_blit,     1, 0 },
   [DRM_IOCTL_NR(DRM_R128_DEPTH)]      = { r128_cce_depth,    1, 0 },
   [DRM_IOCTL_NR(DRM_R128_STIPPLE)]    = { r128_cce_stipple,  1, 0 },
   [DRM_IOCTL_NR(DRM_R128_INDIRECT)]   = { r128_cce_indirect, 1, 1 },
   [DRM_IOCTL_NR(DRM_R128_GETPARAM)]   = { r128_getparam, 1, 0 },
};

static struct drm_driver driver = {
	.driver_features = DRIVER_USE_AGP | DRIVER_USE_MTRR | DRIVER_PCI_DMA | DRIVER_SG | DRIVER_HAVE_DMA | DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_IRQ_VBL,
	.dev_priv_size = sizeof(drm_r128_buf_priv_t),
	.prerelease = r128_driver_prerelease,
	.pretakedown = r128_driver_pretakedown,
	.vblank_wait = r128_driver_vblank_wait,
	.irq_preinstall = r128_driver_irq_preinstall,
	.irq_postinstall = r128_driver_irq_postinstall,
	.irq_uninstall = r128_driver_irq_uninstall,
	.irq_handler = r128_driver_irq_handler,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.postinit = postinit,
	.version = version,
	.ioctls = ioctls,
	.num_ioctls = DRM_ARRAY_SIZE(ioctls),
	.dma_ioctl = r128_cce_buffers,
	.fops = {
		.owner = THIS_MODULE,
		.open = drm_open,
		.release = drm_release,
		.ioctl = drm_ioctl,
		.mmap = drm_mmap,
		.poll = drm_poll,
		.fasync = drm_fasync,
	},
	.pci_driver = {
		.name          = DRIVER_NAME,
		.id_table      = pciidlist,
	}
};

static int __init r128_init(void)
{
	return drm_init(&driver);
}

static void __exit r128_exit(void)
{
	drm_exit(&driver);
}

module_init(r128_init);
module_exit(r128_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL and additional rights");
