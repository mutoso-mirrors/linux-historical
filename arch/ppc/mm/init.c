/*
 *  arch/ppc/mm/init.c
 *
 *  PowerPC version 
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Modifications by Paul Mackerras (PowerMac) (paulus@cs.anu.edu.au)
 *  and Cort Dougan (PReP) (cort@cs.nmt.edu)
 *    Copyright (C) 1996 Paul Mackerras
 *
 *  Derived from "arch/i386/mm/init.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/head.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/stddef.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/residual.h>
#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>		/* for initrd_* */
#endif

int prom_trashed;
int next_mmu_context;
unsigned long _SDR1;
PTE *Hash, *Hash_end;
unsigned long Hash_size, Hash_mask;
unsigned long *end_of_DRAM;
int mem_init_done;
extern pgd_t swapper_pg_dir[];
extern char _start[], _end[];
extern char etext[], _stext[];
extern char __init_begin, __init_end;
extern RESIDUAL res;
char *klimit = _end;
struct device_node *memory_node;

void *find_mem_piece(unsigned, unsigned);
static void mapin_ram(void);
static void hash_init(void);
static void *MMU_get_page(void);
void map_page(struct task_struct *, unsigned long va,
		     unsigned long pa, int flags);
extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);
extern unsigned long *find_end_of_memory(void);

extern struct task_struct *current_set[NR_CPUS];

/*
 * this tells the system to map all of ram with the segregs
 * (i.e. page tables) instead of the bats.
 */
#undef MAP_RAM_WITH_SEGREGS 1

/* optimization for 603 to load the tlb directly from the linux table */
#define NO_RELOAD_HTAB 1 /* change in kernel/head.S too! */

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
unsigned long empty_bad_page_table;

pte_t * __bad_pagetable(void)
{
	memset((void *)empty_bad_page_table, 0, PAGE_SIZE);
	return (pte_t *) empty_bad_page_table;
}

unsigned long empty_bad_page;

pte_t __bad_page(void)
{
	memset((void *)empty_bad_page, 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte(empty_bad_page, PAGE_SHARED));
}

/*
 * The following stuff defines a data structure for representing
 * areas of memory as an array of (address, length) pairs, and
 * procedures for manipulating them.
 */
#define MAX_MEM_REGIONS	32

struct mem_pieces {
	int n_regions;
	struct reg_property regions[MAX_MEM_REGIONS];
};
struct mem_pieces phys_mem;
struct mem_pieces phys_avail;
struct mem_pieces prom_mem;

static void get_mem_prop(char *, struct mem_pieces *);
static void sort_mem_pieces(struct mem_pieces *);
static void coalesce_mem_pieces(struct mem_pieces *);
static void append_mem_piece(struct mem_pieces *, unsigned, unsigned);
static void remove_mem_piece(struct mem_pieces *, unsigned, unsigned, int);
static void print_mem_pieces(struct mem_pieces *);

static void
sort_mem_pieces(struct mem_pieces *mp)
{
	unsigned long a, s;
	int i, j;

	for (i = 1; i < mp->n_regions; ++i) {
		a = mp->regions[i].address;
		s = mp->regions[i].size;
		for (j = i - 1; j >= 0; --j) {
			if (a >= mp->regions[j].address)
				break;
			mp->regions[j+1] = mp->regions[j];
		}
		mp->regions[j+1].address = a;
		mp->regions[j+1].size = s;
	}
}

static void
coalesce_mem_pieces(struct mem_pieces *mp)
{
	unsigned long a, e;
	int i, j, d;

	d = 0;
	for (i = 0; i < mp->n_regions; i = j) {
		a = mp->regions[i].address;
		e = a + mp->regions[i].size;
		for (j = i + 1; j < mp->n_regions
			     && mp->regions[j].address <= e; ++j)
			e = mp->regions[j].address + mp->regions[j].size;
		mp->regions[d].address = a;
		mp->regions[d].size = e - a;
		++d;
	}
	mp->n_regions = d;
}

/*
 * Add some memory to an array of pieces
 */
static void
append_mem_piece(struct mem_pieces *mp, unsigned start, unsigned size)
{
	struct reg_property *rp;

	if (mp->n_regions >= MAX_MEM_REGIONS)
		return;
	rp = &mp->regions[mp->n_regions++];
	rp->address = start;
	rp->size = size;
}

