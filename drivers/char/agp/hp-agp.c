/*
 * HP AGPGART routines.
 *	Copyright (C) 2002-2003 Hewlett-Packard Co
 *		Bjorn Helgaas <bjorn_helgaas@hp.com>
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>

#include <asm/acpi-ext.h>

#include "agp.h"

#ifndef log2
#define log2(x)		ffz(~(x))
#endif

#define HP_ZX1_IOC_OFFSET	0x1000  /* ACPI reports SBA, we want IOC */

/* HP ZX1 IOC registers */
#define HP_ZX1_IBASE		0x300
#define HP_ZX1_IMASK		0x308
#define HP_ZX1_PCOM		0x310
#define HP_ZX1_TCNFG		0x318
#define HP_ZX1_PDIR_BASE	0x320

/* HP ZX1 LBA registers */
#define HP_ZX1_AGP_STATUS	0x64
#define HP_ZX1_AGP_COMMAND	0x68

#define HP_ZX1_IOVA_BASE	GB(1UL)
#define HP_ZX1_IOVA_SIZE	GB(1UL)
#define HP_ZX1_GART_SIZE	(HP_ZX1_IOVA_SIZE / 2)
#define HP_ZX1_SBA_IOMMU_COOKIE	0x0000badbadc0ffeeUL

#define HP_ZX1_PDIR_VALID_BIT	0x8000000000000000UL
#define HP_ZX1_IOVA_TO_PDIR(va)	((va - hp_private.iova_base) >> hp_private.io_tlb_shift)

/* AGP bridge need not be PCI device, but DRM thinks it is. */
static struct pci_dev fake_bridge_dev;

static struct aper_size_info_fixed hp_zx1_sizes[] =
{
	{0, 0, 0},		/* filled in by hp_zx1_fetch_size() */
};

static struct gatt_mask hp_zx1_masks[] =
{
	{.mask = HP_ZX1_PDIR_VALID_BIT, .type = 0}
};

static struct _hp_private {
	volatile u8 *ioc_regs;
	volatile u8 *lba_regs;
	u64 *io_pdir;		// PDIR for entire IOVA
	u64 *gatt;		// PDIR just for GART (subset of above)
	u64 gatt_entries;
	u64 iova_base;
	u64 gart_base;
	u64 gart_size;
	u64 io_pdir_size;
	int io_pdir_owner;	// do we own it, or share it with sba_iommu?
	int io_page_size;
	int io_tlb_shift;
	int io_tlb_ps;		// IOC ps config
	int io_pages_per_kpage;
} hp_private;

static int __init hp_zx1_ioc_shared(void)
{
	struct _hp_private *hp = &hp_private;

	printk(KERN_INFO PFX "HP ZX1 IOC: IOPDIR shared with sba_iommu\n");

	/*
	 * IOC already configured by sba_iommu module; just use
	 * its setup.  We assume:
	 * 	- IOVA space is 1Gb in size
	 * 	- first 512Mb is IOMMU, second 512Mb is GART
	 */
	hp->io_tlb_ps = INREG64(hp->ioc_regs, HP_ZX1_TCNFG);
	switch (hp->io_tlb_ps) {
		case 0: hp->io_tlb_shift = 12; break;
		case 1: hp->io_tlb_shift = 13; break;
		case 2: hp->io_tlb_shift = 14; break;
		case 3: hp->io_tlb_shift = 16; break;
		default:
			printk(KERN_ERR PFX "Invalid IOTLB page size "
			       "configuration 0x%x\n", hp->io_tlb_ps);
			hp->gatt = 0;
			hp->gatt_entries = 0;
			return -ENODEV;
	}
	hp->io_page_size = 1 << hp->io_tlb_shift;
	hp->io_pages_per_kpage = PAGE_SIZE / hp->io_page_size;

	hp->iova_base = INREG64(hp->ioc_regs, HP_ZX1_IBASE) & ~0x1;
	hp->gart_base = hp->iova_base + HP_ZX1_IOVA_SIZE - HP_ZX1_GART_SIZE;

	hp->gart_size = HP_ZX1_GART_SIZE;
	hp->gatt_entries = hp->gart_size / hp->io_page_size;

	hp->io_pdir = phys_to_virt(INREG64(hp->ioc_regs, HP_ZX1_PDIR_BASE));
	hp->gatt = &hp->io_pdir[HP_ZX1_IOVA_TO_PDIR(hp->gart_base)];

	if (hp->gatt[0] != HP_ZX1_SBA_IOMMU_COOKIE) {
	    	hp->gatt = 0;
		hp->gatt_entries = 0;
		printk(KERN_ERR PFX "No reserved IO PDIR entry found; "
		       "GART disabled\n");
		return -ENODEV;
	}

	return 0;
}

