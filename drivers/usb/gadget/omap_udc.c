/*
 * omap_udc.c -- for OMAP 1610 udc, with OTG support
 *
 * Copyright (C) 2004 Texas Instruments, Inc.
 * Copyright (C) 2004 David Brownell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#undef	DEBUG
#undef	VERBOSE

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/device.h>
#include <linux/usb_ch9.h>
#include <linux/usb_gadget.h>
#include <linux/usb_otg.h>
#include <linux/dma-mapping.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/unaligned.h>
#include <asm/mach-types.h>

#include <asm/arch/dma.h>
#include <asm/arch/mux.h>
#include <asm/arch/usb.h>

#include "omap_udc.h"

#undef	USB_TRACE

/* OUT-dma seems to be behaving */
#define	USE_DMA

/* ISO too */
#define	USE_ISO


#define	DRIVER_DESC	"OMAP UDC driver"
#define	DRIVER_VERSION	"24 August 2004"

#define	DMA_ADDR_INVALID	(~(dma_addr_t)0)


/*
 * The OMAP UDC needs _very_ early endpoint setup:  before enabling the
 * D+ pullup to allow enumeration.  That's too early for the gadget
 * framework to use from usb_endpoint_enable(), which happens after
 * enumeration as part of activating an interface.  (But if we add an
 * optional new "UDC not yet running" state to the gadget driver model,
 * even just during driver binding, the endpoint autoconfig logic is the
 * natural spot to manufacture new endpoints.)
 *
 * So instead of using endpoint enable calls to control the hardware setup,
 * this driver defines a "fifo mode" parameter.  It's used during driver
 * initialization to choose among a set of pre-defined endpoint configs.
 * See omap_udc_setup() for available modes, or to add others.  That code
 * lives in an init section, so use this driver as a module if you need
 * to change the fifo mode after the kernel boots.
 *
 * Gadget drivers normally ignore endpoints they don't care about, and
 * won't include them in configuration descriptors.  That means only
 * misbehaving hosts would even notice they exist.
 */
#ifdef	USE_ISO
static unsigned fifo_mode = 3;
#else
static unsigned fifo_mode = 0;
#endif

/* "modprobe omap_udc fifo_mode=42", or else as a kernel
 * boot parameter "omap_udc:fifo_mode=42"
 */
module_param (fifo_mode, uint, 0);
MODULE_PARM_DESC (fifo_mode, "endpoint setup (0 == default)");


#ifdef	USE_DMA
static unsigned use_dma = 1;

/* "modprobe omap_udc use_dma=y", or else as a kernel
 * boot parameter "omap_udc:use_dma=y"
 */
module_param (use_dma, bool, 0);
MODULE_PARM_DESC (use_dma, "enable/disable DMA");
#else	/* !USE_DMA */

/* save a bit of code */
#define	use_dma		0
#endif	/* !USE_DMA */


static const char driver_name [] = "omap_udc";
static const char driver_desc [] = DRIVER_DESC;

/*-------------------------------------------------------------------------*/

/* there's a notion of "current endpoint" for modifying endpoint
 * state, and PIO access to its FIFO.  
 */

static void use_ep(struct omap_ep *ep, u16 select)
{
	u16	num = ep->bEndpointAddress & 0x0f;

	if (ep->bEndpointAddress & USB_DIR_IN)
		num |= UDC_EP_DIR;
	UDC_EP_NUM_REG = num | select;
	/* when select, MUST deselect later !! */
}

static inline void deselect_ep(void)
{
	UDC_EP_NUM_REG &= ~UDC_EP_SEL;
	/* 6 wait states before TX will happen */
}

static void dma_channel_claim(struct omap_ep *ep, unsigned preferred);

/*-------------------------------------------------------------------------*/

static int omap_ep_enable(struct usb_ep *_ep,
		const struct usb_endpoint_descriptor *desc)
{
	struct omap_ep	*ep = container_of(_ep, struct omap_ep, ep);
	struct omap_udc	*udc;
	unsigned long	flags;
	u16		maxp;

	/* catch various bogus parameters */
	if (!_ep || !desc || ep->desc
			|| desc->bDescriptorType != USB_DT_ENDPOINT
			|| ep->bEndpointAddress != desc->bEndpointAddress
			|| ep->maxpacket < le16_to_cpu
						(desc->wMaxPacketSize)) {
		DBG("%s, bad ep or descriptor\n", __FUNCTION__);
		return -EINVAL;
	}
	maxp = le16_to_cpu (desc->wMaxPacketSize);
	if ((desc->bmAttributes == USB_ENDPOINT_XFER_BULK
				&& maxp != ep->maxpacket)
			|| desc->wMaxPacketSize > ep->maxpacket
			|| !desc->wMaxPacketSize) {
		DBG("%s, bad %s maxpacket\n", __FUNCTION__, _ep->name);
		return -ERANGE;
	}

#ifdef	USE_ISO
	if ((desc->bmAttributes == USB_ENDPOINT_XFER_ISOC
				&& desc->bInterval != 1)) {
		/* hardware wants period = 1; USB allows 2^(Interval-1) */
		DBG("%s, unsupported ISO period %dms\n", _ep->name,
				1 << (desc->bInterval - 1));
		return -EDOM;
	}
#else
	if (desc->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		DBG("%s, ISO nyet\n", _ep->name);
		return -EDOM;
	}
#endif

	/* xfer types must match, except that interrupt ~= bulk */
	if (ep->bmAttributes != desc->bmAttributes
			&& ep->bmAttributes != USB_ENDPOINT_XFER_BULK
			&& desc->bmAttributes != USB_ENDPOINT_XFER_INT) {
		DBG("%s, %s type mismatch\n", __FUNCTION__, _ep->name);
		return -EINVAL;
	}

	udc = ep->udc;
	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN) {
		DBG("%s, bogus device state\n", __FUNCTION__);
		return -ESHUTDOWN;
	}

	spin_lock_irqsave(&udc->lock, flags);

	ep->desc = desc;
	ep->irqs = 0;
	ep->stopped = 0;
	ep->ep.maxpacket = maxp;

	/* set endpoint to initial state */
	ep->dma_channel = 0;
	ep->has_dma = 0;
	ep->lch = -1;
	use_ep(ep, UDC_EP_SEL);
	UDC_CTRL_REG = UDC_RESET_EP;
	ep->ackwait = 0;
	deselect_ep();

	if (ep->bmAttributes == USB_ENDPOINT_XFER_ISOC)
		list_add(&ep->iso, &udc->iso);

	/* maybe assign a DMA channel to this endpoint */
	if (use_dma && desc->bmAttributes == USB_ENDPOINT_XFER_BULK
			&& !(ep->bEndpointAddress & USB_DIR_IN))
			/* FIXME ISO can dma, but prefers first channel.
			 * IN can dma, but lacks debugging.
			 */
		dma_channel_claim(ep, 0);

	/* PIO OUT may RX packets */
	if (desc->bmAttributes != USB_ENDPOINT_XFER_ISOC
			&& !ep->has_dma
			&& !(ep->bEndpointAddress & USB_DIR_IN))
		UDC_CTRL_REG = UDC_SET_FIFO_EN;

	spin_unlock_irqrestore(&udc->lock, flags);
	VDBG("%s enabled\n", _ep->name);
	return 0;
}

static void nuke(struct omap_ep *, int status);

static int omap_ep_disable(struct usb_ep *_ep)
{
	struct omap_ep	*ep = container_of(_ep, struct omap_ep, ep);
	unsigned long	flags;

	if (!_ep || !ep->desc) {
		DBG("%s, %s not enabled\n", __FUNCTION__,
			_ep ? ep->ep.name : NULL);
		return -EINVAL;
	}

	spin_lock_irqsave(&ep->udc->lock, flags);
	ep->desc = 0;
	nuke (ep, -ESHUTDOWN);
	ep->ep.maxpacket = ep->maxpacket;
	ep->has_dma = 0;
	UDC_CTRL_REG = UDC_SET_HALT;
	list_del_init(&ep->iso);

	spin_unlock_irqrestore(&ep->udc->lock, flags);

	VDBG("%s disabled\n", _ep->name);
	return 0;
}

/*-------------------------------------------------------------------------*/

static struct usb_request *
omap_alloc_request(struct usb_ep *ep, int gfp_flags)
{
	struct omap_req	*req;

	req = kmalloc(sizeof *req, gfp_flags);
	if (req) {
		memset (req, 0, sizeof *req);
		req->req.dma = DMA_ADDR_INVALID;
		INIT_LIST_HEAD (&req->queue);
	}
	return &req->req;
}

static void
omap_free_request(struct usb_ep *ep, struct usb_request *_req)
{
	struct omap_req	*req = container_of(_req, struct omap_req, req);

	if (_req)
		kfree (req);
}

/*-------------------------------------------------------------------------*/

static void *
omap_alloc_buffer(
	struct usb_ep	*_ep,
	unsigned	bytes,
	dma_addr_t	*dma,
	int		gfp_flags
)
{
	void		*retval;
	struct omap_ep	*ep;

	ep = container_of(_ep, struct omap_ep, ep);
	if (use_dma && ep->has_dma) {
		static int	warned;
		if (!warned && bytes < PAGE_SIZE) {
			dev_warn(ep->udc->gadget.dev.parent,
				"using dma_alloc_coherent for "
				"small allocations wastes memory\n");
			warned++;
		}
		return dma_alloc_coherent(ep->udc->gadget.dev.parent,
				bytes, dma, gfp_flags);
	}

	retval = kmalloc(bytes, gfp_flags);
	if (retval)
		*dma = virt_to_phys(retval);
	return retval;
}

static void omap_free_buffer(
	struct usb_ep	*_ep,
	void		*buf,
	dma_addr_t	dma,
	unsigned	bytes
)
{
	struct omap_ep	*ep;

	ep = container_of(_ep, struct omap_ep, ep);
	if (use_dma && _ep && ep->has_dma)
		dma_free_coherent(ep->udc->gadget.dev.parent, bytes, buf, dma);
	else
		kfree (buf);
}

/*-------------------------------------------------------------------------*/

static void
done(struct omap_ep *ep, struct omap_req *req, int status)
{
	unsigned		stopped = ep->stopped;

	list_del_init(&req->queue);

	if (req->req.status == -EINPROGRESS)
		req->req.status = status;
	else
		status = req->req.status;

	if (use_dma && ep->has_dma) {
		if (req->mapped) {
			dma_unmap_single(ep->udc->gadget.dev.parent,
				req->req.dma, req->req.length,
				(ep->bEndpointAddress & USB_DIR_IN)
					? DMA_TO_DEVICE
					: DMA_FROM_DEVICE);
			req->req.dma = DMA_ADDR_INVALID;
			req->mapped = 0;
		} else
			dma_sync_single_for_cpu(ep->udc->gadget.dev.parent,
				req->req.dma, req->req.length,
				(ep->bEndpointAddress & USB_DIR_IN)
					? DMA_TO_DEVICE
					: DMA_FROM_DEVICE);
	}

#ifndef	USB_TRACE
	if (status && status != -ESHUTDOWN)
#endif
		VDBG("complete %s req %p stat %d len %u/%u\n",
			ep->ep.name, &req->req, status,
			req->req.actual, req->req.length);

	/* don't modify queue heads during completion callback */
	ep->stopped = 1;
	spin_unlock(&ep->udc->lock);
	req->req.complete(&ep->ep, &req->req);
	spin_lock(&ep->udc->lock);
	ep->stopped = stopped;
}

/*-------------------------------------------------------------------------*/

#define	FIFO_FULL	(UDC_NON_ISO_FIFO_FULL | UDC_ISO_FIFO_FULL)
#define	FIFO_UNWRITABLE	(UDC_EP_HALTED | FIFO_FULL)

#define FIFO_EMPTY	(UDC_NON_ISO_FIFO_EMPTY | UDC_ISO_FIFO_EMPTY)
#define FIFO_UNREADABLE (UDC_EP_HALTED | FIFO_EMPTY)

