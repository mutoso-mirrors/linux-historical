/**
 * mft.c - NTFS kernel mft record operations. Part of the Linux-NTFS project.
 *
 * Copyright (c) 2001,2002 Anton Altaparmakov.
 * Copyright (C) 2002 Richard Russon.
 *
 * This program/include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program/include file is distributed in the hope that it will be 
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty 
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (in the main directory of the Linux-NTFS 
 * distribution in the file COPYING); if not, write to the Free Software
 * Foundation,Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/locks.h>
#include <linux/swap.h>

#include "ntfs.h"

#define MAX_BUF_PER_PAGE (PAGE_CACHE_SIZE / 512)

/**
 * __format_mft_record - initialize an empty mft record
 * @m:		mapped, pinned and locked for writing mft record
 * @size:	size of the mft record
 * @rec_no:	mft record number / inode number
 *
 * Private function to initialize an empty mft record. Use one of the two
 * provided format_mft_record() functions instead.
 */
static void __format_mft_record(MFT_RECORD *m, const int size,
		const unsigned long rec_no)
{
	ATTR_RECORD *a;

	memset(m, 0, size);
	m->_MNR(magic) = magic_FILE;
	/* Aligned to 2-byte boundary. */
	m->_MNR(usa_ofs) = cpu_to_le16((sizeof(MFT_RECORD) + 1) & ~1);
	m->_MNR(usa_count) = cpu_to_le16(size / NTFS_BLOCK_SIZE + 1);
	/* Set the update sequence number to 1. */
	*(u16*)((char*)m + ((sizeof(MFT_RECORD) + 1) & ~1)) = cpu_to_le16(1);
	m->lsn = cpu_to_le64(0LL);
	m->sequence_number = cpu_to_le16(1);
	m->link_count = cpu_to_le16(0);
	/* Aligned to 8-byte boundary. */
	m->attrs_offset = cpu_to_le16((le16_to_cpu(m->_MNR(usa_ofs)) +
			(le16_to_cpu(m->_MNR(usa_count)) << 1) + 7) & ~7);
	m->flags = cpu_to_le16(0);
	/*
	 * Using attrs_offset plus eight bytes (for the termination attribute),
	 * aligned to 8-byte boundary.
	 */
	m->bytes_in_use = cpu_to_le32((le16_to_cpu(m->attrs_offset) + 8 + 7) &
			~7);
	m->bytes_allocated = cpu_to_le32(size);
	m->base_mft_record = cpu_to_le64((MFT_REF)0);
	m->next_attr_instance = cpu_to_le16(0);
	a = (ATTR_RECORD*)((char*)m + le16_to_cpu(m->attrs_offset));
	a->type = AT_END;
	a->length = cpu_to_le32(0);
}

/**
 * format_mft_record2 - initialize an empty mft record
 * @vfs_sb:	vfs super block of volume
 * @inum:	mft record number / inode number to format
 * @mft_rec:	mapped, pinned and locked mft record (optional)
 *
 * Initialize an empty mft record. This is used when extending the MFT.
 *
 * If @mft_rec is NULL, we call map_mft_record() to obtain the record and we
 * unmap it again when finished.
 *
 * We return 0 on success or -errno on error.
 */
#if 0
// Can't do this as iget_map_mft_record no longer exists...
int format_mft_record2(struct super_block *vfs_sb, const unsigned long inum,
		MFT_RECORD *mft_rec)
{
	MFT_RECORD *m;
	ntfs_inode *ni;

	if (mft_rec)
		m = mft_rec;
	else {
		m = iget_map_mft_record(WRITE, vfs_sb, inum, &ni);
		if (IS_ERR(m))
			return PTR_ERR(m);
	}
	__format_mft_record(m, NTFS_SB(vfs_sb)->mft_record_size, inum);
	if (!mft_rec) {
		// TODO: dirty mft record
		unmap_mft_record(WRITE, ni);
		// TODO: Do stuff to get rid of the ntfs_inode
	}
	return 0;
}
#endif

