/* $Id: mic.c,v 1.10.6.2 2001/09/23 22:24:50 kai Exp $
 *
 * low level stuff for mic cards
 *
 * Author       Stephan von Krawczynski
 * Copyright    by Stephan von Krawczynski <skraw@ithnet.com>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

extern const char *CardType[];

const char *mic_revision = "$Revision: 1.10.6.2 $";
static spinlock_t mic_lock = SPIN_LOCK_UNLOCKED;

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

#define MIC_ISAC	2
#define MIC_HSCX	1
#define MIC_ADR		7

/* CARD_ADR (Write) */
#define MIC_RESET      0x3	/* same as DOS driver */

static inline u8
readreg(struct IsdnCardState *cs, unsigned int adr, u8 off)
{
	u8 ret;
	unsigned long flags;

	spin_lock_irqsave(&mic_lock, flags);
	byteout(cs->hw.mic.adr, off);
	ret = bytein(adr);
	spin_unlock_irqrestore(&mic_lock, flags);

	return (ret);
}

static inline void
writereg(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 data)
{
	unsigned long flags;

	spin_lock_irqsave(&mic_lock, flags);
	byteout(cs->hw.mic.adr, off);
	byteout(adr, data);
	spin_unlock_irqrestore(&mic_lock, flags);
}

static inline void
readfifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(cs->hw.mic.adr, off);
	insb(adr, data, size);
}

static inline void
writefifo(struct IsdnCardState *cs, unsigned int adr, u8 off, u8 * data, int size)
{
	byteout(cs->hw.mic.adr, off);
	outsb(adr, data, size);
}

static u8
isac_read(struct IsdnCardState *cs, u8 offset)
{
	return readreg(cs, cs->hw.mic.isac, offset);
}

static void
isac_write(struct IsdnCardState *cs, u8 offset, u8 value)
{
	writereg(cs, cs->hw.mic.isac, offset, value);
}

static void
isac_read_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	readfifo(cs, cs->hw.mic.isac, 0, data, size);
}

static void
isac_write_fifo(struct IsdnCardState *cs, u8 * data, int size)
{
	writefifo(cs, cs->hw.mic.isac, 0, data, size);
}

static struct dc_hw_ops isac_ops = {
	.read_reg   = isac_read,
	.write_reg  = isac_write,
	.read_fifo  = isac_read_fifo,
	.write_fifo = isac_write_fifo,
};

static u8
hscx_read(struct IsdnCardState *cs, int hscx, u8 offset)
{
	return readreg(cs, cs->hw.mic.hscx, offset + (hscx ? 0x40 : 0));
}

static void
hscx_write(struct IsdnCardState *cs, int hscx, u8 offset, u8 value)
{
	writereg(cs, cs->hw.mic.hscx, offset + (hscx ? 0x40 : 0), value);
}

static void
hscx_read_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	readfifo(cs, cs->hw.mic.hscx, hscx ? 0x40 : 0, data, size);
}

static void
hscx_write_fifo(struct IsdnCardState *cs, int hscx, u8 *data, int size)
{
	writefifo(cs, cs->hw.mic.hscx, hscx ? 0x40 : 0, data, size);
}

static struct bc_hw_ops hscx_ops = {
	.read_reg   = hscx_read,
	.write_reg  = hscx_write,
	.read_fifo  = hscx_read_fifo,
	.write_fifo = hscx_write_fifo,
};

static void
mic_release(struct IsdnCardState *cs)
{
	if (cs->hw.mic.cfg_reg)
		release_region(cs->hw.mic.cfg_reg, 8);
}

static int
mic_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	return(0);
}

static struct card_ops mic_ops = {
	.init     = inithscxisac,
	.release  = mic_release,
	.irq_func = hscxisac_irq,
};

int __init
setup_mic(struct IsdnCard *card)
{
	int bytecnt;
	struct IsdnCardState *cs = card->cs;
	char tmp[64];

	strcpy(tmp, mic_revision);
	printk(KERN_INFO "HiSax: mic driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_MIC)
		return (0);

	bytecnt = 8;
	cs->hw.mic.cfg_reg = card->para[1];
	cs->irq = card->para[0];
	cs->hw.mic.adr = cs->hw.mic.cfg_reg + MIC_ADR;
	cs->hw.mic.isac = cs->hw.mic.cfg_reg + MIC_ISAC;
	cs->hw.mic.hscx = cs->hw.mic.cfg_reg + MIC_HSCX;

	if (!request_region((cs->hw.mic.cfg_reg), bytecnt, "mic isdn")) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       cs->hw.mic.cfg_reg,
		       cs->hw.mic.cfg_reg + bytecnt);
		return (0);
	}

	printk(KERN_INFO
	       "mic: defined at 0x%x IRQ %d\n",
	       cs->hw.mic.cfg_reg,
	       cs->irq);
	cs->dc_hw_ops = &isac_ops;
	cs->bc_hw_ops = &hscx_ops;
	cs->cardmsg = &mic_card_msg;
	cs->card_ops = &mic_ops;
	ISACVersion(cs, "mic:");
	if (HscxVersion(cs, "mic:")) {
		printk(KERN_WARNING
		    "mic: wrong HSCX versions check IO address\n");
		mic_release(cs);
		return (0);
	}
	return (1);
}