static inline int 
write_packet(u8 *buf, struct omap_req *req, unsigned max)
{
	unsigned	len;
	u16		*wp;

	len = min(req->req.length - req->req.actual, max);
	req->req.actual += len;

	max = len;
	if (likely((((int)buf) & 1) == 0)) {
		wp = (u16 *)buf;
		while (max >= 2) {
			UDC_DATA_REG = *wp++;
			max -= 2;
		}
		buf = (u8 *)wp;
	}
	while (max--)
		*(volatile u8 *)&UDC_DATA_REG = *buf++;
	return len;
}

// FIXME change r/w fifo calling convention


// return:  0 = still running, 1 = completed, negative = errno
static int write_fifo(struct omap_ep *ep, struct omap_req *req)
{
	u8		*buf;
	unsigned	count;
	int		is_last;
	u16		ep_stat;

	buf = req->req.buf + req->req.actual;
	prefetch(buf);

	/* PIO-IN isn't double buffered except for iso */
	ep_stat = UDC_STAT_FLG_REG;
	if (ep_stat & FIFO_UNWRITABLE)
		return 0;

	count = ep->ep.maxpacket;
	count = write_packet(buf, req, count);
	UDC_CTRL_REG = UDC_SET_FIFO_EN;
	ep->ackwait = 1;

	/* last packet is often short (sometimes a zlp) */
	if (count != ep->ep.maxpacket)
		is_last = 1;
	else if (req->req.length == req->req.actual
			&& !req->req.zero)
		is_last = 1;
	else
		is_last = 0;

	/* NOTE:  requests complete when all IN data is in a
	 * FIFO (or sometimes later, if a zlp was needed).
	 * Use usb_ep_fifo_status() where needed.
	 */
	if (is_last)
		done(ep, req, 0);
	return is_last;
}

static inline int 
read_packet(u8 *buf, struct omap_req *req, unsigned avail)
{
	unsigned	len;
	u16		*wp;

	len = min(req->req.length - req->req.actual, avail);
	req->req.actual += len;
	avail = len;

	if (likely((((int)buf) & 1) == 0)) {
		wp = (u16 *)buf;
		while (avail >= 2) {
			*wp++ = UDC_DATA_REG;
			avail -= 2;
		}
		buf = (u8 *)wp;
	}
	while (avail--)
		*buf++ = *(volatile u8 *)&UDC_DATA_REG;
	return len;
}

// return:  0 = still running, 1 = queue empty, negative = errno
static int read_fifo(struct omap_ep *ep, struct omap_req *req)
{
	u8		*buf;
	unsigned	count, avail;
	int		is_last;

	buf = req->req.buf + req->req.actual;
	prefetchw(buf);

	for (;;) {
		u16	ep_stat = UDC_STAT_FLG_REG;

		is_last = 0;
		if (ep_stat & FIFO_UNREADABLE)
			break;

		if (ep_stat & (UDC_NON_ISO_FIFO_FULL|UDC_ISO_FIFO_FULL))
			avail = ep->ep.maxpacket;
		else 
			avail = UDC_RXFSTAT_REG;
		count = read_packet(buf, req, avail);

		// FIXME double buffered PIO OUT wasn't behaving...

		/* partial packet reads may not be errors */
		if (count < ep->ep.maxpacket) {
			is_last = 1;
			/* overflowed this request?  flush extra data */
			if (count != avail) {
				req->req.status = -EOVERFLOW;
				avail -= count;
				while (avail--)
					(void) *(volatile u8 *)&UDC_DATA_REG;
			}
		} else if (req->req.length == req->req.actual)
			is_last = 1;
		else
			is_last = 0;

		if (!ep->bEndpointAddress)
			break;
		if (!ep->double_buf) {
			UDC_CTRL_REG = UDC_SET_FIFO_EN;
			if (!is_last)
				break;
		}

		if (is_last) {
			done(ep, req, 0);
			if (list_empty(&ep->queue) || !ep->double_buf)
				break;
			req = container_of(ep->queue.next,
					struct omap_req, queue);
			is_last = 0;
		}
	}
	return is_last;
}

/*-------------------------------------------------------------------------*/

/* Each USB transfer request using DMA maps to one or more DMA transfers.
 * When DMA completion isn't request completion, the UDC continues with
 * the next DMA transfer for that USB transfer.
 */

static void next_in_dma(struct omap_ep *ep, struct omap_req *req)
{
	u16		txdma_ctrl;
	unsigned	length = req->req.length - req->req.actual;

	/* measure length in either bytes or packets */
	if (length <= (UDC_TXN_TSC + 1)) {
		txdma_ctrl = UDC_TXN_EOT | length;
		omap_set_dma_transfer_params(ep->lch, OMAP_DMA_DATA_TYPE_S8,
				length, 1, OMAP_DMA_SYNC_ELEMENT);
	} else {
		length = max(length / ep->maxpacket,
				(unsigned) UDC_TXN_TSC + 1);
 		txdma_ctrl = length;
		omap_set_dma_transfer_params(ep->lch, OMAP_DMA_DATA_TYPE_S8,
				ep->ep.maxpacket, length,
				OMAP_DMA_SYNC_ELEMENT);
		length *= ep->maxpacket;
	}

	omap_set_dma_src_params(ep->lch, OMAP_DMA_PORT_EMIFF,
		OMAP_DMA_AMODE_POST_INC, req->req.dma + req->req.actual);

	omap_start_dma(ep->lch);
	UDC_DMA_IRQ_EN_REG |= UDC_TX_DONE_IE(ep->dma_channel);
	UDC_TXDMA_REG(ep->dma_channel) = UDC_TXN_START | txdma_ctrl;
	req->dma_bytes = length;
}

static void finish_in_dma(struct omap_ep *ep, struct omap_req *req, int status)
{
	if (status == 0) {
		req->req.actual += req->dma_bytes;

		/* return if this request needs to send data or zlp */
		if (req->req.actual < req->req.length)
			return;
		if (req->req.zero
				&& req->dma_bytes != 0
				&& (req->req.actual % ep->maxpacket) == 0)
			return;
	} else {
		u32	last;

		// FIXME this surely isn't #bytes transferred
		last = (omap_readw(OMAP_DMA_CSSA_U(ep->lch)) << 16)
				| omap_readw(OMAP_DMA_CSSA_L(ep->lch));
		req->req.actual = last - req->req.dma;
	}

	/* tx completion */
	omap_stop_dma(ep->lch);
	UDC_DMA_IRQ_EN_REG &= ~UDC_TX_DONE_IE(ep->dma_channel);
	done(ep, req, status);
}

static void next_out_dma(struct omap_ep *ep, struct omap_req *req)
{
	unsigned packets;

	/* NOTE:  we filtered out "short reads" before, so we know
	 * the buffer has only whole numbers of packets.
	 */

	/* set up this DMA transfer, enable the fifo, start */
	packets = (req->req.length - req->req.actual) / ep->ep.maxpacket;
	packets = min(packets, (unsigned)UDC_RXN_TC + 1);
	req->dma_bytes = packets * ep->ep.maxpacket;
	omap_set_dma_transfer_params(ep->lch, OMAP_DMA_DATA_TYPE_S8,
			ep->ep.maxpacket, packets,
			OMAP_DMA_SYNC_ELEMENT);
	omap_set_dma_dest_params(ep->lch, OMAP_DMA_PORT_EMIFF,
		OMAP_DMA_AMODE_POST_INC, req->req.dma + req->req.actual);

	UDC_RXDMA_REG(ep->dma_channel) = UDC_RXN_STOP | (packets - 1);
	UDC_DMA_IRQ_EN_REG |= UDC_RX_EOT_IE(ep->dma_channel);
	UDC_EP_NUM_REG = (ep->bEndpointAddress & 0xf);
	UDC_CTRL_REG = UDC_SET_FIFO_EN;

	omap_start_dma(ep->lch);
}

static void
finish_out_dma(struct omap_ep *ep, struct omap_req *req, int status)
{
	u16	count;

	/* FIXME must be a better way to see how much dma
	 * happened, even when it never got going...
	 */
	count = omap_readw(OMAP_DMA_CDAC(ep->lch));
	count -= 0xffff & (req->req.dma + req->req.actual);
	count += req->req.actual;
	if (count <= req->req.length)
		req->req.actual = count;
	
	if (count != req->dma_bytes || status)
		omap_stop_dma(ep->lch);

	/* if this wasn't short, request may need another transfer */
	else if (req->req.actual < req->req.length)
		return;

	/* rx completion */
	UDC_DMA_IRQ_EN_REG &= ~UDC_RX_EOT_IE(ep->dma_channel);
	done(ep, req, status);
}

static void dma_irq(struct omap_udc *udc, u16 irq_src)
{
	u16		dman_stat = UDC_DMAN_STAT_REG;
	struct omap_ep	*ep;
	struct omap_req	*req;

	/* IN dma: tx to host */
	if (irq_src & UDC_TXN_DONE) {
		ep = &udc->ep[16 + UDC_DMA_TX_SRC(dman_stat)];
		ep->irqs++;
		/* can see TXN_DONE after dma abort */
		if (!list_empty(&ep->queue)) {
			req = container_of(ep->queue.next,
						struct omap_req, queue);
			finish_in_dma(ep, req, 0);
		}
		UDC_IRQ_SRC_REG = UDC_TXN_DONE;

		if (!list_empty (&ep->queue)) {
			req = container_of(ep->queue.next,
					struct omap_req, queue);
			next_in_dma(ep, req);
		}
	}

	/* OUT dma: rx from host */
	if (irq_src & UDC_RXN_EOT) {
		ep = &udc->ep[UDC_DMA_RX_SRC(dman_stat)];
		ep->irqs++;
		/* can see RXN_EOT after dma abort */
		if (!list_empty(&ep->queue)) {
			req = container_of(ep->queue.next,
					struct omap_req, queue);
			finish_out_dma(ep, req, 0);
		}
		UDC_IRQ_SRC_REG = UDC_RXN_EOT;

		if (!list_empty (&ep->queue)) {
			req = container_of(ep->queue.next,
					struct omap_req, queue);
			next_out_dma(ep, req);
		}
	}

	if (irq_src & UDC_RXN_CNT) {
		ep = &udc->ep[UDC_DMA_RX_SRC(dman_stat)];
		DBG("%s, RX_CNT irq?\n", ep->ep.name);
		UDC_IRQ_SRC_REG = UDC_RXN_CNT;
	}
}

static void dma_error(int lch, u16 ch_status, void *data)
{
	struct omap_ep	*ep = data;

	/* if ch_status & OMAP_DMA_DROP_IRQ ... */
	/* if ch_status & OMAP_DMA_TOUT_IRQ ... */
	ERR("%s dma error, lch %d status %02x\n", ep->ep.name, lch, ch_status);

	/* complete current transfer ... */
}

