/*
 * arch/arm/mach-ixp2000/pci.c
 *
 * PCI routines for IXDP2400/IXDP2800 boards
 *
 * Original Author: Naeem Afzal <naeem.m.afzal@intel.com>
 * Maintained by: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2002 Intel Corp.
 * Copyright (C) 2003-2004 MontaVista Software, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/slab.h>
#include <linux/delay.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/mach-types.h>
#include <asm/hardware.h>

#include <asm/mach/pci.h>

extern int (*external_fault) (unsigned long, struct pt_regs *);

static int pci_master_aborts = 0;

static int clear_master_aborts(void);

static u32 *
ixp2000_pci_config_addr(unsigned int bus_nr, unsigned int devfn, int where)
{
	u32 *paddress;

	if (PCI_SLOT(devfn) > 7)
		return 0;

	/* Must be dword aligned */
	where &= ~3;

	/*
	 * For top bus, generate type 0, else type 1
	 */
	if (!bus_nr) {
		/* only bits[23:16] are used for IDSEL */
		paddress = (u32 *) (IXP2000_PCI_CFG0_VIRT_BASE
				    | (1 << (PCI_SLOT(devfn) + 16))
				    | (PCI_FUNC(devfn) << 8) | where);
	} else {
		paddress = (u32 *) (IXP2000_PCI_CFG1_VIRT_BASE 
				    | (bus_nr << 16)
				    | (PCI_SLOT(devfn) << 11)
				    | (PCI_FUNC(devfn) << 8) | where);
	}

	return paddress;
}

/*
 * Mask table, bits to mask for quantity of size 1, 2 or 4 bytes.
 * 0 and 3 are not valid indexes...
 */
static u32 bytemask[] = {
	/*0*/	0,
	/*1*/	0xff,
	/*2*/	0xffff,
	/*3*/	0,
	/*4*/	0xffffffff,
};


int ixp2000_pci_read_config(struct pci_bus *bus, unsigned int devfn, int where,
				int size, u32 *value)
{
	u32 n;
	u32 *addr;

	n = where % 4;

	addr = ixp2000_pci_config_addr(bus->number, devfn, where);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	pci_master_aborts = 0;
	*value = (*addr >> (8*n)) & bytemask[size];
	if (pci_master_aborts) {
		pci_master_aborts = 0;
		*value = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	return PCIBIOS_SUCCESSFUL;
}

/*
 * We don't do error checks by callling clear_master_aborts() b/c the
 * assumption is that the caller did a read first to make sure a device
 * exists.
 */
int ixp2000_pci_write_config(struct pci_bus *bus, unsigned int devfn, int where,
				int size, u32 value)
{
	u32 mask;
	u32 *addr;
	u32 temp;

	mask = ~(bytemask[size] << ((where % 0x4) * 8));
	addr = ixp2000_pci_config_addr(bus->number, devfn, where);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;
	temp = (u32) (value) << ((where % 0x4) * 8);
	*addr = (*addr & mask) | temp;

	clear_master_aborts();

	return PCIBIOS_SUCCESSFUL;
}


static struct pci_ops ixp2000_pci_ops = {
	.read	= ixp2000_pci_read_config,
	.write	= ixp2000_pci_write_config
};

struct pci_bus *ixp2000_pci_scan_bus(int nr, struct pci_sys_data *sysdata)
{
	return pci_scan_bus(sysdata->busnr, &ixp2000_pci_ops, sysdata);
}


int ixp2000_pci_abort_handler(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{

	volatile u32 temp;

	pci_master_aborts = 1;

	cli();
	temp = *(IXP2000_PCI_CONTROL);
	if (temp & ((1 << 8) | (1 << 5))) {
		*(IXP2000_PCI_CONTROL) = temp;
	}

	temp = *(IXP2000_PCI_CMDSTAT);
	if (temp & (1 << 29)) {
		while (temp & (1 << 29)) {	
			*(IXP2000_PCI_CMDSTAT) = temp;
			temp = *(IXP2000_PCI_CMDSTAT);
		}
	}
	sti();

	/*
	 * If it was an imprecise abort, then we need to correct the
	 * return address to be _after_ the instruction.
	 */
	if (fsr & (1 << 10))
		regs->ARM_pc += 4;

	return 0;
}

int
clear_master_aborts(void)
{
	volatile u32 temp;

	cli();
	temp = *(IXP2000_PCI_CONTROL);
	if (temp & ((1 << 8) | (1 << 5))) {	
		*(IXP2000_PCI_CONTROL) = temp;
	}

	temp = *(IXP2000_PCI_CMDSTAT);
	if (temp & (1 << 29)) {
		while (temp & (1 << 29)) {
			*(IXP2000_PCI_CMDSTAT) = temp;
			temp = *(IXP2000_PCI_CMDSTAT);
		}
	}
	sti();

	return 0;
}

void __init
ixp2000_pci_preinit(void)
{
	hook_fault_code(16+6, ixp2000_pci_abort_handler, SIGBUS,
				"PCI config cycle to non-existent device");
}


/*
 * IXP2000 systems often have large resource requirements, so we just
 * use our own resource space.
 */
static struct resource ixp2000_pci_mem_space = {
	.start	= 0x00000000,
	.end	= 0xffffffff,
	.flags	= IORESOURCE_MEM,
	.name	= "PCI Mem Space"
};

static struct resource ixp2000_pci_io_space = {
	.start	= 0x00000000,
	.end	= 0xffffffff,
	.flags	= IORESOURCE_IO,
	.name	= "PCI I/O Space"
};

int ixp2000_pci_setup(int nr, struct pci_sys_data *sys)
{
	if (nr >= 1)
		return 0;

	sys->resource[0] = &ixp2000_pci_io_space;
	sys->resource[1] = &ixp2000_pci_mem_space;
	sys->resource[2] = NULL;

	return 1;
}