static int __init
hp_zx1_ioc_owner (void)
{
	struct _hp_private *hp = &hp_private;

	printk(KERN_INFO PFX "HP ZX1 IOC: IOPDIR dedicated to GART\n");

	/*
	 * Select an IOV page size no larger than system page size.
	 */
	if (PAGE_SIZE >= KB(64)) {
		hp->io_tlb_shift = 16;
		hp->io_tlb_ps = 3;
	} else if (PAGE_SIZE >= KB(16)) {
		hp->io_tlb_shift = 14;
		hp->io_tlb_ps = 2;
	} else if (PAGE_SIZE >= KB(8)) {
		hp->io_tlb_shift = 13;
		hp->io_tlb_ps = 1;
	} else {
		hp->io_tlb_shift = 12;
		hp->io_tlb_ps = 0;
	}
	hp->io_page_size = 1 << hp->io_tlb_shift;
	hp->io_pages_per_kpage = PAGE_SIZE / hp->io_page_size;

	hp->iova_base = HP_ZX1_IOVA_BASE;
	hp->gart_size = HP_ZX1_GART_SIZE;
	hp->gart_base = hp->iova_base + HP_ZX1_IOVA_SIZE - hp->gart_size;

	hp->gatt_entries = hp->gart_size / hp->io_page_size;
	hp->io_pdir_size = (HP_ZX1_IOVA_SIZE / hp->io_page_size) * sizeof(u64);

	return 0;
}

static int __init
hp_zx1_ioc_init (u64 ioc_hpa, u64 lba_hpa)
{
	struct _hp_private *hp = &hp_private;

	hp->ioc_regs = ioremap(ioc_hpa, 1024);
	hp->lba_regs = ioremap(lba_hpa, 256);

	/*
	 * If the IOTLB is currently disabled, we can take it over.
	 * Otherwise, we have to share with sba_iommu.
	 */
	hp->io_pdir_owner = (INREG64(hp->ioc_regs, HP_ZX1_IBASE) & 0x1) == 0;

	if (hp->io_pdir_owner)
		return hp_zx1_ioc_owner();

	return hp_zx1_ioc_shared();
}

static int
hp_zx1_fetch_size(void)
{
	int size;

	size = hp_private.gart_size / MB(1);
	hp_zx1_sizes[0].size = size;
	agp_bridge->current_size = (void *) &hp_zx1_sizes[0];
	return size;
}

static int
hp_zx1_configure (void)
{
	struct _hp_private *hp = &hp_private;

	agp_bridge->gart_bus_addr = hp->gart_base;
#if 0
	/* ouch!! can't do that with a non-PCI AGP bridge... */
	agp_bridge->capndx = pci_find_capability(agp_bridge->dev, PCI_CAP_ID_AGP);
#else
	agp_bridge->capndx = 0;
#endif
	agp_bridge->mode = INREG32(hp->lba_regs, HP_ZX1_AGP_STATUS);

	if (hp->io_pdir_owner) {
		OUTREG64(hp->ioc_regs, HP_ZX1_PDIR_BASE, virt_to_phys(hp->io_pdir));
		OUTREG64(hp->ioc_regs, HP_ZX1_TCNFG, hp->io_tlb_ps);
		OUTREG64(hp->ioc_regs, HP_ZX1_IMASK, ~(HP_ZX1_IOVA_SIZE - 1));
		OUTREG64(hp->ioc_regs, HP_ZX1_IBASE, hp->iova_base | 0x1);
		OUTREG64(hp->ioc_regs, HP_ZX1_PCOM, hp->iova_base | log2(HP_ZX1_IOVA_SIZE));
		INREG64(hp->ioc_regs, HP_ZX1_PCOM);
	}

	return 0;
}

