/*
 *  linux/fs/nfs/dir.c
 *
 *  Copyright (C) 1992  Rick Sladkey
 *
 *  nfs directory handling functions
 *
 * 10 Apr 1996	Added silly rename for unlink	--okir
 * 28 Sep 1996	Improved directory cache --okir
 * 23 Aug 1997  Claus Heine claus@momo.math.rwth-aachen.de 
 *              Re-implemented silly rename for unlink, newly implemented
 *              silly rename for nfs_rename() following the suggestions
 *              of Olaf Kirch (okir) found in this file.
 *              Following Linus comments on my original hack, this version
 *              depends only on the dcache stuff and doesn't touch the inode
 *              layer (iput() and friends).
 *  6 Jun 1999	Cache readdir lookups in the page cache. -DaveM
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/sunrpc/clnt.h>
#include <linux/nfs_fs.h>
#include <linux/nfs_mount.h>
#include <linux/pagemap.h>

#include <asm/segment.h>	/* for fs functions */

#define NFS_PARANOIA 1
/* #define NFS_DEBUG_VERBOSE 1 */

static int nfs_safe_remove(struct dentry *);

static int nfs_readdir(struct file *, void *, filldir_t);
static struct dentry *nfs_lookup(struct inode *, struct dentry *);
static int nfs_create(struct inode *, struct dentry *, int);
static int nfs_mkdir(struct inode *, struct dentry *, int);
static int nfs_rmdir(struct inode *, struct dentry *);
static int nfs_unlink(struct inode *, struct dentry *);
static int nfs_symlink(struct inode *, struct dentry *, const char *);
static int nfs_link(struct dentry *, struct inode *, struct dentry *);
static int nfs_mknod(struct inode *, struct dentry *, int, int);
static int nfs_rename(struct inode *, struct dentry *,
		      struct inode *, struct dentry *);

struct file_operations nfs_dir_operations = {
	read:		generic_read_dir,
	readdir:	nfs_readdir,
	open:		nfs_open,
	release:	nfs_release,
};

struct inode_operations nfs_dir_inode_operations = {
	create:		nfs_create,
	lookup:		nfs_lookup,
	link:		nfs_link,
	unlink:		nfs_unlink,
	symlink:	nfs_symlink,
	mkdir:		nfs_mkdir,
	rmdir:		nfs_rmdir,
	mknod:		nfs_mknod,
	rename:		nfs_rename,
	revalidate:	nfs_revalidate,
	setattr:	nfs_notify_change,
};

typedef u32 * (*decode_dirent_t)(u32 *, struct nfs_entry *, int);

/*
 * Given a pointer to a buffer that has already been filled by a call
 * to readdir, find the next entry.
 *
 * If the end of the buffer has been reached, return -EAGAIN, if not,
 * return the offset within the buffer of the next entry to be
 * read.
 */
static inline
long find_dirent(struct page *page, loff_t offset,
		 struct nfs_entry *entry,
		 decode_dirent_t decode, int plus, int use_cookie)
{
	u8		*p = (u8 *)kmap(page),
			*start = p;
	unsigned long	base = page_offset(page),
			pg_offset = 0;
	int		loop_count = 0;

	if (!p)
		return -EIO;
	for(;;) {
		p = (u8*)decode((__u32*)p, entry, plus);
		if (IS_ERR(p))
			break;
		pg_offset = p - start;
		entry->prev = entry->offset;
		entry->offset = base + pg_offset;
		if ((use_cookie ? entry->cookie : entry->offset) > offset)
			break;
		if (loop_count++ > 200) {
			loop_count = 0;
			schedule();
		}
	}

	kunmap(page);
	return (IS_ERR(p)) ?  PTR_ERR(p) : (long)pg_offset;
}

/*
 * Find the given page, and call find_dirent() in order to try to
 * return the next entry.
 *
 * Returns -EIO if the page is not available, or up to date.
 */
static inline
long find_dirent_page(struct inode *inode, loff_t offset,
		      struct nfs_entry *entry)
{
	decode_dirent_t	decode = nfs_decode_dirent;
	struct page	*page;
	unsigned long	index = entry->offset >> PAGE_CACHE_SHIFT;
	long		status = -EIO;
	int		plus = NFS_USE_READDIRPLUS(inode),
			use_cookie = NFS_MONOTONE_COOKIES(inode);

	dfprintk(VFS, "NFS: find_dirent_page() searching directory page %ld\n", entry->offset & PAGE_CACHE_MASK);

	if (entry->page)
		page_cache_release(entry->page);

	page = find_get_page(&inode->i_data, index);

