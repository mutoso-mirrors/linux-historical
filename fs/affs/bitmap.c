/*
 *  linux/fs/affs/bitmap.c
 *
 *  (c) 1996 Hans-Joachim Widmaier
 *
 *  bitmap.c contains the code that handles all bitmap related stuff -
 *  block allocation, deallocation, calculation of free space.
 */

#include <linux/time.h>
#include <linux/affs_fs.h>
#include <linux/stat.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/bitops.h>
#include <linux/amigaffs.h>

/* This is, of course, shamelessly stolen from fs/minix */

static int nibblemap[] = { 0,1,1,2,1,2,2,3,1,2,2,3,2,3,3,4 };

u32
affs_count_free_bits(u32 blocksize, const void *data)
{
	const u32 *map;
	u32 free;
	u32 tmp;

	map = data;
	free = 0;
	for (blocksize /= 4; blocksize > 0; blocksize--) {
		tmp = *map++;
		while (tmp) {
			free += nibblemap[tmp & 0xf];
			tmp >>= 4;
		}
	}

	return free;
}

u32
affs_count_free_blocks(struct super_block *sb)
{
	struct affs_bm_info *bm;
	u32 free;
	int i;

	pr_debug("AFFS: count_free_blocks()\n");

	if (sb->s_flags & MS_RDONLY)
		return 0;

	down(&AFFS_SB(sb)->s_bmlock);

	bm = AFFS_SB(sb)->s_bitmap;
	free = 0;
	for (i = AFFS_SB(sb)->s_bmap_count; i > 0; bm++, i--)
		free += bm->bm_free;

	up(&AFFS_SB(sb)->s_bmlock);

	return free;
}

void
affs_free_block(struct super_block *sb, u32 block)
{
	struct affs_sb_info *sbi = AFFS_SB(sb);
	struct affs_bm_info *bm;
	struct buffer_head *bh;
	u32 blk, bmap, bit, mask, tmp;
	u32 *data;

	pr_debug("AFFS: free_block(%u)\n", block);

	if (block > sbi->s_partition_size)
		goto err_range;

	blk     = block - sbi->s_reserved;
	bmap    = blk / sbi->s_bmap_bits;
	bit     = blk % sbi->s_bmap_bits;
	bm      = &sbi->s_bitmap[bmap];

	down(&sbi->s_bmlock);

	bh = sbi->s_bmap_bh;
	if (sbi->s_last_bmap != bmap) {
		affs_brelse(bh);
		bh = affs_bread(sb, bm->bm_key);
		if (!bh)
			goto err_bh_read;
		sbi->s_bmap_bh = bh;
		sbi->s_last_bmap = bmap;
	}

	mask = 1 << (bit & 31);
	data = (u32 *)bh->b_data + bit / 32 + 1;

	/* mark block free */
	tmp = be32_to_cpu(*data);
	if (tmp & mask)
		goto err_free;
	*data = cpu_to_be32(tmp | mask);

	/* fix checksum */
	tmp = be32_to_cpu(*(u32 *)bh->b_data);
	*(u32 *)bh->b_data = cpu_to_be32(tmp - mask);

	mark_buffer_dirty(bh);
	sb->s_dirt = 1;
	bm->bm_free++;

	up(&sbi->s_bmlock);
	return;

err_free:
	affs_warning(sb,"affs_free_block","Trying to free block %u which is already free", block);
	up(&sbi->s_bmlock);
	return;

err_bh_read:
	affs_error(sb,"affs_free_block","Cannot read bitmap block %u", bm->bm_key);
	sbi->s_bmap_bh = NULL;
	sbi->s_last_bmap = ~0;
	up(&sbi->s_bmlock);
	return;

err_range:
	affs_error(sb, "affs_free_block","Block %u outside partition", block);
	return;
}

/*
 * Allocate a block in the given allocation zone.
 * Since we have to byte-swap the bitmap on little-endian
 * machines, this is rather expensive. Therefor we will
 * preallocate up to 16 blocks from the same word, if
 * possible. We are not doing preallocations in the
 * header zone, though.
 */

