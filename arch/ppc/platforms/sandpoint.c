/*
 * arch/ppc/platforms/sandpoint_setup.c
 *
 * Board setup routines for the Motorola SPS Sandpoint Test Platform.
 *
 * Author: Mark A. Greer
 *	 mgreer@mvista.com
 *
 * 2000-2003 (c) MontaVista Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * This file adds support for the Motorola SPS Sandpoint Test Platform.
 * These boards have a PPMC slot for the processor so any combination
 * of cpu and host bridge can be attached.  This port is for an 8240 PPMC
 * module from Motorola SPS and other closely related cpu/host bridge
 * combinations (e.g., 750/755/7400 with MPC107 host bridge).
 * The sandpoint itself has a Windbond 83c553 (PCI-ISA bridge, 2 DMA ctlrs, 2
 * cascaded 8259 interrupt ctlrs, 8254 Timer/Counter, and an IDE ctlr), a
 * National 87308 (RTC, 2 UARTs, Keyboard & mouse ctlrs, and a floppy ctlr),
 * and 4 PCI slots (only 2 of which are usable; the other 2 are keyed for 3.3V
 * but are really 5V).
 *
 * The firmware on the sandpoint is called DINK (not my acronym :).  This port
 * depends on DINK to do some basic initialization (e.g., initialize the memory
 * ctlr) and to ensure that the processor is using MAP B (CHRP map).
 *
 * The switch settings for the Sandpoint board MUST be as follows:
 * 	S3: down
 * 	S4: up
 * 	S5: up
 * 	S6: down
 *
 * 'down' is in the direction from the PCI slots towards the PPMC slot;
 * 'up' is in the direction from the PPMC slot towards the PCI slots.
 * Be careful, the way the sandpoint board is installed in XT chasses will
 * make the directions reversed.
 *
 * Since Motorola listened to our suggestions for improvement, we now have
 * the Sandpoint X3 board.  All of the PCI slots are available, it uses
 * the serial interrupt interface (just a hardware thing we need to
 * configure properly).
 *
 * Use the default X3 switch settings.  The interrupts are then:
 *		EPIC	Source
 *		  0	SIOINT 		(8259, active low)
 *		  1	PCI #1
 *		  2	PCI #2
 *		  3	PCI #3
 *		  4	PCI #4
 *		  7	Winbond INTC	(IDE interrupt)
 *		  8	Winbond INTD	(IDE interrupt)
 *
 *
 * Motorola has finally released a version of DINK32 that correctly
 * (seemingly) initalizes the memory controller correctly, regardless
 * of the amount of memory in the system.  Once a method of determining
 * what version of DINK initializes the system for us, if applicable, is
 * found, we can hopefully stop hardcoding 32MB of RAM.
 *
 * It is important to note that this code only supports the Sandpoint X3
 * (all flavors) platform, and it does not support the X2 anymore.  Code
 * that at one time worked on the X2 can be found at:
 * ftp://source.mvista.com/pub/linuxppc/obsolete/sandpoint/
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/reboot.h>
#include <linux/pci.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/initrd.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/serial.h>
#include <linux/tty.h>	/* for linux/serial_core.h */
#include <linux/serial_core.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/smp.h>
#include <asm/vga.h>
#include <asm/open_pic.h>
#include <asm/i8259.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/mpc10x.h>
#include <asm/pci-bridge.h>

#include "sandpoint.h"

extern void gen550_progress(char *, unsigned short);
extern void gen550_init(int, struct uart_port *);

unsigned char __res[sizeof(bd_t)];

static void sandpoint_halt(void);

/*
 * Define all of the IRQ senses and polarities.  Taken from the
 * Sandpoint X3 User's manual.
 */
static u_char sandpoint_openpic_initsenses[] __initdata = {
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* 0: SIOINT */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* 2: PCI Slot 1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* 3: PCI Slot 2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* 4: PCI Slot 3 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* 5: PCI Slot 4 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),	/* 8: IDE (INT C) */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE)	/* 9: IDE (INT D) */
};

/*
 * Motorola SPS Sandpoint interrupt routing.
 */
static inline int
sandpoint_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	/*
	 *	PCI IDSEL/INTPIN->INTLINE
	 * 	   A   B   C   D
	 */
	{
		{ 16,  0,  0,  0 },	/* IDSEL 11 - i8259 on Winbond */
		{  0,  0,  0,  0 },	/* IDSEL 12 - unused */
		{ 18, 21, 20, 19 },	/* IDSEL 13 - PCI slot 1 */
		{ 19, 18, 21, 20 },	/* IDSEL 14 - PCI slot 2 */
		{ 20, 19, 18, 21 },	/* IDSEL 15 - PCI slot 3 */
		{ 21, 20, 19, 18 },	/* IDSEL 16 - PCI slot 4 */
	};

	const long min_idsel = 11, max_idsel = 16, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

