/*
 *  linux/fs/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

/*
 * Some corrections by tytso.
 */

/* [Feb 1997 T. Schoebel-Theuer] Complete rewrite of the pathname
 * lookup logic.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/dalloc.h>
#include <linux/nametrans.h>
#include <linux/proc_fs.h>
#include <linux/omirr.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

#include <asm/uaccess.h>
#include <asm/unaligned.h>
#include <asm/semaphore.h>
#include <asm/namei.h>

/* This can be removed after the beta phase. */
#define CACHE_SUPERVISE	/* debug the correctness of dcache entries */
#undef DEBUG		/* some other debugging */


#define ACC_MODE(x) ("\000\004\002\006"[(x)&O_ACCMODE])

/* [Feb-1997 T. Schoebel-Theuer]
 * Fundamental changes in the pathname lookup mechanisms (namei)
 * were necessary because of omirr.  The reason is that omirr needs
 * to know the _real_ pathname, not the user-supplied one, in case
 * of symlinks (and also when transname replacements occur).
 *
 * The new code replaces the old recursive symlink resolution with
 * an iterative one (in case of non-nested symlink chains).  It does
 * this by looking up the symlink name from the particular filesystem,
 * and then follows this name as if it were a user-supplied one.  This
 * is done solely in the VFS level, such that <fs>_follow_link() is not
 * used any more and could be removed in future.  As a side effect,
 * dir_namei(), _namei() and follow_link() are now replaced with a single
 * function lookup_dentry() that can handle all the special cases of the former
 * code.
 *
 * With the new dcache, the pathname is stored at each inode, at least as
 * long as the refcount of the inode is positive.  As a side effect, the
 * size of the dcache depends on the inode cache and thus is dynamic.
 */

/* [24-Feb-97 T. Schoebel-Theuer] Side effects caused by new implementation:
 * New symlink semantics: when open() is called with flags O_CREAT | O_EXCL
 * and the name already exists in form of a symlink, try to create the new
 * name indicated by the symlink. The old code always complained that the
 * name already exists, due to not following the symlink even if its target
 * is non-existant.  The new semantics affects also mknod() and link() when
 * the name is a symlink pointing to a non-existant name.
 *
 * I don't know which semantics is the right one, since I have no access
 * to standards. But I found by trial that HP-UX 9.0 has the full "new"
 * semantics implemented, while SunOS 4.1.1 and Solaris (SunOS 5.4) have the
 * "old" one. Personally, I think the new semantics is much more logical.
 * Note that "ln old new" where "new" is a symlink pointing to a non-existing
 * file does succeed in both HP-UX and SunOs, but not in Solaris
 * and in the old Linux semantics.
 */

static char * quicklist = NULL;
static int quickcount = 0;
struct semaphore quicklock = MUTEX;

/* Tuning: increase locality by reusing same pages again...
 * if quicklist becomes too long on low memory machines, either a limit
 * should be added or after a number of cycles some pages should
 * be released again ...
 */
static inline char * get_page(void)
{
	char * res;
	down(&quicklock);
	res = quicklist;
	if (res) {
#ifdef DEBUG
		char * tmp = res;
		int i;
		for(i=0; i<quickcount; i++)
			tmp = *(char**)tmp;
		if (tmp)
			printk("bad quicklist %x\n", (int)tmp);
#endif
		quicklist = *(char**)res;
		quickcount--;
	}
	else
		res = (char*)__get_free_page(GFP_KERNEL);
	up(&quicklock);
	return res;
}

/*
 * Kernel pointers have redundant information, so we can use a
 * scheme where we can return either an error code or a dentry
 * pointer with the same return value.
 *
 * This should be a per-architecture thing, to allow different
 * error and pointer decisions.
 */
#define ERR_PTR(err)	((void *)((long)(err)))
#define PTR_ERR(ptr)	((long)(ptr))
#define IS_ERR(ptr)	((unsigned long)(ptr) > (unsigned long)(-1000))

