/*
 * linux/fs/nfsd/export.c
 *
 * NFS exporting and validation.
 *
 * We maintain a list of clients, each of which has a list of
 * exports. To export an fs to a given client, you first have
 * to create the client entry with NFSCTL_ADDCLIENT, which
 * creates a client control block and adds it to the hash
 * table. Then, you call NFSCTL_EXPORT for each fs.
 *
 * You cannot currently read the export information from the
 * kernel. It would be nice to have a /proc file though.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch, <okir@monad.swb.de>
 */

#include <linux/unistd.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/in.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/nfsfh.h>
#include <linux/nfsd/syscall.h>
#include <linux/lockd/bind.h>

#define NFSDDBG_FACILITY	NFSDDBG_EXPORT
#define NFSD_PARANOIA 1

typedef struct svc_client	svc_client;
typedef struct svc_export	svc_export;

static svc_export *	exp_find(svc_client *clp, kdev_t dev);
static svc_export *	exp_parent(svc_client *clp, kdev_t dev);
static void		exp_unexport_all(svc_client *clp);
static void		exp_do_unexport(svc_export *unexp);
static svc_client *	exp_getclientbyname(char *name);
static void		exp_freeclient(svc_client *clp);
static void		exp_unhashclient(svc_client *clp);
static int		exp_verify_string(char *cp, int max);

#define CLIENT_HASHBITS		6
#define CLIENT_HASHMAX		(1 << CLIENT_HASHBITS)
#define CLIENT_HASHMASK		(CLIENT_HASHMAX - 1)
#define CLIENT_HASH(a) \
		((((a)>>24) ^ ((a)>>16) ^ ((a)>>8) ^(a)) & CLIENT_HASHMASK)
/* XXX: is this adequate for 32bit kdev_t ? */
#define EXPORT_HASH(dev)	((dev) & (NFSCLNT_EXPMAX - 1))

struct svc_clnthash {
	struct svc_clnthash *	h_next;
	struct in_addr		h_addr;
	struct svc_client *	h_client;
};
static struct svc_clnthash *	clnt_hash[CLIENT_HASHMAX];
static svc_client *		clients = NULL;
static int			initialized = 0;

static int			hash_lock = 0;
static int			want_lock = 0;
static int			hash_count = 0;
static struct wait_queue *	hash_wait = NULL;

#define READLOCK		0
#define WRITELOCK		1

/*
 * Find a client's export for a device.
 */
static inline svc_export *
exp_find(svc_client *clp, kdev_t dev)
{
	svc_export *	exp;

	exp = clp->cl_export[EXPORT_HASH(dev)];
	while (exp && exp->ex_dev != dev)
		exp = exp->ex_next;
	return exp;
}

/*
 * Find the client's export entry matching xdev/xino.
 */
svc_export *
exp_get(svc_client *clp, kdev_t dev, ino_t ino)
{
	svc_export *	exp;

	if (!clp)
		return NULL;
	exp = exp_find(clp, dev);
	return (exp && exp->ex_ino == ino)? exp : NULL;
}

/*
 * Check whether there are any exports for a device.
 */
static int
exp_device_in_use(kdev_t dev)
{
	struct svc_client *clp;

	for (clp = clients; clp; clp = clp->cl_next) {
		if (exp_find(clp, dev))
			return 1;
	}
	return 0;
}

/*
 * Look up the device of the parent fs.
 */
static inline int
nfsd_parentdev(kdev_t *devp)
{
	struct super_block	*sb;

	if (!(sb = get_super(*devp)) || !sb->s_root->d_covers)
		return 0;
	if (*devp == sb->s_root->d_covers->d_inode->i_dev)
		return 0;
	*devp = sb->s_root->d_covers->d_inode->i_dev;
	return 1;
}

/*
 * Find the parent export entry for a given fs. This function is used
 * only by the export syscall to keep the export tree consistent.
 */
static svc_export *
exp_parent(svc_client *clp, kdev_t dev)
{
	svc_export	*exp;
	kdev_t		xdev = dev;

	do {
		exp = exp_find(clp, xdev);
		if (exp)
			return exp;
	} while (nfsd_parentdev(&xdev));

	return NULL;
}

/*
 * Export a file system.
 */
