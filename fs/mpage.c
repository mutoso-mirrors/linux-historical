/*
 * fs/mpage.c
 *
 * Copyright (C) 2002, Linus Torvalds.
 *
 * Contains functions related to preparing and submitting BIOs which contain
 * multiple pagecache pages.
 *
 * 15May2002	akpm@zip.com.au
 *		Initial version
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kdev_t.h>
#include <linux/bio.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/blkdev.h>
#include <linux/highmem.h>
#include <linux/prefetch.h>
#include <linux/mpage.h>

/*
 * The largest-sized BIO which this code will assemble, in bytes.  Set this
 * to PAGE_CACHE_SIZE if your drivers are broken.
 */
#define MPAGE_BIO_MAX_SIZE BIO_MAX_SIZE

/*
 * I/O completion handler for multipage BIOs.
 *
 * The mpage code never puts partial pages into a BIO (except for end-of-file).
 * If a page does not map to a contiguous run of blocks then it simply falls
 * back to block_read_full_page().
 *
 * Why is this?  If a page's completion depends on a number of different BIOs
 * which can complete in any order (or at the same time) then determining the
 * status of that page is hard.  See end_buffer_async_read() for the details.
 * There is no point in duplicating all that complexity.
 */
static void mpage_end_io_read(struct bio *bio)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;

	do {
		struct page *page = bvec->bv_page;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (uptodate) {
			SetPageUptodate(page);
		} else {
			ClearPageUptodate(page);
			SetPageError(page);
		}
		unlock_page(page);
	} while (bvec >= bio->bi_io_vec);
	bio_put(bio);
}

static void mpage_end_io_write(struct bio *bio)
{
	const int uptodate = test_bit(BIO_UPTODATE, &bio->bi_flags);
	struct bio_vec *bvec = bio->bi_io_vec + bio->bi_vcnt - 1;

	do {
		struct page *page = bvec->bv_page;

		if (--bvec >= bio->bi_io_vec)
			prefetchw(&bvec->bv_page->flags);

		if (!uptodate)
			SetPageError(page);
		end_page_writeback(page);
	} while (bvec >= bio->bi_io_vec);
	bio_put(bio);
}

struct bio *mpage_bio_submit(int rw, struct bio *bio)
{
	bio->bi_vcnt = bio->bi_idx;
	bio->bi_idx = 0;
	bio->bi_end_io = mpage_end_io_read;
	if (rw == WRITE)
		bio->bi_end_io = mpage_end_io_write;
	submit_bio(rw, bio);
	return NULL;
}

static struct bio *
mpage_alloc(struct block_device *bdev,
		sector_t first_sector, int nr_vecs, int gfp_flags)
{
	struct bio *bio;

	bio = bio_alloc(gfp_flags, nr_vecs);
	if (bio) {
		bio->bi_bdev = bdev;
		bio->bi_vcnt = nr_vecs;
		bio->bi_idx = 0;
		bio->bi_size = 0;
		bio->bi_sector = first_sector;
		bio->bi_io_vec[0].bv_page = NULL;
	}
	return bio;
}

/**
 * mpage_readpages - populate an address space with some pages, and
 *                       start reads against them.
 *
 * @mapping: the address_space
 * @pages: The address of a list_head which contains the target pages.  These
 *   pages have their ->index populated and are otherwise uninitialised.
 *
 *   The page at @pages->prev has the lowest file offset, and reads should be
 *   issued in @pages->prev to @pages->next order.
 *
 * @nr_pages: The number of pages at *@pages
 * @get_block: The filesystem's block mapper function.
 *
 * This function walks the pages and the blocks within each page, building and
 * emitting large BIOs.
 *
 * If anything unusual happens, such as:
 *
 * - encountering a page which has buffers
 * - encountering a page which has a non-hole after a hole
 * - encountering a page with non-contiguous blocks
 *
 * then this code just gives up and calls the buffer_head-based read function.
 * It does handle a page which has holes at the end - that is a common case:
 * the end-of-file on blocksize < PAGE_CACHE_SIZE setups.
 *
 * BH_Boundary explanation:
 *
 * There is a problem.  The mpage read code assembles several pages, gets all
 * their disk mappings, and then submits them all.  That's fine, but obtaining
 * the disk mappings may require I/O.  Reads of indirect blocks, for example.
 *
 * So an mpage read of the first 16 blocks of an ext2 file will cause I/O to be
 * submitted in the following order:
 * 	12 0 1 2 3 4 5 6 7 8 9 10 11 13 14 15 16
 * because the indirect block has to be read to get the mappings of blocks
 * 13,14,15,16.  Obviously, this impacts performance.
 * 
 * So what we do it to allow the filesystem's get_block() function to set
 * BH_Boundary when it maps block 11.  BH_Boundary says: mapping of the block
 * after this one will require I/O against a block which is probably close to
 * this one.  So you should push what I/O you have currently accumulated.
 *
 * This all causes the disk requests to be issued in the correct order.
 */
