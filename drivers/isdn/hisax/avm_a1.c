/* $Id: avm_a1.c,v 1.4 1997/01/27 15:50:21 keil Exp $

 * avm_a1.c     low level stuff for AVM A1 (Fritz) isdn cards
 *
 * Author       Karsten Keil (keil@temic-ech.spacenet.de)
 *
 *
 * $Log: avm_a1.c,v $
 * Revision 1.4  1997/01/27 15:50:21  keil
 * SMP proof,cosmetics
 *
 * Revision 1.3  1997/01/21 22:14:20  keil
 * cleanups
 *
 * Revision 1.2  1996/10/27 22:07:31  keil
 * cosmetic changes
 *
 * Revision 1.1  1996/10/13 20:04:49  keil
 * Initial revision
 *
 *
 */
#define __NO_VERSION__
#include "siemens.h"
#include "hisax.h"
#include "avm_a1.h"
#include "isdnl1.h"
#include <linux/kernel_stat.h>

extern const char *CardType[];
const char *avm_revision = "$Revision: 1.4 $";

#define byteout(addr,val) outb_p(val,addr)
#define bytein(addr) inb_p(addr)

static inline byte
readreg(unsigned int adr, byte off)
{
	return (bytein(adr + off));
}

static inline void
writereg(unsigned int adr, byte off, byte data)
{
	byteout(adr + off, data);
}


static inline void
read_fifo(unsigned int adr, byte * data, int size)
{
	insb(adr - 0x400, data, size);
}

static void
write_fifo(unsigned int adr, byte * data, int size)
{
	outsb(adr - 0x400, data, size);
}

static inline void
waitforCEC(int adr)
{
	int to = 50;

	while ((readreg(adr, HSCX_STAR) & 0x04) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "AVM A1: waitforCEC timeout\n");
}


static inline void
waitforXFW(int adr)
{
	int to = 50;

	while ((!(readreg(adr, HSCX_STAR) & 0x44) == 0x40) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "AVM A1: waitforXFW timeout\n");
}
static inline void
writehscxCMDR(int adr, byte data)
{
	long flags;

	save_flags(flags);
	cli();
	waitforCEC(adr);
	writereg(adr, HSCX_CMDR, data);
	restore_flags(flags);
}

/*
 * fast interrupt here
 */

static void
hscxreport(struct IsdnCardState *sp, int hscx)
{
	printk(KERN_DEBUG "HSCX %d\n", hscx);
	printk(KERN_DEBUG "ISTA %x\n", readreg(sp->hscx[hscx], HSCX_ISTA));
	printk(KERN_DEBUG "STAR %x\n", readreg(sp->hscx[hscx], HSCX_STAR));
	printk(KERN_DEBUG "EXIR %x\n", readreg(sp->hscx[hscx], HSCX_EXIR));
}

void
avm_a1_report(struct IsdnCardState *sp)
{
	printk(KERN_DEBUG "ISAC\n");
	printk(KERN_DEBUG "ISTA %x\n", readreg(sp->isac, ISAC_ISTA));
	printk(KERN_DEBUG "STAR %x\n", readreg(sp->isac, ISAC_STAR));
	printk(KERN_DEBUG "EXIR %x\n", readreg(sp->isac, ISAC_EXIR));
	hscxreport(sp, 0);
	hscxreport(sp, 1);
}

/*
 * HSCX stuff goes here
 */

static void
hscx_empty_fifo(struct HscxState *hsp, int count)
{
	byte *ptr;
	struct IsdnCardState *sp = hsp->sp;
	struct BufHeader *ibh = hsp->rcvibh;
	long flags;

	if ((sp->debug & L1_DEB_HSCX) && !(sp->debug & L1_DEB_HSCX_FIFO))
		debugl1(sp, "hscx_empty_fifo");

	if (hsp->rcvptr + count > BUFFER_SIZE(HSCX_RBUF_ORDER,
					      HSCX_RBUF_BPPS)) {
		if (sp->debug & L1_DEB_WARN)
			debugl1(sp, "hscx_empty_fifo: incoming packet too large");
		writehscxCMDR(sp->hscx[hsp->hscx], 0x80);
		return;
	}
	ptr = DATAPTR(ibh);
	ptr += hsp->rcvptr;

	hsp->rcvptr += count;
	save_flags(flags);
	cli();
	read_fifo(sp->hscx[hsp->hscx], ptr, count);
	writehscxCMDR(sp->hscx[hsp->hscx], 0x80);
	restore_flags(flags);
	if (sp->debug & L1_DEB_HSCX_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "hscx_empty_fifo %c cnt %d",
			     hsp->hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(sp, tmp);
	}
}

