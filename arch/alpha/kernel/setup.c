/*
 *  linux/arch/alpha/kernel/setup.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

/* 2.3.x bootmem, 1999 Andrea Arcangeli <andrea@suse.de> */

/*
 * Bootup setup stuff.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/stddef.h>
#include <linux/unistd.h>
#include <linux/ptrace.h>
#include <linux/slab.h>
#include <linux/user.h>
#include <linux/a.out.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/config.h>	/* CONFIG_ALPHA_LCA etc */
#include <linux/mc146818rtc.h>
#include <linux/console.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/bootmem.h>
#include <linux/pci.h>
#include <linux/seq_file.h>

#ifdef CONFIG_BLK_DEV_INITRD
#include <linux/blk.h>
#endif

#include <linux/notifier.h>
extern struct notifier_block *panic_notifier_list;
static int alpha_panic_event(struct notifier_block *, unsigned long, void *);
static struct notifier_block alpha_panic_block = {
	alpha_panic_event,
        NULL,
        INT_MAX /* try to do it first */
};

#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/hwrpb.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/console.h>

#include "proto.h"
#include "pci_impl.h"


struct hwrpb_struct *hwrpb;
unsigned long srm_hae;

/* Which processor we booted from.  */
int boot_cpuid;

/*
 * Using SRM callbacks for initial console output. This works from
 * setup_arch() time through the end of time_init(), as those places
 * are under our (Alpha) control.

 * "srmcons" specified in the boot command arguments allows us to
 * see kernel messages during the period of time before the true
 * console device is "registered" during console_init(). As of this
 * version (2.4.10), time_init() is the last Alpha-specific code
 * called before console_init(), so we put "unregister" code
 * there to prevent schizophrenic console behavior later... ;-}
 *
 * By default, OFF; set it with a bootcommand arg of "srmcons".
 */
int srmcons_output = 0;

/* Enforce a memory size limit; useful for testing. By default, none. */
unsigned long mem_size_limit = 0;

#ifdef CONFIG_ALPHA_GENERIC
struct alpha_machine_vector alpha_mv;
int alpha_using_srm;
#endif

unsigned char aux_device_present = 0xaa;

#define N(a) (sizeof(a)/sizeof(a[0]))

static struct alpha_machine_vector *get_sysvec(long, long, long);
static struct alpha_machine_vector *get_sysvec_byname(const char *);
static void get_sysnames(long, long, long, char **, char **);

static char command_line[COMMAND_LINE_SIZE];
char saved_command_line[COMMAND_LINE_SIZE];

/*
 * The format of "screen_info" is strange, and due to early
 * i386-setup code. This is just enough to make the console
 * code think we're on a VGA color display.
 */

struct screen_info screen_info = {
	orig_x: 0,
	orig_y: 25,
	orig_video_cols: 80,
	orig_video_lines: 25,
	orig_video_isVGA: 1,
	orig_video_points: 16
};

/*
 * The direct map I/O window, if any.  This should be the same
 * for all busses, since it's used by virt_to_bus.
 */

unsigned long __direct_map_base;
unsigned long __direct_map_size;

/*
 * Declare all of the machine vectors.
 */

/* GCC 2.7.2 (on alpha at least) is lame.  It does not support either 
   __attribute__((weak)) or #pragma weak.  Bypass it and talk directly
   to the assembler.  */

