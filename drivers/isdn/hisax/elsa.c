/* $Id: elsa.c,v 2.26.6.6 2001/09/23 22:24:47 kai Exp $
 *
 * low level stuff for Elsa isdn cards
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * For changes and modifications please read
 * ../../../Documentation/isdn/HiSax.cert
 *
 * Thanks to    Elsa GmbH for documents and information
 *
 *              Klaus Lichtenwalder (Klaus.Lichtenwalder@WebForum.DE)
 *              for ELSA PCMCIA support
 *
 */

#include <linux/init.h>
#include <linux/config.h>
#include "hisax.h"
#include "arcofi.h"
#include "isac.h"
#include "ipac.h"
#include "hscx.h"
#include "isdnl1.h"
#include <linux/pci.h>
#include <linux/isapnp.h>
#include <linux/serial.h>
#include <linux/serial_reg.h>

extern const char *CardType[];
static spinlock_t elsa_lock = SPIN_LOCK_UNLOCKED;

const char *Elsa_revision = "$Revision: 2.26.6.6 $";
const char *Elsa_Types[] =
{"None", "PC", "PCC-8", "PCC-16", "PCF", "PCF-Pro",
 "PCMCIA", "QS 1000", "QS 3000", "Microlink PCI", "QS 3000 PCI", 
 "PCMCIA-IPAC" };

const char *ITACVer[] =
{"?0?", "?1?", "?2?", "?3?", "?4?", "V2.2",
 "B1", "A1"};

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define ELSA_ISAC	0
#define ELSA_ISAC_PCM	1
#define ELSA_ITAC	1
#define ELSA_HSCX	2
#define ELSA_ALE	3
#define ELSA_ALE_PCM	4
#define ELSA_CONTROL	4
#define ELSA_CONFIG	5
#define ELSA_START_TIMER 6
#define ELSA_TRIG_IRQ	7

#define ELSA_PC      1
#define ELSA_PCC8    2
#define ELSA_PCC16   3
#define ELSA_PCF     4
#define ELSA_PCFPRO  5
#define ELSA_PCMCIA  6
#define ELSA_QS1000  7
#define ELSA_QS3000  8
#define ELSA_QS1000PCI 9
#define ELSA_QS3000PCI 10
#define ELSA_PCMCIA_IPAC 11

/* PCI stuff */
#define ELSA_PCI_IRQ_MASK	0x04

/* ITAC Registeradressen (only Microlink PC) */
#define ITAC_SYS	0x34
#define ITAC_ISEN	0x48
#define ITAC_RFIE	0x4A
#define ITAC_XFIE	0x4C
#define ITAC_SCIE	0x4E
#define ITAC_STIE	0x46

/***                                                                    ***
 ***   Makros als Befehle fuer die Kartenregister                       ***
 ***   (mehrere Befehle werden durch Bit-Oderung kombiniert)            ***
 ***                                                                    ***/

/* Config-Register (Read) */
#define ELSA_TIMER_RUN       0x02	/* Bit 1 des Config-Reg     */
#define ELSA_TIMER_RUN_PCC8  0x01	/* Bit 0 des Config-Reg  bei PCC */
#define ELSA_IRQ_IDX       0x38	/* Bit 3,4,5 des Config-Reg */
#define ELSA_IRQ_IDX_PCC8  0x30	/* Bit 4,5 des Config-Reg */
#define ELSA_IRQ_IDX_PC    0x0c	/* Bit 2,3 des Config-Reg */

/* Control-Register (Write) */
#define ELSA_LINE_LED        0x02	/* Bit 1 Gelbe LED */
#define ELSA_STAT_LED        0x08	/* Bit 3 Gruene LED */
#define ELSA_ISDN_RESET      0x20	/* Bit 5 Reset-Leitung */
#define ELSA_ENA_TIMER_INT   0x80	/* Bit 7 Freigabe Timer Interrupt */

/* ALE-Register (Read) */
#define ELSA_HW_RELEASE      0x07	/* Bit 0-2 Hardwarerkennung */
#define ELSA_S0_POWER_BAD    0x08	/* Bit 3 S0-Bus Spannung fehlt */

/* Status Flags */
#define ELSA_TIMER_AKTIV 1
#define ELSA_BAD_PWR     2
#define ELSA_ASSIGN      4

#define RS_ISR_PASS_LIMIT 256
#define _INLINE_ inline
#define FLG_MODEM_ACTIVE 1
/* IPAC AUX */
#define ELSA_IPAC_LINE_LED	0x40	/* Bit 6 Gelbe LED */
#define ELSA_IPAC_STAT_LED	0x80	/* Bit 7 Gruene LED */

#if ARCOFI_USE
static struct arcofi_msg ARCOFI_XOP_F =
	{NULL,0,2,{0xa1,0x3f,0,0,0,0,0,0,0,0}}; /* Normal OP */
static struct arcofi_msg ARCOFI_XOP_1 =
	{&ARCOFI_XOP_F,0,2,{0xa1,0x31,0,0,0,0,0,0,0,0}}; /* PWR UP */
static struct arcofi_msg ARCOFI_SOP_F = 
	{&ARCOFI_XOP_1,0,10,{0xa1,0x1f,0x00,0x50,0x10,0x00,0x00,0x80,0x02,0x12}};
static struct arcofi_msg ARCOFI_COP_9 =
	{&ARCOFI_SOP_F,0,10,{0xa1,0x29,0x80,0xcb,0xe9,0x88,0x00,0xc8,0xd8,0x80}}; /* RX */
static struct arcofi_msg ARCOFI_COP_8 =
	{&ARCOFI_COP_9,0,10,{0xa1,0x28,0x49,0x31,0x8,0x13,0x6e,0x88,0x2a,0x61}}; /* TX */
static struct arcofi_msg ARCOFI_COP_7 =
	{&ARCOFI_COP_8,0,4,{0xa1,0x27,0x80,0x80,0,0,0,0,0,0}}; /* GZ */
static struct arcofi_msg ARCOFI_COP_6 =
	{&ARCOFI_COP_7,0,6,{0xa1,0x26,0,0,0x82,0x7c,0,0,0,0}}; /* GRL GRH */