inline void putname(char * name)
{
	if (name) {
		down(&quicklock);
		*(char**)name = quicklist;
		quicklist = name;
		quickcount++;
		up(&quicklock);
	}
	/* if a quicklist limit is necessary to introduce, call
	 * free_page((unsigned long) name);
	 */
}

/* In order to reduce some races, while at the same time doing additional
 * checking and hopefully speeding things up, we copy filenames to the
 * kernel data space before using them..
 *
 * POSIX.1 2.4: an empty pathname is invalid (ENOENT).
 */
static inline int do_getname(const char *filename, char *page)
{
	int retval;
	unsigned long len = PAGE_SIZE;

	if ((unsigned long) filename >= TASK_SIZE) {
		if (get_fs() != KERNEL_DS)
			return -EFAULT;
	} else if (TASK_SIZE - (unsigned long) filename < PAGE_SIZE)
		len = TASK_SIZE - (unsigned long) filename;

	retval = strncpy_from_user((char *)page, filename, len);
	if (retval > 0) {
		if (retval < len)
			return 0;
		return -ENAMETOOLONG;
	} else if (!retval)
		retval = -ENOENT;
	return retval;
}

int getname(const char * filename, char **result)
{
	char *tmp;
	int retval;

	tmp = get_page();
	if (!tmp)
		return -ENOMEM;
	retval = do_getname(filename, tmp);
	if (retval < 0)
		putname(tmp);
	else
		*result = tmp;
	return retval;
}

/*
 *	permission()
 *
 * is used to check for read/write/execute permissions on a file.
 * We use "fsuid" for this, letting us set arbitrary permissions
 * for filesystem access without changing the "normal" uids which
 * are used for other things..
 */
int permission(struct inode * inode,int mask)
{
	int mode = inode->i_mode;

	if (inode->i_op && inode->i_op->permission)
		return inode->i_op->permission(inode, mask);
	else if ((mask & S_IWOTH) && IS_RDONLY(inode) &&
		 (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)))
		return -EROFS; /* Nobody gets write access to a read-only fs */
	else if ((mask & S_IWOTH) && IS_IMMUTABLE(inode))
		return -EACCES; /* Nobody gets write access to an immutable file */
	else if (current->fsuid == inode->i_uid)
		mode >>= 6;
	else if (in_group_p(inode->i_gid))
		mode >>= 3;
	if (((mode & mask & 0007) == mask) || fsuser())
		return 0;
	return -EACCES;
}

/*
 * get_write_access() gets write permission for a file.
 * put_write_access() releases this write permission.
 * This is used for regular files.
 * We cannot support write (and maybe mmap read-write shared) accesses and
 * MAP_DENYWRITE mmappings simultaneously. The i_writecount field of an inode
 * can have the following values:
 * 0: no writers, no VM_DENYWRITE mappings
 * < 0: (-i_writecount) vm_area_structs with VM_DENYWRITE set exist
 * > 0: (i_writecount) users are writing to the file.
 */
int get_write_access(struct inode * inode)
{
	if (inode->i_writecount < 0)
		return -ETXTBSY;
	inode->i_writecount++;
	return 0;
}

void put_write_access(struct inode * inode)
{
	inode->i_writecount--;
}

/*
 * This is called when everything else fails, and we actually have
 * to go to the low-level filesystem to find out what we should do..
 *
 * We get the directory semaphore, and after getting that we also
 * make sure that nobody added the entry to the dcache in the meantime..
 */
static struct dentry * real_lookup(struct dentry * dentry, struct qstr * name)
{
	struct inode *dir = dentry->d_inode;
	struct dentry *result;
	struct inode *inode;
	int error = -ENOTDIR;

	down(&dir->i_sem);

	error = -ENOTDIR;
	if (dir->i_op && dir->i_op->lookup)
		error = dir->i_op->lookup(dir, name, &inode);
	result = ERR_PTR(error);