/*
 * Remove some memory from an array of pieces
 */
static void
remove_mem_piece(struct mem_pieces *mp, unsigned start, unsigned size,
		 int must_exist)
{
	int i, j;
	unsigned end, rs, re;
	struct reg_property *rp;

	end = start + size;
	for (i = 0, rp = mp->regions; i < mp->n_regions; ++i, ++rp) {
		if (end > rp->address && start < rp->address + rp->size)
			break;
	}
	if (i >= mp->n_regions) {
		if (must_exist)
			printk("remove_mem_piece: [%x,%x) not in any region\n",
			       start, end);
		return;
	}
	for (; i < mp->n_regions && end > rp->address; ++i, ++rp) {
		rs = rp->address;
		re = rs + rp->size;
		if (must_exist && (start < rs || end > re)) {
			printk("remove_mem_piece: bad overlap [%x,%x) with",
			       start, end);
			print_mem_pieces(mp);
			must_exist = 0;
		}
		if (start > rs) {
			rp->size = start - rs;
			if (end < re) {
				/* need to split this entry */
				if (mp->n_regions >= MAX_MEM_REGIONS)
					panic("eek... mem_pieces overflow");
				for (j = mp->n_regions; j > i + 1; --j)
					mp->regions[j] = mp->regions[j-1];
				++mp->n_regions;
				rp[1].address = end;
				rp[1].size = re - end;
			}
		} else {
			if (end < re) {
				rp->address = end;
				rp->size = re - end;
			} else {
				/* need to delete this entry */
				for (j = i; j < mp->n_regions - 1; ++j)
					mp->regions[j] = mp->regions[j+1];
				--mp->n_regions;
				--i;
				--rp;
			}
		}
	}
}

static void
print_mem_pieces(struct mem_pieces *mp)
{
	int i;

	for (i = 0; i < mp->n_regions; ++i)
		printk(" [%x, %x)", mp->regions[i].address,
		       mp->regions[i].address + mp->regions[i].size);
	printk("\n");
}

/*
 * Scan a region for a piece of a given size with the required alignment.
 */
void *
find_mem_piece(unsigned size, unsigned align)
{
	int i;
	unsigned a, e;
	struct mem_pieces *mp = &phys_avail;

	for (i = 0; i < mp->n_regions; ++i) {
		a = mp->regions[i].address;
		e = a + mp->regions[i].size;
		a = (a + align - 1) & -align;
		if (a + size <= e) {
			remove_mem_piece(mp, a, size, 1);
			return __va(a);
		}
	}
	printk("Couldn't find %u bytes at %u alignment\n", size, align);
	abort();
	return NULL;
}

/*
 * Read in a property describing some pieces of memory.
 */
static void
get_mem_prop(char *name, struct mem_pieces *mp)
{
	struct reg_property *rp;
	int s;

	rp = (struct reg_property *) get_property(memory_node, name, &s);
	if (rp == NULL) {
		printk(KERN_ERR "error: couldn't get %s property on /memory\n",
		       name);
		abort();
	}
	mp->n_regions = s / sizeof(mp->regions[0]);
	memcpy(mp->regions, rp, s);

	/* Make sure the pieces are sorted. */
	sort_mem_pieces(mp);
	coalesce_mem_pieces(mp);
}

/*
 * On systems with Open Firmware, collect information about
 * physical RAM and which pieces are already in use.
 * At this point, we have (at least) the first 8MB mapped with a BAT.
 * Our text, data, bss use something over 1MB, starting at 0.
 * Open Firmware may be using 1MB at the 4MB point.
 */