int
exp_export(struct nfsctl_export *nxp)
{
	svc_client	*clp;
	svc_export	*exp, *parent;
	svc_export	**head;
	struct dentry	*dentry = NULL;
	struct inode	*inode = NULL;
	int		i, err;
	kdev_t		dev;
	ino_t		ino;

	/* Consistency check */
	if (!exp_verify_string(nxp->ex_path, NFS_MAXPATHLEN) ||
	    !exp_verify_string(nxp->ex_client, NFSCLNT_IDMAX))
		return -EINVAL;

	dprintk("exp_export called for %s:%s (%x/%ld fl %x).\n",
			nxp->ex_client, nxp->ex_path,
			nxp->ex_dev, nxp->ex_ino, nxp->ex_flags);
	dev = to_kdev_t(nxp->ex_dev);
	ino = nxp->ex_ino;

	/* Try to lock the export table for update */
	if ((err = exp_writelock()) < 0)
		goto out;

	/* Look up client info */
	err = -EINVAL;
	if (!(clp = exp_getclientbyname(nxp->ex_client)))
		goto out_unlock;

	/*
	 * If there's already an export for this file, assume this
	 * is just a flag update.
	 */
	if ((exp = exp_find(clp, dev)) != NULL) {
		/* Ensure there's only one export per FS. */
		err = -EPERM;
		if (exp->ex_ino == ino) {
			exp->ex_flags    = nxp->ex_flags;
			exp->ex_anon_uid = nxp->ex_anon_uid;
			exp->ex_anon_gid = nxp->ex_anon_gid;
			err = 0;
		}
		goto out_unlock;
	}

	/* Look up the dentry */
	err = -EINVAL;
	dentry = lookup_dentry(nxp->ex_path, NULL, 0);
	if (IS_ERR(dentry))
		goto out_unlock;

	err = -ENOENT;
	inode = dentry->d_inode;
	if(!inode)
		goto finish;
	err = -EINVAL;
	if(inode->i_dev != dev || inode->i_ino != nxp->ex_ino) {

		printk(KERN_DEBUG "exp_export: i_dev = %x, dev = %x\n",
			inode->i_dev, dev); 
		/* I'm just being paranoid... */
		goto finish;
	}

	/* We currently export only dirs. */
	err = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode))
		goto finish;

	/* If this is a sub-export, must be root of FS */
	err = -EINVAL;
	if ((parent = exp_parent(clp, dev)) != NULL) {
		struct super_block *sb = inode->i_sb;

		if (inode != sb->s_root->d_inode) {
#ifdef NFSD_PARANOIA
printk("exp_export: sub-export %s not root of device %s\n",
nxp->ex_path, kdevname(sb->s_dev));
#endif
			goto finish;
		}
	}

	err = -ENOMEM;
	if (!(exp = kmalloc(sizeof(*exp), GFP_USER)))
		goto finish;
	dprintk("nfsd: created export entry %p for client %p\n", exp, clp);

	strcpy(exp->ex_path, nxp->ex_path);
	exp->ex_client = clp;
	exp->ex_parent = parent;
	exp->ex_dentry = dentry;
	exp->ex_flags = nxp->ex_flags;
	exp->ex_dev = dev;
	exp->ex_ino = ino;
	exp->ex_anon_uid = nxp->ex_anon_uid;
	exp->ex_anon_gid = nxp->ex_anon_gid;

	/* Update parent pointers of all exports */
	if (parent) {
		for (i = 0; i < NFSCLNT_EXPMAX; i++) {
			svc_export *temp = clp->cl_export[i];

			while (temp) {
				if (temp->ex_parent == parent)
					temp->ex_parent = exp;
				temp = temp->ex_next;
			}
		}
	}

	head = clp->cl_export + EXPORT_HASH(dev);
	exp->ex_next = *head;
	*head = exp;

	err = 0;

	/* Unlock hashtable */
out_unlock:
	exp_unlock();
out:
	return err;

	/* Release the dentry */
finish:
	dput(dentry);
	goto out_unlock;
}

/*
 * Unexport a file system. The export entry has already
 * been removed from the client's list of exported fs's.
 */
