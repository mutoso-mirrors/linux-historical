/* io-unit.h: Definitions for the sun4d IO-UNIT.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */
#ifndef _SPARC_IO_UNIT_H
#define _SPARC_IO_UNIT_H

#include <asm/page.h>
#include <asm/spinlock.h>

/* The io-unit handles all virtual to physical address translations
 * that occur between the SBUS and physical memory.  Access by
 * the cpu to IO registers and similar go over the xdbus so are
 * translated by the on chip SRMMU.  The io-unit and the srmmu do
 * not need to have the same translations at all, in fact most
 * of the time the translations they handle are a disjunct set.
 * Basically the io-unit handles all dvma sbus activity.
 */
 
/* AIEEE, unlike the nice sun4m, these monsters have 
   fixed DMA range 64M */
 
#define IOUNIT_DMA_BASE	    0xfc000000 /* TOP - 64M */
#define IOUNIT_DMA_SIZE	    0x04000000 /* 64M */
/* We use last 4M for sparc_dvma_malloc */
#define IOUNIT_DVMA_SIZE    0x00400000 /* 4M */

/* The format of an iopte in the external page tables */
#define IOUPTE_PAGE          0xffffff00 /* Physical page number (PA[35:12]) */
#define IOUPTE_CACHE         0x00000080 /* Cached (in Viking/MXCC) */
#define IOUPTE_STREAM        0x00000040
#define IOUPTE_INTRA	     0x00000008 /* Not regular memory - probably direct sbus<->sbus dma 
					   FIXME: Play with this and find out how we can make use of this */
#define IOUPTE_WRITE         0x00000004 /* Writeable */
#define IOUPTE_VALID         0x00000002 /* IOPTE is valid */
#define IOUPTE_PARITY        0x00000001

struct iounit_struct {
	spinlock_t		iommu_lock;
	iopte_t			*page_table;
};

#endif /* !(_SPARC_IO_UNIT_H) */
