/* linux/include/asm/arch-s3c2410/regs-udc.h
 *
 * Copyright (C) 2004 Herbert Poetzl <herbert@13thfloor.at>
 *
 * This include file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 *  Changelog:
 *    01-08-2004	initial creation
 *    12-09-2004	cleanup for submission
 */

#ifndef __ASM_ARCH_REGS_UDC_H
#define __ASM_ARCH_REGS_UDC_H


#define S3C2410_USBDREG(x) ((x) + S3C2410_VA_USBDEV)

#define S3C2410_UDC_FUNC_ADDR_REG	S3C2410_USBDREG(0x0140)
#define S3C2410_UDC_PWR_REG		S3C2410_USBDREG(0x0144)
#define S3C2410_UDC_EP_INT_REG		S3C2410_USBDREG(0x0148)

#define S3C2410_UDC_USB_INT_REG		S3C2410_USBDREG(0x0158)
#define S3C2410_UDC_EP_INT_EN_REG	S3C2410_USBDREG(0x015c)

#define S3C2410_UDC_USB_INT_EN_REG	S3C2410_USBDREG(0x016c)

#define S3C2410_UDC_FRAME_NUM1_REG	S3C2410_USBDREG(0x0170)
#define S3C2410_UDC_FRAME_NUM2_REG	S3C2410_USBDREG(0x0174)

#define S3C2410_UDC_EP0_FIFO_REG	S3C2410_USBDREG(0x01c0)
#define S3C2410_UDC_EP1_FIFO_REG	S3C2410_USBDREG(0x01c4)
#define S3C2410_UDC_EP2_FIFO_REG	S3C2410_USBDREG(0x01c8)
#define S3C2410_UDC_EP3_FIFO_REG	S3C2410_USBDREG(0x01cc)
#define S3C2410_UDC_EP4_FIFO_REG	S3C2410_USBDREG(0x01d0)

#define S3C2410_UDC_EP1_DMA_CON		S3C2410_USBDREG(0x0200)
#define S3C2410_UDC_EP1_DMA_UNIT	S3C2410_USBDREG(0x0204)
#define S3C2410_UDC_EP1_DMA_FIFO	S3C2410_USBDREG(0x0208)
#define S3C2410_UDC_EP1_DMA_TTC_L	S3C2410_USBDREG(0x020c)
#define S3C2410_UDC_EP1_DMA_TTC_M	S3C2410_USBDREG(0x0210)
#define S3C2410_UDC_EP1_DMA_TTC_H	S3C2410_USBDREG(0x0214)

#define S3C2410_UDC_EP2_DMA_CON		S3C2410_USBDREG(0x0218)
#define S3C2410_UDC_EP2_DMA_UNIT	S3C2410_USBDREG(0x021c)
#define S3C2410_UDC_EP2_DMA_FIFO	S3C2410_USBDREG(0x0220)
#define S3C2410_UDC_EP2_DMA_TTC_L	S3C2410_USBDREG(0x0224)
#define S3C2410_UDC_EP2_DMA_TTC_M	S3C2410_USBDREG(0x0228)
#define S3C2410_UDC_EP2_DMA_TTC_H	S3C2410_USBDREG(0x022c)

#define S3C2410_UDC_EP3_DMA_CON		S3C2410_USBDREG(0x0240)
#define S3C2410_UDC_EP3_DMA_UNIT	S3C2410_USBDREG(0x0244)
#define S3C2410_UDC_EP3_DMA_FIFO	S3C2410_USBDREG(0x0248)
#define S3C2410_UDC_EP3_DMA_TTC_L	S3C2410_USBDREG(0x024c)
#define S3C2410_UDC_EP3_DMA_TTC_M	S3C2410_USBDREG(0x0250)
#define S3C2410_UDC_EP3_DMA_TTC_H	S3C2410_USBDREG(0x0254)

#define S3C2410_UDC_EP4_DMA_CON		S3C2410_USBDREG(0x0258)
#define S3C2410_UDC_EP4_DMA_UNIT	S3C2410_USBDREG(0x025c)
#define S3C2410_UDC_EP4_DMA_FIFO	S3C2410_USBDREG(0x0260)
#define S3C2410_UDC_EP4_DMA_TTC_L	S3C2410_USBDREG(0x0264)
#define S3C2410_UDC_EP4_DMA_TTC_M	S3C2410_USBDREG(0x0268)
#define S3C2410_UDC_EP4_DMA_TTC_H	S3C2410_USBDREG(0x026c)

#define S3C2410_UDC_INDEX_REG		S3C2410_USBDREG(0x0178)

/* indexed registers */

#define S3C2410_UDC_MAXP_REG		S3C2410_USBDREG(0x018c)

#define S3C2410_UDC_EP0_CSR_REG		S3C2410_USBDREG(0x0184)

#define S3C2410_UDC_IN_CSR1_REG		S3C2410_USBDREG(0x0184)
#define S3C2410_UDC_IN_CSR2_REG		S3C2410_USBDREG(0x0188)

#define S3C2410_UDC_OUT_CSR1_REG	S3C2410_USBDREG(0x0190)
#define S3C2410_UDC_OUT_CSR2_REG	S3C2410_USBDREG(0x0194)
#define S3C2410_UDC_OUT_FIFO_CNT1_REG	S3C2410_USBDREG(0x0198)
#define S3C2410_UDC_OUT_FIFO_CNT2_REG	S3C2410_USBDREG(0x019c)



