/******************************************************************************
 *
 * Name:	skdrv2nd.h
 * Project:	GEnesis, PCI Gigabit Ethernet Adapter
 * Version:	$Revision: 1.19 $
 * Date:	$Date: 2003/07/07 09:53:10 $
 * Purpose:	Second header file for driver and all other modules
 *
 ******************************************************************************/

/******************************************************************************
 *
 *	(C)Copyright 1998-2003 SysKonnect GmbH.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	The information in this file is provided "AS IS" without warranty.
 *
 ******************************************************************************/

/******************************************************************************
 *
 * History:
 *
 *	$Log: skdrv2nd.h,v $
 *	Revision 1.19  2003/07/07 09:53:10  rroesler
 *	Fix: Removed proprietary RxTx defines and used the ones from skgehw.h instead
 *	
 *	Revision 1.18  2003/06/12 07:54:14  mlindner
 *	Fix: Changed Descriptor Alignment to 64 Byte
 *	
 *	Revision 1.17  2003/05/26 12:56:39  mlindner
 *	Add: Support for Kernel 2.5/2.6
 *	Add: New SkOsGetTimeCurrent function
 *	Add: SK_PNMI_HUNDREDS_SEC definition
 *	Fix: SK_TICKS_PER_SEC on Intel Itanium2
 *	
 *	Revision 1.16  2003/03/21 14:56:18  rroesler
 *	Added code regarding interrupt moderation
 *	
 *	Revision 1.15  2003/02/25 14:16:40  mlindner
 *	Fix: Copyright statement
 *	
 *	Revision 1.14  2003/02/25 13:26:26  mlindner
 *	Add: Support for various vendors
 *	
 *	Revision 1.13  2002/10/02 12:46:02  mlindner
 *	Add: Support for Yukon
 *	
 *	Revision 1.12.2.2  2001/09/05 12:14:50  mlindner
 *	add: New hardware revision int
 *	
 *	Revision 1.12.2.1  2001/03/12 16:50:59  mlindner
 *	chg: kernel 2.4 adaption
 *	
 *	Revision 1.12  2001/03/01 12:52:15  mlindner
 *	Fixed ring size
 *
 *	Revision 1.11  2001/02/19 13:28:02  mlindner
 *	Changed PNMI parameter values
 *
 *	Revision 1.10  2001/01/22 14:16:04  mlindner
 *	added ProcFs functionality
 *	Dual Net functionality integrated
 *	Rlmt networks added
 *
 *	Revision 1.1  2000/10/05 19:46:50  phargrov
 *	Add directory src/vipk_devs_nonlbl/vipk_sk98lin/
 *	This is the SysKonnect SK-98xx Gigabit Ethernet driver,
 *	contributed by SysKonnect.
 *
 *	Revision 1.9  2000/02/21 10:39:55  cgoos
 *	Added flag for jumbo support usage.
 *
 *	Revision 1.8  1999/11/22 13:50:44  cgoos
 *	Changed license header to GPL.
 *	Fixed two comments.
 *
 *	Revision 1.7  1999/09/28 12:38:21  cgoos
 *	Added CheckQueue to SK_AC.
 *	
 *	Revision 1.6  1999/07/27 08:04:05  cgoos
 *	Added checksumming variables to SK_AC.
 *	
 *	Revision 1.5  1999/03/29 12:33:26  cgoos
 *	Rreversed to fine lock granularity.
 *	
 *	Revision 1.4  1999/03/15 12:14:02  cgoos
 *	Added DriverLock to SK_AC.
 *	Removed other locks.
 *	
 *	Revision 1.3  1999/03/01 08:52:27  cgoos
 *	Changed pAC->PciDev declaration.
 *	
 *	Revision 1.2  1999/02/18 10:57:14  cgoos
 *	Removed SkDrvTimeStamp prototype.
 *	Fixed SkGeOsGetTime prototype.
 *	
 *	Revision 1.1  1999/02/16 07:41:01  cgoos
 *	First version.
 *	
 *	
 *
 ******************************************************************************/

/******************************************************************************
 *
 * Description:
 *
 * This is the second include file of the driver, which includes all other
 * neccessary files and defines all structures and constants used by the
 * driver and the common modules.
 *
 * Include File Hierarchy:
 *
 *	see skge.c
 *
 ******************************************************************************/

