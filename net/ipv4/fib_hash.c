/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		IPv4 FIB: lookup engine and maintenance routines.
 *
 * Version:	$Id: fib_hash.c,v 1.3 1998/03/08 05:56:16 davem Exp $
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/errno.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/proc_fs.h>
#include <linux/skbuff.h>
#include <linux/netlink.h>
#include <linux/init.h>

#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/sock.h>
#include <net/ip_fib.h>

#define FTprint(a...) 
/*
printk(KERN_DEBUG a)
 */

/*
   These bizarre types are just to force strict type checking.
   When I reversed order of bytes and changed to natural mask lengths,
   I forgot to make fixes in several places. Now I am lazy to return
   it back.
 */

typedef struct {
	u32	datum;
} fn_key_t;

typedef struct {
	u32	datum;
} fn_hash_idx_t;

struct fib_node
{
	struct fib_node		*fn_next;
	struct fib_info		*fn_info;
#define FIB_INFO(f)	((f)->fn_info)
	fn_key_t		fn_key;
	u8			fn_tos;
	u8			fn_type;
	u8			fn_scope;
	u8			fn_state;
};

#define FN_S_ZOMBIE	1
#define FN_S_ACCESSED	2

static int fib_hash_zombies;

struct fn_zone
{
	struct fn_zone	*fz_next;	/* Next not empty zone	*/
	struct fib_node	**fz_hash;	/* Hash table pointer	*/
	int		fz_nent;	/* Number of entries	*/

	int		fz_divisor;	/* Hash divisor		*/
	u32		fz_hashmask;	/* (1<<fz_divisor) - 1	*/
#define FZ_HASHMASK(fz)	((fz)->fz_hashmask)

	int		fz_order;	/* Zone order		*/
	u32		fz_mask;
#define FZ_MASK(fz)	((fz)->fz_mask)
};

/* NOTE. On fast computers evaluation of fz_hashmask and fz_mask
   can be cheaper than memory lookup, so that FZ_* macros are used.
 */

struct fn_hash
{
	struct fn_zone	*fn_zones[33];
	struct fn_zone	*fn_zone_list;
};

static __inline__ fn_hash_idx_t fn_hash(fn_key_t key, struct fn_zone *fz)
{
	u32 h = ntohl(key.datum)>>(32 - fz->fz_order);
	h ^= (h>>20);
	h ^= (h>>10);
	h ^= (h>>5);
	h &= FZ_HASHMASK(fz);
	return *(fn_hash_idx_t*)&h;
}

#define fz_key_0(key)		((key).datum = 0)
#define fz_prefix(key,fz)	((key).datum)

static __inline__ fn_key_t fz_key(u32 dst, struct fn_zone *fz)
{
	fn_key_t k;
	k.datum = dst & FZ_MASK(fz);
	return k;
}

static __inline__ struct fib_node ** fz_chain_p(fn_key_t key, struct fn_zone *fz)
{
	return &fz->fz_hash[fn_hash(key, fz).datum];
}

static __inline__ struct fib_node * fz_chain(fn_key_t key, struct fn_zone *fz)
{
	return fz->fz_hash[fn_hash(key, fz).datum];
}

extern __inline__ int fn_key_eq(fn_key_t a, fn_key_t b)
{
	return a.datum == b.datum;
}

#define FZ_MAX_DIVISOR 1024

#ifdef CONFIG_IP_ROUTE_LARGE_TABLES

static __inline__ void fn_rebuild_zone(struct fn_zone *fz,
					struct fib_node **old_ht,
					int old_divisor)
{
	int i;
	struct fib_node *f, **fp, *next;

	for (i=0; i<old_divisor; i++) {
		for (f=old_ht[i]; f; f=next) {
			next = f->fn_next;
			f->fn_next = NULL;
			for (fp = fz_chain_p(f->fn_key, fz); *fp; fp = &(*fp)->fn_next)
				/* NONE */;
			*fp = f;
		}
	}
}

