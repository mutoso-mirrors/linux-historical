/*
 * linux/mm/slab.c
 * Written by Mark Hemment, 1996/97.
 * (markhe@nextd.demon.co.uk)
 *
 * kmem_cache_destroy() + some cleanup - 1999 Andrea Arcangeli
 *
 * Major cleanup, different bufctl logic, per-cpu arrays
 *	(c) 2000 Manfred Spraul
 *
 * Cleanup, make the head arrays unconditional, preparation for NUMA
 * 	(c) 2002 Manfred Spraul
 *
 * An implementation of the Slab Allocator as described in outline in;
 *	UNIX Internals: The New Frontiers by Uresh Vahalia
 *	Pub: Prentice Hall	ISBN 0-13-101908-2
 * or with a little more detail in;
 *	The Slab Allocator: An Object-Caching Kernel Memory Allocator
 *	Jeff Bonwick (Sun Microsystems).
 *	Presented at: USENIX Summer 1994 Technical Conference
 *
 * The memory is organized in caches, one cache for each object type.
 * (e.g. inode_cache, dentry_cache, buffer_head, vm_area_struct)
 * Each cache consists out of many slabs (they are small (usually one
 * page long) and always contiguous), and each slab contains multiple
 * initialized objects.
 *
 * Each cache can only support one memory type (GFP_DMA, GFP_HIGHMEM,
 * normal). If you need a special memory type, then must create a new
 * cache for that memory type.
 *
 * In order to reduce fragmentation, the slabs are sorted in 3 groups:
 *   full slabs with 0 free objects
 *   partial slabs
 *   empty slabs with no allocated objects
 *
 * If partial slabs exist, then new allocations come from these slabs,
 * otherwise from empty slabs or new slabs are allocated.
 *
 * kmem_cache_destroy() CAN CRASH if you try to allocate from the cache
 * during kmem_cache_destroy(). The caller must prevent concurrent allocs.
 *
 * Each cache has a short per-cpu head array, most allocs
 * and frees go into that array, and if that array overflows, then 1/2
 * of the entries in the array are given back into the global cache.
 * The head array is strictly LIFO and should improve the cache hit rates.
 * On SMP, it additionally reduces the spinlock operations.
 *
 * The c_cpuarray may not be read with enabled local interrupts - 
 * it's changed with a smp_call_function().
 *
 * SMP synchronization:
 *  constructors and destructors are called without any locking.
 *  Several members in kmem_cache_t and struct slab never change, they
 *	are accessed without any locking.
 *  The per-cpu arrays are never accessed from the wrong cpu, no locking,
 *  	and local interrupts are disabled so slab code is preempt-safe.
 *  The non-constant members are protected with a per-cache irq spinlock.
 *
 * Many thanks to Mark Hemment, who wrote another per-cpu slab patch
 * in 2000 - many ideas in the current implementation are derived from
 * his patch.
 *
 * Further notes from the original documentation:
 *
 * 11 April '97.  Started multi-threading - markhe
 *	The global cache-chain is protected by the semaphore 'cache_chain_sem'.
 *	The sem is only needed when accessing/extending the cache-chain, which
 *	can never happen inside an interrupt (kmem_cache_create(),
 *	kmem_cache_shrink() and kmem_cache_reap()).
 *
 *	At present, each engine can be growing a cache.  This should be blocked.
 *
 */

#include	<linux/config.h>
#include	<linux/slab.h>
#include	<linux/mm.h>
#include	<linux/cache.h>
#include	<linux/interrupt.h>
#include	<linux/init.h>
#include	<linux/compiler.h>
#include	<linux/seq_file.h>
#include	<linux/notifier.h>
#include	<asm/uaccess.h>

/*
 * DEBUG	- 1 for kmem_cache_create() to honour; SLAB_DEBUG_INITIAL,
 *		  SLAB_RED_ZONE & SLAB_POISON.
 *		  0 for faster, smaller code (especially in the critical paths).
 *
 * STATS	- 1 to collect stats for /proc/slabinfo.
 *		  0 for faster, smaller code (especially in the critical paths).
 *
 * FORCED_DEBUG	- 1 enables SLAB_RED_ZONE and SLAB_POISON (if possible)
 */

#ifdef CONFIG_DEBUG_SLAB
#define	DEBUG		1
#define	STATS		1
#define	FORCED_DEBUG	1
#else
#define	DEBUG		0
#define	STATS		0
#define	FORCED_DEBUG	0
#endif


/* Shouldn't this be in a header file somewhere? */
#define	BYTES_PER_WORD		sizeof(void *)

/* Legal flag mask for kmem_cache_create(). */
#if DEBUG
# define CREATE_MASK	(SLAB_DEBUG_INITIAL | SLAB_RED_ZONE | \
			 SLAB_POISON | SLAB_HWCACHE_ALIGN | \
			 SLAB_NO_REAP | SLAB_CACHE_DMA | \
			 SLAB_MUST_HWCACHE_ALIGN)
#else
# define CREATE_MASK	(SLAB_HWCACHE_ALIGN | SLAB_NO_REAP | \
			 SLAB_CACHE_DMA | SLAB_MUST_HWCACHE_ALIGN)
#endif

/*
 * kmem_bufctl_t:
 *
 * Bufctl's are used for linking objs within a slab
 * linked offsets.
 *
 * This implementation relies on "struct page" for locating the cache &
 * slab an object belongs to.
 * This allows the bufctl structure to be small (one int), but limits
 * the number of objects a slab (not a cache) can contain when off-slab
 * bufctls are used. The limit is the size of the largest general cache
 * that does not use off-slab slabs.
 * For 32bit archs with 4 kB pages, is this 56.
 * This is not serious, as it is only for large objects, when it is unwise
 * to have too many per slab.
 * Note: This limit can be raised by introducing a general cache whose size
 * is less than 512 (PAGE_SIZE<<3), but greater than 256.
 */

#define BUFCTL_END 0xffffFFFF
#define	SLAB_LIMIT 0xffffFFFE
typedef unsigned int kmem_bufctl_t;

/* Max number of objs-per-slab for caches which use off-slab slabs.
 * Needed to avoid a possible looping condition in cache_grow().
 */
static unsigned long offslab_limit;

/*
 * struct slab
 *
 * Manages the objs in a slab. Placed either at the beginning of mem allocated
 * for a slab, or allocated from an general cache.
 * Slabs are chained into three list: fully used, partial, fully free slabs.
 */
struct slab {
	struct list_head	list;
	unsigned long		colouroff;
	void			*s_mem;		/* including colour offset */
	unsigned int		inuse;		/* num of objs active in slab */
	kmem_bufctl_t		free;
};

/*
 * struct array_cache
 *
 * Per cpu structures
 * Purpose:
 * - LIFO ordering, to hand out cache-warm objects from _alloc
 * - reduce the number of linked list operations
 * - reduce spinlock operations
 *
 * The limit is stored in the per-cpu structure to reduce the data cache
 * footprint.
 *
 */
struct array_cache {
	unsigned int avail;
	unsigned int limit;
	unsigned int batchcount;
	unsigned int touched;
};

/* bootstrap: The caches do not work without cpuarrays anymore,
 * but the cpuarrays are allocated from the generic caches...
 */
#define BOOT_CPUCACHE_ENTRIES	1
struct arraycache_init {
	struct array_cache cache;
	void * entries[BOOT_CPUCACHE_ENTRIES];
};

/*
 * The slab lists of all objects.
 * Hopefully reduce the internal fragmentation
 * NUMA: The spinlock could be moved from the kmem_cache_t
 * into this structure, too. Figure out what causes
 * fewer cross-node spinlock operations.
 */
struct kmem_list3 {
	struct list_head	slabs_partial;	/* partial list first, better asm code */
	struct list_head	slabs_full;
	struct list_head	slabs_free;
	unsigned long	free_objects;
	int		free_touched;
	unsigned long	next_reap;
};

#define LIST3_INIT(parent) \
	{ \
		.slabs_full	= LIST_HEAD_INIT(parent.slabs_full), \
		.slabs_partial	= LIST_HEAD_INIT(parent.slabs_partial), \
		.slabs_free	= LIST_HEAD_INIT(parent.slabs_free) \
	}
#define list3_data(cachep) \
	(&(cachep)->lists)

/* NUMA: per-node */
#define list3_data_ptr(cachep, ptr) \
		list3_data(cachep)

/*
 * kmem_cache_t
 *
 * manages a cache.
 */
	
struct kmem_cache_s {
/* 1) per-cpu data, touched during every alloc/free */
	struct array_cache	*array[NR_CPUS];
	unsigned int		batchcount;
	unsigned int		limit;
/* 2) touched by every alloc & free from the backend */
	struct kmem_list3	lists;
	/* NUMA: kmem_3list_t	*nodelists[NR_NODES] */
	unsigned int		objsize;
	unsigned int	 	flags;	/* constant flags */
	unsigned int		num;	/* # of objs per slab */
	unsigned int		free_limit; /* upper limit of objects in the lists */
	spinlock_t		spinlock;

/* 3) cache_grow/shrink */
	/* order of pgs per slab (2^n) */
	unsigned int		gfporder;

	/* force GFP flags, e.g. GFP_DMA */
	unsigned int		gfpflags;

	size_t			colour;		/* cache colouring range */
	unsigned int		colour_off;	/* colour offset */
	unsigned int		colour_next;	/* cache colouring */
	kmem_cache_t		*slabp_cache;
	unsigned int		dflags;		/* dynamic flags */

	/* constructor func */
	void (*ctor)(void *, kmem_cache_t *, unsigned long);

