/*
 *  linux/arch/arm/kernel/setup.c
 *
 *  Copyright (C) 1995-2001 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/utsname.h>
#include <linux/blk.h>
#include <linux/console.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/tty.h>
#include <linux/init.h>
#include <linux/root_dev.h>
#include <linux/cpu.h>

#include <asm/elf.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/procinfo.h>
#include <asm/setup.h>
#include <asm/mach-types.h>
#include <asm/tlbflush.h>

#include <asm/mach/arch.h>
#include <asm/mach/irq.h>

#ifndef MEM_SIZE
#define MEM_SIZE	(16*1024*1024)
#endif

#if defined(CONFIG_FPE_NWFPE) || defined(CONFIG_FPE_FASTFPE)
char fpe_type[8];

static int __init fpe_setup(char *line)
{
	memcpy(fpe_type, line, 8);
	return 1;
}

__setup("fpe=", fpe_setup);
#endif

extern unsigned int mem_fclk_21285;
extern void paging_init(struct meminfo *, struct machine_desc *desc);
extern void convert_to_tag_list(struct tag *tags);
extern void squash_mem_tags(struct tag *tag);
extern void bootmem_init(struct meminfo *);
extern void reboot_setup(char *str);
extern int root_mountflags;
extern int _stext, _text, _etext, _edata, _end;

unsigned int processor_id;
unsigned int __machine_arch_type;
unsigned int system_rev;
unsigned int system_serial_low;
unsigned int system_serial_high;
unsigned int elf_hwcap;

#ifdef MULTI_CPU
struct processor processor;
#endif
#ifdef MULTI_TLB
struct cpu_tlb_fns cpu_tlb;
#endif
#ifdef MULTI_USER
struct cpu_user_fns cpu_user;
#endif

unsigned char aux_device_present;
char elf_platform[ELF_PLATFORM_SIZE];
char saved_command_line[COMMAND_LINE_SIZE];
unsigned long phys_initrd_start __initdata = 0;
unsigned long phys_initrd_size __initdata = 0;

static struct meminfo meminfo __initdata = { 0, };
static const char *cpu_name;
static const char *machine_name;
static char command_line[COMMAND_LINE_SIZE];

static char default_command_line[COMMAND_LINE_SIZE] __initdata = CONFIG_CMDLINE;
static union { char c[4]; unsigned long l; } endian_test __initdata = { { 'l', '?', '?', 'b' } };
#define ENDIANNESS ((char)endian_test.l)

/*
 * Standard memory resources
 */
static struct resource mem_res[] = {
	{ "Video RAM",   0,     0,     IORESOURCE_MEM			},
	{ "Kernel code", 0,     0,     IORESOURCE_MEM			},
	{ "Kernel data", 0,     0,     IORESOURCE_MEM			}
};

#define video_ram   mem_res[0]
#define kernel_code mem_res[1]
#define kernel_data mem_res[2]

static struct resource io_res[] = {
	{ "reserved",    0x3bc, 0x3be, IORESOURCE_IO | IORESOURCE_BUSY },
	{ "reserved",    0x378, 0x37f, IORESOURCE_IO | IORESOURCE_BUSY },
	{ "reserved",    0x278, 0x27f, IORESOURCE_IO | IORESOURCE_BUSY }
};

#define lp0 io_res[0]
#define lp1 io_res[1]
#define lp2 io_res[2]

static const char *cache_types[16] = {
	"write-through",
	"write-back",
	"write-back",
	"undefined 3",
	"undefined 4",
	"undefined 5",
	"write-back",
	"write-back",
	"undefined 8",
	"undefined 9",
	"undefined 10",
	"undefined 11",
	"undefined 12",
	"undefined 13",
	"undefined 14",
	"undefined 15",
};

static const char *cache_clean[16] = {
	"not required",
	"read-block",
	"cp15 c7 ops",
	"undefined 3",
	"undefined 4",
	"undefined 5",
	"cp15 c7 ops",
	"cp15 c7 ops",
	"undefined 8",
	"undefined 9",
	"undefined 10",
	"undefined 11",
	"undefined 12",
	"undefined 13",
	"undefined 14",
	"undefined 15",
};