static struct arcofi_msg ARCOFI_COP_5 =
	{&ARCOFI_COP_6,0,4,{0xa1,0x25,0xbb,0x4a,0,0,0,0,0,0}}; /* GTX */
static struct arcofi_msg ARCOFI_VERSION =
	{NULL,1,2,{0xa0,0,0,0,0,0,0,0,0,0}};
static struct arcofi_msg ARCOFI_XOP_0 =
	{NULL,0,2,{0xa1,0x30,0,0,0,0,0,0,0,0}}; /* PWR Down */

static void set_arcofi(struct IsdnCardState *cs, int bc);

#include "elsa_ser.c"
#endif /* ARCOFI_USE */

static inline u8
readreg(struct IsdnCardState *cs, unsigned int adr, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&elsa_lock, flags);
	byteout(cs->hw.elsa.ale, off);
	ret = bytein(adr);
	spin_unlock_irqrestore(&elsa_lock, flags);
	return ret;
}

static inline void
writereg(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&elsa_lock, flags);
	byteout(cs->hw.elsa.ale, off);
	byteout(adr, data);
	spin_unlock_irqrestore(&elsa_lock, flags);
}

static inline void
readfifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	unsigned long flags;

	spin_lock_irqsave(&elsa_lock, flags);
	byteout(cs->hw.elsa.ale, off);
	insb(adr, data, size);
	spin_unlock_irqrestore(&elsa_lock, flags);
}

static inline void
writefifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	unsigned long flags;

	spin_lock_irqsave(&elsa_lock, flags);
	byteout(cs->hw.elsa.ale, off);
	outsb(adr, data, size);
	spin_unlock_irqrestore(&elsa_lock, flags);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs, cs->hw.elsa.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs, cs->hw.elsa.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	readfifo(cs, cs->hw.elsa.isac, 0, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	writefifo(cs, cs->hw.elsa.isac, 0, data, size);
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = isac_read,
	.write_reg  = isac_write,
	.read_fifo  = isac_read_fifo,
	.write_fifo = isac_write_fifo,
};

static u8
ipac_dc_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs, cs->hw.elsa.isac, offset+0x80);
}

static void
ipac_dc_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs, cs->hw.elsa.isac, offset|0x80, value);
}

static void
ipac_dc_read_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	readfifo(cs, cs->hw.elsa.isac, 0x80, data, size);
}

static void
ipac_dc_write_fifo(struct IsdnCardState *cs, u8 *data, int size)
{
	writefifo(cs, cs->hw.elsa.isac, 0x80, data, size);
}

static struct dc_hw_ops ipac_dc_ops = {
	.read_reg   = ipac_dc_read,
	.write_reg  = ipac_dc_write,
	.read_fifo  = ipac_dc_read_fifo,
	.write_fifo = ipac_dc_write_fifo,
};

static u8
hscx_read(struct IsdnCardState *cs, int hscx, u8 offset)
{
	return readreg(cs, cs->hw.elsa.hscx, offset + (hscx ? 0x40 : 0));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs, cs->hw.elsa.hscx, offset + (hscx ? 0x40 : 0), value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	writefifo(cs, cs->hw.elsa.hscx, hscx ? 0x40 : 0, data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	writefifo(cs, cs->hw.elsa.hscx, hscx ? 0x40 : 0, data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static inline u8
readitac(struct IsdnCardState *cs, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&elsa_lock, flags);
	byteout(cs->hw.elsa.ale, off);
	ret = bytein(cs->hw.elsa.itac);
	spin_unlock_irqrestore(&elsa_lock, flags);
	return ret;
}

static inline void
writeitac(struct IsdnCardState *cs, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&elsa_lock, flags);
	byteout(cs->hw.elsa.ale, off);
	byteout(cs->hw.elsa.itac, data);
	spin_unlock_irqrestore(&elsa_lock, flags);
}

static inline int
TimerRun(struct IsdnCardState *cs)
{
	register u8 v;

	v = bytein(cs->hw.elsa.cfg);
	if ((cs->subtyp == ELSA_QS1000) || (cs->subtyp == ELSA_QS3000))
		return (0 == (v & ELSA_TIMER_RUN));
	else if (cs->subtyp == ELSA_PCC8)
		return (v & ELSA_TIMER_RUN_PCC8);
	return (v & ELSA_TIMER_RUN);
}

static void
elsa_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 val;
	int icnt=5;

	spin_lock(&cs->lock);
	if ((cs->typ == ISDN_CTYPE_ELSA_PCMCIA) && (*cs->busy_flag == 1)) {
	/* The card tends to generate interrupts while being removed
	   causing us to just crash the kernel. bad. */
		printk(KERN_WARNING "Elsa: card not available!\n");
		goto unlock;
	}
#if ARCOFI_USE
	if (cs->hw.elsa.MFlag) {
		val = serial_inp(cs, UART_IIR);
		if (!(val & UART_IIR_NO_INT)) {
			debugl1(cs,"IIR %02x", val);
			rs_interrupt_elsa(intno, cs);
		}
	}
#endif
	val = hscx_read(cs, 1, HSCX_ISTA);
      Start_HSCX:
	if (val) {
		hscx_int_main(cs, val);
	}
	val = isac_read(cs, ISAC_ISTA);
      Start_ISAC:
	if (val) {
		isac_interrupt(cs, val);
	}
	val = hscx_read(cs, 1, HSCX_ISTA);
	if (val && icnt) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "HSCX IntStat after IntRoutine");
		icnt--;
		goto Start_HSCX;
	}
	val = isac_read(cs, ISAC_ISTA);
	if (val && icnt) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ISAC IntStat after IntRoutine");
		icnt--;
		goto Start_ISAC;
	}
	if (!icnt)
		printk(KERN_WARNING"ELSA IRQ LOOP\n");
	hscx_write(cs, 0, HSCX_MASK, 0xFF);
	hscx_write(cs, 1, HSCX_MASK, 0xFF);
	isac_write(cs, ISAC_MASK, 0xFF);
	if (cs->hw.elsa.status & ELSA_TIMER_AKTIV) {
		if (!TimerRun(cs)) {
			/* Timer Restart */
			byteout(cs->hw.elsa.timer, 0);
			cs->hw.elsa.counter++;
		}
	}