#define WEAK(X) \
	extern struct alpha_machine_vector X; \
	asm(".weak "#X)

WEAK(alcor_mv);
WEAK(alphabook1_mv);
WEAK(avanti_mv);
WEAK(cabriolet_mv);
WEAK(clipper_mv);
WEAK(dp264_mv);
WEAK(eb164_mv);
WEAK(eb64p_mv);
WEAK(eb66_mv);
WEAK(eb66p_mv);
WEAK(eiger_mv);
WEAK(jensen_mv);
WEAK(lx164_mv);
WEAK(miata_mv);
WEAK(mikasa_mv);
WEAK(mikasa_primo_mv);
WEAK(monet_mv);
WEAK(nautilus_mv);
WEAK(noname_mv);
WEAK(noritake_mv);
WEAK(noritake_primo_mv);
WEAK(p2k_mv);
WEAK(pc164_mv);
WEAK(privateer_mv);
WEAK(rawhide_mv);
WEAK(ruffian_mv);
WEAK(rx164_mv);
WEAK(sable_mv);
WEAK(sable_gamma_mv);
WEAK(shark_mv);
WEAK(sx164_mv);
WEAK(takara_mv);
WEAK(webbrick_mv);
WEAK(wildfire_mv);
WEAK(xl_mv);
WEAK(xlt_mv);

#undef WEAK

/*
 * I/O resources inherited from PeeCees.  Except for perhaps the
 * turbochannel alphas, everyone has these on some sort of SuperIO chip.
 *
 * ??? If this becomes less standard, move the struct out into the
 * machine vector.
 */

static void __init
reserve_std_resources(void)
{
	static struct resource standard_io_resources[] = {
		{ "rtc", -1, -1 },
        	{ "dma1", 0x00, 0x1f },
        	{ "pic1", 0x20, 0x3f },
        	{ "timer", 0x40, 0x5f },
        	{ "keyboard", 0x60, 0x6f },
        	{ "dma page reg", 0x80, 0x8f },
        	{ "pic2", 0xa0, 0xbf },
        	{ "dma2", 0xc0, 0xdf },
	};

	struct resource *io = &ioport_resource;
	long i;

	if (hose_head) {
		struct pci_controller *hose;
		for (hose = hose_head; hose; hose = hose->next)
			if (hose->index == 0) {
				io = hose->io_space;
				break;
			}
	}

	/* Fix up for the Jensen's queer RTC placement.  */
	standard_io_resources[0].start = RTC_PORT(0);
	standard_io_resources[0].end = RTC_PORT(0) + 0x10;

	for (i = 0; i < N(standard_io_resources); ++i)
		request_resource(io, standard_io_resources+i);
}

#define PFN_UP(x)	(((x) + PAGE_SIZE-1) >> PAGE_SHIFT)
#define PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define PFN_PHYS(x)	((x) << PAGE_SHIFT)
#define PFN_MAX		PFN_DOWN(0x80000000)
#define for_each_mem_cluster(memdesc, cluster, i)		\
	for ((cluster) = (memdesc)->cluster, (i) = 0;		\
	     (i) < (memdesc)->numclusters; (i)++, (cluster)++)

static unsigned long __init
get_mem_size_limit(char *s)
{
        unsigned long end = 0;
        char *from = s;

        end = simple_strtoul(from, &from, 0);
        if ( *from == 'K' || *from == 'k' ) {
                end = end << 10;
                from++;
        } else if ( *from == 'M' || *from == 'm' ) {
                end = end << 20;
                from++;
        } else if ( *from == 'G' || *from == 'g' ) {
                end = end << 30;
                from++;
        }
        return end >> PAGE_SHIFT; /* Return the PFN of the limit. */
}

#ifndef CONFIG_DISCONTIGMEM
static void __init
setup_memory(void *kernel_end)
{
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;
	unsigned long start_kernel_pfn, end_kernel_pfn;
	unsigned long bootmap_size, bootmap_pages, bootmap_start;
	unsigned long start, end;
	int i;

	/* Find free clusters, and init and free the bootmem accordingly.  */
	memdesc = (struct memdesc_struct *)
	  (hwrpb->mddt_offset + (unsigned long) hwrpb);

	for_each_mem_cluster(memdesc, cluster, i) {
		printk("memcluster %d, usage %01lx, start %8lu, end %8lu\n",
		       i, cluster->usage, cluster->start_pfn,
		       cluster->start_pfn + cluster->numpages);

		/* Bit 0 is console/PALcode reserved.  Bit 1 is
		   non-volatile memory -- we might want to mark
		   this for later.  */
		if (cluster->usage & 3)
			continue;

		end = cluster->start_pfn + cluster->numpages;
		if (end > max_low_pfn)
			max_low_pfn = end;
	}

	if (mem_size_limit && max_low_pfn >= mem_size_limit)
	{
		printk("setup: forcing memory size to %ldK (from %ldK).\n",
		       mem_size_limit << (PAGE_SHIFT - 10),
		       max_low_pfn    << (PAGE_SHIFT - 10));
		max_low_pfn = mem_size_limit;
	}

	/* Find the bounds of kernel memory.  */
	start_kernel_pfn = PFN_DOWN(KERNEL_START_PHYS);
	end_kernel_pfn = PFN_UP(virt_to_phys(kernel_end));
	bootmap_start = -1;

 try_again:
	if (max_low_pfn <= end_kernel_pfn)
		panic("not enough memory to boot");

	/* We need to know how many physically contiguous pages
	   we'll need for the bootmap.  */
	bootmap_pages = bootmem_bootmap_pages(max_low_pfn);

	/* Now find a good region where to allocate the bootmap.  */
	for_each_mem_cluster(memdesc, cluster, i) {
		if (cluster->usage & 3)
			continue;

		start = cluster->start_pfn;
		end = start + cluster->numpages;
		if (start >= max_low_pfn)
			continue;
		if (end > max_low_pfn)
			end = max_low_pfn;
		if (start < start_kernel_pfn) {
			if (end > end_kernel_pfn
			    && end - end_kernel_pfn >= bootmap_pages) {
				bootmap_start = end_kernel_pfn;
				break;
			} else if (end > start_kernel_pfn)
				end = start_kernel_pfn;
		} else if (start < end_kernel_pfn)
			start = end_kernel_pfn;
		if (end - start >= bootmap_pages) {
			bootmap_start = start;
			break;
		}
	}

	if (bootmap_start == -1) {
		max_low_pfn >>= 1;
		goto try_again;
	}

	/* Allocate the bootmap and mark the whole MM as reserved.  */
	bootmap_size = init_bootmem(bootmap_start, max_low_pfn);

	/* Mark the free regions.  */
	for_each_mem_cluster(memdesc, cluster, i) {
		if (cluster->usage & 3)
			continue;

		start = cluster->start_pfn;
		end = cluster->start_pfn + cluster->numpages;
		if (start >= max_low_pfn)
			continue;
		if (end > max_low_pfn)
			end = max_low_pfn;
		if (start < start_kernel_pfn) {
			if (end > end_kernel_pfn) {
				free_bootmem(PFN_PHYS(start),
					     (PFN_PHYS(start_kernel_pfn)
					      - PFN_PHYS(start)));
				printk("freeing pages %ld:%ld\n",
				       start, start_kernel_pfn);
				start = end_kernel_pfn;
			} else if (end > start_kernel_pfn)
				end = start_kernel_pfn;
		} else if (start < end_kernel_pfn)
			start = end_kernel_pfn;
		if (start >= end)
			continue;

		free_bootmem(PFN_PHYS(start), PFN_PHYS(end) - PFN_PHYS(start));
		printk("freeing pages %ld:%ld\n", start, end);
	}

	/* Reserve the bootmap memory.  */
	reserve_bootmem(PFN_PHYS(bootmap_start), bootmap_size);
	printk("reserving pages %ld:%ld\n", bootmap_start, bootmap_start+PFN_UP(bootmap_size));

#ifdef CONFIG_BLK_DEV_INITRD
	initrd_start = INITRD_START;
	if (initrd_start) {
		initrd_end = initrd_start+INITRD_SIZE;
		printk("Initial ramdisk at: 0x%p (%lu bytes)\n",
		       (void *) initrd_start, INITRD_SIZE);

		if ((void *)initrd_end > phys_to_virt(PFN_PHYS(max_low_pfn))) {
			printk("initrd extends beyond end of memory "
			       "(0x%08lx > 0x%p)\ndisabling initrd\n",
			       initrd_end,
			       phys_to_virt(PFN_PHYS(max_low_pfn)));
			initrd_start = initrd_end = 0;
		} else {
			reserve_bootmem(virt_to_phys((void *)initrd_start),
					INITRD_SIZE);
		}
	}
#endif /* CONFIG_BLK_DEV_INITRD */
}
#else
extern void setup_memory(void *);
#endif /* !CONFIG_DISCONTIGMEM */

int __init
page_is_ram(unsigned long pfn)
{
	struct memclust_struct * cluster;
	struct memdesc_struct * memdesc;
	int i;

	memdesc = (struct memdesc_struct *)
		(hwrpb->mddt_offset + (unsigned long) hwrpb);
	for_each_mem_cluster(memdesc, cluster, i)
	{
		if (pfn >= cluster->start_pfn  &&
		    pfn < cluster->start_pfn + cluster->numpages) {
			return (cluster->usage & 3) ? 0 : 1;
		}
	}

	return 0;
}

#undef PFN_UP
#undef PFN_DOWN
#undef PFN_PHYS
#undef PFN_MAX

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_SRM)
/*
 *      Manage the SRM callbacks as a "console".
 */