static const char *cache_lockdown[16] = {
	"not supported",
	"not supported",
	"not supported",
	"undefined 3",
	"undefined 4",
	"undefined 5",
	"format A",
	"format B",
	"undefined 8",
	"undefined 9",
	"undefined 10",
	"undefined 11",
	"undefined 12",
	"undefined 13",
	"undefined 14",
	"undefined 15",
};

static const char *proc_arch[] = {
	"undefined/unknown",
	"3",
	"4",
	"4T",
	"5",
	"5T",
	"5TE",
	"?(8)",
	"?(9)",
	"?(10)",
	"?(11)",
	"?(12)",
	"?(13)",
	"?(14)",
	"?(15)",
	"?(16)",
	"?(17)",
};

#define CACHE_TYPE(x)	(((x) >> 25) & 15)
#define CACHE_S(x)	((x) & (1 << 24))
#define CACHE_DSIZE(x)	(((x) >> 12) & 4095)	/* only if S=1 */
#define CACHE_ISIZE(x)	((x) & 4095)

#define CACHE_SIZE(y)	(((y) >> 6) & 7)
#define CACHE_ASSOC(y)	(((y) >> 3) & 7)
#define CACHE_M(y)	((y) & (1 << 2))
#define CACHE_LINE(y)	((y) & 3)

static inline void dump_cache(const char *prefix, unsigned int cache)
{
	unsigned int mult = 2 + (CACHE_M(cache) ? 1 : 0);

	printk("%s: %d bytes, associativity %d, %d byte lines, %d sets\n",
		prefix,
		mult << (8 + CACHE_SIZE(cache)),
		(mult << CACHE_ASSOC(cache)) >> 1,
		8 << CACHE_LINE(cache),
		1 << (6 + CACHE_SIZE(cache) - CACHE_ASSOC(cache) -
			CACHE_LINE(cache)));
}

static void __init dump_cpu_info(void)
{
	unsigned int info;

	asm("mrc p15, 0, %0, c0, c0, 1" : "=r" (info));

	if (info != processor_id) {
		printk("CPU: D %s cache\n", cache_types[CACHE_TYPE(info)]);
		if (CACHE_S(info)) {
			dump_cache("CPU: I cache", CACHE_ISIZE(info));
			dump_cache("CPU: D cache", CACHE_DSIZE(info));
		} else {
			dump_cache("CPU: cache", CACHE_ISIZE(info));
		}
	}
}

int cpu_architecture(void)
{
	int cpu_arch;

	if ((processor_id & 0x0000f000) == 0) {
		cpu_arch = CPU_ARCH_UNKNOWN;
	} else if ((processor_id & 0x0000f000) == 0x00007000) {
		cpu_arch = (processor_id & (1 << 23)) ? CPU_ARCH_ARMv4T : CPU_ARCH_ARMv3;
	} else {
		cpu_arch = (processor_id >> 16) & 15;
		if (cpu_arch)
			cpu_arch += CPU_ARCH_ARMv3;
	}

	return cpu_arch;
}

static void __init setup_processor(void)
{
	extern struct proc_info_list __proc_info_begin, __proc_info_end;
	struct proc_info_list *list;

	/*
	 * locate processor in the list of supported processor
	 * types.  The linker builds this table for us from the
	 * entries in arch/arm/mm/proc-*.S
	 */
	for (list = &__proc_info_begin; list < &__proc_info_end ; list++)
		if ((processor_id & list->cpu_mask) == list->cpu_val)
			break;

	/*
	 * If processor type is unrecognised, then we
	 * can do nothing...
	 */
	if (list >= &__proc_info_end) {
		printk("CPU configuration botched (ID %08x), unable "
		       "to continue.\n", processor_id);
		while (1);
	}

	cpu_name = list->cpu_name;

#ifdef MULTI_CPU
	processor = *list->proc;
#endif
#ifdef MULTI_TLB
	cpu_tlb = *list->tlb;
#endif
#ifdef MULTI_USER
	cpu_user = *list->user;
#endif

	printk("CPU: %s [%08x] revision %d (ARMv%s)\n",
	       cpu_name, processor_id, (int)processor_id & 15,
	       proc_arch[cpu_architecture()]);

	dump_cpu_info();

	sprintf(system_utsname.machine, "%s%c", list->arch_name, ENDIANNESS);
	sprintf(elf_platform, "%s%c", list->elf_name, ENDIANNESS);
	elf_hwcap = list->elf_hwcap;

	cpu_proc_init();
}

