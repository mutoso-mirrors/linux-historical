/*
 * Driver for AVM Fritz!PCI, Fritz!PCI v2, Fritz!PnP ISDN cards
 *
 * Author       Kai Germaschewski
 * Copyright    2001 by Kai Germaschewski  <kai.germaschewski@gmx.de>
 *              2001 by Karsten Keil       <keil@isdn4linux.de>
 * 
 * based upon Karsten Keil's original avm_pci.c driver
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * Thanks to Wizard Computersysteme GmbH, Bremervoerde and
 *           SoHaNet Technology GmbH, Berlin
 * for supporting the development of this driver
 */


/* TODO:
 *
 * o POWER PC
 * o clean up debugging
 * o tx_skb at PH_DEACTIVATE time
 */

#include <linux/version.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pnp.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include "hisax_fcpcipnp.h"

// debugging cruft
#define __debug_variable debug
#include "hisax_debug.h"

#ifdef CONFIG_HISAX_DEBUG
static int debug = 0;
MODULE_PARM(debug, "i");
#endif

MODULE_AUTHOR("Kai Germaschewski <kai.germaschewski@gmx.de>/Karsten Keil <kkeil@suse.de>");
MODULE_DESCRIPTION("AVM Fritz!PCI/PnP ISDN driver");

// FIXME temporary hack until I sort out the new PnP stuff
#define __ISAPNP__

static struct pci_device_id fcpci_ids[] __devinitdata = {
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_A1   , PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) "Fritz!Card PCI" },
	{ PCI_VENDOR_ID_AVM, PCI_DEVICE_ID_AVM_A1_V2, PCI_ANY_ID, PCI_ANY_ID,
	  0, 0, (unsigned long) "Fritz!Card PCI v2" },
	{ }
};
MODULE_DEVICE_TABLE(pci, fcpci_ids);

static struct pnp_card_device_id fcpnp_ids[] __devinitdata = {
	{ .id = "AVM0900", 
	  .driver_data = (unsigned long) "Fritz!Card PnP",
	  .devs = { { "AVM0900" } } }
};
MODULE_DEVICE_TABLE(pnp_card, fcpnp_ids);

static int protocol = 2;       /* EURO-ISDN Default */
MODULE_PARM(protocol, "i");
MODULE_LICENSE("GPL");

// ----------------------------------------------------------------------

#define  AVM_INDEX              0x04
#define  AVM_DATA               0x10

#define	 AVM_IDX_HDLC_1		0x00
#define	 AVM_IDX_HDLC_2		0x01
#define	 AVM_IDX_ISAC_FIFO	0x02
#define	 AVM_IDX_ISAC_REG_LOW	0x04
#define	 AVM_IDX_ISAC_REG_HIGH	0x06

#define  AVM_STATUS0            0x02

#define  AVM_STATUS0_IRQ_ISAC	0x01
#define  AVM_STATUS0_IRQ_HDLC	0x02
#define  AVM_STATUS0_IRQ_TIMER	0x04
#define  AVM_STATUS0_IRQ_MASK	0x07

#define  AVM_STATUS0_RESET	0x01
#define  AVM_STATUS0_DIS_TIMER	0x02
#define  AVM_STATUS0_RES_TIMER	0x04
#define  AVM_STATUS0_ENA_IRQ	0x08
#define  AVM_STATUS0_TESTBIT	0x10

#define  AVM_STATUS1            0x03
#define  AVM_STATUS1_ENA_IOM	0x80

#define  HDLC_FIFO		0x0
#define  HDLC_STATUS		0x4
#define  HDLC_CTRL		0x4

#define  HDLC_MODE_ITF_FLG	0x01
#define  HDLC_MODE_TRANS	0x02
#define  HDLC_MODE_CCR_7	0x04
#define  HDLC_MODE_CCR_16	0x08
#define  HDLC_MODE_TESTLOOP	0x80

#define  HDLC_INT_XPR		0x80
#define  HDLC_INT_XDU		0x40
#define  HDLC_INT_RPR		0x20
#define  HDLC_INT_MASK		0xE0

#define  HDLC_STAT_RME		0x01
#define  HDLC_STAT_RDO		0x10
#define  HDLC_STAT_CRCVFRRAB	0x0E
#define  HDLC_STAT_CRCVFR	0x06
#define  HDLC_STAT_RML_MASK	0x3f00

