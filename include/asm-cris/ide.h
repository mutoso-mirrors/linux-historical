/*
 *  linux/include/asm-cris/ide.h
 *
 *  Copyright (C) 2000  Axis Communications AB
 *
 *  Authors:    Bjorn Wesen
 *
 */

/*
 *  This file contains the ETRAX 100LX specific IDE code.
 */

#ifndef __ASMCRIS_IDE_H
#define __ASMCRIS_IDE_H

#ifdef __KERNEL__

#include <asm/svinto.h>

/* ETRAX 100 can support 4 IDE busses on the same pins (serialized) */

#define MAX_HWIFS	4

#define ide__sti()	__sti()

static __inline__ int ide_default_irq(ide_ioreg_t base)
{
	/* all IDE busses share the same IRQ, number 4.
	 * this has the side-effect that ide-probe.c will cluster our 4 interfaces
	 * together in a hwgroup, and will serialize accesses. this is good, because
	 * we can't access more than one interface at the same time on ETRAX100.
	 */
	return 4; 
}

static __inline__ ide_ioreg_t ide_default_io_base(int index)
{
	/* we have no real I/O base address per interface, since all go through the
	 * same register. but in a bitfield in that register, we have the i/f number.
	 * so we can use the io_base to remember that bitfield.
	 */
	static const unsigned long io_bases[MAX_HWIFS] = {
		IO_FIELD(R_ATA_CTRL_DATA, sel, 0),
		IO_FIELD(R_ATA_CTRL_DATA, sel, 1),
		IO_FIELD(R_ATA_CTRL_DATA, sel, 2),
		IO_FIELD(R_ATA_CTRL_DATA, sel, 3)
	};
	return io_bases[index];
}

/* this is called once for each interface, to setup the port addresses. data_port is the result
 * of the ide_default_io_base call above. ctrl_port will be 0, but that is don't care for us.
 */

static __inline__ void ide_init_hwif_ports(hw_regs_t *hw, ide_ioreg_t data_port, ide_ioreg_t ctrl_port, int *irq)
{
	int i;

	/* fill in ports for ATA addresses 0 to 7 */

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = data_port | 
			IO_FIELD(R_ATA_CTRL_DATA, addr, i) | 
			IO_STATE(R_ATA_CTRL_DATA, cs0, active);
	}

	/* the IDE control register is at ATA address 6, with CS1 active instead of CS0 */

	hw->io_ports[IDE_CONTROL_OFFSET] = data_port |
			IO_FIELD(R_ATA_CTRL_DATA, addr, 6) | 
			IO_STATE(R_ATA_CTRL_DATA, cs1, active);

	/* whats this for ? */

	hw->io_ports[IDE_IRQ_OFFSET] = 0;
}

static __inline__ void ide_init_default_hwifs(void)
{
	hw_regs_t hw;
	int index;

	for(index = 0; index < MAX_HWIFS; index++) {
		ide_init_hwif_ports(&hw, ide_default_io_base(index), 0, NULL);
		hw.irq = ide_default_irq(ide_default_io_base(index));
		ide_register_hw(&hw);
	}
}

/* some configuration options we don't need */

#undef SUPPORT_SLOW_DATA_PORTS
#define SUPPORT_SLOW_DATA_PORTS	0

/* the drive addressing is done through a controller register on the Etrax CPU */
void OUT_BYTE(unsigned char data, ide_ioreg_t reg);
unsigned char IN_BYTE(ide_ioreg_t reg);

/* this tells ide.h not to define the standard macros */
#define HAVE_ARCH_IN_OUT	1

#endif /* __KERNEL__ */

#endif /* __ASMCRIS_IDE_H */
