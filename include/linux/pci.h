/*
 * PCI defines and function prototypes
 * Copyright 1994, Drew Eckhardt
 *
 * For more information, please consult 
 * 
 * PCI BIOS Specification Revision
 * PCI Local Bus Specification
 * PCI System Design Guide
 *
 * PCI Special Interest Group
 * M/S HF3-15A
 * 5200 N.E. Elam Young Parkway
 * Hillsboro, Oregon 97124-6497
 * +1 (503) 696-2000 
 * +1 (800) 433-5177
 * 
 * Manuals are $25 each or $50 for all three, plus $7 shipping 
 * within the United States, $35 abroad.
 */

#ifndef PCI_H
#define PCI_H

/* Configuration method #1 */
#define PCI_CONFIG1_ADDRESS_REG  0xcf8
#define PCI_CONFIG1_ENABLE 0x80000000
#define PCI_CONFIG1_TUPPLE (bus, device, function, register)	\
        (PCI_CONFIG1_ENABLE | ((bus) << 16) & 0xff0000 |	\
        ((device) << 11) & 0xf800 | ((function) << 8) & 0x700 | \
        ((register) << 2) & 0xfc)
#define PCI_CONFIG1_DATA_REG     0xcfc

/* Configuration method #2, deprecated */
#define PCI_CONFIG2_ENABLE_REG	0xcf8
#define PCI_CONFIG2_ENABLE	0xf0
#define PCI_CONFIG2_TUPPLE (function)				\
	(PCI_CONFIG2_ENABLE | ((function) << 1) & 0xe)
#define PCI_CONFIG2_FORWARD_REG	0xcfa

/*
 * Under PCI, each device has 256 bytes of configuration address space,
 * of which the first 64 bytes is standardized as follows : 
 */

#define PCI_VENDOR_ID		0x00	/* 16 bits */
#define PCI_DEVICE_ID		0x02	/* 16 bits */
#define PCI_COMMAND		0x04	/* 16 bits */
#define  PCI_COMMAND_IO		0x1	/* Enable response in I/O space */
#define  PCI_COMMAND_MEMORY	0x2	/* Enable response in I/O space */
#define  PCI_COMMAND_MASTER	0x4	/* Enable bus mastering */
#define  PCI_COMMAND_SPECIAL	0x8	/* Enable response to special cycles */
#define  PCI_COMMAND_INVALIDATE	0x10	/* Use memory write and invalidate */
#define  PCI_COMMAND_VGA_PALETTE	0x20	/* Enable palette snooping */
#define  PCI_COMMAND_PARITY	0x40	/* Enable parity checking */
#define  PCI_COMMAND_WAIT 	0x80	/* Enable address/data stepping */
#define  PCI_COMMAND_SERR	0x100	/* Enable SERR */
#define  PCI_COMMAND_FAST_BACK	0x200	/* Enable back-to-back writes */

#define PCI_STATUS		0x06	/* 16 bits */
#define  PCI_STATUS_FAST_BACK	0x80	/* Accept fast-back to back */
#define  PCI_STATUS_PARITY	0x100	/* Detected parity error */
#define  PCI_STATUS_DEVSEL_MASK	0x600	/* DEVSEL timing */
#define  PCI_STATUS_DEVSEL_FAST	0x000	
#define  PCI_STATUS_DEVSEL_MEDIUM 0x200
#define  PCI_STATUS_DEVESEL_SLOW 0x400
#define  PCI_STATUS_SIG_TARGET_ABORT 0x800 /* Set on target abort */
#define  PCI_STATUS_REC_TARGET_ABORT 0x1000 /* Master ack of " */
#define  PCI_STATUS_REC_MASTER_ABORT 0x2000 /* Set on master abort */
#define  PCI_STATUS_SIG_SYSTEM_ERROR 0x4000 /* Set when we drive SERR */
#define  PCI_STATUS_DETECTED_PARITY 0x8000 /* Set on parity error */

#define PCI_CLASS_REVISION	0x08	/* High 24 bits are class, low 8
					   revision */
