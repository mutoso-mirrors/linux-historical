/*
 * linux/arch/arm/omap/dma.c
 *
 * Copyright (C) 2003 Nokia Corporation
 * Author: Juha Yrj�l� <juha.yrjola@nokia.com>
 * DMA channel linking for 1610 by Samuel Ortiz <samuel.ortiz@nokia.com>
 * Graphics DMA and LCD DMA graphics tranformations
 * by Imre Deak <imre.deak@nokia.com>
 * Some functions based on earlier dma-omap.c Copyright (C) 2001 RidgeRun, Inc.
 *
 * Support functions for the OMAP internal DMA channels.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/hardware.h>
#include <asm/dma.h>
#include <asm/io.h>

#include <asm/arch/tc.h>

#define OMAP_DMA_ACTIVE		0x01

#define OMAP_DMA_CCR_EN		(1 << 7)

#define OMAP_FUNC_MUX_ARM_BASE	(0xfffe1000 + 0xec)

static int enable_1510_mode = 0;

struct omap_dma_lch {
	int next_lch;
	int dev_id;
	u16 saved_csr;
	u16 enabled_irqs;
	const char *dev_name;
	void (* callback)(int lch, u16 ch_status, void *data);
	void *data;
	long flags;
};

static int dma_chan_count;

static spinlock_t dma_chan_lock;
static struct omap_dma_lch dma_chan[OMAP_LOGICAL_DMA_CH_COUNT];

const static u8 dma_irq[OMAP_LOGICAL_DMA_CH_COUNT] = {
	INT_DMA_CH0_6, INT_DMA_CH1_7, INT_DMA_CH2_8, INT_DMA_CH3,
	INT_DMA_CH4, INT_DMA_CH5, INT_1610_DMA_CH6, INT_1610_DMA_CH7,
	INT_1610_DMA_CH8, INT_1610_DMA_CH9, INT_1610_DMA_CH10,
	INT_1610_DMA_CH11, INT_1610_DMA_CH12, INT_1610_DMA_CH13,
	INT_1610_DMA_CH14, INT_1610_DMA_CH15, INT_DMA_LCD
};

static inline int get_gdma_dev(int req)
{
	u32 reg = OMAP_FUNC_MUX_ARM_BASE + ((req - 1) / 5) * 4;
	int shift = ((req - 1) % 5) * 6;

	return ((omap_readl(reg) >> shift) & 0x3f) + 1;
}

static inline void set_gdma_dev(int req, int dev)
{
	u32 reg = OMAP_FUNC_MUX_ARM_BASE + ((req - 1) / 5) * 4;
	int shift = ((req - 1) % 5) * 6;
	u32 l;

	l = omap_readl(reg);
	l &= ~(0x3f << shift);
	l |= (dev - 1) << shift;
	omap_writel(l, reg);
}

static void clear_lch_regs(int lch)
{
	int i;
	u32 lch_base = OMAP_DMA_BASE + lch * 0x40;

	for (i = 0; i < 0x2c; i += 2)
		omap_writew(0, lch_base + i);
}

void omap_set_dma_priority(int dst_port, int priority)
{
	unsigned long reg;
	u32 l;

	switch (dst_port) {
	case OMAP_DMA_PORT_OCP_T1:	/* FFFECC00 */
		reg = OMAP_TC_OCPT1_PRIOR;
		break;
	case OMAP_DMA_PORT_OCP_T2:	/* FFFECCD0 */
		reg = OMAP_TC_OCPT2_PRIOR;
		break;
	case OMAP_DMA_PORT_EMIFF:	/* FFFECC08 */
		reg = OMAP_TC_EMIFF_PRIOR;
		break;
	case OMAP_DMA_PORT_EMIFS:	/* FFFECC04 */
		reg = OMAP_TC_EMIFS_PRIOR;
		break;
	default:
		BUG();
		return;
	}
	l = omap_readl(reg);
	l &= ~(0xf << 8);
	l |= (priority & 0xf) << 8;
	omap_writel(l, reg);
}

