/* $Id: hfc_2bds0.c,v 1.15.6.3 2001/09/23 22:24:47 kai Exp $
 *
 * specific routines for CCD's HFC 2BDS0
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "hfc_2bds0.h"
#include "isdnl1.h"
#include <linux/interrupt.h>
/*
#define KDEBUG_DEF
#include "kdebug.h"
*/

#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

static inline u8
ReadReg(struct IsdnCardState *cs, int data, u8 reg)
{
	register u8 ret;

	if (data) {
		if (cs->hw.hfcD.cip != reg) { 
			cs->hw.hfcD.cip = reg;
			byteout(cs->hw.hfcD.addr | 1, reg);
		}
		ret = bytein(cs->hw.hfcD.addr);
#if HFC_REG_DEBUG
		if (cs->debug & L1_DEB_HSCX_FIFO && (data != 2))
			debugl1(cs, "t3c RD %02x %02x", reg, ret);
#endif
	} else
		ret = bytein(cs->hw.hfcD.addr | 1);
	return (ret);
}

static inline void
WriteReg(struct IsdnCardState *cs, int data, u8 reg, u8 value)
{
	if (cs->hw.hfcD.cip != reg) { 
		cs->hw.hfcD.cip = reg;
		byteout(cs->hw.hfcD.addr | 1, reg);
	}
	if (data)
		byteout(cs->hw.hfcD.addr, value);
#if HFC_REG_DEBUG
	if (cs->debug & L1_DEB_HSCX_FIFO && (data != HFCD_DATA_NODEB))
		debugl1(cs, "t3c W%c %02x %02x", data ? 'D' : 'C', reg, value);
#endif
}

static struct bc_hw_ops hfcs_bc_ops = {
	.read_reg  = ReadReg,
	.write_reg = WriteReg,
};

/* Interface functions */

static inline u8
hfcs_read_reg(struct IsdnCardState *cs, u8 offset)
{
	return ReadReg(cs, HFCD_DATA, offset);
}

static inline void
hfcs_write_reg(struct IsdnCardState *cs, u8 offset, u8 value)
{
	WriteReg(cs, HFCD_DATA, offset, value);
}

void
set_cs_func(struct IsdnCardState *cs)
{
	cs->bc_hw_ops = &hfcs_bc_ops;
}

static inline int
WaitForBusy(struct IsdnCardState *cs)
{
	int to = 130;

	while (!(ReadReg(cs, HFCD_DATA, HFCD_STAT) & HFCD_BUSY) && to) {
		udelay(1);
		to--;
	}
	if (!to)
		printk(KERN_WARNING "HiSax: WaitForBusy timeout\n");
	return (to);
}

static inline int
WaitNoBusy(struct IsdnCardState *cs)
{
	int to = 130;

	while ((ReadReg(cs, HFCD_STATUS, HFCD_STATUS) & HFCD_BUSY) && to) {
		udelay(1);
		to--;
	}
	if (!to) 
		printk(KERN_WARNING "HiSax: WaitNoBusy timeout\n");
	return (to);
}

