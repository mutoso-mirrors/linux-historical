/*
 * SL811HS HCD (Host Controller Driver) for USB.
 *
 * Copyright (C) 2004 Psion Teklogix (for NetBook PRO)
 * Copyright (C) 2004 David Brownell
 * 
 * Periodic scheduling is based on Roman's OHCI code
 * 	Copyright (C) 1999 Roman Weissgaerber
 *
 * The SL811HS controller handles host side USB (like the SL11H, but with
 * another register set and SOF generation) as well as peripheral side USB
 * (like the SL811S).  This driver version doesn't implement the Gadget API
 * for the peripheral role; or OTG (that'd need much external circuitry).
 *
 * For documentation, see the SL811HS spec and the "SL811HS Embedded Host"
 * document (providing significant pieces missing from that spec); plus
 * the SL811S spec if you want peripheral side info.
 */ 

/*
 * Status:  Passed basic stress testing, works with hubs, mice, keyboards,
 * and usb-storage.
 *
 * TODO:
 * - usb suspend/resume triggered by sl811 (with USB_SUSPEND)
 * - various issues noted in the code
 * - performance work; use both register banks; ...
 * - use urb->iso_frame_desc[] with ISO transfers
 */

#undef	VERBOSE
#undef	PACKET_TRACE

#include <linux/config.h>

#ifdef CONFIG_USB_DEBUG
#	define DEBUG
#else
#	undef DEBUG
#endif

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/list.h>
#include <linux/interrupt.h>
#include <linux/usb.h>
#include <linux/usb_sl811.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/byteorder.h>

#include "../core/hcd.h"
#include "sl811.h"


MODULE_DESCRIPTION("SL811HS USB Host Controller Driver");
MODULE_LICENSE("GPL");

#define DRIVER_VERSION	"06 Dec 2004"


#ifndef DEBUG
#	define	STUB_DEBUG_FILE
#endif

/* for now, use only one transfer register bank */
#undef	USE_B

/* this doesn't understand urb->iso_frame_desc[], but if you had a driver
 * that just queued one ISO frame per URB then iso transfers "should" work
 * using the normal urb status fields.
 */
#define	DISABLE_ISO

// #define	QUIRK2
#define	QUIRK3

static const char hcd_name[] = "sl811-hcd";

/*-------------------------------------------------------------------------*/

static irqreturn_t sl811h_irq(int irq, void *_sl811, struct pt_regs *regs);

static void port_power(struct sl811 *sl811, int is_on)
{
	/* hub is inactive unless the port is powered */
	if (is_on) {
		if (sl811->port1 & (1 << USB_PORT_FEAT_POWER))
			return;

		sl811->port1 = (1 << USB_PORT_FEAT_POWER);
		sl811->irq_enable = SL11H_INTMASK_INSRMV;
		sl811->hcd.self.controller->power.power_state = PM_SUSPEND_ON;
	} else {
		sl811->port1 = 0;
		sl811->irq_enable = 0;
		sl811->hcd.state = USB_STATE_HALT;
		sl811->hcd.self.controller->power.power_state = PM_SUSPEND_DISK;
	}
	sl811->ctrl1 = 0;
	sl811_write(sl811, SL11H_IRQ_ENABLE, 0);
	sl811_write(sl811, SL11H_IRQ_STATUS, ~0);

	if (sl811->board && sl811->board->port_power) {
		/* switch VBUS, at 500mA unless hub power budget gets set */
		DBG("power %s\n", is_on ? "on" : "off");
		sl811->board->port_power(sl811->hcd.self.controller, is_on);
	}

	/* reset as thoroughly as we can */
	if (sl811->board && sl811->board->reset)
		sl811->board->reset(sl811->hcd.self.controller);

	sl811_write(sl811, SL11H_IRQ_ENABLE, 0);
	sl811_write(sl811, SL11H_CTLREG1, sl811->ctrl1);
	sl811_write(sl811, SL811HS_CTLREG2, SL811HS_CTL2_INIT);
	sl811_write(sl811, SL11H_IRQ_ENABLE, sl811->irq_enable);

	// if !is_on, put into lowpower mode now
}

/*-------------------------------------------------------------------------*/

/* This is a PIO-only HCD.  Queueing appends URBs to the endpoint's queue,
 * and may start I/O.  Endpoint queues are scanned during completion irq
 * handlers (one per packet: ACK, NAK, faults, etc) and urb cancelation.
 *
 * Using an external DMA engine to copy a packet at a time could work,
 * though setup/teardown costs may be too big to make it worthwhile.
 */

/* SETUP starts a new control request.  Devices are not allowed to
 * STALL or NAK these; they must cancel any pending control requests.
 */
static void setup_packet(
	struct sl811		*sl811,
	struct sl811h_ep	*ep,
	struct urb		*urb,
	u8			bank,
	u8			control
)
{
	u8			addr;
	u8			len;
	void __iomem		*data_reg;

	addr = SL811HS_PACKET_BUF(bank == 0);
	len = sizeof(struct usb_ctrlrequest);
	data_reg = sl811->data_reg;
	sl811_write_buf(sl811, addr, urb->setup_packet, len);

	/* autoincrementing */
	sl811_write(sl811, bank + SL11H_BUFADDRREG, addr);
	writeb(len, data_reg);
	writeb(SL_SETUP /* | ep->epnum */, data_reg);
	writeb(usb_pipedevice(urb->pipe), data_reg);

	/* always OUT/data0 */ ;
	sl811_write(sl811, bank + SL11H_HOSTCTLREG,
			control | SL11H_HCTLMASK_OUT);
	ep->length = 0;
	PACKET("SETUP qh%p\n", ep);
}

/* STATUS finishes control requests, often after IN or OUT data packets */
static void status_packet(
	struct sl811		*sl811,
	struct sl811h_ep	*ep,
	struct urb		*urb,
	u8			bank,
	u8			control
)
{
	int			do_out;
	void __iomem		*data_reg;

	do_out = urb->transfer_buffer_length && usb_pipein(urb->pipe);
	data_reg = sl811->data_reg;

	/* autoincrementing */
	sl811_write(sl811, bank + SL11H_BUFADDRREG, 0);
	writeb(0, data_reg);
	writeb((do_out ? SL_OUT : SL_IN) /* | ep->epnum */, data_reg);
	writeb(usb_pipedevice(urb->pipe), data_reg);

	/* always data1; sometimes IN */
	control |= SL11H_HCTLMASK_TOGGLE;
	if (do_out)
		control |= SL11H_HCTLMASK_OUT;
	sl811_write(sl811, bank + SL11H_HOSTCTLREG, control);
	ep->length = 0;
	PACKET("STATUS%s/%s qh%p\n", ep->nak_count ? "/retry" : "",
			do_out ? "out" : "in", ep);
}

/* IN packets can be used with any type of endpoint. here we just
 * start the transfer, data from the peripheral may arrive later.
 * urb->iso_frame_desc is currently ignored here...
 */
static void in_packet(
	struct sl811		*sl811,
	struct sl811h_ep	*ep,
	struct urb		*urb,
	u8			bank,
	u8			control
)
{
	u8			addr;
	u8			len;
	void __iomem		*data_reg;

	/* avoid losing data on overflow */
	len = ep->maxpacket;
	addr = SL811HS_PACKET_BUF(bank == 0);
	if (!(control & SL11H_HCTLMASK_ISOCH)
			&& usb_gettoggle(urb->dev, ep->epnum, 0))
		control |= SL11H_HCTLMASK_TOGGLE;
	data_reg = sl811->data_reg;

	/* autoincrementing */
	sl811_write(sl811, bank + SL11H_BUFADDRREG, addr);
	writeb(len, data_reg);
	writeb(SL_IN | ep->epnum, data_reg);
	writeb(usb_pipedevice(urb->pipe), data_reg);

	sl811_write(sl811, bank + SL11H_HOSTCTLREG, control);
	ep->length = min((int)len,
			urb->transfer_buffer_length - urb->actual_length);
	PACKET("IN%s/%d qh%p len%d\n", ep->nak_count ? "/retry" : "",
			!!usb_gettoggle(urb->dev, ep->epnum, 0), ep, len);
}

