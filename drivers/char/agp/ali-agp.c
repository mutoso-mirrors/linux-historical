/*
 * ALi AGPGART routines.
 */

#include <linux/types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/agp_backend.h>
#include "agp.h"

static int agp_try_unsupported __initdata = 0;

static int ali_fetch_size(void)
{
	int i;
	u32 temp;
	struct aper_size_info_32 *values;

	pci_read_config_dword(agp_bridge->dev, ALI_ATTBASE, &temp);
	temp &= ~(0xfffffff0);
	values = A_SIZE_32(agp_bridge->aperture_sizes);

	for (i = 0; i < agp_bridge->num_aperture_sizes; i++) {
		if (temp == values[i].size_value) {
			agp_bridge->previous_size =
			    agp_bridge->current_size = (void *) (values + i);
			agp_bridge->aperture_size_idx = i;
			return values[i].size;
		}
	}

	return 0;
}

static void ali_tlbflush(agp_memory * mem)
{
	u32 temp;

	pci_read_config_dword(agp_bridge->dev, ALI_TLBCTRL, &temp);
// clear tag
	pci_write_config_dword(agp_bridge->dev, ALI_TAGCTRL,
			((temp & 0xfffffff0) | 0x00000001|0x00000002));
}

static void ali_cleanup(void)
{
	struct aper_size_info_32 *previous_size;
	u32 temp;

	previous_size = A_SIZE_32(agp_bridge->previous_size);

	pci_read_config_dword(agp_bridge->dev, ALI_TLBCTRL, &temp);
// clear tag
	pci_write_config_dword(agp_bridge->dev, ALI_TAGCTRL,
			((temp & 0xffffff00) | 0x00000001|0x00000002));

	pci_read_config_dword(agp_bridge->dev,  ALI_ATTBASE, &temp);
	pci_write_config_dword(agp_bridge->dev, ALI_ATTBASE,
			((temp & 0x00000ff0) | previous_size->size_value));
}

static int ali_configure(void)
{
	u32 temp;
	struct aper_size_info_32 *current_size;

	current_size = A_SIZE_32(agp_bridge->current_size);

	/* aperture size and gatt addr */
	pci_read_config_dword(agp_bridge->dev, ALI_ATTBASE, &temp);
	temp = (((temp & 0x00000ff0) | (agp_bridge->gatt_bus_addr & 0xfffff000))
			| (current_size->size_value & 0xf));
	pci_write_config_dword(agp_bridge->dev, ALI_ATTBASE, temp);

	/* tlb control */
	pci_read_config_dword(agp_bridge->dev, ALI_TLBCTRL, &temp);
	pci_write_config_dword(agp_bridge->dev, ALI_TLBCTRL, ((temp & 0xffffff00) | 0x00000010));

	/* address to map to */
	pci_read_config_dword(agp_bridge->dev, ALI_APBASE, &temp);
	agp_bridge->gart_bus_addr = (temp & PCI_BASE_ADDRESS_MEM_MASK);

#if 0
	if (agp_bridge->type == ALI_M1541) {
		u32 nlvm_addr = 0;

		switch (current_size->size_value) {
			case 0:  break;
			case 1:  nlvm_addr = 0x100000;break;
			case 2:  nlvm_addr = 0x200000;break;
			case 3:  nlvm_addr = 0x400000;break;
			case 4:  nlvm_addr = 0x800000;break;
			case 6:  nlvm_addr = 0x1000000;break;
			case 7:  nlvm_addr = 0x2000000;break;
			case 8:  nlvm_addr = 0x4000000;break;
			case 9:  nlvm_addr = 0x8000000;break;
			case 10: nlvm_addr = 0x10000000;break;
			default: break;
		}
		nlvm_addr--;
		nlvm_addr&=0xfff00000;

		nlvm_addr+= agp_bridge->gart_bus_addr;
		nlvm_addr|=(agp_bridge->gart_bus_addr>>12);
		printk(KERN_INFO PFX "nlvm top &base = %8x\n",nlvm_addr);
	}
#endif

	pci_read_config_dword(agp_bridge->dev, ALI_TLBCTRL, &temp);
	temp &= 0xffffff7f;		//enable TLB
	pci_write_config_dword(agp_bridge->dev, ALI_TLBCTRL, temp);

	return 0;
}

