/*
 * linux/fs/nfsd/nfscache.c
 *
 * Request reply cache. This is currently a global cache, but this may
 * change in the future and be a per-client cache.
 *
 * This code is heavily inspired by the 44BSD implementation, although
 * it does things a bit differently.
 *
 * Copyright (C) 1995, 1996 Olaf Kirch <okir@monad.swb.de>
 */

#include <linux/kernel.h>
#include <linux/time.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>

#include <linux/sunrpc/svc.h>
#include <linux/nfsd/nfsd.h>
#include <linux/nfsd/cache.h>

/* Size of reply cache. Common values are:
 * 4.3BSD:	128
 * 4.4BSD:	256
 * Solaris2:	1024
 * DEC Unix:	512-4096
 */
#define CACHESIZE		1024
#define HASHSIZE		64
#define REQHASH(xid)		((((xid) >> 24) ^ (xid)) & (HASHSIZE-1))

struct nfscache_head {
	struct svc_cacherep *	next;
	struct svc_cacherep *	prev;
};

static struct nfscache_head *	hash_list;
static struct svc_cacherep *	lru_head;
static struct svc_cacherep *	lru_tail;
static struct svc_cacherep *	nfscache;
static int			cache_disabled = 1;

static int	nfsd_cache_append(struct svc_rqst *rqstp, struct kvec *vec);

/* 
 * locking for the reply cache:
 * A cache entry is "single use" if c_state == RC_INPROG
 * Otherwise, it when accessing _prev or _next, the lock must be held.
 */
static DEFINE_SPINLOCK(cache_lock);

void
nfsd_cache_init(void)
{
	struct svc_cacherep	*rp;
	struct nfscache_head	*rh;
	size_t			i;
	unsigned long		order;


	i = CACHESIZE * sizeof (struct svc_cacherep);
	for (order = 0; (PAGE_SIZE << order) < i; order++)
		;
	nfscache = (struct svc_cacherep *)
		__get_free_pages(GFP_KERNEL, order);
	if (!nfscache) {
		printk (KERN_ERR "nfsd: cannot allocate %Zd bytes for reply cache\n", i);
		return;
	}
	memset(nfscache, 0, i);

	i = HASHSIZE * sizeof (struct nfscache_head);
	hash_list = kmalloc (i, GFP_KERNEL);
	if (!hash_list) {
		free_pages ((unsigned long)nfscache, order);
		nfscache = NULL;
		printk (KERN_ERR "nfsd: cannot allocate %Zd bytes for hash list\n", i);
		return;
	}

	for (i = 0, rh = hash_list; i < HASHSIZE; i++, rh++)
		rh->next = rh->prev = (struct svc_cacherep *) rh;

	for (i = 0, rp = nfscache; i < CACHESIZE; i++, rp++) {
		rp->c_state = RC_UNUSED;
		rp->c_type = RC_NOCACHE;
		rp->c_hash_next =
		rp->c_hash_prev = rp;
		rp->c_lru_next = rp + 1;
		rp->c_lru_prev = rp - 1;
	}
	lru_head = nfscache;
	lru_tail = nfscache + CACHESIZE - 1;
	lru_head->c_lru_prev = NULL;
	lru_tail->c_lru_next = NULL;

	cache_disabled = 0;
}

void
nfsd_cache_shutdown(void)
{
	struct svc_cacherep	*rp;
	size_t			i;
	unsigned long		order;

	for (rp = lru_head; rp; rp = rp->c_lru_next) {
		if (rp->c_state == RC_DONE && rp->c_type == RC_REPLBUFF)
			kfree(rp->c_replvec.iov_base);
	}

	cache_disabled = 1;

	i = CACHESIZE * sizeof (struct svc_cacherep);
	for (order = 0; (PAGE_SIZE << order) < i; order++)
		;
	free_pages ((unsigned long)nfscache, order);
	nfscache = NULL;
	kfree (hash_list);
	hash_list = NULL;
}

/*
 * Move cache entry to front of LRU list
 */
