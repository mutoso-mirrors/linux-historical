/*
 * fs/dcache.c
 *
 * Complete reimplementation
 * (C) 1997 Thomas Schoebel-Theuer,
 * with heavy changes by Linus Torvalds
 */

/*
 * Notes on the allocation strategy:
 *
 * The dcache is a master of the icache - whenever a dcache entry
 * exists, the inode will always exist. "iput()" is done either when
 * the dcache entry is deleted or garbage collected.
 */

#include <linux/config.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/smp_lock.h>
#include <linux/cache.h>
#include <linux/module.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <asm/uaccess.h>
#include <linux/security.h>
#include <linux/seqlock.h>
#include <linux/swap.h>

#define DCACHE_PARANOIA 1
/* #define DCACHE_DEBUG 1 */

spinlock_t dcache_lock __cacheline_aligned_in_smp = SPIN_LOCK_UNLOCKED;
seqlock_t rename_lock __cacheline_aligned_in_smp = SEQLOCK_UNLOCKED;

EXPORT_SYMBOL(dcache_lock);

static kmem_cache_t *dentry_cache; 

/*
 * This is the single most critical data structure when it comes
 * to the dcache: the hashtable for lookups. Somebody should try
 * to make this good - I've just made it work.
 *
 * This hash-function tries to avoid losing too many bits of hash
 * information, yet avoid using a prime hash-size or similar.
 */
#define D_HASHBITS     d_hash_shift
#define D_HASHMASK     d_hash_mask

static unsigned int d_hash_mask;
static unsigned int d_hash_shift;
static struct hlist_head *dentry_hashtable;
static LIST_HEAD(dentry_unused);

/* Statistics gathering. */
struct dentry_stat_t dentry_stat = {
	.age_limit = 45,
};

static void d_callback(void *arg)
{
	struct dentry * dentry = (struct dentry *)arg;

	if (dname_external(dentry)) {
		kfree(dentry->d_qstr);
	}
	kmem_cache_free(dentry_cache, dentry); 
}

/*
 * no dcache_lock, please.  The caller must decrement dentry_stat.nr_dentry
 * inside dcache_lock.
 */
static void d_free(struct dentry *dentry)
{
	if (dentry->d_op && dentry->d_op->d_release)
		dentry->d_op->d_release(dentry);
 	call_rcu(&dentry->d_rcu, d_callback, dentry);
}

/*
 * Release the dentry's inode, using the filesystem
 * d_iput() operation if defined.
 * Called with dcache_lock and per dentry lock held, drops both.
 */
static inline void dentry_iput(struct dentry * dentry)
{
	struct inode *inode = dentry->d_inode;
	if (inode) {
		dentry->d_inode = NULL;
		list_del_init(&dentry->d_alias);
		spin_unlock(&dentry->d_lock);
		spin_unlock(&dcache_lock);
		if (dentry->d_op && dentry->d_op->d_iput)
			dentry->d_op->d_iput(dentry, inode);
		else
			iput(inode);
	} else {
		spin_unlock(&dentry->d_lock);
		spin_unlock(&dcache_lock);
	}
}

/* 
 * This is dput
 *
 * This is complicated by the fact that we do not want to put
 * dentries that are no longer on any hash chain on the unused
 * list: we'd much rather just get rid of them immediately.
 *
 * However, that implies that we have to traverse the dentry
 * tree upwards to the parents which might _also_ now be
 * scheduled for deletion (it may have been only waiting for
 * its last child to go away).
 *
 * This tail recursion is done by hand as we don't want to depend
 * on the compiler to always get this right (gcc generally doesn't).
 * Real recursion would eat up our stack space.
 */

/*
 * dput - release a dentry
 * @dentry: dentry to release 
 *
 * Release a dentry. This will drop the usage count and if appropriate
 * call the dentry unlink method as well as removing it from the queues and
 * releasing its resources. If the parent dentries were scheduled for release
 * they too may now get deleted.
 *
 * no dcache lock, please.
 */

void dput(struct dentry *dentry)
{
	if (!dentry)
		return;

repeat:
	if (!atomic_dec_and_lock(&dentry->d_count, &dcache_lock))
		return;

	spin_lock(&dentry->d_lock);
	if (atomic_read(&dentry->d_count)) {
		spin_unlock(&dentry->d_lock);
		spin_unlock(&dcache_lock);
		return;
	}
			
	/*
	 * AV: ->d_delete() is _NOT_ allowed to block now.
	 */
	if (dentry->d_op && dentry->d_op->d_delete) {
		if (dentry->d_op->d_delete(dentry))
			goto unhash_it;
	}
	/* Unreachable? Get rid of it */
 	if (d_unhashed(dentry))
		goto kill_it;
  	if (list_empty(&dentry->d_lru)) {
  		dentry->d_vfs_flags |= DCACHE_REFERENCED;
  		list_add(&dentry->d_lru, &dentry_unused);
  		dentry_stat.nr_unused++;
  	}
 	spin_unlock(&dentry->d_lock);
	spin_unlock(&dcache_lock);
	return;

unhash_it:
	__d_drop(dentry);

kill_it: {
		struct dentry *parent;

		/* If dentry was on d_lru list
		 * delete it from there
		 */
  		if (!list_empty(&dentry->d_lru)) {
  			list_del(&dentry->d_lru);
  			dentry_stat.nr_unused--;
  		}
  		list_del(&dentry->d_child);
		dentry_stat.nr_dentry--;	/* For d_free, below */
		/*drops the locks, at that point nobody can reach this dentry */
		dentry_iput(dentry);
		parent = dentry->d_parent;
		d_free(dentry);
		if (dentry == parent)
			return;
		dentry = parent;
		goto repeat;
	}
}

/**
 * d_invalidate - invalidate a dentry
 * @dentry: dentry to invalidate
 *
 * Try to invalidate the dentry if it turns out to be
 * possible. If there are other dentries that can be
 * reached through this one we can't delete it and we
 * return -EBUSY. On success we return 0.
 *
 * no dcache lock.
 */
 