	/* de-constructor func */
	void (*dtor)(void *, kmem_cache_t *, unsigned long);

/* 4) cache creation/removal */
	const char		*name;
	struct list_head	next;

/* 5) statistics */
#if STATS
	unsigned long		num_active;
	unsigned long		num_allocations;
	unsigned long		high_mark;
	unsigned long		grown;
	unsigned long		reaped;
	unsigned long 		errors;
	unsigned long		max_freeable;
	atomic_t		allochit;
	atomic_t		allocmiss;
	atomic_t		freehit;
	atomic_t		freemiss;
#endif
};

/* internal c_flags */
#define	CFLGS_OFF_SLAB	0x010000UL	/* slab management in own cache */

#define	OFF_SLAB(x)	((x)->flags & CFLGS_OFF_SLAB)

#define BATCHREFILL_LIMIT	16
/* Optimization question: fewer reaps means less 
 * probability for unnessary cpucache drain/refill cycles.
 *
 * OTHO the cpuarrays can contain lots of objects,
 * which could lock up otherwise freeable slabs.
 */
#define REAPTIMEOUT_CPUC	(2*HZ)
#define REAPTIMEOUT_LIST3	(4*HZ)

#if STATS
#define	STATS_INC_ACTIVE(x)	((x)->num_active++)
#define	STATS_DEC_ACTIVE(x)	((x)->num_active--)
#define	STATS_INC_ALLOCED(x)	((x)->num_allocations++)
#define	STATS_INC_GROWN(x)	((x)->grown++)
#define	STATS_INC_REAPED(x)	((x)->reaped++)
#define	STATS_SET_HIGH(x)	do { if ((x)->num_active > (x)->high_mark) \
					(x)->high_mark = (x)->num_active; \
				} while (0)
#define	STATS_INC_ERR(x)	((x)->errors++)
#define	STATS_SET_FREEABLE(x, i) \
				do { if ((x)->max_freeable < i) \
					(x)->max_freeable = i; \
				} while (0)

#define STATS_INC_ALLOCHIT(x)	atomic_inc(&(x)->allochit)
#define STATS_INC_ALLOCMISS(x)	atomic_inc(&(x)->allocmiss)
#define STATS_INC_FREEHIT(x)	atomic_inc(&(x)->freehit)
#define STATS_INC_FREEMISS(x)	atomic_inc(&(x)->freemiss)
#else
#define	STATS_INC_ACTIVE(x)	do { } while (0)
#define	STATS_DEC_ACTIVE(x)	do { } while (0)
#define	STATS_INC_ALLOCED(x)	do { } while (0)
#define	STATS_INC_GROWN(x)	do { } while (0)
#define	STATS_INC_REAPED(x)	do { } while (0)
#define	STATS_SET_HIGH(x)	do { } while (0)
#define	STATS_INC_ERR(x)	do { } while (0)
#define	STATS_SET_FREEABLE(x, i) \
				do { } while (0)

#define STATS_INC_ALLOCHIT(x)	do { } while (0)
#define STATS_INC_ALLOCMISS(x)	do { } while (0)
#define STATS_INC_FREEHIT(x)	do { } while (0)
#define STATS_INC_FREEMISS(x)	do { } while (0)
#endif

#if DEBUG
/* Magic nums for obj red zoning.
 * Placed in the first word before and the first word after an obj.
 */
#define	RED_INACTIVE	0x5A2CF071UL	/* when obj is inactive */
#define	RED_ACTIVE	0x170FC2A5UL	/* when obj is active */

/* ...and for poisoning */
#define	POISON_BEFORE	0x5a	/* for use-uninitialised poisoning */
#define POISON_AFTER	0x6b	/* for use-after-free poisoning */
#define	POISON_END	0xa5	/* end-byte of poisoning */

#endif

/* maximum size of an obj (in 2^order pages) */
#define	MAX_OBJ_ORDER	5	/* 32 pages */

/*
 * Do not go above this order unless 0 objects fit into the slab.
 */
#define	BREAK_GFP_ORDER_HI	2
#define	BREAK_GFP_ORDER_LO	1
static int slab_break_gfp_order = BREAK_GFP_ORDER_LO;

/*
 * Absolute limit for the gfp order
 */
#define	MAX_GFP_ORDER	5	/* 32 pages */


/* Macros for storing/retrieving the cachep and or slab from the
 * global 'mem_map'. These are used to find the slab an obj belongs to.
 * With kfree(), these are used to find the cache which an obj belongs to.
 */
#define	SET_PAGE_CACHE(pg,x)  ((pg)->list.next = (struct list_head *)(x))
#define	GET_PAGE_CACHE(pg)    ((kmem_cache_t *)(pg)->list.next)
#define	SET_PAGE_SLAB(pg,x)   ((pg)->list.prev = (struct list_head *)(x))
#define	GET_PAGE_SLAB(pg)     ((struct slab *)(pg)->list.prev)

/* Size description struct for general caches. */
struct cache_sizes {
	size_t		 cs_size;
	kmem_cache_t	*cs_cachep;
	kmem_cache_t	*cs_dmacachep;
};

/* These are the default caches for kmalloc. Custom caches can have other sizes. */
static struct cache_sizes malloc_sizes[] = {
#if PAGE_SIZE == 4096
	{    32,	NULL, NULL},
#endif
	{    64,	NULL, NULL},
#if L1_CACHE_BYTES < 64
	{    96,	NULL, NULL},
#endif
	{   128,	NULL, NULL},
#if L1_CACHE_BYTES < 128
	{   192,	NULL, NULL},
#endif
	{   256,	NULL, NULL},
	{   512,	NULL, NULL},
	{  1024,	NULL, NULL},
	{  2048,	NULL, NULL},
	{  4096,	NULL, NULL},
	{  8192,	NULL, NULL},
	{ 16384,	NULL, NULL},
	{ 32768,	NULL, NULL},
	{ 65536,	NULL, NULL},
	{131072,	NULL, NULL},
	{     0,	NULL, NULL}
};
/* Must match cache_sizes above. Out of line to keep cache footprint low. */
#define CN(x) { x, x "(DMA)" }
static struct { 
	char *name; 
	char *name_dma;
} cache_names[] = { 
#if PAGE_SIZE == 4096
	CN("size-32"),
#endif
	CN("size-64"),
#if L1_CACHE_BYTES < 64
	CN("size-96"),
#endif
	CN("size-128"),
#if L1_CACHE_BYTES < 128
	CN("size-192"),
#endif
	CN("size-256"),
	CN("size-512"),
	CN("size-1024"),
	CN("size-2048"),
	CN("size-4096"),
	CN("size-8192"),
	CN("size-16384"),
	CN("size-32768"),
	CN("size-65536"),
	CN("size-131072")
}; 
#undef CN

struct arraycache_init initarray_cache __initdata = { { 0, BOOT_CPUCACHE_ENTRIES, 1, 0} };
struct arraycache_init initarray_generic __initdata = { { 0, BOOT_CPUCACHE_ENTRIES, 1, 0} };

/* internal cache of cache description objs */
static kmem_cache_t cache_cache = {
	.lists		= LIST3_INIT(cache_cache.lists),
	/* Allow for boot cpu != 0 */
	.array		= { [0 ... NR_CPUS-1] = &initarray_cache.cache },
	.batchcount	= 1,
	.limit		= BOOT_CPUCACHE_ENTRIES,
	.objsize	= sizeof(kmem_cache_t),
	.flags		= SLAB_NO_REAP,
	.spinlock	= SPIN_LOCK_UNLOCKED,
	.colour_off	= L1_CACHE_BYTES,
	.name		= "kmem_cache",
};

/* Guard access to the cache-chain. */
static struct semaphore	cache_chain_sem;

struct list_head cache_chain;

/*
 * chicken and egg problem: delay the per-cpu array allocation
 * until the general caches are up.
 */
enum {
	NONE,
	PARTIAL,
	FULL
} g_cpucache_up;

static struct timer_list reap_timers[NR_CPUS];

static void reap_timer_fnc(unsigned long data);

static void enable_cpucache (kmem_cache_t *cachep);

/* Cal the num objs, wastage, and bytes left over for a given slab size. */
static void cache_estimate (unsigned long gfporder, size_t size,
		 int flags, size_t *left_over, unsigned int *num)
{
	int i;
	size_t wastage = PAGE_SIZE<<gfporder;
	size_t extra = 0;
	size_t base = 0;

	if (!(flags & CFLGS_OFF_SLAB)) {
		base = sizeof(struct slab);
		extra = sizeof(kmem_bufctl_t);
	}
	i = 0;
	while (i*size + L1_CACHE_ALIGN(base+i*extra) <= wastage)
		i++;
	if (i > 0)
		i--;

	if (i > SLAB_LIMIT)
		i = SLAB_LIMIT;

	*num = i;
	wastage -= i*size;
	wastage -= L1_CACHE_ALIGN(base+i*extra);
	*left_over = wastage;
}

#if DEBUG

#define slab_error(cachep, msg) __slab_error(__FUNCTION__, cachep, msg)

static void __slab_error(const char *function, kmem_cache_t *cachep, char *msg)
{
	printk(KERN_ERR "slab error in %s(): cache `%s': %s\n",
		function, cachep->name, msg);
	dump_stack();
}

#endif

/*
 * Start the reap timer running on the target CPU.  We run at around 1 to 2Hz.
 * Add the CPU number into the expiry time to minimize the possibility of the
 * CPUs getting into lockstep and contending for the global cache chain lock.
 */
static void start_cpu_timer(int cpu)
{
	struct timer_list *rt = &reap_timers[cpu];

	if (rt->function == NULL) {
		init_timer(rt);
		rt->expires = jiffies + HZ + 3*cpu;
		rt->function = reap_timer_fnc;
		add_timer_on(rt, cpu);
	}
}

/*
 * Note: if someone calls kmem_cache_alloc() on the new
 * cpu before the cpuup callback had a chance to allocate
 * the head arrays, it will oops.
 * Is CPU_ONLINE early enough?
 */
