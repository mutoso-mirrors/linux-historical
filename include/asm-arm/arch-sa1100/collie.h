/*
 * linux/include/asm-arm/arch-sa1100/collie.h
 *
 * This file contains the hardware specific definitions for Assabet
 * Only include this file from SA1100-specific files.
 *
 * ChangeLog:
 *   04-06-2001 Lineo Japan, Inc.
 *   04-16-2001 SHARP Corporation
 *   07-07-2002 Chris Larson <clarson@digi.com>
 *
 */
#ifndef __ASM_ARCH_COLLIE_H
#define __ASM_ARCH_COLLIE_H

#include <linux/config.h>

#define CF_BUF_CTRL_BASE 0xF0800000
#define	COLLIE_SCP_REG(adr) (*(volatile unsigned short*)(CF_BUF_CTRL_BASE+(adr)))
#define	COLLIE_SCP_MCR	0x00
#define	COLLIE_SCP_CDR	0x04
#define	COLLIE_SCP_CSR	0x08
#define	COLLIE_SCP_CPR	0x0C
#define	COLLIE_SCP_CCR	0x10
#define	COLLIE_SCP_IRR	0x14
#define	COLLIE_SCP_IRM	0x14
#define	COLLIE_SCP_IMR	0x18
#define	COLLIE_SCP_ISR	0x1C
#define	COLLIE_SCP_GPCR	0x20
#define	COLLIE_SCP_GPWR	0x24
#define	COLLIE_SCP_GPRR	0x28
#define	COLLIE_SCP_REG_MCR	COLLIE_SCP_REG(COLLIE_SCP_MCR)
#define	COLLIE_SCP_REG_CDR	COLLIE_SCP_REG(COLLIE_SCP_CDR)
#define	COLLIE_SCP_REG_CSR	COLLIE_SCP_REG(COLLIE_SCP_CSR)
#define	COLLIE_SCP_REG_CPR	COLLIE_SCP_REG(COLLIE_SCP_CPR)
#define	COLLIE_SCP_REG_CCR	COLLIE_SCP_REG(COLLIE_SCP_CCR)
#define	COLLIE_SCP_REG_IRR	COLLIE_SCP_REG(COLLIE_SCP_IRR)
#define	COLLIE_SCP_REG_IRM	COLLIE_SCP_REG(COLLIE_SCP_IRM)
#define	COLLIE_SCP_REG_IMR	COLLIE_SCP_REG(COLLIE_SCP_IMR)
#define	COLLIE_SCP_REG_ISR	COLLIE_SCP_REG(COLLIE_SCP_ISR)
#define	COLLIE_SCP_REG_GPCR	COLLIE_SCP_REG(COLLIE_SCP_GPCR)
#define	COLLIE_SCP_REG_GPWR	COLLIE_SCP_REG(COLLIE_SCP_GPWR)
#define	COLLIE_SCP_REG_GPRR	COLLIE_SCP_REG(COLLIE_SCP_GPRR)

#define COLLIE_SCP_GPCR_PA19	( 1 << 9 )
#define COLLIE_SCP_GPCR_PA18	( 1 << 8 )
#define COLLIE_SCP_GPCR_PA17	( 1 << 7 )
#define COLLIE_SCP_GPCR_PA16	( 1 << 6 )
#define COLLIE_SCP_GPCR_PA15	( 1 << 5 )
#define COLLIE_SCP_GPCR_PA14	( 1 << 4 )
#define COLLIE_SCP_GPCR_PA13	( 1 << 3 )
#define COLLIE_SCP_GPCR_PA12	( 1 << 2 )
#define COLLIE_SCP_GPCR_PA11	( 1 << 1 )

#define COLLIE_SCP_CHARGE_ON	COLLIE_SCP_GPCR_PA11
#define COLLIE_SCP_DIAG_BOOT1	COLLIE_SCP_GPCR_PA12
#define COLLIE_SCP_DIAG_BOOT2	COLLIE_SCP_GPCR_PA13
#define COLLIE_SCP_MUTE_L	COLLIE_SCP_GPCR_PA14
#define COLLIE_SCP_MUTE_R	COLLIE_SCP_GPCR_PA15
#define COLLIE_SCP_5VON	COLLIE_SCP_GPCR_PA16
#define COLLIE_SCP_AMP_ON	COLLIE_SCP_GPCR_PA17
#define COLLIE_SCP_VPEN	COLLIE_SCP_GPCR_PA18
#define COLLIE_SCP_LB_VOL_CHG	COLLIE_SCP_GPCR_PA19

#define COLLIE_SCP_IO_DIR	( COLLIE_SCP_CHARGE_ON | COLLIE_SCP_MUTE_L | COLLIE_SCP_MUTE_R | \
				COLLIE_SCP_5VON | COLLIE_SCP_AMP_ON | COLLIE_SCP_VPEN | \
				COLLIE_SCP_LB_VOL_CHG )
#define COLLIE_SCP_IO_OUT	( COLLIE_SCP_MUTE_L | COLLIE_SCP_MUTE_R | COLLIE_SCP_VPEN | \
				COLLIE_SCP_CHARGE_ON )

/* GPIOs for which the generic definition doesn't say much */