static void
lru_put_front(struct svc_cacherep *rp)
{
	struct svc_cacherep	*prev = rp->c_lru_prev,
				*next = rp->c_lru_next;

	if (prev)
		prev->c_lru_next = next;
	else
		lru_head = next;
	if (next)
		next->c_lru_prev = prev;
	else
		lru_tail = prev;

	rp->c_lru_next = lru_head;
	rp->c_lru_prev = NULL;
	if (lru_head)
		lru_head->c_lru_prev = rp;
	lru_head = rp;
}

/*
 * Move a cache entry from one hash list to another
 */
static void
hash_refile(struct svc_cacherep *rp)
{
	struct svc_cacherep	*prev = rp->c_hash_prev,
				*next = rp->c_hash_next;
	struct nfscache_head	*head = hash_list + REQHASH(rp->c_xid);

	prev->c_hash_next = next;
	next->c_hash_prev = prev;

	rp->c_hash_next = head->next;
	rp->c_hash_prev = (struct svc_cacherep *) head;
	head->next->c_hash_prev = rp;
	head->next = rp;
}

/*
 * Try to find an entry matching the current call in the cache. When none
 * is found, we grab the oldest unlocked entry off the LRU list.
 * Note that no operation within the loop may sleep.
 */
int
nfsd_cache_lookup(struct svc_rqst *rqstp, int type)
{
	struct svc_cacherep	*rh, *rp;
	u32			xid = rqstp->rq_xid,
				proto =  rqstp->rq_prot,
				vers = rqstp->rq_vers,
				proc = rqstp->rq_proc;
	unsigned long		age;
	int rtn;

	rqstp->rq_cacherep = NULL;
	if (cache_disabled || type == RC_NOCACHE) {
		nfsdstats.rcnocache++;
		return RC_DOIT;
	}

	spin_lock(&cache_lock);
	rtn = RC_DOIT;

	rp = rh = (struct svc_cacherep *) &hash_list[REQHASH(xid)];
	while ((rp = rp->c_hash_next) != rh) {
		if (rp->c_state != RC_UNUSED &&
		    xid == rp->c_xid && proc == rp->c_proc &&
		    proto == rp->c_prot && vers == rp->c_vers &&
		    time_before(jiffies, rp->c_timestamp + 120*HZ) &&
		    memcmp((char*)&rqstp->rq_addr, (char*)&rp->c_addr, sizeof(rp->c_addr))==0) {
			nfsdstats.rchits++;
			goto found_entry;
		}
	}
	nfsdstats.rcmisses++;

	/* This loop shouldn't take more than a few iterations normally */
	{
	int	safe = 0;
	for (rp = lru_tail; rp; rp = rp->c_lru_prev) {
		if (rp->c_state != RC_INPROG)
			break;
		if (safe++ > CACHESIZE) {
			printk("nfsd: loop in repcache LRU list\n");
			cache_disabled = 1;
			goto out;
		}
	}
	}

	/* This should not happen */
	if (rp == NULL) {
		static int	complaints;

		printk(KERN_WARNING "nfsd: all repcache entries locked!\n");
		if (++complaints > 5) {
			printk(KERN_WARNING "nfsd: disabling repcache.\n");
			cache_disabled = 1;
		}
		goto out;
	}

	rqstp->rq_cacherep = rp;
	rp->c_state = RC_INPROG;
	rp->c_xid = xid;
	rp->c_proc = proc;
	rp->c_addr = rqstp->rq_addr;
	rp->c_prot = proto;
	rp->c_vers = vers;
	rp->c_timestamp = jiffies;

	hash_refile(rp);

	/* release any buffer */
	if (rp->c_type == RC_REPLBUFF) {
		kfree(rp->c_replvec.iov_base);
		rp->c_replvec.iov_base = NULL;
	}
	rp->c_type = RC_NOCACHE;
 out:
	spin_unlock(&cache_lock);
	return rtn;

found_entry:
	/* We found a matching entry which is either in progress or done. */
	age = jiffies - rp->c_timestamp;
	rp->c_timestamp = jiffies;
	lru_put_front(rp);

	rtn = RC_DROPIT;
	/* Request being processed or excessive rexmits */
	if (rp->c_state == RC_INPROG || age < RC_DELAY)
		goto out;

	/* From the hall of fame of impractical attacks:
	 * Is this a user who tries to snoop on the cache? */
	rtn = RC_DOIT;
	if (!rqstp->rq_secure && rp->c_secure)
		goto out;

	/* Compose RPC reply header */
	switch (rp->c_type) {
	case RC_NOCACHE:
		break;
	case RC_REPLSTAT:
		svc_putu32(&rqstp->rq_res.head[0], rp->c_replstat);
		rtn = RC_REPLY;
		break;
	case RC_REPLBUFF:
		if (!nfsd_cache_append(rqstp, &rp->c_replvec))
			goto out;	/* should not happen */
		rtn = RC_REPLY;
		break;
	default:
		printk(KERN_WARNING "nfsd: bad repcache type %d\n", rp->c_type);
		rp->c_state = RC_UNUSED;
	}

	goto out;
}

