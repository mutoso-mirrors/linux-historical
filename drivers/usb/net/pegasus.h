/*
 * Copyright (c) 1999-2002 Petko Manolov - Petkan (petkan@users.sourceforge.net)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */


#ifndef	PEGASUS_DEV

#define	PEGASUS_II		0x80000000
#define	HAS_HOME_PNA		0x40000000

#define	PEGASUS_MTU		1536
#define	RX_SKBS			4

#define	EPROM_WRITE		0x01
#define	EPROM_READ		0x02
#define	EPROM_DONE		0x04
#define	EPROM_WR_ENABLE		0x10
#define	EPROM_LOAD		0x20

#define	PHY_DONE		0x80
#define	PHY_READ		0x40
#define	PHY_WRITE		0x20
#define	DEFAULT_GPIO_RESET	0x24
#define	LINKSYS_GPIO_RESET	0x24
#define	DEFAULT_GPIO_SET	0x26

#define	PEGASUS_PRESENT		0x00000001
#define	PEGASUS_RUNNING		0x00000002
#define	PEGASUS_TX_BUSY		0x00000004
#define	PEGASUS_RX_BUSY		0x00000008
#define	CTRL_URB_RUNNING	0x00000010
#define	CTRL_URB_SLEEP		0x00000020
#define	PEGASUS_UNPLUG		0x00000040
#define	PEGASUS_RX_URB_FAIL	0x00000080
#define	ETH_REGS_CHANGE		0x40000000
#define	ETH_REGS_CHANGED	0x80000000

#define	RX_MULTICAST		2
#define	RX_PROMISCUOUS		4

#define	REG_TIMEOUT		(HZ)
#define	PEGASUS_TX_TIMEOUT	(HZ*10)

#define	TX_UNDERRUN		0x80
#define	EXCESSIVE_COL		0x40
#define	LATE_COL		0x20
#define	NO_CARRIER		0x10
#define	LOSS_CARRIER		0x08
#define	JABBER_TIMEOUT		0x04

#define	LINK_STATUS		0x01

#define	PEGASUS_REQT_READ	0xc0
#define	PEGASUS_REQT_WRITE	0x40
#define	PEGASUS_REQ_GET_REGS	0xf0
#define	PEGASUS_REQ_SET_REGS	0xf1
#define	PEGASUS_REQ_SET_REG	PEGASUS_REQ_SET_REGS

enum pegasus_registers {
	EthCtrl0 = 0,
	EthCtrl1 = 1,
	EthCtrl2 = 2,
	EthID = 0x10,
	Reg1d = 0x1d,
	EpromOffset = 0x20,
	EpromData = 0x21,	/* 0x21 low, 0x22 high byte */
	EpromCtrl = 0x23,
	PhyAddr = 0x25,
	PhyData = 0x26, 	/* 0x26 low, 0x27 high byte */
	PhyCtrl = 0x28,
	UsbStst = 0x2a,
	EthTxStat0 = 0x2b,
	EthTxStat1 = 0x2c,
	EthRxStat = 0x2d,
	Reg7b = 0x7b,
	Gpio0 = 0x7e,
	Gpio1 = 0x7f,
	Reg81 = 0x81,
};


typedef struct pegasus {
	struct usb_device	*usb;
	struct net_device	*net;
	struct net_device_stats	stats;
	struct mii_if_info	mii;
	unsigned		flags;
	unsigned		features;
	int			dev_index;
	int			intr_interval;
	struct tasklet_struct	rx_tl;
	struct urb		*ctrl_urb, *rx_urb, *tx_urb, *intr_urb;
	struct sk_buff		*rx_pool[RX_SKBS];
	struct sk_buff		*rx_skb;
	struct usb_ctrlrequest	dr;
	wait_queue_head_t	ctrl_wait;
	struct semaphore	sem;
	spinlock_t		rx_pool_lock;
	int			chip;
	unsigned char		intr_buff[8];
	__u8			tx_buff[PEGASUS_MTU];
	__u8			eth_regs[4];
	__u8			phy;
	__u8			gpio_res;
} pegasus_t;


struct usb_eth_dev {
	char	*name;
	__u16	vendor;
	__u16	device;
	__u32	private; /* LSB is gpio reset value */
};

