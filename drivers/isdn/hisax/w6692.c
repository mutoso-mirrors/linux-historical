/* $Id: w6692.c,v 1.12.6.6 2001/09/23 22:24:52 kai Exp $
 *
 * Winbond W6692 specific routines
 *
 * Author       Petr Novak
 * Copyright    by Petr Novak        <petr.novak@i.cz>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#include <linux/config.h>
#include <linux/init.h>
#include "hisax.h"
#include "w6692.h"
#include "isdnl1.h"
#include <linux/interrupt.h>
#include <linux/pci.h>

/* table entry in the PCI devices list */
typedef struct {
	int vendor_id;
	int device_id;
	char *vendor_name;
	char *card_name;
} PCI_ENTRY;

static const PCI_ENTRY id_list[] =
{
	{PCI_VENDOR_ID_WINBOND2, PCI_DEVICE_ID_WINBOND2_6692, "Winbond", "W6692"},
	{PCI_VENDOR_ID_DYNALINK, PCI_DEVICE_ID_DYNALINK_IS64PH, "Dynalink/AsusCom", "IS64PH"},
	{0, 0, "U.S.Robotics", "ISDN PCI Card TA"}
};

#define W6692_SV_USR   0x16ec
#define W6692_SD_USR   0x3409
#define W6692_WINBOND  0
#define W6692_DYNALINK 1
#define W6692_USR      2

extern const char *CardType[];

const char *w6692_revision = "$Revision: 1.12.6.6 $";

#define DBUSY_TIMER_VALUE 80

static char *W6692Ver[] __initdata =
{"W6692 V00", "W6692 V01", "W6692 V10",
 "W6692 V11"};

static void
W6692Version(struct IsdnCardState *cs, char *s)
{
	int val;

	val = cs->readW6692(cs, W_D_RBCH);
	printk(KERN_INFO "%s Winbond W6692 version (%x): %s\n", s, val, W6692Ver[(val >> 6) & 3]);
}

static void
ph_command(struct IsdnCardState *cs, unsigned int command)
{
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "ph_command %x", command);
	cs->writeisac(cs, W_CIX, command);
}


static void
W6692_new_ph(struct IsdnCardState *cs)
{
	switch (cs->dc.w6692.ph_state) {
		case (W_L1CMD_RST):
			ph_command(cs, W_L1CMD_DRC);
			l1_msg(cs, HW_RESET | INDICATION, NULL);
			/* fallthru */
		case (W_L1IND_CD):
			l1_msg(cs, HW_DEACTIVATE | CONFIRM, NULL);
			break;
		case (W_L1IND_DRD):
			l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
			break;
		case (W_L1IND_CE):
			l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
			break;
		case (W_L1IND_LD):
			l1_msg(cs, HW_RSYNC | INDICATION, NULL);
			break;
		case (W_L1IND_ARD):
			l1_msg(cs, HW_INFO2 | INDICATION, NULL);
			break;
		case (W_L1IND_AI8):
			l1_msg(cs, HW_INFO4_P8 | INDICATION, NULL);
			break;
		case (W_L1IND_AI10):
			l1_msg(cs, HW_INFO4_P10 | INDICATION, NULL);
			break;
		default:
			break;
	}
}

static void
W6692_bh(void *data)
{
	struct IsdnCardState *cs = data;
	struct PStack *stptr;

	if (!cs)
		return;
	if (test_and_clear_bit(D_CLEARBUSY, &cs->event)) {
		if (cs->debug)
			debugl1(cs, "D-Channel Busy cleared");
		stptr = cs->stlist;
		while (stptr != NULL) {
			L1L2(stptr, PH_PAUSE | CONFIRM, NULL);
			stptr = stptr->next;
		}
	}
	if (test_and_clear_bit(D_L1STATECHANGE, &cs->event))
		W6692_new_ph(cs);
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
/*
   if (test_and_clear_bit(D_RX_MON1, &cs->event))
   arcofi_fsm(cs, ARCOFI_RX_END, NULL);
   if (test_and_clear_bit(D_TX_MON1, &cs->event))
   arcofi_fsm(cs, ARCOFI_TX_END, NULL);
 */
}