	if (!error || error == -ENOENT) {
		struct dentry *new;
		int isdir = 0, flags = D_NOCHECKDUP;

		if (error) {
			flags = D_NEGATIVE;
			inode = NULL;
		} else if (S_ISDIR(inode->i_mode)) {
			isdir = 1;
			flags = D_DIR|D_NOCHECKDUP;
		}
		new = d_alloc(dentry, name, isdir);

		/*
		 * Ok, now we can't sleep any more. Double-check that
		 * nobody else added this in the meantime..
		 */
		result = d_lookup(dentry, name);
		if (result) {
			d_free(new);
		} else {
			d_add(new, inode, flags);
			result = new;
		}
	}
	up(&dir->i_sem);
	return result;
}

/* Internal lookup() using the new generic dcache. */
static inline struct dentry * cached_lookup(struct dentry * dentry, struct qstr * name)
{
	return d_lookup(dentry, name);
}

/*
 * "." and ".." are special - ".." especially so because it has to be able
 * to know about the current root directory and parent relationships
 */
static struct dentry * reserved_lookup(struct dentry * dir, struct qstr * name)
{
	struct dentry *result = NULL;
	if (name->name[0] == '.') {
		switch (name->len) {
		default:
			break;
		case 2:	
			if (name->name[1] != '.')
				break;

			if (dir != current->fs->root) {
				dir = dir->d_covers->d_parent;
			}
			/* fallthrough */
		case 1:
			result = dget(dir);
			break;
		}
	}
	return result;
}

static inline int is_reserved(struct dentry *dentry)
{
	if (dentry->d_name.name[0] == '.') {
		switch (dentry->d_name.len) {
		case 2:
			if (dentry->d_name.name[1] != '.')
				break;
			/* fallthrough */
		case 1:
			return 1;
		}
	}
	return 0;
}

/* In difference to the former version, lookup() no longer eats the dir. */
static struct dentry * lookup(struct dentry * dir, struct qstr * name)
{
	int err;
	struct dentry * result;

	/* Check permissions before traversing mount-points. */
	err = permission(dir->d_inode, MAY_EXEC);
	result = ERR_PTR(err);
 	if (err)
		goto done;

	result = reserved_lookup(dir, name);
	if (result)
		goto done;

	result = cached_lookup(dir, name);
	if (result)
		goto done;

	result = real_lookup(dir, name);
	if (!result)
		result = ERR_PTR(-ENOTDIR);
done:
	if (!IS_ERR(result))
		result = dget(result->d_mounts);

	return result;
}

/*
 * This should check "link_count", but doesn't do that yet..
 */
static struct dentry * do_follow_link(struct dentry *base, struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;

	if (inode->i_op && inode->i_op->follow_link) {
		struct dentry *result;

		/* This eats the base */
		result = inode->i_op->follow_link(inode, base);
		base = dentry;
		dentry = result;
	}
	dput(base);
	return dentry;
}

/*
 * Name resolution.
 *
 * This is the basic name resolution function, turning a pathname
 * into the final dentry.
 */
struct dentry * lookup_dentry(const char * name, struct dentry * base, int follow_link)
{
	struct dentry * dentry;

	if (*name == '/') {
		if (base)
			dput(base);
		base = dget(current->fs->root);
		do {
			name++;
		} while (*name == '/');
	} else if (!base) {
		base = dget(current->fs->pwd);
	}

	if (*name == '\0')
		return base;

	/* At this point we know we have a real path component. */
	for(;;) {
		int len;
		unsigned long hash;
		struct qstr this;
		char c, trailing;

		this.name = name;
		hash = init_name_hash();
		for (len = 0; (c = *name++) && (c != '/') ; len++)
			hash = partial_name_hash(c, hash);

		this.len = len;
		this.hash = end_name_hash(hash);

		/* remove trailing slashes? */
		trailing = c;
		if (c) {
			while ((c = *name) == '/')
				name++;
		}

		dentry = lookup(base, &this);
		if (IS_ERR(dentry))
			break;
		if (dentry->d_flag & D_NEGATIVE)
			break;

		/* Last component? */
		if (!c) {
			if (!trailing && !follow_link)
				break;
			return do_follow_link(base, dentry);
		}

		base = do_follow_link(base, dentry);
	}
	dput(base);
	return dentry;
}

