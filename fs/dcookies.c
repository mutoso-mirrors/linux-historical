/*
 * dcookies.c
 *
 * Copyright 2002 John Levon <levon@movementarian.org>
 *
 * Persistent cookie-path mappings. These are used by
 * profilers to convert a per-task EIP value into something
 * non-transitory that can be processed at a later date.
 * This is done by locking the dentry/vfsmnt pair in the
 * kernel until released by the tasks needing the persistent
 * objects. The tag is simply an unsigned long that refers
 * to the pair and can be looked up from userspace.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/mount.h>
#include <linux/dcache.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/dcookies.h>
#include <asm/uaccess.h>

/* The dcookies are allocated from a kmem_cache and
 * hashed onto a small number of lists. None of the
 * code here is particularly performance critical
 */
struct dcookie_struct {
	struct dentry * dentry;
	struct vfsmount * vfsmnt;
	struct list_head hash_list;
};

static LIST_HEAD(dcookie_users);
static DECLARE_MUTEX(dcookie_sem);
static kmem_cache_t * dcookie_cache;
static struct list_head * dcookie_hashtable;
static size_t hash_size;

static inline int is_live(void)
{
	return !(list_empty(&dcookie_users));
}


/* The dentry is locked, its address will do for the cookie */
static inline unsigned long dcookie_value(struct dcookie_struct * dcs)
{
	return (unsigned long)dcs->dentry;
}


static size_t dcookie_hash(unsigned long dcookie)
{
	return (dcookie >> 2) & (hash_size - 1);
}


static struct dcookie_struct * find_dcookie(unsigned long dcookie)
{
	struct dcookie_struct * found = 0;
	struct dcookie_struct * dcs;
	struct list_head * pos;
	struct list_head * list;

	list = dcookie_hashtable + dcookie_hash(dcookie);

	list_for_each(pos, list) {
		dcs = list_entry(pos, struct dcookie_struct, hash_list);
		if (dcookie_value(dcs) == dcookie) {
			found = dcs;
			break;
		}
	}

	return found;
}


static void hash_dcookie(struct dcookie_struct * dcs)
{
	struct list_head * list = dcookie_hashtable + dcookie_hash(dcookie_value(dcs));
	list_add(&dcs->hash_list, list);
}


static struct dcookie_struct * alloc_dcookie(struct dentry * dentry,
	struct vfsmount * vfsmnt)
{
	struct dcookie_struct * dcs = kmem_cache_alloc(dcookie_cache, GFP_KERNEL);
	if (!dcs)
		return NULL;

	atomic_inc(&dentry->d_count);
	atomic_inc(&vfsmnt->mnt_count);
	dentry->d_cookie = dcs;

	dcs->dentry = dentry;
	dcs->vfsmnt = vfsmnt;
	hash_dcookie(dcs);

	return dcs;
}


/* This is the main kernel-side routine that retrieves the cookie
 * value for a dentry/vfsmnt pair.
 */
int get_dcookie(struct dentry * dentry, struct vfsmount * vfsmnt,
	unsigned long * cookie)
{
	int err = 0;
	struct dcookie_struct * dcs;

	down(&dcookie_sem);

	if (!is_live()) {
		err = -EINVAL;
		goto out;
	}

	dcs = dentry->d_cookie;

	if (!dcs)
		dcs = alloc_dcookie(dentry, vfsmnt);

	if (!dcs) {
		err = -ENOMEM;
		goto out;
	}

	*cookie = dcookie_value(dcs);

out:
	up(&dcookie_sem);
	return err;
}


/* And here is where the userspace process can look up the cookie value
 * to retrieve the path.
 */
