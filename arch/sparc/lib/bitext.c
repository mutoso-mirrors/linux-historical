/*
 * bitext.c: kernel little helper (of bit shuffling variety).
 *
 * Copyright (C) 2002 Pete Zaitcev <zaitcev@yahoo.com>
 *
 * The algorithm to search a zero bit string is geared towards its application.
 * We expect a couple of fixed sizes of requests, so a rotating counter, reset
 * by align size, should provide fast enough search while maintaining low
 * fragmentation.
 */

#include <linux/smp_lock.h>

#include <asm/bitext.h>
#include <asm/bitops.h>

/**
 * bit_map_string_get - find and set a bit string in bit map.
 * @t: the bit map.
 * @len: requested string length
 * @align: requested alignment
 *
 * Returns offset in the map or -1 if out of space.
 *
 * Not safe to call from an interrupt (uses spin_lock).
 */
int bit_map_string_get(struct bit_map *t, int len, int align)
{
	int offset, count;	/* siamese twins */
	int off_new;
	int align1;
	int i;

	if (align == 0)
		align = 1;
	align1 = align - 1;
	if ((align & align1) != 0)
		BUG();
	if (align < 0 || align >= t->size)
		BUG();
	if (len <= 0 || len > t->size)
		BUG();

	spin_lock(&t->lock);
	offset = t->last_off & ~align1;
	count = 0;
	for (;;) {
		off_new = find_next_zero_bit(t->map, t->size, offset);
		off_new = (off_new + align1) & ~align1;
		count += off_new - offset;
		offset = off_new;
		if (offset >= t->size)
			offset = 0;
		if (count + len > t->size) {
			spin_unlock(&t->lock);
/* P3 */ printk(KERN_ERR
  "bitmap out: size %d used %d off %d len %d align %d count %d\n",
  t->size, t->used, offset, len, align, count);
			return -1;
		}

		if (offset + len > t->size) {
			offset = 0;
			count += t->size - offset;
			continue;
		}

		i = 0;
		while (test_bit(offset + i, t->map) == 0) {
			i++;
			if (i == len) {
				for (i = 0; i < len; i++)
					__set_bit(offset + i, t->map);
				if ((t->last_off = offset + len) >= t->size)
					t->last_off = 0;
				t->used += len;
				spin_unlock(&t->lock);
				return offset;
			}
		}
		count += i + 1;
		if ((offset += i + 1) >= t->size)
			offset = 0;
	}
}

void bit_map_clear(struct bit_map *t, int offset, int len)
{
	int i;

	if (t->used < len)
		BUG();		/* Much too late to do any good, but alas... */
	spin_lock(&t->lock);
	for (i = 0; i < len; i++) {
		if (test_bit(offset + i, t->map) == 0)
			BUG();
		__clear_bit(offset + i, t->map);
	}
	t->used -= len;
	spin_unlock(&t->lock);
}

void bit_map_init(struct bit_map *t, unsigned long *map, int size)
{

	if ((size & 07) != 0)
		BUG();
	memset(map, 0, size>>3);

	memset(t, 0, sizeof *t);
	spin_lock_init(&t->lock);
	t->map = map;
	t->size = size;
}
