/* devices.c: Initial scan of the prom device tree for important
 *            Sparc device nodes which we need to find.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/tasks.h>
#include <linux/config.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/smp.h>
#include <asm/system.h>

struct prom_cpuinfo linux_cpus[NCPUS];
int linux_num_cpus;

extern void cpu_probe(void);
extern void clock_stop_probe(void); /* tadpole.c */
extern void sun4c_probe_memerr_reg(void);

__initfunc(unsigned long
device_scan(unsigned long mem_start))
{
	char node_str[128];
	int nd, prom_node_cpu, thismid;
	int cpu_nds[NCPUS];  /* One node for each cpu */
	int cpu_ctr = 0;

	prom_getstring(prom_root_node, "device_type", node_str, sizeof(node_str));

	if(strcmp(node_str, "cpu") == 0) {
		cpu_nds[0] = prom_root_node;
		cpu_ctr++;
	} else {
		int scan;
		scan = prom_getchild(prom_root_node);
		prom_printf("root child is %08lx\n", (unsigned long) scan);
		nd = 0;
		while((scan = prom_getsibling(scan)) != 0) {
			prom_getstring(scan, "device_type", node_str, sizeof(node_str));
			if(strcmp(node_str, "cpu") == 0) {
				cpu_nds[cpu_ctr] = scan;
				linux_cpus[cpu_ctr].prom_node = scan;
				prom_getproperty(scan, "mid", (char *) &thismid, sizeof(thismid));
				linux_cpus[cpu_ctr].mid = thismid;
				prom_printf("Found CPU %d <node=%08lx,mid=%d>\n",
					    cpu_ctr, (unsigned long) scan,
					    thismid);
				cpu_ctr++;
			}
		};
		if(cpu_ctr == 0) {
			if (sparc_cpu_model == sun4d) {
				scan = prom_getchild(prom_root_node);
				for (scan = prom_searchsiblings(scan, "cpu-unit"); scan;
				     scan = prom_searchsiblings(prom_getsibling(scan), "cpu-unit")) {
					int node = prom_getchild(scan);
					
					prom_getstring(node, "device_type", node_str, sizeof(node_str));
					if (strcmp(node_str, "cpu") == 0) {
						prom_getproperty(node, "cpu-id", (char *) &thismid, sizeof(thismid));
						if (cpu_ctr < NCPUS) {
							cpu_nds[cpu_ctr] = node;
							linux_cpus[cpu_ctr].prom_node = node;
							linux_cpus[cpu_ctr].mid = thismid;
						}
						prom_printf("Found CPU %d <node=%08lx,mid=%d>\n",
							    cpu_ctr, (unsigned long) node,
							    thismid);
						cpu_ctr++;
					}
				}
			}
			if (cpu_ctr > NCPUS)
				cpu_ctr = NCPUS;
		}
		if(cpu_ctr == 0) {
			printk("No CPU nodes found, cannot continue.\n");
			/* Probably a sun4e, Sun is trying to trick us ;-) */
			halt();
		}
		printk("Found %d CPU prom device tree node(s).\n", cpu_ctr);
	};
	prom_node_cpu = cpu_nds[0];

	linux_num_cpus = cpu_ctr;

	cpu_probe();
#ifdef CONFIG_SUN_AUXIO
	{
		extern void auxio_probe(void);
		extern void auxio_power_probe(void);
		auxio_probe();
		auxio_power_probe();
	}
#endif
	clock_stop_probe();

	if (sparc_cpu_model == sun4c)
		sun4c_probe_memerr_reg();

	return mem_start;
}
