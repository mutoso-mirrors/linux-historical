/* $Id: b1pci.c,v 1.1.4.1.2.1 2001/12/21 15:00:17 kai Exp $
 * 
 * Module for AVM B1 PCI-card.
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/capi.h>
#include <asm/io.h>
#include <linux/init.h>
#include "capicmd.h"
#include "capiutil.h"
#include "capilli.h"
#include "avmcard.h"

static char *revision = "$Revision: 1.1.4.1.2.1 $";

/* ------------------------------------------------------------- */

static struct pci_device_id b1pci_pci_tbl[] __devinitdata = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_B1, PCI_ANY_ID, PCI_ANY_ID },
	{ }				/* Terminating entry */
};

MODULE_DEVICE_TABLE(pci, b1pci_pci_tbl);
MODULE_DESCRIPTION("CAPI4Linux: Driver for AVM B1 PCI card");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------- */

static struct capi_driver_interface *di;

/* ------------------------------------------------------------- */

static void b1pci_interrupt(int interrupt, void *devptr, struct pt_regs *regs)
{
	avmcard *card;

	card = (avmcard *) devptr;

	if (!card) {
		printk(KERN_WARNING "b1pci: interrupt: wrong device\n");
		return;
	}
	if (card->interrupt) {
		printk(KERN_ERR "%s: reentering interrupt hander.\n", card->name);
		return;
	}

	card->interrupt = 1;

	b1_handle_interrupt(card);

	card->interrupt = 0;
}

/* ------------------------------------------------------------- */

static char *b1pci_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d r%d",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->revision : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

static int b1pci_add_card(struct capi_driver *driver,
                          struct capicardparams *p,
			  struct pci_dev *dev)
{
	avmcard *card;
	avmctrl_info *cinfo;
	int retval;

	MOD_INC_USE_COUNT;

	retval = -ENOMEM;
	card = kmalloc(sizeof(avmcard), GFP_KERNEL);
	if (!card) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		goto err;
	}
	memset(card, 0, sizeof(avmcard));

        cinfo = kmalloc(sizeof(avmctrl_info), GFP_KERNEL);
	if (!cinfo) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		goto err_kfree;
	}
	memset(cinfo, 0, sizeof(avmctrl_info));
	card->ctrlinfo = cinfo;
	cinfo->card = card;
	sprintf(card->name, "b1pci-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->cardtype = avm_b1pci;
	
	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING
		       "%s: ports 0x%03x-0x%03x in use.\n",
		       driver->name, card->port, card->port + AVMB1_PORTLEN);
		goto err_kfree_ctrlinfo;
	}
	b1_reset(card->port);
	retval = b1_detect(card->port, card->cardtype);
	if (retval) {
		printk(KERN_NOTICE "%s: NO card at 0x%x (%d)\n",
		       driver->name, card->port, retval);
		retval = -EIO;
		goto err_release_region;
	}
	b1_reset(card->port);
	b1_getrevision(card);
	
	retval = request_irq(card->irq, b1pci_interrupt, SA_SHIRQ, card->name, card);
	if (retval) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n",
		       driver->name, card->irq);
		retval = -EBUSY;
		goto err_release_region;
	}
	
	cinfo->capi_ctrl = di->attach_ctr(driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "%s: attach controller failed.\n",
		       driver->name);
		retval = -EBUSY;
		goto err_free_irq;
	}

	if (card->revision >= 4) {
		printk(KERN_INFO
			"%s: AVM B1 PCI V4 at i/o %#x, irq %d, revision %d (no dma)\n",
			driver->name, card->port, card->irq, card->revision);
	} else {
		printk(KERN_INFO
			"%s: AVM B1 PCI at i/o %#x, irq %d, revision %d\n",
			driver->name, card->port, card->irq, card->revision);
	}

	return 0;

 err_free_irq:
	free_irq(card->irq, card);
 err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
 err_kfree_ctrlinfo:
	kfree(card->ctrlinfo);
 err_kfree:
	kfree(card);
 err:
	MOD_DEC_USE_COUNT;
	return retval;
}