void omap_set_dma_transfer_params(int lch, int data_type, int elem_count,
				  int frame_count, int sync_mode)
{
	u16 w;

	w = omap_readw(OMAP_DMA_CSDP(lch));
	w &= ~0x03;
	w |= data_type;
	omap_writew(w, OMAP_DMA_CSDP(lch));

	w = omap_readw(OMAP_DMA_CCR(lch));
	w &= ~(1 << 5);
	if (sync_mode == OMAP_DMA_SYNC_FRAME)
		w |= 1 << 5;
	omap_writew(w, OMAP_DMA_CCR(lch));

	w = omap_readw(OMAP_DMA_CCR2(lch));
	w &= ~(1 << 2);
	if (sync_mode == OMAP_DMA_SYNC_BLOCK)
		w |= 1 << 2;
	omap_writew(w, OMAP_DMA_CCR2(lch));

	omap_writew(elem_count, OMAP_DMA_CEN(lch));
	omap_writew(frame_count, OMAP_DMA_CFN(lch));

}
void omap_set_dma_color_mode(int lch, enum omap_dma_color_mode mode, u32 color)
{
	u16 w;

	BUG_ON(omap_dma_in_1510_mode());

	w = omap_readw(OMAP_DMA_CCR2(lch)) & ~0x03;
	switch (mode) {
	case OMAP_DMA_CONSTANT_FILL:
		w |= 0x01;
		break;
	case OMAP_DMA_TRANSPARENT_COPY:
		w |= 0x02;
		break;
	case OMAP_DMA_COLOR_DIS:
		break;
	default:
		BUG();
	}
	omap_writew(w, OMAP_DMA_CCR2(lch));

	w = omap_readw(OMAP_DMA_LCH_CTRL(lch)) & ~0x0f;
	/* Default is channel type 2D */
	if (mode) {
		omap_writew((u16)color, OMAP_DMA_COLOR_L(lch));
		omap_writew((u16)(color >> 16), OMAP_DMA_COLOR_U(lch));
		w |= 1;		/* Channel type G */
	}
	omap_writew(w, OMAP_DMA_LCH_CTRL(lch));
}


void omap_set_dma_src_params(int lch, int src_port, int src_amode,
			     unsigned long src_start)
{
	u16 w;

	w = omap_readw(OMAP_DMA_CSDP(lch));
	w &= ~(0x1f << 2);
	w |= src_port << 2;
	omap_writew(w, OMAP_DMA_CSDP(lch));

	w = omap_readw(OMAP_DMA_CCR(lch));
	w &= ~(0x03 << 12);
	w |= src_amode << 12;
	omap_writew(w, OMAP_DMA_CCR(lch));

	omap_writew(src_start >> 16, OMAP_DMA_CSSA_U(lch));
	omap_writew(src_start, OMAP_DMA_CSSA_L(lch));
}

void omap_set_dma_src_index(int lch, int eidx, int fidx)
{
	omap_writew(eidx, OMAP_DMA_CSEI(lch));
	omap_writew(fidx, OMAP_DMA_CSFI(lch));
}

void omap_set_dma_src_data_pack(int lch, int enable)
{
	u16 w;

	w = omap_readw(OMAP_DMA_CSDP(lch)) & ~(1 << 6);
	w |= enable ? (1 << 6) : 0;
	omap_writew(w, OMAP_DMA_CSDP(lch));
}

void omap_set_dma_src_burst_mode(int lch, enum omap_dma_burst_mode burst_mode)
{
	u16 w;

	w = omap_readw(OMAP_DMA_CSDP(lch)) & ~(0x03 << 7);
	switch (burst_mode) {
	case OMAP_DMA_DATA_BURST_DIS:
		break;
	case OMAP_DMA_DATA_BURST_4:
		w |= (0x01 << 7);
		break;
	case OMAP_DMA_DATA_BURST_8:
		/* not supported by current hardware
		 * w |= (0x03 << 7);
		 * fall through
		 */
	default:
		BUG();
	}
	omap_writew(w, OMAP_DMA_CSDP(lch));
}

void omap_set_dma_dest_params(int lch, int dest_port, int dest_amode,
			      unsigned long dest_start)
{
	u16 w;

	w = omap_readw(OMAP_DMA_CSDP(lch));
	w &= ~(0x1f << 9);
	w |= dest_port << 9;
	omap_writew(w, OMAP_DMA_CSDP(lch));

	w = omap_readw(OMAP_DMA_CCR(lch));
	w &= ~(0x03 << 14);
	w |= dest_amode << 14;
	omap_writew(w, OMAP_DMA_CCR(lch));

	omap_writew(dest_start >> 16, OMAP_DMA_CDSA_U(lch));
	omap_writew(dest_start, OMAP_DMA_CDSA_L(lch));
}

void omap_set_dma_dest_index(int lch, int eidx, int fidx)
{
	omap_writew(eidx, OMAP_DMA_CDEI(lch));
	omap_writew(fidx, OMAP_DMA_CDFI(lch));
}

void omap_set_dma_dest_data_pack(int lch, int enable)
{
	u16 w;

	w = omap_readw(OMAP_DMA_CSDP(lch)) & ~(1 << 13);
	w |= enable ? (1 << 13) : 0;
	omap_writew(w, OMAP_DMA_CSDP(lch));
}

void omap_set_dma_dest_burst_mode(int lch, enum omap_dma_burst_mode burst_mode)
{
	u16 w;

	w = omap_readw(OMAP_DMA_CSDP(lch)) & ~(0x03 << 14);
	switch (burst_mode) {
	case OMAP_DMA_DATA_BURST_DIS:
		break;
	case OMAP_DMA_DATA_BURST_4:
		w |= (0x01 << 14);
		break;
	case OMAP_DMA_DATA_BURST_8:
		w |= (0x03 << 14);
		break;
	default:
		printk(KERN_ERR "Invalid DMA burst mode\n");
		BUG();
		return;
	}
	omap_writew(w, OMAP_DMA_CSDP(lch));
}

