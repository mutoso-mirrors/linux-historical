/*
 *  linux/fs/proc/root.c
 *
 *  Copyright (C) 1991, 1992 Linus Torvalds
 *
 *  proc root directory handling functions
 */

#include <asm/uaccess.h>

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/config.h>
#include <linux/init.h>
#include <asm/bitops.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#ifdef CONFIG_ZORRO
#include <linux/zorro.h>
#endif

static int proc_root_readdir(struct file *, void *, filldir_t);
static struct dentry *proc_root_lookup(struct inode *,struct dentry *);
static int proc_unlink(struct inode *, struct dentry *);

static unsigned char proc_alloc_map[PROC_NDYNAMIC / 8] = {0};

/*
 * These are the generic /proc directory operations. They
 * use the in-memory "struct proc_dir_entry" tree to parse
 * the /proc directory.
 */
static struct file_operations proc_dir_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	proc_readdir,		/* readdir */
};

/*
 * proc directories can do almost nothing..
 */
struct inode_operations proc_dir_inode_operations = {
	&proc_dir_operations,	/* default net directory file-ops */
	NULL,			/* create */
	proc_lookup,		/* lookup */
};

/*
 * /proc dynamic directories now support unlinking
 */
struct inode_operations proc_dyna_dir_inode_operations = {
	&proc_dir_operations,	/* default proc dir ops */
	NULL,			/* create */
	proc_lookup,		/* lookup */
	NULL,			/* link	*/
	proc_unlink,		/* unlink(struct inode *, struct dentry *) */
};

/*
 * The root /proc directory is special, as it has the
 * <pid> directories. Thus we don't use the generic
 * directory handling functions for that..
 */
static struct file_operations proc_root_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	proc_root_readdir,	/* readdir */
};

/*
 * proc root can do almost nothing..
 */
static struct inode_operations proc_root_inode_operations = {
	&proc_root_operations,	/* default base directory file-ops */
	NULL,			/* create */
	proc_root_lookup,	/* lookup */
};

/*
 * This is the root "inode" in the /proc tree..
 */
struct proc_dir_entry proc_root = {
	PROC_ROOT_INO, 5, "/proc",
	S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,
	0, &proc_root_inode_operations,
	NULL, NULL,
	NULL,
	&proc_root, NULL
};

struct proc_dir_entry *proc_net, *proc_bus, *proc_root_fs, *proc_root_driver;

#ifdef CONFIG_MCA
struct proc_dir_entry *proc_mca;
#endif

#ifdef CONFIG_SYSCTL
struct proc_dir_entry *proc_sys_root;
#endif

#if defined(CONFIG_SUN_OPENPROMFS) || defined(CONFIG_SUN_OPENPROMFS_MODULE)

static int (*proc_openprom_defreaddir_ptr)(struct file *, void *, filldir_t);
static struct dentry * (*proc_openprom_deflookup_ptr)(struct inode *, struct dentry *);
void (*proc_openprom_use)(struct inode *, int) = 0;
static struct openpromfs_dev *proc_openprom_devices = NULL;
static ino_t proc_openpromdev_ino = PROC_OPENPROMD_FIRST;

struct inode_operations *
proc_openprom_register(int (*readdir)(struct file *, void *, filldir_t),
		       struct dentry * (*lookup)(struct inode *, struct dentry *),
		       void (*use)(struct inode *, int),
		       struct openpromfs_dev ***devices)
{
	proc_openprom_defreaddir_ptr = (proc_openprom_inode_operations.default_file_ops)->readdir;
	proc_openprom_deflookup_ptr = proc_openprom_inode_operations.lookup;
	(proc_openprom_inode_operations.default_file_ops)->readdir = readdir;
	proc_openprom_inode_operations.lookup = lookup;
	proc_openprom_use = use;
	*devices = &proc_openprom_devices;
	return &proc_openprom_inode_operations;
}

int proc_openprom_regdev(struct openpromfs_dev *d)
{
	if (proc_openpromdev_ino == PROC_OPENPROMD_FIRST + PROC_NOPENPROMD)
		return -1;
	d->next = proc_openprom_devices;
	d->inode = proc_openpromdev_ino++;
	proc_openprom_devices = d;
	return 0;
}

int proc_openprom_unregdev(struct openpromfs_dev *d)
{
	if (d == proc_openprom_devices) {
		proc_openprom_devices = d->next;
	} else if (!proc_openprom_devices)
		return -1;
	else {
		struct openpromfs_dev *p;
		
		for (p = proc_openprom_devices; p->next != d && p->next; p = p->next);
		if (!p->next) return -1;
		p->next = d->next;
	}
	return 0;
}

