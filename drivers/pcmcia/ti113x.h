/*
 * ti113x.h 1.16 1999/10/25 20:03:34
 *
 * The contents of this file are subject to the Mozilla Public License
 * Version 1.1 (the "License"); you may not use this file except in
 * compliance with the License. You may obtain a copy of the License
 * at http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied. See
 * the License for the specific language governing rights and
 * limitations under the License. 
 *
 * The initial developer of the original code is David A. Hinds
 * <dahinds@users.sourceforge.net>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#ifndef _LINUX_TI113X_H
#define _LINUX_TI113X_H

#include <linux/config.h>

/* Register definitions for TI 113X PCI-to-CardBus bridges */

/* System Control Register */
#define TI113X_SYSTEM_CONTROL		0x0080	/* 32 bit */
#define  TI113X_SCR_SMIROUTE		0x04000000
#define  TI113X_SCR_SMISTATUS		0x02000000
#define  TI113X_SCR_SMIENB		0x01000000
#define  TI113X_SCR_VCCPROT		0x00200000
#define  TI113X_SCR_REDUCEZV		0x00100000
#define  TI113X_SCR_CDREQEN		0x00080000
#define  TI113X_SCR_CDMACHAN		0x00070000
#define  TI113X_SCR_SOCACTIVE		0x00002000
#define  TI113X_SCR_PWRSTREAM		0x00000800
#define  TI113X_SCR_DELAYUP		0x00000400
#define  TI113X_SCR_DELAYDOWN		0x00000200
#define  TI113X_SCR_INTERROGATE		0x00000100
#define  TI113X_SCR_CLKRUN_SEL		0x00000080
#define  TI113X_SCR_PWRSAVINGS		0x00000040
#define  TI113X_SCR_SUBSYSRW		0x00000020
#define  TI113X_SCR_CB_DPAR		0x00000010
#define  TI113X_SCR_CDMA_EN		0x00000008
#define  TI113X_SCR_ASYNC_IRQ		0x00000004
#define  TI113X_SCR_KEEPCLK		0x00000002
#define  TI113X_SCR_CLKRUN_ENA		0x00000001  

#define  TI122X_SCR_SER_STEP		0xc0000000
#define  TI122X_SCR_INTRTIE		0x20000000
#define  TI122X_SCR_CBRSVD		0x00400000
#define  TI122X_SCR_MRBURSTDN		0x00008000
#define  TI122X_SCR_MRBURSTUP		0x00004000
#define  TI122X_SCR_RIMUX		0x00000001

/* Multimedia Control Register */
#define TI1250_MULTIMEDIA_CTL		0x0084	/* 8 bit */
#define  TI1250_MMC_ZVOUTEN		0x80
#define  TI1250_MMC_PORTSEL		0x40
#define  TI1250_MMC_ZVEN1		0x02
#define  TI1250_MMC_ZVEN0		0x01

#define TI1250_GENERAL_STATUS		0x0085	/* 8 bit */
#define TI1250_GPIO0_CONTROL		0x0088	/* 8 bit */
#define TI1250_GPIO1_CONTROL		0x0089	/* 8 bit */
#define TI1250_GPIO2_CONTROL		0x008a	/* 8 bit */
#define TI1250_GPIO3_CONTROL		0x008b	/* 8 bit */
#define TI1250_GPIO_MODE_MASK		0xc0

/* IRQMUX/MFUNC Register */
#define TI122X_MFUNC			0x008c	/* 32 bit */
#define TI122X_MFUNC0_MASK		0x0000000f
#define TI122X_MFUNC1_MASK		0x000000f0
#define TI122X_MFUNC2_MASK		0x00000f00
#define TI122X_MFUNC3_MASK		0x0000f000
#define TI122X_MFUNC4_MASK		0x000f0000
#define TI122X_MFUNC5_MASK		0x00f00000
#define TI122X_MFUNC6_MASK		0x0f000000

#define TI122X_MFUNC0_INTA		0x00000002
#define TI125X_MFUNC0_INTB		0x00000001
#define TI122X_MFUNC1_INTB		0x00000020
#define TI122X_MFUNC3_IRQSER		0x00001000


/* Retry Status Register */
#define TI113X_RETRY_STATUS		0x0090	/* 8 bit */
#define  TI113X_RSR_PCIRETRY		0x80
#define  TI113X_RSR_CBRETRY		0x40
#define  TI113X_RSR_TEXP_CBB		0x20
#define  TI113X_RSR_MEXP_CBB		0x10
#define  TI113X_RSR_TEXP_CBA		0x08
#define  TI113X_RSR_MEXP_CBA		0x04
#define  TI113X_RSR_TEXP_PCI		0x02
#define  TI113X_RSR_MEXP_PCI		0x01

