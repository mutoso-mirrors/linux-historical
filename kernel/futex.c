/*
 *  Fast Userspace Mutexes (which I call "Futexes!").
 *  (C) Rusty Russell, IBM 2002
 *
 *  Generalized futexes, futex requeueing, misc fixes by Ingo Molnar
 *  (C) Copyright 2003 Red Hat Inc, All Rights Reserved
 *
 *  Removed page pinning, fix privately mapped COW pages and other cleanups
 *  (C) Copyright 2003 Jamie Lokier
 *
 *  Thanks to Ben LaHaise for yelling "hashed waitqueues" loudly
 *  enough at me, Linus for the original (flawed) idea, Matthew
 *  Kirkwood for proof-of-concept implementation.
 *
 *  "The futexes are also cursed."
 *  "But they come in a choice of three flavours!"
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/hash.h>
#include <linux/init.h>
#include <linux/futex.h>
#include <linux/mount.h>
#include <linux/pagemap.h>

#define FUTEX_HASHBITS 8

/*
 * Futexes are matched on equal values of this key.
 * The key type depends on whether it's a shared or private mapping.
 */
union futex_key {
	struct {
		unsigned long pgoff;
		struct inode *inode;
		int offset;
	} shared;
	struct {
		unsigned long uaddr;
		struct mm_struct *mm;
		int offset;
	} private;
	struct {
		unsigned long word;
		void *ptr;
		int offset;
	} both;
};

/*
 * We use this hashed waitqueue instead of a normal wait_queue_t, so
 * we can wake only the relevant ones (hashed queues may be shared):
 */
struct futex_q {
	struct list_head list;
	wait_queue_head_t waiters;

	/* Key which the futex is hashed on. */
	union futex_key key;

	/* For fd, sigio sent using these. */
	int fd;
	struct file *filp;
};

/* The key for the hash is the address + index + offset within page */
static struct list_head futex_queues[1<<FUTEX_HASHBITS];
static spinlock_t futex_lock = SPIN_LOCK_UNLOCKED;

/* Futex-fs vfsmount entry: */
static struct vfsmount *futex_mnt;

/*
 * We hash on the keys returned from get_futex_key (see below).
 */
static inline struct list_head *hash_futex(union futex_key *key)
{
	return &futex_queues[hash_long(key->both.word
				       + (unsigned long) key->both.ptr
				       + key->both.offset, FUTEX_HASHBITS)];
}

/*
 * Return 1 if two futex_keys are equal, 0 otherwise.
 */
static inline int match_futex(union futex_key *key1, union futex_key *key2)
{
	return (key1->both.word == key2->both.word
		&& key1->both.ptr == key2->both.ptr
		&& key1->both.offset == key2->both.offset);
}

/*
 * Get parameters which are the keys for a futex.
 *
 * For shared mappings, it's (page->index, vma->vm_file->f_dentry->d_inode,
 * offset_within_page).  For private mappings, it's (uaddr, current->mm).
 * We can usually work out the index without swapping in the page.
 *
 * Returns: 0, or negative error code.
 * The key words are stored in *key on success.
 *
 * Should be called with &current->mm->mmap_sem,
 * but NOT &futex_lock or &current->mm->page_table_lock.
 */
