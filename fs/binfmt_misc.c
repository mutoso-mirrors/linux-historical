/*
 *  binfmt_misc.c
 *
 *  Copyright (C) 1997 Richard G�nther
 *
 *  binfmt_misc detects binaries via a magic or filename extension and invokes
 *  a specified wrapper. This should obsolete binfmt_java, binfmt_em86 and
 *  binfmt_mz.
 *
 *  1997-04-25 first version
 *  [...]
 *  1997-05-19 cleanup
 *  1997-06-26 hpa: pass the real filename rather than argv[0]
 *  1997-06-30 minor cleanup
 *  1997-08-09 removed extension stripping, locking cleanup
 *  2001-02-28 AV: rewritten into something that resembles C. Original didn't.
 */

#include <linux/module.h>
#include <linux/init.h>

#include <linux/binfmts.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/file.h>
#include <linux/pagemap.h>

#include <asm/uaccess.h>

enum {
	VERBOSE_STATUS = 1 /* make it zero to save 400 bytes kernel memory */
};

static LIST_HEAD(entries);
static int enabled = 1;

enum {Enabled, Magic};

typedef struct {
	struct list_head list;
	int flags;			/* type, status, etc. */
	int offset;			/* offset of magic */
	int size;			/* size of magic/mask */
	char *magic;			/* magic or filename extension */
	char *mask;			/* mask, NULL for exact match */
	char *interpreter;		/* filename of interpreter */
	char *name;
	struct dentry *dentry;
} Node;

static rwlock_t entries_lock __attribute__((unused)) = RW_LOCK_UNLOCKED;
static struct vfsmount *bm_mnt;
static int entry_count = 0;

/* 
 * Check if we support the binfmt
 * if we do, return the node, else NULL
 * locking is done in load_misc_binary
 */
static Node *check_file(struct linux_binprm *bprm)
{
	char *p = strrchr(bprm->filename, '.');
	struct list_head *l;

	list_for_each(l, &entries) {
		Node *e = list_entry(l, Node, list);
		char *s;
		int j;

		if (!test_bit(Enabled, &e->flags))
			continue;

		if (!test_bit(Magic, &e->flags)) {
			if (p && !strcmp(e->magic, p + 1))
				return e;
			continue;
		}

		s = bprm->buf + e->offset;
		if (e->mask) {
			for (j = 0; j < e->size; j++)
				if ((*s++ ^ e->magic[j]) & e->mask[j])
					break;
		} else {
			for (j = 0; j < e->size; j++)
				if ((*s++ ^ e->magic[j]))
					break;
		}
		if (j == e->size)
			return e;
	}
	return NULL;
}

/*
 * the loader itself
 */
static int load_misc_binary(struct linux_binprm *bprm, struct pt_regs *regs)
{
	Node *fmt;
	struct file * file;
	char iname[BINPRM_BUF_SIZE];
	char *iname_addr = iname;
	int retval;

	retval = -ENOEXEC;
	if (!enabled)
		goto _ret;

	/* to keep locking time low, we copy the interpreter string */
	read_lock(&entries_lock);
	fmt = check_file(bprm);
	if (fmt) {
		strncpy(iname, fmt->interpreter, BINPRM_BUF_SIZE - 1);
		iname[BINPRM_BUF_SIZE - 1] = '\0';
	}
	read_unlock(&entries_lock);
	if (!fmt)
		goto _ret;

	allow_write_access(bprm->file);
	fput(bprm->file);
	bprm->file = NULL;

	/* Build args for interpreter */
	remove_arg_zero(bprm);
	retval = copy_strings_kernel(1, &bprm->filename, bprm);
	if (retval < 0) goto _ret; 
	bprm->argc++;
	retval = copy_strings_kernel(1, &iname_addr, bprm);
	if (retval < 0) goto _ret; 
	bprm->argc++;
	bprm->filename = iname;	/* for binfmt_script */

	file = open_exec(iname);
	retval = PTR_ERR(file);
	if (IS_ERR(file))
		goto _ret;
	bprm->file = file;

	retval = prepare_binprm(bprm);
	if (retval >= 0)
		retval = search_binary_handler(bprm, regs);
_ret:
	return retval;
}

/* Command parsers */

/*
 * parses and copies one argument enclosed in del from *sp to *dp,
 * recognising the \x special.
 * returns pointer to the copied argument or NULL in case of an
 * error (and sets err) or null argument length.
 */
static char *scanarg(char *s, char del)
{
	char c;

	while ((c = *s++) != del) {
		if (c == '\\' && *s == 'x') {
			s++;
			if (!isxdigit(*s++))
				return NULL;
			if (!isxdigit(*s++))
				return NULL;
		}
	}
	return s;
}