static unsigned long ali_mask_memory(unsigned long addr, int type)
{
	/* Memory type is ignored */

	return addr | agp_bridge->masks[0].mask;
}

static void ali_cache_flush(void)
{
	global_cache_flush();

	if (agp_bridge->type == ALI_M1541) {
		int i, page_count;
		u32 temp;

		page_count = 1 << A_SIZE_32(agp_bridge->current_size)->page_order;
		for (i = 0; i < PAGE_SIZE * page_count; i += PAGE_SIZE) {
			pci_read_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL, &temp);
			pci_write_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL,
					(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
					  (agp_bridge->gatt_bus_addr + i)) |
					    ALI_CACHE_FLUSH_EN));
		}
	}
}

static void *ali_alloc_page(void)
{
	void *adr = agp_generic_alloc_page();
	u32 temp;

	if (adr == 0)
		return 0;

	if (agp_bridge->type == ALI_M1541) {
		pci_read_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL, &temp);
		pci_write_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL,
				(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
				  virt_to_phys(adr)) |
				    ALI_CACHE_FLUSH_EN ));
	}
	return adr;
}

static void ali_destroy_page(void * addr)
{
	u32 temp;

	if (addr == NULL)
		return;

	global_cache_flush();

	if (agp_bridge->type == ALI_M1541) {
		pci_read_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL, &temp);
		pci_write_config_dword(agp_bridge->dev, ALI_CACHE_FLUSH_CTRL,
				(((temp & ALI_CACHE_FLUSH_ADDR_MASK) |
				  virt_to_phys(addr)) |
				    ALI_CACHE_FLUSH_EN));
	}

	agp_generic_destroy_page(addr);
}

/* Setup function */
static struct gatt_mask ali_generic_masks[] =
{
	{.mask = 0x00000000, .type = 0}
};

static struct aper_size_info_32 ali_generic_sizes[7] =
{
	{256, 65536, 6, 10},
	{128, 32768, 5, 9},
	{64, 16384, 4, 8},
	{32, 8192, 3, 7},
	{16, 4096, 2, 6},
	{8, 2048, 1, 4},
	{4, 1024, 0, 3}
};

struct agp_bridge_data ali_generic_bridge = {
	.type			= ALI_GENERIC,
	.masks			= ali_generic_masks,
	.aperture_sizes		= (void *)ali_generic_sizes,
	.size_type		= U32_APER_SIZE,
	.num_aperture_sizes	= 7,
	.configure		= ali_configure,
	.fetch_size		= ali_fetch_size,
	.cleanup		= ali_cleanup,
	.tlb_flush		= ali_tlbflush,
	.mask_memory		= ali_mask_memory,
	.agp_enable		= agp_generic_enable,
	.cache_flush		= ali_cache_flush,
	.create_gatt_table	= agp_generic_create_gatt_table,
	.free_gatt_table	= agp_generic_free_gatt_table,
	.insert_memory		= agp_generic_insert_memory,
	.remove_memory		= agp_generic_remove_memory,
	.alloc_by_type		= agp_generic_alloc_by_type,
	.free_by_type		= agp_generic_free_by_type,
	.agp_alloc_page		= ali_alloc_page,
	.agp_destroy_page	= ali_destroy_page,
	.suspend		= agp_generic_suspend,
	.resume			= agp_generic_resume,
};

struct agp_device_ids ali_agp_device_ids[] __initdata =
{
	{
		.device_id	= PCI_DEVICE_ID_AL_M1541,
		.chipset	= ALI_M1541,
		.chipset_name	= "M1541",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1621,
		.chipset_name	= "M1621",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1631,
		.chipset_name	= "M1631",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1632,
		.chipset_name	= "M1632",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1641,
		.chipset_name	= "M1641",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1644,
		.chipset_name	= "M1644",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1647,
		.chipset_name	= "M1647",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1651,
		.chipset_name	= "M1651",
	},
	{
		.device_id	= PCI_DEVICE_ID_AL_M1671,
		.chipset_name	= "M1671",
	},
	{ }, /* dummy final entry, always present */
};