static int
SelFiFo(struct IsdnCardState *cs, u8 FiFo)
{
	u8 cip;

	if (cs->hw.hfcD.fifo == FiFo)
		return(1);

	switch(FiFo) {
		case 0: cip = HFCB_FIFO | HFCB_Z1 | HFCB_SEND | HFCB_B1;
			break;
		case 1: cip = HFCB_FIFO | HFCB_Z1 | HFCB_REC | HFCB_B1;
			break;
		case 2: cip = HFCB_FIFO | HFCB_Z1 | HFCB_SEND | HFCB_B2;
			break;
		case 3: cip = HFCB_FIFO | HFCB_Z1 | HFCB_REC | HFCB_B2;
			break;
		case 4: cip = HFCD_FIFO | HFCD_Z1 | HFCD_SEND;
			break;
		case 5: cip = HFCD_FIFO | HFCD_Z1 | HFCD_REC;
			break;
		default:
			debugl1(cs, "SelFiFo Error");
			return(0);
	}
	cs->hw.hfcD.fifo = FiFo;
	WaitNoBusy(cs);
	WriteReg(cs, HFCD_DATA, cip, 0);
	WaitForBusy(cs);
	return(2);
}
static int
GetFreeFifoBytes_B(struct BCState *bcs)
{
	int s;

	if (bcs->hw.hfc.f1 == bcs->hw.hfc.f2)
		return (bcs->cs->hw.hfcD.bfifosize);
	s = bcs->hw.hfc.send[bcs->hw.hfc.f1] - bcs->hw.hfc.send[bcs->hw.hfc.f2];
	if (s <= 0)
		s += bcs->cs->hw.hfcD.bfifosize;
	s = bcs->cs->hw.hfcD.bfifosize - s;
	return (s);
}

static int
GetFreeFifoBytes_D(struct IsdnCardState *cs)
{
	int s;

	if (cs->hw.hfcD.f1 == cs->hw.hfcD.f2)
		return (cs->hw.hfcD.dfifosize);
	s = cs->hw.hfcD.send[cs->hw.hfcD.f1] - cs->hw.hfcD.send[cs->hw.hfcD.f2];
	if (s <= 0)
		s += cs->hw.hfcD.dfifosize;
	s = cs->hw.hfcD.dfifosize - s;
	return (s);
}

static int
ReadZReg(struct IsdnCardState *cs, u8 reg)
{
	int val;

	WaitNoBusy(cs);
	val = 256 * ReadReg(cs, HFCD_DATA, reg | HFCB_Z_HIGH);
	WaitNoBusy(cs);
	val += ReadReg(cs, HFCD_DATA, reg | HFCB_Z_LOW);
	return (val);
}

static struct sk_buff
*hfc_empty_fifo(struct BCState *bcs, int count)
{
	u8 *ptr;
	struct sk_buff *skb;
	struct IsdnCardState *cs = bcs->cs;
	int idx;
	int chksum;
	u8 stat, cip;
	
	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "hfc_empty_fifo");
	idx = 0;
	if (count > HSCX_BUFMAX + 3) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfc_empty_fifo: incoming packet too large");
		cip = HFCB_FIFO | HFCB_FIFO_OUT | HFCB_REC | HFCB_CHANNEL(bcs->channel);
		while (idx++ < count) {
			WaitNoBusy(cs);
			ReadReg(cs, HFCD_DATA_NODEB, cip);
		}
		skb = NULL;
	} else if (count < 4) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "hfc_empty_fifo: incoming packet too small");
		cip = HFCB_FIFO | HFCB_FIFO_OUT | HFCB_REC | HFCB_CHANNEL(bcs->channel);
#ifdef ERROR_STATISTIC
		bcs->err_inv++;