static struct machine_desc * __init setup_machine(unsigned int nr)
{
	extern struct machine_desc __arch_info_begin, __arch_info_end;
	struct machine_desc *list;

	/*
	 * locate architecture in the list of supported architectures.
	 */
	for (list = &__arch_info_begin; list < &__arch_info_end; list++)
		if (list->nr == nr)
			break;

	/*
	 * If the architecture type is not recognised, then we
	 * can co nothing...
	 */
	if (list >= &__arch_info_end) {
		printk("Architecture configuration botched (nr %d), unable "
		       "to continue.\n", nr);
		while (1);
	}

	printk("Machine: %s\n", list->name);

	return list;
}

static void __init early_initrd(char **p)
{
	unsigned long start, size;

	start = memparse(*p, p);
	if (**p == ',') {
		size = memparse((*p) + 1, p);

		phys_initrd_start = start;
		phys_initrd_size = size;
	}
}
__early_param("initrd=", early_initrd);

/*
 * Pick out the memory size.  We look for mem=size@start,
 * where start and size are "size[KkMm]"
 */
static void __init early_mem(char **p)
{
	static int usermem __initdata = 0;
	unsigned long size, start;

	/*
	 * If the user specifies memory size, we
	 * blow away any automatically generated
	 * size.
	 */
	if (usermem == 0) {
		usermem = 1;
		meminfo.nr_banks = 0;
	}

	start = PHYS_OFFSET;
	size  = memparse(*p, p);
	if (**p == '@')
		start = memparse(*p + 1, p);

	meminfo.bank[meminfo.nr_banks].start = start;
	meminfo.bank[meminfo.nr_banks].size  = size;
	meminfo.bank[meminfo.nr_banks].node  = PHYS_TO_NID(start);
	meminfo.nr_banks += 1;
}
__early_param("mem=", early_mem);

/*
 * Initial parsing of the command line.
 */
static void __init parse_cmdline(char **cmdline_p, char *from)
{
	char c = ' ', *to = command_line;
	int len = 0;

	for (;;) {
		if (c == ' ') {
			extern struct early_params __early_begin, __early_end;
			struct early_params *p;

			for (p = &__early_begin; p < &__early_end; p++) {
				int len = strlen(p->arg);

				if (memcmp(from, p->arg, len) == 0) {
					if (to != command_line)
						to -= 1;
					from += len;
					p->fn(&from);

					while (*from != ' ' && *from != '\0')
						from++;
					break;
				}
			}
		}
		c = *from++;
		if (!c)
			break;
		if (COMMAND_LINE_SIZE <= ++len)
			break;
		*to++ = c;
	}
	*to = '\0';
	*cmdline_p = command_line;
}

static void __init
setup_ramdisk(int doload, int prompt, int image_start, unsigned int rd_sz)
{
#ifdef CONFIG_BLK_DEV_RAM
	extern int rd_size, rd_image_start, rd_prompt, rd_doload;

	rd_image_start = image_start;
	rd_prompt = prompt;
	rd_doload = doload;

	if (rd_sz)
		rd_size = rd_sz;
#endif
}

static void __init
request_standard_resources(struct meminfo *mi, struct machine_desc *mdesc)
{
	struct resource *res;
	int i;

	kernel_code.start  = __virt_to_phys(init_mm.start_code);
	kernel_code.end    = __virt_to_phys(init_mm.end_code - 1);
	kernel_data.start  = __virt_to_phys(init_mm.end_code);
	kernel_data.end    = __virt_to_phys(init_mm.brk - 1);

	for (i = 0; i < mi->nr_banks; i++) {
		unsigned long virt_start, virt_end;

		if (mi->bank[i].size == 0)
			continue;

		virt_start = __phys_to_virt(mi->bank[i].start);
		virt_end   = virt_start + mi->bank[i].size - 1;

		res = alloc_bootmem_low(sizeof(*res));
		res->name  = "System RAM";
		res->start = __virt_to_phys(virt_start);
		res->end   = __virt_to_phys(virt_end);
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;

		request_resource(&iomem_resource, res);

		if (kernel_code.start >= res->start &&
		    kernel_code.end <= res->end)
			request_resource(res, &kernel_code);
		if (kernel_data.start >= res->start &&
		    kernel_data.end <= res->end)
			request_resource(res, &kernel_data);
	}

	if (mdesc->video_start) {
		video_ram.start = mdesc->video_start;
		video_ram.end   = mdesc->video_end;
		request_resource(&iomem_resource, &video_ram);
	}

	/*
	 * Some machines don't have the possibility of ever
	 * possessing lp0, lp1 or lp2
	 */
	if (mdesc->reserve_lp0)
		request_resource(&ioport_resource, &lp0);
	if (mdesc->reserve_lp1)
		request_resource(&ioport_resource, &lp1);
	if (mdesc->reserve_lp2)
		request_resource(&ioport_resource, &lp2);
}

