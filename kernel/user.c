/*
 * The "user cache".
 *
 * (C) Copyright 1991-2000 Linus Torvalds
 *
 * We have a per-user structure to keep track of how many
 * processes, files etc the user has claimed, in order to be
 * able to have per-user limits for system resources. 
 */

#include <linux/init.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/bitops.h>

/*
 * UID task count cache, to get fast user lookup in "alloc_uid"
 * when changing user ID's (ie setuid() and friends).
 */
#define UIDHASH_BITS		8
#define UIDHASH_SZ		(1 << UIDHASH_BITS)
#define UIDHASH_MASK		(UIDHASH_SZ - 1)
#define __uidhashfn(uid)	(((uid >> UIDHASH_BITS) ^ uid) & UIDHASH_MASK)
#define uidhashentry(uid)	(uidhash_table + __uidhashfn(uid))

static kmem_cache_t *uid_cachep;
static struct list_head *uidhash_table;
static unsigned uidhash_size;
static unsigned uidhash_bits;
static spinlock_t uidhash_lock = SPIN_LOCK_UNLOCKED;

struct user_struct root_user = {
	__count:	ATOMIC_INIT(1),
	processes:	ATOMIC_INIT(1),
	files:		ATOMIC_INIT(0)
};

/*
 * These routines must be called with the uidhash spinlock held!
 */
static inline void uid_hash_insert(struct user_struct *up, struct list_head *hashent)
{
	list_add(&up->uidhash_list, hashent);
}

static inline void uid_hash_remove(struct user_struct *up)
{
	list_del(&up->uidhash_list);
}

static inline struct user_struct *uid_hash_find(uid_t uid, struct list_head *hashent)
{
	struct list_head *up;

	list_for_each(up, hashent) {
		struct user_struct *user;

		user = list_entry(up, struct user_struct, uidhash_list);

		if(user->uid == uid) {
			atomic_inc(&user->__count);
			return user;
		}
	}

	return NULL;
}

void free_uid(struct user_struct *up)
{
	if (up && atomic_dec_and_lock(&up->__count, &uidhash_lock)) {
		uid_hash_remove(up);
		kmem_cache_free(uid_cachep, up);
		spin_unlock(&uidhash_lock);
	}
}

struct user_struct * alloc_uid(uid_t uid)
{
	struct list_head *hashent = uidhashentry(uid);
	struct user_struct *up;

	spin_lock(&uidhash_lock);
	up = uid_hash_find(uid, hashent);
	spin_unlock(&uidhash_lock);

	if (!up) {
		struct user_struct *new;

		new = kmem_cache_alloc(uid_cachep, SLAB_KERNEL);
		if (!new)
			return NULL;
		new->uid = uid;
		atomic_set(&new->__count, 1);
		atomic_set(&new->processes, 0);
		atomic_set(&new->files, 0);

		/*
		 * Before adding this, check whether we raced
		 * on adding the same user already..
		 */
		spin_lock(&uidhash_lock);
		up = uid_hash_find(uid, hashent);
		if (up) {
			kmem_cache_free(uid_cachep, new);
		} else {
			uid_hash_insert(new, hashent);
			up = new;
		}
		spin_unlock(&uidhash_lock);

	}
	return up;
}


static int __init uid_cache_init(void)
{
	int size, n;

	uid_cachep = kmem_cache_create("uid_cache", sizeof(struct user_struct),
				       0,
				       SLAB_HWCACHE_ALIGN, NULL, NULL);
	if(!uid_cachep)
		panic("Cannot create uid taskcount SLAB cache\n");

	size = UIDHASH_SZ * sizeof(struct list_head);
	do {
		uidhash_table = (struct list_head *)
					kmalloc(size, GFP_ATOMIC);
		if(!uidhash_table)
			size >>= 1;
	} while(!uidhash_table && size >= sizeof(struct list_head));

	if(!uidhash_table)
		panic("Failed to allocate uid hash table!\n");

	uidhash_size = size/sizeof(struct list_head);
	uidhash_bits = ffz(uidhash_size);

	for(n = 0; n < uidhash_size; ++n)
		INIT_LIST_HEAD(uidhash_table + n);

	/* Insert the root user immediately - init already runs with this */
	uid_hash_insert(&root_user, uidhashentry(0));
	return 0;
}

module_init(uid_cache_init);