/*
 *	namei()
 *
 * is used by most simple commands to get the inode of a specified name.
 * Open, link etc use their own routines, but this is enough for things
 * like 'chmod' etc.
 *
 * namei exists in two versions: namei/lnamei. The only difference is
 * that namei follows links, while lnamei does not.
 */
int __namei(const char *pathname, struct inode **res_inode, int follow_link)
{
	int error;
	char * name;

	error = getname(pathname, &name);
	if (!error) {
		struct dentry * dentry;

		dentry = lookup_dentry(name, NULL, follow_link);
		putname(name);
		error = PTR_ERR(dentry);
		if (!IS_ERR(dentry)) {
			error = -ENOENT;
			if (dentry) {
				if (!(dentry->d_flag & D_NEGATIVE)) {
					struct inode *inode = dentry->d_inode;
					atomic_inc(&inode->i_count);
					*res_inode = inode;
					error = 0;
				}
				dput(dentry);
			}
		}
	}
	return error;
}

static inline struct inode *get_parent(struct dentry *dentry)
{
	struct inode *dir = dentry->d_parent->d_inode;

	atomic_inc(&dir->i_count);
	return dir;
}

static inline struct inode *lock_parent(struct dentry *dentry)
{
	struct inode *dir = dentry->d_parent->d_inode;

	atomic_inc(&dir->i_count);
	down(&dir->i_sem);
	return dir;
}

/*
 *	open_namei()
 *
 * namei for open - this is in fact almost the whole open-routine.
 *
 * Note that the low bits of "flag" aren't the same as in the open
 * system call - they are 00 - no permissions needed
 *			  01 - read permission needed
 *			  10 - write permission needed
 *			  11 - read/write permissions needed
 * which is a lot more logical, and also allows the "no perm" needed
 * for symlinks (where the permissions are checked later).
 */
int open_namei(const char * pathname, int flag, int mode,
               struct inode ** res_inode, struct dentry * base)
{
	int error;
	struct inode *inode;
	struct dentry *dentry;

	mode &= S_IALLUGO & ~current->fs->umask;
	mode |= S_IFREG;

	dentry = lookup_dentry(pathname, base, 1);
	if (IS_ERR(dentry))
		return PTR_ERR(dentry);

	if (flag & O_CREAT) {
		struct inode *dir;

		dir = lock_parent(dentry);
		/*
		 * The existence test must be done _after_ getting the directory
		 * semaphore - the dentry might otherwise change.
		 */
		if (!(dentry->d_flag & D_NEGATIVE)) {
			error = 0;
			if (flag & O_EXCL)
				error = -EEXIST;
		} else if (IS_RDONLY(dir))
			error = -EROFS;
		else if (!dir->i_op || !dir->i_op->create)
			error = -EACCES;
		else if ((error = permission(dir,MAY_WRITE | MAY_EXEC)) == 0) {
			if (dir->i_sb && dir->i_sb->dq_op)
				dir->i_sb->dq_op->initialize(dir, -1);
			error = dir->i_op->create(dir, dentry, mode);
		}
		up(&dir->i_sem);
		iput(dir);
		if (error)
			goto exit;
	}

	error = -ENOENT;
	if (dentry->d_flag & D_NEGATIVE)
		goto exit;

	inode = dentry->d_inode;
	error = -EISDIR;
	if (S_ISDIR(inode->i_mode) && (flag & 2))
		goto exit;

	error = permission(inode,ACC_MODE(flag));
	if (error)
		goto exit;