/*
 *  Tag parsing.
 *
 * This is the new way of passing data to the kernel at boot time.  Rather
 * than passing a fixed inflexible structure to the kernel, we pass a list
 * of variable-sized tags to the kernel.  The first tag must be a ATAG_CORE
 * tag for the list to be recognised (to distinguish the tagged list from
 * a param_struct).  The list is terminated with a zero-length tag (this tag
 * is not parsed in any way).
 */
static int __init parse_tag_core(const struct tag *tag)
{
	if (tag->hdr.size > 2) {
		if ((tag->u.core.flags & 1) == 0)
			root_mountflags &= ~MS_RDONLY;
		ROOT_DEV = tag->u.core.rootdev;
	}
	return 0;
}

__tagtable(ATAG_CORE, parse_tag_core);

static int __init parse_tag_mem32(const struct tag *tag)
{
	if (meminfo.nr_banks >= NR_BANKS) {
		printk(KERN_WARNING
		       "Ignoring memory bank 0x%08x size %dKB\n",
			tag->u.mem.start, tag->u.mem.size / 1024);
		return -EINVAL;
	}
	meminfo.bank[meminfo.nr_banks].start = tag->u.mem.start;
	meminfo.bank[meminfo.nr_banks].size  = tag->u.mem.size;
	meminfo.bank[meminfo.nr_banks].node  = PHYS_TO_NID(tag->u.mem.start);
	meminfo.nr_banks += 1;

	return 0;
}

__tagtable(ATAG_MEM, parse_tag_mem32);

#if defined(CONFIG_VGA_CONSOLE) || defined(CONFIG_DUMMY_CONSOLE)
struct screen_info screen_info = {
 .orig_video_lines	= 30,
 .orig_video_cols	= 80,
 .orig_video_mode	= 0,
 .orig_video_ega_bx	= 0,
 .orig_video_isVGA	= 1,
 .orig_video_points	= 8
};

static int __init parse_tag_videotext(const struct tag *tag)
{
	screen_info.orig_x            = tag->u.videotext.x;
	screen_info.orig_y            = tag->u.videotext.y;
	screen_info.orig_video_page   = tag->u.videotext.video_page;
	screen_info.orig_video_mode   = tag->u.videotext.video_mode;
	screen_info.orig_video_cols   = tag->u.videotext.video_cols;
	screen_info.orig_video_ega_bx = tag->u.videotext.video_ega_bx;
	screen_info.orig_video_lines  = tag->u.videotext.video_lines;
	screen_info.orig_video_isVGA  = tag->u.videotext.video_isvga;
	screen_info.orig_video_points = tag->u.videotext.video_points;
	return 0;
}

__tagtable(ATAG_VIDEOTEXT, parse_tag_videotext);
#endif

static int __init parse_tag_ramdisk(const struct tag *tag)
{
	setup_ramdisk((tag->u.ramdisk.flags & 1) == 0,
		      (tag->u.ramdisk.flags & 2) == 0,
		      tag->u.ramdisk.start, tag->u.ramdisk.size);
	return 0;
}

__tagtable(ATAG_RAMDISK, parse_tag_ramdisk);

static int __init parse_tag_initrd(const struct tag *tag)
{
	printk(KERN_WARNING "ATAG_INITRD is deprecated; "
		"please update your bootloader.\n");
	phys_initrd_start = __virt_to_phys(tag->u.initrd.start);
	phys_initrd_size = tag->u.initrd.size;
	return 0;
}

__tagtable(ATAG_INITRD, parse_tag_initrd);