static void dma_channel_claim(struct omap_ep *ep, unsigned channel)
{
	u16	reg;
	int	status, restart, is_in;

	is_in = ep->bEndpointAddress & USB_DIR_IN;
	if (is_in)
		reg = UDC_TXDMA_CFG_REG;
	else
		reg = UDC_RXDMA_CFG_REG;
	reg |= 1 << 12;		/* "pulse" activated */

	ep->dma_channel = 0;
	ep->lch = -1;
	if (channel == 0 || channel > 3) {
		if ((reg & 0x0f00) == 0)
			channel = 3;
		else if ((reg & 0x00f0) == 0)
			channel = 2;
		else if ((reg & 0x000f) == 0)	/* preferred for ISO */
			channel = 1;
		else {
			status = -EMLINK;
			goto just_restart;
		}
	}
	reg |= (0x0f & ep->bEndpointAddress) << (4 * (channel - 1));
	ep->dma_channel = channel;

	if (is_in) {
		status = omap_request_dma(OMAP_DMA_USB_W2FC_TX0 - 1 + channel,
			ep->ep.name, dma_error, ep, &ep->lch);
		if (status == 0) {
			UDC_TXDMA_CFG_REG = reg;
			omap_set_dma_dest_params(ep->lch,
				OMAP_DMA_PORT_TIPB,
				OMAP_DMA_AMODE_CONSTANT,
				(unsigned long) io_v2p((u32)&UDC_DATA_DMA_REG));
		}
	} else {
		status = omap_request_dma(OMAP_DMA_USB_W2FC_RX0 - 1 + channel,
			ep->ep.name, dma_error, ep, &ep->lch);
		if (status == 0) {
			UDC_RXDMA_CFG_REG = reg;
			omap_set_dma_src_params(ep->lch,
				OMAP_DMA_PORT_TIPB,
				OMAP_DMA_AMODE_CONSTANT,
				(unsigned long) io_v2p((u32)&UDC_DATA_DMA_REG));
		}
	}
	if (status)
		ep->dma_channel = 0;
	else {
		ep->has_dma = 1;
		omap_disable_dma_irq(ep->lch, OMAP_DMA_BLOCK_IRQ);

		/* channel type P: hw synch (fifo) */
		omap_writew(2, OMAP_DMA_LCH_CTRL(ep->lch));
	}

just_restart:
	/* restart any queue, even if the claim failed  */
	restart = !ep->stopped && !list_empty(&ep->queue);

	if (status)
		DBG("%s no dma channel: %d%s\n", ep->ep.name, status,
			restart ? " (restart)" : "");
	else
		DBG("%s claimed %cxdma%d lch %d%s\n", ep->ep.name,
			is_in ? 't' : 'r',
			ep->dma_channel - 1, ep->lch,
			restart ? " (restart)" : "");

	if (restart) {
		struct omap_req	*req;
		req = container_of(ep->queue.next, struct omap_req, queue);
		if (ep->has_dma)
			(is_in ? next_in_dma : next_out_dma)(ep, req);
		else {
			use_ep(ep, UDC_EP_SEL);
			(is_in ? write_fifo : read_fifo)(ep, req);
			deselect_ep();
			/* IN: 6 wait states before it'll tx */
		}
	}
}

static void dma_channel_release(struct omap_ep *ep)
{
	int		shift = 4 * (ep->dma_channel - 1);
	u16		mask = 0x0f << shift;
	struct omap_req	*req;
	int		active;

	/* abort any active usb transfer request */
	if (!list_empty(&ep->queue))
		req = container_of(ep->queue.next, struct omap_req, queue);
	else
		req = 0;

	active = ((1 << 7) & omap_readl(OMAP_DMA_CCR(ep->lch))) != 0;

	DBG("%s release %s %cxdma%d %p\n", ep->ep.name,
			active ? "active" : "idle",
			(ep->bEndpointAddress & USB_DIR_IN) ? 't' : 'r',
			ep->dma_channel - 1, req);

	/* wait till current packet DMA finishes, and fifo empties */
	if (ep->bEndpointAddress & USB_DIR_IN) {
		UDC_TXDMA_CFG_REG &= ~mask;

		if (req) {
			if (active)
				udelay(50);
			finish_in_dma(ep, req, -ECONNRESET);
			if (UDC_TXDMA_CFG_REG & mask)
				WARN("%s, SPIN abort TX dma\n", ep->ep.name);
		}

		/* host may empty the fifo (or not...) */
		while (UDC_TXDMA_CFG_REG & mask)
			udelay(10);

	} else {
		UDC_RXDMA_CFG_REG &= ~mask;

		/* dma empties the fifo */
		while (active && (UDC_RXDMA_CFG_REG & mask))
			udelay(10);
		omap_stop_dma(ep->lch);
		if (req)
			finish_out_dma(ep, req, -ECONNRESET);
	}
	omap_free_dma(ep->lch);
	ep->dma_channel = 0;
	ep->lch = -1;
	/* has_dma still set, till endpoint is fully quiesced */
}


/*-------------------------------------------------------------------------*/

