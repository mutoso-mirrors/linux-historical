/* $Id: b1isa.c,v 1.10.6.6 2001/09/23 22:24:33 kai Exp $
 * 
 * Module for AVM B1 ISA-card.
 * 
 * Copyright 1999 by Carsten Paeth <calle@calle.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/skbuff.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/capi.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <linux/isdn/capicmd.h>
#include <linux/isdn/capiutil.h>
#include <linux/isdn/capilli.h>
#include "avmcard.h"

static char *revision = "$Revision: 1.10.6.6 $";

/* ------------------------------------------------------------- */

MODULE_DESCRIPTION("CAPI4Linux: Driver for AVM B1 ISA card");
MODULE_AUTHOR("Carsten Paeth");
MODULE_LICENSE("GPL");

/* ------------------------------------------------------------- */

static struct capi_driver b1isa_driver;

static void b1isa_remove(struct pci_dev *pdev)
{
	avmctrl_info *cinfo = pci_get_drvdata(pdev);
	avmcard *card = cinfo->card;
	unsigned int port = cinfo->card->port;

	b1_reset(port);
	b1_reset(port);

	detach_capi_ctr(cinfo->capi_ctrl);
	free_irq(card->irq, card);
	release_region(card->port, AVMB1_PORTLEN);
	b1_free_card(card);
}

/* ------------------------------------------------------------- */

static int __init b1isa_probe(struct pci_dev *pdev)
{
	avmctrl_info *cinfo;
	avmcard *card;
	int retval;

	card = b1_alloc_card(1);
	if (!card) {
		printk(KERN_WARNING "b1isa: no memory.\n");
		retval = -ENOMEM;
		goto err;
	}

	cinfo = card->ctrlinfo;

	card->port = pci_resource_start(pdev, 0);
	card->irq = pdev->irq;
	card->cardtype = avm_b1isa;
	sprintf(card->name, "b1isa-%x", card->port);

	if (   card->port != 0x150 && card->port != 0x250
	    && card->port != 0x300 && card->port != 0x340) {
		printk(KERN_WARNING "b1isa: illegal port 0x%x.\n", card->port);
		retval = -EINVAL;
		goto err_free;
	}
	if (b1_irq_table[card->irq & 0xf] == 0) {
		printk(KERN_WARNING "b1isa: irq %d not valid.\n", card->irq);
		retval = -EINVAL;
		goto err_free;
	}
	if (!request_region(card->port, AVMB1_PORTLEN, card->name)) {
		printk(KERN_WARNING "b1isa: ports 0x%03x-0x%03x in use.\n",
		       card->port, card->port + AVMB1_PORTLEN);
		retval = -EBUSY;
		goto err_free;
	}
	retval = request_irq(card->irq, b1_interrupt, 0, card->name, card);
	if (retval) {
		printk(KERN_ERR "b1isa: unable to get IRQ %d.\n", card->irq);
		goto err_release_region;
	}
	b1_reset(card->port);
	if ((retval = b1_detect(card->port, card->cardtype)) != 0) {
		printk(KERN_NOTICE "b1isa: NO card at 0x%x (%d)\n",
		       card->port, retval);
		retval = -ENODEV;
		goto err_free_irq;
	}
	b1_reset(card->port);
	b1_getrevision(card);

	cinfo->capi_ctrl = attach_capi_ctr(&b1isa_driver, card->name, cinfo);
	if (!cinfo->capi_ctrl) {
		printk(KERN_ERR "b1isa: attach controller failed.\n");
		retval = -EBUSY;
		goto err_free_irq;
	}

	printk(KERN_INFO
		"%s: AVM B1 ISA at i/o %#x, irq %d, revision %d\n",
		b1isa_driver.name, card->port, card->irq, card->revision);

	pci_set_drvdata(pdev, cinfo);
	return 0;

 err_free_irq:
	free_irq(card->irq, card);
 err_release_region:
	release_region(card->port, AVMB1_PORTLEN);
 err_free:
	b1_free_card(card);
 err:
	return retval;
}

static char *b1isa_procinfo(struct capi_ctr *ctrl)
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

static struct capi_driver b1isa_driver = {
	owner: THIS_MODULE,
	name: "b1isa",
	revision: "0.0",
	load_firmware: b1_load_firmware,
	reset_ctr: b1_reset_ctr,
	register_appl: b1_register_appl,
	release_appl: b1_release_appl,
	send_message: b1_send_message,
	
	procinfo: b1isa_procinfo,
	ctr_read_proc: b1ctl_read_proc,
};

#define MAX_CARDS 4
static struct pci_dev isa_dev[MAX_CARDS];
static int io[MAX_CARDS];
static int irq[MAX_CARDS];

MODULE_PARM(io, "1-" __MODULE_STRING(MAX_CARDS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(MAX_CARDS) "i");
MODULE_PARM_DESC(io, "I/O base address(es)");
MODULE_PARM_DESC(irq, "IRQ number(s) (assigned)");

static int __init b1isa_init(void)
{
	int i, retval;
	int found = 0;

	b1_set_revision(&b1isa_driver, revision);
        attach_capi_driver(&b1isa_driver);

	for (i = 0; i < MAX_CARDS; i++) {
		if (!io[i])
			break;

		isa_dev[i].resource[0].start = io[i];
		isa_dev[i].irq_resource[0].start = irq[i];

		if (b1isa_probe(&isa_dev[i]) == 0)
			found++;
	}
	if (found == 0) {
		retval = -ENODEV;
		goto err;
	}
	retval = 0;
	goto out;

 err:
	detach_capi_driver(&b1isa_driver);
 out:
	return retval;
}

static void __exit b1isa_exit(void)
{
	int i;

	for (i = 0; i < MAX_CARDS; i++) {
		if (!io[i])
			break;

		b1isa_remove(&isa_dev[i]);
	}
	detach_capi_driver(&b1isa_driver);
}

module_init(b1isa_init);
module_exit(b1isa_exit);