#ifndef __INC_SKDRV2ND_H
#define __INC_SKDRV2ND_H

#include "h/skqueue.h"
#include "h/skgehwt.h"
#include "h/sktimer.h"
#include "h/ski2c.h"
#include "h/skgepnmi.h"
#include "h/skvpd.h"
#include "h/skgehw.h"
#include "h/skgeinit.h"
#include "h/skaddr.h"
#include "h/skgesirq.h"
#include "h/skcsum.h"
#include "h/skrlmt.h"
#include "h/skgedrv.h"

#define SK_PCI_ISCOMPLIANT(result, pdev) {     \
    result = SK_FALSE; /* default */     \
    /* 3Com (0x10b7) */     \
    if (pdev->vendor == 0x10b7) {     \
        /* Gigabit Ethernet Adapter (0x1700) */     \
        if ((pdev->device == 0x1700)) { \
            result = SK_TRUE;     \
        }     \
    /* SysKonnect (0x1148) */     \
    } else if (pdev->vendor == 0x1148) {     \
        /* SK-98xx Gigabit Ethernet Server Adapter (0x4300) */     \
        /* SK-98xx V2.0 Gigabit Ethernet Adapter (0x4320) */     \
        if ((pdev->device == 0x4300) || \
            (pdev->device == 0x4320)) { \
            result = SK_TRUE;     \
        }     \
    /* D-Link (0x1186) */     \
    } else if (pdev->vendor == 0x1186) {     \
        /* Gigabit Ethernet Adapter (0x4c00) */     \
        if ((pdev->device == 0x4c00)) { \
            result = SK_TRUE;     \
        }     \
    /* Marvell (0x11ab) */     \
    } else if (pdev->vendor == 0x11ab) {     \
        /* Gigabit Ethernet Adapter (0x4320) */     \
        if ((pdev->device == 0x4320)) { \
            result = SK_TRUE;     \
        }     \
    /* CNet (0x1371) */     \
    } else if (pdev->vendor == 0x1371) {     \
        /* GigaCard Network Adapter (0x434e) */     \
        if ((pdev->device == 0x434e)) { \
            result = SK_TRUE;     \
        }     \
    /* Linksys (0x1737) */     \
    } else if (pdev->vendor == 0x1737) {     \
        /* Gigabit Network Adapter (0x1032) */     \
        /* Gigabit Network Adapter (0x1064) */     \
        if ((pdev->device == 0x1032) || \
            (pdev->device == 0x1064)) { \
            result = SK_TRUE;     \
        }     \
    } else {     \
        result = SK_FALSE;     \
    }     \
}


extern SK_MBUF		*SkDrvAllocRlmtMbuf(SK_AC*, SK_IOC, unsigned);
extern void		SkDrvFreeRlmtMbuf(SK_AC*, SK_IOC, SK_MBUF*);
extern SK_U64		SkOsGetTime(SK_AC*);
extern int		SkPciReadCfgDWord(SK_AC*, int, SK_U32*);
extern int		SkPciReadCfgWord(SK_AC*, int, SK_U16*);
extern int		SkPciReadCfgByte(SK_AC*, int, SK_U8*);
extern int		SkPciWriteCfgDWord(SK_AC*, int, SK_U32);
extern int		SkPciWriteCfgWord(SK_AC*, int, SK_U16);
extern int		SkPciWriteCfgByte(SK_AC*, int, SK_U8);
extern int		SkDrvEvent(SK_AC*, SK_IOC IoC, SK_U32, SK_EVPARA);

struct s_DrvRlmtMbuf {
	SK_MBUF		*pNext;		/* Pointer to next RLMT Mbuf. */
	SK_U8		*pData;		/* Data buffer (virtually contig.). */
	unsigned	Size;		/* Data buffer size. */
	unsigned	Length;		/* Length of packet (<= Size). */
	SK_U32		PortIdx;	/* Receiving/transmitting port. */
#ifdef SK_RLMT_MBUF_PRIVATE
	SK_RLMT_MBUF	Rlmt;		/* Private part for RLMT. */
#endif  /* SK_RLMT_MBUF_PRIVATE */
	struct sk_buff	*pOs;		/* Pointer to message block */
};


/*
 * Time macros
 */
