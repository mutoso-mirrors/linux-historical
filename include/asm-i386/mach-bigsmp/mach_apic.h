#ifndef __ASM_MACH_APIC_H
#define __ASM_MACH_APIC_H

#define SEQUENTIAL_APICID
#ifdef SEQUENTIAL_APICID
#define xapic_phys_to_log_apicid(phys_apic) ( (1ul << ((phys_apic) & 0x3)) |\
		((phys_apic<<2) & (~0xf)) )
#elif CLUSTERED_APICID
#define xapic_phys_to_log_apicid(phys_apic) ( (1ul << ((phys_apic) & 0x3)) |\
		((phys_apic) & (~0xf)) )
#endif

#define NO_BALANCE_IRQ (1)
#define esr_disable (1)

static inline int apic_id_registered(void)
{
	        return (1);
}

#define APIC_DFR_VALUE	(APIC_DFR_CLUSTER)
#define TARGET_CPUS	((cpu_online_map < 0xf)?cpu_online_map:0xf)

#define INT_DELIVERY_MODE dest_LowestPrio
#define INT_DEST_MODE 1     /* logical delivery broadcast to all procs */

#define APIC_BROADCAST_ID     (0x0f)
#define check_apicid_used(bitmap, apicid) (0)
#define check_apicid_present(bit) (phys_cpu_present_map & (1 << bit))

static inline unsigned long calculate_ldr(unsigned long old)
{
	unsigned long id;
	id = xapic_phys_to_log_apicid(hard_smp_processor_id());
	return ((old & ~APIC_LDR_MASK) | SET_APIC_LOGICAL_ID(id));
}

/*
 * Set up the logical destination ID.
 *
 * Intel recommends to set DFR, LDR and TPR before enabling
 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
 * document number 292116).  So here it goes...
 */
static inline void init_apic_ldr(void)
{
	unsigned long val;

	apic_write_around(APIC_DFR, APIC_DFR_VALUE);
	val = apic_read(APIC_LDR) & ~APIC_LDR_MASK;
	val = calculate_ldr(val);
	apic_write_around(APIC_LDR, val);
}

static inline void clustered_apic_check(void)
{
	printk("Enabling APIC mode:  %s.  Using %d I/O APICs\n",
		"Cluster", nr_ioapics);
}

static inline int multi_timer_check(int apic, int irq)
{
	return 0;
}

static inline int apicid_to_node(int logical_apicid)
{
	return 0;
}

extern u8 bios_cpu_apicid[];

static inline int cpu_present_to_apicid(int mps_cpu)
{
	return (int) bios_cpu_apicid[mps_cpu];
}

static inline unsigned long apicid_to_cpu_present(int phys_apicid)
{
	return (1ul << phys_apicid);
}

extern volatile u8 cpu_2_logical_apicid[];
/* Mapping from cpu number to logical apicid */
static inline int cpu_to_logical_apicid(int cpu)
{
       return (int)cpu_2_logical_apicid[cpu];
 }

static inline int mpc_apic_id(struct mpc_config_processor *m, int quad)
{
	printk("Processor #%d %ld:%ld APIC version %d\n",
	        m->mpc_apicid,
	        (m->mpc_cpufeature & CPU_FAMILY_MASK) >> 8,
	        (m->mpc_cpufeature & CPU_MODEL_MASK) >> 4,
	        m->mpc_apicver);
	return (m->mpc_apicid);
}

static inline ulong ioapic_phys_id_map(ulong phys_map)
{
	/* For clustered we don't have a good way to do this yet - hack */
	return (0x0F);
}

#define WAKE_SECONDARY_VIA_INIT

static inline void setup_portio_remap(void)
{
}

static inline int check_phys_apicid_present(int boot_cpu_physical_apicid)
{
	return (1);
}

#endif /* __ASM_MACH_APIC_H */