#define	VENDOR_3COM		0x0506
#define	VENDOR_ABOCOM		0x07b8
#define	VENDOR_ACCTON		0x083a
#define	VENDOR_ADMTEK		0x07a6
#define	VENDOR_AEILAB		0x3334
#define	VENDOR_ALLIEDTEL	0x07c9
#define	VENDOR_BELKIN		0x050d
#define	VENDOR_BILLIONTON	0x08dd
#define	VENDOR_COMPAQ		0x049f
#define	VENDOR_COREGA		0x07aa
#define	VENDOR_DLINK		0x2001
#define	VENDOR_ELCON		0x0db7
#define	VENDOR_ELSA		0x05cc
#define	VENDOR_HAWKING		0x0e66
#define	VENDOR_IODATA		0x04bb
#define	VENDOR_KINGSTON		0x0951
#define	VENDOR_LANEED		0x056e
#define	VENDOR_LINKSYS		0x066b
#define	VENDOR_MELCO		0x0411
#define	VENDOR_MOBILITY		0x1342
#define	VENDOR_NETGEAR		0x0846
#define	VENDOR_SMARTBRIDGES	0x08d1
#define	VENDOR_SMC		0x0707
#define	VENDOR_SOHOWARE		0x15e8
#define	VENDOR_SIEMENS		0x067c


#else	/* PEGASUS_DEV */

PEGASUS_DEV( "3Com USB Ethernet 3C460B", VENDOR_3COM, 0x4601,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "USB HPNA/Ethernet", VENDOR_ABOCOM, 0x110c,
		DEFAULT_GPIO_RESET | PEGASUS_II | HAS_HOME_PNA )
PEGASUS_DEV( "USB HPNA/Ethernet", VENDOR_ABOCOM, 0x4104,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "USB HPNA/Ethernet", VENDOR_ABOCOM, 0x4004,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "USB HPNA/Ethernet", VENDOR_ABOCOM, 0x4007,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "USB 10/100 Fast Ethernet", VENDOR_ABOCOM, 0x4102,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "USB 10/100 Fast Ethernet", VENDOR_ABOCOM, 0x4002,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "USB 10/100 Fast Ethernet", VENDOR_ABOCOM, 0x400b,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "USB 10/100 Fast Ethernet", VENDOR_ABOCOM, 0x400c,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "USB 10/100 Fast Ethernet", VENDOR_ABOCOM, 0xabc1,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "USB 10/100 Fast Ethernet", VENDOR_ABOCOM, 0x200c,
		DEFAULT_GPIO_RESET | PEGASUS_II ) 	
PEGASUS_DEV( "Accton USB 10/100 Ethernet Adapter", VENDOR_ACCTON, 0x1046,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "SpeedStream USB 10/100 Ethernet", VENDOR_ACCTON, 0x5046,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "ADMtek ADM8511 \"Pegasus II\" USB Ethernet",
		VENDOR_ADMTEK, 0x8511,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "ADMtek ADM8513 \"Pegasus II\" USB Ethernet",
		VENDOR_ADMTEK, 0x8513,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "ADMtek AN986 \"Pegasus\" USB Ethernet (evaluation board)",
		VENDOR_ADMTEK, 0x0986,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "AN986A USB MAC", VENDOR_ADMTEK, 1986,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "AEI USB Fast Ethernet Adapter", VENDOR_AEILAB, 0x1701,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "Allied Telesyn Int. AT-USB100", VENDOR_ALLIEDTEL, 0xb100,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "Belkin F5D5050 USB Ethernet", VENDOR_BELKIN, 0x0121,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "Billionton USB-100", VENDOR_BILLIONTON, 0x0986,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "Billionton USBLP-100", VENDOR_BILLIONTON, 0x0987,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "iPAQ Networking 10/100 USB", VENDOR_COMPAQ, 0x8511,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "Billionton USBEL-100", VENDOR_BILLIONTON, 0x0988,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "Billionton USBE-100", VENDOR_BILLIONTON, 0x8511,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "Corega FEter USB-TX", VENDOR_COREGA, 0x0004,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "Corega FEter USB-TXS", VENDOR_COREGA, 0x000d,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "D-Link DSB-650TX", VENDOR_DLINK, 0x4001,
		LINKSYS_GPIO_RESET )
