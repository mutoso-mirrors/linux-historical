/* $XConsortium: nv_driver.c /main/3 1996/10/28 05:13:37 kaleb $ */
/*
 * Copyright 1996-1997  David J. McKay
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * DAVID J. MCKAY BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF
 * OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * GPL licensing note -- nVidia is allowing a liberal interpretation of
 * the documentation restriction above, to merely say that this nVidia's
 * copyright and disclaimer should be included with all code derived
 * from this source.  -- Jeff Garzik <jgarzik@pobox.com>, 01/Nov/99 
 */

/* Hacked together from mga driver and 3.3.4 NVIDIA driver by Jarno Paananen
   <jpaana@s2.org> */

/* $XFree86: xc/programs/Xserver/hw/xfree86/drivers/nv/nv_setup.c,v 1.18 2002/08/0
5 20:47:06 mvojkovi Exp $ */

#include <linux/delay.h>
#include <linux/pci_ids.h>
#include "nv_type.h"
#include "rivafb.h"
#include "nvreg.h"


#ifndef CONFIG_PCI		/* sanity check */
#error This driver requires PCI support.
#endif

#define PFX "rivafb: "

static inline unsigned char MISCin(struct riva_par *par)
{
	return (VGA_RD08(par->riva.PVIO, 0x3cc));
}

static Bool 
riva_is_connected(struct riva_par *par, Bool second)
{
	volatile U032 *PRAMDAC = par->riva.PRAMDAC0;
	U032 reg52C, reg608;
	Bool present;

	if(second) PRAMDAC += 0x800;

	reg52C = PRAMDAC[0x052C/4];
	reg608 = PRAMDAC[0x0608/4];

	PRAMDAC[0x0608/4] = reg608 & ~0x00010000;

	PRAMDAC[0x052C/4] = reg52C & 0x0000FEEE;
	mdelay(1); 
	PRAMDAC[0x052C/4] |= 1;

	par->riva.PRAMDAC0[0x0610/4] = 0x94050140;
	par->riva.PRAMDAC0[0x0608/4] |= 0x00001000;

	mdelay(1);

	present = (PRAMDAC[0x0608/4] & (1 << 28)) ? TRUE : FALSE;

	par->riva.PRAMDAC0[0x0608/4] &= 0x0000EFFF;

	PRAMDAC[0x052C/4] = reg52C;
	PRAMDAC[0x0608/4] = reg608;

	return present;
}

static void
riva_override_CRTC(struct riva_par *par)
{
	printk(KERN_INFO PFX
		"Detected CRTC controller %i being used\n",
		par->SecondCRTC ? 1 : 0);

	if(par->forceCRTC != -1) {
		printk(KERN_INFO PFX
			"Forcing usage of CRTC %i\n", par->forceCRTC);
		par->SecondCRTC = par->forceCRTC;
	}
}

static void
riva_is_second(struct riva_par *par)
{
	if (par->FlatPanel == 1) {
		switch(par->Chipset) {
		case NV_CHIP_GEFORCE4_440_GO:
		case NV_CHIP_GEFORCE4_440_GO_M64:
		case NV_CHIP_GEFORCE4_420_GO:
		case NV_CHIP_GEFORCE4_420_GO_M32:
		case NV_CHIP_QUADRO4_500_GOGL:
			par->SecondCRTC = TRUE;
			break;
		default:
			par->SecondCRTC = FALSE;
			break;
		}
	} else {
		if(riva_is_connected(par, 0)) {
			if(par->riva.PRAMDAC0[0x0000052C/4] & 0x100)
				par->SecondCRTC = TRUE;
			else
				par->SecondCRTC = FALSE;
		} else 
		if (riva_is_connected(par, 1)) {
			if(par->riva.PRAMDAC0[0x0000252C/4] & 0x100)
				par->SecondCRTC = TRUE;
			else
				par->SecondCRTC = FALSE;
		} else /* default */
			par->SecondCRTC = FALSE;
	}
	riva_override_CRTC(par);
}

void
riva_common_setup(struct riva_par *par)
{
	par->riva.EnableIRQ = 0;
	par->riva.PRAMDAC0 = (unsigned *)(par->ctrl_base + 0x00680000);
	par->riva.PFB = (unsigned *)(par->ctrl_base + 0x00100000);
	par->riva.PFIFO = (unsigned *)(par->ctrl_base + 0x00002000);
	par->riva.PGRAPH = (unsigned *)(par->ctrl_base + 0x00400000);
	par->riva.PEXTDEV = (unsigned *)(par->ctrl_base + 0x00101000);
	par->riva.PTIMER = (unsigned *)(par->ctrl_base + 0x00009000);
	par->riva.PMC = (unsigned *)(par->ctrl_base + 0x00000000);
	par->riva.FIFO = (unsigned *)(par->ctrl_base + 0x00800000);
	par->riva.PCIO0 = (U008 *)(par->ctrl_base + 0x00601000);
	par->riva.PDIO0 = (U008 *)(par->ctrl_base + 0x00681000);
	par->riva.PVIO = (U008 *)(par->ctrl_base + 0x000C0000);

	par->riva.IO = (MISCin(par) & 0x01) ? 0x3D0 : 0x3B0;
	
	if (par->FlatPanel == -1) {
		switch (par->Chipset) {
		case NV_CHIP_GEFORCE4_440_GO:
		case NV_CHIP_GEFORCE4_440_GO_M64:
		case NV_CHIP_GEFORCE4_420_GO:
		case NV_CHIP_GEFORCE4_420_GO_M32:
		case NV_CHIP_QUADRO4_500_GOGL:
		case NV_CHIP_GEFORCE2_GO:
			printk(KERN_INFO PFX 
				"On a laptop.  Assuming Digital Flat Panel\n");
			par->FlatPanel = 1;
			break;
		default:
			break;
		}
	}
	
	switch (par->Chipset & 0x0ff0) {
	case 0x0110:
		if (par->Chipset == NV_CHIP_GEFORCE2_GO)
			par->SecondCRTC = TRUE; 
#if defined(__powerpc__)
		if (par->FlatPanel == 1)
			par->SecondCRTC = TRUE;
#endif
		riva_override_CRTC(par);
		break;
	case 0x0170:
	case 0x0180:
	case 0x01F0:
	case 0x0250:
	case 0x0280:
		riva_is_second(par);
		break;
	default:
		break;
	}

	if (par->SecondCRTC) {
		par->riva.PCIO = par->riva.PCIO0 + 0x2000;
		par->riva.PCRTC = par->riva.PCRTC0 + 0x800;
		par->riva.PRAMDAC = par->riva.PRAMDAC0 + 0x800;
		par->riva.PDIO = par->riva.PDIO0 + 0x2000;
	} else {
		par->riva.PCIO = par->riva.PCIO0;
		par->riva.PCRTC = par->riva.PCRTC0;
		par->riva.PRAMDAC = par->riva.PRAMDAC0;
		par->riva.PDIO = par->riva.PDIO0;
	}

	RivaGetConfig(&par->riva, par->Chipset);

	if (par->FlatPanel == -1) {
		/* Fix me, need x86 DDC code */
		par->FlatPanel = 0;
	}
	par->riva.flatPanel = (par->FlatPanel > 0) ? TRUE : FALSE;
}