static int get_futex_key(unsigned long uaddr, union futex_key *key)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	struct page *page;
	int err;

	/*
	 * The futex address must be "naturally" aligned.
	 */
	key->both.offset = uaddr % PAGE_SIZE;
	if (unlikely((key->both.offset % sizeof(u32)) != 0))
		return -EINVAL;
	uaddr -= key->both.offset;

	/*
	 * The futex is hashed differently depending on whether
	 * it's in a shared or private mapping.  So check vma first.
	 */
	vma = find_extend_vma(mm, uaddr);
	if (unlikely(!vma))
		return -EFAULT;

	/*
	 * Permissions.
	 */
	if (unlikely((vma->vm_flags & (VM_IO|VM_READ)) != VM_READ))
		return (vma->vm_flags & VM_IO) ? -EPERM : -EACCES;

	/*
	 * Private mappings are handled in a simple way.
	 *
	 * NOTE: When userspace waits on a MAP_SHARED mapping, even if
	 * it's a read-only handle, it's expected that futexes attach to
	 * the object not the particular process.  Therefore we use
	 * VM_MAYSHARE here, not VM_SHARED which is restricted to shared
	 * mappings of _writable_ handles.
	 */
	if (likely(!(vma->vm_flags & VM_MAYSHARE))) {
		key->private.mm = mm;
		key->private.uaddr = uaddr;
		return 0;
	}

	/*
	 * Linear mappings are also simple.
	 */
	key->shared.inode = vma->vm_file->f_dentry->d_inode;
	if (likely(!(vma->vm_flags & VM_NONLINEAR))) {
		key->shared.pgoff = (((uaddr - vma->vm_start) >> PAGE_SHIFT)
				     + vma->vm_pgoff);
		return 0;
	}

	/*
	 * We could walk the page table to read the non-linear
	 * pte, and get the page index without fetching the page
	 * from swap.  But that's a lot of code to duplicate here
	 * for a rare case, so we simply fetch the page.
	 */

	/*
	 * Do a quick atomic lookup first - this is the fastpath.
	 */
	spin_lock(&current->mm->page_table_lock);
	page = follow_page(mm, uaddr, 0);
	if (likely(page != NULL)) {
		key->shared.pgoff =
			page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
		spin_unlock(&current->mm->page_table_lock);
		return 0;
	}
	spin_unlock(&current->mm->page_table_lock);

	/*
	 * Do it the general way.
	 */
	err = get_user_pages(current, mm, uaddr, 1, 0, 0, &page, NULL);
	if (err >= 0) {
		key->shared.pgoff =
			page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);
		put_page(page);
		return 0;
	}
	return err;
}


/*
 * Wake up all waiters hashed on the physical page that is mapped
 * to this virtual address:
 */
static int futex_wake(unsigned long uaddr, int num)
{
	struct list_head *i, *next, *head;
	union futex_key key;
	int ret;

	down_read(&current->mm->mmap_sem);

	ret = get_futex_key(uaddr, &key);
	if (unlikely(ret != 0))
		goto out;

	head = hash_futex(&key);

	spin_lock(&futex_lock);
	list_for_each_safe(i, next, head) {
		struct futex_q *this = list_entry(i, struct futex_q, list);

		if (match_futex (&this->key, &key)) {
			list_del_init(i);
			wake_up_all(&this->waiters);
			if (this->filp)
				send_sigio(&this->filp->f_owner, this->fd, POLL_IN);
			ret++;
			if (ret >= num)
				break;
		}
	}
	spin_unlock(&futex_lock);

out:
	up_read(&current->mm->mmap_sem);
	return ret;
}

/*
 * Requeue all waiters hashed on one physical page to another
 * physical page.
 */
static int futex_requeue(unsigned long uaddr1, unsigned long uaddr2,
				int nr_wake, int nr_requeue)
{
	struct list_head *i, *next, *head1, *head2;
	union futex_key key1, key2;
	int ret;

	down_read(&current->mm->mmap_sem);

	ret = get_futex_key(uaddr1, &key1);
	if (unlikely(ret != 0))
		goto out;
	ret = get_futex_key(uaddr2, &key2);
	if (unlikely(ret != 0))
		goto out;

	head1 = hash_futex(&key1);
	head2 = hash_futex(&key2);

	spin_lock(&futex_lock);
	list_for_each_safe(i, next, head1) {
		struct futex_q *this = list_entry(i, struct futex_q, list);

		if (match_futex (&this->key, &key1)) {
			list_del_init(i);
			if (++ret <= nr_wake) {
				wake_up_all(&this->waiters);
				if (this->filp)
					send_sigio(&this->filp->f_owner,
							this->fd, POLL_IN);
			} else {
				list_add_tail(i, head2);
				this->key = key2;
				if (ret - nr_wake >= nr_requeue)
					break;
				/* Make sure to stop if key1 == key2 */
				if (head1 == head2 && head1 != next)
					head1 = i;
			}
		}
	}
	spin_unlock(&futex_lock);

out:
	up_read(&current->mm->mmap_sem);
	return ret;
}