static int __init parse_tag_initrd2(const struct tag *tag)
{
	phys_initrd_start = tag->u.initrd.start;
	phys_initrd_size = tag->u.initrd.size;
	return 0;
}

__tagtable(ATAG_INITRD2, parse_tag_initrd2);

static int __init parse_tag_serialnr(const struct tag *tag)
{
	system_serial_low = tag->u.serialnr.low;
	system_serial_high = tag->u.serialnr.high;
	return 0;
}

__tagtable(ATAG_SERIAL, parse_tag_serialnr);

static int __init parse_tag_revision(const struct tag *tag)
{
	system_rev = tag->u.revision.rev;
	return 0;
}

__tagtable(ATAG_REVISION, parse_tag_revision);

static int __init parse_tag_cmdline(const struct tag *tag)
{
	strncpy(default_command_line, tag->u.cmdline.cmdline, COMMAND_LINE_SIZE);
	default_command_line[COMMAND_LINE_SIZE - 1] = '\0';
	return 0;
}

__tagtable(ATAG_CMDLINE, parse_tag_cmdline);

/*
 * Scan the tag table for this tag, and call its parse function.
 * The tag table is built by the linker from all the __tagtable
 * declarations.
 */
static int __init parse_tag(const struct tag *tag)
{
	extern struct tagtable __tagtable_begin, __tagtable_end;
	struct tagtable *t;

	for (t = &__tagtable_begin; t < &__tagtable_end; t++)
		if (tag->hdr.tag == t->tag) {
			t->parse(tag);
			break;
		}

	return t < &__tagtable_end;
}

/*
 * Parse all tags in the list, checking both the global and architecture
 * specific tag tables.
 */
static void __init parse_tags(const struct tag *t)
{
	for (; t->hdr.size; t = tag_next(t))
		if (!parse_tag(t))
			printk(KERN_WARNING
				"Ignoring unrecognised tag 0x%08x\n",
				t->hdr.tag);
}

/*
 * This holds our defaults.
 */
static struct init_tags {
	struct tag_header hdr1;
	struct tag_core   core;
	struct tag_header hdr2;
	struct tag_mem32  mem;
	struct tag_header hdr3;
} init_tags __initdata = {
	{ tag_size(tag_core), ATAG_CORE },
	{ 1, PAGE_SIZE, 0xff },
	{ tag_size(tag_mem32), ATAG_MEM },
	{ MEM_SIZE, PHYS_OFFSET },
	{ 0, ATAG_NONE }
};

void __init setup_arch(char **cmdline_p)
{
	struct tag *tags = (struct tag *)&init_tags;
	struct machine_desc *mdesc;
	char *from = default_command_line;

	setup_processor();
	mdesc = setup_machine(machine_arch_type);
	machine_name = mdesc->name;

	if (mdesc->soft_reboot)
		reboot_setup("s");

	if (mdesc->param_offset)
		tags = phys_to_virt(mdesc->param_offset);

	/*
	 * If we have the old style parameters, convert them to
	 * a tag list.
	 */
	if (tags->hdr.tag != ATAG_CORE)
		convert_to_tag_list(tags);
	if (tags->hdr.tag != ATAG_CORE)
		tags = (struct tag *)&init_tags;

	if (mdesc->fixup)
		mdesc->fixup(mdesc, tags, &from, &meminfo);

	if (tags->hdr.tag == ATAG_CORE) {
		if (meminfo.nr_banks != 0)
			squash_mem_tags(tags);
		parse_tags(tags);
	}

	init_mm.start_code = (unsigned long) &_text;
	init_mm.end_code   = (unsigned long) &_etext;
	init_mm.end_data   = (unsigned long) &_edata;
	init_mm.brk	   = (unsigned long) &_end;

	memcpy(saved_command_line, from, COMMAND_LINE_SIZE);
	saved_command_line[COMMAND_LINE_SIZE-1] = '\0';
	parse_cmdline(cmdline_p, from);
	bootmem_init(&meminfo);
	paging_init(&meminfo, mdesc);
	request_standard_resources(&meminfo, mdesc);

	/*
	 * Set up various architecture-specific pointers
	 */
	init_arch_irq = mdesc->init_irq;

#ifdef CONFIG_VT
#if defined(CONFIG_VGA_CONSOLE)
	conswitchp = &vga_con;
#elif defined(CONFIG_DUMMY_CONSOLE)
	conswitchp = &dummy_con;
#endif
#endif
}