#ifdef CONFIG_SUN_OPENPROMFS_MODULE
void
proc_openprom_deregister(void)
{
	(proc_openprom_inode_operations.default_file_ops)->readdir = proc_openprom_defreaddir_ptr;
	proc_openprom_inode_operations.lookup = proc_openprom_deflookup_ptr;
	proc_openprom_use = 0;
}		      
#endif

#if defined(CONFIG_SUN_OPENPROMFS_MODULE) && defined(CONFIG_KMOD)
static int 
proc_openprom_defreaddir(struct file * filp, void * dirent, filldir_t filldir)
{
	request_module("openpromfs");
	if ((proc_openprom_inode_operations.default_file_ops)->readdir !=
	    proc_openprom_defreaddir)
		return (proc_openprom_inode_operations.default_file_ops)->readdir 
				(filp, dirent, filldir);
	return -EINVAL;
}
#define OPENPROM_DEFREADDIR proc_openprom_defreaddir

static struct dentry *
proc_openprom_deflookup(struct inode * dir, struct dentry *dentry)
{
	request_module("openpromfs");
	if (proc_openprom_inode_operations.lookup !=
	    proc_openprom_deflookup)
		return proc_openprom_inode_operations.lookup 
				(dir, dentry);
	return ERR_PTR(-ENOENT);
}
#define OPENPROM_DEFLOOKUP proc_openprom_deflookup
#else
#define OPENPROM_DEFREADDIR NULL
#define OPENPROM_DEFLOOKUP NULL
#endif

static struct file_operations proc_openprom_operations = {
	NULL,			/* lseek - default */
	NULL,			/* read - bad */
	NULL,			/* write - bad */
	OPENPROM_DEFREADDIR,	/* readdir */
};

struct inode_operations proc_openprom_inode_operations = {
	&proc_openprom_operations,/* default net directory file-ops */
	NULL,			/* create */
	OPENPROM_DEFLOOKUP,	/* lookup */
};

struct proc_dir_entry proc_openprom = {
	PROC_OPENPROM, 8, "openprom",
	S_IFDIR | S_IRUGO | S_IXUGO, 2, 0, 0,
	0, &proc_openprom_inode_operations,
	NULL, NULL,
	NULL,
	&proc_root, NULL
};

extern void openpromfs_init (void);
#endif /* CONFIG_SUN_OPENPROMFS */

static int make_inode_number(void)
{
	int i = find_first_zero_bit((void *) proc_alloc_map, PROC_NDYNAMIC);
	if (i<0 || i>=PROC_NDYNAMIC) 
		return -1;
	set_bit(i, (void *) proc_alloc_map);
	return PROC_DYNAMIC_FIRST + i;
}

int proc_readlink(struct dentry * dentry, char * buffer, int buflen)
{
	struct inode *inode = dentry->d_inode;
	struct proc_dir_entry * de;
	char 	*page;
	int len = 0;

	de = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!de)
		return -ENOENT;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	if (de->readlink_proc)
		len = de->readlink_proc(de, page);

	if (len > buflen)
		len = buflen;

	copy_to_user(buffer, page, len);
	free_page((unsigned long) page);
	return len;
}

struct dentry * proc_follow_link(struct dentry * dentry, struct dentry *base, unsigned int follow)
{
	struct inode *inode = dentry->d_inode;
	struct proc_dir_entry * de;
	char 	*page;
	struct dentry *d;
	int len = 0;

	de = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return NULL;

	if (de->readlink_proc)
		len = de->readlink_proc(de, page);

	d = lookup_dentry(page, base, follow);
	free_page((unsigned long) page);
	return d;
}

static struct inode_operations proc_link_inode_operations = {
	NULL,			/* no file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	proc_readlink,		/* readlink */
	proc_follow_link,	/* follow_link */
	NULL,			/* get_block */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};

int proc_register(struct proc_dir_entry * dir, struct proc_dir_entry * dp)
{
	int	i;
	
	if (dp->low_ino == 0) {
		i = make_inode_number();
		if (i < 0)
			return -EAGAIN;
		dp->low_ino = i;
	}
	dp->next = dir->subdir;
	dp->parent = dir;
	dir->subdir = dp;
	if (S_ISDIR(dp->mode)) {
		if (dp->ops == NULL)
			dp->ops = &proc_dir_inode_operations;
		dir->nlink++;
	} else if (S_ISLNK(dp->mode)) {
		if (dp->ops == NULL)
			dp->ops = &proc_link_inode_operations;
	} else {
		if (dp->ops == NULL)
			dp->ops = &proc_file_inode_operations;
	}
	return 0;
}

