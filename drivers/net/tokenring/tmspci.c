/*
 *  tmspci.c: A generic network driver for TMS380-based PCI token ring cards.
 *
 *  Written 1999 by Adam Fritzler
 *
 *  This software may be used and distributed according to the terms
 *  of the GNU General Public License, incorporated herein by reference.
 *
 *  This driver module supports the following cards:
 *	- SysKonnect TR4/16(+) PCI	(SK-4590)
 *	- SysKonnect TR4/16 PCI		(SK-4591)
 *      - Compaq TR 4/16 PCI
 *      - Thomas-Conrad TC4048 4/16 PCI 
 *      - 3Com 3C339 Token Link Velocity
 *
 *  Maintainer(s):
 *    AF	Adam Fritzler		mid@auk.cx
 *
 *  Modification History:
 *	30-Dec-99	AF	Split off from the tms380tr driver.
 *	22-Jan-00	AF	Updated to use indirect read/writes
 *	23-Nov-00	JG	New PCI API, cleanups
 *
 *  TODO:
 *	1. See if we can use MMIO instead of port accesses
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <linux/netdevice.h>
#include <linux/trdevice.h>
#include "tms380tr.h"

static char version[] __initdata =
"tmspci.c: v1.02 23/11/2000 by Adam Fritzler\n";

#define TMS_PCI_IO_EXTENT 32

struct card_info {
	unsigned char nselout[2]; /* NSELOUT vals for 4mb([0]) and 16mb([1]) */
	char *name;
};

static struct card_info card_info_table[] = {
	{ {0x03, 0x01}, "Compaq 4/16 TR PCI"},
	{ {0x03, 0x01}, "SK NET TR 4/16 PCI"},
	{ {0x03, 0x01}, "Thomas-Conrad TC4048 PCI 4/16"},
	{ {0x03, 0x01}, "3Com Token Link Velocity"},
};

static struct pci_device_id tmspci_pci_tbl[] __initdata = {
	{ PCI_VENDOR_ID_COMPAQ, PCI_DEVICE_ID_COMPAQ_TOKENRING, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_SYSKONNECT, PCI_DEVICE_ID_SYSKONNECT_TR, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1 },
	{ PCI_VENDOR_ID_TCONRAD, PCI_DEVICE_ID_TCONRAD_TOKENRING, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2 },
	{ PCI_VENDOR_ID_3COM, PCI_DEVICE_ID_3COM_3C339, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3 },
	{ }			/* Terminating entry */
};
MODULE_DEVICE_TABLE(pci, tmspci_pci_tbl);

MODULE_LICENSE("GPL");

static void tms_pci_read_eeprom(struct net_device *dev);
static unsigned short tms_pci_setnselout_pins(struct net_device *dev);

static unsigned short tms_pci_sifreadb(struct net_device *dev, unsigned short reg)
{
	return inb(dev->base_addr + reg);
}

static unsigned short tms_pci_sifreadw(struct net_device *dev, unsigned short reg)
{
	return inw(dev->base_addr + reg);
}

static void tms_pci_sifwriteb(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outb(val, dev->base_addr + reg);
}

static void tms_pci_sifwritew(struct net_device *dev, unsigned short val, unsigned short reg)
{
	outw(val, dev->base_addr + reg);
}