	/*
	 * FIFO's, sockets and device files are special: they don't
	 * actually live on the filesystem itself, and as such you
	 * can write to them even if the filesystem is read-only.
	 */
	if (S_ISFIFO(inode->i_mode) || S_ISSOCK(inode->i_mode)) {
	    	flag &= ~O_TRUNC;
	} else if (S_ISBLK(inode->i_mode) || S_ISCHR(inode->i_mode)) {
		error = -EACCES;
		if (IS_NODEV(inode))
			goto exit;

		flag &= ~O_TRUNC;
	} else {
		error = -EROFS;
		if (IS_RDONLY(inode) && (flag & 2))
			goto exit;
	}
	/*
	 * An append-only file must be opened in append mode for writing.
	 */
	error = -EPERM;
	if (IS_APPEND(inode) && ((flag & FMODE_WRITE) && !(flag & O_APPEND)))
		goto exit;

	if (flag & O_TRUNC) {
		error = get_write_access(inode);
		if (error)
			goto exit;

		/*
		 * Refuse to truncate files with mandatory locks held on them.
		 */
		error = locks_verify_locked(inode);
		if (!error) {
			if (inode->i_sb && inode->i_sb->dq_op)
				inode->i_sb->dq_op->initialize(inode, -1);
			
			error = do_truncate(inode, 0);
		}
		put_write_access(inode);
		if (error)
			goto exit;
	} else
		if (flag & FMODE_WRITE)
			if (inode->i_sb && inode->i_sb->dq_op)
				inode->i_sb->dq_op->initialize(inode, -1);

	*res_inode = inode;
	atomic_inc(&inode->i_count);
	error = 0;

exit:
	dput(dentry);
	return error;
}

int do_mknod(const char * filename, int mode, dev_t dev)
{
	int error;
	struct inode *dir;
	struct dentry *dentry;

	mode &= ~current->fs->umask;
	dentry = lookup_dentry(filename, NULL, 1);

	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	dir = lock_parent(dentry);

	error = -EEXIST;
	if (!(dentry->d_flag & D_NEGATIVE))
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(dir))
		goto exit_lock;

	error = permission(dir,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->i_op || !dir->i_op->mknod)
		goto exit_lock;

	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	error = dir->i_op->mknod(dir, dentry, mode, dev);