/**
 * format_mft_record - initialize an empty mft record
 * @ni:		ntfs inode of mft record
 * @mft_rec:	mapped, pinned and locked mft record (optional)
 *
 * Initialize an empty mft record. This is used when extending the MFT.
 *
 * If @mft_rec is NULL, we call map_mft_record() to obtain the
 * record and we unmap it again when finished.
 *
 * We return 0 on success or -errno on error.
 */
int format_mft_record(ntfs_inode *ni, MFT_RECORD *mft_rec)
{
	MFT_RECORD *m;

	if (mft_rec)
		m = mft_rec;
	else {
		m = map_mft_record(WRITE, ni);
		if (IS_ERR(m))
			return PTR_ERR(m);
	}
	__format_mft_record(m, ni->vol->mft_record_size, ni->mft_no);
	if (!mft_rec)
		unmap_mft_record(WRITE, ni);
	return 0;
}

/* From fs/ntfs/aops.c */
extern int ntfs_mst_readpage(struct file *, struct page *);

/**
 * ntfs_mft_aops - address space operations for access to $MFT
 *
 * Address space operations for access to $MFT. This allows us to simply use
 * read_cache_page() in map_mft_record().
 */
struct address_space_operations ntfs_mft_aops = {
	writepage:	NULL,			/* Write dirty page to disk. */
	readpage:	ntfs_mst_readpage,	/* Fill page with data. */
	sync_page:	block_sync_page,	/* Currently, just unplugs the
						   disk request queue. */
	prepare_write:	NULL,			/* . */
	commit_write:	NULL,			/* . */
	bmap:		NULL,			/* Needed for FIBMAP.
						   Don't use it. */
	flushpage:	NULL,			/* . */
	releasepage:	NULL,			/* . */
#ifdef KERNEL_HAS_O_DIRECT
	direct_IO:	NULL,			/* . */
#endif
};

/**
 * map_mft_record_page - map the page in which a specific mft record resides
 * @ni:		ntfs inode whose mft record page to map
 *
 * This maps the page in which the mft record of the ntfs inode @ni is situated
 * and returns a pointer to the mft record within the mapped page.
 *
 * Return value needs to be checked with IS_ERR() and if that is true PTR_ERR()
 * contains the negative error code returned.
 */
static inline MFT_RECORD *map_mft_record_page(ntfs_inode *ni)
{
	ntfs_volume *vol = ni->vol;
	struct inode *mft_vi = vol->mft_ino;
	struct page *page;
	unsigned long index, ofs, end_index;

	BUG_ON(atomic_read(&ni->mft_count) || ni->page);
	/*
	 * The index into the page cache and the offset within the page cache
	 * page of the wanted mft record. FIXME: We need to check for
	 * overflowing the unsigned long, but I don't think we would ever get
	 * here if the volume was that big...
	 */
	index = ni->mft_no << vol->mft_record_size_bits >> PAGE_CACHE_SHIFT;
	ofs = (ni->mft_no << vol->mft_record_size_bits) & ~PAGE_CACHE_MASK;

	/* The maximum valid index into the page cache for $MFT's data. */
	end_index = mft_vi->i_size >> PAGE_CACHE_SHIFT;

	/* If the wanted index is out of bounds the mft record doesn't exist. */
	if (index >= end_index) {
		if (index > end_index || (mft_vi->i_size & ~PAGE_CACHE_MASK) <
				ofs + vol->mft_record_size) {
			page = ERR_PTR(-ENOENT);
			goto up_err_out;
		}
	}
	/* Read, map, and pin the page. */
	page = ntfs_map_page(mft_vi->i_mapping, index);
	if (!IS_ERR(page)) {
		/* Pin the mft record mapping in the ntfs_inode. */
		atomic_inc(&ni->mft_count);

		/* Setup the references in the ntfs_inode. */
		ni->page = page;
		ni->page_ofs = ofs;

		return page_address(page) + ofs;
	}
up_err_out:
	/* Just in case... */
	ni->page = NULL;
	ni->page_ofs = 0;

	ntfs_error(vol->sb, "Failed with error code %lu.", -PTR_ERR(page));
	return (void*)page;
}