static struct console srmcons;

void __init register_srm_console(void)
{
        register_console(&srmcons);
}

void __init unregister_srm_console(void)
{
        unregister_console(&srmcons);
}

static void srm_console_write(struct console *co, const char *s,
                                unsigned count)
{
	srm_printk(s);
}

static kdev_t srm_console_device(struct console *c)
{
	/* Huh? */
        return mk_kdev(TTY_MAJOR, 64 + c->index);
}

static int __init srm_console_setup(struct console *co, char *options)
{
	return 1;
}

static struct console srmcons = {
	name:		"srm0",
	write:		srm_console_write,
	device:		srm_console_device,
	setup:		srm_console_setup,
	flags:		CON_PRINTBUFFER | CON_ENABLED, /* fake it out */
	index:		-1,
};

#else
void __init register_srm_console(void)
{
}
void __init unregister_srm_console(void)
{
}
#endif

void __init
setup_arch(char **cmdline_p)
{
	extern char _end[];

	struct alpha_machine_vector *vec = NULL;
	struct percpu_struct *cpu;
	char *type_name, *var_name, *p;
	void *kernel_end = _end; /* end of kernel */
	char *args = command_line;

	hwrpb = (struct hwrpb_struct*) __va(INIT_HWRPB->phys_addr);
	boot_cpuid = hard_smp_processor_id();

	/* Register a call for panic conditions. */
	notifier_chain_register(&panic_notifier_list, &alpha_panic_block);

#ifdef CONFIG_ALPHA_GENERIC
	/* Assume that we've booted from SRM if we havn't booted from MILO.
	   Detect the later by looking for "MILO" in the system serial nr.  */
	alpha_using_srm = strncmp((const char *)hwrpb->ssn, "MILO", 4) != 0;
#endif

	/* If we are using SRM, we want to allow callbacks
	   as early as possible, so do this NOW, and then
	   they should work immediately thereafter.
	*/
	kernel_end = callback_init(kernel_end);

	/* 
	 * Locate the command line.
	 */
	/* Hack for Jensen... since we're restricted to 8 or 16 chars for
	   boot flags depending on the boot mode, we need some shorthand.
	   This should do for installation.  */
	if (strcmp(COMMAND_LINE, "INSTALL") == 0) {
		strcpy(command_line, "root=/dev/fd0 load_ramdisk=1");
	} else {
		strncpy(command_line, COMMAND_LINE, sizeof command_line);
		command_line[sizeof(command_line)-1] = 0;
	}
	strcpy(saved_command_line, command_line);
	*cmdline_p = command_line;

	/* 
	 * Process command-line arguments.
	 */
	while ((p = strsep(&args, " \t")) != NULL) {
		if (!*p) continue;
		if (strncmp(p, "alpha_mv=", 9) == 0) {
			vec = get_sysvec_byname(p+9);
			continue;
		}
		if (strncmp(p, "cycle=", 6) == 0) {
			est_cycle_freq = simple_strtol(p+6, NULL, 0);
			continue;
		}
		if (strncmp(p, "mem=", 4) == 0) {
			mem_size_limit = get_mem_size_limit(p+4);
			continue;
		}
		if (strncmp(p, "srmcons", 7) == 0) {
			srmcons_output = 1;
			continue;
		}
	}

	/* Replace the command line, now that we've killed it with strsep.  */
	strcpy(command_line, saved_command_line);

	/* If we want SRM console printk echoing early, do it now. */
	if (alpha_using_srm && srmcons_output) {
		register_srm_console();
	}

	/*
	 * Indentify and reconfigure for the current system.
	 */
	cpu = (struct percpu_struct*)((char*)hwrpb + hwrpb->processor_offset);

	get_sysnames(hwrpb->sys_type, hwrpb->sys_variation,
		     cpu->type, &type_name, &var_name);
	if (*var_name == '0')
		var_name = "";

	if (!vec) {
		vec = get_sysvec(hwrpb->sys_type, hwrpb->sys_variation,
				 cpu->type);
	}

	if (!vec) {
		panic("Unsupported system type: %s%s%s (%ld %ld)\n",
		      type_name, (*var_name ? " variation " : ""), var_name,
		      hwrpb->sys_type, hwrpb->sys_variation);
	}
	if (vec != &alpha_mv) {
		alpha_mv = *vec;
	}
	
	printk("Booting "
#ifdef CONFIG_ALPHA_GENERIC
	       "GENERIC "
#endif
	       "on %s%s%s using machine vector %s from %s\n",
	       type_name, (*var_name ? " variation " : ""),
	       var_name, alpha_mv.vector_name,
	       (alpha_using_srm ? "SRM" : "MILO"));

	printk("Command line: %s\n", command_line);

	/* 
	 * Sync up the HAE.
	 * Save the SRM's current value for restoration.
	 */
	srm_hae = *alpha_mv.hae_register;
	__set_hae(alpha_mv.hae_cache);

	/* Reset enable correctable error reports.  */
	wrmces(0x7);

	/* Find our memory.  */
	setup_memory(kernel_end);

	/* Initialize the machine.  Usually has to do with setting up
	   DMA windows and the like.  */
	if (alpha_mv.init_arch)
		alpha_mv.init_arch();

	/* Reserve standard resources.  */
	reserve_std_resources();

	/* 
	 * Give us a default console.  TGA users will see nothing until
	 * chr_dev_init is called, rather late in the boot sequence.
	 */

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif

	/* Default root filesystem to sda2.  */
	ROOT_DEV = to_kdev_t(0x0802);

 	/*
	 * Check ASN in HWRPB for validity, report if bad.
	 * FIXME: how was this failing?  Should we trust it instead,
	 * and copy the value into alpha_mv.max_asn?
 	 */

 	if (hwrpb->max_asn != MAX_ASN) {
		printk("Max ASN from HWRPB is bad (0x%lx)\n", hwrpb->max_asn);
 	}

	/*
	 * Identify the flock of penguins.
	 */

#ifdef CONFIG_SMP
	setup_smp();
#endif
	paging_init();
}

