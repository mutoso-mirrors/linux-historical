/* $Id: icc.c,v 1.5.6.4 2001/09/23 22:24:48 kai Exp $
 *
 * ICC specific routines
 *
 * Author       Matt Henderson & Guy Ellis
 * Copyright    by Traverse Technologies Pty Ltd, www.travers.com.au
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 * 1999.6.25 Initial implementation of routines for Siemens ISDN
 * Communication Controller PEB 2070 based on the ISAC routines
 * written by Karsten Keil.
 *
 */

#include <linux/init.h>
#include "hisax.h"
#include "icc.h"
// #include "arcofi.h"
#include "isdnl1.h"
#include <linux/interrupt.h>

#define DBUSY_TIMER_VALUE 80
#define ARCOFI_USE 0
static spinlock_t icc_lock = SPIN_LOCK_UNLOCKED;

static char *ICCVer[] __initdata =
{"2070 A1/A3", "2070 B1", "2070 B2/B3", "2070 V2.4"};

void
ICCVersion(struct IsdnCardState *cs, char *s)
{
	int val;

	val = cs->readisac(cs, ICC_RBCH);
	printk(KERN_INFO "%s ICC version (%x): %s\n", s, val, ICCVer[(val >> 5) & 3]);
}

static void
ph_command(struct IsdnCardState *cs, unsigned int command)
{
	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "ph_command %x", command);
	cs->writeisac(cs, ICC_CIX0, (command << 2) | 3);
}


static void
icc_new_ph(struct IsdnCardState *cs)
{
	switch (cs->dc.icc.ph_state) {
		case (ICC_IND_EI1):
			ph_command(cs, ICC_CMD_DI);
			l1_msg(cs, HW_RESET | INDICATION, NULL);
			break;
		case (ICC_IND_DC):
			l1_msg(cs, HW_DEACTIVATE | CONFIRM, NULL);
			break;
		case (ICC_IND_DR):
			l1_msg(cs, HW_DEACTIVATE | INDICATION, NULL);
			break;
		case (ICC_IND_PU):
			l1_msg(cs, HW_POWERUP | CONFIRM, NULL);
			break;
		case (ICC_IND_FJ):
			l1_msg(cs, HW_RSYNC | INDICATION, NULL);
			break;
		case (ICC_IND_AR):
			l1_msg(cs, HW_INFO2 | INDICATION, NULL);
			break;
		case (ICC_IND_AI):
			l1_msg(cs, HW_INFO4 | INDICATION, NULL);
			break;
		default:
			break;
	}
}

static void
icc_bh(void *data)
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
		icc_new_ph(cs);		
	if (test_and_clear_bit(D_RCVBUFREADY, &cs->event))
		DChannel_proc_rcv(cs);
	if (test_and_clear_bit(D_XMTBUFREADY, &cs->event))
		DChannel_proc_xmt(cs);
#if ARCOFI_USE
	if (!test_bit(HW_ARCOFI, &cs->HW_Flags))
		return;
	if (test_and_clear_bit(D_RX_MON1, &cs->event))
		arcofi_fsm(cs, ARCOFI_RX_END, NULL);
	if (test_and_clear_bit(D_TX_MON1, &cs->event))
		arcofi_fsm(cs, ARCOFI_TX_END, NULL);
#endif
}

void
icc_empty_fifo(struct IsdnCardState *cs, int count)
{
	u_char *ptr;
	unsigned long flags;

	if ((cs->debug & L1_DEB_ISAC) && !(cs->debug & L1_DEB_ISAC_FIFO))
		debugl1(cs, "icc_empty_fifo");

	if ((cs->rcvidx + count) >= MAX_DFRAME_LEN_L1) {
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "icc_empty_fifo overrun %d",
				cs->rcvidx + count);
		cs->writeisac(cs, ICC_CMDR, 0x80);
		cs->rcvidx = 0;
		return;
	}
	ptr = cs->rcvbuf + cs->rcvidx;
	cs->rcvidx += count;
	spin_lock_irqsave(&icc_lock, flags);
	cs->readisacfifo(cs, ptr, count);
	cs->writeisac(cs, ICC_CMDR, 0x80);
	spin_unlock_irqrestore(&icc_lock, flags);
	if (cs->debug & L1_DEB_ISAC_FIFO) {
		char *t = cs->dlog;

		t += sprintf(t, "icc_empty_fifo cnt %d", count);
		QuickHex(t, ptr, count);
		debugl1(cs, cs->dlog);
	}
}