	if (page && Page_Uptodate(page))
		status = find_dirent(page, offset, entry, decode, plus, use_cookie);

	/* NB: on successful return we will be holding the page */
	if (status < 0) {
		entry->page = NULL;
		if (page)
			page_cache_release(page);
	} else
		entry->page = page;

	dfprintk(VFS, "NFS: find_dirent_page() returns %ld\n", status);
	return status;
}


/*
 * Recurse through the page cache pages, and return a
 * filled nfs_entry structure of the next directory entry if possible.
 *
 * The target for the search is position 'offset'.
 * The latter may either be an offset into the page cache, or (better)
 * a cookie depending on whether we're interested in strictly following
 * the RFC wrt. not assuming monotonicity of cookies or not.
 *
 * For most systems, the latter is more reliable since it naturally
 * copes with holes in the directory.
 */
static inline
long search_cached_dirent_pages(struct inode *inode, loff_t offset,
				struct nfs_entry *entry)
{
	long		res = 0;
	int		loop_count = 0;

	dfprintk(VFS, "NFS: search_cached_dirent_pages() searching for cookie %Ld\n", (long long)offset);
	for (;;) {
		res = find_dirent_page(inode, offset, entry);
		if (res == -EAGAIN) {
			/* Align to beginning of next page */
			entry->offset &= PAGE_CACHE_MASK;
			entry->offset += PAGE_CACHE_SIZE;
		}
		if (res != -EAGAIN)
			break;
		if (loop_count++ > 200) {
			loop_count = 0;
			schedule();
		}
	}
	if (res < 0 && entry->page) {
		page_cache_release(entry->page);
		entry->page = NULL;
	}
	dfprintk(VFS, "NFS: search_cached_dirent_pages() returned %ld\n", res);
	return res;
}


/* Now we cache directories properly, by stuffing the dirent
 * data directly in the page cache.
 *
 * Inode invalidation due to refresh etc. takes care of
 * _everything_, no sloppy entry flushing logic, no extraneous
 * copying, network direct to page cache, the way it was meant
 * to be.
 *
 * NOTE: Dirent information verification is done always by the
 *	 page-in of the RPC reply, nowhere else, this simplies
 *	 things substantially.
 */
static inline
long try_to_get_dirent_page(struct file *file, struct inode *inode,
			    struct nfs_entry *entry)
{
	struct dentry	*dir = file->f_dentry;
	struct page	*page;
	struct nfs_fattr	dir_attr;
	__u32		*p;
	unsigned long	index = entry->offset >> PAGE_CACHE_SHIFT;
	long		res = 0;
	unsigned int	dtsize = NFS_SERVER(inode)->dtsize;
	int		plus = NFS_USE_READDIRPLUS(inode);

	dfprintk(VFS, "NFS: try_to_get_dirent_page() reading directory page @ index %ld\n", index);

	page = grab_cache_page(&inode->i_data, index);

	if (!page) {
		res = -ENOMEM;
		goto out;
	}

	if (Page_Uptodate(page)) {
		dfprintk(VFS, "NFS: try_to_get_dirent_page(): page already up to date.\n");
		goto unlock_out;
	}

	p = (__u32 *)kmap(page);

	if (dtsize > PAGE_CACHE_SIZE)
		dtsize = PAGE_CACHE_SIZE;
	res = nfs_proc_readdir(dir, &dir_attr, entry->cookie, p, dtsize, plus);

	kunmap(page);

	if (res < 0)
		goto error;
	nfs_refresh_inode(inode, &dir_attr);
	if (PageError(page))
		ClearPageError(page);
	SetPageUptodate(page);

 unlock_out:
	UnlockPage(page);
	page_cache_release(page);
 out:
	dfprintk(VFS, "NFS: try_to_get_dirent_page() returns %ld\n", res);
	return res;
 error:
	SetPageError(page);
	goto unlock_out;
}

/* Recover from a revalidation flush.  The case here is that
 * the inode for the directory got invalidated somehow, and
 * all of our cached information is lost.  In order to get
 * a correct cookie for the current readdir request from the
 * user, we must (re-)fetch all the older readdir page cache
 * entries.
 *
 * Returns < 0 if some error occurs.
 */