/**
 * unmap_mft_record_page - unmap the page in which a specific mft record resides
 * @ni:		ntfs inode whose mft record page to unmap
 *
 * This unmaps the page in which the mft record of the ntfs inode @ni is
 * situated and returns. This is a NOOP if highmem is not configured.
 *
 * The unmap happens via ntfs_unmap_page() which in turn decrements the use
 * count on the page thus releasing it from the pinned state.
 *
 * We do not actually unmap the page from memory of course, as that will be
 * done by the page cache code itself when memory pressure increases or
 * whatever.
 */
static inline void unmap_mft_record_page(ntfs_inode *ni)
{
	BUG_ON(atomic_read(&ni->mft_count) || !ni->page);
	// TODO: If dirty, blah...
	ntfs_unmap_page(ni->page);
	ni->page = NULL;
	ni->page_ofs = 0;
	return;
}

/**
 * map_mft_record - map, pin and lock an mft record
 * @rw:		map for read (rw = READ) or write (rw = WRITE)
 * @ni:		ntfs inode whose MFT record to map
 *
 * First, take the mrec_lock semaphore for reading or writing, depending on
 * the value or @rw. We might now be sleeping, while waiting for the semaphore
 * if it was already locked by someone else.
 *
 * Then increment the map reference count and return the mft. If this is the
 * first invocation, the page of the record is first mapped using
 * map_mft_record_page().
 *
 * This in turn uses ntfs_map_page() to get the page containing the wanted mft
 * record (it in turn calls read_cache_page() which reads it in from disk if
 * necessary, increments the use count on the page so that it cannot disappear
 * under us and returns a reference to the page cache page).
 *
 * If read_cache_page() invokes ntfs_mst_readpage() to load the page from disk,
 * it sets PG_locked and clears PG_uptodate on the page. Once I/O has
 * completed and the post-read mst fixups on each mft record in the page have
 * been performed, the page gets PG_uptodate set and PG_locked cleared (this is
 * done in our asynchronous I/O completion handler end_buffer_read_mft_async()).
 * ntfs_map_page() waits for PG_locked to become clear and checks if
 * PG_uptodate is set and returns an error code if not. This provides
 * sufficient protection against races when reading/using the page.
 *
 * However there is the write mapping to think about. Doing the above described
 * checking here will be fine, because when initiating the write we will set
 * PG_locked and clear PG_uptodate making sure nobody is touching the page
 * contents. Doing the locking this way means that the commit to disk code in
 * the page cache code paths is automatically sufficiently locked with us as
 * we will not touch a page that has been locked or is not uptodate. The only
 * locking problem then is them locking the page while we are accessing it.
 *
 * So that code will end up having to own the mrec_lock of all mft
 * records/inodes present in the page before I/O can proceed. Grr. In that
 * case we wouldn't need need to bother with PG_locked and PG_uptodate as
 * nobody will be accessing anything without owning the mrec_lock semaphore.
 * But we do need to use them because of the read_cache_page() invokation and
 * the code becomes so much simpler this way that it is well worth it.
 *
 * The mft record is now ours and we return a pointer to it. You need to check
 * the returned pointer with IS_ERR() and if that is true, PTR_ERR() will return
 * the error code. The following error codes are defined:
 * 	TODO: Fill in the possible error codes.
 *
 * NOTE: Caller is responsible for setting the mft record dirty before calling
 * unmap_mft_record(). This is obviously only necessary if the caller really
 * modified the mft record...
 * Q: Do we want to recycle one of the VFS inode state bits instead?
 * A: No, the inode ones mean we want to change the mft record, not we want to
 * write it out.
 */
