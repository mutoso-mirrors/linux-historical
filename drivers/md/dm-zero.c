/*
 * Copyright (C) 2003 Christophe Saout <christophe@saout.de>
 *
 * This file is released under the GPL.
 */

#include "dm.h"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/bio.h>

/*
 * Construct a dummy mapping that only returns zeros
 */
static int zero_ctr(struct dm_target *ti, unsigned int argc, char **argv)
{
	if (argc != 0) {
		ti->error = "dm-zero: No arguments required";
		return -EINVAL;
	}

	return 0;
}

/*
 * Fills the bio pages with zeros
 */
static void zero_fill_bio(struct bio *bio)
{
	unsigned long flags;
	struct bio_vec *bv;
	int i;

	bio_for_each_segment(bv, bio, i) {
		char *data = bvec_kmap_irq(bv, &flags);
		memset(data, 0, bv->bv_len);
		bvec_kunmap_irq(bv, &flags);
	}
}

/*
 * Return zeros only on reads
 */
static int zero_map(struct dm_target *ti, struct bio *bio,
		      union map_info *map_context)
{
	switch(bio_rw(bio)) {
	case READ:
		zero_fill_bio(bio);
		break;
	case READA:
		/* readahead of null bytes only wastes buffer cache */
		return -EIO;
	case WRITE:
		/* writes get silently dropped */
		break;
	}

	bio_endio(bio, bio->bi_size, 0);

	/* accepted bio, don't make new request */
	return 0;
}

static struct target_type zero_target = {
	.name   = "zero",
	.version = {1, 0, 0},
	.module = THIS_MODULE,
	.ctr    = zero_ctr,
	.map    = zero_map,
};

int __init dm_zero_init(void)
{
	int r = dm_register_target(&zero_target);

	if (r < 0)
		DMERR("zero: register failed %d", r);

	return r;
}

void __exit dm_zero_exit(void)
{
	int r = dm_unregister_target(&zero_target);

	if (r < 0)
		DMERR("zero: unregister failed %d", r);
}

module_init(dm_zero_init)
module_exit(dm_zero_exit)

MODULE_AUTHOR("Christophe Saout <christophe@saout.de>");
MODULE_DESCRIPTION(DM_NAME " dummy target returning zeros");
MODULE_LICENSE("GPL");