/*
 * Kill an inode that got unregistered..
 */
static void proc_kill_inodes(int ino)
{
	struct list_head *p;
	struct super_block *sb;

	/*
	 * Actually it's a partial revoke(). We have to go through all
	 * copies of procfs. proc_super_blocks is protected by the big
	 * lock for the time being.
	 */
	for (sb = proc_super_blocks;
	     sb;
	     sb = (struct super_block*)sb->u.generic_sbp) {
		file_list_lock();
		for (p = sb->s_files.next; p != &sb->s_files; p = p->next) {
			struct file * filp = list_entry(p, struct file, f_list);
			struct dentry * dentry;
			struct inode * inode;

			dentry = filp->f_dentry;
			if (!dentry)
				continue;
			if (dentry->d_op != &proc_dentry_operations)
				continue;
			inode = dentry->d_inode;
			if (!inode)
				continue;
			if (inode->i_ino != ino)
				continue;
			filp->f_op = NULL;
		}
		file_list_unlock();
	}
}

int proc_unregister(struct proc_dir_entry * dir, int ino)
{
	struct proc_dir_entry **p = &dir->subdir, *dp;

	while ((dp = *p) != NULL) {
		if (dp->low_ino == ino) {
			*p = dp->next;
			dp->next = NULL;
			if (S_ISDIR(dp->mode))
				dir->nlink--;
			if (ino >= PROC_DYNAMIC_FIRST &&
			    ino < PROC_DYNAMIC_FIRST+PROC_NDYNAMIC)
				clear_bit(ino-PROC_DYNAMIC_FIRST, 
					  (void *) proc_alloc_map);
			proc_kill_inodes(ino);
			return 0;
		}
		p = &dp->next;
	}
	return -EINVAL;
}	

/*
 * /proc/self:
 */
static int proc_self_readlink(struct dentry *dentry, char *buffer, int buflen)
{
	int len;
	char tmp[30];

	len = sprintf(tmp, "%d", current->pid);
	if (buflen < len)
		len = buflen;
	copy_to_user(buffer, tmp, len);
	return len;
}

static struct dentry * proc_self_follow_link(struct dentry *dentry,
						struct dentry *base,
						unsigned int follow)
{
	char tmp[30];

	sprintf(tmp, "%d", current->pid);
	return lookup_dentry(tmp, base, follow);
}	

static struct inode_operations proc_self_inode_operations = {
	NULL,			/* no file-ops */
	NULL,			/* create */
	NULL,			/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	proc_self_readlink,	/* readlink */
	proc_self_follow_link,	/* follow_link */
	NULL,			/* get_block */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};

static struct proc_dir_entry proc_root_self = {
	0, 4, "self",
	S_IFLNK | S_IRUGO | S_IWUGO | S_IXUGO, 1, 0, 0,
	64, &proc_self_inode_operations,
};
#ifdef __powerpc__
static struct proc_dir_entry proc_root_ppc_htab = {
	0, 8, "ppc_htab",
	S_IFREG | S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH, 1, 0, 0,
	0, &proc_ppc_htab_inode_operations,
};
#endif

void __init proc_root_init(void)
{
	proc_misc_init();
	proc_register(&proc_root, &proc_root_self);
	proc_net = create_proc_entry("net", S_IFDIR, 0);
#ifdef CONFIG_SYSVIPC
	create_proc_entry("sysvipc", S_IFDIR, 0);
#endif
#ifdef CONFIG_SYSCTL
	proc_sys_root = create_proc_entry("sys", S_IFDIR, 0);
#endif
#ifdef CONFIG_MCA
	proc_mca = create_proc_entry("mca", S_IFDIR, 0);
#endif
	proc_root_fs = create_proc_entry("fs", S_IFDIR, 0);
	proc_root_driver = create_proc_entry("driver", S_IFDIR, 0);
#if defined(CONFIG_SUN_OPENPROMFS) || defined(CONFIG_SUN_OPENPROMFS_MODULE)
#ifdef CONFIG_SUN_OPENPROMFS
	openpromfs_init ();
#endif
	proc_register(&proc_root, &proc_openprom);
#endif
	proc_tty_init();
#ifdef __powerpc__
	proc_register(&proc_root, &proc_root_ppc_htab);
#endif
#ifdef CONFIG_PROC_DEVICETREE
	proc_device_tree_init();
#endif
	proc_bus = create_proc_entry("bus", S_IFDIR, 0);
}

/*
 * As some entries in /proc are volatile, we want to 
 * get rid of unused dentries.  This could be made 
 * smarter: we could keep a "volatile" flag in the 
 * inode to indicate which ones to keep.
 */