static int __devinit cpuup_callback(struct notifier_block *nfb,
				  unsigned long action,
				  void *hcpu)
{
	long cpu = (long)hcpu;
	struct list_head *p;

	switch (action) {
	case CPU_UP_PREPARE:
		down(&cache_chain_sem);
		list_for_each(p, &cache_chain) {
			int memsize;
			struct array_cache *nc;

			kmem_cache_t* cachep = list_entry(p, kmem_cache_t, next);
			memsize = sizeof(void*)*cachep->limit+sizeof(struct array_cache);
			nc = kmalloc(memsize, GFP_KERNEL);
			if (!nc)
				goto bad;
			nc->avail = 0;
			nc->limit = cachep->limit;
			nc->batchcount = cachep->batchcount;
			nc->touched = 0;

			spin_lock_irq(&cachep->spinlock);
			cachep->array[cpu] = nc;
			cachep->free_limit = (1+num_online_cpus())*cachep->batchcount
						+ cachep->num;
			spin_unlock_irq(&cachep->spinlock);

		}
		up(&cache_chain_sem);
		break;
	case CPU_ONLINE:
		if (g_cpucache_up == FULL)
			start_cpu_timer(cpu);
		break;
	case CPU_UP_CANCELED:
		down(&cache_chain_sem);

		list_for_each(p, &cache_chain) {
			struct array_cache *nc;
			kmem_cache_t* cachep = list_entry(p, kmem_cache_t, next);

			nc = cachep->array[cpu];
			cachep->array[cpu] = NULL;
			kfree(nc);
		}
		up(&cache_chain_sem);
		break;
	}
	return NOTIFY_OK;
bad:
	up(&cache_chain_sem);
	return NOTIFY_BAD;
}

static struct notifier_block cpucache_notifier = { &cpuup_callback, NULL, 0 };

static inline void ** ac_entry(struct array_cache *ac)
{
	return (void**)(ac+1);
}

static inline struct array_cache *ac_data(kmem_cache_t *cachep)
{
	return cachep->array[smp_processor_id()];
}

/* Initialisation - setup the `cache' cache. */
void __init kmem_cache_init(void)
{
	size_t left_over;

	init_MUTEX(&cache_chain_sem);
	INIT_LIST_HEAD(&cache_chain);
	list_add(&cache_cache.next, &cache_chain);

	cache_estimate(0, cache_cache.objsize, 0,
			&left_over, &cache_cache.num);
	if (!cache_cache.num)
		BUG();

	cache_cache.colour = left_over/cache_cache.colour_off;
	cache_cache.colour_next = 0;

	/* Register a cpu startup notifier callback
	 * that initializes ac_data for all new cpus
	 */
	register_cpu_notifier(&cpucache_notifier);
}


/* Initialisation - setup remaining internal and general caches.
 * Called after the gfp() functions have been enabled, and before smp_init().
 */
void __init kmem_cache_sizes_init(void)
{
	struct cache_sizes *sizes = malloc_sizes;
	/*
	 * Fragmentation resistance on low memory - only use bigger
	 * page orders on machines with more than 32MB of memory.
	 */
	if (num_physpages > (32 << 20) >> PAGE_SHIFT)
		slab_break_gfp_order = BREAK_GFP_ORDER_HI;
	do {
		/* For performance, all the general caches are L1 aligned.
		 * This should be particularly beneficial on SMP boxes, as it
		 * eliminates "false sharing".
		 * Note for systems short on memory removing the alignment will
		 * allow tighter packing of the smaller caches. */
		if (!(sizes->cs_cachep =
			kmem_cache_create(cache_names[sizes-malloc_sizes].name, 
					  sizes->cs_size,
					0, SLAB_HWCACHE_ALIGN, NULL, NULL))) {
			BUG();
		}

		/* Inc off-slab bufctl limit until the ceiling is hit. */
		if (!(OFF_SLAB(sizes->cs_cachep))) {
			offslab_limit = sizes->cs_size-sizeof(struct slab);
			offslab_limit /= sizeof(kmem_bufctl_t);
		}
		sizes->cs_dmacachep = kmem_cache_create(
		    cache_names[sizes-malloc_sizes].name_dma, 
			sizes->cs_size, 0,
			SLAB_CACHE_DMA|SLAB_HWCACHE_ALIGN, NULL, NULL);
		if (!sizes->cs_dmacachep)
			BUG();
		sizes++;
	} while (sizes->cs_size);
	/*
	 * The generic caches are running - time to kick out the
	 * bootstrap cpucaches.
	 */
	{
		void * ptr;
		
		ptr = kmalloc(sizeof(struct arraycache_init), GFP_KERNEL);
		local_irq_disable();
		BUG_ON(ac_data(&cache_cache) != &initarray_cache.cache);
		memcpy(ptr, ac_data(&cache_cache), sizeof(struct arraycache_init));
		cache_cache.array[smp_processor_id()] = ptr;
		local_irq_enable();
	
		ptr = kmalloc(sizeof(struct arraycache_init), GFP_KERNEL);
		local_irq_disable();
		BUG_ON(ac_data(malloc_sizes[0].cs_cachep) != &initarray_generic.cache);
		memcpy(ptr, ac_data(malloc_sizes[0].cs_cachep),
				sizeof(struct arraycache_init));
		malloc_sizes[0].cs_cachep->array[smp_processor_id()] = ptr;
		local_irq_enable();
	}
}

int __init cpucache_init(void)
{
	kmem_cache_t *cachep;
	int cpu;

	down(&cache_chain_sem);
	g_cpucache_up = FULL;

	list_for_each_entry(cachep, &cache_chain, next)
		enable_cpucache(cachep);
	
	/* 
	 * Register the timers that return unneeded
	 * pages to gfp.
	 */

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		if (cpu_online(cpu))
			start_cpu_timer(cpu);
	}
	up(&cache_chain_sem);

	return 0;
}

__initcall(cpucache_init);

/* Interface to system's page allocator. No need to hold the cache-lock.
 */
static inline void * kmem_getpages (kmem_cache_t *cachep, unsigned long flags)
{
	void	*addr;

	/*
	 * If we requested dmaable memory, we will get it. Even if we
	 * did not request dmaable memory, we might get it, but that
	 * would be relatively rare and ignorable.
	 */
	flags |= cachep->gfpflags;
	addr = (void*) __get_free_pages(flags, cachep->gfporder);
	/* Assume that now we have the pages no one else can legally
	 * messes with the 'struct page's.
	 * However vm_scan() might try to test the structure to see if
	 * it is a named-page or buffer-page.  The members it tests are
	 * of no interest here.....
	 */
	return addr;
}

/* Interface to system's page release. */
static inline void kmem_freepages (kmem_cache_t *cachep, void *addr)
{
	unsigned long i = (1<<cachep->gfporder);
	struct page *page = virt_to_page(addr);

	/* free_pages() does not clear the type bit - we do that.
	 * The pages have been unlinked from their cache-slab,
	 * but their 'struct page's might be accessed in
	 * vm_scan(). Shouldn't be a worry.
	 */
	while (i--) {
		ClearPageSlab(page);
		dec_page_state(nr_slab);
		page++;
	}
	free_pages((unsigned long)addr, cachep->gfporder);
}

#if DEBUG
static void poison_obj(kmem_cache_t *cachep, void *addr, unsigned char val)
{
	int size = cachep->objsize;
	if (cachep->flags & SLAB_RED_ZONE) {
		addr += BYTES_PER_WORD;
		size -= 2*BYTES_PER_WORD;
	}
	memset(addr, val, size);
	*(unsigned char *)(addr+size-1) = POISON_END;
}

static void check_poison_obj(kmem_cache_t *cachep, void *addr)
{
	int size = cachep->objsize;
	void *end;
	if (cachep->flags & SLAB_RED_ZONE) {
		addr += BYTES_PER_WORD;
		size -= 2*BYTES_PER_WORD;
	}
	end = memchr(addr, POISON_END, size);
	if (end != (addr+size-1))
		slab_error(cachep, "object was modified after freeing");
}
#endif

/* Destroy all the objs in a slab, and release the mem back to the system.
 * Before calling the slab must have been unlinked from the cache.
 * The cache-lock is not held/needed.
 */
static void slab_destroy (kmem_cache_t *cachep, struct slab *slabp)
{
#if DEBUG
	int i;
	for (i = 0; i < cachep->num; i++) {
		void *objp = slabp->s_mem + cachep->objsize * i;

		if (cachep->flags & SLAB_POISON)
			check_poison_obj(cachep, objp);

		if (cachep->flags & SLAB_RED_ZONE) {
			if (*((unsigned long*)(objp)) != RED_INACTIVE)
				slab_error(cachep, "start of a freed object "
							"was overwritten");
			if (*((unsigned long*)(objp + cachep->objsize -
					BYTES_PER_WORD)) != RED_INACTIVE)
				slab_error(cachep, "end of a freed object "
							"was overwritten");
			objp += BYTES_PER_WORD;
		}
		if (cachep->dtor && !(cachep->flags & SLAB_POISON))
			(cachep->dtor)(objp, cachep, 0);
	}
#else
	if (cachep->dtor) {
		int i;
		for (i = 0; i < cachep->num; i++) {
			void* objp = slabp->s_mem+cachep->objsize*i;
			(cachep->dtor)(objp, cachep, 0);
		}
	}
#endif
	
	kmem_freepages(cachep, slabp->s_mem-slabp->colouroff);
	if (OFF_SLAB(cachep))
		kmem_cache_free(cachep->slabp_cache, slabp);
}

