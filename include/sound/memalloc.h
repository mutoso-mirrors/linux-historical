/*
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *                   Takashi Iwai <tiwai@suse.de>
 * 
 *  Generic memory allocators
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#ifndef __SOUND_MEMALLOC_H
#define __SOUND_MEMALLOC_H

#include <linux/pci.h>
#ifdef CONFIG_SBUS
#include <asm/sbus.h>
#endif

struct device;

/*
 * buffer device info
 */
struct snd_dma_device {
	int type;			/* SNDRV_MEM_TYPE_XXX */
	union {
		void *data;
		struct device *dev;	/* generic device */
		struct pci_dev *pci;	/* PCI device */
		unsigned int flags;	/* GFP_XXX for continous and ISA types */
#ifdef CONFIG_SBUS
		struct sbus_dev *sbus;	/* for SBUS type */
#endif
	} dev;
	unsigned int id;		/* a unique ID */
};

/*
 * buffer types
 */
#define SNDRV_DMA_TYPE_UNKNOWN		0	/* not defined */
#define SNDRV_DMA_TYPE_CONTINUOUS	1	/* continuous no-DMA memory */
#define SNDRV_DMA_TYPE_ISA		2	/* ISA continuous */
#define SNDRV_DMA_TYPE_PCI		3	/* PCI continuous */
#define SNDRV_DMA_TYPE_PCI_SG		4	/* PCI SG-buffer */
#define SNDRV_DMA_TYPE_DEV		5	/* generic device continuous */
#define SNDRV_DMA_TYPE_DEV_SG		6	/* generic device SG-buffer */
#define SNDRV_DMA_TYPE_SBUS		7	/* SBUS continuous */

/*
 * info for buffer allocation
 */
struct snd_dma_buffer {
	unsigned char *area;	/* virtual pointer */
	dma_addr_t addr;	/* physical address */
	size_t bytes;		/* buffer size in bytes */
	void *private_data;	/* private for allocator; don't touch */
};

/*
 * Scatter-Gather generic device pages
 */
struct snd_sg_page {
	void *buf;
	dma_addr_t addr;
};

struct snd_sg_buf {
	int size;	/* allocated byte size */
	int pages;	/* allocated pages */
	int tblsize;	/* allocated table size */
	struct snd_sg_page *table;	/* address table */
	struct page **page_table;	/* page table (for vmap/vunmap) */
	const struct snd_dma_device *dev;
};

/*
 * return the pages matching with the given byte size
 */
static inline unsigned int snd_sgbuf_aligned_pages(size_t size)
{
	return (size + PAGE_SIZE - 1) >> PAGE_SHIFT;
}

/*
 * return the physical address at the corresponding offset
 */
static inline dma_addr_t snd_sgbuf_get_addr(struct snd_sg_buf *sgbuf, size_t offset)
{
	return sgbuf->table[offset >> PAGE_SHIFT].addr + offset % PAGE_SIZE;
}


/* snd_dma_device management */
void snd_dma_device_init(const struct snd_dma_device *dev, int type, void *data);

/* allocate/release a buffer */
int snd_dma_alloc_pages(const struct snd_dma_device *dev, size_t size,
			struct snd_dma_buffer *dmab);
int snd_dma_alloc_pages_fallback(const struct snd_dma_device *dev, size_t size,
                                 struct snd_dma_buffer *dmab);
void snd_dma_free_pages(const struct snd_dma_device *dev, struct snd_dma_buffer *dmab);

/* buffer-preservation managements */
size_t snd_dma_get_reserved(const struct snd_dma_device *dev, struct snd_dma_buffer *dmab);
int snd_dma_free_reserved(const struct snd_dma_device *dev);
int snd_dma_set_reserved(const struct snd_dma_device *dev, struct snd_dma_buffer *dmab);

/* basic memory allocation functions */
void *snd_malloc_pages(size_t size, unsigned int gfp_flags);
void *snd_malloc_pages_fallback(size_t size, unsigned int gfp_flags, size_t *res_size);
void snd_free_pages(void *ptr, size_t size);

#endif /* __SOUND_MEMALLOC_H */