int d_invalidate(struct dentry * dentry)
{
	/*
	 * If it's already been dropped, return OK.
	 */
	spin_lock(&dcache_lock);
	if (d_unhashed(dentry)) {
		spin_unlock(&dcache_lock);
		return 0;
	}
	/*
	 * Check whether to do a partial shrink_dcache
	 * to get rid of unused child entries.
	 */
	if (!list_empty(&dentry->d_subdirs)) {
		spin_unlock(&dcache_lock);
		shrink_dcache_parent(dentry);
		spin_lock(&dcache_lock);
	}

	/*
	 * Somebody else still using it?
	 *
	 * If it's a directory, we can't drop it
	 * for fear of somebody re-populating it
	 * with children (even though dropping it
	 * would make it unreachable from the root,
	 * we might still populate it if it was a
	 * working directory or similar).
	 */
	spin_lock(&dentry->d_lock);
	if (atomic_read(&dentry->d_count) > 1) {
		if (dentry->d_inode && S_ISDIR(dentry->d_inode->i_mode)) {
			spin_unlock(&dentry->d_lock);
			spin_unlock(&dcache_lock);
			return -EBUSY;
		}
	}

	__d_drop(dentry);
	spin_unlock(&dentry->d_lock);
	spin_unlock(&dcache_lock);
	return 0;
}

/* This should be called _only_ with dcache_lock held */

static inline struct dentry * __dget_locked(struct dentry *dentry)
{
	atomic_inc(&dentry->d_count);
	if (atomic_read(&dentry->d_count) == 1) {
		dentry_stat.nr_unused--;
		list_del_init(&dentry->d_lru);
	}
	return dentry;
}

struct dentry * dget_locked(struct dentry *dentry)
{
	return __dget_locked(dentry);
}

/**
 * d_find_alias - grab a hashed alias of inode
 * @inode: inode in question
 *
 * If inode has a hashed alias - acquire the reference to alias and
 * return it. Otherwise return NULL. Notice that if inode is a directory
 * there can be only one alias and it can be unhashed only if it has
 * no children.
 *
 * If the inode has a DCACHE_DISCONNECTED alias, then prefer
 * any other hashed alias over that one.
 */

struct dentry * d_find_alias(struct inode *inode)
{
	struct list_head *head, *next, *tmp;
	struct dentry *alias, *discon_alias=NULL;

	spin_lock(&dcache_lock);
	head = &inode->i_dentry;
	next = inode->i_dentry.next;
	while (next != head) {
		tmp = next;
		next = tmp->next;
		prefetch(next);
		alias = list_entry(tmp, struct dentry, d_alias);
 		if (!d_unhashed(alias)) {
			if (alias->d_flags & DCACHE_DISCONNECTED)
				discon_alias = alias;
			else {
				__dget_locked(alias);
				spin_unlock(&dcache_lock);
				return alias;
			}
		}
	}
	if (discon_alias)
		__dget_locked(discon_alias);
	spin_unlock(&dcache_lock);
	return discon_alias;
}

/*
 *	Try to kill dentries associated with this inode.
 * WARNING: you must own a reference to inode.
 */
void d_prune_aliases(struct inode *inode)
{
	struct list_head *tmp, *head = &inode->i_dentry;
restart:
	spin_lock(&dcache_lock);
	tmp = head;
	while ((tmp = tmp->next) != head) {
		struct dentry *dentry = list_entry(tmp, struct dentry, d_alias);
		if (!atomic_read(&dentry->d_count)) {
			__dget_locked(dentry);
			__d_drop(dentry);
			spin_unlock(&dcache_lock);
			dput(dentry);
			goto restart;
		}
	}
	spin_unlock(&dcache_lock);
}

/*
 * Throw away a dentry - free the inode, dput the parent.
 * This requires that the LRU list has already been
 * removed.
 * Called with dcache_lock, drops it and then regains.
 */
static inline void prune_one_dentry(struct dentry * dentry)
{
	struct dentry * parent;

	__d_drop(dentry);
	list_del(&dentry->d_child);
	dentry_stat.nr_dentry--;	/* For d_free, below */
	dentry_iput(dentry);
	parent = dentry->d_parent;
	d_free(dentry);
	if (parent != dentry)
		dput(parent);
	spin_lock(&dcache_lock);
}

/**
 * prune_dcache - shrink the dcache
 * @count: number of entries to try and free
 *
 * Shrink the dcache. This is done when we need
 * more memory, or simply when we need to unmount
 * something (at which point we need to unuse
 * all dentries).
 *
 * This function may fail to free any resources if
 * all the dentries are in use.
 */
 
static void prune_dcache(int count)
{
	spin_lock(&dcache_lock);
	for (; count ; count--) {
		struct dentry *dentry;
		struct list_head *tmp;

		tmp = dentry_unused.prev;
		if (tmp == &dentry_unused)
			break;
		list_del_init(tmp);
		prefetch(dentry_unused.prev);
 		dentry_stat.nr_unused--;
		dentry = list_entry(tmp, struct dentry, d_lru);

 		spin_lock(&dentry->d_lock);
		/* leave inuse dentries */
 		if (atomic_read(&dentry->d_count)) {
 			spin_unlock(&dentry->d_lock);
			continue;
		}
		/* If the dentry was recently referenced, don't free it. */
		if (dentry->d_vfs_flags & DCACHE_REFERENCED) {
			dentry->d_vfs_flags &= ~DCACHE_REFERENCED;
 			list_add(&dentry->d_lru, &dentry_unused);
 			dentry_stat.nr_unused++;
 			spin_unlock(&dentry->d_lock);
			continue;
		}
		prune_one_dentry(dentry);
	}
	spin_unlock(&dcache_lock);
}

/*
 * Shrink the dcache for the specified super block.
 * This allows us to unmount a device without disturbing
 * the dcache for the other devices.
 *
 * This implementation makes just two traversals of the
 * unused list.  On the first pass we move the selected
 * dentries to the most recent end, and on the second
 * pass we free them.  The second pass must restart after
 * each dput(), but since the target dentries are all at
 * the end, it's really just a single traversal.
 */

/**
 * shrink_dcache_sb - shrink dcache for a superblock
 * @sb: superblock
 *
 * Shrink the dcache for the specified super block. This
 * is used to free the dcache before unmounting a file
 * system
 */