/**
 * kmem_cache_create - Create a cache.
 * @name: A string which is used in /proc/slabinfo to identify this cache.
 * @size: The size of objects to be created in this cache.
 * @offset: The offset to use within the page.
 * @flags: SLAB flags
 * @ctor: A constructor for the objects.
 * @dtor: A destructor for the objects.
 *
 * Returns a ptr to the cache on success, NULL on failure.
 * Cannot be called within a int, but can be interrupted.
 * The @ctor is run when new pages are allocated by the cache
 * and the @dtor is run before the pages are handed back.
 *
 * @name must be valid until the cache is destroyed. This implies that
 * the module calling this has to destroy the cache before getting 
 * unloaded.
 * 
 * The flags are
 *
 * %SLAB_POISON - Poison the slab with a known test pattern (a5a5a5a5)
 * to catch references to uninitialised memory.
 *
 * %SLAB_RED_ZONE - Insert `Red' zones around the allocated memory to check
 * for buffer overruns.
 *
 * %SLAB_NO_REAP - Don't automatically reap this cache when we're under
 * memory pressure.
 *
 * %SLAB_HWCACHE_ALIGN - Align the objects in this cache to a hardware
 * cacheline.  This can be beneficial if you're counting cycles as closely
 * as davem.
 */
kmem_cache_t *
kmem_cache_create (const char *name, size_t size, size_t offset,
	unsigned long flags, void (*ctor)(void*, kmem_cache_t *, unsigned long),
	void (*dtor)(void*, kmem_cache_t *, unsigned long))
{
	const char *func_nm = KERN_ERR "kmem_create: ";
	size_t left_over, align, slab_size;
	kmem_cache_t *cachep = NULL;

	/*
	 * Sanity checks... these are all serious usage bugs.
	 */
	if ((!name) ||
		in_interrupt() ||
		(size < BYTES_PER_WORD) ||
		(size > (1<<MAX_OBJ_ORDER)*PAGE_SIZE) ||
		(dtor && !ctor) ||
		(offset < 0 || offset > size))
			BUG();

#if DEBUG
	if ((flags & SLAB_DEBUG_INITIAL) && !ctor) {
		/* No constructor, but inital state check requested */
		printk("%sNo con, but init state check requested - %s\n", func_nm, name);
		flags &= ~SLAB_DEBUG_INITIAL;
	}

#if FORCED_DEBUG
	if ((size < (PAGE_SIZE>>3)) && !(flags & SLAB_MUST_HWCACHE_ALIGN))
		/*
		 * do not red zone large object, causes severe
		 * fragmentation.
		 */
		flags |= SLAB_RED_ZONE;
	flags |= SLAB_POISON;
#endif
#endif

	/*
	 * Always checks flags, a caller might be expecting debug
	 * support which isn't available.
	 */
	if (flags & ~CREATE_MASK)
		BUG();

	/* Get cache's description obj. */
	cachep = (kmem_cache_t *) kmem_cache_alloc(&cache_cache, SLAB_KERNEL);
	if (!cachep)
		goto opps;
	memset(cachep, 0, sizeof(kmem_cache_t));

	/* Check that size is in terms of words.  This is needed to avoid
	 * unaligned accesses for some archs when redzoning is used, and makes
	 * sure any on-slab bufctl's are also correctly aligned.
	 */
	if (size & (BYTES_PER_WORD-1)) {
		size += (BYTES_PER_WORD-1);
		size &= ~(BYTES_PER_WORD-1);
		printk("%sForcing size word alignment - %s\n", func_nm, name);
	}
	
#if DEBUG
	if (flags & SLAB_RED_ZONE) {
		/*
		 * There is no point trying to honour cache alignment
		 * when redzoning.
		 */
		flags &= ~SLAB_HWCACHE_ALIGN;
		size += 2*BYTES_PER_WORD;	/* words for redzone */
	}
#endif
	align = BYTES_PER_WORD;
	if (flags & SLAB_HWCACHE_ALIGN)
		align = L1_CACHE_BYTES;

	/* Determine if the slab management is 'on' or 'off' slab. */
	if (size >= (PAGE_SIZE>>3))
		/*
		 * Size is large, assume best to place the slab management obj
		 * off-slab (should allow better packing of objs).
		 */
		flags |= CFLGS_OFF_SLAB;

	if (flags & SLAB_HWCACHE_ALIGN) {
		/* Need to adjust size so that objs are cache aligned. */
		/* Small obj size, can get at least two per cache line. */
		while (size < align/2)
			align /= 2;
		size = (size+align-1)&(~(align-1));
	}

	/* Cal size (in pages) of slabs, and the num of objs per slab.
	 * This could be made much more intelligent.  For now, try to avoid
	 * using high page-orders for slabs.  When the gfp() funcs are more
	 * friendly towards high-order requests, this should be changed.
	 */
	do {
		unsigned int break_flag = 0;
cal_wastage:
		cache_estimate(cachep->gfporder, size, flags,
						&left_over, &cachep->num);
		if (break_flag)
			break;
		if (cachep->gfporder >= MAX_GFP_ORDER)
			break;
		if (!cachep->num)
			goto next;
		if (flags & CFLGS_OFF_SLAB && cachep->num > offslab_limit) {
			/* Oops, this num of objs will cause problems. */
			cachep->gfporder--;
			break_flag++;
			goto cal_wastage;
		}

		/*
		 * Large num of objs is good, but v. large slabs are currently
		 * bad for the gfp()s.
		 */
		if (cachep->gfporder >= slab_break_gfp_order)
			break;

		if ((left_over*8) <= (PAGE_SIZE<<cachep->gfporder))
			break;	/* Acceptable internal fragmentation. */
next:
		cachep->gfporder++;
	} while (1);

	if (!cachep->num) {
		printk("kmem_cache_create: couldn't create cache %s.\n", name);
		kmem_cache_free(&cache_cache, cachep);
		cachep = NULL;
		goto opps;
	}
	slab_size = L1_CACHE_ALIGN(cachep->num*sizeof(kmem_bufctl_t)+sizeof(struct slab));

	/*
	 * If the slab has been placed off-slab, and we have enough space then
	 * move it on-slab. This is at the expense of any extra colouring.
	 */
	if (flags & CFLGS_OFF_SLAB && left_over >= slab_size) {
		flags &= ~CFLGS_OFF_SLAB;
		left_over -= slab_size;
	}

	/* Offset must be a multiple of the alignment. */
	offset += (align-1);
	offset &= ~(align-1);
	if (!offset)
		offset = L1_CACHE_BYTES;
	cachep->colour_off = offset;
	cachep->colour = left_over/offset;

	cachep->flags = flags;
	cachep->gfpflags = 0;
	if (flags & SLAB_CACHE_DMA)
		cachep->gfpflags |= GFP_DMA;
	spin_lock_init(&cachep->spinlock);
	cachep->objsize = size;
	/* NUMA */
	INIT_LIST_HEAD(&cachep->lists.slabs_full);
	INIT_LIST_HEAD(&cachep->lists.slabs_partial);
	INIT_LIST_HEAD(&cachep->lists.slabs_free);

	if (flags & CFLGS_OFF_SLAB)
		cachep->slabp_cache = kmem_find_general_cachep(slab_size,0);
	cachep->ctor = ctor;
	cachep->dtor = dtor;
	cachep->name = name;

	if (g_cpucache_up == FULL) {
		enable_cpucache(cachep);
	} else {
		if (g_cpucache_up == NONE) {
			/* Note: the first kmem_cache_create must create
			 * the cache that's used by kmalloc(24), otherwise
			 * the creation of further caches will BUG().
			 */
			cachep->array[smp_processor_id()] = &initarray_generic.cache;
			g_cpucache_up = PARTIAL;
		} else {
			cachep->array[smp_processor_id()] = kmalloc(sizeof(struct arraycache_init),GFP_KERNEL);
		}
		BUG_ON(!ac_data(cachep));
		ac_data(cachep)->avail = 0;
		ac_data(cachep)->limit = BOOT_CPUCACHE_ENTRIES;
		ac_data(cachep)->batchcount = 1;
		ac_data(cachep)->touched = 0;
		cachep->batchcount = 1;
		cachep->limit = BOOT_CPUCACHE_ENTRIES;
		cachep->free_limit = (1+num_online_cpus())*cachep->batchcount
					+ cachep->num;
	} 

	cachep->lists.next_reap = jiffies + REAPTIMEOUT_LIST3 +
					((unsigned long)cachep)%REAPTIMEOUT_LIST3;

	/* Need the semaphore to access the chain. */
	down(&cache_chain_sem);
	{
		struct list_head *p;
		mm_segment_t old_fs;

		old_fs = get_fs();
		set_fs(KERNEL_DS);
		list_for_each(p, &cache_chain) {
			kmem_cache_t *pc = list_entry(p, kmem_cache_t, next);
			char tmp;
			/* This happens when the module gets unloaded and doesn't
			   destroy its slab cache and noone else reuses the vmalloc
			   area of the module. Print a warning. */
			if (__get_user(tmp,pc->name)) { 
				printk("SLAB: cache with size %d has lost its name\n", 
					pc->objsize); 
				continue; 
			} 	
			if (!strcmp(pc->name,name)) { 
				printk("kmem_cache_create: duplicate cache %s\n",name); 
				up(&cache_chain_sem); 
				BUG(); 
			}	
		}
		set_fs(old_fs);
	}

	/* cache setup completed, link it into the list */
	list_add(&cachep->next, &cache_chain);
	up(&cache_chain_sem);
opps:
	return cachep;
}

static inline void check_irq_off(void)
{
#if DEBUG
	BUG_ON(!irqs_disabled());
#endif
}

static inline void check_irq_on(void)
{
#if DEBUG
	BUG_ON(irqs_disabled());
#endif
}

static inline void check_spinlock_acquired(kmem_cache_t *cachep)
{
#ifdef CONFIG_SMP
	check_irq_off();
	BUG_ON(spin_trylock(&cachep->spinlock));
#endif
}

/*
 * Waits for all CPUs to execute func().
 */
static void smp_call_function_all_cpus(void (*func) (void *arg), void *arg)
{
	check_irq_on();
	local_irq_disable();
	func(arg);
	local_irq_enable();

	if (smp_call_function(func, arg, 1, 1))
		BUG();
}