#define S3C2410_UDC_PWR_ISOUP		(1<<7) // R/W
#define S3C2410_UDC_PWR_RESET		(1<<3) // R
#define S3C2410_UDC_PWR_RESUME		(1<<2) // R/W
#define S3C2410_UDC_PWR_SUSPEND		(1<<1) // R
#define S3C2410_UDC_PWR_ENSUSPEND	(1<<0) // R/W

#define S3C2410_UDC_PWR_DEFAULT		0x00

#define S3C2410_UDC_INT_EP4		(1<<4) // R/W (clear only)
#define S3C2410_UDC_INT_EP3		(1<<3) // R/W (clear only)
#define S3C2410_UDC_INT_EP2		(1<<2) // R/W (clear only)
#define S3C2410_UDC_INT_EP1		(1<<1) // R/W (clear only)
#define S3C2410_UDC_INT_EP0		(1<<0) // R/W (clear only)

#define S3C2410_UDC_USBINT_RESET	(1<<2) // R/W (clear only)
#define S3C2410_UDC_USBINT_RESUME	(1<<1) // R/W (clear only)
#define S3C2410_UDC_USBINT_SUSPEND	(1<<0) // R/W (clear only)

#define S3C2410_UDC_INTE_EP4		(1<<4) // R/W
#define S3C2410_UDC_INTE_EP3		(1<<3) // R/W
#define S3C2410_UDC_INTE_EP2		(1<<2) // R/W
#define S3C2410_UDC_INTE_EP1		(1<<1) // R/W
#define S3C2410_UDC_INTE_EP0		(1<<0) // R/W

#define S3C2410_UDC_USBINTE_RESET	(1<<2) // R/W
#define S3C2410_UDC_USBINTE_SUSPEND	(1<<0) // R/W


#define S3C2410_UDC_INDEX_EP0		(0x00)
#define S3C2410_UDC_INDEX_EP1		(0x01) // ??
#define S3C2410_UDC_INDEX_EP2		(0x02) // ??
#define S3C2410_UDC_INDEX_EP3		(0x03) // ??
#define S3C2410_UDC_INDEX_EP4		(0x04) // ??

#define S3C2410_UDC_ICSR1_CLRDT		(1<<6) // R/W
#define S3C2410_UDC_ICSR1_SENTSTL	(1<<5) // R/W (clear only)
#define S3C2410_UDC_ICSR1_SENDSTL	(1<<4) // R/W
#define S3C2410_UDC_ICSR1_FFLUSH	(1<<3) // W   (set only)
#define S3C2410_UDC_ICSR1_UNDRUN	(1<<2) // R/W (clear only)
#define S3C2410_UDC_ICSR1_PKTRDY	(1<<0) // R/W (set only)

#define S3C2410_UDC_ICSR2_AUTOSET	(1<<7) // R/W
#define S3C2410_UDC_ICSR2_ISO		(1<<6) // R/W
#define S3C2410_UDC_ICSR2_MODEIN	(1<<5) // R/W
#define S3C2410_UDC_ICSR2_DMAIEN	(1<<4) // R/W

#define S3C2410_UDC_OCSR1_CLRDT		(1<<7) // R/W
#define S3C2410_UDC_OCSR1_SENTSTL	(1<<6) // R/W (clear only)
#define S3C2410_UDC_OCSR1_SENDSTL	(1<<5) // R/W
#define S3C2410_UDC_OCSR1_FFLUSH	(1<<4) // R/W
#define S3C2410_UDC_OCSR1_DERROR	(1<<3) // R
#define S3C2410_UDC_OCSR1_OVRRUN	(1<<2) // R/W (clear only)
#define S3C2410_UDC_OCSR1_PKTRDY	(1<<0) // R/W (clear only)

#define S3C2410_UDC_OCSR2_AUTOCLR	(1<<7) // R/W
#define S3C2410_UDC_OCSR2_ISO		(1<<6) // R/W
#define S3C2410_UDC_OCSR2_DMAIEN	(1<<5) // R/W

#define S3C2410_UDC_SETIX(x)	    \
	__raw_writel(S3C2410_UDC_INDEX_ ## x, S3C2410_UDC_INDEX_REG);


#define S3C2410_UDC_EP0_CSR_OPKRDY	(1<<0)
#define S3C2410_UDC_EP0_CSR_IPKRDY	(1<<1)
#define S3C2410_UDC_EP0_CSR_SENTSTL	(1<<2)
#define S3C2410_UDC_EP0_CSR_DE		(1<<3)
#define S3C2410_UDC_EP0_CSR_SE		(1<<4)
#define S3C2410_UDC_EP0_CSR_SENDSTL	(1<<5)
#define S3C2410_UDC_EP0_CSR_SOPKTRDY	(1<<6)
#define S3C2410_UDC_EP0_CSR_SSE	(1<<7)

#define S3C2410_UDC_MAXP_8		(1<<0)
#define S3C2410_UDC_MAXP_16		(1<<1)
#define S3C2410_UDC_MAXP_32		(1<<2)
#define S3C2410_UDC_MAXP_64		(1<<3)


#endif
