/*
 * kernel/configs.c
 * Echo the kernel .config file used to build the kernel
 *
 * Copyright (C) 2002 Khalid Aziz <khalid_aziz@hp.com>
 * Copyright (C) 2002 Randy Dunlap <rddunlap@osdl.org>
 * Copyright (C) 2002 Al Stone <ahs3@fc.hp.com>
 * Copyright (C) 2002 Hewlett-Packard Company
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/compile.h>
#include <linux/version.h>
#include <asm/uaccess.h>

/**************************************************/
/* the actual current config file                 */

/* This one is for extraction from the kernel binary file image. */
#include "ikconfig.h"

#ifdef CONFIG_IKCONFIG_PROC

/* This is the data that can be read from /proc/config.gz. */
#include "config_data.h"

/**************************************************/
/* globals and useful constants                   */

static const char IKCONFIG_VERSION[] __initdata = "0.7";

static ssize_t
ikconfig_read_current(struct file *file, char __user *buf,
		      size_t len, loff_t * offset)
{
	loff_t pos = *offset;
	ssize_t count;

	if (pos >= kernel_config_data_size)
		return 0;

	count = min(len, (size_t)(kernel_config_data_size - pos));
	if(copy_to_user(buf, kernel_config_data + pos, count))
		return -EFAULT;

	*offset += count;
	return count;
}

static struct file_operations ikconfig_file_ops = {
	.owner = THIS_MODULE,
	.read = ikconfig_read_current,
};

/***************************************************/
/* ikconfig_init: start up everything we need to */

static int __init ikconfig_init(void)
{
	struct proc_dir_entry *entry;

	printk(KERN_INFO "ikconfig %s with /proc/config*\n",
	       IKCONFIG_VERSION);

	/* create the current config file */
	entry = create_proc_entry("config.gz", S_IFREG | S_IRUGO,
				  &proc_root);
	if (!entry)
		return -ENOMEM;

	entry->proc_fops = &ikconfig_file_ops;
	entry->size = kernel_config_data_size;

	return 0;
}

/***************************************************/
/* ikconfig_cleanup: clean up our mess           */

static void __exit ikconfig_cleanup(void)
{
	remove_proc_entry("config.gz", &proc_root);
}

module_init(ikconfig_init);
module_exit(ikconfig_cleanup);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Randy Dunlap");
MODULE_DESCRIPTION("Echo the kernel .config file used to build the kernel");

#endif /* CONFIG_IKCONFIG_PROC */