static inline void init_intr(int lch)
{
	u16 w;

	/* Read CSR to make sure it's cleared. */
	w = omap_readw(OMAP_DMA_CSR(lch));
	/* Enable some nice interrupts. */
	omap_writew(dma_chan[lch].enabled_irqs, OMAP_DMA_CICR(lch));
	dma_chan[lch].flags |= OMAP_DMA_ACTIVE;
}

static inline void enable_lnk(int lch)
{
	u16 w;

	/* Clear the STOP_LNK bits */
	w = omap_readw(OMAP_DMA_CLNK_CTRL(lch));
	w &= ~(1 << 14);
	omap_writew(w, OMAP_DMA_CLNK_CTRL(lch));

	/* And set the ENABLE_LNK bits */
	if (dma_chan[lch].next_lch != -1)
		omap_writew(dma_chan[lch].next_lch | (1 << 15),
			    OMAP_DMA_CLNK_CTRL(lch));
}

static inline void disable_lnk(int lch)
{
	u16 w;

	/* Disable interrupts */
	omap_writew(0, OMAP_DMA_CICR(lch));

	/* Set the STOP_LNK bit */
	w = omap_readw(OMAP_DMA_CLNK_CTRL(lch));
	w |= (1 << 14);
	w = omap_writew(w, OMAP_DMA_CLNK_CTRL(lch));

	dma_chan[lch].flags &= ~OMAP_DMA_ACTIVE;
}

void omap_start_dma(int lch)
{
	u16 w;

	if (!omap_dma_in_1510_mode() && dma_chan[lch].next_lch != -1) {
		int next_lch, cur_lch;
		char dma_chan_link_map[OMAP_LOGICAL_DMA_CH_COUNT];

		dma_chan_link_map[lch] = 1;
		/* Set the link register of the first channel */
		enable_lnk(lch);

		memset(dma_chan_link_map, 0, sizeof(dma_chan_link_map));
		cur_lch = dma_chan[lch].next_lch;
		do {
			next_lch = dma_chan[cur_lch].next_lch;

                        /* The loop case: we've been here already */
			if (dma_chan_link_map[cur_lch])
				break;
			/* Mark the current channel */
			dma_chan_link_map[cur_lch] = 1;

			enable_lnk(cur_lch);
			init_intr(cur_lch);

			cur_lch = next_lch;
		} while (next_lch != -1);
	}

	init_intr(lch);

	w = omap_readw(OMAP_DMA_CCR(lch));
	w |= OMAP_DMA_CCR_EN;
	omap_writew(w, OMAP_DMA_CCR(lch));
	dma_chan[lch].flags |= OMAP_DMA_ACTIVE;
}

void omap_stop_dma(int lch)
{
	u16 w;

	if (!omap_dma_in_1510_mode() && dma_chan[lch].next_lch != -1) {
		int next_lch, cur_lch = lch;
		char dma_chan_link_map[OMAP_LOGICAL_DMA_CH_COUNT];

		memset(dma_chan_link_map, 0, sizeof(dma_chan_link_map));
		do {
			/* The loop case: we've been here already */
			if (dma_chan_link_map[cur_lch])
				break;
			/* Mark the current channel */
			dma_chan_link_map[cur_lch] = 1;

			disable_lnk(cur_lch);

			next_lch = dma_chan[cur_lch].next_lch;
			cur_lch = next_lch;
		} while (next_lch != -1);

		return;
	}
	/* Disable all interrupts on the channel */
	omap_writew(0, OMAP_DMA_CICR(lch));

	w = omap_readw(OMAP_DMA_CCR(lch));
	w &= ~OMAP_DMA_CCR_EN;
	omap_writew(w, OMAP_DMA_CCR(lch));
	dma_chan[lch].flags &= ~OMAP_DMA_ACTIVE;
}

void omap_enable_dma_irq(int lch, u16 bits)
{
	dma_chan[lch].enabled_irqs |= bits;
}

void omap_disable_dma_irq(int lch, u16 bits)
{
	dma_chan[lch].enabled_irqs &= ~bits;
}

static int dma_handle_ch(int ch)
{
	u16 csr;

	if (enable_1510_mode && ch >= 6) {
		csr = dma_chan[ch].saved_csr;
		dma_chan[ch].saved_csr = 0;
	} else
		csr = omap_readw(OMAP_DMA_CSR(ch));
	if (enable_1510_mode && ch <= 2 && (csr >> 7) != 0) {
		dma_chan[ch + 6].saved_csr = csr >> 7;
		csr &= 0x7f;
	}
	if (!csr)
		return 0;
	if (unlikely(dma_chan[ch].dev_id == -1)) {
		printk(KERN_WARNING "Spurious interrupt from DMA channel %d (CSR %04x)\n",
		       ch, csr);
		return 0;
	}
	if (unlikely(csr & OMAP_DMA_TOUT_IRQ))
		printk(KERN_WARNING "DMA timeout with device %d\n", dma_chan[ch].dev_id);
	if (unlikely(csr & OMAP_DMA_DROP_IRQ))
		printk(KERN_WARNING "DMA synchronization event drop occurred with device %d\n",
		       dma_chan[ch].dev_id);
	if (likely(csr & OMAP_DMA_BLOCK_IRQ))
		dma_chan[ch].flags &= ~OMAP_DMA_ACTIVE;
	if (likely(dma_chan[ch].callback != NULL))
		dma_chan[ch].callback(ch, csr, dma_chan[ch].data);
	return 1;
}