static void
hscx_fill_fifo(struct HscxState *hsp)
{
	struct IsdnCardState *sp = hsp->sp;
	struct BufHeader *ibh;
	int more, count;
	byte *ptr;
	long flags;

	if ((sp->debug & L1_DEB_HSCX) && !(sp->debug & L1_DEB_HSCX_FIFO))
		debugl1(sp, "hscx_fill_fifo");

	ibh = hsp->xmtibh;
	if (!ibh)
		return;

	count = ibh->datasize - hsp->sendptr;
	if (count <= 0)
		return;

	more = (hsp->mode == 1) ? 1 : 0;
	if (count > 32) {
		more = !0;
		count = 32;
	}
	ptr = DATAPTR(ibh);
	ptr += hsp->sendptr;
	hsp->sendptr += count;

	waitforXFW(sp->hscx[hsp->hscx]);
	save_flags(flags);
	cli();
	write_fifo(sp->hscx[hsp->hscx], ptr, count);
	writehscxCMDR(sp->hscx[hsp->hscx], more ? 0x8 : 0xa);
	restore_flags(flags);
	if (sp->debug & L1_DEB_HSCX_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "hscx_fill_fifo %c cnt %d",
			     hsp->hscx ? 'B' : 'A', count);
		QuickHex(t, ptr, count);
		debugl1(sp, tmp);
	}
}

static inline void
hscx_interrupt(struct IsdnCardState *sp, byte val, byte hscx)
{
	byte r;
	struct HscxState *hsp = sp->hs + hscx;
	int count, err;
	char tmp[32];

	if (!hsp->init)
		return;

	if (val & 0x80) {	/* RME */

		r = readreg(sp->hscx[hsp->hscx], HSCX_RSTA);
		if ((r & 0xf0) != 0xa0) {
			if (!r & 0x80)
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "HSCX invalid frame");
			if ((r & 0x40) && hsp->mode)
				if (sp->debug & L1_DEB_WARN) {
					sprintf(tmp, "HSCX RDO mode=%d",
						hsp->mode);
					debugl1(sp, tmp);
				}
			if (!r & 0x20)
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "HSCX CRC error");
			if (hsp->rcvibh)
				BufPoolRelease(hsp->rcvibh);
			hsp->rcvibh = NULL;
			writehscxCMDR(sp->hscx[hsp->hscx], 0x80);
			goto afterRME;
		}
		if (!hsp->rcvibh)
			if (BufPoolGet(&hsp->rcvibh, &hsp->rbufpool,
				       GFP_ATOMIC, (void *) 1, 1)) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "HSCX RME out of buffers");
				writehscxCMDR(sp->hscx[hsp->hscx], 0x80);
				goto afterRME;
			} else
				hsp->rcvptr = 0;

		count = readreg(sp->hscx[hsp->hscx], HSCX_RBCL) & 0x1f;
		if (count == 0)
			count = 32;
		hscx_empty_fifo(hsp, count);
		hsp->rcvibh->datasize = hsp->rcvptr - 1;
		BufQueueLink(&hsp->rq, hsp->rcvibh);
		hsp->rcvibh = NULL;
		hscx_sched_event(hsp, HSCX_RCVBUFREADY);
	}
      afterRME:
	if (val & 0x40) {	/* RPF */
		if (!hsp->rcvibh) {
			if (hsp->mode == 1)
				err = BufPoolGet(&hsp->rcvibh, &hsp->smallpool,
					      GFP_ATOMIC, (void *) 1, 2);
			else
				err = BufPoolGet(&hsp->rcvibh, &hsp->rbufpool,
					      GFP_ATOMIC, (void *) 1, 2);

			if (err) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "HSCX RPF out of buffers");
				writehscxCMDR(sp->hscx[hsp->hscx], 0x80);
				goto afterRPF;
			} else
				hsp->rcvptr = 0;
		}
		hscx_empty_fifo(hsp, 32);
		if (hsp->mode == 1) {
			/* receive audio data */
			hsp->rcvibh->datasize = hsp->rcvptr;
			BufQueueLink(&hsp->rq, hsp->rcvibh);
			hsp->rcvibh = NULL;
			hscx_sched_event(hsp, HSCX_RCVBUFREADY);
		}
	}
      afterRPF:
	if (val & 0x10) {	/* XPR */
		if (hsp->xmtibh)
			if (hsp->xmtibh->datasize > hsp->sendptr) {
				hscx_fill_fifo(hsp);
				goto afterXPR;
			} else {
				if (hsp->releasebuf)
					BufPoolRelease(hsp->xmtibh);
				hsp->sendptr = 0;
				if (hsp->st->l4.l1writewakeup)
					hsp->st->l4.l1writewakeup(hsp->st);
				hsp->xmtibh = NULL;
			}
		if (!BufQueueUnlink(&hsp->xmtibh, &hsp->sq)) {
			hsp->releasebuf = !0;
			hscx_fill_fifo(hsp);
		} else
			hscx_sched_event(hsp, HSCX_XMTBUFREADY);
	}
      afterXPR:
}

