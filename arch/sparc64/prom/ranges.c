/* $Id: ranges.c,v 1.1 1997/02/25 12:40:28 jj Exp $
 * ranges.c: Handle ranges in newer proms for obio/sbus.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/init.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/sbus.h>
#include <asm/system.h>

struct linux_prom_ranges promlib_obio_ranges[PROMREG_MAX];
int num_obio_ranges;

/* Adjust register values based upon the ranges parameters. */
inline void
prom_adjust_regs(struct linux_prom_registers *regp, int nregs,
		 struct linux_prom_ranges *rangep, int nranges)
{
	int regc, rngc;

	for(regc=0; regc < nregs; regc++) {
		for(rngc=0; rngc < nranges; rngc++)
			if(regp[regc].which_io == rangep[rngc].ot_child_space)
				break; /* Fount it */
		if(rngc==nranges) /* oops */
			prom_printf("adjust_regs: Could not find range with matching bus type...\n");
		regp[regc].which_io = rangep[rngc].ot_parent_space;
		regp[regc].phys_addr += rangep[rngc].ot_parent_base;
	}
}

inline void
prom_adjust_ranges(struct linux_prom_ranges *ranges1, int nranges1,
		   struct linux_prom_ranges *ranges2, int nranges2)
{
	int rng1c, rng2c;

	for(rng1c=0; rng1c < nranges1; rng1c++) {
		for(rng2c=0; rng2c < nranges2; rng2c++)
			if(ranges1[rng1c].ot_child_space ==
			   ranges2[rng2c].ot_child_space) break;
		if(rng2c == nranges2) /* oops */
			prom_printf("adjust_ranges: Could not find matching bus type...\n");
		ranges1[rng1c].ot_parent_space = ranges2[rng2c].ot_parent_space;
		ranges1[rng1c].ot_parent_base += ranges2[rng2c].ot_parent_base;
	}
}

/* Apply probed sbus ranges to registers passed, if no ranges return. */
void prom_apply_sbus_ranges(struct linux_sbus *sbus, struct linux_prom_registers *regs,
			    int nregs, struct linux_sbus_device *sdev)
{
	if(sbus->num_sbus_ranges) {
		if(sdev && (sdev->ranges_applied == 0)) {
			sdev->ranges_applied = 1;
			prom_adjust_regs(regs, nregs, sbus->sbus_ranges,
					 sbus->num_sbus_ranges);
		}
	}
}

__initfunc(void prom_ranges_init(void))
{
}

__initfunc(void prom_sbus_ranges_init(int parentnd, struct linux_sbus *sbus))
{
	int success;
	
	sbus->num_sbus_ranges = 0;
	success = prom_getproperty(sbus->prom_node, "ranges",
				   (char *) sbus->sbus_ranges,
				   sizeof (sbus->sbus_ranges));
	if (success != -1)
		sbus->num_sbus_ranges = (success/sizeof(struct linux_prom_ranges));
}

void
prom_apply_generic_ranges (int node, int parent, struct linux_prom_registers *regs, int nregs)
{
	int success;
	int num_ranges;
	struct linux_prom_ranges ranges[PROMREG_MAX];
	
	success = prom_getproperty(node, "ranges",
				   (char *) ranges,
				   sizeof (ranges));
	if (success != -1) {
		num_ranges = (success/sizeof(struct linux_prom_ranges));
		if (parent) {
			struct linux_prom_ranges parent_ranges[PROMREG_MAX];
			int num_parent_ranges;
		
			success = prom_getproperty(parent, "ranges",
				   		   (char *) parent_ranges,
				   		   sizeof (parent_ranges));
			if (success != -1) {
				num_parent_ranges = (success/sizeof(struct linux_prom_ranges));
				prom_adjust_ranges (ranges, num_ranges, parent_ranges, num_parent_ranges);
			}
		}
		prom_adjust_regs(regs, nregs, ranges, num_ranges);
	}
}