static void
icc_fill_fifo(struct IsdnCardState *cs)
{
	int count, more;
	unsigned char *p;
	unsigned long flags;

	p = xmit_fill_fifo_d(cs, 32, &count, &more);
	if (!p)
		return;

	spin_lock_irqsave(&icc_lock, flags);
	cs->writeisacfifo(cs, p, count);
	cs->writeisac(cs, ICC_CMDR, more ? 0x8 : 0xa);
	if (test_and_set_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		debugl1(cs, "icc_fill_fifo dbusytimer running");
		del_timer(&cs->dbusytimer);
	}
	init_timer(&cs->dbusytimer);
	cs->dbusytimer.expires = jiffies + ((DBUSY_TIMER_VALUE * HZ)/1000);
	add_timer(&cs->dbusytimer);
	spin_unlock_irqrestore(&icc_lock, flags);
}

void
icc_interrupt(struct IsdnCardState *cs, u_char val)
{
	u_char exval, v1;
	struct sk_buff *skb;
	unsigned int count;
	unsigned long flags;

	if (cs->debug & L1_DEB_ISAC)
		debugl1(cs, "ICC interrupt %x", val);
	if (val & 0x80) {	/* RME */
		exval = cs->readisac(cs, ICC_RSTA);
		if ((exval & 0x70) != 0x20) {
			if (exval & 0x40) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "ICC RDO");
#ifdef ERROR_STATISTIC
				cs->err_rx++;
#endif
			}
			if (!(exval & 0x20)) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, "ICC CRC error");
#ifdef ERROR_STATISTIC
				cs->err_crc++;
