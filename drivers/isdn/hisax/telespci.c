/* $Id: telespci.c,v 2.16.6.5 2001/09/23 22:24:52 kai Exp $
 *
 * low level stuff for Teles PCI isdn cards
 *
 * Author       Ton van Rosmalen
 *              Karsten Keil
 * Copyright    by Ton van Rosmalen
 *              by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include <linux/config.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>

extern const char *CardType[];
const char *telespci_revision = "$Revision: 2.16.6.5 $";

#define ZORAN_PO_RQ_PEN	0x02000000
#define ZORAN_PO_WR	0x00800000
#define ZORAN_PO_GID0	0x00000000
#define ZORAN_PO_GID1	0x00100000
#define ZORAN_PO_GREG0	0x00000000
#define ZORAN_PO_GREG1	0x00010000
#define ZORAN_PO_DMASK	0xFF

#define WRITE_ADDR_ISAC	(ZORAN_PO_WR | ZORAN_PO_GID0 | ZORAN_PO_GREG0)
#define READ_DATA_ISAC	(ZORAN_PO_GID0 | ZORAN_PO_GREG1)
#define WRITE_DATA_ISAC	(ZORAN_PO_WR | ZORAN_PO_GID0 | ZORAN_PO_GREG1)
#define WRITE_ADDR_HSCX	(ZORAN_PO_WR | ZORAN_PO_GID1 | ZORAN_PO_GREG0)
#define READ_DATA_HSCX	(ZORAN_PO_GID1 | ZORAN_PO_GREG1)
#define WRITE_DATA_HSCX	(ZORAN_PO_WR | ZORAN_PO_GID1 | ZORAN_PO_GREG1)

#define ZORAN_WAIT_NOBUSY	do { \
					portdata = readl(adr + 0x200); \
				} while (portdata & ZORAN_PO_RQ_PEN)

static inline u_char
readisac(unsigned long adr, u_char off)
{
	register unsigned int portdata;

	ZORAN_WAIT_NOBUSY;
	
	/* set address for ISAC */
	writel(WRITE_ADDR_ISAC | off, adr + 0x200);
	ZORAN_WAIT_NOBUSY;
	
	/* read data from ISAC */
	writel(READ_DATA_ISAC, adr + 0x200);
	ZORAN_WAIT_NOBUSY;
	return((u_char)(portdata & ZORAN_PO_DMASK));
}

static inline void
writeisac(unsigned long adr, u_char off, u_char data)
{
	register unsigned int portdata;

	ZORAN_WAIT_NOBUSY;
	
	/* set address for ISAC */
	writel(WRITE_ADDR_ISAC | off, adr + 0x200);
	ZORAN_WAIT_NOBUSY;

	/* write data to ISAC */
	writel(WRITE_DATA_ISAC | data, adr + 0x200);
	ZORAN_WAIT_NOBUSY;
}

static inline u_char
readhscx(unsigned long adr, int hscx, u_char off)
{
	register unsigned int portdata;

	ZORAN_WAIT_NOBUSY;
	/* set address for HSCX */
	writel(WRITE_ADDR_HSCX | ((hscx ? 0x40:0) + off), adr + 0x200);
	ZORAN_WAIT_NOBUSY;
	
	/* read data from HSCX */
	writel(READ_DATA_HSCX, adr + 0x200);
	ZORAN_WAIT_NOBUSY;
	return ((u_char)(portdata & ZORAN_PO_DMASK));
}

static inline void
writehscx(unsigned long adr, int hscx, u_char off, u_char data)
{
	register unsigned int portdata;

	ZORAN_WAIT_NOBUSY;
	/* set address for HSCX */
	writel(WRITE_ADDR_HSCX | ((hscx ? 0x40:0) + off), adr + 0x200);
	ZORAN_WAIT_NOBUSY;

	/* write data to HSCX */
	writel(WRITE_DATA_HSCX | data, adr + 0x200);
	ZORAN_WAIT_NOBUSY;
}