static struct bio *
do_mpage_readpage(struct bio *bio, struct page *page, unsigned nr_pages,
			sector_t *last_block_in_bio, get_block_t get_block)
{
	struct inode *inode = page->mapping->host;
	const unsigned blkbits = inode->i_blkbits;
	const unsigned blocks_per_page = PAGE_CACHE_SIZE >> blkbits;
	const unsigned blocksize = 1 << blkbits;
	struct bio_vec *bvec;
	sector_t block_in_file;
	sector_t last_block;
	sector_t blocks[MAX_BUF_PER_PAGE];
	unsigned page_block;
	unsigned first_hole = blocks_per_page;
	struct block_device *bdev = NULL;
	struct buffer_head bh;

	if (page_has_buffers(page))
		goto confused;

	block_in_file = page->index << (PAGE_CACHE_SHIFT - blkbits);
	last_block = (inode->i_size + blocksize - 1) >> blkbits;

	for (page_block = 0; page_block < blocks_per_page;
				page_block++, block_in_file++) {
		bh.b_state = 0;
		if (block_in_file < last_block) {
			if (get_block(inode, block_in_file, &bh, 0))
				goto confused;
		}

		if (!buffer_mapped(&bh)) {
			if (first_hole == blocks_per_page)
				first_hole = page_block;
			continue;
		}
	
		if (first_hole != blocks_per_page)
			goto confused;		/* hole -> non-hole */

		/* Contiguous blocks? */
		if (page_block && blocks[page_block-1] != bh.b_blocknr-1)
			goto confused;
		blocks[page_block] = bh.b_blocknr;
		bdev = bh.b_bdev;
	}

	if (first_hole != blocks_per_page) {
		memset(kmap(page) + (first_hole << blkbits), 0,
				PAGE_CACHE_SIZE - (first_hole << blkbits));
		flush_dcache_page(page);
		kunmap(page);
		if (first_hole == 0) {
			SetPageUptodate(page);
			unlock_page(page);
			goto out;
		}
	}

	/*
	 * This page will go to BIO.  Do we need to send this BIO off first?
	 */
	if (bio && (bio->bi_idx == bio->bi_vcnt ||
			*last_block_in_bio != blocks[0] - 1))
		bio = mpage_bio_submit(READ, bio);

	if (bio == NULL) {
		unsigned nr_bvecs = MPAGE_BIO_MAX_SIZE / PAGE_CACHE_SIZE;

		if (nr_bvecs > nr_pages)
			nr_bvecs = nr_pages;
		bio = mpage_alloc(bdev, blocks[0] << (blkbits - 9),
					nr_bvecs, GFP_KERNEL);
		if (bio == NULL)
			goto confused;
	}

	bvec = &bio->bi_io_vec[bio->bi_idx++];
	bvec->bv_page = page;
	bvec->bv_len = (first_hole << blkbits);
	bvec->bv_offset = 0;
	bio->bi_size += bvec->bv_len;
	if (buffer_boundary(&bh) || (first_hole != blocks_per_page))
		bio = mpage_bio_submit(READ, bio);
	else
		*last_block_in_bio = blocks[blocks_per_page - 1];
out:
	return bio;

confused:
	if (bio)
		bio = mpage_bio_submit(READ, bio);
	block_read_full_page(page, get_block);
	goto out;
}

int
mpage_readpages(struct address_space *mapping, struct list_head *pages,
				unsigned nr_pages, get_block_t get_block)
{
	struct bio *bio = NULL;
	unsigned page_idx;
	sector_t last_block_in_bio = 0;

	for (page_idx = 0; page_idx < nr_pages; page_idx++) {
		struct page *page = list_entry(pages->prev, struct page, list);

		prefetchw(&page->flags);
		list_del(&page->list);
		if (!add_to_page_cache_unique(page, mapping, page->index))
			bio = do_mpage_readpage(bio, page,
					nr_pages - page_idx,
					&last_block_in_bio, get_block);
		page_cache_release(page);
	}
	BUG_ON(!list_empty(pages));
	if (bio)
		mpage_bio_submit(READ, bio);
	return 0;
}
EXPORT_SYMBOL(mpage_readpages);