#endif
		while ((idx++ < count) && WaitNoBusy(cs))
			ReadReg(cs, HFCD_DATA_NODEB, cip);
		skb = NULL;
	} else if (!(skb = dev_alloc_skb(count - 3)))
		printk(KERN_WARNING "HFC: receive out of memory\n");
	else {
		ptr = skb_put(skb, count - 3);
		idx = 0;
		cip = HFCB_FIFO | HFCB_FIFO_OUT | HFCB_REC | HFCB_CHANNEL(bcs->channel);
		while (idx < (count - 3)) {
			if (!WaitNoBusy(cs))
				break;
			*ptr = ReadReg(cs,  HFCD_DATA_NODEB, cip);
			ptr++;
			idx++;
		}
		if (idx != count - 3) {
			debugl1(cs, "RFIFO BUSY error");
			printk(KERN_WARNING "HFC FIFO channel %d BUSY Error\n", bcs->channel);
			dev_kfree_skb_irq(skb);
			skb = NULL;
		} else {
			WaitNoBusy(cs);
			chksum = (ReadReg(cs, HFCD_DATA, cip) << 8);
			WaitNoBusy(cs);
			chksum += ReadReg(cs, HFCD_DATA, cip);
			WaitNoBusy(cs);
			stat = ReadReg(cs, HFCD_DATA, cip);
			if (cs->debug & L1_DEB_HSCX)
				debugl1(cs, "hfc_empty_fifo %d chksum %x stat %x",
					bcs->channel, chksum, stat);
			if (stat) {
				debugl1(cs, "FIFO CRC error");
				dev_kfree_skb_irq(skb);
				skb = NULL;
#ifdef ERROR_STATISTIC
				bcs->err_crc++;
#endif
			}
		}
	}
	WaitForBusy(cs);
	WaitNoBusy(cs);
	stat = ReadReg(cs, HFCD_DATA, HFCB_FIFO | HFCB_F2_INC |
		HFCB_REC | HFCB_CHANNEL(bcs->channel));
	WaitForBusy(cs);
	return (skb);
}

static void
hfc_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int idx, fcnt;
	int count;
	u8 cip;

	if (!bcs->tx_skb)
		return;
	if (bcs->tx_skb->len <= 0)
		return;
	SelFiFo(cs, HFCB_SEND | HFCB_CHANNEL(bcs->channel)); 
	cip = HFCB_FIFO | HFCB_F1 | HFCB_SEND | HFCB_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	bcs->hw.hfc.f1 = ReadReg(cs, HFCD_DATA, cip);
	WaitNoBusy(cs);
	cip = HFCB_FIFO | HFCB_F2 | HFCB_SEND | HFCB_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	bcs->hw.hfc.f2 = ReadReg(cs, HFCD_DATA, cip);
	bcs->hw.hfc.send[bcs->hw.hfc.f1] = ReadZReg(cs, HFCB_FIFO | HFCB_Z1 | HFCB_SEND | HFCB_CHANNEL(bcs->channel));
 	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hfc_fill_fifo %d f1(%d) f2(%d) z1(%x)",
			bcs->channel, bcs->hw.hfc.f1, bcs->hw.hfc.f2,
			bcs->hw.hfc.send[bcs->hw.hfc.f1]);
	fcnt = bcs->hw.hfc.f1 - bcs->hw.hfc.f2;
	if (fcnt < 0)
		fcnt += 32;
	if (fcnt > 30) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc_fill_fifo more as 30 frames");
		return;
	}
	count = GetFreeFifoBytes_B(bcs);
	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "hfc_fill_fifo %d count(%ld/%d),%lx",
			bcs->channel, bcs->tx_skb->len,
			count, current->state);
	if (count < bcs->tx_skb->len) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc_fill_fifo no fifo mem");
		return;
	}
	cip = HFCB_FIFO | HFCB_FIFO_IN | HFCB_SEND | HFCB_CHANNEL(bcs->channel);
	idx = 0;
	WaitForBusy(cs);
	WaitNoBusy(cs);
	WriteReg(cs, HFCD_DATA_NODEB, cip, bcs->tx_skb->data[idx++]);
	while (idx < bcs->tx_skb->len) {
		if (!WaitNoBusy(cs))
			break;
		WriteReg(cs, HFCD_DATA_NODEB, cip, bcs->tx_skb->data[idx]);
		idx++;
	}
	if (idx != bcs->tx_skb->len) {
		debugl1(cs, "FIFO Send BUSY error");
		printk(KERN_WARNING "HFC S FIFO channel %d BUSY Error\n", bcs->channel);
	} else {
		bcs->tx_cnt -= bcs->tx_skb->len;
		xmit_complete_b(bcs);
	}
	WaitForBusy(cs);
	WaitNoBusy(cs);
	ReadReg(cs, HFCD_DATA, HFCB_FIFO | HFCB_F1_INC | HFCB_SEND | HFCB_CHANNEL(bcs->channel));
	WaitForBusy(cs);
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	return;
}