u32
affs_alloc_block(struct inode *inode, u32 goal)
{
	struct super_block *sb;
	struct affs_sb_info *sbi;
	struct affs_bm_info *bm;
	struct buffer_head *bh;
	u32 *data, *enddata;
	u32 blk, bmap, bit, mask, mask2, tmp;
	int i;

	sb = inode->i_sb;
	sbi = AFFS_SB(sb);

	pr_debug("AFFS: balloc(inode=%lu,goal=%u): ", inode->i_ino, goal);

	if (AFFS_I(inode)->i_pa_cnt) {
		pr_debug("%d\n", AFFS_I(inode)->i_lastalloc+1);
		AFFS_I(inode)->i_pa_cnt--;
		return ++AFFS_I(inode)->i_lastalloc;
	}

	if (!goal || goal > sbi->s_partition_size) {
		if (goal)
			affs_warning(sb, "affs_balloc", "invalid goal %d", goal);
		//if (!AFFS_I(inode)->i_last_block)
		//	affs_warning(sb, "affs_balloc", "no last alloc block");
		goal = sbi->s_reserved;
	}

	blk = goal - sbi->s_reserved;
	bmap = blk / sbi->s_bmap_bits;
	bm = &sbi->s_bitmap[bmap];

	down(&sbi->s_bmlock);

	if (bm->bm_free)
		goto find_bmap_bit;

find_bmap:
	/* search for the next bmap buffer with free bits */
	i = sbi->s_bmap_count;
	do {
		bmap++;
		bm++;
		if (bmap < sbi->s_bmap_count)
			continue;
		/* restart search at zero */
		bmap = 0;
		bm = sbi->s_bitmap;
		if (--i <= 0)
			goto err_full;
	} while (!bm->bm_free);
	blk = bmap * sbi->s_bmap_bits;

find_bmap_bit:

	bh = sbi->s_bmap_bh;
	if (sbi->s_last_bmap != bmap) {
		affs_brelse(bh);
		bh = affs_bread(sb, bm->bm_key);
		if (!bh)
			goto err_bh_read;
		sbi->s_bmap_bh = bh;
		sbi->s_last_bmap = bmap;
	}

	/* find an unused block in this bitmap block */
	bit = blk % sbi->s_bmap_bits;
	data = (u32 *)bh->b_data + bit / 32 + 1;
	enddata = (u32 *)((u8 *)bh->b_data + sb->s_blocksize);
	mask = ~0UL << (bit & 31);
	blk &= ~31UL;

	tmp = be32_to_cpu(*data) & mask;
	if (tmp)
		goto find_bit;

	/* scan the rest of the buffer */
	do {
		blk += 32;
		if (++data >= enddata)
			/* didn't find something, can only happen
			 * if scan didn't start at 0, try next bmap
			 */
			goto find_bmap;
	} while (!(tmp = *data));
	tmp = be32_to_cpu(tmp);

find_bit:
	/* finally look for a free bit in the word */
	bit = ffs(tmp) - 1;
	blk += bit + sbi->s_reserved;
	mask2 = mask = 1 << (bit & 31);
	AFFS_I(inode)->i_lastalloc = blk;

	/* prealloc as much as possible within this word */
	while ((mask2 <<= 1)) {
		if (!(tmp & mask2))
			break;
		AFFS_I(inode)->i_pa_cnt++;
		mask |= mask2;
	}
	bm->bm_free -= AFFS_I(inode)->i_pa_cnt + 1;

	*data = cpu_to_be32(tmp & ~mask);

	/* fix checksum */
	tmp = be32_to_cpu(*(u32 *)bh->b_data);
	*(u32 *)bh->b_data = cpu_to_be32(tmp + mask);

	mark_buffer_dirty(bh);
	sb->s_dirt = 1;

	up(&sbi->s_bmlock);

	pr_debug("%d\n", blk);
	return blk;

err_bh_read:
	affs_error(sb,"affs_read_block","Cannot read bitmap block %u", bm->bm_key);
	sbi->s_bmap_bh = NULL;
	sbi->s_last_bmap = ~0;
err_full:
	pr_debug("failed\n");
	up(&sbi->s_bmlock);
	return 0;
}