MFT_RECORD *map_mft_record(const int rw, ntfs_inode *ni)
{
	MFT_RECORD *m;

	ntfs_debug("Entering for i_ino 0x%Lx, mapping for %s.",
			(unsigned long long)ni->mft_no,
			rw == READ ? "READ" : "WRITE");

	/* Make sure the ntfs inode doesn't go away. */
	atomic_inc(&ni->count);

	/* Serialize access to this mft record. */
	if (rw == READ)
		down_read(&ni->mrec_lock);
	else
		down_write(&ni->mrec_lock);

	/* If already mapped, bump reference count and return the mft record. */
	if (atomic_read(&ni->mft_count)) {
		BUG_ON(!ni->page);
		atomic_inc(&ni->mft_count);
		return page_address(ni->page) + ni->page_ofs;
	}

	/* Wasn't mapped. Map it now and return it if all was ok. */
	m = map_mft_record_page(ni);
	if (!IS_ERR(m))
		return m;

	/* Mapping failed. Release the mft record lock. */
	if (rw == READ)
		up_read(&ni->mrec_lock);
	else
		up_write(&ni->mrec_lock);

	ntfs_error(ni->vol->sb, "Failed with error code %lu.", -PTR_ERR(m));

	/* Release the ntfs inode and return the error code. */
	atomic_dec(&ni->count);
	return m;
}

/**
 * iget_map_mft_record - iget, map, pin, lock an mft record
 * @rw:		map for read (rw = READ) or write (rw = WRITE)
 * @vfs_sb:	vfs super block of mounted volume
 * @inum:	inode number / MFT record number whose mft record to map
 * @vfs_ino:	output parameter which we set to the inode on successful return
 *
 * Does the same as map_mft_record(), except that it starts out only with the
 * knowledge of the super block (@vfs_sb) and the mft record number which is of
 * course the same as the inode number (@inum).
 *
 * On success, *@vfs_ino will contain a pointer to the inode structure of the
 * mft record on return. On error return, *@vfs_ino is undefined.
 *
 * See map_mft_record() description for details and for a description of how
 * errors are returned and what error codes are defined.
 *
 * IMPROTANT: The caller is responsible for calling iput(@vfs_ino) when
 * finished with the inode, i.e. after unmap_mft_record() has been called. If
 * that is omitted you will get busy inodes upon umount...
 */
#if 0
// this is no longer possible. iget() cannot be called as we may be loading
// an ntfs inode which will never have a corresponding vfs inode counter part.
// this is not going to be pretty. )-:
// we need our own hash for ntfs inodes now, ugh. )-:
// not having vfs inodes associated with all ntfs inodes is a bad mistake I am
// getting the impression. this will in the end turn out uglier than just
// having iget_no_wait().
// my only hope is that we can get away without this functionality in the driver
// altogether. we are ok for extent inodes already because we only handle them
// via map_extent_mft_record().
// if we really need it, we could have a list or hash of "pure ntfs inodes"
// to cope with this situation, so the lookup would be:
// look for the inode and if not present look for pure ntfs inode and if not
// present add a new pure ntfs inode. under this scheme extent inodes have to
// also be added to the list/hash of pure inodes.
MFT_RECORD *iget_map_mft_record(const int rw, struct super_block *vfs_sb,
		const unsigned long inum, struct inode **vfs_ino)
{
	struct inode *inode;
	MFT_RECORD *mrec;

	/*
	 * The corresponding iput() happens when clear_inode() is called on the
	 * base mft record of this extent mft record.
	 * When used on base mft records, caller has to perform the iput().
	 */
	inode = iget(vfs_sb, inum);
	if (inode && !is_bad_inode(inode)) {
		mrec = map_mft_record(rw, inode);
		if (!IS_ERR(mrec)) {
			ntfs_debug("Success for i_ino 0x%lx.", inum);
			*vfs_ino = inode;
			return mrec;
		}
	} else
		mrec = ERR_PTR(-EIO);
	if (inode)
		iput(inode);
	ntfs_debug("Failed for i_ino 0x%lx.", inum);
	return mrec;
}
#endif