/*
 * ISAC stuff goes here
 */

static void
isac_empty_fifo(struct IsdnCardState *sp, int count)
{
	byte *ptr;
	struct BufHeader *ibh = sp->rcvibh;
	long flags;

	if ((sp->debug & L1_DEB_ISAC) && !(sp->debug & L1_DEB_ISAC_FIFO))
		if (sp->debug & L1_DEB_ISAC)
			debugl1(sp, "isac_empty_fifo");

	if (sp->rcvptr >= 3072) {
		if (sp->debug & L1_DEB_WARN) {
			char tmp[40];
			sprintf(tmp, "isac_empty_fifo rcvptr %d", sp->rcvptr);
			debugl1(sp, tmp);
		}
		return;
	}
	ptr = DATAPTR(ibh);
	ptr += sp->rcvptr;
	sp->rcvptr += count;

	save_flags(flags);
	cli();
	read_fifo(sp->isac, ptr, count);
	writereg(sp->isac, ISAC_CMDR, 0x80);
	restore_flags(flags);
	if (sp->debug & L1_DEB_ISAC_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "isac_empty_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(sp, tmp);
	}
}

static void
isac_fill_fifo(struct IsdnCardState *sp)
{
	struct BufHeader *ibh;
	int count, more;
	byte *ptr;
	long flags;

	if ((sp->debug & L1_DEB_ISAC) && !(sp->debug & L1_DEB_ISAC_FIFO))
		debugl1(sp, "isac_fill_fifo");

	ibh = sp->xmtibh;
	if (!ibh)
		return;

	count = ibh->datasize - sp->sendptr;
	if (count <= 0)
		return;
	if (count >= 3072)
		return;

	more = 0;
	if (count > 32) {
		more = !0;
		count = 32;
	}
	ptr = DATAPTR(ibh);
	ptr += sp->sendptr;
	sp->sendptr += count;

	save_flags(flags);
	cli();
	write_fifo(sp->isac, ptr, count);
	writereg(sp->isac, ISAC_CMDR, more ? 0x8 : 0xa);
	restore_flags(flags);
	if (sp->debug & L1_DEB_ISAC_FIFO) {
		char tmp[128];
		char *t = tmp;

		t += sprintf(t, "isac_fill_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(sp, tmp);
	}
}

static void
ph_command(struct IsdnCardState *sp, unsigned int command)
{
	if (sp->debug & L1_DEB_ISAC) {
		char tmp[32];
		sprintf(tmp, "ph_command %d", command);
		debugl1(sp, tmp);
	}
	writereg(sp->isac, ISAC_CIX0, (command << 2) | 3);
}


static inline void
isac_interrupt(struct IsdnCardState *sp, byte val)
{
	byte exval;
	unsigned int count;
	char tmp[32];

	if (sp->debug & L1_DEB_ISAC) {
		sprintf(tmp, "ISAC interrupt %x", val);
		debugl1(sp, tmp);
	}
	if (val & 0x80) {	/* RME */
		exval = readreg(sp->isac, ISAC_RSTA);
		if ((exval & 0x70) != 0x20) {
			if (exval & 0x40)
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC RDO");
			if (!exval & 0x20)
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC CRC error");
			if (sp->rcvibh)
				BufPoolRelease(sp->rcvibh);
			sp->rcvibh = NULL;
			writereg(sp->isac, ISAC_CMDR, 0x80);
			goto afterRME;
		}
		if (!sp->rcvibh)
			if (BufPoolGet(&(sp->rcvibh), &(sp->rbufpool),
				       GFP_ATOMIC, (void *) 1, 3)) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC RME out of buffers!");
				writereg(sp->isac, ISAC_CMDR, 0x80);
				goto afterRME;
			} else
				sp->rcvptr = 0;
		count = readreg(sp->isac, ISAC_RBCL) & 0x1f;
		if (count == 0)
			count = 32;
		isac_empty_fifo(sp, count);
		sp->rcvibh->datasize = sp->rcvptr;
		BufQueueLink(&(sp->rq), sp->rcvibh);
		sp->rcvibh = NULL;
		isac_sched_event(sp, ISAC_RCVBUFREADY);
	}
      afterRME:
	if (val & 0x40) {	/* RPF */
		if (!sp->rcvibh)
			if (BufPoolGet(&(sp->rcvibh), &(sp->rbufpool),
				       GFP_ATOMIC, (void *) 1, 4)) {
				if (sp->debug & L1_DEB_WARN)
					debugl1(sp, "ISAC RME out of buffers!");
				writereg(sp->isac, ISAC_CMDR, 0x80);
				goto afterRPF;
			} else
				sp->rcvptr = 0;
		isac_empty_fifo(sp, 32);
	}
      afterRPF:
	if (val & 0x20) {	/* RSC */
		/* never */
		if (sp->debug & L1_DEB_WARN)
			debugl1(sp, "ISAC RSC interrupt");
	}
	if (val & 0x10) {	/* XPR */
		if (sp->xmtibh)
			if (sp->xmtibh->datasize > sp->sendptr) {
				isac_fill_fifo(sp);
				goto afterXPR;
			} else {
				if (sp->releasebuf)
					BufPoolRelease(sp->xmtibh);
				sp->xmtibh = NULL;
				sp->sendptr = 0;
			}
		if (!BufQueueUnlink(&sp->xmtibh, &sp->sq)) {
			sp->releasebuf = !0;
			isac_fill_fifo(sp);
		} else
			isac_sched_event(sp, ISAC_XMTBUFREADY);
	}
      afterXPR:
	if (val & 0x04) {	/* CISQ */
		sp->ph_state = (readreg(sp->isac, ISAC_CIX0) >> 2)
		    & 0xf;
		if (sp->debug & L1_DEB_ISAC) {
			sprintf(tmp, "l1state %d", sp->ph_state);
			debugl1(sp, tmp);
		}
		isac_new_ph(sp);
	}
	if (val & 0x02) {	/* SIN */
		/* never */
		if (sp->debug & L1_DEB_WARN)
			debugl1(sp, "ISAC SIN interrupt");
	}
	if (val & 0x01) {	/* EXI */
		exval = readreg(sp->isac, ISAC_EXIR);
		if (sp->debug & L1_DEB_WARN) {
			sprintf(tmp, "ISAC EXIR %02x", exval);
			debugl1(sp, tmp);
		}
	}
}