static irqreturn_t dma_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	int ch = ((int) dev_id) - 1;
	int handled = 0;

	for (;;) {
		int handled_now = 0;

		handled_now += dma_handle_ch(ch);
		if (enable_1510_mode && dma_chan[ch + 6].saved_csr)
			handled_now += dma_handle_ch(ch + 6);
		if (!handled_now)
			break;
		handled += handled_now;
	}

	return handled ? IRQ_HANDLED : IRQ_NONE;
}

int omap_request_dma(int dev_id, const char *dev_name,
		     void (* callback)(int lch, u16 ch_status, void *data),
		     void *data, int *dma_ch_out)
{
	int ch, free_ch = -1;
	unsigned long flags;
	struct omap_dma_lch *chan;

	spin_lock_irqsave(&dma_chan_lock, flags);
	for (ch = 0; ch < dma_chan_count; ch++) {
		if (free_ch == -1 && dma_chan[ch].dev_id == -1) {
			free_ch = ch;
			if (dev_id == 0)
				break;
		}
	}
	if (free_ch == -1) {
		spin_unlock_irqrestore(&dma_chan_lock, flags);
		return -EBUSY;
	}
	chan = dma_chan + free_ch;
	chan->dev_id = dev_id;
	clear_lch_regs(free_ch);
	spin_unlock_irqrestore(&dma_chan_lock, flags);

	chan->dev_id = dev_id;
	chan->dev_name = dev_name;
	chan->callback = callback;
	chan->data = data;
	chan->enabled_irqs = OMAP_DMA_TOUT_IRQ | OMAP_DMA_DROP_IRQ | OMAP_DMA_BLOCK_IRQ;

	if (cpu_is_omap16xx()) {
		/* If the sync device is set, configure it dynamically. */
		if (dev_id != 0) {
			set_gdma_dev(free_ch + 1, dev_id);
			dev_id = free_ch + 1;
		}
		/* Disable the 1510 compatibility mode and set the sync device
		 * id. */
		omap_writew(dev_id | (1 << 10), OMAP_DMA_CCR(free_ch));
	} else {
		omap_writew(dev_id, OMAP_DMA_CCR(free_ch));
	}
	*dma_ch_out = free_ch;

	return 0;
}

void omap_free_dma(int ch)
{
	unsigned long flags;

	spin_lock_irqsave(&dma_chan_lock, flags);
	if (dma_chan[ch].dev_id == -1) {
		printk("omap_dma: trying to free nonallocated DMA channel %d\n", ch);
		spin_unlock_irqrestore(&dma_chan_lock, flags);
		return;
	}
	dma_chan[ch].dev_id = -1;
	spin_unlock_irqrestore(&dma_chan_lock, flags);

	/* Disable all DMA interrupts for the channel. */
	omap_writew(0, OMAP_DMA_CICR(ch));
	/* Make sure the DMA transfer is stopped. */
	omap_writew(0, OMAP_DMA_CCR(ch));
}

int omap_dma_in_1510_mode(void)
{
	return enable_1510_mode;
}

/*
 * lch_queue DMA will start right after lch_head one is finished.
 * For this DMA link to start, you still need to start (see omap_start_dma)
 * the first one. That will fire up the entire queue.
 */
void omap_dma_link_lch (int lch_head, int lch_queue)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA linking is not supported in 1510 mode\n");
		BUG();
		return;
	}

	if ((dma_chan[lch_head].dev_id == -1) ||
	    (dma_chan[lch_queue].dev_id == -1)) {
		printk(KERN_ERR "omap_dma: trying to link non requested channels\n");
		dump_stack();
	}

	dma_chan[lch_head].next_lch = lch_queue;
}

/*
 * Once the DMA queue is stopped, we can destroy it.
 */
void omap_dma_unlink_lch (int lch_head, int lch_queue)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA linking is not supported in 1510 mode\n");
		BUG();
		return;
	}

	if (dma_chan[lch_head].next_lch != lch_queue ||
	    dma_chan[lch_head].next_lch == -1) {
		printk(KERN_ERR "omap_dma: trying to unlink non linked channels\n");
		dump_stack();
	}


	if ((dma_chan[lch_head].flags & OMAP_DMA_ACTIVE) ||
	    (dma_chan[lch_head].flags & OMAP_DMA_ACTIVE)) {
		printk(KERN_ERR "omap_dma: You need to stop the DMA channels before unlinking\n");
		dump_stack();
	}

	dma_chan[lch_head].next_lch = -1;
}


