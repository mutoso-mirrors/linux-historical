/*
 * video1394.c - video driver for OHCI 1394 boards
 * Copyright (C)1999,2000 Sebastien Rougeaux <sebastien.rougeaux@anu.edu.au>
 *                        Peter Schlaile <udbz@rz.uni-karlsruhe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/wait.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/smp_lock.h>
#include <asm/byteorder.h>
#include <asm/atomic.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/tqueue.h>
#include <linux/delay.h>
#include <linux/devfs_fs_kernel.h>

#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <linux/types.h>
#include <linux/wrapper.h>
#include <linux/vmalloc.h>
#include <linux/timex.h>
#include <linux/mm.h>

#include "ieee1394.h"
#include "ieee1394_types.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "video1394.h"

#include "ohci1394.h"

#define ISO_CHANNELS 64
#define ISO_RECEIVE 0
#define ISO_TRANSMIT 1

#ifndef virt_to_page
#define virt_to_page(x) MAP_NR(x)
#endif

#ifndef vmalloc_32
#define vmalloc_32(x) vmalloc(x)
#endif

struct it_dma_prg {
	struct dma_cmd begin;
	quadlet_t data[4];
	struct dma_cmd end;
	quadlet_t pad[4]; /* FIXME: quick hack for memory alignment */
};

struct dma_iso_ctx {
	struct ti_ohci *ohci;
	int type; /* ISO_TRANSMIT or ISO_RECEIVE */
	int ctx;
	int channel;
	int last_buffer;
	int * next_buffer;  /* For ISO Transmit of video packets
			       to write the correct SYT field
			       into the next block */
	unsigned int num_desc;
	unsigned int buf_size;
	unsigned int frame_size;
	unsigned int packet_size;
	unsigned int left_size;
	unsigned int nb_cmd;
	unsigned char *buf;
        struct dma_cmd **ir_prg;
	struct it_dma_prg **it_prg;
	unsigned int *buffer_status;
        struct timeval *buffer_time; /* time when the buffer was received */
	unsigned int *last_used_cmd; /* For ISO Transmit with 
					variable sized packets only ! */
	int ctrlClear;
	int ctrlSet;
	int cmdPtr;
	int ctxMatch;
	wait_queue_head_t waitq;
	spinlock_t lock;
	unsigned int syt_offset;
	int flags;
};

struct video_card {
	struct ti_ohci *ohci;
	struct list_head list;
	int id;
	devfs_handle_t devfs;

	struct dma_iso_ctx **ir_context;
	struct dma_iso_ctx **it_context;
	struct dma_iso_ctx *current_ctx;
};

#ifdef CONFIG_IEEE1394_VERBOSEDEBUG
#define VIDEO1394_DEBUG
#endif

#ifdef DBGMSG
#undef DBGMSG
#endif

