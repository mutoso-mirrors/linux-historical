/* $Id: avm_a1p.c,v 2.3 1998/11/15 23:54:22 keil Exp $
 *
 * avm_a1p.c    low level stuff for the following AVM cards:
 *              A1 PCMCIA
 *		FRITZ!Card PCMCIA
 *		FRITZ!Card PCMCIA 2.0
 *
 * Author       Carsten Paeth (calle@calle.in-berlin.de)
 *
 * $Log: avm_a1p.c,v $
 * Revision 2.3  1998/11/15 23:54:22  keil
 * changes from 2.0
 *
 * Revision 2.2  1998/08/13 23:36:13  keil
 * HiSax 3.1 - don't work stable with current LinkLevel
 *
 * Revision 2.1  1998/07/15 15:01:23  calle
 * Support for AVM passive PCMCIA cards:
 *    A1 PCMCIA, FRITZ!Card PCMCIA and FRITZ!Card PCMCIA 2.0
 *
 * Revision 1.1.2.1  1998/07/15 14:43:26  calle
 * Support for AVM passive PCMCIA cards:
 *    A1 PCMCIA, FRITZ!Card PCMCIA and FRITZ!Card PCMCIA 2.0
 *
 *
 */
#define __NO_VERSION__
#include "hisax.h"
#include "isac.h"
#include "hscx.h"
#include "isdnl1.h"

/* register offsets */
#define ADDRREG_OFFSET		0x02
#define DATAREG_OFFSET		0x03
#define ASL0_OFFSET		0x04
#define ASL1_OFFSET		0x05
#define MODREG_OFFSET		0x06
#define VERREG_OFFSET		0x07

/* address offsets */
#define ISAC_FIFO_OFFSET	0x00
#define ISAC_REG_OFFSET		0x20
#define HSCX_CH_DIFF		0x40
#define HSCX_FIFO_OFFSET	0x80
#define HSCX_REG_OFFSET		0xa0

/* read bits ASL0 */
#define	 ASL0_R_TIMER		0x10 /* active low */
#define	 ASL0_R_ISAC		0x20 /* active low */
#define	 ASL0_R_HSCX		0x40 /* active low */
#define	 ASL0_R_TESTBIT		0x80
#define  ASL0_R_IRQPENDING	(ASL0_R_ISAC|ASL0_R_HSCX|ASL0_R_TIMER)

/* write bits ASL0 */
#define	 ASL0_W_RESET		0x01
#define	 ASL0_W_TDISABLE	0x02
#define	 ASL0_W_TRESET		0x04
#define	 ASL0_W_IRQENABLE	0x08
#define	 ASL0_W_TESTBIT		0x80

/* write bits ASL1 */
#define	 ASL1_W_LED0		0x10
#define	 ASL1_W_LED1		0x20
#define	 ASL1_W_ENABLE_S0	0xC0
 
#define byteout(addr,val) outb(val,addr)
#define bytein(addr) inb(addr)

static const char *avm_revision = "$Revision: 2.3 $";

static inline u_char
ReadISAC(struct IsdnCardState *cs, u_char offset)
{
	long flags;
        u_char ret;

        offset -= 0x20;
	save_flags(flags);
	cli();
        byteout(cs->hw.avm.cfg_reg+ADDRREG_OFFSET,ISAC_REG_OFFSET+offset);
	ret = bytein(cs->hw.avm.cfg_reg+DATAREG_OFFSET);
	restore_flags(flags);
	return ret;
}

static inline void
WriteISAC(struct IsdnCardState *cs, u_char offset, u_char value)
{
	long flags;

        offset -= 0x20;

	save_flags(flags);
	cli();
        byteout(cs->hw.avm.cfg_reg+ADDRREG_OFFSET,ISAC_REG_OFFSET+offset);
	byteout(cs->hw.avm.cfg_reg+DATAREG_OFFSET, value);
	restore_flags(flags);
}

static inline void
ReadISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(cs->hw.avm.cfg_reg+ADDRREG_OFFSET,ISAC_FIFO_OFFSET);
	insb(cs->hw.avm.cfg_reg+DATAREG_OFFSET, data, size);
	restore_flags(flags);
}