static void __init
sandpoint_setup_winbond_83553(struct pci_controller *hose)
{
	int		devfn;

	/*
	 * Route IDE interrupts directly to the 8259's IRQ 14 & 15.
	 * We can't route the IDE interrupt to PCI INTC# or INTD# because those
	 * woule interfere with the PMC's INTC# and INTD# lines.
	 */
	/*
	 * Winbond Fcn 0
	 */
	devfn = PCI_DEVFN(11,0);

	early_write_config_byte(hose,
				0,
				devfn,
				0x43, /* IDE Interrupt Routing Control */
				0xef);
	early_write_config_word(hose,
				0,
				devfn,
				0x44, /* PCI Interrupt Routing Control */
				0x0000);

	/* Want ISA memory cycles to be forwarded to PCI bus */
	early_write_config_byte(hose,
				0,
				devfn,
				0x48, /* ISA-to-PCI Addr Decoder Control */
				0xf0);

	/* Enable RTC and Keyboard address locations.  */
	early_write_config_byte(hose,
				0,
				devfn,
				0x4d,	/* Chip Select Control Register */
				0x00);

	/* Enable Port 92.  */
	early_write_config_byte(hose,
				0,
				devfn,
				0x4e,	/* AT System Control Register */
				0x06);
	/*
	 * Winbond Fcn 1
	 */
	devfn = PCI_DEVFN(11,1);

	/* Put IDE controller into native mode. */
	early_write_config_byte(hose,
				0,
				devfn,
				0x09,	/* Programming interface Register */
				0x8f);

	/* Init IRQ routing, enable both ports, disable fast 16 */
	early_write_config_dword(hose,
				0,
				devfn,
				0x40,	/* IDE Control/Status Register */
				0x00ff0011);
	return;
}

static void __init
sandpoint_find_bridges(void)
{
	struct pci_controller	*hose;

	hose = pcibios_alloc_controller();

	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	if (mpc10x_bridge_init(hose,
			       MPC10X_MEM_MAP_B,
			       MPC10X_MEM_MAP_B,
			       MPC10X_MAPB_EUMB_BASE) == 0) {

		/* Do early winbond init, then scan PCI bus */
		sandpoint_setup_winbond_83553(hose);
		hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

		ppc_md.pcibios_fixup = NULL;
		ppc_md.pcibios_fixup_bus = NULL;
		ppc_md.pci_swizzle = common_swizzle;
		ppc_md.pci_map_irq = sandpoint_map_irq;
	}
	else {
		if (ppc_md.progress)
			ppc_md.progress("Bridge init failed", 0x100);
		printk("Host bridge init failed\n");
	}

	return;
}

#if defined(CONFIG_SERIAL_8250) && \
	(defined(CONFIG_KGDB) || defined(CONFIG_SERIAL_TEXT_DEBUG))
static void __init
sandpoint_early_serial_map(void)
{
	struct uart_port serial_req;

	/* Setup serial port access */
	memset(&serial_req, 0, sizeof(serial_req));
	serial_req.uartclk = UART_CLK;
	serial_req.irq = 4;
	serial_req.flags = STD_COM_FLAGS;
	serial_req.iotype = SERIAL_IO_MEM;
	serial_req.membase = (u_char *)SANDPOINT_SERIAL_0;

	gen550_init(0, &serial_req);

	if (early_serial_setup(&serial_req) != 0)
		printk(KERN_ERR "Early serial init of port 0 failed\n");

	/* Assume early_serial_setup() doesn't modify serial_req */
	serial_req.line = 1;
	serial_req.irq = 3; /* XXXX */
	serial_req.membase = (u_char *)SANDPOINT_SERIAL_1;

	gen550_init(1, &serial_req);

	if (early_serial_setup(&serial_req) != 0)
		printk(KERN_ERR "Early serial init of port 1 failed\n");
}
#endif