static struct lcd_dma_info {
	spinlock_t lock;
	int reserved;
	void (* callback)(u16 status, void *data);
	void *cb_data;

	int active;
	unsigned long addr, size;
	int rotate, data_type, xres, yres;
	int vxres;
	int mirror;
	int xscale, yscale;
	int ext_ctrl;
	int src_port;
	int single_transfer;
} lcd_dma;

void omap_set_lcd_dma_b1(unsigned long addr, u16 fb_xres, u16 fb_yres,
			 int data_type)
{
	lcd_dma.addr = addr;
	lcd_dma.data_type = data_type;
	lcd_dma.xres = fb_xres;
	lcd_dma.yres = fb_yres;
}

void omap_set_lcd_dma_src_port(int port)
{
	lcd_dma.src_port = port;
}

void omap_set_lcd_dma_ext_controller(int external)
{
	lcd_dma.ext_ctrl = external;
}

void omap_set_lcd_dma_single_transfer(int single)
{
	lcd_dma.single_transfer = single;
}


void omap_set_lcd_dma_b1_rotation(int rotate)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA rotation is not supported in 1510 mode\n");
		BUG();
		return;
	}
	lcd_dma.rotate = rotate;
}

void omap_set_lcd_dma_b1_mirror(int mirror)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA mirror is not supported in 1510 mode\n");
		BUG();
	}
	lcd_dma.mirror = mirror;
}

void omap_set_lcd_dma_b1_vxres(unsigned long vxres)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA virtual resulotion is not supported "
				"in 1510 mode\n");
		BUG();
	}
	lcd_dma.vxres = vxres;
}

void omap_set_lcd_dma_b1_scale(unsigned int xscale, unsigned int yscale)
{
	if (omap_dma_in_1510_mode()) {
		printk(KERN_ERR "DMA scale is not supported in 1510 mode\n");
		BUG();
	}
	lcd_dma.xscale = xscale;
	lcd_dma.yscale = yscale;
}