static void fn_rehash_zone(struct fn_zone *fz)
{
	struct fib_node **ht, **old_ht;
	int old_divisor, new_divisor;
	u32 new_hashmask;
		
	old_divisor = fz->fz_divisor;

	switch (old_divisor) {
	case 16:
		new_divisor = 256;
		new_hashmask = 0xFF;
		break;
	case 256:
		new_divisor = 1024;
		new_hashmask = 0x3FF;
		break;
	default:
		printk(KERN_CRIT "route.c: bad divisor %d!\n", old_divisor);
		return;
	}
#if RT_CACHE_DEBUG >= 2
	printk("fn_rehash_zone: hash for zone %d grows from %d\n", fz->fz_order, old_divisor);
#endif

	ht = kmalloc(new_divisor*sizeof(struct fib_node*), GFP_KERNEL);

	if (ht)	{
		memset(ht, 0, new_divisor*sizeof(struct fib_node*));
		start_bh_atomic();
		old_ht = fz->fz_hash;
		fz->fz_hash = ht;
		fz->fz_hashmask = new_hashmask;
		fz->fz_divisor = new_divisor;
		fn_rebuild_zone(fz, old_ht, old_divisor);
		end_bh_atomic();
		kfree(old_ht);
FTprint("REHASHED ZONE: order %d mask %08x hash %d/%08x\n", fz->fz_order, fz->fz_mask, fz->fz_divisor, fz->fz_hashmask);
	}
}
#endif /* CONFIG_IP_ROUTE_LARGE_TABLES */

static void fn_free_node(struct fib_node * f)
{
	fib_release_info(FIB_INFO(f));
	kfree_s(f, sizeof(struct fib_node));
}


static struct fn_zone *
fn_new_zone(struct fn_hash *table, int z)
{
	int i;
	struct fn_zone *fz = kmalloc(sizeof(struct fn_zone), GFP_KERNEL);
	if (!fz)
		return NULL;

	memset(fz, 0, sizeof(struct fn_zone));
	if (z) {
		fz->fz_divisor = 16;
		fz->fz_hashmask = 0xF;
	} else {
		fz->fz_divisor = 1;
		fz->fz_hashmask = 0;
	}
	fz->fz_hash = kmalloc(fz->fz_divisor*sizeof(struct fib_node*), GFP_KERNEL);
	if (!fz->fz_hash) {
		kfree(fz);
		return NULL;
	}
	memset(fz->fz_hash, 0, fz->fz_divisor*sizeof(struct fib_node*));
	fz->fz_order = z;
	fz->fz_mask = inet_make_mask(z);

	/* Find the first not empty zone with more specific mask */
	for (i=z+1; i<=32; i++)
		if (table->fn_zones[i])
			break;
	start_bh_atomic();
	if (i>32) {
		/* No more specific masks, we are the first. */
		fz->fz_next = table->fn_zone_list;
		table->fn_zone_list = fz;
	} else {
		fz->fz_next = table->fn_zones[i]->fz_next;
		table->fn_zones[i]->fz_next = fz;
	}
	table->fn_zones[z] = fz;
	end_bh_atomic();
FTprint("NEW ZONE: order %d mask %08x hash %d/%08x\n", fz->fz_order, fz->fz_mask, fz->fz_divisor, fz->fz_hashmask);
	return fz;
}

static int
fn_hash_lookup(struct fib_table *tb, const struct rt_key *key, struct fib_result *res)
{
	int err;
	struct fn_zone *fz;
	struct fn_hash *t = (struct fn_hash*)tb->tb_data;

	for (fz = t->fn_zone_list; fz; fz = fz->fz_next) {
		struct fib_node *f;
		fn_key_t k = fz_key(key->dst, fz);
		int matched = 0;

		for (f = fz_chain(k, fz); f; f = f->fn_next) {
			if (!fn_key_eq(k, f->fn_key)
#ifdef CONFIG_IP_ROUTE_TOS
			    || (f->fn_tos && f->fn_tos != key->tos)
#endif
			    ) {
				if (matched)
					return 1;
				continue;
			}
			matched = 1;
			f->fn_state |= FN_S_ACCESSED;

			if (f->fn_state&FN_S_ZOMBIE)
				continue;
			if (f->fn_scope < key->scope)
				continue;

			err = fib_semantic_match(f->fn_type, FIB_INFO(f), key, res);
			if (err == 0) {
				res->type = f->fn_type;
				res->scope = f->fn_scope;
				res->prefixlen = fz->fz_order;
				res->prefix = &fz_prefix(f->fn_key, fz);
				return 0;
			}
			if (err < 0)
				return err;
		}
	}
	return 1;
}