static void b1pci_remove_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;
	unsigned int port = card->port;

	b1_reset(port);
	b1_reset(port);

	di->detach_ctr(ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	ctrl->driverdata = 0;
	kfree(card->ctrlinfo);
	kfree(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */

static struct capi_driver b1pci_driver = {
    name: "b1pci",
    revision: "0.0",
    load_firmware: b1_load_firmware,
    reset_ctr: b1_reset_ctr,
    remove_ctr: b1pci_remove_ctr,
    register_appl: b1_register_appl,
    release_appl: b1_release_appl,
    send_message: b1_send_message,

    procinfo: b1pci_procinfo,
    ctr_read_proc: b1ctl_read_proc,
    driver_read_proc: 0,	/* use standard driver_read_proc */

    add_card: 0, /* no add_card function */
};

#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
/* ------------------------------------------------------------- */

static struct capi_driver_interface *div4;

/* ------------------------------------------------------------- */

static char *b1pciv4_procinfo(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);

	if (!cinfo)
		return "";
	sprintf(cinfo->infobuf, "%s %s 0x%x %d 0x%lx r%d",
		cinfo->cardname[0] ? cinfo->cardname : "-",
		cinfo->version[VER_DRIVER] ? cinfo->version[VER_DRIVER] : "-",
		cinfo->card ? cinfo->card->port : 0x0,
		cinfo->card ? cinfo->card->irq : 0,
		cinfo->card ? cinfo->card->membase : 0,
		cinfo->card ? cinfo->card->revision : 0
		);
	return cinfo->infobuf;
}

/* ------------------------------------------------------------- */

static int b1pciv4_add_card(struct capi_driver *driver,
                            struct capicardparams *p,
			    struct pci_dev *dev)
{
	avmcard *card;
	avmctrl_info *cinfo;
	int retval;

	MOD_INC_USE_COUNT;

	retval = -ENOMEM;
	card = kmalloc(sizeof(avmcard), GFP_KERNEL);
	if (!card) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		goto err;
	}
	memset(card, 0, sizeof(avmcard));

        card->dma = avmcard_dma_alloc(driver->name, dev, 2048+128, 2048+128);
	if (!card->dma) {
		printk(KERN_WARNING "%s: dma alloc.\n", driver->name);
		goto err_kfree;
	}
        cinfo = kmalloc(sizeof(avmctrl_info), GFP_KERNEL);
	if (!cinfo) {
		printk(KERN_WARNING "%s: no memory.\n", driver->name);
		goto err_dma_free;
	}
	memset(cinfo, 0, sizeof(avmctrl_info));
	card->ctrlinfo = cinfo;
	cinfo->card = card;
	sprintf(card->name, "b1pciv4-%x", p->port);
	card->port = p->port;
	card->irq = p->irq;
	card->membase = p->membase;
	card->cardtype = avm_b1pci;

	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING
		       "%s: ports 0x%03x-0x%03x in use.\n",
		       driver->name, card->port, card->port + AVMB1_PORTLEN);
		retval = -EBUSY;
		goto err_kfree_ctrlinfo;
	}

	card->mbase = ioremap_nocache(card->membase, 64);
	if (!card->mbase) {
		printk(KERN_NOTICE "%s: can't remap memory at 0x%lx\n",
					driver->name, card->membase);
		retval = -EIO;
		goto err_release_region;
	}

	b1dma_reset(card);

	retval = b1pciv4_detect(card);
	if (retval) {
		printk(KERN_NOTICE "%s: NO card at 0x%x (%d)\n",
					driver->name, card->port, retval);
		retval = -EIO;
		goto err_unmap;
	}
	b1dma_reset(card);
	b1_getrevision(card);

	retval = request_irq(card->irq, b1dma_interrupt, SA_SHIRQ, card->name, card);
	if (retval) {
		printk(KERN_ERR "%s: unable to get IRQ %d.\n",
				driver->name, card->irq);
		retval = -EBUSY;
		goto err_unmap;
	}

	cinfo->capi_ctrl = div4->attach_ctr(driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "%s: attach controller failed.\n", driver->name);
		retval = -EBUSY;
		goto err_free_irq;
	}
	card->cardnr = cinfo->capi_ctrl->cnr;

	printk(KERN_INFO
		"%s: AVM B1 PCI V4 at i/o %#x, irq %d, mem %#lx, revision %d (dma)\n",
		driver->name, card->port, card->irq,
		card->membase, card->revision);

	return 0;

 err_free_irq:
	free_irq(card->irq, card);
 err_unmap:
	iounmap(card->mbase);
 err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
 err_kfree_ctrlinfo:
	kfree(card->ctrlinfo);
 err_dma_free:
	avmcard_dma_free(card->dma);
 err_kfree:
	kfree(card);
 err:
	MOD_DEC_USE_COUNT;
	return retval;

}

static void b1pciv4_remove_ctr(struct capi_ctr *ctrl)
{
	avmctrl_info *cinfo = (avmctrl_info *)(ctrl->driverdata);
	avmcard *card = cinfo->card;

 	b1dma_reset(card);

	div4->detach_ctr(ctrl);
	free_irq(card->irq, card);
	iounmap(card->mbase);
	release_region(card->port, AVMB1_PORTLEN);
	ctrl->driverdata = 0;
	kfree(card->ctrlinfo);
        avmcard_dma_free(card->dma);
	kfree(card);

	MOD_DEC_USE_COUNT;
}

/* ------------------------------------------------------------- */


static struct capi_driver b1pciv4_driver = {
    name: "b1pciv4",
    revision: "0.0",
    load_firmware: b1dma_load_firmware,
    reset_ctr: b1dma_reset_ctr,
    remove_ctr: b1pciv4_remove_ctr,
    register_appl: b1dma_register_appl,
    release_appl: b1dma_release_appl,
    send_message: b1dma_send_message,