static void free_block (kmem_cache_t* cachep, void** objpp, int len);

static void do_drain(void *arg)
{
	kmem_cache_t *cachep = (kmem_cache_t*)arg;
	struct array_cache *ac;

	check_irq_off();
	ac = ac_data(cachep);
	free_block(cachep, &ac_entry(ac)[0], ac->avail);
	ac->avail = 0;
}

static void drain_cpu_caches(kmem_cache_t *cachep)
{
	smp_call_function_all_cpus(do_drain, cachep);
}


/* NUMA shrink all list3s */
static int __cache_shrink(kmem_cache_t *cachep)
{
	struct slab *slabp;
	int ret;

	drain_cpu_caches(cachep);

	check_irq_on();
	spin_lock_irq(&cachep->spinlock);

	for(;;) {
		struct list_head *p;

		p = cachep->lists.slabs_free.prev;
		if (p == &cachep->lists.slabs_free)
			break;

		slabp = list_entry(cachep->lists.slabs_free.prev, struct slab, list);
#if DEBUG
		if (slabp->inuse)
			BUG();
#endif
		list_del(&slabp->list);

		cachep->lists.free_objects -= cachep->num;
		spin_unlock_irq(&cachep->spinlock);
		slab_destroy(cachep, slabp);
		spin_lock_irq(&cachep->spinlock);
	}
	ret = !list_empty(&cachep->lists.slabs_full) ||
		!list_empty(&cachep->lists.slabs_partial);
	spin_unlock_irq(&cachep->spinlock);
	return ret;
}

/**
 * kmem_cache_shrink - Shrink a cache.
 * @cachep: The cache to shrink.
 *
 * Releases as many slabs as possible for a cache.
 * To help debugging, a zero exit status indicates all slabs were released.
 */
int kmem_cache_shrink(kmem_cache_t *cachep)
{
	if (!cachep || in_interrupt())
		BUG();

	return __cache_shrink(cachep);
}

/**
 * kmem_cache_destroy - delete a cache
 * @cachep: the cache to destroy
 *
 * Remove a kmem_cache_t object from the slab cache.
 * Returns 0 on success.
 *
 * It is expected this function will be called by a module when it is
 * unloaded.  This will remove the cache completely, and avoid a duplicate
 * cache being allocated each time a module is loaded and unloaded, if the
 * module doesn't have persistent in-kernel storage across loads and unloads.
 *
 * The cache must be empty before calling this function.
 *
 * The caller must guarantee that noone will allocate memory from the cache
 * during the kmem_cache_destroy().
 */
int kmem_cache_destroy (kmem_cache_t * cachep)
{
	if (!cachep || in_interrupt())
		BUG();

	/* Find the cache in the chain of caches. */
	down(&cache_chain_sem);
	/*
	 * the chain is never empty, cache_cache is never destroyed
	 */
	list_del(&cachep->next);
	up(&cache_chain_sem);

	if (__cache_shrink(cachep)) {
		printk(KERN_ERR "kmem_cache_destroy: Can't free all objects %p\n",
		       cachep);
		down(&cache_chain_sem);
		list_add(&cachep->next,&cache_chain);
		up(&cache_chain_sem);
		return 1;
	}
	{
		int i;
		for (i = 0; i < NR_CPUS; i++)
			kfree(cachep->array[i]);
		/* NUMA: free the list3 structures */
	}
	kmem_cache_free(&cache_cache, cachep);

	return 0;
}

/* Get the memory for a slab management obj. */
static inline struct slab* alloc_slabmgmt (kmem_cache_t *cachep,
			void *objp, int colour_off, int local_flags)
{
	struct slab *slabp;
	
	if (OFF_SLAB(cachep)) {
		/* Slab management obj is off-slab. */
		slabp = kmem_cache_alloc(cachep->slabp_cache, local_flags);
		if (!slabp)
			return NULL;
	} else {
		slabp = objp+colour_off;
		colour_off += L1_CACHE_ALIGN(cachep->num *
				sizeof(kmem_bufctl_t) + sizeof(struct slab));
	}
	slabp->inuse = 0;
	slabp->colouroff = colour_off;
	slabp->s_mem = objp+colour_off;

	return slabp;
}

static inline kmem_bufctl_t *slab_bufctl(struct slab *slabp)
{
	return (kmem_bufctl_t *)(slabp+1);
}

static void cache_init_objs (kmem_cache_t * cachep,
			struct slab * slabp, unsigned long ctor_flags)
{
	int i;

	for (i = 0; i < cachep->num; i++) {
		void* objp = slabp->s_mem+cachep->objsize*i;
#if DEBUG
		/* need to poison the objs? */
		if (cachep->flags & SLAB_POISON)
			poison_obj(cachep, objp, POISON_BEFORE);

		if (cachep->flags & SLAB_RED_ZONE) {
			*((unsigned long*)(objp)) = RED_INACTIVE;
			*((unsigned long*)(objp + cachep->objsize -
					BYTES_PER_WORD)) = RED_INACTIVE;
			objp += BYTES_PER_WORD;
		}
		/*
		 * Constructors are not allowed to allocate memory from
		 * the same cache which they are a constructor for.
		 * Otherwise, deadlock. They must also be threaded.
		 */
		if (cachep->ctor && !(cachep->flags & SLAB_POISON))
			cachep->ctor(objp, cachep, ctor_flags);

		if (cachep->flags & SLAB_RED_ZONE) {
			objp -= BYTES_PER_WORD;
			if (*((unsigned long*)(objp)) != RED_INACTIVE)
				slab_error(cachep, "constructor overwrote the"
							" start of an object");
			if (*((unsigned long*)(objp + cachep->objsize -
					BYTES_PER_WORD)) != RED_INACTIVE)
				slab_error(cachep, "constructor overwrote the"
							" end of an object");
		}
#else
		if (cachep->ctor)
			cachep->ctor(objp, cachep, ctor_flags);
#endif
		slab_bufctl(slabp)[i] = i+1;
	}
	slab_bufctl(slabp)[i-1] = BUFCTL_END;
	slabp->free = 0;
}

static void kmem_flagcheck(kmem_cache_t *cachep, int flags)
{
	if (flags & SLAB_DMA) {
		if (!(cachep->gfpflags & GFP_DMA))
			BUG();
	} else {
		if (cachep->gfpflags & GFP_DMA)
			BUG();
	}
}

/*
 * Grow (by 1) the number of slabs within a cache.  This is called by
 * kmem_cache_alloc() when there are no active objs left in a cache.
 */
static int cache_grow (kmem_cache_t * cachep, int flags)
{
	struct slab	*slabp;
	struct page	*page;
	void		*objp;
	size_t		 offset;
	unsigned int	 i, local_flags;
	unsigned long	 ctor_flags;

	/* Be lazy and only check for valid flags here,
 	 * keeping it out of the critical path in kmem_cache_alloc().
	 */
	if (flags & ~(SLAB_DMA|SLAB_LEVEL_MASK|SLAB_NO_GROW))
		BUG();
	if (flags & SLAB_NO_GROW)
		return 0;

	ctor_flags = SLAB_CTOR_CONSTRUCTOR;
	local_flags = (flags & SLAB_LEVEL_MASK);
	if (!(local_flags & __GFP_WAIT))
		/*
		 * Not allowed to sleep.  Need to tell a constructor about
		 * this - it might need to know...
		 */
		ctor_flags |= SLAB_CTOR_ATOMIC;

	/* About to mess with non-constant members - lock. */
	check_irq_off();
	spin_lock(&cachep->spinlock);

	/* Get colour for the slab, and cal the next value. */
	offset = cachep->colour_next;
	cachep->colour_next++;
	if (cachep->colour_next >= cachep->colour)
		cachep->colour_next = 0;
	offset *= cachep->colour_off;

	spin_unlock(&cachep->spinlock);

	if (local_flags & __GFP_WAIT)
		local_irq_enable();

	/*
	 * The test for missing atomic flag is performed here, rather than
	 * the more obvious place, simply to reduce the critical path length
	 * in kmem_cache_alloc(). If a caller is seriously mis-behaving they
	 * will eventually be caught here (where it matters).
	 */
	kmem_flagcheck(cachep, flags);


	/* Get mem for the objs. */
	if (!(objp = kmem_getpages(cachep, flags)))
		goto failed;

	/* Get slab management. */
	if (!(slabp = alloc_slabmgmt(cachep, objp, offset, local_flags)))
		goto opps1;

	/* Nasty!!!!!! I hope this is OK. */
	i = 1 << cachep->gfporder;
	page = virt_to_page(objp);
	do {
		SET_PAGE_CACHE(page, cachep);
		SET_PAGE_SLAB(page, slabp);
		SetPageSlab(page);
		inc_page_state(nr_slab);
		page++;
	} while (--i);

	cache_init_objs(cachep, slabp, ctor_flags);

	if (local_flags & __GFP_WAIT)
		local_irq_disable();
	check_irq_off();
	spin_lock(&cachep->spinlock);

	/* Make slab active. */
	list_add_tail(&slabp->list, &(list3_data(cachep)->slabs_free));
	STATS_INC_GROWN(cachep);
	list3_data(cachep)->free_objects += cachep->num;
	spin_unlock(&cachep->spinlock);
	return 1;
opps1:
	kmem_freepages(cachep, objp);
failed:
	if (local_flags & __GFP_WAIT)
		local_irq_disable();
	return 0;
}

/*
 * Perform extra freeing checks:
 * - detect bad pointers.
 * - POISON/RED_ZONE checking
 * - destructor calls, for caches with POISON+dtor
 */
static inline void kfree_debugcheck(const void *objp)
{
#if DEBUG
	struct page *page;

	if (!virt_addr_valid(objp)) {
		printk(KERN_ERR "kfree_debugcheck: out of range ptr %lxh.\n",
			(unsigned long)objp);	
		BUG();	
	}
	page = virt_to_page(objp);
	if (!PageSlab(page)) {
		printk(KERN_ERR "kfree_debugcheck: bad ptr %lxh.\n", (unsigned long)objp);
		BUG();
	}
#endif 
}