static void
exp_do_unexport(svc_export *unexp)
{
	svc_export	*exp;
	svc_client	*clp;
	struct dentry	*dentry;
	struct inode	*inode;
	int		i;

	/* Update parent pointers. */
	clp = unexp->ex_client;
	for (i = 0; i < NFSCLNT_EXPMAX; i++) {
		for (exp = clp->cl_export[i]; exp; exp = exp->ex_next)
			if (exp->ex_parent == unexp)
				exp->ex_parent = unexp->ex_parent;
	}

	/*
	 * Check whether this is the last export for this device,
	 * and if so flush any cached dentries.
	 */
	if (!exp_device_in_use(unexp->ex_dev)) {
printk("exp_do_unexport: %s last use, flushing cache\n",
kdevname(unexp->ex_dev));
		nfsd_fh_flush(unexp->ex_dev);
	}

	dentry = unexp->ex_dentry;
	inode = dentry->d_inode;
	if (unexp->ex_dev != inode->i_dev || unexp->ex_ino != inode->i_ino)
		printk(KERN_WARNING "nfsd: bad dentry in unexport!\n");
	dput(dentry);

	kfree(unexp);
}

/*
 * Revoke all exports for a given client.
 * This may look very awkward, but we have to do it this way in order
 * to avoid race conditions (aka mind the parent pointer).
 */
static void
exp_unexport_all(svc_client *clp)
{
	svc_export	*exp;
	int		i;

	dprintk("unexporting all fs's for clnt %p\n", clp);
	for (i = 0; i < NFSCLNT_EXPMAX; i++) {
		exp = clp->cl_export[i];
		clp->cl_export[i] = NULL;
		while (exp) {
			svc_export *next = exp->ex_next;
			exp_do_unexport(exp);
			exp = next;
		}
	}
}

/*
 * unexport syscall.
 */
int
exp_unexport(struct nfsctl_export *nxp)
{
	svc_client	*clp;
	svc_export	**expp, *exp = NULL;
	int		err;

	/* Consistency check */
	if (!exp_verify_string(nxp->ex_client, NFSCLNT_IDMAX))
		return -EINVAL;

	if ((err = exp_writelock()) < 0)
		goto out;

	err = -EINVAL;
	clp = exp_getclientbyname(nxp->ex_client);
	if (clp) {
printk("exp_unexport: found client %s\n", nxp->ex_client);
		expp = clp->cl_export + EXPORT_HASH(nxp->ex_dev);
		while ((exp = *expp) != NULL) {
			if (exp->ex_dev == nxp->ex_dev) {
				if (exp->ex_ino != nxp->ex_ino) {
printk("exp_unexport: ino mismatch, %ld not %ld\n", exp->ex_ino, nxp->ex_ino);
					break;
				}
				*expp = exp->ex_next;
				exp_do_unexport(exp);
				err = 0;
				break;
			}
			expp = &(exp->ex_next);
		}
	}

	exp_unlock();
out:
	return err;
}

/*
 * Obtain the root fh on behalf of a client.
 * This could be done in user space, but I feel that it adds some safety
 * since its harder to fool a kernel module than a user space program.
 */
int
exp_rootfh(struct svc_client *clp, kdev_t dev, ino_t ino, struct knfs_fh *f)
{
	struct svc_export	*exp;
	struct dentry		*dentry;
	struct inode		*inode;
	struct svc_fh		fh;

	dprintk("nfsd: exp_rootfh(%s:%x/%ld)\n", clp->cl_ident, dev, ino);

	exp = exp_get(clp, dev, ino);
	if (!exp)
		return -EPERM;

	dentry = exp->ex_dentry;
	inode = dentry->d_inode;
	if(!inode) {
		printk("exp_rootfh: Aieee, NULL d_inode\n");
		return -EPERM;
	}
	if(inode->i_dev != dev || inode->i_ino != ino) {
		printk("exp_rootfh: Aieee, ino/dev mismatch\n");
		printk("exp_rootfh: arg[dev(%x):ino(%ld)] inode[dev(%x):ino(%ld)]\n",
		       dev, ino, inode->i_dev, inode->i_ino);
	}

	/*
	 * fh must be initialized before calling fh_compose
	 */
	fh_init(&fh);
	fh_compose(&fh, exp, dget(dentry));
	memcpy(f, &fh.fh_handle, sizeof(struct knfs_fh));
	fh_put(&fh);

	return 0;
}

/*
 * Hashtable locking. Write locks are placed only by user processes
 * wanting to modify export information.
 */