#define PCI_REVISION_ID         0x08    /* Revision ID */
#define PCI_CLASS_PROG          0x09    /* Reg. Level Programming Interface */
#define PCI_CLASS_DEVICE        0x0a    /* Device class */

#define PCI_CACHE_LINE_SIZE	0x0c	/* 8 bits */
#define PCI_LATENCY_TIMER	0x0d	/* 8 bits */
#define PCI_HEADER_TYPE		0x0e	/* 8 bits */
#define PCI_BIST		0x0f	/* 8 bits */
#define PCI_BIST_CODE_MASK	0x0f	/* Return result */
#define PCI_BIST_START		0x40	/* 1 to start BIST, 2 secs or less */
#define PCI_BIST_CAPABLE	0x80	/* 1 if BIST capable */

/*
 * Base addresses specify locations in memory or I/O space.
 * Decoded size can be determined by writing a value of 
 * 0xffffffff to the register, and reading it back.  Only 
 * 1 bits are decoded.
 */

#define PCI_BASE_ADDRESS_0	0x10	/* 32 bits */
#define PCI_BASE_ADDRESS_1	0x14	/* 32 bits */
#define PCI_BASE_ADDRESS_2	0x18	/* 32 bits */
#define PCI_BASE_ADDRESS_3	0x1c	/* 32 bits */
#define PCI_BASE_ADDRESS_4	0x20	/* 32 bits */
#define PCI_BASE_ADDRESS_5	0x24	/* 32 bits */
#define  PCI_BASE_ADDRESS_SPACE	0x01	/* 0 = memory, 1 = I/O */
#define  PCI_BASE_ADDRESS_SPACE_IO 0x01
#define  PCI_BASE_ADDRESS_SPACE_MEMORY 0x00
#define  PCI_BASE_ADDRESS_MEM_TYPE_MASK 0x06
#define  PCI_BASE_ADDRESS_MEM_TYPE_32	0x00	/* 32 bit address */
#define  PCI_BASE_ADDRESS_MEM_TYPE_1M	0x02	/* Below 1M */
#define  PCI_BASE_ADDRESS_MEM_TYPE_64	0x04	/* 64 bit address */
#define  PCI_BASE_ADDRESS_MEM_MASK	~7
#define  PCI_BASE_ADDRESS_IO_MASK	~3
/* bit 1 is reserved if address_space = 1 */

/* 0x28-0x2f are reserved */
#define PCI_ROM_ADDRESS		0x30	/* 32 bits */
#define  PCI_ROM_ADDRESS_ENABLE	0x01	/* Write 1 to enable ROM,
					   bits 31..11 are address,
					   10..2 are reserved */
/* 0x34-0x3b are reserved */
#define PCI_INTERRUPT_LINE	0x3c	/* 8 bits */
#define PCI_INTERRUPT_PIN	0x3d	/* 8 bits */
#define PCI_MIN_GNT		0x3e	/* 8 bits */
#define PCI_MAX_LAT		0x3f	/* 8 bits */

#define PCI_CLASS_NOT_DEFINED		0x0000
#define PCI_CLASS_NOT_DEFINED_VGA	0x0001

#define PCI_CLASS_STORAGE_SCSI		0x0100
#define PCI_CLASS_STORAGE_IDE		0x0101
#define PCI_CLASS_STORAGE_FLOPPY	0x0102
#define PCI_CLASS_STORAGE_IPI		0x0103
#define PCI_CLASS_STORAGE_OTHER		0x0180

#define PCI_CLASS_NETWORK_ETHERNET	0x0200
#define PCI_CLASS_NETWORK_TOKEN_RING	0x0201
#define PCI_CLASS_NETWORK_FDDI		0x0202
#define PCI_CLASS_NETWORK_OTHER		0x0280

#define PCI_CLASS_DISPLAY_VGA		0x0300
#define PCI_CLASS_DISPLAY_XGA		0x0301
#define PCI_CLASS_DISPLAY_OTHER		0x0380

#define PCI_CLASS_MULTIMEDIA_VIDEO	0x0400
#define PCI_CLASS_MULTIMEDIA_AUDIO	0x0401
#define PCI_CLASS_MULTIMEDIA_OTHER	0x0480

