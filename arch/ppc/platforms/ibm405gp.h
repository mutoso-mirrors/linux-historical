/*
 * ibm405gp.h
 *
 *	This was derived from the ppc4xx.h and all 405GP specific
 *	definition and board inclusions were moved here.
 *
 *      Armin Kuster akuster@mvista.com
 *      Oct, 2001
 *
 *
 * Copyright 2001 MontaVista Softare Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR   IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT,  INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	Version 1.0 (01/10/10) - A. Kuster
 *	Initial version	 - moved 40GP  specific out of ppc4xx.h
 *			- moved emac reg from ppc405_enet.h
 *
 *	Version 1.1 02/01/17 - A. Kuster
 *	Moved offsets to ibm405.h
 *
 *	Version 1.2 05/01/02 - Armin
 *	 added IIC_PORT_DFNS
 *
 *	Version 1.3 - armin
 *	  removed iic & uart num defines
 *	  and IIC_PORT_DFNS
 */

#ifdef __KERNEL__
#ifndef __ASM_IBM405GP_H__
#define __ASM_IBM405GP_H__

#include <linux/config.h>
#include <platforms/ibm_ocp.h>

/* ibm405.h at bottom of this file */

/* PCI
 * PCI Bridge config reg definitions
 * see 17-19 of manual
 */

#define PPC405_PCI_CONFIG_ADDR	0xeec00000
#define PPC405_PCI_CONFIG_DATA	0xeec00004

#define PPC405_PCI_PHY_MEM_BASE	0x80000000	/* hose_a->pci_mem_offset */
						/* setbat */
#define PPC405_PCI_MEM_BASE	PPC405_PCI_PHY_MEM_BASE	/* setbat */
#define PPC405_PCI_PHY_IO_BASE	0xe8000000	/* setbat */
#define PPC405_PCI_IO_BASE	PPC405_PCI_PHY_IO_BASE	/* setbat */

#define PPC405_PCI_LOWER_MEM	0x80000000	/* hose_a->mem_space.start */
#define PPC405_PCI_UPPER_MEM	0xBfffffff	/* hose_a->mem_space.end */
#define PPC405_PCI_LOWER_IO	0x00000000	/* hose_a->io_space.start */
#define PPC405_PCI_UPPER_IO	0x0000ffff	/* hose_a->io_space.end */

#define PPC405_ISA_IO_BASE	PPC405_PCI_IO_BASE

#define PPC4xx_PCI_IO_PADDR	((uint)PPC405_PCI_PHY_IO_BASE)
#define PPC4xx_PCI_IO_VADDR	PPC4xx_PCI_IO_PADDR
#define PPC4xx_PCI_IO_SIZE	((uint)64*1024)
#define PPC4xx_PCI_CFG_PADDR	((uint)PPC405_PCI_CONFIG_ADDR)
#define PPC4xx_PCI_CFG_VADDR	PPC4xx_PCI_CFG_PADDR
#define PPC4xx_PCI_CFG_SIZE	((uint)4*1024)
#define PPC4xx_PCI_LCFG_PADDR	((uint)0xef400000)
#define PPC4xx_PCI_LCFG_VADDR	PPC4xx_PCI_LCFG_PADDR
#define PPC4xx_PCI_LCFG_SIZE	((uint)4*1024)
#define PPC4xx_ONB_IO_PADDR	((uint)0xef600000)
#define PPC4xx_ONB_IO_VADDR	PPC4xx_ONB_IO_PADDR
#define PPC4xx_ONB_IO_SIZE	((uint)4*1024)

/* serial port defines */
#define RS_TABLE_SIZE	2

#define UART0_INT	0
#define UART1_INT	1

#define PCIL0_BASE	0xEF400000
#define UART0_IO_BASE	0xEF600300
#define UART1_IO_BASE	0xEF600400
#define IIC0_BASE	0xEF600500
#define OPB0_BASE	0xEF600600
#define GPIO0_BASE	0xEF600700
#define EMAC0_BASE	0xEF600800

#define EMAC_NUMS	1
#define IIC0_IRQ	2
#define IIC1_IRQ	0

#define IIC_OWN		0x55
#define IIC_CLOCK	50
#define BD_EMAC_ADDR(e,i) bi_enetaddr[i]