#endif
			}
			cs->writeisac(cs, ICC_CMDR, 0x80);
		} else {
			count = cs->readisac(cs, ICC_RBCL) & 0x1f;
			if (count == 0)
				count = 32;
			icc_empty_fifo(cs, count);
			spin_lock_irqsave(&icc_lock, flags);
			if ((count = cs->rcvidx) > 0) {
				cs->rcvidx = 0;
				if (!(skb = alloc_skb(count, GFP_ATOMIC)))
					printk(KERN_WARNING "HiSax: D receive out of memory\n");
				else {
					memcpy(skb_put(skb, count), cs->rcvbuf, count);
					skb_queue_tail(&cs->rq, skb);
				}
			}
			spin_unlock_irqrestore(&icc_lock, flags);
		}
		cs->rcvidx = 0;
		sched_d_event(cs, D_RCVBUFREADY);
	}
	if (val & 0x40) {	/* RPF */
		icc_empty_fifo(cs, 32);
	}
	if (val & 0x20) {	/* RSC */
		/* never */
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ICC RSC interrupt");
	}
	if (val & 0x10) {	/* XPR */
		xmit_xpr_d(cs);
	}
	if (val & 0x04) {	/* CISQ */
		exval = cs->readisac(cs, ICC_CIR0);
		if (cs->debug & L1_DEB_ISAC)
			debugl1(cs, "ICC CIR0 %02X", exval );
		if (exval & 2) {
			cs->dc.icc.ph_state = (exval >> 2) & 0xf;
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "ph_state change %x", cs->dc.icc.ph_state);
			sched_d_event(cs, D_L1STATECHANGE);
		}
		if (exval & 1) {
			exval = cs->readisac(cs, ICC_CIR1);
			if (cs->debug & L1_DEB_ISAC)
				debugl1(cs, "ICC CIR1 %02X", exval );
		}
	}
	if (val & 0x02) {	/* SIN */
		/* never */
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ICC SIN interrupt");
	}
	if (val & 0x01) {	/* EXI */
		exval = cs->readisac(cs, ICC_EXIR);
		if (cs->debug & L1_DEB_WARN)
			debugl1(cs, "ICC EXIR %02x", exval);
		if (exval & 0x80) {  /* XMR */
			debugl1(cs, "ICC XMR");
			printk(KERN_WARNING "HiSax: ICC XMR\n");
		}
		if (exval & 0x40) {  /* XDU */
			xmit_xdu_d(cs, NULL);
		}
		if (exval & 0x04) {  /* MOS */
			v1 = cs->readisac(cs, ICC_MOSR);
			if (cs->debug & L1_DEB_MONITOR)
				debugl1(cs, "ICC MOSR %02x", v1);
#if ARCOFI_USE
			if (v1 & 0x08) {
				if (!cs->dc.icc.mon_rx) {
					if (!(cs->dc.icc.mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
						if (cs->debug & L1_DEB_WARN)
							debugl1(cs, "ICC MON RX out of memory!");
						cs->dc.icc.mocr &= 0xf0;
						cs->dc.icc.mocr |= 0x0a;
						cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
						goto afterMONR0;
					} else
						cs->dc.icc.mon_rxp = 0;
				}
				if (cs->dc.icc.mon_rxp >= MAX_MON_FRAME) {
					cs->dc.icc.mocr &= 0xf0;
					cs->dc.icc.mocr |= 0x0a;
					cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
					cs->dc.icc.mon_rxp = 0;
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "ICC MON RX overflow!");
					goto afterMONR0;
				}
				cs->dc.icc.mon_rx[cs->dc.icc.mon_rxp++] = cs->readisac(cs, ICC_MOR0);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ICC MOR0 %02x", cs->dc.icc.mon_rx[cs->dc.icc.mon_rxp -1]);
				if (cs->dc.icc.mon_rxp == 1) {
					cs->dc.icc.mocr |= 0x04;
					cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
				}
			}
		      afterMONR0:
			if (v1 & 0x80) {
				if (!cs->dc.icc.mon_rx) {
					if (!(cs->dc.icc.mon_rx = kmalloc(MAX_MON_FRAME, GFP_ATOMIC))) {
						if (cs->debug & L1_DEB_WARN)
							debugl1(cs, "ICC MON RX out of memory!");
						cs->dc.icc.mocr &= 0x0f;
						cs->dc.icc.mocr |= 0xa0;
						cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
						goto afterMONR1;
					} else
						cs->dc.icc.mon_rxp = 0;
				}
				if (cs->dc.icc.mon_rxp >= MAX_MON_FRAME) {
					cs->dc.icc.mocr &= 0x0f;
					cs->dc.icc.mocr |= 0xa0;
					cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
					cs->dc.icc.mon_rxp = 0;
					if (cs->debug & L1_DEB_WARN)
						debugl1(cs, "ICC MON RX overflow!");
					goto afterMONR1;
				}
				cs->dc.icc.mon_rx[cs->dc.icc.mon_rxp++] = cs->readisac(cs, ICC_MOR1);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ICC MOR1 %02x", cs->dc.icc.mon_rx[cs->dc.icc.mon_rxp -1]);
				cs->dc.icc.mocr |= 0x40;
				cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
			}
		      afterMONR1:
			if (v1 & 0x04) {
				cs->dc.icc.mocr &= 0xf0;
				cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
				cs->dc.icc.mocr |= 0x0a;
				cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
				sched_d_event(cs, D_RX_MON0);
			}
			if (v1 & 0x40) {
				cs->dc.icc.mocr &= 0x0f;
				cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
				cs->dc.icc.mocr |= 0xa0;
				cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
				sched_d_event(cs, D_RX_MON1);
			}
			if (v1 & 0x02) {
				if ((!cs->dc.icc.mon_tx) || (cs->dc.icc.mon_txc && 
					(cs->dc.icc.mon_txp >= cs->dc.icc.mon_txc) && 
					!(v1 & 0x08))) {
					cs->dc.icc.mocr &= 0xf0;
					cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
					cs->dc.icc.mocr |= 0x0a;
					cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
					if (cs->dc.icc.mon_txc &&
						(cs->dc.icc.mon_txp >= cs->dc.icc.mon_txc))
						sched_d_event(cs, D_TX_MON0);
					goto AfterMOX0;
				}
				if (cs->dc.icc.mon_txc && (cs->dc.icc.mon_txp >= cs->dc.icc.mon_txc)) {
					sched_d_event(cs, D_TX_MON0);
					goto AfterMOX0;
				}
				cs->writeisac(cs, ICC_MOX0,
					cs->dc.icc.mon_tx[cs->dc.icc.mon_txp++]);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ICC %02x -> MOX0", cs->dc.icc.mon_tx[cs->dc.icc.mon_txp -1]);
			}
		      AfterMOX0:
			if (v1 & 0x20) {
				if ((!cs->dc.icc.mon_tx) || (cs->dc.icc.mon_txc && 
					(cs->dc.icc.mon_txp >= cs->dc.icc.mon_txc) && 
					!(v1 & 0x80))) {
					cs->dc.icc.mocr &= 0x0f;
					cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
					cs->dc.icc.mocr |= 0xa0;
					cs->writeisac(cs, ICC_MOCR, cs->dc.icc.mocr);
					if (cs->dc.icc.mon_txc &&
						(cs->dc.icc.mon_txp >= cs->dc.icc.mon_txc))
						sched_d_event(cs, D_TX_MON1);
					goto AfterMOX1;
				}
				if (cs->dc.icc.mon_txc && (cs->dc.icc.mon_txp >= cs->dc.icc.mon_txc)) {
					sched_d_event(cs, D_TX_MON1);
					goto AfterMOX1;
				}
				cs->writeisac(cs, ICC_MOX1,
					cs->dc.icc.mon_tx[cs->dc.icc.mon_txp++]);
				if (cs->debug & L1_DEB_MONITOR)
					debugl1(cs, "ICC %02x -> MOX1", cs->dc.icc.mon_tx[cs->dc.icc.mon_txp -1]);
			}
		      AfterMOX1:
#endif
		}
	}
}