#define PCI_CLASS_MEMORY_RAM		0x0500
#define PCI_CLASS_MEMORY_FLASH		0x0501
#define PCI_CLASS_MEMORY_OTHER		0x0580

#define PCI_CLASS_BRIDGE_HOST		0x0600
#define PCI_CLASS_BRIDGE_ISA		0x0601
#define PCI_CLASS_BRIDGE_EISA		0x0602
#define PCI_CLASS_BRIDGE_MC		0x0603
#define PCI_CLASS_BRIDGE_PCI		0x0604
#define PCI_CLASS_BRIDGE_PCMCIA		0x0605
#define PCI_CLASS_BRIDGE_OTHER		0x0680

#define PCI_CLASS_OTHERS		0xff

struct pci_class_type {
	unsigned long class_id;
	char *class_name;
};

#define PCI_CLASS_NUM 28
#define PCI_CLASS_TYPE    { \
	{PCI_CLASS_NOT_DEFINED,		"Old unidentified device"}, \
	{PCI_CLASS_NOT_DEFINED_VGA,	"Old VGA controller"}, \
	{PCI_CLASS_STORAGE_SCSI,	"SCSI bus controller"}, \
	{PCI_CLASS_STORAGE_IDE,		"IDE controller"}, \
	{PCI_CLASS_STORAGE_FLOPPY,	"Floppy controller"}, \
	{PCI_CLASS_STORAGE_IPI,		"IPI bus controller"}, \
	{PCI_CLASS_STORAGE_OTHER,	"Unknown mass storage controller"}, \
	{PCI_CLASS_NETWORK_ETHERNET,	"Ethernet controller"}, \
	{PCI_CLASS_NETWORK_TOKEN_RING,	"Token ring controller"}, \
	{PCI_CLASS_NETWORK_FDDI,	"FDDI controller"}, \
	{PCI_CLASS_NETWORK_OTHER,	"Unknown network controller"}, \
	{PCI_CLASS_DISPLAY_VGA,		"VGA display controller"}, \
	{PCI_CLASS_DISPLAY_XGA,		"XGA display controller"}, \
	{PCI_CLASS_DISPLAY_OTHER,	"Unknown display controller"}, \
	{PCI_CLASS_MULTIMEDIA_VIDEO,	"Video device"}, \
	{PCI_CLASS_MULTIMEDIA_AUDIO,	"Audio device"}, \
	{PCI_CLASS_MULTIMEDIA_OTHER,	"Unknown multimedia device"}, \
	{PCI_CLASS_MEMORY_RAM,		"RAM controller"}, \
	{PCI_CLASS_MEMORY_FLASH,	"FLASH controller"}, \
	{PCI_CLASS_MEMORY_OTHER,	"Unknown memory controller"}, \
	{PCI_CLASS_BRIDGE_HOST,		"Host bridge"}, \
	{PCI_CLASS_BRIDGE_ISA,		"ISA bridge"}, \
	{PCI_CLASS_BRIDGE_EISA,		"EISA bridge"}, \
	{PCI_CLASS_BRIDGE_MC,		"MC bridge"}, \
	{PCI_CLASS_BRIDGE_PCI,		"PCI to PCI bridge"}, \
	{PCI_CLASS_BRIDGE_PCMCIA,	"PCMCIA bridge"}, \
	{PCI_CLASS_BRIDGE_OTHER,	"Unknown bridge device"}, \
	{0,				"Unknown type of PCI device"} \
}

#define PCI_VENDOR_ID_NCR		0x1000
#define PCI_DEVICE_ID_NCR_53C810	0x0001
#define PCI_DEVICE_ID_NCR_53C815	0x0004
#define PCI_DEVICE_ID_NCR_53C820	0x0002
#define PCI_DEVICE_ID_NCR_53C825	0x0003

#define PCI_VENDOR_ID_ADAPTEC		0x9004
#define PCI_DEVICE_ID_ADAPTEC_2940	0x7178

#define PCI_VENDOR_ID_DPT               0x1044   
#define PCI_DEVICE_ID_DPT               0xa400  