void
main_rec_2bds0(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int z1, z2, rcnt;
	u8 f1, f2, cip;
	int receive, count = 5;
	struct sk_buff *skb;

    Begin:
	count--;
	SelFiFo(cs, HFCB_REC | HFCB_CHANNEL(bcs->channel));
	cip = HFCB_FIFO | HFCB_F1 | HFCB_REC | HFCB_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	f1 = ReadReg(cs, HFCD_DATA, cip);
	cip = HFCB_FIFO | HFCB_F2 | HFCB_REC | HFCB_CHANNEL(bcs->channel);
	WaitNoBusy(cs);
	f2 = ReadReg(cs, HFCD_DATA, cip);
	if (f1 != f2) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc rec %d f1(%d) f2(%d)",
				bcs->channel, f1, f2);
		z1 = ReadZReg(cs, HFCB_FIFO | HFCB_Z1 | HFCB_REC | HFCB_CHANNEL(bcs->channel));
		z2 = ReadZReg(cs, HFCB_FIFO | HFCB_Z2 | HFCB_REC | HFCB_CHANNEL(bcs->channel));
		rcnt = z1 - z2;
		if (rcnt < 0)
			rcnt += cs->hw.hfcD.bfifosize;
		rcnt++;
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc rec %d z1(%x) z2(%x) cnt(%d)",
				bcs->channel, z1, z2, rcnt);
		if ((skb = hfc_empty_fifo(bcs, rcnt))) {
			skb_queue_tail(&bcs->rqueue, skb);
			sched_b_event(bcs, B_RCVBUFREADY);
		}
		rcnt = f1 -f2;
		if (rcnt<0)
			rcnt += 32;
		if (rcnt>1)
			receive = 1;
		else
			receive = 0;
	} else
		receive = 0;
	if (count && receive)
		goto Begin;	
	return;
}

void
mode_2bs0(struct BCState *bcs, int mode, int bc)
{
	struct IsdnCardState *cs = bcs->cs;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "HFCD bchannel mode %d bchan %d/%d",
			mode, bc, bcs->channel);
	bcs->mode = mode;
	bcs->channel = bc;
	switch (mode) {
		case (L1_MODE_NULL):
			if (bc) {
				cs->hw.hfcD.conn |= 0x18;
				cs->hw.hfcD.sctrl &= ~SCTRL_B2_ENA;
			} else {
				cs->hw.hfcD.conn |= 0x3;
				cs->hw.hfcD.sctrl &= ~SCTRL_B1_ENA;
			}
			break;
		case (L1_MODE_TRANS):
			if (bc) {
				cs->hw.hfcD.ctmt |= 2;
				cs->hw.hfcD.conn &= ~0x18;
				cs->hw.hfcD.sctrl |= SCTRL_B2_ENA;
			} else {
				cs->hw.hfcD.ctmt |= 1;
				cs->hw.hfcD.conn &= ~0x3;
				cs->hw.hfcD.sctrl |= SCTRL_B1_ENA;
			}
			break;
		case (L1_MODE_HDLC):
			if (bc) {
				cs->hw.hfcD.ctmt &= ~2;
				cs->hw.hfcD.conn &= ~0x18;
				cs->hw.hfcD.sctrl |= SCTRL_B2_ENA;
			} else {
				cs->hw.hfcD.ctmt &= ~1;
				cs->hw.hfcD.conn &= ~0x3;
				cs->hw.hfcD.sctrl |= SCTRL_B1_ENA;
			}
			break;
	}
	WriteReg(cs, HFCD_DATA, HFCD_SCTRL, cs->hw.hfcD.sctrl);
	WriteReg(cs, HFCD_DATA, HFCD_CTMT, cs->hw.hfcD.ctmt);
	WriteReg(cs, HFCD_DATA, HFCD_CONN, cs->hw.hfcD.conn);
}

