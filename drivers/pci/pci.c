/*
 *	$Id: pci.c,v 1.91 1999/01/21 13:34:01 davem Exp $
 *
 *	PCI Bus Services, see include/linux/pci.h for further explanation.
 *
 *	Copyright 1993 -- 1997 Drew Eckhardt, Frederic Potter,
 *	David Mosberger-Tang
 *
 *	Copyright 1997 -- 2000 Martin Mares <mj@suse.cz>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/ioport.h>

#include <asm/page.h>

#undef DEBUG

#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif

LIST_HEAD(pci_root_buses);
LIST_HEAD(pci_devices);

struct pci_dev *
pci_find_slot(unsigned int bus, unsigned int devfn)
{
	struct pci_dev *dev;

	pci_for_each_dev(dev) {
		if (dev->bus->number == bus && dev->devfn == devfn)
			return dev;
	}
	return NULL;
}


struct pci_dev *
pci_find_subsys(unsigned int vendor, unsigned int device,
		unsigned int ss_vendor, unsigned int ss_device,
		const struct pci_dev *from)
{
	struct list_head *n = from ? from->global_list.next : pci_devices.next;

	while (n != &pci_devices) {
		struct pci_dev *dev = pci_dev_g(n);
		if ((vendor == PCI_ANY_ID || dev->vendor == vendor) &&
		    (device == PCI_ANY_ID || dev->device == device) &&
		    (ss_vendor == PCI_ANY_ID || dev->subsystem_vendor == ss_vendor) &&
		    (ss_device == PCI_ANY_ID || dev->subsystem_device == ss_device))
			return dev;
		n = n->next;
	}
	return NULL;
}


struct pci_dev *
pci_find_device(unsigned int vendor, unsigned int device, const struct pci_dev *from)
{
	return pci_find_subsys(vendor, device, PCI_ANY_ID, PCI_ANY_ID, from);
}


struct pci_dev *
pci_find_class(unsigned int class, const struct pci_dev *from)
{
	struct list_head *n = from ? from->global_list.next : pci_devices.next;

	while (n != &pci_devices) {
		struct pci_dev *dev = pci_dev_g(n);
		if (dev->class == class)
			return dev;
		n = n->next;
	}
	return NULL;
}


int
pci_find_capability(struct pci_dev *dev, int cap)
{
	u16 status;
	u8 pos, id;
	int ttl = 48;

	pci_read_config_word(dev, PCI_STATUS, &status);
	if (!(status & PCI_STATUS_CAP_LIST))
		return 0;
	pci_read_config_byte(dev, PCI_CAPABILITY_LIST, &pos);
	while (ttl-- && pos >= 0x40) {
		pos &= ~3;
		pci_read_config_byte(dev, pos + PCI_CAP_LIST_ID, &id);
		if (id == 0xff)
			break;
		if (id == cap)
			return pos;
		pci_read_config_byte(dev, pos + PCI_CAP_LIST_NEXT, &pos);
	}
	return 0;
}


/*
 *  For given resource region of given device, return the resource
 *  region of parent bus the given region is contained in or where
 *  it should be allocated from.
 */
struct resource *
pci_find_parent_resource(const struct pci_dev *dev, struct resource *res)
{
	const struct pci_bus *bus = dev->bus;
	int i;
	struct resource *best = NULL;

	for(i=0; i<4; i++) {
		struct resource *r = bus->resource[i];
		if (!r)
			continue;
		if (res->start && !(res->start >= r->start && res->end <= r->end))
			continue;	/* Not contained */
		if ((res->flags ^ r->flags) & (IORESOURCE_IO | IORESOURCE_MEM))
			continue;	/* Wrong type */
		if (!((res->flags ^ r->flags) & IORESOURCE_PREFETCH))
			return r;	/* Exact match */
		if ((res->flags & IORESOURCE_PREFETCH) && !(r->flags & IORESOURCE_PREFETCH))
			best = r;	/* Approximating prefetchable by non-prefetchable */
	}
	return best;
}

/*
 *  Set power management state of a device.  For transitions from state D3
 *  it isn't as straightforward as one could assume since many devices forget
 *  their configuration space during wakeup.  Returns old power state.
 */
