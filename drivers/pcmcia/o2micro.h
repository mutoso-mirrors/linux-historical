/*
 * o2micro.h 1.10 1999/09/03 16:43:35
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
 * <dhinds@hyper.stanford.edu>.  Portions created by David A. Hinds
 * are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU Public License version 2 (the "GPL"), in which
 * case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use
 * your version of this file under the MPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the MPL or the GPL.
 */

#ifndef _LINUX_O2MICRO_H
#define _LINUX_O2MICRO_H

#ifndef PCI_VENDOR_ID_O2
#define PCI_VENDOR_ID_O2		0x1217
#endif
#ifndef PCI_DEVICE_ID_O2_6729
#define PCI_DEVICE_ID_O2_6729		0x6729
#endif
#ifndef PCI_DEVICE_ID_O2_6730
#define PCI_DEVICE_ID_O2_6730		0x673a
#endif
#ifndef PCI_DEVICE_ID_O2_6832
#define PCI_DEVICE_ID_O2_6832		0x6832
#endif
#ifndef PCI_DEVICE_ID_O2_6836
#define PCI_DEVICE_ID_O2_6836		0x6836
#endif

#define O2_MODE_A		0x38
#define O2_MODE_A_2		0x26	/* For 6833B, 6860C */
#define  O2_MODE_A_CD_PULSE	0x04
#define  O2_MODE_A_SUSP_EDGE	0x08
#define  O2_MODE_A_HOST_SUSP	0x10
#define  O2_MODE_A_PWRCHIP	0x60
#define  O2_MODE_A_QUIET	0x80

#define O2_MODE_B		0x39
#define O2_MODE_B_2		0x2e	/* For 6833B, 6860C */
#define  O2_MODE_B_IDENT	0x03
#define  O2_MODE_B_ID_BSTEP	0x00
#define  O2_MODE_B_ID_CSTEP	0x01
#define  O2_MODE_B_ID_O2	0x02
#define  O2_MODE_B_VS1		0x04
#define  O2_MODE_B_VS2		0x08
#define  O2_MODE_B_IRQ15_RI	0x80

#define O2_MODE_C		0x3a
#define  O2_MODE_C_DREQ_MASK	0x03
#define  O2_MODE_C_DREQ_INPACK	0x01
#define  O2_MODE_C_DREQ_WP	0x02
#define  O2_MODE_C_DREQ_BVD2	0x03
#define  O2_MODE_C_ZVIDEO	0x08

#define O2_MODE_D		0x3b
#define  O2_MODE_D_IRQ_MODE	0x03
#define  O2_MODE_D_SKT_ACTV	0x20
#define  O2_MODE_D_PCI_FIFO	0x40	/* for OZ6729, OZ6730 */
#define  O2_MODE_D_W97_IRQ	0x40	/* for OZ6832 */
#define  O2_MODE_D_ISA_IRQ	0x80

#define O2_MHPG_DMA		0x3c
#define  O2_MHPG_CHANNEL	0x07
#define  O2_MHPG_CINT_ENA	0x08
#define  O2_MHPG_CSC_ENA	0x10

#define O2_FIFO_ENA		0x3d
#define  O2_FIFO_ZVIDEO_3	0x08
#define  O2_FIFO_PCI_FIFO	0x10
#define  O2_FIFO_POSTWR		0x40
#define  O2_FIFO_BUFFER		0x80

#define O2_MODE_E		0x3e
#define  O2_MODE_E_MHPG_DMA	0x01
#define  O2_MODE_E_SPKR_OUT	0x02
#define  O2_MODE_E_LED_OUT	0x08
#define  O2_MODE_E_SKTA_ACTV	0x10

#endif /* _LINUX_O2MICRO_H */
