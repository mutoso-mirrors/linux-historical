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
# ifdef CONFIG_MULTIQUAD
#  define TARGET_CPUS 0xf     /* all CPUs in *THIS* quad */
#  define INT_DELIVERY_MODE 0     /* physical delivery on LOCAL quad */
# else
#  define TARGET_CPUS cpu_online_map
#  define INT_DELIVERY_MODE 1     /* logical delivery broadcast to all procs */
# endif
#else
# define INT_DELIVERY_MODE 1     /* logical delivery */
# define TARGET_CPUS 0x01
#endif

#ifndef clustered_apic_mode
 #ifdef CONFIG_MULTIQUAD
  #define clustered_apic_mode (1)
  #define esr_disable (1)
 #else /* !CONFIG_MULTIQUAD */
  #define clustered_apic_mode (0)
  #define esr_disable (0)
 #endif /* CONFIG_MULTIQUAD */
#endif 

#ifdef CONFIG_SMP
#ifndef __ASSEMBLY__

/*
 * Private routines/data
 */
 
extern void smp_alloc_memory(void);
extern unsigned long phys_cpu_present_map;
extern unsigned long cpu_online_map;
extern volatile unsigned long smp_invalidate_needed;
extern int pic_mode;
extern int smp_num_siblings;
extern int cpu_sibling_map[];

extern void smp_flush_tlb(void);
extern void smp_message_irq(int cpl, void *dev_id, struct pt_regs *regs);
extern void smp_send_reschedule(int cpu);
extern void smp_send_reschedule_all(void);
extern void smp_invalidate_rcv(void);		/* Process an NMI */
extern void (*mtrr_hook) (void);
extern void zap_low_mappings (void);

/*
 * Some lowlevel functions might want to know about
 * the real APIC ID <-> CPU # mapping.
 */
#define MAX_APICID 256
extern volatile int cpu_to_physical_apicid[NR_CPUS];
extern volatile int physical_apicid_to_cpu[MAX_APICID];
extern volatile int cpu_to_logical_apicid[NR_CPUS];
extern volatile int logical_apicid_to_cpu[MAX_APICID];

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
#define smp_processor_id() (current_thread_info()->cpu)

#define cpu_online(cpu) (cpu_online_map & (1<<(cpu)))

extern inline unsigned int num_online_cpus(void)
{
	return hweight32(cpu_online_map);
}

extern inline int any_online_cpu(unsigned int mask)
{
	if (mask & cpu_online_map)
		return __ffs(mask & cpu_online_map);

	return -1;
}

static __inline int hard_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_ID(*(unsigned long *)(APIC_BASE+APIC_ID));
}

static __inline int logical_smp_processor_id(void)
{
	/* we don't want to mark this access volatile - bad code generation */
	return GET_APIC_LOGICAL_ID(*(unsigned long *)(APIC_BASE+APIC_LDR));
}

#endif /* !__ASSEMBLY__ */

#define NO_PROC_ID		0xFF		/* No processor magic marker */

#endif
#endif