/* OUT packets can be used with any type of endpoint.
 * urb->iso_frame_desc is currently ignored here...
 */
static void out_packet(
	struct sl811		*sl811,
	struct sl811h_ep	*ep,
	struct urb		*urb,
	u8			bank,
	u8			control
)
{
	void			*buf;
	u8			addr;
	u8			len;
	void __iomem		*data_reg;

	buf = urb->transfer_buffer + urb->actual_length;
	prefetch(buf);

	len = min((int)ep->maxpacket,
			urb->transfer_buffer_length - urb->actual_length);

	if (!(control & SL11H_HCTLMASK_ISOCH)
			&& usb_gettoggle(urb->dev, ep->epnum, 1))
		control |= SL11H_HCTLMASK_TOGGLE;
	addr = SL811HS_PACKET_BUF(bank == 0);
	data_reg = sl811->data_reg;

	sl811_write_buf(sl811, addr, buf, len);

	/* autoincrementing */
	sl811_write(sl811, bank + SL11H_BUFADDRREG, addr);
	writeb(len, data_reg);
	writeb(SL_OUT | ep->epnum, data_reg);
	writeb(usb_pipedevice(urb->pipe), data_reg);

	sl811_write(sl811, bank + SL11H_HOSTCTLREG,
			control | SL11H_HCTLMASK_OUT);
	ep->length = len;
	PACKET("OUT%s/%d qh%p len%d\n", ep->nak_count ? "/retry" : "",
			!!usb_gettoggle(urb->dev, ep->epnum, 1), ep, len);
}

/*-------------------------------------------------------------------------*/

/* caller updates on-chip enables later */

static inline void sofirq_on(struct sl811 *sl811)
{
	if (sl811->irq_enable & SL11H_INTMASK_SOFINTR)
		return;
	VDBG("sof irq on\n");
	sl811->irq_enable |= SL11H_INTMASK_SOFINTR;
}

static inline void sofirq_off(struct sl811 *sl811)
{
	if (!(sl811->irq_enable & SL11H_INTMASK_SOFINTR))
		return;
	VDBG("sof irq off\n");
	sl811->irq_enable &= ~SL11H_INTMASK_SOFINTR;
}

/*-------------------------------------------------------------------------*/

/* pick the next endpoint for a transaction, and issue it.
 * frames start with periodic transfers (after whatever is pending
 * from the previous frame), and the rest of the time is async
 * transfers, scheduled round-robin.
 */
static struct sl811h_ep	*start(struct sl811 *sl811, u8 bank)
{
	struct sl811h_ep	*ep;
	struct sl811h_req	*req;
	struct urb		*urb;
	int			fclock;
	u8			control;

	/* use endpoint at schedule head */
	if (sl811->next_periodic) {
		ep = sl811->next_periodic;
		sl811->next_periodic = ep->next;
	} else {
		if (sl811->next_async)
			ep = sl811->next_async;
		else if (!list_empty(&sl811->async))
			ep = container_of(sl811->async.next,
					struct sl811h_ep, schedule);
		else {
			/* could set up the first fullspeed periodic
			 * transfer for the next frame ...
			 */
			return NULL;
		}

#ifdef USE_B
		if ((bank && sl811->active_b == ep) || sl811->active_a == ep)
			return NULL;
#endif

		if (ep->schedule.next == &sl811->async)
			sl811->next_async = NULL;
		else
			sl811->next_async = container_of(ep->schedule.next,
					struct sl811h_ep, schedule);
	}

	if (unlikely(list_empty(&ep->queue))) {
		DBG("empty %p queue?\n", ep);
		return NULL;
	}

	req = container_of(ep->queue.next, struct sl811h_req, queue);
	urb = req->urb;
	control = ep->defctrl;

	/* if this frame doesn't have enough time left to transfer this
	 * packet, wait till the next frame.  too-simple algorithm...
	 */
	fclock = sl811_read(sl811, SL11H_SOFTMRREG) << 6;
	fclock -= 100;		/* setup takes not much time */
	if (urb->dev->speed == USB_SPEED_LOW) {
		if (control & SL11H_HCTLMASK_PREAMBLE) {
			/* also note erratum 1: some hubs won't work */
			fclock -= 800;
		}
		fclock -= ep->maxpacket << 8;

		/* erratum 2: AFTERSOF only works for fullspeed */
		if (fclock < 0) {
			if (ep->period)
				sl811->stat_overrun++;
			sofirq_on(sl811);
			return NULL;
		}
	} else {
		fclock -= 12000 / 19;	/* 19 64byte packets/msec */
		if (fclock < 0) {
			if (ep->period)
				sl811->stat_overrun++;
			control |= SL11H_HCTLMASK_AFTERSOF;

		/* throttle bulk/control irq noise */
		} else if (ep->nak_count)
			control |= SL11H_HCTLMASK_AFTERSOF;
	}


	switch (ep->nextpid) {
	case USB_PID_IN:
		in_packet(sl811, ep, urb, bank, control);
		break;
	case USB_PID_OUT:
		out_packet(sl811, ep, urb, bank, control);
		break;
	case USB_PID_SETUP:
		setup_packet(sl811, ep, urb, bank, control);
		break;
	case USB_PID_ACK:		/* for control status */
		status_packet(sl811, ep, urb, bank, control);
		break;
	default:
		DBG("bad ep%p pid %02x\n", ep, ep->nextpid);
		ep = NULL;
	}
	return ep;
}

#define MIN_JIFFIES	((msecs_to_jiffies(2) > 1) ? msecs_to_jiffies(2) : 2)

static inline void start_transfer(struct sl811 *sl811)
{
	if (sl811->port1 & (1 << USB_PORT_FEAT_SUSPEND))
		return;
	if (sl811->active_a == NULL) {
		sl811->active_a = start(sl811, SL811_EP_A(SL811_HOST_BUF));
		if (sl811->active_a != NULL)
			sl811->jiffies_a = jiffies + MIN_JIFFIES;
	}
#ifdef USE_B
	if (sl811->active_b == NULL) {
		sl811->active_b = start(sl811, SL811_EP_B(SL811_HOST_BUF));
		if (sl811->active_b != NULL)
			sl811->jiffies_b = jiffies + MIN_JIFFIES;
	}
#endif
}

static void finish_request(
	struct sl811		*sl811,
	struct sl811h_ep	*ep,
	struct sl811h_req	*req,
	struct pt_regs		*regs,
	int			status
) __releases(sl811->lock) __acquires(sl811->lock)
{
	unsigned		i;
	struct urb		*urb = req->urb;

	list_del(&req->queue);
	kfree(req);
	urb->hcpriv = NULL;

	if (usb_pipecontrol(urb->pipe))
		ep->nextpid = USB_PID_SETUP;

	spin_lock(&urb->lock);
	if (urb->status == -EINPROGRESS)
		urb->status = status;
	spin_unlock(&urb->lock);

	spin_unlock(&sl811->lock);
	usb_hcd_giveback_urb(&sl811->hcd, urb, regs);
	spin_lock(&sl811->lock);

	/* leave active endpoints in the schedule */
	if (!list_empty(&ep->queue))
		return;

	/* async deschedule? */
	if (!list_empty(&ep->schedule)) {
		list_del_init(&ep->schedule);
		if (ep == sl811->next_async)
			sl811->next_async = NULL;
		return;
	}

	/* periodic deschedule */
	DBG("deschedule qh%d/%p branch %d\n", ep->period, ep, ep->branch);
	for (i = ep->branch; i < PERIODIC_SIZE; i += ep->period) {
		struct sl811h_ep	*temp;
		struct sl811h_ep	**prev = &sl811->periodic[i];

		while (*prev && ((temp = *prev) != ep))
			prev = &temp->next;
		if (*prev)
			*prev = ep->next;
		sl811->load[i] -= ep->load;
	}	
	ep->branch = PERIODIC_SIZE;
	sl811->periodic_count--;
	hcd_to_bus(&sl811->hcd)->bandwidth_allocated
		-= ep->load / ep->period;
	if (ep == sl811->next_periodic)
		sl811->next_periodic = ep->next;

	/* we might turn SOFs back on again for the async schedule */
	if (sl811->periodic_count == 0)
		sofirq_off(sl811);
}