static void
hfc_l2l1(struct PStack *st, int pr, void *arg)
{
	struct sk_buff *skb = arg;

	switch (pr) {
		case (PH_DATA | REQUEST):
			xmit_data_req_b(st->l1.bcs, skb);
			break;
		case (PH_PULL | INDICATION):
			xmit_pull_ind_b(st->l1.bcs, skb);
			break;
		case (PH_PULL | REQUEST):
			xmit_pull_req_b(st);
			break;
		case (PH_ACTIVATE | REQUEST):
			test_and_set_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			mode_2bs0(st->l1.bcs, st->l1.mode, st->l1.bc);
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			mode_2bs0(st->l1.bcs, 0, st->l1.bc);
			L1L2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

void
close_2bs0(struct BCState *bcs)
{
	mode_2bs0(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_purge(&bcs->rqueue);
		skb_queue_purge(&bcs->squeue);
		if (bcs->tx_skb) {
			dev_kfree_skb_any(bcs->tx_skb);
			bcs->tx_skb = NULL;
			test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
		}
	}
}

static int
open_hfcstate(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->tx_cnt = 0;
	return (0);
}

int
setstack_2b(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_hfcstate(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l1.l2l1 = hfc_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

static void
hfcd_bh(void *data)
{
	struct IsdnCardState *cs = data;
/*	struct PStack *stptr;
*/
	if (!cs)
		return;
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event)) {
		switch (cs->dc.hfcd.ph_state) {
			case (0):
				l1_msg(cs, HW_RESET | INDICATION, NULL);
				break;
			case (3):
				l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
				break;
			case (8):
				l1_msg(cs, HW_RSYNC | INDICATION, NULL);
				break;
			case (6):
				l1_msg(cs, HW_INFO2 | INDICATION, NULL);
				break;
			case (7):
				l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
				break;
			default:
				break;
		}
	}
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
}

static
int receive_dmsg(struct IsdnCardState *cs)
{
	struct sk_buff *skb;
	int idx;
	int rcnt, z1, z2;
	u8 stat, cip, f1, f2;
	int chksum;
	int count=5;
	u8 *ptr;

	SelFiFo(cs, 4 | HFCD_REC);
	cip = HFCD_FIFO | HFCD_F1 | HFCD_REC;
	WaitNoBusy(cs);
	f1 = hfcs_read_reg(cs, cip) & 0xf;
	cip = HFCD_FIFO | HFCD_F2 | HFCD_REC;
	WaitNoBusy(cs);
	f2 = hfcs_read_reg(cs, cip) & 0xf;
	while ((f1 != f2) && count--) {
		z1 = ReadZReg(cs, HFCD_FIFO | HFCD_Z1 | HFCD_REC);
		z2 = ReadZReg(cs, HFCD_FIFO | HFCD_Z2 | HFCD_REC);
		rcnt = z1 - z2;
		if (rcnt < 0)
			rcnt += cs->hw.hfcD.dfifosize;
		rcnt++;
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfcd recd f1(%d) f2(%d) z1(%x) z2(%x) cnt(%d)",
				f1, f2, z1, z2, rcnt);
		idx = 0;
		cip = HFCD_FIFO | HFCD_FIFO_OUT | HFCD_REC;
		if (rcnt > MAX_DFRAME_LEN + 3) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "empty_fifo d: incoming packet too large");
			while (idx < rcnt) {
				if (!(WaitNoBusy(cs)))
					break;
				ReadReg(cs, HFCD_DATA_NODEB, cip);
				idx++;
			}
		} else if (rcnt < 4) {
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "empty_fifo d: incoming packet too small");
			while ((idx++ < rcnt) && WaitNoBusy(cs))
				ReadReg(cs, HFCD_DATA_NODEB, cip);
		} else if ((skb = dev_alloc_skb(rcnt - 3))) {
			ptr = skb_put(skb, rcnt - 3);
			while (idx < (rcnt - 3)) {
				if (!(WaitNoBusy(cs)))
					break;
				*ptr = ReadReg(cs, HFCD_DATA_NODEB, cip);
				idx++;
				ptr++;
			}
			if (idx != (rcnt - 3)) {
				debugl1(cs, "RFIFO D BUSY error");
				printk(KERN_WARNING "HFC DFIFO channel BUSY Error\n");
				dev_kfree_skb_irq(skb);
				skb = NULL;
#ifdef ERROR_STATISTIC
				cs->err_rx++;
#endif
			} else {
				WaitNoBusy(cs);
				chksum = (ReadReg(cs, HFCD_DATA, cip) << 8);
				WaitNoBusy(cs);
				chksum += ReadReg(cs, HFCD_DATA, cip);
				WaitNoBusy(cs);
				stat = ReadReg(cs, HFCD_DATA, cip);
				if (cs->debug & L1_DEB_ISAC)
					debugl1(cs, "empty_dfifo chksum %x stat %x",
						chksum, stat);
				if (stat) {
					debugl1(cs, "FIFO CRC error");
					dev_kfree_skb_irq(skb);
					skb = NULL;
#ifdef ERROR_STATISTIC
					cs->err_crc++;
#endif
				} else {
					skb_queue_tail(&cs->rq, skb);
					sched_d_event(cs, D_RCVBUFREADY);
				}
			}
		} else
			printk(KERN_WARNING "HFC: D receive out of memory\n");
		WaitForBusy(cs);
		cip = HFCD_FIFO | HFCD_F2_INC | HFCD_REC;
		WaitNoBusy(cs);
		stat = ReadReg(cs, HFCD_DATA, cip);
		WaitForBusy(cs);
		cip = HFCD_FIFO | HFCD_F2 | HFCD_REC;
		WaitNoBusy(cs);
		f2 = hfcs_read_reg(cs, cip) & 0xf;
	}
	return(1);
} 