#define  HDLC_CMD_XRS		0x80
#define  HDLC_CMD_XME		0x01
#define  HDLC_CMD_RRS		0x20
#define  HDLC_CMD_XML_MASK	0x3f00

#define  AVM_HDLC_FIFO_1        0x10
#define  AVM_HDLC_FIFO_2        0x18

#define  AVM_HDLC_STATUS_1      0x14
#define  AVM_HDLC_STATUS_2      0x1c

#define  AVM_ISACSX_INDEX       0x04
#define  AVM_ISACSX_DATA        0x08

// ----------------------------------------------------------------------
// Fritz!PCI

static unsigned char fcpci_read_isac(struct isac *isac, unsigned char offset)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned char idx = (offset > 0x2f) ? 
		AVM_IDX_ISAC_REG_HIGH : AVM_IDX_ISAC_REG_LOW;
	unsigned char val;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outb(idx, adapter->io + AVM_INDEX);
	val = inb(adapter->io + AVM_DATA + (offset & 0xf));
 	spin_unlock_irqrestore(&adapter->hw_lock, flags);
	DBG(0x1000, " port %#x, value %#x",
	    offset, val);
	return val;
}

static void fcpci_write_isac(struct isac *isac, unsigned char offset,
			     unsigned char value)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned char idx = (offset > 0x2f) ? 
		AVM_IDX_ISAC_REG_HIGH : AVM_IDX_ISAC_REG_LOW;
	unsigned long flags;

	DBG(0x1000, " port %#x, value %#x",
	    offset, value);
	spin_lock_irqsave(&adapter->hw_lock, flags);
	outb(idx, adapter->io + AVM_INDEX);
	outb(value, adapter->io + AVM_DATA + (offset & 0xf));
 	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static void fcpci_read_isac_fifo(struct isac *isac, unsigned char * data, 
				 int size)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outb(AVM_IDX_ISAC_FIFO, adapter->io + AVM_INDEX);
	insb(adapter->io + AVM_DATA, data, size);
 	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static void fcpci_write_isac_fifo(struct isac *isac, unsigned char * data, 
				  int size)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outb(AVM_IDX_ISAC_FIFO, adapter->io + AVM_INDEX);
	outsb(adapter->io + AVM_DATA, data, size);
 	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static u32 fcpci_read_hdlc_status(struct fritz_adapter *adapter, int nr)
{
	u32 val;
	int idx = nr ? AVM_IDX_HDLC_2 : AVM_IDX_HDLC_1;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outl(idx, adapter->io + AVM_INDEX);
	val = inl(adapter->io + AVM_DATA + HDLC_STATUS);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
	return val;
}

static void __fcpci_write_ctrl(struct fritz_bcs *bcs, int which)
{
	struct fritz_adapter *adapter = bcs->adapter;
	int idx = bcs->channel ? AVM_IDX_HDLC_2 : AVM_IDX_HDLC_1;

	DBG(0x40, "hdlc %c wr%x ctrl %x",
	    'A' + bcs->channel, which, bcs->ctrl.ctrl);

	outl(idx, adapter->io + AVM_INDEX);
	outl(bcs->ctrl.ctrl, adapter->io + AVM_DATA + HDLC_CTRL);
}

static void fcpci_write_ctrl(struct fritz_bcs *bcs, int which)
{
	struct fritz_adapter *adapter = bcs->adapter;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	__fcpci_write_ctrl(bcs, which);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

// ----------------------------------------------------------------------
// Fritz!PCI v2

static unsigned char fcpci2_read_isac(struct isac *isac, unsigned char offset)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned char val;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outl(offset, adapter->io + AVM_ISACSX_INDEX);
	val = inl(adapter->io + AVM_ISACSX_DATA);
 	spin_unlock_irqrestore(&adapter->hw_lock, flags);
	DBG(0x1000, " port %#x, value %#x",
	    offset, val);

	return val;
}