int
pci_set_power_state(struct pci_dev *dev, int new_state)
{
	u32 base[5], romaddr;
	u16 pci_command, pwr_command;
	u8  pci_latency, pci_cacheline;
	int i, old_state;
	int pm = pci_find_capability(dev, PCI_CAP_ID_PM);

	if (!pm)
		return 0;
	pci_read_config_word(dev, pm + PCI_PM_CTRL, &pwr_command);
	old_state = pwr_command & PCI_PM_CTRL_STATE_MASK;
	if (old_state == new_state)
		return old_state;
	DBG("PCI: %s goes from D%d to D%d\n", dev->slot_name, old_state, new_state);
	if (old_state == 3) {
		pci_read_config_word(dev, PCI_COMMAND, &pci_command);
		pci_write_config_word(dev, PCI_COMMAND, pci_command & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY));
		for (i = 0; i < 5; i++)
			pci_read_config_dword(dev, PCI_BASE_ADDRESS_0 + i*4, &base[i]);
		pci_read_config_dword(dev, PCI_ROM_ADDRESS, &romaddr);
		pci_read_config_byte(dev, PCI_LATENCY_TIMER, &pci_latency);
		pci_read_config_byte(dev, PCI_CACHE_LINE_SIZE, &pci_cacheline);
		pci_write_config_word(dev, pm + PCI_PM_CTRL, new_state);
		for (i = 0; i < 5; i++)
			pci_write_config_dword(dev, PCI_BASE_ADDRESS_0 + i*4, base[i]);
		pci_write_config_dword(dev, PCI_ROM_ADDRESS, romaddr);
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
		pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, pci_cacheline);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, pci_latency);
		pci_write_config_word(dev, PCI_COMMAND, pci_command);
	} else
		pci_write_config_word(dev, pm + PCI_PM_CTRL, (pwr_command & ~PCI_PM_CTRL_STATE_MASK) | new_state);
	return old_state;
}

/*
 *  Initialize device before it's used by a driver. Ask low-level code
 *  to enable I/O and memory. Wake up the device if it was suspended.
 *  Beware, this function can fail.
 */
int
pci_enable_device(struct pci_dev *dev)
{
	int err;

	if ((err = pcibios_enable_device(dev)) < 0)
		return err;
	pci_set_power_state(dev, 0);
	return 0;
}

/*
 *  Registration of PCI drivers and handling of hot-pluggable devices.
 */

static LIST_HEAD(pci_drivers);

const struct pci_device_id *
pci_match_device(const struct pci_device_id *ids, const struct pci_dev *dev)
{
	while (ids->vendor || ids->subvendor || ids->class_mask) {
		if ((ids->vendor == PCI_ANY_ID || ids->vendor == dev->vendor) &&
		    (ids->device == PCI_ANY_ID || ids->device == dev->device) &&
		    (ids->subvendor == PCI_ANY_ID || ids->subvendor == dev->subsystem_vendor) &&
		    (ids->subdevice == PCI_ANY_ID || ids->subdevice == dev->subsystem_device) &&
		    !((ids->class ^ dev->class) & ids->class_mask))
			return ids;
		ids++;
	}
	return NULL;
}

static int
pci_announce_device(struct pci_driver *drv, struct pci_dev *dev)
{
	const struct pci_device_id *id;

	if (drv->id_table) {
		id = pci_match_device(drv->id_table, dev);
		if (!id)
			return 0;
	} else
		id = NULL;
	if (drv->probe(dev, id) >= 0) {
		dev->driver = drv;
		return 1;
	}
	return 0;
}

int
pci_register_driver(struct pci_driver *drv)
{
	struct pci_dev *dev;
	int count = 0;

	list_add_tail(&drv->node, &pci_drivers);
	pci_for_each_dev(dev) {
		if (!pci_dev_driver(dev))
			count += pci_announce_device(drv, dev);
	}
	return count;
}

void
pci_unregister_driver(struct pci_driver *drv)
{
	struct pci_dev *dev;

	list_del(&drv->node);
	pci_for_each_dev(dev) {
		if (dev->driver == drv) {
			if (drv->remove)
				drv->remove(dev);
			dev->driver = NULL;
		}
	}
}