static void
hfc_fill_dfifo(struct IsdnCardState *cs)
{
	int idx, fcnt;
	int count;
	u8 cip;

	if (!cs->tx_skb)
		return;
	if (cs->tx_skb->len <= 0)
		return;

	SelFiFo(cs, 4 | HFCD_SEND);
	cip = HFCD_FIFO | HFCD_F1 | HFCD_SEND;
	WaitNoBusy(cs);
	cs->hw.hfcD.f1 = ReadReg(cs, HFCD_DATA, cip) & 0xf;
	WaitNoBusy(cs);
	cip = HFCD_FIFO | HFCD_F2 | HFCD_SEND;
	cs->hw.hfcD.f2 = ReadReg(cs, HFCD_DATA, cip) & 0xf;
	cs->hw.hfcD.send[cs->hw.hfcD.f1] = ReadZReg(cs, HFCD_FIFO | HFCD_Z1 | HFCD_SEND);
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "hfc_fill_Dfifo f1(%d) f2(%d) z1(%x)",
			cs->hw.hfcD.f1, cs->hw.hfcD.f2,
			cs->hw.hfcD.send[cs->hw.hfcD.f1]);
	fcnt = cs->hw.hfcD.f1 - cs->hw.hfcD.f2;
	if (fcnt < 0)
		fcnt += 16;
	if (fcnt > 14) {
		if (cs->debug & L1_DEB_HSCX)
			debugl1(cs, "hfc_fill_Dfifo more as 14 frames");
		return;
	}
	count = GetFreeFifoBytes_D(cs);
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "hfc_fill_Dfifo count(%ld/%d)",
			cs->tx_skb->len, count);
	if (count < cs->tx_skb->len) {
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "hfc_fill_Dfifo no fifo mem");
		return;
	}
	cip = HFCD_FIFO | HFCD_FIFO_IN | HFCD_SEND;
	idx = 0;
	WaitForBusy(cs);
	WaitNoBusy(cs);
	WriteReg(cs, HFCD_DATA_NODEB, cip, cs->tx_skb->data[idx++]);
	while (idx < cs->tx_skb->len) {
		if (!(WaitNoBusy(cs)))
			break;
		WriteReg(cs, HFCD_DATA_NODEB, cip, cs->tx_skb->data[idx]);
		idx++;
	}
	if (idx != cs->tx_skb->len) {
		debugl1(cs, "DFIFO Send BUSY error");
		printk(KERN_WARNING "HFC S DFIFO channel BUSY Error\n");
	}
	WaitForBusy(cs);
	WaitNoBusy(cs);
	ReadReg(cs, HFCD_DATA, HFCD_FIFO | HFCD_F1_INC | HFCD_SEND);
	dev_kfree_skb_any(cs->tx_skb);
	cs->tx_skb = NULL;
	WaitForBusy(cs);
	return;
}