static struct agp_driver ali_agp_driver = {
	.owner = THIS_MODULE,
};

static int __init agp_ali_probe(struct pci_dev *pdev,
				const struct pci_device_id *ent)
{
	struct agp_device_ids *devs = ali_agp_device_ids;
	u8 hidden_1621_id, cap_ptr;
	int j;

	cap_ptr = pci_find_capability(pdev, PCI_CAP_ID_AGP);
	if (!cap_ptr)
		return -ENODEV;

	/* probe for known chipsets */
	for (j = 0; devs[j].chipset_name; j++) {
		if (pdev->device == devs[j].device_id)
			goto found;
	}

	if (!agp_try_unsupported) {
		printk(KERN_ERR PFX
		     "Unsupported ALi chipset (device id: %04x),"
		     " you might want to try agp_try_unsupported=1.\n",
		     pdev->device);
		return -ENODEV;
	}

	printk(KERN_WARNING PFX "Trying generic ALi routines"
	       " for device id: %04x\n", pdev->device);
	goto generic;

found:
	switch (pdev->device == PCI_DEVICE_ID_AL_M1621) {
	case PCI_DEVICE_ID_AL_M1541:
		ali_generic_bridge.type = ALI_M1541;
		break;
	case PCI_DEVICE_ID_AL_M1621:
		pci_read_config_byte(pdev, 0xFB, &hidden_1621_id);
		switch (hidden_1621_id) {
		case 0x31:
			devs[j].chipset_name = "M1631";
			break;
		case 0x32:
			devs[j].chipset_name = "M1632";
			break;
		case 0x41:
			devs[j].chipset_name = "M1641";
			break;
		case 0x43:
			break;
		case 0x47:
			devs[j].chipset_name = "M1647";
			break;
		case 0x51:
			devs[j].chipset_name = "M1651";
			break;
		default:
			break;
		}
	}

	printk(KERN_INFO PFX "Detected ALi %s chipset\n",
			devs[j].chipset_name);
generic:
	ali_generic_bridge.dev = pdev;
	ali_generic_bridge.capndx = cap_ptr;

	/* Fill in the mode register */
	pci_read_config_dword(pdev,
			ali_generic_bridge.capndx+PCI_AGP_STATUS,
			&ali_generic_bridge.mode);
	
	memcpy(agp_bridge, &ali_generic_bridge, sizeof(struct agp_bridge_data));

	ali_agp_driver.dev = pdev;
	agp_register_driver(&ali_agp_driver);
	return 0;
}

static struct pci_device_id agp_ali_pci_table[] __initdata = {
	{
	.class		= (PCI_CLASS_BRIDGE_HOST << 8),
	.class_mask	= ~0,
	.vendor		= PCI_VENDOR_ID_AL,
	.device		= PCI_ANY_ID,
	.subvendor	= PCI_ANY_ID,
	.subdevice	= PCI_ANY_ID,
	},
	{ }
};

MODULE_DEVICE_TABLE(pci, agp_ali_pci_table);

static struct __initdata pci_driver agp_ali_pci_driver = {
	.name		= "agpgart-ali",
	.id_table	= agp_ali_pci_table,
	.probe		= agp_ali_probe,
};

static int __init agp_ali_init(void)
{
	return pci_module_init(&agp_ali_pci_driver);
}

static void __exit agp_ali_cleanup(void)
{
	agp_unregister_driver(&ali_agp_driver);
	pci_unregister_driver(&agp_ali_pci_driver);
}

module_init(agp_ali_init);
module_exit(agp_ali_cleanup);

MODULE_PARM(agp_try_unsupported, "1i");
MODULE_AUTHOR("Dave Jones <davej@codemonkey.org.uk>");
MODULE_LICENSE("GPL and additional rights");

