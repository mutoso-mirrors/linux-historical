/*
 * CHRP pci routines.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/openpic.h>

#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/hydra.h>
#include <asm/prom.h>
#include <asm/gg2.h>

/* LongTrail */
#define pci_config_addr(bus, dev, offset) \
	(GG2_PCI_CONFIG_BASE | ((bus)<<16) | ((dev)<<8) | (offset))

volatile struct Hydra *Hydra = NULL;

/*
 * The VLSI Golden Gate II has only 512K of PCI configuration space, so we
 * limit the bus number to 3 bits
 */

int gg2_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				 unsigned char offset, unsigned char *val)
{
    if (bus > 7) {
	*val = 0xff;
	return PCIBIOS_DEVICE_NOT_FOUND;
    }
    *val = in_8((unsigned char *)pci_config_addr(bus, dev_fn, offset));
    return PCIBIOS_SUCCESSFUL;
}

int gg2_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				 unsigned char offset, unsigned short *val)
{
    if (bus > 7) {
	*val = 0xffff;
	return PCIBIOS_DEVICE_NOT_FOUND;
    }
    *val = in_le16((unsigned short *)pci_config_addr(bus, dev_fn, offset));
    return PCIBIOS_SUCCESSFUL;
}


int gg2_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned int *val)
{
    if (bus > 7) {
	*val = 0xffffffff;
	return PCIBIOS_DEVICE_NOT_FOUND;
    }
    *val = in_le32((unsigned int *)pci_config_addr(bus, dev_fn, offset));
    return PCIBIOS_SUCCESSFUL;
}

int gg2_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned char val)
{
    if (bus > 7)
	return PCIBIOS_DEVICE_NOT_FOUND;
    out_8((unsigned char *)pci_config_addr(bus, dev_fn, offset), val);
    return PCIBIOS_SUCCESSFUL;
}

int gg2_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				  unsigned char offset, unsigned short val)
{
    if (bus > 7)
	return PCIBIOS_DEVICE_NOT_FOUND;
    out_le16((unsigned short *)pci_config_addr(bus, dev_fn, offset), val);
    return PCIBIOS_SUCCESSFUL;
}

int gg2_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				   unsigned char offset, unsigned int val)
{
   if (bus > 7)
	return PCIBIOS_DEVICE_NOT_FOUND;
    out_le32((unsigned int *)pci_config_addr(bus, dev_fn, offset), val);
    return PCIBIOS_SUCCESSFUL;
}

extern volatile unsigned int *pci_config_address;
extern volatile unsigned char *pci_config_data;

#define DEV_FN_MAX (31<<3)

int raven_pcibios_read_config_byte(unsigned char bus, 
                                      unsigned char dev_fn,
                                      unsigned char offset, 
                                      unsigned char *val)
{
        if (dev_fn >= DEV_FN_MAX) return PCIBIOS_DEVICE_NOT_FOUND;
        out_be32(pci_config_address,
                 0x80|(bus<<8)|(dev_fn<<16)|((offset&~3)<<24));
        *val = in_8(pci_config_data+(offset&3));
        return PCIBIOS_SUCCESSFUL;
}

int raven_pcibios_read_config_word(unsigned char bus, 
                                      unsigned char dev_fn,
                                      unsigned char offset, 
                                      unsigned short *val)
{
        if (dev_fn >= DEV_FN_MAX) return PCIBIOS_DEVICE_NOT_FOUND;
        if (offset&1)return PCIBIOS_BAD_REGISTER_NUMBER;
        out_be32(pci_config_address,
                 0x80|(bus<<8)|(dev_fn<<16)|((offset&~3)<<24));
        *val = in_le16((volatile unsigned short *)
                       (pci_config_data+(offset&3)));
        return PCIBIOS_SUCCESSFUL;
}

int raven_pcibios_read_config_dword(unsigned char bus, 
                                       unsigned char dev_fn,
                                       unsigned char offset, 
                                       unsigned int *val)
{
        if (dev_fn >= DEV_FN_MAX) return PCIBIOS_DEVICE_NOT_FOUND;
        if (offset&3)return PCIBIOS_BAD_REGISTER_NUMBER;
        out_be32(pci_config_address,
                 0x80|(bus<<8)|(dev_fn<<16)|(offset<<24));
        *val = in_le32((volatile unsigned int *)(pci_config_data));
        return PCIBIOS_SUCCESSFUL;
}

int raven_pcibios_write_config_byte(unsigned char bus, 
                                       unsigned char dev_fn,
                                       unsigned char offset, 
                                       unsigned char val) 
{
        if (dev_fn >= DEV_FN_MAX) return PCIBIOS_DEVICE_NOT_FOUND;
        out_be32(pci_config_address,
                 0x80|(bus<<8)|(dev_fn<<16)|((offset&~3)<<24));
        out_8(pci_config_data+(offset&3),val);
        return PCIBIOS_SUCCESSFUL;
}