static inline
long refetch_to_readdir(struct file *file, struct inode *inode,
			loff_t off, struct nfs_entry *entry)
{
	struct nfs_entry	my_dirent,
				*dirent = &my_dirent;
	long			res;
	int			plus = NFS_USE_READDIRPLUS(inode),
				use_cookie = NFS_MONOTONE_COOKIES(inode),
				loop_count = 0;

	dfprintk(VFS, "NFS: refetch_to_readdir() searching for cookie %Ld\n", (long long)off);
	*dirent = *entry;
	entry->page = NULL;

	for (res = 0;res >= 0;) {
		if (loop_count++ > 200) {
			loop_count = 0;
			schedule();
		}

		/* Search for last cookie in page cache */
		res = search_cached_dirent_pages(inode, off, dirent);

		if (res >= 0) {
			/* Cookie was found */
			if ((use_cookie?dirent->cookie:dirent->offset) > off) {
				*entry = *dirent;
				dirent->page = NULL;
				break;
			}
			continue;
		}

		if (dirent->page)
			page_cache_release(dirent->page);
		dirent->page = NULL;

		if (res != -EIO) {
			*entry = *dirent;
			break;
		}

		/* Read in a new page */
		res = try_to_get_dirent_page(file, inode, dirent);
		if (res == -EBADCOOKIE) {
			memset(dirent, 0, sizeof(*dirent));
			nfs_zap_caches(inode);
			res = 0;
		}
		/* We requested READDIRPLUS, but the server doesn't grok it */
		if (plus && res == -ENOTSUPP) {
			NFS_FLAGS(inode) &= ~NFS_INO_ADVISE_RDPLUS;
			memset(dirent, 0, sizeof(*dirent));
			nfs_zap_caches(inode);
			plus = 0;
			res = 0;
		}
	}
	if (dirent->page)
		page_cache_release(dirent->page);

	dfprintk(VFS, "NFS: refetch_to_readdir() returns %ld\n", res);
	return res;
}

/*
 * Once we've found the start of the dirent within a page: fill 'er up...
 */
static
int nfs_do_filldir(struct file *file, struct inode *inode,
		   struct nfs_entry *entry, void *dirent, filldir_t filldir)
{
	decode_dirent_t	decode = nfs_decode_dirent;
	struct page	*page = entry->page;
	__u8		*p,
			*start;
	unsigned long	base = page_offset(page),
			offset = entry->offset,
			pg_offset,
			fileid;
	int		plus = NFS_USE_READDIRPLUS(inode),
			use_cookie = NFS_MONOTONE_COOKIES(inode),
			loop_count = 0,
			res = 0;

	dfprintk(VFS, "NFS: nfs_do_filldir() filling starting @ offset %ld\n", entry->offset);
	pg_offset = offset & ~PAGE_CACHE_MASK;
	start = (u8*)kmap(page);
	p = start + pg_offset;

	for(;;) {
		/* Note: entry->prev contains the offset of the start of the
		 *       current dirent */
		fileid = nfs_fileid_to_ino_t(entry->ino);
		if (use_cookie)
			res = filldir(dirent, entry->name, entry->len, entry->prev_cookie, fileid);
		else
			res = filldir(dirent, entry->name, entry->len, entry->prev, fileid);
		if (res < 0)
			break;
		file->f_pos = (use_cookie) ? entry->cookie : entry->offset;
		p = (u8*)decode((__u32*)p, entry, plus);
		if (!p || IS_ERR(p))
			break;
		pg_offset = p - start;
		entry->prev = entry->offset;
		entry->offset = base + pg_offset;
		if (loop_count++ > 200) {
			loop_count = 0;
			schedule();
		}
	}
	kunmap(page);

	dfprintk(VFS, "NFS: nfs_do_filldir() filling ended @ offset %ld; returning = %d\n", entry->offset, res);
	return res;
}

/* The file offset position is now represented as a true offset into the
 * page cache as is the case in most of the other filesystems.
 */
static int nfs_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct dentry	*dentry = filp->f_dentry;
	struct inode	*inode = dentry->d_inode;
	struct page	*page;
	struct nfs_entry my_entry,
			*entry = &my_entry;
	loff_t		offset;
	long		res;

	res = nfs_revalidate(dentry);
	if (res < 0)
		return res;

	/*
	 * filp->f_pos points to the file offset in the page cache.
	 * but if the cache has meanwhile been zapped, we need to
	 * read from the last dirent to revalidate f_pos
	 * itself.
	 */
	memset(entry, 0, sizeof(*entry));

	offset = filp->f_pos;

	while(!entry->eof) {
		res = search_cached_dirent_pages(inode, offset, entry);

		if (res < 0) {
			if (entry->eof)
				break;
			res = refetch_to_readdir(filp, inode, offset, entry);
			if (res < 0)
				break;
		}

		page = entry->page;
		if (!page)
			printk(KERN_ERR "NFS: Missing page...\n");
		res = nfs_do_filldir(filp, inode, entry, dirent, filldir);
		page_cache_release(page);
		entry->page = NULL;
		if (res < 0) {
			res = 0;
			break;
		}
		offset = filp->f_pos;
	}
	if (entry->page)
		page_cache_release(entry->page);
	if (res < 0 && res != -EBADCOOKIE)
		return res;
	return 0;
}