static inline void
read_fifo_isac(unsigned long adr, u_char * data, int size)
{
	register unsigned int portdata;
	register int i;

	ZORAN_WAIT_NOBUSY;
	/* read data from ISAC */
	for (i = 0; i < size; i++) {
		/* set address for ISAC fifo */
		writel(WRITE_ADDR_ISAC | 0x1E, adr + 0x200);
		ZORAN_WAIT_NOBUSY;
		writel(READ_DATA_ISAC, adr + 0x200);
		ZORAN_WAIT_NOBUSY;
		data[i] = (u_char)(portdata & ZORAN_PO_DMASK);
	}
}

static void
write_fifo_isac(unsigned long adr, u_char * data, int size)
{
	register unsigned int portdata;
	register int i;

	ZORAN_WAIT_NOBUSY;
	/* write data to ISAC */
	for (i = 0; i < size; i++) {
		/* set address for ISAC fifo */
		writel(WRITE_ADDR_ISAC | 0x1E, adr + 0x200);
		ZORAN_WAIT_NOBUSY;
		writel(WRITE_DATA_ISAC | data[i], adr + 0x200);
		ZORAN_WAIT_NOBUSY;
	}
}

static inline void
read_fifo_hscx(unsigned long adr, int hscx, u_char * data, int size)
{
	register unsigned int portdata;
	register int i;

	ZORAN_WAIT_NOBUSY;
	/* read data from HSCX */
	for (i = 0; i < size; i++) {
		/* set address for HSCX fifo */
		writel(WRITE_ADDR_HSCX |(hscx ? 0x5F:0x1F), adr + 0x200);
		ZORAN_WAIT_NOBUSY;
		writel(READ_DATA_HSCX, adr + 0x200);
		ZORAN_WAIT_NOBUSY;
		data[i] = (u_char) (portdata & ZORAN_PO_DMASK);
	}
}

static inline void
write_fifo_hscx(unsigned long adr, int hscx, u_char * data, int size)
{
	unsigned int portdata;
	register int i;

	ZORAN_WAIT_NOBUSY;
	/* write data to HSCX */
	for (i = 0; i < size; i++) {
		/* set address for HSCX fifo */
		writel(WRITE_ADDR_HSCX |(hscx ? 0x5F:0x1F), adr + 0x200);
		ZORAN_WAIT_NOBUSY;
		writel(WRITE_DATA_HSCX | data[i], adr + 0x200);
		ZORAN_WAIT_NOBUSY;
		udelay(10);
	}
}

/* Interface functions */

static u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	return (readisac(cs->hw.teles0.membase, offset));
}

static void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	writeisac(cs->hw.teles0.membase, offset, value);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	read_fifo_isac(cs->hw.teles0.membase, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	write_fifo_isac(cs->hw.teles0.membase, data, size);
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = ReadISAC,
	.write_reg  = WriteISAC,
	.read_fifo  = ReadISACfifo,
	.write_fifo = WriteISACfifo,
};

static u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	return (readhscx(cs->hw.teles0.membase, hscx, offset));
}

static void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	writehscx(cs->hw.teles0.membase, hscx, offset, value);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg  = ReadHSCX,
	.write_reg = WriteHSCX,
};

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) readhscx(cs->hw.teles0.membase, nr, reg)
#define WRITEHSCX(cs, nr, reg, data) writehscx(cs->hw.teles0.membase, nr, reg, data)
#define READHSCXFIFO(cs, nr, ptr, cnt) read_fifo_hscx(cs->hw.teles0.membase, nr, ptr, cnt)
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) write_fifo_hscx(cs->hw.teles0.membase, nr, ptr, cnt)

#include "hscx_irq.c"