#define FIB_SCAN(f, fp) \
for ( ; ((f) = *(fp)) != NULL; (fp) = &(f)->fn_next)

#define FIB_SCAN_KEY(f, fp, key) \
for ( ; ((f) = *(fp)) != NULL && fn_key_eq((f)->fn_key, (key)); (fp) = &(f)->fn_next)

#define FIB_CONTINUE(f, fp) \
{ \
	fp = &f->fn_next; \
	continue; \
}

#ifdef CONFIG_RTNETLINK
static void rtmsg_fib(int, struct fib_node*, int, int,
		      struct nlmsghdr *n,
		      struct netlink_skb_parms *);
#else
#define rtmsg_fib(a, b, c, d, e, f)
#endif


static int
fn_hash_insert(struct fib_table *tb, struct rtmsg *r, struct kern_rta *rta,
		struct nlmsghdr *n, struct netlink_skb_parms *req)
{
	struct fn_hash *table = (struct fn_hash*)tb->tb_data;
	struct fib_node *new_f, *f, **fp;
	struct fn_zone *fz;
	struct fib_info *fi;

	int z = r->rtm_dst_len;
	int type = r->rtm_type;
#ifdef CONFIG_IP_ROUTE_TOS
	u8 tos = r->rtm_tos;
#endif
	fn_key_t key;
	unsigned state = 0;
	int err;

FTprint("tb(%d)_insert: %d %08x/%d %d %08x\n", tb->tb_id, r->rtm_type, rta->rta_dst ?
*(u32*)rta->rta_dst : 0, z, rta->rta_oif ? *rta->rta_oif : -1,
rta->rta_prefsrc ? *(u32*)rta->rta_prefsrc : 0);
	if (z > 32)
		return -EINVAL;
	fz = table->fn_zones[z];
	if (!fz && !(fz = fn_new_zone(table, z)))
		return -ENOBUFS;

	fz_key_0(key);
	if (rta->rta_dst) {
		u32 dst;
		memcpy(&dst, rta->rta_dst, 4);
		if (dst & ~FZ_MASK(fz))
			return -EINVAL;
		key = fz_key(dst, fz);
	}

	if  ((fi = fib_create_info(r, rta, n, &err)) == NULL) {
FTprint("fib_create_info err=%d\n", err);
		return err;
	}

#ifdef CONFIG_IP_ROUTE_LARGE_TABLES
	if (fz->fz_nent > (fz->fz_divisor<<2) &&
	    fz->fz_divisor < FZ_MAX_DIVISOR &&
	    (z==32 || (1<<z) > fz->fz_divisor))
		fn_rehash_zone(fz);
#endif

	fp = fz_chain_p(key, fz);

	/*
	 * Scan list to find the first route with the same destination
	 */
	FIB_SCAN(f, fp) {
		if (fn_key_eq(f->fn_key,key))
			break;
	}

#ifdef CONFIG_IP_ROUTE_TOS
	/*
	 * Find route with the same destination and tos.
	 */
	FIB_SCAN_KEY(f, fp, key) {
		if (f->fn_tos <= tos)
			break;
	}
#endif