static void set_b1_regs(void)
{
	unsigned long top, bottom;
	int es;
	u16 w;
	unsigned long en, fn;
	long ei, fi;
	unsigned long vxres;
	unsigned int xscale, yscale;

	switch (lcd_dma.data_type) {
	case OMAP_DMA_DATA_TYPE_S8:
		es = 1;
		break;
	case OMAP_DMA_DATA_TYPE_S16:
		es = 2;
		break;
	case OMAP_DMA_DATA_TYPE_S32:
		es = 4;
		break;
	default:
		BUG();
		return;
	}

	vxres = lcd_dma.vxres ? lcd_dma.vxres : lcd_dma.xres;
	xscale = lcd_dma.xscale ? lcd_dma.xscale : 1;
	yscale = lcd_dma.yscale ? lcd_dma.yscale : 1;
	BUG_ON(vxres < lcd_dma.xres);
#define PIXADDR(x,y) (lcd_dma.addr + ((y) * vxres * yscale + (x) * xscale) * es)
#define PIXSTEP(sx, sy, dx, dy) (PIXADDR(dx, dy) - PIXADDR(sx, sy) - es + 1)
	switch (lcd_dma.rotate) {
	case 0:
		if (!lcd_dma.mirror) {
			top = PIXADDR(0, 0);
			bottom = PIXADDR(lcd_dma.xres - 1, lcd_dma.yres - 1);
			/* 1510 DMA requires the bottom address to be 2 more
			 * than the actual last memory access location. */
			if (omap_dma_in_1510_mode() &&
			    lcd_dma.data_type == OMAP_DMA_DATA_TYPE_S32)
				bottom += 2;
			ei = PIXSTEP(0, 0, 1, 0);
			fi = PIXSTEP(lcd_dma.xres - 1, 0, 0, 1);
		} else {
			top = PIXADDR(lcd_dma.xres - 1, 0);
			bottom = PIXADDR(0, lcd_dma.yres - 1);
			ei = PIXSTEP(1, 0, 0, 0);
			fi = PIXSTEP(0, 0, lcd_dma.xres - 1, 1);
		}
		en = lcd_dma.xres;
		fn = lcd_dma.yres;
		break;
	case 90:
		if (!lcd_dma.mirror) {
			top = PIXADDR(0, lcd_dma.yres - 1);
			bottom = PIXADDR(lcd_dma.xres - 1, 0);
			ei = PIXSTEP(0, 1, 0, 0);
			fi = PIXSTEP(0, 0, 1, lcd_dma.yres - 1);
		} else {
			top = PIXADDR(lcd_dma.xres - 1, lcd_dma.yres - 1);
			bottom = PIXADDR(0, 0);
			ei = PIXSTEP(0, 1, 0, 0);
			fi = PIXSTEP(1, 0, 0, lcd_dma.yres - 1);
		}
		en = lcd_dma.yres;
		fn = lcd_dma.xres;
		break;
	case 180:
		if (!lcd_dma.mirror) {
			top = PIXADDR(lcd_dma.xres - 1, lcd_dma.yres - 1);
			bottom = PIXADDR(0, 0);
			ei = PIXSTEP(1, 0, 0, 0);
			fi = PIXSTEP(0, 1, lcd_dma.xres - 1, 0);
		} else {
			top = PIXADDR(0, lcd_dma.yres - 1);
			bottom = PIXADDR(lcd_dma.xres - 1, 0);
			ei = PIXSTEP(0, 0, 1, 0);
			fi = PIXSTEP(lcd_dma.xres - 1, 1, 0, 0);
		}
		en = lcd_dma.xres;
		fn = lcd_dma.yres;
		break;
	case 270:
		if (!lcd_dma.mirror) {
			top = PIXADDR(lcd_dma.xres - 1, 0);
			bottom = PIXADDR(0, lcd_dma.yres - 1);
			ei = PIXSTEP(0, 0, 0, 1);
			fi = PIXSTEP(1, lcd_dma.yres - 1, 0, 0);
		} else {
			top = PIXADDR(0, 0);
			bottom = PIXADDR(lcd_dma.xres - 1, lcd_dma.yres - 1);
			ei = PIXSTEP(0, 0, 0, 1);
			fi = PIXSTEP(0, lcd_dma.yres - 1, 1, 0);
		}
		en = lcd_dma.yres;
		fn = lcd_dma.xres;
		break;
	default:
		BUG();
		return;	/* Supress warning about uninitialized vars */
	}

	if (omap_dma_in_1510_mode()) {
		omap_writew(top >> 16, OMAP1510_DMA_LCD_TOP_F1_U);
		omap_writew(top, OMAP1510_DMA_LCD_TOP_F1_L);
		omap_writew(bottom >> 16, OMAP1510_DMA_LCD_BOT_F1_U);
		omap_writew(bottom, OMAP1510_DMA_LCD_BOT_F1_L);

		return;
	}

	/* 1610 regs */
	omap_writew(top >> 16, OMAP1610_DMA_LCD_TOP_B1_U);
	omap_writew(top, OMAP1610_DMA_LCD_TOP_B1_L);
	omap_writew(bottom >> 16, OMAP1610_DMA_LCD_BOT_B1_U);
	omap_writew(bottom, OMAP1610_DMA_LCD_BOT_B1_L);

	omap_writew(en, OMAP1610_DMA_LCD_SRC_EN_B1);
	omap_writew(fn, OMAP1610_DMA_LCD_SRC_FN_B1);

	w = omap_readw(OMAP1610_DMA_LCD_CSDP);
	w &= ~0x03;
	w |= lcd_dma.data_type;
	omap_writew(w, OMAP1610_DMA_LCD_CSDP);

	w = omap_readw(OMAP1610_DMA_LCD_CTRL);
	/* Always set the source port as SDRAM for now*/
	w &= ~(0x03 << 6);
	if (lcd_dma.ext_ctrl)
		w |= 1 << 8;
	else
		w &= ~(1 << 8);
	if (lcd_dma.callback != NULL)
		w |= 1 << 1;            /* Block interrupt enable */
	else
		w &= ~(1 << 1);
	omap_writew(w, OMAP1610_DMA_LCD_CTRL);

	if (!(lcd_dma.rotate || lcd_dma.mirror ||
	      lcd_dma.vxres || lcd_dma.xscale || lcd_dma.yscale))
		return;

	w = omap_readw(OMAP1610_DMA_LCD_CCR);
	/* Set the double-indexed addressing mode */
	w |= (0x03 << 12);
	omap_writew(w, OMAP1610_DMA_LCD_CCR);

	omap_writew(ei, OMAP1610_DMA_LCD_SRC_EI_B1);
	omap_writew(fi >> 16, OMAP1610_DMA_LCD_SRC_FI_B1_U);
	omap_writew(fi, OMAP1610_DMA_LCD_SRC_FI_B1_L);
}

static irqreturn_t lcd_dma_irq_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	u16 w;

	w = omap_readw(OMAP1610_DMA_LCD_CTRL);
	if (unlikely(!(w & (1 << 3)))) {
		printk(KERN_WARNING "Spurious LCD DMA IRQ\n");
		return IRQ_NONE;
	}
	/* Ack the IRQ */
	w |= (1 << 3);
	omap_writew(w, OMAP1610_DMA_LCD_CTRL);
	lcd_dma.active = 0;
	if (lcd_dma.callback != NULL)
		lcd_dma.callback(w, lcd_dma.cb_data);

	return IRQ_HANDLED;
}