#if ARCOFI_USE
	if (cs->hw.elsa.MFlag) {
		val = serial_inp(cs, UART_MCR);
		val ^= 0x8;
		serial_outp(cs, UART_MCR, val);
		val = serial_inp(cs, UART_MCR);
		val ^= 0x8;
		serial_outp(cs, UART_MCR, val);
	}
#endif
	if (cs->hw.elsa.trig)
		byteout(cs->hw.elsa.trig, 0x00);
	hscx_write(cs, 0, HSCX_MASK, 0x00);
	hscx_write(cs, 1, HSCX_MASK, 0x00);
	isac_write(cs, ISAC_MASK, 0x00);
 unlock:
	spin_unlock(&cs->lock);
}

static void
elsa_interrupt_ipac(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u8 ista,val;
	int icnt=5;

	if (!cs) {
		printk(KERN_WARNING "Elsa: Spurious interrupt!\n");
		return;
	}
	if (cs->subtyp == ELSA_QS1000PCI || cs->subtyp == ELSA_QS3000PCI) {
		val = bytein(cs->hw.elsa.cfg + 0x4c); /* PCI IRQ */
		if (!test_bit(FLG_BUGGY_PLX9050, &cs->HW_Flags) && 
		    !(val & ELSA_PCI_IRQ_MASK))
			return;
	}
#if ARCOFI_USE
	if (cs->hw.elsa.MFlag) {
		val = serial_inp(cs, UART_IIR);
		if (!(val & UART_IIR_NO_INT)) {
			debugl1(cs,"IIR %02x", val);
			rs_interrupt_elsa(intno, cs);
		}
	}
#endif
	ista = readreg(cs, cs->hw.elsa.isac, IPAC_ISTA);
Start_IPAC:
	if (cs->debug & L1_DEB_IPAC)
		debugl1(cs, "IPAC ISTA %02X", ista);
	if (ista & 0x0f) {
		val = hscx_read(cs, 1, HSCX_ISTA);
		if (ista & 0x01)
			val |= 0x01;
		if (ista & 0x04)
			val |= 0x02;
		if (ista & 0x08)
			val |= 0x04;
		if (val)
			hscx_int_main(cs, val);
	}
	if (ista & 0x20) {
		val = ipac_dc_read(cs, ISAC_ISTA) & 0xfe;
		if (val) {
			isac_interrupt(cs, val);
		}
	}
	if (ista & 0x10) {
		val = 0x01;
		isac_interrupt(cs, val);
	}
	ista  = readreg(cs, cs->hw.elsa.isac, IPAC_ISTA);
	if ((ista & 0x3f) && icnt) {
		icnt--;
		goto Start_IPAC;
	}
	if (!icnt)
		printk(KERN_WARNING "ELSA IRQ LOOP\n");
	writereg(cs, cs->hw.elsa.isac, IPAC_MASK, 0xFF);
	writereg(cs, cs->hw.elsa.isac, IPAC_MASK, 0xC0);
}

void
release_io_elsa(struct IsdnCardState *cs)
{
	int bytecnt = 8;

	del_timer(&cs->hw.elsa.tl);
#if ARCOFI_USE
	clear_arcofi(cs);
#endif
	if (cs->hw.elsa.ctrl)
		byteout(cs->hw.elsa.ctrl, 0);	/* LEDs Out */
	if (cs->subtyp == ELSA_QS1000PCI) {
		byteout(cs->hw.elsa.cfg + 0x4c, 0x01);  /* disable IRQ */
		writereg(cs, cs->hw.elsa.isac, IPAC_ATX, 0xff);
		bytecnt = 2;
		release_region(cs->hw.elsa.cfg, 0x80);
	}
	if (cs->subtyp == ELSA_QS3000PCI) {
		byteout(cs->hw.elsa.cfg + 0x4c, 0x03); /* disable ELSA PCI IRQ */
		writereg(cs, cs->hw.elsa.isac, IPAC_ATX, 0xff);
		release_region(cs->hw.elsa.cfg, 0x80);
	}
 	if (cs->subtyp == ELSA_PCMCIA_IPAC) {
		writereg(cs, cs->hw.elsa.isac, IPAC_ATX, 0xff);
 	}
	if ((cs->subtyp == ELSA_PCFPRO) ||
		(cs->subtyp == ELSA_QS3000) ||
		(cs->subtyp == ELSA_PCF) ||
		(cs->subtyp == ELSA_QS3000PCI)) {
		bytecnt = 16;
#if ARCOFI_USE
		release_modem(cs);
#endif
	}
	if (cs->hw.elsa.base)
		release_region(cs->hw.elsa.base, bytecnt);
}