	if (f && fn_key_eq(f->fn_key, key)
#ifdef CONFIG_IP_ROUTE_TOS
	    && f->fn_tos == tos
#endif
	    ) {
		struct fib_node **ins_fp;

		state = f->fn_state;
		if (n->nlmsg_flags&NLM_F_EXCL && !(state&FN_S_ZOMBIE))
			return -EEXIST;
		if (n->nlmsg_flags&NLM_F_REPLACE) {
			struct fib_info *old_fi = FIB_INFO(f);
			if (old_fi != fi) {
				rtmsg_fib(RTM_DELROUTE, f, z, tb->tb_id, n, req);
				start_bh_atomic();
				FIB_INFO(f) = fi;
				f->fn_type = r->rtm_type;
				f->fn_scope = r->rtm_scope;
				end_bh_atomic();
				rtmsg_fib(RTM_NEWROUTE, f, z, tb->tb_id, n, req);
			}
			state = f->fn_state;
			f->fn_state = 0;
			fib_release_info(old_fi);
			if (state&FN_S_ACCESSED)
				rt_cache_flush(-1);
			return 0;
		}

		ins_fp = fp;

		for ( ; (f = *fp) != NULL && fn_key_eq(f->fn_key, key)
#ifdef CONFIG_IP_ROUTE_TOS
		     && f->fn_tos == tos
#endif
		     ; fp = &f->fn_next) {
			state |= f->fn_state;
			if (f->fn_type == type && f->fn_scope == r->rtm_scope
			    && FIB_INFO(f) == fi) {
				fib_release_info(fi);
				if (f->fn_state&FN_S_ZOMBIE) {
					f->fn_state = 0;
					rtmsg_fib(RTM_NEWROUTE, f, z, tb->tb_id, n, req);
					if (state&FN_S_ACCESSED)
						rt_cache_flush(-1);
					return 0;
				}
				return -EEXIST;
			}
		}
		if (!(n->nlmsg_flags&NLM_F_APPEND)) {
			fp = ins_fp;
			f = *fp;
		}
	} else {
		if (!(n->nlmsg_flags&NLM_F_CREATE))
			return -ENOENT;
	}

	new_f = (struct fib_node *) kmalloc(sizeof(struct fib_node), GFP_KERNEL);
	if (new_f == NULL) {
		fib_release_info(fi);
		return -ENOBUFS;
	}

	memset(new_f, 0, sizeof(struct fib_node));

	new_f->fn_key = key;
#ifdef CONFIG_IP_ROUTE_TOS
	new_f->fn_tos = tos;
#endif
	new_f->fn_type = type;
	new_f->fn_scope = r->rtm_scope;
	FIB_INFO(new_f) = fi;

	/*
	 * Insert new entry to the list.
	 */

	new_f->fn_next = f;
	/* ATOMIC_SET */
	*fp = new_f;
	fz->fz_nent++;

	rtmsg_fib(RTM_NEWROUTE, new_f, z, tb->tb_id, n, req);
	rt_cache_flush(-1);
	return 0;
}


static int
fn_hash_delete(struct fib_table *tb, struct rtmsg *r, struct kern_rta *rta,
		struct nlmsghdr *n, struct netlink_skb_parms *req)
{
	struct fn_hash *table = (struct fn_hash*)tb->tb_data;
	struct fib_node **fp, *f;
	int z = r->rtm_dst_len;
	struct fn_zone *fz;
	fn_key_t key;
#ifdef CONFIG_IP_ROUTE_TOS
	u8 tos = r->rtm_tos;
#endif

FTprint("tb(%d)_delete: %d %08x/%d %d\n", tb->tb_id, r->rtm_type, rta->rta_dst ?
       *(u32*)rta->rta_dst : 0, z, rta->rta_oif ? *rta->rta_oif : -1);
	if (z > 32)
		return -EINVAL;
	if ((fz  = table->fn_zones[z]) == NULL)
		return -ESRCH;

	fz_key_0(key);
	if (rta->rta_dst) {
		u32 dst;
		memcpy(&dst, rta->rta_dst, 4);
		if (dst & ~FZ_MASK(fz))
			return -EINVAL;
		key = fz_key(dst, fz);
	}

	fp = fz_chain_p(key, fz);

	FIB_SCAN(f, fp) {
		if (fn_key_eq(f->fn_key, key))
			break;
	}
#ifdef CONFIG_IP_ROUTE_TOS
	FIB_SCAN_KEY(f, fp, key) {
		if (f->fn_tos == tos)
			break;
	}
#endif