void shrink_dcache_sb(struct super_block * sb)
{
	struct list_head *tmp, *next;
	struct dentry *dentry;

	/*
	 * Pass one ... move the dentries for the specified
	 * superblock to the most recent end of the unused list.
	 */
	spin_lock(&dcache_lock);
	next = dentry_unused.next;
	while (next != &dentry_unused) {
		tmp = next;
		next = tmp->next;
		dentry = list_entry(tmp, struct dentry, d_lru);
		if (dentry->d_sb != sb)
			continue;
		list_del(tmp);
		list_add(tmp, &dentry_unused);
	}

	/*
	 * Pass two ... free the dentries for this superblock.
	 */
repeat:
	next = dentry_unused.next;
	while (next != &dentry_unused) {
		tmp = next;
		next = tmp->next;
		dentry = list_entry(tmp, struct dentry, d_lru);
		if (dentry->d_sb != sb)
			continue;
		dentry_stat.nr_unused--;
		list_del_init(tmp);
		spin_lock(&dentry->d_lock);
		if (atomic_read(&dentry->d_count)) {
			spin_unlock(&dentry->d_lock);
			continue;
		}
		prune_one_dentry(dentry);
		goto repeat;
	}
	spin_unlock(&dcache_lock);
}

/*
 * Search for at least 1 mount point in the dentry's subdirs.
 * We descend to the next level whenever the d_subdirs
 * list is non-empty and continue searching.
 */
 
/**
 * have_submounts - check for mounts over a dentry
 * @parent: dentry to check.
 *
 * Return true if the parent or its subdirectories contain
 * a mount point
 */
 
int have_submounts(struct dentry *parent)
{
	struct dentry *this_parent = parent;
	struct list_head *next;

	spin_lock(&dcache_lock);
	if (d_mountpoint(parent))
		goto positive;
repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) {
		struct list_head *tmp = next;
		struct dentry *dentry = list_entry(tmp, struct dentry, d_child);
		next = tmp->next;
		/* Have we found a mount point ? */
		if (d_mountpoint(dentry))
			goto positive;
		if (!list_empty(&dentry->d_subdirs)) {
			this_parent = dentry;
			goto repeat;
		}
	}
	/*
	 * All done at this level ... ascend and resume the search.
	 */
	if (this_parent != parent) {
		next = this_parent->d_child.next; 
		this_parent = this_parent->d_parent;
		goto resume;
	}
	spin_unlock(&dcache_lock);
	return 0; /* No mount points found in tree */
positive:
	spin_unlock(&dcache_lock);
	return 1;
}

/*
 * Search the dentry child list for the specified parent,
 * and move any unused dentries to the end of the unused
 * list for prune_dcache(). We descend to the next level
 * whenever the d_subdirs list is non-empty and continue
 * searching.
 */
static int select_parent(struct dentry * parent)
{
	struct dentry *this_parent = parent;
	struct list_head *next;
	int found = 0;

	spin_lock(&dcache_lock);
repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) {
		struct list_head *tmp = next;
		struct dentry *dentry = list_entry(tmp, struct dentry, d_child);
		next = tmp->next;

		if (!list_empty(&dentry->d_lru)) {
			dentry_stat.nr_unused--;
			list_del_init(&dentry->d_lru);
		}
		/* 
		 * move only zero ref count dentries to the end 
		 * of the unused list for prune_dcache
		 */
		if (!atomic_read(&dentry->d_count)) {
			list_add(&dentry->d_lru, dentry_unused.prev);
			dentry_stat.nr_unused++;
			found++;
		}
		/*
		 * Descend a level if the d_subdirs list is non-empty.
		 */
		if (!list_empty(&dentry->d_subdirs)) {
			this_parent = dentry;
#ifdef DCACHE_DEBUG
printk(KERN_DEBUG "select_parent: descending to %s/%s, found=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, found);
#endif
			goto repeat;
		}
	}
	/*
	 * All done at this level ... ascend and resume the search.
	 */
	if (this_parent != parent) {
		next = this_parent->d_child.next; 
		this_parent = this_parent->d_parent;
#ifdef DCACHE_DEBUG
printk(KERN_DEBUG "select_parent: ascending to %s/%s, found=%d\n",
this_parent->d_parent->d_name.name, this_parent->d_name.name, found);
#endif
		goto resume;
	}
	spin_unlock(&dcache_lock);
	return found;
}

/**
 * shrink_dcache_parent - prune dcache
 * @parent: parent of entries to prune
 *
 * Prune the dcache to remove unused children of the parent dentry.
 */
 
void shrink_dcache_parent(struct dentry * parent)
{
	int found;

	while ((found = select_parent(parent)) != 0)
		prune_dcache(found);
}

/**
 * shrink_dcache_anon - further prune the cache
 * @head: head of d_hash list of dentries to prune
 *
 * Prune the dentries that are anonymous
 *
 * parsing d_hash list does not read_barrier_depends() as it
 * done under dcache_lock.
 *
 */
void shrink_dcache_anon(struct hlist_head *head)
{
	struct hlist_node *lp;
	int found;
	do {
		found = 0;
		spin_lock(&dcache_lock);
		hlist_for_each(lp, head) {
			struct dentry *this = hlist_entry(lp, struct dentry, d_hash);
			if (!list_empty(&this->d_lru)) {
				dentry_stat.nr_unused--;
				list_del(&this->d_lru);
			}

			/* 
			 * move only zero ref count dentries to the end 
			 * of the unused list for prune_dcache
			 */
			if (!atomic_read(&this->d_count)) {
				list_add_tail(&this->d_lru, &dentry_unused);
				dentry_stat.nr_unused++;
				found++;
			}
		}
		spin_unlock(&dcache_lock);
		prune_dcache(found);
	} while(found);
}

/*
 * This is called from kswapd when we think we need some more memory.
 */
static int shrink_dcache_memory(int nr, unsigned int gfp_mask)
{
	if (nr) {
		/*
		 * Nasty deadlock avoidance.
		 *
	 	 * ext2_new_block->getblk->GFP->shrink_dcache_memory->
		 * prune_dcache->prune_one_dentry->dput->dentry_iput->iput->
		 * inode->i_sb->s_op->put_inode->ext2_discard_prealloc->
		 * ext2_free_blocks->lock_super->DEADLOCK.
	 	 *
	 	 * We should make sure we don't hold the superblock lock over
	 	 * block allocations, but for now:
		 */
		if (gfp_mask & __GFP_FS)
			prune_dcache(nr);
	}
	return dentry_stat.nr_unused;
}

#define NAME_ALLOC_LEN(len)	((len+16) & ~15)