static void
W6692_empty_fifo(struct IsdnCardState *cs, int count)
{
	u_char *ptr;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "W6692_empty_fifo");

	if ((cs->rcvidx + count) >= MAX_DFRAME_LEN_L1) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "W6692_empty_fifo overrun %d",
				cs->rcvidx + count);
		cs->writeW6692(cs, W_D_CMDR, W_D_CMDR_RACK);
		cs->rcvidx = 0;
		return;
	}
	ptr = cs->rcvbuf + cs->rcvidx;
	cs->rcvidx += count;
	cs->readW6692fifo(cs, ptr, count);
	cs->writeW6692(cs, W_D_CMDR, W_D_CMDR_RACK);
	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char *t = cs->dlog;

		t += sprintf(t, "W6692_empty_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(cs, cs->dlog);
	}
}

static void
W6692_fill_fifo(struct IsdnCardState *cs)
{
	int count, more;
	unsigned char *p;

	p = xmit_fill_fifo_d(cs, W_D_FIFO_THRESH, &count, &more);
	if (!p)
		return;

	cs->writeW6692fifo(cs, p, count);
	cs->writeW6692(cs, W_D_CMDR, more ? W_D_CMDR_XMS : (W_D_CMDR_XMS | W_D_CMDR_XME));
	if (test_and_set_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		debugl1(cs, "W6692_fill_fifo dbusytimer running");
		del_timer(&cs->dbusytimer);
	}
	init_timer(&cs->dbusytimer);
	cs->dbusytimer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ) / 1000);
	add_timer(&cs->dbusytimer);
}

static void
W6692B_empty_fifo(struct BCState *bcs, int count)
{
	u_char *ptr;
	struct IsdnCardState *cs = bcs->cs;

	if ((cs->debug & L1_DEB_HSCX) && !(cs->debug & L1_DEB_HSCX_FIFO))
		debugl1(cs, "W6692B_empty_fifo");

	if (bcs->hw.w6692.rcvidx + count > HSCX_BUFMAX) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "W6692B_empty_fifo: incoming packet too large");
		cs->BC_Write_Reg(cs, bcs->channel, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);
		bcs->hw.w6692.rcvidx = 0;
		return;
	}
	ptr = bcs->hw.w6692.rcvbuf + bcs->hw.w6692.rcvidx;
	bcs->hw.w6692.rcvidx += count;
	READW6692BFIFO(cs, bcs->channel, ptr, count);
	cs->BC_Write_Reg(cs, bcs->channel, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RACT);
	if (cs->debug & L1_DEB_HSCX_FIFO) {
		char *t = bcs->blog;

		t += sprintf(t, "W6692B_empty_fifo %c cnt %d",
			     bcs->channel + '1', count);
		QuickHex(t, ptr, count);
		debugl1(cs, bcs->blog);
	}
}

static void
W6692B_fill_fifo(struct BCState *bcs)
{
	struct IsdnCardState *cs = bcs->cs;
	int more, count;
	unsigned char *p;

	p = xmit_fill_fifo_b(bcs, W_B_FIFO_THRESH, &count, &more);
	if (!p)
		return;

	WRITEW6692BFIFO(cs, bcs->channel, p, count);
	cs->BC_Write_Reg(cs, bcs->channel, W_B_CMDR, W_B_CMDR_RACT | W_B_CMDR_XMS | (more ? 0 : W_B_CMDR_XME));
}

static void
reset_xmit(struct BCState *bcs)
{
	bcs->cs->BC_Write_Reg(bcs->cs, bcs->channel, W_B_CMDR,
			      W_B_CMDR_XRST | W_B_CMDR_RACT);
}