	while ((f = *fp) != NULL && fn_key_eq(f->fn_key, key)
#ifdef CONFIG_IP_ROUTE_TOS
	       && f->fn_tos == tos
#endif
	       ) {
		struct fib_info * fi = FIB_INFO(f);

		if ((f->fn_state&FN_S_ZOMBIE) ||
		    (r->rtm_type && f->fn_type != r->rtm_type) ||
		    (r->rtm_scope && f->fn_scope != r->rtm_scope) ||
		    (r->rtm_protocol && fi->fib_protocol != r->rtm_protocol) ||
		    fib_nh_match(r, n, rta, fi))
			FIB_CONTINUE(f, fp);
		break;
	}
	if (!f)
		return -ESRCH;
#if 0
	*fp = f->fn_next;
	rtmsg_fib(RTM_DELROUTE, f, z, tb->tb_id, n, req);
	fn_free_node(f);
	fz->fz_nent--;
	rt_cache_flush(0);
#else
	f->fn_state |= FN_S_ZOMBIE;
	rtmsg_fib(RTM_DELROUTE, f, z, tb->tb_id, n, req);
	if (f->fn_state&FN_S_ACCESSED) {
		f->fn_state &= ~FN_S_ACCESSED;
		rt_cache_flush(-1);
	}
	if (++fib_hash_zombies > 128)
		fib_flush();
#endif
	return 0;
}

extern __inline__ int
fn_flush_list(struct fib_node ** fp, int z, struct fn_hash *table)
{
	int found = 0;
	struct fib_node *f;

	while ((f = *fp) != NULL) {
		struct fib_info *fi = FIB_INFO(f);

		if (fi && ((f->fn_state&FN_S_ZOMBIE) || (fi->fib_flags&RTNH_F_DEAD))) {
			*fp = f->fn_next;
			fn_free_node(f);
			found++;
			continue;
		}
		fp = &f->fn_next;
	}
	return found;
}

static int fn_hash_flush(struct fib_table *tb)
{
	struct fn_hash *table = (struct fn_hash*)tb->tb_data;
	struct fn_zone *fz;
	int found = 0;

	fib_hash_zombies = 0;
	for (fz = table->fn_zone_list; fz; fz = fz->fz_next) {
		int i;
		int tmp = 0;
		for (i=fz->fz_divisor-1; i>=0; i--)
			tmp += fn_flush_list(&fz->fz_hash[i], fz->fz_order, table);
		fz->fz_nent -= tmp;
		found += tmp;
	}
	return found;
}


#ifdef CONFIG_PROC_FS

static int fn_hash_get_info(struct fib_table *tb, char *buffer, int first, int count)
{
	struct fn_hash *table = (struct fn_hash*)tb->tb_data;
	struct fn_zone *fz;
	int pos = 0;
	int n = 0;

	for (fz=table->fn_zone_list; fz; fz = fz->fz_next) {
		int i;
		struct fib_node *f;
		int maxslot = fz->fz_divisor;
		struct fib_node **fp = fz->fz_hash;

		if (fz->fz_nent == 0)
			continue;

		if (pos + fz->fz_nent <= first) {
			pos += fz->fz_nent;
			continue;
		}

		for (i=0; i < maxslot; i++, fp++) {
			for (f = *fp; f; f = f->fn_next) {
				if (++pos <= first)
					continue;
				fib_node_get_info(f->fn_type,
						  f->fn_state&FN_S_ZOMBIE,
						  FIB_INFO(f),
						  fz_prefix(f->fn_key, fz),
						  FZ_MASK(fz), buffer);
				buffer += 128;
				if (++n >= count)
					return n;
			}
		}
	}
  	return n;
}
#endif


#ifdef CONFIG_RTNETLINK

extern __inline__ int
fn_hash_dump_bucket(struct sk_buff *skb, struct netlink_callback *cb,
		     struct fib_table *tb,
		     struct fn_zone *fz,
		     struct fib_node *f)
{
	int i, s_i;