exit_lock:
	up(&dir->i_sem);
	iput(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage int sys_mknod(const char * filename, int mode, dev_t dev)
{
	int error;
	char * tmp;

	lock_kernel();
	error = -EPERM;
	if (S_ISDIR(mode) || (!S_ISFIFO(mode) && !fsuser()))
		goto out;
	error = -EINVAL;
	switch (mode & S_IFMT) {
	case 0:
		mode |= S_IFREG;
		break;
	case S_IFREG: case S_IFCHR: case S_IFBLK: case S_IFIFO: case S_IFSOCK:
		break;
	default:
		goto out;
	}
	error = getname(filename,&tmp);
	if (!error) {
		error = do_mknod(tmp,mode,dev);
		putname(tmp);
	}
out:
	unlock_kernel();
	return error;
}

/*
 * Look out: this function may change a normal dentry
 * into a directory dentry (different size)..
 */
static inline int do_mkdir(const char * pathname, int mode)
{
	int error;
	struct inode *dir;
	struct dentry *dentry;

	dentry = lookup_dentry(pathname, NULL, 1);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	dir = lock_parent(dentry);

	error = -EEXIST;
	if (!(dentry->d_flag & D_NEGATIVE))
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(dir))
		goto exit_lock;

	error = permission(dir,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->i_op || !dir->i_op->mkdir)
		goto exit_lock;

	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	mode &= 0777 & ~current->fs->umask;
	error = dir->i_op->mkdir(dir, dentry, mode);

exit_lock:
	up(&dir->i_sem);
	iput(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage int sys_mkdir(const char * pathname, int mode)
{
	int error;
	char * tmp;

	lock_kernel();
	error = getname(pathname,&tmp);
	if (!error) {
		error = do_mkdir(tmp,mode);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

static inline int do_rmdir(const char * name)
{
	int error;
	struct inode *dir;
	struct dentry *dentry;

	dentry = lookup_dentry(name, NULL, 1);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	dir = lock_parent(dentry);
	error = -ENOENT;
	if (dentry->d_flag & D_NEGATIVE)
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(dir))
		goto exit_lock;

	error = permission(dir,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	/*
	 * A subdirectory cannot be removed from an append-only directory.
	 */
	error = -EPERM;
	if (IS_APPEND(dir))
		goto exit_lock;

	/* Disallow removals of mountpoints. */
	error = -EBUSY;
	if (dentry->d_covers != dentry)
		goto exit_lock;

	error = -EPERM;
	if (!dir->i_op || !dir->i_op->rmdir)
		goto exit_lock;

	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);

	error = dir->i_op->rmdir(dir, dentry);

exit_lock:
        up(&dir->i_sem);
        iput(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage int sys_rmdir(const char * pathname)
{
	int error;
	char * tmp;

	lock_kernel();
	error = getname(pathname,&tmp);
	if (!error) {
		error = do_rmdir(tmp);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

static inline int do_unlink(const char * name)
{
	int error;
	struct inode *dir;
	struct dentry *dentry;

	dentry = lookup_dentry(name, NULL, 0);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	dir = lock_parent(dentry);

	error = -EROFS;
	if (IS_RDONLY(dir))
		goto exit_lock;

	error = permission(dir,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	/*
	 * A file cannot be removed from an append-only directory.
	 */
	error = -EPERM;
	if (IS_APPEND(dir))
		goto exit_lock;

	error = -EPERM;
	if (!dir->i_op || !dir->i_op->unlink)
		goto exit_lock;

	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);

	error = dir->i_op->unlink(dir, dentry);

exit_lock:
        up(&dir->i_sem);
        iput(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage int sys_unlink(const char * pathname)
{
	int error;
	char * tmp;

	lock_kernel();
	error = getname(pathname,&tmp);
	if (!error) {
		error = do_unlink(tmp);
		putname(tmp);
	}
	unlock_kernel();
	return error;
}

static inline int do_symlink(const char * oldname, const char * newname)
{
	int error;
	struct inode *dir;
	struct dentry *dentry;

	dentry = lookup_dentry(newname, NULL, 0);

	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto exit;

	error = -EEXIST;
	if (!(dentry->d_flag & D_NEGATIVE))
		goto exit;

	dir = lock_parent(dentry);

	error = -EROFS;
	if (IS_RDONLY(dir))
		goto exit_lock;

	error = permission(dir,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (!dir->i_op || !dir->i_op->symlink)
		goto exit_lock;

	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	error = dir->i_op->symlink(dir, dentry, oldname);

exit_lock:
	up(&dir->i_sem);
	iput(dir);
	dput(dentry);
exit:
	return error;
}

asmlinkage int sys_symlink(const char * oldname, const char * newname)
{
	int error;
	char * from, * to;

	lock_kernel();
	error = getname(oldname,&from);
	if (!error) {
		error = getname(newname,&to);
		if (!error) {
			error = do_symlink(from,to);
			putname(to);
		}
		putname(from);
	}
	unlock_kernel();
	return error;
}

static inline int do_link(const char * oldname, const char * newname)
{
	struct dentry *old_dentry, *new_dentry;
	struct inode *dir, *inode;
	int error;

	old_dentry = lookup_dentry(oldname, NULL, 1);
	error = PTR_ERR(old_dentry);
	if (IS_ERR(old_dentry))
		goto exit;

	new_dentry = lookup_dentry(newname, NULL, 1);
	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto exit_old;

	dir = lock_parent(new_dentry);

	error = -ENOENT;
	if (old_dentry->d_flag & D_NEGATIVE)
		goto exit_lock;

	error = -EEXIST;
	if (!(new_dentry->d_flag & D_NEGATIVE))
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(dir))
		goto exit_lock;

	inode = old_dentry->d_inode;
	error = -EXDEV;
	if (dir->i_dev != inode->i_dev)
		goto exit_lock;

	error = permission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	/*
	 * A link to an append-only or immutable file cannot be created.
	 */
	error = -EPERM;
	if (IS_APPEND(inode) || IS_IMMUTABLE(inode))
		goto exit_lock;

	error = -EPERM;
	if (!dir->i_op || !dir->i_op->link)
		goto exit_lock;

	if (dir->i_sb && dir->i_sb->dq_op)
		dir->i_sb->dq_op->initialize(dir, -1);
	error = dir->i_op->link(inode, dir, new_dentry);

exit_lock:
	up(&dir->i_sem);
	iput(dir);
	dput(new_dentry);
exit_old:
	dput(old_dentry);
exit:
	return error;
}

asmlinkage int sys_link(const char * oldname, const char * newname)
{
	int error;
	char * from, * to;

	lock_kernel();
	error = getname(oldname,&from);
	if (!error) {
		error = getname(newname,&to);
		if (!error) {
			error = do_link(from,to);
			putname(to);
		}
		putname(from);
	}
	unlock_kernel();
	return error;
}

/*
 * Whee.. Deadlock country. Happily there is only one VFS
 * operation that does this..
 */
static inline void double_down(struct semaphore *s1, struct semaphore *s2)
{
	if ((unsigned long) s1 < (unsigned long) s2) {
		down(s1);
		down(s2);
	} else if (s1 == s2) {
		down(s1);
		atomic_dec(&s1->count);
	} else {
		down(s2);
		down(s1);
	}
}

static inline int do_rename(const char * oldname, const char * newname)
{
	int error;
	struct inode * old_dir, * new_dir;
	struct dentry * old_dentry, *new_dentry;

	old_dentry = lookup_dentry(oldname, NULL, 1);

	error = PTR_ERR(old_dentry);
	if (IS_ERR(old_dentry))
		goto exit;

	new_dentry = lookup_dentry(newname, NULL, 1);

	error = PTR_ERR(new_dentry);
	if (IS_ERR(new_dentry))
		goto exit_old;

	new_dir = get_parent(new_dentry);
	old_dir = get_parent(old_dentry);

	double_down(&new_dir->i_sem, &old_dir->i_sem);

	error = -ENOENT;
	if (old_dentry->d_flag & D_NEGATIVE)
		goto exit_lock;

	error = permission(old_dir,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;
	error = permission(new_dir,MAY_WRITE | MAY_EXEC);
	if (error)
		goto exit_lock;

	error = -EPERM;
	if (is_reserved(new_dentry) || is_reserved(old_dentry))
		goto exit_lock;

	/* Disallow moves of mountpoints. */
	error = -EBUSY;
	if (old_dentry->d_covers != old_dentry)
		goto exit_lock;

	error = -EXDEV;
	if (new_dir->i_dev != old_dir->i_dev)
		goto exit_lock;

	error = -EROFS;
	if (IS_RDONLY(new_dir) || IS_RDONLY(old_dir))
		goto exit_lock;

	/*
	 * A file cannot be removed from an append-only directory.
	 */
	error = -EPERM;
	if (IS_APPEND(old_dir))
		goto exit_lock;

	error = -EPERM;
	if (!old_dir->i_op || !old_dir->i_op->rename)
		goto exit_lock;

	if (new_dir->i_sb && new_dir->i_sb->dq_op)
		new_dir->i_sb->dq_op->initialize(new_dir, -1);
	error = old_dir->i_op->rename(old_dir, old_dentry, new_dir, new_dentry);

exit_lock:
	up(&new_dir->i_sem);
	up(&old_dir->i_sem);
	iput(old_dir);
	iput(new_dir);
	dput(new_dentry);
exit_old:
	dput(old_dentry);
exit:
	return error;
}

asmlinkage int sys_rename(const char * oldname, const char * newname)
{
	int error;
	char * from, * to;

	lock_kernel();
	error = getname(oldname,&from);
	if (!error) {
		error = getname(newname,&to);
		if (!error) {
			error = do_rename(from,to);
			putname(to);
		}
		putname(from);
	}
	unlock_kernel();
	return error;
}
