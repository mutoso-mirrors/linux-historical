#ifndef _PARISC_HARDWARE_H
#define _PARISC_HARDWARE_H

#include <asm/pdc.h>

struct parisc_device_id {
	unsigned char	hw_type;	/* 5 bits used */
	unsigned char	hversion_rev;	/* 4 bits */
	unsigned short	hversion;	/* 12 bits */
	unsigned int	sversion;	/* 20 bits */
};

#define HWTYPE_ANY_ID	0xff
#define HVERSION_REV_ANY_ID	0xff
#define HVERSION_ANY_ID	0xffff
#define SVERSION_ANY_ID	0xffffffffU

struct hp_hardware {
	unsigned short	hw_type:5;	/* HPHW_xxx */
	unsigned short	hversion;
	unsigned long	sversion:28;
	unsigned short	opt;
	const char	name[80];	/* The hardware description */
};

struct parisc_device {
	unsigned long   hpa;		/* Hard Physical Address */
	struct parisc_device_id id;
	struct parisc_device *parent;
	struct parisc_device *sibling;
	struct parisc_device *child;
	struct parisc_driver *driver;	/* Driver for this device */
	void		*sysdata;	/* Driver instance private data */
	char		name[80];	/* The hardware description */
	int		irq;

	char		hw_path;        /* The module number on this bus */
	unsigned int	num_addrs;	/* some devices have additional address ranges. */
	unsigned long	*addr;          /* which will be stored here */
 
#ifdef __LP64__
	/* parms for pdc_pat_cell_module() call */
	unsigned long	pcell_loc;	/* Physical Cell location */
	unsigned long	mod_index;	/* PAT specific - Misc Module info */

	/* generic info returned from pdc_pat_cell_module() */
	unsigned long	mod_info;	/* PAT specific - Misc Module info */
	unsigned long	pmod_loc;	/* physical Module location */
#endif
};

enum cpu_type {
	pcx	= 0, /* pa7000		pa 1.0  */
	pcxs	= 1, /* pa7000		pa 1.1a */
	pcxt	= 2, /* pa7100		pa 1.1b */
	pcxt_	= 3, /* pa7200	(t')	pa 1.1c */
	pcxl	= 4, /* pa7100lc	pa 1.1d */
	pcxl2	= 5, /* pa7300lc	pa 1.1e */
	pcxu	= 6, /* pa8000		pa 2.0  */
	pcxu_	= 7, /* pa8200	(u+)	pa 2.0  */
	pcxw	= 8, /* pa8500		pa 2.0  */
	pcxw_	= 9, /* pa8600	(w+)	pa 2.0  */
	pcxw2	= 10 /* pa8700		pa 2.0  */
};

extern char *cpu_name_version[][2]; /* mapping from enum cpu_type to strings */

struct parisc_driver {
	struct parisc_driver *next;
	char *name; 
	const struct parisc_device_id *id_table;
	int (*probe) (struct parisc_device *dev); /* New device discovered */
};

struct io_module {
        volatile uint32_t nothing;		/* reg 0 */
        volatile uint32_t io_eim;
        volatile uint32_t io_dc_adata;
        volatile uint32_t io_ii_cdata;
        volatile uint32_t io_dma_link;		/* reg 4 */
        volatile uint32_t io_dma_command;
        volatile uint32_t io_dma_address;
        volatile uint32_t io_dma_count;
        volatile uint32_t io_flex;		/* reg 8 */
        volatile uint32_t io_spa_address;
        volatile uint32_t reserved1[2];
        volatile uint32_t io_command;		/* reg 12 */
        volatile uint32_t io_status;
        volatile uint32_t io_control;
        volatile uint32_t io_data;
        volatile uint32_t reserved2;		/* reg 16 */
        volatile uint32_t chain_addr;
        volatile uint32_t sub_mask_clr;
        volatile uint32_t reserved3[13];
        volatile uint32_t undefined[480];
        volatile uint32_t unpriv[512];
};

struct bc_module {
        volatile uint32_t unused1[12];
        volatile uint32_t io_command;
        volatile uint32_t io_status;
        volatile uint32_t io_control;
        volatile uint32_t unused2[1];
        volatile uint32_t io_err_resp;
        volatile uint32_t io_err_info;
        volatile uint32_t io_err_req;
        volatile uint32_t unused3[11];
        volatile uint32_t io_io_low;
        volatile uint32_t io_io_high;
};

#define HPHW_NPROC     0 
#define HPHW_MEMORY    1       
#define HPHW_B_DMA     2
#define HPHW_OBSOLETE  3
#define HPHW_A_DMA     4
#define HPHW_A_DIRECT  5
#define HPHW_OTHER     6
#define HPHW_BCPORT    7
#define HPHW_CIO       8
#define HPHW_CONSOLE   9
#define HPHW_FIO       10
#define HPHW_BA        11
#define HPHW_IOA       12
#define HPHW_BRIDGE    13
#define HPHW_FABRIC    14
#define HPHW_FAULTY    31


/* hardware.c: */
extern const char *parisc_hardware_description(struct parisc_device_id *id);
extern enum cpu_type parisc_get_cpu_type(unsigned long hversion);

struct pci_dev;

/* drivers.c: */
extern struct parisc_device *alloc_pa_dev(unsigned long hpa,
		struct hardware_path *path);
extern int register_parisc_device(struct parisc_device *dev);
extern int register_parisc_driver(struct parisc_driver *driver);
extern int count_parisc_driver(struct parisc_driver *driver);
extern int unregister_parisc_driver(struct parisc_driver *driver);
extern void walk_central_bus(void);
extern void fixup_child_irqs(struct parisc_device *parent, int irqbase,
		int (*choose)(struct parisc_device *parent));
extern void print_subdevices(struct parisc_device *dev);
extern const struct parisc_device *find_pa_parent_type(const struct parisc_device *, int);
extern void print_parisc_devices(void);
extern char *print_pa_hwpath(struct parisc_device *dev, char *path);
extern char *print_pci_hwpath(struct pci_dev *dev, char *path);
extern void get_pci_node_path(struct pci_dev *dev, struct hardware_path *path);


/* inventory.c: */
extern void do_memory_inventory(void);
extern void do_device_inventory(void);

#endif /* _PARISC_HARDWARE_H */