#define PCI_VENDOR_ID_S3		0x5333
#define PCI_DEVICE_ID_S3_864_1		0x88c0
#define PCI_DEVICE_ID_S3_864_2		0x88c1
#define PCI_DEVICE_ID_S3_928		0x88b0
#define PCI_DEVICE_ID_S3_964		0x88d0
#define PCI_DEVICE_ID_S3_811		0x8811

#define PCI_VENDOR_ID_OPTI		0x1045
#define PCI_DEVICE_ID_OPTI_82C822	0xc822
#define PCI_DEVICE_ID_OPTI_82C621	0xc621

#define PCI_VENDOR_ID_UMC		0x1060
#define PCI_DEVICE_ID_UMC_UM8881F	0x8881
#define PCI_DEVICE_ID_UMC_UM8891A	0x0891
#define PCI_DEVICE_ID_UMC_UM8886F	0x8886
#define PCI_DEVICE_ID_UMC_UM8673F	0x0101

#define PCI_VENDOR_ID_DEC		0x1011
#define PCI_DEVICE_ID_DEC_TULIP		0x0002
#define PCI_DEVICE_ID_DEC_TULIP_FAST	0x0009
#define PCI_DEVICE_ID_DEC_FDDI		0x000F

#define PCI_VENDOR_ID_MATROX		0x102B

#define PCI_VENDOR_ID_INTEL		0x8086
#define PCI_DEVICE_ID_INTEL_82378	0x0484
#define PCI_DEVICE_ID_INTEL_82424	0x0483
#define PCI_DEVICE_ID_INTEL_82375	0x0482
#define PCI_DEVICE_ID_INTEL_82434	0x04a3
#define PCI_DEVICE_ID_INTEL_82430	0x0486

#define PCI_VENDOR_ID_SMC		0x1042
#define PCI_DEVICE_ID_SMC_37C665	0x1000

#define PCI_VENDOR_ID_ATI		0x1002
#define PCI_DEVICE_ID_ATI_M32		0x4158
#define PCI_DEVICE_ID_ATI_M64		0x4758

#define PCI_VENDOR_ID_WEITEK		0x100e
#define PCI_DEVICE_ID_WEITEK_P9000	0x9001
#define PCI_DEVICE_ID_WEITEK_P9100	0x9100

#define PCI_VENDOR_ID_CIRRUS		0x1013
#define PCI_DEVICE_ID_CIRRUS_5434_4	0x00A4
#define PCI_DEVICE_ID_CIRRUS_5434_8	0x00A8
#define PCI_DEVICE_ID_CIRRUS_6729	0x1100

#define PCI_VENDOR_ID_BUSLOGIC		0x104B
#define PCI_DEVICE_ID_BUSLOGIC_946C	0x0140

#define PCI_VENDOR_ID_N9		0x105D
#define PCI_DEVICE_ID_N9_I128		0x2309

#define PCI_VENDOR_ID_ALI		0x1025
#define PCI_DEVICE_ID_ALI_M1435		0x1435

#define PCI_VENDOR_ID_TSENG		0x100c
#define PCI_DEVICE_ID_TSENG_W32P_2	0x3202
#define PCI_DEVICE_ID_TSENG_W32P_5	0x3205

#define PCI_VENDOR_ID_CMD		0x1095
#define PCI_DEVICE_ID_CMD_640		0x0640

#define PCI_VENDOR_ID_VISION		0x1098
#define PCI_DEVICE_ID_VISION_QD8500	0x0001

#define PCI_VENDOR_ID_AMD		0x1022
#define PCI_DEVICE_ID_AMD_LANCE		0x2000

#define PCI_VENDOR_ID_VLSI		0x1004
#define PCI_DEVICE_ID_VLSI_82C593	0x0006

#define PCI_VENDOR_ID_AL		0x1005
#define PCI_DEVICE_ID_AL_2301		0x2301

#define PCI_VENDOR_ID_SYMPHONY		0x1c1c
#define PCI_DEVICE_ID_SYMPHONY_101	0x0001

#define PCI_VENDOR_ID_TRIDENT		0x1023
#define PCI_DEVICE_ID_TRIDENT_9420	0x9420

struct pci_vendor_type {
	unsigned short vendor_id;
	char *vendor_name;
};