/*
 * This isn't called much at all
 */
int mpage_readpage(struct page *page, get_block_t get_block)
{
	struct bio *bio = NULL;
	sector_t last_block_in_bio = 0;

	bio = do_mpage_readpage(bio, page, 1,
			&last_block_in_bio, get_block);
	if (bio)
		mpage_bio_submit(READ, bio);
	return 0;
}
EXPORT_SYMBOL(mpage_readpage);

/*
 * Writing is not so simple.
 *
 * If the page has buffers then they will be used for obtaining the disk
 * mapping.  We only support pages which are fully mapped-and-dirty, with a
 * special case for pages which are unmapped at the end: end-of-file.
 *
 * If the page has no buffers (preferred) then the page is mapped here.
 *
 * If all blocks are found to be contiguous then the page can go into the
 * BIO.  Otherwise fall back to block_write_full_page().
 * 
 * FIXME: This code wants an estimate of how many pages are still to be
 * written, so it can intelligently allocate a suitably-sized BIO.  For now,
 * just allocate full-size (16-page) BIOs.
 */
static /* inline */ struct bio *
mpage_writepage(struct bio *bio, struct page *page, get_block_t get_block,
			sector_t *last_block_in_bio, int *ret)
{
	struct inode *inode = page->mapping->host;
	const unsigned blkbits = inode->i_blkbits;
	unsigned long end_index;
	const unsigned blocks_per_page = PAGE_CACHE_SIZE >> blkbits;
	struct bio_vec *bvec;
	sector_t last_block;
	sector_t block_in_file;
	sector_t blocks[MAX_BUF_PER_PAGE];
	unsigned page_block;
	unsigned first_unmapped = blocks_per_page;
	struct block_device *bdev = NULL;
	int boundary = 0;

	if (page_has_buffers(page)) {
		struct buffer_head *head = page_buffers(page);
		struct buffer_head *bh = head;

		/* If they're all mapped and dirty, do it */
		page_block = 0;
		do {
			BUG_ON(buffer_locked(bh));
			if (!buffer_mapped(bh)) {
				/*
				 * unmapped dirty buffers are created by
				 * __set_page_dirty_buffers -> mmapped data
				 */
				if (buffer_dirty(bh))
					goto confused;
				if (first_unmapped == blocks_per_page)
					first_unmapped = page_block;
				continue;
			}

			if (first_unmapped != blocks_per_page)
				goto confused;	/* hole -> non-hole */

			if (!buffer_dirty(bh) || !buffer_uptodate(bh))
				goto confused;
			if (page_block) {
				if (bh->b_blocknr != blocks[page_block-1] + 1)
					goto confused;
			}
			blocks[page_block++] = bh->b_blocknr;
			boundary = buffer_boundary(bh);
			bdev = bh->b_bdev;
		} while ((bh = bh->b_this_page) != head);

		if (first_unmapped)
			goto page_is_mapped;

		/*
		 * Page has buffers, but they are all unmapped. The page was
		 * created by pagein or read over a hole which was handled by
		 * block_read_full_page().  If this address_space is also
		 * using mpage_readpages then this can rarely happen.
		 */
		goto confused;
	}

	/*
	 * The page has no buffers: map it to disk
	 */
	BUG_ON(!PageUptodate(page));
	block_in_file = page->index << (PAGE_CACHE_SHIFT - blkbits);
	last_block = (inode->i_size - 1) >> blkbits;
	for (page_block = 0; page_block < blocks_per_page; ) {
		struct buffer_head map_bh;

		map_bh.b_state = 0;
		if (get_block(inode, block_in_file, &map_bh, 1))
			goto confused;
		if (buffer_new(&map_bh))
			unmap_underlying_metadata(map_bh.b_bdev,
						map_bh.b_blocknr);
		if (page_block) {
			if (map_bh.b_blocknr != blocks[page_block-1] + 1)
				goto confused;
		}
		blocks[page_block++] = map_bh.b_blocknr;
		boundary = buffer_boundary(&map_bh);
		bdev = map_bh.b_bdev;
		if (block_in_file == last_block)
			break;
		block_in_file++;
	}
	if (page_block == 0)
		buffer_error();

	first_unmapped = page_block;

	end_index = inode->i_size >> PAGE_CACHE_SHIFT;
	if (page->index >= end_index) {
		unsigned offset = inode->i_size & (PAGE_CACHE_SIZE - 1);

		if (page->index > end_index || !offset)
			goto confused;
		memset(kmap(page) + offset, 0, PAGE_CACHE_SIZE - offset);
		flush_dcache_page(page);
		kunmap(page);
	}

page_is_mapped:

	/*
	 * This page will go to BIO.  Do we need to send this BIO off first?
	 */
	if (bio && (bio->bi_idx == bio->bi_vcnt ||
				*last_block_in_bio != blocks[0] - 1))
		bio = mpage_bio_submit(WRITE, bio);

	if (bio == NULL) {
		unsigned nr_bvecs = MPAGE_BIO_MAX_SIZE / PAGE_CACHE_SIZE;

		bio = mpage_alloc(bdev, blocks[0] << (blkbits - 9),
					nr_bvecs, GFP_NOFS);
		if (bio == NULL)
			goto confused;
	}

	/*
	 * OK, we have our BIO, so we can now mark the buffers clean.  Make
	 * sure to only clean buffers which we know we'll be writing.
	 */
	if (page_has_buffers(page)) {
		struct buffer_head *head = page_buffers(page);
		struct buffer_head *bh = head;
		unsigned buffer_counter = 0;

		do {
			if (buffer_counter++ == first_unmapped)
				break;
			clear_buffer_dirty(bh);
			bh = bh->b_this_page;
		} while (bh != head);
	}

	bvec = &bio->bi_io_vec[bio->bi_idx++];
	bvec->bv_page = page;
	bvec->bv_len = (first_unmapped << blkbits);
	bvec->bv_offset = 0;
	bio->bi_size += bvec->bv_len;
	BUG_ON(PageWriteback(page));
	SetPageWriteback(page);
	unlock_page(page);
	if (boundary || (first_unmapped != blocks_per_page))
		bio = mpage_bio_submit(WRITE, bio);
	else
		*last_block_in_bio = blocks[blocks_per_page - 1];
	goto out;

confused:
	if (bio)
		bio = mpage_bio_submit(WRITE, bio);
	*ret = block_write_full_page(page, get_block);
out:
	return bio;
}

