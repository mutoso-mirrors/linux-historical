#ifndef __ASM_SMP_H
#define __ASM_SMP_H

/*
 * We need the APIC definitions automatically as part of 'smp.h'
 */
#ifndef __ASSEMBLY__
#include <linux/config.h>
#include <linux/threads.h>
#include <linux/ptrace.h>
#endif

#ifdef CONFIG_X86_LOCAL_APIC
#ifndef __ASSEMBLY__
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <asm/mpspec.h>
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif
#include <asm/apic.h>
#endif
#endif

#ifdef CONFIG_SMP
#ifndef ASSEMBLY

#include <asm/pda.h>

/*
 * Private routines/data
 */
 
extern void smp_alloc_memory(void);
extern unsigned long phys_cpu_present_map;
extern unsigned long cpu_online_map;
extern volatile unsigned long smp_invalidate_needed;
extern int pic_mode;
extern void smp_flush_tlb(void);
extern void smp_message_irq(int cpl, void *dev_id, struct pt_regs *regs);
extern void smp_send_reschedule(int cpu);
extern void smp_send_reschedule_all(void);
extern void smp_invalidate_rcv(void);		/* Process an NMI */
extern void (*mtrr_hook) (void);
extern void zap_low_mappings (void);

/*
 * On x86 all CPUs are mapped 1:1 to the APIC space.
 * This simplifies scheduling and IPI sending and
 * compresses data structures.
 */
extern inline int cpu_logical_map(int cpu)
{
	return cpu;
}
extern inline int cpu_number_map(int cpu)
{
	return cpu;
}

/*
 * Some lowlevel functions might want to know about
 * the real APIC ID <-> CPU # mapping.
 */
extern volatile int x86_apicid_to_cpu[NR_CPUS];
extern volatile int x86_cpu_to_apicid[NR_CPUS];

/*
 * General functions that each host system must provide.
 */
 
extern void smp_boot_cpus(void);
extern void smp_store_cpu_info(int id);		/* Store per CPU info (like the initial udelay numbers */

/*
 * This function is needed by all SMP systems. It must _always_ be valid
 * from the initial startup. We map APIC_BASE very early in page_setup(),
 * so this is correct in the x86 case.
 */

#define smp_processor_id() read_pda(cpunumber)

extern __inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(unsigned long *)(APIC_BASE+APIC_ID));
}

#endif /* !ASSEMBLY */

#define NO_PROC_ID		0xFF		/* No processor magic marker */



#endif
#define INT_DELIVERY_MODE 1     /* logical delivery */
#define TARGET_CPUS 1
#endif