/* Card Control Register */
#define TI113X_CARD_CONTROL		0x0091	/* 8 bit */
#define  TI113X_CCR_RIENB		0x80
#define  TI113X_CCR_ZVENABLE		0x40
#define  TI113X_CCR_PCI_IRQ_ENA		0x20
#define  TI113X_CCR_PCI_IREQ		0x10
#define  TI113X_CCR_PCI_CSC		0x08
#define  TI113X_CCR_SPKROUTEN		0x02
#define  TI113X_CCR_IFG			0x01

#define  TI1220_CCR_PORT_SEL		0x20
#define  TI122X_CCR_AUD2MUX		0x04

/* Device Control Register */
#define TI113X_DEVICE_CONTROL		0x0092	/* 8 bit */
#define  TI113X_DCR_5V_FORCE		0x40
#define  TI113X_DCR_3V_FORCE		0x20
#define  TI113X_DCR_IMODE_MASK		0x06
#define  TI113X_DCR_IMODE_ISA		0x02
#define  TI113X_DCR_IMODE_SERIAL	0x04

#define  TI12XX_DCR_IMODE_PCI_ONLY	0x00
#define  TI12XX_DCR_IMODE_ALL_SERIAL	0x06

/* Buffer Control Register */
#define TI113X_BUFFER_CONTROL		0x0093	/* 8 bit */
#define  TI113X_BCR_CB_READ_DEPTH	0x08
#define  TI113X_BCR_CB_WRITE_DEPTH	0x04
#define  TI113X_BCR_PCI_READ_DEPTH	0x02
#define  TI113X_BCR_PCI_WRITE_DEPTH	0x01

/* Diagnostic Register */
#define TI1250_DIAGNOSTIC		0x0093	/* 8 bit */
#define  TI1250_DIAG_TRUE_VALUE		0x80
#define  TI1250_DIAG_PCI_IREQ		0x40
#define  TI1250_DIAG_PCI_CSC		0x20
#define  TI1250_DIAG_ASYNC_CSC		0x01

/* DMA Registers */
#define TI113X_DMA_0			0x0094	/* 32 bit */
#define TI113X_DMA_1			0x0098	/* 32 bit */

/* ExCA IO offset registers */
#define TI113X_IO_OFFSET(map)		(0x36+((map)<<1))

/* EnE test register */
#define ENE_TEST_C9			0xc9	/* 8bit */
#define ENE_TEST_C9_TLTENABLE		0x02

#ifdef CONFIG_CARDBUS

/*
 * Texas Instruments CardBus controller overrides.
 */
#define ti_sysctl(socket)	((socket)->private[0])
#define ti_cardctl(socket)	((socket)->private[1])
#define ti_devctl(socket)	((socket)->private[2])
#define ti_diag(socket)		((socket)->private[3])
#define ti_mfunc(socket)	((socket)->private[4])
#define ene_test_c9(socket)	((socket)->private[5])

/*
 * These are the TI specific power management handlers.
 */
static void ti_save_state(struct yenta_socket *socket)
{
	ti_sysctl(socket) = config_readl(socket, TI113X_SYSTEM_CONTROL);
	ti_mfunc(socket) = config_readl(socket, TI122X_MFUNC);
	ti_cardctl(socket) = config_readb(socket, TI113X_CARD_CONTROL);
	ti_devctl(socket) = config_readb(socket, TI113X_DEVICE_CONTROL);
	ti_diag(socket) = config_readb(socket, TI1250_DIAGNOSTIC);

	if (socket->dev->vendor == PCI_VENDOR_ID_ENE)
		ene_test_c9(socket) = config_readb(socket, ENE_TEST_C9);
}

static void ti_restore_state(struct yenta_socket *socket)
{
	config_writel(socket, TI113X_SYSTEM_CONTROL, ti_sysctl(socket));
	config_writel(socket, TI122X_MFUNC, ti_mfunc(socket));
	config_writeb(socket, TI113X_CARD_CONTROL, ti_cardctl(socket));
	config_writeb(socket, TI113X_DEVICE_CONTROL, ti_devctl(socket));
	config_writeb(socket, TI1250_DIAGNOSTIC, ti_diag(socket));

	if (socket->dev->vendor == PCI_VENDOR_ID_ENE)
		config_writeb(socket, ENE_TEST_C9, ene_test_c9(socket));
}