#if SK_TICKS_PER_SEC == 100
#define SK_PNMI_HUNDREDS_SEC(t)	(t)
#else
#define SK_PNMI_HUNDREDS_SEC(t)	((((unsigned long)t) * 100) / \
										(SK_TICKS_PER_SEC))
#endif

/*
 * New SkOsGetTime
 */
#define SkOsGetTimeCurrent(pAC, pUsec) {\
	struct timeval t;\
	do_gettimeofday(&t);\
	*pUsec = ((((t.tv_sec) * 1000000L)+t.tv_usec)/10000);\
}


/*
 * ioctl definitions
 */
#define		SK_IOCTL_BASE		(SIOCDEVPRIVATE)
#define		SK_IOCTL_GETMIB		(SK_IOCTL_BASE + 0)
#define		SK_IOCTL_SETMIB		(SK_IOCTL_BASE + 1)
#define		SK_IOCTL_PRESETMIB	(SK_IOCTL_BASE + 2)
#define		SK_IOCTL_GEN		(SK_IOCTL_BASE + 3)

typedef struct s_IOCTL	SK_GE_IOCTL;

struct s_IOCTL {
	char*		pData;
	unsigned int	Len;
};


/*
 * define sizes of descriptor rings in bytes
 */

#define		TX_RING_SIZE	(8*1024)
#define		RX_RING_SIZE	(24*1024)

/*
 * Buffer size for ethernet packets
 */
#define	ETH_BUF_SIZE	1540
#define	ETH_MAX_MTU	1514
#define ETH_MIN_MTU	60
#define ETH_MULTICAST_BIT	0x01
#define SK_JUMBO_MTU	9000

/*
 * transmit priority selects the queue: LOW=asynchron, HIGH=synchron
 */
#define TX_PRIO_LOW	0
#define TX_PRIO_HIGH	1

/*
 * alignment of rx/tx descriptors
 */
#define DESCR_ALIGN	64

/*
 * definitions for pnmi. TODO
 */
#define SK_DRIVER_RESET(pAC, IoC)	0
#define SK_DRIVER_SENDEVENT(pAC, IoC)	0
#define SK_DRIVER_SELFTEST(pAC, IoC)	0
/* For get mtu you must add an own function */
#define SK_DRIVER_GET_MTU(pAc,IoC,i)	0
#define SK_DRIVER_SET_MTU(pAc,IoC,i,v)	0
#define SK_DRIVER_PRESET_MTU(pAc,IoC,i,v)	0

/*
** Interim definition of SK_DRV_TIMER placed in this file until 
** common modules have boon finallized
*/
#define SK_DRV_TIMER			11 
#define	SK_DRV_MODERATION_TIMER		1
#define SK_DRV_MODERATION_TIMER_LENGTH  1000000  /* 1 second */
#define SK_DRV_RX_CLEANUP_TIMER		2
#define SK_DRV_RX_CLEANUP_TIMER_LENGTH	1000000	 /* 100 millisecs */

/*
** Definitions regarding transmitting frames 
** any calculating any checksum.
*/
#define C_LEN_ETHERMAC_HEADER_DEST_ADDR 6
#define C_LEN_ETHERMAC_HEADER_SRC_ADDR  6
#define C_LEN_ETHERMAC_HEADER_LENTYPE   2
#define C_LEN_ETHERMAC_HEADER           ( (C_LEN_ETHERMAC_HEADER_DEST_ADDR) + \
                                          (C_LEN_ETHERMAC_HEADER_SRC_ADDR)  + \
                                          (C_LEN_ETHERMAC_HEADER_LENTYPE) )

#define C_LEN_ETHERMTU_MINSIZE          46
#define C_LEN_ETHERMTU_MAXSIZE_STD      1500
#define C_LEN_ETHERMTU_MAXSIZE_JUMBO    9000

#define C_LEN_ETHERNET_MINSIZE          ( (C_LEN_ETHERMAC_HEADER) + \
                                          (C_LEN_ETHERMTU_MINSIZE) )

#define C_OFFSET_IPHEADER               C_LEN_ETHERMAC_HEADER
#define C_OFFSET_IPHEADER_IPPROTO       9
#define C_OFFSET_TCPHEADER_TCPCS        16