static void
W6692B_interrupt(struct IsdnCardState *cs, u_char bchan)
{
	u_char val;
	u_char r;
	struct BCState *bcs;
	struct sk_buff *skb;
	int count;

	bcs = (cs->bcs->channel == bchan) ? cs->bcs : (cs->bcs+1);
	val = cs->BC_Read_Reg(cs, bchan, W_B_EXIR);
	debugl1(cs, "W6692B chan %d B_EXIR 0x%02X", bchan, val);

	if (!test_bit(BC_FLG_INIT, &bcs->Flag)) {
		debugl1(cs, "W6692B not INIT yet");
		return;
	}
	if (val & W_B_EXI_RME) {	/* RME */
		r = cs->BC_Read_Reg(cs, bchan, W_B_STAR);
		if (r & (W_B_STAR_RDOV | W_B_STAR_CRCE | W_B_STAR_RMB | W_B_STAR_XDOW)) {
			if ((r & W_B_STAR_RDOV) && bcs->mode)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "W6692 B RDOV mode=%d",
						bcs->mode);
			if (r & W_B_STAR_CRCE)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "W6692 B CRC error");
			cs->BC_Write_Reg(cs, bchan, W_B_CMDR, W_B_CMDR_RACK | W_B_CMDR_RRST | W_B_CMDR_RACT);
		} else {
			count = cs->BC_Read_Reg(cs, bchan, W_B_RBCL) & (W_B_FIFO_THRESH - 1);
			if (count == 0)
				count = W_B_FIFO_THRESH;
			W6692B_empty_fifo(bcs, count);
			if ((count = bcs->hw.w6692.rcvidx) > 0) {
				if (cs->debug & L1_DEB_HSCX_FIFO)
					debugl1(cs, "W6692 Bchan Frame %d", count);
				if (!(skb = dev_alloc_skb(count)))
					printk(KERN_WARNING "W6692: Bchan receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), bcs->hw.w6692.rcvbuf, count);
					skb_queue_tail(&bcs->rqueue, skb);
				}
			}
		}
		bcs->hw.w6692.rcvidx = 0;
		sched_b_event(bcs, B_RCVBUFREADY);
	}
	if (val & W_B_EXI_RMR) {	/* RMR */
		W6692B_empty_fifo(bcs, W_B_FIFO_THRESH);
		if (bcs->mode == L1_MODE_TRANS) {
			/* receive audio data */
			if (!(skb = dev_alloc_skb(W_B_FIFO_THRESH)))
				printk(KERN_WARNING "HiSax: receive out of memory\n");
			else {
				memcpy(skb_put(skb, W_B_FIFO_THRESH), bcs->hw.w6692.rcvbuf, W_B_FIFO_THRESH);
				skb_queue_tail(&bcs->rqueue, skb);
			}
			bcs->hw.w6692.rcvidx = 0;
			sched_b_event(bcs, B_RCVBUFREADY);
		}
	}
	if (val & W_B_EXI_XFR) {	/* XFR */
		xmit_xpr_b(bcs);
	}
	if (val & W_B_EXI_XDUN) {	/* XDUN */
		xmit_xdu_b(bcs, reset_xmit);
	}
}

