/*
 * pci.c - Low-Level PCI Access in IA64
 * 
 * Derived from bios32.c of i386 tree.
 *
 */

#include <linux/config.h>

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/smp_lock.h>
#include <linux/spinlock.h>

#include <asm/machvec.h>
#include <asm/page.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

#include <asm/sal.h>


#ifdef CONFIG_SMP
# include <asm/smp.h>
#endif
#include <asm/irq.h>


#undef DEBUG
#define DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */

spinlock_t pci_lock = SPIN_LOCK_UNLOCKED;

struct pci_fixup pcibios_fixups[] = { { 0 } };

#define PCI_NO_CHECKS		0x400
#define PCI_NO_PEER_FIXUP	0x800

static unsigned int pci_probe = PCI_NO_CHECKS;

/* Macro to build a PCI configuration address to be passed as a parameter to SAL. */

#define PCI_CONFIG_ADDRESS(dev, where) (((u64) dev->bus->number << 16) | ((u64) (dev->devfn & 0xff) << 8) | (where & 0xff))

static int 
pci_conf_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	s64 status;
	u64 lval;

	status = ia64_sal_pci_config_read(PCI_CONFIG_ADDRESS(dev, where), 1, &lval);
	*value = lval;
	return status;
}

static int 
pci_conf_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	s64 status;
	u64 lval;

	status = ia64_sal_pci_config_read(PCI_CONFIG_ADDRESS(dev, where), 2, &lval);
	*value = lval;
	return status;
}

static int 
pci_conf_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	s64 status;
	u64 lval;

	status = ia64_sal_pci_config_read(PCI_CONFIG_ADDRESS(dev, where), 4, &lval);
	*value = lval;
	return status;
}

static int 
pci_conf_write_config_byte (struct pci_dev *dev, int where, u8 value)
{
	return ia64_sal_pci_config_write(PCI_CONFIG_ADDRESS(dev, where), 1, value);
}

static int 
pci_conf_write_config_word (struct pci_dev *dev, int where, u16 value)
{
	return ia64_sal_pci_config_write(PCI_CONFIG_ADDRESS(dev, where), 2, value);
}

static int 
pci_conf_write_config_dword (struct pci_dev *dev, int where, u32 value)
{
	return ia64_sal_pci_config_write(PCI_CONFIG_ADDRESS(dev, where), 4, value);
}


static struct pci_ops pci_conf = {
      pci_conf_read_config_byte,
      pci_conf_read_config_word,
      pci_conf_read_config_dword,
      pci_conf_write_config_byte,
      pci_conf_write_config_word,
      pci_conf_write_config_dword
};

/*
 * Try to find PCI BIOS.  This will always work for IA64.
 */

static struct pci_ops * __init
pci_find_bios(void)
{
	return &pci_conf;
}

/*
 * Initialization. Uses the SAL interface
 */

#define PCI_BUSSES_TO_SCAN 2	/* On "real" ;) hardware this will be 255 */

void __init 
pcibios_init(void)
{
	struct pci_ops *ops = NULL;
	int i;

	if ((ops = pci_find_bios()) == NULL) {
		printk("PCI: No PCI bus detected\n");
		return;
	}

	printk("PCI: Probing PCI hardware\n");
	for (i = 0; i < PCI_BUSSES_TO_SCAN; i++) 
		pci_scan_bus(i, ops, NULL);
	platform_pci_fixup();
	return;
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */

void __init
pcibios_fixup_bus(struct pci_bus *b)
{
	return;
}

void __init
pcibios_update_resource(struct pci_dev *dev, struct resource *root,
			struct resource *res, int resource)
{
        unsigned long where, size;
        u32 reg;

        where = PCI_BASE_ADDRESS_0 + (resource * 4);
        size = res->end - res->start;
        pci_read_config_dword(dev, where, &reg);
        reg = (reg & size) | (((u32)(res->start - root->start)) & ~size);
        pci_write_config_dword(dev, where, reg);

	/* ??? FIXME -- record old value for shutdown.  */
}

void __init
pcibios_update_irq(struct pci_dev *dev, int irq)
{
	pci_write_config_byte(dev, PCI_INTERRUPT_LINE, irq);

	/* ??? FIXME -- record old value for shutdown.  */
}

void __init
pcibios_fixup_pbus_ranges (struct pci_bus * bus, struct pbus_set_ranges_data * ranges)
{
	ranges->io_start -= bus->resource[0]->start;
	ranges->io_end -= bus->resource[0]->start;
	ranges->mem_start -= bus->resource[1]->start;
	ranges->mem_end -= bus->resource[1]->start;
}

int __init
pcibios_enable_device (struct pci_dev *dev)
{
	/* Not needed, since we enable all devices at startup.  */
	return 0;
}

/*
 * PCI BIOS setup, always defaults to SAL interface
 */

char * __init 
pcibios_setup(char *str)
{
	pci_probe =  PCI_NO_CHECKS;
	return NULL;
}

void
pcibios_align_resource (void *data, struct resource *res, unsigned long size)
{
}
