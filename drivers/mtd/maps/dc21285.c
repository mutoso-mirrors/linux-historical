/*
 * MTD map driver for flash on the DC21285 (the StrongARM-110 companion chip)
 *
 * (C) 2000  Nicolas Pitre <nico@cam.org>
 *
 * This code is GPL
 * 
 * $Id: dc21285.c,v 1.21 2004/09/16 23:27:13 gleixner Exp $
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/map.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/hardware/dec21285.h>
#include <asm/mach-types.h>


static struct mtd_info *dc21285_mtd;

#ifdef CONFIG_ARCH_NETWINDER
/* 
 * This is really ugly, but it seams to be the only
 * realiable way to do it, as the cpld state machine 
 * is unpredictible. So we have a 25us penalty per
 * write access.
 */
static void nw_en_write(void) {
	extern spinlock_t gpio_lock;
	unsigned long flags;

	/*
	 * we want to write a bit pattern XXX1 to Xilinx to enable
	 * the write gate, which will be open for about the next 2ms.
	 */
	spin_lock_irqsave(&gpio_lock, flags);
	cpld_modify(1, 1);
	spin_unlock_irqrestore(&gpio_lock, flags);

	/*
	 * let the ISA bus to catch on...
	 */
	udelay(25);
}
#else
#define nw_en_write() do { } while (0)
#endif

static map_word dc21285_read8(struct map_info *map, unsigned long ofs)
{
	return *(uint8_t*)(map->map_priv_1 + ofs);
}

static map_word dc21285_read16(struct map_info *map, unsigned long ofs)
{
	return *(uint16_t*)(map->map_priv_1 + ofs);
}

static map_word dc21285_read32(struct map_info *map, unsigned long ofs)
{
	return *(uint32_t*)(map->map_priv_1 + ofs);
}

static void dc21285_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	memcpy(to, (void*)(map->map_priv_1 + from), len);
}

static void dc21285_write(struct map_info *map, map_word d, unsigned long adr)
{
	if (machine_is_netwinder())
		nw_en_write();
	*CSR_ROMWRITEREG = adr & 3;
	adr &= ~3;
	*(uint8_t*)(map->map_priv_1 + adr) = d.x[0];
}

static void dc21285_write16(struct map_info *map, map_word d, unsigned long adr)
{
	if (machine_is_netwinder())
		nw_en_write();
	*CSR_ROMWRITEREG = adr & 3;
	adr &= ~3;
	*(uint16_t*)(map->map_priv_1 + adr) = d.x[0];
}

static void dc21285_write32(struct map_info *map, map_word d, unsigned long adr)
{
	if (machine_is_netwinder())
		nw_en_write();
	*(uint32_t*)(map->map_priv_1 + adr) = d.x[0];
}

static void dc21285_copy_to_32(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	while (len > 0) {
		uint32_t d = *((uint32_t*)from)++;
		dc21285_write32(map, d, to);
		to += 4;
		len -= 4;
	}
}

static void dc21285_copy_to_16(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	while (len > 0) {
		uint16_t d = *((uint16_t*)from)++;
		dc21285_write16(map, d, to);
		to += 2;
		len -= 2;
	}
}

static void dc21285_copy_to_8(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	uint8_t d = *((uint8_t*)from)++;
	dc21285_write8(map, d, to);
	to++;
	len--;
}

static struct map_info dc21285_map = {
	.name = "DC21285 flash",
	.phys = NO_XIP,
	.size = 16*1024*1024,
	.copy_from = dc21285_copy_from,
};


/* Partition stuff */
static struct mtd_partition *dc21285_parts;
#ifdef CONFIG_MTD_PARTITIONS
static const char *probes[] = { "RedBoot", "cmdlinepart", NULL };
#endif
  
static int __init init_dc21285(void)
{

#ifdef CONFIG_MTD_PARTITIONS
	int nrparts;
#endif

	/* Determine bankwidth */
	switch (*CSR_SA110_CNTL & (3<<14)) {
		case SA110_CNTL_ROMWIDTH_8: 
			dc21285_map.bankwidth = 1;
			dc21285_map.read = dc21285_read8;
			dc21285_map.write = dc21285_write8;
			dc21285_map.copy_to = dc21285_copy_to_8;
			break;
		case SA110_CNTL_ROMWIDTH_16: 
			dc21285_map.bankwidth = 2; 
			dc21285_map.read = dc21285_read16;
			dc21285_map.write = dc21285_write16;
			dc21285_map.copy_to = dc21285_copy_to_16;
			break;
		case SA110_CNTL_ROMWIDTH_32: 
			dc21285_map.bankwidth = 4; 
			break;
			dc21285_map.read = dc21285_read32;
			dc21285_map.write = dc21285_write32;
			dc21285_map.copy_to = dc21285_copy_to_32;
		default:
			printk (KERN_ERR "DC21285 flash: undefined bankwidth\n");
			return -ENXIO;
	}
	printk (KERN_NOTICE "DC21285 flash support (%d-bit bankwidth)\n",
		dc21285_map.bankwidth*8);

	/* Let's map the flash area */
	dc21285_map.map_priv_1 = (void __iomem *)ioremap(DC21285_FLASH, 16*1024*1024);
	if (!dc21285_map.map_priv_1) {
		printk("Failed to ioremap\n");
		return -EIO;
	}

	if (machine_is_ebsa285()) {
		dc21285_mtd = do_map_probe("cfi_probe", &dc21285_map);
	} else {
		dc21285_mtd = do_map_probe("jedec_probe", &dc21285_map);
	}

	if (!dc21285_mtd) {
		iounmap((void *)dc21285_map.map_priv_1);
		return -ENXIO;
	}	
	
	dc21285_mtd->owner = THIS_MODULE;

#ifdef CONFIG_MTD_PARTITIONS
	nrparts = parse_mtd_partitions(dc21285_mtd, probes, &dc21285_parts, (void *)0);
	if (nrparts > 0)
		add_mtd_partitions(dc21285_mtd, dc21285_parts, nrparts);
	else	
#endif	
		add_mtd_device(dc21285_mtd);
			
	if(machine_is_ebsa285()) {
		/* 
		 * Flash timing is determined with bits 19-16 of the
		 * CSR_SA110_CNTL.  The value is the number of wait cycles, or
		 * 0 for 16 cycles (the default).  Cycles are 20 ns.
		 * Here we use 7 for 140 ns flash chips.
		 */
		/* access time */
		*CSR_SA110_CNTL = ((*CSR_SA110_CNTL & ~0x000f0000) | (7 << 16));
		/* burst time */
		*CSR_SA110_CNTL = ((*CSR_SA110_CNTL & ~0x00f00000) | (7 << 20));
		/* tristate time */
		*CSR_SA110_CNTL = ((*CSR_SA110_CNTL & ~0x0f000000) | (7 << 24));
	}
	
	return 0;
}

static void __exit cleanup_dc21285(void)
{
#ifdef CONFIG_MTD_PARTITIONS
	if (dc21285_parts) {
		del_mtd_partitions(dc21285_mtd);
		kfree(dc21285_parts);
	} else
#endif
		del_mtd_device(dc21285_mtd);

	map_destroy(dc21285_mtd);
	iounmap((void *)dc21285_map.map_priv_1);
}

module_init(init_dc21285);
module_exit(cleanup_dc21285);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Pitre <nico@cam.org>");
MODULE_DESCRIPTION("MTD map driver for DC21285 boards");