static void
W6692_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, exval, v1;
	struct sk_buff *skb;
	unsigned int count;
	int icnt = 5;

	spin_lock(&cs->lock);

	val = cs->readW6692(cs, W_ISTA);

      StartW6692:
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "W6692 ISTA %x", val);

	if (val & W_INT_D_RME) {	/* RME */
		exval = cs->readW6692(cs, W_D_RSTA);
		if (exval & (W_D_RSTA_RDOV | W_D_RSTA_CRCE | W_D_RSTA_RMB)) {
			if (exval & W_D_RSTA_RDOV)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "W6692 RDOV");
			if (exval & W_D_RSTA_CRCE)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "W6692 D-channel CRC error");
			if (exval & W_D_RSTA_RMB)
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "W6692 D-channel ABORT");
			cs->writeW6692(cs, W_D_CMDR, W_D_CMDR_RACK | W_D_CMDR_RRST);
		} else {
			count = cs->readW6692(cs, W_D_RBCL) & (W_D_FIFO_THRESH - 1);
			if (count == 0)
				count = W_D_FIFO_THRESH;
			W6692_empty_fifo(cs, count);
			if ((count = cs->rcvidx) > 0) {
				cs->rcvidx = 0;
				if (!(skb = alloc_skb(count, GFP_ATOMIC)))
					printk(KERN_WARNING "HiSax: D receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), cs->rcvbuf, count);
					skb_queue_tail(&cs->rq, skb);
				}
			}
		}
		cs->rcvidx = 0;
		sched_d_event(cs, D_RCVBUFREADY);
	}
	if (val & W_INT_D_RMR) {	/* RMR */
		W6692_empty_fifo(cs, W_D_FIFO_THRESH);
	}
	if (val & W_INT_D_XFR) {	/* XFR */
		xmit_xpr_d(cs);
	}
	if (val & (W_INT_XINT0 | W_INT_XINT1)) {	/* XINT0/1 - never */
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "W6692 spurious XINT!");
	}
	if (val & W_INT_D_EXI) {	/* EXI */
		exval = cs->readW6692(cs, W_D_EXIR);
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "W6692 D_EXIR %02x", exval);
		if (exval & (W_D_EXI_XDUN | W_D_EXI_XCOL)) {	/* Transmit underrun/collision */
			xmit_xdu_d(cs, NULL);
		}
		if (exval & W_D_EXI_RDOV) {	/* RDOV */
			debugl1(cs, "W6692 D-channel RDOV");
			printk(KERN_WARNING "HiSax: W6692 D-RDOV\n");
			cs->writeW6692(cs, W_D_CMDR, W_D_CMDR_RRST);
		}
		if (exval & W_D_EXI_TIN2) {	/* TIN2 - never */
			debugl1(cs, "W6692 spurious TIN2 interrupt");
		}
		if (exval & W_D_EXI_MOC) {	/* MOC - not supported */
			debugl1(cs, "W6692 spurious MOC interrupt");
			v1 = cs->readW6692(cs, W_MOSR);
			debugl1(cs, "W6692 MOSR %02x", v1);
		}
		if (exval & W_D_EXI_ISC) {	/* ISC - Level1 change */
			v1 = cs->readW6692(cs, W_CIR);
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "W6692 ISC CIR=0x%02X", v1);
			if (v1 & W_CIR_ICC) {
				cs->dc.w6692.ph_state = v1 & W_CIR_COD_MASK;
				if (cs->debug & L1_DEB_ISAC)
					debugl1(cs, "ph_state_change %x", cs->dc.w6692.ph_state);
				sched_d_event(cs, D_L1STATECHANGE);
			}
			if (v1 & W_CIR_SCC) {
				v1 = cs->readW6692(cs, W_SQR);
				debugl1(cs, "W6692 SCC SQR=0x%02X", v1);
			}
		}
		if (exval & W_D_EXI_WEXP) {
			debugl1(cs, "W6692 spurious WEXP interrupt!");
		}
		if (exval & W_D_EXI_TEXP) {
			debugl1(cs, "W6692 spurious TEXP interrupt!");
		}
	}
	if (val & W_INT_B1_EXI) {
		debugl1(cs, "W6692 B channel 1 interrupt");
		W6692B_interrupt(cs, 0);
	}
	if (val & W_INT_B2_EXI) {
		debugl1(cs, "W6692 B channel 2 interrupt");
		W6692B_interrupt(cs, 1);
	}
	val = cs->readW6692(cs, W_ISTA);
	if (val && icnt) {
		icnt--;
		goto StartW6692;
	}
	if (!icnt) {
		printk(KERN_WARNING "W6692 IRQ LOOP\n");
		cs->writeW6692(cs, W_IMASK, 0xff);
	}
	spin_unlock(&cs->lock);
}