static inline void
WriteISACfifo(struct IsdnCardState *cs, u_char * data, int size)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(cs->hw.avm.cfg_reg+ADDRREG_OFFSET,ISAC_FIFO_OFFSET);
	outsb(cs->hw.avm.cfg_reg+DATAREG_OFFSET, data, size);
	restore_flags(flags);
}

static inline u_char
ReadHSCX(struct IsdnCardState *cs, int hscx, u_char offset)
{
	u_char ret;
	long flags;

        offset -= 0x20;

	save_flags(flags);
	cli();
	byteout(cs->hw.avm.cfg_reg+ADDRREG_OFFSET,
			HSCX_REG_OFFSET+hscx*HSCX_CH_DIFF+offset);
	ret = bytein(cs->hw.avm.cfg_reg+DATAREG_OFFSET);
	restore_flags(flags);
	return ret;
}

static inline void
WriteHSCX(struct IsdnCardState *cs, int hscx, u_char offset, u_char value)
{
	long flags;

        offset -= 0x20;

	save_flags(flags);
	cli();
	byteout(cs->hw.avm.cfg_reg+ADDRREG_OFFSET,
			HSCX_REG_OFFSET+hscx*HSCX_CH_DIFF+offset);
	byteout(cs->hw.avm.cfg_reg+DATAREG_OFFSET, value);
	restore_flags(flags);
}

static inline void
ReadHSCXfifo(struct IsdnCardState *cs, int hscx, u_char * data, int size)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(cs->hw.avm.cfg_reg+ADDRREG_OFFSET,
			HSCX_FIFO_OFFSET+hscx*HSCX_CH_DIFF);
	insb(cs->hw.avm.cfg_reg+DATAREG_OFFSET, data, size);
	restore_flags(flags);
}

static inline void
WriteHSCXfifo(struct IsdnCardState *cs, int hscx, u_char * data, int size)
{
	long flags;

	save_flags(flags);
	cli();
	byteout(cs->hw.avm.cfg_reg+ADDRREG_OFFSET,
			HSCX_FIFO_OFFSET+hscx*HSCX_CH_DIFF);
	outsb(cs->hw.avm.cfg_reg+DATAREG_OFFSET, data, size);
	restore_flags(flags);
}

/*
 * fast interrupt HSCX stuff goes here
 */

#define READHSCX(cs, nr, reg) ReadHSCX(cs, nr, reg)
#define WRITEHSCX(cs, nr, reg, data) WriteHSCX(cs, nr, reg, data)
#define READHSCXFIFO(cs, nr, ptr, cnt) ReadHSCXfifo(cs, nr, ptr, cnt) 
#define WRITEHSCXFIFO(cs, nr, ptr, cnt) WriteHSCXfifo(cs, nr, ptr, cnt)

#include "hscx_irq.c"

static void
avm_a1p_interrupt(int intno, void *dev_id, struct pt_regs *regs)
{
	struct IsdnCardState *cs = dev_id;
	u_char val, sval, stat = 0;

	if (!cs) {
		printk(KERN_WARNING "AVM A1 PCMCIA: Spurious interrupt!\n");
		return;
	}
	while ((sval = (~bytein(cs->hw.avm.cfg_reg+ASL0_OFFSET) & ASL0_R_IRQPENDING))) {
		if (cs->debug & L1_DEB_INTSTAT)
			debugl1(cs, "avm IntStatus %x", sval);
		if (sval & ASL0_R_HSCX) {
                        val = ReadHSCX(cs, 1, HSCX_ISTA);
			if (val) {
				hscx_int_main(cs, val);
				stat |= 1;
			}
		}
		if (sval & ASL0_R_ISAC) {
			val = ReadISAC(cs, ISAC_ISTA);
			if (val) {
				isac_interrupt(cs, val);
				stat |= 2;
			}
		}
	}
	if (stat & 1) {
		WriteHSCX(cs, 0, HSCX_MASK, 0xff);
		WriteHSCX(cs, 1, HSCX_MASK, 0xff);
		WriteHSCX(cs, 0, HSCX_MASK, 0x00);
		WriteHSCX(cs, 1, HSCX_MASK, 0x00);
	}
	if (stat & 2) {
		WriteISAC(cs, ISAC_MASK, 0xff);
		WriteISAC(cs, ISAC_MASK, 0x00);
	}
}

