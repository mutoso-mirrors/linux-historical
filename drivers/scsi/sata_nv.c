/*
 *  sata_nv.c - NVIDIA nForce SATA
 *
 *  Copyright 2004 NVIDIA Corp.  All rights reserved.
 *  Copyright 2004 Andrew Chew
 *
 *  The contents of this file are subject to the Open
 *  Software License version 1.1 that can be found at
 *  http://www.opensource.org/licenses/osl-1.1.txt and is included herein
 *  by reference.
 *
 *  Alternatively, the contents of this file may be used under the terms
 *  of the GNU General Public License version 2 (the "GPL") as distributed
 *  in the kernel source COPYING file, in which case the provisions of
 *  the GPL are applicable instead of the above.  If you wish to allow
 *  the use of your version of this file only under the terms of the
 *  GPL and not to allow others to use your version of this file under
 *  the OSL, indicate your decision by deleting the provisions above and
 *  replace them with the notice and other provisions required by the GPL.
 *  If you do not delete the provisions above, a recipient may use your
 *  version of this file under either the OSL or the GPL.
 *
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include <linux/libata.h>

#define DRV_NAME			"sata_nv"
#define DRV_VERSION			"0.01"

#define NV_PORTS			2
#define NV_PIO_MASK			0x1f
#define NV_MWDMA_MASK			0x07
#define NV_UDMA_MASK			0x7f
#define NV_PORT0_BMDMA_REG_OFFSET	0x00
#define NV_PORT1_BMDMA_REG_OFFSET	0x08
#define NV_PORT0_SCR_REG_OFFSET		0x00
#define NV_PORT1_SCR_REG_OFFSET		0x40

#define NV_INT_STATUS			0x10
#define NV_INT_STATUS_PDEV_INT		0x01
#define NV_INT_STATUS_PDEV_PM		0x02
#define NV_INT_STATUS_PDEV_ADDED	0x04
#define NV_INT_STATUS_PDEV_REMOVED	0x08
#define NV_INT_STATUS_SDEV_INT		0x10
#define NV_INT_STATUS_SDEV_PM		0x20
#define NV_INT_STATUS_SDEV_ADDED	0x40
#define NV_INT_STATUS_SDEV_REMOVED	0x80
#define NV_INT_STATUS_PDEV_HOTPLUG	(NV_INT_STATUS_PDEV_ADDED | \
					NV_INT_STATUS_PDEV_REMOVED)
#define NV_INT_STATUS_SDEV_HOTPLUG	(NV_INT_STATUS_SDEV_ADDED | \
					NV_INT_STATUS_SDEV_REMOVED)
#define NV_INT_STATUS_HOTPLUG		(NV_INT_STATUS_PDEV_HOTPLUG | \
					NV_INT_STATUS_SDEV_HOTPLUG)

#define NV_INT_ENABLE			0x11
#define NV_INT_ENABLE_PDEV_MASK		0x01
#define NV_INT_ENABLE_PDEV_PM		0x02
#define NV_INT_ENABLE_PDEV_ADDED	0x04
#define NV_INT_ENABLE_PDEV_REMOVED	0x08
#define NV_INT_ENABLE_SDEV_MASK		0x10
#define NV_INT_ENABLE_SDEV_PM		0x20
#define NV_INT_ENABLE_SDEV_ADDED	0x40
#define NV_INT_ENABLE_SDEV_REMOVED	0x80
#define NV_INT_ENABLE_PDEV_HOTPLUG	(NV_INT_ENABLE_PDEV_ADDED | \
					NV_INT_ENABLE_PDEV_REMOVED)
#define NV_INT_ENABLE_SDEV_HOTPLUG	(NV_INT_ENABLE_SDEV_ADDED | \
					NV_INT_ENABLE_SDEV_REMOVED)
#define NV_INT_ENABLE_HOTPLUG		(NV_INT_ENABLE_PDEV_HOTPLUG | \
					NV_INT_ENABLE_SDEV_HOTPLUG)

#define NV_INT_CONFIG			0x12
#define NV_INT_CONFIG_METHD		0x01 // 0 = INT, 1 = SMI

static int nv_init_one (struct pci_dev *pdev, const struct pci_device_id *ent);
irqreturn_t nv_interrupt (int irq, void *dev_instance, struct pt_regs *regs);
static u32 nv_scr_read (struct ata_port *ap, unsigned int sc_reg);
static void nv_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val);
static void nv_host_stop (struct ata_host_set *host_set);

static struct pci_device_id nv_pci_tbl[] = {
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE2S_SATA,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE3S_SATA2,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_SATA,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_CK804_SATA2,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_SATA,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_NFORCE_MCP04_SATA2,
		PCI_ANY_ID, PCI_ANY_ID, },
	{ 0, } /* terminate list */
};