static void
reset_elsa(struct IsdnCardState *cs)
{
	if (cs->hw.elsa.timer) {
		/* Wait 1 Timer */
		byteout(cs->hw.elsa.timer, 0);
		while (TimerRun(cs));
		cs->hw.elsa.ctrl_reg |= 0x50;
		cs->hw.elsa.ctrl_reg &= ~ELSA_ISDN_RESET;	/* Reset On */
		byteout(cs->hw.elsa.ctrl, cs->hw.elsa.ctrl_reg);
		/* Wait 1 Timer */
		byteout(cs->hw.elsa.timer, 0);
		while (TimerRun(cs));
		cs->hw.elsa.ctrl_reg |= ELSA_ISDN_RESET;	/* Reset Off */
		byteout(cs->hw.elsa.ctrl, cs->hw.elsa.ctrl_reg);
		/* Wait 1 Timer */
		byteout(cs->hw.elsa.timer, 0);
		while (TimerRun(cs));
		if (cs->hw.elsa.trig)
			byteout(cs->hw.elsa.trig, 0xff);
	}
	if ((cs->subtyp == ELSA_QS1000PCI) || (cs->subtyp == ELSA_QS3000PCI) || (cs->subtyp == ELSA_PCMCIA_IPAC)) {
		writereg(cs, cs->hw.elsa.isac, IPAC_POTA2, 0x20);
		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_timeout((10*HZ)/1000); /* Timeout 10ms */
		writereg(cs, cs->hw.elsa.isac, IPAC_POTA2, 0x00);
		set_current_state(TASK_UNINTERRUPTIBLE);
		writereg(cs, cs->hw.elsa.isac, IPAC_MASK, 0xc0);
		schedule_timeout((10*HZ)/1000); /* Timeout 10ms */
		if (cs->subtyp != ELSA_PCMCIA_IPAC) {
			writereg(cs, cs->hw.elsa.isac, IPAC_ACFG, 0x0);
			writereg(cs, cs->hw.elsa.isac, IPAC_AOE, 0x3c);
		} else {
			writereg(cs, cs->hw.elsa.isac, IPAC_PCFG, 0x10);
			writereg(cs, cs->hw.elsa.isac, IPAC_ACFG, 0x4);
			writereg(cs, cs->hw.elsa.isac, IPAC_AOE, 0xf8);
		}
		writereg(cs, cs->hw.elsa.isac, IPAC_ATX, 0xff);
		if (cs->subtyp == ELSA_QS1000PCI)
			byteout(cs->hw.elsa.cfg + 0x4c, 0x41); /* enable ELSA PCI IRQ */
		else if (cs->subtyp == ELSA_QS3000PCI)
			byteout(cs->hw.elsa.cfg + 0x4c, 0x43); /* enable ELSA PCI IRQ */
	}
}

#if ARCOFI_USE

static void
set_arcofi(struct IsdnCardState *cs, int bc) {
	cs->dc.isac.arcofi_bc = bc;
	arcofi_fsm(cs, ARCOFI_START, &ARCOFI_COP_5);
	interruptible_sleep_on(&cs->dc.isac.arcofi_wait);
}

static int
check_arcofi(struct IsdnCardState *cs)
{
	int arcofi_present = 0;
	char tmp[40];
	char *t;
	u8 *p;

	if (!cs->dc.isac.mon_tx)
		if (!(cs->dc.isac.mon_tx=kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "ISAC MON TX out of buffers!");
			return(0);
		}
	cs->dc.isac.arcofi_bc = 0;
	arcofi_fsm(cs, ARCOFI_START, &ARCOFI_VERSION);
	interruptible_sleep_on(&cs->dc.isac.arcofi_wait);
	if (!test_and_clear_bit(FLG_ARCOFI_ERROR, &cs->HW_Flags)) {
			debugl1(cs, "Arcofi response received %d bytes", cs->dc.isac.mon_rxp);
			p = cs->dc.isac.mon_rx;
			t = tmp;
			t += sprintf(tmp, "Arcofi data");
			QuickHex(t, p, cs->dc.isac.mon_rxp);
			debugl1(cs, tmp);
			if ((cs->dc.isac.mon_rxp == 2) && (cs->dc.isac.mon_rx[0] == 0xa0)) {
				switch(cs->dc.isac.mon_rx[1]) {
					case 0x80:
						debugl1(cs, "Arcofi 2160 detected");
						arcofi_present = 1;
						break;
					case 0x82:
						debugl1(cs, "Arcofi 2165 detected");
						arcofi_present = 2;
						break;
					case 0x84:
						debugl1(cs, "Arcofi 2163 detected");
						arcofi_present = 3;
						break;
					default:
						debugl1(cs, "unknown Arcofi response");
						break;
				}
			} else
				debugl1(cs, "undefined Monitor response");
			cs->dc.isac.mon_rxp = 0;
	} else if (cs->dc.isac.mon_tx) {
		debugl1(cs, "Arcofi not detected");
	}
	if (arcofi_present) {
		if (cs->subtyp==ELSA_QS1000) {
			cs->subtyp = ELSA_QS3000;
			printk(KERN_INFO
				"Elsa: %s detected modem at 0x%lx\n",
				Elsa_Types[cs->subtyp],
				cs->hw.elsa.base+8);
			release_region(cs->hw.elsa.base, 8);
			if (!request_region(cs->hw.elsa.base, 16,"elsa isdn modem")) {
				printk(KERN_WARNING
				"HiSax: %s config port %lx-%lx already in use\n",
				Elsa_Types[cs->subtyp],
				cs->hw.elsa.base + 8,
				cs->hw.elsa.base + 16);
			}
		} else if (cs->subtyp==ELSA_PCC16) {
			cs->subtyp = ELSA_PCF;
			printk(KERN_INFO
				"Elsa: %s detected modem at 0x%lx\n",
				Elsa_Types[cs->subtyp],
				cs->hw.elsa.base+8);
			release_region(cs->hw.elsa.base, 8);
			if (!request_region(cs->hw.elsa.base, 16,"elsa isdn modem")) {
				printk(KERN_WARNING
				"HiSax: %s config port %lx-%lx already in use\n",
				Elsa_Types[cs->subtyp],
				cs->hw.elsa.base + 8,
				cs->hw.elsa.base + 16);
			}
		} else
			printk(KERN_INFO
				"Elsa: %s detected modem at 0x%lx\n",
				Elsa_Types[cs->subtyp],
				cs->hw.elsa.base+8);
		arcofi_fsm(cs, ARCOFI_START, &ARCOFI_XOP_0);
		interruptible_sleep_on(&cs->dc.isac.arcofi_wait);
		return(1);
	}
	return(0);
}
#endif /* ARCOFI_USE */