static 
struct BCState *Sel_BCS(struct IsdnCardState *cs, int channel)
{
	if (cs->bcs[0].mode && (cs->bcs[0].channel == channel))
		return(&cs->bcs[0]);
	else if (cs->bcs[1].mode && (cs->bcs[1].channel == channel))
		return(&cs->bcs[1]);
	else
		return(NULL);
}

void
hfc2bds0_interrupt(struct IsdnCardState *cs, u8 val)
{
       	u8 exval;
       	struct BCState *bcs;
	int count=15;

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "HFCD irq %x", val);

	val &= cs->hw.hfcD.int_m1;
	if (val & 0x40) { /* TE state machine irq */
		exval = hfcs_read_reg(cs, HFCD_STATES) & 0xf;
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ph_state chg %d->%d", cs->dc.hfcd.ph_state,
				exval);
		cs->dc.hfcd.ph_state = exval;
		sched_d_event(cs, D_L1STATECHANGE);
		val &= ~0x40;
	}
	while (val) {
		if (cs->hw.hfcD.int_s1 & 0x18) {
			exval = val;
			val =  cs->hw.hfcD.int_s1;
			cs->hw.hfcD.int_s1 = exval;
		}	
		if (val & 0x08) {
			if (!(bcs=Sel_BCS(cs, 0))) {
				if (cs->debug)
					debugl1(cs, "hfcd spurious 0x08 IRQ");
			} else 
				main_rec_2bds0(bcs);
		}
		if (val & 0x10) {
			if (!(bcs=Sel_BCS(cs, 1))) {
				if (cs->debug)
					debugl1(cs, "hfcd spurious 0x10 IRQ");
			} else 
				main_rec_2bds0(bcs);
		}
		if (val & 0x01) {
			if (!(bcs=Sel_BCS(cs, 0))) {
				if (cs->debug)
					debugl1(cs, "hfcd spurious 0x01 IRQ");
			} else {
				xmit_xpr_b(bcs);
			}
		}
		if (val & 0x02) {
			if (!(bcs=Sel_BCS(cs, 1))) {
				if (cs->debug)
					debugl1(cs, "hfcd spurious 0x02 IRQ");
			} else {
				xmit_xpr_b(bcs);
			}
		}
		if (val & 0x20) {	/* receive dframe */
			receive_dmsg(cs);
		}
		if (val & 0x04) {	/* dframe transmitted */
			xmit_xpr_d(cs);
		}
		if (cs->hw.hfcD.int_s1 && count--) {
			val = cs->hw.hfcD.int_s1;
			cs->hw.hfcD.int_s1 = 0;
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "HFCD irq %x loop %d", val, 15-count);
		} else
			val = 0;
	}
}

