/*
 * include/asm-ppc/platforms/prpmc750.h
 *
 * Definitions for Motorola PrPMC750 board support
 *
 * Author: Matt Porter <mporter@mvista.com>
 *
 * Copyright 2001 MontaVista Software Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifdef __KERNEL__
#ifndef __ASM_PRPMC750_H__
#define __ASM_PRPMC750_H__

#include <linux/serial_reg.h>

/*
 * Due to limiations imposed by legacy hardware (primaryily IDE controllers),
 * the PrPMC750 carrier board operates using a PReP address map.
 *
 * From Processor (physical) -> PCI:
 *   PCI Mem Space: 0xc0000000 - 0xfe000000 -> 0x00000000 - 0x3e000000 (768 MB)
 *   PCI I/O Space: 0x80000000 - 0x90000000 -> 0x00000000 - 0x10000000 (256 MB)
 *	Note: Must skip 0xfe000000-0xfe400000 for CONFIG_HIGHMEM/PKMAP area
 *
 * From PCI -> Processor (physical):
 *   System Memory: 0x80000000 -> 0x00000000
 */

#define PRPMC750_ISA_IO_BASE		PREP_ISA_IO_BASE
#define PRPMC750_ISA_MEM_BASE		PREP_ISA_MEM_BASE

/* PCI Memory space mapping info */
#define PRPMC750_PCI_MEM_SIZE		0x30000000U
#define PRPMC750_PROC_PCI_MEM_START	PRPMC750_ISA_MEM_BASE
#define PRPMC750_PROC_PCI_MEM_END	(PRPMC750_PROC_PCI_MEM_START +	\
					 PRPMC750_PCI_MEM_SIZE - 1)
#define PRPMC750_PCI_MEM_START		0x00000000U
#define PRPMC750_PCI_MEM_END		(PRPMC750_PCI_MEM_START +	\
					 PRPMC750_PCI_MEM_SIZE - 1)

/* PCI I/O space mapping info */
#define PRPMC750_PCI_IO_SIZE		0x10000000U
#define PRPMC750_PROC_PCI_IO_START	PRPMC750_ISA_IO_BASE
#define PRPMC750_PROC_PCI_IO_END	(PRPMC750_PROC_PCI_IO_START +	\
					 PRPMC750_PCI_IO_SIZE - 1)
#define PRPMC750_PCI_IO_START		0x00000000U
#define PRPMC750_PCI_IO_END		(PRPMC750_PCI_IO_START + 	\
					 PRPMC750_PCI_IO_SIZE - 1)

/* System memory mapping info */
#define PRPMC750_PCI_DRAM_OFFSET	PREP_PCI_DRAM_OFFSET
#define PRPMC750_PCI_PHY_MEM_OFFSET	(PRPMC750_ISA_MEM_BASE-PRPMC750_PCI_MEM_START)

/* Register address definitions */
#define PRPMC750_HAWK_SMC_BASE		0xfef80000U
#define PRPMC750_HAWK_PPC_REG_BASE	0xfeff0000U

#define PRPMC750_BASE_BAUD		1843200
#define PRPMC750_SERIAL_0		0xfef88000
#define PRPMC750_SERIAL_0_DLL		(PRPMC750_SERIAL_0 + (UART_DLL << 4))
#define PRPMC750_SERIAL_0_DLM		(PRPMC750_SERIAL_0 + (UART_DLM << 4))
#define PRPMC750_SERIAL_0_LCR		(PRPMC750_SERIAL_0 + (UART_LCR << 4))

#define PRPMC750_STATUS_REG		0xfef88080
#define PRPMC750_BAUDOUT_MASK		0x02
#define PRPMC750_MONARCH_MASK		0x01

#define PRPMC750_MODRST_REG		0xfef880a0
#define PRPMC750_MODRST_MASK		0x01

#define PRPMC750_PIRQ_REG		0xfef880b0
#define PRPMC750_SEL1_MASK		0x02
#define PRPMC750_SEL0_MASK		0x01

#define PRPMC750_TBEN_REG		0xfef880c0
#define PRPMC750_TBEN_MASK		0x01

#endif				/* __ASM_PRPMC750_H__ */
#endif				/* __KERNEL__ */