static inline void queue_me(struct futex_q *q, union futex_key *key,
			    int fd, struct file *filp)
{
	struct list_head *head = hash_futex(key);

	q->key = *key;
	q->fd = fd;
	q->filp = filp;

	spin_lock(&futex_lock);
	list_add_tail(&q->list, head);
	spin_unlock(&futex_lock);
}

/* Return 1 if we were still queued (ie. 0 means we were woken) */
static inline int unqueue_me(struct futex_q *q)
{
	int ret = 0;

	spin_lock(&futex_lock);
	if (!list_empty(&q->list)) {
		list_del(&q->list);
		ret = 1;
	}
	spin_unlock(&futex_lock);
	return ret;
}

static int futex_wait(unsigned long uaddr, int val, unsigned long time)
{
	DECLARE_WAITQUEUE(wait, current);
	int ret, curval;
	union futex_key key;
	struct futex_q q;

 try_again:
	init_waitqueue_head(&q.waiters);

	down_read(&current->mm->mmap_sem);

	ret = get_futex_key(uaddr, &key);
	if (unlikely(ret != 0))
		goto out_release_sem;

	queue_me(&q, &key, -1, NULL);

	/*
	 * Access the page after the futex is queued.
	 * We hold the mmap semaphore, so the mapping cannot have changed
	 * since we looked it up.
	 */
	if (get_user(curval, (int *)uaddr) != 0) {
		ret = -EFAULT;
		goto out_unqueue;
	}
	if (curval != val) {
		ret = -EWOULDBLOCK;
		goto out_unqueue;
	}

	/*
	 * Now the futex is queued and we have checked the data, we
	 * don't want to hold mmap_sem while we sleep.
	 */	
	up_read(&current->mm->mmap_sem);

	/*
	 * There might have been scheduling since the queue_me(), as we
	 * cannot hold a spinlock across the get_user() in case it
	 * faults.  So we cannot just set TASK_INTERRUPTIBLE state when
	 * queueing ourselves into the futex hash.  This code thus has to
	 * rely on the futex_wake() code doing a wakeup after removing
	 * the waiter from the list.
	 */
	add_wait_queue(&q.waiters, &wait);
	spin_lock(&futex_lock);
	set_current_state(TASK_INTERRUPTIBLE);

	if (unlikely(list_empty(&q.list))) {
		/*
		 * We were woken already.
		 */
		spin_unlock(&futex_lock);
		set_current_state(TASK_RUNNING);
		return 0;
	}

	spin_unlock(&futex_lock);
	time = schedule_timeout(time);
	set_current_state(TASK_RUNNING);

	/*
	 * NOTE: we don't remove ourselves from the waitqueue because
	 * we are the only user of it.
	 */

	/*
	 * Were we woken or interrupted for a valid reason?
	 */
	ret = unqueue_me(&q);
	if (ret == 0)
		return 0;
	if (time == 0)
		return -ETIMEDOUT;
	if (signal_pending(current))
		return -EINTR;

	/*
	 * No, it was a spurious wakeup.  Try again.  Should never happen. :)
	 */
	goto try_again;

 out_unqueue:
	/*
	 * Were we unqueued anyway?
	 */
	if (!unqueue_me(&q))
		ret = 0;
 out_release_sem:
	up_read(&current->mm->mmap_sem);
	return ret;
}

static int futex_close(struct inode *inode, struct file *filp)
{
	struct futex_q *q = filp->private_data;

	unqueue_me(q);
	kfree(filp->private_data);
	return 0;
}

/* This is one-shot: once it's gone off you need a new fd */
static unsigned int futex_poll(struct file *filp,
			       struct poll_table_struct *wait)
{
	struct futex_q *q = filp->private_data;
	int ret = 0;

	poll_wait(filp, &q->waiters, wait);
	spin_lock(&futex_lock);
	if (list_empty(&q->list))
		ret = POLLIN | POLLRDNORM;
	spin_unlock(&futex_lock);

	return ret;
}