#define PCI_VENDOR_NUM 24
#define PCI_VENDOR_TYPE { \
	{PCI_VENDOR_ID_NCR,		"NCR"}, \
	{PCI_VENDOR_ID_ADAPTEC,		"Adaptec"}, \
	{PCI_VENDOR_ID_DPT,		"DPT"}, \
	{PCI_VENDOR_ID_S3,		"S3 Inc."}, \
	{PCI_VENDOR_ID_OPTI,		"OPTI"}, \
	{PCI_VENDOR_ID_UMC,		"UMC"}, \
	{PCI_VENDOR_ID_DEC,		"DEC"}, \
	{PCI_VENDOR_ID_MATROX,		"Matrox"}, \
	{PCI_VENDOR_ID_INTEL,		"Intel"}, \
	{PCI_VENDOR_ID_SMC,		"SMC"}, \
	{PCI_VENDOR_ID_ATI,		"ATI"}, \
	{PCI_VENDOR_ID_WEITEK,		"Weitek"}, \
	{PCI_VENDOR_ID_CIRRUS,		"Cirrus Logic"}, \
	{PCI_VENDOR_ID_BUSLOGIC,	"Bus Logic"}, \
	{PCI_VENDOR_ID_N9,		"Number #9"}, \
	{PCI_VENDOR_ID_ALI,		"ALI"}, \
	{PCI_VENDOR_ID_TSENG,		"Tseng'Lab"}, \
	{PCI_VENDOR_ID_CMD,		"CMD"}, \
	{PCI_VENDOR_ID_VISION,		"Vision"}, \
	{PCI_VENDOR_ID_AMD,		"AMD"}, \
	{PCI_VENDOR_ID_VLSI,		"VLSI"}, \
	{PCI_VENDOR_ID_AL,		"Advance Logic"}, \
	{PCI_VENDOR_ID_SYMPHONY,	"Symphony"}, \
	{PCI_VENDOR_ID_TRIDENT,		"Trident"} \
}


/* Optimisation pointer is a offset of an item into the array		*/
/* BRIDGE_MAPPING_TYPE. 0xff indicates that the device is not a PCI	*/
/* bridge, or that we don't know for the moment how to configure it.	*/
/* I'm trying to do my best so that the kernel stays small.		*/
/* Different chipset can have same optimisation structure. i486 and	*/
/* pentium chipsets from the same manufacturer usually have the same	*/
/* structure 								*/

struct pci_device_type {
	unsigned char bridge_id;
	unsigned short vendor_id;
	unsigned short device_id;
	char *device_name;
};