static inline void
hscx_int_main(struct IsdnCardState *sp, byte val)
{

	byte exval;
	struct HscxState *hsp;
	char tmp[32];


	if (val & 0x01) {
		hsp = sp->hs + 1;
		exval = readreg(sp->hscx[1], HSCX_EXIR);
		if (exval == 0x40) {
			if (hsp->mode == 1)
				hscx_fill_fifo(hsp);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				hsp->sendptr = 0;
				writehscxCMDR(sp->hscx[hsp->hscx], 0x01);
				if (sp->debug & L1_DEB_WARN) {
					sprintf(tmp, "HSCX B EXIR %x Lost TX", exval);
					debugl1(sp, tmp);
				}
			}
		} else if (sp->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX B EXIR %x", exval);
			debugl1(sp, tmp);
		}
	}
	if (val & 0xf8) {
		if (sp->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX B interrupt %x", val);
			debugl1(sp, tmp);
		}
		hscx_interrupt(sp, val, 1);
	}
	if (val & 0x02) {
		hsp = sp->hs;
		exval = readreg(sp->hscx[0], HSCX_EXIR);
		if (exval == 0x40) {
			if (hsp->mode == 1)
				hscx_fill_fifo(hsp);
			else {
				/* Here we lost an TX interrupt, so
				   * restart transmitting the whole frame.
				 */
				hsp->sendptr = 0;
				writehscxCMDR(sp->hscx[hsp->hscx], 0x01);
				if (sp->debug & L1_DEB_WARN) {
					sprintf(tmp, "HSCX A EXIR %x Lost TX", exval);
					debugl1(sp, tmp);
				}
			}
		} else if (sp->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX A EXIR %x", exval);
			debugl1(sp, tmp);
		}
	}
	if (val & 0x04) {
		exval = readreg(sp->hscx[0], HSCX_ISTA);
		if (sp->debug & L1_DEB_HSCX) {
			sprintf(tmp, "HSCX A interrupt %x", exval);
			debugl1(sp, tmp);
		}
		hscx_interrupt(sp, exval, 0);
	}
}