static char sys_unknown[] = "Unknown";
static char systype_names[][16] = {
	"0",
	"ADU", "Cobra", "Ruby", "Flamingo", "Mannequin", "Jensen",
	"Pelican", "Morgan", "Sable", "Medulla", "Noname",
	"Turbolaser", "Avanti", "Mustang", "Alcor", "Tradewind",
	"Mikasa", "EB64", "EB66", "EB64+", "AlphaBook1",
	"Rawhide", "K2", "Lynx", "XL", "EB164", "Noritake",
	"Cortex", "29", "Miata", "XXM", "Takara", "Yukon",
	"Tsunami", "Wildfire", "CUSCO", "Eiger", "Titan"
};

static char unofficial_names[][8] = {"100", "Ruffian"};

static char api_names[][16] = {"200", "Nautilus"};

static char eb164_names[][8] = {"EB164", "PC164", "LX164", "SX164", "RX164"};
static int eb164_indices[] = {0,0,0,1,1,1,1,1,2,2,2,2,3,3,3,3,4};

static char alcor_names[][16] = {"Alcor", "Maverick", "Bret"};
static int alcor_indices[] = {0,0,0,1,1,1,0,0,0,0,0,0,2,2,2,2,2,2};

static char eb64p_names[][16] = {"EB64+", "Cabriolet", "AlphaPCI64"};
static int eb64p_indices[] = {0,0,1,2};