static void
elsa_led_handler(struct IsdnCardState *cs)
{
	int blink = 0;

	if (cs->subtyp == ELSA_PCMCIA || cs->subtyp == ELSA_PCMCIA_IPAC)
		return;
	del_timer(&cs->hw.elsa.tl);
	if (cs->hw.elsa.status & ELSA_ASSIGN)
		cs->hw.elsa.ctrl_reg |= ELSA_STAT_LED;
	else if (cs->hw.elsa.status & ELSA_BAD_PWR)
		cs->hw.elsa.ctrl_reg &= ~ELSA_STAT_LED;
	else {
		cs->hw.elsa.ctrl_reg ^= ELSA_STAT_LED;
		blink = 250;
	}
	if (cs->hw.elsa.status & 0xf000)
		cs->hw.elsa.ctrl_reg |= ELSA_LINE_LED;
	else if (cs->hw.elsa.status & 0x0f00) {
		cs->hw.elsa.ctrl_reg ^= ELSA_LINE_LED;
		blink = 500;
	} else
		cs->hw.elsa.ctrl_reg &= ~ELSA_LINE_LED;

	if ((cs->subtyp == ELSA_QS1000PCI) ||
		(cs->subtyp == ELSA_QS3000PCI)) {
		u8 led = 0xff;
		if (cs->hw.elsa.ctrl_reg & ELSA_LINE_LED)
			led ^= ELSA_IPAC_LINE_LED;
		if (cs->hw.elsa.ctrl_reg & ELSA_STAT_LED)
			led ^= ELSA_IPAC_STAT_LED;
		writereg(cs, cs->hw.elsa.isac, IPAC_ATX, led);
	} else
		byteout(cs->hw.elsa.ctrl, cs->hw.elsa.ctrl_reg);
	if (blink) {
		init_timer(&cs->hw.elsa.tl);
		cs->hw.elsa.tl.expires = jiffies + ((blink * HZ) / 1000);
		add_timer(&cs->hw.elsa.tl);
	}
}

static int
Elsa_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	int ret = 0;

	switch (mt) {
		case CARD_RESET:
			reset_elsa(cs);
			return(0);
		case CARD_RELEASE:
			release_io_elsa(cs);
			return(0);
		case CARD_INIT:
			cs->debug |= L1_DEB_IPAC;
			if ((cs->subtyp == ELSA_QS1000) ||
			    (cs->subtyp == ELSA_QS3000))
			{
				byteout(cs->hw.elsa.timer, 0);
			}
			if (cs->hw.elsa.trig)
				byteout(cs->hw.elsa.trig, 0xff);
			inithscxisac(cs);
			return(0);
		case CARD_TEST:
			if ((cs->subtyp == ELSA_PCMCIA) ||
				(cs->subtyp == ELSA_PCMCIA_IPAC) ||
				(cs->subtyp == ELSA_QS1000PCI)) {
				return(0);
			} else if (cs->subtyp == ELSA_QS3000PCI) {
				ret = 0;
			} else {
				cs->hw.elsa.counter = 0;
				cs->hw.elsa.ctrl_reg |= ELSA_ENA_TIMER_INT;
				cs->hw.elsa.status |= ELSA_TIMER_AKTIV;
				byteout(cs->hw.elsa.ctrl, cs->hw.elsa.ctrl_reg);
				byteout(cs->hw.elsa.timer, 0);
				set_current_state(TASK_UNINTERRUPTIBLE);
				schedule_timeout((110*HZ)/1000);
				cs->hw.elsa.ctrl_reg &= ~ELSA_ENA_TIMER_INT;
				byteout(cs->hw.elsa.ctrl, cs->hw.elsa.ctrl_reg);
				cs->hw.elsa.status &= ~ELSA_TIMER_AKTIV;
				printk(KERN_INFO "Elsa: %d timer tics in 110 msek\n",
				       cs->hw.elsa.counter);
				if ((cs->hw.elsa.counter > 10) &&
					(cs->hw.elsa.counter < 16)) {
					printk(KERN_INFO "Elsa: timer and irq OK\n");
					ret = 0;
				} else {
					printk(KERN_WARNING
					       "Elsa: timer tic problem (%d/12) maybe an IRQ(%d) conflict\n",
					       cs->hw.elsa.counter, cs->irq);
					ret = 1;
				}
			}
#if ARCOFI_USE
			if (check_arcofi(cs)) {
				init_modem(cs);
			}
#endif
			elsa_led_handler(cs);
			return(ret);
		case (MDL_REMOVE | REQUEST):
			cs->hw.elsa.status &= 0;
			break;
		case (MDL_ASSIGN | REQUEST):
			cs->hw.elsa.status |= ELSA_ASSIGN;
			break;
		case MDL_INFO_SETUP:
			if ((long) arg)
				cs->hw.elsa.status |= 0x0200;
			else
				cs->hw.elsa.status |= 0x0100;
			break;
		case MDL_INFO_CONN:
			if ((long) arg)
				cs->hw.elsa.status |= 0x2000;
			else
				cs->hw.elsa.status |= 0x1000;
			break;
		case MDL_INFO_REL:
			if ((long) arg) {
				cs->hw.elsa.status &= ~0x2000;
				cs->hw.elsa.status &= ~0x0200;
			} else {
				cs->hw.elsa.status &= ~0x1000;
				cs->hw.elsa.status &= ~0x0100;
			}
			break;
#if ARCOFI_USE
		case CARD_AUX_IND:
			if (cs->hw.elsa.MFlag) {
				int len;
				u8 *msg;

				if (!arg)
					return(0);
				msg = arg;
				len = *msg;
				msg++;
				modem_write_cmd(cs, msg, len);
			}
			break;
#endif
	}
	if (cs->typ == ISDN_CTYPE_ELSA) {
		int pwr = bytein(cs->hw.elsa.ale);
		if (pwr & 0x08)
			cs->hw.elsa.status |= ELSA_BAD_PWR;
		else
			cs->hw.elsa.status &= ~ELSA_BAD_PWR;
	}
	elsa_led_handler(cs);
	return(ret);
}