#define C_OFFSET_IPPROTO                ( (C_LEN_ETHERMAC_HEADER) + \
                                          (C_OFFSET_IPHEADER_IPPROTO) )

#define C_PROTO_ID_UDP                  6       /* refer to RFC 790 or Stevens'   */
#define C_PROTO_ID_TCP                  17      /* TCP/IP illustrated for details */

/* TX and RX descriptors *****************************************************/

typedef struct s_RxD RXD; /* the receive descriptor */

struct s_RxD {
	volatile SK_U32	RBControl;	/* Receive Buffer Control */
	SK_U32		VNextRxd;	/* Next receive descriptor,low dword */
	SK_U32		VDataLow;	/* Receive buffer Addr, low dword */
	SK_U32		VDataHigh;	/* Receive buffer Addr, high dword */
	SK_U32		FrameStat;	/* Receive Frame Status word */
	SK_U32		TimeStamp;	/* Time stamp from XMAC */
	SK_U32		TcpSums;	/* TCP Sum 2 / TCP Sum 1 */
	SK_U32		TcpSumStarts;	/* TCP Sum Start 2 / TCP Sum Start 1 */
	RXD		*pNextRxd;	/* Pointer to next Rxd */
	struct sk_buff	*pMBuf;		/* Pointer to Linux' socket buffer */
};

typedef struct s_TxD TXD; /* the transmit descriptor */

struct s_TxD {
	volatile SK_U32	TBControl;	/* Transmit Buffer Control */
	SK_U32		VNextTxd;	/* Next transmit descriptor,low dword */
	SK_U32		VDataLow;	/* Transmit Buffer Addr, low dword */
	SK_U32		VDataHigh;	/* Transmit Buffer Addr, high dword */
	SK_U32		FrameStat;	/* Transmit Frame Status Word */
	SK_U32		TcpSumOfs;	/* Reserved / TCP Sum Offset */
	SK_U16		TcpSumSt;	/* TCP Sum Start */
	SK_U16		TcpSumWr;	/* TCP Sum Write */
	SK_U32		TcpReserved;	/* not used */
	TXD		*pNextTxd;	/* Pointer to next Txd */
	struct sk_buff	*pMBuf;		/* Pointer to Linux' socket buffer */
};

/* Used interrupt bits in the interrupts source register *********************/

#define DRIVER_IRQS	((IS_IRQ_SW)   | \
			(IS_R1_F)      |(IS_R2_F)  | \
			(IS_XS1_F)     |(IS_XA1_F) | \
			(IS_XS2_F)     |(IS_XA2_F))

#define SPECIAL_IRQS	((IS_HW_ERR)   |(IS_I2C_READY)  | \
			(IS_EXT_REG)   |(IS_TIMINT)     | \
			(IS_PA_TO_RX1) |(IS_PA_TO_RX2)  | \
			(IS_PA_TO_TX1) |(IS_PA_TO_TX2)  | \
			(IS_MAC1)      |(IS_LNK_SYNC_M1)| \
			(IS_MAC2)      |(IS_LNK_SYNC_M2)| \
			(IS_R1_C)      |(IS_R2_C)       | \
			(IS_XS1_C)     |(IS_XA1_C)      | \
			(IS_XS2_C)     |(IS_XA2_C))

#define IRQ_MASK	((IS_IRQ_SW)   | \
			(IS_R1_B)      |(IS_R1_F)     |(IS_R2_B) |(IS_R2_F) | \
			(IS_XS1_B)     |(IS_XS1_F)    |(IS_XA1_B)|(IS_XA1_F)| \
			(IS_XS2_B)     |(IS_XS2_F)    |(IS_XA2_B)|(IS_XA2_F)| \
			(IS_HW_ERR)    |(IS_I2C_READY)| \
			(IS_EXT_REG)   |(IS_TIMINT)   | \
			(IS_PA_TO_RX1) |(IS_PA_TO_RX2)| \
			(IS_PA_TO_TX1) |(IS_PA_TO_TX2)| \
			(IS_MAC1)      |(IS_MAC2)     | \
			(IS_R1_C)      |(IS_R2_C)     | \
			(IS_XS1_C)     |(IS_XA1_C)    | \
			(IS_XS2_C)     |(IS_XA2_C))

#define IRQ_HWE_MASK	(IS_ERR_MSK) /* enable all HW irqs */