int
affs_init_bitmap(struct super_block *sb)
{
	struct affs_bm_info *bm;
	struct buffer_head *bmap_bh = NULL, *bh = NULL;
	u32 *bmap_blk;
	u32 size, blk, end, offset, mask;
	int i, res = 0;
	struct affs_sb_info *sbi = AFFS_SB(sb);

	if (sb->s_flags & MS_RDONLY)
		return 0;

	if (!AFFS_ROOT_TAIL(sb, sbi->s_root_bh)->bm_flag) {
		printk(KERN_NOTICE "AFFS: Bitmap invalid - mounting %s read only\n",
			sb->s_id);
		sb->s_flags |= MS_RDONLY;
		return 0;
	}

	sbi->s_last_bmap = ~0;
	sbi->s_bmap_bh = NULL;
	sbi->s_bmap_bits = sb->s_blocksize * 8 - 32;
	sbi->s_bmap_count = (sbi->s_partition_size - sbi->s_reserved +
				 sbi->s_bmap_bits - 1) / sbi->s_bmap_bits;
	size = sbi->s_bmap_count * sizeof(struct affs_bm_info);
	bm = sbi->s_bitmap = kmalloc(size, GFP_KERNEL);
	if (!sbi->s_bitmap) {
		printk(KERN_ERR "AFFS: Bitmap allocation failed\n");
		return 1;
	}
	memset(sbi->s_bitmap, 0, size);

	bmap_blk = (u32 *)sbi->s_root_bh->b_data;
	blk = sb->s_blocksize / 4 - 49;
	end = blk + 25;

	for (i = sbi->s_bmap_count; i > 0; bm++, i--) {
		affs_brelse(bh);

		bm->bm_key = be32_to_cpu(bmap_blk[blk]);
		bh = affs_bread(sb, bm->bm_key);
		if (!bh) {
			printk(KERN_ERR "AFFS: Cannot read bitmap\n");
			res = 1;
			goto out;
		}
		if (affs_checksum_block(sb, bh)) {
			printk(KERN_WARNING "AFFS: Bitmap %u invalid - mounting %s read only.\n",
			       bm->bm_key, sb->s_id);
			sb->s_flags |= MS_RDONLY;
			goto out;
		}
		pr_debug("AFFS: read bitmap block %d: %d\n", blk, bm->bm_key);
		bm->bm_free = affs_count_free_bits(sb->s_blocksize - 4, bh->b_data + 4);

		/* Don't try read the extension if this is the last block,
		 * but we also need the right bm pointer below
		 */
		if (++blk < end || i == 1)
			continue;
		if (bmap_bh)
			affs_brelse(bmap_bh);
		bmap_bh = affs_bread(sb, be32_to_cpu(bmap_blk[blk]));
		if (!bmap_bh) {
			printk(KERN_ERR "AFFS: Cannot read bitmap extension\n");
			res = 1;
			goto out;
		}
		bmap_blk = (u32 *)bmap_bh->b_data;
		blk = 0;
		end = sb->s_blocksize / 4 - 1;
	}

	offset = (sbi->s_partition_size - sbi->s_reserved) % sbi->s_bmap_bits;
	mask = ~(0xFFFFFFFFU << (offset & 31));
	pr_debug("last word: %d %d %d\n", offset, offset / 32 + 1, mask);
	offset = offset / 32 + 1;

	if (mask) {
		u32 old, new;

		/* Mark unused bits in the last word as allocated */
		old = be32_to_cpu(((u32 *)bh->b_data)[offset]);
		new = old & mask;
		//if (old != new) {
			((u32 *)bh->b_data)[offset] = cpu_to_be32(new);
			/* fix checksum */
			//new -= old;
			//old = be32_to_cpu(*(u32 *)bh->b_data);
			//*(u32 *)bh->b_data = cpu_to_be32(old - new);
			//mark_buffer_dirty(bh);
		//}
		/* correct offset for the bitmap count below */
		//offset++;
	}
	while (++offset < sb->s_blocksize / 4)
		((u32 *)bh->b_data)[offset] = 0;
	((u32 *)bh->b_data)[0] = 0;
	((u32 *)bh->b_data)[0] = cpu_to_be32(-affs_checksum_block(sb, bh));
	mark_buffer_dirty(bh);

	/* recalculate bitmap count for last block */
	bm--;
	bm->bm_free = affs_count_free_bits(sb->s_blocksize - 4, bh->b_data + 4);

out:
	affs_brelse(bh);
	affs_brelse(bmap_bh);
	return res;
}