static char eb66_names[][8] = {"EB66", "EB66+"};
static int eb66_indices[] = {0,0,1};

static char rawhide_names[][16] = {
	"Dodge", "Wrangler", "Durango", "Tincup", "DaVinci"
};
static int rawhide_indices[] = {0,0,0,1,1,2,2,3,3,4,4};

static char titan_names[][16] = {
	"0", "Privateer"
};
static int titan_indices[] = {0,1};

static char tsunami_names[][16] = {
	"0", "DP264", "Warhol", "Windjammer", "Monet", "Clipper",
	"Goldrush", "Webbrick", "Catamaran", "Brisbane", "Melbourne",
	"Flying Clipper", "Shark"
};
static int tsunami_indices[] = {0,1,2,3,4,5,6,7,8,9,10,11,12};

static struct alpha_machine_vector * __init
get_sysvec(long type, long variation, long cpu)
{
	static struct alpha_machine_vector *systype_vecs[] __initdata =
	{
		NULL,		/* 0 */
		NULL,		/* ADU */
		NULL,		/* Cobra */
		NULL,		/* Ruby */
		NULL,		/* Flamingo */
		NULL,		/* Mannequin */
		&jensen_mv,
		NULL, 		/* Pelican */
		NULL,		/* Morgan */
		NULL,		/* Sable -- see below.  */
		NULL,		/* Medulla */
		&noname_mv,
		NULL,		/* Turbolaser */
		&avanti_mv,
		NULL,		/* Mustang */
		&alcor_mv,	/* Alcor, Bret, Maverick.  */
		NULL,		/* Tradewind */
		NULL,		/* Mikasa -- see below.  */
		NULL,		/* EB64 */
		NULL,		/* EB66 -- see variation.  */
		NULL,		/* EB64+ -- see variation.  */
		&alphabook1_mv,
		&rawhide_mv,
		NULL,		/* K2 */
		NULL,		/* Lynx */
		&xl_mv,
		NULL,		/* EB164 -- see variation.  */
		NULL,		/* Noritake -- see below.  */
		NULL,		/* Cortex */
		NULL,		/* 29 */
		&miata_mv,
		NULL,		/* XXM */
		&takara_mv,
		NULL,		/* Yukon */
		NULL,		/* Tsunami -- see variation.  */
		&wildfire_mv,	/* Wildfire */
		NULL,		/* CUSCO */
		&eiger_mv,	/* Eiger */
		NULL,		/* Titan */
	};

	static struct alpha_machine_vector *unofficial_vecs[] __initdata =
	{
		NULL,		/* 100 */
		&ruffian_mv,
	};

	static struct alpha_machine_vector *api_vecs[] __initdata =
	{
		NULL,		/* 200 */
		&nautilus_mv,
	};

	static struct alpha_machine_vector *alcor_vecs[] __initdata = 
	{
		&alcor_mv, &xlt_mv, &xlt_mv
	};

	static struct alpha_machine_vector *eb164_vecs[] __initdata =
	{
		&eb164_mv, &pc164_mv, &lx164_mv, &sx164_mv, &rx164_mv
	};

	static struct alpha_machine_vector *eb64p_vecs[] __initdata =
	{
		&eb64p_mv,
		&cabriolet_mv,
		&cabriolet_mv		/* AlphaPCI64 */
	};

	static struct alpha_machine_vector *eb66_vecs[] __initdata =
	{
		&eb66_mv,
		&eb66p_mv
	};

	static struct alpha_machine_vector *titan_vecs[] __initdata =
	{
		NULL,
		&privateer_mv,		/* privateer */
	};

	static struct alpha_machine_vector *tsunami_vecs[]  __initdata =
	{
		NULL,
		&dp264_mv,		/* dp264 */
		&dp264_mv,		/* warhol */
		&dp264_mv,		/* windjammer */
		&monet_mv,		/* monet */
		&clipper_mv,		/* clipper */
		&dp264_mv,		/* goldrush */
		&webbrick_mv,		/* webbrick */
		&dp264_mv,		/* catamaran */
		NULL,			/* brisbane? */
		NULL,			/* melbourne? */
		NULL,			/* flying clipper? */
		&shark_mv,		/* shark */
	};

	/* ??? Do we need to distinguish between Rawhides?  */

	struct alpha_machine_vector *vec;

	/* Restore real CABRIO and EB66+ family names, ie EB64+ and EB66 */
	if (type < 0)
		type = -type;

	/* Search the system tables first... */
	vec = NULL;
	if (type < N(systype_vecs)) {
		vec = systype_vecs[type];
	} else if ((type > ST_API_BIAS) &&
		   (type - ST_API_BIAS) < N(api_vecs)) {
		vec = api_vecs[type - ST_API_BIAS];
	} else if ((type > ST_UNOFFICIAL_BIAS) &&
		   (type - ST_UNOFFICIAL_BIAS) < N(unofficial_vecs)) {
		vec = unofficial_vecs[type - ST_UNOFFICIAL_BIAS];
	}

	/* If we've not found one, try for a variation.  */

	if (!vec) {
		/* Member ID is a bit-field. */
		long member = (variation >> 10) & 0x3f;

		cpu &= 0xffffffff; /* make it usable */

		switch (type) {
		case ST_DEC_ALCOR:
			if (member < N(alcor_indices))
				vec = alcor_vecs[alcor_indices[member]];
			break;
		case ST_DEC_EB164:
			if (member < N(eb164_indices))
				vec = eb164_vecs[eb164_indices[member]];
			/* PC164 may show as EB164 variation with EV56 CPU,
			   but, since no true EB164 had anything but EV5... */
			if (vec == &eb164_mv && cpu == EV56_CPU)
				vec = &pc164_mv;
			break;
		case ST_DEC_EB64P:
			if (member < N(eb64p_indices))
				vec = eb64p_vecs[eb64p_indices[member]];
			break;
		case ST_DEC_EB66:
			if (member < N(eb66_indices))
				vec = eb66_vecs[eb66_indices[member]];
			break;
		case ST_DEC_TITAN:
			if (member < N(titan_indices))
				vec = titan_vecs[titan_indices[member]];
			break;
		case ST_DEC_TSUNAMI:
			if (member < N(tsunami_indices))
				vec = tsunami_vecs[tsunami_indices[member]];
			break;
		case ST_DEC_1000:
			if (cpu == EV5_CPU || cpu == EV56_CPU)
				vec = &mikasa_primo_mv;
			else
				vec = &mikasa_mv;
			break;
		case ST_DEC_NORITAKE:
			if (cpu == EV5_CPU || cpu == EV56_CPU)
				vec = &noritake_primo_mv;
			else
				vec = &noritake_mv;
			break;
		case ST_DEC_2100_A500:
			if (cpu == EV5_CPU || cpu == EV56_CPU)
				vec = &sable_gamma_mv;
			else
				vec = &sable_mv;
			break;
		}
	}
	return vec;
}