typedef struct s_DevNet DEV_NET;

struct s_DevNet {
	int             PortNr;
	int             NetNr;
	int             Mtu;
	int             Up;
	SK_AC   *pAC;
};  

typedef struct s_TxPort		TX_PORT;

struct s_TxPort {
	/* the transmit descriptor rings */
	caddr_t		pTxDescrRing;	/* descriptor area memory */
	SK_U64		VTxDescrRing;	/* descr. area bus virt. addr. */
	TXD		*pTxdRingHead;	/* Head of Tx rings */
	TXD		*pTxdRingTail;	/* Tail of Tx rings */
	TXD		*pTxdRingPrev;	/* descriptor sent previously */
	int		TxdRingFree;	/* # of free entrys */
	spinlock_t	TxDesRingLock;	/* serialize descriptor accesses */
	caddr_t		HwAddr;		/* bmu registers address */
	int		PortIndex;	/* index number of port (0 or 1) */
};

typedef struct s_RxPort		RX_PORT;

struct s_RxPort {
	/* the receive descriptor rings */
	caddr_t		pRxDescrRing;	/* descriptor area memory */
	SK_U64		VRxDescrRing;   /* descr. area bus virt. addr. */
	RXD		*pRxdRingHead;	/* Head of Rx rings */
	RXD		*pRxdRingTail;	/* Tail of Rx rings */
	RXD		*pRxdRingPrev;	/* descriptor given to BMU previously */
	int		RxdRingFree;	/* # of free entrys */
	spinlock_t	RxDesRingLock;	/* serialize descriptor accesses */
	int		RxFillLimit;	/* limit for buffers in ring */
	caddr_t		HwAddr;		/* bmu registers address */
	int		PortIndex;	/* index number of port (0 or 1) */
};

/* Definitions needed for interrupt moderation *******************************/

#define IRQ_EOF_AS_TX     ((IS_XA1_F)     | (IS_XA2_F))
#define IRQ_EOF_SY_TX     ((IS_XS1_F)     | (IS_XS2_F))
#define IRQ_MASK_TX_ONLY  ((IRQ_EOF_AS_TX)| (IRQ_EOF_SY_TX))
#define IRQ_MASK_RX_ONLY  ((IS_R1_F)      | (IS_R2_F))
#define IRQ_MASK_SP_ONLY  (SPECIAL_IRQS)
#define IRQ_MASK_TX_RX    ((IRQ_MASK_TX_ONLY)| (IRQ_MASK_RX_ONLY))
#define IRQ_MASK_SP_RX    ((SPECIAL_IRQS)    | (IRQ_MASK_RX_ONLY))
#define IRQ_MASK_SP_TX    ((SPECIAL_IRQS)    | (IRQ_MASK_TX_ONLY))
#define IRQ_MASK_RX_TX_SP ((SPECIAL_IRQS)    | (IRQ_MASK_TX_RX))

#define C_INT_MOD_NONE                 1
#define C_INT_MOD_STATIC               2
#define C_INT_MOD_DYNAMIC              4

#define C_CLK_FREQ_GENESIS      53215000 /* shorter: 53.125 MHz  */
#define C_CLK_FREQ_YUKON        78215000 /* shorter: 78.125 MHz  */

#define C_INTS_PER_SEC_DEFAULT      2000 
#define C_INT_MOD_ENABLE_PERCENTAGE   50 /* if higher 50% enable */
#define C_INT_MOD_DISABLE_PERCENTAGE  50 /* if lower 50% disable */

typedef struct s_DynIrqModInfo  DIM_INFO;
struct s_DynIrqModInfo {
	unsigned long   PrevTimeVal;
	unsigned int    PrevSysLoad;
	unsigned int    PrevUsedTime;
	unsigned int    PrevTotalTime;
	int             PrevUsedDescrRatio;
	int             NbrProcessedDescr;
        SK_U64          PrevPort0RxIntrCts;
        SK_U64          PrevPort1RxIntrCts;
        SK_U64          PrevPort0TxIntrCts;
        SK_U64          PrevPort1TxIntrCts;
	SK_BOOL         ModJustEnabled;     /* Moderation just enabled yes/no */