static inline void *cache_free_debugcheck (kmem_cache_t * cachep, void * objp)
{
#if DEBUG
	struct page *page;
	unsigned int objnr;
	struct slab *slabp;

	kfree_debugcheck(objp);
	page = virt_to_page(objp);

	BUG_ON(GET_PAGE_CACHE(page) != cachep);
	slabp = GET_PAGE_SLAB(page);

	if (cachep->flags & SLAB_RED_ZONE) {
		objp -= BYTES_PER_WORD;
		if (xchg((unsigned long *)objp, RED_INACTIVE) != RED_ACTIVE)
			slab_error(cachep, "double free, or memory before"
						" object was overwritten");
		if (xchg((unsigned long *)(objp+cachep->objsize -
				BYTES_PER_WORD), RED_INACTIVE) != RED_ACTIVE)
			slab_error(cachep, "double free, or memory after "
						" object was overwritten");
	}

	objnr = (objp-slabp->s_mem)/cachep->objsize;

	BUG_ON(objnr >= cachep->num);
	BUG_ON(objp != slabp->s_mem + objnr*cachep->objsize);

	if (cachep->flags & SLAB_DEBUG_INITIAL) {
		/* Need to call the slab's constructor so the
		 * caller can perform a verify of its state (debugging).
		 * Called without the cache-lock held.
		 */
		if (cachep->flags & SLAB_RED_ZONE) {
			cachep->ctor(objp+BYTES_PER_WORD,
					cachep, SLAB_CTOR_CONSTRUCTOR|SLAB_CTOR_VERIFY);
		} else {
			cachep->ctor(objp, cachep, SLAB_CTOR_CONSTRUCTOR|SLAB_CTOR_VERIFY);
		}
	}
	if (cachep->flags & SLAB_POISON && cachep->dtor) {
		/* we want to cache poison the object,
		 * call the destruction callback
		 */
		if (cachep->flags & SLAB_RED_ZONE)
			cachep->dtor(objp+BYTES_PER_WORD, cachep, 0);
		else
			cachep->dtor(objp, cachep, 0);
	}
	if (cachep->flags & SLAB_POISON)
		poison_obj(cachep, objp, POISON_AFTER);
#endif
	return objp;
}

static inline void check_slabp(kmem_cache_t *cachep, struct slab *slabp)
{
#if DEBUG
	int i;
	int entries = 0;
	
	check_spinlock_acquired(cachep);
	/* Check slab's freelist to see if this obj is there. */
	for (i = slabp->free; i != BUFCTL_END; i = slab_bufctl(slabp)[i]) {
		entries++;
		BUG_ON(entries > cachep->num);
	}
	BUG_ON(entries != cachep->num - slabp->inuse);
#endif
}

static inline void * cache_alloc_one_tail (kmem_cache_t *cachep,
						struct slab *slabp)
{
	void *objp;

	check_spinlock_acquired(cachep);

	STATS_INC_ALLOCED(cachep);
	STATS_INC_ACTIVE(cachep);
	STATS_SET_HIGH(cachep);

	/* get obj pointer */
	slabp->inuse++;
	objp = slabp->s_mem + slabp->free*cachep->objsize;
	slabp->free=slab_bufctl(slabp)[slabp->free];

	return objp;
}

static inline void cache_alloc_listfixup(struct kmem_list3 *l3, struct slab *slabp)
{
	list_del(&slabp->list);
	if (slabp->free == BUFCTL_END) {
		list_add(&slabp->list, &l3->slabs_full);
	} else {
		list_add(&slabp->list, &l3->slabs_partial);
	}
}

static void* cache_alloc_refill(kmem_cache_t* cachep, int flags)
{
	int batchcount;
	struct kmem_list3 *l3;
	struct array_cache *ac;

	check_irq_off();
	ac = ac_data(cachep);
retry:
	batchcount = ac->batchcount;
	if (!ac->touched && batchcount > BATCHREFILL_LIMIT) {
		/* if there was little recent activity on this
		 * cache, then perform only a partial refill.
		 * Otherwise we could generate refill bouncing.
		 */
		batchcount = BATCHREFILL_LIMIT;
	}
	l3 = list3_data(cachep);

	BUG_ON(ac->avail > 0);
	spin_lock(&cachep->spinlock);
	while (batchcount > 0) {
		struct list_head *entry;
		struct slab *slabp;
		/* Get slab alloc is to come from. */
		entry = l3->slabs_partial.next;
		if (entry == &l3->slabs_partial) {
			l3->free_touched = 1;
			entry = l3->slabs_free.next;
			if (entry == &l3->slabs_free)
				goto must_grow;
		}

		slabp = list_entry(entry, struct slab, list);
		check_slabp(cachep, slabp);
		while (slabp->inuse < cachep->num && batchcount--)
			ac_entry(ac)[ac->avail++] =
				cache_alloc_one_tail(cachep, slabp);
		check_slabp(cachep, slabp);
		cache_alloc_listfixup(l3, slabp);
	}

must_grow:
	l3->free_objects -= ac->avail;
	spin_unlock(&cachep->spinlock);

	if (unlikely(!ac->avail)) {
		int x;
		x = cache_grow(cachep, flags);
		
		// cache_grow can reenable interrupts, then ac could change.
		ac = ac_data(cachep);
		if (!x && ac->avail == 0)	// no objects in sight? abort
			return NULL;

		if (!ac->avail)		// objects refilled by interrupt?
			goto retry;
	}
	ac->touched = 1;
	return ac_entry(ac)[--ac->avail];
}

static inline void
cache_alloc_debugcheck_before(kmem_cache_t *cachep, int flags)
{
	if (flags & __GFP_WAIT)
		might_sleep();
#if DEBUG
	kmem_flagcheck(cachep, flags);
#endif
}

static inline void *
cache_alloc_debugcheck_after(kmem_cache_t *cachep,
			unsigned long flags, void *objp)
{
#if DEBUG
	if (!objp)	
		return objp;
	if (cachep->flags & SLAB_POISON)
		check_poison_obj(cachep, objp);
	if (cachep->flags & SLAB_RED_ZONE) {
		/* Set alloc red-zone, and check old one. */
		if (xchg((unsigned long *)objp, RED_ACTIVE) != RED_INACTIVE)
			slab_error(cachep, "memory before object was "
						"overwritten");
		if (xchg((unsigned long *)(objp+cachep->objsize -
			  BYTES_PER_WORD), RED_ACTIVE) != RED_INACTIVE)
			slab_error(cachep, "memory after object was "
						"overwritten");
		objp += BYTES_PER_WORD;
	}
	if (cachep->ctor && cachep->flags & SLAB_POISON) {
		unsigned long	ctor_flags = SLAB_CTOR_CONSTRUCTOR;

		if (!flags & __GFP_WAIT)
			ctor_flags |= SLAB_CTOR_ATOMIC;

		cachep->ctor(objp, cachep, ctor_flags);
	}	
#endif
	return objp;
}


static inline void * __cache_alloc (kmem_cache_t *cachep, int flags)
{
	unsigned long save_flags;
	void* objp;
	struct array_cache *ac;

	cache_alloc_debugcheck_before(cachep, flags);

	local_irq_save(save_flags);
	ac = ac_data(cachep);
	if (likely(ac->avail)) {
		STATS_INC_ALLOCHIT(cachep);
		ac->touched = 1;
		objp = ac_entry(ac)[--ac->avail];
	} else {
		STATS_INC_ALLOCMISS(cachep);
		objp = cache_alloc_refill(cachep, flags);
	}
	local_irq_restore(save_flags);
	objp = cache_alloc_debugcheck_after(cachep, flags, objp);
	return objp;
}

/* 
 * NUMA: different approach needed if the spinlock is moved into
 * the l3 structure
 */

static inline void
__free_block(kmem_cache_t *cachep, void **objpp, int nr_objects)
{
	int i;

	check_irq_off();
	spin_lock(&cachep->spinlock);

	/* NUMA: move add into loop */
	cachep->lists.free_objects += nr_objects;

	for (i = 0; i < nr_objects; i++) {
		void *objp = objpp[i];
		struct slab *slabp;
		unsigned int objnr;

		slabp = GET_PAGE_SLAB(virt_to_page(objp));
		list_del(&slabp->list);
		objnr = (objp - slabp->s_mem) / cachep->objsize;
		slab_bufctl(slabp)[objnr] = slabp->free;
		slabp->free = objnr;
		STATS_DEC_ACTIVE(cachep);
		slabp->inuse--;

		/* fixup slab chains */
		if (slabp->inuse == 0) {
			if (cachep->lists.free_objects > cachep->free_limit) {
				cachep->lists.free_objects -= cachep->num;
				slab_destroy(cachep, slabp);
			} else {
				list_add(&slabp->list,
				&list3_data_ptr(cachep, objp)->slabs_free);
			}
		} else {
			/* Unconditionally move a slab to the end of the
			 * partial list on free - maximum time for the
			 * other objects to be freed, too.
			 */
			list_add_tail(&slabp->list,
				&list3_data_ptr(cachep, objp)->slabs_partial);
		}
	}
	spin_unlock(&cachep->spinlock);
}

static void free_block(kmem_cache_t* cachep, void** objpp, int len)
{
	__free_block(cachep, objpp, len);
}

