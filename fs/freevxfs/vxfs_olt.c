/*
 * Copyright (c) 2000-2001 Christoph Hellwig.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL").
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ident "$Id: vxfs_olt.c,v 1.9 2001/08/07 16:14:45 hch Exp hch $"

/* 
 * Veritas filesystem driver - object location table support.
 */
#include <linux/fs.h>
#include <linux/kernel.h>

#include "vxfs.h"
#include "vxfs_olt.h"


static __inline__ void
vxfs_get_fshead(struct vxfs_oltfshead *fshp, struct vxfs_sb_info *infp)
{
	if (infp->vsi_fshino)
		BUG();
	infp->vsi_fshino = fshp->olt_fsino[0];
}

static __inline__ void
vxfs_get_ilist(struct vxfs_oltilist *ilistp, struct vxfs_sb_info *infp)
{
	if (infp->vsi_iext)
		BUG();
	infp->vsi_iext = ilistp->olt_iext[0]; 
}

static __inline__ u_long
vxfs_oblock(struct super_block *sbp, daddr_t block, u_long bsize)
{
	if (sbp->s_blocksize % bsize)
		BUG();
	return (block * (sbp->s_blocksize / bsize));
}


/**
 * vxfs_read_olt - read olt
 * @sbp:	superblock of the filesystem
 * @bsize:	blocksize of the filesystem
 *
 * Description:
 *   vxfs_read_olt reads the olt of the filesystem described by @sbp
 *   into main memory and does some basic setup.
 *
 * Returns:
 *   Zero on success, else a negative error code.
 */
int
vxfs_read_olt(struct super_block *sbp, u_long bsize)
{
	struct vxfs_sb_info	*infp = VXFS_SBI(sbp);
	struct buffer_head	*bp;
	struct vxfs_olt		*op;
	char			*oaddr, *eaddr;


	bp = bread(sbp->s_dev,
			vxfs_oblock(sbp, infp->vsi_oltext, bsize), bsize);
	if (!bp || !bp->b_data)
		goto fail;

	op = (struct vxfs_olt *)bp->b_data;
	if (op->olt_magic != VXFS_OLT_MAGIC) {
		printk(KERN_NOTICE "vxfs: ivalid olt magic number\n");
		goto fail;
	}

	oaddr = (char *)bp->b_data + op->olt_size;
	eaddr = (char *)bp->b_data + (infp->vsi_oltsize * sbp->s_blocksize);

	while (oaddr < eaddr) {
		struct vxfs_oltcommon	*ocp =
			(struct vxfs_oltcommon *)oaddr;
		
		switch (ocp->olt_type) {
		case VXFS_OLT_FSHEAD:
			vxfs_get_fshead((struct vxfs_oltfshead *)oaddr, infp);
			break;
		case VXFS_OLT_ILIST:
			vxfs_get_ilist((struct vxfs_oltilist *)oaddr, infp);
			break;
		}

		oaddr += ocp->olt_size;
	}

	brelse(bp);
	return 0;

fail:
	brelse(bp);
	return -EINVAL;
}
