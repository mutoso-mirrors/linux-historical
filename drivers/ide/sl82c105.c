/*
 * linux/drivers/ide/sl82c105.c
 *
 * SL82C105/Winbond 553 IDE driver
 *
 * Maintainer unknown.
 *
 * Drive tuning added from Rebel.com's kernel sources
 *  -- Russell King (15/11/98) linux@arm.linux.org.uk
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/hdreg.h>
#include <linux/pci.h>
#include <linux/ide.h>

#include <asm/io.h>
#include <asm/dma.h>

#include "ata-timing.h"
#include "pcihost.h"

extern char *ide_xfer_verbose (byte xfer_rate);

/*
 * Convert a PIO mode and cycle time to the required on/off
 * times for the interface.  This has protection against run-away
 * timings.
 */
static unsigned int get_timing_sl82c105(struct ata_timing *t)
{
	unsigned int cmd_on;
	unsigned int cmd_off;

	cmd_on = (t->active + 29) / 30;
	cmd_off = (t->cycle - 30 * cmd_on + 29) / 30;

	if (cmd_on > 32)
		cmd_on = 32;
	if (cmd_on == 0)
		cmd_on = 1;

	if (cmd_off > 32)
		cmd_off = 32;
	if (cmd_off == 0)
		cmd_off = 1;

	return (cmd_on - 1) << 8 | (cmd_off - 1) | ((t->mode > XFER_PIO_2) ? 0x40 : 0x00);
}

/*
 * Configure the drive and chipset for PIO
 */
static void config_for_pio(ide_drive_t *drive, int pio, int report)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;
	struct ata_timing *t;
	unsigned short drv_ctrl = 0x909;
	unsigned int xfer_mode, reg;

	reg = (hwif->unit ? 0x4c : 0x44) + (drive->select.b.unit ? 4 : 0);

	if (pio == 255)
		xfer_mode = ata_timing_mode(drive, XFER_PIO | XFER_EPIO);
	else
		xfer_mode = XFER_PIO_0 + min_t(byte, pio, 4);

	t = ata_timing_data(xfer_mode);

	if (ide_config_drive_speed(drive, xfer_mode) == 0)
		drv_ctrl = get_timing_sl82c105(t);

	if (!drive->using_dma) {
		/*
		 * If we are actually using MW DMA, then we can not
		 * reprogram the interface drive control register.
		 */
		pci_write_config_word(dev, reg, drv_ctrl);
		pci_read_config_word(dev, reg, &drv_ctrl);

		if (report) {
			printk("%s: selected %s (%dns) (%04X)\n", drive->name,
			       ide_xfer_verbose(xfer_mode), t->cycle, drv_ctrl);
		}
	}
}

/*
 * Configure the drive and the chipset for DMA
 */
static int config_for_dma(ide_drive_t *drive)
{
	struct ata_channel *hwif = drive->channel;
	struct pci_dev *dev = hwif->pci_dev;
	unsigned short drv_ctrl = 0x909;
	unsigned int reg;

	reg = (hwif->unit ? 0x4c : 0x44) + (drive->select.b.unit ? 4 : 0);

	if (ide_config_drive_speed(drive, XFER_MW_DMA_2) == 0)
		drv_ctrl = 0x0240;

	pci_write_config_word(dev, reg, drv_ctrl);

	return 0;
}

/*
 * Check to see if the drive and
 * chipset is capable of DMA mode
 */
static int sl82c105_check_drive(ide_drive_t *drive)
{
	int on = 0;

	do {
		struct hd_driveid *id = drive->id;
		struct ata_channel *hwif = drive->channel;

		if (!hwif->autodma)
			break;

		if (!id || !(id->capability & 1))
			break;

		/* Consult the list of known "bad" drives */
		if (udma_black_list(drive)) {
			on = 0;
			break;
		}

		if (id->field_valid & 2) {
			if  (id->dma_mword & 7 || id->dma_1word & 7)
				on = 1;
			break;
		}

		if (udma_white_list(drive)) {
			on = 1;
			break;
		}
	} while (0);
	if (on)
		config_for_dma(drive);
	else
		config_for_pio(drive, 4, 0);

	udma_enable(drive, on, 0);


	return 0;
}

/*
 * Our own dmaproc, only to intercept ide_dma_check
 */
static int sl82c105_dmaproc(struct ata_device *drive)
{
	return sl82c105_check_drive(drive);
}

/*
 * We only deal with PIO mode here - DMA mode 'using_dma' is not
 * initialised at the point that this function is called.
 */
static void tune_sl82c105(ide_drive_t *drive, byte pio)
{
	config_for_pio(drive, pio, 1);

	/*
	 * We support 32-bit I/O on this interface, and it
	 * doesn't have problems with interrupts.
	 */
	drive->channel->io_32bit = 1;
	drive->channel->unmask = 1;
}

/*
 * Return the revision of the Winbond bridge
 * which this function is part of.
 */
static unsigned int sl82c105_bridge_revision(struct pci_dev *dev)
{
	struct pci_dev *bridge;
	unsigned char rev;

	bridge = pci_find_device(PCI_VENDOR_ID_WINBOND, PCI_DEVICE_ID_WINBOND_83C553, NULL);

	/*
	 * If we are part of a Winbond 553
	 */
	if (!bridge || bridge->class >> 8 != PCI_CLASS_BRIDGE_ISA)
		return -1;

	if (bridge->bus != dev->bus ||
	    PCI_SLOT(bridge->devfn) != PCI_SLOT(dev->devfn))
		return -1;

	/*
	 * We need to find function 0's revision, not function 1
	 */
	pci_read_config_byte(bridge, PCI_REVISION_ID, &rev);

	return rev;
}

/*
 * Enable the PCI device
 */
static unsigned int __init sl82c105_init_chipset(struct pci_dev *dev)
{
	unsigned char ctrl_stat;

	/*
	 * Enable the ports
	 */
	pci_read_config_byte(dev, 0x40, &ctrl_stat);
	pci_write_config_byte(dev, 0x40, ctrl_stat | 0x33);

	return dev->irq;
}

static void __init sl82c105_init_dma(struct ata_channel *hwif, unsigned long dma_base)
{
	unsigned int rev;
	byte dma_state;

	dma_state = inb(dma_base + 2);
	rev = sl82c105_bridge_revision(hwif->pci_dev);
	if (rev <= 5) {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
		printk("    %s: Winbond 553 bridge revision %d, BM-DMA disabled\n",
		       hwif->name, rev);
		dma_state &= ~0x60;
	} else {
		dma_state |= 0x60;
		hwif->autodma = 1;
	}
	outb(dma_state, dma_base + 2);

	hwif->XXX_udma = NULL;
	ata_init_dma(hwif, dma_base);
	if (hwif->XXX_udma)
		hwif->XXX_udma = sl82c105_dmaproc;
}

/*
 * Initialise the chip
 */
static void __init sl82c105_init_channel(struct ata_channel *hwif)
{
	hwif->tuneproc = tune_sl82c105;
}


/* module data table */
static struct ata_pci_device chipset __initdata = {
	vendor: PCI_VENDOR_ID_WINBOND,
	device: PCI_DEVICE_ID_WINBOND_82C105,
	init_chipset: sl82c105_init_chipset,
	init_channel: sl82c105_init_channel,
	init_dma: sl82c105_init_dma,
	enablebits: { {0x40,0x01,0x01}, {0x40,0x10,0x10} },
	bootable: ON_BOARD
};

int __init init_sl82c105(void)
{
	ata_register_chipset(&chipset);

	return 0;
}