PEGASUS_DEV( "D-Link DSB-650TX", VENDOR_DLINK, 0x4002,
		LINKSYS_GPIO_RESET )
PEGASUS_DEV( "D-Link DSB-650TX", VENDOR_DLINK, 0x4102,
		LINKSYS_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "D-Link DSB-650TX", VENDOR_DLINK, 0x400b,
		LINKSYS_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "D-Link DSB-650TX", VENDOR_DLINK, 0x200c,
		LINKSYS_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "D-Link DSB-650TX(PNA)", VENDOR_DLINK, 0x4003,
		DEFAULT_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "D-Link DSB-650", VENDOR_DLINK, 0xabc1,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "GOLDPFEIL USB Adapter", VENDOR_ELCON, 0x0002,
		DEFAULT_GPIO_RESET | PEGASUS_II | HAS_HOME_PNA )
PEGASUS_DEV( "EasiDock Ethernet", VENDOR_MOBILITY, 0x0304,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "Elsa Micolink USB2Ethernet", VENDOR_ELSA, 0x3000,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "Hawking UF100 10/100 Ethernet", VENDOR_HAWKING, 0x400c,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "IO DATA USB ET/TX", VENDOR_IODATA, 0x0904,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "IO DATA USB ET/TX-S", VENDOR_IODATA, 0x0913,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "Kingston KNU101TX Ethernet", VENDOR_KINGSTON, 0x000a,
		DEFAULT_GPIO_RESET)
PEGASUS_DEV( "LANEED USB Ethernet LD-USB/TX", VENDOR_LANEED, 0x4002,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "LANEED USB Ethernet LD-USB/TX", VENDOR_LANEED, 0x400b,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "LANEED USB Ethernet LD-USB/T", VENDOR_LANEED, 0xabc1,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "LANEED USB Ethernet LD-USB/TX", VENDOR_LANEED, 0x200c,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "Linksys USB10TX", VENDOR_LINKSYS, 0x2202,
		LINKSYS_GPIO_RESET )
PEGASUS_DEV( "Linksys USB100TX", VENDOR_LINKSYS, 0x2203,
		LINKSYS_GPIO_RESET )
PEGASUS_DEV( "Linksys USB100TX", VENDOR_LINKSYS, 0x2204,
		LINKSYS_GPIO_RESET | HAS_HOME_PNA )
PEGASUS_DEV( "Linksys USB10T Ethernet Adapter", VENDOR_LINKSYS, 0x2206,
		LINKSYS_GPIO_RESET | PEGASUS_II)
PEGASUS_DEV( "Linksys USB USB100TX", VENDOR_LINKSYS, 0x400b,
		LINKSYS_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "Linksys USB10TX", VENDOR_LINKSYS, 0x200c,
		LINKSYS_GPIO_RESET | PEGASUS_II )	
PEGASUS_DEV( "MELCO/BUFFALO LUA-TX", VENDOR_MELCO, 0x0001,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "MELCO/BUFFALO LUA-TX", VENDOR_MELCO, 0x0005,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "MELCO/BUFFALO LUA2-TX", VENDOR_MELCO, 0x0009,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "NETGEAR FA101", VENDOR_NETGEAR, 0x1020,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "smartNIC 2 PnP Adapter", VENDOR_SMARTBRIDGES, 0x0003,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "SMC 202 USB Ethernet", VENDOR_SMC, 0x0200,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "SMC 2206 USB Ethernet", VENDOR_SMC, 0x0201,
		DEFAULT_GPIO_RESET | PEGASUS_II)
PEGASUS_DEV( "SOHOware NUB100 Ethernet", VENDOR_SOHOWARE, 0x9100,
		DEFAULT_GPIO_RESET )
PEGASUS_DEV( "SOHOware NUB110 Ethernet", VENDOR_SOHOWARE, 0x9110,
		DEFAULT_GPIO_RESET | PEGASUS_II )
PEGASUS_DEV( "SpeedStream USB 10/100 Ethernet", VENDOR_SIEMENS, 0x1001,
		DEFAULT_GPIO_RESET | PEGASUS_II )


#endif	/* PEGASUS_DEV */