static struct alpha_machine_vector * __init
get_sysvec_byname(const char *name)
{
	static struct alpha_machine_vector *all_vecs[] __initdata =
	{
		&alcor_mv,
		&alphabook1_mv,
		&avanti_mv,
		&cabriolet_mv,
		&clipper_mv,
		&dp264_mv,
		&eb164_mv,
		&eb64p_mv,
		&eb66_mv,
		&eb66p_mv,
		&eiger_mv,
		&jensen_mv,
		&lx164_mv,
		&miata_mv,
		&mikasa_mv,
		&mikasa_primo_mv,
		&monet_mv,
		&nautilus_mv,
		&noname_mv,
		&noritake_mv,
		&noritake_primo_mv,
		&p2k_mv,
		&pc164_mv,
		&privateer_mv,
		&rawhide_mv,
		&ruffian_mv,
		&rx164_mv,
		&sable_mv,
		&sable_gamma_mv,
		&shark_mv,
		&sx164_mv,
		&takara_mv,
		&webbrick_mv,
		&wildfire_mv,
		&xl_mv,
		&xlt_mv
	};

	int i, n = sizeof(all_vecs)/sizeof(*all_vecs);
	for (i = 0; i < n; ++i) {
		struct alpha_machine_vector *mv = all_vecs[i];
		if (strcasecmp(mv->vector_name, name) == 0)
			return mv;
	}
	return NULL;
}