/**
 * unmap_mft_record - release a mapped mft record
 * @rw:		unmap from read (@rw = READ) or write (@rw = WRITE)
 * @ni:		ntfs inode whose MFT record to unmap
 *
 * First, decrement the mapping count and when it reaches zero unmap the mft
 * record.
 *
 * Second, release the mrec_lock semaphore.
 *
 * The mft record is now released for others to get hold of.
 *
 * Finally, release the ntfs inode by decreasing the ntfs inode reference count.
 *
 * NOTE: If caller had the mft record mapped for write and has modified it, it
 * is imperative to set the mft record dirty BEFORE calling unmap_mft_record().
 *
 * NOTE: This has to be done both for 'normal' mft records, and for extent mft
 * records.
 */
void unmap_mft_record(const int rw, ntfs_inode *ni)
{
	struct page *page = ni->page;

	BUG_ON(!atomic_read(&ni->mft_count) || !page);

	ntfs_debug("Entering for mft_no 0x%Lx, unmapping from %s.",
			(unsigned long long)ni->mft_no,
			rw == READ ? "READ" : "WRITE");

	/* Only release the actual page mapping if this is the last one. */
	if (atomic_dec_and_test(&ni->mft_count))
		unmap_mft_record_page(ni);

	/* Release the semaphore. */
	if (rw == READ)
		up_read(&ni->mrec_lock);
	else
		up_write(&ni->mrec_lock);

	/* Release the ntfs inode. */
	atomic_dec(&ni->count);

	/*
	 * If pure ntfs_inode, i.e. no vfs inode attached, we leave it to
	 * ntfs_clear_inode() in the extent inode case, and to the caller in
	 * the non-extent, yet pure ntfs inode case, to do the actual tear
	 * down of all structures and freeing of all allocated memory.
	 */
	return;
}

/**
 * map_extent_mft_record - load an extent inode and attach it to its base
 * @base_ni:	base ntfs inode
 * @mref:	mft reference of the extent inode to load (in little endian)
 * @ntfs_ino:	on successful return, pointer to the ntfs_inode structure
 *
 * Load the extent mft record @mref and attach it to its base inode @base_ni.
 * Return the mapped extent mft record if IS_ERR(result) is false. Otherwise
 * PTR_ERR(result) gives the negative error code.
 *
 * On successful return, @ntfs_ino contains a pointer to the ntfs_inode
 * structure of the mapped extent inode.
 *
 * Note, we always map for READ. We consider this lock as irrelevant because
 * the base inode will be write locked in all cases when we want to write to
 * an extent inode which already gurantees that there is no-one else accessing
 * the extent inode.
 */