static void
hp_zx1_cleanup (void)
{
	struct _hp_private *hp = &hp_private;

	if (hp->io_pdir_owner)
		OUTREG64(hp->ioc_regs, HP_ZX1_IBASE, 0);
	iounmap((void *) hp->ioc_regs);
}

static void
hp_zx1_tlbflush (struct agp_memory *mem)
{
	struct _hp_private *hp = &hp_private;

	OUTREG64(hp->ioc_regs, HP_ZX1_PCOM, hp->gart_base | log2(hp->gart_size));
	INREG64(hp->ioc_regs, HP_ZX1_PCOM);
}

static int
hp_zx1_create_gatt_table (void)
{
	struct _hp_private *hp = &hp_private;
	int i;

	if (hp->io_pdir_owner) {
		hp->io_pdir = (u64 *) __get_free_pages(GFP_KERNEL,
						get_order(hp->io_pdir_size));
		if (!hp->io_pdir) {
			printk(KERN_ERR PFX "Couldn't allocate contiguous "
				"memory for I/O PDIR\n");
			hp->gatt = 0;
			hp->gatt_entries = 0;
			return -ENOMEM;
		}
		memset(hp->io_pdir, 0, hp->io_pdir_size);

		hp->gatt = &hp->io_pdir[HP_ZX1_IOVA_TO_PDIR(hp->gart_base)];
	}

	for (i = 0; i < hp->gatt_entries; i++) {
		hp->gatt[i] = (unsigned long) agp_bridge->scratch_page;
	}

	return 0;
}

static int
hp_zx1_free_gatt_table (void)
{
	struct _hp_private *hp = &hp_private;

	if (hp->io_pdir_owner)
		free_pages((unsigned long) hp->io_pdir,
			    get_order(hp->io_pdir_size));
	else
		hp->gatt[0] = HP_ZX1_SBA_IOMMU_COOKIE;
	return 0;
}

static int
hp_zx1_insert_memory (struct agp_memory *mem, off_t pg_start, int type)
{
	struct _hp_private *hp = &hp_private;
	int i, k;
	off_t j, io_pg_start;
	int io_pg_count;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	io_pg_start = hp->io_pages_per_kpage * pg_start;
	io_pg_count = hp->io_pages_per_kpage * mem->page_count;
	if ((io_pg_start + io_pg_count) > hp->gatt_entries) {
		return -EINVAL;
	}

	j = io_pg_start;
	while (j < (io_pg_start + io_pg_count)) {
		if (hp->gatt[j]) {
			return -EBUSY;
		}
		j++;
	}

	if (mem->is_flushed == FALSE) {
		global_cache_flush();
		mem->is_flushed = TRUE;
	}

	for (i = 0, j = io_pg_start; i < mem->page_count; i++) {
		unsigned long paddr;

		paddr = mem->memory[i];
		for (k = 0;
		     k < hp->io_pages_per_kpage;
		     k++, j++, paddr += hp->io_page_size) {
			hp->gatt[j] = agp_bridge->driver->mask_memory(paddr, type);
		}
	}

	agp_bridge->driver->tlb_flush(mem);
	return 0;
}

static int
hp_zx1_remove_memory (struct agp_memory *mem, off_t pg_start, int type)
{
	struct _hp_private *hp = &hp_private;
	int i, io_pg_start, io_pg_count;

	if (type != 0 || mem->type != 0) {
		return -EINVAL;
	}

	io_pg_start = hp->io_pages_per_kpage * pg_start;
	io_pg_count = hp->io_pages_per_kpage * mem->page_count;
	for (i = io_pg_start; i < io_pg_count + io_pg_start; i++) {
		hp->gatt[i] = agp_bridge->scratch_page;
	}

	agp_bridge->driver->tlb_flush(mem);
	return 0;
}

static unsigned long
hp_zx1_mask_memory (unsigned long addr, int type)
{
	return HP_ZX1_PDIR_VALID_BIT | addr;
}