static unsigned char
probe_elsa_adr(unsigned int adr, int typ)
{
	int i, in1, in2, p16_1 = 0, p16_2 = 0, p8_1 = 0, p8_2 = 0, pc_1 = 0,
	 pc_2 = 0, pfp_1 = 0, pfp_2 = 0;

	/* In case of the elsa pcmcia card, this region is in use,
	   reserved for us by the card manager. So we do not check it
	   here, it would fail. */
	if (typ != ISDN_CTYPE_ELSA_PCMCIA && check_region(adr, 8)) {
		printk(KERN_WARNING
		       "Elsa: Probing Port 0x%x: already in use\n",
		       adr);
		return (0);
	}
	for (i = 0; i < 16; i++) {
		in1 = inb(adr + ELSA_CONFIG);	/* 'toggelt' bei */
		in2 = inb(adr + ELSA_CONFIG);	/* jedem Zugriff */
		p16_1 += 0x04 & in1;
		p16_2 += 0x04 & in2;
		p8_1 += 0x02 & in1;
		p8_2 += 0x02 & in2;
		pc_1 += 0x01 & in1;
		pc_2 += 0x01 & in2;
		pfp_1 += 0x40 & in1;
		pfp_2 += 0x40 & in2;
	}
	printk(KERN_INFO "Elsa: Probing IO 0x%x", adr);
	if (65 == ++p16_1 * ++p16_2) {
		printk(" PCC-16/PCF found\n");
		return (ELSA_PCC16);
	} else if (1025 == ++pfp_1 * ++pfp_2) {
		printk(" PCF-Pro found\n");
		return (ELSA_PCFPRO);
	} else if (33 == ++p8_1 * ++p8_2) {
		printk(" PCC8 found\n");
		return (ELSA_PCC8);
	} else if (17 == ++pc_1 * ++pc_2) {
		printk(" PC found\n");
		return (ELSA_PC);
	} else {
		printk(" failed\n");
		return (0);
	}
}

static unsigned int
probe_elsa(struct IsdnCardState *cs)
{
	int i;
	unsigned int CARD_portlist[] =
	{0x160, 0x170, 0x260, 0x360, 0};

	for (i = 0; CARD_portlist[i]; i++) {
		if ((cs->subtyp = probe_elsa_adr(CARD_portlist[i], cs->typ)))
			break;
	}
	return (CARD_portlist[i]);
}

static 	struct pci_dev *dev_qs1000 __devinitdata = NULL;
static 	struct pci_dev *dev_qs3000 __devinitdata = NULL;

#ifdef __ISAPNP__
static struct isapnp_device_id elsa_ids[] __initdata = {
	{ ISAPNP_VENDOR('E', 'L', 'S'), ISAPNP_FUNCTION(0x0133),
	  ISAPNP_VENDOR('E', 'L', 'S'), ISAPNP_FUNCTION(0x0133), 
	  (unsigned long) "Elsa QS1000" },
	{ ISAPNP_VENDOR('E', 'L', 'S'), ISAPNP_FUNCTION(0x0134),
	  ISAPNP_VENDOR('E', 'L', 'S'), ISAPNP_FUNCTION(0x0134), 
	  (unsigned long) "Elsa QS3000" },
	{ 0, }
};

static struct isapnp_device_id *pdev = &elsa_ids[0];
static struct pci_bus *pnp_c __devinitdata = NULL;
#endif

