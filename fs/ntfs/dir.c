/**
 * dir.c - NTFS kernel directory operations. Part of the Linux-NTFS project.
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

#include <linux/smp_lock.h>
#include "ntfs.h"
#include "dir.h"

/**
 * The little endian Unicode string $I30 as a global constant.
 */
const uchar_t I30[5] = { const_cpu_to_le16('$'), const_cpu_to_le16('I'),
		const_cpu_to_le16('3'),	const_cpu_to_le16('0'),
		const_cpu_to_le16(0) };

/**
 * ntfs_lookup_inode_by_name - find an inode in a directory given its name
 * @dir_ni:	ntfs inode of the directory in which to search for the name
 * @uname:	Unicode name for which to search in the directory
 * @uname_len:	length of the name @uname in Unicode characters
 * @res:	return the found file name if necessary (see below)
 *
 * Look for an inode with name @uname in the directory with inode @dir_ni.
 * ntfs_lookup_inode_by_name() walks the contents of the directory looking for
 * the Unicode name. If the name is found in the directory, the corresponding
 * inode number (>= 0) is returned as a mft reference in cpu format, i.e. it
 * is a 64-bit number containing the sequence number.
 *
 * On error, a negative value is returned corresponding to the error code. In
 * particular if the inode is not found -ENOENT is returned. Note that you
 * can't just check the return value for being negative, you have to check the
 * inode number for being negative which you can extract using MREC(return
 * value).
 *
 * Note, @uname_len does not include the (optional) terminating NULL character.
 *
 * Note, we look for a case sensitive match first but we also look for a case
 * insensitive match at the same time. If we find a case insensitive match, we
 * save that for the case that we don't find an exact match, where we return
 * the case insensitive match and setup @res (which we allocate!) with the mft
 * reference, the file name type, length and with a copy of the little endian
 * Unicode file name itself. If we match a file name which is in the DOS name
 * space, we only return the mft reference and file name type in @res.
 * ntfs_lookup() then uses this to find the long file name in the inode itself.
 * This is to avoid polluting the dcache with short file names. We want them to
 * work but we don't care for how quickly one can access them. This also fixes
 * the dcache aliasing issues.
 */