static void __init
sandpoint_setup_arch(void)
{
	loops_per_jiffy = 100000000 / HZ;

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef	CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_HDA1;
#endif

	/* Lookup PCI host bridges */
	sandpoint_find_bridges();

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	printk(KERN_INFO "Motorola SPS Sandpoint Test Platform\n");
	printk(KERN_INFO "Port by MontaVista Software, Inc. (source@mvista.com)\n");

	/* DINK32 12.3 and below do not correctly enable any caches.
	 * We will do this now with good known values.  Future versions
	 * of DINK32 are supposed to get this correct.
	 */
	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_SPEC7450)
		/* 745x is different.  We only want to pass along enable. */
		_set_L2CR(L2CR_L2E);
	else if (cur_cpu_spec[0]->cpu_features & CPU_FTR_L2CR)
		/* All modules have 1MB of L2.  We also assume that an
		 * L2 divisor of 3 will work.
		 */
		_set_L2CR(L2CR_L2E | L2CR_L2SIZ_1MB | L2CR_L2CLK_DIV3
				| L2CR_L2RAM_PIPE | L2CR_L2OH_1_0 | L2CR_L2DF);
#if 0
	/* Untested right now. */
	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_L3CR) {
		/* Magic value. */
		_set_L3CR(0x8f032000);
	}
#endif
}

#define	SANDPOINT_87308_CFG_ADDR		0x15c
#define	SANDPOINT_87308_CFG_DATA		0x15d

#define	SANDPOINT_87308_CFG_INB(addr, byte) {				\
	outb((addr), SANDPOINT_87308_CFG_ADDR);				\
	(byte) = inb(SANDPOINT_87308_CFG_DATA);				\
}

#define	SANDPOINT_87308_CFG_OUTB(addr, byte) {				\
	outb((addr), SANDPOINT_87308_CFG_ADDR);				\
	outb((byte), SANDPOINT_87308_CFG_DATA);				\
}

#define SANDPOINT_87308_SELECT_DEV(dev_num) {				\
	SANDPOINT_87308_CFG_OUTB(0x07, (dev_num));			\
}

#define	SANDPOINT_87308_DEV_ENABLE(dev_num) {				\
	SANDPOINT_87308_SELECT_DEV(dev_num);				\
	SANDPOINT_87308_CFG_OUTB(0x30, 0x01);				\
}

/*
 * Initialize the ISA devices on the Nat'l PC87308VUL SuperIO chip.
 */
static int __init
sandpoint_setup_natl_87308(void)
{
	u_char	reg;

	/*
	 * Enable all the devices on the Super I/O chip.
	 */
	SANDPOINT_87308_SELECT_DEV(0x00); /* Select kbd logical device */
	SANDPOINT_87308_CFG_OUTB(0xf0, 0x00); /* Set KBC clock to 8 Mhz */
	SANDPOINT_87308_DEV_ENABLE(0x00); /* Enable keyboard */
	SANDPOINT_87308_DEV_ENABLE(0x01); /* Enable mouse */
	SANDPOINT_87308_DEV_ENABLE(0x02); /* Enable rtc */
	SANDPOINT_87308_DEV_ENABLE(0x03); /* Enable fdc (floppy) */
	SANDPOINT_87308_DEV_ENABLE(0x04); /* Enable parallel */
	SANDPOINT_87308_DEV_ENABLE(0x05); /* Enable UART 2 */
	SANDPOINT_87308_CFG_OUTB(0xf0, 0x82); /* Enable bank select regs */
	SANDPOINT_87308_DEV_ENABLE(0x06); /* Enable UART 1 */
	SANDPOINT_87308_CFG_OUTB(0xf0, 0x82); /* Enable bank select regs */

	/* Set up floppy in PS/2 mode */
	outb(0x09, SIO_CONFIG_RA);
	reg = inb(SIO_CONFIG_RD);
	reg = (reg & 0x3F) | 0x40;
	outb(reg, SIO_CONFIG_RD);
	outb(reg, SIO_CONFIG_RD);	/* Have to write twice to change! */

	return 0;
}

arch_initcall(sandpoint_setup_natl_87308);

/*
 * Fix IDE interrupts.
 */
static int __init
sandpoint_fix_winbond_83553(void)
{
	/* Make all 8259 interrupt level sensitive */
	outb(0xf8, 0x4d0);
	outb(0xde, 0x4d1);

	return 0;
}

arch_initcall(sandpoint_fix_winbond_83553);

static int __init
sandpoint_request_io(void)
{
	request_region(0x00,0x20,"dma1");
	request_region(0x20,0x20,"pic1");
	request_region(0x40,0x20,"timer");
	request_region(0x80,0x10,"dma page reg");
	request_region(0xa0,0x20,"pic2");
	request_region(0xc0,0x20,"dma2");

	return 0;
}

arch_initcall(sandpoint_request_io);

