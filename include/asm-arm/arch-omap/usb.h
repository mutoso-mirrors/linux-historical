// include/asm-arm/mach-omap/usb.h

#ifndef	__ASM_ARCH_OMAP_USB_H
#define	__ASM_ARCH_OMAP_USB_H

#include <asm/arch/board.h>

/*-------------------------------------------------------------------------*/

#define OTG_BASE			0xfffb0400
#define UDC_BASE			0xfffb4000
#define OMAP_OHCI_BASE			0xfffba000

/*-------------------------------------------------------------------------*/

/*
 * OTG and transceiver registers, for OMAPs starting with ARM926
 */
#define OTG_REG32(offset)		__REG32(OTG_BASE + (offset))
#define OTG_REG16(offset)		__REG16(OTG_BASE + (offset))

#define OTG_REV_REG			OTG_REG32(0x00)
#define OTG_SYSCON_1_REG		OTG_REG32(0x04)
#	define	 USB2_TRX_MODE(w)	(((w)>>24)&0x07)
#	define	 USB1_TRX_MODE(w)	(((w)>>20)&0x07)
#	define	 USB0_TRX_MODE(w)	(((w)>>16)&0x07)
#	define	 OTG_IDLE_EN		(1 << 15)
#	define	 HST_IDLE_EN		(1 << 14)
#	define	 DEV_IDLE_EN		(1 << 13)
#	define	 OTG_RESET_DONE		(1 << 2)
#define OTG_SYSCON_2_REG		OTG_REG32(0x08)
#	define	 OTG_EN			(1 << 31)
#	define	 USBX_SYNCHRO		(1 << 30)
#	define	 OTG_MST16		(1 << 29)
#	define	 SRP_GPDATA		(1 << 28)
#	define	 SRP_GPDVBUS		(1 << 27)
#	define	 SRP_GPUVBUS(w)		(((w)>>24)&0x07)
#	define	 A_WAIT_VRISE(w)	(((w)>>20)&0x07)
#	define	 B_ASE_BRST(w)		(((w)>>16)&0x07)
#	define	 SRP_DPW		(1 << 14)
#	define	 SRP_DATA		(1 << 13)
#	define	 SRP_VBUS		(1 << 12)
#	define	 OTG_PADEN		(1 << 10)
#	define	 HMC_PADEN		(1 << 9)
#	define	 UHOST_EN		(1 << 8)
#	define	 HMC_TLLSPEED		(1 << 7)
#	define	 HMC_TLLATTACH		(1 << 6)
#	define	 OTG_HMC(w)		(((w)>>0)&0x3f)
#define OTG_CTRL_REG			OTG_REG32(0x0c)
#	define	 OTG_ASESSVLD		(1 << 20)
#	define	 OTG_BSESSEND		(1 << 19)
#	define	 OTG_BSESSVLD		(1 << 18)
#	define	 OTG_VBUSVLD		(1 << 17)
#	define	 OTG_ID			(1 << 16)
#	define	 OTG_DRIVER_SEL		(1 << 15)
#	define	 OTG_A_SETB_HNPEN	(1 << 12)
#	define	 OTG_A_BUSREQ		(1 << 11)
#	define	 OTG_B_HNPEN		(1 << 9)
#	define	 OTG_B_BUSREQ		(1 << 8)
#	define	 OTG_BUSDROP		(1 << 7)
#	define	 OTG_PULLDOWN		(1 << 5)
#	define	 OTG_PULLUP		(1 << 4)
#	define	 OTG_DRV_VBUS		(1 << 3)
#	define	 OTG_PD_VBUS		(1 << 2)
#	define	 OTG_PU_VBUS		(1 << 1)
#	define	 OTG_PU_ID		(1 << 0)
#define OTG_IRQ_EN_REG			OTG_REG16(0x10)
#	define	 DRIVER_SWITCH		(1 << 15)
#	define	 A_VBUS_ERR		(1 << 13)
#	define	 A_REQ_TMROUT		(1 << 12)
#	define	 A_SRP_DETECT		(1 << 11)
#	define	 B_HNP_FAIL		(1 << 10)
#	define	 B_SRP_TMROUT		(1 << 9)
#	define	 B_SRP_DONE		(1 << 8)
#	define	 B_SRP_STARTED		(1 << 7)
#	define	 OPRT_CHG		(1 << 0)
#define OTG_IRQ_SRC_REG			OTG_REG16(0x14)
	// same bits as in IRQ_EN
#define OTG_OUTCTRL_REG			OTG_REG16(0x18)
#	define	 OTGVPD			(1 << 14)
#	define	 OTGVPU			(1 << 13)
#	define	 OTGPUID		(1 << 12)
#	define	 USB2VDR		(1 << 10)
#	define	 USB2PDEN		(1 << 9)
#	define	 USB2PUEN		(1 << 8)
#	define	 USB1VDR		(1 << 6)
#	define	 USB1PDEN		(1 << 5)
#	define	 USB1PUEN		(1 << 4)
#	define	 USB0VDR		(1 << 2)
#	define	 USB0PDEN		(1 << 1)
#	define	 USB0PUEN		(1 << 0)
#define OTG_TEST_REG			OTG_REG16(0x20)
#define OTG_VENDOR_CODE_REG		OTG_REG32(0xfc)

/*-------------------------------------------------------------------------*/

#define	USB_TRANSCEIVER_CTRL_REG	__REG32(0xfffe1000 + 0x0064)
#	define	CONF_USB2_UNI_R		(1 << 8)
#	define	CONF_USB1_UNI_R		(1 << 7)
#	define	CONF_USB_PORT0_R(x)	(((x)>>4)&0x7)
#	define	CONF_USB0_ISOLATE_R	(1 << 3)
#	define	CONF_USB_PWRDN_DM_R	(1 << 2)
#	define	CONF_USB_PWRDN_DP_R	(1 << 1)




#endif	/* __ASM_ARCH_OMAP_USB_H */
