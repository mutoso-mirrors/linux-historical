#ifndef __ASM_MACH_MPPARSE_H
#define __ASM_MACH_MPPARSE_H

static inline void mpc_oem_bus_info(struct mpc_config_bus *m, char *name, 
				struct mpc_config_translation *translation)
{
	Dprintk("Bus #%d is %s\n", m->mpc_busid, name);
}

static inline void mpc_oem_pci_bus(struct mpc_config_bus *m, 
				struct mpc_config_translation *translation)
{
}

static inline void mps_oem_check(struct mp_config_table *mpc, char *oem, 
		char *productid)
{
	if (!strncmp(oem, "IBM ENSW", 8) && !strncmp(str, "VIGIL SMP", 9))
		x86_summit = 1;
}

/* Hook from generic ACPI tables.c */
static inline void acpi_madt_oem_check(char *oem_id, char *oem_table_id)
{
	if (!strncmp(oem_id, "IBM", 3) && !strncmp(oem_table_id, "SERVIGIL", 8))
		x86_summit = 1;
}
#endif /* __ASM_MACH_MPPARSE_H */
