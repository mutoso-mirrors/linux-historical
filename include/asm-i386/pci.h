#ifndef __i386_PCI_H
#define __i386_PCI_H

#ifdef __KERNEL__

/* Can be used to override the logic in pci_scan_bus for skipping
   already-configured bus numbers - to be used for buggy BIOSes
   or architectures with incomplete PCI setup by the loader */

#define pcibios_assign_all_busses()	0

extern unsigned long pci_mem_start;
#define PCIBIOS_MIN_IO		0x1000
#define PCIBIOS_MIN_MEM		(pci_mem_start)

void pcibios_set_master(struct pci_dev *dev);
void pcibios_penalize_isa_irq(int irq);

/* Dynamic DMA mapping stuff.
 * i386 has everything mapped statically.
 */

#include <linux/types.h>
#include <linux/slab.h>
#include <asm/scatterlist.h>
#include <linux/string.h>
#include <asm/io.h>

struct pci_dev;

/* Allocate and map kernel buffer using consistent mode DMA for a device.
 * hwdev should be valid struct pci_dev pointer for PCI devices,
 * NULL for PCI-like buses (ISA, EISA).
 * Returns non-NULL cpu-view pointer to the buffer if successful and
 * sets *dma_addrp to the pci side dma address as well, else *dma_addrp
 * is undefined.
 */
extern void *pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
				  dma_addr_t *dma_handle);

/* Free and unmap a consistent DMA buffer.
 * cpu_addr is what was returned from pci_alloc_consistent,
 * size must be the same as what as passed into pci_alloc_consistent,
 * and likewise dma_addr must be the same as what *dma_addrp was set to.
 *
 * References to the memory and mappings associated with cpu_addr/dma_addr
 * past this call are illegal.
 */
extern void pci_free_consistent(struct pci_dev *hwdev, size_t size,
				void *vaddr, dma_addr_t dma_handle);

/* Map a single buffer of the indicated size for DMA in streaming mode.
 * The 32-bit bus address to use is returned.
 *
 * Once the device is given the dma address, the device owns this memory
 * until either pci_unmap_single or pci_dma_sync_single is performed.
 */
extern inline dma_addr_t pci_map_single(struct pci_dev *hwdev, void *ptr,
					size_t size, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
	return virt_to_bus(ptr);
}

/* Unmap a single streaming mode DMA translation.  The dma_addr and size
 * must match what was provided for in a previous pci_map_single call.  All
 * other usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guarenteed to see
 * whatever the device wrote there.
 */
extern inline void pci_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_addr,
				    size_t size, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
	/* Nothing to do */
}

/* Map a set of buffers described by scatterlist in streaming
 * mode for DMA.  This is the scather-gather version of the
 * above pci_map_single interface.  Here the scatter gather list
 * elements are each tagged with the appropriate dma address
 * and length.  They are obtained via sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for pci_map_single are
 * the same here.
 */
extern inline int pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sg,
			     int nents, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
	return nents;
}

/* Unmap a set of streaming mode DMA translations.
 * Again, cpu read rules concerning calls here are the same as for
 * pci_unmap_single() above.
 */
extern inline void pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sg,
				int nents, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
	/* Nothing to do */
}

/* Make physical memory consistent for a single
 * streaming mode DMA translation after a transfer.
 *
 * If you perform a pci_map_single() but wish to interrogate the
 * buffer using the cpu, yet do not wish to teardown the PCI dma
 * mapping, you must call this function before doing so.  At the
 * next point you give the PCI dma address back to the card, the
 * device again owns the buffer.
 */
extern inline void pci_dma_sync_single(struct pci_dev *hwdev,
				       dma_addr_t dma_handle,
				       size_t size, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
	/* Nothing to do */
}

/* Make physical memory consistent for a set of streaming
 * mode DMA translations after a transfer.
 *
 * The same as pci_dma_sync_single but for a scatter-gather list,
 * same rules and usage.
 */
extern inline void pci_dma_sync_sg(struct pci_dev *hwdev,
				   struct scatterlist *sg,
				   int nelems, int direction)
{
	if (direction == PCI_DMA_NONE)
		BUG();
	/* Nothing to do */
}

/* Return whether the given PCI device DMA address mask can
 * be supported properly.  For example, if your device can
 * only drive the low 24-bits during PCI bus mastering, then
 * you would pass 0x00ffffff as the mask to this function.
 */
extern inline int pci_dma_supported(struct pci_dev *hwdev, dma_addr_t mask)
{
        /*
         * we fall back to GFP_DMA when the mask isn't all 1s,
         * so we can't guarantee allocations that must be
         * within a tighter range than GFP_DMA..
         */
        if(mask < 0x00ffffff)
                return 0;

	return 1;
}

/* These macros should be used after a pci_map_sg call has been done
 * to get bus addresses of each of the SG entries and their lengths.
 * You should only work with the number of sg entries pci_map_sg
 * returns, or alternatively stop on the first sg_dma_len(sg) which
 * is 0.
 */
#define sg_dma_address(sg)	(virt_to_bus((sg)->address))
#define sg_dma_len(sg)		((sg)->length)

/* Return the index of the PCI controller for device. */
static inline int pci_controller_num(struct pci_dev *dev)
{
	return 0;
}

#define HAVE_PCI_MMAP
extern int pci_mmap_page_range(struct pci_dev *dev, struct vm_area_struct *vma,
			       enum pci_mmap_state mmap_state, int write_combine);

#endif /* __KERNEL__ */

#endif /* __i386_PCI_H */
