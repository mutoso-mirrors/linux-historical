#ifndef __ALPHA_PCI_H
#define __ALPHA_PCI_H

#include <asm/machvec.h>

/*
 * The following structure is used to manage multiple PCI busses.
 */

struct pci_bus;
struct resource;

struct pci_controler {
	/* Mandated.  */
	struct pci_controler *next;
        struct pci_bus *bus;
	struct resource *io_space;
	struct resource *mem_space;

	/* Alpha specific.  */
	unsigned long config_space;
	unsigned int index;
	unsigned int first_busno;
	unsigned int last_busno;
};

/* Override the logic in pci_scan_bus for skipping already-configured
   bus numbers.  */

#define pcibios_assign_all_busses()	1

#define PCIBIOS_MIN_IO		alpha_mv.min_io_address
#define PCIBIOS_MIN_MEM		alpha_mv.min_mem_address

#endif /* __ALPHA_PCI_H */