u64 ntfs_lookup_inode_by_name(ntfs_inode *dir_ni, const uchar_t *uname,
		const int uname_len, ntfs_name **res)
{
	ntfs_volume *vol = dir_ni->vol;
	struct super_block *sb = vol->sb;
	MFT_RECORD *m;
	INDEX_ROOT *ir;
	INDEX_ENTRY *ie;
	INDEX_ALLOCATION *ia;
	u8 *index_end;
	u64 mref;
	attr_search_context *ctx;
	int err = 0, rc;
	VCN vcn, old_vcn;
	struct address_space *ia_mapping;
	struct page *page;
	u8 *kaddr;
	ntfs_name *name = NULL;

	/* Get hold of the mft record for the directory. */
	m = map_mft_record(READ, dir_ni);
	if (IS_ERR(m))
		goto map_err_out;

	ctx = get_attr_search_ctx(dir_ni, m);
	if (!ctx) {
		err = -ENOMEM;
		goto unm_err_out;
	}

	/* Find the index root attribute in the mft record. */
	if (!lookup_attr(AT_INDEX_ROOT, I30, 4, CASE_SENSITIVE, 0, NULL, 0,
			ctx)) {
		ntfs_error(sb, "Index root attribute missing in directory "
				"inode 0x%Lx.",
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto put_unm_err_out;
	}
	/* Get to the index root value (it's been verified in read_inode). */
	ir = (INDEX_ROOT*)((u8*)ctx->attr +
			le16_to_cpu(ctx->attr->_ARA(value_offset)));
	index_end = (u8*)&ir->index + le32_to_cpu(ir->index.index_length);
	/* The first index entry. */
	ie = (INDEX_ENTRY*)((u8*)&ir->index +
			le32_to_cpu(ir->index.entries_offset));
	/*
	 * Loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry.
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->_IEH(length)))) {
		/* Bounds checks. */
		if ((u8*)ie < (u8*)ctx->mrec || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->_IEH(key_length)) >
				index_end)
			goto dir_err_out;
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->_IEH(flags) & INDEX_ENTRY_END)
			break;
		/*
		 * We perform a case sensitive comparison and if that matches
		 * we are done and return the mft reference of the inode (i.e.
		 * the inode number together with the sequence number for
		 * consistency checking). We convert it to cpu format before
		 * returning.
		 */
		if (ntfs_are_names_equal(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len)) {
found_it:
			/*
			 * We have a perfect match, so we don't need to care
			 * about having matched imperfectly before, so we can
			 * free name and set *res to NULL.
			 * However, if the perfect match is a short file name,
			 * we need to signal this through *res, so that
			 * ntfs_lookup() can fix dcache aliasing issues.
			 * As an optimization we just reuse an existing
			 * allocation of *res.
			 */
			if (ie->key.file_name.file_name_type == FILE_NAME_DOS) {
				if (!name) {
					name = kmalloc(sizeof(ntfs_name),
							GFP_NOFS);
					if (!name) {
						err = -ENOMEM;
						goto put_unm_err_out;
					}
				}
				name->mref = le64_to_cpu(
						ie->_IIF(indexed_file));
				name->type = FILE_NAME_DOS;
				name->len = 0;
				*res = name;
			} else {
				if (name)
					kfree(name);
				*res = NULL;
			}
			mref = le64_to_cpu(ie->_IIF(indexed_file));
			put_attr_search_ctx(ctx);
			unmap_mft_record(READ, dir_ni);
			return mref;
		}
		/*
		 * For a case insensitive mount, we also perform a case
		 * insensitive comparison (provided the file name is not in the
		 * POSIX namespace). If the comparison matches, and the name is
		 * in the WIN32 namespace, we cache the filename in *res so
		 * that the caller, ntfs_lookup(), can work on it. If the
		 * comparison matches, and the name is in the DOS namespace, we
		 * only cache the mft reference and the file name type (we set
		 * the name length to zero for simplicity).
		 */
		if (!NVolCaseSensitive(vol) &&
				ie->key.file_name.file_name_type &&
				ntfs_are_names_equal(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length,
				IGNORE_CASE, vol->upcase, vol->upcase_len)) {
			int name_size = sizeof(ntfs_name);
			u8 type = ie->key.file_name.file_name_type;
			u8 len = ie->key.file_name.file_name_length;

			/* Only one case insensitive matching name allowed. */
			if (name) {
				ntfs_error(sb, "Found already allocated name "
						"in phase 1. Please run chkdsk "
						"and if that doesn't find any "
						"errors please report you saw "
						"this message to "
						"linux-ntfs-dev@lists.sf.net.");
				goto dir_err_out;
			}

			if (type != FILE_NAME_DOS)
				name_size += len * sizeof(uchar_t);
			name = kmalloc(name_size, GFP_NOFS);
			if (!name) {
				err = -ENOMEM;
				goto put_unm_err_out;
			}
			name->mref = le64_to_cpu(ie->_IIF(indexed_file));
			name->type = type;
			if (type != FILE_NAME_DOS) {
				name->len = len;
				memcpy(name->name, ie->key.file_name.file_name,
						len * sizeof(uchar_t));
			} else
				name->len = 0;
			*res = name;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				IGNORE_CASE, vol->upcase, vol->upcase_len);
		/*
		 * If uname collates before the name of the current entry, there
		 * is definitely no such name in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/* The names are not equal, continue the search. */
		if (rc)
			continue;
		/*
		 * Names match with case insensitive comparison, now try the
		 * case sensitive comparison, which is required for proper
		 * collation.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len);
		if (rc == -1)
			break;
		if (rc)
			continue;
		/*
		 * Perfect match, this will never happen as the
		 * ntfs_are_names_equal() call will have gotten a match but we
		 * still treat it correctly.
		 */
		goto found_it;
	}
	/*
	 * We have finished with this index without success. Check for the
	 * presence of a child node and if not present return -ENOENT, unless
	 * we have got a matching name cached in name in which case return the
	 * mft reference associated with it.
	 */
	if (!(ie->_IEH(flags) & INDEX_ENTRY_NODE)) {
		if (name) {
			put_attr_search_ctx(ctx);
			unmap_mft_record(READ, dir_ni);
			return name->mref;
		}
		ntfs_debug("Entry not found.");
		err = -ENOENT;
		goto put_unm_err_out;
	} /* Child node present, descend into it. */
	/* Consistency check: Verify that an index allocation exists. */
	if (!NInoIndexAllocPresent(dir_ni)) {
		ntfs_error(sb, "No index allocation attribute but index entry "
				"requires one. Directory inode 0x%Lx is "
				"corrupt or driver bug.",
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto put_unm_err_out;
	}
	/* Get the starting vcn of the index_block holding the child node. */
	vcn = sle64_to_cpup((u8*)ie + le16_to_cpu(ie->_IEH(length)) - 8);
	ia_mapping = VFS_I(dir_ni)->i_mapping;
descend_into_child_node:
	/*
	 * Convert vcn to index into the index allocation attribute in units
	 * of PAGE_CACHE_SIZE and map the page cache page, reading it from
	 * disk if necessary.
	 */
	page = ntfs_map_page(ia_mapping, vcn <<
			dir_ni->_IDM(index_vcn_size_bits) >> PAGE_CACHE_SHIFT);
	if (IS_ERR(page)) {
		ntfs_error(sb, "Failed to map directory index page, error %ld.",
				-PTR_ERR(page));
		goto put_unm_err_out;
	}
	kaddr = (u8*)page_address(page);
fast_descend_into_child_node:
	/* Get to the index allocation block. */
	ia = (INDEX_ALLOCATION*)(kaddr + ((vcn <<
			dir_ni->_IDM(index_vcn_size_bits)) & ~PAGE_CACHE_MASK));
	/* Bounds checks. */
	if ((u8*)ia < kaddr || (u8*)ia > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Out of bounds check failed. Corrupt directory "
				"inode 0x%Lx or driver bug.",
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto unm_unm_err_out;
	}
	if (sle64_to_cpu(ia->index_block_vcn) != vcn) {
		ntfs_error(sb, "Actual VCN (0x%Lx) of index buffer is "
				"different from expected VCN (0x%Lx). "
				"Directory inode 0x%Lx is corrupt or driver "
				"bug.",
				(long long)sle64_to_cpu(ia->index_block_vcn),
				(long long)vcn,
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto unm_unm_err_out;
	}
	if (le32_to_cpu(ia->index.allocated_size) + 0x18 !=
			dir_ni->_IDM(index_block_size)) {
		ntfs_error(sb, "Index buffer (VCN 0x%Lx) of directory inode "
				"0x%Lx has a size (%u) differing from the "
				"directory specified size (%u). Directory "
				"inode is corrupt or driver bug.",
				(long long)vcn,
				(unsigned long long)dir_ni->mft_no,
				le32_to_cpu(ia->index.allocated_size) + 0x18,
				dir_ni->_IDM(index_block_size));
		err = -EIO;
		goto unm_unm_err_out;
	}
	index_end = (u8*)ia + dir_ni->_IDM(index_block_size);
	if (index_end > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Index buffer (VCN 0x%Lx) of directory inode "
				"0x%Lx crosses page boundary. Impossible! "
				"Cannot access! This is probably a bug in the "
				"driver.", (long long)vcn,
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto unm_unm_err_out;
	}
	index_end = (u8*)&ia->index + le32_to_cpu(ia->index.index_length);
	if (index_end > (u8*)ia + dir_ni->_IDM(index_block_size)) {
		ntfs_error(sb, "Size of index buffer (VCN 0x%Lx) of directory "
				"inode 0x%Lx exceeds maximum size.",
				(long long)vcn,
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto unm_unm_err_out;
	}
	/* The first index entry. */
	ie = (INDEX_ENTRY*)((u8*)&ia->index +
			le32_to_cpu(ia->index.entries_offset));
	/*
	 * Iterate similar to above big loop but applied to index buffer, thus
	 * loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry.
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->_IEH(length)))) {
		/* Bounds check. */
		if ((u8*)ie < (u8*)ia || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->_IEH(key_length)) >
				index_end) {
			ntfs_error(sb, "Index entry out of bounds in "
					"directory inode 0x%Lx.",
					(unsigned long long)dir_ni->mft_no);
			err = -EIO;
			goto unm_unm_err_out;
		}
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->_IEH(flags) & INDEX_ENTRY_END)
			break;
		/*
		 * We perform a case sensitive comparison and if that matches
		 * we are done and return the mft reference of the inode (i.e.
		 * the inode number together with the sequence number for
		 * consistency checking). We convert it to cpu format before
		 * returning.
		 */
		if (ntfs_are_names_equal(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len)) {
found_it2:
			/*
			 * We have a perfect match, so we don't need to care
			 * about having matched imperfectly before, so we can
			 * free name and set *res to NULL.
			 * However, if the perfect match is a short file name,
			 * we need to signal this through *res, so that
			 * ntfs_lookup() can fix dcache aliasing issues.
			 * As an optimization we just reuse an existing
			 * allocation of *res.
			 */
			if (ie->key.file_name.file_name_type == FILE_NAME_DOS) {
				if (!name) {
					name = kmalloc(sizeof(ntfs_name),
							GFP_NOFS);
					if (!name) {
						err = -ENOMEM;
						goto unm_unm_err_out;
					}
				}
				name->mref = le64_to_cpu(
						ie->_IIF(indexed_file));
				name->type = FILE_NAME_DOS;
				name->len = 0;
				*res = name;
			} else {
				if (name)
					kfree(name);
				*res = NULL;
			}
			mref = le64_to_cpu(ie->_IIF(indexed_file));
			ntfs_unmap_page(page);
			put_attr_search_ctx(ctx);
			unmap_mft_record(READ, dir_ni);
			return mref;
		}
		/*
		 * For a case insensitive mount, we also perform a case
		 * insensitive comparison (provided the file name is not in the
		 * POSIX namespace). If the comparison matches, and the name is
		 * in the WIN32 namespace, we cache the filename in *res so
		 * that the caller, ntfs_lookup(), can work on it. If the
		 * comparison matches, and the name is in the DOS namespace, we
		 * only cache the mft reference and the file name type (we set
		 * the name length to zero for simplicity).
		 */
		if (!NVolCaseSensitive(vol) &&
				ie->key.file_name.file_name_type &&
				ntfs_are_names_equal(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length,
				IGNORE_CASE, vol->upcase, vol->upcase_len)) {
			int name_size = sizeof(ntfs_name);
			u8 type = ie->key.file_name.file_name_type;
			u8 len = ie->key.file_name.file_name_length;

			/* Only one case insensitive matching name allowed. */
			if (name) {
				ntfs_error(sb, "Found already allocated name "
						"in phase 2. Please run chkdsk "
						"and if that doesn't find any "
						"errors please report you saw "
						"this message to "
						"linux-ntfs-dev@lists.sf.net.");
				ntfs_unmap_page(page);
				goto dir_err_out;
			}

			if (type != FILE_NAME_DOS)
				name_size += len * sizeof(uchar_t);
			name = kmalloc(name_size, GFP_NOFS);
			if (!name) {
				err = -ENOMEM;
				goto put_unm_err_out;
			}
			name->mref = le64_to_cpu(ie->_IIF(indexed_file));
			name->type = type;
			if (type != FILE_NAME_DOS) {
				name->len = len;
				memcpy(name->name, ie->key.file_name.file_name,
						len * sizeof(uchar_t));
			} else
				name->len = 0;
			*res = name;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				IGNORE_CASE, vol->upcase, vol->upcase_len);
		/*
		 * If uname collates before the name of the current entry, there
		 * is definitely no such name in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/* The names are not equal, continue the search. */
		if (rc)
			continue;
		/*
		 * Names match with case insensitive comparison, now try the
		 * case sensitive comparison, which is required for proper
		 * collation.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len);
		if (rc == -1)
			break;
		if (rc)
			continue;
		/*
		 * Perfect match, this will never happen as the
		 * ntfs_are_names_equal() call will have gotten a match but we
		 * still treat it correctly.
		 */
		goto found_it2;
	}
	/*
	 * We have finished with this index buffer without success. Check for
	 * the presence of a child node.
	 */
	if (ie->_IEH(flags) & INDEX_ENTRY_NODE) {
		if ((ia->index.flags & NODE_MASK) == LEAF_NODE) {
			ntfs_error(sb, "Index entry with child node found in "
					"a leaf node in directory inode 0x%Lx.",
					(unsigned long long)dir_ni->mft_no);
			err = -EIO;
			goto unm_unm_err_out;
		}
		/* Child node present, descend into it. */
		old_vcn = vcn;
		vcn = sle64_to_cpup((u8*)ie +
				le16_to_cpu(ie->_IEH(length)) - 8);
		if (vcn >= 0) {
			/* If vcn is in the same page cache page as old_vcn we
			 * recycle the mapped page. */
			if (old_vcn << vol->cluster_size_bits >>
					PAGE_CACHE_SHIFT == vcn <<
					vol->cluster_size_bits >>
					PAGE_CACHE_SHIFT)
				goto fast_descend_into_child_node;
			ntfs_unmap_page(page);
			goto descend_into_child_node;
		}
		ntfs_error(sb, "Negative child node vcn in directory inode "
				"0x%Lx.", (unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto unm_unm_err_out;
	}
	/*
	 * No child node present, return -ENOENT, unless we have got a matching
	 * name cached in name in which case return the mft reference
	 * associated with it.
	 */
	if (name) {
		ntfs_unmap_page(page);
		put_attr_search_ctx(ctx);
		unmap_mft_record(READ, dir_ni);
		return name->mref;
	}
	ntfs_debug("Entry not found.");
	err = -ENOENT;
unm_unm_err_out:
	ntfs_unmap_page(page);
put_unm_err_out:
	put_attr_search_ctx(ctx);
unm_err_out:
	unmap_mft_record(READ, dir_ni);
	if (name) {
		kfree(name);
		*res = NULL;
	}
	return ERR_MREF(err);
map_err_out:
	ntfs_error(sb, "map_mft_record(READ) failed with error code %ld.",
			-PTR_ERR(m));
	return ERR_MREF(PTR_ERR(m));
dir_err_out:
	ntfs_error(sb, "Corrupt directory. Aborting lookup.");
	err = -EIO;
	goto put_unm_err_out;
}

#if 0

// TODO: (AIA)
// The algorithm embedded in this code will be required for the time when we
// want to support adding of entries to directories, where we require correct
// collation of file names in order not to cause corruption of the file system.

/**
 * ntfs_lookup_inode_by_name - find an inode in a directory given its name
 * @dir_ni:	ntfs inode of the directory in which to search for the name
 * @uname:	Unicode name for which to search in the directory
 * @uname_len:	length of the name @uname in Unicode characters
 *
 * Look for an inode with name @uname in the directory with inode @dir_ni.
 * ntfs_lookup_inode_by_name() walks the contents of the directory looking for
 * the Unicode name. If the name is found in the directory, the corresponding
 * inode number (>= 0) is returned as a mft reference in cpu format, i.e. it
 * is a 64-bit number containing the sequence number.
 *
 * On error, a negative value is returned corresponding to the error code. In
 * particular if the inode is not found -ENOENT is returned. Note that you
 * can't just check the return value for being negative, you have to check the
 * inode number for being negative which you can extract using MREC(return
 * value).
 *
 * Note, @uname_len does not include the (optional) terminating NULL character.
 */
u64 ntfs_lookup_inode_by_name(ntfs_inode *dir_ni, const uchar_t *uname,
		const int uname_len)
{
	ntfs_volume *vol = dir_ni->vol;
	struct super_block *sb = vol->sb;
	MFT_RECORD *m;
	INDEX_ROOT *ir;
	INDEX_ENTRY *ie;
	INDEX_ALLOCATION *ia;
	u8 *index_end;
	u64 mref;
	attr_search_context *ctx;
	int err = 0, rc;
	IGNORE_CASE_BOOL ic;
	VCN vcn, old_vcn;
	struct address_space *ia_mapping;
	struct page *page;
	u8 *kaddr;

	/* Get hold of the mft record for the directory. */
	m = map_mft_record(READ, dir_ni);
	if (IS_ERR(m))
		goto map_err_out;

	ctx = get_attr_search_ctx(dir_ni, m);
	if (!ctx) {
		err = -ENOMEM;
		goto unm_err_out;
	}

	/* Find the index root attribute in the mft record. */
	if (!lookup_attr(AT_INDEX_ROOT, I30, 4, CASE_SENSITIVE, 0, NULL, 0,
			ctx)) {
		ntfs_error(sb, "Index root attribute missing in directory "
				"inode 0x%Lx.",
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto put_unm_err_out;
	}
	/* Get to the index root value (it's been verified in read_inode). */
	ir = (INDEX_ROOT*)((u8*)ctx->attr +
			le16_to_cpu(ctx->attr->_ARA(value_offset)));
	index_end = (u8*)&ir->index + le32_to_cpu(ir->index.index_length);
	/* The first index entry. */
	ie = (INDEX_ENTRY*)((u8*)&ir->index +
			le32_to_cpu(ir->index.entries_offset));
	/*
	 * Loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry.
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->_IEH(length)))) {
		/* Bounds checks. */
		if ((u8*)ie < (u8*)ctx->mrec || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->_IEH(key_length)) >
				index_end)
			goto dir_err_out;
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->_IEH(flags) & INDEX_ENTRY_END)
			break;
		/*
		 * If the current entry has a name type of POSIX, the name is
		 * case sensitive and not otherwise. This has the effect of us
		 * not being able to access any POSIX file names which collate
		 * after the non-POSIX one when they only differ in case, but
		 * anyone doing screwy stuff like that deserves to burn in
		 * hell... Doing that kind of stuff on NT4 actually causes
		 * corruption on the partition even when using SP6a and Linux
		 * is not involved at all.
		 */
		ic = ie->key.file_name.file_name_type ? IGNORE_CASE :
				CASE_SENSITIVE;
		/*
		 * If the names match perfectly, we are done and return the
		 * mft reference of the inode (i.e. the inode number together
		 * with the sequence number for consistency checking. We
		 * convert it to cpu format before returning.
		 */
		if (ntfs_are_names_equal(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, ic,
				vol->upcase, vol->upcase_len)) {
found_it:
			mref = le64_to_cpu(ie->_IIF(indexed_file));
			put_attr_search_ctx(ctx);
			unmap_mft_record(READ, dir_ni);
			return mref;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				IGNORE_CASE, vol->upcase, vol->upcase_len);
		/*
		 * If uname collates before the name of the current entry, there
		 * is definitely no such name in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/* The names are not equal, continue the search. */
		if (rc)
			continue;
		/*
		 * Names match with case insensitive comparison, now try the
		 * case sensitive comparison, which is required for proper
		 * collation.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len);
		if (rc == -1)
			break;
		if (rc)
			continue;
		/*
		 * Perfect match, this will never happen as the
		 * ntfs_are_names_equal() call will have gotten a match but we
		 * still treat it correctly.
		 */
		goto found_it;
	}
	/*
	 * We have finished with this index without success. Check for the
	 * presence of a child node.
	 */
	if (!(ie->_IEH(flags) & INDEX_ENTRY_NODE)) {
		/* No child node, return -ENOENT. */
		err = -ENOENT;
		goto put_unm_err_out;
	} /* Child node present, descend into it. */
	/* Consistency check: Verify that an index allocation exists. */
	if (!NInoIndexAllocPresent(dir_ni)) {
		ntfs_error(sb, "No index allocation attribute but index entry "
				"requires one. Directory inode 0x%Lx is "
				"corrupt or driver bug.",
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto put_unm_err_out;
	}
	/* Get the starting vcn of the index_block holding the child node. */
	vcn = sle64_to_cpup((u8*)ie + le16_to_cpu(ie->_IEH(length)) - 8);
	ia_mapping = VFS_I(dir_ni)->i_mapping;
descend_into_child_node:
	/*
	 * Convert vcn to index into the index allocation attribute in units
	 * of PAGE_CACHE_SIZE and map the page cache page, reading it from
	 * disk if necessary.
	 */
	page = ntfs_map_page(ia_mapping, vcn <<
			dir_ni->_IDM(index_vcn_size_bits) >> PAGE_CACHE_SHIFT);
	if (IS_ERR(page)) {
		ntfs_error(sb, "Failed to map directory index page, error %ld.",
				-PTR_ERR(page));
		goto put_unm_err_out;
	}
	kaddr = (u8*)page_address(page);
fast_descend_into_child_node:
	/* Get to the index allocation block. */
	ia = (INDEX_ALLOCATION*)(kaddr + ((vcn <<
			dir_ni->_IDM(index_vcn_size_bits)) & ~PAGE_CACHE_MASK));
	/* Bounds checks. */
	if ((u8*)ia < kaddr || (u8*)ia > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Out of bounds check failed. Corrupt directory "
				"inode 0x%Lx or driver bug.",
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto unm_unm_err_out;
	}
	if (sle64_to_cpu(ia->index_block_vcn) != vcn) {
		ntfs_error(sb, "Actual VCN (0x%Lx) of index buffer is "
				"different from expected VCN (0x%Lx). "
				"Directory inode 0x%Lx is corrupt or driver "
				"bug.",
				(long long)sle64_to_cpu(ia->index_block_vcn),
				(long long)vcn,
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto unm_unm_err_out;
	}
	if (le32_to_cpu(ia->index.allocated_size) + 0x18 !=
			dir_ni->_IDM(index_block_size)) {
		ntfs_error(sb, "Index buffer (VCN 0x%Lx) of directory inode "
				"0x%Lx has a size (%u) differing from the "
				"directory specified size (%u). Directory "
				"inode is corrupt or driver bug.",
				(long long)vcn,
				(unsigned long long)dir_ni->mft_no,
				le32_to_cpu(ia->index.allocated_size) + 0x18,
				dir_ni->_IDM(index_block_size));
		err = -EIO;
		goto unm_unm_err_out;
	}
	index_end = (u8*)ia + dir_ni->_IDM(index_block_size);
	if (index_end > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Index buffer (VCN 0x%Lx) of directory inode "
				"0x%Lx crosses page boundary. Impossible! "
				"Cannot access! This is probably a bug in the "
				"driver.", (long long)vcn,
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto unm_unm_err_out;
	}
	index_end = (u8*)&ia->index + le32_to_cpu(ia->index.index_length);
	if (index_end > (u8*)ia + dir_ni->_IDM(index_block_size)) {
		ntfs_error(sb, "Size of index buffer (VCN 0x%Lx) of directory "
				"inode 0x%Lx exceeds maximum size.",
				(long long)vcn,
				(unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto unm_unm_err_out;
	}
	/* The first index entry. */
	ie = (INDEX_ENTRY*)((u8*)&ia->index +
			le32_to_cpu(ia->index.entries_offset));
	/*
	 * Iterate similar to above big loop but applied to index buffer, thus
	 * loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry.
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->_IEH(length)))) {
		/* Bounds check. */
		if ((u8*)ie < (u8*)ia || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->_IEH(key_length)) >
				index_end) {
			ntfs_error(sb, "Index entry out of bounds in "
					"directory inode 0x%Lx.",
					(unsigned long long)dir_ni->mft_no);
			err = -EIO;
			goto unm_unm_err_out;
		}
		/*
		 * The last entry cannot contain a name. It can however contain
		 * a pointer to a child node in the B+tree so we just break out.
		 */
		if (ie->_IEH(flags) & INDEX_ENTRY_END)
			break;
		/*
		 * If the current entry has a name type of POSIX, the name is
		 * case sensitive and not otherwise. This has the effect of us
		 * not being able to access any POSIX file names which collate
		 * after the non-POSIX one when they only differ in case, but
		 * anyone doing screwy stuff like that deserves to burn in
		 * hell... Doing that kind of stuff on NT4 actually causes
		 * corruption on the partition even when using SP6a and Linux
		 * is not involved at all.
		 */
		ic = ie->key.file_name.file_name_type ? IGNORE_CASE :
				CASE_SENSITIVE;
		/*
		 * If the names match perfectly, we are done and return the
		 * mft reference of the inode (i.e. the inode number together
		 * with the sequence number for consistency checking. We
		 * convert it to cpu format before returning.
		 */
		if (ntfs_are_names_equal(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, ic,
				vol->upcase, vol->upcase_len)) {
found_it2:
			mref = le64_to_cpu(ie->_IIF(indexed_file));
			ntfs_unmap_page(page);
			put_attr_search_ctx(ctx);
			unmap_mft_record(READ, dir_ni);
			return mref;
		}
		/*
		 * Not a perfect match, need to do full blown collation so we
		 * know which way in the B+tree we have to go.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				IGNORE_CASE, vol->upcase, vol->upcase_len);
		/*
		 * If uname collates before the name of the current entry, there
		 * is definitely no such name in this index but we might need to
		 * descend into the B+tree so we just break out of the loop.
		 */
		if (rc == -1)
			break;
		/* The names are not equal, continue the search. */
		if (rc)
			continue;
		/*
		 * Names match with case insensitive comparison, now try the
		 * case sensitive comparison, which is required for proper
		 * collation.
		 */
		rc = ntfs_collate_names(uname, uname_len,
				(uchar_t*)&ie->key.file_name.file_name,
				ie->key.file_name.file_name_length, 1,
				CASE_SENSITIVE, vol->upcase, vol->upcase_len);
		if (rc == -1)
			break;
		if (rc)
			continue;
		/*
		 * Perfect match, this will never happen as the
		 * ntfs_are_names_equal() call will have gotten a match but we
		 * still treat it correctly.
		 */
		goto found_it2;
	}
	/*
	 * We have finished with this index buffer without success. Check for
	 * the presence of a child node.
	 */
	if (ie->_IEH(flags) & INDEX_ENTRY_NODE) {
		if ((ia->index.flags & NODE_MASK) == LEAF_NODE) {
			ntfs_error(sb, "Index entry with child node found in "
					"a leaf node in directory inode 0x%Lx.",
					(unsigned long long)dir_ni->mft_no);
			err = -EIO;
			goto unm_unm_err_out;
		}
		/* Child node present, descend into it. */
		old_vcn = vcn;
		vcn = sle64_to_cpup((u8*)ie +
				le16_to_cpu(ie->_IEH(length)) - 8);
		if (vcn >= 0) {
			/* If vcn is in the same page cache page as old_vcn we
			 * recycle the mapped page. */
			if (old_vcn << vol->cluster_size_bits >>
					PAGE_CACHE_SHIFT == vcn <<
					vol->cluster_size_bits >>
					PAGE_CACHE_SHIFT)
				goto fast_descend_into_child_node;
			ntfs_unmap_page(page);
			goto descend_into_child_node;
		}
		ntfs_error(sb, "Negative child node vcn in directory inode "
				"0x%Lx.", (unsigned long long)dir_ni->mft_no);
		err = -EIO;
		goto unm_unm_err_out;
	}
	/* No child node, return -ENOENT. */
	ntfs_debug("Entry not found.");
	err = -ENOENT;
unm_unm_err_out:
	ntfs_unmap_page(page);
put_unm_err_out:
	put_attr_search_ctx(ctx);
unm_err_out:
	unmap_mft_record(READ, dir_ni);
	return ERR_MREF(err);
map_err_out:
	ntfs_error(sb, "map_mft_record(READ) failed with error code %ld.",
			-PTR_ERR(m));
	return ERR_MREF(PTR_ERR(m));
dir_err_out:
	ntfs_error(sb, "Corrupt directory. Aborting lookup.");
	err = -EIO;
	goto put_unm_err_out;
}

#endif

typedef union {
	INDEX_ROOT *ir;
	INDEX_ALLOCATION *ia;
} index_union __attribute__ ((__transparent_union__));

typedef enum {
	INDEX_TYPE_ROOT,	/* index root */
	INDEX_TYPE_ALLOCATION,	/* index allocation */
} INDEX_TYPE;

/**
 * ntfs_filldir - ntfs specific filldir method
 * @vol:	current ntfs volume
 * @filp:	open file descriptor for the current directory
 * @ndir:	ntfs inode of current directory
 * @index_type:	specifies whether @iu is an index root or an index allocation
 * @iu:		index root or index allocation attribute to which @ie belongs
 * @ie:		current index entry
 * @name:	buffer to use for the converted name
 * @dirent:	vfs filldir callback context
 * filldir:	vfs filldir callback
 *
 * Convert the Unicode name to the loaded NLS and pass it to
 * the filldir callback.
 */
static inline int ntfs_filldir(ntfs_volume *vol, struct file *filp,
		ntfs_inode *ndir, const INDEX_TYPE index_type,
		index_union iu, INDEX_ENTRY *ie, u8 *name,
		void *dirent, filldir_t filldir)
{
	int name_len;
	unsigned dt_type;
	FILE_NAME_TYPE_FLAGS name_type;

	/* Advance the position even if going to skip the entry. */
	if (index_type == INDEX_TYPE_ALLOCATION)
		filp->f_pos = (u8*)ie - (u8*)iu.ia +
				(sle64_to_cpu(iu.ia->index_block_vcn) <<
				ndir->_IDM(index_vcn_size_bits)) +
				vol->mft_record_size;
	else /* if (index_type == INDEX_TYPE_ROOT) */
		filp->f_pos = (u8*)ie - (u8*)iu.ir;
	name_type = ie->key.file_name.file_name_type;
	if (name_type == FILE_NAME_DOS) {
		ntfs_debug("Skipping DOS name space entry.");
		return 0;
	}
	if (MREF_LE(ie->_IIF(indexed_file)) == FILE_root) {
		ntfs_debug("Skipping root directory self reference entry.");
		return 0;
	}
	if (MREF_LE(ie->_IIF(indexed_file)) < FILE_first_user &&
			!NVolShowSystemFiles(vol)) {
		ntfs_debug("Skipping system file.");
		return 0;
	}
	name_len = ntfs_ucstonls(vol, (uchar_t*)&ie->key.file_name.file_name,
			ie->key.file_name.file_name_length, &name,
			NTFS_MAX_NAME_LEN * 3 + 1);
	if (name_len <= 0) {
		ntfs_debug("Skipping unrepresentable file.");
		return 0;
	}
	if (ie->key.file_name.file_attributes &
			FILE_ATTR_DUP_FILE_NAME_INDEX_PRESENT)
		dt_type = DT_DIR;
	else
		dt_type = DT_REG;
	ntfs_debug("Calling filldir for %s with len %i, f_pos 0x%Lx, inode "
			"0x%Lx, DT_%s.", name, name_len, filp->f_pos,
			(unsigned long long)MREF_LE(ie->_IIF(indexed_file)),
			dt_type == DT_DIR ? "DIR" : "REG");
	return filldir(dirent, name, name_len, filp->f_pos,
			(unsigned long)MREF_LE(ie->_IIF(indexed_file)),
			dt_type);
}

/*
 * VFS calls readdir without BKL but with i_sem held. This protects the VFS
 * parts (e.g. ->f_pos and ->i_size, and it also protects against directory
 * modifications). Together with the rw semaphore taken by the call to
 * map_mft_record(), the directory is truly locked down so we have a race free
 * ntfs_readdir() without the BKL. (-:
 *
 * We use the same basic approach as the old NTFS driver, i.e. we parse the
 * index root entries and then the index allocation entries that are marked
 * as in use in the index bitmap.
 * While this will return the names in random order this doesn't matter for
 * readdir but OTOH results in a faster readdir.
 */
static int ntfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	s64 ia_pos, ia_start, prev_ia_pos;
	struct inode *vdir = filp->f_dentry->d_inode;
	struct super_block *sb = vdir->i_sb;
	ntfs_inode *ndir = NTFS_I(vdir);
	ntfs_volume *vol = NTFS_SB(sb);
	MFT_RECORD *m;
	INDEX_ROOT *ir;
	INDEX_ENTRY *ie;
	INDEX_ALLOCATION *ia;
	u8 *name;
	int rc, err, ir_pos, bmp_pos;
	struct address_space *ia_mapping;
	struct page *page;
	u8 *kaddr, *bmp, *index_end;
	attr_search_context *ctx;

	ntfs_debug("Entering for inode 0x%Lx, f_pos 0x%Lx.",
			(unsigned long long)ndir->mft_no, filp->f_pos);
	rc = err = 0;
	/* Are we at end of dir yet? */
	if (filp->f_pos >= vdir->i_size + vol->mft_record_size)
		goto done;
	/* Emulate . and .. for all directories. */
	if (!filp->f_pos) {
		ntfs_debug("Calling filldir for . with len 1, f_pos 0x0, "
				"inode 0x%Lx, DT_DIR.",
				(unsigned long long)ndir->mft_no);
		rc = filldir(dirent, ".", 1, filp->f_pos, vdir->i_ino, DT_DIR);
		if (rc)
			goto done;
		filp->f_pos++;
	}
	if (filp->f_pos == 1) {
		ntfs_debug("Calling filldir for .. with len 2, f_pos 0x1, "
				"inode 0x%lx, DT_DIR.",
				parent_ino(filp->f_dentry));
		rc = filldir(dirent, "..", 2, filp->f_pos,
				parent_ino(filp->f_dentry), DT_DIR);
		if (rc)
			goto done;
		filp->f_pos++;
	}
	/* Get hold of the mft record for the directory. */
	m = map_mft_record(READ, ndir);
	if (IS_ERR(m)) {
		err = PTR_ERR(m);
		goto err_out;
	}

	ctx = get_attr_search_ctx(ndir, m);
	if (!ctx) {
		err = -ENOMEM;
		goto unm_err_out;
	}

	/*
	 * Allocate a buffer to store the current name being processed
	 * converted to format determined by current NLS.
	 */
	name = (u8*)kmalloc(NTFS_MAX_NAME_LEN * 3 + 1, GFP_NOFS);
	if (!name) {
		err = -ENOMEM;
		goto put_unm_err_out;
	}
	/* Are we jumping straight into the index allocation attribute? */
	if (filp->f_pos >= vol->mft_record_size)
		goto skip_index_root;
	/* Get the offset into the index root attribute. */
	ir_pos = (s64)filp->f_pos;
	/* Find the index root attribute in the mft record. */
	if (!lookup_attr(AT_INDEX_ROOT, I30, 4, CASE_SENSITIVE, 0, NULL, 0,
			ctx)) {
		ntfs_error(sb, "Index root attribute missing in directory "
				"inode 0x%Lx.",
				(unsigned long long)ndir->mft_no);
		err = -EIO;
		goto kf_unm_err_out;
	}
	/* Get to the index root value (it's been verified in read_inode). */
	ir = (INDEX_ROOT*)((u8*)ctx->attr +
			le16_to_cpu(ctx->attr->_ARA(value_offset)));
	index_end = (u8*)&ir->index + le32_to_cpu(ir->index.index_length);
	/* The first index entry. */
	ie = (INDEX_ENTRY*)((u8*)&ir->index +
			le32_to_cpu(ir->index.entries_offset));
	/*
	 * Loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry or until filldir tells us it has had enough
	 * or signals an error (both covered by the rc test).
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->_IEH(length)))) {
		ntfs_debug("In index root, offset 0x%x.", (u8*)ie - (u8*)ir);
		/* Bounds checks. */
		if ((u8*)ie < (u8*)ctx->mrec || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->_IEH(key_length)) >
				index_end)
			goto dir_err_out;
		/* The last entry cannot contain a name. */
		if (ie->_IEH(flags) & INDEX_ENTRY_END)
			break;
		/* Skip index root entry if continuing previous readdir. */
		if (ir_pos > (u8*)ie - (u8*)ir)
			continue;
		/* Submit the name to the filldir callback. */
		rc = ntfs_filldir(vol, filp, ndir, INDEX_TYPE_ROOT, ir, ie,
				name, dirent, filldir);
		if (rc)
			goto abort;
	}
	/* If there is no index allocation attribute we are finished. */
	if (!NInoIndexAllocPresent(ndir))
		goto EOD;
	/* Advance f_pos to the beginning of the index allocation. */
	filp->f_pos = vol->mft_record_size;
	/* Reinitialize the search context. */
	reinit_attr_search_ctx(ctx);