	s_i = cb->args[3];
	for (i=0; f; i++, f=f->fn_next) {
		if (i < s_i) continue;
		if (f->fn_state&FN_S_ZOMBIE) continue;
		if (fib_dump_info(skb, NETLINK_CB(cb->skb).pid, cb->nlh->nlmsg_seq,
				  RTM_NEWROUTE,
				  tb->tb_id, (f->fn_state&FN_S_ZOMBIE) ? 0 : f->fn_type, f->fn_scope,
				  &f->fn_key, fz->fz_order, f->fn_tos,
				  f->fn_info) < 0) {
			cb->args[3] = i;
			return -1;
		}
	}
	cb->args[3] = i;
	return skb->len;
}

extern __inline__ int
fn_hash_dump_zone(struct sk_buff *skb, struct netlink_callback *cb,
		   struct fib_table *tb,
		   struct fn_zone *fz)
{
	int h, s_h;

	s_h = cb->args[2];
	for (h=0; h < fz->fz_divisor; h++) {
		if (h < s_h) continue;
		if (h > s_h)
			memset(&cb->args[3], 0, sizeof(cb->args) - 3*sizeof(int));
		if (fz->fz_hash == NULL || fz->fz_hash[h] == NULL)
			continue;
		if (fn_hash_dump_bucket(skb, cb, tb, fz, fz->fz_hash[h]) < 0) {
			cb->args[2] = h;
			return -1;
		}
	}
	cb->args[2] = h;
	return skb->len;
}

static int fn_hash_dump(struct fib_table *tb, struct sk_buff *skb, struct netlink_callback *cb)
{
	int m, s_m;
	struct fn_zone *fz;
	struct fn_hash *table = (struct fn_hash*)tb->tb_data;

	s_m = cb->args[1];
	for (fz = table->fn_zone_list, m=0; fz; fz = fz->fz_next, m++) {
		if (m < s_m) continue;
		if (m > s_m)
			memset(&cb->args[2], 0, sizeof(cb->args) - 2*sizeof(int));
		if (fn_hash_dump_zone(skb, cb, tb, fz) < 0) {
			cb->args[1] = m;
			return -1;
		}
	}
	cb->args[1] = m;
	return skb->len;
}

static void rtmsg_fib(int event, struct fib_node* f, int z, int tb_id,
		      struct nlmsghdr *n, struct netlink_skb_parms *req)
{
	struct sk_buff *skb;
	pid_t pid = req ? req->pid : 0;
	int size = NLMSG_SPACE(sizeof(struct rtmsg)+256);

	skb = alloc_skb(size, GFP_KERNEL);
	if (!skb)
		return;

	if (fib_dump_info(skb, pid, n->nlmsg_seq, event, tb_id,
			  f->fn_type, f->fn_scope, &f->fn_key, z, f->fn_tos,
			  FIB_INFO(f)) < 0) {
		kfree_skb(skb);
		return;
	}
	NETLINK_CB(skb).dst_groups = RTMGRP_IPV4_ROUTE;
	if (n->nlmsg_flags&NLM_F_ECHO)
		atomic_inc(&skb->users);
	netlink_broadcast(rtnl, skb, pid, RTMGRP_IPV4_ROUTE, GFP_KERNEL);
	if (n->nlmsg_flags&NLM_F_ECHO)
		netlink_unicast(rtnl, skb, pid, MSG_DONTWAIT);
}

#endif /* CONFIG_RTNETLINK */

#ifdef CONFIG_IP_MULTIPLE_TABLES
struct fib_table * fib_hash_init(int id)
#else
__initfunc(struct fib_table * fib_hash_init(int id))
#endif
{
	struct fib_table *tb;
	tb = kmalloc(sizeof(struct fib_table) + sizeof(struct fn_hash), GFP_KERNEL);
	if (tb == NULL)
		return NULL;
	tb->tb_id = id;
	tb->tb_lookup = fn_hash_lookup;
	tb->tb_insert = fn_hash_insert;
	tb->tb_delete = fn_hash_delete;
	tb->tb_flush = fn_hash_flush;
#ifdef CONFIG_RTNETLINK
	tb->tb_dump = fn_hash_dump;
#endif
#ifdef CONFIG_PROC_FS
	tb->tb_get_info = fn_hash_get_info;
#endif
	memset(tb->tb_data, 0, sizeof(struct fn_hash));
	return tb;
}