#ifdef CONFIG_HOTPLUG

void
pci_insert_device(struct pci_dev *dev, struct pci_bus *bus)
{
	struct list_head *ln;

	list_add_tail(&dev->bus_list, &bus->devices);
	list_add_tail(&dev->global_list, &pci_devices);
#ifdef CONFIG_PROC_FS
	pci_proc_attach_device(dev);
#endif
	for(ln=pci_drivers.next; ln != &pci_drivers; ln=ln->next) {
		struct pci_driver *drv = list_entry(ln, struct pci_driver, node);
		if (drv->remove && pci_announce_device(drv, dev))
			break;
	}
}

static void pci_free_resources(struct pci_dev *dev)
{
	int i;

	for (i = 0; i < PCI_NUM_RESOURCES; i++) {
		struct resource *res = dev->resource + i;
		if (res->parent)
			release_resource(res);
	}
}

void
pci_remove_device(struct pci_dev *dev)
{
	if (dev->driver->remove)
		dev->driver->remove(dev);
	dev->driver = NULL;
	list_del(&dev->bus_list);
	list_del(&dev->global_list);
	pci_free_resources(dev);
#ifdef CONFIG_PROC_FS
	pci_proc_detach_device(dev);
#endif
}

#endif

static struct pci_driver pci_compat_driver = {
	name: "compat"
};

struct pci_driver *
pci_dev_driver(const struct pci_dev *dev)
{
	if (dev->driver)
		return dev->driver;
	else {
		int i;
		for(i=0; i<=PCI_ROM_RESOURCE; i++)
			if (dev->resource[i].flags & IORESOURCE_BUSY)
				return &pci_compat_driver;
	}
	return NULL;
}


/*
 * This interrupt-safe spinlock protects all accesses to PCI
 * configuration space.
 */

static spinlock_t pci_lock = SPIN_LOCK_UNLOCKED;

/*
 *  Wrappers for all PCI configuration access functions.  They just check
 *  alignment, do locking and call the low-level functions pointed to
 *  by pci_dev->ops.
 */

#define PCI_byte_BAD 0
#define PCI_word_BAD (pos & 1)
#define PCI_dword_BAD (pos & 3)

#define PCI_OP(rw,size,type) \
int pci_##rw##_config_##size (struct pci_dev *dev, int pos, type value) \
{									\
	int res;							\
	unsigned long flags;						\
	if (PCI_##size##_BAD) return PCIBIOS_BAD_REGISTER_NUMBER;	\
	spin_lock_irqsave(&pci_lock, flags);				\
	res = dev->bus->ops->rw##_##size(dev, pos, value);		\
	spin_unlock_irqrestore(&pci_lock, flags);			\
	return res;							\
}

PCI_OP(read, byte, u8 *)
PCI_OP(read, word, u16 *)
PCI_OP(read, dword, u32 *)
PCI_OP(write, byte, u8)
PCI_OP(write, word, u16)
PCI_OP(write, dword, u32)


void
pci_set_master(struct pci_dev *dev)
{
	u16 cmd;
	u8 lat;

	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (! (cmd & PCI_COMMAND_MASTER)) {
		printk("PCI: Enabling bus mastering for device %s\n", dev->slot_name);
		cmd |= PCI_COMMAND_MASTER;
		pci_write_config_word(dev, PCI_COMMAND, cmd);
	}
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &lat);
	if (lat < 16) {
		printk("PCI: Increasing latency timer of device %s to 64\n", dev->slot_name);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
	}
}

/*
 * Translate the low bits of the PCI base
 * to the resource type
 */
static inline unsigned int pci_resource_flags(unsigned int flags)
{
	if (flags & PCI_BASE_ADDRESS_SPACE_IO)
		return IORESOURCE_IO;

	if (flags & PCI_BASE_ADDRESS_MEM_PREFETCH)
		return IORESOURCE_MEM | IORESOURCE_PREFETCH;

	return IORESOURCE_MEM;
}

