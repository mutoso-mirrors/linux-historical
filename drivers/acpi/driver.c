/*
 *  driver.c - ACPI driver
 *
 *  Copyright (C) 2000 Andrew Henroid
 *  Copyright (C) 2001 Andrew Grover
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * Changes
 * David Woodhouse <dwmw2@redhat.com> 2000-12-6
 * - Fix interruptible_sleep_on() races
 * Andrew Grover <andrew.grover@intel.com> 2001-2-28
 * - Major revamping
 * Peter Breuer <ptb@it.uc3m.es> 2001-5-20
 * - parse boot time params.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/pm.h>
#include <linux/acpi.h>
#include <asm/uaccess.h>
#include "acpi.h"
#include "driver.h"

#ifdef CONFIG_ACPI_KERNEL_CONFIG
#include <asm/efi.h>
#define ACPI_USE_EFI
#endif


#define _COMPONENT	OS_DEPENDENT
	MODULE_NAME	("driver")

FADT_DESCRIPTOR acpi_fadt;

static int acpi_disabled = 0;

/*
 * Start the interpreter
 */
int
acpi_init(void)
{
	ACPI_PHYSICAL_ADDRESS	rsdp_phys;
	ACPI_BUFFER		buffer;
	ACPI_SYSTEM_INFO	sys_info;

	if (PM_IS_ACTIVE()) {
		printk(KERN_NOTICE "ACPI: APM is already active, exiting\n");
		return -ENODEV;
	}


	if (acpi_disabled) {
		printk(KERN_NOTICE "ACPI: disabled by cmdline, exiting\n");
		return -ENODEV;
	}

	if (!ACPI_SUCCESS(acpi_initialize_subsystem())) {
		printk(KERN_ERR "ACPI: Driver initialization failed\n");
		return -ENODEV;
	}

#ifndef ACPI_USE_EFI
	if (!ACPI_SUCCESS(acpi_find_root_pointer(&rsdp_phys))) {
		printk(KERN_ERR "ACPI: System description tables not found\n");
		return -ENODEV;
	}
#else
	rsdp_phys = efi.acpi;
#endif

	/* from this point on, on error we must call acpi_terminate() */

	if (!ACPI_SUCCESS(acpi_load_tables(rsdp_phys))) {
		printk(KERN_ERR "ACPI: System description table load failed\n");
		acpi_terminate();
		return -ENODEV;
	}

	/* get a separate copy of the FADT for use by other drivers */
	memset(&acpi_fadt, 0, sizeof(acpi_fadt));
	buffer.pointer = &acpi_fadt;
	buffer.length = sizeof(acpi_fadt);

	if (!ACPI_SUCCESS(acpi_get_table(ACPI_TABLE_FADT, 1, &buffer))) {
		printk(KERN_ERR "ACPI: Could not get FADT\n");
		acpi_terminate();
		return -ENODEV;
	}

	buffer.length  = sizeof(sys_info);
	buffer.pointer = &sys_info;

	if (!ACPI_SUCCESS (acpi_get_system_info(&buffer))) {
		printk(KERN_ERR "ACPI: Could not get system info\n");
		acpi_terminate();
		return -ENODEV;
	}

	printk(KERN_INFO "ACPI: Core Subsystem version [%x]\n", sys_info.acpi_ca_version);

	if (!ACPI_SUCCESS(acpi_enable_subsystem(ACPI_FULL_INITIALIZATION))) {
		printk(KERN_ERR "ACPI: Subsystem enable failed\n");
		acpi_terminate();
		return -ENODEV;
	}

	printk(KERN_INFO "ACPI: Subsystem enabled\n");

	pm_active = 1;

	return 0;
}

/*
 * Terminate the interpreter
 */
void
acpi_exit(void)
{
	acpi_terminate();

	pm_active = 0;

	printk(KERN_ERR "ACPI: Subsystem disabled\n");
}

module_init(acpi_init);
module_exit(acpi_exit);

#ifndef MODULE
static int __init acpi_setup(char *str) {
	while (str && *str) {
	if (strncmp(str, "off", 3) == 0)
		acpi_disabled = 1;
	str = strchr(str, ',');
	if (str)
		str += strspn(str, ", \t");
	}
	return 1;
}

__setup("acpi=", acpi_setup);
#endif