static int unquote(char *from)
{
	char c = 0, *s = from, *p = from;

	while ((c = *s++) != '\0') {
		if (c == '\\' && *s == 'x') {
			s++;
			c = toupper(*s++);
			*p = (c - (isdigit(c) ? '0' : 'A' - 10)) << 4;
			c = toupper(*s++);
			*p++ |= c - (isdigit(c) ? '0' : 'A' - 10);
			continue;
		}
		*p++ = c;
	}
	return p - from;
}

/*
 * This registers a new binary format, it recognises the syntax
 * ':name:type:offset:magic:mask:interpreter:'
 * where the ':' is the IFS, that can be chosen with the first char
 */
static Node *create_entry(const char *buffer, size_t count)
{
	Node *e;
	int memsize, err;
	char *buf, *p;
	char del;

	/* some sanity checks */
	err = -EINVAL;
	if ((count < 11) || (count > 256))
		goto out;

	err = -ENOMEM;
	memsize = sizeof(Node) + count + 8;
	e = (Node *) kmalloc(memsize, GFP_USER);
	if (!e)
		goto out;

	p = buf = (char *)e + sizeof(Node);

	memset(e, 0, sizeof(Node));
	if (copy_from_user(buf, buffer, count))
		goto Efault;

	del = *p++;	/* delimeter */

	memset(buf+count, del, 8);

	e->name = p;
	p = strchr(p, del);
	if (!p)
		goto Einval;
	*p++ = '\0';
	if (!e->name[0] ||
	    !strcmp(e->name, ".") ||
	    !strcmp(e->name, "..") ||
	    strchr(e->name, '/'))
		goto Einval;
	switch (*p++) {
		case 'E': e->flags = 1<<Enabled; break;
		case 'M': e->flags = (1<<Enabled) | (1<<Magic); break;
		default: goto Einval;
	}
	if (*p++ != del)
		goto Einval;
	if (test_bit(Magic, &e->flags)) {
		char *s = strchr(p, del);
		if (!s)
			goto Einval;
		*s++ = '\0';
		e->offset = simple_strtoul(p, &p, 10);
		if (*p++)
			goto Einval;
		e->magic = p;
		p = scanarg(p, del);
		if (!p)
			goto Einval;
		p[-1] = '\0';
		if (!e->magic[0])
			goto Einval;
		e->mask = p;
		p = scanarg(p, del);
		if (!p)
			goto Einval;
		p[-1] = '\0';
		if (!e->mask[0])
			e->mask = NULL;
		e->size = unquote(e->magic);
		if (e->mask && unquote(e->mask) != e->size)
			goto Einval;
		if (e->size + e->offset > BINPRM_BUF_SIZE)
			goto Einval;
	} else {
		p = strchr(p, del);
		if (!p)
			goto Einval;
		*p++ = '\0';
		e->magic = p;
		p = strchr(p, del);
		if (!p)
			goto Einval;
		*p++ = '\0';
		if (!e->magic[0] || strchr(e->magic, '/'))
			goto Einval;
		p = strchr(p, del);
		if (!p)
			goto Einval;
		*p++ = '\0';
	}
	e->interpreter = p;
	p = strchr(p, del);
	if (!p)
		goto Einval;
	*p++ = '\0';
	if (!e->interpreter[0])
		goto Einval;

	if (*p == '\n')
		p++;
	if (p != buf + count)
		goto Einval;
	return e;

out:
	return ERR_PTR(err);

Efault:
	kfree(e);
	return ERR_PTR(-EFAULT);
Einval:
	kfree(e);
	return ERR_PTR(-EINVAL);
}

/*
 * Set status of entry/binfmt_misc:
 * '1' enables, '0' disables and '-1' clears entry/binfmt_misc
 */
static int parse_command(const char *buffer, size_t count)
{
	char s[4];

	if (!count)
		return 0;
	if (count > 3)
		return -EINVAL;
	if (copy_from_user(s, buffer, count))
		return -EFAULT;
	if (s[count-1] == '\n')
		count--;
	if (count == 1 && s[0] == '0')
		return 1;
	if (count == 1 && s[0] == '1')
		return 2;
	if (count == 2 && s[0] == '-' && s[1] == '1')
		return 3;
	return -EINVAL;
}

/* generic stuff */

static void entry_status(Node *e, char *page)
{
	char *dp;
	char *status = "disabled";

	if (test_bit(Enabled, &e->flags))
		status = "enabled";

	if (!VERBOSE_STATUS) {
		sprintf(page, "%s\n", status);
		return;
	}

	sprintf(page, "%s\ninterpreter %s\n", status, e->interpreter);
	dp = page + strlen(page);
	if (!test_bit(Magic, &e->flags)) {
		sprintf(dp, "extension .%s\n", e->magic);
	} else {
		int i;

		sprintf(dp, "offset %i\nmagic ", e->offset);
		dp = page + strlen(page);
		for (i = 0; i < e->size; i++) {
			sprintf(dp, "%02x", 0xff & (int) (e->magic[i]));
			dp += 2;
		}
		if (e->mask) {
			sprintf(dp, "\nmask ");
			dp += 6;
			for (i = 0; i < e->size; i++) {
				sprintf(dp, "%02x", 0xff & (int) (e->mask[i]));
				dp += 2;
			}
		}
		*dp++ = '\n';
		*dp = '\0';
	}
}