void
exp_readlock(void)
{
	while (hash_lock || want_lock)
		sleep_on(&hash_wait);
	hash_count++;
}

int
exp_writelock(void)
{
	/* fast track */
	if (!hash_count && !hash_lock) {
	lock_it:
		hash_lock = 1;
		return 0;
	}

	current->sigpending = 0;
	want_lock++;
	while (hash_count || hash_lock) {
		interruptible_sleep_on(&hash_wait);
		if (signal_pending(current))
			break;
	}
	want_lock--;

	/* restore the task's signals */
	spin_lock_irq(&current->sigmask_lock);
	recalc_sigpending(current);
	spin_unlock_irq(&current->sigmask_lock);

	if (!hash_count && !hash_lock)
		goto lock_it;
	return -EINTR;
}

void
exp_unlock(void)
{
	if (!hash_count && !hash_lock)
		printk(KERN_WARNING "exp_unlock: not locked!\n");
	if (hash_count)
		hash_count--;
	else
		hash_lock = 0;
	wake_up(&hash_wait);
}

/*
 * Find a valid client given an inet address. We always move the most
 * recently used client to the front of the hash chain to speed up
 * future lookups.
 * Locking against other processes is the responsibility of the caller.
 */
struct svc_client *
exp_getclient(struct sockaddr_in *sin)
{
	struct svc_clnthash	**hp, **head, *tmp;
	unsigned long		addr = sin->sin_addr.s_addr;

	if (!initialized)
		return NULL;

	head = &clnt_hash[CLIENT_HASH(addr)];

	for (hp = head; (tmp = *hp) != NULL; hp = &(tmp->h_next)) {
		if (tmp->h_addr.s_addr == addr) {
			/* Move client to the front */
			if (head != hp) {
				*hp = tmp->h_next;
				tmp->h_next = *head;
				*head = tmp;
			}

			return tmp->h_client;
		}
	}

	return NULL;
}

/*
 * Find a client given its identifier.
 */
static svc_client *
exp_getclientbyname(char *ident)
{
	svc_client *	clp;

	for (clp = clients; clp; clp = clp->cl_next) {
		if (!strcmp(clp->cl_ident, ident))
			return clp;
	}
	return NULL;
}

/*
 * Add or modify a client.
 * Change requests may involve the list of host addresses. The list of
 * exports and possibly existing uid maps are left untouched.
 */
int
exp_addclient(struct nfsctl_client *ncp)
{
	struct svc_clnthash *	ch[NFSCLNT_ADDRMAX];
	svc_client *		clp;
	int			i, err, change = 0, ilen;

	/* First, consistency check. */
	err = -EINVAL;
	if (!(ilen = exp_verify_string(ncp->cl_ident, NFSCLNT_IDMAX)))
		goto out;
	if (ncp->cl_naddr > NFSCLNT_ADDRMAX)
		goto out;

	/* Lock the hashtable */
	if ((err = exp_writelock()) < 0)
		goto out;

	/* First check if this is a change request for a client. */
	for (clp = clients; clp; clp = clp->cl_next)
		if (!strcmp(clp->cl_ident, ncp->cl_ident))
			break;

	err = -ENOMEM;
	if (clp) {
		change = 1;
	} else {
		if (!(clp = kmalloc(sizeof(*clp), GFP_KERNEL)))
			goto out_unlock;
		memset(clp, 0, sizeof(*clp));

		dprintk("created client %s (%p)\n", ncp->cl_ident, clp);

		strcpy(clp->cl_ident, ncp->cl_ident);
		clp->cl_idlen = ilen;
	}

	/* Allocate hash buckets */
	for (i = 0; i < ncp->cl_naddr; i++) {
		ch[i] = kmalloc(sizeof(struct svc_clnthash), GFP_KERNEL);
		if (!ch[i]) {
			while (i--)
				kfree(ch[i]);
			if (!change)
				kfree(clp);
			goto out_unlock;
		}
	}

	/* Copy addresses. */
	for (i = 0; i < ncp->cl_naddr; i++) {
		clp->cl_addr[i] = ncp->cl_addrlist[i];
	}
	clp->cl_naddr = ncp->cl_naddr;

	/* Remove old client hash entries. */
	if (change)
		exp_unhashclient(clp);

	/* Insert client into hashtable. */
	for (i = 0; i < ncp->cl_naddr; i++) {
		struct in_addr	addr = clp->cl_addr[i];
		int		hash;

		hash = CLIENT_HASH(addr.s_addr);
		ch[i]->h_client = clp;
		ch[i]->h_addr = addr;
		ch[i]->h_next = clnt_hash[hash];
		clnt_hash[hash] = ch[i];
	}

	if (!change) {
		clp->cl_next = clients;
		clients = clp;
	}
	err = 0;

out_unlock:
	exp_unlock();
out:
	return err;
}