/**
 * d_alloc	-	allocate a dcache entry
 * @parent: parent of entry to allocate
 * @name: qstr of the name
 *
 * Allocates a dentry. It returns %NULL if there is insufficient memory
 * available. On a success the dentry is returned. The name passed in is
 * copied and the copy passed in may be reused after this call.
 */
 
struct dentry * d_alloc(struct dentry * parent, const struct qstr *name)
{
	char * str;
	struct dentry *dentry;
	struct qstr * qstr;

	dentry = kmem_cache_alloc(dentry_cache, GFP_KERNEL); 
	if (!dentry)
		return NULL;

	if (name->len > DNAME_INLINE_LEN-1) {
		qstr = kmalloc(sizeof(*qstr) + NAME_ALLOC_LEN(name->len), 
				GFP_KERNEL);  
		if (!qstr) {
			kmem_cache_free(dentry_cache, dentry); 
			return NULL;
		}
		qstr->name = qstr->name_str;
		qstr->len = name->len;
		qstr->hash = name->hash;
		dentry->d_qstr = qstr;
		str = qstr->name_str;
	} else  {
		dentry->d_qstr = &dentry->d_name;
		str = dentry->d_iname;
	}	

	memcpy(str, name->name, name->len);
	str[name->len] = 0;

	atomic_set(&dentry->d_count, 1);
	dentry->d_vfs_flags = DCACHE_UNHASHED;
	dentry->d_lock = SPIN_LOCK_UNLOCKED;
	dentry->d_flags = 0;
	dentry->d_inode = NULL;
	dentry->d_parent = NULL;
	dentry->d_move_count = 0;
	dentry->d_sb = NULL;
	dentry->d_name.name = str;
	dentry->d_name.len = name->len;
	dentry->d_name.hash = name->hash;
	dentry->d_op = NULL;
	dentry->d_fsdata = NULL;
	dentry->d_mounted = 0;
	dentry->d_cookie = NULL;
	dentry->d_bucket = NULL;
	INIT_HLIST_NODE(&dentry->d_hash);
	INIT_LIST_HEAD(&dentry->d_lru);
	INIT_LIST_HEAD(&dentry->d_subdirs);
	INIT_LIST_HEAD(&dentry->d_alias);

	if (parent) {
		dentry->d_parent = dget(parent);
		dentry->d_sb = parent->d_sb;
	} else {
		INIT_LIST_HEAD(&dentry->d_child);
	}

	spin_lock(&dcache_lock);
	if (parent)
		list_add(&dentry->d_child, &parent->d_subdirs);
	dentry_stat.nr_dentry++;
	spin_unlock(&dcache_lock);

	return dentry;
}

/**
 * d_instantiate - fill in inode information for a dentry
 * @entry: dentry to complete
 * @inode: inode to attach to this dentry
 *
 * Fill in inode information in the entry.
 *
 * This turns negative dentries into productive full members
 * of society.
 *
 * NOTE! This assumes that the inode count has been incremented
 * (or otherwise set) by the caller to indicate that it is now
 * in use by the dcache.
 */
 
void d_instantiate(struct dentry *entry, struct inode * inode)
{
	if (!list_empty(&entry->d_alias)) BUG();
	spin_lock(&dcache_lock);
	if (inode)
		list_add(&entry->d_alias, &inode->i_dentry);
	entry->d_inode = inode;
	spin_unlock(&dcache_lock);
	security_d_instantiate(entry, inode);
}

/**
 * d_alloc_root - allocate root dentry
 * @root_inode: inode to allocate the root for
 *
 * Allocate a root ("/") dentry for the inode given. The inode is
 * instantiated and returned. %NULL is returned if there is insufficient
 * memory or the inode passed is %NULL.
 */
 
struct dentry * d_alloc_root(struct inode * root_inode)
{
	struct dentry *res = NULL;

	if (root_inode) {
		static const struct qstr name = { .name = "/", .len = 1, .hash = 0 };
		res = d_alloc(NULL, &name);
		if (res) {
			res->d_sb = root_inode->i_sb;
			res->d_parent = res;
			d_instantiate(res, root_inode);
		}
	}
	return res;
}

static inline struct hlist_head * d_hash(struct dentry * parent, unsigned long hash)
{
	hash += (unsigned long) parent / L1_CACHE_BYTES;
	hash = hash ^ (hash >> D_HASHBITS);
	return dentry_hashtable + (hash & D_HASHMASK);
}

/**
 * d_alloc_anon - allocate an anonymous dentry
 * @inode: inode to allocate the dentry for
 *
 * This is similar to d_alloc_root.  It is used by filesystems when
 * creating a dentry for a given inode, often in the process of 
 * mapping a filehandle to a dentry.  The returned dentry may be
 * anonymous, or may have a full name (if the inode was already
 * in the cache).  The file system may need to make further
 * efforts to connect this dentry into the dcache properly.
 *
 * When called on a directory inode, we must ensure that
 * the inode only ever has one dentry.  If a dentry is
 * found, that is returned instead of allocating a new one.
 *
 * On successful return, the reference to the inode has been transferred
 * to the dentry.  If %NULL is returned (indicating kmalloc failure),
 * the reference on the inode has not been released.
 */

struct dentry * d_alloc_anon(struct inode *inode)
{
	static const struct qstr anonstring = { "", 0, 0};
	struct dentry *tmp;
	struct dentry *res;

	if ((res = d_find_alias(inode))) {
		iput(inode);
		return res;
	}

	tmp = d_alloc(NULL, &anonstring);
	if (!tmp)
		return NULL;

	tmp->d_parent = tmp; /* make sure dput doesn't croak */
	
	spin_lock(&dcache_lock);
	if (S_ISDIR(inode->i_mode) && !list_empty(&inode->i_dentry)) {
		/* A directory can only have one dentry.
		 * This (now) has one, so use it.
		 */
		res = list_entry(inode->i_dentry.next, struct dentry, d_alias);
		__dget_locked(res);
	} else {
		/* attach a disconnected dentry */
		res = tmp;
		tmp = NULL;
		if (res) {
			spin_lock(&res->d_lock);
			res->d_sb = inode->i_sb;
			res->d_parent = res;
			res->d_inode = inode;
			res->d_bucket = d_hash(res, res->d_name.hash);
			res->d_flags |= DCACHE_DISCONNECTED;
			res->d_vfs_flags &= ~DCACHE_UNHASHED;
			list_add(&res->d_alias, &inode->i_dentry);
			hlist_add_head(&res->d_hash, &inode->i_sb->s_anon);
			spin_unlock(&res->d_lock);
		}
		inode = NULL; /* don't drop reference */
	}
	spin_unlock(&dcache_lock);