/*
 * Interrupt setup and service.  Interrrupts on the Sandpoint come
 * from the four PCI slots plus the 8259 in the Winbond Super I/O (SIO).
 * The 8259 is cascaded from EPIC IRQ0, IRQ1-4 map to PCI slots 1-4,
 * IDE is on EPIC 7 and 8.
 */
static void __init
sandpoint_init_IRQ(void)
{
	int i;

	OpenPIC_InitSenses = sandpoint_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(sandpoint_openpic_initsenses);

	/*
	 * We need to tell openpic_set_sources where things actually are.
	 * mpc10x_common will setup OpenPIC_Addr at ioremap(EUMB phys base +
	 * EPIC offset (0x40000));  The EPIC IRQ Register Address Map -
	 * Interrupt Source Configuration Registers gives these numbers
	 * as offsets starting at 0x50200, we need to adjust occordinly.
	 */
	/* Map serial interrupts 0-15 */
	openpic_set_sources(0, 16, OpenPIC_Addr + 0x10200);

	openpic_init(NUM_8259_INTERRUPTS);

	/*
	 * openpic_init() has set up irq_desc[16-31] to be openpic
	 * interrupts.  We need to set irq_desc[0-15] to be i8259
	 * interrupts.
	 */
	for(i=0; i < NUM_8259_INTERRUPTS; i++)
		irq_desc[i].handler = &i8259_pic;

	/*
	 * The EPIC allows for a read in the range of 0xFEF00000 ->
	 * 0xFEFFFFFF to generate a PCI interrupt-acknowledge transaction.
	 */
	i8259_init(0xfef00000);
}

static u32
sandpoint_irq_canonicalize(u32 irq)
{
	if (irq == 2)
		return 9;
	else
		return irq;
}

static unsigned long __init
sandpoint_find_end_of_memory(void)
{
	bd_t *bp = (bd_t *)__res;

	if (bp->bi_memsize)
		return bp->bi_memsize;

	/* DINK32 13.0 correctly initalizes things, so iff you use
	 * this you _should_ be able to change this instead of a
	 * hardcoded value. */
#if 0
	return mpc10x_get_mem_size(MPC10X_MEM_MAP_B);
#else
	return 32*1024*1024;
#endif
}

static void __init
sandpoint_map_io(void)
{
	io_block_mapping(0xfe000000, 0xfe000000, 0x02000000, _PAGE_IO);
}

static void
sandpoint_restart(char *cmd)
{
	local_irq_disable();

	/* Set exception prefix high - to the firmware */
	_nmask_and_or_msr(0, MSR_IP);

	/* Reset system via Port 92 */
	outb(0x00, 0x92);
	outb(0x01, 0x92);
	for(;;);	/* Spin until reset happens */
}

static void
sandpoint_power_off(void)
{
	local_irq_disable();
	for(;;);	/* No way to shut power off with software */
	/* NOTREACHED */
}

static void
sandpoint_halt(void)
{
	sandpoint_power_off();
	/* NOTREACHED */
}

static int
sandpoint_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Motorola SPS\n");
	seq_printf(m, "machine\t\t: Sandpoint\n");

	return 0;
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE support.
 */
static int		sandpoint_ide_ports_known = 0;
static unsigned long	sandpoint_ide_regbase[MAX_HWIFS];
static unsigned long	sandpoint_ide_ctl_regbase[MAX_HWIFS];
static unsigned long	sandpoint_idedma_regbase;

static void
sandpoint_ide_probe(void)
{
	struct pci_dev *pdev = pci_find_device(PCI_VENDOR_ID_WINBOND,
			PCI_DEVICE_ID_WINBOND_82C105, NULL);

	if (pdev) {
		sandpoint_ide_regbase[0]=pdev->resource[0].start;
		sandpoint_ide_regbase[1]=pdev->resource[2].start;
		sandpoint_ide_ctl_regbase[0]=pdev->resource[1].start;
		sandpoint_ide_ctl_regbase[1]=pdev->resource[3].start;
		sandpoint_idedma_regbase=pdev->resource[4].start;
	}

	sandpoint_ide_ports_known = 1;
}

static int
sandpoint_ide_default_irq(unsigned long base)
{
	if (sandpoint_ide_ports_known == 0)
		sandpoint_ide_probe();

	if (base == sandpoint_ide_regbase[0])
		return SANDPOINT_IDE_INT0;
	else if (base == sandpoint_ide_regbase[1])
		return SANDPOINT_IDE_INT1;
	else
		return 0;
}