static void
ICC_l1hw(struct PStack *st, int pr, void *arg)
{
	struct IsdnCardState *cs = (struct IsdnCardState *) st->l1.hardware;
	struct sk_buff *skb = arg;
	int  val;

	switch (pr) {
		case (PH_DATA |REQUEST):
			if (cs->debug & DEB_DLOG_HEX)
				LogFrame(cs, skb->data, skb->len);
			if (cs->debug & DEB_DLOG_VERBOSE)
				dlogframe(cs, skb, 0);
			if (cs->tx_skb) {
				skb_queue_tail(&cs->sq, skb);
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA Queued", 0);
#endif
			} else {
				cs->tx_skb = skb;
				cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
				if (cs->debug & L1_DEB_LAPD)
					Logl2Frame(cs, skb, "PH_DATA", 0);
#endif
				icc_fill_fifo(cs);
			}
			break;
		case (PH_PULL |INDICATION):
			if (cs->tx_skb) {
				if (cs->debug & L1_DEB_WARN)
					debugl1(cs, " l2l1 tx_skb exist this shouldn't happen");
				skb_queue_tail(&cs->sq, skb);
				break;
			}
			if (cs->debug & DEB_DLOG_HEX)
				LogFrame(cs, skb->data, skb->len);
			if (cs->debug & DEB_DLOG_VERBOSE)
				dlogframe(cs, skb, 0);
			cs->tx_skb = skb;
			cs->tx_cnt = 0;
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				Logl2Frame(cs, skb, "PH_DATA_PULLED", 0);
#endif
			icc_fill_fifo(cs);
			break;
		case (PH_PULL | REQUEST):
#ifdef L2FRAME_DEBUG		/* psa */
			if (cs->debug & L1_DEB_LAPD)
				debugl1(cs, "-> PH_REQUEST_PULL");
#endif
			if (!cs->tx_skb) {
				test_and_clear_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
				L1L2(st, PH_PULL | CONFIRM, NULL);
			} else
				test_and_set_bit(FLG_L1_PULL_REQ, &st->l1.Flags);
			break;
		case (HW_RESET | REQUEST):
			if ((cs->dc.icc.ph_state == ICC_IND_EI1) ||
				(cs->dc.icc.ph_state == ICC_IND_DR))
			        ph_command(cs, ICC_CMD_DI);
			else
				ph_command(cs, ICC_CMD_RES);
			break;
		case (HW_ENABLE | REQUEST):
			ph_command(cs, ICC_CMD_DI);
			break;
		case (HW_INFO1 | REQUEST):
			ph_command(cs, ICC_CMD_AR);
			break;
		case (HW_INFO3 | REQUEST):
			ph_command(cs, ICC_CMD_AI);
			break;
		case (HW_TESTLOOP | REQUEST):
			val = 0;
			if (1 & (long) arg)
				val |= 0x0c;
			if (2 & (long) arg)
				val |= 0x3;
			if (test_bit(HW_IOM1, &cs->HW_Flags)) {
				/* IOM 1 Mode */
				if (!val) {
					cs->writeisac(cs, ICC_SPCR, 0xa);
					cs->writeisac(cs, ICC_ADF1, 0x2);
				} else {
					cs->writeisac(cs, ICC_SPCR, val);
					cs->writeisac(cs, ICC_ADF1, 0xa);
				}
			} else {
				/* IOM 2 Mode */
				cs->writeisac(cs, ICC_SPCR, val);
				if (val)
					cs->writeisac(cs, ICC_ADF1, 0x8);
				else
					cs->writeisac(cs, ICC_ADF1, 0x0);
			}
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
				debugl1(cs, "icc_l1hw unknown %04x", pr);
			break;
	}
}