static void
proc_delete_dentry(struct dentry * dentry)
{
	d_drop(dentry);
}

struct dentry_operations proc_dentry_operations =
{
	NULL,			/* revalidate */
	NULL,			/* d_hash */
	NULL,			/* d_compare */
	proc_delete_dentry	/* d_delete(struct dentry *) */
};

/*
 * Don't create negative dentries here, return -ENOENT by hand
 * instead.
 */
struct dentry *proc_lookup(struct inode * dir, struct dentry *dentry)
{
	struct inode *inode;
	struct proc_dir_entry * de;
	int error;

	error = -ENOENT;
	inode = NULL;
	de = (struct proc_dir_entry *) dir->u.generic_ip;
	if (de) {
		for (de = de->subdir; de ; de = de->next) {
			if (!de || !de->low_ino)
				continue;
			if (de->namelen != dentry->d_name.len)
				continue;
			if (!memcmp(dentry->d_name.name, de->name, de->namelen)) {
				int ino = de->low_ino;
				error = -EINVAL;
				inode = proc_get_inode(dir->i_sb, ino, de);
				break;
			}
		}
	}

	if (inode) {
		dentry->d_op = &proc_dentry_operations;
		d_add(dentry, inode);
		return NULL;
	}
	return ERR_PTR(error);
}

static struct dentry *proc_root_lookup(struct inode * dir, struct dentry * dentry)
{
	struct task_struct *p;

	if (dir->i_ino == PROC_ROOT_INO) { /* check for safety... */
		extern unsigned long total_forks;
		static int last_timestamp = 0;

		/*
		 * this one can be a serious 'ps' performance problem if
		 * there are many threads running - thus we do 'lazy'
		 * link-recalculation - we change it only if the number
		 * of threads has increased.
		 */
		if (total_forks != last_timestamp) {
			int nlink = proc_root.nlink;

			read_lock(&tasklist_lock);
			last_timestamp = total_forks;
			for_each_task(p)
				nlink++;
			read_unlock(&tasklist_lock);
			/*
			 * subtract the # of idle threads which
			 * do not show up in /proc:
			 */
			dir->i_nlink = nlink - smp_num_cpus;
		}
	}

	if (!proc_lookup(dir, dentry))
		return NULL;
	
	return proc_pid_lookup(dir, dentry);
}

/*
 * This returns non-zero if at EOF, so that the /proc
 * root directory can use this and check if it should
 * continue with the <pid> entries..
 *
 * Note that the VFS-layer doesn't care about the return
 * value of the readdir() call, as long as it's non-negative
 * for success..
 */
int proc_readdir(struct file * filp,
	void * dirent, filldir_t filldir)
{
	struct proc_dir_entry * de;
	unsigned int ino;
	int i;
	struct inode *inode = filp->f_dentry->d_inode;

	ino = inode->i_ino;
	de = (struct proc_dir_entry *) inode->u.generic_ip;
	if (!de)
		return -EINVAL;
	i = filp->f_pos;
	switch (i) {
		case 0:
			if (filldir(dirent, ".", 1, i, ino) < 0)
				return 0;
			i++;
			filp->f_pos++;
			/* fall through */
		case 1:
			if (filldir(dirent, "..", 2, i, de->parent->low_ino) < 0)
				return 0;
			i++;
			filp->f_pos++;
			/* fall through */
		default:
			de = de->subdir;
			i -= 2;
			for (;;) {
				if (!de)
					return 1;
				if (!i)
					break;
				de = de->next;
				i--;
			}

			do {
				if (filldir(dirent, de->name, de->namelen, filp->f_pos, de->low_ino) < 0)
					return 0;
				filp->f_pos++;
				de = de->next;
			} while (de);
	}
	return 1;
}

static int proc_root_readdir(struct file * filp,
	void * dirent, filldir_t filldir)
{
	unsigned int nr = filp->f_pos;

	if (nr < FIRST_PROCESS_ENTRY) {
		int error = proc_readdir(filp, dirent, filldir);
		if (error <= 0)
			return error;
		filp->f_pos = FIRST_PROCESS_ENTRY;
	}

	return proc_pid_readdir(filp, dirent, filldir);
}

static int proc_unlink(struct inode *dir, struct dentry *dentry)
{
	struct proc_dir_entry * dp = dir->u.generic_ip;

printk("proc_file_unlink: deleting %s/%s\n", dp->name, dentry->d_name.name);

	remove_proc_entry(dentry->d_name.name, dp);
	dentry->d_inode->i_nlink = 0;
	d_delete(dentry);
	return 0;
}