#define PCI_DEVICE_NUM 45
#define PCI_DEVICE_TYPE { \
	{0xff,	PCI_VENDOR_ID_NCR,	PCI_DEVICE_ID_NCR_53C810,	"53c810"}, \
	{0xff,	PCI_VENDOR_ID_NCR,	PCI_DEVICE_ID_NCR_53C815,	"53c815"}, \
	{0xff,	PCI_VENDOR_ID_NCR,	PCI_DEVICE_ID_NCR_53C820,	"53c820"}, \
	{0xff,	PCI_VENDOR_ID_NCR,	PCI_DEVICE_ID_NCR_53C825,	"53c825"}, \
	{0xff,	PCI_VENDOR_ID_ADAPTEC,	PCI_DEVICE_ID_ADAPTEC_2940,	"2940"}, \
	{0xff,	PCI_VENDOR_ID_DPT,	PCI_DEVICE_ID_DPT,		"SmartCache/Raid"}, \
	{0xff,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_864_1,		"Vision 864-P"}, \
	{0xff,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_864_2,		"Vision 864-P"}, \
	{0xff,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_928,		"Vision 928-P"}, \
	{0xff,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_964,		"Vision 964-P"}, \
	{0xff,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_811,		"Trio64"}, \
	{0x02,	PCI_VENDOR_ID_OPTI,	PCI_DEVICE_ID_OPTI_82C822,	"82C822"}, \
	{0xff,	PCI_VENDOR_ID_OPTI,	PCI_DEVICE_ID_OPTI_82C621,	"82C621"}, \
	{0xff,	PCI_VENDOR_ID_UMC,	PCI_DEVICE_ID_UMC_UM8881F,	"UM8881F"}, \
	{0x01,	PCI_VENDOR_ID_UMC,	PCI_DEVICE_ID_UMC_UM8891A,	"UM8891A"}, \
	{0xff,	PCI_VENDOR_ID_UMC,	PCI_DEVICE_ID_UMC_UM8886F,	"UM8886F"}, \
	{0xff,	PCI_VENDOR_ID_UMC,	PCI_DEVICE_ID_UMC_UM8673F,	"UM8673F"}, \
	{0xff,	PCI_VENDOR_ID_DEC,	PCI_DEVICE_ID_DEC_TULIP,	"DC21040"}, \
	{0xff,	PCI_VENDOR_ID_DEC,	PCI_DEVICE_ID_DEC_TULIP_FAST,	"DC21040"}, \
	{0xff,	PCI_VENDOR_ID_DEC,	PCI_DEVICE_ID_DEC_FDDI,		"DEFPA"}, \
	{0xff,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82378,	"82378IB"}, \
	{0x00,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82424,	"82424ZX Saturn"}, \
	{0xff,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82375,	"82375EB"}, \
	{0x00,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82434,	"82434LX Mercury/Neptune"}, \
	{0xff,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82430,	"82430ZX Aries"}, \
	{0xff,	PCI_VENDOR_ID_SMC,	PCI_DEVICE_ID_SMC_37C665,	"FDC 37C665"}, \
	{0xff,	PCI_VENDOR_ID_ATI,	PCI_DEVICE_ID_ATI_M32,		"Mach 32"}, \
	{0xff,	PCI_VENDOR_ID_ATI,	PCI_DEVICE_ID_ATI_M64,		"Mach 64"}, \
	{0xff,	PCI_VENDOR_ID_WEITEK,	PCI_DEVICE_ID_WEITEK_P9000,	"P9000"}, \
	{0xff,	PCI_VENDOR_ID_WEITEK,	PCI_DEVICE_ID_WEITEK_P9100,	"P9100"}, \
	{0xff,	PCI_VENDOR_ID_CIRRUS,	PCI_DEVICE_ID_CIRRUS_5434_4,	"GD 5434"}, \
	{0xff,	PCI_VENDOR_ID_CIRRUS,	PCI_DEVICE_ID_CIRRUS_5434_8,	"GD 5434"}, \
	{0xff,	PCI_VENDOR_ID_CIRRUS,	PCI_DEVICE_ID_CIRRUS_6729,	"CL 6729"}, \
	{0xff,	PCI_VENDOR_ID_BUSLOGIC,PCI_DEVICE_ID_BUSLOGIC_946C,	"946C"}, \
	{0xff,	PCI_VENDOR_ID_N9,	PCI_DEVICE_ID_N9_I128,		"Imagine 128"}, \
	{0xff,	PCI_VENDOR_ID_ALI,	PCI_DEVICE_ID_ALI_M1435,	"M1435"}, \
	{0xff,	PCI_VENDOR_ID_TSENG,	PCI_DEVICE_ID_TSENG_W32P_2,	"ET4000W32P"}, \
	{0xff,	PCI_VENDOR_ID_TSENG,	PCI_DEVICE_ID_TSENG_W32P_5,	"ET4000W32P"}, \
	{0xff,	PCI_VENDOR_ID_CMD,	PCI_DEVICE_ID_CMD_640,		"640A"}, \
	{0xff,	PCI_VENDOR_ID_VISION,	PCI_DEVICE_ID_VISION_QD8500,	"QD-8500PCI"}, \
	{0xff,	PCI_VENDOR_ID_AMD,	PCI_DEVICE_ID_AMD_LANCE,	"79C970"}, \
	{0xff,	PCI_VENDOR_ID_VLSI,	PCI_DEVICE_ID_VLSI_82C593,	"82C593-FC1"}, \
	{0xff,	PCI_VENDOR_ID_AL,	PCI_DEVICE_ID_AL_2301,		"2301"}, \
	{0xff,	PCI_VENDOR_ID_SYMPHONY,	PCI_DEVICE_ID_SYMPHONY_101,	"82C101"}, \
	{0xff,	PCI_VENDOR_ID_TRIDENT,	PCI_DEVICE_ID_TRIDENT_9420,	"TG 9420"} \
}