void
setstack_icc(struct PStack *st, struct IsdnCardState *cs)
{
	st->l1.l1hw = ICC_l1hw;
}

void 
DC_Close_icc(struct IsdnCardState *cs) {
	if (cs->dc.icc.mon_rx) {
		kfree(cs->dc.icc.mon_rx);
		cs->dc.icc.mon_rx = NULL;
	}
	if (cs->dc.icc.mon_tx) {
		kfree(cs->dc.icc.mon_tx);
		cs->dc.icc.mon_tx = NULL;
	}
}

static void
dbusy_timer_handler(struct IsdnCardState *cs)
{
	struct PStack *stptr;
	int	rbch, star;

	if (test_bit(FLG_DBUSY_TIMER, &cs->HW_Flags)) {
		rbch = cs->readisac(cs, ICC_RBCH);
		star = cs->readisac(cs, ICC_STAR);
		if (cs->debug) 
			debugl1(cs, "D-Channel Busy RBCH %02x STAR %02x",
				rbch, star);
		if (rbch & ICC_RBCH_XAC) { /* D-Channel Busy */
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
				printk(KERN_WARNING "HiSax: ICC D-Channel Busy no skb\n");
				debugl1(cs, "D-Channel Busy no skb");
			}
			cs->writeisac(cs, ICC_CMDR, 0x01); /* Transmitter reset */
			cs->irq_func(cs->irq, cs, NULL);
		}
	}
}

void __init
initicc(struct IsdnCardState *cs)
{
	int val, eval;

	INIT_WORK(&cs->work, icc_bh, cs);
	cs->setstack_d = setstack_icc;
	cs->DC_Send_Data = icc_fill_fifo;
	cs->DC_Close = DC_Close_icc;
	cs->dc.icc.mon_tx = NULL;
	cs->dc.icc.mon_rx = NULL;
	cs->dbusytimer.function = (void *) dbusy_timer_handler;
	cs->dbusytimer.data = (long) cs;
	init_timer(&cs->dbusytimer);

	val = cs->readisac(cs, ICC_STAR);
	debugl1(cs, "ICC STAR %x", val);
	val = cs->readisac(cs, ICC_MODE);
	debugl1(cs, "ICC MODE %x", val);
	val = cs->readisac(cs, ICC_ADF2);
	debugl1(cs, "ICC ADF2 %x", val);
	val = cs->readisac(cs, ICC_ISTA);
	debugl1(cs, "ICC ISTA %x", val);
	if (val & 0x01) {
		eval = cs->readisac(cs, ICC_EXIR);
		debugl1(cs, "ICC EXIR %x", eval);
	}
	val = cs->readisac(cs, ICC_CIR0);
	debugl1(cs, "ICC CIR0 %x", val);
	cs->dc.icc.ph_state = (val >> 2) & 0xf;
	sched_d_event(cs, D_L1STATECHANGE);
	/* Disable all IRQ */
	cs->writeisac(cs, ICC_MASK, 0xFF);

  	cs->dc.icc.mocr = 0xaa;
	if (test_bit(HW_IOM1, &cs->HW_Flags)) {
		/* IOM 1 Mode */
		cs->writeisac(cs, ICC_ADF2, 0x0);
		cs->writeisac(cs, ICC_SPCR, 0xa);
		cs->writeisac(cs, ICC_ADF1, 0x2);
		cs->writeisac(cs, ICC_STCR, 0x70);
		cs->writeisac(cs, ICC_MODE, 0xc9);
	} else {
		/* IOM 2 Mode */
		if (!cs->dc.icc.adf2)
			cs->dc.icc.adf2 = 0x80;
		cs->writeisac(cs, ICC_ADF2, cs->dc.icc.adf2);
		cs->writeisac(cs, ICC_SQXR, 0xa0);
		cs->writeisac(cs, ICC_SPCR, 0x20);
		cs->writeisac(cs, ICC_STCR, 0x70);
		cs->writeisac(cs, ICC_MODE, 0xca);
		cs->writeisac(cs, ICC_TIMR, 0x00);
		cs->writeisac(cs, ICC_ADF1, 0x20);
	}
	ph_command(cs, ICC_CMD_RES);
	cs->writeisac(cs, ICC_MASK, 0x0);
	ph_command(cs, ICC_CMD_DI);
}