static struct pci_driver nv_pci_driver = {
	.name			= DRV_NAME,
	.id_table		= nv_pci_tbl,
	.probe			= nv_init_one,
	.remove			= ata_pci_remove_one,
};

static Scsi_Host_Template nv_sht = {
	.module			= THIS_MODULE,
	.name			= DRV_NAME,
	.queuecommand		= ata_scsi_queuecmd,
	.eh_strategy_handler	= ata_scsi_error,
	.can_queue		= ATA_DEF_QUEUE,
	.this_id		= ATA_SHT_THIS_ID,
	.sg_tablesize		= ATA_MAX_PRD,
	.max_sectors		= ATA_MAX_SECTORS,
	.cmd_per_lun		= ATA_SHT_CMD_PER_LUN,
	.emulated		= ATA_SHT_EMULATED,
	.use_clustering		= ATA_SHT_USE_CLUSTERING,
	.proc_name		= DRV_NAME,
	.dma_boundary		= ATA_DMA_BOUNDARY,
	.slave_configure	= ata_scsi_slave_config,
	.bios_param		= ata_std_bios_param,
};

static struct ata_port_operations nv_ops = {
	.port_disable		= ata_port_disable,
	.tf_load		= ata_tf_load_pio,
	.tf_read		= ata_tf_read_pio,
	.exec_command		= ata_exec_command_pio,
	.check_status		= ata_check_status_pio,
	.phy_reset		= sata_phy_reset,
	.bmdma_setup		= ata_bmdma_setup_pio,
	.bmdma_start		= ata_bmdma_start_pio,
	.qc_prep		= ata_qc_prep,
	.qc_issue		= ata_qc_issue_prot,
	.eng_timeout		= ata_eng_timeout,
	.irq_handler		= nv_interrupt,
	.irq_clear		= ata_bmdma_irq_clear,
	.scr_read		= nv_scr_read,
	.scr_write		= nv_scr_write,
	.port_start		= ata_port_start,
	.port_stop		= ata_port_stop,
	.host_stop		= nv_host_stop,
};

MODULE_AUTHOR("NVIDIA");
MODULE_DESCRIPTION("low-level driver for NVIDIA nForce SATA controller");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(pci, nv_pci_tbl);

irqreturn_t nv_interrupt (int irq, void *dev_instance, struct pt_regs *regs)
{
	struct ata_host_set *host_set = dev_instance;
	unsigned int i;
	unsigned int handled = 0;
	unsigned long flags;
	u8 intr_status;
	u8 intr_enable;

	spin_lock_irqsave(&host_set->lock, flags);

	for (i = 0; i < host_set->n_ports; i++) {
		struct ata_port *ap;

		ap = host_set->ports[i];
		if (ap && (!(ap->flags & ATA_FLAG_PORT_DISABLED))) {
			struct ata_queued_cmd *qc;

			qc = ata_qc_from_tag(ap, ap->active_tag);
			if (qc && (!(qc->tf.ctl & ATA_NIEN)))
				handled += ata_host_intr(ap, qc);
		}

		intr_status = inb(ap->ioaddr.scr_addr + NV_INT_STATUS);
		intr_enable = inb(ap->ioaddr.scr_addr + NV_INT_ENABLE);

		// Clear interrupt status.
		outb(0xff, ap->ioaddr.scr_addr + NV_INT_STATUS);

		if (intr_status & NV_INT_STATUS_HOTPLUG) {
			if (intr_status & NV_INT_STATUS_PDEV_ADDED) {
				printk(KERN_WARNING "ata%u: "
					"Primary device added\n", ap->id);
			}

			if (intr_status & NV_INT_STATUS_PDEV_REMOVED) {
				printk(KERN_WARNING "ata%u: "
					"Primary device removed\n", ap->id);
			}

			if (intr_status & NV_INT_STATUS_SDEV_ADDED) {
				printk(KERN_WARNING "ata%u: "
					"Secondary device added\n", ap->id);
			}

			if (intr_status & NV_INT_STATUS_SDEV_REMOVED) {
				printk(KERN_WARNING "ata%u: "
					"Secondary device removed\n", ap->id);
			}
		}
	}

	spin_unlock_irqrestore(&host_set->lock, flags);

	return IRQ_RETVAL(handled);
}