/*
 *	Zoom video control for TI122x/113x chips
 */

static void ti_zoom_video(struct pcmcia_socket *sock, int onoff)
{
	u8 reg;
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);

	/* If we don't have a Zoom Video switch this is harmless,
	   we just tristate the unused (ZV) lines */
	reg = config_readb(socket, TI113X_CARD_CONTROL);
	if (onoff)
		/* Zoom zoom, we will all go together, zoom zoom, zoom zoom */
		reg |= TI113X_CCR_ZVENABLE;
	else
		reg &= ~TI113X_CCR_ZVENABLE;
	config_writeb(socket, TI113X_CARD_CONTROL, reg);
}

/*
 *	The 145x series can also use this. They have an additional
 *	ZV autodetect mode we don't use but don't actually need.
 *	FIXME: manual says its in func0 and func1 but disagrees with
 *	itself about this - do we need to force func0, if so we need
 *	to know a lot more about socket pairings in pcmcia_socket than
 *	we do now.. uggh.
 */
 
static void ti1250_zoom_video(struct pcmcia_socket *sock, int onoff)
{	
	struct yenta_socket *socket = container_of(sock, struct yenta_socket, socket);
	int shift = 0;
	u8 reg;

	ti_zoom_video(sock, onoff);

	reg = config_readb(socket, TI1250_MULTIMEDIA_CTL);
	reg |= TI1250_MMC_ZVOUTEN;	/* ZV bus enable */

	if(PCI_FUNC(socket->dev->devfn)==1)
		shift = 1;
	
	if(onoff)
	{
		reg &= ~(1<<6); 	/* Clear select bit */
		reg |= shift<<6;	/* Favour our socket */
		reg |= 1<<shift;	/* Socket zoom video on */
	}
	else
	{
		reg &= ~(1<<6); 	/* Clear select bit */
		reg |= (1^shift)<<6;	/* Favour other socket */
		reg &= ~(1<<shift);	/* Socket zoon video off */
	}

	config_writeb(socket, TI1250_MULTIMEDIA_CTL, reg);
}

static void ti_set_zv(struct yenta_socket *socket)
{
	if(socket->dev->vendor == PCI_VENDOR_ID_TI)
	{
		switch(socket->dev->device)
		{
			/* There may be more .. */
			case PCI_DEVICE_ID_TI_1220:
			case PCI_DEVICE_ID_TI_1221:
			case PCI_DEVICE_ID_TI_1225:
			case PCI_DEVICE_ID_TI_4510:
				socket->socket.zoom_video = ti_zoom_video;
				break;	
			case PCI_DEVICE_ID_TI_1250:
			case PCI_DEVICE_ID_TI_1251A:
			case PCI_DEVICE_ID_TI_1251B:
			case PCI_DEVICE_ID_TI_1450:
				socket->socket.zoom_video = ti1250_zoom_video;
		}
	}
}


/*
 * Generic TI init - TI has an extension for the
 * INTCTL register that sets the PCI CSC interrupt.
 * Make sure we set it correctly at open and init
 * time
 * - override: disable the PCI CSC interrupt. This makes
 *   it possible to use the CSC interrupt to probe the
 *   ISA interrupts.
 * - init: set the interrupt to match our PCI state.
 *   This makes us correctly get PCI CSC interrupt
 *   events.
 */
static int ti_init(struct yenta_socket *socket)
{
	u8 new, reg = exca_readb(socket, I365_INTCTL);

	new = reg & ~I365_INTR_ENA;
	if (socket->cb_irq)
		new |= I365_INTR_ENA;
	if (new != reg)
		exca_writeb(socket, I365_INTCTL, new);
	return 0;
}

static int ti_override(struct yenta_socket *socket)
{
	u8 new, reg = exca_readb(socket, I365_INTCTL);

	new = reg & ~I365_INTR_ENA;
	if (new != reg)
		exca_writeb(socket, I365_INTCTL, new);

	ti_set_zv(socket);

	return 0;
}

static int ti113x_override(struct yenta_socket *socket)
{
	u8 cardctl;

	cardctl = config_readb(socket, TI113X_CARD_CONTROL);
	cardctl &= ~(TI113X_CCR_PCI_IRQ_ENA | TI113X_CCR_PCI_IREQ | TI113X_CCR_PCI_CSC);
	if (socket->cb_irq)
		cardctl |= TI113X_CCR_PCI_IRQ_ENA | TI113X_CCR_PCI_CSC | TI113X_CCR_PCI_IREQ;
	config_writeb(socket, TI113X_CARD_CONTROL, cardctl);

	return ti_override(socket);
}


