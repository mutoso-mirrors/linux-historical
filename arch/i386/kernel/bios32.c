/*
 * bios32.c - Low-Level PCI Access
 *
 * $Id: bios32.c,v 1.26 1998/02/18 15:21:09 mj Exp $
 *
 * Sponsored by
 *	iX Multiuser Multitasking Magazine
 *	Hannover, Germany
 *	hm@ix.de
 *
 * Copyright 1993, 1994 Drew Eckhardt
 *      Visionary Computing
 *      (Unix and Linux consulting and custom programming)
 *      Drew@Colorado.EDU
 *      +1 (303) 786-7975
 *
 * For more information, please consult
 *
 * PCI BIOS Specification Revision
 * PCI Local Bus Specification
 * PCI System Design Guide
 *
 * PCI Special Interest Group
 * M/S HF3-15A
 * 5200 N.E. Elam Young Parkway
 * Hillsboro, Oregon 97124-6497
 * +1 (503) 696-2000
 * +1 (800) 433-5177
 *
 * Manuals are $25 each or $50 for all three, plus $7 shipping
 * within the United States, $35 abroad.
 *
 *
 * CHANGELOG :
 * Jun 17, 1994 : Modified to accommodate the broken pre-PCI BIOS SPECIFICATION
 *	Revision 2.0 present on <thys@dennis.ee.up.ac.za>'s ASUS mainboard.
 *
 * Jan 5,  1995 : Modified to probe PCI hardware at boot time by Frederic
 *     Potter, potter@cao-vlsi.ibp.fr
 *
 * Jan 10, 1995 : Modified to store the information about configured pci
 *      devices into a list, which can be accessed via /proc/pci by
 *      Curtis Varner, cvarner@cs.ucr.edu
 *
 * Jan 12, 1995 : CPU-PCI bridge optimization support by Frederic Potter.
 *	Alpha version. Intel & UMC chipset support only.
 *
 * Apr 16, 1995 : Source merge with the DEC Alpha PCI support. Most of the code
 *	moved to drivers/pci/pci.c.
 *
 * Dec 7, 1996  : Added support for direct configuration access of boards
 *      with Intel compatible access schemes (tsbogend@alpha.franken.de)
 *
 * Feb 3, 1997  : Set internal functions to static, save/restore flags
 *	avoid dead locks reading broken PCI BIOS, werner@suse.de 
 *
 * Apr 26, 1997 : Fixed case when there is BIOS32, but not PCI BIOS
 *	(mj@atrey.karlin.mff.cuni.cz)
 *
 * May 7,  1997 : Added some missing cli()'s. [mj]
 * 
 * Jun 20, 1997 : Corrected problems in "conf1" type accesses.
 *      (paubert@iram.es)
 *
 * Aug 2,  1997 : Split to PCI BIOS handling and direct PCI access parts
 *	and cleaned it up...     Martin Mares <mj@atrey.karlin.mff.cuni.cz>
 *
 * Feb 6,  1998 : No longer using BIOS to find devices and device classes. [mj]
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <asm/io.h>

#include <linux/smp_lock.h>
#include <asm/irq.h>
#include <asm/bitops.h>
#include <asm/smp.h>

#include "irq.h"

/*
 * Generic PCI access -- indirect calls according to detected HW.
 */

struct pci_access {
    int pci_present;
    int (*read_config_byte)(unsigned char, unsigned char, unsigned char, unsigned char *);
    int (*read_config_word)(unsigned char, unsigned char, unsigned char, unsigned short *);
    int (*read_config_dword)(unsigned char, unsigned char, unsigned char, unsigned int *);
    int (*write_config_byte)(unsigned char, unsigned char, unsigned char, unsigned char);
    int (*write_config_word)(unsigned char, unsigned char, unsigned char, unsigned short);
    int (*write_config_dword)(unsigned char, unsigned char, unsigned char, unsigned int);
};

static int pci_stub(void)
{
	return PCIBIOS_FUNC_NOT_SUPPORTED;
}

static struct pci_access pci_access_none = {
	0,		   		/* No PCI present */
	(void *) pci_stub,
	(void *) pci_stub,
	(void *) pci_stub,
	(void *) pci_stub,
	(void *) pci_stub,
	(void *) pci_stub
};