unsigned long *pmac_find_end_of_memory(void)
{
	unsigned long a, total;
	unsigned long kstart, ksize;
	int i;

	memory_node = find_devices("memory");
	if (memory_node == NULL) {
		printk(KERN_ERR "can't find memory node\n");
		abort();
	}

	/*
	 * Find out where physical memory is, and check that it
	 * starts at 0 and is contiguous.  It seems that RAM is
	 * always physically contiguous on Power Macintoshes,
	 * because MacOS can't cope if it isn't.
	 *
	 * Supporting discontiguous physical memory isn't hard,
	 * it just makes the virtual <-> physical mapping functions
	 * more complicated (or else you end up wasting space
	 * in mem_map).
	 */
	get_mem_prop("reg", &phys_mem);
	if (phys_mem.n_regions == 0)
		panic("No RAM??");
	a = phys_mem.regions[0].address;
	if (a != 0)
		panic("RAM doesn't start at physical address 0");
	total = phys_mem.regions[0].size;
	if (phys_mem.n_regions > 1) {
		printk("RAM starting at 0x%x is not contiguous\n",
		       phys_mem.regions[1].address);
		printk("Using RAM from 0 to 0x%lx\n", total-1);
		phys_mem.n_regions = 1;
	}

	/* record which bits the prom is using */
	get_mem_prop("available", &phys_avail);
	prom_mem = phys_mem;
	for (i = 0; i < phys_avail.n_regions; ++i)
		remove_mem_piece(&prom_mem, phys_avail.regions[i].address,
				 phys_avail.regions[i].size, 1);

	/*
	 * phys_avail records memory we can use now.
	 * prom_mem records memory allocated by the prom that we
	 * don't want to use now, but we'll reclaim later.
	 * Make sure the kernel text/data/bss is in neither.
	 */
	kstart = __pa(_stext);	/* should be 0 */
	ksize = PAGE_ALIGN(klimit - _stext);
	remove_mem_piece(&phys_avail, kstart, ksize, 0);
	remove_mem_piece(&prom_mem, kstart, ksize, 0);
	remove_mem_piece(&phys_avail, 0, 0x4000, 0);
	remove_mem_piece(&prom_mem, 0, 0x4000, 0);

	return __va(total);
}

/*
 * Find some memory for setup_arch to return.
 * We use the last chunk of available memory as the area
 * that setup_arch returns, making sure that there are at
 * least 32 pages unused before this for MMU_get_page to use.
 */
unsigned long avail_start;

unsigned long find_available_memory(void)
{
	int i;
	unsigned long a, free;
	unsigned long start, end;

	free = 0;
	for (i = 0; i < phys_avail.n_regions - 1; ++i) {
		start = phys_avail.regions[i].address;
		end = start + phys_avail.regions[i].size;
		free += (end & PAGE_MASK) - PAGE_ALIGN(start);
	}
	a = PAGE_ALIGN(phys_avail.regions[i].address);
	if (free < 32 * PAGE_SIZE)
		a += 32 * PAGE_SIZE - free;
	avail_start = (unsigned long) __va(a);
	return avail_start;
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;
	struct task_struct *p;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6dkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (!atomic_read(&mem_map[i].count))
			free++;
		else
			shared += atomic_read(&mem_map[i].count) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	show_buffers();
#ifdef CONFIG_NET
	show_net_buffers();
#endif
	printk("%-8s %3s %3s %8s %8s %8s %9s %8s", "Process", "Pid", "Cnt",
	       "Ctx", "Ctx<<4", "Last Sys", "pc", "task");
#ifdef __SMP__
	printk(" %3s", "CPU");
#endif /* __SMP__ */
	printk("\n");
	for_each_task(p)
	{	
		printk("%-8.8s %3d %3d %8ld %8ld %8ld %c%08lx %08lx ",
		       p->comm,p->pid,
		       p->mm->count,p->mm->context,
		       p->mm->context<<4, p->tss.last_syscall,
		       user_mode(p->tss.regs) ? 'u' : 'k', p->tss.regs->nip,
		       (ulong)p);
		{
			int iscur = 0;
#ifdef __SMP__
			printk("%3d ", p->processor);
			if ( (p->processor != NO_PROC_ID) &&
			     (p == current_set[p->processor]) )
			
#else		
			if ( p == current )
#endif /* __SMP__ */
			{
				iscur = 1;
				printk("current");
			}
			if ( p == last_task_used_math )
			{
				if ( iscur )
					printk(",");
				printk("last math");
			}			
			printk("\n");
		}
	}
}

extern unsigned long free_area_init(unsigned long, unsigned long);