static void
hp_zx1_enable (u32 mode)
{
	struct _hp_private *hp = &hp_private;
	u32 command;

	command = INREG32(hp->lba_regs, HP_ZX1_AGP_STATUS);

	command = agp_collect_device_status(mode, command);
	command |= 0x00000100;

	OUTREG32(hp->lba_regs, HP_ZX1_AGP_COMMAND, command);

	agp_device_command(command, 0);
}

struct agp_bridge_driver hp_zx1_driver = {
	.owner			= THIS_MODULE,
	.size_type		= FIXED_APER_SIZE,
	.configure		= hp_zx1_configure,
	.fetch_size		= hp_zx1_fetch_size,
	.cleanup		= hp_zx1_cleanup,
	.tlb_flush		= hp_zx1_tlbflush,
	.mask_memory		= hp_zx1_mask_memory,
	.masks			= hp_zx1_masks,
	.agp_enable		= hp_zx1_enable,
	.cache_flush		= global_cache_flush,
	.create_gatt_table	= hp_zx1_create_gatt_table,
	.free_gatt_table	= hp_zx1_free_gatt_table,
	.insert_memory		= hp_zx1_insert_memory,
	.remove_memory		= hp_zx1_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= agp_generic_alloc_page,
	.agp_destroy_page	= agp_generic_destroy_page,
	.cant_use_aperture	= 1,
};

static int __init
hp_zx1_setup (u64 ioc_hpa, u64 lba_hpa)
{
	struct agp_bridge_data *bridge;
	int error;

	printk(KERN_INFO PFX "Detected HP ZX1 AGP chipset (ioc=%lx, lba=%lx)\n", ioc_hpa, lba_hpa);

	error = hp_zx1_ioc_init(ioc_hpa, lba_hpa);
	if (error)
		return error;

	bridge = agp_alloc_bridge();
	if (!bridge)
		return -ENOMEM;
	bridge->driver = &hp_zx1_driver;

	fake_bridge_dev.vendor = PCI_VENDOR_ID_HP;
	fake_bridge_dev.device = PCI_DEVICE_ID_HP_ZX1_LBA;
	bridge->dev = &fake_bridge_dev;

	return agp_add_bridge(bridge);
}

static acpi_status __init
zx1_gart_probe (acpi_handle obj, u32 depth, void *context, void **ret)
{
	acpi_handle handle, parent;
	acpi_status status;
	struct acpi_buffer buffer;
	struct acpi_device_info *info;
	u64 lba_hpa, sba_hpa, length;
	int match;

	status = hp_acpi_csr_space(obj, &lba_hpa, &length);
	if (ACPI_FAILURE(status))
		return 1;

	/* Look for an enclosing IOC scope and find its CSR space */
	handle = obj;
	do {
		buffer.length = ACPI_ALLOCATE_LOCAL_BUFFER;
		status = acpi_get_object_info(handle, &buffer);
		if (ACPI_SUCCESS(status)) {
			/* TBD check _CID also */
			info = buffer.pointer;
			info->hardware_id.value[sizeof(info->hardware_id)-1] = '\0';
			match = (strcmp(info->hardware_id.value, "HWP0001") == 0);
			ACPI_MEM_FREE(info);
			if (match) {
				status = hp_acpi_csr_space(handle, &sba_hpa, &length);
				if (ACPI_SUCCESS(status))
					break;
				else {
					printk(KERN_ERR PFX "Detected HP ZX1 "
					       "AGP LBA but no IOC.\n");
					return status;
				}
			}
		}

		status = acpi_get_parent(handle, &parent);
		handle = parent;
	} while (ACPI_SUCCESS(status));

	if (hp_zx1_setup(sba_hpa + HP_ZX1_IOC_OFFSET, lba_hpa))
		return 1;
	return 0;
}

static int __init
agp_hp_init (void)
{
	acpi_status status;

	status = acpi_get_devices("HWP0003", zx1_gart_probe, "HWP0003 AGP LBA", NULL);
	if (!(ACPI_SUCCESS(status))) {
		agp_bridge->type = NOT_SUPPORTED;
		printk(KERN_INFO PFX "Failed to initialize zx1 AGP.\n");
		return -ENODEV;
	}
	return 0;
}

static void __exit
agp_hp_cleanup (void)
{
}

module_init(agp_hp_init);
module_exit(agp_hp_cleanup);

MODULE_LICENSE("GPL and additional rights");