static void
avm_a1_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *sp;
	byte val, sval, stat = 0;
	char tmp[32];

	sp = (struct IsdnCardState *) irq2dev_map[intno];

	if (!sp) {
		printk(KERN_WARNING "AVM A1: Spurious interrupt!\n");
		return;
	}
	while (((sval = bytein(sp->cfg_reg)) & 0xf) != 0x7) {
		if (!(sval & AVM_A1_STAT_TIMER)) {
			byteout(sp->cfg_reg, 0x14);
			byteout(sp->cfg_reg, 0x18);
			sval = bytein(sp->cfg_reg);
		} else if (sp->debug & L1_DEB_INTSTAT) {
			sprintf(tmp, "avm IntStatus %x", sval);
			debugl1(sp, tmp);
		}
		if (!(sval & AVM_A1_STAT_HSCX)) {
			val = readreg(sp->hscx[1], HSCX_ISTA);
			if (val) {
				hscx_int_main(sp, val);
				stat |= 1;
			}
		}
		if (!(sval & AVM_A1_STAT_ISAC)) {
			val = readreg(sp->isac, ISAC_ISTA);
			if (val) {
				isac_interrupt(sp, val);
				stat |= 2;
			}
		}
	}
	if (stat & 1) {
		writereg(sp->hscx[0], HSCX_MASK, 0xFF);
		writereg(sp->hscx[1], HSCX_MASK, 0xFF);
		writereg(sp->hscx[0], HSCX_MASK, 0x0);
		writereg(sp->hscx[1], HSCX_MASK, 0x0);
	}
	if (stat & 2) {
		writereg(sp->isac, ISAC_MASK, 0xFF);
		writereg(sp->isac, ISAC_MASK, 0x0);
	}
}


static void
initisac(struct IsdnCardState *sp)
{
	unsigned int adr = sp->isac;

	/* 16.3 IOM 2 Mode */
	writereg(adr, ISAC_MASK, 0xff);
	writereg(adr, ISAC_ADF2, 0x80);
	writereg(adr, ISAC_SQXR, 0x2f);
	writereg(adr, ISAC_SPCR, 0x0);
	writereg(adr, ISAC_ADF1, 0x2);
	writereg(adr, ISAC_STCR, 0x70);
	writereg(adr, ISAC_MODE, 0xc9);
	writereg(adr, ISAC_TIMR, 0x0);
	writereg(adr, ISAC_ADF1, 0x0);
	writereg(adr, ISAC_CMDR, 0x41);
	writereg(adr, ISAC_CIX0, (1 << 2) | 3);
	writereg(adr, ISAC_MASK, 0xff);
	writereg(adr, ISAC_MASK, 0x0);
}