static void
done(struct sl811 *sl811, struct sl811h_ep *ep, u8 bank, struct pt_regs *regs)
{
	u8			status;
	struct sl811h_req	*req;
	struct urb		*urb;
	int			urbstat = -EINPROGRESS;

	if (unlikely(!ep))
		return;

	status = sl811_read(sl811, bank + SL11H_PKTSTATREG);

	req = container_of(ep->queue.next, struct sl811h_req, queue);
	urb = req->urb;

	/* we can safely ignore NAKs */
	if (status & SL11H_STATMASK_NAK) {
		// PACKET("...NAK_%02x qh%p\n", bank, ep);
		if (!ep->period)
			ep->nak_count++;
		ep->error_count = 0;

	/* ACK advances transfer, toggle, and maybe queue */
	} else if (status & SL11H_STATMASK_ACK) {
		struct usb_device	*udev = urb->dev;
		int			len;
		unsigned char		*buf;

		/* urb->iso_frame_desc is currently ignored here... */

		ep->nak_count = ep->error_count = 0;
		switch (ep->nextpid) {
		case USB_PID_OUT:
			// PACKET("...ACK/out_%02x qh%p\n", bank, ep);
			urb->actual_length += ep->length;
			usb_dotoggle(udev, ep->epnum, 1);
			if (urb->actual_length
					== urb->transfer_buffer_length) {
				if (usb_pipecontrol(urb->pipe))
					ep->nextpid = USB_PID_ACK;

				/* some bulk protocols terminate OUT transfers
				 * by a short packet, using ZLPs not padding.
				 */
				else if (ep->length < ep->maxpacket
						|| !(urb->transfer_flags
							& URB_ZERO_PACKET))
					urbstat = 0;
			}
			break;
		case USB_PID_IN:
			// PACKET("...ACK/in_%02x qh%p\n", bank, ep);
			buf = urb->transfer_buffer + urb->actual_length;
			prefetchw(buf);
			len = ep->maxpacket - sl811_read(sl811,
						bank + SL11H_XFERCNTREG);
			if (len > ep->length) {
				len = ep->length;
				urb->status = -EOVERFLOW;
			}
			urb->actual_length += len;
			sl811_read_buf(sl811, SL811HS_PACKET_BUF(bank == 0),
					buf, len);
			usb_dotoggle(udev, ep->epnum, 0);
			if (urb->actual_length == urb->transfer_buffer_length)
				urbstat = 0;
			else if (len < ep->maxpacket) {
				if (urb->transfer_flags & URB_SHORT_NOT_OK)
					urbstat = -EREMOTEIO;
				else
					urbstat = 0;
			}
			if (usb_pipecontrol(urb->pipe)
					&& (urbstat == -EREMOTEIO
						|| urbstat == 0)) {

				/* NOTE if the status stage STALLs (why?),
				 * this reports the wrong urb status.
				 */
				spin_lock(&urb->lock);
				if (urb->status == -EINPROGRESS)
					urb->status = urbstat;
				spin_unlock(&urb->lock);

				req = NULL;
				ep->nextpid = USB_PID_ACK;
			}
			break;
		case USB_PID_SETUP:
			// PACKET("...ACK/setup_%02x qh%p\n", bank, ep);
			if (urb->transfer_buffer_length == urb->actual_length)
				ep->nextpid = USB_PID_ACK;
			else if (usb_pipeout(urb->pipe)) {
				usb_settoggle(udev, 0, 1, 1);
				ep->nextpid = USB_PID_OUT;
			} else {
				usb_settoggle(udev, 0, 0, 1);
				ep->nextpid = USB_PID_IN;
			}
			break;
		case USB_PID_ACK:
			// PACKET("...ACK/status_%02x qh%p\n", bank, ep);
			urbstat = 0;
			break;
		}

	/* STALL stops all transfers */
	} else if (status & SL11H_STATMASK_STALL) {
		PACKET("...STALL_%02x qh%p\n", bank, ep);
		ep->nak_count = ep->error_count = 0;
		urbstat = -EPIPE;

	/* error? retry, until "3 strikes" */
	} else if (++ep->error_count >= 3) {
		if (status & SL11H_STATMASK_TMOUT)
			urbstat = -ETIMEDOUT;
		else if (status & SL11H_STATMASK_OVF)
			urbstat = -EOVERFLOW;
		else
			urbstat = -EPROTO;
		ep->error_count = 0;
		PACKET("...3STRIKES_%02x %02x qh%p stat %d\n",
				bank, status, ep, urbstat);
	}

	if ((urbstat != -EINPROGRESS || urb->status != -EINPROGRESS)
			&& req)
		finish_request(sl811, ep, req, regs, urbstat);
}

static inline u8 checkdone(struct sl811 *sl811)
{
	u8	ctl;
	u8	irqstat = 0;

	if (sl811->active_a && time_before_eq(sl811->jiffies_a, jiffies)) {
		ctl = sl811_read(sl811, SL811_EP_A(SL11H_HOSTCTLREG));
		if (ctl & SL11H_HCTLMASK_ARM)
			sl811_write(sl811, SL811_EP_A(SL11H_HOSTCTLREG), 0);
		DBG("%s DONE_A: ctrl %02x sts %02x\n",
			(ctl & SL11H_HCTLMASK_ARM) ? "timeout" : "lost",
			ctl,
			sl811_read(sl811, SL811_EP_A(SL11H_PKTSTATREG)));
		irqstat |= SL11H_INTMASK_DONE_A;
	}
#ifdef	USE_B
	if (sl811->active_b && time_before_eq(sl811->jiffies_b, jiffies)) {
		ctl = sl811_read(sl811, SL811_EP_B(SL11H_HOSTCTLREG));
		if (ctl & SL11H_HCTLMASK_ARM)
			sl811_write(sl811, SL811_EP_B(SL11H_HOSTCTLREG), 0);
		DBG("%s DONE_B: ctrl %02x sts %02x\n", ctl,
			(ctl & SL11H_HCTLMASK_ARM) ? "timeout" : "lost",
			ctl,
			sl811_read(sl811, SL811_EP_B(SL11H_PKTSTATREG)));
		irqstat |= SL11H_INTMASK_DONE_A;
	}
#endif
	return irqstat;
}