static void cache_flusharray (kmem_cache_t* cachep, struct array_cache *ac)
{
	int batchcount;

	batchcount = ac->batchcount;
#if DEBUG
	BUG_ON(!batchcount || batchcount > ac->avail);
#endif
	check_irq_off();
	__free_block(cachep, &ac_entry(ac)[0], batchcount);

#if STATS
	{
		int i = 0;
		struct list_head *p;

		spin_lock(&cachep->spinlock);
		p = list3_data(cachep)->slabs_free.next;
		while (p != &(list3_data(cachep)->slabs_free)) {
			struct slab *slabp;

			slabp = list_entry(p, struct slab, list);
			BUG_ON(slabp->inuse);

			i++;
			p = p->next;
		}
		STATS_SET_FREEABLE(cachep, i);
		spin_unlock(&cachep->spinlock);
	}
#endif
	ac->avail -= batchcount;
	memmove(&ac_entry(ac)[0], &ac_entry(ac)[batchcount],
			sizeof(void*)*ac->avail);
}

/*
 * __cache_free
 * Release an obj back to its cache. If the obj has a constructed
 * state, it must be in this state _before_ it is released.
 *
 * Called with disabled ints.
 */
static inline void __cache_free (kmem_cache_t *cachep, void* objp)
{
	struct array_cache *ac = ac_data(cachep);

	check_irq_off();
	objp = cache_free_debugcheck(cachep, objp);

	if (likely(ac->avail < ac->limit)) {
		STATS_INC_FREEHIT(cachep);
		ac_entry(ac)[ac->avail++] = objp;
		return;
	} else {
		STATS_INC_FREEMISS(cachep);
		cache_flusharray(cachep, ac);
		ac_entry(ac)[ac->avail++] = objp;
	}
}

/**
 * kmem_cache_alloc - Allocate an object
 * @cachep: The cache to allocate from.
 * @flags: See kmalloc().
 *
 * Allocate an object from this cache.  The flags are only relevant
 * if the cache has no available objects.
 */
void * kmem_cache_alloc (kmem_cache_t *cachep, int flags)
{
	return __cache_alloc(cachep, flags);
}

/**
 * kmalloc - allocate memory
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate.
 *
 * kmalloc is the normal method of allocating memory
 * in the kernel.
 *
 * The @flags argument may be one of:
 *
 * %GFP_USER - Allocate memory on behalf of user.  May sleep.
 *
 * %GFP_KERNEL - Allocate normal kernel ram.  May sleep.
 *
 * %GFP_ATOMIC - Allocation will not sleep.  Use inside interrupt handlers.
 *
 * Additionally, the %GFP_DMA flag may be set to indicate the memory
 * must be suitable for DMA.  This can mean different things on different
 * platforms.  For example, on i386, it means that the memory must come
 * from the first 16MB.
 */
void * kmalloc (size_t size, int flags)
{
	struct cache_sizes *csizep = malloc_sizes;

	for (; csizep->cs_size; csizep++) {
		if (size > csizep->cs_size)
			continue;
#if DEBUG
		/* This happens if someone tries to call
		 * kmem_cache_create(), or kmalloc(), before
		 * the generic caches are initialized.
		 */
		BUG_ON(csizep->cs_cachep == NULL);
#endif
		return __cache_alloc(flags & GFP_DMA ?
			 csizep->cs_dmacachep : csizep->cs_cachep, flags);
	}
	return NULL;
}

#ifdef CONFIG_SMP
/**
 * kmalloc_percpu - allocate one copy of the object for every present
 * cpu in the system.
 * Objects should be dereferenced using per_cpu_ptr/get_cpu_ptr
 * macros only.
 *
 * @size: how many bytes of memory are required.
 * @flags: the type of memory to allocate.
 * The @flags argument may be one of:
 *
 * %GFP_USER - Allocate memory on behalf of user.  May sleep.
 *
 * %GFP_KERNEL - Allocate normal kernel ram.  May sleep.
 *
 * %GFP_ATOMIC - Allocation will not sleep.  Use inside interrupt handlers.
 */
void *
kmalloc_percpu(size_t size, int flags)
{
	int i;
	struct percpu_data *pdata = kmalloc(sizeof (*pdata), flags);

	if (!pdata)
		return NULL;

	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;
		pdata->ptrs[i] = kmalloc(size, flags);
		if (!pdata->ptrs[i])
			goto unwind_oom;
	}

	/* Catch derefs w/o wrappers */
	return (void *) (~(unsigned long) pdata);

unwind_oom:
	while (--i >= 0) {
		if (!cpu_possible(i))
			continue;
		kfree(pdata->ptrs[i]);
	}
	kfree(pdata);
	return NULL;
}
#endif

/**
 * kmem_cache_free - Deallocate an object
 * @cachep: The cache the allocation was from.
 * @objp: The previously allocated object.
 *
 * Free an object which was previously allocated from this
 * cache.
 */
void kmem_cache_free (kmem_cache_t *cachep, void *objp)
{
	unsigned long flags;

	local_irq_save(flags);
	__cache_free(cachep, objp);
	local_irq_restore(flags);
}

/**
 * kfree - free previously allocated memory
 * @objp: pointer returned by kmalloc.
 *
 * Don't free memory not originally allocated by kmalloc()
 * or you will run into trouble.
 */
void kfree (const void *objp)
{
	kmem_cache_t *c;
	unsigned long flags;

	if (!objp)
		return;
	local_irq_save(flags);
	kfree_debugcheck(objp);
	c = GET_PAGE_CACHE(virt_to_page(objp));
	__cache_free(c, (void*)objp);
	local_irq_restore(flags);
}

#ifdef CONFIG_SMP
/**
 * kfree_percpu - free previously allocated percpu memory
 * @objp: pointer returned by kmalloc_percpu.
 *
 * Don't free memory not originally allocated by kmalloc_percpu()
 * The complemented objp is to check for that.
 */
void
kfree_percpu(const void *objp)
{
	int i;
	struct percpu_data *p = (struct percpu_data *) (~(unsigned long) objp);

	for (i = 0; i < NR_CPUS; i++) {
		if (!cpu_possible(i))
			continue;
		kfree(p->ptrs[i]);
	}
}
#endif

unsigned int kmem_cache_size(kmem_cache_t *cachep)
{
#if DEBUG
	if (cachep->flags & SLAB_RED_ZONE)
		return (cachep->objsize - 2*BYTES_PER_WORD);
#endif
	return cachep->objsize;
}

kmem_cache_t * kmem_find_general_cachep (size_t size, int gfpflags)
{
	struct cache_sizes *csizep = malloc_sizes;

	/* This function could be moved to the header file, and
	 * made inline so consumers can quickly determine what
	 * cache pointer they require.
	 */
	for ( ; csizep->cs_size; csizep++) {
		if (size > csizep->cs_size)
			continue;
		break;
	}
	return (gfpflags & GFP_DMA) ? csizep->cs_dmacachep : csizep->cs_cachep;
}

struct ccupdate_struct {
	kmem_cache_t *cachep;
	struct array_cache *new[NR_CPUS];
};

static void do_ccupdate_local(void *info)
{
	struct ccupdate_struct *new = (struct ccupdate_struct *)info;
	struct array_cache *old;

	check_irq_off();
	old = ac_data(new->cachep);
	
	new->cachep->array[smp_processor_id()] = new->new[smp_processor_id()];
	new->new[smp_processor_id()] = old;
}


static int do_tune_cpucache (kmem_cache_t* cachep, int limit, int batchcount)
{
	struct ccupdate_struct new;
	int i;

	memset(&new.new,0,sizeof(new.new));
	for (i = 0; i < NR_CPUS; i++) {
		struct array_cache *ccnew;

		ccnew = kmalloc(sizeof(void*)*limit+
				sizeof(struct array_cache), GFP_KERNEL);
		if (!ccnew) {
			for (i--; i >= 0; i--) kfree(new.new[i]);
			return -ENOMEM;
		}
		ccnew->avail = 0;
		ccnew->limit = limit;
		ccnew->batchcount = batchcount;
		ccnew->touched = 0;
		new.new[i] = ccnew;
	}
	new.cachep = cachep;

	smp_call_function_all_cpus(do_ccupdate_local, (void *)&new);
	
	check_irq_on();
	spin_lock_irq(&cachep->spinlock);
	cachep->batchcount = batchcount;
	cachep->limit = limit;
	cachep->free_limit = (1+num_online_cpus())*cachep->batchcount + cachep->num;
	spin_unlock_irq(&cachep->spinlock);

	for (i = 0; i < NR_CPUS; i++) {
		struct array_cache *ccold = new.new[i];
		if (!ccold)
			continue;
		local_irq_disable();
		free_block(cachep, ac_entry(ccold), ccold->avail);
		local_irq_enable();
		kfree(ccold);
	}
	return 0;
}


static void enable_cpucache (kmem_cache_t *cachep)
{
	int err;
	int limit;

	/* The head array serves three purposes:
	 * - create a LIFO ordering, i.e. return objects that are cache-warm
	 * - reduce the number of spinlock operations.
	 * - reduce the number of linked list operations on the slab and 
	 *   bufctl chains: array operations are cheaper.
	 * The numbers are guessed, we should auto-tune as described by
	 * Bonwick.
	 */
	if (cachep->objsize > PAGE_SIZE)
		limit = 8;
	else if (cachep->objsize > 1024)
		limit = 54;
	else if (cachep->objsize > 256)
		limit = 120;
	else
		limit = 248;

#ifndef DEBUG
	/* With debugging enabled, large batchcount lead to excessively
	 * long periods with disabled local interrupts. Limit the 
	 * batchcount
	 */
	if (limit > 32)
		limit = 32;
#endif
	err = do_tune_cpucache(cachep, limit, limit/2);
	if (err)
		printk(KERN_ERR "enable_cpucache failed for %s, error %d.\n",
					cachep->name, -err);
}

/**
 * cache_reap - Reclaim memory from caches.
 *
 * Called from a timer, every few seconds
 * Purpose:
 * - clear the per-cpu caches for this CPU.
 * - return freeable pages to the main free memory pool.
 *
 * If we cannot acquire the cache chain semaphore then just give up - we'll
 * try again next timer interrupt.
 */