static void
HFCD_l1hw(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	
	switch (pr) {
		case (PH_DATA | REQUEST):
			xmit_data_req_d(cs, skb);
			break;
		case (PH_PULL |INDICATION):
			xmit_pull_ind_d(cs, skb);
			break;
		case (PH_PULL | REQUEST):
			xmit_pull_req_d(st);
			break;
		case (HW_RESET | REQUEST):
			hfcs_write_reg(cs, HFCD_STATES, HFCD_LOAD_STATE | 3); /* HFC ST 3 */
			udelay(6);
			hfcs_write_reg(cs, HFCD_STATES, 3); /* HFC ST 2 */
			cs->hw.hfcD.mst_m |= HFCD_MASTER;
			hfcs_write_reg(cs, HFCD_MST_MODE, cs->hw.hfcD.mst_m);
			hfcs_write_reg(cs, HFCD_STATES, HFCD_ACTIVATE | HFCD_DO_ACTION);
			l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
			break;
		case (HW_ENABLE | REQUEST):
			hfcs_write_reg(cs, HFCD_STATES, HFCD_ACTIVATE | HFCD_DO_ACTION);
			break;
		case (HW_DEACTIVATE | REQUEST):
			cs->hw.hfcD.mst_m &= ~HFCD_MASTER;
			hfcs_write_reg(cs, HFCD_MST_MODE, cs->hw.hfcD.mst_m);
			break;
		case (HW_INFO3 | REQUEST):
			cs->hw.hfcD.mst_m |= HFCD_MASTER;
			hfcs_write_reg(cs, HFCD_MST_MODE, cs->hw.hfcD.mst_m);
			break;
		default:
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "hfcd_l1hw unknown pr %4x", pr);
			break;
	}
}

void
setstack_hfcd(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = HFCD_l1hw;
}

static void
hfc_dbusy_timer(struct IsdnCardState *cs)
{
}

unsigned int __init
*init_send_hfcd(int cnt)
{
	int i, *send;

	if (!(send = kmalloc(cnt * sizeof(unsigned int), GFP_ATOMIC))) {
		printk(KERN_WARNING
		       "HiSax: No memory for hfcd.send\n");
		return(NULL);
	}
	for (i = 0; i < cnt; i++)
		send[i] = 0x1fff;
	return(send);
}

void __init
init2bds0(struct IsdnCardState *cs)
{
	cs->setstack_d = setstack_hfcd;
	cs->dbusytimer.function = (void *) hfc_dbusy_timer;
	cs->dbusytimer.data = (long) cs;
	init_timer(&cs->dbusytimer);
	INIT_WORK(&cs->work, hfcd_bh, cs);
	if (!cs->hw.hfcD.send)
		cs->hw.hfcD.send = init_send_hfcd(16);
	if (!cs->bcs[0].hw.hfc.send)
		cs->bcs[0].hw.hfc.send = init_send_hfcd(32);
	if (!cs->bcs[1].hw.hfc.send)
		cs->bcs[1].hw.hfc.send = init_send_hfcd(32);
	cs->BC_Send_Data = hfc_fill_fifo;
	cs->DC_Send_Data = hfc_fill_dfifo;
	cs->bcs[0].BC_SetStack = setstack_2b;
	cs->bcs[1].BC_SetStack = setstack_2b;
	cs->bcs[0].BC_Close = close_2bs0;
	cs->bcs[1].BC_Close = close_2bs0;
	mode_2bs0(cs->bcs, 0, 0);
	mode_2bs0(cs->bcs + 1, 0, 1);
}

void
release2bds0(struct IsdnCardState *cs)
{
	if (cs->bcs[0].hw.hfc.send) {
		kfree(cs->bcs[0].hw.hfc.send);
		cs->bcs[0].hw.hfc.send = NULL;
	}
	if (cs->bcs[1].hw.hfc.send) {
		kfree(cs->bcs[1].hw.hfc.send);
		cs->bcs[1].hw.hfc.send = NULL;
	}
	if (cs->hw.hfcD.send) {
		kfree(cs->hw.hfcD.send);
		cs->hw.hfcD.send = NULL;
	}
}
