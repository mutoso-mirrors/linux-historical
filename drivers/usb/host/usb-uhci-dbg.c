/*  
    UHCI HCD (Host Controller Driver) for USB, debugging calls
   
    (c) 1999-2002 
    Georg Acher      +    Deti Fliegl    +    Thomas Sailer
    georg@acher.org      deti@fliegl.de   sailer@ife.ee.ethz.ch
  
    $Id: usb-uhci-dbg.c,v 1.2 2002/05/21 21:40:16 acher Exp $
*/

#ifdef DEBUG
static void __attribute__((__unused__)) uhci_show_qh (puhci_desc_t qh)
{
	if (qh->type != QH_TYPE) {
		dbg("qh has not QH_TYPE");
		return;
	}
	dbg("QH @ %p/%08llX:", qh, (unsigned long long)qh->dma_addr);

	if (qh->hw.qh.head & UHCI_PTR_TERM)
		dbg("    Head Terminate");
	else 
		dbg("    Head: %s @ %08X",
		    (qh->hw.qh.head & UHCI_PTR_QH?"QH":"TD"),
		    qh->hw.qh.head & ~UHCI_PTR_BITS);

	if (qh->hw.qh.element & UHCI_PTR_TERM)
		dbg("    Element Terminate");
	else 
		dbg("    Element: %s @ %08X",
		    (qh->hw.qh.element & UHCI_PTR_QH?"QH":"TD"),
		    qh->hw.qh.element & ~UHCI_PTR_BITS);
}
#endif

#if 0
static void uhci_show_td (puhci_desc_t td)
{
	char *spid;
	
	switch (td->hw.td.info & 0xff) {
	case USB_PID_SETUP:
		spid = "SETUP";
		break;
	case USB_PID_OUT:
		spid = " OUT ";
		break;
	case USB_PID_IN:
		spid = " IN  ";
		break;
	default:
		spid = "  ?  ";
		break;
	}

	warn("  TD @ %p/%08X, MaxLen=%02x DT%d EP=%x Dev=%x PID=(%s) buf=%08x",
	     td, td->dma_addr,
	     td->hw.td.info >> 21,
	     ((td->hw.td.info >> 19) & 1),
	     (td->hw.td.info >> 15) & 15,
	     (td->hw.td.info >> 8) & 127,
	     spid,
	     td->hw.td.buffer);

	warn("    Len=%02x e%d %s%s%s%s%s%s%s%s%s%s",
	     td->hw.td.status & 0x7ff,
	     ((td->hw.td.status >> 27) & 3),
	     (td->hw.td.status & TD_CTRL_SPD) ? "SPD " : "",
	     (td->hw.td.status & TD_CTRL_LS) ? "LS " : "",
	     (td->hw.td.status & TD_CTRL_IOC) ? "IOC " : "",
	     (td->hw.td.status & TD_CTRL_ACTIVE) ? "Active " : "",
	     (td->hw.td.status & TD_CTRL_STALLED) ? "Stalled " : "",
	     (td->hw.td.status & TD_CTRL_DBUFERR) ? "DataBufErr " : "",
	     (td->hw.td.status & TD_CTRL_BABBLE) ? "Babble " : "",
	     (td->hw.td.status & TD_CTRL_NAK) ? "NAK " : "",
	     (td->hw.td.status & TD_CTRL_CRCTIMEO) ? "CRC/Timeo " : "",
	     (td->hw.td.status & TD_CTRL_BITSTUFF) ? "BitStuff " : ""
		);

	if (td->hw.td.link & UHCI_PTR_TERM)
		warn("   TD Link Terminate");
	else 
		warn("    Link points to %s @ %08x, %s",
		     (td->hw.td.link & UHCI_PTR_QH?"QH":"TD"),
		     td->hw.td.link & ~UHCI_PTR_BITS,
		     (td->hw.td.link & UHCI_PTR_DEPTH ? "Depth first" : "Breadth first"));
}
#endif

#ifdef DEBUG
static void __attribute__((__unused__)) uhci_show_sc (int port, unsigned short status)
{
	dbg("  stat%d     =     %04x   %s%s%s%s%s%s%s%s",
	     port,
	     status,
	     (status & USBPORTSC_SUSP) ? "PortSuspend " : "",
	     (status & USBPORTSC_PR) ? "PortReset " : "",
	     (status & USBPORTSC_LSDA) ? "LowSpeed " : "",
	     (status & USBPORTSC_RD) ? "ResumeDetect " : "",
	     (status & USBPORTSC_PEC) ? "EnableChange " : "",
	     (status & USBPORTSC_PE) ? "PortEnabled " : "",
	     (status & USBPORTSC_CSC) ? "ConnectChange " : "",
	     (status & USBPORTSC_CCS) ? "PortConnected " : "");
}

void uhci_show_status (struct uhci_hcd *uhci)
{
	unsigned long io_addr = (unsigned long)uhci->hcd.regs;
	unsigned short usbcmd, usbstat, usbint, usbfrnum;
	unsigned int flbaseadd;
	unsigned char sof;
	unsigned short portsc1, portsc2;

	usbcmd = inw (io_addr + 0);
	usbstat = inw (io_addr + 2);
	usbint = inw (io_addr + 4);
	usbfrnum = inw (io_addr + 6);
	flbaseadd = inl (io_addr + 8);
	sof = inb (io_addr + 12);
	portsc1 = inw (io_addr + 16);
	portsc2 = inw (io_addr + 18);

	dbg("  usbcmd    =     %04x   %s%s%s%s%s%s%s%s",
	     usbcmd,
	     (usbcmd & USBCMD_MAXP) ? "Maxp64 " : "Maxp32 ",
	     (usbcmd & USBCMD_CF) ? "CF " : "",
	     (usbcmd & USBCMD_SWDBG) ? "SWDBG " : "",
	     (usbcmd & USBCMD_FGR) ? "FGR " : "",
	     (usbcmd & USBCMD_EGSM) ? "EGSM " : "",
	     (usbcmd & USBCMD_GRESET) ? "GRESET " : "",
	     (usbcmd & USBCMD_HCRESET) ? "HCRESET " : "",
	     (usbcmd & USBCMD_RS) ? "RS " : "");

	dbg("  usbstat   =     %04x   %s%s%s%s%s%s",
	     usbstat,
	     (usbstat & USBSTS_HCH) ? "HCHalted " : "",
	     (usbstat & USBSTS_HCPE) ? "HostControllerProcessError " : "",
	     (usbstat & USBSTS_HSE) ? "HostSystemError " : "",
	     (usbstat & USBSTS_RD) ? "ResumeDetect " : "",
	     (usbstat & USBSTS_ERROR) ? "USBError " : "",
	     (usbstat & USBSTS_USBINT) ? "USBINT " : "");

	dbg("  usbint    =     %04x", usbint);
	dbg("  usbfrnum  =   (%d)%03x", (usbfrnum >> 10) & 1,
	     0xfff & (4 * (unsigned int) usbfrnum));
	dbg("  flbaseadd = %08x", flbaseadd);
	dbg("  sof       =       %02x", sof);
	uhci_show_sc (1, portsc1);
	uhci_show_sc (2, portsc2);
}
#endif