static irqreturn_t sl811h_irq(int irq, void *_sl811, struct pt_regs *regs)
{
	struct sl811	*sl811 = _sl811;
	u8		irqstat;
	irqreturn_t	ret = IRQ_NONE;
	unsigned	retries = 5;

	spin_lock(&sl811->lock);

retry:
	irqstat = sl811_read(sl811, SL11H_IRQ_STATUS) & ~SL11H_INTMASK_DP;
	if (irqstat) {
		sl811_write(sl811, SL11H_IRQ_STATUS, irqstat);
		irqstat &= sl811->irq_enable;
	}

#ifdef	QUIRK2
	/* this may no longer be necessary ... */
	if (irqstat == 0 && ret == IRQ_NONE) {
		irqstat = checkdone(sl811);
		if (irqstat && irq != ~0)
			sl811->stat_lost++;
	}
#endif

	/* USB packets, not necessarily handled in the order they're
	 * issued ... that's fine if they're different endpoints.
	 */
	if (irqstat & SL11H_INTMASK_DONE_A) {
		done(sl811, sl811->active_a, SL811_EP_A(SL811_HOST_BUF), regs);
		sl811->active_a = NULL;
		sl811->stat_a++;
	}
#ifdef USE_B
	if (irqstat & SL11H_INTMASK_DONE_B) {
		done(sl811, sl811->active_b, SL811_EP_B(SL811_HOST_BUF), regs);
		sl811->active_b = NULL;
		sl811->stat_b++;
	}
#endif
	if (irqstat & SL11H_INTMASK_SOFINTR) {
		unsigned index;

		index = sl811->frame++ % (PERIODIC_SIZE - 1);
		sl811->stat_sof++;

		/* be graceful about almost-inevitable periodic schedule
		 * overruns:  continue the previous frame's transfers iff
		 * this one has nothing scheduled.
		 */
		if (sl811->next_periodic) {
			// ERR("overrun to slot %d\n", index);
			sl811->stat_overrun++;
		}
		if (sl811->periodic[index])
			sl811->next_periodic = sl811->periodic[index];
	}

	/* khubd manages debouncing and wakeup */
	if (irqstat & SL11H_INTMASK_INSRMV) {
		sl811->stat_insrmv++;

		/* most stats are reset for each VBUS session */
		sl811->stat_wake = 0;
		sl811->stat_sof = 0;
		sl811->stat_a = 0;
		sl811->stat_b = 0;
		sl811->stat_lost = 0;

		sl811->ctrl1 = 0;
		sl811_write(sl811, SL11H_CTLREG1, sl811->ctrl1);

		sl811->irq_enable = SL11H_INTMASK_INSRMV;
		sl811_write(sl811, SL11H_IRQ_ENABLE, sl811->irq_enable);

		/* usbcore nukes other pending transactions on disconnect */
		if (sl811->active_a) {
			sl811_write(sl811, SL811_EP_A(SL11H_HOSTCTLREG), 0);
			finish_request(sl811, sl811->active_a,
				container_of(sl811->active_a->queue.next,
					struct sl811h_req, queue),
				NULL, -ESHUTDOWN);
			sl811->active_a = NULL;
		}
#ifdef	USE_B
		if (sl811->active_b) {
			sl811_write(sl811, SL811_EP_B(SL11H_HOSTCTLREG), 0);
			finish_request(sl811, sl811->active_b,
				container_of(sl811->active_b->queue.next,
					struct sl811h_req, queue),
				NULL, -ESHUTDOWN);
			sl811->active_b = NULL;
		}
#endif

		/* port status seems wierd until after reset, so
		 * force the reset and make khubd clean up later.
		 */
		sl811->port1 |= (1 << USB_PORT_FEAT_C_CONNECTION)
				| (1 << USB_PORT_FEAT_CONNECTION);

	} else if (irqstat & SL11H_INTMASK_RD) {
		if (sl811->port1 & (1 << USB_PORT_FEAT_SUSPEND)) {
			DBG("wakeup\n");
			sl811->port1 |= 1 << USB_PORT_FEAT_C_SUSPEND;
			sl811->stat_wake++;
		} else
			irqstat &= ~SL11H_INTMASK_RD;
	}

	if (irqstat) {
		if (sl811->port1 & (1 << USB_PORT_FEAT_ENABLE))
			start_transfer(sl811);
		ret = IRQ_HANDLED;
		sl811->hcd.saw_irq = 1;
		if (retries--)
			goto retry;
	}

	if (sl811->periodic_count == 0 && list_empty(&sl811->async)) 
		sofirq_off(sl811);
	sl811_write(sl811, SL11H_IRQ_ENABLE, sl811->irq_enable);

	spin_unlock(&sl811->lock);

	return ret;
}

/*-------------------------------------------------------------------------*/

/* usb 1.1 says max 90% of a frame is available for periodic transfers.
 * this driver doesn't promise that much since it's got to handle an
 * IRQ per packet; irq handling latencies also use up that time.
 */
#define	MAX_PERIODIC_LOAD	500	/* out of 1000 usec */

static int balance(struct sl811 *sl811, u16 period, u16 load)
{
	int	i, branch = -ENOSPC;

	/* search for the least loaded schedule branch of that period
	 * which has enough bandwidth left unreserved.
	 */
	for (i = 0; i < period ; i++) {
		if (branch < 0 || sl811->load[branch] > sl811->load[i]) {
			int	j;

			for (j = i; j < PERIODIC_SIZE; j += period) {
				if ((sl811->load[j] + load)
						> MAX_PERIODIC_LOAD)
					break;
			}
			if (j < PERIODIC_SIZE)
				continue;
			branch = i; 
		}
	}
	return branch;
}

/*-------------------------------------------------------------------------*/

static int sl811h_urb_enqueue(
	struct usb_hcd	*hcd,
	struct urb	*urb,
	int		mem_flags
) {
	struct sl811		*sl811 = hcd_to_sl811(hcd);
	struct usb_device	*udev = urb->dev;
	struct hcd_dev		*hdev = (struct hcd_dev *) udev->hcpriv;
	unsigned int		pipe = urb->pipe;
	int			is_out = !usb_pipein(pipe);
	int			type = usb_pipetype(pipe);
	int			epnum = usb_pipeendpoint(pipe);
	struct sl811h_ep	*ep = NULL;
	struct sl811h_req	*req;
	unsigned long		flags;
	int			i;
	int			retval = 0;

#ifdef	DISABLE_ISO
	if (type == PIPE_ISOCHRONOUS)
		return -ENOSPC;
#endif

	/* avoid all allocations within spinlocks: request or endpoint */
	urb->hcpriv = req = kmalloc(sizeof *req, mem_flags);
	if (!req)
		return -ENOMEM;
	req->urb = urb;

	i = epnum << 1;
	if (i && is_out)
		i |= 1;
	if (!hdev->ep[i])
		ep = kcalloc(1, sizeof *ep, mem_flags);

	spin_lock_irqsave(&sl811->lock, flags);

	/* don't submit to a dead or disabled port */
	if (!(sl811->port1 & (1 << USB_PORT_FEAT_ENABLE))
			|| !HCD_IS_RUNNING(sl811->hcd.state)) {
		retval = -ENODEV;
		goto fail;
	}

	if (hdev->ep[i]) {
		kfree(ep);
		ep = hdev->ep[i];
	} else if (!ep) {
		retval = -ENOMEM;
		goto fail;

	} else {
		INIT_LIST_HEAD(&ep->queue);
		INIT_LIST_HEAD(&ep->schedule);
		ep->udev = usb_get_dev(udev);
		ep->epnum = epnum;
		ep->maxpacket = usb_maxpacket(udev, urb->pipe, is_out);
		ep->defctrl = SL11H_HCTLMASK_ARM | SL11H_HCTLMASK_ENABLE;
		usb_settoggle(udev, epnum, is_out, 0);

		if (type == PIPE_CONTROL)
			ep->nextpid = USB_PID_SETUP;
		else if (is_out)
			ep->nextpid = USB_PID_OUT;
		else
			ep->nextpid = USB_PID_IN;

		if (ep->maxpacket > H_MAXPACKET) {
			/* iso packets up to 240 bytes could work... */
			DBG("dev %d ep%d maxpacket %d\n",
				udev->devnum, epnum, ep->maxpacket);
			retval = -EINVAL;
			goto fail;
		}

		if (udev->speed == USB_SPEED_LOW) {
			/* send preamble for external hub? */
			if (!(sl811->ctrl1 & SL11H_CTL1MASK_LSPD))
				ep->defctrl |= SL11H_HCTLMASK_PREAMBLE;
		}
		switch (type) {
		case PIPE_ISOCHRONOUS:
		case PIPE_INTERRUPT:
			if (urb->interval > PERIODIC_SIZE)
				urb->interval = PERIODIC_SIZE;
			ep->period = urb->interval;
			ep->branch = PERIODIC_SIZE;
			if (type == PIPE_ISOCHRONOUS)
				ep->defctrl |= SL11H_HCTLMASK_ISOCH;
			ep->load = usb_calc_bus_time(udev->speed, !is_out,
				(type == PIPE_ISOCHRONOUS),
				usb_maxpacket(udev, pipe, is_out))
					/ 1000;
			break;
		}

		hdev->ep[i] = ep;
	}

	/* maybe put endpoint into schedule */
	switch (type) {
	case PIPE_CONTROL:
	case PIPE_BULK:
		if (list_empty(&ep->schedule))
			list_add_tail(&ep->schedule, &sl811->async);
		break;
	case PIPE_ISOCHRONOUS:
	case PIPE_INTERRUPT:
		urb->interval = ep->period;
		if (ep->branch < PERIODIC_SIZE)
			break;

		retval = balance(sl811, ep->period, ep->load);
		if (retval < 0)
			goto fail;
		ep->branch = retval;
		retval = 0;
		urb->start_frame = (sl811->frame & (PERIODIC_SIZE - 1))
					+ ep->branch;

		/* sort each schedule branch by period (slow before fast)
		 * to share the faster parts of the tree without needing
		 * dummy/placeholder nodes
		 */
		DBG("schedule qh%d/%p branch %d\n", ep->period, ep, ep->branch);
		for (i = ep->branch; i < PERIODIC_SIZE; i += ep->period) {
			struct sl811h_ep	**prev = &sl811->periodic[i];
			struct sl811h_ep	*here = *prev;

			while (here && ep != here) {
				if (ep->period > here->period)
					break;
				prev = &here->next;
				here = *prev;
			}
			if (ep != here) {
				ep->next = here;
				*prev = ep;
			}
			sl811->load[i] += ep->load;
		}
		sl811->periodic_count++;
		hcd_to_bus(&sl811->hcd)->bandwidth_allocated
				+= ep->load / ep->period;
		sofirq_on(sl811);
	}

	/* in case of unlink-during-submit */
	spin_lock(&urb->lock);
	if (urb->status != -EINPROGRESS) {
		spin_unlock(&urb->lock);
		finish_request(sl811, ep, req, NULL, 0);
		req = NULL;
		retval = 0;
		goto fail;
	}
	list_add_tail(&req->queue, &ep->queue);
	spin_unlock(&urb->lock);

	start_transfer(sl811);
	sl811_write(sl811, SL11H_IRQ_ENABLE, sl811->irq_enable);
fail:
	spin_unlock_irqrestore(&sl811->lock, flags);
	if (retval)
		kfree(req);
	return retval;
}

static int sl811h_urb_dequeue(struct usb_hcd *hcd, struct urb *urb)
{
	struct sl811		*sl811 = hcd_to_sl811(hcd);
	struct usb_device	*udev = urb->dev;
	struct hcd_dev		*hdev = (struct hcd_dev *) udev->hcpriv;
	unsigned int		pipe = urb->pipe;
	int			is_out = !usb_pipein(pipe);
	unsigned long		flags;
	unsigned		i;
	struct sl811h_ep	*ep;
	struct sl811h_req	*req = urb->hcpriv;
	int			retval = 0;

	i = usb_pipeendpoint(pipe) << 1;
	if (i && is_out)
		i |= 1;

	spin_lock_irqsave(&sl811->lock, flags);
	ep = hdev->ep[i];
	if (ep) {
		/* finish right away if this urb can't be active ...
		 * note that some drivers wrongly expect delays
		 */
		if (ep->queue.next != &req->queue) {
			/* not front of queue?  never active */

		/* for active transfers, we expect an IRQ */
		} else if (sl811->active_a == ep) {
			if (time_before_eq(sl811->jiffies_a, jiffies)) {
				/* happens a lot with lowspeed?? */
				DBG("giveup on DONE_A: ctrl %02x sts %02x\n",
					sl811_read(sl811,
						SL811_EP_A(SL11H_HOSTCTLREG)),
					sl811_read(sl811,
						SL811_EP_A(SL11H_PKTSTATREG)));
				sl811_write(sl811, SL811_EP_A(SL11H_HOSTCTLREG),
						0);
				sl811->active_a = NULL;
			} else
				req = NULL;
#ifdef	USE_B
		} else if (sl811->active_b == ep) {
			if (time_before_eq(sl811->jiffies_a, jiffies)) {
				/* happens a lot with lowspeed?? */
				DBG("giveup on DONE_B: ctrl %02x sts %02x\n",
					sl811_read(sl811,
						SL811_EP_B(SL11H_HOSTCTLREG)),
					sl811_read(sl811,
						SL811_EP_B(SL11H_PKTSTATREG)));
				sl811_write(sl811, SL811_EP_B(SL11H_HOSTCTLREG),
						0);
				sl811->active_b = NULL;
			} else
				req = NULL;
#endif
		} else {
			/* front of queue for inactive endpoint */
		}

		if (req)
			finish_request(sl811, ep, req, NULL, 0);
		else
			VDBG("dequeue, urb %p active %s; wait4irq\n", urb,
				(sl811->active_a == ep) ? "A" : "B");
	} else
		retval = -EINVAL;
	spin_unlock_irqrestore(&sl811->lock, flags);
	return retval;
}

static void
sl811h_endpoint_disable(struct usb_hcd *hcd, struct hcd_dev *hdev, int epnum)
{
	struct sl811		*sl811 = hcd_to_sl811(hcd);
	struct sl811h_ep	*ep;
	unsigned long		flags;
	int			i;

	i = (epnum & 0xf) << 1;
	if (epnum && !(epnum & USB_DIR_IN))
		i |= 1;

	spin_lock_irqsave(&sl811->lock, flags);
	ep = hdev->ep[i];
	hdev->ep[i] = NULL;
	spin_unlock_irqrestore(&sl811->lock, flags);

	if (ep) {
		/* assume we'd just wait for the irq */
		if (!list_empty(&ep->queue))
			msleep(3);
		if (!list_empty(&ep->queue))
			WARN("ep %p not empty?\n", ep);

		usb_put_dev(ep->udev);
		kfree(ep);
	}
	return;
}

static int
sl811h_get_frame(struct usb_hcd *hcd)
{
	struct sl811 *sl811 = hcd_to_sl811(hcd);

	/* wrong except while periodic transfers are scheduled;
	 * never matches the on-the-wire frame;
	 * subject to overruns.
	 */
	return sl811->frame;
}


/*-------------------------------------------------------------------------*/

/* the virtual root hub timer IRQ checks for hub status */
static int
sl811h_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct sl811 *sl811 = hcd_to_sl811(hcd);
#ifdef	QUIRK3
	unsigned long flags;

	/* non-SMP HACK: use root hub timer as i/o watchdog
	 * this seems essential when SOF IRQs aren't in use...
	 */
	local_irq_save(flags);
	if (!timer_pending(&sl811->timer)) {
		if (sl811h_irq(~0, sl811, NULL) != IRQ_NONE)
			sl811->stat_lost++;
	}
	local_irq_restore(flags);
#endif

	if (!(sl811->port1 & (0xffff << 16)))
		return 0;

	/* tell khubd port 1 changed */
	*buf = (1 << 1);
	return 1;
}

static void
sl811h_hub_descriptor (
	struct sl811			*sl811,
	struct usb_hub_descriptor	*desc
) {
	u16		temp = 0;

	desc->bDescriptorType = 0x29;
	desc->bHubContrCurrent = 0;

	desc->bNbrPorts = 1;
	desc->bDescLength = 9;

	/* per-port power switching (gang of one!), or none */
	desc->bPwrOn2PwrGood = 0;
	if (sl811->board && sl811->board->port_power) {
		desc->bPwrOn2PwrGood = sl811->board->potpg;
		if (!desc->bPwrOn2PwrGood)
			desc->bPwrOn2PwrGood = 10;
		temp = 0x0001;
	} else
		temp = 0x0002;

	/* no overcurrent errors detection/handling */
	temp |= 0x0010;

	desc->wHubCharacteristics = (__force __u16)cpu_to_le16(temp);

	/* two bitmaps:  ports removable, and legacy PortPwrCtrlMask */
	desc->bitmap[0] = 1 << 1;
	desc->bitmap[1] = ~0;
}

static void
sl811h_timer(unsigned long _sl811)
{
	struct sl811 	*sl811 = (void *) _sl811;
	unsigned long	flags;
	u8		irqstat;
	u8		signaling = sl811->ctrl1 & SL11H_CTL1MASK_FORCE;
	const u32	mask = (1 << USB_PORT_FEAT_CONNECTION)
				| (1 << USB_PORT_FEAT_ENABLE)
				| (1 << USB_PORT_FEAT_LOWSPEED);

	spin_lock_irqsave(&sl811->lock, flags);

	/* stop special signaling */
	sl811->ctrl1 &= ~SL11H_CTL1MASK_FORCE;
	sl811_write(sl811, SL11H_CTLREG1, sl811->ctrl1);
	udelay(3);

	irqstat = sl811_read(sl811, SL11H_IRQ_STATUS);

	switch (signaling) {
	case SL11H_CTL1MASK_SE0:
		DBG("end reset\n");
		sl811->port1 = (1 << USB_PORT_FEAT_C_RESET)
				| (1 << USB_PORT_FEAT_POWER);
		sl811->ctrl1 = 0;
		/* don't wrongly ack RD */
		if (irqstat & SL11H_INTMASK_INSRMV)
			irqstat &= ~SL11H_INTMASK_RD;
		break;
	case SL11H_CTL1MASK_K:
		DBG("end resume\n");
		sl811->port1 &= ~(1 << USB_PORT_FEAT_SUSPEND);
		break;
	default:
		DBG("odd timer signaling: %02x\n", signaling);
		break;
	}
	sl811_write(sl811, SL11H_IRQ_STATUS, irqstat);

	if (irqstat & SL11H_INTMASK_RD) {
		/* usbcore nukes all pending transactions on disconnect */
		if (sl811->port1 & (1 << USB_PORT_FEAT_CONNECTION))
			sl811->port1 |= (1 << USB_PORT_FEAT_C_CONNECTION)
					| (1 << USB_PORT_FEAT_C_ENABLE);
		sl811->port1 &= ~mask;
		sl811->irq_enable = SL11H_INTMASK_INSRMV;
	} else {
		sl811->port1 |= mask;
		if (irqstat & SL11H_INTMASK_DP)
			sl811->port1 &= ~(1 << USB_PORT_FEAT_LOWSPEED);
		sl811->irq_enable = SL11H_INTMASK_INSRMV | SL11H_INTMASK_RD;
	}

	if (sl811->port1 & (1 << USB_PORT_FEAT_CONNECTION)) {
		u8	ctrl2 = SL811HS_CTL2_INIT;

		sl811->irq_enable |= SL11H_INTMASK_DONE_A;
#ifdef USE_B
		sl811->irq_enable |= SL11H_INTMASK_DONE_B;
#endif
		if (sl811->port1 & (1 << USB_PORT_FEAT_LOWSPEED)) {
			sl811->ctrl1 |= SL11H_CTL1MASK_LSPD;
			ctrl2 |= SL811HS_CTL2MASK_DSWAP;
		}

		/* start SOFs flowing, kickstarting with A registers */
		sl811->ctrl1 |= SL11H_CTL1MASK_SOF_ENA;
		sl811_write(sl811, SL11H_SOFLOWREG, 0xe0);
		sl811_write(sl811, SL811HS_CTLREG2, ctrl2);

		/* autoincrementing */
		sl811_write(sl811, SL811_EP_A(SL11H_BUFLNTHREG), 0);
		writeb(SL_SOF, sl811->data_reg);
		writeb(0, sl811->data_reg);
		sl811_write(sl811, SL811_EP_A(SL11H_HOSTCTLREG),
				SL11H_HCTLMASK_ARM);

		/* khubd provides debounce delay */
	} else {
		sl811->ctrl1 = 0;
	}
	sl811_write(sl811, SL11H_CTLREG1, sl811->ctrl1);

	/* reenable irqs */
	sl811_write(sl811, SL11H_IRQ_ENABLE, sl811->irq_enable);
	spin_unlock_irqrestore(&sl811->lock, flags);
}

static int
sl811h_hub_control(
	struct usb_hcd	*hcd,
	u16		typeReq,
	u16		wValue,
	u16		wIndex,
	char		*buf,
	u16		wLength
) {
	struct sl811	*sl811 = hcd_to_sl811(hcd);
	int		retval = 0;
	unsigned long	flags;

	spin_lock_irqsave(&sl811->lock, flags);

	switch (typeReq) {
	case ClearHubFeature:
	case SetHubFeature:
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
		case C_HUB_LOCAL_POWER:
			break;
		default:
			goto error;
		}
		break;
	case ClearPortFeature:
		if (wIndex != 1 || wLength != 0)
			goto error;

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			sl811->port1 &= (1 << USB_PORT_FEAT_POWER);
			sl811->ctrl1 = 0;
			sl811_write(sl811, SL11H_CTLREG1, sl811->ctrl1);
			sl811->irq_enable = SL11H_INTMASK_INSRMV;
			sl811_write(sl811, SL11H_IRQ_ENABLE,
						sl811->irq_enable);
			break;
		case USB_PORT_FEAT_SUSPEND:
			if (!(sl811->port1 & (1 << USB_PORT_FEAT_SUSPEND)))
				break;

			/* 20 msec of resume/K signaling, other irqs blocked */
			DBG("start resume...\n");
			sl811->irq_enable = 0;
			sl811_write(sl811, SL11H_IRQ_ENABLE,
						sl811->irq_enable);
			sl811->ctrl1 |= SL11H_CTL1MASK_K;
			sl811_write(sl811, SL11H_CTLREG1, sl811->ctrl1);

			mod_timer(&sl811->timer, jiffies
					+ msecs_to_jiffies(20));
			break;
		case USB_PORT_FEAT_POWER:
			port_power(sl811, 0);
			break;
		case USB_PORT_FEAT_C_ENABLE:
		case USB_PORT_FEAT_C_SUSPEND:
		case USB_PORT_FEAT_C_CONNECTION:
		case USB_PORT_FEAT_C_OVER_CURRENT:
		case USB_PORT_FEAT_C_RESET:
			break;
		default:
			goto error;
		}
		sl811->port1 &= ~(1 << wValue);
		break;
	case GetHubDescriptor:
		sl811h_hub_descriptor(sl811, (struct usb_hub_descriptor *) buf);
		break;
	case GetHubStatus:
		*(__le32 *) buf = cpu_to_le32(0);
		break;
	case GetPortStatus:
		if (wIndex != 1)
			goto error;
		*(__le32 *) buf = cpu_to_le32(sl811->port1);

#ifndef	VERBOSE
	if (*(u16*)(buf+2))	/* only if wPortChange is interesting */
#endif
		DBG("GetPortStatus %08x\n", sl811->port1);
		break;
	case SetPortFeature:
		if (wIndex != 1 || wLength != 0)
			goto error;
		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			if (sl811->port1 & (1 << USB_PORT_FEAT_RESET))
				goto error;
			if (!(sl811->port1 & (1 << USB_PORT_FEAT_ENABLE)))
				goto error;

			DBG("suspend...\n");
			sl811->ctrl1 &= ~SL11H_CTL1MASK_SOF_ENA;
			sl811_write(sl811, SL11H_CTLREG1, sl811->ctrl1);
			break;
		case USB_PORT_FEAT_POWER:
			port_power(sl811, 1);
			break;
		case USB_PORT_FEAT_RESET:
			if (sl811->port1 & (1 << USB_PORT_FEAT_SUSPEND))
				goto error;
			if (!(sl811->port1 & (1 << USB_PORT_FEAT_POWER)))
				break;

			/* 50 msec of reset/SE0 signaling, irqs blocked */
			sl811->irq_enable = 0;
			sl811_write(sl811, SL11H_IRQ_ENABLE,
						sl811->irq_enable);
			sl811->ctrl1 = SL11H_CTL1MASK_SE0;
			sl811_write(sl811, SL11H_CTLREG1, sl811->ctrl1);
			sl811->port1 |= (1 << USB_PORT_FEAT_RESET);
			mod_timer(&sl811->timer, jiffies
					+ msecs_to_jiffies(50));
			break;
		default:
			goto error;
		}
		sl811->port1 |= 1 << wValue;
		break;

	default:
error:
		/* "protocol stall" on error */
		retval = -EPIPE;
	}

	spin_unlock_irqrestore(&sl811->lock, flags);
	return retval;
}

#ifdef	CONFIG_PM

static int
sl811h_hub_suspend(struct usb_hcd *hcd)
{
	// SOFs off
	DBG("%s\n", __FUNCTION__);
	return 0;
}

static int
sl811h_hub_resume(struct usb_hcd *hcd)
{
	// SOFs on
	DBG("%s\n", __FUNCTION__);
	return 0;
}

#else

#define	sl811h_hub_suspend	NULL
#define	sl811h_hub_resume	NULL

#endif


/*-------------------------------------------------------------------------*/

#ifdef STUB_DEBUG_FILE

static inline void create_debug_file(struct sl811 *sl811) { }
static inline void remove_debug_file(struct sl811 *sl811) { }

#else

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static void dump_irq(struct seq_file *s, char *label, u8 mask)
{
	seq_printf(s, "%s %02x%s%s%s%s%s%s\n", label, mask,
		(mask & SL11H_INTMASK_DONE_A) ? " done_a" : "",
		(mask & SL11H_INTMASK_DONE_B) ? " done_b" : "",
		(mask & SL11H_INTMASK_SOFINTR) ? " sof" : "",
		(mask & SL11H_INTMASK_INSRMV) ? " ins/rmv" : "",
		(mask & SL11H_INTMASK_RD) ? " rd" : "",
		(mask & SL11H_INTMASK_DP) ? " dp" : "");
}

static int proc_sl811h_show(struct seq_file *s, void *unused)
{
	struct sl811		*sl811 = s->private;
	struct sl811h_ep	*ep;
	unsigned		i;

	seq_printf(s, "%s\n%s version %s\nportstatus[1] = %08x\n",
		sl811->hcd.product_desc,
		hcd_name, DRIVER_VERSION,
		sl811->port1);

	seq_printf(s, "insert/remove: %ld\n", sl811->stat_insrmv);
	seq_printf(s, "current session:  done_a %ld done_b %ld "
			"wake %ld sof %ld overrun %ld lost %ld\n\n",
		sl811->stat_a, sl811->stat_b,
		sl811->stat_wake, sl811->stat_sof,
		sl811->stat_overrun, sl811->stat_lost);

	spin_lock_irq(&sl811->lock);

	if (sl811->ctrl1 & SL11H_CTL1MASK_SUSPEND)
		seq_printf(s, "(suspended)\n\n");
	else {
		u8	t = sl811_read(sl811, SL11H_CTLREG1);

		seq_printf(s, "ctrl1 %02x%s%s%s%s\n", t,
			(t & SL11H_CTL1MASK_SOF_ENA) ? " sofgen" : "",
			({char *s; switch (t & SL11H_CTL1MASK_FORCE) {
			case SL11H_CTL1MASK_NORMAL: s = ""; break;
			case SL11H_CTL1MASK_SE0: s = " se0/reset"; break;
			case SL11H_CTL1MASK_K: s = " k/resume"; break;
			default: s = "j"; break;
			}; s; }),
			(t & SL11H_CTL1MASK_LSPD) ? " lowspeed" : "",
			(t & SL11H_CTL1MASK_SUSPEND) ? " suspend" : "");

		dump_irq(s, "irq_enable",
				sl811_read(sl811, SL11H_IRQ_ENABLE));
		dump_irq(s, "irq_status",
				sl811_read(sl811, SL11H_IRQ_STATUS));
		seq_printf(s, "frame clocks remaining:  %d\n",
				sl811_read(sl811, SL11H_SOFTMRREG) << 6);
	}

	seq_printf(s, "A: qh%p ctl %02x sts %02x\n", sl811->active_a,
		sl811_read(sl811, SL811_EP_A(SL11H_HOSTCTLREG)),
		sl811_read(sl811, SL811_EP_A(SL11H_PKTSTATREG)));
	seq_printf(s, "B: qh%p ctl %02x sts %02x\n", sl811->active_b,
		sl811_read(sl811, SL811_EP_B(SL11H_HOSTCTLREG)),
		sl811_read(sl811, SL811_EP_B(SL11H_PKTSTATREG)));
	seq_printf(s, "\n");
	list_for_each_entry (ep, &sl811->async, schedule) {
		struct sl811h_req	*req;

		seq_printf(s, "%s%sqh%p, ep%d%s, maxpacket %d"
					" nak %d err %d\n",
			(ep == sl811->active_a) ? "(A) " : "",
			(ep == sl811->active_b) ? "(B) " : "",
			ep, ep->epnum,
			({ char *s; switch (ep->nextpid) {
			case USB_PID_IN: s = "in"; break;
			case USB_PID_OUT: s = "out"; break;
			case USB_PID_SETUP: s = "setup"; break;
			case USB_PID_ACK: s = "status"; break;
			default: s = "?"; break;
			}; s;}),
			ep->maxpacket,
			ep->nak_count, ep->error_count);
		list_for_each_entry (req, &ep->queue, queue) {
			seq_printf(s, "  urb%p, %d/%d\n", req->urb,
				req->urb->actual_length,
				req->urb->transfer_buffer_length);
		}
	}
	if (!list_empty(&sl811->async))
		seq_printf(s, "\n");

	seq_printf(s, "periodic size= %d\n", PERIODIC_SIZE);

	for (i = 0; i < PERIODIC_SIZE; i++) {
		ep = sl811->periodic[i];
		if (!ep)
			continue;
		seq_printf(s, "%2d [%3d]:\n", i, sl811->load[i]);

		/* DUMB: prints shared entries multiple times */
		do {
			seq_printf(s,
				"   %s%sqh%d/%p (%sdev%d ep%d%s max %d) "
					"err %d\n",
				(ep == sl811->active_a) ? "(A) " : "",
				(ep == sl811->active_b) ? "(B) " : "",
				ep->period, ep,
				(ep->udev->speed == USB_SPEED_FULL)
					? "" : "ls ",
				ep->udev->devnum, ep->epnum,
				(ep->epnum == 0) ? ""
					: ((ep->nextpid == USB_PID_IN)
						? "in"
						: "out"),
				ep->maxpacket, ep->error_count);
			ep = ep->next;
		} while (ep);
	}

	spin_unlock_irq(&sl811->lock);
	seq_printf(s, "\n");

	return 0;
}

static int proc_sl811h_open(struct inode *inode, struct file *file)
{
	return single_open(file, proc_sl811h_show, PDE(inode)->data);
}

static struct file_operations proc_ops = {
	.open		= proc_sl811h_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

/* expect just one sl811 per system */
static const char proc_filename[] = "driver/sl811h";

static void create_debug_file(struct sl811 *sl811)
{
	struct proc_dir_entry *pde;

	pde = create_proc_entry(proc_filename, 0, NULL);
	if (pde == NULL)
		return;

	pde->proc_fops = &proc_ops;
	pde->data = sl811;
	sl811->pde = pde;
}

static void remove_debug_file(struct sl811 *sl811)
{
	if (sl811->pde)
		remove_proc_entry(proc_filename, NULL);
}

#endif

/*-------------------------------------------------------------------------*/

static void
sl811h_stop(struct usb_hcd *hcd)
{
	struct sl811	*sl811 = hcd_to_sl811(hcd);
	unsigned long	flags;

	del_timer_sync(&sl811->hcd.rh_timer);

	spin_lock_irqsave(&sl811->lock, flags);
	port_power(sl811, 0);
	spin_unlock_irqrestore(&sl811->lock, flags);
}

static int
sl811h_start(struct usb_hcd *hcd)
{
	struct sl811		*sl811 = hcd_to_sl811(hcd);
	struct usb_device	*udev;

	/* chip has been reset, VBUS power is off */

	udev = usb_alloc_dev(NULL, &sl811->hcd.self, 0);
	if (!udev)
		return -ENOMEM;

	udev->speed = USB_SPEED_FULL;
	hcd->state = USB_STATE_RUNNING;

	if (sl811->board)
		sl811->hcd.can_wakeup = sl811->board->can_wakeup;

	if (hcd_register_root(udev, &sl811->hcd) != 0) {
		usb_put_dev(udev);
		sl811h_stop(hcd);
		return -ENODEV;
	}

	if (sl811->board && sl811->board->power)
		hub_set_power_budget(udev, sl811->board->power * 2);

	return 0;
}

/*-------------------------------------------------------------------------*/

static struct hc_driver sl811h_hc_driver = {
	.description =		hcd_name,

	/*
	 * generic hardware linkage
	 */
	.flags =		HCD_USB11,

	/*
	 * managing i/o requests and associated device resources
	 */
	.urb_enqueue =		sl811h_urb_enqueue,
	.urb_dequeue =		sl811h_urb_dequeue,
	.endpoint_disable =	sl811h_endpoint_disable,

	/*
	 * periodic schedule support
	 */
	.get_frame_number =	sl811h_get_frame,

	/*
	 * root hub support
	 */
	.hub_status_data =	sl811h_hub_status_data,
	.hub_control =		sl811h_hub_control,
	.hub_suspend =		sl811h_hub_suspend,
	.hub_resume =		sl811h_hub_resume,
};

/*-------------------------------------------------------------------------*/

static int __init_or_module
sl811h_remove(struct device *dev)
{
	struct sl811		*sl811 = dev_get_drvdata(dev);
	struct platform_device	*pdev;
	struct resource		*res;

	pdev = container_of(dev, struct platform_device, dev);

	if (HCD_IS_RUNNING(sl811->hcd.state))
		sl811->hcd.state = USB_STATE_QUIESCING;

	usb_disconnect(&sl811->hcd.self.root_hub);
	remove_debug_file(sl811);
	sl811h_stop(&sl811->hcd);

	if (!list_empty(&sl811->hcd.self.bus_list))
		usb_deregister_bus(&sl811->hcd.self);

	if (sl811->hcd.irq >= 0)
		free_irq(sl811->hcd.irq, sl811);

	if (sl811->data_reg)
		iounmap(sl811->data_reg);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	release_mem_region(res->start, 1);

	if (sl811->addr_reg) 
		iounmap(sl811->addr_reg);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, 1);

	kfree(sl811);
	return 0;
}

#define resource_len(r) (((r)->end - (r)->start) + 1)

static int __init
sl811h_probe(struct device *dev)
{
	struct sl811		*sl811;
	struct platform_device	*pdev;
	struct resource		*addr, *data;
	int			irq;
	int			status;
	u8			tmp;
	unsigned long		flags;

	/* basic sanity checks first.  board-specific init logic should
	 * have initialized these three resources and probably board
	 * specific platform_data.  we don't probe for IRQs, and do only
	 * minimal sanity checking.
	 */
	pdev = container_of(dev, struct platform_device, dev);
	if (pdev->num_resources < 3)
		return -ENODEV;

	addr = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	data = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	irq = platform_get_irq(pdev, 0);
	if (!addr || !data || irq < 0)
		return -ENODEV;

	/* refuse to confuse usbcore */
	if (dev->dma_mask) {
		DBG("no we won't dma\n");
		return -EINVAL;
	}

	if (!request_mem_region(addr->start, 1, hcd_name))
		return -EBUSY;
	if (!request_mem_region(data->start, 1, hcd_name)) {
		release_mem_region(addr->start, 1);
		return -EBUSY;
	}

	/* allocate and initialize hcd */
	sl811 = kcalloc(1, sizeof *sl811, GFP_KERNEL);
	if (!sl811)
		return 0;
	dev_set_drvdata(dev, sl811);

	usb_bus_init(&sl811->hcd.self);
	sl811->hcd.self.controller = dev;
	sl811->hcd.self.bus_name = dev->bus_id;
	sl811->hcd.self.op = &usb_hcd_operations;
	sl811->hcd.self.hcpriv = sl811;

	// NOTE: 2.6.11 starts to change the hcd glue layer some more,
	// eventually letting us eliminate struct sl811h_req and a
	// lot of the boilerplate code here 

	INIT_LIST_HEAD(&sl811->hcd.dev_list);
	sl811->hcd.self.release = &usb_hcd_release;

	sl811->hcd.description = sl811h_hc_driver.description;
	init_timer(&sl811->hcd.rh_timer);
	sl811->hcd.driver = &sl811h_hc_driver;
	sl811->hcd.irq = -1;
	sl811->hcd.state = USB_STATE_HALT;

	spin_lock_init(&sl811->lock);
	INIT_LIST_HEAD(&sl811->async);
	sl811->board = dev->platform_data;
	init_timer(&sl811->timer);
	sl811->timer.function = sl811h_timer;
	sl811->timer.data = (unsigned long) sl811;

	sl811->addr_reg = ioremap(addr->start, resource_len(addr));
	if (sl811->addr_reg == NULL) {
		status = -ENOMEM;
		goto fail;
	}
	sl811->data_reg = ioremap(data->start, resource_len(addr));
	if (sl811->data_reg == NULL) {
		status = -ENOMEM;
		goto fail;
	}

	spin_lock_irqsave(&sl811->lock, flags);
	port_power(sl811, 0);
	spin_unlock_irqrestore(&sl811->lock, flags);
	msleep(200);

	tmp = sl811_read(sl811, SL11H_HWREVREG);
	switch (tmp >> 4) {
	case 1:
		sl811->hcd.product_desc = "SL811HS v1.2";
		break;
	case 2:
		sl811->hcd.product_desc = "SL811HS v1.5";
		break;
	default:
		/* reject case 0, SL11S is less functional */
		DBG("chiprev %02x\n", tmp);
		status = -ENXIO;
		goto fail;
	}

	/* sl811s would need a different handler for this irq */
#ifdef	CONFIG_ARM
	/* Cypress docs say the IRQ is IRQT_HIGH ... */
	set_irq_type(irq, IRQT_RISING);
#endif
	status = request_irq(irq, sl811h_irq, SA_INTERRUPT, hcd_name, sl811);
	if (status < 0)
		goto fail;
	sl811->hcd.irq = irq;

	INFO("%s, irq %d\n", sl811->hcd.product_desc, irq);

	status = usb_register_bus(&sl811->hcd.self);
	if (status < 0)
		goto fail;
	status = sl811h_start(&sl811->hcd);
	if (status == 0) {
		create_debug_file(sl811);
		return 0;
	}
fail:
	sl811h_remove(dev);
	DBG("init error, %d\n", status);
	return status;
}

#ifdef	CONFIG_PM

/* for this device there's no useful distinction between the controller
 * and its root hub, except that the root hub only gets direct PM calls 
 * when CONFIG_USB_SUSPEND is enabled.
 */

static int
sl811h_suspend(struct device *dev, u32 state, u32 phase)
{
	struct sl811	*sl811 = dev_get_drvdata(dev);
	int		retval = 0;

	if (phase != SUSPEND_POWER_DOWN)
		return retval;

	if (state <= PM_SUSPEND_MEM)
		retval = sl811h_hub_suspend(&sl811->hcd);
	else
		port_power(sl811, 0);
	if (retval == 0)
		dev->power.power_state = state;
	return retval;
}

static int
sl811h_resume(struct device *dev, u32 phase)
{
	struct sl811	*sl811 = dev_get_drvdata(dev);

	if (phase != RESUME_POWER_ON)
		return 0;

	/* with no "check to see if VBUS is still powered" board hook,
	 * let's assume it'd only be powered to enable remote wakeup.
	 */
	if (dev->power.power_state > PM_SUSPEND_MEM
			|| !sl811->hcd.can_wakeup) {
		sl811->port1 = 0;
		port_power(sl811, 1);
		return 0;
	}

	dev->power.power_state = PM_SUSPEND_ON;
	return sl811h_hub_resume(&sl811->hcd);
}

#else

#define	sl811h_suspend	NULL
#define	sl811h_resume	NULL

#endif


static struct device_driver sl811h_driver = {
	.name =		(char *) hcd_name,
	.bus =		&platform_bus_type,

	.probe =	sl811h_probe,
	.remove =	sl811h_remove,

	.suspend =	sl811h_suspend,
	.resume =	sl811h_resume,
};

/*-------------------------------------------------------------------------*/
 
static int __init sl811h_init(void) 
{
	if (usb_disabled())
		return -ENODEV;

	INFO("driver %s, %s\n", hcd_name, DRIVER_VERSION);
	return driver_register(&sl811h_driver);
}
module_init(sl811h_init);

static void __exit sl811h_cleanup(void) 
{	
	driver_unregister(&sl811h_driver);
}
module_exit(sl811h_cleanup);