static void
telespci_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
#define MAXCOUNT 20
	struct IsdnCardState *cs = dev_id;
	u_char val;

	spin_lock(&cs->lock);
	val = readhscx(cs->hw.teles0.membase, 1, HSCX_ISTA);
	if (val)
		hscx_int_main(cs, val);
	val = readisac(cs->hw.teles0.membase, ISAC_ISTA);
	if (val)
		isac_interrupt(cs, val);
	/* Clear interrupt register for Zoran PCI controller */
	writel(0x70000000, cs->hw.teles0.membase + 0x3C);

	writehscx(cs->hw.teles0.membase, 0, HSCX_MASK, 0xFF);
	writehscx(cs->hw.teles0.membase, 1, HSCX_MASK, 0xFF);
	writeisac(cs->hw.teles0.membase, ISAC_MASK, 0xFF);
	writeisac(cs->hw.teles0.membase, ISAC_MASK, 0x0);
	writehscx(cs->hw.teles0.membase, 0, HSCX_MASK, 0x0);
	writehscx(cs->hw.teles0.membase, 1, HSCX_MASK, 0x0);
	spin_unlock(&cs->lock);
}

void
release_io_telespci(struct IsdnCardState *cs)
{
	iounmap((void *)cs->hw.teles0.membase);
}

static int
TelesPCI_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			return(0);
		case CARD_RELEASE:
			release_io_telespci(cs);
			return(0);
		case CARD_INIT:
			inithscxisac(cs);
			return(0);
		case CARD_TEST:
			return(0);
	}
	return(0);
}

static struct pci_dev *dev_tel __initdata = NULL;

int __init
setup_telespci(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

#ifdef __BIG_ENDIAN
#error "not running on big endian machines now"
#endif
	strcpy(tmp, telespci_revision);
	printk(KERN_INFO "HiSax: Teles/PCI driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_TELESPCI)
		return (0);
#if CONFIG_PCI
	if (!pci_present()) {
		printk(KERN_ERR "TelesPCI: no PCI bus present\n");
		return(0);
	}
	if ((dev_tel = pci_find_device (PCI_VENDOR_ID_ZORAN, PCI_DEVICE_ID_ZORAN_36120, dev_tel))) {
		if (pci_enable_device(dev_tel))
			return(0);
		cs->irq = dev_tel->irq;
		if (!cs->irq) {
			printk(KERN_WARNING "Teles: No IRQ for PCI card found\n");
			return(0);
		}
		cs->hw.teles0.membase = (u_long) ioremap(pci_resource_start(dev_tel, 0),
			PAGE_SIZE);
		printk(KERN_INFO "Found: Zoran, base-address: 0x%lx, irq: 0x%x\n",
			pci_resource_start(dev_tel, 0), dev_tel->irq);
	} else {
		printk(KERN_WARNING "TelesPCI: No PCI card found\n");
		return(0);
	}
#else
	printk(KERN_WARNING "HiSax: Teles/PCI and NO_PCI_BIOS\n");
	printk(KERN_WARNING "HiSax: Teles/PCI unable to config\n");
	return (0);
#endif /* CONFIG_PCI */

	/* Initialize Zoran PCI controller */
	writel(0x00000000, cs->hw.teles0.membase + 0x28);
	writel(0x01000000, cs->hw.teles0.membase + 0x28);
	writel(0x01000000, cs->hw.teles0.membase + 0x28);
	writel(0x7BFFFFFF, cs->hw.teles0.membase + 0x2C);
	writel(0x70000000, cs->hw.teles0.membase + 0x3C);
	writel(0x61000000, cs->hw.teles0.membase + 0x40);
	/* writel(0x00800000, cs->hw.teles0.membase + 0x200); */

	printk(KERN_INFO
	       "HiSax: %s config irq:%d mem:%lx\n",
	       CardType[cs->typ], cs->irq,
	       cs->hw.teles0.membase);

	cs->dc_hw_ops = &isac_ops;
	cs->bc_hw_ops = &hscx_ops;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &TelesPCI_card_msg;
	cs->irq_func = &telespci_interrupt;
	cs->irq_flags |= SA_SHIRQ;
	ISACVersion(cs, "TelesPCI:");
	if (HscxVersion(cs, "TelesPCI:")) {
		printk(KERN_WARNING
		 "TelesPCI: wrong HSCX versions check IO/MEM addresses\n");
		release_io_telespci(cs);
		return (0);
	}
	return (1);
}