/*
 * This is a cut-n-paste of generic_writepages().  We _could_
 * generalise that function.  It'd get a bit messy.  We'll see.
 */
int
mpage_writepages(struct address_space *mapping,
			int *nr_to_write, get_block_t get_block)
{
	struct bio *bio = NULL;
	sector_t last_block_in_bio = 0;
	int ret = 0;
	int done = 0;

	write_lock(&mapping->page_lock);

	list_splice(&mapping->dirty_pages, &mapping->io_pages);
	INIT_LIST_HEAD(&mapping->dirty_pages);

        while (!list_empty(&mapping->io_pages) && !done) {
		struct page *page = list_entry(mapping->io_pages.prev,
					struct page, list);
		list_del(&page->list);
		if (PageWriteback(page)) {
			if (PageDirty(page)) {
				list_add(&page->list, &mapping->dirty_pages);
				continue;
			}
			list_add(&page->list, &mapping->locked_pages);
			continue;
		}
		if (!PageDirty(page)) {
			list_add(&page->list, &mapping->clean_pages);
			continue;
		}
		list_add(&page->list, &mapping->locked_pages);

		page_cache_get(page);
		write_unlock(&mapping->page_lock);

		lock_page(page);

		if (page->mapping && TestClearPageDirty(page) &&
					!PageWriteback(page)) {
			/* FIXME: batch this up */
			if (!PageActive(page) && PageLRU(page)) {
				spin_lock(&pagemap_lru_lock);
				if (!PageActive(page) && PageLRU(page)) {
					list_del(&page->lru);
					list_add(&page->lru, &inactive_list);
				}
				spin_unlock(&pagemap_lru_lock);
			}
			bio = mpage_writepage(bio, page, get_block,
					&last_block_in_bio, &ret);
			if (ret || (nr_to_write && --(*nr_to_write) <= 0))
				done = 1;
		} else {
			unlock_page(page);
		}

		page_cache_release(page);
		write_lock(&mapping->page_lock);
	}
	if (!list_empty(&mapping->io_pages)) {
		/*
		 * Put the rest back, in the correct order.
		 */
		list_splice(&mapping->io_pages, mapping->dirty_pages.prev);
		INIT_LIST_HEAD(&mapping->io_pages);
	}
	write_unlock(&mapping->page_lock);
	if (bio)
		mpage_bio_submit(WRITE, bio);
	return ret;
}
EXPORT_SYMBOL(mpage_writepages);