/* irqrouting for func0, probes PCI interrupt and ISA interrupts */
static void ti12xx_irqroute_func0(struct yenta_socket *socket)
{
	u32 mfunc, mfunc_old, devctl;
	u8 gpio3, gpio3_old;
	int pci_irq_status;

	mfunc = mfunc_old = config_readl(socket, TI122X_MFUNC);
	devctl = config_readb(socket, TI113X_DEVICE_CONTROL);
	printk(KERN_INFO "Yenta TI: socket %s, mfunc 0x%08x, devctl 0x%02x\n",
	       pci_name(socket->dev), mfunc, devctl);

	/* make sure PCI interrupts are enabled before probing */
	ti_init(socket);

	/* test PCI interrupts first. only try fixing if return value is 0! */
	pci_irq_status = yenta_probe_cb_irq(socket);
	if (pci_irq_status)
		goto out;

	/*
	 * We're here which means PCI interrupts are _not_ delivered. try to
	 * find the right setting (all serial or parallel)
	 */
	printk(KERN_INFO "Yenta TI: socket %s probing PCI interrupt failed, trying to fix\n",
	       pci_name(socket->dev));

	/* for serial PCI make sure MFUNC3 is set to IRQSER */
	if ((devctl & TI113X_DCR_IMODE_MASK) == TI12XX_DCR_IMODE_ALL_SERIAL) {
		switch (socket->dev->device) {
		case PCI_DEVICE_ID_TI_1250:
		case PCI_DEVICE_ID_TI_1251A:
		case PCI_DEVICE_ID_TI_1251B:
		case PCI_DEVICE_ID_TI_1450:
		case PCI_DEVICE_ID_TI_1451A:
		case PCI_DEVICE_ID_TI_4450:
		case PCI_DEVICE_ID_TI_4451:
			/* these chips have no IRQSER setting in MFUNC3  */
			break;

		default:
			mfunc = (mfunc & ~TI122X_MFUNC3_MASK) | TI122X_MFUNC3_IRQSER;

			/* write down if changed, probe */
			if (mfunc != mfunc_old) {
				config_writel(socket, TI122X_MFUNC, mfunc);

				pci_irq_status = yenta_probe_cb_irq(socket);
				if (pci_irq_status == 1) {
					printk(KERN_INFO "Yenta TI: socket %s all-serial interrupts ok\n",
					       pci_name(socket->dev));
					mfunc_old = mfunc;
					goto out;
				}

				/* not working, back to old value */
				mfunc = mfunc_old;
				config_writel(socket, TI122X_MFUNC, mfunc);

				if (pci_irq_status == -1)
					goto out;
			}
		}

		/* serial PCI interrupts not working fall back to parallel */
		printk(KERN_INFO "Yenta TI: socket %s falling back to parallel PCI interrupts\n",
		       pci_name(socket->dev));
		devctl &= ~TI113X_DCR_IMODE_MASK;
		devctl |= TI113X_DCR_IMODE_SERIAL; /* serial ISA could be right */
		config_writeb(socket, TI113X_DEVICE_CONTROL, devctl);
	}

	/* parallel PCI interrupts: route INTA */
	switch (socket->dev->device) {
	case PCI_DEVICE_ID_TI_1250:
	case PCI_DEVICE_ID_TI_1251A:
	case PCI_DEVICE_ID_TI_1251B:
	case PCI_DEVICE_ID_TI_1450:
		/* make sure GPIO3 is set to INTA */
		gpio3 = gpio3_old = config_readb(socket, TI1250_GPIO3_CONTROL);
		gpio3 &= ~TI1250_GPIO_MODE_MASK;
		if (gpio3 != gpio3_old)
			config_writeb(socket, TI1250_GPIO3_CONTROL, gpio3);
		break;

	default:
		gpio3 = gpio3_old = 0;

		mfunc = (mfunc & ~TI122X_MFUNC0_MASK) | TI122X_MFUNC0_INTA;
		if (mfunc != mfunc_old)
			config_writel(socket, TI122X_MFUNC, mfunc);
	}

	/* time to probe again */
	pci_irq_status = yenta_probe_cb_irq(socket);
	if (pci_irq_status == 1) {
		mfunc_old = mfunc;
		printk(KERN_INFO "Yenta TI: socket %s parallel PCI interrupts ok\n",
		       pci_name(socket->dev));
	} else {
		/* not working, back to old value */
		mfunc = mfunc_old;
		config_writel(socket, TI122X_MFUNC, mfunc);
		if (gpio3 != gpio3_old)
			config_writeb(socket, TI1250_GPIO3_CONTROL, gpio3_old);
	}

out:
	if (pci_irq_status < 1) {
		socket->cb_irq = 0;
		printk(KERN_INFO "Yenta TI: socket %s no PCI interrupts. Fish. Please report.\n",
		       pci_name(socket->dev));
	}
}