/*
 * Whenever an NFS operation succeeds, we know that the dentry
 * is valid, so we update the revalidation timestamp.
 */
static inline void nfs_renew_times(struct dentry * dentry)
{
	dentry->d_time = jiffies;
}

static inline int nfs_dentry_force_reval(struct dentry *dentry, int flags)
{
	struct inode *inode = dentry->d_inode;
	unsigned long timeout = NFS_ATTRTIMEO(inode);

	/*
	 * If it's the last lookup in a series, we use a stricter
	 * cache consistency check by looking at the parent mtime.
	 *
	 * If it's been modified in the last hour, be really strict.
	 * (This still means that we can avoid doing unnecessary
	 * work on directories like /usr/share/bin etc which basically
	 * never change).
	 */
	if (!(flags & LOOKUP_CONTINUE)) {
		long diff = CURRENT_TIME - dentry->d_parent->d_inode->i_mtime;

		if (diff < 15*60)
			timeout = 0;
	}
	
	return time_after(jiffies,dentry->d_time + timeout);
}

/*
 * We judge how long we want to trust negative
 * dentries by looking at the parent inode mtime.
 *
 * If mtime is close to present time, we revalidate
 * more often.
 */
#define NFS_REVALIDATE_NEGATIVE (1 * HZ)
static inline int nfs_neg_need_reval(struct dentry *dentry)
{
	struct inode *dir = dentry->d_parent->d_inode;
	unsigned long timeout = NFS_ATTRTIMEO(dir);
	long diff = CURRENT_TIME - dir->i_mtime;

	if (diff < 5*60 && timeout > NFS_REVALIDATE_NEGATIVE)
		timeout = NFS_REVALIDATE_NEGATIVE;

	return time_after(jiffies, dentry->d_time + timeout);
}

/*
 * This is called every time the dcache has a lookup hit,
 * and we should check whether we can really trust that
 * lookup.
 *
 * NOTE! The hit can be a negative hit too, don't assume
 * we have an inode!
 *
 * If the dentry is older than the revalidation interval, 
 * we do a new lookup and verify that the dentry is still
 * correct.
 */
static int nfs_lookup_revalidate(struct dentry * dentry, int flags)
{
	struct dentry * parent = dentry->d_parent;
	struct inode * inode = dentry->d_inode;
	int error;
	struct nfs_fh fhandle;
	struct nfs_fattr fattr;

	/*
	 * If we don't have an inode, let's look at the parent
	 * directory mtime to get a hint about how often we
	 * should validate things..
	 */
	if (!inode) {
		if (nfs_neg_need_reval(dentry))
			goto out_bad;
		goto out_valid;
	}

	if (is_bad_inode(inode)) {
		dfprintk(VFS, "nfs_lookup_validate: %s/%s has dud inode\n",
			parent->d_name.name, dentry->d_name.name);
		goto out_bad;
	}

	if (!nfs_dentry_force_reval(dentry, flags))
		goto out_valid;

	if (IS_ROOT(dentry)) {
		__nfs_revalidate_inode(NFS_DSERVER(dentry), dentry);
		goto out_valid_renew;
	}

	/*
	 * Do a new lookup and check the dentry attributes.
	 */
	error = nfs_proc_lookup(NFS_DSERVER(parent), NFS_FH(parent),
				dentry->d_name.name, &fhandle, &fattr);
	if (error)
		goto out_bad;

	/* Inode number matches? */
	if (!(fattr.valid & NFS_ATTR_FATTR) ||
	    NFS_FSID(inode) != fattr.fsid ||
	    NFS_FILEID(inode) != fattr.fileid)
		goto out_bad;

	/* Filehandle matches? */
	if (memcmp(dentry->d_fsdata, &fhandle, sizeof(struct nfs_fh)))
		goto out_bad;

	/* Ok, remeber that we successfully checked it.. */
	nfs_refresh_inode(inode, &fattr);

 out_valid_renew:
	nfs_renew_times(dentry);
out_valid:
	return 1;
out_bad:
	if (!list_empty(&dentry->d_subdirs))
		shrink_dcache_parent(dentry);
	/* If we have submounts, don't unhash ! */
	if (have_submounts(dentry))
		goto out_valid;
	d_drop(dentry);
	/* Purge readdir caches. */
	if (dentry->d_parent->d_inode)
		nfs_zap_caches(dentry->d_parent->d_inode);
	if (inode && S_ISDIR(inode->i_mode))
		nfs_zap_caches(inode);
	return 0;
}

