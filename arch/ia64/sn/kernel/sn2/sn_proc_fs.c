/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000-2003 Silicon Graphics, Inc. All rights reserved.
 */
#include <linux/config.h>
#include <asm/uaccess.h>

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <asm/sn/sn_sal.h>


static int partition_id_read_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data) {

	return sprintf(page, "%d\n", sn_local_partid());
}

static struct proc_dir_entry * sgi_proc_dir;

void
register_sn_partition_id(void) {
	struct proc_dir_entry *entry;

	if (!sgi_proc_dir) {
		sgi_proc_dir = proc_mkdir("sgi_sn", 0);
	}
	entry = create_proc_entry("partition_id", 0444, sgi_proc_dir);
	if (entry) {
		entry->nlink = 1;
		entry->data = 0;
		entry->read_proc = partition_id_read_proc;
		entry->write_proc = NULL;
	}
}

static int
system_serial_number_read_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data) {
	return sprintf(page, "%s\n", sn_system_serial_number());
}

static int
licenseID_read_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data) {
	return sprintf(page, "0x%lx\n",sn_partition_serial_number_val());
}

void
register_sn_serial_numbers(void) {
	struct proc_dir_entry *entry;

	if (!sgi_proc_dir) {
		sgi_proc_dir = proc_mkdir("sgi_sn", 0);
	}
	entry = create_proc_entry("system_serial_number", 0444, sgi_proc_dir);
	if (entry) {
		entry->nlink = 1;
		entry->data = 0;
		entry->read_proc = system_serial_number_read_proc;
		entry->write_proc = NULL;
	}
	entry = create_proc_entry("licenseID", 0444, sgi_proc_dir);
	if (entry) {
		entry->nlink = 1;
		entry->data = 0;
		entry->read_proc = licenseID_read_proc;
		entry->write_proc = NULL;
	}
}

// Disable forced interrupts, but leave the code in, just in case.
int sn_force_interrupt_flag = 0;

static int
sn_force_interrupt_read_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data) {
	if (sn_force_interrupt_flag) {
		return sprintf(page, "Force interrupt is enabled\n");
	}
	return sprintf(page, "Force interrupt is disabled\n");
}

static int 
sn_force_interrupt_write_proc(struct file *file, const char *buffer,
                                        unsigned long count, void *data)
{
	if (*buffer == '0') {
		sn_force_interrupt_flag = 0;
	} else {
		sn_force_interrupt_flag = 1;
	}
	return 1;
}

void
register_sn_force_interrupt(void) {
	struct proc_dir_entry *entry;

	if (!sgi_proc_dir) {
		sgi_proc_dir = proc_mkdir("sgi_sn", 0);
	}
	entry = create_proc_entry("sn_force_interrupt",0444, sgi_proc_dir);
	if (entry) {
		entry->nlink = 1;
		entry->data = 0;
		entry->read_proc = sn_force_interrupt_read_proc;
		entry->write_proc = sn_force_interrupt_write_proc;
	}
}

extern int sn_linkstats_get(char *);
extern int sn_linkstats_reset(unsigned long);

static int
sn_linkstats_read_proc(char *page, char **start, off_t off,
		int count, int *eof, void *data) {
       
	return sn_linkstats_get(page);
}

static int 
sn_linkstats_write_proc(struct file *file, const char *buffer,
                                        unsigned long count, void *data)
{
	char		s[64];
	unsigned long	msecs;
	int		e = count;

	if (copy_from_user(s, buffer, count < sizeof(s) ? count : sizeof(s)))
		e = -EFAULT;
	else {
		if (sscanf(s, "%lu", &msecs) != 1 || msecs < 5)
			/* at least 5 milliseconds between updates */
			e = -EINVAL;
		else
			sn_linkstats_reset(msecs);
	}

	return e;
}

void
register_sn_linkstats(void) {
	struct proc_dir_entry *entry;

	if (!sgi_proc_dir) {
		sgi_proc_dir = proc_mkdir("sgi_sn", 0);
	}
	entry = create_proc_entry("linkstats", 0444, sgi_proc_dir);
	if (entry) {
		entry->nlink = 1;
		entry->data = 0;
		entry->read_proc = sn_linkstats_read_proc;
		entry->write_proc = sn_linkstats_write_proc;
	}
}

void
register_sn_procfs(void) {
	register_sn_partition_id();
	register_sn_serial_numbers();
	register_sn_force_interrupt();
	register_sn_linkstats();
}

#endif /* CONFIG_PROC_FS */