static int
omap_ep_queue(struct usb_ep *_ep, struct usb_request *_req, int gfp_flags)
{
	struct omap_ep	*ep = container_of(_ep, struct omap_ep, ep);
	struct omap_req	*req = container_of(_req, struct omap_req, req);
	struct omap_udc	*udc;
	unsigned long	flags;
	int		is_iso = 0;

	/* catch various bogus parameters */
	if (!_req || !req->req.complete || !req->req.buf
			|| !list_empty(&req->queue)) {
		DBG("%s, bad params\n", __FUNCTION__);
		return -EINVAL;
	}
	if (!_ep || (!ep->desc && ep->bEndpointAddress)) {
		DBG("%s, bad ep\n", __FUNCTION__);
		return -EINVAL;
	}
	if (ep->bmAttributes == USB_ENDPOINT_XFER_ISOC) {
		if (req->req.length > ep->ep.maxpacket)
			return -EMSGSIZE;
		is_iso = 1;
	}

	/* this isn't bogus, but OMAP DMA isn't the only hardware to
	 * have a hard time with partial packet reads...  reject it.
	 */
	if (use_dma
			&& ep->has_dma
			&& ep->bEndpointAddress != 0
			&& (ep->bEndpointAddress & USB_DIR_IN) == 0
			&& (req->req.length % ep->ep.maxpacket) != 0) {
		DBG("%s, no partial packet OUT reads\n", __FUNCTION__);
		return -EMSGSIZE;
	}

	udc = ep->udc;
	if (!udc->driver || udc->gadget.speed == USB_SPEED_UNKNOWN)
		return -ESHUTDOWN;

	if (use_dma && ep->has_dma) {
		if (req->req.dma == DMA_ADDR_INVALID) {
			req->req.dma = dma_map_single(
				ep->udc->gadget.dev.parent,
				req->req.buf,
				req->req.length,
				(ep->bEndpointAddress & USB_DIR_IN)
					? DMA_TO_DEVICE
					: DMA_FROM_DEVICE);
			req->mapped = 1;
		} else {
			dma_sync_single_for_device(
				ep->udc->gadget.dev.parent,
				req->req.dma, req->req.length,
				(ep->bEndpointAddress & USB_DIR_IN)
					? DMA_TO_DEVICE
					: DMA_FROM_DEVICE);
			req->mapped = 0;
		}
	}

	VDBG("%s queue req %p, len %d buf %p\n",
		ep->ep.name, _req, _req->length, _req->buf);

	spin_lock_irqsave(&udc->lock, flags);

	req->req.status = -EINPROGRESS;
	req->req.actual = 0;

	/* maybe kickstart non-iso i/o queues */
	if (is_iso)
		UDC_IRQ_EN_REG |= UDC_SOF_IE;
	else if (list_empty(&ep->queue) && !ep->stopped && !ep->ackwait) {
		int	is_in;

		if (ep->bEndpointAddress == 0) {
			if (!udc->ep0_pending || !list_empty (&ep->queue)) {
				spin_unlock_irqrestore(&udc->lock, flags);
				return -EL2HLT;
			}

			/* empty DATA stage? */
			is_in = udc->ep0_in;
			if (!req->req.length) {

				/* chip became CONFIGURED or ADDRESSED
				 * earlier; drivers may already have queued
				 * requests to non-control endpoints
				 */
				if (udc->ep0_set_config) {
					u16	irq_en = UDC_IRQ_EN_REG;

					irq_en |= UDC_DS_CHG_IE | UDC_EP0_IE;
					if (!udc->ep0_reset_config)
						irq_en |= UDC_EPN_RX_IE
							| UDC_EPN_TX_IE;
					UDC_IRQ_EN_REG = irq_en;
				}

				/* STATUS is reverse direction */
				UDC_EP_NUM_REG = is_in
						? UDC_EP_SEL
						: (UDC_EP_SEL|UDC_EP_DIR);
				UDC_CTRL_REG = UDC_CLR_EP;
				UDC_CTRL_REG = UDC_SET_FIFO_EN;
				UDC_EP_NUM_REG = udc->ep0_in ? 0 : UDC_EP_DIR;

				/* cleanup */
				udc->ep0_pending = 0;
				done(ep, req, 0);
				req = 0;

			/* non-empty DATA stage */
			} else if (is_in) {
				UDC_EP_NUM_REG = UDC_EP_SEL|UDC_EP_DIR;
			} else {
				if (udc->ep0_setup)
					goto irq_wait;
				UDC_EP_NUM_REG = UDC_EP_SEL;
			}
		} else {
			is_in = ep->bEndpointAddress & USB_DIR_IN;
			if (!ep->has_dma)
				use_ep(ep, UDC_EP_SEL);
			/* if ISO: SOF IRQs must be enabled/disabled! */
		}

		if (ep->has_dma)
			(is_in ? next_in_dma : next_out_dma)(ep, req);
		else if (req) {
			if ((is_in ? write_fifo : read_fifo)(ep, req) == 1)
				req = 0;
			deselect_ep();
			/* IN: 6 wait states before it'll tx */
		}
	}

irq_wait:
	/* irq handler advances the queue */
	if (req != 0)
		list_add_tail(&req->queue, &ep->queue);
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int omap_ep_dequeue(struct usb_ep *_ep, struct usb_request *_req)
{
	struct omap_ep	*ep = container_of(_ep, struct omap_ep, ep);
	struct omap_req	*req;
	unsigned long	flags;

	if (!_ep || !_req)
		return -EINVAL;

	spin_lock_irqsave(&ep->udc->lock, flags);

	/* make sure it's actually queued on this endpoint */
	list_for_each_entry (req, &ep->queue, queue) {
		if (&req->req == _req)
			break;
	}
	if (&req->req != _req) {
		spin_unlock_irqrestore(&ep->udc->lock, flags);
		return -EINVAL;
	}

	if (use_dma && ep->dma_channel && ep->queue.next == &req->queue) {
		int channel = ep->dma_channel;

		/* releasing the dma completion cancels the request,
		 * reclaiming the channel restarts the queue
		 */
		dma_channel_release(ep);
		dma_channel_claim(ep, channel);
	} else 
		done(ep, req, -ECONNRESET);
	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return 0;
}

/*-------------------------------------------------------------------------*/

static int omap_ep_set_halt(struct usb_ep *_ep, int value)
{
	struct omap_ep	*ep = container_of(_ep, struct omap_ep, ep);
	unsigned long	flags;
	int		status = -EOPNOTSUPP;

	spin_lock_irqsave(&ep->udc->lock, flags);

	/* just use protocol stalls for ep0; real halts are annoying */
	if (ep->bEndpointAddress == 0) {
		if (!ep->udc->ep0_pending)
			status = -EINVAL;
		else if (value) {
			if (ep->udc->ep0_set_config) {
				WARN("error changing config?\n");
				UDC_SYSCON2_REG = UDC_CLR_CFG;
			}
			UDC_SYSCON2_REG = UDC_STALL_CMD;
			ep->udc->ep0_pending = 0;
			status = 0;
		} else /* NOP */
			status = 0;

	/* otherwise, all active non-ISO endpoints can halt */
	} else if (ep->bmAttributes != USB_ENDPOINT_XFER_ISOC && ep->desc) {

		/* IN endpoints must already be idle */
		if ((ep->bEndpointAddress & USB_DIR_IN)
				&& !list_empty(&ep->queue)) { 
			status = -EAGAIN;
			goto done;
		}

		if (value) {
			int	channel;

			if (use_dma && ep->dma_channel
					&& !list_empty(&ep->queue)) {
				channel = ep->dma_channel;
				dma_channel_release(ep);
			} else
				channel = 0;

			use_ep(ep, UDC_EP_SEL);
			if (UDC_STAT_FLG_REG & UDC_NON_ISO_FIFO_EMPTY) {
				UDC_CTRL_REG = UDC_SET_HALT;
				status = 0;
			} else
				status = -EAGAIN;
			deselect_ep();

			if (channel)
				dma_channel_claim(ep, channel);
		} else {
			use_ep(ep, 0);
			UDC_CTRL_REG = UDC_RESET_EP;
			ep->ackwait = 0;
			if (!(ep->bEndpointAddress & USB_DIR_IN))
				UDC_CTRL_REG = UDC_SET_FIFO_EN;
		}
	}
done:
	VDBG("%s %s halt stat %d\n", ep->ep.name,
		value ? "set" : "clear", status);

	spin_unlock_irqrestore(&ep->udc->lock, flags);
	return status;
}

static struct usb_ep_ops omap_ep_ops = {
	.enable		= omap_ep_enable,
	.disable	= omap_ep_disable,

	.alloc_request	= omap_alloc_request,
	.free_request	= omap_free_request,

	.alloc_buffer	= omap_alloc_buffer,
	.free_buffer	= omap_free_buffer,

	.queue		= omap_ep_queue,
	.dequeue	= omap_ep_dequeue,

	.set_halt	= omap_ep_set_halt,
	// fifo_status ... report bytes in fifo
	// fifo_flush ... flush fifo
};

/*-------------------------------------------------------------------------*/

static int omap_get_frame(struct usb_gadget *gadget)
{
	u16	sof = UDC_SOF_REG;
	return (sof & UDC_TS_OK) ? (sof & UDC_TS) : -EL2NSYNC;
}

static int omap_wakeup(struct usb_gadget *gadget)
{
	struct omap_udc	*udc;
	unsigned long	flags;
	int		retval = -EHOSTUNREACH;

	udc = container_of(gadget, struct omap_udc, gadget);

	spin_lock_irqsave(&udc->lock, flags);
	if (udc->devstat & UDC_SUS) {
		/* NOTE:  OTG spec erratum says that OTG devices may
		 * issue wakeups without host enable.
		 */
		if (udc->devstat & (UDC_B_HNP_ENABLE|UDC_R_WK_OK)) {
			DBG("remote wakeup...\n");
			UDC_SYSCON2_REG = UDC_RMT_WKP;
			retval = 0;
		}

	/* NOTE:  non-OTG systems may use SRP TOO... */
	} else if (!(udc->devstat & UDC_ATT)) {
		if (udc->transceiver)
			retval = otg_start_srp(udc->transceiver);
	}
	spin_unlock_irqrestore(&udc->lock, flags);

	return retval;
}

static int
omap_set_selfpowered(struct usb_gadget *gadget, int is_selfpowered)
{
	struct omap_udc	*udc;
	unsigned long	flags;
	u16		syscon1;

	udc = container_of(gadget, struct omap_udc, gadget);
	spin_lock_irqsave(&udc->lock, flags);
	syscon1 = UDC_SYSCON1_REG;
	if (is_selfpowered)
		syscon1 |= UDC_SELF_PWR;
	else
		syscon1 &= ~UDC_SELF_PWR;
	UDC_SYSCON1_REG = syscon1;
	spin_unlock_irqrestore(&udc->lock, flags);

	return 0;
}

static int can_pullup(struct omap_udc *udc)
{
	return udc->driver && udc->softconnect && udc->vbus_active;
}

static void pullup_enable(struct omap_udc *udc)
{
	UDC_SYSCON1_REG |= UDC_PULLUP_EN;
#ifndef CONFIG_USB_OTG
	OTG_CTRL_REG |= OTG_BSESSVLD;
#endif
	UDC_IRQ_EN_REG = UDC_DS_CHG_IE;
}

static void pullup_disable(struct omap_udc *udc)
{
#ifndef CONFIG_USB_OTG
	OTG_CTRL_REG &= ~OTG_BSESSVLD;
#endif
	UDC_IRQ_EN_REG = UDC_DS_CHG_IE;
	UDC_SYSCON1_REG &= ~UDC_PULLUP_EN;
}

/*
 * Called by whatever detects VBUS sessions:  external transceiver
 * driver, or maybe GPIO0 VBUS IRQ.
 */
static int omap_vbus_session(struct usb_gadget *gadget, int is_active)
{
	struct omap_udc	*udc;
	unsigned long	flags;

	udc = container_of(gadget, struct omap_udc, gadget);
	spin_lock_irqsave(&udc->lock, flags);
	VDBG("VBUS %s\n", is_active ? "on" : "off");
	udc->vbus_active = (is_active != 0);
	if (can_pullup(udc))
		pullup_enable(udc);
	else
		pullup_disable(udc);
	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

static int omap_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{
	struct omap_udc	*udc;

	udc = container_of(gadget, struct omap_udc, gadget);
	if (udc->transceiver)
		return otg_set_power(udc->transceiver, mA);
	return -EOPNOTSUPP;
}

static int omap_pullup(struct usb_gadget *gadget, int is_on)
{
	struct omap_udc	*udc;
	unsigned long	flags;

	udc = container_of(gadget, struct omap_udc, gadget);
	spin_lock_irqsave(&udc->lock, flags);
	udc->softconnect = (is_on != 0);
	if (can_pullup(udc))
		pullup_enable(udc);
	else
		pullup_disable(udc);
	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

static struct usb_gadget_ops omap_gadget_ops = {
	.get_frame		= omap_get_frame,
	.wakeup			= omap_wakeup,
	.set_selfpowered	= omap_set_selfpowered,
	.vbus_session		= omap_vbus_session,
	.vbus_draw		= omap_vbus_draw,
	.pullup			= omap_pullup,
};

/*-------------------------------------------------------------------------*/

/* dequeue ALL requests; caller holds udc->lock */
static void nuke(struct omap_ep *ep, int status)
{
	struct omap_req	*req;

	ep->stopped = 1;

	if (use_dma && ep->dma_channel)
		dma_channel_release(ep);

	use_ep(ep, 0);
	UDC_CTRL_REG = UDC_CLR_EP;
	if (ep->bEndpointAddress && ep->bmAttributes != USB_ENDPOINT_XFER_ISOC)
		UDC_CTRL_REG = UDC_SET_HALT;

	while (!list_empty(&ep->queue)) {
		req = list_entry(ep->queue.next, struct omap_req, queue);
		done(ep, req, status);
	}
}

/* caller holds udc->lock */
static void udc_quiesce(struct omap_udc *udc)
{
	struct omap_ep	*ep;

	udc->gadget.speed = USB_SPEED_UNKNOWN;
	nuke(&udc->ep[0], -ESHUTDOWN);
	list_for_each_entry (ep, &udc->gadget.ep_list, ep.ep_list)
		nuke(ep, -ESHUTDOWN);
}

/*-------------------------------------------------------------------------*/

static void update_otg(struct omap_udc *udc)
{
	u16	devstat;

	if (!udc->gadget.is_otg)
		return;

	if (OTG_CTRL_REG & OTG_ID)
		devstat = UDC_DEVSTAT_REG;
	else
		devstat = 0;

	udc->gadget.b_hnp_enable = !!(devstat & UDC_B_HNP_ENABLE);
	udc->gadget.a_hnp_support = !!(devstat & UDC_A_HNP_SUPPORT);
	udc->gadget.a_alt_hnp_support = !!(devstat & UDC_A_ALT_HNP_SUPPORT);

	/* Enable HNP early, avoiding races on suspend irq path.
	 * ASSUMES OTG state machine B_BUS_REQ input is true.
	 */
	if (udc->gadget.b_hnp_enable)
		OTG_CTRL_REG = (OTG_CTRL_REG | OTG_B_HNPEN | OTG_B_BUSREQ)
				& ~OTG_PULLUP;
}

static void ep0_irq(struct omap_udc *udc, u16 irq_src)
{
	struct omap_ep	*ep0 = &udc->ep[0];
	struct omap_req	*req = 0;

	ep0->irqs++;

	/* Clear any pending requests and then scrub any rx/tx state
	 * before starting to handle the SETUP request.
	 */
	if (irq_src & UDC_SETUP)
		nuke(ep0, 0);

	/* IN/OUT packets mean we're in the DATA or STATUS stage.  
	 * This driver uses only uses protocol stalls (ep0 never halts),
	 * and if we got this far the gadget driver already had a
	 * chance to stall.  Tries to be forgiving of host oddities.
	 *
	 * NOTE:  the last chance gadget drivers have to stall control
	 * requests is during their request completion callback.
	 */
	if (!list_empty(&ep0->queue))
		req = container_of(ep0->queue.next, struct omap_req, queue);

	/* IN == TX to host */
	if (irq_src & UDC_EP0_TX) {
		int	stat;

		UDC_IRQ_SRC_REG = UDC_EP0_TX;
		UDC_EP_NUM_REG = UDC_EP_SEL|UDC_EP_DIR;
		stat = UDC_STAT_FLG_REG;
		if (stat & UDC_ACK) {
			if (udc->ep0_in) {
				/* write next IN packet from response,
				 * or set up the status stage.
				 */
				if (req)
					stat = write_fifo(ep0, req);
				UDC_EP_NUM_REG = UDC_EP_DIR;
				if (!req && udc->ep0_pending) {
					UDC_EP_NUM_REG = UDC_EP_SEL;
					UDC_CTRL_REG = UDC_CLR_EP;
					UDC_CTRL_REG = UDC_SET_FIFO_EN;
					UDC_EP_NUM_REG = 0;
					udc->ep0_pending = 0;
				} /* else:  6 wait states before it'll tx */
			} else {
				/* ack status stage of OUT transfer */
				UDC_EP_NUM_REG = UDC_EP_DIR;
				if (req)
					done(ep0, req, 0);
			}
			req = 0;
		} else if (stat & UDC_STALL) {
			UDC_CTRL_REG = UDC_CLR_HALT;
			UDC_EP_NUM_REG = UDC_EP_DIR;
		} else {
			UDC_EP_NUM_REG = UDC_EP_DIR;
		}
	}

	/* OUT == RX from host */
	if (irq_src & UDC_EP0_RX) {
		int	stat;

		UDC_IRQ_SRC_REG = UDC_EP0_RX;
		UDC_EP_NUM_REG = UDC_EP_SEL;
		stat = UDC_STAT_FLG_REG;
		if (stat & UDC_ACK) {
			if (!udc->ep0_in) {
				stat = 0;
				/* read next OUT packet of request, maybe
				 * reactiviting the fifo; stall on errors.
				 */
				if (!req || (stat = read_fifo(ep0, req)) < 0) {
					UDC_SYSCON2_REG = UDC_STALL_CMD;
					udc->ep0_pending = 0;
					stat = 0;
				} else if (stat == 0)
					UDC_CTRL_REG = UDC_SET_FIFO_EN;
				UDC_EP_NUM_REG = 0;
				
				/* activate status stage */
				if (stat == 1) {
					done(ep0, req, 0);
					/* that may have STALLed ep0... */
					UDC_EP_NUM_REG = UDC_EP_SEL|UDC_EP_DIR;
					UDC_CTRL_REG = UDC_CLR_EP;
					UDC_CTRL_REG = UDC_SET_FIFO_EN;
					UDC_EP_NUM_REG = UDC_EP_DIR;
					udc->ep0_pending = 0;
				}
			} else {
				/* ack status stage of IN transfer */
				UDC_EP_NUM_REG = 0;
				if (req)
					done(ep0, req, 0);
			}
		} else if (stat & UDC_STALL) {
			UDC_CTRL_REG = UDC_CLR_HALT;
			UDC_EP_NUM_REG = 0;
		} else {
			UDC_EP_NUM_REG = 0;
		}
	}

	/* SETUP starts all control transfers */
	if (irq_src & UDC_SETUP) {
		union u {
			u16			word[4];
			struct usb_ctrlrequest	r;
		} u;
		int			status = -EINVAL;
		struct omap_ep		*ep;

		/* read the (latest) SETUP message */
		do {
			UDC_EP_NUM_REG = UDC_SETUP_SEL;
			/* two bytes at a time */
			u.word[0] = UDC_DATA_REG;
			u.word[1] = UDC_DATA_REG;
			u.word[2] = UDC_DATA_REG;
			u.word[3] = UDC_DATA_REG;
			UDC_EP_NUM_REG = 0;
		} while (UDC_IRQ_SRC_REG & UDC_SETUP);
		le16_to_cpus (&u.r.wValue);
		le16_to_cpus (&u.r.wIndex);
		le16_to_cpus (&u.r.wLength);

		/* Delegate almost all control requests to the gadget driver,
		 * except for a handful of ch9 status/feature requests that
		 * hardware doesn't autodecode _and_ the gadget API hides.
		 */
		udc->ep0_in = (u.r.bRequestType & USB_DIR_IN) != 0;
		udc->ep0_set_config = 0;
		udc->ep0_pending = 1;
		ep0->stopped = 0;
		ep0->ackwait = 0;
		switch (u.r.bRequest) {
		case USB_REQ_SET_CONFIGURATION:
			/* udc needs to know when ep != 0 is valid */
			if (u.r.bRequestType != USB_RECIP_DEVICE)
				goto delegate;
			if (u.r.wLength != 0)
				goto do_stall;
			udc->ep0_set_config = 1;
			udc->ep0_reset_config = (u.r.wValue == 0);
			VDBG("set config %d\n", u.r.wValue);

			/* update udc NOW since gadget driver may start
			 * queueing requests immediately; clear config
			 * later if it fails the request.
			 */
			if (udc->ep0_reset_config)
				UDC_SYSCON2_REG = UDC_CLR_CFG;
			else
				UDC_SYSCON2_REG = UDC_DEV_CFG;
			update_otg(udc);
			goto delegate;
		case USB_REQ_CLEAR_FEATURE:
			/* clear endpoint halt */
			if (u.r.bRequestType != USB_RECIP_ENDPOINT)
				goto delegate;
			if (u.r.wValue != USB_ENDPOINT_HALT
					|| u.r.wLength != 0)
				goto do_stall;
			ep = &udc->ep[u.r.wIndex & 0xf];
			if (ep != ep0) {
				if (u.r.wIndex & USB_DIR_IN)
					ep += 16;
				if (ep->bmAttributes == USB_ENDPOINT_XFER_ISOC
						|| !ep->desc)
					goto do_stall;
				use_ep(ep, 0);
				UDC_CTRL_REG = UDC_RESET_EP;
				ep->ackwait = 0;
				if (!(ep->bEndpointAddress & USB_DIR_IN))
					UDC_CTRL_REG = UDC_SET_FIFO_EN;
			}
			VDBG("%s halt cleared by host\n", ep->name);
			goto ep0out_status_stage;
		case USB_REQ_SET_FEATURE:
			/* set endpoint halt */
			if (u.r.bRequestType != USB_RECIP_ENDPOINT)
				goto delegate;
			if (u.r.wValue != USB_ENDPOINT_HALT
					|| u.r.wLength != 0)
				goto do_stall;
			ep = &udc->ep[u.r.wIndex & 0xf];
			if (u.r.wIndex & USB_DIR_IN)
				ep += 16;
			if (ep->bmAttributes == USB_ENDPOINT_XFER_ISOC
					|| ep == ep0 || !ep->desc)
				goto do_stall;
			if (use_dma && ep->has_dma) {
				/* this has rude side-effects (aborts) and
				 * can't really work if DMA-IN is active
				 */
				DBG("%s host set_halt, NYET \n", ep->name);
				goto do_stall;
			}
			use_ep(ep, 0);
			/* can't halt if fifo isn't empty... */
			UDC_CTRL_REG = UDC_CLR_EP;
			UDC_CTRL_REG = UDC_SET_HALT;
			VDBG("%s halted by host\n", ep->name);
ep0out_status_stage:
			status = 0;
			UDC_EP_NUM_REG = UDC_EP_SEL|UDC_EP_DIR;
			UDC_CTRL_REG = UDC_CLR_EP;
			UDC_CTRL_REG = UDC_SET_FIFO_EN;
			UDC_EP_NUM_REG = UDC_EP_DIR;
			udc->ep0_pending = 0;
			break;
		case USB_REQ_GET_STATUS:
			/* return interface status.  if we were pedantic,
			 * we'd detect non-existent interfaces, and stall.
			 */
			if (u.r.bRequestType
					!= (USB_DIR_IN|USB_RECIP_INTERFACE))
				goto delegate;
			/* return two zero bytes */
			UDC_EP_NUM_REG = UDC_EP_SEL|UDC_EP_DIR;
			UDC_DATA_REG = 0;
			UDC_CTRL_REG = UDC_SET_FIFO_EN;
			UDC_EP_NUM_REG = UDC_EP_DIR;
			status = 0;
			VDBG("GET_STATUS, interface %d\n", u.r.wIndex);
			/* next, status stage */
			break;
		default:
delegate:
			/* activate the ep0out fifo right away */
			if (!udc->ep0_in && u.r.wLength) {
				UDC_EP_NUM_REG = 0;
				UDC_CTRL_REG = UDC_SET_FIFO_EN;
			}

			/* gadget drivers see class/vendor specific requests,
			 * {SET,GET}_{INTERFACE,DESCRIPTOR,CONFIGURATION},
			 * and more
			 */
			VDBG("SETUP %02x.%02x v%04x i%04x l%04x\n",
				u.r.bRequestType, u.r.bRequest,
				u.r.wValue, u.r.wIndex, u.r.wLength);

			/* The gadget driver may return an error here,
			 * causing an immediate protocol stall.
			 *
			 * Else it must issue a response, either queueing a
			 * response buffer for the DATA stage, or halting ep0
			 * (causing a protocol stall, not a real halt).  A
			 * zero length buffer means no DATA stage.
			 *
			 * It's fine to issue that response after the setup()
			 * call returns, and this IRQ was handled.
			 */
			udc->ep0_setup = 1;
			spin_unlock(&udc->lock);
			status = udc->driver->setup (&udc->gadget, &u.r);
			spin_lock(&udc->lock);
			udc->ep0_setup = 0;
		}

		if (status < 0) {
do_stall:
			VDBG("req %02x.%02x protocol STALL; stat %d\n",
					u.r.bRequestType, u.r.bRequest, status);
			if (udc->ep0_set_config) {
				if (udc->ep0_reset_config)
					WARN("error resetting config?\n");
				else
					UDC_SYSCON2_REG = UDC_CLR_CFG;
			}
			UDC_SYSCON2_REG = UDC_STALL_CMD;
			udc->ep0_pending = 0;
		}
	}
}

/*-------------------------------------------------------------------------*/

#define OTG_FLAGS (UDC_B_HNP_ENABLE|UDC_A_HNP_SUPPORT|UDC_A_ALT_HNP_SUPPORT)

static void devstate_irq(struct omap_udc *udc, u16 irq_src)
{
	u16	devstat, change;

	devstat = UDC_DEVSTAT_REG;
	change = devstat ^ udc->devstat;
	udc->devstat = devstat;

	if (change & (UDC_USB_RESET|UDC_ATT)) {
		udc_quiesce(udc);

		if (change & UDC_ATT) {
			/* driver for any external transceiver will
			 * have called omap_vbus_session() already
			 */
			if (devstat & UDC_ATT) {
				udc->gadget.speed = USB_SPEED_FULL;
				VDBG("connect\n");
				if (!udc->transceiver)
					pullup_enable(udc);
				// if (driver->connect) call it
			} else if (udc->gadget.speed != USB_SPEED_UNKNOWN) {
				udc->gadget.speed = USB_SPEED_UNKNOWN;
				if (!udc->transceiver)
					pullup_disable(udc);
				DBG("disconnect, gadget %s\n",
					udc->driver->driver.name);
				if (udc->driver->disconnect) {
					spin_unlock(&udc->lock);
					udc->driver->disconnect(&udc->gadget);
					spin_lock(&udc->lock);
				}
			}
			change &= ~UDC_ATT;
		}

		if (change & UDC_USB_RESET) {
			if (devstat & UDC_USB_RESET) {
				VDBG("RESET=1\n");
			} else {
				udc->gadget.speed = USB_SPEED_FULL;
				INFO("USB reset done, gadget %s\n",
					udc->driver->driver.name);
				/* ep0 traffic is legal from now on */
				UDC_IRQ_EN_REG = UDC_DS_CHG_IE | UDC_EP0_IE;
			}
			change &= ~UDC_USB_RESET;
		}
	}
	if (change & UDC_SUS) {
		if (udc->gadget.speed != USB_SPEED_UNKNOWN) {
			// FIXME tell isp1301 to suspend/resume (?)
			if (devstat & UDC_SUS) {
				VDBG("suspend\n");
				update_otg(udc);
				/* HNP could be under way already */
				if (udc->gadget.speed == USB_SPEED_FULL
						&& udc->driver->suspend) {
					spin_unlock(&udc->lock);
					udc->driver->suspend(&udc->gadget);
					spin_lock(&udc->lock);
				}
			} else {
				VDBG("resume\n");
				if (udc->gadget.speed == USB_SPEED_FULL
						&& udc->driver->resume) {
					spin_unlock(&udc->lock);
					udc->driver->resume(&udc->gadget);
					spin_lock(&udc->lock);
				}
			}
		}
		change &= ~UDC_SUS;
	}
	if (change & OTG_FLAGS) {
		update_otg(udc);
		change &= ~OTG_FLAGS;
	}

	change &= ~(UDC_CFG|UDC_DEF|UDC_ADD);
	if (change)
		VDBG("devstat %03x, ignore change %03x\n",
			devstat,  change);

	UDC_IRQ_SRC_REG = UDC_DS_CHG;
}

static irqreturn_t
omap_udc_irq(int irq, void *_udc, struct pt_regs *r)
{
	struct omap_udc	*udc = _udc;
	u16		irq_src;
	irqreturn_t	status = IRQ_NONE;
	unsigned long	flags;

	spin_lock_irqsave(&udc->lock, flags);
	irq_src = UDC_IRQ_SRC_REG;

	/* Device state change (usb ch9 stuff) */
	if (irq_src & UDC_DS_CHG) {
		devstate_irq(_udc, irq_src);
		status = IRQ_HANDLED;
		irq_src &= ~UDC_DS_CHG;
	}

	/* EP0 control transfers */
	if (irq_src & (UDC_EP0_RX|UDC_SETUP|UDC_EP0_TX)) {
		ep0_irq(_udc, irq_src);
		status = IRQ_HANDLED;
		irq_src &= ~(UDC_EP0_RX|UDC_SETUP|UDC_EP0_TX);
	}

	/* DMA transfer completion */
	if (use_dma && (irq_src & (UDC_TXN_DONE|UDC_RXN_CNT|UDC_RXN_EOT))) {
		dma_irq(_udc, irq_src);
		status = IRQ_HANDLED;
		irq_src &= ~(UDC_TXN_DONE|UDC_RXN_CNT|UDC_RXN_EOT);
	}

	irq_src &= ~(UDC_SOF|UDC_EPN_TX|UDC_EPN_RX);
	if (irq_src)
		DBG("udc_irq, unhandled %03x\n", irq_src);
	spin_unlock_irqrestore(&udc->lock, flags);

	return status;
}

static irqreturn_t
omap_udc_pio_irq(int irq, void *_dev, struct pt_regs *r)
{
	u16		epn_stat, irq_src;
	irqreturn_t	status = IRQ_NONE;
	struct omap_ep	*ep;
	int		epnum;
	struct omap_udc	*udc = _dev;
	struct omap_req	*req;
	unsigned long	flags;

	spin_lock_irqsave(&udc->lock, flags);
	epn_stat = UDC_EPN_STAT_REG;
	irq_src = UDC_IRQ_SRC_REG;

	/* handle OUT first, to avoid some wasteful NAKs */
	if (irq_src & UDC_EPN_RX) {
		epnum = (epn_stat >> 8) & 0x0f;
		UDC_IRQ_SRC_REG = UDC_EPN_RX;
		status = IRQ_HANDLED;
		ep = &udc->ep[epnum];
		ep->irqs++;

		if (!list_empty(&ep->queue)) {
			UDC_EP_NUM_REG = epnum | UDC_EP_SEL;
			if ((UDC_STAT_FLG_REG & UDC_ACK)) {
				int stat;
				req = container_of(ep->queue.next,
						struct omap_req, queue);
				stat = read_fifo(ep, req);
				// FIXME double buffered PIO OUT should work
			}
			UDC_EP_NUM_REG = epnum;
		}
	}

	/* then IN transfers */
	if (irq_src & UDC_EPN_TX) {
		epnum = epn_stat & 0x0f;
		UDC_IRQ_SRC_REG = UDC_EPN_TX;
		status = IRQ_HANDLED;
		ep = &udc->ep[16 + epnum];
		ep->irqs++;
		ep->ackwait = 0;

		if (!list_empty(&ep->queue)) {
			UDC_EP_NUM_REG = epnum | UDC_EP_DIR | UDC_EP_SEL;
			if ((UDC_STAT_FLG_REG & UDC_ACK)) {
				req = container_of(ep->queue.next,
						struct omap_req, queue);
				(void) write_fifo(ep, req);
			}
			UDC_EP_NUM_REG = epnum | UDC_EP_DIR;
			/* 6 wait states before it'll tx */
		}
	}

	spin_unlock_irqrestore(&udc->lock, flags);
	return status;
}

#ifdef	USE_ISO
static irqreturn_t
omap_udc_iso_irq(int irq, void *_dev, struct pt_regs *r)
{
	struct omap_udc	*udc = _dev;
	struct omap_ep	*ep;
	int		pending = 0;
	unsigned long	flags;

	spin_lock_irqsave(&udc->lock, flags);

	/* handle all non-DMA ISO transfers */
	list_for_each_entry (ep, &udc->iso, iso) {
		u16		stat;
		struct omap_req	*req;

		if (ep->has_dma || list_empty(&ep->queue))
			continue;
		req = list_entry(ep->queue.next, struct omap_req, queue);

		use_ep(ep, UDC_EP_SEL);
		stat = UDC_STAT_FLG_REG;

		/* NOTE: like the other controller drivers, this isn't
		 * currently reporting lost or damaged frames.
		 */
		if (ep->bEndpointAddress & USB_DIR_IN) {
			if (stat & UDC_MISS_IN)
				/* done(ep, req, -EPROTO) */;
			else
				write_fifo(ep, req);
		} else {
			int	status = 0;

			if (stat & UDC_NO_RXPACKET)
				status = -EREMOTEIO;
			else if (stat & UDC_ISO_ERR)
				status = -EILSEQ;
			else if (stat & UDC_DATA_FLUSH)
				status = -ENOSR;

			if (status)
				/* done(ep, req, status) */;
			else
				read_fifo(ep, req);
		}
		deselect_ep();
		/* 6 wait states before next EP */

		ep->irqs++;
		if (!list_empty(&ep->queue))
			pending = 1;
	}
	if (!pending)
		UDC_IRQ_EN_REG &= ~UDC_SOF_IE;
	UDC_IRQ_SRC_REG = UDC_SOF;

	spin_unlock_irqrestore(&udc->lock, flags);
	return IRQ_HANDLED;
}
#endif

/*-------------------------------------------------------------------------*/

static struct omap_udc *udc;

int usb_gadget_register_driver (struct usb_gadget_driver *driver)
{
	int		status = -ENODEV;
	struct omap_ep	*ep;
	unsigned long	flags;

	/* basic sanity tests */
	if (!udc)
		return -ENODEV;
	if (!driver
			// FIXME if otg, check:  driver->is_otg
			|| driver->speed < USB_SPEED_FULL
			|| !driver->bind
			|| !driver->unbind
			|| !driver->setup)
		return -EINVAL;

	spin_lock_irqsave(&udc->lock, flags);
	if (udc->driver) {
		spin_unlock_irqrestore(&udc->lock, flags);
		return -EBUSY;
	}

	/* reset state */
	list_for_each_entry (ep, &udc->gadget.ep_list, ep.ep_list) {
		ep->irqs = 0;
		if (ep->bmAttributes == USB_ENDPOINT_XFER_ISOC)
			continue;
		use_ep(ep, 0);
		UDC_CTRL_REG = UDC_SET_HALT;
	}
	udc->ep0_pending = 0;
	udc->ep[0].irqs = 0;
	udc->softconnect = 1;

	/* hook up the driver */
	driver->driver.bus = 0;
	udc->driver = driver;
	udc->gadget.dev.driver = &driver->driver;
	spin_unlock_irqrestore(&udc->lock, flags);

	status = driver->bind (&udc->gadget);
	if (status) {
		DBG("bind to %s --> %d\n", driver->driver.name, status);
		udc->gadget.dev.driver = 0;
		udc->driver = 0;
		goto done;
	}
	DBG("bound to driver %s\n", driver->driver.name);

	UDC_IRQ_SRC_REG = UDC_IRQ_SRC_MASK;

	/* connect to bus through transceiver */
	if (udc->transceiver) {
		status = otg_set_peripheral(udc->transceiver, &udc->gadget);
		if (status < 0) {
			ERR("can't bind to transceiver\n");
			driver->unbind (&udc->gadget);
			udc->gadget.dev.driver = 0;
			udc->driver = 0;
			goto done;
		}
	} else {
		if (can_pullup(udc))
			pullup_enable (udc);
		else
			pullup_disable (udc);
	}

done:
	return status;
}
EXPORT_SYMBOL(usb_gadget_register_driver);

int usb_gadget_unregister_driver (struct usb_gadget_driver *driver)
{
	unsigned long	flags;
	int		status = -ENODEV;

	if (!udc)
		return -ENODEV;
	if (!driver || driver != udc->driver)
		return -EINVAL;

	if (udc->transceiver)
		(void) otg_set_peripheral(udc->transceiver, 0);
	else
		pullup_disable(udc);

	spin_lock_irqsave(&udc->lock, flags);
	udc_quiesce(udc);
	spin_unlock_irqrestore(&udc->lock, flags);

	driver->unbind(&udc->gadget);
	udc->gadget.dev.driver = 0;
	udc->driver = 0;


	DBG("unregistered driver '%s'\n", driver->driver.name);
	return status;
}
EXPORT_SYMBOL(usb_gadget_unregister_driver);


/*-------------------------------------------------------------------------*/

#ifdef CONFIG_USB_OMAP_PROC

#include <linux/seq_file.h>

static const char proc_filename[] = "driver/udc";

#define FOURBITS "%s%s%s%s"
#define EIGHTBITS FOURBITS FOURBITS

static void proc_ep_show(struct seq_file *s, struct omap_ep *ep)
{
	u16		stat_flg;
	struct omap_req	*req;
	char		buf[20];

	use_ep(ep, 0);

	if (use_dma && ep->has_dma)
		snprintf(buf, sizeof buf, "(%cxdma%d lch%d) ",
			(ep->bEndpointAddress & USB_DIR_IN) ? 't' : 'r',
			ep->dma_channel - 1, ep->lch);
	else
		buf[0] = 0;

	stat_flg = UDC_STAT_FLG_REG;
	seq_printf(s,
		"\n%s %sirqs %ld stat %04x " EIGHTBITS FOURBITS "%s\n",
		ep->name, buf, ep->irqs, stat_flg,
		(stat_flg & UDC_NO_RXPACKET) ? "no_rxpacket " : "",
		(stat_flg & UDC_MISS_IN) ? "miss_in " : "",
		(stat_flg & UDC_DATA_FLUSH) ? "data_flush " : "",
		(stat_flg & UDC_ISO_ERR) ? "iso_err " : "",
		(stat_flg & UDC_ISO_FIFO_EMPTY) ? "iso_fifo_empty " : "",
		(stat_flg & UDC_ISO_FIFO_FULL) ? "iso_fifo_full " : "",
		(stat_flg & UDC_EP_HALTED) ? "HALT " : "",
		(stat_flg & UDC_STALL) ? "STALL " : "",
		(stat_flg & UDC_NAK) ? "NAK " : "",
		(stat_flg & UDC_ACK) ? "ACK " : "",
		(stat_flg & UDC_FIFO_EN) ? "fifo_en " : "",
		(stat_flg & UDC_NON_ISO_FIFO_EMPTY) ? "fifo_empty " : "",
		(stat_flg & UDC_NON_ISO_FIFO_FULL) ? "fifo_full " : "");

	if (list_empty (&ep->queue))
		seq_printf(s, "\t(queue empty)\n");
	else
		list_for_each_entry (req, &ep->queue, queue)
			seq_printf(s, "\treq %p len %d/%d buf %p\n",
					&req->req, req->req.actual,
					req->req.length, req->req.buf);
}

static char *trx_mode(unsigned m)
{
	switch (m) {
	case 3:
	case 0:		return "6wire";
	case 1:		return "4wire";
	case 2:		return "3wire";
	default:	return "unknown";
	}
}

static int proc_udc_show(struct seq_file *s, void *_)
{
	u32		tmp;
	struct omap_ep	*ep;
	unsigned long	flags;

	spin_lock_irqsave(&udc->lock, flags);

	seq_printf(s, "%s, version: " DRIVER_VERSION
#ifdef	USE_ISO
		" (iso)"
#endif
		"%s\n",
		driver_desc,
		use_dma ?  " (dma)" : "");

	tmp = UDC_REV_REG & 0xff; 
	seq_printf(s,
		"UDC rev %d.%d, OTG rev %d.%d, fifo mode %d, gadget %s\n"
		"hmc %d, transceiver %08x %s\n",
		tmp >> 4, tmp & 0xf,
		OTG_REV_REG >> 4, OTG_REV_REG & 0xf,
		fifo_mode,
		udc->driver ? udc->driver->driver.name : "(none)",
		HMC, USB_TRANSCEIVER_CTRL_REG,
		udc->transceiver ? udc->transceiver->label : "");

	/* OTG controller registers */
	tmp = OTG_SYSCON_1_REG;
	seq_printf(s, "otg_syscon1 %08x usb2 %s, usb1 %s, usb0 %s,"
			FOURBITS "\n", tmp,
		trx_mode(USB2_TRX_MODE(tmp)),
		trx_mode(USB1_TRX_MODE(tmp)),
		trx_mode(USB0_TRX_MODE(tmp)),
		(tmp & OTG_IDLE_EN) ? " !otg" : "",
		(tmp & HST_IDLE_EN) ? " !host" : "",
		(tmp & DEV_IDLE_EN) ? " !dev" : "",
		(tmp & OTG_RESET_DONE) ? " reset_done" : " reset_active");
	tmp = OTG_SYSCON_2_REG;
	seq_printf(s, "otg_syscon2 %08x%s" EIGHTBITS
			" b_ase_brst=%d hmc=%d\n", tmp,
		(tmp & OTG_EN) ? " otg_en" : "",
		(tmp & USBX_SYNCHRO) ? " synchro" : "",
		// much more SRP stuff
		(tmp & SRP_DATA) ? " srp_data" : "",
		(tmp & SRP_VBUS) ? " srp_vbus" : "",
		(tmp & OTG_PADEN) ? " otg_paden" : "",
		(tmp & HMC_PADEN) ? " hmc_paden" : "",
		(tmp & UHOST_EN) ? " uhost_en" : "",
		(tmp & HMC_TLLSPEED) ? " tllspeed" : "",
		(tmp & HMC_TLLATTACH) ? " tllattach" : "",
		B_ASE_BRST(tmp),
		OTG_HMC(tmp));
	tmp = OTG_CTRL_REG;
	seq_printf(s, "otg_ctrl    %06x" EIGHTBITS EIGHTBITS "%s\n", tmp,
		(tmp & OTG_ASESSVLD) ? " asess" : "",
		(tmp & OTG_BSESSEND) ? " bsess_end" : "",
		(tmp & OTG_BSESSVLD) ? " bsess" : "",
		(tmp & OTG_VBUSVLD) ? " vbus" : "",
		(tmp & OTG_ID) ? " id" : "",
		(tmp & OTG_DRIVER_SEL) ? " DEVICE" : " HOST",
		(tmp & OTG_A_SETB_HNPEN) ? " a_setb_hnpen" : "",
		(tmp & OTG_A_BUSREQ) ? " a_bus" : "",
		(tmp & OTG_B_HNPEN) ? " b_hnpen" : "",
		(tmp & OTG_B_BUSREQ) ? " b_bus" : "",
		(tmp & OTG_BUSDROP) ? " busdrop" : "",
		(tmp & OTG_PULLDOWN) ? " down" : "",
		(tmp & OTG_PULLUP) ? " up" : "",
		(tmp & OTG_DRV_VBUS) ? " drv" : "",
		(tmp & OTG_PD_VBUS) ? " pd_vb" : "",
		(tmp & OTG_PU_VBUS) ? " pu_vb" : "",
		(tmp & OTG_PU_ID) ? " pu_id" : ""
		);
	tmp = OTG_IRQ_EN_REG;
	seq_printf(s, "otg_irq_en  %04x" "\n", tmp);
	tmp = OTG_IRQ_SRC_REG;
	seq_printf(s, "otg_irq_src %04x" "\n", tmp);
	tmp = OTG_OUTCTRL_REG;
	seq_printf(s, "otg_outctrl %04x" "\n", tmp);
	tmp = OTG_TEST_REG;
	seq_printf(s, "otg_test    %04x" "\n", tmp);

	tmp = UDC_SYSCON1_REG;
	seq_printf(s, "\nsyscon1     %04x" EIGHTBITS "\n", tmp,
		(tmp & UDC_CFG_LOCK) ? " cfg_lock" : "",
		(tmp & UDC_DATA_ENDIAN) ? " data_endian" : "",
		(tmp & UDC_DMA_ENDIAN) ? " dma_endian" : "",
		(tmp & UDC_NAK_EN) ? " nak" : "",
		(tmp & UDC_AUTODECODE_DIS) ? " autodecode_dis" : "",
		(tmp & UDC_SELF_PWR) ? " self_pwr" : "",
		(tmp & UDC_SOFF_DIS) ? " soff_dis" : "",
		(tmp & UDC_PULLUP_EN) ? " PULLUP" : "");
	// syscon2 is write-only

	/* UDC controller registers */
	if (!(tmp & UDC_PULLUP_EN)) {
		seq_printf(s, "(suspended)\n");
		spin_unlock_irqrestore(&udc->lock, flags);
		return 0;
	}

	tmp = UDC_DEVSTAT_REG;
	seq_printf(s, "devstat     %04x" EIGHTBITS "%s%s\n", tmp,
		(tmp & UDC_B_HNP_ENABLE) ? " b_hnp" : "",
		(tmp & UDC_A_HNP_SUPPORT) ? " a_hnp" : "",
		(tmp & UDC_A_ALT_HNP_SUPPORT) ? " a_alt_hnp" : "",
		(tmp & UDC_R_WK_OK) ? " r_wk_ok" : "",
		(tmp & UDC_USB_RESET) ? " usb_reset" : "",
		(tmp & UDC_SUS) ? " SUS" : "",
		(tmp & UDC_CFG) ? " CFG" : "",
		(tmp & UDC_ADD) ? " ADD" : "",
		(tmp & UDC_DEF) ? " DEF" : "",
		(tmp & UDC_ATT) ? " ATT" : "");
	seq_printf(s, "sof         %04x\n", UDC_SOF_REG);
	tmp = UDC_IRQ_EN_REG;
	seq_printf(s, "irq_en      %04x" FOURBITS "%s\n", tmp,
		(tmp & UDC_SOF_IE) ? " sof" : "",
		(tmp & UDC_EPN_RX_IE) ? " epn_rx" : "",
		(tmp & UDC_EPN_TX_IE) ? " epn_tx" : "",
		(tmp & UDC_DS_CHG_IE) ? " ds_chg" : "",
		(tmp & UDC_EP0_IE) ? " ep0" : "");
	tmp = UDC_IRQ_SRC_REG;
	seq_printf(s, "irq_src     %04x" EIGHTBITS "%s%s\n", tmp,
		(tmp & UDC_TXN_DONE) ? " txn_done" : "",
		(tmp & UDC_RXN_CNT) ? " rxn_cnt" : "",
		(tmp & UDC_RXN_EOT) ? " rxn_eot" : "",
		(tmp & UDC_SOF) ? " sof" : "",
		(tmp & UDC_EPN_RX) ? " epn_rx" : "",
		(tmp & UDC_EPN_TX) ? " epn_tx" : "",
		(tmp & UDC_DS_CHG) ? " ds_chg" : "",
		(tmp & UDC_SETUP) ? " setup" : "",
		(tmp & UDC_EP0_RX) ? " ep0out" : "",
		(tmp & UDC_EP0_TX) ? " ep0in" : "");
	if (use_dma) {
		unsigned i;

		tmp = UDC_DMA_IRQ_EN_REG;
		seq_printf(s, "dma_irq_en  %04x%s" EIGHTBITS "\n", tmp,
			(tmp & UDC_TX_DONE_IE(3)) ? " tx2_done" : "",
			(tmp & UDC_RX_CNT_IE(3)) ? " rx2_cnt" : "",
			(tmp & UDC_RX_EOT_IE(3)) ? " rx2_eot" : "",

			(tmp & UDC_TX_DONE_IE(2)) ? " tx1_done" : "",
			(tmp & UDC_RX_CNT_IE(2)) ? " rx1_cnt" : "",
			(tmp & UDC_RX_EOT_IE(2)) ? " rx1_eot" : "",

			(tmp & UDC_TX_DONE_IE(1)) ? " tx0_done" : "",
			(tmp & UDC_RX_CNT_IE(1)) ? " rx0_cnt" : "",
			(tmp & UDC_RX_EOT_IE(1)) ? " rx0_eot" : "");

		tmp = UDC_RXDMA_CFG_REG;
		seq_printf(s, "rxdma_cfg   %04x\n", tmp);
		if (tmp) {
			for (i = 0; i < 3; i++) {
				if ((tmp & (0x0f << (i * 4))) == 0)
					continue;
				seq_printf(s, "rxdma[%d]    %04x\n", i,
						UDC_RXDMA_REG(i + 1));
			}
		}
		tmp = UDC_TXDMA_CFG_REG;
		seq_printf(s, "txdma_cfg   %04x\n", tmp);
		if (tmp) {
			for (i = 0; i < 3; i++) {
				if (!(tmp & (0x0f << (i * 4))))
					continue;
				seq_printf(s, "txdma[%d]    %04x\n", i,
						UDC_TXDMA_REG(i + 1));
			}
		}
	}

	tmp = UDC_DEVSTAT_REG;
	if (tmp & UDC_ATT) {
		proc_ep_show(s, &udc->ep[0]);
		if (tmp & UDC_ADD) {
			list_for_each_entry (ep, &udc->gadget.ep_list,
					ep.ep_list) {
				if (ep->desc)
					proc_ep_show(s, ep);
			}
		}
	}
	spin_unlock_irqrestore(&udc->lock, flags);
	return 0;
}

static int proc_udc_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_udc_show, 0);
}

static struct file_operations proc_ops = {
	.open		= proc_udc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void create_proc_file(void)
{
	struct proc_dir_entry *pde;

	pde = create_proc_entry (proc_filename, 0, NULL);
	if (pde)
		pde->proc_fops = &proc_ops;
}

static void remove_proc_file(void)
{
	remove_proc_entry(proc_filename, 0);
}

#else

static inline void create_proc_file(void) {}
static inline void remove_proc_file(void) {}

#endif

/*-------------------------------------------------------------------------*/

/* Before this controller can enumerate, we need to pick an endpoint
 * configuration, or "fifo_mode"  That involves allocating 2KB of packet
 * buffer space among the endpoints we'll be operating.
 */
static unsigned __init
omap_ep_setup(char *name, u8 addr, u8 type,
		unsigned buf, unsigned maxp, int dbuf)
{
	struct omap_ep	*ep;
	u16		epn_rxtx = 0;

	/* OUT endpoints first, then IN */
	ep = &udc->ep[addr & 0xf];
	if (addr & USB_DIR_IN)
		ep += 16;

	/* in case of ep init table bugs */
	BUG_ON(ep->name[0]);

	/* chip setup ... bit values are same for IN, OUT */
	if (type == USB_ENDPOINT_XFER_ISOC) {
		switch (maxp) {
		case 8:		epn_rxtx = 0 << 12; break;
		case 16:	epn_rxtx = 1 << 12; break;
		case 32:	epn_rxtx = 2 << 12; break;
		case 64:	epn_rxtx = 3 << 12; break;
		case 128:	epn_rxtx = 4 << 12; break;
		case 256:	epn_rxtx = 5 << 12; break;
		case 512:	epn_rxtx = 6 << 12; break;
		default:	BUG();
		}
		epn_rxtx |= UDC_EPN_RX_ISO;
		dbuf = 1;
	} else {
		/* pio-out could potentially double-buffer,
		 * as can (should!) DMA-IN
		 */
		if (!use_dma || (addr & USB_DIR_IN))
			dbuf = 0;

		switch (maxp) {
		case 8:		epn_rxtx = 0 << 12; break;
		case 16:	epn_rxtx = 1 << 12; break;
		case 32:	epn_rxtx = 2 << 12; break;
		case 64:	epn_rxtx = 3 << 12; break;
		default:	BUG();
		}
		if (dbuf && addr)
			epn_rxtx |= UDC_EPN_RX_DB;
	}
	if (addr)
		epn_rxtx |= UDC_EPN_RX_VALID;
	BUG_ON(buf & 0x07);
	epn_rxtx |= buf >> 3;

	DBG("%s addr %02x rxtx %04x maxp %d%s buf %d\n",
		name, addr, epn_rxtx, maxp, dbuf ? "x2" : "", buf);

	if (addr & USB_DIR_IN)
		UDC_EP_TX_REG(addr & 0xf) = epn_rxtx;
	else
		UDC_EP_RX_REG(addr) = epn_rxtx;

	/* next endpoint's buffer starts after this one's */
	buf += maxp;
	if (dbuf)
		buf += maxp;
	BUG_ON(buf > 2048);

	/* set up driver data structures */
	BUG_ON(strlen(name) >= sizeof ep->name);
	strlcpy(ep->name, name, sizeof ep->name);
	INIT_LIST_HEAD(&ep->queue);
	INIT_LIST_HEAD(&ep->iso);
	ep->bEndpointAddress = addr;
	ep->bmAttributes = type;
	ep->double_buf = dbuf;
	ep->udc = udc; 

	ep->ep.name = ep->name;
	ep->ep.ops = &omap_ep_ops;
	ep->ep.maxpacket = ep->maxpacket = maxp;
	list_add_tail (&ep->ep.ep_list, &udc->gadget.ep_list);

	return buf;
}

static void omap_udc_release(struct device *dev)
{
	complete(udc->done);
	kfree (udc);
	udc = 0;
}

static int __init
omap_udc_setup(struct platform_device *odev, struct otg_transceiver *xceiv)
{
	unsigned	tmp, buf;

	/* abolish any previous hardware state */
	UDC_SYSCON1_REG = 0;
	UDC_IRQ_EN_REG = 0;
	UDC_IRQ_SRC_REG = UDC_IRQ_SRC_MASK;
	UDC_DMA_IRQ_EN_REG = 0;
	UDC_RXDMA_CFG_REG = 0;
	UDC_TXDMA_CFG_REG = 0;

	/* UDC_PULLUP_EN gates the chip clock */
	// OTG_SYSCON_1_REG |= DEV_IDLE_EN;

	udc = kmalloc (sizeof *udc, SLAB_KERNEL);
	if (!udc)
		return -ENOMEM;

	memset(udc, 0, sizeof *udc);
	spin_lock_init (&udc->lock);

	udc->gadget.ops = &omap_gadget_ops;
	udc->gadget.ep0 = &udc->ep[0].ep;
	INIT_LIST_HEAD(&udc->gadget.ep_list);
	INIT_LIST_HEAD(&udc->iso);
	udc->gadget.speed = USB_SPEED_UNKNOWN;
	udc->gadget.name = driver_name;

	device_initialize(&udc->gadget.dev);
	strcpy (udc->gadget.dev.bus_id, "gadget");
	udc->gadget.dev.release = omap_udc_release;
	udc->gadget.dev.parent = &odev->dev;
	if (use_dma)
		udc->gadget.dev.dma_mask = odev->dev.dma_mask;

	udc->transceiver = xceiv;

	/* ep0 is special; put it right after the SETUP buffer */
	buf = omap_ep_setup("ep0", 0, USB_ENDPOINT_XFER_CONTROL,
			8 /* after SETUP */, 64 /* maxpacket */, 0);
	list_del_init(&udc->ep[0].ep.ep_list);

	/* initially disable all non-ep0 endpoints */
	for (tmp = 1; tmp < 15; tmp++) {
		UDC_EP_RX_REG(tmp) = 0;
		UDC_EP_TX_REG(tmp) = 0;
	}

#define OMAP_BULK_EP(name,addr) \
	buf = omap_ep_setup(name "-bulk", addr, \
			USB_ENDPOINT_XFER_BULK, buf, 64, 1);
#define OMAP_INT_EP(name,addr, maxp) \
	buf = omap_ep_setup(name "-int", addr, \
			USB_ENDPOINT_XFER_INT, buf, maxp, 0);
#define OMAP_ISO_EP(name,addr, maxp) \
	buf = omap_ep_setup(name "-iso", addr, \
			USB_ENDPOINT_XFER_ISOC, buf, maxp, 1);

	switch (fifo_mode) {
	case 0:
		OMAP_BULK_EP("ep1in",  USB_DIR_IN  | 1);
		OMAP_BULK_EP("ep2out", USB_DIR_OUT | 2);
		OMAP_INT_EP("ep3in",   USB_DIR_IN  | 3, 16);
		break;
	case 1:
		OMAP_BULK_EP("ep1in",  USB_DIR_IN  | 1);
		OMAP_BULK_EP("ep2out", USB_DIR_OUT | 2);
		OMAP_BULK_EP("ep3in",  USB_DIR_IN  | 3);
		OMAP_BULK_EP("ep4out", USB_DIR_OUT | 4);

		OMAP_BULK_EP("ep5in",  USB_DIR_IN  | 5);
		OMAP_BULK_EP("ep5out", USB_DIR_OUT | 5);
		OMAP_BULK_EP("ep6in",  USB_DIR_IN  | 6);
		OMAP_BULK_EP("ep6out", USB_DIR_OUT | 6);

		OMAP_BULK_EP("ep7in",  USB_DIR_IN  | 7);
		OMAP_BULK_EP("ep7out", USB_DIR_OUT | 7);
		OMAP_BULK_EP("ep8in",  USB_DIR_IN  | 8);
		OMAP_BULK_EP("ep8out", USB_DIR_OUT | 8);

		OMAP_INT_EP("ep9in",   USB_DIR_IN  | 9, 16);
		OMAP_INT_EP("ep10out", USB_DIR_IN  | 10, 16);
		OMAP_INT_EP("ep11in",  USB_DIR_IN  | 9, 16);
		OMAP_INT_EP("ep12out", USB_DIR_IN  | 10, 16);
		break;

#ifdef	USE_ISO
	case 2:			/* mixed iso/bulk */
		OMAP_ISO_EP("ep1in",   USB_DIR_IN  | 1, 256);
		OMAP_ISO_EP("ep2out",  USB_DIR_OUT | 2, 256);
		OMAP_ISO_EP("ep3in",   USB_DIR_IN  | 3, 128);
		OMAP_ISO_EP("ep4out",  USB_DIR_OUT | 4, 128);

		OMAP_INT_EP("ep5in",   USB_DIR_IN  | 5, 16);

		OMAP_BULK_EP("ep6in",  USB_DIR_IN  | 6);
		OMAP_BULK_EP("ep7out", USB_DIR_OUT | 7);
		OMAP_INT_EP("ep8in",   USB_DIR_IN  | 8, 16);
		break;
	case 3:			/* mixed bulk/iso */
		OMAP_BULK_EP("ep1in",  USB_DIR_IN  | 1);
		OMAP_BULK_EP("ep2out", USB_DIR_OUT | 2);
		OMAP_INT_EP("ep3in",   USB_DIR_IN  | 3, 16);

		OMAP_BULK_EP("ep4in",  USB_DIR_IN  | 4);
		OMAP_BULK_EP("ep5out", USB_DIR_OUT | 5);
		OMAP_INT_EP("ep6in",   USB_DIR_IN  | 6, 16);

		OMAP_ISO_EP("ep7in",   USB_DIR_IN  | 7, 256);
		OMAP_ISO_EP("ep8out",  USB_DIR_OUT | 8, 256);
		OMAP_INT_EP("ep9in",   USB_DIR_IN  | 9, 16);
		break;
#endif

	/* add more modes as needed */

	default:
		ERR("unsupported fifo_mode #%d\n", fifo_mode);
		return -ENODEV;
	}
	UDC_SYSCON1_REG = UDC_CFG_LOCK|UDC_SELF_PWR;
	INFO("fifo mode %d, %d bytes not used\n", fifo_mode, 2048 - buf);
	return 0;
}

static int __init omap_udc_probe(struct device *dev)
{
	struct platform_device	*odev = to_platform_device(dev);
	int			status = -ENODEV;
	int			hmc;
	struct otg_transceiver	*xceiv = 0;
	const char		*type = 0;
	struct omap_usb_config	*config = dev->platform_data;

	/* NOTE:  "knows" the order of the resources! */
	if (!request_mem_region(odev->resource[0].start, 
			odev->resource[0].end - odev->resource[0].start + 1,
			driver_name)) {
		DBG("request_mem_region failed\n");
		return -EBUSY;
	}

	INFO("OMAP UDC rev %d.%d, OTG rev %d.%d, %s receptacle\n",
		UDC_REV_REG >> 4, UDC_REV_REG & 0xf,
		OTG_REV_REG >> 4, OTG_REV_REG & 0xf,
		config->otg ? "Mini-AB" : "B/Mini-B");

	/* use the mode given to us by board init code */
	hmc = HMC;
	switch (hmc) {
	case 3:
	case 11:
	case 19:
	case 25:
		xceiv = otg_get_transceiver();
		if (!xceiv) {
			DBG("external transceiver not registered!\n");
			goto cleanup0;
		}
		type = xceiv->label;
		break;
	case 0:			/* POWERUP DEFAULT == 0 */
	case 4:
	case 12:
	case 20:
		type = "INTEGRATED";
		break;
	case 21:			/* internal loopback */
		type = "(loopback)";
		break;
	case 14:			/* transceiverless */
		type = "(none)";
		break;

	default:
		ERR("unrecognized UDC HMC mode %d\n", hmc);
		return -ENODEV;
	}
	INFO("hmc mode %d, transceiver %s\n", hmc, type);

	/* a "gadget" abstracts/virtualizes the controller */
	status = omap_udc_setup(odev, xceiv);
	if (status) {
		goto cleanup0;
	}
	xceiv = 0;
	// "udc" is now valid
	pullup_disable(udc);
	udc->gadget.is_otg = (config->otg != 0);

	/* USB general purpose IRQ:  ep0, state changes, dma, etc */
	status = request_irq(odev->resource[1].start, omap_udc_irq,
			SA_SAMPLE_RANDOM, driver_name, udc);
	if (status != 0) {
		ERR( "can't get irq %ld, err %d\n",
			odev->resource[1].start, status);
		goto cleanup1;
	}

	/* USB "non-iso" IRQ (PIO for all but ep0) */
	status = request_irq(odev->resource[2].start, omap_udc_pio_irq,
			SA_SAMPLE_RANDOM, "omap_udc pio", udc);
	if (status != 0) {
		ERR( "can't get irq %ld, err %d\n",
			odev->resource[2].start, status);
		goto cleanup2;
	}
#ifdef	USE_ISO
	status = request_irq(odev->resource[3].start, omap_udc_iso_irq,
			SA_INTERRUPT, "omap_udc iso", udc);
	if (status != 0) {
		ERR("can't get irq %ld, err %d\n",
			odev->resource[3].start, status);
		goto cleanup3;
	}
#endif

	create_proc_file();
	device_add(&udc->gadget.dev);
	return 0;

#ifdef	USE_ISO
cleanup3:
	free_irq(odev->resource[2].start, udc);
#endif

cleanup2:
	free_irq(odev->resource[1].start, udc);

cleanup1:
	kfree (udc);
	udc = 0;

cleanup0:
	if (xceiv)
		put_device(xceiv->dev);
	release_mem_region(odev->resource[0].start,
			odev->resource[0].end - odev->resource[0].start + 1);
	return status;
}

static int __exit omap_udc_remove(struct device *dev)
{
	struct platform_device	*odev = to_platform_device(dev);
	DECLARE_COMPLETION(done);

	if (!udc)
		return -ENODEV;

	udc->done = &done;

	pullup_disable(udc);
	if (udc->transceiver) {
		put_device(udc->transceiver->dev);
		udc->transceiver = 0;
	}
	UDC_SYSCON1_REG = 0;

	remove_proc_file();

#ifdef	USE_ISO
	free_irq(odev->resource[3].start, udc);
#endif
	free_irq(odev->resource[2].start, udc);
	free_irq(odev->resource[1].start, udc);

	release_mem_region(odev->resource[0].start,
			odev->resource[0].end - odev->resource[0].start + 1);

	device_unregister(&udc->gadget.dev);
	wait_for_completion(&done);

	return 0;
}

/* suspend/resume/wakeup from sysfs (echo > power/state) */

static int omap_udc_suspend(struct device *dev, u32 state, u32 level)
{
	if (level != 0)
		return 0;

	DBG("suspend, state %d\n", state);
	omap_pullup(&udc->gadget, 0);
	udc->gadget.dev.power.power_state = 3;
	udc->gadget.dev.parent->power.power_state = 3;
	return 0;
}

static int omap_udc_resume(struct device *dev, u32 level)
{
	if (level != 0)
		return 0;

	DBG("resume + wakeup/SRP\n");
	udc->gadget.dev.parent->power.power_state = 0;
	udc->gadget.dev.power.power_state = 0;
	omap_pullup(&udc->gadget, 1);

	/* maybe the host would enumerate us if we nudged it */
	msleep(100);
	return omap_wakeup(&udc->gadget);
}

/*-------------------------------------------------------------------------*/

static struct device_driver udc_driver = {
	.name		= (char *) driver_name,
	.bus		= &platform_bus_type,
	.probe		= omap_udc_probe,
	.remove		= __exit_p(omap_udc_remove),
	.suspend	= omap_udc_suspend,
	.resume		= omap_udc_resume,
};

static int __init udc_init(void)
{
	/* should work on many OMAP systems with at most minor changes,
	 * but the 1510 doesn't have an OTG controller.
	 */
	if (cpu_is_omap1510()) {
		DBG("no OMAP1510 support yet\n");
		return -ENODEV;
	}
	INFO("%s, version: " DRIVER_VERSION "%s\n", driver_desc,
		use_dma ?  " (dma)" : "");
	return driver_register(&udc_driver);
}
module_init(udc_init);

static void __exit udc_exit(void)
{
	driver_unregister(&udc_driver);
}
module_exit(udc_exit);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