static void
modehscx(struct HscxState *hs, int mode, int ichan)
{
	struct IsdnCardState *sp = hs->sp;
	int hscx = hs->hscx;

	if (sp->debug & L1_DEB_HSCX) {
		char tmp[40];
		sprintf(tmp, "hscx %c mode %d ichan %d",
			'A' + hscx, mode, ichan);
		debugl1(sp, tmp);
	}
	hs->mode = mode;
	writereg(sp->hscx[hscx], HSCX_CCR1, 0x85);
	writereg(sp->hscx[hscx], HSCX_XAD1, 0xFF);
	writereg(sp->hscx[hscx], HSCX_XAD2, 0xFF);
	writereg(sp->hscx[hscx], HSCX_RAH2, 0xFF);
	writereg(sp->hscx[hscx], HSCX_XBCH, 0x0);
	writereg(sp->hscx[hscx], HSCX_RLCR, 0x0);

	switch (mode) {
	case (0):
		writereg(sp->hscx[hscx], HSCX_CCR2, 0x30);
		writereg(sp->hscx[hscx], HSCX_TSAX, 0xff);
		writereg(sp->hscx[hscx], HSCX_TSAR, 0xff);
		writereg(sp->hscx[hscx], HSCX_XCCR, 7);
		writereg(sp->hscx[hscx], HSCX_RCCR, 7);
		writereg(sp->hscx[hscx], HSCX_MODE, 0x84);
		break;
	case (1):
		if (ichan == 0) {
			writereg(sp->hscx[hscx], HSCX_CCR2, 0x30);
			writereg(sp->hscx[hscx], HSCX_TSAX, 0x2f);
			writereg(sp->hscx[hscx], HSCX_TSAR, 0x2f);
			writereg(sp->hscx[hscx], HSCX_XCCR, 7);
			writereg(sp->hscx[hscx], HSCX_RCCR, 7);
		} else {
			writereg(sp->hscx[hscx], HSCX_CCR2, 0x30);
			writereg(sp->hscx[hscx], HSCX_TSAX, 0x3);
			writereg(sp->hscx[hscx], HSCX_TSAR, 0x3);
			writereg(sp->hscx[hscx], HSCX_XCCR, 7);
			writereg(sp->hscx[hscx], HSCX_RCCR, 7);
		}
		writereg(sp->hscx[hscx], HSCX_MODE, 0xe4);
		writereg(sp->hscx[hscx], HSCX_CMDR, 0x41);
		break;
	case (2):
		if (ichan == 0) {
			writereg(sp->hscx[hscx], HSCX_CCR2, 0x30);
			writereg(sp->hscx[hscx], HSCX_TSAX, 0x2f);
			writereg(sp->hscx[hscx], HSCX_TSAR, 0x2f);
			writereg(sp->hscx[hscx], HSCX_XCCR, 7);
			writereg(sp->hscx[hscx], HSCX_RCCR, 7);
		} else {
			writereg(sp->hscx[hscx], HSCX_CCR2, 0x30);
			writereg(sp->hscx[hscx], HSCX_TSAX, 0x3);
			writereg(sp->hscx[hscx], HSCX_TSAR, 0x3);
			writereg(sp->hscx[hscx], HSCX_XCCR, 7);
			writereg(sp->hscx[hscx], HSCX_RCCR, 7);
		}
		writereg(sp->hscx[hscx], HSCX_MODE, 0x8c);
		writereg(sp->hscx[hscx], HSCX_CMDR, 0x41);
		break;
	}
	writereg(sp->hscx[hscx], HSCX_ISTA, 0x00);
}

inline static void
release_ioregs(struct IsdnCard *card, int mask)
{
	release_region(card->sp->cfg_reg, 8);
	if (mask & 1)
		release_region(card->sp->isac, 32);
	if (mask & 2)
		release_region(card->sp->isac - 0x400, 1);
	if (mask & 4)
		release_region(card->sp->hscx[0], 32);
	if (mask & 8)
		release_region(card->sp->hscx[0] - 0x400, 1);
	if (mask & 0x10)
		release_region(card->sp->hscx[1], 32);
	if (mask & 0x20)
		release_region(card->sp->hscx[1] - 0x400, 1);
}

void
release_io_avm_a1(struct IsdnCard *card)
{
	release_ioregs(card, 0x3f);
}