static struct pci_access *access_pci = &pci_access_none;

int pcibios_present(void)
{
	return access_pci->pci_present;
}

int pcibios_read_config_byte (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned char *value)
{
	return access_pci->read_config_byte(bus, device_fn, where, value);
}

int pcibios_read_config_word (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned short *value)
{
	return access_pci->read_config_word(bus, device_fn, where, value);
}

int pcibios_read_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned int *value)
{
	return access_pci->read_config_dword(bus, device_fn, where, value);
}

int pcibios_write_config_byte (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned char value)
{
	return access_pci->write_config_byte(bus, device_fn, where, value);
}

int pcibios_write_config_word (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned short value)
{
	return access_pci->write_config_word(bus, device_fn, where, value);
}

int pcibios_write_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned int value)
{
	return access_pci->write_config_dword(bus, device_fn, where, value);
}

static unsigned int pci_probe = ~0;

#define PCI_PROBE_BIOS 1
#define PCI_PROBE_CONF1 2
#define PCI_PROBE_CONF2 4

/*
 * Direct access to PCI hardware...
 */

#ifdef CONFIG_PCI_DIRECT

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */

#define CONFIG_CMD(bus, device_fn, where)   (0x80000000 | (bus << 16) | (device_fn << 8) | (where & ~3))

static int pci_conf1_read_config_byte(unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned char *value)
{
    unsigned long flags;

    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    *value = inb(0xCFC + (where&3));
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_read_config_word (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned short *value)
{
    unsigned long flags;

    if (where&1) return PCIBIOS_BAD_REGISTER_NUMBER;
    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);    
    *value = inw(0xCFC + (where&2));
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;    
}

static int pci_conf1_read_config_dword (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned int *value)
{
    unsigned long flags;

    if (where&3) return PCIBIOS_BAD_REGISTER_NUMBER;
    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    *value = inl(0xCFC);
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;    
}