static void
get_sysnames(long type, long variation, long cpu,
	     char **type_name, char **variation_name)
{
	long member;

	/* Restore real CABRIO and EB66+ family names, ie EB64+ and EB66 */
	if (type < 0)
		type = -type;

	/* If not in the tables, make it UNKNOWN,
	   else set type name to family */
	if (type < N(systype_names)) {
		*type_name = systype_names[type];
	} else if ((type > ST_API_BIAS) &&
		   (type - ST_API_BIAS) < N(api_names)) {
		*type_name = api_names[type - ST_API_BIAS];
	} else if ((type > ST_UNOFFICIAL_BIAS) &&
		   (type - ST_UNOFFICIAL_BIAS) < N(unofficial_names)) {
		*type_name = unofficial_names[type - ST_UNOFFICIAL_BIAS];
	} else {
		*type_name = sys_unknown;
		*variation_name = sys_unknown;
		return;
	}

	/* Set variation to "0"; if variation is zero, done */
	*variation_name = systype_names[0];
	if (variation == 0) {
		return;
	}

	member = (variation >> 10) & 0x3f; /* member ID is a bit-field */

	cpu &= 0xffffffff; /* make it usable */

	switch (type) { /* select by family */
	default: /* default to variation "0" for now */
		break;
	case ST_DEC_EB164:
		if (member < N(eb164_indices))
			*variation_name = eb164_names[eb164_indices[member]];
		/* PC164 may show as EB164 variation, but with EV56 CPU,
		   so, since no true EB164 had anything but EV5... */
		if (eb164_indices[member] == 0 && cpu == EV56_CPU)
			*variation_name = eb164_names[1]; /* make it PC164 */
		break;
	case ST_DEC_ALCOR:
		if (member < N(alcor_indices))
			*variation_name = alcor_names[alcor_indices[member]];
		break;
	case ST_DEC_EB64P:
		if (member < N(eb64p_indices))
			*variation_name = eb64p_names[eb64p_indices[member]];
		break;
	case ST_DEC_EB66:
		if (member < N(eb66_indices))
			*variation_name = eb66_names[eb66_indices[member]];
		break;
	case ST_DEC_RAWHIDE:
		if (member < N(rawhide_indices))
			*variation_name = rawhide_names[rawhide_indices[member]];
		break;
	case ST_DEC_TITAN:
		if (member < N(titan_indices))
			*variation_name = titan_names[titan_indices[member]];
		break;
	case ST_DEC_TSUNAMI:
		if (member < N(tsunami_indices))
			*variation_name = tsunami_names[tsunami_indices[member]];
		break;
	}
}

/*
 * A change was made to the HWRPB via an ECO and the following code
 * tracks a part of the ECO.  In HWRPB versions less than 5, the ECO
 * was not implemented in the console firmware.  If it's revision 5 or
 * greater we can get the name of the platform as an ASCII string from
 * the HWRPB.  That's what this function does.  It checks the revision
 * level and if the string is in the HWRPB it returns the address of
 * the string--a pointer to the name of the platform.
 *
 * Returns:
 *      - Pointer to a ASCII string if it's in the HWRPB
 *      - Pointer to a blank string if the data is not in the HWRPB.
 */

static char *
platform_string(void)
{
	struct dsr_struct *dsr;
	static char unk_system_string[] = "N/A";

	/* Go to the console for the string pointer.
	 * If the rpb_vers is not 5 or greater the rpb
	 * is old and does not have this data in it.
	 */
	if (hwrpb->revision < 5)
		return (unk_system_string);
	else {
		/* The Dynamic System Recognition struct
		 * has the system platform name starting
		 * after the character count of the string.
		 */
		dsr =  ((struct dsr_struct *)
			((char *)hwrpb + hwrpb->dsr_offset));
		return ((char *)dsr + (dsr->sysname_off +
				       sizeof(long)));
	}
}

