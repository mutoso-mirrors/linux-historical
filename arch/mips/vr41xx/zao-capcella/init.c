/*
 * FILE NAME
 *	arch/mips/vr41xx/zao-capcella/init.c
 *
 * BRIEF MODULE DESCRIPTION
 *	Initialisation code for the ZAO Networks Capcella.
 *
 * Copyright 2002 Yoichi Yuasa
 *                yuasa@hh.iij4u.or.jp
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/string.h>

#include <asm/bootinfo.h>
#include <asm/cpu.h>
#include <asm/mipsregs.h>
#include <asm/vr41xx/vr41xx.h>

const char *get_system_type(void)
{
	return "ZAO Networks Capcella";
}

void __init prom_init(int argc, char **argv, unsigned long magic, int *prom_vec)
{
	int argc = fw_arg0;
	char **argv = (char **) fw_arg1;
	int i;

	/*
	 * collect args and prepare cmd_line
	 */
	for (i = 1; i < argc; i++) {
		strcat(arcs_cmdline, argv[i]);
		if (i < (argc - 1))
			strcat(arcs_cmdline, " ");
	}

	mips_machgroup = MACH_GROUP_NEC_VR41XX;
	mips_machtype = MACH_ZAO_CAPCELLA;
}

unsigned long __init prom_free_prom_memory(void)
{
	return 0;
}