int __devinit
setup_elsa(struct IsdnCard *card)
{
	int bytecnt;
	u8 val, pci_rev;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, Elsa_revision);
	printk(KERN_INFO "HiSax: Elsa driver Rev. %s\n", HiSax_getrev(tmp));
	cs->hw.elsa.ctrl_reg = 0;
	cs->hw.elsa.status = 0;
	cs->hw.elsa.MFlag = 0;
	cs->subtyp = 0;
	if (cs->typ == ISDN_CTYPE_ELSA) {
		cs->hw.elsa.base = card->para[0];
		printk(KERN_INFO "Elsa: Microlink IO probing\n");
		if (cs->hw.elsa.base) {
			if (!(cs->subtyp = probe_elsa_adr(cs->hw.elsa.base,
							  cs->typ))) {
				printk(KERN_WARNING
				       "Elsa: no Elsa Microlink at %#lx\n",
				       cs->hw.elsa.base);
				return (0);
			}
		} else
			cs->hw.elsa.base = probe_elsa(cs);
		if (cs->hw.elsa.base) {
			cs->hw.elsa.cfg = cs->hw.elsa.base + ELSA_CONFIG;
			cs->hw.elsa.ctrl = cs->hw.elsa.base + ELSA_CONTROL;
			cs->hw.elsa.ale = cs->hw.elsa.base + ELSA_ALE;
			cs->hw.elsa.isac = cs->hw.elsa.base + ELSA_ISAC;
			cs->hw.elsa.itac = cs->hw.elsa.base + ELSA_ITAC;
			cs->hw.elsa.hscx = cs->hw.elsa.base + ELSA_HSCX;
			cs->hw.elsa.trig = cs->hw.elsa.base + ELSA_TRIG_IRQ;
			cs->hw.elsa.timer = cs->hw.elsa.base + ELSA_START_TIMER;
			val = bytein(cs->hw.elsa.cfg);
			if (cs->subtyp == ELSA_PC) {
				const u8 CARD_IrqTab[8] =
				{7, 3, 5, 9, 0, 0, 0, 0};
				cs->irq = CARD_IrqTab[(val & ELSA_IRQ_IDX_PC) >> 2];
			} else if (cs->subtyp == ELSA_PCC8) {
				const u8 CARD_IrqTab[8] =
				{7, 3, 5, 9, 0, 0, 0, 0};
				cs->irq = CARD_IrqTab[(val & ELSA_IRQ_IDX_PCC8) >> 4];
			} else {
				const u8 CARD_IrqTab[8] =
				{15, 10, 15, 3, 11, 5, 11, 9};
				cs->irq = CARD_IrqTab[(val & ELSA_IRQ_IDX) >> 3];
			}
			val = bytein(cs->hw.elsa.ale) & ELSA_HW_RELEASE;
			if (val < 3)
				val |= 8;
			val += 'A' - 3;
			if (val == 'B' || val == 'C')
				val ^= 1;
			if ((cs->subtyp == ELSA_PCFPRO) && (val = 'G'))
				val = 'C';
			printk(KERN_INFO
			       "Elsa: %s found at %#lx Rev.:%c IRQ %d\n",
			       Elsa_Types[cs->subtyp],
			       cs->hw.elsa.base,
			       val, cs->irq);
			val = bytein(cs->hw.elsa.ale) & ELSA_S0_POWER_BAD;
			if (val) {
				printk(KERN_WARNING
				   "Elsa: Microlink S0 bus power bad\n");
				cs->hw.elsa.status |= ELSA_BAD_PWR;
			}
		} else {
			printk(KERN_WARNING
			       "No Elsa Microlink found\n");
			return (0);
		}
	} else if (cs->typ == ISDN_CTYPE_ELSA_PNP) {
#ifdef __ISAPNP__
		if (!card->para[1] && isapnp_present()) {
			struct pci_bus *pb;
			struct pci_dev *pd;

			while(pdev->card_vendor) {
				if ((pb = isapnp_find_card(pdev->card_vendor,
					pdev->card_device, pnp_c))) {
					pnp_c = pb;
					pd = NULL;
					if ((pd = isapnp_find_dev(pnp_c,
						pdev->vendor, pdev->function, pd))) {
						printk(KERN_INFO "HiSax: %s detected\n",
							(char *)pdev->driver_data);
						pd->prepare(pd);
						pd->deactivate(pd);
						pd->activate(pd);
						card->para[1] =
							pd->resource[0].start;
						card->para[0] =
							pd->irq_resource[0].start;
						if (!card->para[0] || !card->para[1]) {
							printk(KERN_ERR "Elsa PnP:some resources are missing %ld/%lx\n",
								card->para[0], card->para[1]);
							pd->deactivate(pd);
							return(0);
						}
						if (pdev->function == ISAPNP_FUNCTION(0x133))
							cs->subtyp = ELSA_QS1000;
						else
							cs->subtyp = ELSA_QS3000;
						break;
					} else {
						printk(KERN_ERR "Elsa PnP: PnP error card found, no device\n");
						return(0);
					}
				}
				pdev++;
				pnp_c=NULL;
			} 
			if (!pdev->card_vendor) {
				printk(KERN_INFO "Elsa PnP: no ISAPnP card found\n");
				return(0);
			}
		}
#endif
		if (card->para[1] && card->para[0]) { 
			cs->hw.elsa.base = card->para[1];
			cs->irq = card->para[0];
			if (!cs->subtyp)
				cs->subtyp = ELSA_QS1000;
		} else {
			printk(KERN_ERR "Elsa PnP: no parameter\n");
		}
		cs->hw.elsa.cfg = cs->hw.elsa.base + ELSA_CONFIG;
		cs->hw.elsa.ale = cs->hw.elsa.base + ELSA_ALE;
		cs->hw.elsa.isac = cs->hw.elsa.base + ELSA_ISAC;
		cs->hw.elsa.hscx = cs->hw.elsa.base + ELSA_HSCX;
		cs->hw.elsa.trig = cs->hw.elsa.base + ELSA_TRIG_IRQ;
		cs->hw.elsa.timer = cs->hw.elsa.base + ELSA_START_TIMER;
		cs->hw.elsa.ctrl = cs->hw.elsa.base + ELSA_CONTROL;
		printk(KERN_INFO
		       "Elsa: %s defined at %#lx IRQ %d\n",
		       Elsa_Types[cs->subtyp],
		       cs->hw.elsa.base,
		       cs->irq);
	} else if (cs->typ == ISDN_CTYPE_ELSA_PCMCIA) {
		cs->hw.elsa.base = card->para[1];
		cs->irq = card->para[0];
		cs->hw.elsa.ale = cs->hw.elsa.base + 0;
		val = readreg(cs, cs->hw.elsa.base + 2, IPAC_ID);
		if ((val == 1) || (val == 2)) { /* IPAC version 1.1/1.2 */
			cs->subtyp = ELSA_PCMCIA_IPAC;
			cs->hw.elsa.isac = cs->hw.elsa.base + 2;
			cs->hw.elsa.hscx = cs->hw.elsa.base + 2;
			test_and_set_bit(HW_IPAC, &cs->HW_Flags);
		} else {
			cs->subtyp = ELSA_PCMCIA;
			cs->hw.elsa.ale = cs->hw.elsa.base + ELSA_ALE_PCM;
			cs->hw.elsa.isac = cs->hw.elsa.base + ELSA_ISAC_PCM;
			cs->hw.elsa.hscx = cs->hw.elsa.base + ELSA_HSCX;
		}
		cs->hw.elsa.timer = 0;
		cs->hw.elsa.trig = 0;
		cs->hw.elsa.ctrl = 0;
		printk(KERN_INFO
		       "Elsa: %s defined at %#lx IRQ %d\n",
		       Elsa_Types[cs->subtyp],
		       cs->hw.elsa.base,
		       cs->irq);
	} else if (cs->typ == ISDN_CTYPE_ELSA_PCI) {
#if CONFIG_PCI
		if (!pci_present()) {
			printk(KERN_ERR "Elsa: no PCI bus present\n");
			return(0);
		}
		cs->subtyp = 0;
		if ((dev_qs1000 = pci_find_device(PCI_VENDOR_ID_ELSA,
			PCI_DEVICE_ID_ELSA_MICROLINK, dev_qs1000))) {
			if (pci_enable_device(dev_qs1000))
				return(0);
			cs->subtyp = ELSA_QS1000PCI;
			cs->irq = dev_qs1000->irq;
			cs->hw.elsa.cfg = pci_resource_start(dev_qs1000, 1);
			cs->hw.elsa.base = pci_resource_start(dev_qs1000, 3);
			pci_read_config_byte(dev_qs1000, PCI_REVISION_ID, &pci_rev);
		} else if ((dev_qs3000 = pci_find_device(PCI_VENDOR_ID_ELSA,
			PCI_DEVICE_ID_ELSA_QS3000, dev_qs3000))) {
			if (pci_enable_device(dev_qs3000))
				return(0);
			cs->subtyp = ELSA_QS3000PCI;
			cs->irq = dev_qs3000->irq;
			cs->hw.elsa.cfg = pci_resource_start(dev_qs3000, 1);
			cs->hw.elsa.base = pci_resource_start(dev_qs3000, 3);
			pci_read_config_byte(dev_qs1000, PCI_REVISION_ID, &pci_rev);
		} else {
			printk(KERN_WARNING "Elsa: No PCI card found\n");
			return(0);
		}
		if (!cs->irq) {
			printk(KERN_WARNING "Elsa: No IRQ for PCI card found\n");
			return(0);
		}

		if (!(cs->hw.elsa.base && cs->hw.elsa.cfg)) {
			printk(KERN_WARNING "Elsa: No IO-Adr for PCI card found\n");
			return(0);
		}
		if (cs->hw.elsa.cfg & 0x80 && pci_rev == 1) {
			printk(KERN_INFO "Elsa: PLX9050 rev1 workaround activated\n");
			set_bit(FLG_BUGGY_PLX9050, &cs->HW_Flags);
		}
		cs->hw.elsa.ale  = cs->hw.elsa.base;
		cs->hw.elsa.isac = cs->hw.elsa.base +1;
		cs->hw.elsa.hscx = cs->hw.elsa.base +1; 
		test_and_set_bit(HW_IPAC, &cs->HW_Flags);
		cs->hw.elsa.timer = 0;
		cs->hw.elsa.trig  = 0;
		cs->irq_flags |= SA_SHIRQ;
		printk(KERN_INFO
		       "Elsa: %s defined at %#lx/0x%x IRQ %d\n",
		       Elsa_Types[cs->subtyp],
		       cs->hw.elsa.base,
		       cs->hw.elsa.cfg,
		       cs->irq);
#else
		printk(KERN_WARNING "Elsa: Elsa PCI and NO_PCI_BIOS\n");
		printk(KERN_WARNING "Elsa: unable to config Elsa PCI\n");
		return (0);
#endif /* CONFIG_PCI */
	} else 
		return (0);

	switch (cs->subtyp) {
		case ELSA_PC:
		case ELSA_PCC8:
		case ELSA_PCC16:
		case ELSA_QS1000:
		case ELSA_PCMCIA:
		case ELSA_PCMCIA_IPAC:
			bytecnt = 8;
			break;
		case ELSA_PCFPRO:
		case ELSA_PCF:
		case ELSA_QS3000:
		case ELSA_QS3000PCI:
			bytecnt = 16;
			break;
		case ELSA_QS1000PCI:
			bytecnt = 2;
			break;
		default:
			printk(KERN_WARNING
			       "Unknown ELSA subtype %d\n", cs->subtyp);
			return (0);
	}
	/* In case of the elsa pcmcia card, this region is in use,
	   reserved for us by the card manager. So we do not check it
	   here, it would fail. */
	if (cs->typ != ISDN_CTYPE_ELSA_PCMCIA)
		if (!request_region(cs->hw.elsa.base, bytecnt, "elsa isdn")) {		
			printk(KERN_WARNING
			       "HiSax: %s config port %#lx-%#lx already in use\n",
			       CardType[card->typ],
			       cs->hw.elsa.base,
			       cs->hw.elsa.base + bytecnt);
			return (0);
		}
	
	if ((cs->subtyp == ELSA_QS1000PCI) || (cs->subtyp == ELSA_QS3000PCI)) {
		if (!request_region(cs->hw.elsa.cfg, 0x80, "elsa isdn pci")) {
			printk(KERN_WARNING
			       "HiSax: %s pci port %x-%x already in use\n",
				CardType[card->typ],
				cs->hw.elsa.cfg,
				cs->hw.elsa.cfg + 0x80);
			release_region(cs->hw.elsa.base, bytecnt);
			return (0);
		}
	}