static int
AVM_card_msg(struct IsdnCardState *cs, int mt, void *arg)
{
	int ret;
	switch (mt) {
		case CARD_RESET:
			byteout(cs->hw.avm.cfg_reg+ASL0_OFFSET,0x00);
			HZDELAY(HZ / 5 + 1);
			byteout(cs->hw.avm.cfg_reg+ASL0_OFFSET,ASL0_W_RESET);
			HZDELAY(HZ / 5 + 1);
			byteout(cs->hw.avm.cfg_reg+ASL0_OFFSET,0x00);
			return 0;

		case CARD_RELEASE:
			/* free_irq is done in HiSax_closecard(). */
		        /* free_irq(cs->irq, cs); */
			return 0;

		case CARD_SETIRQ:
			ret = request_irq(cs->irq, &avm_a1p_interrupt,
					I4L_IRQ_FLAG, "HiSax", cs);
			if (ret)
				return ret;
			byteout(cs->hw.avm.cfg_reg+ASL0_OFFSET,
				ASL0_W_TDISABLE|ASL0_W_TRESET|ASL0_W_IRQENABLE);
                        return 0;

		case CARD_INIT:
			clear_pending_isac_ints(cs);
			clear_pending_hscx_ints(cs);
			inithscxisac(cs, 1);
			inithscxisac(cs, 2);
			return 0;

		case CARD_TEST:
			/* we really don't need it for the PCMCIA Version */
			return 0;

		default:
			/* all card drivers ignore others, so we do the same */
			return 0;
	}
	return 0;
}

__initfunc(int
setup_avm_a1_pcmcia(struct IsdnCard *card))
{
	u_char model, vers;
	struct IsdnCardState *cs = card->cs;
	long flags;
	char tmp[64];


	strcpy(tmp, avm_revision);
	printk(KERN_INFO "HiSax: AVM A1 PCMCIA driver Rev. %s\n",
						 HiSax_getrev(tmp));
	if (cs->typ != ISDN_CTYPE_A1_PCMCIA)
		return (0);

	cs->hw.avm.cfg_reg = card->para[1];
	cs->irq = card->para[0];


	save_flags(flags);
	outb(cs->hw.avm.cfg_reg+ASL1_OFFSET, ASL1_W_ENABLE_S0);
        sti();

	byteout(cs->hw.avm.cfg_reg+ASL0_OFFSET,0x00);
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg+ASL0_OFFSET,ASL0_W_RESET);
	HZDELAY(HZ / 5 + 1);
	byteout(cs->hw.avm.cfg_reg+ASL0_OFFSET,0x00);

	byteout(cs->hw.avm.cfg_reg+ASL0_OFFSET, ASL0_W_TDISABLE|ASL0_W_TRESET);

	restore_flags(flags);

	model = bytein(cs->hw.avm.cfg_reg+MODREG_OFFSET);
	vers = bytein(cs->hw.avm.cfg_reg+VERREG_OFFSET);

	printk(KERN_INFO "AVM A1 PCMCIA: io 0x%x irq %d model %d version %d\n",
				cs->hw.avm.cfg_reg, cs->irq, model, vers);

	cs->readisac = &ReadISAC;
	cs->writeisac = &WriteISAC;
	cs->readisacfifo = &ReadISACfifo;
	cs->writeisacfifo = &WriteISACfifo;
	cs->BC_Read_Reg = &ReadHSCX;
	cs->BC_Write_Reg = &WriteHSCX;
	cs->BC_Send_Data = &hscx_fill_fifo;
	cs->cardmsg = &AVM_card_msg;

	ISACVersion(cs, "AVM A1 PCMCIA:");
	if (HscxVersion(cs, "AVM A1 PCMCIA:")) {
		printk(KERN_WARNING
		       "AVM A1 PCMCIA: wrong HSCX versions check IO address\n");
		return (0);
	}
	return (1);
}