asmlinkage int sys_lookup_dcookie(unsigned long cookie, char * buf, size_t len)
{
	char * kbuf;
	char * path;
	int err = -EINVAL;
	size_t pathlen;
	struct dcookie_struct * dcs;

	/* we could leak path information to users
	 * without dir read permission without this
	 */
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	down(&dcookie_sem);

	if (!is_live()) {
		err = -EINVAL;
		goto out;
	}

	if (!(dcs = find_dcookie(cookie)))
		goto out;

	err = -ENOMEM;
	kbuf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!kbuf)
		goto out;
	memset(kbuf, 0, PAGE_SIZE);

	/* FIXME: (deleted) ? */
	path = d_path(dcs->dentry, dcs->vfsmnt, kbuf, PAGE_SIZE);

	err = 0;

	pathlen = kbuf + PAGE_SIZE - path;
	if (len > pathlen)
		len = pathlen;

	if (copy_to_user(buf, path, len))
		err = -EFAULT;

	kfree(kbuf);
out:
	up(&dcookie_sem);
	return err;
}


static int dcookie_init(void)
{
	struct list_head * d;
	unsigned int i, hash_bits;
	int err = -ENOMEM;

	dcookie_cache = kmem_cache_create("dcookie_cache",
		sizeof(struct dcookie_struct),
		0, 0, NULL, NULL);

	if (!dcookie_cache)
		goto out;

	dcookie_hashtable = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!dcookie_hashtable)
		goto out_kmem;

	err = 0;

	/*
	 * Find the power-of-two list-heads that can fit into the allocation..
	 * We don't guarantee that "sizeof(struct list_head)" is necessarily
	 * a power-of-two.
	 */
	hash_size = PAGE_SIZE / sizeof(struct list_head);
	hash_bits = 0;
	do {
		hash_bits++;
	} while ((hash_size >> hash_bits) != 0);
	hash_bits--;

	/*
	 * Re-calculate the actual number of entries and the mask
	 * from the number of bits we can fit.
	 */
	hash_size = 1UL << hash_bits;

	/* And initialize the newly allocated array */
	d = dcookie_hashtable;
	i = hash_size;
	do {
		INIT_LIST_HEAD(d);
		d++;
		i--;
	} while (i);

out:
	return err;
out_kmem:
	kmem_cache_destroy(dcookie_cache);
	goto out;
}


static void free_dcookie(struct dcookie_struct * dcs)
{
	dcs->dentry->d_cookie = NULL;
	dput(dcs->dentry);
	mntput(dcs->vfsmnt);
	kmem_cache_free(dcookie_cache, dcs);
}


static void dcookie_exit(void)
{
	struct list_head * list;
	struct list_head * pos;
	struct list_head * pos2;
	struct dcookie_struct * dcs;
	size_t i;

	for (i = 0; i < hash_size; ++i) {
		list = dcookie_hashtable + i;
		list_for_each_safe(pos, pos2, list) {
			dcs = list_entry(pos, struct dcookie_struct, hash_list);
			list_del(&dcs->hash_list);
			free_dcookie(dcs);
		}
	}

	kfree(dcookie_hashtable);
	kmem_cache_destroy(dcookie_cache);
}


struct dcookie_user {
	struct list_head next;
};
 
struct dcookie_user * dcookie_register(void)
{
	struct dcookie_user * user;

	down(&dcookie_sem);

	user = kmalloc(sizeof(struct dcookie_user), GFP_KERNEL);
	if (!user)
		goto out;

	if (!is_live() && dcookie_init())
		goto out_free;

	list_add(&user->next, &dcookie_users);

out:
	up(&dcookie_sem);
	return user;
out_free:
	kfree(user);
	user = NULL;
	goto out;
}


void dcookie_unregister(struct dcookie_user * user)
{
	down(&dcookie_sem);

	list_del(&user->next);
	kfree(user);

	if (!is_live())
		dcookie_exit();

	up(&dcookie_sem);
}

EXPORT_SYMBOL_GPL(dcookie_register);
EXPORT_SYMBOL_GPL(dcookie_unregister);
EXPORT_SYMBOL_GPL(get_dcookie);