static u32 nv_scr_read (struct ata_port *ap, unsigned int sc_reg)
{
	if (sc_reg > SCR_CONTROL)
		return 0xffffffffU;

	return inl(ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void nv_scr_write (struct ata_port *ap, unsigned int sc_reg, u32 val)
{
	if (sc_reg > SCR_CONTROL)
		return;

	outl(val, ap->ioaddr.scr_addr + (sc_reg * 4));
}

static void nv_host_stop (struct ata_host_set *host_set)
{
	int i;

	for (i=0; i<host_set->n_ports; i++) {
		u8 intr_mask;

		// Disable hotplug event interrupts.
		intr_mask = inb(host_set->ports[i]->ioaddr.scr_addr +
				NV_INT_ENABLE);
		intr_mask &= ~(NV_INT_ENABLE_HOTPLUG);
		outb(intr_mask, host_set->ports[i]->ioaddr.scr_addr +
				NV_INT_ENABLE);
	}
}

static int nv_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	static int printed_version = 0;
	struct ata_probe_ent *probe_ent = NULL;
	int i;
	int rc;

	if (!printed_version++)
		printk(KERN_DEBUG DRV_NAME " version " DRV_VERSION "\n");

	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc = pci_request_regions(pdev, DRV_NAME);
	if (rc)
		goto err_out;

	rc = pci_set_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;
	rc = pci_set_consistent_dma_mask(pdev, ATA_DMA_MASK);
	if (rc)
		goto err_out_regions;

	probe_ent = kmalloc(sizeof(*probe_ent), GFP_KERNEL);
	if (!probe_ent) {
		rc = -ENOMEM;
		goto err_out_regions;
	}

	memset(probe_ent, 0, sizeof(*probe_ent));
	INIT_LIST_HEAD(&probe_ent->node);

	probe_ent->pdev = pdev;
	probe_ent->sht = &nv_sht;
	probe_ent->host_flags = ATA_FLAG_SATA |
				ATA_FLAG_SATA_RESET |
				ATA_FLAG_SRST |
				ATA_FLAG_NO_LEGACY;
	probe_ent->port_ops = &nv_ops;
	probe_ent->n_ports = NV_PORTS;
	probe_ent->irq = pdev->irq;
	probe_ent->irq_flags = SA_SHIRQ;
	probe_ent->pio_mask = NV_PIO_MASK;
	probe_ent->mwdma_mask = NV_MWDMA_MASK;
	probe_ent->udma_mask = NV_UDMA_MASK;

	probe_ent->port[0].cmd_addr = pci_resource_start(pdev, 0);
	ata_std_ports(&probe_ent->port[0]);
	probe_ent->port[0].altstatus_addr =
	probe_ent->port[0].ctl_addr =
		pci_resource_start(pdev, 1) | ATA_PCI_CTL_OFS;
	probe_ent->port[0].bmdma_addr =
		pci_resource_start(pdev, 4) | NV_PORT0_BMDMA_REG_OFFSET;
	probe_ent->port[0].scr_addr =
		pci_resource_start(pdev, 5) | NV_PORT0_SCR_REG_OFFSET;

	probe_ent->port[1].cmd_addr = pci_resource_start(pdev, 2);
	ata_std_ports(&probe_ent->port[1]);
	probe_ent->port[1].altstatus_addr =
	probe_ent->port[1].ctl_addr =
		pci_resource_start(pdev, 3) | ATA_PCI_CTL_OFS;
	probe_ent->port[1].bmdma_addr =
		pci_resource_start(pdev, 4) | NV_PORT1_BMDMA_REG_OFFSET;
	probe_ent->port[1].scr_addr =
		pci_resource_start(pdev, 5) | NV_PORT1_SCR_REG_OFFSET;

	pci_set_master(pdev);

	rc = ata_device_add(probe_ent);
	if (rc != NV_PORTS)
		goto err_out_regions;

	// Enable hotplug event interrupts.
	for (i=0; i<probe_ent->n_ports; i++) {
		u8 intr_mask;

		outb(NV_INT_STATUS_HOTPLUG, probe_ent->port[i].scr_addr +
						NV_INT_STATUS);

		intr_mask = inb(probe_ent->port[i].scr_addr + NV_INT_ENABLE);
		intr_mask |= NV_INT_ENABLE_HOTPLUG;
		outb(intr_mask, probe_ent->port[i].scr_addr + NV_INT_ENABLE);
	}

	kfree(probe_ent);

	return 0;

err_out_regions:
	pci_release_regions(pdev);

err_out:
	pci_disable_device(pdev);
	return rc;
}

static int __init nv_init(void)
{
	return pci_module_init(&nv_pci_driver);
}

static void __exit nv_exit(void)
{
	pci_unregister_driver(&nv_pci_driver);
}

module_init(nv_init);
module_exit(nv_exit);