static void pci_read_bases(struct pci_dev *dev, unsigned int howmany, int rom)
{
	unsigned int pos, reg, next;
	u32 l, sz, tmp;
	u16 cmd;
	struct resource *res;

	/* Disable IO and memory while we fiddle */
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	tmp = cmd & ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
	pci_write_config_word(dev, PCI_COMMAND, tmp);
	
	for(pos=0; pos<howmany; pos = next) {
		next = pos+1;
		res = &dev->resource[pos];
		res->name = dev->name;
		reg = PCI_BASE_ADDRESS_0 + (pos << 2);
		pci_read_config_dword(dev, reg, &l);
		pci_write_config_dword(dev, reg, ~0);
		pci_read_config_dword(dev, reg, &sz);
		pci_write_config_dword(dev, reg, l);
		if (!sz || sz == 0xffffffff)
			continue;
		if (l == 0xffffffff)
			l = 0;
		if ((l & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_MEMORY) {
			res->start = l & PCI_BASE_ADDRESS_MEM_MASK;
			sz = ~(sz & PCI_BASE_ADDRESS_MEM_MASK);
		} else {
			res->start = l & PCI_BASE_ADDRESS_IO_MASK;
			sz = ~(sz & PCI_BASE_ADDRESS_IO_MASK) & 0xffff;
		}
		res->end = res->start + (unsigned long) sz;
		res->flags |= (l & 0xf) | pci_resource_flags(l);
		if ((l & (PCI_BASE_ADDRESS_SPACE | PCI_BASE_ADDRESS_MEM_TYPE_MASK))
		    == (PCI_BASE_ADDRESS_SPACE_MEMORY | PCI_BASE_ADDRESS_MEM_TYPE_64)) {
			pci_read_config_dword(dev, reg+4, &l);
			next++;
#if BITS_PER_LONG == 64
			res->start |= ((unsigned long) l) << 32;
			res->end = res->start + sz;
			pci_write_config_dword(dev, reg+4, ~0);
			pci_read_config_dword(dev, reg+4, &tmp);
			pci_write_config_dword(dev, reg+4, l);
			if (l)
				res->end = res->start + (((unsigned long) ~l) << 32);
#else
			if (l) {
				printk(KERN_ERR "PCI: Unable to handle 64-bit address for device %s\n", dev->slot_name);
				res->start = 0;
				res->flags = 0;
				continue;
			}
#endif
		}
	}
	if (rom) {
		dev->rom_base_reg = rom;
		res = &dev->resource[PCI_ROM_RESOURCE];
		pci_read_config_dword(dev, rom, &l);
		pci_write_config_dword(dev, rom, ~PCI_ROM_ADDRESS_ENABLE);
		pci_read_config_dword(dev, rom, &sz);
		pci_write_config_dword(dev, rom, l);
		if (l == 0xffffffff)
			l = 0;
		if (sz && sz != 0xffffffff) {
			res->flags = (l & PCI_ROM_ADDRESS_ENABLE) |
			  IORESOURCE_MEM | IORESOURCE_PREFETCH | IORESOURCE_READONLY | IORESOURCE_CACHEABLE;
			res->start = l & PCI_ROM_ADDRESS_MASK;
			sz = ~(sz & PCI_ROM_ADDRESS_MASK);
			res->end = res->start + (unsigned long) sz;
		}
		res->name = dev->name;
	}
	pci_write_config_word(dev, PCI_COMMAND, cmd);
}

void __init pci_read_bridge_bases(struct pci_bus *child)
{
	struct pci_dev *dev = child->self;
	u8 io_base_lo, io_limit_lo;
	u16 mem_base_lo, mem_limit_lo, io_base_hi, io_limit_hi;
	u32 mem_base_hi, mem_limit_hi;
	unsigned long base, limit;
	struct resource *res;
	int i;

	if (!dev)		/* It's a host bus, nothing to read */
		return;

	for(i=0; i<3; i++)
		child->resource[i] = &dev->resource[PCI_BRIDGE_RESOURCES+i];

	res = child->resource[0];
	pci_read_config_byte(dev, PCI_IO_BASE, &io_base_lo);
	pci_read_config_byte(dev, PCI_IO_LIMIT, &io_limit_lo);
	pci_read_config_word(dev, PCI_IO_BASE_UPPER16, &io_base_hi);
	pci_read_config_word(dev, PCI_IO_LIMIT_UPPER16, &io_limit_hi);
	base = ((io_base_lo & PCI_IO_RANGE_MASK) << 8) | (io_base_hi << 16);
	limit = ((io_limit_lo & PCI_IO_RANGE_MASK) << 8) | (io_limit_hi << 16);
	if (base && base <= limit) {
		res->flags |= (io_base_lo & PCI_IO_RANGE_TYPE_MASK) | IORESOURCE_IO;
		res->start = base;
		res->end = limit + 0xfff;
		res->name = child->name;
	}

	res = child->resource[1];
	pci_read_config_word(dev, PCI_MEMORY_BASE, &mem_base_lo);
	pci_read_config_word(dev, PCI_MEMORY_LIMIT, &mem_limit_lo);
	base = (mem_base_lo & PCI_MEMORY_RANGE_MASK) << 16;
	limit = (mem_limit_lo & PCI_MEMORY_RANGE_MASK) << 16;
	if (base && base <= limit) {
		res->flags |= (mem_base_lo & PCI_MEMORY_RANGE_TYPE_MASK) | IORESOURCE_MEM;
		res->start = base;
		res->end = limit + 0xfffff;
		res->name = child->name;
	}

	res = child->resource[2];
	pci_read_config_word(dev, PCI_PREF_MEMORY_BASE, &mem_base_lo);
	pci_read_config_word(dev, PCI_PREF_MEMORY_LIMIT, &mem_limit_lo);
	pci_read_config_dword(dev, PCI_PREF_BASE_UPPER32, &mem_base_hi);
	pci_read_config_dword(dev, PCI_PREF_LIMIT_UPPER32, &mem_limit_hi);
	base = (mem_base_lo & PCI_MEMORY_RANGE_MASK) << 16;
	limit = (mem_limit_lo & PCI_MEMORY_RANGE_MASK) << 16;
#if BITS_PER_LONG == 64
	base |= ((long) mem_base_hi) << 32;
	limit |= ((long) mem_limit_hi) << 32;
#else
	if (mem_base_hi || mem_limit_hi) {
		printk(KERN_ERR "PCI: Unable to handle 64-bit address space for %s\n", child->name);
		return;
	}
#endif
	if (base && base <= limit) {
		res->flags |= (mem_base_lo & PCI_MEMORY_RANGE_TYPE_MASK) | IORESOURCE_MEM | IORESOURCE_PREFETCH;
		res->start = base;
		res->end = limit + 0xfffff;
		res->name = child->name;
	}
}

static struct pci_bus * __init pci_alloc_bus(void)
{
	struct pci_bus *b;

	b = kmalloc(sizeof(*b), GFP_KERNEL);
	if (b) {
		memset(b, 0, sizeof(*b));
		INIT_LIST_HEAD(&b->children);
		INIT_LIST_HEAD(&b->devices);
	}
	return b;
}

static struct pci_bus * __init pci_add_new_bus(struct pci_bus *parent, struct pci_dev *dev, int busnr)
{
	struct pci_bus *child;

	/*
	 * Allocate a new bus, and inherit stuff from the parent..
	 */
	child = pci_alloc_bus();

	list_add_tail(&child->node, &parent->children);
	child->self = dev;
	dev->subordinate = child;
	child->parent = parent;
	child->ops = parent->ops;
	child->sysdata = parent->sysdata;

	/*
	 * Set up the primary, secondary and subordinate
	 * bus numbers.
	 */
	child->number = child->secondary = busnr;
	child->primary = parent->secondary;
	child->subordinate = 0xff;

	return child;
}

/*
 * A CardBus bridge is basically the same as a regular PCI bridge,
 * except we don't scan behind it because it will be changing.
 */
static int __init pci_scan_cardbus(struct pci_bus *bus, struct pci_dev *dev, int busnr)
{
	int i;
	unsigned short cr;
	unsigned int buses;
	struct pci_bus *child;

	/*
	 * Insert it into the tree of buses.
	 */
	DBG("Scanning CardBus bridge %s\n", dev->slot_name);
	child = pci_add_new_bus(bus, dev, ++busnr);

	for (i = 0; i < 4; i++)
		child->resource[i] = &dev->resource[PCI_BRIDGE_RESOURCES+i];

	/*
	 * Maybe we'll have another bus behind this one?
	 */
	child->subordinate = ++busnr;
	sprintf(child->name, "PCI CardBus #%02x", child->number);

	/*
	 * Clear all status bits and turn off memory,
	 * I/O and master enables.
	 */
	pci_read_config_word(dev, PCI_COMMAND, &cr);
	pci_write_config_word(dev, PCI_COMMAND, 0x0000);
	pci_write_config_word(dev, PCI_STATUS, 0xffff);

	/*
	 * Read the existing primary/secondary/subordinate bus
	 * number configuration to determine if the bridge
	 * has already been configured by the system.  If so,
	 * do not modify the configuration, merely note it.
	 */
	pci_read_config_dword(dev, PCI_PRIMARY_BUS, &buses);
	if ((buses & 0xFFFFFF) != 0 && ! pcibios_assign_all_busses()) {
		child->primary = buses & 0xFF;
		child->secondary = (buses >> 8) & 0xFF;
		child->subordinate = (buses >> 16) & 0xFF;
		child->number = child->secondary;
		if (child->subordinate > busnr)
			busnr = child->subordinate;
	} else {
		/*
		 * Configure the bus numbers for this bridge:
		 */
		buses &= 0xff000000;
		buses |=
		      (((unsigned int)(child->primary)     <<  0) |
		       ((unsigned int)(child->secondary)   <<  8) |
		       ((unsigned int)(child->subordinate) << 16));
		pci_write_config_dword(dev, PCI_PRIMARY_BUS, buses);
	}
	pci_write_config_word(dev, PCI_COMMAND, cr);
	return busnr;
}

static unsigned int __init pci_do_scan_bus(struct pci_bus *bus);

/*
 * If it's a bridge, scan the bus behind it.
 */
static int __init pci_scan_bridge(struct pci_bus *bus, struct pci_dev * dev, int max)
{
	unsigned int buses;
	unsigned short cr;
	struct pci_bus *child;

	/*
	 * Insert it into the tree of buses.
	 */
	DBG("Scanning behind PCI bridge %s\n", dev->slot_name);
	child = pci_add_new_bus(bus, dev, ++max);
	sprintf(child->name, "PCI Bus #%02x", child->number);

	/*
	 * Clear all status bits and turn off memory,
	 * I/O and master enables.
	 */
	pci_read_config_word(dev, PCI_COMMAND, &cr);
	pci_write_config_word(dev, PCI_COMMAND, 0x0000);
	pci_write_config_word(dev, PCI_STATUS, 0xffff);

	/*
	 * Read the existing primary/secondary/subordinate bus
	 * number configuration to determine if the PCI bridge
	 * has already been configured by the system.  If so,
	 * do not modify the configuration, merely note it.
	 */
	pci_read_config_dword(dev, PCI_PRIMARY_BUS, &buses);
	if ((buses & 0xFFFFFF) != 0 && ! pcibios_assign_all_busses()) {
		unsigned int cmax;
		child->primary = buses & 0xFF;
		child->secondary = (buses >> 8) & 0xFF;
		child->subordinate = (buses >> 16) & 0xFF;
		child->number = child->secondary;
		cmax = pci_do_scan_bus(child);
		if (cmax > max) max = cmax;
	} else {
		/*
		 * Configure the bus numbers for this bridge:
		 */
		buses &= 0xff000000;
		buses |=
		      (((unsigned int)(child->primary)     <<  0) |
		       ((unsigned int)(child->secondary)   <<  8) |
		       ((unsigned int)(child->subordinate) << 16));
		pci_write_config_dword(dev, PCI_PRIMARY_BUS, buses);
		/*
		 * Now we can scan all subordinate buses:
		 */
		max = pci_do_scan_bus(child);
		/*
		 * Set the subordinate bus number to its real
		 * value:
		 */
		child->subordinate = max;
		buses = (buses & 0xff00ffff)
			| ((unsigned int)(child->subordinate) << 16);
		pci_write_config_dword(dev, PCI_PRIMARY_BUS, buses);
	}
	pci_write_config_word(dev, PCI_COMMAND, cr);
	return max;
}

/*
 * Read interrupt line and base address registers.
 * The architecture-dependent code can tweak these, of course.
 */
static void pci_read_irq(struct pci_dev *dev)
{
	unsigned char irq;

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &irq);
	if (irq)
		pci_read_config_byte(dev, PCI_INTERRUPT_LINE, &irq);
	dev->irq = irq;
}