static int
get_nr_processors(struct percpu_struct *cpubase, unsigned long num)
{
	struct percpu_struct *cpu;
	int i, count = 0;

	for (i = 0; i < num; i++) {
		cpu = (struct percpu_struct *)
			((char *)cpubase + i*hwrpb->processor_size);
		if ((cpu->flags & 0x1cc) == 0x1cc)
			count++;
	}
	return count;
}


static int
show_cpuinfo(struct seq_file *f, void *slot)
{
	extern struct unaligned_stat {
		unsigned long count, va, pc;
	} unaligned[2];

	static char cpu_names[][8] = {
		"EV3", "EV4", "Simulate", "LCA4", "EV5", "EV45", "EV56",
		"EV6", "PCA56", "PCA57", "EV67", "EV68CB", "EV68AL",
		"EV68CX", "EV7", "EV79", "EV69"
	};

	struct percpu_struct *cpu = slot;
	unsigned int cpu_index;
	char *cpu_name;
	char *systype_name;
	char *sysvariation_name;
	int nr_processors;

	cpu_index = (unsigned) (cpu->type - 1);
	cpu_name = "Unknown";
	if (cpu_index < N(cpu_names))
		cpu_name = cpu_names[cpu_index];

	get_sysnames(hwrpb->sys_type, hwrpb->sys_variation,
		     cpu->type, &systype_name, &sysvariation_name);

	nr_processors = get_nr_processors(cpu, hwrpb->nr_processors);

	seq_printf(f, "cpu\t\t\t: Alpha\n"
		      "cpu model\t\t: %s\n"
		      "cpu variation\t\t: %ld\n"
		      "cpu revision\t\t: %ld\n"
		      "cpu serial number\t: %s\n"
		      "system type\t\t: %s\n"
		      "system variation\t: %s\n"
		      "system revision\t\t: %ld\n"
		      "system serial number\t: %s\n"
		      "cycle frequency [Hz]\t: %lu %s\n"
		      "timer frequency [Hz]\t: %lu.%02lu\n"
		      "page size [bytes]\t: %ld\n"
		      "phys. address bits\t: %ld\n"
		      "max. addr. space #\t: %ld\n"
		      "BogoMIPS\t\t: %lu.%02lu\n"
		      "kernel unaligned acc\t: %ld (pc=%lx,va=%lx)\n"
		      "user unaligned acc\t: %ld (pc=%lx,va=%lx)\n"
		      "platform string\t\t: %s\n"
		      "cpus detected\t\t: %d\n",
		       cpu_name, cpu->variation, cpu->revision,
		       (char*)cpu->serial_no,
		       systype_name, sysvariation_name, hwrpb->sys_revision,
		       (char*)hwrpb->ssn,
		       est_cycle_freq ? : hwrpb->cycle_freq,
		       est_cycle_freq ? "est." : "",
		       hwrpb->intr_freq / 4096,
		       (100 * hwrpb->intr_freq / 4096) % 100,
		       hwrpb->pagesize,
		       hwrpb->pa_bits,
		       hwrpb->max_asn,
		       loops_per_jiffy / (500000/HZ),
		       (loops_per_jiffy / (5000/HZ)) % 100,
		       unaligned[0].count, unaligned[0].pc, unaligned[0].va,
		       unaligned[1].count, unaligned[1].pc, unaligned[1].va,
		       platform_string(), nr_processors);

#ifdef CONFIG_SMP
	seq_printf(f, "cpus active\t\t: %d\n"
		      "cpu active mask\t\t: %016lx\n",
		       smp_num_cpus, cpu_present_mask);
#endif

	return 0;
}

/*
 * We show only CPU #0 info.
 */
static void *
c_start(struct seq_file *f, loff_t *pos)
{
	return *pos ? NULL : (char *)hwrpb + hwrpb->processor_offset;
}

static void *
c_next(struct seq_file *f, void *v, loff_t *pos)
{
	return NULL;
}

static void
c_stop(struct seq_file *f, void *v)
{
}

struct seq_operations cpuinfo_op = {
	start:	c_start,
	next:	c_next,
	stop:	c_stop,
	show:	show_cpuinfo,
};


static int alpha_panic_event(struct notifier_block *this,
			     unsigned long event,
			     void *ptr)
{
#if 1
	/* FIXME FIXME FIXME */
	/* If we are using SRM and serial console, just hard halt here. */
	if (alpha_using_srm && srmcons_output)
		__halt();
#endif
        return NOTIFY_DONE;
}