int raven_pcibios_write_config_word(unsigned char bus, 
                                       unsigned char dev_fn,
                                       unsigned char offset, 
                                       unsigned short val) 
{
        if (dev_fn >= DEV_FN_MAX) return PCIBIOS_DEVICE_NOT_FOUND;
        if (offset&1)return PCIBIOS_BAD_REGISTER_NUMBER;
        out_be32(pci_config_address,
                 0x80|(bus<<8)|(dev_fn<<16)|((offset&~3)<<24));
        out_le16((volatile unsigned short *)(pci_config_data+(offset&3)),val);
        return PCIBIOS_SUCCESSFUL;
}

int raven_pcibios_write_config_dword(unsigned char bus, 
                                        unsigned char dev_fn,
                                        unsigned char offset, 
                                        unsigned int val) 
{
        if (dev_fn >= DEV_FN_MAX) return PCIBIOS_DEVICE_NOT_FOUND;
        if (offset&3)return PCIBIOS_BAD_REGISTER_NUMBER;
        out_be32(pci_config_address,
                 0x80|(bus<<8)|(dev_fn<<16)|(offset<<24));
        out_le32((volatile unsigned int *)pci_config_data,val);
        return PCIBIOS_SUCCESSFUL;
}

#define python_config_address(bus) (unsigned *)((0xfef00000+0xf8000)-(bus*0x100000))
#define python_config_data(bus) ((0xfef00000+0xf8010)-(bus*0x100000))
#define PYTHON_CFA(b, d, o)	(0x80 | ((b) << 8) | ((d) << 16) \
				 | (((o) & ~3) << 24))
     
int python_pcibios_read_config_byte(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned char *val)
{
	if (bus > 2) {
		*val = 0xff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset) );
	*val = in_8((unsigned char *)python_config_data(bus) + (offset&3));
	return PCIBIOS_SUCCESSFUL;
}

int python_pcibios_read_config_word(unsigned char bus, unsigned char dev_fn,
				    unsigned char offset, unsigned short *val)
{
	if (bus > 2) {
		*val = 0xffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset) );
	*val = in_le16((unsigned short *)(python_config_data(bus) + (offset&3)));
	return PCIBIOS_SUCCESSFUL;
}


int python_pcibios_read_config_dword(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned int *val)
{
	if (bus > 2) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset) );
	*val = in_le32((unsigned *)python_config_data(bus));
	return PCIBIOS_SUCCESSFUL;
}

int python_pcibios_write_config_byte(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned char val)
{
	if (bus > 2)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset) );
	out_8((volatile unsigned char *)python_config_data(bus) + (offset&3), val);
	return PCIBIOS_SUCCESSFUL;
}

int python_pcibios_write_config_word(unsigned char bus, unsigned char dev_fn,
				     unsigned char offset, unsigned short val)
{
	if (bus > 2)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset) );
	out_le16((volatile unsigned short *)python_config_data(bus) + (offset&3),
		 val);
	return PCIBIOS_SUCCESSFUL;
}

int python_pcibios_write_config_dword(unsigned char bus, unsigned char dev_fn,
				      unsigned char offset, unsigned int val)
{
	if (bus > 2)
		return PCIBIOS_DEVICE_NOT_FOUND;
	out_be32( python_config_address( bus ), PYTHON_CFA(bus,dev_fn,offset) );
	out_le32((unsigned *)python_config_data(bus) + (offset&3), val);
	return PCIBIOS_SUCCESSFUL;
}

    /*
     *  Temporary fixes for PCI devices. These should be replaced by OF query
     *  code -- Geert
     */

static u_char hydra_openpic_initsenses[] __initdata = {
    1,	/* HYDRA_INT_SIO */
    0,	/* HYDRA_INT_SCSI_DMA */
    0,	/* HYDRA_INT_SCCA_TX_DMA */
    0,	/* HYDRA_INT_SCCA_RX_DMA */
    0,	/* HYDRA_INT_SCCB_TX_DMA */
    0,	/* HYDRA_INT_SCCB_RX_DMA */
    1,	/* HYDRA_INT_SCSI */
    1,	/* HYDRA_INT_SCCA */
    1,	/* HYDRA_INT_SCCB */
    1,	/* HYDRA_INT_VIA */
    1,	/* HYDRA_INT_ADB */
    0,	/* HYDRA_INT_ADB_NMI */
    	/* all others are 1 (= default) */
};

__initfunc(int hydra_init(void))
{
	struct device_node *np;

	np = find_devices("mac-io");
	if (np == NULL || np->n_addrs == 0) {
		printk(KERN_WARNING "Warning: no mac-io found\n");
		return 0;
	}
	Hydra = ioremap(np->addrs[0].address, np->addrs[0].size);
	printk("Hydra Mac I/O at %x\n", np->addrs[0].address);
	out_le32(&Hydra->Feature_Control, (HYDRA_FC_SCC_CELL_EN |
					   HYDRA_FC_SCSI_CELL_EN |
					   HYDRA_FC_SCCA_ENABLE |
					   HYDRA_FC_SCCB_ENABLE |
					   HYDRA_FC_ARB_BYPASS |
					   HYDRA_FC_MPIC_ENABLE |
					   HYDRA_FC_SLOW_SCC_PCLK |
					   HYDRA_FC_MPIC_IS_MASTER));
	OpenPIC = (volatile struct OpenPIC *)&Hydra->OpenPIC;
	OpenPIC_InitSenses = hydra_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(hydra_openpic_initsenses);
	return 1;
}