#ifdef VIDEO1394_DEBUG
#define DBGMSG(card, fmt, args...) \
printk(KERN_INFO "video1394_%d: " fmt "\n" , card , ## args)
#else
#define DBGMSG(card, fmt, args...)
#endif

/* print general (card independent) information */
#define PRINT_G(level, fmt, args...) \
printk(level "video1394: " fmt "\n" , ## args)

/* print card specific information */
#define PRINT(level, card, fmt, args...) \
printk(level "video1394_%d: " fmt "\n" , card , ## args)

static void irq_handler(int card, quadlet_t isoRecvIntEvent, 
			quadlet_t isoXmitIntEvent, void *data);

static LIST_HEAD(video1394_cards);
static spinlock_t video1394_cards_lock = SPIN_LOCK_UNLOCKED;

static devfs_handle_t devfs_handle;
static struct hpsb_highlevel *hl_handle = NULL;

/* Code taken from bttv.c */

/*******************************/
/* Memory management functions */
/*******************************/

#define MDEBUG(x)	do { } while(0)		/* Debug memory management */

/* [DaveM] I've recoded most of this so that:
 * 1) It's easier to tell what is happening
 * 2) It's more portable, especially for translating things
 *    out of vmalloc mapped areas in the kernel.
 * 3) Less unnecessary translations happen.
 *
 * The code used to assume that the kernel vmalloc mappings
 * existed in the page tables of every process, this is simply
 * not guaranteed.  We now use pgd_offset_k which is the
 * defined way to get at the kernel page tables.
 */

static inline unsigned long uvirt_to_bus(unsigned long adr) 
{
        unsigned long kva, ret;

        kva = page_address(vmalloc_to_page(adr));
	ret = virt_to_bus((void *)kva);
        MDEBUG(printk("uv2b(%lx-->%lx)", adr, ret));
        return ret;
}

static inline unsigned long kvirt_to_bus(unsigned long adr) 
{
        unsigned long va, kva, ret;

        va = VMALLOC_VMADDR(adr);
	kva = page_address(vmalloc_to_page(va));
	ret = virt_to_bus((void *)kva);
        MDEBUG(printk("kv2b(%lx-->%lx)", adr, ret));
        return ret;
}

/* Here we want the physical address of the memory.
 * This is used when initializing the contents of the
 * area and marking the pages as reserved.
 */
static inline unsigned long kvirt_to_pa(unsigned long adr) 
{
        unsigned long va, kva, ret;

        va = VMALLOC_VMADDR(adr);
	kva = page_address(vmalloc_to_page(va));
	ret = __pa(kva);
        MDEBUG(printk("kv2pa(%lx-->%lx)", adr, ret));
        return ret;
}

static void * rvmalloc(unsigned long size)
{
	void * mem;
	unsigned long adr, page;
        
	mem=vmalloc_32(size);
	if (mem) 
	{
		memset(mem, 0, size); /* Clear the ram out, 
					 no junk to the user */
	        adr=(unsigned long) mem;
		while (size > 0) 
                {
	                page = kvirt_to_pa(adr);
			mem_map_reserve(virt_to_page(__va(page)));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
	}
	return mem;
}

static void rvfree(void * mem, unsigned long size)
{
        unsigned long adr, page;
        
	if (mem) 
	{
	        adr=(unsigned long) mem;
		while (size > 0) 
                {
	                page = kvirt_to_pa(adr);
			mem_map_unreserve(virt_to_page(__va(page)));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
}
/* End of code taken from bttv.c */

static int free_dma_iso_ctx(struct dma_iso_ctx **d)
{
	int i;
	struct ti_ohci *ohci;
	unsigned long *usage;
	
	if ((*d)==NULL) return -1;

	ohci = (struct ti_ohci *)(*d)->ohci;

	DBGMSG(ohci->id, "Freeing dma_iso_ctx %d", (*d)->ctx);

	ohci1394_stop_context(ohci, (*d)->ctrlClear, NULL);

	if ((*d)->buf) rvfree((void *)(*d)->buf, 
			      (*d)->num_desc * (*d)->buf_size);

	if ((*d)->ir_prg) {
		for (i=0;i<(*d)->num_desc;i++) 
			if ((*d)->ir_prg[i]) kfree((*d)->ir_prg[i]);
		kfree((*d)->ir_prg);
	}

	if ((*d)->it_prg) {
		for (i=0;i<(*d)->num_desc;i++) 
			if ((*d)->it_prg[i]) kfree((*d)->it_prg[i]);
		kfree((*d)->it_prg);
	}

	if ((*d)->buffer_status)
		kfree((*d)->buffer_status);
	if ((*d)->buffer_time)
		kfree((*d)->buffer_time);
	if ((*d)->last_used_cmd)
		kfree((*d)->last_used_cmd);
	if ((*d)->next_buffer)
		kfree((*d)->next_buffer);

	usage = ((*d)->type == ISO_RECEIVE) ? &ohci->ir_ctx_usage :
		&ohci->it_ctx_usage;
       
	/* clear the ISO context usage bit */
	clear_bit((*d)->ctx, usage);
	
	kfree(*d);
	*d = NULL;

	return 0;
}

static struct dma_iso_ctx *
alloc_dma_iso_ctx(struct ti_ohci *ohci, int type, int num_desc,
		  int buf_size, int channel, unsigned int packet_size)
{
	struct dma_iso_ctx *d=NULL;
	int i;

	unsigned long *usage = (type == ISO_RECEIVE) ? &ohci->ir_ctx_usage :
		                                       &ohci->it_ctx_usage;

	/* try to claim the ISO context usage bit */
	for (i = 0; i < ohci->nb_iso_rcv_ctx; i++) {
		if (!test_and_set_bit(i, usage)) {
			PRINT(KERN_ERR, ohci->id, "Free iso ctx %d found", i);
			break;
		}
	}

	if (i == ohci->nb_iso_rcv_ctx) {
		PRINT(KERN_ERR, ohci->id, "No DMA contexts available");
		return NULL;
	}
	
	d = (struct dma_iso_ctx *)kmalloc(sizeof(struct dma_iso_ctx), 
					  GFP_KERNEL);
	if (d==NULL) {
		PRINT(KERN_ERR, ohci->id, "Failed to allocate dma_iso_ctx");
		return NULL;
	}

	memset(d, 0, sizeof(struct dma_iso_ctx));

	d->ohci = (void *)ohci;
	d->type = type;
	d->ctx = i;
	d->channel = channel;
	d->num_desc = num_desc;
	d->frame_size = buf_size;
	if (buf_size%PAGE_SIZE) 
		d->buf_size = buf_size + PAGE_SIZE - (buf_size%PAGE_SIZE);
	else
		d->buf_size = buf_size;
	d->last_buffer = -1;
	d->buf = NULL;
	d->ir_prg = NULL;
	init_waitqueue_head(&d->waitq);

	d->buf = rvmalloc(d->num_desc * d->buf_size);

	if (d->buf == NULL) {
		PRINT(KERN_ERR, ohci->id, "Failed to allocate dma buffer");
		free_dma_iso_ctx(&d);
		return NULL;
	}
	memset(d->buf, 0, d->num_desc * d->buf_size);

	if (type == ISO_RECEIVE) {
		d->ctrlSet = OHCI1394_IsoRcvContextControlSet+32*d->ctx;
		d->ctrlClear = OHCI1394_IsoRcvContextControlClear+32*d->ctx;
		d->cmdPtr = OHCI1394_IsoRcvCommandPtr+32*d->ctx;
		d->ctxMatch = OHCI1394_IsoRcvContextMatch+32*d->ctx;

		d->ir_prg = kmalloc(d->num_desc * sizeof(struct dma_cmd *), 
				    GFP_KERNEL);

		if (d->ir_prg == NULL) {
			PRINT(KERN_ERR, ohci->id, 
			      "Failed to allocate dma ir prg");
			free_dma_iso_ctx(&d);
			return NULL;
		}
		memset(d->ir_prg, 0, d->num_desc * sizeof(struct dma_cmd *));
	
		d->nb_cmd = d->buf_size / PAGE_SIZE + 1;
		d->left_size = (d->frame_size % PAGE_SIZE) ?
			d->frame_size % PAGE_SIZE : PAGE_SIZE;

		for (i=0;i<d->num_desc;i++) {
			d->ir_prg[i] = kmalloc(d->nb_cmd * 
					       sizeof(struct dma_cmd), 
					       GFP_KERNEL);
			if (d->ir_prg[i] == NULL) {
				PRINT(KERN_ERR, ohci->id, 
				      "Failed to allocate dma ir prg");
				free_dma_iso_ctx(&d);
				return NULL;
			}
		}
	}
	else {  /* ISO_TRANSMIT */
		d->ctrlSet = OHCI1394_IsoXmitContextControlSet+16*d->ctx;
		d->ctrlClear = OHCI1394_IsoXmitContextControlClear+16*d->ctx;
		d->cmdPtr = OHCI1394_IsoXmitCommandPtr+16*d->ctx;

		d->it_prg = kmalloc(d->num_desc * sizeof(struct it_dma_prg *), 
				    GFP_KERNEL);

		if (d->it_prg == NULL) {
			PRINT(KERN_ERR, ohci->id, 
			      "Failed to allocate dma it prg");
			free_dma_iso_ctx(&d);
			return NULL;
		}
		memset(d->it_prg, 0, d->num_desc*sizeof(struct it_dma_prg *));
		
		d->packet_size = packet_size;

		if (PAGE_SIZE % packet_size || packet_size>4096) {
			PRINT(KERN_ERR, ohci->id, 
			      "Packet size %d (page_size: %ld) "
			      "not yet supported\n",
			      packet_size, PAGE_SIZE);
			free_dma_iso_ctx(&d);
			return NULL;
		}

		d->nb_cmd = d->frame_size / d->packet_size;
		if (d->frame_size % d->packet_size) {
			d->nb_cmd++;
			d->left_size = d->frame_size % d->packet_size;
		}
		else
			d->left_size = d->packet_size;

		for (i=0;i<d->num_desc;i++) {
			d->it_prg[i] = kmalloc(d->nb_cmd * 
					       sizeof(struct it_dma_prg), 
					       GFP_KERNEL);
			if (d->it_prg[i] == NULL) {
				PRINT(KERN_ERR, ohci->id, 
				      "Failed to allocate dma it prg");
				free_dma_iso_ctx(&d);
				return NULL;
			}
		}
	}

	d->buffer_status = kmalloc(d->num_desc * sizeof(unsigned int),
				   GFP_KERNEL);
	d->buffer_time = kmalloc(d->num_desc * sizeof(struct timeval),
				   GFP_KERNEL);
	d->last_used_cmd = kmalloc(d->num_desc * sizeof(unsigned int),
				   GFP_KERNEL);
	d->next_buffer = kmalloc(d->num_desc * sizeof(int),
				 GFP_KERNEL);

	if (d->buffer_status == NULL) {
		PRINT(KERN_ERR, ohci->id, "Failed to allocate buffer_status");
		free_dma_iso_ctx(&d);
		return NULL;
	}
	if (d->buffer_time == NULL) {
		PRINT(KERN_ERR, ohci->id, "Failed to allocate buffer_time");
		free_dma_iso_ctx(&d);
		return NULL;
	}
	if (d->last_used_cmd == NULL) {
		PRINT(KERN_ERR, ohci->id, "Failed to allocate last_used_cmd");
		free_dma_iso_ctx(&d);
		return NULL;
	}
	if (d->next_buffer == NULL) {
		PRINT(KERN_ERR, ohci->id, "Failed to allocate next_buffer");
		free_dma_iso_ctx(&d);
		return NULL;
	}
	memset(d->buffer_status, 0, d->num_desc * sizeof(unsigned int));
	memset(d->buffer_time, 0, d->num_desc * sizeof(struct timeval));
	memset(d->last_used_cmd, 0, d->num_desc * sizeof(unsigned int));
	memset(d->next_buffer, -1, d->num_desc * sizeof(int));
	
        spin_lock_init(&d->lock);

	PRINT(KERN_INFO, ohci->id, "Iso %s DMA: %d buffers "
	      "of size %d allocated for a frame size %d, each with %d prgs",
	      (type==ISO_RECEIVE) ? "receive" : "transmit",
	      d->num_desc, d->buf_size, d->frame_size, d->nb_cmd);

	return d;
}

static void reset_ir_status(struct dma_iso_ctx *d, int n)
{
	int i;
	d->ir_prg[n][0].status = 4;
	d->ir_prg[n][1].status = PAGE_SIZE-4;
	for (i=2;i<d->nb_cmd-1;i++)
		d->ir_prg[n][i].status = PAGE_SIZE;
	d->ir_prg[n][i].status = d->left_size;
}

static void initialize_dma_ir_prg(struct dma_iso_ctx *d, int n, int flags)
{
	struct dma_cmd *ir_prg = d->ir_prg[n];
	unsigned long buf = (unsigned long)d->buf+n*d->buf_size;
	int i;

	/* the first descriptor will read only 4 bytes */
	ir_prg[0].control = DMA_CTL_INPUT_MORE | DMA_CTL_UPDATE |
		DMA_CTL_BRANCH | 4;

	/* set the sync flag */
	if (flags & VIDEO1394_SYNC_FRAMES)
		ir_prg[0].control |= DMA_CTL_WAIT;

	ir_prg[0].address = kvirt_to_bus(buf);
	ir_prg[0].branchAddress =  (virt_to_bus(&(ir_prg[1].control)) 
				    & 0xfffffff0) | 0x1;

	/* the second descriptor will read PAGE_SIZE-4 bytes */
	ir_prg[1].control = DMA_CTL_INPUT_MORE | DMA_CTL_UPDATE |
		DMA_CTL_BRANCH | (PAGE_SIZE-4);
	ir_prg[1].address = kvirt_to_bus(buf+4);
	ir_prg[1].branchAddress =  (virt_to_bus(&(ir_prg[2].control)) 
				    & 0xfffffff0) | 0x1;
	
	for (i=2;i<d->nb_cmd-1;i++) {
		ir_prg[i].control = DMA_CTL_INPUT_MORE | DMA_CTL_UPDATE | 
			DMA_CTL_BRANCH | PAGE_SIZE;
		ir_prg[i].address = kvirt_to_bus(buf+(i-1)*PAGE_SIZE);

		ir_prg[i].branchAddress =  
			(virt_to_bus(&(ir_prg[i+1].control)) 
			 & 0xfffffff0) | 0x1;
	}

	/* the last descriptor will generate an interrupt */
	ir_prg[i].control = DMA_CTL_INPUT_MORE | DMA_CTL_UPDATE | 
		DMA_CTL_IRQ | DMA_CTL_BRANCH | d->left_size;
	ir_prg[i].address = kvirt_to_bus(buf+(i-1)*PAGE_SIZE);
}
	
static void initialize_dma_ir_ctx(struct dma_iso_ctx *d, int tag, int flags)
{
	struct ti_ohci *ohci = (struct ti_ohci *)d->ohci;
	int i;

	d->flags = flags;

	ohci1394_stop_context(ohci, d->ctrlClear, NULL);

	for (i=0;i<d->num_desc;i++) {
		initialize_dma_ir_prg(d, i, flags);
		reset_ir_status(d, i);
	}

	/* reset the ctrl register */
	reg_write(ohci, d->ctrlClear, 0xf0000000);

	/* Set bufferFill */
	reg_write(ohci, d->ctrlSet, 0x80000000);

	/* Set isoch header */
	if (flags & VIDEO1394_INCLUDE_ISO_HEADERS) 
		reg_write(ohci, d->ctrlSet, 0x40000000);

	/* Set the context match register to match on all tags, 
	   sync for sync tag, and listen to d->channel */
	reg_write(ohci, d->ctxMatch, 0xf0000000|((tag&0xf)<<8)|d->channel);
	
	/* Set up isoRecvIntMask to generate interrupts */
	reg_write(ohci, OHCI1394_IsoRecvIntMaskSet, 1<<d->ctx);
}

/* find which context is listening to this channel */
static struct dma_iso_ctx **
ir_ctx_listening(struct video_card *video, int channel)
{
	int i;
	struct ti_ohci *ohci = video->ohci;

	for (i = 0; i < ohci->nb_iso_rcv_ctx-1; i++) 
		if (video->ir_context[i] &&
		    video->ir_context[i]->channel == channel)
			return &video->ir_context[i];
		
	PRINT(KERN_ERR, ohci->id, "No iso context is listening to channel %d",
	      channel);

	return NULL;
}

static struct dma_iso_ctx **
it_ctx_talking(struct video_card *video, int channel)
{
	int i;
	struct ti_ohci *ohci = video->ohci;

	for (i = 0; i < ohci->nb_iso_xmit_ctx; i++) 
		if (video->it_context[i] &&
		    video->it_context[i]->channel==channel)
			return &video->ir_context[i];
		
	PRINT(KERN_ERR, ohci->id, "No iso context is talking to channel %d",
	      channel);

	return NULL;
}

int wakeup_dma_ir_ctx(struct ti_ohci *ohci, struct dma_iso_ctx *d) 
{
	int i;

	if (d==NULL) {
		PRINT(KERN_ERR, ohci->id, "Iso receive event received but "
		      "context not allocated");
		return -EFAULT;
	}

	spin_lock(&d->lock);
	for (i=0;i<d->num_desc;i++) {
		if (d->ir_prg[i][d->nb_cmd-1].status & 0xFFFF0000) {
			reset_ir_status(d, i);
			d->buffer_status[i] = VIDEO1394_BUFFER_READY;
			do_gettimeofday(&d->buffer_time[i]);
		}
	}
	spin_unlock(&d->lock);
	if (waitqueue_active(&d->waitq)) wake_up_interruptible(&d->waitq);
	return 0;
}

static inline void put_timestamp(struct ti_ohci *ohci, struct dma_iso_ctx * d,
				 int n)
{
	unsigned char* buf = d->buf + n * d->buf_size;
	u32 cycleTimer;
	u32 timeStamp;

	if (n == -1) {
	  return;
	}

	cycleTimer = reg_read(ohci, OHCI1394_IsochronousCycleTimer);

	timeStamp = ((cycleTimer & 0x0fff) + d->syt_offset); /* 11059 = 450 us */
	timeStamp = (timeStamp % 3072 + ((timeStamp / 3072) << 12)
		+ (cycleTimer & 0xf000)) & 0xffff;
	
	buf[6] = timeStamp >> 8; 
	buf[7] = timeStamp & 0xff; 

    /* if first packet is empty packet, then put timestamp into the next full one too */
    if ( (d->it_prg[n][0].data[1] >>16) == 0x008) {
   	    buf += d->packet_size;
    	buf[6] = timeStamp >> 8;
	    buf[7] = timeStamp & 0xff;
	}

    /* do the next buffer frame too in case of irq latency */
	n = d->next_buffer[n];
	if (n == -1) {
	  return;
	}
	buf = d->buf + n * d->buf_size;

	timeStamp += (d->last_used_cmd[n] << 12) & 0xffff;

	buf[6] = timeStamp >> 8;
	buf[7] = timeStamp & 0xff;

    /* if first packet is empty packet, then put timestamp into the next full one too */
    if ( (d->it_prg[n][0].data[1] >>16) == 0x008) {
   	    buf += d->packet_size;
    	buf[6] = timeStamp >> 8;
	    buf[7] = timeStamp & 0xff;
	}

#if 0
	printk("curr: %d, next: %d, cycleTimer: %08x timeStamp: %08x\n",
	       curr, n, cycleTimer, timeStamp);
#endif	
}

int wakeup_dma_it_ctx(struct ti_ohci *ohci, struct dma_iso_ctx *d) 
{
	int i;

	if (d==NULL) {
		PRINT(KERN_ERR, ohci->id, "Iso transmit event received but "
		      "context not allocated");
		return -EFAULT;
	}

	spin_lock(&d->lock);
	for (i=0;i<d->num_desc;i++) {
		if (d->it_prg[i][d->last_used_cmd[i]].end.status& 0xFFFF0000) {
			int next = d->next_buffer[i];
			put_timestamp(ohci, d, next);
			d->it_prg[i][d->last_used_cmd[i]].end.status = 0;
			d->buffer_status[i] = VIDEO1394_BUFFER_READY;
		}
	}
	spin_unlock(&d->lock);
	if (waitqueue_active(&d->waitq)) wake_up_interruptible(&d->waitq);
	return 0;
}

static void initialize_dma_it_prg(struct dma_iso_ctx *d, int n, int sync_tag)
{
	struct it_dma_prg *it_prg = d->it_prg[n];
	unsigned long buf = (unsigned long)d->buf+n*d->buf_size;
	int i;
	d->last_used_cmd[n] = d->nb_cmd - 1;
	for (i=0;i<d->nb_cmd;i++) {
				 
		it_prg[i].begin.control = DMA_CTL_OUTPUT_MORE |
			DMA_CTL_IMMEDIATE | 8 ;
		it_prg[i].begin.address = 0;
		
		it_prg[i].begin.status = 0;
		
		it_prg[i].data[0] = 
			(SPEED_100 << 16) 
			| (/* tag */ 1 << 14)
			| (d->channel << 8) 
			| (TCODE_ISO_DATA << 4);
		if (i==0) it_prg[i].data[0] |= sync_tag;
		it_prg[i].data[1] = d->packet_size << 16;
		it_prg[i].data[2] = 0;
		it_prg[i].data[3] = 0;
		
		it_prg[i].end.control = DMA_CTL_OUTPUT_LAST | DMA_CTL_BRANCH;
		it_prg[i].end.address =
			kvirt_to_bus(buf+i*d->packet_size);

		if (i<d->nb_cmd-1) {
			it_prg[i].end.control |= d->packet_size;
			it_prg[i].begin.branchAddress = 
				(virt_to_bus(&(it_prg[i+1].begin.control)) 
				 & 0xfffffff0) | 0x3;
			it_prg[i].end.branchAddress = 
				(virt_to_bus(&(it_prg[i+1].begin.control)) 
				 & 0xfffffff0) | 0x3;
		}
		else {
			/* the last prg generates an interrupt */
			it_prg[i].end.control |= DMA_CTL_UPDATE | 
				DMA_CTL_IRQ | d->left_size;
			/* the last prg doesn't branch */
			it_prg[i].begin.branchAddress = 0;
			it_prg[i].end.branchAddress = 0;
		}
		it_prg[i].end.status = 0;

#if 0
		printk("%d:%d: %08x-%08x ctrl %08x brch %08x d0 %08x d1 %08x\n",n,i,
		       virt_to_bus(&(it_prg[i].begin.control)),
		       virt_to_bus(&(it_prg[i].end.control)),
		       it_prg[i].end.control,
		       it_prg[i].end.branchAddress,
		       it_prg[i].data[0], it_prg[i].data[1]);
#endif
	}
}

static void initialize_dma_it_prg_var_packet_queue(
	struct dma_iso_ctx *d, int n, unsigned int * packet_sizes,
	struct ti_ohci *ohci)
{
	struct it_dma_prg *it_prg = d->it_prg[n];
	int i;

#if 0
	if (n != -1) {
		put_timestamp(ohci, d, n);
	}
#endif
	d->last_used_cmd[n] = d->nb_cmd - 1;

	for (i = 0; i < d->nb_cmd; i++) {
		unsigned int size;
		if (packet_sizes[i] > d->packet_size) {
			size = d->packet_size;
		} else {
			size = packet_sizes[i];
		}
		it_prg[i].data[1] = size << 16; 
		it_prg[i].end.control = DMA_CTL_OUTPUT_LAST | DMA_CTL_BRANCH;

		if (i < d->nb_cmd-1 && packet_sizes[i+1] != 0) {
			it_prg[i].end.control |= size;
			it_prg[i].begin.branchAddress = 
				(virt_to_bus(&(it_prg[i+1].begin.control)) 
				 & 0xfffffff0) | 0x3;
			it_prg[i].end.branchAddress = 
				(virt_to_bus(&(it_prg[i+1].begin.control)) 
				 & 0xfffffff0) | 0x3;
		} else {
			/* the last prg generates an interrupt */
			it_prg[i].end.control |= DMA_CTL_UPDATE | 
				DMA_CTL_IRQ | size;
			/* the last prg doesn't branch */
			it_prg[i].begin.branchAddress = 0;
			it_prg[i].end.branchAddress = 0;
			d->last_used_cmd[n] = i;
			break;
		}
	}
}

static void initialize_dma_it_ctx(struct dma_iso_ctx *d, int sync_tag,
				  unsigned int syt_offset, int flags)
{
	struct ti_ohci *ohci = (struct ti_ohci *)d->ohci;
	int i;

	d->flags = flags;
	d->syt_offset = (syt_offset == 0 ? 11000 : syt_offset);

	ohci1394_stop_context(ohci, d->ctrlClear, NULL);

	for (i=0;i<d->num_desc;i++)
		initialize_dma_it_prg(d, i, sync_tag);
	
	/* Set up isoRecvIntMask to generate interrupts */
	reg_write(ohci, OHCI1394_IsoXmitIntMaskSet, 1<<d->ctx);
}

static int do_iso_mmap(struct vm_area_struct *vma, struct ti_ohci *ohci, struct dma_iso_ctx *d, 
		       const char *adr, unsigned long size)
{
        unsigned long start=(unsigned long) adr;
        unsigned long page,pos;

        if (size>d->num_desc * d->buf_size) {
		PRINT(KERN_ERR, ohci->id, 
		      "iso context %d buf size is different from mmap size", 
		      d->ctx);
                return -EINVAL;
	}
        if (!d->buf) {
		PRINT(KERN_ERR, ohci->id, 
		      "iso context %d is not allocated", d->ctx);
		return -EINVAL;
	}

        pos=(unsigned long) d->buf;
        while (size > 0) {
                page = kvirt_to_pa(pos);
                if (remap_page_range(vma, start, page, PAGE_SIZE, PAGE_SHARED))
                        return -EAGAIN;
                start+=PAGE_SIZE;
                pos+=PAGE_SIZE;
                size-=PAGE_SIZE;
        }
        return 0;
}

static int video1394_ioctl(struct inode *inode, struct file *file,
			   unsigned int cmd, unsigned long arg)
{
	struct video_card *video = NULL;
	struct ti_ohci *ohci = NULL;
	unsigned long flags;
	struct list_head *lh;

	spin_lock_irqsave(&video1394_cards_lock, flags);
	if (!list_empty(&video1394_cards)) {
		struct video_card *p;
		list_for_each(lh, &video1394_cards) {
			p = list_entry(lh, struct video_card, list);
			if (p->id == ieee1394_file_to_instance(file)) {
				video = p;
				ohci = video->ohci;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&video1394_cards_lock, flags);

	if (video == NULL) {
		PRINT_G(KERN_ERR, "%s: Unknown video card for minor %d",
			__FUNCTION__, ieee1394_file_to_instance(file));
		return -EFAULT;
	}

	switch(cmd)
	{
	case VIDEO1394_LISTEN_CHANNEL:
	case VIDEO1394_TALK_CHANNEL:
	{
		struct video1394_mmap v;
		u64 mask;
		int i;

		if(copy_from_user(&v, (void *)arg, sizeof(v)))
			return -EFAULT;

		/* if channel < 0, find lowest available one */
		if (v.channel < 0) {
		    mask = (u64)0x1;
		    for (i=0; i<ISO_CHANNELS; i++) {
			if (!(ohci->ISO_channel_usage & mask)) {
			    v.channel = i;
			    PRINT(KERN_INFO, ohci->id, "Found free channel %d", i);
			    break;
			}
			mask = mask << 1;
		    }
		}
		    
		if (v.channel<0 || v.channel>(ISO_CHANNELS-1)) {
			PRINT(KERN_ERR, ohci->id, 
			      "Iso channel %d out of bounds", v.channel);
			return -EFAULT;
		}
		mask = (u64)0x1<<v.channel;
		printk("mask: %08X%08X usage: %08X%08X\n",
		       (u32)(mask>>32),(u32)(mask&0xffffffff),
		       (u32)(ohci->ISO_channel_usage>>32),
		       (u32)(ohci->ISO_channel_usage&0xffffffff));
		if (ohci->ISO_channel_usage & mask) {
			PRINT(KERN_ERR, ohci->id, 
			      "Channel %d is already taken", v.channel);
			return -EFAULT;
		}
		ohci->ISO_channel_usage |= mask;
		
		if (v.buf_size<=0) {
			PRINT(KERN_ERR, ohci->id,
			      "Invalid %d length buffer requested",v.buf_size);
			return -EFAULT;
		}

		if (v.nb_buffers<=0) {
			PRINT(KERN_ERR, ohci->id,
			      "Invalid %d buffers requested",v.nb_buffers);
			return -EFAULT;
		}

		if (v.nb_buffers * v.buf_size > VIDEO1394_MAX_SIZE) {
			PRINT(KERN_ERR, ohci->id, 
			      "%d buffers of size %d bytes is too big", 
			      v.nb_buffers, v.buf_size);
			return -EFAULT;
		}

		if (cmd == VIDEO1394_LISTEN_CHANNEL) {
			/* find a free iso receive context */
			for (i=0;i<ohci->nb_iso_rcv_ctx-1;i++) 
				if (video->ir_context[i]==NULL) break;
			    
			if (i==(ohci->nb_iso_rcv_ctx-1)) {
				PRINT(KERN_ERR, ohci->id, 
				      "No iso context available");
				return -EFAULT;
			}

			video->ir_context[i] = 
				alloc_dma_iso_ctx(ohci, ISO_RECEIVE,
						  v.nb_buffers, v.buf_size, 
						  v.channel, 0);

			if (video->ir_context[i] == NULL) {
				PRINT(KERN_ERR, ohci->id, 
				      "Couldn't allocate ir context");
				return -EFAULT;
			}
			initialize_dma_ir_ctx(video->ir_context[i], 
					      v.sync_tag, v.flags);

			video->current_ctx = video->ir_context[i];

			v.buf_size = video->ir_context[i]->buf_size;

			PRINT(KERN_INFO, ohci->id, 
			      "iso context %d listen on channel %d",
			      video->current_ctx->ctx, v.channel);
		}
		else {
			/* find a free iso transmit context */
			for (i=0;i<ohci->nb_iso_xmit_ctx;i++) 
				if (video->it_context[i]==NULL) break;
			    
			if (i==ohci->nb_iso_xmit_ctx) {
				PRINT(KERN_ERR, ohci->id, 
				      "No iso context available");
				return -EFAULT;
			}
			
			video->it_context[i] = 
				alloc_dma_iso_ctx(ohci, ISO_TRANSMIT,
						  v.nb_buffers, v.buf_size, 
						  v.channel, v.packet_size);

			if (video->it_context[i] == NULL) {
				PRINT(KERN_ERR, ohci->id, 
				      "Couldn't allocate it context");
				return -EFAULT;
			}
			initialize_dma_it_ctx(video->it_context[i], 
					      v.sync_tag, v.syt_offset, v.flags);

			video->current_ctx = video->it_context[i];

			v.buf_size = video->it_context[i]->buf_size;

			PRINT(KERN_INFO, ohci->id, 
			      "Iso context %d talk on channel %d", i, 
			      v.channel);
		}

		if(copy_to_user((void *)arg, &v, sizeof(v)))
			return -EFAULT;

		return 0;
	}
	case VIDEO1394_UNLISTEN_CHANNEL: 
	case VIDEO1394_UNTALK_CHANNEL:
	{
		int channel;
		u64 mask;

		if(copy_from_user(&channel, (void *)arg, sizeof(int)))
			return -EFAULT;

		if (channel<0 || channel>(ISO_CHANNELS-1)) {
			PRINT(KERN_ERR, ohci->id, 
			      "Iso channel %d out of bound", channel);
			return -EFAULT;
		}
		mask = (u64)0x1<<channel;
		if (!(ohci->ISO_channel_usage & mask)) {
			PRINT(KERN_ERR, ohci->id, 
			      "Channel %d is not being used", channel);
			return -EFAULT;
		}
		ohci->ISO_channel_usage &= ~mask;

		if (cmd == VIDEO1394_UNLISTEN_CHANNEL) {
			struct dma_iso_ctx **d;
			d = ir_ctx_listening(video, channel);
			if (d == NULL) return -EFAULT;
			PRINT(KERN_INFO, ohci->id, 
			      "Iso context %d stop listening on channel %d", 
			      (*d)->ctx, channel);
			free_dma_iso_ctx(d);
		}
		else {
			struct dma_iso_ctx **d;
			d = it_ctx_talking(video, channel);
			if (d == NULL) return -EFAULT;
			PRINT(KERN_INFO, ohci->id, 
			      "Iso context %d stop talking on channel %d", 
			      (*d)->ctx, channel);
			free_dma_iso_ctx(d);

		}
		
		return 0;
	}
	case VIDEO1394_LISTEN_QUEUE_BUFFER:
	{
		struct video1394_wait v;
		struct dma_iso_ctx *d, **dd;

		if(copy_from_user(&v, (void *)arg, sizeof(v)))
			return -EFAULT;

		dd = ir_ctx_listening(video, v.channel);
		if (dd == NULL) return -EFAULT;
		d = *dd;

		if ((v.buffer<0) || (v.buffer>d->num_desc)) {
			PRINT(KERN_ERR, ohci->id, 
			      "Buffer %d out of range",v.buffer);
			return -EFAULT;
		}
		
		spin_lock_irqsave(&d->lock,flags);

		if (d->buffer_status[v.buffer]==VIDEO1394_BUFFER_QUEUED) {
			PRINT(KERN_ERR, ohci->id, 
			      "Buffer %d is already used",v.buffer);
			spin_unlock_irqrestore(&d->lock,flags);
			return -EFAULT;
		}
		
		d->buffer_status[v.buffer]=VIDEO1394_BUFFER_QUEUED;

		if (d->last_buffer>=0) 
			d->ir_prg[d->last_buffer][d->nb_cmd-1].branchAddress = 
				(virt_to_bus(&(d->ir_prg[v.buffer][0].control)) 
				 & 0xfffffff0) | 0x1;

		d->last_buffer = v.buffer;

		d->ir_prg[d->last_buffer][d->nb_cmd-1].branchAddress = 0;

		spin_unlock_irqrestore(&d->lock,flags);

		if (!(reg_read(ohci, d->ctrlSet) & 0x8000)) 
		{
			DBGMSG(ohci->id, "Starting iso DMA ctx=%d",d->ctx);

			/* Tell the controller where the first program is */
			reg_write(ohci, d->cmdPtr, 
				  virt_to_bus(&(d->ir_prg[v.buffer][0]))|0x1);
			
			/* Run IR context */
			reg_write(ohci, d->ctrlSet, 0x8000);
		}
		else {
			/* Wake up dma context if necessary */
			if (!(reg_read(ohci, d->ctrlSet) & 0x400)) {
				PRINT(KERN_INFO, ohci->id, 
				      "Waking up iso dma ctx=%d", d->ctx);
				reg_write(ohci, d->ctrlSet, 0x1000);
			}
		}
		return 0;
		
	}
	case VIDEO1394_LISTEN_WAIT_BUFFER:
	case VIDEO1394_LISTEN_POLL_BUFFER:
	{
		struct video1394_wait v;
		struct dma_iso_ctx *d, **dd;
		int i;

		if(copy_from_user(&v, (void *)arg, sizeof(v)))
			return -EFAULT;

		dd = ir_ctx_listening(video, v.channel);
		if (dd==NULL) return -EFAULT;
		d = *dd;

		if ((v.buffer<0) || (v.buffer>d->num_desc)) {
			PRINT(KERN_ERR, ohci->id, 
			      "Buffer %d out of range",v.buffer);
			return -EFAULT;
		}

		/*
		 * I change the way it works so that it returns 
		 * the last received frame.
		 */
		spin_lock_irqsave(&d->lock, flags);
		switch(d->buffer_status[v.buffer]) {
		case VIDEO1394_BUFFER_READY:
			d->buffer_status[v.buffer]=VIDEO1394_BUFFER_FREE;
			break;
		case VIDEO1394_BUFFER_QUEUED:
			if (cmd == VIDEO1394_LISTEN_POLL_BUFFER) {
			    /* for polling, return error code EINTR */
			    spin_unlock_irqrestore(&d->lock, flags);
			    return -EINTR;
			}

#if 1
			while(d->buffer_status[v.buffer]!=
			      VIDEO1394_BUFFER_READY) {
				spin_unlock_irqrestore(&d->lock, flags);
				interruptible_sleep_on(&d->waitq);
				spin_lock_irqsave(&d->lock, flags);
				if(signal_pending(current)) {
					spin_unlock_irqrestore(&d->lock,flags);
					return -EINTR;
				}
			}
#else
			if (wait_event_interruptible(d->waitq, 
						     d->buffer_status[v.buffer]
						     == VIDEO1394_BUFFER_READY)
			    == -ERESTARTSYS)
				return -EINTR;
#endif
			d->buffer_status[v.buffer]=VIDEO1394_BUFFER_FREE;
			break;
		default:
			PRINT(KERN_ERR, ohci->id, 
			      "Buffer %d is not queued",v.buffer);
			spin_unlock_irqrestore(&d->lock, flags);
			return -EFAULT;
		}

		/* set time of buffer */
		v.filltime = d->buffer_time[v.buffer];
//		printk("Buffer %d time %d\n", v.buffer, (d->buffer_time[v.buffer]).tv_usec);

		/*
		 * Look ahead to see how many more buffers have been received
		 */
		i=0;
		while (d->buffer_status[(v.buffer+1)%d->num_desc]==
		       VIDEO1394_BUFFER_READY) {
			v.buffer=(v.buffer+1)%d->num_desc;
			i++;
		}
		spin_unlock_irqrestore(&d->lock, flags);

		v.buffer=i;
		if(copy_to_user((void *)arg, &v, sizeof(v)))
			return -EFAULT;

		return 0;
	}
	case VIDEO1394_TALK_QUEUE_BUFFER:
	{
		struct video1394_wait v;
		struct video1394_queue_variable qv;
		struct dma_iso_ctx *d, **dd;

		if(copy_from_user(&v, (void *)arg, sizeof(v)))
			return -EFAULT;

		dd = it_ctx_talking(video, v.channel);
		if (dd == NULL) return -EFAULT;
		d = *dd;

		if ((v.buffer<0) || (v.buffer>d->num_desc)) {
			PRINT(KERN_ERR, ohci->id, 
			      "Buffer %d out of range",v.buffer);
			return -EFAULT;
		}
		
		if (d->flags & VIDEO1394_VARIABLE_PACKET_SIZE) {
			if (copy_from_user(&qv, (void *)arg, sizeof(qv))) 
				return -EFAULT;
			if (!access_ok(VERIFY_READ, qv.packet_sizes, 
				       d->nb_cmd * sizeof(unsigned int))) {
				return -EFAULT;
			}
		}

		spin_lock_irqsave(&d->lock,flags);

		if (d->buffer_status[v.buffer]!=VIDEO1394_BUFFER_FREE) {
			PRINT(KERN_ERR, ohci->id, 
			      "Buffer %d is already used",v.buffer);
			spin_unlock_irqrestore(&d->lock,flags);
			return -EFAULT;
		}
		
		if (d->flags & VIDEO1394_VARIABLE_PACKET_SIZE) {
			initialize_dma_it_prg_var_packet_queue(
				d, v.buffer, qv.packet_sizes,
				ohci);
		}

		d->buffer_status[v.buffer]=VIDEO1394_BUFFER_QUEUED;

		if (d->last_buffer>=0) {
			d->it_prg[d->last_buffer]
				[ d->last_used_cmd[d->last_buffer]
					].end.branchAddress = 
				(virt_to_bus(&(d->it_prg[v.buffer][0].begin.control)) 
				 & 0xfffffff0) | 0x3;

			d->it_prg[d->last_buffer]
				[d->last_used_cmd[d->last_buffer]
					].begin.branchAddress = 
				(virt_to_bus(&(d->it_prg[v.buffer][0].begin.control)) 
				 & 0xfffffff0) | 0x3;
			d->next_buffer[d->last_buffer] = v.buffer;
		}
		d->last_buffer = v.buffer;
		d->next_buffer[d->last_buffer] = -1;

		d->it_prg[d->last_buffer][d->last_used_cmd[d->last_buffer]].end.branchAddress = 0;

		spin_unlock_irqrestore(&d->lock,flags);

		if (!(reg_read(ohci, d->ctrlSet) & 0x8000)) 
		{
			DBGMSG(ohci->id, "Starting iso transmit DMA ctx=%d",
			       d->ctx);
			put_timestamp(ohci, d, d->last_buffer);

			/* Tell the controller where the first program is */
			reg_write(ohci, d->cmdPtr, 
				  virt_to_bus(&(d->it_prg[v.buffer][0]))|0x3);
			
			/* Run IT context */
			reg_write(ohci, d->ctrlSet, 0x8000);
		}
		else {
			/* Wake up dma context if necessary */
			if (!(reg_read(ohci, d->ctrlSet) & 0x400)) {
				PRINT(KERN_INFO, ohci->id, 
				      "Waking up iso transmit dma ctx=%d", 
				      d->ctx);
				put_timestamp(ohci, d, d->last_buffer);
				reg_write(ohci, d->ctrlSet, 0x1000);
			}
		}
		return 0;
		
	}
	case VIDEO1394_TALK_WAIT_BUFFER:
	{
		struct video1394_wait v;
		struct dma_iso_ctx *d, **dd;

		if(copy_from_user(&v, (void *)arg, sizeof(v)))
			return -EFAULT;

		dd = it_ctx_talking(video, v.channel);
		if (dd == NULL) return -EFAULT;
		d = *dd;

		if ((v.buffer<0) || (v.buffer>d->num_desc)) {
			PRINT(KERN_ERR, ohci->id, 
			      "Buffer %d out of range",v.buffer);
			return -EFAULT;
		}

		switch(d->buffer_status[v.buffer]) {
		case VIDEO1394_BUFFER_READY:
			d->buffer_status[v.buffer]=VIDEO1394_BUFFER_FREE;
			return 0;
		case VIDEO1394_BUFFER_QUEUED:
#if 1
			while(d->buffer_status[v.buffer]!=
			      VIDEO1394_BUFFER_READY) {
				interruptible_sleep_on(&d->waitq);
				if(signal_pending(current)) return -EINTR;
			}
#else
			if (wait_event_interruptible(d->waitq, 
						     d->buffer_status[v.buffer]
						     == VIDEO1394_BUFFER_READY)
			    == -ERESTARTSYS)
				return -EINTR;
#endif
			d->buffer_status[v.buffer]=VIDEO1394_BUFFER_FREE;
			return 0;
		default:
			PRINT(KERN_ERR, ohci->id, 
			      "Buffer %d is not queued",v.buffer);
			return -EFAULT;
		}
	}
	default:
		return -EINVAL;
	}
}

/*
 *	This maps the vmalloced and reserved buffer to user space.
 *
 *  FIXME: 
 *  - PAGE_READONLY should suffice!?
 *  - remap_page_range is kind of inefficient for page by page remapping.
 *    But e.g. pte_alloc() does not work in modules ... :-(
 */

int video1394_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct video_card *video = NULL;
	struct ti_ohci *ohci;
	int res = -EINVAL;
	unsigned long flags;
	struct list_head *lh;

        spin_lock_irqsave(&video1394_cards_lock, flags);
        if (!list_empty(&video1394_cards)) {
		struct video_card *p;
		list_for_each(lh, &video1394_cards) {
			p = list_entry(lh, struct video_card, list);
			if (p->id == ieee1394_file_to_instance(file)) {
				video = p;
				break;
			}
                }
        }
        spin_unlock_irqrestore(&video1394_cards_lock, flags);

	if (video == NULL) {
		PRINT_G(KERN_ERR, "%s: Unknown video card for minor %d",
			__FUNCTION__, ieee1394_file_to_instance(file));
		return -EFAULT;
	}

	lock_kernel();
	ohci = video->ohci;

	if (video->current_ctx == NULL) {
		PRINT(KERN_ERR, ohci->id, "Current iso context not set");
	} else
		res = do_iso_mmap(ohci, video->current_ctx, 
			   (char *)vma->vm_start, 
			   (unsigned long)(vma->vm_end-vma->vm_start));
	unlock_kernel();
	return res;
}

static int video1394_open(struct inode *inode, struct file *file)
{
	int i = ieee1394_file_to_instance(file);
	unsigned long flags;
	struct video_card *video = NULL;
	struct list_head *lh;

	spin_lock_irqsave(&video1394_cards_lock, flags);
	if (!list_empty(&video1394_cards)) {
		struct video_card *p;
		list_for_each(lh, &video1394_cards) {
			p = list_entry(lh, struct video_card, list);
			if (p->id == i) {
				video = p;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&video1394_cards_lock, flags);

        if (video == NULL)
                return -EIO;

	V22_COMPAT_MOD_INC_USE_COUNT;

	return 0;
}

static int video1394_release(struct inode *inode, struct file *file)
{
	struct video_card *video = NULL;
	struct ti_ohci *ohci;
	u64 mask;
	int i;
	unsigned long flags;
	struct list_head *lh;

        spin_lock_irqsave(&video1394_cards_lock, flags);
        if (!list_empty(&video1394_cards)) {
		struct video_card *p;
		list_for_each(lh, &video1394_cards) {
			p = list_entry(lh, struct video_card, list);
			if (p->id == ieee1394_file_to_instance(file)) {
				video = p;
				break;
			}
		}
	}
        spin_unlock_irqrestore(&video1394_cards_lock, flags);

	if (video == NULL) {
		PRINT_G(KERN_ERR, "%s: Unknown device for minor %d",
			__FUNCTION__, ieee1394_file_to_instance(file));
		return 1;
	}

	ohci = video->ohci;

	lock_kernel();
	for (i=0;i<ohci->nb_iso_rcv_ctx-1;i++) 
		if (video->ir_context[i]) {
			mask = (u64)0x1<<video->ir_context[i]->channel;
			if (!(ohci->ISO_channel_usage & mask))
				PRINT(KERN_ERR, ohci->id, 
				      "On release: Channel %d is not being used", 
				      video->ir_context[i]->channel);
			else
				ohci->ISO_channel_usage &= ~mask;
			PRINT(KERN_INFO, ohci->id, 
			      "Iso receive context %d stop listening "
			      "on channel %d", video->ir_context[i]->ctx,
			      video->ir_context[i]->channel);
			free_dma_iso_ctx(&video->ir_context[i]);
		}
	
	for (i=0;i<ohci->nb_iso_xmit_ctx;i++) 
		if (video->it_context[i]) {
			mask = (u64)0x1<<video->it_context[i]->channel;
			if (!(ohci->ISO_channel_usage & mask))
				PRINT(KERN_ERR, ohci->id, 
				      "Channel %d is not being used", 
				      video->it_context[i]->channel);
			else
				ohci->ISO_channel_usage &= ~mask;
			PRINT(KERN_INFO, ohci->id, 
			      "Iso transmit context %d stop talking "
			      "on channel %d", video->it_context[i]->ctx,
			      video->it_context[i]->channel);
			free_dma_iso_ctx(&video->it_context[i]);
		}

	V22_COMPAT_MOD_DEC_USE_COUNT;

	unlock_kernel();
	return 0;
}

static void irq_handler(int card, quadlet_t isoRecvIntEvent, 
			quadlet_t isoXmitIntEvent, void *data)
{
	int i;
	struct video_card *video = (struct video_card*) data;

	if (video == NULL) {
		PRINT_G(KERN_ERR, "%s: Unknown card number %d",
			__FUNCTION__, card);
		return;
	}
	
	DBGMSG(card, "Iso event Recv: %08x Xmit: %08x",
	       isoRecvIntEvent, isoXmitIntEvent);

	for (i = 0; i < video->ohci->nb_iso_rcv_ctx-1; i++)
		if (video->ir_context[i] != NULL &&
		    isoRecvIntEvent & (1<<(video->ir_context[i]->ctx)))
			wakeup_dma_ir_ctx(video->ohci, video->ir_context[i]);

	for (i = 0; i < video->ohci->nb_iso_xmit_ctx; i++)
		if (video->it_context[i] != NULL &&
		    isoXmitIntEvent & (1<<(video->it_context[i]->ctx)))
			wakeup_dma_it_ctx(video->ohci, video->it_context[i]);
}

static struct file_operations video1394_fops=
{
        OWNER_THIS_MODULE
	ioctl:		video1394_ioctl,
	mmap:		video1394_mmap,
	open:		video1394_open,
	release:	video1394_release
};

static int video1394_init(struct ti_ohci *ohci)
{
	struct video_card *video = kmalloc(sizeof(struct video_card), GFP_KERNEL);
	unsigned long flags;
	char name[16];

	if (video == NULL) {
		PRINT(KERN_ERR, ohci->id, "Cannot allocate video_card");
		return -1;
	}

	memset(video, 0, sizeof(struct video_card));

	spin_lock_irqsave(&video1394_cards_lock, flags);
	INIT_LIST_HEAD(&video->list);
	list_add_tail(&video->list, &video1394_cards);
	spin_unlock_irqrestore(&video1394_cards_lock, flags);

	if (ohci1394_hook_irq(ohci, irq_handler, (void*) video) != 0) {
		PRINT(KERN_ERR, ohci->id, "ohci1394_hook_irq() failed");
		return -1;
	}

	video->id = ohci->id;
	video->ohci = ohci;

	/* Iso receive dma contexts */
	video->ir_context = (struct dma_iso_ctx **)
		kmalloc((ohci->nb_iso_rcv_ctx-1)*
			sizeof(struct dma_iso_ctx *), GFP_KERNEL);
	if (video->ir_context) 
		memset(video->ir_context, 0,
		       (ohci->nb_iso_rcv_ctx-1)*sizeof(struct dma_iso_ctx *));
	else {
		PRINT(KERN_ERR, ohci->id, "Cannot allocate ir_context");
		return -1;
	}

	/* Iso transmit dma contexts */
	video->it_context = (struct dma_iso_ctx **)
		kmalloc(ohci->nb_iso_xmit_ctx *
			sizeof(struct dma_iso_ctx *), GFP_KERNEL);
	if (video->it_context) 
		memset(video->it_context, 0,
		       ohci->nb_iso_xmit_ctx * sizeof(struct dma_iso_ctx *));
	else {
		PRINT(KERN_ERR, ohci->id, "Cannot allocate it_context");
		return -1;
	}

	sprintf(name, "%d", video->id);
	video->devfs = devfs_register(devfs_handle, name,
				      DEVFS_FL_AUTO_OWNER,
				      IEEE1394_MAJOR,
				      IEEE1394_MINOR_BLOCK_VIDEO1394*16+video->id,
				      S_IFCHR | S_IRUSR | S_IWUSR,
				      &video1394_fops, NULL);

	return 0;
}

/* Must be called under spinlock */
static void remove_card(struct video_card *video)
{
	int i;

	ohci1394_unhook_irq(video->ohci, irq_handler, (void*) video);

	devfs_unregister(video->devfs);

	/* Free the iso receive contexts */
	if (video->ir_context) {
		for (i=0;i<video->ohci->nb_iso_rcv_ctx-1;i++) {
			free_dma_iso_ctx(&video->ir_context[i]);
		}
		kfree(video->ir_context);
	}

	/* Free the iso transmit contexts */
	if (video->it_context) {
		for (i=0;i<video->ohci->nb_iso_xmit_ctx;i++) {
			free_dma_iso_ctx(&video->it_context[i]);
		}
		kfree(video->it_context);
	}
	list_del(&video->list);

	kfree(video);
}

static void video1394_remove_host (struct hpsb_host *host)
{
	struct ti_ohci *ohci;
	unsigned long flags;
	struct list_head *lh, *next;
	struct video_card *p;

	/* We only work with the OHCI-1394 driver */
	if (strcmp(host->driver->name, OHCI1394_DRIVER_NAME))
		return;

	ohci = (struct ti_ohci *)host->hostdata;

        spin_lock_irqsave(&video1394_cards_lock, flags);
	list_for_each_safe(lh, next, &video1394_cards) {
		p = list_entry(lh, struct video_card, list);
		if (p->ohci == ohci) {
			remove_card(p);
			break;
		}
	}
	spin_unlock_irqrestore(&video1394_cards_lock, flags);

	return;
}

static void video1394_add_host (struct hpsb_host *host)
{
	struct ti_ohci *ohci;

	/* We only work with the OHCI-1394 driver */
	if (strcmp(host->driver->name, OHCI1394_DRIVER_NAME))
		return;

	ohci = (struct ti_ohci *)host->hostdata;

	video1394_init(ohci);
	
	return;
}

static struct hpsb_highlevel_ops hl_ops = {
	add_host:	video1394_add_host,
	remove_host:	video1394_remove_host,
};

MODULE_AUTHOR("Sebastien Rougeaux <sebastien.rougeaux@anu.edu.au>");
MODULE_DESCRIPTION("driver for digital video on OHCI board");
MODULE_SUPPORTED_DEVICE(VIDEO1394_DRIVER_NAME);
MODULE_LICENSE("GPL");

static void __exit video1394_exit_module (void)
{
	hpsb_unregister_highlevel (hl_handle);

	devfs_unregister(devfs_handle);
	ieee1394_unregister_chardev(IEEE1394_MINOR_BLOCK_VIDEO1394);
	
	PRINT_G(KERN_INFO, "Removed " VIDEO1394_DRIVER_NAME " module");
}

static int __init video1394_init_module (void)
{
	if (ieee1394_register_chardev(IEEE1394_MINOR_BLOCK_VIDEO1394,
				      THIS_MODULE, &video1394_fops)) {
		PRINT_G(KERN_ERR, "video1394: unable to get minor device block");
 		return -EIO;
 	}
	
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)
	devfs_handle = devfs_mk_dir(NULL, VIDEO1394_DRIVER_NAME,
			strlen(VIDEO1394_DRIVER_NAME), NULL);
#else
	devfs_handle = devfs_mk_dir(NULL, VIDEO1394_DRIVER_NAME, NULL);
#endif

	hl_handle = hpsb_register_highlevel (VIDEO1394_DRIVER_NAME, &hl_ops);
	if (hl_handle == NULL) {
		PRINT_G(KERN_ERR, "No more memory for driver\n");
		devfs_unregister(devfs_handle);
		ieee1394_unregister_chardev(IEEE1394_MINOR_BLOCK_VIDEO1394);
		return -ENOMEM;
	}

	PRINT_G(KERN_INFO, "Installed " VIDEO1394_DRIVER_NAME " module");
	return 0;
}

module_init(video1394_init_module);
module_exit(video1394_exit_module);