skip_index_root:
	if (NInoBmpNonResident(ndir)) {
		/*
		 * Read the page of the bitmap that contains the current index
		 * block.
		 */
		// TODO: FIXME: Implement this!
		ntfs_error(sb, "Index bitmap is non-resident, which is not "
				"supported yet. Pretending that end of "
				"directory has been reached.\n");
		goto EOD;
	} else {
		/* Find the index bitmap attribute in the mft record. */
		if (!lookup_attr(AT_BITMAP, I30, 4, CASE_SENSITIVE, 0, NULL, 0,
				ctx)) {
			ntfs_error(sb, "Index bitmap attribute missing in "
					"directory inode 0x%Lx.",
					(unsigned long long)ndir->mft_no);
			err = -EIO;
			goto kf_unm_err_out;
		}
		bmp = (u8*)ctx->attr +
				le16_to_cpu(ctx->attr->_ARA(value_offset));
	}
	/* Get the offset into the index allocation attribute. */
	ia_pos = (s64)filp->f_pos - vol->mft_record_size;
	ia_mapping = vdir->i_mapping;
	/* If the index block is not in use find the next one that is. */
	bmp_pos = ia_pos >> ndir->_IDM(index_block_size_bits);
	page = NULL;
	kaddr = NULL;
	prev_ia_pos = -1LL;
	if (bmp_pos >> 3 >= ndir->_IDM(bmp_size)) {
		ntfs_error(sb, "Current index allocation position exceeds "
				"index bitmap size.");
		goto kf_unm_err_out;
	}
	while (!(bmp[bmp_pos >> 3] & (1 << (bmp_pos & 7)))) {
find_next_index_buffer:
		bmp_pos++;
		/* If we have reached the end of the bitmap, we are done. */
		if (bmp_pos >> 3 >= ndir->_IDM(bmp_size))
			goto EOD;
		ia_pos = (s64)bmp_pos << ndir->_IDM(index_block_size_bits);
	}
	ntfs_debug("Handling index buffer 0x%x.", bmp_pos);
	/* If the current index buffer is in the same page we reuse the page. */
	if ((prev_ia_pos & PAGE_CACHE_MASK) != (ia_pos & PAGE_CACHE_MASK)) {
		prev_ia_pos = ia_pos;
		if (page)
			ntfs_unmap_page(page);
		/*
		 * Map the page cache page containing the current ia_pos,
		 * reading it from disk if necessary.
		 */
		page = ntfs_map_page(ia_mapping, ia_pos >> PAGE_CACHE_SHIFT);
		if (IS_ERR(page))
			goto map_page_err_out;
		kaddr = (u8*)page_address(page);
	}
	/* Get the current index buffer. */
	ia = (INDEX_ALLOCATION*)(kaddr + (ia_pos & ~PAGE_CACHE_MASK &
			~(s64)(ndir->_IDM(index_block_size) - 1)));
	/* Bounds checks. */
	if ((u8*)ia < kaddr || (u8*)ia > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Out of bounds check failed. Corrupt directory "
				"inode 0x%Lx or driver bug.",
				(unsigned long long)ndir->mft_no);
		err = -EIO;
		goto unm_dir_err_out;
	}
	if (sle64_to_cpu(ia->index_block_vcn) != (ia_pos &
			~(s64)(ndir->_IDM(index_block_size) - 1)) >>
			ndir->_IDM(index_vcn_size_bits)) {
		ntfs_error(sb, "Actual VCN (0x%Lx) of index buffer is "
				"different from expected VCN (0x%Lx). "
				"Directory inode 0x%Lx is corrupt or driver "
				"bug. ",
				(long long)sle64_to_cpu(ia->index_block_vcn),
				(long long)ia_pos >>
				ndir->_IDM(index_vcn_size_bits),
				(unsigned long long)ndir->mft_no);
		err = -EIO;
		goto unm_dir_err_out;
	}
	if (le32_to_cpu(ia->index.allocated_size) + 0x18 !=
			ndir->_IDM(index_block_size)) {
		ntfs_error(sb, "Index buffer (VCN 0x%Lx) of directory inode "
				"0x%Lx has a size (%u) differing from the "
				"directory specified size (%u). Directory "
				"inode is corrupt or driver bug.",
				(long long)ia_pos >> ndir->_IDM(index_vcn_size_bits),
				(unsigned long long)ndir->mft_no,
				le32_to_cpu(ia->index.allocated_size) + 0x18,
				ndir->_IDM(index_block_size));
		err = -EIO;
		goto unm_dir_err_out;
	}
	index_end = (u8*)ia + ndir->_IDM(index_block_size);
	if (index_end > kaddr + PAGE_CACHE_SIZE) {
		ntfs_error(sb, "Index buffer (VCN 0x%Lx) of directory inode "
				"0x%Lx crosses page boundary. Impossible! "
				"Cannot access! This is probably a bug in the "
				"driver.", (long long)ia_pos >>
				ndir->_IDM(index_vcn_size_bits),
				(unsigned long long)ndir->mft_no);
		err = -EIO;
		goto unm_dir_err_out;
	}
	ia_start = ia_pos & ~(s64)(ndir->_IDM(index_block_size) - 1);
	index_end = (u8*)&ia->index + le32_to_cpu(ia->index.index_length);
	if (index_end > (u8*)ia + ndir->_IDM(index_block_size)) {
		ntfs_error(sb, "Size of index buffer (VCN 0x%Lx) of directory "
				"inode 0x%Lx exceeds maximum size.",
				(long long)ia_pos >>
				ndir->_IDM(index_vcn_size_bits),
				(unsigned long long)ndir->mft_no);
		err = -EIO;
		goto unm_dir_err_out;
	}
	/* The first index entry in this index buffer. */
	ie = (INDEX_ENTRY*)((u8*)&ia->index +
			le32_to_cpu(ia->index.entries_offset));
	/*
	 * Loop until we exceed valid memory (corruption case) or until we
	 * reach the last entry or until filldir tells us it has had enough
	 * or signals an error (both covered by the rc test).
	 */
	for (;; ie = (INDEX_ENTRY*)((u8*)ie + le16_to_cpu(ie->_IEH(length)))) {
		ntfs_debug("In index allocation, offset 0x%Lx.",
				(long long)ia_start + ((u8*)ie - (u8*)ia));
		/* Bounds checks. */
		if ((u8*)ie < (u8*)ia || (u8*)ie +
				sizeof(INDEX_ENTRY_HEADER) > index_end ||
				(u8*)ie + le16_to_cpu(ie->_IEH(key_length)) >
				index_end)
			goto unm_dir_err_out;
		/* The last entry cannot contain a name. */
		if (ie->_IEH(flags) & INDEX_ENTRY_END)
			break;
		/* Skip index block entry if continuing previous readdir. */
		if (ia_pos - ia_start > (u8*)ie - (u8*)ia)
			continue;
		/* Submit the name to the filldir callback. */
		rc = ntfs_filldir(vol, filp, ndir, INDEX_TYPE_ALLOCATION, ia,
				ie, name, dirent, filldir);
		if (rc) {
			ntfs_unmap_page(page);
			goto abort;
		}
	}
	goto find_next_index_buffer;