#define COLLIE_GPIO_ON_KEY		GPIO_GPIO (0)
#define COLLIE_GPIO_AC_IN		GPIO_GPIO (1)
#define COLLIE_GPIO_CF_IRQ		GPIO_GPIO (14)
#define COLLIE_GPIO_nREMOCON_INT	GPIO_GPIO (15)
#define COLLIE_GPIO_UCB1x00_RESET	GPIO_GPIO (16)
#define COLLIE_GPIO_CO			GPIO_GPIO (20)
#define COLLIE_GPIO_MCP_CLK		GPIO_GPIO (21)
#define COLLIE_GPIO_CF_CD		GPIO_GPIO (22)
#define COLLIE_GPIO_UCB1x00_IRQ		GPIO_GPIO (23)
#define COLLIE_GPIO_WAKEUP		GPIO_GPIO (24)
#define COLLIE_GPIO_GA_INT		GPIO_GPIO (25)
#define COLLIE_GPIO_MAIN_BAT_LOW	GPIO_GPIO (26)

/* Interrupts */

#define COLLIE_IRQ_GPIO_ON_KEY		IRQ_GPIO0
#define COLLIE_IRQ_GPIO_AC_IN		IRQ_GPIO1
#define COLLIE_IRQ_GPIO_CF_IRQ		IRQ_GPIO14
#define COLLIE_IRQ_GPIO_nREMOCON_INT	IRQ_GPIO15
#define COLLIE_IRQ_GPIO_CO		IRQ_GPIO20
#define COLLIE_IRQ_GPIO_CF_CD		IRQ_GPIO22
#define COLLIE_IRQ_GPIO_UCB1x00_IRQ	IRQ_GPIO23
#define COLLIE_IRQ_GPIO_WAKEUP		IRQ_GPIO24
#define COLLIE_IRQ_GPIO_GA_INT		IRQ_GPIO25
#define COLLIE_IRQ_GPIO_MAIN_BAT_LOW	IRQ_GPIO26

#define COLLIE_LCM_IRQ_GPIO_RTS		IRQ_LOCOMO_GPIO0
#define COLLIE_LCM_IRQ_GPIO_CTS		IRQ_LOCOMO_GPIO1
#define COLLIE_LCM_IRQ_GPIO_DSR		IRQ_LOCOMO_GPIO2
#define COLLIE_LCM_IRQ_GPIO_DTR		IRQ_LOCOMO_GPIO3
#define COLLIE_LCM_IRQ_GPIO_nSD_DETECT	IRQ_LOCOMO_GPIO13
#define COLLIE_LCM_IRQ_GPIO_nSD_WP	IRQ_LOCOMO_GPIO14

/*
 * Flash Memory mappings
 *
 */

#define FLASH_MEM_BASE 0xe8ffc000
#define	FLASH_DATA(adr) (*(volatile unsigned int*)(FLASH_MEM_BASE+(adr)))
#define	FLASH_DATA_F(adr) (*(volatile float32 *)(FLASH_MEM_BASE+(adr)))
#define FLASH_MAGIC_CHG(a,b,c,d) ( ( d << 24 ) | ( c << 16 )  | ( b << 8 ) | a )

// COMADJ
#define FLASH_COMADJ_MAJIC	FLASH_MAGIC_CHG('C','M','A','D')
#define	FLASH_COMADJ_MAGIC_ADR	0x00
#define	FLASH_COMADJ_DATA_ADR	0x04

// TOUCH PANEL
#define FLASH_TOUCH_MAJIC	FLASH_MAGIC_CHG('T','U','C','H')
#define	FLASH_TOUCH_MAGIC_ADR	0x1C
#define	FLASH_TOUCH_XP_DATA_ADR	0x20
#define	FLASH_TOUCH_YP_DATA_ADR	0x24
#define	FLASH_TOUCH_XD_DATA_ADR	0x28
#define	FLASH_TOUCH_YD_DATA_ADR	0x2C

// AD
#define FLASH_AD_MAJIC	FLASH_MAGIC_CHG('B','V','A','D')
#define	FLASH_AD_MAGIC_ADR	0x30
#define	FLASH_AD_DATA_ADR	0x34

/* GPIO's on the TC35143AF (Toshiba Analog Frontend) */
#define COLLIE_TC35143_GPIO_VERSION0    UCB_IO_0	/* GPIO0=Version                 */
#define COLLIE_TC35143_GPIO_TBL_CHK     UCB_IO_1	/* GPIO1=TBL_CHK                 */
#define COLLIE_TC35143_GPIO_VPEN_ON     UCB_IO_2	/* GPIO2=VPNE_ON                 */
#define COLLIE_TC35143_GPIO_IR_ON       UCB_IO_3	/* GPIO3=IR_ON                   */
#define COLLIE_TC35143_GPIO_AMP_ON      UCB_IO_4	/* GPIO4=AMP_ON                  */
#define COLLIE_TC35143_GPIO_VERSION1    UCB_IO_5	/* GPIO5=Version                 */
#define COLLIE_TC35143_GPIO_FS8KLPF     UCB_IO_5	/* GPIO5=fs 8k LPF               */
#define COLLIE_TC35143_GPIO_BUZZER_BIAS UCB_IO_6	/* GPIO6=BUZZER BIAS             */
#define COLLIE_TC35143_GPIO_MBAT_ON     UCB_IO_7	/* GPIO7=MBAT_ON                 */
#define COLLIE_TC35143_GPIO_BBAT_ON     UCB_IO_8	/* GPIO8=BBAT_ON                 */
#define COLLIE_TC35143_GPIO_TMP_ON      UCB_IO_9	/* GPIO9=TMP_ON                  */
#define COLLIE_TC35143_GPIO_IN		( UCB_IO_0 | UCB_IO_2 | UCB_IO_5 )
#define COLLIE_TC35143_GPIO_OUT		( UCB_IO_1 | UCB_IO_3 | UCB_IO_4 | UCB_IO_6 | \
					UCB_IO_7 | UCB_IO_8 | UCB_IO_9 )

#endif
