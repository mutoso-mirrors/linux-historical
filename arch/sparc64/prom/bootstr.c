/* $Id: bootstr.c,v 1.4 1997/06/17 13:25:35 jj Exp $
 * bootstr.c:  Boot string/argument acquisition from the PROM.
 *
 * Copyright(C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright(C) 1996 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/string.h>
#include <linux/init.h>
#include <asm/oplib.h>

#define BARG_LEN  256
int bootstr_len __initdata = BARG_LEN;
static int bootstr_valid __initdata = 0;
static char bootstr_buf[BARG_LEN] __initdata = { 0 };

__initfunc(char *
prom_getbootargs(void))
{
	/* This check saves us from a panic when bootfd patches args. */
	if (bootstr_valid) return bootstr_buf;
	prom_getstring(prom_chosen_node, "bootargs", bootstr_buf, BARG_LEN);
	bootstr_valid = 1;
	return bootstr_buf;
}