/*
 * paging_init() sets up the page tables - in fact we've already done this.
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	/*
	 * Grab some memory for bad_page and bad_pagetable to use.
	 */
	empty_bad_page = PAGE_ALIGN(start_mem);
	empty_bad_page_table = empty_bad_page + PAGE_SIZE;
	start_mem = empty_bad_page + 2 * PAGE_SIZE;

	/* note: free_area_init uses its second argument
	   to size the mem_map array. */
	start_mem = free_area_init(start_mem, end_mem);
	return start_mem;
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long addr;
	int i;
	unsigned long a, lim;
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;

	end_mem &= PAGE_MASK;
	high_memory = (void *) end_mem;
	max_mapnr = MAP_NR(high_memory);
	num_physpages = max_mapnr;	/* RAM is assumed contiguous */

	/* mark usable pages in the mem_map[] */
	start_mem = PAGE_ALIGN(start_mem);

	remove_mem_piece(&phys_avail, __pa(avail_start),
			 start_mem - avail_start, 1);

	for (addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE)
		set_bit(PG_reserved, &mem_map[MAP_NR(addr)].flags);

	for (i = 0; i < phys_avail.n_regions; ++i) {
		a = (unsigned long) __va(phys_avail.regions[i].address);
		lim = a + phys_avail.regions[i].size;
		a = PAGE_ALIGN(a);
		for (; a < lim; a += PAGE_SIZE)
			clear_bit(PG_reserved, &mem_map[MAP_NR(a)].flags);
	}
	phys_avail.n_regions = 0;

	/* free the prom's memory - no-op on prep */
	for (i = 0; i < prom_mem.n_regions; ++i) {
		a = (unsigned long) __va(prom_mem.regions[i].address);
		lim = a + prom_mem.regions[i].size;
		a = PAGE_ALIGN(a);
		for (; a < lim; a += PAGE_SIZE)
			clear_bit(PG_reserved, &mem_map[MAP_NR(a)].flags);
	}
	prom_trashed = 1;

	for (addr = PAGE_OFFSET; addr < end_mem; addr += PAGE_SIZE) {
		if (PageReserved(mem_map + MAP_NR(addr))) {
			if (addr < (ulong) etext)
				codepages++;
			else if (addr >= (unsigned long)&__init_begin
				 && addr < (unsigned long)&__init_end)
                                initpages++;
                        else if (addr < (ulong) start_mem)
				datapages++;
			continue;
		}
		atomic_set(&mem_map[MAP_NR(addr)].count, 1);
#ifdef CONFIG_BLK_DEV_INITRD
		if (!initrd_start ||
		    addr < (initrd_start & PAGE_MASK) || addr >= initrd_end)
#endif /* CONFIG_BLK_DEV_INITRD */
			free_page(addr);
	}

        printk("Memory: %luk available (%dk kernel code, %dk data, %dk init) [%08x,%08lx]\n",
	       (unsigned long) nr_free_pages << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10), 
	       initpages << (PAGE_SHIFT-10),
	       PAGE_OFFSET, end_mem);
	mem_init_done = 1;
}

/*
 * Unfortunately, we can't put initialization functions in their
 * own section and free that at this point, because gas gets some
 * relocations wrong if we do. :-(  But this code is here for when
 * gas gets fixed.
 */
void free_initmem(void)
{
	unsigned long a;
	unsigned long num_freed_pages = 0;

	a = (unsigned long)(&__init_begin);
	for (; a < (unsigned long)(&__init_end); a += PAGE_SIZE) {
		clear_bit(PG_reserved, &mem_map[MAP_NR(a)].flags);
		atomic_set(&mem_map[MAP_NR(a)].count, 1);
		free_page(a);
		num_freed_pages++;
	}

	printk ("Freeing unused kernel memory: %ldk freed\n",
		(num_freed_pages * PAGE_SIZE) >> 10);
}

void si_meminfo(struct sysinfo *val)
{
	int i;

	i = max_mapnr;
	val->totalram = 0;
	val->sharedram = 0;
	val->freeram = nr_free_pages << PAGE_SHIFT;
	val->bufferram = buffermem;
	while (i-- > 0)  {
		if (PageReserved(mem_map+i))
			continue;
		val->totalram++;
		if (!atomic_read(&mem_map[i].count))
			continue;
		val->sharedram += atomic_read(&mem_map[i].count) - 1;
	}
	val->totalram <<= PAGE_SHIFT;
	val->sharedram <<= PAGE_SHIFT;
	return;
}

