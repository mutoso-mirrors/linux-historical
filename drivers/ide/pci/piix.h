#ifndef PIIX_H
#define PIIX_H

#include <linux/config.h>
#include <linux/pci.h>
#include <linux/ide.h>

#define PIIX_DEBUG_DRIVE_INFO		0

#define DISPLAY_PIIX_TIMINGS

#if defined(DISPLAY_PIIX_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static u8 piix_proc;

static int piix_get_info(char *, char **, off_t, int);

static ide_pci_host_proc_t piix_procs[] __initdata = {
	{
		name:		"piix",
		set:		1,
		get_info:	piix_get_info,
		parent:		NULL,
	},
};
#endif  /* defined(DISPLAY_PIIX_TIMINGS) && defined(CONFIG_PROC_FS) */

static void init_setup_piix(struct pci_dev *, ide_pci_device_t *);
static unsigned int __init init_chipset_piix(struct pci_dev *, const char *);
static void init_hwif_piix(ide_hwif_t *);
static void init_dma_piix(ide_hwif_t *, unsigned long);


/*
 *	Table of the various PIIX capability blocks
 *
 */
 
static ide_pci_device_t piix_pci_info[] __devinit = {
	{	/* 0 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82371FB_0,
		name:		"PIIXa",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 1 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82371FB_1,
		name:		"PIIXb",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 2 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82371MX,
		name:		"MPIIX",
		init_setup:	init_setup_piix,
		init_chipset:	NULL,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	NULL,
		channels:	2,
		autodma:	NODMA,
		enablebits:	{{0x6D,0x80,0x80}, {0x6F,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 3 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82371SB_1,
		name:		"PIIX3",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 4 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82371AB,
		name:		"PIIX4",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 5 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82801AB_1,
		name:		"ICH0",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 6 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82443MX_1,
		name:		"PIIX4",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 7 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82801AA_1,
		name:		"ICH",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 8 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82372FB_1,
		name:		"PIIX4",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 9 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82451NX,
		name:		"PIIX4",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	NOAUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 10 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82801BA_9,
		name:		"ICH2",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 11 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82801BA_8,
		name:		"ICH2M",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 12 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82801CA_10,
		name:		"ICH3M",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 13 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82801CA_11,
		name:		"ICH3",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 14 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82801DB_11,
		name:		"ICH4",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{	/* 15 */
		vendor:		PCI_VENDOR_ID_INTEL,
		device:		PCI_DEVICE_ID_INTEL_82801E_11,
		name:		"C-ICH",
		init_setup:	init_setup_piix,
		init_chipset:	init_chipset_piix,
		init_iops:	NULL,
		init_hwif:	init_hwif_piix,
		init_dma:	init_dma_piix,
		channels:	2,
		autodma:	AUTODMA,
		enablebits:	{{0x41,0x80,0x80}, {0x43,0x80,0x80}},
		bootable:	ON_BOARD,
		extra:		0,
	},{
		vendor:		0,
		device:		0,
		channels:	0,
		init_setup:	NULL,
		bootable:	EOL,
	}
};

#endif /* PIIX_H */
