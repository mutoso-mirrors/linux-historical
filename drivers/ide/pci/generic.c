/*
 *  linux/drivers/ide/pci-orphan.c	Version 0.01	December 8, 1997
 *
 *  Copyright (C) 2001-2002	Andre Hedrick <andre@linux-ide.org>
 */


#undef REALLY_SLOW_IO		/* most systems can safely undef this */

#include <linux/config.h> /* for CONFIG_BLK_DEV_IDEPCI */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/init.h>

#include <asm/io.h>

#include "generic.h"

static unsigned int __init init_chipset_generic (struct pci_dev *dev, const char *name)
{
	return 0;
}

static void __init init_hwif_generic (ide_hwif_t *hwif)
{
	switch(hwif->pci_dev->device) {
		case PCI_DEVICE_ID_UMC_UM8673F:
		case PCI_DEVICE_ID_UMC_UM8886A:
		case PCI_DEVICE_ID_UMC_UM8886BF:
			hwif->irq = hwif->channel ? 15 : 14;
			break;
		default:
			break;
	}

	if (!(hwif->dma_base))
		return;

	hwif->atapi_dma = 1;
	hwif->ultra_mask = 0x7f;
	hwif->mwdma_mask = 0x07;
	hwif->swdma_mask = 0x07;

#ifdef CONFIG_BLK_DEV_IDEDMA
	if (!noautodma)
		hwif->autodma = 1;
	hwif->drives[0].autodma = hwif->autodma;
	hwif->drives[1].autodma = hwif->autodma;
#endif /* CONFIG_BLK_DEV_IDEDMA */
}

static void init_dma_generic (ide_hwif_t *hwif, unsigned long dmabase)
{
	ide_setup_dma(hwif, dmabase, 8);
}

extern void ide_setup_pci_device(struct pci_dev *, ide_pci_device_t *);

static void __init init_setup_generic (struct pci_dev *dev, ide_pci_device_t *d)
{
	if ((d->vendor == PCI_VENDOR_ID_UMC) &&
	    (d->device == PCI_DEVICE_ID_UMC_UM8886A) &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		return; /* UM8886A/BF pair */

	if ((d->vendor == PCI_VENDOR_ID_OPTI) &&
	    (d->device == PCI_DEVICE_ID_OPTI_82C558) &&
	    (!(PCI_FUNC(dev->devfn) & 1)))
		return;

	ide_setup_pci_device(dev, d);
}

static void __init init_setup_unknown (struct pci_dev *dev, ide_pci_device_t *d)
{
	ide_setup_pci_device(dev, d);
}

int __init generic_scan_pcidev (struct pci_dev *dev)
{
	ide_pci_device_t *d;

	for (d = generic_chipsets; d && d->vendor && d->device; ++d) {
		if (((d->vendor == dev->vendor) &&
		     (d->device == dev->device)) &&
		    (d->init_setup)) {
			d->init_setup(dev, d);
			return 1;
		}
	}

	if ((dev->class >> 8) == PCI_CLASS_STORAGE_IDE) {
		ide_pci_device_t *unknown = unknown_chipset;
//		unknown->vendor = dev->vendor;
//		unknown->device = dev->device;
		init_setup_unknown(dev, unknown);
		return 1;
	}
	return 0;
}