    procinfo: b1pciv4_procinfo,
    ctr_read_proc: b1dmactl_read_proc,
    driver_read_proc: 0,	/* use standard driver_read_proc */

    add_card: 0, /* no add_card function */
};

#endif /* CONFIG_ISDN_DRV_AVMB1_B1PCIV4 */

static int __devinit b1pci_probe(struct pci_dev *dev,
				 const struct pci_device_id *ent)
{
	struct capi_driver *driver = &b1pci_driver;
	struct capicardparams param;
	int retval;

	if (pci_enable_device(dev) < 0) {
		printk(KERN_ERR "%s: failed to enable AVM-B1\n",
		       driver->name);
		return -ENODEV;
	}
	param.irq = dev->irq;

	if (pci_resource_start(dev, 2)) { /* B1 PCI V4 */
#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
		driver = &b1pciv4_driver;

		pci_set_master(dev);
#endif
		param.membase = pci_resource_start(dev, 0);
		param.port = pci_resource_start(dev, 2);

		printk(KERN_INFO
		"%s: PCI BIOS reports AVM-B1 V4 at i/o %#x, irq %d, mem %#x\n",
		driver->name, param.port, param.irq, param.membase);
#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
		retval = b1pciv4_add_card(driver, &param, dev);
#else
		retval = b1pci_add_card(driver, &param, dev);
#endif
		if (retval != 0) {
		        printk(KERN_ERR
			"%s: no AVM-B1 V4 at i/o %#x, irq %d, mem %#x detected\n",
			driver->name, param.port, param.irq, param.membase);
		}
	} else {
		param.membase = 0;
		param.port = pci_resource_start(dev, 1);

		printk(KERN_INFO
		"%s: PCI BIOS reports AVM-B1 at i/o %#x, irq %d\n",
		driver->name, param.port, param.irq);
		retval = b1pci_add_card(driver, &param, dev);
		if (retval != 0) {
		        printk(KERN_ERR
			"%s: no AVM-B1 at i/o %#x, irq %d detected\n",
			driver->name, param.port, param.irq);
		}
	}
	return retval;
}

static struct pci_driver b1pci_pci_driver = {
	name:		"b1pci",
	id_table:	b1pci_pci_tbl,
	probe:		b1pci_probe,
};

static int __init b1pci_init(void)
{
	struct capi_driver *driver = &b1pci_driver;
#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
	struct capi_driver *driverv4 = &b1pciv4_driver;
#endif
	char *p;
	int ncards;

	MOD_INC_USE_COUNT;

	if ((p = strchr(revision, ':')) != 0 && p[1]) {
		strncpy(driver->revision, p + 2, sizeof(driver->revision));
		driver->revision[sizeof(driver->revision)-1] = 0;
		if ((p = strchr(driver->revision, '$')) != 0 && p > driver->revision)
			*(p-1) = 0;
	}
#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
	if ((p = strchr(revision, ':')) != 0 && p[1]) {
		strncpy(driverv4->revision, p + 2, sizeof(driverv4->revision));
		driverv4->revision[sizeof(driverv4->revision)-1] = 0;
		if ((p = strchr(driverv4->revision, '$')) != 0 && p > driverv4->revision)
			*(p-1) = 0;
	}
#endif

	printk(KERN_INFO "%s: revision %s\n", driver->name, driver->revision);

        di = attach_capi_driver(driver);
	if (!di) {
		printk(KERN_ERR "%s: failed to attach capi_driver\n",
				driver->name);
		MOD_DEC_USE_COUNT;
		return -ENODEV;
	}

#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
	printk(KERN_INFO "%s: revision %s\n", driverv4->name, driverv4->revision);

        div4 = attach_capi_driver(driverv4);
	if (!div4) {
    		detach_capi_driver(driver);
		printk(KERN_ERR "%s: failed to attach capi_driver\n",
				driverv4->name);
		MOD_DEC_USE_COUNT;
		return -ENODEV;
	}
#endif

	ncards = pci_register_driver(&b1pci_pci_driver);
	if (ncards) {
		printk(KERN_INFO "%s: %d B1-PCI card(s) detected\n",
				driver->name, ncards);
		MOD_DEC_USE_COUNT;
		return 0;
	}
	printk(KERN_ERR "%s: NO B1-PCI card detected\n", driver->name);
	pci_unregister_driver(&b1pci_pci_driver);
	detach_capi_driver(driver);
#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
	detach_capi_driver(driverv4);
#endif
	MOD_DEC_USE_COUNT;
	return -ENODEV;
}

static void __exit b1pci_exit(void)
{
	pci_unregister_driver(&b1pci_pci_driver);
	detach_capi_driver(&b1pci_driver);
#ifdef CONFIG_ISDN_DRV_AVMB1_B1PCIV4
	detach_capi_driver(&b1pciv4_driver);
#endif
}

module_init(b1pci_init);
module_exit(b1pci_exit);