int omap_request_lcd_dma(void (* callback)(u16 status, void *data),
			 void *data)
{
	spin_lock_irq(&lcd_dma.lock);
	if (lcd_dma.reserved) {
		spin_unlock_irq(&lcd_dma.lock);
		printk(KERN_ERR "LCD DMA channel already reserved\n");
		BUG();
		return -EBUSY;
	}
	lcd_dma.reserved = 1;
	spin_unlock_irq(&lcd_dma.lock);
	lcd_dma.callback = callback;
	lcd_dma.cb_data = data;
	lcd_dma.active = 0;
	lcd_dma.single_transfer = 0;
	lcd_dma.rotate = 0;
	lcd_dma.vxres = 0;
	lcd_dma.mirror = 0;
	lcd_dma.xscale = 0;
	lcd_dma.yscale = 0;
	lcd_dma.ext_ctrl = 0;
	lcd_dma.src_port = 0;

	return 0;
}

void omap_free_lcd_dma(void)
{
	spin_lock(&lcd_dma.lock);
	if (!lcd_dma.reserved) {
		spin_unlock(&lcd_dma.lock);
		printk(KERN_ERR "LCD DMA is not reserved\n");
		BUG();
		return;
	}
	if (!enable_1510_mode)
		omap_writew(omap_readw(OMAP1610_DMA_LCD_CCR) & ~1, OMAP1610_DMA_LCD_CCR);
	lcd_dma.reserved = 0;
	spin_unlock(&lcd_dma.lock);
}

void omap_enable_lcd_dma(void)
{
	u16 w;

	/* Set the Enable bit only if an external controller is
	 * connected. Otherwise the OMAP internal controller will
	 * start the transfer when it gets enabled.
	 */
	if (enable_1510_mode || !lcd_dma.ext_ctrl)
		return;
	w = omap_readw(OMAP1610_DMA_LCD_CCR);
	w |= 1 << 7;
	omap_writew(w, OMAP1610_DMA_LCD_CCR);
	lcd_dma.active = 1;
}

void omap_setup_lcd_dma(void)
{
	BUG_ON(lcd_dma.active);
	if (!enable_1510_mode) {
		/* Set some reasonable defaults */
		omap_writew(0x5440, OMAP1610_DMA_LCD_CCR);
		omap_writew(0x9102, OMAP1610_DMA_LCD_CSDP);
		omap_writew(0x0004, OMAP1610_DMA_LCD_LCH_CTRL);
	}
	set_b1_regs();
	if (!enable_1510_mode) {
		u16 w;

		w = omap_readw(OMAP1610_DMA_LCD_CCR);
		/* If DMA was already active set the end_prog bit to have
		 * the programmed register set loaded into the active
		 * register set.
		 */
		w |= 1 << 11;		/* End_prog */
		if (!lcd_dma.single_transfer)
	        	w |= (3 << 8);	/* Auto_init, repeat */
		omap_writew(w, OMAP1610_DMA_LCD_CCR);
	}
}

void omap_stop_lcd_dma(void)
{
	lcd_dma.active = 0;
	if (!enable_1510_mode && lcd_dma.ext_ctrl)
		omap_writew(omap_readw(OMAP1610_DMA_LCD_CCR) & ~(1 << 7),
			    OMAP1610_DMA_LCD_CCR);
}

/*
 * Clears any DMA state so the DMA engine is ready to restart with new buffers
 * through omap_start_dma(). Any buffers in flight are discarded.
 */
void omap_clear_dma(int lch)
{
	unsigned long flags;
	int status;

	local_irq_save(flags);
	omap_writew(omap_readw(OMAP_DMA_CCR(lch)) & ~OMAP_DMA_CCR_EN,
		    OMAP_DMA_CCR(lch));
	status = OMAP_DMA_CSR(lch);	/* clear pending interrupts */
	local_irq_restore(flags);
}

/*
 * Returns current physical source address for the given DMA channel.
 * If the channel is running the caller must disable interrupts prior calling
 * this function and process the returned value before re-enabling interrupt to
 * prevent races with the interrupt handler. Note that in continuous mode there
 * is a chance for CSSA_L register overflow inbetween the two reads resulting
 * in incorrect return value.
 */
dma_addr_t omap_get_dma_src_pos(int lch)
{
	return (dma_addr_t) (OMAP_DMA_CSSA_L(lch) |
			     (OMAP_DMA_CSSA_U(lch) << 16));
}

/*
 * Returns current physical destination address for the given DMA channel.
 * If the channel is running the caller must disable interrupts prior calling
 * this function and process the returned value before re-enabling interrupt to
 * prevent races with the interrupt handler. Note that in continuous mode there
 * is a chance for CDSA_L register overflow inbetween the two reads resulting
 * in incorrect return value.
 */
dma_addr_t omap_get_dma_dst_pos(int lch)
{
	return (dma_addr_t) (OMAP_DMA_CDSA_L(lch) |
			     (OMAP_DMA_CDSA_U(lch) << 16));
}