EOD:
	/* We are finished, set f_pos to EOD. */
	filp->f_pos = vdir->i_size + vol->mft_record_size;
abort:
	put_attr_search_ctx(ctx);
	unmap_mft_record(READ, ndir);
	kfree(name);
done:
#ifdef DEBUG
	if (!rc)
		ntfs_debug("EOD, f_pos 0x%Lx, returning 0.", filp->f_pos);
	else
		ntfs_debug("filldir returned %i, f_pos 0x%Lx, returning 0.",
				rc, filp->f_pos);
#endif
	return 0;
map_page_err_out:
	ntfs_error(sb, "Reading index allocation data failed.");
	err = PTR_ERR(page);
kf_unm_err_out:
	kfree(name);
put_unm_err_out:
	put_attr_search_ctx(ctx);
unm_err_out:
	unmap_mft_record(READ, ndir);
err_out:
	ntfs_debug("Failed. Returning error code %i.", -err);
	return err;
unm_dir_err_out:
	ntfs_unmap_page(page);
dir_err_out:
	ntfs_error(sb, "Corrupt directory. Aborting. You should run chkdsk.");
	err = -EIO;
	goto kf_unm_err_out;
}

struct file_operations ntfs_dir_ops = {
	llseek:		generic_file_llseek,	/* Seek inside directory. */
	read:		generic_read_dir,	/* Return -EISDIR. */
	readdir:	ntfs_readdir,		/* Read directory contents. */
};