static struct cpu cpu[1];

static int __init topology_init(void)
{
	return register_cpu(cpu, 0, NULL);
}

subsys_initcall(topology_init);

static const char *hwcap_str[] = {
	"swp",
	"half",
	"thumb",
	"26bit",
	"fastmult",
	"fpa",
	"vfp",
	"edsp",
	NULL
};

static void
c_show_cache(struct seq_file *m, const char *type, unsigned int cache)
{
	unsigned int mult = 2 + (CACHE_M(cache) ? 1 : 0);

	seq_printf(m, "%s size\t\t: %d\n"
		      "%s assoc\t\t: %d\n"
		      "%s line length\t: %d\n"
		      "%s sets\t\t: %d\n",
		type, mult << (8 + CACHE_SIZE(cache)),
		type, (mult << CACHE_ASSOC(cache)) >> 1,
		type, 8 << CACHE_LINE(cache),
		type, 1 << (6 + CACHE_SIZE(cache) - CACHE_ASSOC(cache) -
			    CACHE_LINE(cache)));
}

static int c_show(struct seq_file *m, void *v)
{
	int i;

	seq_printf(m, "Processor\t: %s rev %d (%s)\n",
		   cpu_name, (int)processor_id & 15, elf_platform);

	seq_printf(m, "BogoMIPS\t: %lu.%02lu\n",
		   loops_per_jiffy / (500000/HZ),
		   (loops_per_jiffy / (5000/HZ)) % 100);

	/* dump out the processor features */
	seq_puts(m, "Features\t: ");

	for (i = 0; hwcap_str[i]; i++)
		if (elf_hwcap & (1 << i))
			seq_printf(m, "%s ", hwcap_str[i]);

	seq_printf(m, "\nCPU implementer\t: 0x%02x\n", processor_id >> 24);
	seq_printf(m, "CPU architecture: %s\n", proc_arch[cpu_architecture()]);

	if ((processor_id & 0x0000f000) == 0x00000000) {
		/* pre-ARM7 */
		seq_printf(m, "CPU part\t\t: %07x\n", processor_id >> 4);
	} else {
		if ((processor_id & 0x0000f000) == 0x00007000) {
			/* ARM7 */
			seq_printf(m, "CPU variant\t: 0x%02x\n",
				   (processor_id >> 16) & 127);
		} else {
			/* post-ARM7 */
			seq_printf(m, "CPU variant\t: 0x%x\n",
				   (processor_id >> 20) & 15);
		}
		seq_printf(m, "CPU part\t: 0x%03x\n",
			   (processor_id >> 4) & 0xfff);
	}
	seq_printf(m, "CPU revision\t: %d\n", processor_id & 15);

	{
		unsigned int cache_info;

		asm("mrc p15, 0, %0, c0, c0, 1" : "=r" (cache_info));
		if (cache_info != processor_id) {
			seq_printf(m, "Cache type\t: %s\n"
				      "Cache clean\t: %s\n"
				      "Cache lockdown\t: %s\n"
				      "Cache unified\t: %s\n",
				   cache_types[CACHE_TYPE(cache_info)],
				   cache_clean[CACHE_TYPE(cache_info)],
				   cache_lockdown[CACHE_TYPE(cache_info)],
				   CACHE_S(cache_info) ? "Harvard" : "Unified");

			if (CACHE_S(cache_info)) {
				c_show_cache(m, "I", CACHE_ISIZE(cache_info));
				c_show_cache(m, "D", CACHE_DSIZE(cache_info));
			} else {
				c_show_cache(m, "Cache", CACHE_ISIZE(cache_info));
			}
		}
	}

	seq_puts(m, "\n");

	seq_printf(m, "Hardware\t: %s\n", machine_name);
	seq_printf(m, "Revision\t: %04x\n", system_rev);
	seq_printf(m, "Serial\t\t: %08x%08x\n",
		   system_serial_high, system_serial_low);

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void c_stop(struct seq_file *m, void *v)
{
}

struct seq_operations cpuinfo_op = {
	.start	= c_start,
	.next	= c_next,
	.stop	= c_stop,
	.show	= c_show
};