/* An item of this structure has the following meaning	*/
/* For each optimisation, the register address, the mask	*/
/* and value to write to turn it on.			*/
/* There are 5 optimizations for the moment :		*/
/* Cache L2 write back best than write through		*/
/* Posted Write for CPU to PCI enable			*/
/* Posted Write for CPU to MEMORY enable		*/
/* Posted Write for PCI to MEMORY enable		*/
/* PCI Burst enable					*/

/* Half of the bios I've meet don't allow you to turn	*/
/* that on, and you can gain more than 15% on graphic	*/
/* accesses using those optimisations...		*/

struct optimisation_type {
	char	*type;
	char	*off;
	char	*on;
};

#define OPTIMISATION_NUM 5
#define OPTIMISATION_TYPE { \
	{"Cache L2","write trough","write back"}, \
	{"CPU-PCI posted write","off","on"}, \
	{"CPU-Memory posted write","off","on"}, \
	{"PCI-Memory posted write","off","on"}, \
	{"PCI burst","off","on"} \
}

struct bridge_mapping_type {
	unsigned char	address;
	unsigned char	mask;
	unsigned char	value;
};

/* Intel Neptune/Mercury/Saturn */
/*	If the Internal cache is Write back,	*/
/*	the L2 cache must be write through !	*/
/*	I've to check out how to control that	*/
/*	for the moment, we won't touch the cache*/
/* UMC 8891A Pentium chipset			*/
/*	Why did you think UMC was cheaper ??	*/
/* OPTI 82C822					*/
/*	This is a dummy entry for my tests.	*/
/*	I have this chipset and no docs....	*/   

/* I'm gathering docs. If you can help......	*/

#define BRIDGE_MAPPING_NUM 3
#define BRIDGE_MAPPING_TYPE { \
	{0x0	,0x02	,0x02	}, \
	{0x53	,0x02	,0x02	}, \
	{0x53	,0x01	,0x01	}, \
	{0x54	,0x01	,0x01	}, \
	{0x54	,0x02	,0x02	}, \
\
	{0x50	,0x10	,0x00	}, \
	{0x51	,0x40	,0x40	}, \
	{0x0	,0x0	,0x0	}, \
	{0x0	,0x0	,0x0	}, \
	{0x0	,0x0	,0x0	}, \
\
	{0x0	,0x1	,0x1	}, \
	{0x0	,0x2	,0x0	}, \
	{0x0	,0x0	,0x0	}, \
	{0x0	,0x0	,0x0	}, \
	{0x0	,0x0	,0x0	}  \
}


/* PCI BIOS */

extern int pcibios_present (void);

#define PCIBIOS_SUCCESSFUL		0x00
#define PCIBIOS_FUNC_NOT_SUPPORTED	0x81
#define PCIBIOS_BAD_VENDOR_ID		0x83
#define PCIBIOS_DEVICE_NOT_FOUND	0x86
#define PCIBIOS_BAD_REGISTER_NUMBER	0x87

/*
 * The PCIBIOS calls all bit-field the device_function variable such that 
 * the bit fielding matches that of the bl register used in the actual
 * calls.
 */

extern int pcibios_find_class (unsigned long class_code, unsigned short index, 
    unsigned char *bus, unsigned char *device_fn);
extern int pcibios_find_device (unsigned short vendor, unsigned short device_id, 
    unsigned short index, unsigned char *bus, unsigned char *device_fn);
extern int pcibios_read_config_byte (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned char *value);
extern int pcibios_read_config_word (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned short *value);
extern int pcibios_read_config_dword (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned long *value);
extern char *pcibios_strerror (int error);
extern int pcibios_write_config_byte (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned char value);
extern int pcibios_write_config_word (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned short value);
extern pcibios_write_config_dword (unsigned char bus,
    unsigned char device_fn, unsigned char where, unsigned long value);
#endif /* ndef PCI_H */