	if (inode)
		iput(inode);
	if (tmp)
		dput(tmp);
	return res;
}


/**
 * d_splice_alias - splice a disconnected dentry into the tree if one exists
 * @inode:  the inode which may have a disconnected dentry
 * @dentry: a negative dentry which we want to point to the inode.
 *
 * If inode is a directory and has a 'disconnected' dentry (i.e. IS_ROOT and
 * DCACHE_DISCONNECTED), then d_move that in place of the given dentry
 * and return it, else simply d_add the inode to the dentry and return NULL.
 *
 * This is (will be) needed in the lookup routine of any filesystem that is exportable
 * (via knfsd) so that we can build dcache paths to directories effectively.
 *
 * If a dentry was found and moved, then it is returned.  Otherwise NULL
 * is returned.  This matches the expected return value of ->lookup.
 *
 */
struct dentry *d_splice_alias(struct inode *inode, struct dentry *dentry)
{
	struct dentry *new = NULL;

	if (inode && S_ISDIR(inode->i_mode)) {
		spin_lock(&dcache_lock);
		if (!list_empty(&inode->i_dentry)) {
			new = list_entry(inode->i_dentry.next, struct dentry, d_alias);
			__dget_locked(new);
			spin_unlock(&dcache_lock);
			security_d_instantiate(new, inode);
			d_rehash(dentry);
			d_move(new, dentry);
			iput(inode);
		} else {
			/* d_instantiate takes dcache_lock, so we do it by hand */
			list_add(&dentry->d_alias, &inode->i_dentry);
			dentry->d_inode = inode;
			spin_unlock(&dcache_lock);
			security_d_instantiate(dentry, inode);
			d_rehash(dentry);
		}
	} else
		d_add(dentry, inode);
	return new;
}


/**
 * d_lookup - search for a dentry
 * @parent: parent dentry
 * @name: qstr of name we wish to find
 *
 * Searches the children of the parent dentry for the name in question. If
 * the dentry is found its reference count is incremented and the dentry
 * is returned. The caller must use d_put to free the entry when it has
 * finished using it. %NULL is returned on failure.
 *
 * __d_lookup is dcache_lock free. The hash list is protected using RCU.
 * Memory barriers are used while updating and doing lockless traversal. 
 * To avoid races with d_move while rename is happening, d_move_count is 
 * used. 
 *
 * Overflows in memcmp(), while d_move, are avoided by keeping the length
 * and name pointer in one structure pointed by d_qstr.
 *
 * rcu_read_lock() and rcu_read_unlock() are used to disable preemption while
 * lookup is going on.
 *
 * d_lru list is not updated, which can leave non-zero d_count dentries
 * around in d_lru list.
 *
 * d_lookup() is protected against the concurrent renames in some unrelated
 * directory using the seqlockt_t rename_lock.
 */

struct dentry * d_lookup(struct dentry * parent, struct qstr * name)
{
	struct dentry * dentry = NULL;
	unsigned long seq;

        do {
                seq = read_seqbegin(&rename_lock);
                dentry = __d_lookup(parent, name);
                if (dentry)
			break;
	} while (read_seqretry(&rename_lock, seq));
	return dentry;
}

struct dentry * __d_lookup(struct dentry * parent, struct qstr * name)
{
	unsigned int len = name->len;
	unsigned int hash = name->hash;
	const unsigned char *str = name->name;
	struct hlist_head *head = d_hash(parent,hash);
	struct dentry *found = NULL;
	struct hlist_node *node;

	rcu_read_lock();
	
	hlist_for_each (node, head) { 
		struct dentry *dentry; 
		unsigned long move_count;
		struct qstr * qstr;

		smp_read_barrier_depends();
		dentry = hlist_entry(node, struct dentry, d_hash);

		/* if lookup ends up in a different bucket 
		 * due to concurrent rename, fail it
		 */
		if (unlikely(dentry->d_bucket != head))
			break;

		/*
		 * We must take a snapshot of d_move_count followed by
		 * read memory barrier before any search key comparison 
		 */
		move_count = dentry->d_move_count;
		smp_rmb();

		if (dentry->d_name.hash != hash)
			continue;
		if (dentry->d_parent != parent)
			continue;

		qstr = dentry->d_qstr;
		smp_read_barrier_depends();
		if (parent->d_op && parent->d_op->d_compare) {
			if (parent->d_op->d_compare(parent, qstr, name))
				continue;
		} else {
			if (qstr->len != len)
				continue;
			if (memcmp(qstr->name, str, len))
				continue;
		}
		spin_lock(&dentry->d_lock);
		/*
		 * If dentry is moved, fail the lookup
		 */ 
		if (likely(move_count == dentry->d_move_count)) {
			if (!d_unhashed(dentry)) {
				atomic_inc(&dentry->d_count);
				found = dentry;
			}
		}
		spin_unlock(&dentry->d_lock);
		break;
 	}
 	rcu_read_unlock();

 	return found;
}

/**
 * d_validate - verify dentry provided from insecure source
 * @dentry: The dentry alleged to be valid child of @dparent
 * @dparent: The parent dentry (known to be valid)
 * @hash: Hash of the dentry
 * @len: Length of the name
 *
 * An insecure source has sent us a dentry, here we verify it and dget() it.
 * This is used by ncpfs in its readdir implementation.
 * Zero is returned in the dentry is invalid.
 */
 
int d_validate(struct dentry *dentry, struct dentry *dparent)
{
	struct hlist_head *base;
	struct hlist_node *lhp;

	/* Check whether the ptr might be valid at all.. */
	if (!kmem_ptr_validate(dentry_cache, dentry))
		goto out;

	if (dentry->d_parent != dparent)
		goto out;

	spin_lock(&dcache_lock);
	base = d_hash(dparent, dentry->d_name.hash);
	hlist_for_each(lhp,base) { 
		/* read_barrier_depends() not required for d_hash list
		 * as it is parsed under dcache_lock
		 */
		if (dentry == hlist_entry(lhp, struct dentry, d_hash)) {
			__dget_locked(dentry);
			spin_unlock(&dcache_lock);
			return 1;
		}
	}
	spin_unlock(&dcache_lock);
out:
	return 0;
}