static int pci_conf1_write_config_byte (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned char value)
{
    unsigned long flags;

    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);    
    outb(value, 0xCFC + (where&3));
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config_word (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned short value)
{
    unsigned long flags;

    if (where&1) return PCIBIOS_BAD_REGISTER_NUMBER;
    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    outw(value, 0xCFC + (where&2));
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config_dword (unsigned char bus, unsigned char device_fn, 
				  unsigned char where, unsigned int value)
{
    unsigned long flags;

    if (where&3) return PCIBIOS_BAD_REGISTER_NUMBER;
    save_flags(flags); cli();
    outl(CONFIG_CMD(bus,device_fn,where), 0xCF8);
    outl(value, 0xCFC);
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

#undef CONFIG_CMD

static struct pci_access pci_direct_conf1 = {
      1,
      pci_conf1_read_config_byte,
      pci_conf1_read_config_word,
      pci_conf1_read_config_dword,
      pci_conf1_write_config_byte,
      pci_conf1_write_config_word,
      pci_conf1_write_config_dword
};

/*
 * Functions for accessing PCI configuration space with type 2 accesses
 */

#define IOADDR(devfn, where)   ((0xC000 | ((devfn & 0x78) << 5)) + where)
#define FUNC(devfn)            (((devfn & 7) << 1) | 0xf0)

static int pci_conf2_read_config_byte(unsigned char bus, unsigned char device_fn, 
			       unsigned char where, unsigned char *value)
{
    unsigned long flags;

    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    *value = inb(IOADDR(device_fn,where));
    outb (0, 0xCF8);
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_read_config_word (unsigned char bus, unsigned char device_fn, 
				unsigned char where, unsigned short *value)
{
    unsigned long flags;

    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    *value = inw(IOADDR(device_fn,where));
    outb (0, 0xCF8);
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_read_config_dword (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned int *value)
{
    unsigned long flags;

    if (device_fn & 0x80)
	return PCIBIOS_DEVICE_NOT_FOUND;
    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    *value = inl (IOADDR(device_fn,where));    
    outb (0, 0xCF8);    
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_byte (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned char value)
{
    unsigned long flags;

    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    outb (value, IOADDR(device_fn,where));
    outb (0, 0xCF8);    
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_word (unsigned char bus, unsigned char device_fn, 
				 unsigned char where, unsigned short value)
{
    unsigned long flags;

    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    outw (value, IOADDR(device_fn,where));
    outb (0, 0xCF8);    
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_dword (unsigned char bus, unsigned char device_fn, 
				  unsigned char where, unsigned int value)
{
    unsigned long flags;

    save_flags(flags); cli();
    outb (FUNC(device_fn), 0xCF8);
    outb (bus, 0xCFA);
    outl (value, IOADDR(device_fn,where));    
    outb (0, 0xCF8);    
    restore_flags(flags);
    return PCIBIOS_SUCCESSFUL;
}

#undef IOADDR
#undef FUNC

static struct pci_access pci_direct_conf2 = {
      1,
      pci_conf2_read_config_byte,
      pci_conf2_read_config_word,
      pci_conf2_read_config_dword,
      pci_conf2_write_config_byte,
      pci_conf2_write_config_word,
      pci_conf2_write_config_dword
};

__initfunc(static struct pci_access *pci_check_direct(void))
{
	unsigned int tmp;
	unsigned long flags;

	save_flags(flags); cli();

	/*
	 * Check if configuration type 1 works.
	 */
	if (pci_probe & PCI_PROBE_CONF1) {
		outb (0x01, 0xCFB);
		tmp = inl (0xCF8);
		outl (0x80000000, 0xCF8);
		if (inl (0xCF8) == 0x80000000) {
			outl (tmp, 0xCF8);
			restore_flags(flags);
			printk("PCI: Using configuration type 1\n");
			return &pci_direct_conf1;
		}
		outl (tmp, 0xCF8);
	}

	/*
	 * Check if configuration type 2 works.
	 */
	if (pci_probe & PCI_PROBE_CONF2) {
		outb (0x00, 0xCFB);
		outb (0x00, 0xCF8);
		outb (0x00, 0xCFA);
		if (inb (0xCF8) == 0x00 && inb (0xCFA) == 0x00) {
			restore_flags(flags);
			printk("PCI: Using configuration type 2\n");
			return &pci_direct_conf2;
		}
	}

	restore_flags(flags);
	return NULL;
}

#endif

/*
 * BIOS32 and PCI BIOS handling.
 */

#ifdef CONFIG_PCI_BIOS

#define PCIBIOS_PCI_FUNCTION_ID 	0xb1XX
#define PCIBIOS_PCI_BIOS_PRESENT 	0xb101
#define PCIBIOS_FIND_PCI_DEVICE		0xb102
#define PCIBIOS_FIND_PCI_CLASS_CODE	0xb103
#define PCIBIOS_GENERATE_SPECIAL_CYCLE	0xb106
#define PCIBIOS_READ_CONFIG_BYTE	0xb108
#define PCIBIOS_READ_CONFIG_WORD	0xb109
#define PCIBIOS_READ_CONFIG_DWORD	0xb10a
#define PCIBIOS_WRITE_CONFIG_BYTE	0xb10b
#define PCIBIOS_WRITE_CONFIG_WORD	0xb10c
#define PCIBIOS_WRITE_CONFIG_DWORD	0xb10d

/* BIOS32 signature: "_32_" */
#define BIOS32_SIGNATURE	(('_' << 0) + ('3' << 8) + ('2' << 16) + ('_' << 24))

/* PCI signature: "PCI " */
#define PCI_SIGNATURE		(('P' << 0) + ('C' << 8) + ('I' << 16) + (' ' << 24))

/* PCI service signature: "$PCI" */
#define PCI_SERVICE		(('$' << 0) + ('P' << 8) + ('C' << 16) + ('I' << 24))

/*
 * This is the standard structure used to identify the entry point
 * to the BIOS32 Service Directory, as documented in
 * 	Standard BIOS 32-bit Service Directory Proposal
 * 	Revision 0.4 May 24, 1993
 * 	Phoenix Technologies Ltd.
 *	Norwood, MA
 * and the PCI BIOS specification.
 */

union bios32 {
	struct {
		unsigned long signature;	/* _32_ */
		unsigned long entry;		/* 32 bit physical address */
		unsigned char revision;		/* Revision level, 0 */
		unsigned char length;		/* Length in paragraphs should be 01 */
		unsigned char checksum;		/* All bytes must add up to zero */
		unsigned char reserved[5]; 	/* Must be zero */
	} fields;
	char chars[16];
};

/*
 * Physical address of the service directory.  I don't know if we're
 * allowed to have more than one of these or not, so just in case
 * we'll make pcibios_present() take a memory start parameter and store
 * the array there.
 */

static unsigned long bios32_entry = 0;
static struct {
	unsigned long address;
	unsigned short segment;
} bios32_indirect = { 0, __KERNEL_CS };

/*
 * Returns the entry point for the given service, NULL on error
 */

static unsigned long bios32_service(unsigned long service)
{
	unsigned char return_code;	/* %al */
	unsigned long address;		/* %ebx */
	unsigned long length;		/* %ecx */
	unsigned long entry;		/* %edx */
	unsigned long flags;

	save_flags(flags); cli();
	__asm__("lcall (%%edi)"
		: "=a" (return_code),
		  "=b" (address),
		  "=c" (length),
		  "=d" (entry)
		: "0" (service),
		  "1" (0),
		  "D" (&bios32_indirect));
	restore_flags(flags);

	switch (return_code) {
		case 0:
			return address + entry;
		case 0x80:	/* Not present */
			printk("bios32_service(0x%lx): not present\n", service);
			return 0;
		default: /* Shouldn't happen */
			printk("bios32_service(0x%lx): returned 0x%x, report to <mj@ucw.cz>.\n",
				service, return_code);
			return 0;
	}
}

static long pcibios_entry = 0;
static struct {
	unsigned long address;
	unsigned short segment;
} pci_indirect = { 0, __KERNEL_CS };

__initfunc(static int check_pcibios(void))
{
	unsigned long signature;
	unsigned char present_status;
	unsigned char major_revision;
	unsigned char minor_revision;
	unsigned long flags;
	int pack;

	if ((pcibios_entry = bios32_service(PCI_SERVICE))) {
		pci_indirect.address = pcibios_entry | PAGE_OFFSET;

		save_flags(flags); cli();
		__asm__("lcall (%%edi)\n\t"
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:\tshl $8, %%eax\n\t"
			"movw %%bx, %%ax"
			: "=d" (signature),
			  "=a" (pack)
			: "1" (PCIBIOS_PCI_BIOS_PRESENT),
			  "D" (&pci_indirect)
			: "bx", "cx");
		restore_flags(flags);

		present_status = (pack >> 16) & 0xff;
		major_revision = (pack >> 8) & 0xff;
		minor_revision = pack & 0xff;
		if (present_status || (signature != PCI_SIGNATURE)) {
			printk ("PCI: %s: BIOS32 Service Directory says PCI BIOS is present,\n"
				"	but PCI_BIOS_PRESENT subfunction fails with present status of 0x%x\n"
				"	and signature of 0x%08lx (%c%c%c%c).  Report to <mj@ucw.cz>.\n",
				(signature == PCI_SIGNATURE) ?  "WARNING" : "ERROR",
				present_status, signature,
				(char) (signature >>  0), (char) (signature >>  8),
				(char) (signature >> 16), (char) (signature >> 24));

			if (signature != PCI_SIGNATURE)
				pcibios_entry = 0;
		}
		if (pcibios_entry) {
			printk ("PCI: PCI BIOS revision %x.%02x entry at 0x%lx\n",
				major_revision, minor_revision, pcibios_entry);
			return 1;
		}
	}
	return 0;
}

#if 0	/* Not used */

static int pci_bios_find_class (unsigned int class_code, unsigned short index,
	unsigned char *bus, unsigned char *device_fn)
{
	unsigned long bx;
	unsigned long ret;
	unsigned long flags;

	save_flags(flags); cli();
	__asm__ ("lcall (%%edi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=b" (bx),
		  "=a" (ret)
		: "1" (PCIBIOS_FIND_PCI_CLASS_CODE),
		  "c" (class_code),
		  "S" ((int) index),
		  "D" (&pci_indirect));
	restore_flags(flags);
	*bus = (bx >> 8) & 0xff;
	*device_fn = bx & 0xff;
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_find_device (unsigned short vendor, unsigned short device_id,
	unsigned short index, unsigned char *bus, unsigned char *device_fn)
{
	unsigned short bx;
	unsigned short ret;
	unsigned long flags;

	save_flags(flags); cli();
	__asm__("lcall (%%edi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=b" (bx),
		  "=a" (ret)
		: "1" (PCIBIOS_FIND_PCI_DEVICE),
		  "c" (device_id),
		  "d" (vendor),
		  "S" ((int) index),
		  "D" (&pci_indirect));
	restore_flags(flags);
	*bus = (bx >> 8) & 0xff;
	*device_fn = bx & 0xff;
	return (int) (ret & 0xff00) >> 8;
}

#endif

static int pci_bios_read_config_byte(unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned char *value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=c" (*value),
		  "=a" (ret)
		: "1" (PCIBIOS_READ_CONFIG_BYTE),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_read_config_word (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned short *value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=c" (*value),
		  "=a" (ret)
		: "1" (PCIBIOS_READ_CONFIG_WORD),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_read_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned int *value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=c" (*value),
		  "=a" (ret)
		: "1" (PCIBIOS_READ_CONFIG_DWORD),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_write_config_byte (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned char value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret)
		: "0" (PCIBIOS_WRITE_CONFIG_BYTE),
		  "c" (value),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_write_config_word (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned short value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret)
		: "0" (PCIBIOS_WRITE_CONFIG_WORD),
		  "c" (value),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_write_config_dword (unsigned char bus,
	unsigned char device_fn, unsigned char where, unsigned int value)
{
	unsigned long ret;
	unsigned long bx = (bus << 8) | device_fn;
	unsigned long flags;

	save_flags(flags); cli();
	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret)
		: "0" (PCIBIOS_WRITE_CONFIG_DWORD),
		  "c" (value),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	restore_flags(flags);
	return (int) (ret & 0xff00) >> 8;
}

/*
 * Function table for BIOS32 access
 */

static struct pci_access pci_bios_access = {
      1,
      pci_bios_read_config_byte,
      pci_bios_read_config_word,
      pci_bios_read_config_dword,
      pci_bios_write_config_byte,
      pci_bios_write_config_word,
      pci_bios_write_config_dword
};

/*
 * Try to find PCI BIOS.
 */

__initfunc(static struct pci_access *pci_find_bios(void))
{
	union bios32 *check;
	unsigned char sum;
	int i, length;

	/*
	 * Follow the standard procedure for locating the BIOS32 Service
	 * directory by scanning the permissible address range from
	 * 0xe0000 through 0xfffff for a valid BIOS32 structure.
	 */

	for (check = (union bios32 *) __va(0xe0000);
	     check <= (union bios32 *) __va(0xffff0);
	     ++check) {
		if (check->fields.signature != BIOS32_SIGNATURE)
			continue;
		length = check->fields.length * 16;
		if (!length)
			continue;
		sum = 0;
		for (i = 0; i < length ; ++i)
			sum += check->chars[i];
		if (sum != 0)
			continue;
		if (check->fields.revision != 0) {
			printk("PCI: unsupported BIOS32 revision %d at 0x%p, report to <mj@ucw.cz>\n",
				check->fields.revision, check);
			continue;
		}
		printk ("PCI: BIOS32 Service Directory structure at 0x%p\n", check);
		if (check->fields.entry >= 0x100000) {
			printk("PCI: BIOS32 entry in high memory, cannot use.\n");
			return NULL;
		} else {
			bios32_entry = check->fields.entry;
			printk ("PCI: BIOS32 Service Directory entry at 0x%lx\n", bios32_entry);
			bios32_indirect.address = bios32_entry + PAGE_OFFSET;
			if (check_pcibios())
				return &pci_bios_access;
		}
		break;	/* Hopefully more than one BIOS32 cannot happen... */
	}

	/*
	 * If we were told to use the PCI BIOS and it's not present, avoid
	 * touching the hardware.
	 */
	pci_probe = 0;
	return NULL;
}

#endif

/*
 * Arch-dependent fixups.
 */

__initfunc(void pcibios_fixup(void))
{
	struct pci_dev *dev;
	int i, has_io, has_mem;
	unsigned short cmd;
	unsigned char pin;

	for(dev = pci_devices; dev; dev=dev->next) {
		/*
		 * There are buggy BIOSes that forget to enable I/O and memory
		 * access to PCI devices. We try to fix this, but we need to
		 * be sure that the BIOS didn't forget to assign an address
		 * to the device. [mj]
		 */
		has_io = has_mem = 0;
		for(i=0; i<6; i++) {
			unsigned long a = dev->base_address[i];
			if (a & PCI_BASE_ADDRESS_SPACE_IO) {
				has_io |= 1;
				a &= PCI_BASE_ADDRESS_IO_MASK;
				if (!a || a == PCI_BASE_ADDRESS_IO_MASK) {
					printk(KERN_WARNING "PCI: BIOS forgot to assign address #%d to device %02x:%02x,"
						" please report to <mj@ucw.cz>\n", i, dev->bus->number, dev->devfn);
					has_io |= 2;
				}
			} else if (a & PCI_BASE_ADDRESS_MEM_MASK)
				has_mem = 1;
		}
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		if (has_io == 1 && !(cmd & PCI_COMMAND_IO)) {
			printk("PCI: Enabling I/O for device %02x:%02x\n",
				dev->bus->number, dev->devfn);
			cmd |= PCI_COMMAND_IO;
			pci_write_config_word(dev, PCI_COMMAND, cmd);
		}
		if (has_mem && !(cmd & PCI_COMMAND_MEMORY)) {
			printk("PCI: Enabling memory for device %02x:%02x\n",
				dev->bus->number, dev->devfn);
			cmd |= PCI_COMMAND_MEMORY;
			pci_write_config_word(dev, PCI_COMMAND, cmd);
		}
		pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
#ifdef __SMP__
		/*
		 * Recalculate IRQ numbers if we use the I/O APIC
		 */
		{
		int irq;

		if (pin) {
			pin--;		/* interrupt pins are numbered starting from 1 */
			irq = IO_APIC_get_PCI_irq_vector (dev->bus->number, PCI_SLOT(dev->devfn), pin);
			if (irq >= 0) {
				printk("PCI->APIC IRQ transform: (B%d,I%d,P%d) -> %d\n",
					dev->bus->number, PCI_SLOT(dev->devfn), pin, irq);
				dev->irq = irq;
				}
		}
		}
#endif
		/*
		 * Fix out-of-range IRQ numbers and report bogus IRQ.
		 */
		if (dev->irq >= NR_IRQS)
			dev->irq = 0;
		if (pin && !dev->irq)
			printk(KERN_WARNING "PCI: Bogus IRQ for device %02x:%02x [pin=%x], please report to <mj@ucw.cz>\n",
				dev->bus->number, dev->devfn, pin);
	}
}

/*
 * Initialization. Try all known PCI access methods.
 */

__initfunc(void pcibios_init(void))
{
	struct pci_access *a = NULL;

#ifdef CONFIG_PCI_BIOS
	if (pci_probe & PCI_PROBE_BIOS)
		a = pci_find_bios();
#endif
#ifdef CONFIG_PCI_DIRECT
	if (!a && (pci_probe & (PCI_PROBE_CONF1 | PCI_PROBE_CONF2)))
		a = pci_check_direct();
#endif
	if (a)
		access_pci = a;
}

#if !defined(CONFIG_PCI_BIOS) && !defined(CONFIG_PCI_DIRECT)
#error PCI configured with neither PCI BIOS or PCI direct access support.
#endif

__initfunc(char *pcibios_setup(char *str))
{
	if (!strncmp(str, "off", 3)) {
		pci_probe = 0;
		return NULL;
	}
#ifdef CONFIG_PCI_BIOS
	else if (!strncmp(str, "bios", 4)) {
		pci_probe = PCI_PROBE_BIOS;
		return NULL;
	} else if (!strncmp(str, "nobios", 6)) {
		pci_probe &= ~PCI_PROBE_BIOS;
		return NULL;
	}
#endif
#ifdef CONFIG_PCI_DIRECT
	else if (!strncmp(str, "conf1", 5)) {
		pci_probe = PCI_PROBE_CONF1;
		return NULL;
	}
	else if (!strncmp(str, "conf2", 5)) {
		pci_probe = PCI_PROBE_CONF2;
		return NULL;
	}
#endif
	return str;
}