	int             MaxModIntsPerSec;            /* Moderation Threshold */
	int             MaxModIntsPerSecUpperLimit;  /* Upper limit for DIM  */
	int             MaxModIntsPerSecLowerLimit;  /* Lower limit for DIM  */

	long            MaskIrqModeration;   /* ModIrqType (eg. 'TxRx')      */
	SK_BOOL         DisplayStats;        /* Stats yes/no                 */
	SK_BOOL         AutoSizing;          /* Resize DIM-timer on/off      */
	int             IntModTypeSelect;    /* EnableIntMod (eg. 'dynamic') */

	SK_TIMER        ModTimer; /* just some timer */
};

typedef struct s_PerStrm	PER_STRM;

#define SK_ALLOC_IRQ	0x00000001

/****************************************************************************
 * Per board structure / Adapter Context structure:
 *	Allocated within attach(9e) and freed within detach(9e).
 *	Contains all 'per device' necessary handles, flags, locks etc.:
 */
struct s_AC  {
	SK_GEINIT	GIni;		/* GE init struct */
	SK_PNMI		Pnmi;		/* PNMI data struct */
	SK_VPD		vpd;		/* vpd data struct */
	SK_QUEUE	Event;		/* Event queue */
	SK_HWT		Hwt;		/* Hardware Timer control struct */
	SK_TIMCTRL	Tim;		/* Software Timer control struct */
	SK_I2C		I2c;		/* I2C relevant data structure */
	SK_ADDR		Addr;		/* for Address module */
	SK_CSUM		Csum;		/* for checksum module */
	SK_RLMT		Rlmt;		/* for rlmt module */
	spinlock_t	SlowPathLock;	/* Normal IRQ lock */
	SK_PNMI_STRUCT_DATA PnmiStruct;	/* structure to get all Pnmi-Data */
	int			RlmtMode;	/* link check mode to set */
	int			RlmtNets;	/* Number of nets */
	
	SK_IOC		IoBase;		/* register set of adapter */
	int		BoardLevel;	/* level of active hw init (0-2) */
	char		DeviceStr[80];	/* adapter string from vpd */
	SK_U32		AllocFlag;	/* flag allocation of resources */
	struct pci_dev	*PciDev;	/* for access to pci config space */
	SK_U32		PciDevId;	/* pci device id */
	struct SK_NET_DEVICE	*dev[2];	/* pointer to device struct */
	char		Name[30];	/* driver name */
	struct SK_NET_DEVICE	*Next;		/* link all devices (for clearing) */
	int		RxBufSize;	/* length of receive buffers */
        struct net_device_stats stats;	/* linux 'netstat -i' statistics */
	int		Index;		/* internal board index number */

	/* adapter RAM sizes for queues of active port */
	int		RxQueueSize;	/* memory used for receive queue */
	int		TxSQueueSize;	/* memory used for sync. tx queue */
	int		TxAQueueSize;	/* memory used for async. tx queue */

	int		PromiscCount;	/* promiscuous mode counter  */
	int		AllMultiCount;  /* allmulticast mode counter */
	int		MulticCount;	/* number of different MC    */
					/*  addresses for this board */
					/*  (may be more than HW can)*/

	int		HWRevision;	/* Hardware revision */
	int		ActivePort;	/* the active XMAC port */
	int		MaxPorts;		/* number of activated ports */
	int		TxDescrPerRing;	/* # of descriptors per tx ring */
	int		RxDescrPerRing;	/* # of descriptors per rx ring */

	caddr_t		pDescrMem;	/* Pointer to the descriptor area */
	dma_addr_t	pDescrMemDMA;	/* PCI DMA address of area */

	/* the port structures with descriptor rings */
	TX_PORT		TxPort[SK_MAX_MACS][2];
	RX_PORT		RxPort[SK_MAX_MACS];

	unsigned int	CsOfs1;		/* for checksum calculation */
	unsigned int	CsOfs2;		/* for checksum calculation */
	SK_U32		CsOfs;		/* for checksum calculation */

	SK_BOOL		CheckQueue;	/* check event queue soon */
	SK_TIMER        DrvCleanupTimer;/* to check for pending descriptors */
	DIM_INFO        DynIrqModInfo;  /* all data related to DIM */

	/* Only for tests */
	int		PortUp;
	int		PortDown;
};


#endif /* __INC_SKDRV2ND_H */