/*
 * When a file is deleted, we have two options:
 * - turn this dentry into a negative dentry
 * - unhash this dentry and free it.
 *
 * Usually, we want to just turn this into
 * a negative dentry, but if anybody else is
 * currently using the dentry or the inode
 * we can't do that and we fall back on removing
 * it from the hash queues and waiting for
 * it to be deleted later when it has no users
 */
 
/**
 * d_delete - delete a dentry
 * @dentry: The dentry to delete
 *
 * Turn the dentry into a negative dentry if possible, otherwise
 * remove it from the hash queues so it can be deleted later
 */
 
void d_delete(struct dentry * dentry)
{
	/*
	 * Are we the only user?
	 */
	spin_lock(&dcache_lock);
	spin_lock(&dentry->d_lock);
	if (atomic_read(&dentry->d_count) == 1) {
		dentry_iput(dentry);
		return;
	}

	if (!d_unhashed(dentry))
		__d_drop(dentry);

	spin_unlock(&dentry->d_lock);
	spin_unlock(&dcache_lock);
}

/**
 * d_rehash	- add an entry back to the hash
 * @entry: dentry to add to the hash
 *
 * Adds a dentry to the hash according to its name.
 */
 
void d_rehash(struct dentry * entry)
{
	struct hlist_head *list = d_hash(entry->d_parent, entry->d_name.hash);
	spin_lock(&dcache_lock);
 	entry->d_vfs_flags &= ~DCACHE_UNHASHED;
	entry->d_bucket = list;
 	hlist_add_head_rcu(&entry->d_hash, list);
	spin_unlock(&dcache_lock);
}

#define do_switch(x,y) do { \
	__typeof__ (x) __tmp = x; \
	x = y; y = __tmp; } while (0)

/*
 * When switching names, the actual string doesn't strictly have to
 * be preserved in the target - because we're dropping the target
 * anyway. As such, we can just do a simple memcpy() to copy over
 * the new name before we switch.
 *
 * Note that we have to be a lot more careful about getting the hash
 * switched - we have to switch the hash value properly even if it
 * then no longer matches the actual (corrupted) string of the target.
 * The hash value has to match the hash queue that the dentry is on..
 */
static inline void switch_names(struct dentry * dentry, struct dentry * target)
{
	const unsigned char *old_name, *new_name;
	struct qstr *old_qstr, *new_qstr;

	memcpy(dentry->d_iname, target->d_iname, DNAME_INLINE_LEN); 
	old_qstr = target->d_qstr;
	old_name = target->d_name.name;
	new_qstr = dentry->d_qstr;
	new_name = dentry->d_name.name;
	if (old_name == target->d_iname) {
		old_name = dentry->d_iname;
		old_qstr = &dentry->d_name;
	}
	if (new_name == dentry->d_iname) {
		new_name = target->d_iname;
		new_qstr = &target->d_name;
	}
	target->d_name.name = new_name;
	dentry->d_name.name = old_name;
	target->d_qstr = new_qstr;
	dentry->d_qstr = old_qstr;
}

/*
 * We cannibalize "target" when moving dentry on top of it,
 * because it's going to be thrown away anyway. We could be more
 * polite about it, though.
 *
 * This forceful removal will result in ugly /proc output if
 * somebody holds a file open that got deleted due to a rename.
 * We could be nicer about the deleted file, and let it show
 * up under the name it got deleted rather than the name that
 * deleted it.
 */
 
/**
 * d_move - move a dentry
 * @dentry: entry to move
 * @target: new dentry
 *
 * Update the dcache to reflect the move of a file name. Negative
 * dcache entries should not be moved in this way.
 */

void d_move(struct dentry * dentry, struct dentry * target)
{
	if (!dentry->d_inode)
		printk(KERN_WARNING "VFS: moving negative dcache entry\n");

	spin_lock(&dcache_lock);
	write_seqlock(&rename_lock);
	/*
	 * XXXX: do we really need to take target->d_lock?
	 */
	if (target < dentry) {
		spin_lock(&target->d_lock);
		spin_lock(&dentry->d_lock);
	} else {
		spin_lock(&dentry->d_lock);
		spin_lock(&target->d_lock);
	}

	/* Move the dentry to the target hash queue, if on different bucket */
	if (dentry->d_vfs_flags & DCACHE_UNHASHED)
		goto already_unhashed;
	if (dentry->d_bucket != target->d_bucket) {
		hlist_del_rcu(&dentry->d_hash);
already_unhashed:
		dentry->d_bucket = target->d_bucket;
		hlist_add_head_rcu(&dentry->d_hash, target->d_bucket);
		dentry->d_vfs_flags &= ~DCACHE_UNHASHED;
	}

	/* Unhash the target: dput() will then get rid of it */
	__d_drop(target);

	list_del(&dentry->d_child);
	list_del(&target->d_child);

	/* Switch the names.. */
	switch_names(dentry, target);
	smp_wmb();
	do_switch(dentry->d_name.len, target->d_name.len);
	do_switch(dentry->d_name.hash, target->d_name.hash);

	/* ... and switch the parents */
	if (IS_ROOT(dentry)) {
		dentry->d_parent = target->d_parent;
		target->d_parent = target;
		INIT_LIST_HEAD(&target->d_child);
	} else {
		do_switch(dentry->d_parent, target->d_parent);

		/* And add them back to the (new) parent lists */
		list_add(&target->d_child, &target->d_parent->d_subdirs);
	}

	list_add(&dentry->d_child, &dentry->d_parent->d_subdirs);
	dentry->d_move_count++;
	spin_unlock(&target->d_lock);
	spin_unlock(&dentry->d_lock);
	write_sequnlock(&rename_lock);
	spin_unlock(&dcache_lock);
}

/**
 * d_path - return the path of a dentry
 * @dentry: dentry to report
 * @vfsmnt: vfsmnt to which the dentry belongs
 * @root: root dentry
 * @rootmnt: vfsmnt to which the root dentry belongs
 * @buffer: buffer to return value in
 * @buflen: buffer length
 *
 * Convert a dentry into an ASCII path name. If the entry has been deleted
 * the string " (deleted)" is appended. Note that this is ambiguous.
 *
 * Returns the buffer or an error code if the path was too long.
 *
 * "buflen" should be positive. Caller holds the dcache_lock.
 */