union ubat {			/* BAT register values to be loaded */
	BAT	bat;
	P601_BAT bat_601;
	u32	word[2];
} BATS[4][2];			/* 4 pairs of IBAT, DBAT */

struct batrange {		/* stores address ranges mapped by BATs */
	unsigned long start;
	unsigned long limit;
	unsigned long phys;
} bat_addrs[4];

/*
 * Set up one of the I/D BAT (block address translation) register pairs.
 * The parameters are not checked; in particular size must be a power
 * of 2 between 128k and 256M.
 */
void
setbat(int index, unsigned long virt, unsigned long phys,
       unsigned int size, int flags)
{
	unsigned int bl;
	int wimgxpp;
	union ubat *bat = BATS[index];

	bl = (size >> 17) - 1;
	if ((_get_PVR() >> 16) != 1) {
		/* 603, 604, etc. */
		/* Do DBAT first */
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT | _PAGE_GUARDED);
		wimgxpp |= (flags & _PAGE_RW)? BPP_RW: BPP_RX;
		bat[1].word[0] = virt | (bl << 2) | 2; /* Vs=1, Vp=0 */
		bat[1].word[1] = phys | wimgxpp;
		if (flags & _PAGE_USER)
			bat[1].bat.batu.vp = 1;
		if (flags & _PAGE_GUARDED) {
			/* G bit must be zero in IBATs */
			bat[0].word[0] = bat[0].word[1] = 0;
		} else {
			/* make IBAT same as DBAT */
			bat[0] = bat[1];
		}
	} else {
		/* 601 cpu */
		if (bl > BL_8M)
			bl = BL_8M;
		wimgxpp = flags & (_PAGE_WRITETHRU | _PAGE_NO_CACHE
				   | _PAGE_COHERENT);
		wimgxpp |= (flags & _PAGE_RW)?
			((flags & _PAGE_USER)? PP_RWRW: PP_RWXX): PP_RXRX;
		bat->word[0] = virt | wimgxpp | 4;	/* Ks=0, Ku=1 */
		bat->word[1] = phys | bl | 0x40;	/* V=1 */
	}

	bat_addrs[index].start = virt;
	bat_addrs[index].limit = virt + ((bl + 1) << 17) - 1;
	bat_addrs[index].phys = phys;
}

#define IO_PAGE	(_PAGE_NO_CACHE | _PAGE_GUARDED | _PAGE_RW)
#ifdef __SMP__
#define RAM_PAGE (_PAGE_COHERENT | _PAGE_RW)
#else
#define RAM_PAGE (_PAGE_RW)
#endif

/*
 * This finds the amount of physical ram and does necessary
 * setup for prep.  This is pretty architecture specific so
 * this will likely stay seperate from the pmac.
 * -- Cort
 */
unsigned long *prep_find_end_of_memory(void)
{
	unsigned long kstart, ksize;
	unsigned long total;
	total = res.TotalMemory;

	if (total == 0 )
	{
		/*
		 * I need a way to probe the amount of memory if the residual
		 * data doesn't contain it. -- Cort
		 */
		printk("Ramsize from residual data was 0 -- Probing for value\n");
		total = 0x02000000;
		printk("Ramsize default to be %ldM\n", total>>20);
	}
	append_mem_piece(&phys_mem, 0, total);
	phys_avail = phys_mem;
	kstart = __pa(_stext);	/* should be 0 */
	ksize = PAGE_ALIGN(klimit - _stext);
	remove_mem_piece(&phys_avail, kstart, ksize, 0);
	remove_mem_piece(&phys_avail, 0, 0x4000, 0);

	return (__va(total));
}


/*
 * Map in all of physical memory starting at KERNELBASE.
 */
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_ACCESSED)

static void mapin_ram()
{
	int i;
	unsigned long tot, bl, done;
	unsigned long v, p, s, f;

#ifndef MAP_RAM_WITH_SEGREGS
	/* Set up BAT2 and if necessary BAT3 to cover RAM. */
	tot = (unsigned long)end_of_DRAM - KERNELBASE;
	for (bl = 128<<10; bl < 256<<20; bl <<= 1)
		if (bl * 2 > tot)
			break;
	setbat(2, KERNELBASE, 0, bl, RAM_PAGE);
	done = __pa(bat_addrs[2].limit) + 1;
	if (done < tot) {
		/* use BAT3 to cover a bit more */
		tot -= done;
		for (bl = 128<<10; bl < 256<<20; bl <<= 1)
			if (bl * 2 > tot)
				break;
		setbat(3, KERNELBASE+done, done, bl, RAM_PAGE);
	}
#endif

	v = KERNELBASE;
	for (i = 0; i < phys_mem.n_regions; ++i) {
		p = phys_mem.regions[i].address;
		for (s = 0; s < phys_mem.regions[i].size; s += PAGE_SIZE) {
			f = _PAGE_PRESENT | _PAGE_ACCESSED;
			if ((char *) v < _stext || (char *) v >= etext)
				f |= _PAGE_RW | _PAGE_DIRTY | _PAGE_HWWRITE;
			else
				/* On the powerpc, no user access
				   forces R/W kernel access */
				f |= _PAGE_USER;
			map_page(&init_task, v, p, f);
			v += PAGE_SIZE;
			p += PAGE_SIZE;
		}
	}	    
}

/*
 * Initialize the hash table and patch the instructions in head.S.
 */
static void hash_init(void)
{
	int Hash_bits;
	unsigned long h, ramsize;

	extern unsigned int hash_page_patch_A[], hash_page_patch_B[],
		hash_page_patch_C[], hash_page_patch_D[];

	/*
	 * Allow 64k of hash table for every 16MB of memory,
	 * up to a maximum of 2MB.
	 */
	ramsize = (ulong)end_of_DRAM - KERNELBASE;
	for (h = 64<<10; h < ramsize / 256 && h < 2<<20; h *= 2)
		;
	Hash_size = h;
	Hash_mask = (h >> 6) - 1;
	
#ifdef NO_RELOAD_HTAB
	/* shrink the htab since we don't use it on 603's -- Cort */
	switch (_get_PVR()>>16) {
	case 3: /* 603 */
	case 6: /* 603e */
	case 7: /* 603ev */
		Hash_size = 0;
		Hash_mask = 0;
		break;
	default:
	        /* on 601/4 let things be */
		break;
 	}
#endif /* NO_RELOAD_HTAB */
	
	/* Find some memory for the hash table. */
	if ( Hash_size )
		Hash = find_mem_piece(Hash_size, Hash_size);
	else
		Hash = 0;

	printk("Total memory = %ldMB; using %ldkB for hash table (at %p)\n",
	       ramsize >> 20, Hash_size >> 10, Hash);
	if ( Hash_size )
	{
		memset(Hash, 0, Hash_size);
		Hash_end = (PTE *) ((unsigned long)Hash + Hash_size);

		/*
		 * Patch up the instructions in head.S:hash_page
		 */
		Hash_bits = ffz(~Hash_size) - 6;
		hash_page_patch_A[0] = (hash_page_patch_A[0] & ~0xffff)
			| (__pa(Hash) >> 16);
		hash_page_patch_A[1] = (hash_page_patch_A[1] & ~0x7c0)
			| ((26 - Hash_bits) << 6);
		if (Hash_bits > 16)
			Hash_bits = 16;
		hash_page_patch_A[2] = (hash_page_patch_A[2] & ~0x7c0)
			| ((26 - Hash_bits) << 6);
		hash_page_patch_B[0] = (hash_page_patch_B[0] & ~0xffff)
			| (Hash_mask >> 10);
		hash_page_patch_C[0] = (hash_page_patch_C[0] & ~0xffff)
			| (Hash_mask >> 10);
		hash_page_patch_D[0] = (hash_page_patch_D[0] & ~0xffff)
			| (Hash_mask >> 10);
		/*
		 * Ensure that the locations we've patched have been written
		 * out from the data cache and invalidated in the instruction
		 * cache, on those machines with split caches.
		 */
		flush_icache_range((unsigned long) hash_page_patch_A,
				   (unsigned long) (hash_page_patch_D + 1));
	}
	else
		Hash_end = 0;

}


/*
 * Do very early mm setup such as finding the size of memory
 * and setting up the hash table.
 * A lot of this is prep/pmac specific but a lot of it could
 * still be merged.
 * -- Cort
 */
void
MMU_init(void)
{
	if (have_of)
		end_of_DRAM = pmac_find_end_of_memory();
	else /* prep */
		end_of_DRAM = prep_find_end_of_memory();

        hash_init();
        _SDR1 = __pa(Hash) | (Hash_mask >> 10);

	/* Map in all of RAM starting at KERNELBASE */
	mapin_ram();

	/*
	 * Setup the bat mappings we're going to load that cover
	 * the io areas.  RAM was mapped by mapin_ram().
	 * -- Cort
	 */
	switch (_machine) {
	case _MACH_prep:
		setbat(0, 0x80000000, 0x80000000, 0x10000000,
		       IO_PAGE + ((_prep_type == _PREP_IBM)? _PAGE_USER: 0));
		setbat(1, 0xd0000000, 0xc0000000, 0x10000000,
		       IO_PAGE + ((_prep_type == _PREP_IBM)? _PAGE_USER: 0));
		break;
	case _MACH_chrp:
		setbat(0, 0xc0000000, 0xc0000000, 0x10000000, IO_PAGE);
		setbat(1, 0xf8000000, 0xf8000000, 0x20000, IO_PAGE);
		setbat(3, 0x80000000, 0x80000000, 0x10000000, IO_PAGE);
		break;
	case _MACH_Pmac:
		setbat(0, 0xf3000000, 0xf3000000, 0x100000, IO_PAGE);
		/* this is used to cover registers used by smp boards -- Cort */
		setbat(3, 0xf8000000, 0xf8000000, 0x100000, IO_PAGE);
		break;
	}
}

static void *
MMU_get_page()
{
	void *p;

	if (mem_init_done) {
		p = (void *) __get_free_page(GFP_KERNEL);
		if (p == 0)
			panic("couldn't get a page in MMU_get_page");
	} else {
		p = find_mem_piece(PAGE_SIZE, PAGE_SIZE);
	}
	memset(p, 0, PAGE_SIZE);
	return p;
}

void *
ioremap(unsigned long addr, unsigned long size)
{
	unsigned long p, end = addr + size;

	for (p = addr & PAGE_MASK; p < end; p += PAGE_SIZE)
		map_page(&init_task, p, p,
			 pgprot_val(PAGE_KERNEL_CI) | _PAGE_GUARDED);
	return (void *) addr;
}

void iounmap(unsigned long *addr)
{
	/* XXX todo */
}

void
map_page(struct task_struct *tsk, unsigned long va,
	 unsigned long pa, int flags)
{
	pmd_t *pd;
	pte_t *pg;
	int b;

	if (tsk->mm->pgd == NULL) {
		/* Allocate upper level page map */
		tsk->mm->pgd = (pgd_t *) MMU_get_page();
	}
	/* Use upper 10 bits of VA to index the first level map */
	pd = (pmd_t *) (tsk->mm->pgd + (va >> PGDIR_SHIFT));
	if (pmd_none(*pd)) {
		/*
		 * Need to allocate second-level table, but first
		 * check whether this address is already mapped by
		 * the BATs; if so, don't bother allocating the page.
		 */
		for (b = 0; b < 4; ++b) {
			if (va >= bat_addrs[b].start
			    && va <= bat_addrs[b].limit) {
				/* XXX should check the phys address matches */
				return;
			}
		}
		pg = (pte_t *) MMU_get_page();
		pmd_val(*pd) = (unsigned long) pg;
	}
	/* Use middle 10 bits of VA to index the second-level map */
	pg = pte_offset(pd, va);
	set_pte(pg, mk_pte_phys(pa & PAGE_MASK, __pgprot(flags)));
	/*flush_hash_page(va >> 28, va);*/
	flush_hash_page(0, va);
}

/*
 * TLB flushing:
 *
 *  - flush_tlb_all() flushes all processes TLBs
 *  - flush_tlb_mm(mm) flushes the specified mm context TLB's
 *  - flush_tlb_page(vma, vmaddr) flushes one page
 *  - flush_tlb_range(mm, start, end) flushes a range of pages
 *
 * since the hardware hash table functions as an extension of the
 * tlb as far as the linux tables are concerned, flush it too.
 *    -- Cort
 */

