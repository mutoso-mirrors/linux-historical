/*
 *  linux/arch/alpha/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
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

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/hwrpb.h>
#include <asm/dma.h>

extern void die_if_kernel(char *,struct pt_regs *,long);
extern void show_net_buffers(void);

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
pmd_t * __bad_pagetable(void)
{
	memset((void *) EMPTY_PGT, 0, PAGE_SIZE);
	return (pmd_t *) EMPTY_PGT;
}

pte_t __bad_page(void)
{
	memset((void *) EMPTY_PGE, 0, PAGE_SIZE);
	return pte_mkdirty(mk_pte((unsigned long) EMPTY_PGE, PAGE_SHARED));
}

void show_mem(void)
{
	int i,free = 0,total = 0,reserved = 0;
	int shared = 0;

	printk("\nMem-info:\n");
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
}

extern unsigned long free_area_init(unsigned long, unsigned long);

static void load_PCB(struct thread_struct * pcb)
{
	__asm__ __volatile__(
		"stq $30,0(%0)\n\t"
		"bis %0,%0,$16\n\t"
		"call_pal %1"
		: /* no outputs */
		: "r" (pcb), "i" (PAL_swpctx)
		: "$0", "$1", "$16", "$22", "$23", "$24", "$25");
}

/*
 * paging_init() sets up the page tables: in the alpha version this actually
 * unmaps the bootup page table (as we're now in KSEG, so we don't need it).
 */
unsigned long paging_init(unsigned long start_mem, unsigned long end_mem)
{
	int i;
	unsigned long newptbr;
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;

	/* initialize mem_map[] */
	start_mem = free_area_init(start_mem, end_mem);

	/* find free clusters, update mem_map[] accordingly */
	memdesc = (struct memdesc_struct *) (INIT_HWRPB->mddt_offset + (unsigned long) INIT_HWRPB);
	cluster = memdesc->cluster;
	for (i = memdesc->numclusters ; i > 0; i--, cluster++) {
		unsigned long pfn, nr;
		if (cluster->usage & 1)
			continue;
		pfn = cluster->start_pfn;
		nr = cluster->numpages;

		/* non-volatile memory. We might want to mark this for later */
		if (cluster->usage & 2)
			continue;

		while (nr--)
			clear_bit(PG_reserved, &mem_map[pfn++].flags);
	}

	/* unmap the console stuff: we don't need it, and we don't want it */
	/* Also set up the real kernel PCB while we're at it.. */
	memset((void *) ZERO_PAGE, 0, PAGE_SIZE);
	memset(swapper_pg_dir, 0, PAGE_SIZE);
	newptbr = ((unsigned long) swapper_pg_dir - PAGE_OFFSET) >> PAGE_SHIFT;
	pgd_val(swapper_pg_dir[1023]) = (newptbr << 32) | pgprot_val(PAGE_KERNEL);
	init_task.tss.ptbr = newptbr;
	init_task.tss.pal_flags = 1;	/* set FEN, clear everything else */
	init_task.tss.flags = 0;
	load_PCB(&init_task.tss);

	flush_tlb_all();
	return start_mem;
}

void mem_init(unsigned long start_mem, unsigned long end_mem)
{
	unsigned long tmp;

	end_mem &= PAGE_MASK;
	max_mapnr = num_physpages = MAP_NR(end_mem);
	high_memory = (void *) end_mem;
	start_mem = PAGE_ALIGN(start_mem);

	/*
	 * Mark the pages used by the kernel as reserved.
	 */
	tmp = KERNEL_START;
	while (tmp < start_mem) {
		set_bit(PG_reserved, &mem_map[MAP_NR(tmp)].flags);
		tmp += PAGE_SIZE;
	}

	for (tmp = PAGE_OFFSET ; tmp < end_mem ; tmp += PAGE_SIZE) {
		if (tmp >= MAX_DMA_ADDRESS)
			clear_bit(PG_DMA, &mem_map[MAP_NR(tmp)].flags);
		if (PageReserved(mem_map+MAP_NR(tmp)))
			continue;
		atomic_set(&mem_map[MAP_NR(tmp)].count, 1);
		free_page(tmp);
	}
	tmp = nr_free_pages << PAGE_SHIFT;
	printk("Memory: %luk available\n", tmp >> 10);
	return;
}

void free_initmem (void)
{
        extern char __init_begin, __init_end;
        unsigned long addr;

        addr = (unsigned long)(&__init_begin);
        for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
                mem_map[MAP_NR(addr)].flags &= ~(1 << PG_reserved);
                atomic_set(&mem_map[MAP_NR(addr)].count, 1);
                free_page(addr);
        }
        printk ("Freeing unused kernel memory: %ldk freed\n",
		(&__init_end - &__init_begin) >> 10);
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