/*
 * Delete a client given an identifier.
 */
int
exp_delclient(struct nfsctl_client *ncp)
{
	svc_client	**clpp, *clp;
	int		err;

	err = -EINVAL;
	if (!exp_verify_string(ncp->cl_ident, NFSCLNT_IDMAX))
		goto out;

	/* Lock the hashtable */
	if ((err = exp_writelock()) < 0)
		goto out;

	err = -EINVAL;
	for (clpp = &clients; (clp = *clpp); clpp = &(clp->cl_next))
		if (!strcmp(ncp->cl_ident, clp->cl_ident))
			break;

	if (clp) {
		*clpp = clp->cl_next;
		exp_freeclient(clp);
		err = 0;
	}

	exp_unlock();
out:
	return err;
}

/*
 * Free a client. The caller has already removed it from the client list.
 */
static void
exp_freeclient(svc_client *clp)
{
	exp_unhashclient(clp);

	/* umap_free(&(clp->cl_umap)); */
	exp_unexport_all(clp);
	nfsd_lockd_unexport(clp);
	kfree (clp);
}

/*
 * Remove client from hashtable. We first collect all hashtable
 * entries and free them in one go.
 * The hash table must be writelocked by the caller.
 */
static void
exp_unhashclient(svc_client *clp)
{
	struct svc_clnthash	**hpp, *hp, *ch[NFSCLNT_ADDRMAX];
	int			i, count, err;

again:
	err = 0;
	for (i = 0, count = 0; i < CLIENT_HASHMAX && !err; i++) {
		hpp = clnt_hash + i;
		while ((hp = *hpp) && !err) {
			if (hp->h_client == clp) {
				*hpp = hp->h_next;
				ch[count++] = hp;
				err = (count >= NFSCLNT_ADDRMAX);
			} else {
				hpp = &(hp->h_next);
			}
		}
	}
	if (count != clp->cl_naddr)
		printk(KERN_WARNING "nfsd: bad address count in freeclient!\n");
	if (err)
		goto again;
	for (i = 0; i < count; i++)
		kfree (ch[i]);
}

/*
 * Lockd is shutting down and tells us to unregister all clients
 */
void
exp_nlmdetach(void)
{
	struct svc_client	*clp;

	for (clp = clients; clp; clp = clp->cl_next)
		nfsd_lockd_unexport(clp);
}

/*
 * Verify that string is non-empty and does not exceed max length.
 */
static int
exp_verify_string(char *cp, int max)
{
	int	i;

	for (i = 0; i < max; i++)
		if (!cp[i])
			return i;
	cp[i] = 0;
	printk(KERN_NOTICE "nfsd: couldn't validate string %s\n", cp);
	return 0;
}

/*
 * Initialize the exports module.
 */
void
nfsd_export_init(void)
{
	int		i;

	dprintk("nfsd: initializing export module.\n");
	if (initialized)
		return;
	for (i = 0; i < CLIENT_HASHMAX; i++)
		clnt_hash[i] = NULL;
	clients = NULL;

	initialized = 1;
}

/*
 * Shutdown the exports module.
 */
void
nfsd_export_shutdown(void)
{
	int	i;

	dprintk("nfsd: shutting down export module.\n");
	if (!initialized)
		return;
	if (exp_writelock() < 0) {
		printk(KERN_WARNING "Weird: hashtable locked in exp_shutdown");
		return;
	}
	for (i = 0; i < CLIENT_HASHMAX; i++) {
		while (clnt_hash[i])
			exp_freeclient(clnt_hash[i]->h_client);
	}
	clients = NULL; /* we may be restarted before the module unloads */
	
	exp_unlock();
	dprintk("nfsd: export shutdown complete.\n");
}