/*
 * This is called from dput() when d_count is going to 0.
 * We use it to clean up silly-renamed files.
 */
static void nfs_dentry_delete(struct dentry *dentry)
{
	dfprintk(VFS, "NFS: dentry_delete(%s/%s, %x)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		dentry->d_flags);

	if (dentry->d_flags & DCACHE_NFSFS_RENAMED) {
		int error;
		
		dentry->d_flags &= ~DCACHE_NFSFS_RENAMED;
		/* Unhash it first */
		d_drop(dentry);
		error = nfs_safe_remove(dentry);
		if (error)
			printk("NFS: can't silly-delete %s/%s, error=%d\n",
				dentry->d_parent->d_name.name,
				dentry->d_name.name, error);
	}

}

static kmem_cache_t *nfs_fh_cachep;

__inline__ struct nfs_fh *nfs_fh_alloc(void)
{
	return kmem_cache_alloc(nfs_fh_cachep, SLAB_KERNEL);
}

__inline__ void nfs_fh_free(struct nfs_fh *p)
{
	kmem_cache_free(nfs_fh_cachep, p);
}

/*
 * Called when the dentry is being freed to release private memory.
 */
static void nfs_dentry_release(struct dentry *dentry)
{
	if (dentry->d_fsdata)
		nfs_fh_free(dentry->d_fsdata);
}

struct dentry_operations nfs_dentry_operations = {
	d_revalidate:	nfs_lookup_revalidate,
	d_delete:	nfs_dentry_delete,
	d_release:	nfs_dentry_release,
};

#if 0 /* dead code */
#ifdef NFS_PARANOIA
/*
 * Display all dentries holding the specified inode.
 */
static void show_dentry(struct list_head * dlist)
{
	struct list_head *tmp = dlist;

	while ((tmp = tmp->next) != dlist) {
		struct dentry * dentry = list_entry(tmp, struct dentry, d_alias);
		const char * unhashed = "";

		if (d_unhashed(dentry))
			unhashed = "(unhashed)";

		printk("show_dentry: %s/%s, d_count=%d%s\n",
			dentry->d_parent->d_name.name,
			dentry->d_name.name, dentry->d_count,
			unhashed);
	}
}
#endif /* NFS_PARANOIA */
#endif /* 0 */

static struct dentry *nfs_lookup(struct inode *dir, struct dentry * dentry)
{
	struct inode *inode;
	int error;
	struct nfs_fh fhandle;
	struct nfs_fattr fattr;

	dfprintk(VFS, "NFS: lookup(%s/%s)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name);

	error = -ENAMETOOLONG;
	if (dentry->d_name.len > NFS_MAXNAMLEN)
		goto out;

	error = -ENOMEM;
	if (!dentry->d_fsdata) {
		dentry->d_fsdata = nfs_fh_alloc();
		if (!dentry->d_fsdata)
			goto out;
	}
	dentry->d_op = &nfs_dentry_operations;

	error = nfs_proc_lookup(NFS_SERVER(dir), NFS_FH(dentry->d_parent), 
				dentry->d_name.name, &fhandle, &fattr);
	inode = NULL;
	if (error == -ENOENT)
		goto no_entry;
	if (!error) {
		error = -EACCES;
		inode = nfs_fhget(dentry, &fhandle, &fattr);
		if (inode) {
	    no_entry:
			d_add(dentry, inode);
			nfs_renew_times(dentry);
			error = 0;
		}
	}
out:
	return ERR_PTR(error);
}

/*
 * Code common to create, mkdir, and mknod.
 */
static int nfs_instantiate(struct dentry *dentry, struct nfs_fh *fhandle,
				struct nfs_fattr *fattr)
{
	struct inode *inode;
	int error = -EACCES;

	inode = nfs_fhget(dentry, fhandle, fattr);
	if (inode) {
		d_instantiate(dentry, inode);
		nfs_renew_times(dentry);
		error = 0;
	}
	return error;
}

/*
 * Following a failed create operation, we drop the dentry rather
 * than retain a negative dentry. This avoids a problem in the event
 * that the operation succeeded on the server, but an error in the
 * reply path made it appear to have failed.
 */
static int nfs_create(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;
	struct iattr attr;
	struct nfs_fattr fattr;
	struct nfs_fh fhandle;

	dfprintk(VFS, "NFS: create(%x/%ld, %s\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

	attr.ia_mode = mode;
	attr.ia_valid = ATTR_MODE;

	/*
	 * Invalidate the dir cache before the operation to avoid a race.
	 */
	nfs_zap_caches(dir);
	error = nfs_proc_create(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
			dentry->d_name.name, &attr, &fhandle, &fattr);
	if (!error)
		error = nfs_instantiate(dentry, &fhandle, &fattr);
	if (error)
		d_drop(dentry);
	return error;
}

/*
 * See comments for nfs_proc_create regarding failed operations.
 */
static int nfs_mknod(struct inode *dir, struct dentry *dentry, int mode, int rdev)
{
	int error;
	struct iattr attr;
	struct nfs_fattr fattr;
	struct nfs_fh fhandle;

	dfprintk(VFS, "NFS: mknod(%x/%ld, %s\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

	attr.ia_mode = mode;
	attr.ia_valid = ATTR_MODE;
	/* FIXME: move this to a special nfs_proc_mknod() */
	if (S_ISCHR(mode) || S_ISBLK(mode)) {
		attr.ia_size = rdev; /* get out your barf bag */
		attr.ia_valid |= ATTR_SIZE;
	}

	nfs_zap_caches(dir);
	error = nfs_proc_create(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
				dentry->d_name.name, &attr, &fhandle, &fattr);
	if (!error)
		error = nfs_instantiate(dentry, &fhandle, &fattr);
	if (error)
		d_drop(dentry);
	return error;
}

/*
 * See comments for nfs_proc_create regarding failed operations.
 */
static int nfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int error;
	struct iattr attr;
	struct nfs_fattr fattr;
	struct nfs_fh fhandle;

	dfprintk(VFS, "NFS: mkdir(%x/%ld, %s\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

	attr.ia_valid = ATTR_MODE;
	attr.ia_mode = mode | S_IFDIR;

	/*
	 * Always drop the dentry, we can't always depend on
	 * the fattr returned by the server (AIX seems to be
	 * broken). We're better off doing another lookup than
	 * depending on potentially bogus information.
	 */
	d_drop(dentry);
	nfs_zap_caches(dir);
	error = nfs_proc_mkdir(NFS_DSERVER(dentry), NFS_FH(dentry->d_parent),
				dentry->d_name.name, &attr, &fhandle, &fattr);
	if (!error)
		dir->i_nlink++;
	return error;
}

static int nfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: rmdir(%x/%ld, %s\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

	nfs_zap_caches(dir);
	error = nfs_proc_rmdir(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
				dentry->d_name.name);

	/* Update i_nlink and invalidate dentry. */
	if (!error) {
		d_drop(dentry);
		if (dir->i_nlink)
			dir->i_nlink--;
	}

	return error;
}

static int nfs_sillyrename(struct inode *dir, struct dentry *dentry)
{
	static unsigned int sillycounter = 0;
	const int      i_inosize  = sizeof(dir->i_ino)*2;
	const int      countersize = sizeof(sillycounter)*2;
	const int      slen       = strlen(".nfs") + i_inosize + countersize;
	char           silly[slen+1];
	struct dentry *sdentry;
	int            error = -EIO;

	dfprintk(VFS, "NFS: silly-rename(%s/%s, ct=%d)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name, 
		dentry->d_count);

	/*
	 * Note that a silly-renamed file can be deleted once it's
	 * no longer in use -- it's just an ordinary file now.
	 */
	if (dentry->d_count == 1) {
		dentry->d_flags &= ~DCACHE_NFSFS_RENAMED;
		goto out;  /* No need to silly rename. */
	}

#ifdef NFS_PARANOIA
if (!dentry->d_inode)
printk("NFS: silly-renaming %s/%s, negative dentry??\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	/*
	 * We don't allow a dentry to be silly-renamed twice.
	 */
	error = -EBUSY;
	if (dentry->d_flags & DCACHE_NFSFS_RENAMED)
		goto out;

	sprintf(silly, ".nfs%*.*lx",
		i_inosize, i_inosize, dentry->d_inode->i_ino);

	sdentry = NULL;
	do {
		char *suffix = silly + slen - countersize;

		dput(sdentry);
		sillycounter++;
		sprintf(suffix, "%*.*x", countersize, countersize, sillycounter);

		dfprintk(VFS, "trying to rename %s to %s\n",
			 dentry->d_name.name, silly);
		
		sdentry = lookup_one(silly, dget(dentry->d_parent));
		/*
		 * N.B. Better to return EBUSY here ... it could be
		 * dangerous to delete the file while it's in use.
		 */
		if (IS_ERR(sdentry))
			goto out;
	} while(sdentry->d_inode != NULL); /* need negative lookup */

	nfs_zap_caches(dir);
	error = nfs_proc_rename(NFS_SERVER(dir),
				NFS_FH(dentry->d_parent), dentry->d_name.name,
				NFS_FH(dentry->d_parent), silly);
	if (!error) {
		nfs_renew_times(dentry);
		d_move(dentry, sdentry);
		dentry->d_flags |= DCACHE_NFSFS_RENAMED;
 		/* If we return 0 we don't unlink */
	}
	dput(sdentry);
out:
	return error;
}

/*
 * Remove a file after making sure there are no pending writes,
 * and after checking that the file has only one user. 
 *
 * We update inode->i_nlink and free the inode prior to the operation
 * to avoid possible races if the server reuses the inode.
 */
static int nfs_safe_remove(struct dentry *dentry)
{
	struct inode *dir = dentry->d_parent->d_inode;
	struct inode *inode = dentry->d_inode;
	int error, rehash = 0;
		
	dfprintk(VFS, "NFS: safe_remove(%s/%s, %ld)\n",
		dentry->d_parent->d_name.name, dentry->d_name.name,
		inode->i_ino);

	/* N.B. not needed now that d_delete is done in advance? */
	error = -EBUSY;
	if (!inode) {
#ifdef NFS_PARANOIA
printk("nfs_safe_remove: %s/%s already negative??\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	}

	if (dentry->d_count > 1) {
#ifdef NFS_PARANOIA
printk("nfs_safe_remove: %s/%s busy, d_count=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, dentry->d_count);
#endif
		goto out;
	}
	/*
	 * Unhash the dentry while we remove the file ...
	 */
	if (!d_unhashed(dentry)) {
		d_drop(dentry);
		rehash = 1;
	}
	/*
	 * Update i_nlink and free the inode before unlinking.
	 */
	if (inode) {
		if (inode->i_nlink)
			inode->i_nlink --;
		d_delete(dentry);
	}
	nfs_zap_caches(dir);
	error = nfs_proc_remove(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
				dentry->d_name.name);
	/*
	 * Rehash the negative dentry if the operation succeeded.
	 */
	if (!error && rehash)
		d_add(dentry, NULL);
out:
	return error;
}

/*  We do silly rename. In case sillyrename() returns -EBUSY, the inode
 *  belongs to an active ".nfs..." file and we return -EBUSY.
 *
 *  If sillyrename() returns 0, we do nothing, otherwise we unlink.
 */
static int nfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int error;

	dfprintk(VFS, "NFS: unlink(%x/%ld, %s)\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name);

	error = nfs_sillyrename(dir, dentry);
	if (error && error != -EBUSY) {
		error = nfs_safe_remove(dentry);
		if (!error) {
			nfs_renew_times(dentry);
		}
	}
	return error;
}

static int
nfs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct iattr attr;
	int error;

	dfprintk(VFS, "NFS: symlink(%x/%ld, %s, %s)\n",
		dir->i_dev, dir->i_ino, dentry->d_name.name, symname);

	error = -ENAMETOOLONG;
	if (strlen(symname) > NFS_MAXPATHLEN)
		goto out;

#ifdef NFS_PARANOIA
if (dentry->d_inode)
printk("nfs_proc_symlink: %s/%s not negative!\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	/*
	 * Fill in the sattr for the call.
 	 * Note: SunOS 4.1.2 crashes if the mode isn't initialized!
	 */
	attr.ia_valid = ATTR_MODE;
	attr.ia_mode = S_IFLNK | S_IRWXUGO;

	/*
	 * Drop the dentry in advance to force a new lookup.
	 * Since nfs_proc_symlink doesn't return a fattr, we
	 * can't instantiate the new inode.
	 */
	d_drop(dentry);
	nfs_zap_caches(dir);
	error = nfs_proc_symlink(NFS_SERVER(dir), NFS_FH(dentry->d_parent),
				dentry->d_name.name, symname, &attr);
	if (!error) {
		nfs_renew_times(dentry->d_parent);
	} else if (error == -EEXIST) {
		printk("nfs_proc_symlink: %s/%s already exists??\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
	}

out:
	return error;
}

static int 
nfs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	int error;

	dfprintk(VFS, "NFS: link(%s/%s -> %s/%s)\n",
		old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
		dentry->d_parent->d_name.name, dentry->d_name.name);

	/*
	 * Drop the dentry in advance to force a new lookup.
	 * Since nfs_proc_link doesn't return a file handle,
	 * we can't use the existing dentry.
	 */
	d_drop(dentry);
	nfs_zap_caches(dir);
	error = nfs_proc_link(NFS_DSERVER(old_dentry), NFS_FH(old_dentry),
				NFS_FH(dentry->d_parent), dentry->d_name.name);
	if (!error) {
 		/*
		 * Update the link count immediately, as some apps
		 * (e.g. pine) test this after making a link.
		 */
		inode->i_nlink++;
	}
	return error;
}

/*
 * RENAME
 * FIXME: Some nfsds, like the Linux user space nfsd, may generate a
 * different file handle for the same inode after a rename (e.g. when
 * moving to a different directory). A fail-safe method to do so would
 * be to look up old_dir/old_name, create a link to new_dir/new_name and
 * rename the old file using the sillyrename stuff. This way, the original
 * file in old_dir will go away when the last process iput()s the inode.
 *
 * FIXED.
 * 
 * It actually works quite well. One needs to have the possibility for
 * at least one ".nfs..." file in each directory the file ever gets
 * moved or linked to which happens automagically with the new
 * implementation that only depends on the dcache stuff instead of
 * using the inode layer
 *
 * Unfortunately, things are a little more complicated than indicated
 * above. For a cross-directory move, we want to make sure we can get
 * rid of the old inode after the operation.  This means there must be
 * no pending writes (if it's a file), and the use count must be 1.
 * If these conditions are met, we can drop the dentries before doing
 * the rename.
 */
static int nfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		      struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *old_inode = old_dentry->d_inode;
	struct inode *new_inode = new_dentry->d_inode;
	struct dentry *dentry = NULL;
	int error, rehash = 0;

	dfprintk(VFS, "NFS: rename(%s/%s -> %s/%s, ct=%d)\n",
		 old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
		 new_dentry->d_parent->d_name.name, new_dentry->d_name.name,
		 new_dentry->d_count);

	/*
	 * First check whether the target is busy ... we can't
	 * safely do _any_ rename if the target is in use.
	 *
	 * For files, make a copy of the dentry and then do a 
	 * silly-rename. If the silly-rename succeeds, the
	 * copied dentry is hashed and becomes the new target.
	 */
	if (!new_inode)
		goto go_ahead;
	error = -EBUSY;
	if (S_ISDIR(new_inode->i_mode))
		goto out;
	else if (new_dentry->d_count > 1) {
		int err;
		/* copy the target dentry's name */
		dentry = d_alloc(new_dentry->d_parent,
				 &new_dentry->d_name);
		if (!dentry)
			goto out;

		/* silly-rename the existing target ... */
		err = nfs_sillyrename(new_dir, new_dentry);
		if (!err) {
			new_dentry = dentry;
			new_inode = NULL;
			/* hash the replacement target */
			d_add(new_dentry, NULL);
		}

		/* dentry still busy? */
		if (new_dentry->d_count > 1) {
#ifdef NFS_PARANOIA
			printk("nfs_rename: target %s/%s busy, d_count=%d\n",
			       new_dentry->d_parent->d_name.name,
			       new_dentry->d_name.name,
			       new_dentry->d_count);
#endif
			goto out;
		}
	}

go_ahead:
	/*
	 * ... prune child dentries and writebacks if needed.
	 */
	if (old_dentry->d_count > 1) {
		nfs_wb_all(old_inode);
		shrink_dcache_parent(old_dentry);
	}

	/*
	 * To prevent any new references to the target during the rename,
	 * we unhash the dentry and free the inode in advance.
	 */
	if (!d_unhashed(new_dentry)) {
		d_drop(new_dentry);
		rehash = 1;
	}
	if (new_inode)
		d_delete(new_dentry);

	nfs_zap_caches(new_dir);
	nfs_zap_caches(old_dir);
	error = nfs_proc_rename(NFS_DSERVER(old_dentry),
			NFS_FH(old_dentry->d_parent), old_dentry->d_name.name,
			NFS_FH(new_dentry->d_parent), new_dentry->d_name.name);

	NFS_CACHEINV(old_dir);
	NFS_CACHEINV(new_dir);
	/* Update the dcache if needed */
	if (rehash)
		d_add(new_dentry, NULL);
	if (!error && !S_ISDIR(old_inode->i_mode))
		d_move(old_dentry, new_dentry);

out:
	/* new dentry created? */
	if (dentry)
		dput(dentry);
	return error;
}

int nfs_init_fhcache(void)
{
	nfs_fh_cachep = kmem_cache_create("nfs_fh",
					  sizeof(struct nfs_fh),
					  0, SLAB_HWCACHE_ALIGN,
					  NULL, NULL);
	if (nfs_fh_cachep == NULL)
		return -ENOMEM;

	return 0;
}

/*
 * Local variables:
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */
