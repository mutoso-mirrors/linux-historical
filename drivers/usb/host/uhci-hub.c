/*
 * Universal Host Controller Interface driver for USB.
 *
 * Maintainer: Alan Stern <stern@rowland.harvard.edu>
 *
 * (C) Copyright 1999 Linus Torvalds
 * (C) Copyright 1999-2002 Johannes Erdfelt, johannes@erdfelt.com
 * (C) Copyright 1999 Randy Dunlap
 * (C) Copyright 1999 Georg Acher, acher@in.tum.de
 * (C) Copyright 1999 Deti Fliegl, deti@fliegl.de
 * (C) Copyright 1999 Thomas Sailer, sailer@ife.ee.ethz.ch
 * (C) Copyright 2004 Alan Stern, stern@rowland.harvard.edu
 */

static __u8 root_hub_hub_des[] =
{
	0x09,			/*  __u8  bLength; */
	0x29,			/*  __u8  bDescriptorType; Hub-descriptor */
	0x02,			/*  __u8  bNbrPorts; */
	0x0a,			/* __u16  wHubCharacteristics; */
	0x00,			/*   (per-port OC, no power switching) */
	0x01,			/*  __u8  bPwrOn2pwrGood; 2ms */
	0x00,			/*  __u8  bHubContrCurrent; 0 mA */
	0x00,			/*  __u8  DeviceRemovable; *** 7 Ports max *** */
	0xff			/*  __u8  PortPwrCtrlMask; *** 7 ports max *** */
};

#define	UHCI_RH_MAXCHILD	7

/* must write as zeroes */
#define WZ_BITS		(USBPORTSC_RES2 | USBPORTSC_RES3 | USBPORTSC_RES4)

/* status change bits:  nonzero writes will clear */
#define RWC_BITS	(USBPORTSC_OCC | USBPORTSC_PEC | USBPORTSC_CSC)

static int uhci_hub_status_data(struct usb_hcd *hcd, char *buf)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	int port;

	*buf = 0;
	for (port = 0; port < uhci->rh_numports; ++port) {
		if ((inw(uhci->io_addr + USBPORTSC1 + port * 2) & RWC_BITS) ||
				test_bit(port, &uhci->port_c_suspend))
			*buf |= (1 << (port + 1));
	}
	return !!*buf;
}

#define OK(x)			len = (x); break

#define CLR_RH_PORTSTAT(x) \
	status = inw(port_addr); \
	status &= ~(RWC_BITS|WZ_BITS); \
	status &= ~(x); \
	status |= RWC_BITS & (x); \
	outw(status, port_addr)

#define SET_RH_PORTSTAT(x) \
	status = inw(port_addr); \
	status |= (x); \
	status &= ~(RWC_BITS|WZ_BITS); \
	outw(status, port_addr)

/* UHCI controllers don't automatically stop resume signalling after 20 msec,
 * so we have to poll and check timeouts in order to take care of it.
 * FIXME:  Synchronize access to these fields by a spinlock.
 */
static void uhci_finish_suspend(struct uhci_hcd *uhci, int port,
		unsigned int port_addr)
{
	int status;

	if (test_bit(port, &uhci->suspended_ports)) {
		CLR_RH_PORTSTAT(USBPORTSC_SUSP | USBPORTSC_RD);
		clear_bit(port, &uhci->suspended_ports);
		clear_bit(port, &uhci->resuming_ports);
		set_bit(port, &uhci->port_c_suspend);
	}
}

static void uhci_check_resume(struct uhci_hcd *uhci)
{
	unsigned int port;
	unsigned int port_addr;

	for (port = 0; port < uhci->rh_numports; ++port) {
		port_addr = uhci->io_addr + USBPORTSC1 + 2 * port;
		if (unlikely(inw(port_addr) & USBPORTSC_RD)) {
			if (!test_bit(port, &uhci->resuming_ports)) {

				/* Port received a wakeup request */
				set_bit(port, &uhci->resuming_ports);
				uhci->resume_timeout = jiffies +
						msecs_to_jiffies(20);
			} else if (time_after_eq(jiffies,
						uhci->resume_timeout)) {
				uhci_finish_suspend(uhci, port, port_addr);
			}
		}
	}
}

/* size of returned buffer is part of USB spec */
static int uhci_hub_control(struct usb_hcd *hcd, u16 typeReq, u16 wValue,
			u16 wIndex, char *buf, u16 wLength)
{
	struct uhci_hcd *uhci = hcd_to_uhci(hcd);
	int status, lstatus, retval = 0, len = 0;
	unsigned int port = wIndex - 1;
	unsigned long port_addr = uhci->io_addr + USBPORTSC1 + 2 * port;
	u16 wPortChange, wPortStatus;

	switch (typeReq) {

	case GetHubStatus:
		*(u32 *) buf = cpu_to_le32(0);
		OK(4);		/* hub power */
	case GetPortStatus:
		if (port >= uhci->rh_numports)
			goto err;

		if (uhci->resuming_ports)
			uhci_check_resume(uhci);

		status = inw(port_addr);

		/* Intel controllers report the OverCurrent bit active on.
		 * VIA controllers report it active off, so we'll adjust the
		 * bit value.  (It's not standardized in the UHCI spec.)
		 */
		if (to_pci_dev(hcd->self.controller)->vendor ==
				PCI_VENDOR_ID_VIA)
			status ^= USBPORTSC_OC;

		/* UHCI doesn't support C_RESET (always false) */
		wPortChange = lstatus = 0;
		if (status & USBPORTSC_CSC)
			wPortChange |= USB_PORT_STAT_C_CONNECTION;
		if (status & USBPORTSC_PEC)
			wPortChange |= USB_PORT_STAT_C_ENABLE;
		if (status & USBPORTSC_OCC)
			wPortChange |= USB_PORT_STAT_C_OVERCURRENT;

		if (test_bit(port, &uhci->port_c_suspend)) {
			wPortChange |= USB_PORT_STAT_C_SUSPEND;
			lstatus |= 1;
		}
		if (test_bit(port, &uhci->suspended_ports))
			lstatus |= 2;
		if (test_bit(port, &uhci->resuming_ports))
			lstatus |= 4;

		/* UHCI has no power switching (always on) */
		wPortStatus = USB_PORT_STAT_POWER;
		if (status & USBPORTSC_CCS)
			wPortStatus |= USB_PORT_STAT_CONNECTION;
		if (status & USBPORTSC_PE) {
			wPortStatus |= USB_PORT_STAT_ENABLE;
			if (status & (USBPORTSC_SUSP | USBPORTSC_RD))
				wPortStatus |= USB_PORT_STAT_SUSPEND;
		}
		if (status & USBPORTSC_OC)
			wPortStatus |= USB_PORT_STAT_OVERCURRENT;
		if (status & USBPORTSC_PR)
			wPortStatus |= USB_PORT_STAT_RESET;
		if (status & USBPORTSC_LSDA)
			wPortStatus |= USB_PORT_STAT_LOW_SPEED;

		if (wPortChange)
			dev_dbg(uhci_dev(uhci), "port %d portsc %04x,%02x\n",
					wIndex, status, lstatus);

		*(u16 *) buf = cpu_to_le16(wPortStatus);
		*(u16 *) (buf + 2) = cpu_to_le16(wPortChange);
		OK(4);
	case SetHubFeature:		/* We don't implement these */
	case ClearHubFeature:
		switch (wValue) {
		case C_HUB_OVER_CURRENT:
		case C_HUB_LOCAL_POWER:
			OK(0);
		default:
			goto err;
		}
		break;
	case SetPortFeature:
		if (port >= uhci->rh_numports)
			goto err;

		switch (wValue) {
		case USB_PORT_FEAT_SUSPEND:
			set_bit(port, &uhci->suspended_ports);
			SET_RH_PORTSTAT(USBPORTSC_SUSP);
			OK(0);
		case USB_PORT_FEAT_RESET:
			SET_RH_PORTSTAT(USBPORTSC_PR);
			mdelay(50);	/* USB v1.1 7.1.7.3 */
			CLR_RH_PORTSTAT(USBPORTSC_PR);
			udelay(10);

			/* Reset terminates Resume signalling */
			uhci_finish_suspend(uhci, port, port_addr);
			SET_RH_PORTSTAT(USBPORTSC_PE);
			mdelay(10);
			CLR_RH_PORTSTAT(USBPORTSC_PEC|USBPORTSC_CSC);
			OK(0);
		case USB_PORT_FEAT_POWER:
			/* UHCI has no power switching */
			OK(0);
		default:
			goto err;
		}
		break;
	case ClearPortFeature:
		if (port >= uhci->rh_numports)
			goto err;

		switch (wValue) {
		case USB_PORT_FEAT_ENABLE:
			CLR_RH_PORTSTAT(USBPORTSC_PE);

			/* Disable terminates Resume signalling */
			uhci_finish_suspend(uhci, port, port_addr);
			OK(0);
		case USB_PORT_FEAT_C_ENABLE:
			CLR_RH_PORTSTAT(USBPORTSC_PEC);
			OK(0);
		case USB_PORT_FEAT_SUSPEND:
			if (test_bit(port, &uhci->suspended_ports) &&
					!test_and_set_bit(port,
						&uhci->resuming_ports)) {
				uhci->resume_timeout = jiffies +
						msecs_to_jiffies(20);
				SET_RH_PORTSTAT(USBPORTSC_RD);

				/* The controller won't allow RD to be set
				 * if the port is disabled.  When this happens
				 * just skip the Resume signalling.
				 */
				if (!(inw(port_addr) & USBPORTSC_RD))
					uhci_finish_suspend(uhci, port,
							port_addr);
			}
			OK(0);
		case USB_PORT_FEAT_C_SUSPEND:
			clear_bit(port, &uhci->port_c_suspend);
			OK(0);
		case USB_PORT_FEAT_POWER:
			/* UHCI has no power switching */
			goto err;
		case USB_PORT_FEAT_C_CONNECTION:
			CLR_RH_PORTSTAT(USBPORTSC_CSC);
			OK(0);
		case USB_PORT_FEAT_C_OVER_CURRENT:
			CLR_RH_PORTSTAT(USBPORTSC_OCC);
			OK(0);
		case USB_PORT_FEAT_C_RESET:
			/* this driver won't report these */
			OK(0);
		default:
			goto err;
		}
		break;
	case GetHubDescriptor:
		len = min_t(unsigned int, sizeof(root_hub_hub_des), wLength);
		memcpy(buf, root_hub_hub_des, len);
		if (len > 2)
			buf[2] = uhci->rh_numports;
		OK(len);
	default:
err:
		retval = -EPIPE;
	}

	return retval;
}