static struct file_operations futex_fops = {
	.release	= futex_close,
	.poll		= futex_poll,
};

/* Signal allows caller to avoid the race which would occur if they
   set the sigio stuff up afterwards. */
static int futex_fd(unsigned long uaddr, int signal)
{
	struct futex_q *q;
	union futex_key key;
	struct file *filp;
	int ret, err;

	ret = -EINVAL;
	if (signal < 0 || signal > _NSIG)
		goto out;

	ret = get_unused_fd();
	if (ret < 0)
		goto out;
	filp = get_empty_filp();
	if (!filp) {
		put_unused_fd(ret);
		ret = -ENFILE;
		goto out;
	}
	filp->f_op = &futex_fops;
	filp->f_vfsmnt = mntget(futex_mnt);
	filp->f_dentry = dget(futex_mnt->mnt_root);

	if (signal) {
		int err;
		err = f_setown(filp, current->tgid, 1);
		if (err < 0) {
			put_unused_fd(ret);
			put_filp(filp);
			ret = err;
			goto out;
		}
		filp->f_owner.signum = signal;
	}

	q = kmalloc(sizeof(*q), GFP_KERNEL);
	if (!q) {
		put_unused_fd(ret);
		put_filp(filp);
		ret = -ENOMEM;
		goto out;
	}

	down_read(&current->mm->mmap_sem);
	err = get_futex_key(uaddr, &key);
	up_read(&current->mm->mmap_sem);

	if (unlikely(err != 0)) {
		put_unused_fd(ret);
		put_filp(filp);
		kfree(q);
		return err;
	}

	init_waitqueue_head(&q->waiters);
	filp->private_data = q;

	queue_me(q, &key, ret, filp);

	/* Now we map fd to filp, so userspace can access it */
	fd_install(ret, filp);
out:
	return ret;
}

long do_futex(unsigned long uaddr, int op, int val, unsigned long timeout,
		unsigned long uaddr2, int val2)
{
	int ret;

	switch (op) {
	case FUTEX_WAIT:
		ret = futex_wait(uaddr, val, timeout);
		break;
	case FUTEX_WAKE:
		ret = futex_wake(uaddr, val);
		break;
	case FUTEX_FD:
		/* non-zero val means F_SETOWN(getpid()) & F_SETSIG(val) */
		ret = futex_fd(uaddr, val);
		break;
	case FUTEX_REQUEUE:
		ret = futex_requeue(uaddr, uaddr2, val, val2);
		break;
	default:
		ret = -ENOSYS;
	}
	return ret;
}


asmlinkage long sys_futex(u32 __user *uaddr, int op, int val,
			  struct timespec __user *utime, u32 __user *uaddr2)
{
	struct timespec t;
	unsigned long timeout = MAX_SCHEDULE_TIMEOUT;
	int val2 = 0;

	if ((op == FUTEX_WAIT) && utime) {
		if (copy_from_user(&t, utime, sizeof(t)) != 0)
			return -EFAULT;
		timeout = timespec_to_jiffies(&t) + 1;
	}
	/*
	 * requeue parameter in 'utime' if op == FUTEX_REQUEUE.
	 */
	if (op == FUTEX_REQUEUE)
		val2 = (int) (long) utime;

	return do_futex((unsigned long)uaddr, op, val, timeout,
			(unsigned long)uaddr2, val2);
}

static struct super_block *
futexfs_get_sb(struct file_system_type *fs_type,
	       int flags, const char *dev_name, void *data)
{
	return get_sb_pseudo(fs_type, "futex", NULL, 0xBAD1DEA);
}

static struct file_system_type futex_fs_type = {
	.name		= "futexfs",
	.get_sb		= futexfs_get_sb,
	.kill_sb	= kill_anon_super,
};

static int __init init(void)
{
	unsigned int i;

	register_filesystem(&futex_fs_type);
	futex_mnt = kern_mount(&futex_fs_type);

	for (i = 0; i < ARRAY_SIZE(futex_queues); i++)
		INIT_LIST_HEAD(&futex_queues[i]);
	return 0;
}
__initcall(init);