static void
clear_pending_ints(struct IsdnCardState *sp)
{
	int val;
	char tmp[64];

	val = readreg(sp->hscx[1], HSCX_ISTA);
	sprintf(tmp, "HSCX B ISTA %x", val);
	debugl1(sp, tmp);
	if (val & 0x01) {
		val = readreg(sp->hscx[1], HSCX_EXIR);
		sprintf(tmp, "HSCX B EXIR %x", val);
		debugl1(sp, tmp);
	} else if (val & 0x02) {
		val = readreg(sp->hscx[0], HSCX_EXIR);
		sprintf(tmp, "HSCX A EXIR %x", val);
		debugl1(sp, tmp);
	}
	val = readreg(sp->hscx[0], HSCX_ISTA);
	sprintf(tmp, "HSCX A ISTA %x", val);
	debugl1(sp, tmp);
	val = readreg(sp->hscx[1], HSCX_STAR);
	sprintf(tmp, "HSCX B STAR %x", val);
	debugl1(sp, tmp);
	val = readreg(sp->hscx[0], HSCX_STAR);
	sprintf(tmp, "HSCX A STAR %x", val);
	debugl1(sp, tmp);
	val = readreg(sp->isac, ISAC_STAR);
	sprintf(tmp, "ISAC STAR %x", val);
	debugl1(sp, tmp);
	val = readreg(sp->isac, ISAC_MODE);
	sprintf(tmp, "ISAC MODE %x", val);
	debugl1(sp, tmp);
	val = readreg(sp->isac, ISAC_ADF2);
	sprintf(tmp, "ISAC ADF2 %x", val);
	debugl1(sp, tmp);
	val = readreg(sp->isac, ISAC_ISTA);
	sprintf(tmp, "ISAC ISTA %x", val);
	debugl1(sp, tmp);
	if (val & 0x01) {
		val = readreg(sp->isac, ISAC_EXIR);
		sprintf(tmp, "ISAC EXIR %x", val);
		debugl1(sp, tmp);
	} else if (val & 0x04) {
		val = readreg(sp->isac, ISAC_CIR0);
		sprintf(tmp, "ISAC CIR0 %x", val);
		debugl1(sp, tmp);
	}
	writereg(sp->isac, ISAC_MASK, 0);
	writereg(sp->isac, ISAC_CMDR, 0x41);
}

int
initavm_a1(struct IsdnCardState *sp)
{
	int ret;
	char tmp[40];

	sp->counter = kstat.interrupts[sp->irq];
	sprintf(tmp, "IRQ %d count %d", sp->irq, sp->counter);
	debugl1(sp, tmp);
	clear_pending_ints(sp);
	ret = get_irq(sp->cardnr, &avm_a1_interrupt);
	if (ret) {
		initisac(sp);
		sp->modehscx(sp->hs, 0, 0);
		sp->modehscx(sp->hs + 1, 0, 0);
		sprintf(tmp, "IRQ %d count %d", sp->irq,
			kstat.interrupts[sp->irq]);
		debugl1(sp, tmp);
		if (kstat.interrupts[sp->irq] == sp->counter) {
			printk(KERN_WARNING
			       "AVM A1: IRQ(%d) getting no interrupts during init\n",
			       sp->irq);
			irq2dev_map[sp->irq] = NULL;
			free_irq(sp->irq, NULL);
			return (0);
		}
	}
	return (ret);
}

