/**
 * @file nmi_int.c
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/smp.h>
#include <linux/oprofile.h>
#include <linux/pm.h>
#include <asm/thread_info.h>
#include <asm/nmi.h>
#include <asm/ptrace.h>
#include <asm/msr.h>
#include <asm/apic.h>
#include <asm/bitops.h>
#include <asm/processor.h>
 
#include "op_counter.h"
#include "op_x86_model.h"
 
static struct op_x86_model_spec const * model;
static struct op_msrs cpu_msrs[NR_CPUS];
static unsigned long saved_lvtpc[NR_CPUS];
static unsigned long kernel_only;
 
static int nmi_start(void);
static void nmi_stop(void);

static struct pm_dev * oprofile_pmdev;
 
/* We're at risk of causing big trouble unless we
 * make sure to not cause any NMI interrupts when
 * suspended.
 */
static int oprofile_pm_callback(struct pm_dev * dev,
		pm_request_t rqst, void * data)
{
	switch (rqst) {
		case PM_SUSPEND:
			nmi_stop();
			break;
		case PM_RESUME:
			nmi_start();
			break;
	}
	return 0;
}
 
 
// FIXME: kernel_only
static int nmi_callback(struct pt_regs * regs, int cpu)
{
	return (model->check_ctrs(cpu, &cpu_msrs[cpu], regs));
}
 
 
static void nmi_save_registers(struct op_msrs * msrs)
{
	unsigned int const nr_ctrs = model->num_counters;
	unsigned int const nr_ctrls = model->num_controls; 
	struct op_msr_group * counters = &msrs->counters;
	struct op_msr_group * controls = &msrs->controls;
	int i;

	for (i = 0; i < nr_ctrs; ++i) {
		rdmsr(counters->addrs[i],
			counters->saved[i].low,
			counters->saved[i].high);
	}
 
	for (i = 0; i < nr_ctrls; ++i) {
		rdmsr(controls->addrs[i],
			controls->saved[i].low,
			controls->saved[i].high);
	}
}

 
static void nmi_cpu_setup(void * dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
	model->fill_in_addresses(msrs);
	nmi_save_registers(msrs);
	spin_lock(&oprofilefs_lock);
	model->setup_ctrs(msrs);
	spin_unlock(&oprofilefs_lock);
	saved_lvtpc[cpu] = apic_read(APIC_LVTPC);
	apic_write(APIC_LVTPC, APIC_DM_NMI);
}
 

static int nmi_setup(void)
{
	/* We walk a thin line between law and rape here.
	 * We need to be careful to install our NMI handler
	 * without actually triggering any NMIs as this will
	 * break the core code horrifically.
	 */
	smp_call_function(nmi_cpu_setup, NULL, 0, 1);
	nmi_cpu_setup(0);
	set_nmi_callback(nmi_callback);
	oprofile_pmdev = set_nmi_pm_callback(oprofile_pm_callback);
	return 0;
}


static void nmi_restore_registers(struct op_msrs * msrs)
{
	unsigned int const nr_ctrs = model->num_counters;
	unsigned int const nr_ctrls = model->num_controls; 
	struct op_msr_group * counters = &msrs->counters;
	struct op_msr_group * controls = &msrs->controls;
	int i;

	for (i = 0; i < nr_ctrls; ++i) {
		wrmsr(controls->addrs[i],
			controls->saved[i].low,
			controls->saved[i].high);
	}
 
	for (i = 0; i < nr_ctrs; ++i) {
		wrmsr(counters->addrs[i],
			counters->saved[i].low,
			counters->saved[i].high);
	}
}
 

static void nmi_cpu_shutdown(void * dummy)
{
	int cpu = smp_processor_id();
	struct op_msrs * msrs = &cpu_msrs[cpu];
	apic_write(APIC_LVTPC, saved_lvtpc[cpu]);
	nmi_restore_registers(msrs);
}

 
static void nmi_shutdown(void)
{
	unset_nmi_pm_callback(oprofile_pmdev);
	unset_nmi_callback();
	smp_call_function(nmi_cpu_shutdown, NULL, 0, 1);
	nmi_cpu_shutdown(0);
}

 
static void nmi_cpu_start(void * dummy)
{
	struct op_msrs const * msrs = &cpu_msrs[smp_processor_id()];
	model->start(msrs);
}
 

static int nmi_start(void)
{
	smp_call_function(nmi_cpu_start, NULL, 0, 1);
	nmi_cpu_start(0);
	return 0;
}
 
 
static void nmi_cpu_stop(void * dummy)
{
	struct op_msrs const * msrs = &cpu_msrs[smp_processor_id()];
	model->stop(msrs);
}
 
 
static void nmi_stop(void)
{
	smp_call_function(nmi_cpu_stop, NULL, 0, 1);
	nmi_cpu_stop(0);
}


struct op_counter_config counter_config[OP_MAX_COUNTER];

static int nmi_create_files(struct super_block * sb, struct dentry * root)
{
	int i;

	for (i = 0; i < model->num_counters; ++i) {
		struct dentry * dir;
		char buf[2];
 
		snprintf(buf, 2, "%d", i);
		dir = oprofilefs_mkdir(sb, root, buf);
		oprofilefs_create_ulong(sb, dir, "enabled", &counter_config[i].enabled); 
		oprofilefs_create_ulong(sb, dir, "event", &counter_config[i].event); 
		oprofilefs_create_ulong(sb, dir, "count", &counter_config[i].count); 
		oprofilefs_create_ulong(sb, dir, "unit_mask", &counter_config[i].unit_mask); 
		oprofilefs_create_ulong(sb, dir, "kernel", &counter_config[i].kernel); 
		oprofilefs_create_ulong(sb, dir, "user", &counter_config[i].user); 
	}

	oprofilefs_create_ulong(sb, root, "kernel_only", &kernel_only);
	return 0;
}
 
 
struct oprofile_operations nmi_ops = {
	.create_files 	= nmi_create_files,
	.setup 		= nmi_setup,
	.shutdown	= nmi_shutdown,
	.start		= nmi_start,
	.stop		= nmi_stop
};
 
 
int __init nmi_init(struct oprofile_operations ** ops, enum oprofile_cpu * cpu)
{
	__u8 vendor = current_cpu_data.x86_vendor;
	__u8 family = current_cpu_data.x86;
	__u8 cpu_model = current_cpu_data.x86_model;
 
	if (!cpu_has_apic)
		return 0;
 
	switch (vendor) {
		case X86_VENDOR_AMD:
			/* Needs to be at least an Athlon (or hammer in 32bit mode) */
			if (family < 6)
				return 0;
			model = &op_athlon_spec;
			*cpu = OPROFILE_CPU_ATHLON;
			break;
 
		case X86_VENDOR_INTEL:
			/* Less than a P6-class processor */
			if (family != 6)
				return 0;
	 
			if (cpu_model > 5) {
				*cpu = OPROFILE_CPU_PIII;
			} else if (cpu_model > 2) {
				*cpu = OPROFILE_CPU_PII;
			} else {
				*cpu = OPROFILE_CPU_PPRO;
			}
 
			model = &op_ppro_spec;
			break;

		default:
			return 0;
	}

	*ops = &nmi_ops;
	printk(KERN_INFO "oprofile: using NMI interrupt.\n");
	return 1;
}