/*
 * Fill in class and map information of a device
 */
int pci_setup_device(struct pci_dev * dev)
{
	u32 class;

	sprintf(dev->slot_name, "%02x:%02x.%d", dev->bus->number, PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	sprintf(dev->name, "PCI device %04x:%04x", dev->vendor, dev->device);
	
	pci_read_config_dword(dev, PCI_CLASS_REVISION, &class);
	class >>= 8;				    /* upper 3 bytes */
	dev->class = class;
	class >>= 8;

	DBG("Found %02x:%02x [%04x/%04x] %06x %02x\n", dev->bus->number, dev->devfn, dev->vendor, dev->device, class, dev->hdr_type);

	switch (dev->hdr_type) {		    /* header type */
	case PCI_HEADER_TYPE_NORMAL:		    /* standard header */
		if (class == PCI_CLASS_BRIDGE_PCI)
			goto bad;
		pci_read_irq(dev);
		pci_read_bases(dev, 6, PCI_ROM_ADDRESS);
		pci_read_config_word(dev, PCI_SUBSYSTEM_VENDOR_ID, &dev->subsystem_vendor);
		pci_read_config_word(dev, PCI_SUBSYSTEM_ID, &dev->subsystem_device);
		break;

	case PCI_HEADER_TYPE_BRIDGE:		    /* bridge header */
		if (class != PCI_CLASS_BRIDGE_PCI)
			goto bad;
		pci_read_bases(dev, 2, PCI_ROM_ADDRESS1);
		break;

	case PCI_HEADER_TYPE_CARDBUS:		    /* CardBus bridge header */
		if (class != PCI_CLASS_BRIDGE_CARDBUS)
			goto bad;
		pci_read_irq(dev);
		pci_read_bases(dev, 1, 0);
		pci_read_config_word(dev, PCI_CB_SUBSYSTEM_VENDOR_ID, &dev->subsystem_vendor);
		pci_read_config_word(dev, PCI_CB_SUBSYSTEM_ID, &dev->subsystem_device);
		break;

	default:				    /* unknown header */
		printk(KERN_ERR "PCI: device %s has unknown header type %02x, ignoring.\n",
			dev->slot_name, dev->hdr_type);
		return -1;

	bad:
		printk(KERN_ERR "PCI: %s: class %x doesn't match header type %02x. Ignoring class.\n",
		       dev->slot_name, class, dev->hdr_type);
		dev->class = PCI_CLASS_NOT_DEFINED;
	}

	/* We found a fine healthy device, go go go... */
	return 0;
}

/*
 * Read the config data for a PCI device, sanity-check it
 * and fill in the dev structure...
 */
static struct pci_dev * __init pci_scan_device(struct pci_dev *temp)
{
	struct pci_dev *dev;
	u32 l;

	if (pci_read_config_dword(temp, PCI_VENDOR_ID, &l))
		return NULL;

	/* some broken boards return 0 or ~0 if a slot is empty: */
	if (l == 0xffffffff || l == 0x00000000 || l == 0x0000ffff || l == 0xffff0000)
		return NULL;

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	memcpy(dev, temp, sizeof(*dev));
	dev->vendor = l & 0xffff;
	dev->device = (l >> 16) & 0xffff;
	if (pci_setup_device(dev) < 0) {
		kfree(dev);
		dev = NULL;
	}
	return dev;
}

struct pci_dev * __init pci_scan_slot(struct pci_dev *temp)
{
	struct pci_bus *bus = temp->bus;
	struct pci_dev *dev;
	struct pci_dev *first_dev = NULL;
	int func = 0;
	int is_multi = 0;
	u8 hdr_type;

	for (func = 0; func < 8; func++, temp->devfn++) {
		if (func && !is_multi)		/* not a multi-function device */
			continue;
		if (pci_read_config_byte(temp, PCI_HEADER_TYPE, &hdr_type))
			continue;
		temp->hdr_type = hdr_type & 0x7f;

		dev = pci_scan_device(temp);
		if (!dev)
			continue;
		pci_name_device(dev);
		if (!func) {
			is_multi = hdr_type & 0x80;
			first_dev = dev;
		}

		/*
		 * Link the device to both the global PCI device chain and
		 * the per-bus list of devices.
		 */
		list_add_tail(&dev->global_list, &pci_devices);
		list_add_tail(&dev->bus_list, &bus->devices);

		/* Fix up broken headers */
		pci_fixup_device(PCI_FIXUP_HEADER, dev);
	}
	return first_dev;
}

static unsigned int __init pci_do_scan_bus(struct pci_bus *bus)
{
	unsigned int devfn, max;
	struct list_head *ln;
	struct pci_dev *dev, dev0;

	DBG("Scanning bus %02x\n", bus->number);
	max = bus->secondary;

	/* Create a device template */
	memset(&dev0, 0, sizeof(dev0));
	dev0.bus = bus;
	dev0.sysdata = bus->sysdata;

	/* Go find them, Rover! */
	for (devfn = 0; devfn < 0x100; devfn += 8) {
		dev0.devfn = devfn;
		pci_scan_slot(&dev0);
	}

	/*
	 * After performing arch-dependent fixup of the bus, look behind
	 * all PCI-to-PCI bridges on this bus.
	 */
	DBG("Fixups for bus %02x\n", bus->number);
	pcibios_fixup_bus(bus);
	for (ln=bus->devices.next; ln != &bus->devices; ln=ln->next) {
		dev = pci_dev_b(ln);
		switch (dev->class >> 8) {
		case PCI_CLASS_BRIDGE_PCI:
			max = pci_scan_bridge(bus, dev, max);
			break;
		case PCI_CLASS_BRIDGE_CARDBUS:
			max = pci_scan_cardbus(bus, dev, max);
			break;
		}
	}

	/*
	 * We've scanned the bus and so we know all about what's on
	 * the other side of any bridges that may be on this bus plus
	 * any devices.
	 *
	 * Return how far we've got finding sub-buses.
	 */
	DBG("Bus scan for %02x returning with max=%02x\n", bus->number, max);
	return max;
}

static int __init pci_bus_exists(const struct list_head *list, int nr)
{
	const struct list_head *l;

	for(l=list->next; l != list; l = l->next) {
		const struct pci_bus *b = pci_bus_b(l);
		if (b->number == nr || pci_bus_exists(&b->children, nr))
			return 1;
	}
	return 0;
}

struct pci_bus * __init pci_alloc_primary_bus(int bus)
{
	struct pci_bus *b;

	if (pci_bus_exists(&pci_root_buses, bus)) {
		/* If we already got to this bus through a different bridge, ignore it */
		DBG("PCI: Bus %02x already known\n", bus);
		return NULL;
	}

	b = pci_alloc_bus();
	list_add_tail(&b->node, &pci_root_buses);

	b->number = b->secondary = bus;
	b->resource[0] = &ioport_resource;
	b->resource[1] = &iomem_resource;
	return b;
}

struct pci_bus * __init pci_scan_bus(int bus, struct pci_ops *ops, void *sysdata)
{
	struct pci_bus *b = pci_alloc_primary_bus(bus);
	if (b) {
		b->sysdata = sysdata;
		b->ops = ops;
		b->subordinate = pci_do_scan_bus(b);
	}
	return b;
}

void __init pci_init(void)
{
	struct pci_dev *dev;

	pcibios_init();

	pci_for_each_dev(dev) {
		pci_fixup_device(PCI_FIXUP_FINAL, dev);
	}
}

static int __init pci_setup(char *str)
{
	while (str) {
		char *k = strchr(str, ',');
		if (k)
			*k++ = 0;
		if (*str && (str = pcibios_setup(str)) && *str) {
			/* PCI layer options should be handled here */
			printk(KERN_ERR "PCI: Unknown option `%s'\n", str);
		}
		str = k;
	}
	return 1;
}

__setup("pci=", pci_setup);