static struct inode *bm_get_inode(struct super_block *sb, int mode)
{
	struct inode * inode = new_inode(sb);

	if (inode) {
		inode->i_mode = mode;
		inode->i_uid = 0;
		inode->i_gid = 0;
		inode->i_blksize = PAGE_CACHE_SIZE;
		inode->i_blocks = 0;
		inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	}
	return inode;
}

static void bm_clear_inode(struct inode *inode)
{
	Node *e = inode->u.generic_ip;

	if (e) {
		struct vfsmount *mnt;
		write_lock(&entries_lock);
		list_del(&e->list);
		mnt = bm_mnt;
		if (!--entry_count)
			bm_mnt = NULL;
		write_unlock(&entries_lock);
		kfree(e);
		mntput(mnt);
	}
}

static void kill_node(Node *e)
{
	struct dentry *dentry;

	write_lock(&entries_lock);
	dentry = e->dentry;
	if (dentry) {
		list_del_init(&e->list);
		e->dentry = NULL;
	}
	write_unlock(&entries_lock);

	if (dentry) {
		dentry->d_inode->i_nlink--;
		d_drop(dentry);
		dput(dentry);
	}
}

/* /<entry> */

static ssize_t
bm_entry_read(struct file * file, char * buf, size_t nbytes, loff_t *ppos)
{
	Node *e = file->f_dentry->d_inode->u.generic_ip;
	loff_t pos = *ppos;
	ssize_t res;
	char *page;
	int len;

	if (!(page = (char*) __get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	entry_status(e, page);
	len = strlen(page);

	res = -EINVAL;
	if (pos < 0)
		goto out;
	res = 0;
	if (pos >= len)
		goto out;
	if (len < pos + nbytes)
		nbytes = len - pos;
	res = -EFAULT;
	if (copy_to_user(buf, page + pos, nbytes))
		goto out;
	*ppos = pos + nbytes;
	res = nbytes;
out:
	free_page((unsigned long) page);
	return res;
}

static ssize_t bm_entry_write(struct file *file, const char *buffer,
				size_t count, loff_t *ppos)
{
	struct dentry *root;
	Node *e = file->f_dentry->d_inode->u.generic_ip;
	int res = parse_command(buffer, count);

	switch (res) {
		case 1: clear_bit(Enabled, &e->flags);
			break;
		case 2: set_bit(Enabled, &e->flags);
			break;
		case 3: root = dget(file->f_vfsmnt->mnt_sb->s_root);
			down(&root->d_inode->i_sem);

			kill_node(e);

			up(&root->d_inode->i_sem);
			dput(root);
			break;
		default: return res;
	}
	return count;
}

static struct file_operations bm_entry_operations = {
	read:		bm_entry_read,
	write:		bm_entry_write,
};

static struct file_system_type bm_fs_type;

/* /register */

static ssize_t bm_register_write(struct file *file, const char *buffer,
			       size_t count, loff_t *ppos)
{
	Node *e;
	struct inode *inode;
	struct vfsmount *mnt = NULL;
	struct dentry *root, *dentry;
	struct super_block *sb = file->f_vfsmnt->mnt_sb;
	int err = 0;

	e = create_entry(buffer, count);

	if (IS_ERR(e))
		return PTR_ERR(e);

	root = dget(sb->s_root);
	down(&root->d_inode->i_sem);
	dentry = lookup_one_len(e->name, root, strlen(e->name));
	err = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out;

	err = -EEXIST;
	if (dentry->d_inode)
		goto out2;

	inode = bm_get_inode(sb, S_IFREG | 0644);

	err = -ENOMEM;
	if (!inode)
		goto out2;

	write_lock(&entries_lock);
	if (!bm_mnt) {
		write_unlock(&entries_lock);
		mnt = kern_mount(&bm_fs_type);
		if (IS_ERR(mnt)) {
			err = PTR_ERR(mnt);
			iput(inode);
			inode = NULL;
			goto out2;
		}
		write_lock(&entries_lock);
		if (!bm_mnt)
			bm_mnt = mnt;
	}
	mntget(bm_mnt);
	entry_count++;

	e->dentry = dget(dentry);
	inode->u.generic_ip = e;
	inode->i_fop = &bm_entry_operations;
	d_instantiate(dentry, inode);

	list_add(&e->list, &entries);
	write_unlock(&entries_lock);

	mntput(mnt);
	err = 0;
out2:
	dput(dentry);
out:
	up(&root->d_inode->i_sem);
	dput(root);

	if (err) {
		kfree(e);
		return -EINVAL;
	}
	return count;
}

static struct file_operations bm_register_operations = {
	write:		bm_register_write,
};

/* /status */

static ssize_t
bm_status_read(struct file * file, char * buf, size_t nbytes, loff_t *ppos)
{
	char *s = enabled ? "enabled" : "disabled";
	int len = strlen(s);
	loff_t pos = *ppos;

	if (pos < 0)
		return -EINVAL;
	if (pos >= len)
		return 0;
	if (len < pos + nbytes)
		nbytes = len - pos;
	if (copy_to_user(buf, s + pos, nbytes))
		return -EFAULT;
	*ppos = pos + nbytes;
	return nbytes;
}

static ssize_t bm_status_write(struct file * file, const char * buffer,
		size_t count, loff_t *ppos)
{
	int res = parse_command(buffer, count);
	struct dentry *root;

	switch (res) {
		case 1: enabled = 0; break;
		case 2: enabled = 1; break;
		case 3: root = dget(file->f_vfsmnt->mnt_sb->s_root);
			down(&root->d_inode->i_sem);

			while (!list_empty(&entries))
				kill_node(list_entry(entries.next, Node, list));

			up(&root->d_inode->i_sem);
			dput(root);
		default: return res;
	}
	return count;
}

static struct file_operations bm_status_operations = {
	read:		bm_status_read,
	write:		bm_status_write,
};

/* Superblock handling */

static struct super_operations s_ops = {
	statfs:		simple_statfs,
	put_inode:	force_delete,
	clear_inode:	bm_clear_inode,
};

static int bm_fill_super(struct super_block * sb, void * data, int silent)
{
	struct qstr names[2] = {{name:"status"}, {name:"register"}};
	struct inode * inode;
	struct dentry * dentry[3];
	int i;

	for (i=0; i<sizeof(names)/sizeof(names[0]); i++) {
		names[i].len = strlen(names[i].name);
		names[i].hash = full_name_hash(names[i].name, names[i].len);
	}

	sb->s_blocksize = PAGE_CACHE_SIZE;
	sb->s_blocksize_bits = PAGE_CACHE_SHIFT;
	sb->s_magic = 0x42494e4d;
	sb->s_op = &s_ops;

	inode = bm_get_inode(sb, S_IFDIR | 0755);
	if (!inode)
		return -ENOMEM;
	inode->i_op = &simple_dir_inode_operations;
	inode->i_fop = &simple_dir_operations;
	dentry[0] = d_alloc_root(inode);
	if (!dentry[0]) {
		iput(inode);
		return -ENOMEM;
	}
	dentry[1] = d_alloc(dentry[0], &names[0]);
	if (!dentry[1])
		goto out1;
	dentry[2] = d_alloc(dentry[0], &names[1]);
	if (!dentry[2])
		goto out2;
	inode = bm_get_inode(sb, S_IFREG | 0644);
	if (!inode)
		goto out3;
	inode->i_fop = &bm_status_operations;
	d_add(dentry[1], inode);
	inode = bm_get_inode(sb, S_IFREG | 0400);
	if (!inode)
		goto out3;
	inode->i_fop = &bm_register_operations;
	d_add(dentry[2], inode);

	sb->s_root = dentry[0];
	return 0;

out3:
	dput(dentry[2]);
out2:
	dput(dentry[1]);
out1:
	dput(dentry[0]);
	return -ENOMEM;
}

static struct super_block *bm_get_sb(struct file_system_type *fs_type,
	int flags, char *dev_name, void *data)
{
	return get_sb_single(fs_type, flags, data, bm_fill_super);
}

static struct linux_binfmt misc_format = {
	module: THIS_MODULE,
	load_binary: load_misc_binary,
};

static struct file_system_type bm_fs_type = {
	owner:		THIS_MODULE,
	name:		"binfmt_misc",
	get_sb:		bm_get_sb,
	kill_sb:	kill_litter_super,
};

static int __init init_misc_binfmt(void)
{
	int err = register_filesystem(&bm_fs_type);
	if (!err) {
		err = register_binfmt(&misc_format);
		if (err)
			unregister_filesystem(&bm_fs_type);
	}
	return err;
}

static void __exit exit_misc_binfmt(void)
{
	unregister_binfmt(&misc_format);
	unregister_filesystem(&bm_fs_type);
}

EXPORT_NO_SYMBOLS;

module_init(init_misc_binfmt);
module_exit(exit_misc_binfmt);
MODULE_LICENSE("GPL");