/*
 * ties INTA and INTB together. also changes the devices irq to that of
 * the function 0 device. call from func1 only.
 * returns 1 if INTRTIE changed, 0 otherwise.
 */
static int ti12xx_tie_interrupts(struct yenta_socket *socket, int *old_irq)
{
	struct pci_dev *func0;
	u32 sysctl;

	sysctl = config_readl(socket, TI113X_SYSTEM_CONTROL);
	if (sysctl & TI122X_SCR_INTRTIE)
		return 0;

	/* find func0 device */
	func0 = pci_get_slot(socket->dev->bus, socket->dev->devfn & ~0x07);
	if (!func0)
		return 0;

	/* change the interrupt to match func0, tie 'em up */
	*old_irq = socket->cb_irq;
	socket->cb_irq = socket->dev->irq = func0->irq;
	sysctl |= TI122X_SCR_INTRTIE;
	config_writel(socket, TI113X_SYSTEM_CONTROL, sysctl);

	pci_dev_put(func0);

	return 1;
}

/* undo what ti12xx_tie_interrupts() did */
static void ti12xx_untie_interrupts(struct yenta_socket *socket, int old_irq)
{
	u32 sysctl = config_readl(socket, TI113X_SYSTEM_CONTROL);
	sysctl &= ~TI122X_SCR_INTRTIE;
	config_writel(socket, TI113X_SYSTEM_CONTROL, sysctl);

	socket->cb_irq = socket->dev->irq = old_irq;
}

/* 
 * irqrouting for func1, plays with INTB routing
 * only touches MFUNC for INTB routing. all other bits are taken
 * care of in func0 already.
 */
static void ti12xx_irqroute_func1(struct yenta_socket *socket)
{
	u32 mfunc, mfunc_old, devctl;
	int pci_irq_status;

	mfunc = mfunc_old = config_readl(socket, TI122X_MFUNC);
	devctl = config_readb(socket, TI113X_DEVICE_CONTROL);
	printk(KERN_INFO "Yenta TI: socket %s, mfunc 0x%08x, devctl 0x%02x\n",
	       pci_name(socket->dev), mfunc, devctl);

	/* make sure PCI interrupts are enabled before probing */
	ti_init(socket);

	/* test PCI interrupts first. only try fixing if return value is 0! */
	pci_irq_status = yenta_probe_cb_irq(socket);
	if (pci_irq_status)
		goto out;

	/*
	 * We're here which means PCI interrupts are _not_ delivered. try to
	 * find the right setting
	 */
	printk(KERN_INFO "Yenta TI: socket %s probing PCI interrupt failed, trying to fix\n",
	       pci_name(socket->dev));


	/* if all serial: set INTRTIE, probe again */
	if ((devctl & TI113X_DCR_IMODE_MASK) == TI12XX_DCR_IMODE_ALL_SERIAL) {
		int old_irq;

		if (ti12xx_tie_interrupts(socket, &old_irq)) {
			pci_irq_status = yenta_probe_cb_irq(socket);
			if (pci_irq_status == 1) {
				printk(KERN_INFO "Yenta TI: socket %s all-serial interrupts, tied ok\n",
				       pci_name(socket->dev));
				goto out;
			}

			ti12xx_untie_interrupts(socket, old_irq);
		}
	}
	/* parallel PCI: route INTB, probe again */
	else {
		int old_irq;

		switch (socket->dev->device) {
		case PCI_DEVICE_ID_TI_1250:
			/* the 1250 has one pin for IRQSER/INTB depending on devctl */
			break;

		case PCI_DEVICE_ID_TI_1251A:
		case PCI_DEVICE_ID_TI_1251B:
		case PCI_DEVICE_ID_TI_1450:
			/*
			 *  those have a pin for IRQSER/INTB plus INTB in MFUNC0
			 *  we alread probed the shared pin, now go for MFUNC0
			 */
			mfunc = (mfunc & ~TI122X_MFUNC0_MASK) | TI125X_MFUNC0_INTB;
			break;

		default:
			mfunc = (mfunc & ~TI122X_MFUNC1_MASK) | TI122X_MFUNC1_INTB;
			break;
		}

		/* write, probe */
		if (mfunc != mfunc_old) {
			config_writel(socket, TI122X_MFUNC, mfunc);

			pci_irq_status = yenta_probe_cb_irq(socket);
			if (pci_irq_status == 1) {
				printk(KERN_INFO "Yenta TI: socket %s parallel PCI interrupts ok\n",
				       pci_name(socket->dev));
				goto out;
			}

			mfunc = mfunc_old;
			config_writel(socket, TI122X_MFUNC, mfunc);

			if (pci_irq_status == -1)
				goto out;
		}
		
		/* still nothing: set INTRTIE */
		if (ti12xx_tie_interrupts(socket, &old_irq)) {
			pci_irq_status = yenta_probe_cb_irq(socket);
			if (pci_irq_status == 1) {
				printk(KERN_INFO "Yenta TI: socket %s parallel PCI interrupts, tied ok\n",
				       pci_name(socket->dev));
				goto out;
			}

			ti12xx_untie_interrupts(socket, old_irq);
		}
	}

out:
	if (pci_irq_status < 1) {
		socket->cb_irq = 0;
		printk(KERN_INFO "Yenta TI: socket %s no PCI interrupts. Fish. Please report.\n",
		       pci_name(socket->dev));
	}
}

