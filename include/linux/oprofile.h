/**
 * @file oprofile.h
 *
 * API for machine-specific interrupts to interface
 * to oprofile.
 *
 * @remark Copyright 2002 OProfile authors
 * @remark Read the file COPYING
 *
 * @author John Levon <levon@movementarian.org>
 */

#ifndef OPROFILE_H
#define OPROFILE_H

#include <linux/types.h>
#include <linux/spinlock.h>
#include <asm/atomic.h>
 
struct super_block;
struct dentry;
struct file_operations;
 
enum oprofile_cpu {
	OPROFILE_CPU_PPRO,
	OPROFILE_CPU_PII,
	OPROFILE_CPU_PIII,
	OPROFILE_CPU_ATHLON,
	OPROFILE_CPU_TIMER,
	OPROFILE_CPU_RTC,
	OPROFILE_CPU_P4,
	OPROFILE_CPU_IA64,
	OPROFILE_CPU_IA64_1,
	OPROFILE_CPU_IA64_2,
	OPROFILE_CPU_HAMMER,
	OPROFILE_CPU_AXP_EV4,
	OPROFILE_CPU_AXP_EV5,
	OPROFILE_CPU_AXP_PCA56,
	OPROFILE_CPU_AXP_EV6,
	OPROFILE_CPU_AXP_EV67,
};

/* Operations structure to be filled in */
struct oprofile_operations {
	/* create any necessary configuration files in the oprofile fs.
	 * Optional. */
	int (*create_files)(struct super_block * sb, struct dentry * root);
	/* Do any necessary interrupt setup. Optional. */
	int (*setup)(void);
	/* Do any necessary interrupt shutdown. Optional. */
	void (*shutdown)(void);
	/* Start delivering interrupts. */
	int (*start)(void);
	/* Stop delivering interrupts. */
	void (*stop)(void);
};

/**
 * One-time initialisation. *ops must be set to a filled-in
 * operations structure. oprofile_cpu_type must be set.
 * Return 0 on success.
 */
int oprofile_arch_init(struct oprofile_operations ** ops, enum oprofile_cpu * cpu);
 
/**
 * Add a sample. This may be called from any context. Pass
 * smp_processor_id() as cpu.
 */
extern void oprofile_add_sample(unsigned long eip, unsigned long event, int cpu);

/**
 * Create a file of the given name as a child of the given root, with
 * the specified file operations.
 */
int oprofilefs_create_file(struct super_block * sb, struct dentry * root,
	char const * name, struct file_operations * fops);
 
/** Create a file for read/write access to an unsigned long. */
int oprofilefs_create_ulong(struct super_block * sb, struct dentry * root,
	char const * name, ulong * val);
 
/** Create a file for read-only access to an unsigned long. */
int oprofilefs_create_ro_ulong(struct super_block * sb, struct dentry * root,
	char const * name, ulong * val);
 
/** Create a file for read-only access to an atomic_t. */
int oprofilefs_create_ro_atomic(struct super_block * sb, struct dentry * root,
	char const * name, atomic_t * val);
 
/** create a directory */
struct dentry * oprofilefs_mkdir(struct super_block * sb, struct dentry * root,
	char const * name);

/**
 * Convert an unsigned long value into ASCII and copy it to the user buffer @buf,
 * updating *offset appropriately. Returns bytes written or -EFAULT.
 */
ssize_t oprofilefs_ulong_to_user(unsigned long * val, char * buf, size_t count, loff_t * offset);

/**
 * Read an ASCII string for a number from a userspace buffer and fill *val on success.
 * Returns 0 on success, < 0 on error.
 */
int oprofilefs_ulong_from_user(unsigned long * val, char const * buf, size_t count);

/** lock for read/write safety */
extern spinlock_t oprofilefs_lock;
 
#endif /* OPROFILE_H */