static int __init omap_init_dma(void)
{
	int ch, r;

	if (cpu_is_omap1510()) {
		printk(KERN_INFO "DMA support for OMAP1510 initialized\n");
		dma_chan_count = 9;
		enable_1510_mode = 1;
	} else if (cpu_is_omap16xx() || cpu_is_omap730()) {
		printk(KERN_INFO "OMAP DMA hardware version %d\n",
		       omap_readw(OMAP_DMA_HW_ID));
		printk(KERN_INFO "DMA capabilities: %08x:%08x:%04x:%04x:%04x\n",
		       (omap_readw(OMAP_DMA_CAPS_0_U) << 16) | omap_readw(OMAP_DMA_CAPS_0_L),
		       (omap_readw(OMAP_DMA_CAPS_1_U) << 16) | omap_readw(OMAP_DMA_CAPS_1_L),
		       omap_readw(OMAP_DMA_CAPS_2), omap_readw(OMAP_DMA_CAPS_3),
		       omap_readw(OMAP_DMA_CAPS_4));
		if (!enable_1510_mode) {
			u16 w;

			/* Disable OMAP 3.0/3.1 compatibility mode. */
			w = omap_readw(OMAP_DMA_GSCR);
			w |= 1 << 3;
			omap_writew(w, OMAP_DMA_GSCR);
			dma_chan_count = 16;
		} else
			dma_chan_count = 9;
	} else {
		dma_chan_count = 0;
		return 0;
	}

	memset(&lcd_dma, 0, sizeof(lcd_dma));
	spin_lock_init(&lcd_dma.lock);
	spin_lock_init(&dma_chan_lock);
	memset(&dma_chan, 0, sizeof(dma_chan));

	for (ch = 0; ch < dma_chan_count; ch++) {
		dma_chan[ch].dev_id = -1;
		dma_chan[ch].next_lch = -1;

		if (ch >= 6 && enable_1510_mode)
			continue;

		/* request_irq() doesn't like dev_id (ie. ch) being zero,
		 * so we have to kludge around this. */
		r = request_irq(dma_irq[ch], dma_irq_handler, 0, "DMA",
				(void *) (ch + 1));
		if (r != 0) {
			int i;

			printk(KERN_ERR "unable to request IRQ %d for DMA (error %d)\n",
			       dma_irq[ch], r);
			for (i = 0; i < ch; i++)
				free_irq(dma_irq[i], (void *) (i + 1));
			return r;
		}
	}
	r = request_irq(INT_DMA_LCD, lcd_dma_irq_handler, 0, "LCD DMA", NULL);
	if (r != 0) {
		int i;

		printk(KERN_ERR "unable to request IRQ for LCD DMA (error %d)\n", r);
		for (i = 0; i < dma_chan_count; i++)
			free_irq(dma_irq[i], (void *) (i + 1));
		return r;
	}
	return 0;
}

arch_initcall(omap_init_dma);


EXPORT_SYMBOL(omap_get_dma_src_pos);
EXPORT_SYMBOL(omap_get_dma_dst_pos);
EXPORT_SYMBOL(omap_clear_dma);
EXPORT_SYMBOL(omap_set_dma_priority);
EXPORT_SYMBOL(omap_request_dma);
EXPORT_SYMBOL(omap_free_dma);
EXPORT_SYMBOL(omap_start_dma);
EXPORT_SYMBOL(omap_stop_dma);
EXPORT_SYMBOL(omap_enable_dma_irq);
EXPORT_SYMBOL(omap_disable_dma_irq);

EXPORT_SYMBOL(omap_set_dma_transfer_params);
EXPORT_SYMBOL(omap_set_dma_color_mode);

EXPORT_SYMBOL(omap_set_dma_src_params);
EXPORT_SYMBOL(omap_set_dma_src_index);
EXPORT_SYMBOL(omap_set_dma_src_data_pack);
EXPORT_SYMBOL(omap_set_dma_src_burst_mode);

EXPORT_SYMBOL(omap_set_dma_dest_params);
EXPORT_SYMBOL(omap_set_dma_dest_index);
EXPORT_SYMBOL(omap_set_dma_dest_data_pack);
EXPORT_SYMBOL(omap_set_dma_dest_burst_mode);

EXPORT_SYMBOL(omap_dma_link_lch);
EXPORT_SYMBOL(omap_dma_unlink_lch);

EXPORT_SYMBOL(omap_request_lcd_dma);
EXPORT_SYMBOL(omap_free_lcd_dma);
EXPORT_SYMBOL(omap_enable_lcd_dma);
EXPORT_SYMBOL(omap_setup_lcd_dma);
EXPORT_SYMBOL(omap_stop_lcd_dma);
EXPORT_SYMBOL(omap_set_lcd_dma_b1);
EXPORT_SYMBOL(omap_set_lcd_dma_single_transfer);
EXPORT_SYMBOL(omap_set_lcd_dma_ext_controller);
EXPORT_SYMBOL(omap_set_lcd_dma_b1_rotation);
EXPORT_SYMBOL(omap_set_lcd_dma_b1_vxres);
EXPORT_SYMBOL(omap_set_lcd_dma_b1_scale);
EXPORT_SYMBOL(omap_set_lcd_dma_b1_mirror);