int
setup_avm_a1(struct IsdnCard *card)
{
	byte val, verA, verB;
	struct IsdnCardState *sp = card->sp;
	long flags;
	char tmp[64];

	strcpy(tmp, avm_revision);
	printk(KERN_NOTICE "HiSax: AVM driver Rev. %s\n", HiSax_getrev(tmp));
	if (sp->typ != ISDN_CTYPE_A1)
		return (0);

	sp->cfg_reg = card->para[1] + 0x1800;
	sp->isac = card->para[1] + 0x1400;
	sp->hscx[0] = card->para[1] + 0x400;
	sp->hscx[1] = card->para[1] + 0xc00;
	sp->irq = card->para[0];
	if (check_region((sp->cfg_reg), 8)) {
		printk(KERN_WARNING
		       "HiSax: %s config port %x-%x already in use\n",
		       CardType[card->typ],
		       sp->cfg_reg,
		       sp->cfg_reg + 8);
		return (0);
	} else {
		request_region(sp->cfg_reg, 8, "avm cfg");
	}
	if (check_region((sp->isac), 32)) {
		printk(KERN_WARNING
		       "HiSax: %s isac ports %x-%x already in use\n",
		       CardType[sp->typ],
		       sp->isac,
		       sp->isac + 32);
		release_ioregs(card, 0);
		return (0);
	} else {
		request_region(sp->isac, 32, "HiSax isac");
	}
	if (check_region((sp->isac - 0x400), 1)) {
		printk(KERN_WARNING
		       "HiSax: %s isac fifo port %x already in use\n",
		       CardType[sp->typ],
		       sp->isac - 0x400);
		release_ioregs(card, 1);
		return (0);
	} else {
		request_region(sp->isac - 0x400, 1, "HiSax isac fifo");
	}
	if (check_region((sp->hscx[0]), 32)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx A ports %x-%x already in use\n",
		       CardType[sp->typ],
		       sp->hscx[0],
		       sp->hscx[0] + 32);
		release_ioregs(card, 3);
		return (0);
	} else {
		request_region(sp->hscx[0], 32, "HiSax hscx A");
	}
	if (check_region((sp->hscx[0] - 0x400), 1)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx A fifo port %x already in use\n",
		       CardType[sp->typ],
		       sp->hscx[0] - 0x400);
		release_ioregs(card, 7);
		return (0);
	} else {
		request_region(sp->hscx[0] - 0x400, 1, "HiSax hscx A fifo");
	}
	if (check_region((sp->hscx[1]), 32)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx B ports %x-%x already in use\n",
		       CardType[sp->typ],
		       sp->hscx[1],
		       sp->hscx[1] + 32);
		release_ioregs(card, 0xf);
		return (0);
	} else {
		request_region(sp->hscx[1], 32, "HiSax hscx B");
	}
	if (check_region((sp->hscx[1] - 0x400), 1)) {
		printk(KERN_WARNING
		       "HiSax: %s hscx B fifo port %x already in use\n",
		       CardType[sp->typ],
		       sp->hscx[1] - 0x400);
		release_ioregs(card, 0x1f);
		return (0);
	} else {
		request_region(sp->hscx[1] - 0x400, 1, "HiSax hscx B fifo");
	}
	save_flags(flags);
	byteout(sp->cfg_reg, 0x0);
	sti();
	HZDELAY(HZ / 5 + 1);
	byteout(sp->cfg_reg, 0x1);
	HZDELAY(HZ / 5 + 1);
	byteout(sp->cfg_reg, 0x0);
	HZDELAY(HZ / 5 + 1);
	val = sp->irq;
	if (val == 9)
		val = 2;
	byteout(sp->cfg_reg + 1, val);
	HZDELAY(HZ / 5 + 1);
	byteout(sp->cfg_reg, 0x0);
	HZDELAY(HZ / 5 + 1);
	restore_flags(flags);

	val = bytein(sp->cfg_reg);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       sp->cfg_reg, val);
	val = bytein(sp->cfg_reg + 3);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       sp->cfg_reg + 3, val);
	val = bytein(sp->cfg_reg + 2);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       sp->cfg_reg + 2, val);
	byteout(sp->cfg_reg, 0x14);
	byteout(sp->cfg_reg, 0x18);
	val = bytein(sp->cfg_reg);
	printk(KERN_INFO "AVM A1: Byte at %x is %x\n",
	       sp->cfg_reg, val);

	printk(KERN_NOTICE
	       "HiSax: %s config irq:%d cfg:%x\n",
	       CardType[sp->typ], sp->irq,
	       sp->cfg_reg);
	printk(KERN_NOTICE
	       "HiSax: isac:%x/%x\n",
	       sp->isac, sp->isac - 0x400);
	printk(KERN_NOTICE
	       "HiSax: hscx A:%x/%x  hscx B:%x/%x\n",
	       sp->hscx[0], sp->hscx[0] - 0x400,
	       sp->hscx[1], sp->hscx[1] - 0x400);
	verA = readreg(sp->hscx[0], HSCX_VSTR) & 0xf;
	verB = readreg(sp->hscx[1], HSCX_VSTR) & 0xf;
	printk(KERN_INFO "AVM A1: HSCX version A: %s  B: %s\n",
	       HscxVersion(verA), HscxVersion(verB));
	val = readreg(sp->isac, ISAC_RBCH);
	printk(KERN_INFO "AVM A1: ISAC %s\n",
	       ISACVersion(val));
	if ((verA == 0) | (verA == 0xf) | (verB == 0) | (verB == 0xf)) {
		printk(KERN_WARNING
		       "AVM A1: wrong HSCX versions check IO address\n");
		release_io_avm_a1(card);
		return (0);
	}
	sp->modehscx = &modehscx;
	sp->ph_command = &ph_command;
	sp->hscx_fill_fifo = &hscx_fill_fifo;
	sp->isac_fill_fifo = &isac_fill_fifo;
	return (1);
}