static unsigned long
sandpoint_ide_default_io_base(int index)
{
	if (sandpoint_ide_ports_known == 0)
		sandpoint_ide_probe();

	return sandpoint_ide_regbase[index];
}

static void __init
sandpoint_ide_init_hwif_ports(hw_regs_t *hw, unsigned long data_port,
		unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	uint	alt_status_base;
	int	i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg++;
	}

	if (data_port == sandpoint_ide_regbase[0]) {
		alt_status_base = sandpoint_ide_ctl_regbase[0] + 2;
		hw->irq = 14;
	}
	else if (data_port == sandpoint_ide_regbase[1]) {
		alt_status_base = sandpoint_ide_ctl_regbase[1] + 2;
		hw->irq = 15;
	}
	else {
		alt_status_base = 0;
		hw->irq = 0;
	}

	if (ctrl_port) {
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	} else {
		hw->io_ports[IDE_CONTROL_OFFSET] = alt_status_base;
	}

	if (irq != NULL) {
		*irq = hw->irq;
	}
}
#endif

/*
 * Set BAT 3 to map 0xf8000000 to end of physical memory space 1-to-1.
 */
static __inline__ void
sandpoint_set_bat(void)
{
	unsigned long bat3u, bat3l;

	__asm__ __volatile__(
			" lis %0,0xf800\n	\
			ori %1,%0,0x002a\n	\
			ori %0,%0,0x0ffe\n	\
			mtspr 0x21e,%0\n	\
			mtspr 0x21f,%1\n	\
			isync\n			\
			sync "
			: "=r" (bat3u), "=r" (bat3l));
}

TODC_ALLOC();

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
		unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/* ASSUMPTION:  If both r3 (bd_t pointer) and r6 (cmdline pointer)
	 * are non-zero, then we should use the board info from the bd_t
	 * structure and the cmdline pointed to by r6 instead of the
	 * information from birecs, if any.  Otherwise, use the information
	 * from birecs as discovered by the preceeding call to
	 * parse_bootinfo().  This rule should work with both PPCBoot, which
	 * uses a bd_t board info structure, and the kernel boot wrapper,
	 * which uses birecs.
	 */
	if (r3 && r6) {
		/* copy board info structure */
		memcpy( (void *)__res,(void *)(r3+KERNELBASE), sizeof(bd_t) );
		/* copy command line */
		*(char *)(r7+KERNELBASE) = 0;
		strcpy(cmd_line, (char *)(r6+KERNELBASE));
	}

#ifdef CONFIG_BLK_DEV_INITRD
	/* take care of initrd if we have one */
	if (r4) {
		initrd_start = r4 + KERNELBASE;
		initrd_end = r5 + KERNELBASE;
	}
#endif /* CONFIG_BLK_DEV_INITRD */

	/* Map in board regs, etc. */
	sandpoint_set_bat();

	isa_io_base = MPC10X_MAPB_ISA_IO_BASE;
	isa_mem_base = MPC10X_MAPB_ISA_MEM_BASE;
	pci_dram_offset = MPC10X_MAPB_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = 0x00ffffff;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;

	ppc_md.setup_arch = sandpoint_setup_arch;
	ppc_md.show_cpuinfo = sandpoint_show_cpuinfo;
	ppc_md.irq_canonicalize = sandpoint_irq_canonicalize;
	ppc_md.init_IRQ = sandpoint_init_IRQ;
	ppc_md.get_irq = openpic_get_irq;

	ppc_md.restart = sandpoint_restart;
	ppc_md.power_off = sandpoint_power_off;
	ppc_md.halt = sandpoint_halt;

	ppc_md.find_end_of_memory = sandpoint_find_end_of_memory;
	ppc_md.setup_io_mappings = sandpoint_map_io;

	TODC_INIT(TODC_TYPE_PC97307, 0x70, 0x00, 0x71, 8);
	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = todc_calibrate_decr;

	ppc_md.nvram_read_val = todc_mc146818_read_val;
	ppc_md.nvram_write_val = todc_mc146818_write_val;

#if defined(CONFIG_SERIAL_8250) && \
	(defined(CONFIG_KGDB) || defined(CONFIG_SERIAL_TEXT_DEBUG))
	sandpoint_early_serial_map();
#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#endif
#endif

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	ppc_ide_md.default_irq = sandpoint_ide_default_irq;
	ppc_ide_md.default_io_base = sandpoint_ide_default_io_base;
	ppc_ide_md.ide_init_hwif = sandpoint_ide_init_hwif_ports;
#endif
}