#if ARCOFI_USE
	init_arcofi(cs);
#endif
	cs->hw.elsa.tl.function = (void *) elsa_led_handler;
	cs->hw.elsa.tl.data = (long) cs;
	init_timer(&cs->hw.elsa.tl);
	/* Teste Timer */
	if (cs->hw.elsa.timer) {
		byteout(cs->hw.elsa.trig, 0xff);
		byteout(cs->hw.elsa.timer, 0);
		if (!TimerRun(cs)) {
			byteout(cs->hw.elsa.timer, 0);	/* 2. Versuch */
			if (!TimerRun(cs)) {
				printk(KERN_WARNING
				       "Elsa: timer do not start\n");
				release_io_elsa(cs);
				return (0);
			}
		}
		HZDELAY(1);	/* wait >=10 ms */
		if (TimerRun(cs)) {
			printk(KERN_WARNING "Elsa: timer do not run down\n");
			release_io_elsa(cs);
			return (0);
		}
		printk(KERN_INFO "Elsa: timer OK; resetting card\n");
	}
	cs->bc_hw_ops = &hscx_ops;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &Elsa_card_msg;
	reset_elsa(cs);
	if ((cs->subtyp == ELSA_QS1000PCI) || (cs->subtyp == ELSA_QS3000PCI) || (cs->subtyp == ELSA_PCMCIA_IPAC)) {
		cs->dc_hw_ops = &ipac_dc_ops;
		cs->irq_func = &elsa_interrupt_ipac;
		val = readreg(cs, cs->hw.elsa.isac, IPAC_ID);
		printk(KERN_INFO "Elsa: IPAC version %x\n", val);
	} else {
		cs->dc_hw_ops = &isac_ops;
		cs->irq_func = &elsa_interrupt;
		ISACVersion(cs, "Elsa:");
		if (HscxVersion(cs, "Elsa:")) {
			printk(KERN_WARNING
				"Elsa: wrong HSCX versions check IO address\n");
			release_io_elsa(cs);
			return (0);
		}
	}
	if (cs->subtyp == ELSA_PC) {
		val = readitac(cs, ITAC_SYS);
		printk(KERN_INFO "Elsa: ITAC version %s\n", ITACVer[val & 7]);
		writeitac(cs, ITAC_ISEN, 0);
		writeitac(cs, ITAC_RFIE, 0);
		writeitac(cs, ITAC_XFIE, 0);
		writeitac(cs, ITAC_SCIE, 0);
		writeitac(cs, ITAC_STIE, 0);
	}
	return (1);
}