static int ti12xx_override(struct yenta_socket *socket)
{
	u32 val, val_orig;

	/* make sure that memory burst is active */
	val_orig = val = config_readl(socket, TI113X_SYSTEM_CONTROL);
	if (disable_clkrun && PCI_FUNC(socket->dev->devfn) == 0) {
		printk(KERN_INFO "Yenta: Disabling CLKRUN feature\n");
		val |= TI113X_SCR_KEEPCLK;
	}
	if (!(val & TI122X_SCR_MRBURSTUP)) {
		printk(KERN_INFO "Yenta: Enabling burst memory read transactions\n");
		val |= TI122X_SCR_MRBURSTUP;
	}
	if (val_orig != val)
		config_writel(socket, TI113X_SYSTEM_CONTROL, val);

	/*
	 * for EnE bridges only: clear testbit TLTEnable. this makes the
	 * RME Hammerfall DSP sound card working.
	 */
	if (socket->dev->vendor == PCI_VENDOR_ID_ENE) {
		u8 test_c9 = config_readb(socket, ENE_TEST_C9);
		test_c9 &= ~ENE_TEST_C9_TLTENABLE;
		config_writeb(socket, ENE_TEST_C9, test_c9);
	}

	/*
	 * Yenta expects controllers to use CSCINT to route
	 * CSC interrupts to PCI rather than INTVAL.
	 */
	val = config_readb(socket, TI1250_DIAGNOSTIC);
	printk(KERN_INFO "Yenta: Using %s to route CSC interrupts to PCI\n",
		(val & TI1250_DIAG_PCI_CSC) ? "CSCINT" : "INTVAL");
	printk(KERN_INFO "Yenta: Routing CardBus interrupts to %s\n",
		(val & TI1250_DIAG_PCI_IREQ) ? "PCI" : "ISA");

	/* do irqrouting, depending on function */
	if (PCI_FUNC(socket->dev->devfn) == 0)
		ti12xx_irqroute_func0(socket);
	else
		ti12xx_irqroute_func1(socket);

	return ti_override(socket);
}


static int ti1250_override(struct yenta_socket *socket)
{
	u8 old, diag;

	old = config_readb(socket, TI1250_DIAGNOSTIC);
	diag = old & ~(TI1250_DIAG_PCI_CSC | TI1250_DIAG_PCI_IREQ);
	if (socket->cb_irq)
		diag |= TI1250_DIAG_PCI_CSC | TI1250_DIAG_PCI_IREQ;

	if (diag != old) {
		printk(KERN_INFO "Yenta: adjusting diagnostic: %02x -> %02x\n",
			old, diag);
		config_writeb(socket, TI1250_DIAGNOSTIC, diag);
	}

	return ti12xx_override(socket);
}

#endif /* CONFIG_CARDBUS */

#endif /* _LINUX_TI113X_H */