/*
 * Flush all tlb/hash table entries (except perhaps for those
 * mapping RAM starting at PAGE_OFFSET, since they never change).
 */
void
local_flush_tlb_all(void)
{
	memset(Hash, 0, Hash_size);
	_tlbia();
}


/*
 * Flush all the (user) entries for the address space described
 * by mm.  We can't rely on mm->mmap describing all the entries
 * that might be in the hash table.
 */
void
local_flush_tlb_mm(struct mm_struct *mm)
{
	mm->context = NO_CONTEXT;
	if (mm == current->mm) {
		get_mmu_context(current);
		/* done by get_mmu_context() now -- Cort */
		/*set_context(current->mm->context);*/
	}
}

void
local_flush_tlb_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	if (vmaddr < TASK_SIZE)
		flush_hash_page(vma->vm_mm->context, vmaddr);
	else
		flush_hash_page(0, vmaddr);
}


/* for each page addr in the range, call MMU_invalidate_page()
   if the range is very large and the hash table is small it might be faster to
   do a search of the hash table and just invalidate pages that are in the range
   but that's for study later.
        -- Cort
   */
void
local_flush_tlb_range(struct mm_struct *mm, unsigned long start, unsigned long end)
{
	start &= PAGE_MASK;

	if (end - start > 20 * PAGE_SIZE)
	{
		flush_tlb_mm(mm);
		return;
	}

	for (; start < end && start < TASK_SIZE; start += PAGE_SIZE)
	{
		flush_hash_page(mm->context, start);
	}
}

/*
 * The context counter has overflowed.
 * We set mm->context to NO_CONTEXT for all mm's in the system.
 * We assume we can get to all mm's by looking as tsk->mm for
 * all tasks in the system.
 */
void
mmu_context_overflow(void)
{
	struct task_struct *tsk;

	printk(KERN_DEBUG "mmu_context_overflow\n");
	read_lock(&tasklist_lock);
 	for_each_task(tsk) {
		if (tsk->mm)
			tsk->mm->context = NO_CONTEXT;
	}
	read_unlock(&tasklist_lock);
	flush_hash_segments(0x10, 0xffffff);
	next_mmu_context = 0;
	/* make sure current always has a context */
	current->mm->context = MUNGE_CONTEXT(++next_mmu_context);
	set_context(current->mm->context);
}

#if 0
/*
 * Cache flush functions - these functions cause caches to be flushed
 * on _all_ processors due to their use of dcbf.  local_flush_cache_all() is
 * the only function that will not act on all processors in the system.
 * -- Cort
 */
void local_flush_cache_all(void)
{
#if 0  
	unsigned long hid0,tmp;
	asm volatile(
		"mfspr %0,1008 \n\t"
		"mr    %1,%0 \n\t"
		"or    %0,%2,%2 \n\t"
		"mtspr 1008,%0 \n\t"
		"sync \n\t"
		"isync \n\t"
		"andc  %0,%0,%2 \n\t"
		"mtspr 1008,%0 \n\t"
		: "=r" (tmp), "=r" (hid0)
		: "r" (HID0_ICFI|HID0_DCI)
		);
#endif	
}

void local_flush_cache_mm(struct mm_struct *mm)
{
	struct vm_area_struct *vma = NULL;
	vma = mm->mmap;
	while(vma)
	{
		local_flush_cache_range(mm,vma->vm_start,vma->vm_end);
		vma = vma->vm_next;
	}
}

void local_flush_cache_page(struct vm_area_struct *vma, unsigned long vmaddr)
{
	unsigned long i;
	vmaddr = PAGE_ALIGN(vmaddr);
	for ( i = vmaddr ; i <= (vmaddr+PAGE_SIZE); i += 32 )
		asm volatile("dcbf %0,%1\n\ticbi %0,%1\n\t" :: "r" (i), "r" (0));
}

void local_flush_cache_range(struct mm_struct *mm, unsigned long start,
			    unsigned long end)
{
	unsigned long i;
	for ( i = start ; i <= end; i += 32 )
		asm volatile("dcbf %0,%1\n\ticbi %0,%1\n\t" :: "r" (i), "r" (0));
}
#endif