#define STD_UART_OP(num)					\
	{ 0, BASE_BAUD, 0, UART##num##_INT,			\
		(ASYNC_BOOT_AUTOCONF | ASYNC_SKIP_TEST),	\
		iomem_base: (u8 *)UART##num##_IO_BASE,		\
		io_type: SERIAL_IO_MEM},

#if defined(CONFIG_UART0_TTYS0)
#define SERIAL_DEBUG_IO_BASE	UART0_IO_BASE
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(0)		\
	STD_UART_OP(1)
#endif

#if defined(CONFIG_UART0_TTYS1)
#define SERIAL_DEBUG_IO_BASE	UART1_IO_BASE
#define SERIAL_PORT_DFNS	\
	STD_UART_OP(1)		\
	STD_UART_OP(0)
#endif

/* DCR defines */
#define DCRN_CHCR_BASE		0x0B1
#define DCRN_CHPSR_BASE		0x0B4
#define DCRN_CPMSR_BASE		0x0B8
#define DCRN_CPMFR_BASE		0x0BA

#define CHR0_U0EC	0x00000080	/* Select external clock for UART0 */
#define CHR0_U1EC	0x00000040	/* Select external clock for UART1 */
#define CHR0_UDIV	0x0000003E	/* UART internal clock divisor */
#define CHR1_CETE	0x00800000	/* CPU external timer enable */

#define DCRN_CHPSR_BASE         0x0B4
#define  PSR_PLL_FWD_MASK        0xC0000000
#define  PSR_PLL_FDBACK_MASK     0x30000000
#define  PSR_PLL_TUNING_MASK     0x0E000000
#define  PSR_PLB_CPU_MASK        0x01800000
#define  PSR_OPB_PLB_MASK        0x00600000
#define  PSR_PCI_PLB_MASK        0x00180000
#define  PSR_EB_PLB_MASK         0x00060000
#define  PSR_ROM_WIDTH_MASK      0x00018000
#define  PSR_ROM_LOC             0x00004000
#define  PSR_PCI_ASYNC_EN        0x00001000
#define  PSR_PCI_ARBIT_EN        0x00000400

#define CPM_IIC0		0x80000000	/* IIC interface */
#define CPM_PCI			0x40000000	/* PCI bridge */
#define CPM_CPU			0x20000000	/* processor core */
#define CPM_DMA			0x10000000	/* DMA controller */
#define CPM_BRG			0x08000000	/* PLB to OPB bridge */
#define CPM_DCP			0x04000000	/* CodePack */
#define CPM_EBC			0x02000000	/* ROM/SRAM peripheral controller */
#define CPM_SDRAM0		0x01000000	/* SDRAM memory controller */
#define CPM_PLB			0x00800000	/* PLB bus arbiter */
#define CPM_GPIO0		0x00400000	/* General Purpose IO (??) */
#define CPM_UART0		0x00200000	/* serial port 0 */
#define CPM_UART1		0x00100000	/* serial port 1 */
#define CPM_UIC			0x00080000	/* Universal Interrupt Controller */
#define CPM_TMRCLK		0x00040000	/* CPU timers */
#define CPM_EMAC_MM		0x00020000	/* on-chip ethernet MM unit */
#define CPM_EMAC_RM		0x00010000	/* on-chip ethernet RM unit */
#define CPM_EMAC_TM		0x00008000	/* on-chip ethernet TM unit */

#define DCRN_DMA0_BASE		0x100
#define DCRN_DMA1_BASE		0x108
#define DCRN_DMA2_BASE		0x110
#define DCRN_DMA3_BASE		0x118
#define DCRNCAP_DMA_SG		1	/* have DMA scatter/gather capability */
#define DCRN_DMASR_BASE		0x120
#define DCRN_EBC_BASE		0x012
#define DCRN_DCP0_BASE		0x014
#define DCRN_MAL_BASE		0x180
#define DCRN_OCM0_BASE		0x018
#define DCRN_PLB0_BASE		0x084
#define DCRN_PLLMR_BASE		0x0B0
#define DCRN_POB0_BASE		0x0A0
#define DCRN_SDRAM0_BASE	0x010
#define DCRN_UIC0_BASE		0x0C0
#define UIC0 DCRN_UIC0_BASE

#include <platforms/ibm405.h>

#endif				/* __ASM_IBM405GP_H__ */
#endif				/* __KERNEL__ */