MFT_RECORD *map_extent_mft_record(ntfs_inode *base_ni, MFT_REF mref,
		ntfs_inode **ntfs_ino)
{
	MFT_RECORD *m;
	ntfs_inode *ni = NULL;
	ntfs_inode **extent_nis = NULL;
	int i;
	u64 mft_no = MREF_LE(mref);
	u16 seq_no = MSEQNO_LE(mref);
	BOOL destroy_ni = FALSE;

	ntfs_debug("Mapping extent mft record 0x%Lx (base mft record 0x%Lx).",
			(unsigned long long)mft_no,
			(unsigned long long)base_ni->mft_no);
	/* Make sure the base ntfs inode doesn't go away. */
	atomic_inc(&base_ni->count);
	/*
	 * Check if this extent inode has already been added to the base inode,
	 * in which case just return it. If not found, add it to the base
	 * inode before returning it.
	 */
	down(&base_ni->extent_lock);
	if (base_ni->nr_extents > 0) {
		extent_nis = base_ni->_INE(extent_ntfs_inos);
		for (i = 0; i < base_ni->nr_extents; i++) {
			if (mft_no != extent_nis[i]->mft_no)
				continue;
			ni = extent_nis[i];
			/* Make sure the ntfs inode doesn't go away. */
			atomic_inc(&ni->count);
			break;
		}
	}
	if (ni) {
		up(&base_ni->extent_lock);
		atomic_dec(&base_ni->count);
		/* We found the record; just have to map and return it. */
		m = map_mft_record(READ, ni);
		/* Map mft record increments this on success. */
		atomic_dec(&ni->count);
		if (!IS_ERR(m)) {
			/* Verify the sequence number. */
			if (le16_to_cpu(m->sequence_number) == seq_no) {
				ntfs_debug("Done 1.");
				*ntfs_ino = ni;
				return m;
			}
			unmap_mft_record(READ, ni);
			ntfs_error(base_ni->vol->sb, "Found stale extent mft "
					"reference! Corrupt file system. "
					"Run chkdsk.");
			return ERR_PTR(-EIO);
		}
map_err_out:
		ntfs_error(base_ni->vol->sb, "Failed to map extent "
				"mft record, error code %ld.", -PTR_ERR(m));
		return m;
	}
	/* Record wasn't there. Get a new ntfs inode and initialize it. */
	ni = ntfs_new_inode(base_ni->vol->sb);
	if (!ni) {
		up(&base_ni->extent_lock);
		atomic_dec(&base_ni->count);
		return ERR_PTR(-ENOMEM);
	}
	ni->vol = base_ni->vol;
	ni->mft_no = mft_no;
	ni->seq_no = seq_no;
	ni->nr_extents = -1;
	ni->_INE(base_ntfs_ino) = base_ni;
	/* Now map the record. */
	m = map_mft_record(READ, ni);
	if (IS_ERR(m)) {
		up(&base_ni->extent_lock);
		atomic_dec(&base_ni->count);
		ntfs_clear_inode(ni);
		goto map_err_out;
	}
	/* Verify the sequence number. */
	if (le16_to_cpu(m->sequence_number) != seq_no) {
		ntfs_error(base_ni->vol->sb, "Found stale extent mft "
				"reference! Corrupt file system. Run chkdsk.");
		destroy_ni = TRUE;
		m = ERR_PTR(-EIO);
		goto unm_err_out;
	}
	/* Attach extent inode to base inode, reallocating memory if needed. */
	if (!(base_ni->nr_extents & ~3)) {
		ntfs_inode **tmp;
		int new_size = (base_ni->nr_extents + 4) * sizeof(ntfs_inode *);

		tmp = (ntfs_inode **)kmalloc(new_size, GFP_NOFS);
		if (!tmp) {
			ntfs_error(base_ni->vol->sb, "Failed to allocate "
					"internal buffer.");
			destroy_ni = TRUE;
			m = ERR_PTR(-ENOMEM);
			goto unm_err_out;
		}
		if (base_ni->_INE(extent_ntfs_inos)) {
			memcpy(tmp, base_ni->_INE(extent_ntfs_inos), new_size -
					4 * sizeof(ntfs_inode *));
			kfree(base_ni->_INE(extent_ntfs_inos));
		}
		base_ni->_INE(extent_ntfs_inos) = tmp;
	}
	base_ni->_INE(extent_ntfs_inos)[base_ni->nr_extents++] = ni;
	up(&base_ni->extent_lock);
	atomic_dec(&base_ni->count);
	ntfs_debug("Done 2.");
	*ntfs_ino = ni;
	return m;
unm_err_out:
	unmap_mft_record(READ, ni);
	up(&base_ni->extent_lock);
	atomic_dec(&base_ni->count);
	/*
	 * If the extent inode was not attached to the base inode we need to
	 * release it or we will leak memory.
	 */
	if (destroy_ni)
		ntfs_clear_inode(ni);
	return m;
}