static void
W6692_l1hw(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	int val;

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
			if ((cs->dc.w6692.ph_state == W_L1IND_DRD))
				ph_command(cs, W_L1CMD_ECK);
			else {
				ph_command(cs, W_L1CMD_RST);
				cs->dc.w6692.ph_state = W_L1CMD_RST;
				W6692_new_ph(cs);
			}
			break;
		case (HW_ENABLE | REQUEST):
			ph_command(cs, W_L1CMD_ECK);
			break;
		case (HW_INFO3 | REQUEST):
			ph_command(cs, W_L1CMD_AR8);
			break;
		case (HW_TESTLOOP | REQUEST):
			val = 0;
			if (1 & (long) arg)
				val |= 0x0c;
			if (2 & (long) arg)
				val |= 0x3;
			/* !!! not implemented yet */
			break;
		case (HW_DEACTIVATE | RESPONSE):
			skb_queue_purge(&cs->rq);
			skb_queue_purge(&cs->sq);
			if (cs->tx_skb) {
				dev_kfree_skb_any(cs->tx_skb);
				cs->tx_skb = NULL;
			}
			if (test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags))
				del_timer(&cs->dbusytimer);
			if (test_and_clear_bit(FLG_L1_DBUSY, &cs->HW_Flags))
				sched_d_event(cs, D_CLEARBUSY);
			break;
		default:
			if (cs->debug & L1_DEB_WARN)
				debugl1(cs, "W6692_l1hw unknown %04x", pr);
			break;
	}
}

static void
setstack_W6692(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = W6692_l1hw;
}

static void
DC_Close_W6692(struct IsdnCardState *cs)
{
}

static void
dbusy_timer_handler(struct IsdnCardState *cs)
{
	struct PStack *stptr;
	int rbch, star;

	if (test_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		rbch = cs->readW6692(cs, W_D_RBCH);
		star = cs->readW6692(cs, W_D_STAR);
		if (cs->debug)
			debugl1(cs, "D-Channel Busy D_RBCH %02x D_STAR %02x",
				rbch, star);
		if (star & W_D_STAR_XBZ) {	/* D-Channel Busy */
			test_and_set_bit(FLG_L1_DBUSY, &cs->HW_Flags);
			stptr = cs->stlist;
			while (stptr != NULL) {
				L1L2(stptr, PH_PAUSE | INDICATION, NULL);
				stptr = stptr->next;
			}
		} else {
			/* discard frame; reset transceiver */
			test_and_clear_bit(FLG_DBUSY_TIMER, &cs->HW_Flags);
			if (cs->tx_skb) {
				dev_kfree_skb_any(cs->tx_skb);
				cs->tx_cnt = 0;
				cs->tx_skb = NULL;
			} else {
				printk(KERN_WARNING "HiSax: W6692 D-Channel Busy no skb\n");
				debugl1(cs, "D-Channel Busy no skb");
			}
			cs->writeW6692(cs, W_D_CMDR, W_D_CMDR_XRST);	/* Transmitter reset */
			cs->irq_func(cs->irq, cs, NULL);
		}
	}
}

static void
W6692Bmode(struct BCState *bcs, int mode, int bchan)
{
	struct IsdnCardState *cs = bcs->cs;

	if (cs->debug & L1_DEB_HSCX)
		debugl1(cs, "w6692 %c mode %d ichan %d",
			'1' + bchan, mode, bchan);
	bcs->mode = mode;
	bcs->channel = bchan;
	bcs->hw.w6692.bchan = bchan;

	switch (mode) {
		case (L1_MODE_NULL):
			cs->BC_Write_Reg(cs, bchan, W_B_MODE, 0);
			break;
		case (L1_MODE_TRANS):
			cs->BC_Write_Reg(cs, bchan, W_B_MODE, W_B_MODE_MMS);
			break;
		case (L1_MODE_HDLC):
			cs->BC_Write_Reg(cs, bchan, W_B_MODE, W_B_MODE_ITF);
			cs->BC_Write_Reg(cs, bchan, W_B_ADM1, 0xff);
			cs->BC_Write_Reg(cs, bchan, W_B_ADM2, 0xff);
			break;
	}
	if (mode)
		cs->BC_Write_Reg(cs, bchan, W_B_CMDR, W_B_CMDR_RRST |
				 W_B_CMDR_RACT | W_B_CMDR_XRST);
	cs->BC_Write_Reg(cs, bchan, W_B_EXIM, 0x00);
}