static char * __d_path( struct dentry *dentry, struct vfsmount *vfsmnt,
			struct dentry *root, struct vfsmount *rootmnt,
			char *buffer, int buflen)
{
	char * end = buffer+buflen;
	char * retval;
	int namelen;

	*--end = '\0';
	buflen--;
	if (!IS_ROOT(dentry) && d_unhashed(dentry)) {
		buflen -= 10;
		end -= 10;
		if (buflen < 0)
			goto Elong;
		memcpy(end, " (deleted)", 10);
	}

	if (buflen < 1)
		goto Elong;
	/* Get '/' right */
	retval = end-1;
	*retval = '/';

	for (;;) {
		struct dentry * parent;

		if (dentry == root && vfsmnt == rootmnt)
			break;
		if (dentry == vfsmnt->mnt_root || IS_ROOT(dentry)) {
			/* Global root? */
			spin_lock(&vfsmount_lock);
			if (vfsmnt->mnt_parent == vfsmnt) {
				spin_unlock(&vfsmount_lock);
				goto global_root;
			}
			dentry = vfsmnt->mnt_mountpoint;
			vfsmnt = vfsmnt->mnt_parent;
			spin_unlock(&vfsmount_lock);
			continue;
		}
		parent = dentry->d_parent;
		prefetch(parent);
		namelen = dentry->d_name.len;
		buflen -= namelen + 1;
		if (buflen < 0)
			goto Elong;
		end -= namelen;
		memcpy(end, dentry->d_name.name, namelen);
		*--end = '/';
		retval = end;
		dentry = parent;
	}

	return retval;

global_root:
	namelen = dentry->d_name.len;
	buflen -= namelen;
	if (buflen < 0)
		goto Elong;
	retval -= namelen-1;	/* hit the slash */
	memcpy(retval, dentry->d_name.name, namelen);
	return retval;
Elong:
	return ERR_PTR(-ENAMETOOLONG);
}

/* write full pathname into buffer and return start of pathname */
char * d_path(struct dentry *dentry, struct vfsmount *vfsmnt,
				char *buf, int buflen)
{
	char *res;
	struct vfsmount *rootmnt;
	struct dentry *root;
	read_lock(&current->fs->lock);
	rootmnt = mntget(current->fs->rootmnt);
	root = dget(current->fs->root);
	read_unlock(&current->fs->lock);
	spin_lock(&dcache_lock);
	res = __d_path(dentry, vfsmnt, root, rootmnt, buf, buflen);
	spin_unlock(&dcache_lock);
	dput(root);
	mntput(rootmnt);
	return res;
}

/*
 * NOTE! The user-level library version returns a
 * character pointer. The kernel system call just
 * returns the length of the buffer filled (which
 * includes the ending '\0' character), or a negative
 * error value. So libc would do something like
 *
 *	char *getcwd(char * buf, size_t size)
 *	{
 *		int retval;
 *
 *		retval = sys_getcwd(buf, size);
 *		if (retval >= 0)
 *			return buf;
 *		errno = -retval;
 *		return NULL;
 *	}
 */
asmlinkage long sys_getcwd(char __user *buf, unsigned long size)
{
	int error;
	struct vfsmount *pwdmnt, *rootmnt;
	struct dentry *pwd, *root;
	char *page = (char *) __get_free_page(GFP_USER);

	if (!page)
		return -ENOMEM;

	read_lock(&current->fs->lock);
	pwdmnt = mntget(current->fs->pwdmnt);
	pwd = dget(current->fs->pwd);
	rootmnt = mntget(current->fs->rootmnt);
	root = dget(current->fs->root);
	read_unlock(&current->fs->lock);

	error = -ENOENT;
	/* Has the current directory has been unlinked? */
	spin_lock(&dcache_lock);
	if (pwd->d_parent == pwd || !d_unhashed(pwd)) {
		unsigned long len;
		char * cwd;

		cwd = __d_path(pwd, pwdmnt, root, rootmnt, page, PAGE_SIZE);
		spin_unlock(&dcache_lock);

		error = PTR_ERR(cwd);
		if (IS_ERR(cwd))
			goto out;

		error = -ERANGE;
		len = PAGE_SIZE + page - cwd;
		if (len <= size) {
			error = len;
			if (copy_to_user(buf, cwd, len))
				error = -EFAULT;
		}
	} else
		spin_unlock(&dcache_lock);

out:
	dput(pwd);
	mntput(pwdmnt);
	dput(root);
	mntput(rootmnt);
	free_page((unsigned long) page);
	return error;
}

/*
 * Test whether new_dentry is a subdirectory of old_dentry.
 *
 * Trivially implemented using the dcache structure
 */

/**
 * is_subdir - is new dentry a subdirectory of old_dentry
 * @new_dentry: new dentry
 * @old_dentry: old dentry
 *
 * Returns 1 if new_dentry is a subdirectory of the parent (at any depth).
 * Returns 0 otherwise.
 * Caller must ensure that "new_dentry" is pinned before calling is_subdir()
 */
  
int is_subdir(struct dentry * new_dentry, struct dentry * old_dentry)
{
	int result;
	struct dentry * saved = new_dentry;
	unsigned long seq;

	result = 0;
	/* need rcu_readlock to protect against the d_parent trashing due to
	 * d_move
	 */
	rcu_read_lock();
        do {
		/* for restarting inner loop in case of seq retry */
		new_dentry = saved;
		seq = read_seqbegin(&rename_lock);
		for (;;) {
			if (new_dentry != old_dentry) {
				struct dentry * parent = new_dentry->d_parent;
				if (parent == new_dentry)
					break;
				new_dentry = parent;
				continue;
			}
			result = 1;
			break;
		}
	} while (read_seqretry(&rename_lock, seq));
	rcu_read_unlock();

	return result;
}

