/*
 *	linux/kernel/resource.c
 *
 * Copyright (C) 1999	Linus Torvalds
 *
 * Arbitrary resource management.
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/malloc.h>

#include <asm/spinlock.h>

struct resource ioport_resource = { "PCI IO", 0x0000, 0xFFFF };
struct resource iomem_resource = { "PCI mem", 0x00000000, 0xFFFFFFFF };

static rwlock_t resource_lock = RW_LOCK_UNLOCKED;

/*
 * This generates reports for /proc/ioports and /proc/memory
 */
static char * do_resource_list(struct resource *entry, const char *fmt, int offset, char *buf, char *end)
{
	if (offset < 0)
		offset = 0;

	while (entry) {
		const char *name = entry->name;
		unsigned long from, to;

		if ((int) (end-buf) < 80)
			return buf;

		from = entry->start;
		to = entry->end;
		if (!name)
			name = "<BAD>";

		buf += sprintf(buf, fmt + offset, from, to, name);
		if (entry->child)
			buf = do_resource_list(entry->child, fmt, offset-2, buf, end);
		entry = entry->sibling;
	}

	return buf;
}

int get_resource_list(struct resource *root, char *buf, int size)
{
	char *fmt;
	int retval;

	fmt = "        %08lx-%08lx : %s\n";
	if (root == &ioport_resource)
		fmt = "        %04lx-%04lx : %s\n";
	read_lock(&resource_lock);
	retval = do_resource_list(root->child, fmt, 8, buf, buf + size) - buf;
	read_unlock(&resource_lock);
	return retval;
}	

/* Return the conflict entry if you can't request it */
static struct resource * __request_resource(struct resource *root, struct resource *new)
{
	unsigned long start = new->start;
	unsigned long end = new->end;
	struct resource *tmp, **p;

	if (end < start)
		return root;
	if (start < root->start)
		return root;
	if (end > root->end)
		return root;
	p = &root->child;
	for (;;) {
		tmp = *p;
		if (!tmp || tmp->start > end) {
			new->sibling = tmp;
			*p = new;
			new->parent = root;
			return NULL;
		}
		p = &tmp->sibling;
		if (tmp->end < start)
			continue;
		return tmp;
	}
}

int request_resource(struct resource *root, struct resource *new)
{
	struct resource *conflict;

	write_lock(&resource_lock);
	conflict = __request_resource(root, new);
	write_unlock(&resource_lock);
	return conflict ? -EBUSY : 0;
}

int release_resource(struct resource *old)
{
	struct resource *tmp, **p;

	p = &old->parent->child;
	for (;;) {
		tmp = *p;
		if (!tmp)
			break;
		if (tmp == old) {
			*p = tmp->sibling;
			old->parent = NULL;
			return 0;
		}
		p = &tmp->sibling;
	}
	return -EINVAL;
}

/*
 * This is compatibility stuff for IO resources.
 *
 * Note how this, unlike the above, knows about
 * the IO flag meanings (busy etc).
 *
 * Request-region creates a new busy region.
 *
 * Check-region returns non-zero if the area is already busy
 *
 * Release-region releases a matching busy region.
 */
struct resource * __request_region(struct resource *parent, unsigned long start, unsigned long n, const char *name)
{
	struct resource *res = kmalloc(sizeof(*res), GFP_KERNEL);

	if (res) {
		memset(res, 0, sizeof(*res));
		res->name = name;
		res->start = start;
		res->end = start + n - 1;
		res->flags = IORESOURCE_BUSY;

		write_lock(&resource_lock);

		while (!(parent->flags & IORESOURCE_BUSY)) {
			struct resource *conflict;

			conflict = __request_resource(parent, res);
			if (!conflict)
				break;
			if (conflict != parent) {
				parent = conflict;
				continue;
			}

			/* Uhhuh, that didn't work out.. */
			kfree(res);
			res = NULL;
			break;
		}
		write_unlock(&resource_lock);
	}
	return res;
}

/*
 */
int __check_region(struct resource *parent, unsigned long start, unsigned long n)
{
	struct resource * res;

	res = __request_region(parent, start, n, "check-region");
	if (!res)
		return -EBUSY;

	release_resource(res);
	kfree(res);
	return 0;
}

void __release_region(struct resource *parent, unsigned long start, unsigned long n)
{
	struct resource **p;
	unsigned long end;

	p = &parent->child;
	end = start + n - 1;

	for (;;) {
		struct resource *res = *p;

		if (!res)
			break;
		if (res->start <= start && res->end >= end) {
			if (!(res->flags & IORESOURCE_BUSY)) {
				p = &res->child;
				continue;
			}
			if (res->start != start || res->end != end)
				break;
			*p = res->sibling;
			kfree(res);
			return;
		}
		p = &res->sibling;
	}
	printk("Trying to free nonexistent resource <%04lx-%04lx>\n", start, end);
}

/*
 * Called from init/main.c to reserve IO ports.
 */
#define MAXRESERVE 4
static int __init reserve_setup(char *str)
{
	int opt = 2, io_start, io_num;
	static int reserved = 0;
	static struct resource reserve[MAXRESERVE];

    while (opt==2) {
		int x = reserved;

        if (get_option (&str, &io_start) != 2) break;
        if (get_option (&str, &io_num)   == 0) break;
		if (x < MAXRESERVE) {
			struct resource *res = reserve + x;
			res->name = "reserved";
			res->start = io_start;
			res->end = io_start + io_num - 1;
			res->child = NULL;
			if (request_resource(&ioport_resource, res) == 0)
				reserved = x+1;
		}
	}
	return 1;
}

__setup("reserve=", reserve_setup);