static void
W6692_l2l1(struct PStack *st, int pr, void *arg)
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
			W6692Bmode(st->l1.bcs, st->l1.mode, st->l1.bc);
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | REQUEST):
			l1_msg_b(st, pr, arg);
			break;
		case (PH_DEACTIVATE | CONFIRM):
			test_and_clear_bit(BC_FLG_ACTIV, &st->l1.bcs->Flag);
			test_and_clear_bit(BC_FLG_BUSY, &st->l1.bcs->Flag);
			W6692Bmode(st->l1.bcs, 0, st->l1.bc);
			L1L2(st, PH_DEACTIVATE | CONFIRM, NULL);
			break;
	}
}

static void
close_w6692state(struct BCState *bcs)
{
	W6692Bmode(bcs, 0, bcs->channel);
	if (test_and_clear_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (bcs->hw.w6692.rcvbuf) {
			kfree(bcs->hw.w6692.rcvbuf);
			bcs->hw.w6692.rcvbuf = NULL;
		}
		if (bcs->blog) {
			kfree(bcs->blog);
			bcs->blog = NULL;
		}
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
open_w6692state(struct IsdnCardState *cs, struct BCState *bcs)
{
	if (!test_and_set_bit(BC_FLG_INIT, &bcs->Flag)) {
		if (!(bcs->hw.w6692.rcvbuf = kmalloc(HSCX_BUFMAX, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for w6692.rcvbuf\n");
			test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
			return (1);
		}
		if (!(bcs->blog = kmalloc(MAX_BLOG_SPACE, GFP_ATOMIC))) {
			printk(KERN_WARNING
			       "HiSax: No memory for bcs->blog\n");
			test_and_clear_bit(BC_FLG_INIT, &bcs->Flag);
			kfree(bcs->hw.w6692.rcvbuf);
			bcs->hw.w6692.rcvbuf = NULL;
			return (2);
		}
		skb_queue_head_init(&bcs->rqueue);
		skb_queue_head_init(&bcs->squeue);
	}
	bcs->tx_skb = NULL;
	test_and_clear_bit(BC_FLG_BUSY, &bcs->Flag);
	bcs->event = 0;
	bcs->hw.w6692.rcvidx = 0;
	bcs->tx_cnt = 0;
	return (0);
}

static int
setstack_w6692(struct PStack *st, struct BCState *bcs)
{
	bcs->channel = st->l1.bc;
	if (open_w6692state(st->l1.hardware, bcs))
		return (-1);
	st->l1.bcs = bcs;
	st->l1.l2l1 = W6692_l2l1;
	setstack_manager(st);
	bcs->st = st;
	setstack_l1_B(st);
	return (0);
}

void resetW6692(struct IsdnCardState *cs)
{
	cs->writeW6692(cs, W_D_CTL, W_D_CTL_SRST);
	schedule_timeout((10*HZ)/1000);
	cs->writeW6692(cs, W_D_CTL, 0x00);
	schedule_timeout((10*HZ)/1000);
	cs->writeW6692(cs, W_IMASK, 0xff);
	cs->writeW6692(cs, W_D_SAM, 0xff);
	cs->writeW6692(cs, W_D_TAM, 0xff);
	cs->writeW6692(cs, W_D_EXIM, 0x00);
	cs->writeW6692(cs, W_D_MODE, W_D_MODE_RACT);
	cs->writeW6692(cs, W_IMASK, 0x18);
	if (cs->subtyp == W6692_USR) {
		/* seems that USR implemented some power control features
		 * Pin 79 is connected to the oscilator circuit so we
		 * have to handle it here
		 */
		cs->writeW6692(cs, W_PCTL, 0x80);
		cs->writeW6692(cs, W_XDATA, 0x00);
	}
}

void __init initW6692(struct IsdnCardState *cs, int part)
{
	if (part & 1) {
		INIT_WORK(&cs->work, W6692_bh, cs);
		cs->setstack_d = setstack_W6692;
		cs->DC_Close = DC_Close_W6692;
		cs->dbusytimer.function = (void *) dbusy_timer_handler;
		cs->dbusytimer.data = (long) cs;
		init_timer(&cs->dbusytimer);
		resetW6692(cs);
		ph_command(cs, W_L1CMD_RST);
		cs->dc.w6692.ph_state = W_L1CMD_RST;
		W6692_new_ph(cs);
		ph_command(cs, W_L1CMD_ECK);

		cs->bcs[0].BC_SetStack = setstack_w6692;
		cs->bcs[1].BC_SetStack = setstack_w6692;
		cs->bcs[0].BC_Close = close_w6692state;
		cs->bcs[1].BC_Close = close_w6692state;
		W6692Bmode(cs->bcs, 0, 0);
		W6692Bmode(cs->bcs + 1, 0, 0);
	}
	if (part & 2) {
		/* Reenable all IRQ */
		cs->writeW6692(cs, W_IMASK, 0x18);
		cs->writeW6692(cs, W_D_EXIM, 0x00);
		cs->BC_Write_Reg(cs, 0, W_B_EXIM, 0x00);
		cs->BC_Write_Reg(cs, 1, W_B_EXIM, 0x00);
		/* Reset D-chan receiver and transmitter */
		cs->writeW6692(cs, W_D_CMDR, W_D_CMDR_RRST | W_D_CMDR_XRST);
	}
}

/* Interface functions */

static u_char
ReadW6692(struct IsdnCardState *cs, u_char offset)
{
	return (inb(cs->hw.w6692.iobase + offset));
}

static void
WriteW6692(struct IsdnCardState *cs, u_char offset, u_char value)
{
	outb(value, cs->hw.w6692.iobase + offset);
}

static void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	insb(cs->hw.w6692.iobase + W_D_RFIFO, data, size);
}

static void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	outsb(cs->hw.w6692.iobase + W_D_XFIFO, data, size);
}

static u_char
ReadW6692B(struct IsdnCardState *cs, int bchan, u_char offset)
{
	return (inb(cs->hw.w6692.iobase + (bchan ? 0x40 : 0) + offset));
}

static void
WriteW6692B(struct IsdnCardState *cs, int bchan, u_char offset, u_char value)
{
	outb(value, cs->hw.w6692.iobase + (bchan ? 0x40 : 0) + offset);
}

static int
w6692_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	switch (mt) {
		case CARD_RESET:
			resetW6692(cs);
			return (0);
		case CARD_RELEASE:
			cs->writeW6692(cs, W_IMASK, 0xff);
			release_region(cs->hw.w6692.iobase, 256);
			if (cs->subtyp == W6692_USR) {
				cs->writeW6692(cs, W_XDATA, 0x04);
			}
			return (0);
		case CARD_INIT:
			initW6692(cs, 3);
			return (0);
		case CARD_TEST:
			return (0);
	}
	return (0);
}

static int id_idx ;

static struct pci_dev *dev_w6692 __initdata = NULL;

int __init 
setup_w6692(struct IsdnCard *card)
{
	struct IsdnCardState *cs = card->cs;
	char tmp[64];
	u_char found = 0;
	u_char pci_irq = 0;
	u_int pci_ioaddr = 0;

#ifdef __BIG_ENDIAN
#error "not running on big endian machines now"
#endif
	strcpy(tmp, w6692_revision);
	printk(KERN_INFO "HiSax: W6692 driver Rev. %s\n", HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_W6692)
		return (0);
#if CONFIG_PCI
	if (!pci_present()) {
		printk(KERN_ERR "W6692: no PCI bus present\n");
		return (0);
	}
	while (id_list[id_idx].vendor_id) {
		dev_w6692 = pci_find_device(id_list[id_idx].vendor_id,
					    id_list[id_idx].device_id,
					    dev_w6692);
		if (dev_w6692) {
			if (pci_enable_device(dev_w6692))
				continue;
			cs->subtyp = id_idx;
			break;
		}
		id_idx++;
	}
	if (dev_w6692) {
		found = 1;
		pci_irq = dev_w6692->irq;
		/* I think address 0 is allways the configuration area */
		/* and address 1 is the real IO space KKe 03.09.99 */
		pci_ioaddr = pci_resource_start(dev_w6692, 1);
		/* USR ISDN PCI card TA need some special handling */
		if (cs->subtyp == W6692_WINBOND) {
			if ((W6692_SV_USR == dev_w6692->subsystem_vendor) &&
			    (W6692_SD_USR == dev_w6692->subsystem_device)) {
				cs->subtyp = W6692_USR;
			}
		}
	}
	if (!found) {
		printk(KERN_WARNING "W6692: No PCI card found\n");
		return (0);
	}
	cs->irq = pci_irq;
	if (!cs->irq) {
		printk(KERN_WARNING "W6692: No IRQ for PCI card found\n");
		return (0);
	}
	if (!pci_ioaddr) {
		printk(KERN_WARNING "W6692: NO I/O Base Address found\n");
		return (0);
	}
	cs->hw.w6692.iobase = pci_ioaddr;
	printk(KERN_INFO "Found: %s %s, I/O base: 0x%x, irq: %d\n",
	       id_list[cs->subtyp].vendor_name, id_list[cs->subtyp].card_name,
	       pci_ioaddr, pci_irq);
	if (check_region((cs->hw.w6692.iobase), 256)) {
		printk(KERN_WARNING
		       "HiSax: %s I/O ports %x-%x already in use\n",
		       id_list[cs->subtyp].card_name,
		       cs->hw.w6692.iobase,
		       cs->hw.w6692.iobase + 255);
		return (0);
	} else {
		request_region(cs->hw.w6692.iobase, 256,
			       id_list[cs->subtyp].card_name);
	}
#else
	printk(KERN_WARNING "HiSax: W6692 and NO_PCI_BIOS\n");
	printk(KERN_WARNING "HiSax: W6692 unable to config\n");
	return (0);
#endif				/* CONFIG_PCI */

	printk(KERN_INFO
	       "HiSax: %s config irq:%d I/O:%x\n",
	       id_list[cs->subtyp].card_name, cs->irq,
	       cs->hw.w6692.iobase);

	cs->readW6692 = &ReadW6692;
	cs->writeW6692 = &WriteW6692;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadW6692B;
	cs->BC_Write_Reg = &WriteW6692B;
	cs->BC_Send_Data = &W6692B_fill_fifo;
	cs->DC_Send_Data = &W6692_fill_fifo;
	cs->cardmsg = &w6692_card_msg;
	cs->irq_func = &W6692_interrupt;
	cs->irq_flags |= SA_SHIRQ;
	W6692Version(cs, "W6692:");
	printk(KERN_INFO "W6692 ISTA=0x%X\n", ReadW6692(cs, W_ISTA));
	printk(KERN_INFO "W6692 IMASK=0x%X\n", ReadW6692(cs, W_IMASK));
	printk(KERN_INFO "W6692 D_EXIR=0x%X\n", ReadW6692(cs, W_D_EXIR));
	printk(KERN_INFO "W6692 D_EXIM=0x%X\n", ReadW6692(cs, W_D_EXIM));
	printk(KERN_INFO "W6692 D_RSTA=0x%X\n", ReadW6692(cs, W_D_RSTA));
	return (1);
}
