#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#define APIC_DFR_VALUE	(APIC_DFR_CLUSTER)

#define TARGET_CPUS (0xf)

#define no_balance_irq (1)
#define esr_disable (1)

#define APIC_BROADCAST_ID      0x0F
#define check_apicid_used(bitmap, apicid) ((bitmap) & (1 << (apicid)))

static inline int apic_id_registered(void)
{
	return (1);
}

static inline void init_apic_ldr(void)
{
	/* Already done in NUMA-Q firmware */
}

static inline void clustered_apic_check(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
		"NUMA-Q", nr_ioapics);
}

/*
 * Skip adding the timer int on secondary nodes, which causes
 * a small but painful rift in the time-space continuum.
 */
static inline int multi_timer_check(int apic, int irq)
{
	return (apic != 0 && irq == 0);
}

static inline ulong ioapic_phys_id_map(ulong phys_map)
{
	/* We don't have a good way to do this yet - hack */
	return 0xf;
}

static inline int cpu_present_to_apicid(int mps_cpu)
{
	return ( ((mps_cpu/4)*16) + (1<<(mps_cpu%4)) );
}

static inline int generate_logical_apicid(int quad, int phys_apicid)
{
	return ( (quad << 4) + (phys_apicid ? phys_apicid << 1 : 1) );
}

static inline int apicid_to_node(int logical_apicid) 
{
	return (logical_apicid >> 4);
}

static inline unsigned long apicid_to_cpu_present(int logical_apicid)
{
	return ( (logical_apicid&0xf) << (4*apicid_to_node(logical_apicid)) );
}

static inline int mpc_apic_id(struct mpc_config_processor *m, int quad)
{
	int logical_apicid = generate_logical_apicid(quad, m->mpc_apicid);

	printk("Processor #%d %ld:%ld APIC version %d (quad %d, apic %d)\n",
			m->mpc_apicid,
			(m->mpc_cpufeature & CPU_FAMILY_MASK) >> 8,
			(m->mpc_cpufeature & CPU_MODEL_MASK) >> 4,
			m->mpc_apicver, quad, logical_apicid);
	return logical_apicid;
}

static inline void setup_portio_remap(void)
{
	if (numnodes <= 1)
       		return;

	printk("Remapping cross-quad port I/O for %d quads\n", numnodes);
	xquad_portio = ioremap (XQUAD_PORTIO_BASE, numnodes*XQUAD_PORTIO_QUAD);
	printk("xquad_portio vaddr 0x%08lx, len %08lx\n",
		(u_long) xquad_portio, (u_long) numnodes*XQUAD_PORTIO_QUAD);
}

static inline int check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return (1);
}

#endif /* __ASM_MACH_APIC_H */