static int __init tms_pci_attach(struct pci_dev *pdev, const struct pci_device_id *ent)
{	
	static int versionprinted;
	struct net_device *dev;
	struct net_local *tp;
	int i, ret;
	unsigned int pci_irq_line;
	unsigned long pci_ioaddr;
	struct card_info *cardinfo = &card_info_table[ent->driver_data];
		
	if (versionprinted++ == 0)
		printk("%s", version);

	if (pci_enable_device(pdev))
		return -EIO;

	/* Remove I/O space marker in bit 0. */
	pci_irq_line = pdev->irq;
	pci_ioaddr = pci_resource_start (pdev, 0);

	/* At this point we have found a valid card. */
	dev = init_trdev(NULL, 0);
	if (!dev)
		return -ENOMEM;
	SET_MODULE_OWNER(dev);
		
	if (!request_region(pci_ioaddr, TMS_PCI_IO_EXTENT, dev->name)) {
		ret = -EBUSY;
		goto err_out_trdev;
	}

	ret = request_irq(pdev->irq, tms380tr_interrupt, SA_SHIRQ,
			  dev->name, dev);
	if (ret)
		goto err_out_region;

	dev->base_addr	= pci_ioaddr;
	dev->irq 	= pci_irq_line;
	dev->dma	= 0;

	printk("%s: %s\n", dev->name, cardinfo->name);
	printk("%s:    IO: %#4lx  IRQ: %d\n",
	       dev->name, dev->base_addr, dev->irq);
		
	tms_pci_read_eeprom(dev);

	printk("%s:    Ring Station Address: ", dev->name);
	printk("%2.2x", dev->dev_addr[0]);
	for (i = 1; i < 6; i++)
		printk(":%2.2x", dev->dev_addr[i]);
	printk("\n");
		
	ret = tmsdev_init(dev, PCI_MAX_ADDRESS, pdev);
	if (ret) {
		printk("%s: unable to get memory for dev->priv.\n", dev->name);
		goto err_out_irq;
	}

	tp = dev->priv;
	tp->setnselout = tms_pci_setnselout_pins;
		
	tp->sifreadb = tms_pci_sifreadb;
	tp->sifreadw = tms_pci_sifreadw;
	tp->sifwriteb = tms_pci_sifwriteb;
	tp->sifwritew = tms_pci_sifwritew;
		
	memcpy(tp->ProductID, cardinfo->name, PROD_ID_SIZE + 1);

	tp->tmspriv = cardinfo;

	dev->open = tms380tr_open;
	dev->stop = tms380tr_close;

	ret = register_trdev(dev);
	if (ret)
		goto err_out_tmsdev;
	
	pci_set_drvdata(pdev, dev);
	return 0;

err_out_tmsdev:
	tmsdev_term(dev);
err_out_irq:
	free_irq(pdev->irq, dev);
err_out_region:
	release_region(pci_ioaddr, TMS_PCI_IO_EXTENT);
err_out_trdev:
	unregister_netdev(dev);
	kfree(dev);
	return ret;
}

/*
 * Reads MAC address from adapter RAM, which should've read it from
 * the onboard ROM.  
 *
 * Calling this on a board that does not support it can be a very
 * dangerous thing.  The Madge board, for instance, will lock your
 * machine hard when this is called.  Luckily, its supported in a
 * seperate driver.  --ASF
 */
static void tms_pci_read_eeprom(struct net_device *dev)
{
	int i;
	
	/* Address: 0000:0000 */
	tms_pci_sifwritew(dev, 0, SIFADX);
	tms_pci_sifwritew(dev, 0, SIFADR);	
	
	/* Read six byte MAC address data */
	dev->addr_len = 6;
	for(i = 0; i < 6; i++)
		dev->dev_addr[i] = tms_pci_sifreadw(dev, SIFINC) >> 8;
}

static unsigned short tms_pci_setnselout_pins(struct net_device *dev)
{
	unsigned short val = 0;
	struct net_local *tp = dev->priv;
	struct card_info *cardinfo = tp->tmspriv;
  
	if(tp->DataRate == SPEED_4)
		val |= cardinfo->nselout[0];	/* Set 4Mbps */
	else
		val |= cardinfo->nselout[1];	/* Set 16Mbps */
	return val;
}

static void __devexit tms_pci_detach (struct pci_dev *pdev)
{
	struct net_device *dev = pci_get_drvdata(pdev);

	if (!dev)
		BUG();
	unregister_netdev(dev);
	release_region(dev->base_addr, TMS_PCI_IO_EXTENT);
	free_irq(dev->irq, dev);
	tmsdev_term(dev);
	kfree(dev);
	pci_set_drvdata(pdev, NULL);
}

static struct pci_driver tms_pci_driver = {
	name:		"tmspci",
	id_table:	tmspci_pci_tbl,
	probe:		tms_pci_attach,
	remove:		__devexit_p(tms_pci_detach),
};

static int __init tms_pci_init (void)
{
	int rc = pci_register_driver (&tms_pci_driver);
	if (rc < 0)
		return rc;
	if (rc == 0) {
		pci_unregister_driver (&tms_pci_driver);
		return -ENODEV;
	}
	return 0;
}

static void __exit tms_pci_rmmod (void)
{
	pci_unregister_driver (&tms_pci_driver);
}

module_init(tms_pci_init);
module_exit(tms_pci_rmmod);


/*
 * Local variables:
 *  compile-command: "gcc -DMODVERSIONS  -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer -I/usr/src/linux/drivers/net/tokenring/ -c tmspci.c"
 *  alt-compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -fomit-frame-pointer -I/usr/src/linux/drivers/net/tokenring/ -c tmspci.c"
 *  c-set-style "K&R"
 *  c-indent-level: 8
 *  c-basic-offset: 8
 *  tab-width: 8
 * End:
 */