static void fcpci2_write_isac(struct isac *isac, unsigned char offset, 
			      unsigned char value)
{
	struct fritz_adapter *adapter = isac->priv;
	unsigned long flags;

	DBG(0x1000, " port %#x, value %#x",
	    offset, value);
	spin_lock_irqsave(&adapter->hw_lock, flags);
	outl(offset, adapter->io + AVM_ISACSX_INDEX);
	outl(value, adapter->io + AVM_ISACSX_DATA);
 	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static void fcpci2_read_isac_fifo(struct isac *isac, unsigned char * data, 
				  int size)
{
	struct fritz_adapter *adapter = isac->priv;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outl(0, adapter->io + AVM_ISACSX_INDEX);
	for (i = 0; i < size; i++)
		data[i] = inl(adapter->io + AVM_ISACSX_DATA);
 	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static void fcpci2_write_isac_fifo(struct isac *isac, unsigned char * data, 
				   int size)
{
	struct fritz_adapter *adapter = isac->priv;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outl(0, adapter->io + AVM_ISACSX_INDEX);
	for (i = 0; i < size; i++)
		outl(data[i], adapter->io + AVM_ISACSX_DATA);
 	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

static u32 fcpci2_read_hdlc_status(struct fritz_adapter *adapter, int nr)
{
	int offset = nr ? AVM_HDLC_STATUS_2 : AVM_HDLC_STATUS_1;

	return inl(adapter->io + offset);
}

static void fcpci2_write_ctrl(struct fritz_bcs *bcs, int which)
{
	struct fritz_adapter *adapter = bcs->adapter;
	int offset = bcs->channel ? AVM_HDLC_STATUS_2 : AVM_HDLC_STATUS_1;

	DBG(0x40, "hdlc %c wr%x ctrl %x",
	    'A' + bcs->channel, which, bcs->ctrl.ctrl);

	outl(bcs->ctrl.ctrl, adapter->io + offset);
}

// ----------------------------------------------------------------------
// Fritz!PnP (ISAC access as for Fritz!PCI)

static u32 fcpnp_read_hdlc_status(struct fritz_adapter *adapter, int nr)
{
	unsigned char idx = nr ? AVM_IDX_HDLC_2 : AVM_IDX_HDLC_1;
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	outb(idx, adapter->io + AVM_INDEX);
	val = inb(adapter->io + AVM_DATA + HDLC_STATUS);
	if (val & HDLC_INT_RPR)
		val |= inb(adapter->io + AVM_DATA + HDLC_STATUS + 1) << 8;
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
	return val;
}

static void __fcpnp_write_ctrl(struct fritz_bcs *bcs, int which)
{
	struct fritz_adapter *adapter = bcs->adapter;
	unsigned char idx = bcs->channel ? AVM_IDX_HDLC_2 : AVM_IDX_HDLC_1;

	DBG(0x40, "hdlc %c wr%x ctrl %x",
	    'A' + bcs->channel, which, bcs->ctrl.ctrl);

	outb(idx, adapter->io + AVM_INDEX);
	if (which & 4)
		outb(bcs->ctrl.sr.mode, 
		     adapter->io + AVM_DATA + HDLC_STATUS + 2);
	if (which & 2)
		outb(bcs->ctrl.sr.xml, 
		     adapter->io + AVM_DATA + HDLC_STATUS + 1);
	if (which & 1)
		outb(bcs->ctrl.sr.cmd,
		     adapter->io + AVM_DATA + HDLC_STATUS + 0);
}

static void fcpnp_write_ctrl(struct fritz_bcs *bcs, int which)
{
	struct fritz_adapter *adapter = bcs->adapter;
	unsigned long flags;

	spin_lock_irqsave(&adapter->hw_lock, flags);
	__fcpnp_write_ctrl(bcs, which);
	spin_unlock_irqrestore(&adapter->hw_lock, flags);
}

// ----------------------------------------------------------------------

static inline void B_L1L2(struct fritz_bcs *bcs, int pr, void *arg)
{
	struct hisax_if *ifc = (struct hisax_if *) &bcs->b_if;

	DBG(2, "pr %#x", pr);
	ifc->l1l2(ifc, pr, arg);
}

static void hdlc_fill_fifo(struct fritz_bcs *bcs)
{
	struct fritz_adapter *adapter = bcs->adapter;
	struct sk_buff *skb = bcs->tx_skb;
	int count;
	int fifo_size = 32;
	unsigned long flags;
	unsigned char *p;

	DBG(0x40, "hdlc_fill_fifo");

	if (skb->len == 0)
		BUG();

	bcs->ctrl.sr.cmd &= ~HDLC_CMD_XME;
	if (bcs->tx_skb->len > fifo_size) {
		count = fifo_size;
	} else {
		count = bcs->tx_skb->len;
		if (bcs->mode != L1_MODE_TRANS)
			bcs->ctrl.sr.cmd |= HDLC_CMD_XME;
	}
	DBG(0x40, "hdlc_fill_fifo %d/%d", count, bcs->tx_skb->len);
	p = bcs->tx_skb->data;
	skb_pull(bcs->tx_skb, count);
	bcs->tx_cnt += count;
	bcs->ctrl.sr.xml = ((count == fifo_size) ? 0 : count);

	switch (adapter->type) {
	case AVM_FRITZ_PCI:
		spin_lock_irqsave(&adapter->hw_lock, flags);
		// sets the correct AVM_INDEX, too
		__fcpci_write_ctrl(bcs, 3);
		outsl(adapter->io + AVM_DATA + HDLC_FIFO,
		      p, (count + 3) / 4);
		spin_unlock_irqrestore(&adapter->hw_lock, flags);
		break;
	case AVM_FRITZ_PCIV2:
		fcpci2_write_ctrl(bcs, 3);
		outsl(adapter->io + 
		      (bcs->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1),
		      p, (count + 3) / 4);
		break;
	case AVM_FRITZ_PNP:
		spin_lock_irqsave(&adapter->hw_lock, flags);
		// sets the correct AVM_INDEX, too
		__fcpnp_write_ctrl(bcs, 3);
		outsb(adapter->io + AVM_DATA, p, count);
		spin_unlock_irqrestore(&adapter->hw_lock, flags);
		break;
	}
}

static inline void hdlc_empty_fifo(struct fritz_bcs *bcs, int count)
{
	struct fritz_adapter *adapter = bcs->adapter;
	unsigned char *p;
	unsigned char idx = bcs->channel ? AVM_IDX_HDLC_2 : AVM_IDX_HDLC_1;

	DBG(0x10, "hdlc_empty_fifo %d", count);
	if (bcs->rcvidx + count > HSCX_BUFMAX) {
		DBG(0x10, "hdlc_empty_fifo: incoming packet too large");
		return;
	}
	p = bcs->rcvbuf + bcs->rcvidx;
	bcs->rcvidx += count;
	switch (adapter->type) {
	case AVM_FRITZ_PCI:
		spin_lock(&adapter->hw_lock);
		outl(idx, adapter->io + AVM_INDEX);
		insl(adapter->io + AVM_DATA + HDLC_FIFO, 
		     p, (count + 3) / 4);
		spin_unlock(&adapter->hw_lock);
		break;
	case AVM_FRITZ_PCIV2:
		insl(adapter->io + 
		     (bcs->channel ? AVM_HDLC_FIFO_2 : AVM_HDLC_FIFO_1),
		     p, (count + 3) / 4);
		break;
	case AVM_FRITZ_PNP:
		spin_lock(&adapter->hw_lock);
		outb(idx, adapter->io + AVM_INDEX);
		insb(adapter->io + AVM_DATA, p, count);
		spin_unlock(&adapter->hw_lock);
		break;
	}
}

static inline void hdlc_rpr_irq(struct fritz_bcs *bcs, u32 stat)
{
	struct fritz_adapter *adapter = bcs->adapter;
	struct sk_buff *skb;
	int len;

	if (stat & HDLC_STAT_RDO) {
		DBG(0x10, "RDO");
		bcs->ctrl.sr.xml = 0;
		bcs->ctrl.sr.cmd |= HDLC_CMD_RRS;
		adapter->write_ctrl(bcs, 1);
		bcs->ctrl.sr.cmd &= ~HDLC_CMD_RRS;
		adapter->write_ctrl(bcs, 1);
		bcs->rcvidx = 0;
		return;
	}

	len = (stat & HDLC_STAT_RML_MASK) >> 8;
	if (len == 0)
		len = 32;

	hdlc_empty_fifo(bcs, len);

	if ((stat & HDLC_STAT_RME) || (bcs->mode == L1_MODE_TRANS)) {
		if (((stat & HDLC_STAT_CRCVFRRAB)== HDLC_STAT_CRCVFR) ||
		    (bcs->mode == L1_MODE_TRANS)) {
			skb = dev_alloc_skb(bcs->rcvidx);
			if (!skb) {
				printk(KERN_WARNING "HDLC: receive out of memory\n");
			} else {
				memcpy(skb_put(skb, bcs->rcvidx), bcs->rcvbuf,
				       bcs->rcvidx);
				DBG_SKB(1, skb);
				B_L1L2(bcs, PH_DATA | INDICATION, skb);
			}
			bcs->rcvidx = 0;
		} else {
			DBG(0x10, "ch%d invalid frame %#x",
			    bcs->channel, stat);
			bcs->rcvidx = 0;
		}
	}
}

static inline void hdlc_xdu_irq(struct fritz_bcs *bcs)
{
	struct fritz_adapter *adapter = bcs->adapter;

	/* Here we lost an TX interrupt, so
	 * restart transmitting the whole frame.
	 */
	bcs->ctrl.sr.xml = 0;
	bcs->ctrl.sr.cmd |= HDLC_CMD_XRS;
	adapter->write_ctrl(bcs, 1);
	bcs->ctrl.sr.cmd &= ~HDLC_CMD_XRS;
	adapter->write_ctrl(bcs, 1);

	if (!bcs->tx_skb) {
		DBG(0x10, "XDU without skb");
		return;
	}
	skb_push(bcs->tx_skb, bcs->tx_cnt);
	bcs->tx_cnt = 0;
}

static inline void hdlc_xpr_irq(struct fritz_bcs *bcs)
{
	struct sk_buff *skb;

	skb = bcs->tx_skb;
	if (!skb)
		return;

	if (skb->len) {
		hdlc_fill_fifo(bcs);
		return;
	}
	bcs->tx_cnt = 0;
	bcs->tx_skb = NULL;
	B_L1L2(bcs, PH_DATA | CONFIRM, skb);
}

static void hdlc_irq_one(struct fritz_bcs *bcs, u32 stat)
{
	DBG(0x10, "ch%d stat %#x", bcs->channel, stat);
	if (stat & HDLC_INT_RPR) {
		DBG(0x10, "RPR");
		hdlc_rpr_irq(bcs, stat);
	}
	if (stat & HDLC_INT_XDU) {
		DBG(0x10, "XDU");
		hdlc_xdu_irq(bcs);
	}
	if (stat & HDLC_INT_XPR) {
		DBG(0x10, "XPR");
		hdlc_xpr_irq(bcs);
	}
}

static inline void hdlc_irq(struct fritz_adapter *adapter)
{
	int nr;
	u32 stat;

	for (nr = 0; nr < 2; nr++) {
		stat = adapter->read_hdlc_status(adapter, nr);
		DBG(0x10, "HDLC %c stat %#x", 'A' + nr, stat);
		if (stat & HDLC_INT_MASK)
			hdlc_irq_one(&adapter->bcs[nr], stat);
	}
}

static void modehdlc(struct fritz_bcs *bcs, int mode)
{
	struct fritz_adapter *adapter = bcs->adapter;
	
	DBG(0x40, "hdlc %c mode %d --> %d",
	    'A' + bcs->channel, bcs->mode, mode);

	if (bcs->mode == mode)
		return;

	bcs->ctrl.ctrl = 0;
	bcs->ctrl.sr.cmd  = HDLC_CMD_XRS | HDLC_CMD_RRS;
	switch (mode) {
	case L1_MODE_NULL:
		bcs->ctrl.sr.mode = HDLC_MODE_TRANS;
		adapter->write_ctrl(bcs, 5);
		break;
	case L1_MODE_TRANS:
	case L1_MODE_HDLC:
		bcs->rcvidx = 0;
		bcs->tx_cnt = 0;
		bcs->tx_skb = NULL;
		if (mode == L1_MODE_TRANS)
			bcs->ctrl.sr.mode = HDLC_MODE_TRANS;
		else
			bcs->ctrl.sr.mode = HDLC_MODE_ITF_FLG;
		adapter->write_ctrl(bcs, 5);
		bcs->ctrl.sr.cmd = HDLC_CMD_XRS;
		adapter->write_ctrl(bcs, 1);
		bcs->ctrl.sr.cmd = 0;
		break;
	}
	bcs->mode = mode;
}

static void fritz_b_l2l1(struct hisax_if *ifc, int pr, void *arg)
{
	struct fritz_bcs *bcs = ifc->priv;
	struct sk_buff *skb = arg;
	int mode;

	DBG(0x10, "pr %#x", pr);

	switch (pr) {
	case PH_DATA | REQUEST:
		if (bcs->tx_skb)
			BUG();
		
		bcs->tx_skb = skb;
		DBG_SKB(1, skb);
		hdlc_fill_fifo(bcs);
		break;
	case PH_ACTIVATE | REQUEST:
		mode = (int) arg;
		DBG(4,"B%d,PH_ACTIVATE_REQUEST %d", bcs->channel + 1, mode);
		modehdlc(bcs, mode);
		B_L1L2(bcs, PH_ACTIVATE | INDICATION, NULL);
		break;
	case PH_DEACTIVATE | REQUEST:
		DBG(4,"B%d,PH_DEACTIVATE_REQUEST", bcs->channel + 1);
		modehdlc(bcs, L1_MODE_NULL);
		B_L1L2(bcs, PH_DEACTIVATE | INDICATION, NULL);
		break;
	}
}

// ----------------------------------------------------------------------

static void fcpci2_irq(int intno, void *dev, struct pt_regs *regs)
{
	struct fritz_adapter *adapter = dev;
	unsigned char val;

	val = inb(adapter->io + AVM_STATUS0);
	if (!(val & AVM_STATUS0_IRQ_MASK))
		/* hopefully a shared  IRQ reqest */
		return;
	DBG(2, "STATUS0 %#x", val);
	if (val & AVM_STATUS0_IRQ_ISAC)
		isacsx_irq(&adapter->isac);

	if (val & AVM_STATUS0_IRQ_HDLC)
		hdlc_irq(adapter);
}

static void fcpci_irq(int intno, void *dev, struct pt_regs *regs)
{
	struct fritz_adapter *adapter = dev;
	unsigned char sval;

	sval = inb(adapter->io + 2);
	if ((sval & AVM_STATUS0_IRQ_MASK) == AVM_STATUS0_IRQ_MASK)
		/* possibly a shared  IRQ reqest */
		return;
	DBG(2, "sval %#x", sval);
	if (!(sval & AVM_STATUS0_IRQ_ISAC))
		isac_irq(&adapter->isac);

	if (!(sval & AVM_STATUS0_IRQ_HDLC))
		hdlc_irq(adapter);
}

// ----------------------------------------------------------------------

static inline void fcpci2_init(struct fritz_adapter *adapter)
{
	outb(AVM_STATUS0_RES_TIMER, adapter->io + AVM_STATUS0);
	outb(AVM_STATUS0_ENA_IRQ, adapter->io + AVM_STATUS0);

}

static inline void fcpci_init(struct fritz_adapter *adapter)
{
	outb(AVM_STATUS0_DIS_TIMER | AVM_STATUS0_RES_TIMER | 
	     AVM_STATUS0_ENA_IRQ, adapter->io + AVM_STATUS0);

	outb(AVM_STATUS1_ENA_IOM | adapter->irq, 
	     adapter->io + AVM_STATUS1);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(50*HZ / 1000); /* Timeout 50ms */
}

// ----------------------------------------------------------------------

static int __devinit fcpcipnp_setup(struct fritz_adapter *adapter)
{
	u32 val = 0;
	int retval;

	DBG(1,"");

	isac_init(&adapter->isac); // FIXME is this okay now

	retval = -EBUSY;
	if (!request_region(adapter->io, 32, "fcpcipnp"))
		goto err;

	switch (adapter->type) {
	case AVM_FRITZ_PCIV2:
		retval = request_irq(adapter->irq, fcpci2_irq, SA_SHIRQ, 
				     "fcpcipnp", adapter);
		break;
	case AVM_FRITZ_PCI:
		retval = request_irq(adapter->irq, fcpci_irq, SA_SHIRQ,
				     "fcpcipnp", adapter);
		break;
	case AVM_FRITZ_PNP:
		retval = request_irq(adapter->irq, fcpci_irq, 0,
				     "fcpcipnp", adapter);
		break;
	}
	if (retval)
		goto err_region;

	switch (adapter->type) {
	case AVM_FRITZ_PCIV2:
	case AVM_FRITZ_PCI:
		val = inl(adapter->io);
		break;
	case AVM_FRITZ_PNP:
		val = inb(adapter->io);
		val |= inb(adapter->io + 1) << 8;
		break;
	}

	DBG(1, "stat %#x Class %X Rev %d",
	    val, val & 0xff, (val>>8) & 0xff);

	spin_lock_init(&adapter->hw_lock);
	adapter->isac.priv = adapter;
	switch (adapter->type) {
	case AVM_FRITZ_PCIV2:
		adapter->isac.read_isac       = &fcpci2_read_isac;;
		adapter->isac.write_isac      = &fcpci2_write_isac;
		adapter->isac.read_isac_fifo  = &fcpci2_read_isac_fifo;
		adapter->isac.write_isac_fifo = &fcpci2_write_isac_fifo;

		adapter->read_hdlc_status     = &fcpci2_read_hdlc_status;
		adapter->write_ctrl           = &fcpci2_write_ctrl;
		break;
	case AVM_FRITZ_PCI:
		adapter->isac.read_isac       = &fcpci_read_isac;;
		adapter->isac.write_isac      = &fcpci_write_isac;
		adapter->isac.read_isac_fifo  = &fcpci_read_isac_fifo;
		adapter->isac.write_isac_fifo = &fcpci_write_isac_fifo;

		adapter->read_hdlc_status     = &fcpci_read_hdlc_status;
		adapter->write_ctrl           = &fcpci_write_ctrl;
		break;
	case AVM_FRITZ_PNP:
		adapter->isac.read_isac       = &fcpci_read_isac;;
		adapter->isac.write_isac      = &fcpci_write_isac;
		adapter->isac.read_isac_fifo  = &fcpci_read_isac_fifo;
		adapter->isac.write_isac_fifo = &fcpci_write_isac_fifo;

		adapter->read_hdlc_status     = &fcpnp_read_hdlc_status;
		adapter->write_ctrl           = &fcpnp_write_ctrl;
		break;
	}

	// Reset
	outb(0, adapter->io + AVM_STATUS0);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(50 * HZ / 1000); // 50 msec
	outb(AVM_STATUS0_RESET, adapter->io + AVM_STATUS0);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(50 * HZ / 1000); // 50 msec
	outb(0, adapter->io + AVM_STATUS0);
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(10 * HZ / 1000); // 10 msec

	switch (adapter->type) {
	case AVM_FRITZ_PCIV2:
		fcpci2_init(adapter);
		isacsx_setup(&adapter->isac);
		break;
	case AVM_FRITZ_PCI:
	case AVM_FRITZ_PNP:
		fcpci_init(adapter);
		isac_setup(&adapter->isac);
		break;
	}
	val = adapter->read_hdlc_status(adapter, 0);
	DBG(0x20, "HDLC A STA %x", val);
	val = adapter->read_hdlc_status(adapter, 1);
	DBG(0x20, "HDLC B STA %x", val);

	adapter->bcs[0].mode = -1;
	adapter->bcs[1].mode = -1;
	modehdlc(&adapter->bcs[0], L1_MODE_NULL);
	modehdlc(&adapter->bcs[1], L1_MODE_NULL);

	return 0;

 err_region:
	release_region(adapter->io, 32);
 err:
	return retval;
}

static void __devexit fcpcipnp_release(struct fritz_adapter *adapter)
{
	DBG(1,"");

	outb(0, adapter->io + AVM_STATUS0);
	free_irq(adapter->irq, adapter);
	release_region(adapter->io, 32);
}

// ----------------------------------------------------------------------

static struct fritz_adapter * __devinit 
new_adapter(struct pci_dev *pdev)
{
	struct fritz_adapter *adapter;
	struct hisax_b_if *b_if[2];
	int i;

	adapter = kmalloc(sizeof(struct fritz_adapter), GFP_KERNEL);
	if (!adapter)
		return NULL;

	memset(adapter, 0, sizeof(struct fritz_adapter));

	SET_MODULE_OWNER(&adapter->isac.hisax_d_if);
	adapter->isac.hisax_d_if.ifc.priv = &adapter->isac;
	adapter->isac.hisax_d_if.ifc.l2l1 = isac_d_l2l1;
	
	for (i = 0; i < 2; i++) {
		adapter->bcs[i].adapter = adapter;
		adapter->bcs[i].channel = i;
		adapter->bcs[i].b_if.ifc.priv = &adapter->bcs[i];
		adapter->bcs[i].b_if.ifc.l2l1 = fritz_b_l2l1;
	}

	pci_set_drvdata(pdev, adapter);

	for (i = 0; i < 2; i++)
		b_if[i] = &adapter->bcs[i].b_if;

	hisax_register(&adapter->isac.hisax_d_if, b_if, "fcpcipnp", protocol);

	return adapter;
}

static void delete_adapter(struct fritz_adapter *adapter)
{
	hisax_unregister(&adapter->isac.hisax_d_if);
	kfree(adapter);
}

static int __devinit fcpci_probe(struct pci_dev *pdev,
				 const struct pci_device_id *ent)
{
	struct fritz_adapter *adapter;
	int retval;

	retval = -ENOMEM;
	adapter = new_adapter(pdev);
	if (!adapter)
		goto err;

	if (pdev->device == PCI_DEVICE_ID_AVM_A1_V2) 
		adapter->type = AVM_FRITZ_PCIV2;
	else
		adapter->type = AVM_FRITZ_PCI;

	retval = pci_enable_device(pdev);
	if (retval)
		goto err_free;

	adapter->io = pci_resource_start(pdev, 1);
	adapter->irq = pdev->irq;

	printk(KERN_INFO "hisax_fcpcipnp: found adapter %s at %s\n",
	       (char *) ent->driver_data, pdev->slot_name);

	retval = fcpcipnp_setup(adapter);
	if (retval)
		goto err_free;

	return 0;
	
 err_free:
	delete_adapter(adapter);
 err:
	return retval;
}

static void __devexit fcpci_remove(struct pci_dev *pdev)
{
	struct fritz_adapter *adapter = pci_get_drvdata(pdev);

	fcpcipnp_release(adapter);
	pci_disable_device(pdev);
	delete_adapter(adapter);
}

static struct pci_driver fcpci_driver = {
	.name     = "fcpci",
	.probe    = fcpci_probe,
	.remove   = __devexit_p(fcpci_remove),
	.id_table = fcpci_ids,
};

#ifdef __ISAPNP__

static int __devinit fcpnp_probe(struct pnp_card *card,
				 const struct pnp_card_device_id *card_id)
{
	struct fritz_adapter *adapter;
	struct pnp_dev *pnp_dev;
	int retval;

	retval = -ENODEV;
	pnp_dev = pnp_request_card_device(card, card_id->devs[0].id, NULL);
	if (!pnp_dev)
		goto err;

	if (!pnp_port_valid(pnp_dev, 0) || !pnp_irq_valid(pnp_dev, 0))
		goto err;

	retval = -ENOMEM;
	adapter = new_adapter((struct pci_dev *)pnp_dev); // FIXME
	if (!adapter)
		goto err;
	
	adapter->type = AVM_FRITZ_PNP;
	adapter->io = pnp_port_start(pnp_dev, 0);
	adapter->irq = pnp_irq(pnp_dev, 0);
	
	printk(KERN_INFO "hisax_fcpcipnp: found adapter %s at IO %#x irq %d\n",
	       (char *) card_id->driver_data, adapter->io, adapter->irq);
	
	retval = fcpcipnp_setup(adapter);
	if (retval)
		goto err_delete;
	
	return 0;
	
 err_delete:
	delete_adapter(adapter);
 err:
	return retval;
}

static void __devexit fcpnp_remove(struct pnp_card *pcard)
{
	struct fritz_adapter *adapter = pnpc_get_drvdata(pcard);

	fcpcipnp_release(adapter);
	delete_adapter(adapter);
}

static struct pnpc_driver fcpnp_driver = {
	.name     = "fcpnp",
	.probe    = fcpnp_probe,
	.remove   = __devexit_p(fcpnp_remove),
	.id_table = fcpnp_ids,
};

#endif

static int __init hisax_fcpcipnp_init(void)
{
	int retval, pci_nr_found;

	printk(KERN_INFO "hisax_fcpcipnp: Fritz!Card PCI/PCIv2/PnP ISDN driver v0.0.1\n");

	retval = pci_register_driver(&fcpci_driver);
	if (retval < 0)
		goto out;
	pci_nr_found = retval;

#ifdef __ISAPNP__
	retval = pnpc_register_driver(&fcpnp_driver);
#else
	retval = 0;
#endif
	if (retval < 0)
		goto out_unregister_pci;

#if !defined(CONFIG_HOTPLUG) || defined(MODULE)
	if (pci_nr_found + retval == 0) {
		retval = -ENODEV;
		goto out_unregister_isapnp;
	}
#endif
	return 0;

#if !defined(CONFIG_HOTPLUG) || defined(MODULE)
 out_unregister_isapnp:
#ifdef __ISAPNP__
	pnpc_unregister_driver(&fcpnp_driver);
#endif
#endif
 out_unregister_pci:
	pci_unregister_driver(&fcpci_driver);
 out:
	return retval;
}

static void __exit hisax_fcpcipnp_exit(void)
{
#ifdef __ISAPNP__
	pnpc_unregister_driver(&fcpnp_driver);
#endif
	pci_unregister_driver(&fcpci_driver);
}

module_init(hisax_fcpcipnp_init);
module_exit(hisax_fcpcipnp_exit);