static inline void cache_reap (void)
{
	struct list_head *walk;

#if DEBUG
	BUG_ON(!in_interrupt());
	BUG_ON(in_irq());
#endif
	if (down_trylock(&cache_chain_sem))
		return;

	list_for_each(walk, &cache_chain) {
		kmem_cache_t *searchp;
		struct list_head* p;
		int tofree;
		struct array_cache *ac;
		struct slab *slabp;

		searchp = list_entry(walk, kmem_cache_t, next);

		if (searchp->flags & SLAB_NO_REAP)
			goto next;

		check_irq_on();
		local_irq_disable();
		ac = ac_data(searchp);
		if (ac->touched) {
			ac->touched = 0;
		} else if (ac->avail) {
			tofree = (ac->limit+4)/5;
			if (tofree > ac->avail) {
				tofree = (ac->avail+1)/2;
			}
			free_block(searchp, ac_entry(ac), tofree);
			ac->avail -= tofree;
			memmove(&ac_entry(ac)[0], &ac_entry(ac)[tofree],
					sizeof(void*)*ac->avail);
		}
		if(time_after(searchp->lists.next_reap, jiffies))
			goto next_irqon;

		spin_lock(&searchp->spinlock);
		if(time_after(searchp->lists.next_reap, jiffies)) {
			goto next_unlock;
		}
		searchp->lists.next_reap = jiffies + REAPTIMEOUT_LIST3;
		if (searchp->lists.free_touched) {
			searchp->lists.free_touched = 0;
			goto next_unlock;
		}

		tofree = (searchp->free_limit+5*searchp->num-1)/(5*searchp->num);
		do {
			p = list3_data(searchp)->slabs_free.next;
			if (p == &(list3_data(searchp)->slabs_free))
				break;

			slabp = list_entry(p, struct slab, list);
			BUG_ON(slabp->inuse);
			list_del(&slabp->list);
			STATS_INC_REAPED(searchp);

			/* Safe to drop the lock. The slab is no longer
			 * linked to the cache.
			 * searchp cannot disappear, we hold
			 * cache_chain_lock
			 */
			searchp->lists.free_objects -= searchp->num;
			spin_unlock_irq(&searchp->spinlock);
			slab_destroy(searchp, slabp);
			spin_lock_irq(&searchp->spinlock);
		} while(--tofree > 0);
next_unlock:
		spin_unlock(&searchp->spinlock);
next_irqon:
		local_irq_enable();
next:
		;
	}
	check_irq_on();
	up(&cache_chain_sem);
}

/*
 * This is a timer handler.  There is on per CPU.  It is called periodially
 * to shrink this CPU's caches.  Otherwise there could be memory tied up
 * for long periods (or for ever) due to load changes.
 */
static void reap_timer_fnc(unsigned long data)
{
	int cpu = smp_processor_id();
	struct timer_list *rt = &reap_timers[cpu];

	cache_reap();
	mod_timer(rt, jiffies + REAPTIMEOUT_CPUC + cpu);
}

#ifdef CONFIG_PROC_FS

static void *s_start(struct seq_file *m, loff_t *pos)
{
	loff_t n = *pos;
	struct list_head *p;

	down(&cache_chain_sem);
	if (!n)
		return (void *)1;
	p = cache_chain.next;
	while (--n) {
		p = p->next;
		if (p == &cache_chain)
			return NULL;
	}
	return list_entry(p, kmem_cache_t, next);
}

static void *s_next(struct seq_file *m, void *p, loff_t *pos)
{
	kmem_cache_t *cachep = p;
	++*pos;
	if (p == (void *)1)
		return list_entry(cache_chain.next, kmem_cache_t, next);
	return cachep->next.next == &cache_chain ? NULL
		: list_entry(cachep->next.next, kmem_cache_t, next);
}

static void s_stop(struct seq_file *m, void *p)
{
	up(&cache_chain_sem);
}

static int s_show(struct seq_file *m, void *p)
{
	kmem_cache_t *cachep = p;
	struct list_head *q;
	struct slab	*slabp;
	unsigned long	active_objs;
	unsigned long	num_objs;
	unsigned long	active_slabs = 0;
	unsigned long	num_slabs;
	const char *name; 
	char *error = NULL;
	mm_segment_t old_fs;
	char tmp; 


	if (p == (void*)1) {
		/*
		 * Output format version, so at least we can change it
		 * without _too_ many complaints.
		 */
		seq_puts(m, "slabinfo - version: 1.2"
#if STATS
				" (statistics)"
#endif
				"\n");
		return 0;
	}

	check_irq_on();
	spin_lock_irq(&cachep->spinlock);
	active_objs = 0;
	num_slabs = 0;
	list_for_each(q,&cachep->lists.slabs_full) {
		slabp = list_entry(q, struct slab, list);
		if (slabp->inuse != cachep->num && !error)
			error = "slabs_full accounting error";
		active_objs += cachep->num;
		active_slabs++;
	}
	list_for_each(q,&cachep->lists.slabs_partial) {
		slabp = list_entry(q, struct slab, list);
		if (slabp->inuse == cachep->num && !error)
			error = "slabs_partial inuse accounting error";
		if (!slabp->inuse && !error)
			error = "slabs_partial/inuse accounting error";
		active_objs += slabp->inuse;
		active_slabs++;
	}
	list_for_each(q,&cachep->lists.slabs_free) {
		slabp = list_entry(q, struct slab, list);
		if (slabp->inuse && !error)
			error = "slabs_free/inuse accounting error";
		num_slabs++;
	}
	num_slabs+=active_slabs;
	num_objs = num_slabs*cachep->num;
	if (num_objs - active_objs != cachep->lists.free_objects && !error)
		error = "free_objects accounting error";

	name = cachep->name; 

	/*
	 * Check to see if `name' resides inside a module which has been
	 * unloaded (someone forgot to destroy their cache)
	 */
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	if (__get_user(tmp, name)) 
		name = "broken"; 
	set_fs(old_fs);

	if (error)
		printk(KERN_ERR "slab: cache %s error: %s\n", name, error);

	seq_printf(m, "%-17s %6lu %6lu %6u %4lu %4lu %4u",
		name, active_objs, num_objs, cachep->objsize,
		active_slabs, num_slabs, (1<<cachep->gfporder));

	seq_printf(m, " : %4u %4u", cachep->limit, cachep->batchcount);
#if STATS
	{	// list3 stats
		unsigned long high = cachep->high_mark;
		unsigned long allocs = cachep->num_allocations;
		unsigned long grown = cachep->grown;
		unsigned long reaped = cachep->reaped;
		unsigned long errors = cachep->errors;
		unsigned long max_freeable = cachep->max_freeable;
		unsigned long free_limit = cachep->free_limit;

		seq_printf(m, " : %6lu %7lu %5lu %4lu %4lu %4lu %4lu",
				high, allocs, grown, reaped, errors, 
				max_freeable, free_limit);
	}
	{	// cpucache stats
		unsigned long allochit = atomic_read(&cachep->allochit);
		unsigned long allocmiss = atomic_read(&cachep->allocmiss);
		unsigned long freehit = atomic_read(&cachep->freehit);
		unsigned long freemiss = atomic_read(&cachep->freemiss);

		seq_printf(m, " : %6lu %6lu %6lu %6lu",
				allochit, allocmiss, freehit, freemiss);
	}
#endif
	spin_unlock_irq(&cachep->spinlock);
	seq_putc(m, '\n');
	return 0;
}

/*
 * slabinfo_op - iterator that generates /proc/slabinfo
 *
 * Output layout:
 * cache-name
 * num-active-objs
 * total-objs
 * object size
 * num-active-slabs
 * total-slabs
 * num-pages-per-slab
 * + further values on SMP and with statistics enabled
 */

struct seq_operations slabinfo_op = {
	.start	= s_start,
	.next	= s_next,
	.stop	= s_stop,
	.show	= s_show,
};

#define MAX_SLABINFO_WRITE 128
/**
 * slabinfo_write - SMP tuning for the slab allocator
 * @file: unused
 * @buffer: user buffer
 * @count: data len
 * @data: unused
 */
ssize_t slabinfo_write(struct file *file, const char *buffer,
				size_t count, loff_t *ppos)
{
	char kbuf[MAX_SLABINFO_WRITE+1], *tmp;
	int limit, batchcount, res;
	struct list_head *p;
	
	if (count > MAX_SLABINFO_WRITE)
		return -EINVAL;
	if (copy_from_user(&kbuf, buffer, count))
		return -EFAULT;
	kbuf[MAX_SLABINFO_WRITE] = '\0'; 

	tmp = strchr(kbuf, ' ');
	if (!tmp)
		return -EINVAL;
	*tmp = '\0';
	tmp++;
	limit = simple_strtol(tmp, &tmp, 10);
	while (*tmp == ' ')
		tmp++;
	batchcount = simple_strtol(tmp, &tmp, 10);

	/* Find the cache in the chain of caches. */
	down(&cache_chain_sem);
	res = -EINVAL;
	list_for_each(p,&cache_chain) {
		kmem_cache_t *cachep = list_entry(p, kmem_cache_t, next);

		if (!strcmp(cachep->name, kbuf)) {
			if (limit < 1 ||
			    batchcount < 1 ||
			    batchcount > limit) {
				res = -EINVAL;
			} else {
				res = do_tune_cpucache(cachep, limit, batchcount);
			}
			break;
		}
	}
	up(&cache_chain_sem);
	if (res >= 0)
		res = count;
	return res;
}
#endif

unsigned int ksize(const void *objp)
{
	kmem_cache_t *c;
	unsigned long flags;
	unsigned int size = 0;

	if (likely(objp != NULL)) {
		local_irq_save(flags);
		c = GET_PAGE_CACHE(virt_to_page(objp));
		size = kmem_cache_size(c);
		local_irq_restore(flags);
	}

	return size;
}