void d_genocide(struct dentry *root)
{
	struct dentry *this_parent = root;
	struct list_head *next;

	spin_lock(&dcache_lock);
repeat:
	next = this_parent->d_subdirs.next;
resume:
	while (next != &this_parent->d_subdirs) {
		struct list_head *tmp = next;
		struct dentry *dentry = list_entry(tmp, struct dentry, d_child);
		next = tmp->next;
		if (d_unhashed(dentry)||!dentry->d_inode)
			continue;
		if (!list_empty(&dentry->d_subdirs)) {
			this_parent = dentry;
			goto repeat;
		}
		atomic_dec(&dentry->d_count);
	}
	if (this_parent != root) {
		next = this_parent->d_child.next; 
		atomic_dec(&this_parent->d_count);
		this_parent = this_parent->d_parent;
		goto resume;
	}
	spin_unlock(&dcache_lock);
}

/**
 * find_inode_number - check for dentry with name
 * @dir: directory to check
 * @name: Name to find.
 *
 * Check whether a dentry already exists for the given name,
 * and return the inode number if it has an inode. Otherwise
 * 0 is returned.
 *
 * This routine is used to post-process directory listings for
 * filesystems using synthetic inode numbers, and is necessary
 * to keep getcwd() working.
 */
 
ino_t find_inode_number(struct dentry *dir, struct qstr *name)
{
	struct dentry * dentry;
	ino_t ino = 0;

	/*
	 * Check for a fs-specific hash function. Note that we must
	 * calculate the standard hash first, as the d_op->d_hash()
	 * routine may choose to leave the hash value unchanged.
	 */
	name->hash = full_name_hash(name->name, name->len);
	if (dir->d_op && dir->d_op->d_hash)
	{
		if (dir->d_op->d_hash(dir, name) != 0)
			goto out;
	}

	dentry = d_lookup(dir, name);
	if (dentry)
	{
		if (dentry->d_inode)
			ino = dentry->d_inode->i_ino;
		dput(dentry);
	}
out:
	return ino;
}

static __initdata unsigned long dhash_entries;
static int __init set_dhash_entries(char *str)
{
	if (!str)
		return 0;
	dhash_entries = simple_strtoul(str, &str, 0);
	return 1;
}
__setup("dhash_entries=", set_dhash_entries);

static void __init dcache_init(unsigned long mempages)
{
	struct hlist_head *d;
	unsigned long order;
	unsigned int nr_hash;
	int i;

	/* 
	 * A constructor could be added for stable state like the lists,
	 * but it is probably not worth it because of the cache nature
	 * of the dcache. 
	 * If fragmentation is too bad then the SLAB_HWCACHE_ALIGN
	 * flag could be removed here, to hint to the allocator that
	 * it should not try to get multiple page regions.  
	 */
	dentry_cache = kmem_cache_create("dentry_cache",
					 sizeof(struct dentry),
					 0,
					 SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT,
					 NULL, NULL);
	if (!dentry_cache)
		panic("Cannot create dentry cache");
	
	set_shrinker(DEFAULT_SEEKS, shrink_dcache_memory);

	if (!dhash_entries)
		dhash_entries = PAGE_SHIFT < 13 ?
				mempages >> (13 - PAGE_SHIFT) :
				mempages << (PAGE_SHIFT - 13);

	dhash_entries *= sizeof(struct hlist_head);
	for (order = 0; ((1UL << order) << PAGE_SHIFT) < dhash_entries; order++)
		;

	do {
		unsigned long tmp;

		nr_hash = (1UL << order) * PAGE_SIZE /
			sizeof(struct hlist_head);
		d_hash_mask = (nr_hash - 1);

		tmp = nr_hash;
		d_hash_shift = 0;
		while ((tmp >>= 1UL) != 0UL)
			d_hash_shift++;

		dentry_hashtable = (struct hlist_head *)
			__get_free_pages(GFP_ATOMIC, order);
	} while (dentry_hashtable == NULL && --order >= 0);

	printk(KERN_INFO "Dentry cache hash table entries: %d (order: %ld, %ld bytes)\n",
			nr_hash, order, (PAGE_SIZE << order));

	if (!dentry_hashtable)
		panic("Failed to allocate dcache hash table\n");

	d = dentry_hashtable;
	i = nr_hash;
	do {
		INIT_HLIST_HEAD(d);
		d++;
		i--;
	} while (i);
}

/* SLAB cache for __getname() consumers */
kmem_cache_t *names_cachep;

/* SLAB cache for file structures */
kmem_cache_t *filp_cachep;

EXPORT_SYMBOL(d_genocide);

extern void bdev_cache_init(void);
extern void chrdev_init(void);

void __init vfs_caches_init(unsigned long mempages)
{
	unsigned long reserve;

	/* Base hash sizes on available memory, with a reserve equal to
           150% of current kernel size */

	reserve = (mempages - nr_free_pages()) * 3/2;
	mempages -= reserve;

	names_cachep = kmem_cache_create("names_cache",
			PATH_MAX, 0,
			SLAB_HWCACHE_ALIGN, NULL, NULL);
	if (!names_cachep)
		panic("Cannot create names SLAB cache");

	filp_cachep = kmem_cache_create("filp",
			sizeof(struct file), 0,
			SLAB_HWCACHE_ALIGN, filp_ctor, filp_dtor);
	if(!filp_cachep)
		panic("Cannot create filp SLAB cache");

	dcache_init(mempages);
	inode_init(mempages);
	files_init(mempages);
	mnt_init(mempages);
	bdev_cache_init();
	chrdev_init();
}

EXPORT_SYMBOL(d_alloc);
EXPORT_SYMBOL(d_alloc_anon);
EXPORT_SYMBOL(d_alloc_root);
EXPORT_SYMBOL(d_delete);
EXPORT_SYMBOL(d_find_alias);
EXPORT_SYMBOL(d_instantiate);
EXPORT_SYMBOL(d_invalidate);
EXPORT_SYMBOL(d_lookup);
EXPORT_SYMBOL(d_move);
EXPORT_SYMBOL(d_path);
EXPORT_SYMBOL(d_prune_aliases);
EXPORT_SYMBOL(d_rehash);
EXPORT_SYMBOL(d_splice_alias);
EXPORT_SYMBOL(d_validate);
EXPORT_SYMBOL(dget_locked);
EXPORT_SYMBOL(dput);
EXPORT_SYMBOL(find_inode_number);
EXPORT_SYMBOL(have_submounts);
EXPORT_SYMBOL(is_subdir);
EXPORT_SYMBOL(names_cachep);
EXPORT_SYMBOL(shrink_dcache_anon);
EXPORT_SYMBOL(shrink_dcache_parent);
EXPORT_SYMBOL(shrink_dcache_sb);