/*
 * Update a cache entry. This is called from nfsd_dispatch when
 * the procedure has been executed and the complete reply is in
 * rqstp->rq_res.
 *
 * We're copying around data here rather than swapping buffers because
 * the toplevel loop requires max-sized buffers, which would be a waste
 * of memory for a cache with a max reply size of 100 bytes (diropokres).
 *
 * If we should start to use different types of cache entries tailored
 * specifically for attrstat and fh's, we may save even more space.
 *
 * Also note that a cachetype of RC_NOCACHE can legally be passed when
 * nfsd failed to encode a reply that otherwise would have been cached.
 * In this case, nfsd_cache_update is called with statp == NULL.
 */
void
nfsd_cache_update(struct svc_rqst *rqstp, int cachetype, u32 *statp)
{
	struct svc_cacherep *rp;
	struct kvec	*resv = &rqstp->rq_res.head[0], *cachv;
	int		len;

	if (!(rp = rqstp->rq_cacherep) || cache_disabled)
		return;

	len = resv->iov_len - ((char*)statp - (char*)resv->iov_base);
	len >>= 2;
	
	/* Don't cache excessive amounts of data and XDR failures */
	if (!statp || len > (256 >> 2)) {
		rp->c_state = RC_UNUSED;
		return;
	}

	switch (cachetype) {
	case RC_REPLSTAT:
		if (len != 1)
			printk("nfsd: RC_REPLSTAT/reply len %d!\n",len);
		rp->c_replstat = *statp;
		break;
	case RC_REPLBUFF:
		cachv = &rp->c_replvec;
		cachv->iov_base = kmalloc(len << 2, GFP_KERNEL);
		if (!cachv->iov_base) {
			spin_lock(&cache_lock);
			rp->c_state = RC_UNUSED;
			spin_unlock(&cache_lock);
			return;
		}
		cachv->iov_len = len << 2;
		memcpy(cachv->iov_base, statp, len << 2);
		break;
	}
	spin_lock(&cache_lock);
	lru_put_front(rp);
	rp->c_secure = rqstp->rq_secure;
	rp->c_type = cachetype;
	rp->c_state = RC_DONE;
	rp->c_timestamp = jiffies;
	spin_unlock(&cache_lock);
	return;
}

/*
 * Copy cached reply to current reply buffer. Should always fit.
 * FIXME as reply is in a page, we should just attach the page, and
 * keep a refcount....
 */
static int
nfsd_cache_append(struct svc_rqst *rqstp, struct kvec *data)
{
	struct kvec	*vec = &rqstp->rq_res.head[0];

	if (vec->iov_len + data->iov_len > PAGE_SIZE) {
		printk(KERN_WARNING "nfsd: cached reply too large (%Zd).\n",
				data->iov_len);
		return 0;
	}
	memcpy((char*)vec->iov_base + vec->iov_len, data->iov_base, data->iov_len);
	vec->iov_len += data->iov_len;
	return 1;
}
