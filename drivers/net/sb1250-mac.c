/*
 * Copyright (C) 2001 Broadcom Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

/*
  This driver is designed for the Broadcom BCM12500 SOC chip's built-in
  Ethernet controllers.
  
  The author may be reached as mpl@broadcom.com
*/


/* A few user-configurable values.
   These may be modified when a driver module is loaded. */

static int debug = 1;			/* 1 normal messages, 0 quiet .. 7 verbose. */

/* Used to pass the media type, etc.
   Both 'options[]' and 'full_duplex[]' should exist for driver
   interoperability.
   The media type is usually passed in 'options[]'.
*/

#define MAX_UNITS 3		/* More are supported, limit only on options */
#ifdef MODULE
static int options[MAX_UNITS] = {-1, -1, -1};
static int full_duplex[MAX_UNITS] = {-1, -1, -1};
#endif


/* Operational parameters that usually are not changed. */

/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (2*HZ)

#if !defined(__OPTIMIZE__)  ||  !defined(__KERNEL__)
#warning  You must compile this file with the correct options!
#warning  See the last lines of the source file.
#error  You must compile this driver with "-O".
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/config.h>
#include <asm/processor.h>		/* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/sibyte/sb1250.h>
#include <asm/sibyte/64bit.h>

/* This is only here until the firmware is ready.  In that case,
   the firmware leaves the ethernet address in the register for us. */
#ifdef CONFIG_SWARM_STANDALONE
#define SBMAC_ETH0_HWADDR "40:00:00:00:01:00"
#define SBMAC_ETH1_HWADDR "40:00:00:00:01:01"
#define SBMAC_ETH2_HWADDR "40:00:00:00:01:02"
#endif


/* These identify the driver base version and may not be removed. */
#if 0
static char version1[] __devinitdata =
"sb1250-mac.c:1.00 1/11/2001 Written by Mitch Lichtenberg (mpl@broadcom.com)\n";
#endif



MODULE_AUTHOR("Mitch Lichtenberg (mpl@broadcom.com)");
MODULE_DESCRIPTION("Broadcom BCM12500 SOC GB Ethernet driver");
MODULE_PARM(debug, "i");
MODULE_PARM(options, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");


#include <asm/sibyte/sb1250_defs.h>
#include <asm/sibyte/sb1250_regs.h>
#include <asm/sibyte/sb1250_mac.h>
#include <asm/sibyte/sb1250_dma.h>
#include <asm/sibyte/sb1250_int.h>


/**********************************************************************
 *  Simple types
 ********************************************************************* */


typedef unsigned long sbmac_port_t;
typedef uint64_t sbmac_physaddr_t;
typedef uint64_t sbmac_enetaddr_t;

typedef enum { sbmac_speed_auto, sbmac_speed_10,
	       sbmac_speed_100, sbmac_speed_1000 } sbmac_speed_t;

typedef enum { sbmac_duplex_auto, sbmac_duplex_half,
	       sbmac_duplex_full } sbmac_duplex_t;

typedef enum { sbmac_fc_auto, sbmac_fc_disabled, sbmac_fc_frame,
	       sbmac_fc_collision, sbmac_fc_carrier } sbmac_fc_t;

typedef enum { sbmac_state_uninit, sbmac_state_off, sbmac_state_on, 
	       sbmac_state_broken } sbmac_state_t;


/**********************************************************************
 *  Macros
 ********************************************************************* */


#define SBDMA_NEXTBUF(d,f) ((((d)->f+1) == (d)->sbdma_dscrtable_end) ? \
			  (d)->sbdma_dscrtable : (d)->f+1)


#define CACHELINESIZE 32
#define NUMCACHEBLKS(x) (((x)+CACHELINESIZE-1)/CACHELINESIZE)
#define KMALLOC(x) kmalloc((x),GFP_KERNEL)
#define KFREE(x) kfree(x)
#define KVTOPHYS(x) virt_to_bus((void *)(x))

 
#define SBMAC_READCSR(t)    (in64((unsigned long)(t)))
#define SBMAC_WRITECSR(t,v) (out64(v, (unsigned long)(t)))

#define PKSEG1(x) ((sbmac_port_t) KSEG1ADDR(x))

#define SBMAC_MAX_TXDESCR	32
#define SBMAC_MAX_RXDESCR	32

#define ETHER_ALIGN	2
#define ETHER_ADDR_LEN	6
#define ENET_PACKET_SIZE	1518

/**********************************************************************
 *  DMA Descriptor structure
 ********************************************************************* */

typedef struct sbdmadscr_s {
	uint64_t  dscr_a;
	uint64_t  dscr_b;
} sbdmadscr_t;

typedef unsigned long paddr_t;
typedef unsigned long vaddr_t;

/**********************************************************************
 *  DMA Controller structure
 ********************************************************************* */

typedef struct sbmacdma_s {
	
	/* 
	 * This stuff is used to identify the channel and the registers
	 * associated with it.
	 */
	
	struct sbmac_softc *sbdma_eth;	        /* back pointer to associated MAC */
	int              sbdma_channel;	/* channel number */
	int		     sbdma_txdir;       /* direction (1=transmit) */
	int		     sbdma_maxdescr;	/* total # of descriptors in ring */
	sbmac_port_t     sbdma_config0;	/* DMA config register 0 */
	sbmac_port_t     sbdma_config1;	/* DMA config register 1 */
	sbmac_port_t     sbdma_dscrbase;	/* Descriptor base address */
	sbmac_port_t     sbdma_dscrcnt;     /* Descriptor count register */
	sbmac_port_t     sbdma_curdscr;	/* current descriptor address */
	
	/*
	 * This stuff is for maintenance of the ring
	 */
	
	sbdmadscr_t     *sbdma_dscrtable;	/* base of descriptor table */
	sbdmadscr_t     *sbdma_dscrtable_end; /* end of descriptor table */
	
	struct sk_buff **sbdma_ctxtable;    /* context table, one per descr */
	
	paddr_t          sbdma_dscrtable_phys; /* and also the phys addr */
	sbdmadscr_t     *sbdma_addptr;	/* next dscr for sw to add */
	sbdmadscr_t     *sbdma_remptr;	/* next dscr for sw to remove */
	
} sbmacdma_t;


/**********************************************************************
 *  Ethernet softc structure
 ********************************************************************* */

struct sbmac_softc {
	
	/*
	 * Linux-specific things
	 */
	
	struct net_device *sbm_dev;		/* pointer to linux device */
	spinlock_t sbm_lock;		/* spin lock */
	struct timer_list sbm_timer;     	/* for monitoring MII */
	struct net_device_stats sbm_stats; 
	int sbm_devflags;			/* current device flags */

        int	     sbm_phy_oldbmsr;
        int	     sbm_phy_oldanlpar;
        int	     sbm_phy_oldk1stsr;
        int          sbm_phy_oldlinkstat;
	int sbm_buffersize;
	
	unsigned char sbm_phys[2];
	
	/*
	 * Controller-specific things
	 */
	
	sbmac_port_t     sbm_base;          /* MAC's base address */
	sbmac_state_t    sbm_state;         /* current state */
	
	sbmac_port_t     sbm_macenable;	/* MAC Enable Register */
	sbmac_port_t     sbm_maccfg;	/* MAC Configuration Register */
	sbmac_port_t     sbm_fifocfg;	/* FIFO configuration register */
	sbmac_port_t     sbm_framecfg;	/* Frame configuration register */
	sbmac_port_t     sbm_rxfilter;	/* receive filter register */
	sbmac_port_t     sbm_isr;		/* Interrupt status register */
	sbmac_port_t     sbm_imr;		/* Interrupt mask register */
	sbmac_port_t     sbm_mdio;		/* MDIO register */
	
	sbmac_speed_t    sbm_speed;		/* current speed */
	sbmac_duplex_t   sbm_duplex;	/* current duplex */
	sbmac_fc_t       sbm_fc;		/* current flow control setting */
	
	u_char           sbm_hwaddr[ETHER_ADDR_LEN];
	
	sbmacdma_t       sbm_txdma;		/* for now, only use channel 0 */
	sbmacdma_t       sbm_rxdma;
	
};


/**********************************************************************
 *  Externs
 ********************************************************************* */

/**********************************************************************
 *  Prototypes
 ********************************************************************* */

static void sbdma_initctx(sbmacdma_t *d,
			  struct sbmac_softc *s,
			  int chan,
			  int txrx,
			  int maxdescr);
static void sbdma_channel_start(sbmacdma_t *d);
static int sbdma_add_rcvbuffer(sbmacdma_t *d,struct sk_buff *m);
static int sbdma_add_txbuffer(sbmacdma_t *d,struct sk_buff *m);
static void sbdma_emptyring(sbmacdma_t *d);
static void sbdma_fillring(sbmacdma_t *d);
static void sbdma_rx_process(struct sbmac_softc *sc,sbmacdma_t *d);
static void sbdma_tx_process(struct sbmac_softc *sc,sbmacdma_t *d);
static int sbmac_initctx(struct sbmac_softc *s);
static void sbmac_channel_start(struct sbmac_softc *s);
static void sbmac_channel_stop(struct sbmac_softc *s);
static sbmac_state_t sbmac_set_channel_state(struct sbmac_softc *,sbmac_state_t);
static void sbmac_promiscuous_mode(struct sbmac_softc *sc,int onoff);
/*static void sbmac_init_and_start(struct sbmac_softc *sc);*/
static uint64_t sbmac_addr2reg(unsigned char *ptr);
static void sbmac_intr(int irq,void *dev_instance,struct pt_regs *rgs);
static int sbmac_start_tx(struct sk_buff *skb, struct net_device *dev);
static void sbmac_setmulti(struct sbmac_softc *sc);
static int sbmac_init(struct net_device *dev);
static int sbmac_set_speed(struct sbmac_softc *s,sbmac_speed_t speed);
static int sbmac_set_duplex(struct sbmac_softc *s,sbmac_duplex_t duplex,sbmac_fc_t fc);

static int sbmac_open(struct net_device *dev);
static void sbmac_timer(unsigned long data);
static void sbmac_tx_timeout (struct net_device *dev);
static struct net_device_stats *sbmac_get_stats(struct net_device *dev);
static void sbmac_set_rx_mode(struct net_device *dev);
static int sbmac_mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static int sbmac_close(struct net_device *dev);
static int sbmac_mii_poll(struct sbmac_softc *s,int noisy);

static void sbmac_mii_sync(struct sbmac_softc *s);
static void sbmac_mii_senddata(struct sbmac_softc *s,unsigned int data, int bitcnt);
static unsigned int sbmac_mii_read(struct sbmac_softc *s,int phyaddr,int regidx);
static void sbmac_mii_write(struct sbmac_softc *s,int phyaddr,int regidx,
			    unsigned int regval);


/**********************************************************************
 *  Globals
 ********************************************************************* */


/**********************************************************************
 *  MDIO constants
 ********************************************************************* */

#define	MII_COMMAND_START	0x01
#define	MII_COMMAND_READ	0x02
#define	MII_COMMAND_WRITE	0x01
#define	MII_COMMAND_ACK		0x02

#define BMCR_RESET     0x8000
#define BMCR_LOOPBACK  0x4000
#define BMCR_SPEED0    0x2000
#define BMCR_ANENABLE  0x1000
#define BMCR_POWERDOWN 0x0800
#define BMCR_ISOLATE   0x0400
#define BMCR_RESTARTAN 0x0200
#define BMCR_DUPLEX    0x0100
#define BMCR_COLTEST   0x0080
#define BMCR_SPEED1    0x0040
#define BMCR_SPEED1000 (BMCR_SPEED1|BMCR_SPEED0)
#define BMCR_SPEED100  (BMCR_SPEED0)
#define BMCR_SPEED10 	0

#define BMSR_100BT4	0x8000
#define BMSR_100BT_FDX	0x4000
#define BMSR_100BT_HDX  0x2000
#define BMSR_10BT_FDX   0x1000
#define BMSR_10BT_HDX   0x0800
#define BMSR_100BT2_FDX 0x0400
#define BMSR_100BT2_HDX 0x0200
#define BMSR_1000BT_XSR	0x0100
#define BMSR_PRESUP	0x0040
#define BMSR_ANCOMPLT	0x0020
#define BMSR_REMFAULT	0x0010
#define BMSR_AUTONEG	0x0008
#define BMSR_LINKSTAT	0x0004
#define BMSR_JABDETECT	0x0002
#define BMSR_EXTCAPAB	0x0001

#define PHYIDR1 	0x2000
#define PHYIDR2		0x5C60

#define ANAR_NP		0x8000
#define ANAR_RF		0x2000
#define ANAR_ASYPAUSE	0x0800
#define ANAR_PAUSE	0x0400
#define ANAR_T4		0x0200
#define ANAR_TXFD	0x0100
#define ANAR_TXHD	0x0080
#define ANAR_10FD	0x0040
#define ANAR_10HD	0x0020
#define ANAR_PSB	0x0001

#define ANLPAR_NP	0x8000
#define ANLPAR_ACK	0x4000
#define ANLPAR_RF	0x2000
#define ANLPAR_ASYPAUSE	0x0800
#define ANLPAR_PAUSE	0x0400
#define ANLPAR_T4	0x0200
#define ANLPAR_TXFD	0x0100
#define ANLPAR_TXHD	0x0080
#define ANLPAR_10FD	0x0040
#define ANLPAR_10HD	0x0020
#define ANLPAR_PSB	0x0001	/* 802.3 */

#define ANER_PDF	0x0010
#define ANER_LPNPABLE	0x0008
#define ANER_NPABLE	0x0004
#define ANER_PAGERX	0x0002
#define ANER_LPANABLE	0x0001

#define ANNPTR_NP	0x8000
#define ANNPTR_MP	0x2000
#define ANNPTR_ACK2	0x1000
#define ANNPTR_TOGTX	0x0800
#define ANNPTR_CODE	0x0008

#define ANNPRR_NP	0x8000
#define ANNPRR_MP	0x2000
#define ANNPRR_ACK3	0x1000
#define ANNPRR_TOGTX	0x0800
#define ANNPRR_CODE	0x0008

#define K1TCR_TESTMODE	0x0000
#define K1TCR_MSMCE	0x1000
#define K1TCR_MSCV	0x0800
#define K1TCR_RPTR	0x0400
#define K1TCR_1000BT_FDX 0x200
#define K1TCR_1000BT_HDX 0x100

#define K1STSR_MSMCFLT	0x8000
#define K1STSR_MSCFGRES	0x4000
#define K1STSR_LRSTAT	0x2000
#define K1STSR_RRSTAT	0x1000
#define K1STSR_LP1KFD	0x0800
#define K1STSR_LP1KHD   0x0400
#define K1STSR_LPASMDIR	0x0200

#define K1SCR_1KX_FDX	0x8000
#define K1SCR_1KX_HDX	0x4000
#define K1SCR_1KT_FDX	0x2000
#define K1SCR_1KT_HDX	0x1000

#define STRAP_PHY1	0x0800
#define STRAP_NCMODE	0x0400
#define STRAP_MANMSCFG	0x0200
#define STRAP_ANENABLE	0x0100
#define STRAP_MSVAL	0x0080
#define STRAP_1KHDXADV	0x0010
#define STRAP_1KFDXADV	0x0008
#define STRAP_100ADV	0x0004
#define STRAP_SPEEDSEL	0x0000
#define STRAP_SPEED100	0x0001

#define PHYSUP_SPEED1000 0x10
#define PHYSUP_SPEED100  0x08
#define PHYSUP_SPEED10   0x00
#define PHYSUP_LINKUP	 0x04
#define PHYSUP_FDX       0x02

#define	MII_BMCR	0x00 	/* Basic mode control register (rw) */
#define	MII_BMSR	0x01	/* Basic mode status register (ro) */
#define MII_K1STSR	0x0A	/* 1K Status Register (ro) */
#define	MII_ANLPAR	0x05	/* Autonegotiation lnk partner abilities (rw) */


#define M_MAC_MDIO_DIR_OUTPUT	0		/* for clarity */


/**********************************************************************
 *  SBMAC_MII_SYNC(s)
 *  
 *  Synchronize with the MII - send a pattern of bits to the MII
 *  that will guarantee that it is ready to accept a command.
 *  
 *  Input parameters: 
 *  	   s - sbmac structure
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_mii_sync(struct sbmac_softc *s)
{
	int cnt;
	uint64_t bits;
	
	bits = M_MAC_MDIO_DIR_OUTPUT | M_MAC_MDIO_OUT;
	
	SBMAC_WRITECSR(s->sbm_mdio,bits);
	
	for (cnt = 0; cnt < 32; cnt++) {
		SBMAC_WRITECSR(s->sbm_mdio,bits | M_MAC_MDC);
		SBMAC_WRITECSR(s->sbm_mdio,bits);
	}
}

/**********************************************************************
 *  SBMAC_MII_SENDDATA(s,data,bitcnt)
 *  
 *  Send some bits to the MII.  The bits to be sent are right-
 *  justified in the 'data' parameter.
 *  
 *  Input parameters: 
 *  	   s - sbmac structure
 *  	   data - data to send
 *  	   bitcnt - number of bits to send
 ********************************************************************* */

static void sbmac_mii_senddata(struct sbmac_softc *s,unsigned int data, int bitcnt)
{
	int i;
	uint64_t bits;
	unsigned int curmask;
	
	bits = M_MAC_MDIO_DIR_OUTPUT;
	SBMAC_WRITECSR(s->sbm_mdio,bits);
	
	curmask = 1 << (bitcnt - 1);
	
	for (i = 0; i < bitcnt; i++) {
		if (data & curmask) bits |= M_MAC_MDIO_OUT;
		else bits &= ~M_MAC_MDIO_OUT;
		SBMAC_WRITECSR(s->sbm_mdio,bits);
		SBMAC_WRITECSR(s->sbm_mdio,bits | M_MAC_MDC);
		SBMAC_WRITECSR(s->sbm_mdio,bits);
		curmask >>= 1;
	}
}



/**********************************************************************
 *  SBMAC_MII_READ(s,phyaddr,regidx)
 *  
 *  Read a PHY register.
 *  
 *  Input parameters: 
 *  	   s - sbmac structure
 *  	   phyaddr - PHY's address
 *  	   regidx = index of register to read
 *  	   
 *  Return value:
 *  	   value read, or 0 if an error occured.
 ********************************************************************* */

static unsigned int sbmac_mii_read(struct sbmac_softc *s,int phyaddr,int regidx)
{
	int idx;
	int error;
	int regval;
	
	/*
	 * Synchronize ourselves so that the PHY knows the next
	 * thing coming down is a command
	 */
	
	sbmac_mii_sync(s);
	
	/*
	 * Send the data to the PHY.  The sequence is
	 * a "start" command (2 bits)
	 * a "read" command (2 bits)
	 * the PHY addr (5 bits)
	 * the register index (5 bits)
	 */
	
	sbmac_mii_senddata(s,MII_COMMAND_START, 2);
	sbmac_mii_senddata(s,MII_COMMAND_READ, 2);
	sbmac_mii_senddata(s,phyaddr, 5);
	sbmac_mii_senddata(s,regidx, 5);
	
	/* 
	 * Switch the port around without a clock transition.
	 */
	SBMAC_WRITECSR(s->sbm_mdio,M_MAC_MDIO_DIR_INPUT);
	
	/*
	 * Send out a clock pulse to signal we want the status
	 */
	
	SBMAC_WRITECSR(s->sbm_mdio,M_MAC_MDIO_DIR_INPUT | M_MAC_MDC);
	SBMAC_WRITECSR(s->sbm_mdio,M_MAC_MDIO_DIR_INPUT);
	
	/* 
	 * If an error occured, the PHY will signal '1' back
	 */
	error = SBMAC_READCSR(s->sbm_mdio) & M_MAC_MDIO_IN;
	
	/* 
	 * Issue an 'idle' clock pulse, but keep the direction
	 * the same.
	 */
	SBMAC_WRITECSR(s->sbm_mdio,M_MAC_MDIO_DIR_INPUT | M_MAC_MDC);
	SBMAC_WRITECSR(s->sbm_mdio,M_MAC_MDIO_DIR_INPUT);
	
	regval = 0;
	
	for (idx = 0; idx < 16; idx++) {
		regval <<= 1;
		
		if (error == 0) {
			if (SBMAC_READCSR(s->sbm_mdio) & M_MAC_MDIO_IN) regval |= 1;
		}
		
		SBMAC_WRITECSR(s->sbm_mdio,M_MAC_MDIO_DIR_INPUT | M_MAC_MDC);
		SBMAC_WRITECSR(s->sbm_mdio,M_MAC_MDIO_DIR_INPUT);
	}
	
	/* Switch back to output */
	SBMAC_WRITECSR(s->sbm_mdio,M_MAC_MDIO_DIR_OUTPUT);
	
	if (error == 0) return regval;
	return 0;
}


/**********************************************************************
 *  SBMAC_MII_WRITE(s,phyaddr,regidx,regval)
 *  
 *  Write a value to a PHY register.
 *  
 *  Input parameters: 
 *  	   s - sbmac structure
 *  	   phyaddr - PHY to use
 *  	   regidx - register within the PHY
 *  	   regval - data to write to register
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_mii_write(struct sbmac_softc *s,int phyaddr,int regidx,
			    unsigned int regval)
{
	
	sbmac_mii_sync(s);
	
	sbmac_mii_senddata(s,MII_COMMAND_START,2);
	sbmac_mii_senddata(s,MII_COMMAND_WRITE,2);
	sbmac_mii_senddata(s,phyaddr, 5);
	sbmac_mii_senddata(s,regidx, 5);
	sbmac_mii_senddata(s,MII_COMMAND_ACK,2);
	sbmac_mii_senddata(s,regval,16);
	
	SBMAC_WRITECSR(s->sbm_mdio,M_MAC_MDIO_DIR_OUTPUT);
}



/**********************************************************************
 *  SBDMA_INITCTX(d,s,chan,txrx,maxdescr)
 *  
 *  Initialize a DMA channel context.  Since there are potentially
 *  eight DMA channels per MAC, it's nice to do this in a standard
 *  way.  
 *  
 *  Input parameters: 
 *  	   d - sbmacdma_t structure (DMA channel context)
 *  	   s - sbmac_softc structure (pointer to a MAC)
 *  	   chan - channel number (0..1 right now)
 *  	   txrx - Identifies DMA_TX or DMA_RX for channel direction
 *      maxdescr - number of descriptors
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_initctx(sbmacdma_t *d,
			  struct sbmac_softc *s,
			  int chan,
			  int txrx,
			  int maxdescr)
{
	/* 
	 * Save away interesting stuff in the structure 
	 */
	
	d->sbdma_eth       = s;
	d->sbdma_channel   = chan;
	d->sbdma_txdir     = txrx;
	
	/* 
	 * initialize register pointers 
	 */
	
	d->sbdma_config0 = 
		PKSEG1(s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_CONFIG0));
	d->sbdma_config1 = 
		PKSEG1(s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_CONFIG0));
	d->sbdma_dscrbase = 
		PKSEG1(s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_DSCR_BASE));
	d->sbdma_dscrcnt = 
		PKSEG1(s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_DSCR_CNT));
	d->sbdma_curdscr = 	
		PKSEG1(s->sbm_base + R_MAC_DMA_REGISTER(txrx,chan,R_MAC_DMA_CUR_DSCRADDR));
	
	/*
	 * Allocate memory for the ring
	 */
	
	d->sbdma_maxdescr = maxdescr;
	
	d->sbdma_dscrtable = (sbdmadscr_t *) 
		KMALLOC(d->sbdma_maxdescr*sizeof(sbdmadscr_t));
	
	memset(d->sbdma_dscrtable,0,d->sbdma_maxdescr*sizeof(sbdmadscr_t));
	
	d->sbdma_dscrtable_end = d->sbdma_dscrtable + d->sbdma_maxdescr;
	
	d->sbdma_dscrtable_phys = KVTOPHYS(d->sbdma_dscrtable);
	
	/*
	 * And context table
	 */
	
	d->sbdma_ctxtable = (struct sk_buff **) 
		KMALLOC(d->sbdma_maxdescr*sizeof(struct sk_buff *));
	
	memset(d->sbdma_ctxtable,0,d->sbdma_maxdescr*sizeof(struct sk_buff *));
	
}

/**********************************************************************
 *  SBDMA_CHANNEL_START(d)
 *  
 *  Initialize the hardware registers for a DMA channel.
 *  
 *  Input parameters: 
 *  	   d - DMA channel to init (context must be previously init'd
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_channel_start(sbmacdma_t *d)
{
	/*
	 * Turn on the DMA channel
	 */
	
	SBMAC_WRITECSR(d->sbdma_config1,0);
	
	SBMAC_WRITECSR(d->sbdma_dscrbase,d->sbdma_dscrtable_phys);
	
	SBMAC_WRITECSR(d->sbdma_config0,
		       V_DMA_RINGSZ(d->sbdma_maxdescr) |
		       0);
	
	/*
	 * Initialize ring pointers
	 */
	
	d->sbdma_addptr = d->sbdma_dscrtable;
	d->sbdma_remptr = d->sbdma_dscrtable;
}


static void sbdma_align_skb(struct sk_buff *skb,int power2,int offset)
{
	unsigned long addr;
	unsigned long newaddr;
	
	addr = (unsigned long) skb->data;
	
	newaddr = (addr + power2 - 1) & ~(power2 - 1);
	
	skb_reserve(skb,newaddr-addr+offset);
}


/**********************************************************************
 *  SBDMA_ADD_RCVBUFFER(d,sb)
 *  
 *  Add a buffer to the specified DMA channel.   For receive channels,
 *  this queues a buffer for inbound packets.
 *  
 *  Input parameters: 
 *  	   d - DMA channel descriptor
 * 	   sb - sk_buff to add, or NULL if we should allocate one
 *  	   
 *  Return value:
 *  	   0 if buffer could not be added (ring is full)
 *  	   1 if buffer added successfully
 ********************************************************************* */


static int sbdma_add_rcvbuffer(sbmacdma_t *d,struct sk_buff *sb)
{
	sbdmadscr_t *dsc;
	sbdmadscr_t *nextdsc;
	struct sk_buff *sb_new = NULL;
	int pktsize = ENET_PACKET_SIZE;
	
	/* get pointer to our current place in the ring */
	
	dsc = d->sbdma_addptr;
	nextdsc = SBDMA_NEXTBUF(d,sbdma_addptr);
	
	/*
	 * figure out if the ring is full - if the next descriptor
	 * is the same as the one that we're going to remove from
	 * the ring, the ring is full
	 */
	
	if (nextdsc == d->sbdma_remptr) {
		return -ENOSPC;
	}
	
	/* 
	 * Allocate a sk_buff if we don't already have one.  
	 * If we do have an sk_buff, reset it so that it's empty.
	 *
	 * Note: sk_buffs don't seem to be guaranteed to have any sort
	 * of alignment when they are allocated.  Therefore, allocate enough
	 * extra space to make sure that:
	 *
	 *    1. the data does not start in the middle of a cache line.
	 *    2. The data does not end in the middle of a cache line
	 * 	  3. The buffer can be aligned such that the IP addresses are 
	 *       naturally aligned.
	 *
	 *  Remember, the SB1250's MAC writes whole cache lines at a time,
	 *  without reading the old contents first.  So, if the sk_buff's
	 *  data portion starts in the middle of a cache line, the SB1250
	 *  DMA will trash the beginning (and ending) portions.
	 */
	
	if (sb == NULL) {
		sb_new = dev_alloc_skb(ENET_PACKET_SIZE + CACHELINESIZE*2 + ETHER_ALIGN);
		if (sb_new == NULL) {
			printk(KERN_INFO "%s: sk_buff allocation failed\n",
			       d->sbdma_eth->sbm_dev->name);
			return -ENOBUFS;
		}
		
		sbdma_align_skb(sb_new,CACHELINESIZE,ETHER_ALIGN);
		
		/* mark skbuff owned by our device */
		sb_new->dev = d->sbdma_eth->sbm_dev;
	}
	else {
		sb_new = sb;
		/* 
		 * nothing special to reinit buffer, it's already aligned
		 * and sb->tail already points to a good place.
		 */
	}
	
	/*
	 * fill in the descriptor 
	 */
	
	dsc->dscr_a = KVTOPHYS(sb_new->tail) |
		V_DMA_DSCRA_A_SIZE(NUMCACHEBLKS(pktsize+ETHER_ALIGN)) |
		M_DMA_DSCRA_INTERRUPT;
	
	/* receiving: no options */
	dsc->dscr_b = 0;
	
	/*
	 * fill in the context 
	 */
	
	d->sbdma_ctxtable[dsc-d->sbdma_dscrtable] = sb_new;
	
	/* 
	 * point at next packet 
	 */
	
	d->sbdma_addptr = nextdsc;
	
	/* 
	 * Give the buffer to the DMA engine.
	 */
	
	SBMAC_WRITECSR(d->sbdma_dscrcnt,1);
	
	return 0;					/* we did it */
}

/**********************************************************************
 *  SBDMA_ADD_TXBUFFER(d,sb)
 *  
 *  Add a transmit buffer to the specified DMA channel, causing a
 *  transmit to start.
 *  
 *  Input parameters: 
 *  	   d - DMA channel descriptor
 * 	   sb - sk_buff to add
 *  	   
 *  Return value:
 *  	   0 transmit queued successfully
 *  	   otherwise error code
 ********************************************************************* */


static int sbdma_add_txbuffer(sbmacdma_t *d,struct sk_buff *sb)
{
	sbdmadscr_t *dsc;
	sbdmadscr_t *nextdsc;
	uint64_t phys;
	uint64_t ncb;
	int length;
	
	/* get pointer to our current place in the ring */
	
	dsc = d->sbdma_addptr;
	nextdsc = SBDMA_NEXTBUF(d,sbdma_addptr);
	
	/*
	 * figure out if the ring is full - if the next descriptor
	 * is the same as the one that we're going to remove from
	 * the ring, the ring is full
	 */
	
	if (nextdsc == d->sbdma_remptr) {
		return -ENOSPC;
	}
	
	/*
	 * Under Linux, it's not necessary to copy/coalesce buffers
	 * like it is on NetBSD.  We think they're all contiguous,
	 * but that may not be true for GBE.
	 */
	
	length = sb->len;
	
	/*
	 * fill in the descriptor.  Note that the number of cache
	 * blocks in the descriptor is the number of blocks
	 * *spanned*, so we need to add in the offset (if any)
	 * while doing the calculation.
	 */
	
	phys = KVTOPHYS(sb->data);
	ncb = NUMCACHEBLKS(length+(phys & (CACHELINESIZE-1)));
	
	dsc->dscr_a = phys | 
		V_DMA_DSCRA_A_SIZE(ncb) |
		M_DMA_DSCRA_INTERRUPT |
		M_DMA_ETHTX_SOP;
	
	/* transmitting: set outbound options and length */

	dsc->dscr_b = V_DMA_DSCRB_OPTIONS(K_DMA_ETHTX_APPENDCRC_APPENDPAD) |
		V_DMA_DSCRB_PKT_SIZE(length);
	
	/*
	 * fill in the context 
	 */
	
	d->sbdma_ctxtable[dsc-d->sbdma_dscrtable] = sb;
	
	/* 
	 * point at next packet 
	 */
	
	d->sbdma_addptr = nextdsc;
	
	/* 
	 * Give the buffer to the DMA engine.
	 */
	
	SBMAC_WRITECSR(d->sbdma_dscrcnt,1);
	
	return 0;					/* we did it */
}




/**********************************************************************
 *  SBDMA_EMPTYRING(d)
 *  
 *  Free all allocated sk_buffs on the specified DMA channel;
 *  
 *  Input parameters: 
 *  	   d  - DMA channel
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_emptyring(sbmacdma_t *d)
{
	int idx;
	struct sk_buff *sb;
	
	for (idx = 0; idx < d->sbdma_maxdescr; idx++) {
		sb = d->sbdma_ctxtable[idx];
		if (sb) {
			dev_kfree_skb(sb);
			d->sbdma_ctxtable[idx] = NULL;
		}
	}
}


/**********************************************************************
 *  SBDMA_FILLRING(d)
 *  
 *  Fill the specified DMA channel (must be receive channel)
 *  with sk_buffs
 *  
 *  Input parameters: 
 *  	   d - DMA channel
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_fillring(sbmacdma_t *d)
{
	int idx;
	
	for (idx = 0; idx < SBMAC_MAX_RXDESCR-1; idx++) {
		if (sbdma_add_rcvbuffer(d,NULL) != 0) break;
	}
}


/**********************************************************************
 *  SBDMA_RX_PROCESS(sc,d)
 *  
 *  Process "completed" receive buffers on the specified DMA channel.  
 *  Note that this isn't really ideal for priority channels, since
 *  it processes all of the packets on a given channel before 
 *  returning. 
 *
 *  Input parameters: 
 *	   sc - softc structure
 *  	   d - DMA channel context
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_rx_process(struct sbmac_softc *sc,sbmacdma_t *d)
{
	int curidx;
	int hwidx;
	sbdmadscr_t *dsc;
	struct sk_buff *sb;
	int len;
	
	for (;;) {
		/* 
		 * figure out where we are (as an index) and where
		 * the hardware is (also as an index)
		 *
		 * This could be done faster if (for example) the 
		 * descriptor table was page-aligned and contiguous in
		 * both virtual and physical memory -- you could then
		 * just compare the low-order bits of the virtual address
		 * (sbdma_remptr) and the physical address (sbdma_curdscr CSR)
		 */
		
		curidx = d->sbdma_remptr - d->sbdma_dscrtable;
		hwidx = (int) (((SBMAC_READCSR(d->sbdma_curdscr) & M_DMA_CURDSCR_ADDR) -
				d->sbdma_dscrtable_phys) / sizeof(sbdmadscr_t));
		
		/*
		 * If they're the same, that means we've processed all
		 * of the descriptors up to (but not including) the one that
		 * the hardware is working on right now.
		 */
		
		if (curidx == hwidx) break;
		
		/*
		 * Otherwise, get the packet's sk_buff ptr back
		 */
		
		dsc = &(d->sbdma_dscrtable[curidx]);
		sb = d->sbdma_ctxtable[curidx];
		d->sbdma_ctxtable[curidx] = NULL;
		
		len = (int)G_DMA_DSCRB_PKT_SIZE(dsc->dscr_b) - 4;
		
		/*
		 * Check packet status.  If good, process it.
		 * If not, silently drop it and put it back on the
		 * receive ring.
		 */
		
		if (!(dsc->dscr_a & M_DMA_ETHRX_BAD)) {
			
			/*
			 * Set length into the packet
			 */
			skb_put(sb,len);
			
       			/*
			 * Add a new buffer to replace the old one.  If we fail
			 * to allocate a buffer, we're going to drop this
			 * packet and put it right back on the receive ring.
			 */
			
			if (sbdma_add_rcvbuffer(d,NULL) == -ENOBUFS) {
			    sbdma_add_rcvbuffer(d,sb);	/* re-add old buffer */
			    }
			else {
			    /*
			     * Buffer has been replaced on the receive ring.
			     * Pass the buffer to the kernel
			     */
			    sc->sbm_stats.rx_bytes += len;
			    sc->sbm_stats.rx_packets++;
			    sb->protocol = eth_type_trans(sb,d->sbdma_eth->sbm_dev);
			    netif_rx(sb);
			    }
		}
		else {
			/*
			 * Packet was mangled somehow.  Just drop it and
			 * put it back on the receive ring.
			 */
			sbdma_add_rcvbuffer(d,sb);
		}
		
		
		/* 
		 * .. and advance to the next buffer.
		 */
		
		d->sbdma_remptr = SBDMA_NEXTBUF(d,sbdma_remptr);
		
	}
}



/**********************************************************************
 *  SBDMA_TX_PROCESS(sc,d)
 *  
 *  Process "completed" transmit buffers on the specified DMA channel.  
 *  This is normally called within the interrupt service routine.
 *  Note that this isn't really ideal for priority channels, since
 *  it processes all of the packets on a given channel before 
 *  returning. 
 *
 *  Input parameters: 
 *      sc - softc structure
 *  	   d - DMA channel context
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbdma_tx_process(struct sbmac_softc *sc,sbmacdma_t *d)
{
	int curidx;
	int hwidx;
	sbdmadscr_t *dsc;
	struct sk_buff *sb;
	unsigned long flags;

	spin_lock_irqsave(&(sc->sbm_lock), flags);
	
	for (;;) {
		/* 
		 * figure out where we are (as an index) and where
		 * the hardware is (also as an index)
		 *
		 * This could be done faster if (for example) the 
		 * descriptor table was page-aligned and contiguous in
		 * both virtual and physical memory -- you could then
		 * just compare the low-order bits of the virtual address
		 * (sbdma_remptr) and the physical address (sbdma_curdscr CSR)
		 */
		
		curidx = d->sbdma_remptr - d->sbdma_dscrtable;
		{
			/* XXX This is gross, ugly, and only here because justin hacked it
			   in to fix a problem without really understanding it. 
			   
			   It seems that, for whatever reason, this routine is invoked immediately upon the enabling of interrupts.
			   So then the Read below returns zero, making hwidx a negative number, and anti-hilarity
			   ensues.
			   
			   I'm guessing there's a proper fix involving clearing out interrupt state from old packets
			   before enabling interrupts, but I'm not sure.  

			   Anyways, this hack seems to work, and is Good Enough for 11 PM.  :)
			   
			   -Justin
			*/
			  
			uint64_t tmp = SBMAC_READCSR(d->sbdma_curdscr);
			if (!tmp) {
				break;
			}
			hwidx = (int) (((tmp & M_DMA_CURDSCR_ADDR) -
					d->sbdma_dscrtable_phys) / sizeof(sbdmadscr_t));
		}
		/*
		 * If they're the same, that means we've processed all
		 * of the descriptors up to (but not including) the one that
		 * the hardware is working on right now.
		 */
		
		if (curidx == hwidx) break;
		
		/*
		 * Otherwise, get the packet's sk_buff ptr back
		 */
		
		dsc = &(d->sbdma_dscrtable[curidx]);
		sb = d->sbdma_ctxtable[curidx];
		d->sbdma_ctxtable[curidx] = NULL;
		
		/*
		 * Stats
		 */
		
		sc->sbm_stats.tx_bytes += sb->len;
		sc->sbm_stats.tx_packets++;
		
		/*
		 * for transmits, we just free buffers.
		 */
		
		dev_kfree_skb_irq(sb);
		
		/* 
		 * .. and advance to the next buffer.
		 */

		d->sbdma_remptr = SBDMA_NEXTBUF(d,sbdma_remptr);
		
	}
	
	/*
	 * Decide if we should wake up the protocol or not.
	 * Other drivers seem to do this when we reach a low
	 * watermark on the transmit queue.
	 */
	
	netif_wake_queue(d->sbdma_eth->sbm_dev);
	
	spin_unlock_irqrestore(&(sc->sbm_lock), flags);
	
}



/**********************************************************************
 *  SBMAC_INITCTX(s)
 *  
 *  Initialize an Ethernet context structure - this is called
 *  once per MAC on the 1250.  Memory is allocated here, so don't
 *  call it again from inside the ioctl routines that bring the
 *  interface up/down
 *  
 *  Input parameters: 
 *  	   s - sbmac context structure
 *  	   
 *  Return value:
 *  	   0
 ********************************************************************* */

static int sbmac_initctx(struct sbmac_softc *s)
{
	
	/* 
	 * figure out the addresses of some ports 
	 */
	
	s->sbm_macenable = PKSEG1(s->sbm_base + R_MAC_ENABLE);
	s->sbm_maccfg    = PKSEG1(s->sbm_base + R_MAC_CFG);
	s->sbm_fifocfg   = PKSEG1(s->sbm_base + R_MAC_THRSH_CFG);
	s->sbm_framecfg  = PKSEG1(s->sbm_base + R_MAC_FRAMECFG);
	s->sbm_rxfilter  = PKSEG1(s->sbm_base + R_MAC_ADFILTER_CFG);
	s->sbm_isr       = PKSEG1(s->sbm_base + R_MAC_STATUS);
	s->sbm_imr       = PKSEG1(s->sbm_base + R_MAC_INT_MASK);
	s->sbm_mdio      = PKSEG1(s->sbm_base + R_MAC_MDIO);

	s->sbm_phys[0]   = 1;
	s->sbm_phys[1]   = 0;

	s->sbm_phy_oldbmsr = 0;
	s->sbm_phy_oldanlpar = 0;
	s->sbm_phy_oldk1stsr = 0;
	s->sbm_phy_oldlinkstat = 0;
	
	/*
	 * Initialize the DMA channels.  Right now, only one per MAC is used
	 * Note: Only do this _once_, as it allocates memory from the kernel!
	 */
	
	sbdma_initctx(&(s->sbm_txdma),s,0,DMA_TX,SBMAC_MAX_TXDESCR);
	sbdma_initctx(&(s->sbm_rxdma),s,0,DMA_RX,SBMAC_MAX_RXDESCR);
	
	/*
	 * initial state is OFF
	 */
	
	s->sbm_state = sbmac_state_off;
	
	/*
	 * Initial speed is (XXX TEMP) 10MBit/s HDX no FC
	 */
	
	s->sbm_speed = sbmac_speed_10;
	s->sbm_duplex = sbmac_duplex_half;
	s->sbm_fc = sbmac_fc_disabled;
	
	return 0;
}


static void sbdma_uninitctx(struct sbmacdma_s *d)
{
	if (d->sbdma_dscrtable) {
		KFREE(d->sbdma_dscrtable);
		d->sbdma_dscrtable = NULL;
	}
	
	if (d->sbdma_ctxtable) {
		KFREE(d->sbdma_ctxtable);
		d->sbdma_ctxtable = NULL;
	}
}


static void sbmac_uninitctx(struct sbmac_softc *sc)
{
	sbdma_uninitctx(&(sc->sbm_txdma));
	sbdma_uninitctx(&(sc->sbm_rxdma));
}


/**********************************************************************
 *  SBMAC_CHANNEL_START(s)
 *  
 *  Start packet processing on this MAC.
 *  
 *  Input parameters: 
 *  	   s - sbmac structure
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_channel_start(struct sbmac_softc *s)
{
	uint64_t reg;
	sbmac_port_t port;
	uint64_t cfg,fifo,framecfg;
	int idx;
	
	/*
	 * Don't do this if running
	 */

	if (s->sbm_state == sbmac_state_on) return;
	
	/*
	 * Bring the controller out of reset, but leave it off.
	 */
	
	SBMAC_WRITECSR(s->sbm_macenable,0);
	
	/*
	 * Ignore all received packets
	 */
	
	SBMAC_WRITECSR(s->sbm_rxfilter,0);
	
	/* 
	 * Calculate values for various control registers.
	 */
	
	cfg = M_MAC_RETRY_EN |
		M_MAC_TX_HOLD_SOP_EN | 
		V_MAC_TX_PAUSE_CNT_16K |
		M_MAC_AP_STAT_EN |
		M_MAC_FAST_SYNC |
		M_MAC_SS_EN |
		0;
	
	fifo = V_MAC_TX_WR_THRSH(4) |	/* Must be '4' or '8' */
		V_MAC_TX_RD_THRSH(4) |
		V_MAC_TX_RL_THRSH(4) |
		V_MAC_RX_PL_THRSH(4) |
		V_MAC_RX_RD_THRSH(4) |	/* Must be '4' */
		V_MAC_RX_PL_THRSH(4) |
		V_MAC_RX_RL_THRSH(8) |
		0;
	
	framecfg = V_MAC_MIN_FRAMESZ_DEFAULT |
		V_MAC_MAX_FRAMESZ_DEFAULT |
		V_MAC_BACKOFF_SEL(1);
	
	
	/*
	 * Clear out the hash address map 
	 */
	
	port = PKSEG1(s->sbm_base + R_MAC_HASH_BASE);
        for (idx = 0; idx < MAC_HASH_COUNT; idx++) {
		SBMAC_WRITECSR(port,0);
		port += sizeof(uint64_t);
	}
	
	/*
	 * Clear out the exact-match table
	 */
	
	port = PKSEG1(s->sbm_base + R_MAC_ADDR_BASE);
	for (idx = 0; idx < MAC_ADDR_COUNT; idx++) {
		SBMAC_WRITECSR(port,0);
		port += sizeof(uint64_t);
	}
	
	/*
	 * Clear out the DMA Channel mapping table registers
	 */
	
	port = PKSEG1(s->sbm_base + R_MAC_CHUP0_BASE);
	for (idx = 0; idx < MAC_CHMAP_COUNT; idx++) {
		SBMAC_WRITECSR(port,0);
		port += sizeof(uint64_t);
	}


	port = PKSEG1(s->sbm_base + R_MAC_CHLO0_BASE);
	for (idx = 0; idx < MAC_CHMAP_COUNT; idx++) {
		SBMAC_WRITECSR(port,0);
		port += sizeof(uint64_t);
	}
	
	/*
	 * Program the hardware address.  It goes into the hardware-address
	 * register as well as the first filter register.
	 */
	
	reg = sbmac_addr2reg(s->sbm_hwaddr);
	
	port = PKSEG1(s->sbm_base + R_MAC_ADDR_BASE);
	SBMAC_WRITECSR(port,reg);
	port = PKSEG1(s->sbm_base + R_MAC_ETHERNET_ADDR);

#ifdef CONFIG_SB1_PASS_1_WORKAROUNDS
	/*
	 * Pass1 SB1250s do not receive packets addressed to the
	 * destination address in the R_MAC_ETHERNET_ADDR register.
	 * Set the value to zero.
	 */
	SBMAC_WRITECSR(port,0);
#else
	SBMAC_WRITECSR(port,reg);
#endif
	
	/*
	 * Set the receive filter for no packets, and write values
	 * to the various config registers
	 */
	
	SBMAC_WRITECSR(s->sbm_rxfilter,0);
	SBMAC_WRITECSR(s->sbm_imr,0);
	SBMAC_WRITECSR(s->sbm_framecfg,framecfg);
	SBMAC_WRITECSR(s->sbm_fifocfg,fifo);
	SBMAC_WRITECSR(s->sbm_maccfg,cfg);
	
	/*
	 * Initialize DMA channels (rings should be ok now)
	 */
	
	sbdma_channel_start(&(s->sbm_rxdma));
	sbdma_channel_start(&(s->sbm_txdma));
	
	/*
	 * Configure the speed, duplex, and flow control
	 */

	sbmac_set_speed(s,s->sbm_speed);
	sbmac_set_duplex(s,s->sbm_duplex,s->sbm_fc);
	
	/*
	 * Fill the receive ring
	 */
	
	sbdma_fillring(&(s->sbm_rxdma));
	
	/* 
	 * Turn on the rest of the bits in the enable register
	 */      
	
	SBMAC_WRITECSR(s->sbm_macenable,
		       M_MAC_RXDMA_EN0 |
		       M_MAC_TXDMA_EN0 |
		       M_MAC_RX_ENABLE |
		       M_MAC_TX_ENABLE);
	
	

	/*
	 * Accept any kind of interrupt on TX and RX DMA channel 0
	 */
	SBMAC_WRITECSR(s->sbm_imr,
		       (M_MAC_INT_CHANNEL << S_MAC_TX_CH0) |
		       (M_MAC_INT_CHANNEL << S_MAC_RX_CH0));
	
	/* 
	 * Enable receiving unicasts and broadcasts 
	 */
	
	SBMAC_WRITECSR(s->sbm_rxfilter,M_MAC_UCAST_EN | M_MAC_BCAST_EN);
	
	/*
	 * we're running now. 
	 */
	
	s->sbm_state = sbmac_state_on;
	
	/* 
	 * Program multicast addresses 
	 */
	
	sbmac_setmulti(s);
	
	/* 
	 * If channel was in promiscuous mode before, turn that on 
	 */
	
	if (s->sbm_devflags & IFF_PROMISC) {
		sbmac_promiscuous_mode(s,1);
	}
	
}


/**********************************************************************
 *  SBMAC_CHANNEL_STOP(s)
 *  
 *  Stop packet processing on this MAC.
 *  
 *  Input parameters: 
 *  	   s - sbmac structure
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_channel_stop(struct sbmac_softc *s)
{
	uint64_t ctl;
	
	/* don't do this if already stopped */
	
	if (s->sbm_state == sbmac_state_off) return;
	
	/* don't accept any packets, disable all interrupts */
	
	SBMAC_WRITECSR(s->sbm_rxfilter,0);
	SBMAC_WRITECSR(s->sbm_imr,0);
	
	/* Turn off ticker */
	
	/* XXX */
	
	/* turn off receiver and transmitter */
	
	ctl = SBMAC_READCSR(s->sbm_macenable);
	ctl &= ~(M_MAC_RXDMA_EN0 | M_MAC_TXDMA_EN0);
	SBMAC_WRITECSR(s->sbm_macenable,ctl);
	
	/* We're stopped now. */
	
	s->sbm_state = sbmac_state_off;
	
	
	/* Empty the receive and transmit rings */
	
	sbdma_emptyring(&(s->sbm_rxdma));
	sbdma_emptyring(&(s->sbm_txdma));
	
}

/**********************************************************************
 *  SBMAC_SET_CHANNEL_STATE(state)
 *  
 *  Set the channel's state ON or OFF
 *  
 *  Input parameters: 
 *  	   state - new state
 *  	   
 *  Return value:
 *  	   old state
 ********************************************************************* */
static sbmac_state_t sbmac_set_channel_state(struct sbmac_softc *sc,
					     sbmac_state_t state)
{
	sbmac_state_t oldstate = sc->sbm_state;
	
	/*
	 * If same as previous state, return
	 */
	
	if (state == oldstate) {
		return oldstate;
	}
	
	/*
	 * If new state is ON, turn channel on 
	 */
	
	if (state == sbmac_state_on) {
		sbmac_channel_start(sc);
	}
	else {
		sbmac_channel_stop(sc);
	}
	
	/*
	 * Return previous state
	 */
	
	return oldstate;
}


/**********************************************************************
 *  SBMAC_PROMISCUOUS_MODE(sc,onoff)
 *  
 *  Turn on or off promiscuous mode
 *  
 *  Input parameters: 
 *  	   sc - softc
 *      onoff - 1 to turn on, 0 to turn off
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_promiscuous_mode(struct sbmac_softc *sc,int onoff)
{
	uint64_t reg;
	
	if (sc->sbm_state != sbmac_state_on) return;
	
	if (onoff) {
		reg = SBMAC_READCSR(sc->sbm_rxfilter);
		reg |= M_MAC_ALLPKT_EN;
		SBMAC_WRITECSR(sc->sbm_rxfilter,reg);
	}	
	else {
		reg = SBMAC_READCSR(sc->sbm_rxfilter);
		reg &= ~M_MAC_ALLPKT_EN;
		SBMAC_WRITECSR(sc->sbm_rxfilter,reg);
	}
}



#if 0
/**********************************************************************
 *  SBMAC_INIT_AND_START(sc)
 *  
 *  Stop the channel and restart it.  This is generally used
 *  when we have to do something to the channel that requires
 *  a swift kick.
 *  
 *  Input parameters: 
 *  	   sc - softc
 ********************************************************************* */

static void sbmac_init_and_start(struct sbmac_softc *sc)
{
	unsigned long flags;
	
	spin_lock_irqsave(&(sc->sbm_lock),flags);
	
	sbmac_set_channel_state(sc,sbmac_state_on);
	
	spin_unlock_irqrestore(&(sc->sbm_lock),flags);
}
#endif


/**********************************************************************
 *  SBMAC_ADDR2REG(ptr)
 *  
 *  Convert six bytes into the 64-bit register value that
 *  we typically write into the SBMAC's address/mcast registers
 *  
 *  Input parameters: 
 *  	   ptr - pointer to 6 bytes
 *  	   
 *  Return value:
 *  	   register value
 ********************************************************************* */

static uint64_t sbmac_addr2reg(unsigned char *ptr)
{
	uint64_t reg = 0;
	
	ptr += 6;
	
	reg |= (uint64_t) *(--ptr); 
	reg <<= 8;
	reg |= (uint64_t) *(--ptr); 
	reg <<= 8;
	reg |= (uint64_t) *(--ptr); 
	reg <<= 8;
	reg |= (uint64_t) *(--ptr); 
	reg <<= 8;
	reg |= (uint64_t) *(--ptr); 
	reg <<= 8;
	reg |= (uint64_t) *(--ptr); 
	
	return reg;
}


/**********************************************************************
 *  SBMAC_SET_SPEED(s,speed)
 *  
 *  Configure LAN speed for the specified MAC.
 *  Warning: must be called when MAC is off!
 *  
 *  Input parameters: 
 *  	   s - sbmac structure
 *  	   speed - speed to set MAC to (see sbmac_speed_t enum)
 *  	   
 *  Return value:
 *  	   1 if successful
 *      0 indicates invalid parameters
 ********************************************************************* */

static int sbmac_set_speed(struct sbmac_softc *s,sbmac_speed_t speed)
{
	uint64_t cfg;
	uint64_t framecfg;

	/*
	 * Save new current values
	 */
	
	s->sbm_speed = speed;
	
	if (s->sbm_state == sbmac_state_on) return 0;	/* save for next restart */

	/*
	 * Read current register values 
	 */
	
	cfg = SBMAC_READCSR(s->sbm_maccfg);
	framecfg = SBMAC_READCSR(s->sbm_framecfg);
	
	/*
	 * Mask out the stuff we want to change
	 */
	
	cfg &= ~(M_MAC_BURST_EN | M_MAC_SPEED_SEL);
	framecfg &= ~(M_MAC_IFG_RX | M_MAC_IFG_TX | M_MAC_IFG_THRSH |
		      M_MAC_SLOT_SIZE);
	
	/*
	 * Now add in the new bits
	 */
	
	switch (speed) {
	case sbmac_speed_10:
		framecfg |= V_MAC_IFG_RX_10 |
			V_MAC_IFG_TX_10 |
			K_MAC_IFG_THRSH_10 |
			V_MAC_SLOT_SIZE_10;
		cfg |= V_MAC_SPEED_SEL_10MBPS;
		break;
		
	case sbmac_speed_100:
		framecfg |= V_MAC_IFG_RX_100 |
			V_MAC_IFG_TX_100 |
			V_MAC_IFG_THRSH_100 |
			V_MAC_SLOT_SIZE_100;
		cfg |= V_MAC_SPEED_SEL_100MBPS ;
		break;
		
	case sbmac_speed_1000:
		framecfg |= V_MAC_IFG_RX_1000 |
			V_MAC_IFG_TX_1000 |
			V_MAC_IFG_THRSH_1000 |
			V_MAC_SLOT_SIZE_1000;
		cfg |= V_MAC_SPEED_SEL_1000MBPS | M_MAC_BURST_EN;
		break;
		
	case sbmac_speed_auto:		/* XXX not implemented */
		/* fall through */
	default:
		return 0;
	}
	
	/*
	 * Send the bits back to the hardware 
	 */
	
	SBMAC_WRITECSR(s->sbm_framecfg,framecfg);
	SBMAC_WRITECSR(s->sbm_maccfg,cfg);
	
	return 1;
	
}

/**********************************************************************
 *  SBMAC_SET_DUPLEX(s,duplex,fc)
 *  
 *  Set Ethernet duplex and flow control options for this MAC
 *  Warning: must be called when MAC is off!
 *  
 *  Input parameters: 
 *  	   s - sbmac structure
 *  	   duplex - duplex setting (see sbmac_duplex_t)
 *  	   fc - flow control setting (see sbmac_fc_t)
 *  	   
 *  Return value:
 *  	   1 if ok
 *  	   0 if an invalid parameter combination was specified
 ********************************************************************* */

static int sbmac_set_duplex(struct sbmac_softc *s,sbmac_duplex_t duplex,sbmac_fc_t fc)
{
	uint64_t cfg;
	
	/*
	 * Save new current values
	 */
	
	s->sbm_duplex = duplex;
	s->sbm_fc = fc;
	
	if (s->sbm_state == sbmac_state_on) return 0;	/* save for next restart */
	
	/*
	 * Read current register values 
	 */
	
	cfg = SBMAC_READCSR(s->sbm_maccfg);
	
	/*
	 * Mask off the stuff we're about to change
	 */
	
	cfg &= ~(M_MAC_FC_SEL | M_MAC_FC_CMD | M_MAC_HDX_EN);
	
	
	switch (duplex) {
	case sbmac_duplex_half:
		switch (fc) {
		case sbmac_fc_disabled:
			cfg |= M_MAC_HDX_EN | V_MAC_FC_CMD_DISABLED;
			break;
			
		case sbmac_fc_collision:
			cfg |= M_MAC_HDX_EN | V_MAC_FC_CMD_ENABLED;
			break;
			
		case sbmac_fc_carrier:
			cfg |= M_MAC_HDX_EN | V_MAC_FC_CMD_ENAB_FALSECARR;
			break;
			
		case sbmac_fc_auto:		/* XXX not implemented */
			/* fall through */					   
		case sbmac_fc_frame:		/* not valid in half duplex */
		default:			/* invalid selection */
			return 0;
		}
		break;
		
	case sbmac_duplex_full:
		switch (fc) {
		case sbmac_fc_disabled:
			cfg |= V_MAC_FC_CMD_DISABLED;
			break;
			
		case sbmac_fc_frame:
			cfg |= V_MAC_FC_CMD_ENABLED;
			break;
			
		case sbmac_fc_collision:	/* not valid in full duplex */
		case sbmac_fc_carrier:		/* not valid in full duplex */
		case sbmac_fc_auto:		/* XXX not implemented */
			/* fall through */					   
		default:
			return 0;
		}
		break;
	case sbmac_duplex_auto:
		/* XXX not implemented */
		break;
	}
	
	/*
	 * Send the bits back to the hardware 
	 */
	
	SBMAC_WRITECSR(s->sbm_maccfg,cfg);
	
	return 1;
}




/**********************************************************************
 *  SBMAC_INTR()
 *  
 *  Interrupt handler for MAC interrupts
 *  
 *  Input parameters: 
 *  	   MAC structure
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */
static void sbmac_intr(int irq,void *dev_instance,struct pt_regs *rgs)
{
	struct net_device *dev = (struct net_device *) dev_instance;
	struct sbmac_softc *sc = (struct sbmac_softc *) (dev->priv);
	uint64_t isr;
	
	for (;;) {
		
		/*
		 * Read the ISR (this clears the bits in the real register)
		 */
		
		isr = SBMAC_READCSR(sc->sbm_isr);
		
		if (isr == 0) break;
		
		/*
		 * Transmits on channel 0
		 */
		
		if (isr & (M_MAC_INT_CHANNEL << S_MAC_TX_CH0)) {
			sbdma_tx_process(sc,&(sc->sbm_txdma));
		}
		
		/*
		 * Receives on channel 0
		 */
		
		if (isr & (M_MAC_INT_CHANNEL << S_MAC_RX_CH0)) {
			sbdma_rx_process(sc,&(sc->sbm_rxdma));
		}
	}
	
}


/**********************************************************************
 *  SBMAC_START_TX(skb,dev)
 *  
 *  Start output on the specified interface.  Basically, we 
 *  queue as many buffers as we can until the ring fills up, or
 *  we run off the end of the queue, whichever comes first.
 *  
 *  Input parameters: 
 *  	   
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */
static int sbmac_start_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct sbmac_softc *sc = (struct sbmac_softc *)dev->priv;
	
	/* lock eth irq */
	spin_lock_irq (&sc->sbm_lock);
	
	/*
	 * Put the buffer on the transmit ring.  If we 
	 * don't have room, stop the queue.
	 */
	
	if (sbdma_add_txbuffer(&(sc->sbm_txdma),skb)) {
		/* XXX save skb that we could not send */
		netif_stop_queue(dev);
	}
	
	dev->trans_start = jiffies;
	
	spin_unlock_irq (&sc->sbm_lock);
	
	return 0;
}

/**********************************************************************
 *  SBMAC_SETMULTI(sc)
 *  
 *  Reprogram the multicast table into the hardware, given
 *  the list of multicasts associated with the interface
 *  structure.
 *  
 *  Input parameters: 
 *  	   sc - softc
 *  	   
 *  Return value:
 *  	   nothing
 ********************************************************************* */

static void sbmac_setmulti(struct sbmac_softc *sc)
{
	uint64_t reg;
	sbmac_port_t port;
	int idx;
	struct dev_mc_list *mclist;
	struct net_device *dev = sc->sbm_dev;
	
	/* 
	 * Clear out entire multicast table.  We do this by nuking
	 * the entire hash table and all the direct matches except
	 * the first one, which is used for our station address 
	 */
	
	for (idx = 1; idx < MAC_ADDR_COUNT; idx++) {
		port = PKSEG1(sc->sbm_base + R_MAC_ADDR_BASE+(idx*sizeof(uint64_t)));
		SBMAC_WRITECSR(port,0);	
	}
	
	for (idx = 0; idx < MAC_HASH_COUNT; idx++) {
		port = PKSEG1(sc->sbm_base + R_MAC_HASH_BASE+(idx*sizeof(uint64_t)));
		SBMAC_WRITECSR(port,0);	
	}
	
	/*
	 * Clear the filter to say we don't want any multicasts.
	 */
	
	reg = SBMAC_READCSR(sc->sbm_rxfilter);
	reg &= ~(M_MAC_MCAST_INV | M_MAC_MCAST_EN);
	SBMAC_WRITECSR(sc->sbm_rxfilter,reg);
	
	if (dev->flags & IFF_ALLMULTI) {
		/* 
		 * Enable ALL multicasts.  Do this by inverting the 
		 * multicast enable bit. 
		 */
		reg = SBMAC_READCSR(sc->sbm_rxfilter);
		reg |= (M_MAC_MCAST_INV | M_MAC_MCAST_EN);
		SBMAC_WRITECSR(sc->sbm_rxfilter,reg);
		return;
	}
	

	/* 
	 * Progam new multicast entries.  For now, only use the
	 * perfect filter.  In the future we'll need to use the
	 * hash filter if the perfect filter overflows
	 */
	
	/* XXX only using perfect filter for now, need to use hash
	 * XXX if the table overflows */
	
	idx = 1;		/* skip station address */
	mclist = dev->mc_list;
	while (mclist && (idx < MAC_ADDR_COUNT)) {
		reg = sbmac_addr2reg(mclist->dmi_addr);
		port = PKSEG1(sc->sbm_base + 
			      R_MAC_ADDR_BASE+(idx*sizeof(uint64_t)));
		SBMAC_WRITECSR(port,reg);
		idx++;
		mclist = mclist->next;
	}
	
	/*	
	 * Enable the "accept multicast bits" if we programmed at least one
	 * multicast. 
	 */
	
	if (idx > 1) {
		reg = SBMAC_READCSR(sc->sbm_rxfilter);
		reg |= M_MAC_MCAST_EN;
		SBMAC_WRITECSR(sc->sbm_rxfilter,reg);
	}
}



#if defined(SBMAC_ETH0_HWADDR) || defined(SBMAC_ETH1_HWADDR) || defined(SBMAC_ETH2_HWADDR)
/**********************************************************************
 *  SBMAC_PARSE_XDIGIT(str)
 *  
 *  Parse a hex digit, returning its value
 *  
 *  Input parameters: 
 *  	   str - character
 *  	   
 *  Return value:
 *  	   hex value, or -1 if invalid
 ********************************************************************* */

static int sbmac_parse_xdigit(char str)
{
	int digit;
	
	if ((str >= '0') && (str <= '9')) digit = str - '0';
	else if ((str >= 'a') && (str <= 'f')) digit = str - 'a' + 10;
	else if ((str >= 'A') && (str <= 'F')) digit = str - 'A' + 10;
	else return -1;
	
	return digit;
}

/**********************************************************************
 *  SBMAC_PARSE_HWADDR(str,hwaddr)
 *  
 *  Convert a string in the form xx:xx:xx:xx:xx:xx into a 6-byte
 *  Ethernet address.
 *  
 *  Input parameters: 
 *  	   str - string
 *  	   hwaddr - pointer to hardware address
 *  	   
 *  Return value:
 *  	   0 if ok, else -1
 ********************************************************************* */

static int sbmac_parse_hwaddr(char *str,u_char *hwaddr)
{
	int digit1,digit2;
	int idx = 6;
	
	while (*str && (idx > 0)) {
		digit1 = sbmac_parse_xdigit(*str);
		if (digit1 < 0) return -1;
		str++;
		if (!*str) return -1;
		
		if ((*str == ':') || (*str == '-')) {
			digit2 = digit1;
			digit1 = 0;
		}
		else {
			digit2 = sbmac_parse_xdigit(*str);
			if (digit2 < 0) return -1;
			str++;
		}
		
		*hwaddr++ = (digit1 << 4) | digit2;
		idx--;
		
		if (*str == '-') str++;
		if (*str == ':') str++;
	}
	return 0;
}
#endif

/**********************************************************************
 *  SBMAC_INIT(dev)
 *  
 *  Attach routine - init hardware and hook ourselves into linux
 *  
 *  Input parameters: 
 *  	   dev - net_device structure
 *  	   
 *  Return value:
 *  	   status
 ********************************************************************* */

static int sbmac_init(struct net_device *dev)
{
	struct sbmac_softc *sc;
	u_char *eaddr;
	uint64_t ea_reg;
	int idx;
	
	sc = (struct sbmac_softc *)dev->priv;
	
	/* Determine controller base address */
	
	sc->sbm_base = (sbmac_port_t) dev->base_addr;
	sc->sbm_dev = dev;
	
	eaddr = sc->sbm_hwaddr;
	
	/* 
	 * Read the ethernet address.  The firwmare left this programmed
	 * for us in the ethernet address register for each mac.
	 */
	
	ea_reg = SBMAC_READCSR(PKSEG1(sc->sbm_base + R_MAC_ETHERNET_ADDR));
	SBMAC_WRITECSR(PKSEG1(sc->sbm_base + R_MAC_ETHERNET_ADDR), 0);
	for (idx = 0; idx < 6; idx++) {
		eaddr[idx] = (uint8_t) (ea_reg & 0xFF);
		ea_reg >>= 8;
	}
	
	
	for (idx = 0; idx < 6; idx++) {
		dev->dev_addr[idx] = eaddr[idx];
	}
	
	
	/*
	 * Init packet size 
	 */
	
	sc->sbm_buffersize = ENET_PACKET_SIZE + CACHELINESIZE*2 + ETHER_ALIGN;
	
	/* 
	 * Initialize context (get pointers to registers and stuff), then
	 * allocate the memory for the descriptor tables.
	 */
	
	sbmac_initctx(sc);
	
	
	/*
	 * Display Ethernet address (this is called during the config process
	 * so we need to finish off the config message that was being displayed)
	 */
	printk(KERN_INFO
	       "%s: SB1250 Ethernet at 0x%08lX, address: %02X-%02X-%02X-%02X-%02X-%02X\n", 
	       dev->name,
	       (unsigned long) sc->sbm_base,
	       eaddr[0],eaddr[1],eaddr[2],eaddr[3],eaddr[4],eaddr[5]);
	
	/*
	 * Set up Linux device callins
	 */
	
	spin_lock_init(&(sc->sbm_lock));
	
	ether_setup(dev);
	dev->open               = sbmac_open;
	dev->hard_start_xmit    = sbmac_start_tx;
	dev->stop               = sbmac_close;
	dev->get_stats          = sbmac_get_stats;
	dev->set_multicast_list = sbmac_set_rx_mode;
	dev->do_ioctl           = sbmac_mii_ioctl;
	dev->tx_timeout         = sbmac_tx_timeout;
	dev->watchdog_timeo     = TX_TIMEOUT;
	
	return 0;
	
}


static int sbmac_open(struct net_device *dev)
{
	struct sbmac_softc *sc = (struct sbmac_softc *)dev->priv;
	
	MOD_INC_USE_COUNT;

	if (debug > 1) {
		printk(KERN_DEBUG "%s: sbmac_open() irq %d.\n", dev->name, dev->irq);
	}
	
	/* 
	 * map/route interrupt 
	 */
	
	if (request_irq(dev->irq, &sbmac_intr, SA_SHIRQ, dev->name, dev)) {
		MOD_DEC_USE_COUNT;
		return -EBUSY;
	}
	
	/*
	 * Configure default speed 
	 */

	sbmac_mii_poll(sc,1);
	
	/*
	 * Turn on the channel
	 */

	sbmac_set_channel_state(sc,sbmac_state_on);
	
	/*
	 * XXX Station address is in dev->dev_addr
	 */
	
	if (dev->if_port == 0)
		dev->if_port = 0; 
	
	netif_start_queue(dev);
	
	sbmac_set_rx_mode(dev);
	
	/* Set the timer to check for link beat. */
	init_timer(&sc->sbm_timer);
	sc->sbm_timer.expires = jiffies + 2;
	sc->sbm_timer.data = (unsigned long)dev;
	sc->sbm_timer.function = &sbmac_timer;
	add_timer(&sc->sbm_timer);
	
	return 0;
}



static int sbmac_mii_poll(struct sbmac_softc *s,int noisy)
{
    int bmsr,bmcr,k1stsr,anlpar;
    int chg;
    char buffer[100];
    char *p = buffer;

    /* Read the mode status and mode control registers. */
    bmsr = sbmac_mii_read(s,s->sbm_phys[0],MII_BMSR);
    bmcr = sbmac_mii_read(s,s->sbm_phys[0],MII_BMCR);

    /* get the link partner status */
    anlpar = sbmac_mii_read(s,s->sbm_phys[0],MII_ANLPAR);

    /* if supported, read the 1000baseT register */
    if (bmsr & BMSR_1000BT_XSR) {
	k1stsr = sbmac_mii_read(s,s->sbm_phys[0],MII_K1STSR);
	}
    else {
	k1stsr = 0;
	}

    chg = 0;

    if ((bmsr & BMSR_LINKSTAT) == 0) {
	/*
	 * If link status is down, clear out old info so that when
	 * it comes back up it will force us to reconfigure speed
	 */
	s->sbm_phy_oldbmsr = 0;
	s->sbm_phy_oldanlpar = 0;
	s->sbm_phy_oldk1stsr = 0;
	return 0;
	}

    if ((s->sbm_phy_oldbmsr != bmsr) ||
	(s->sbm_phy_oldanlpar != anlpar) ||
	(s->sbm_phy_oldk1stsr != k1stsr)) {
	if (debug > 1) {
	    printk(KERN_DEBUG "%s: bmsr:%x/%x anlpar:%x/%x  k1stsr:%x/%x\n",
	       s->sbm_dev->name,
	       s->sbm_phy_oldbmsr,bmsr,
	       s->sbm_phy_oldanlpar,anlpar,
	       s->sbm_phy_oldk1stsr,k1stsr);
	    }
	s->sbm_phy_oldbmsr = bmsr;
	s->sbm_phy_oldanlpar = anlpar;
	s->sbm_phy_oldk1stsr = k1stsr;
	chg = 1;
	}

    if (chg == 0) return 0;

    p += sprintf(p,"Link speed: ");

    if (k1stsr & K1STSR_LP1KFD) {
	s->sbm_speed = sbmac_speed_1000;
	s->sbm_duplex = sbmac_duplex_full;
	s->sbm_fc = sbmac_fc_frame;
	p += sprintf(p,"1000BaseT FDX");
	}
    else if (k1stsr & K1STSR_LP1KHD) {
	s->sbm_speed = sbmac_speed_1000;
	s->sbm_duplex = sbmac_duplex_half;
	s->sbm_fc = sbmac_fc_disabled;
	p += sprintf(p,"1000BaseT HDX");
	}
    else if (anlpar & ANLPAR_TXFD) {
	s->sbm_speed = sbmac_speed_100;
	s->sbm_duplex = sbmac_duplex_full;
	s->sbm_fc = (anlpar & ANLPAR_PAUSE) ? sbmac_fc_frame : sbmac_fc_disabled;
	p += sprintf(p,"100BaseT FDX");
	}
    else if (anlpar & ANLPAR_TXHD) {
	s->sbm_speed = sbmac_speed_100;
	s->sbm_duplex = sbmac_duplex_half;
	s->sbm_fc = sbmac_fc_disabled;
	p += sprintf(p,"100BaseT HDX");
	}
    else if (anlpar & ANLPAR_10FD) {
	s->sbm_speed = sbmac_speed_10;
	s->sbm_duplex = sbmac_duplex_full;
	s->sbm_fc = sbmac_fc_frame;
	p += sprintf(p,"10BaseT FDX");
	}
    else if (anlpar & ANLPAR_10HD) {
	s->sbm_speed = sbmac_speed_10;
	s->sbm_duplex = sbmac_duplex_half;
	s->sbm_fc = sbmac_fc_collision;
	p += sprintf(p,"10BaseT HDX");
	}
    else {
	p += sprintf(p,"Unknown");
	}

    if (noisy) {
	    printk(KERN_INFO "%s: %s\n",s->sbm_dev->name,buffer);
	    }

    return 1;
}




static void sbmac_timer(unsigned long data)
{
	struct net_device *dev = (struct net_device *)data;
	struct sbmac_softc *sc = (struct sbmac_softc *)dev->priv;
	int next_tick = HZ;
	int mii_status;

	spin_lock_irq (&sc->sbm_lock);
	
	/* make IFF_RUNNING follow the MII status bit "Link established" */
	mii_status = sbmac_mii_read(sc, sc->sbm_phys[0], MII_BMSR);
	
	if ( (mii_status & BMSR_LINKSTAT) != (sc->sbm_phy_oldlinkstat) ) {
    	        sc->sbm_phy_oldlinkstat = mii_status & BMSR_LINKSTAT;
		if (mii_status & BMSR_LINKSTAT) {
			netif_carrier_on(dev);
		}
		else {
			netif_carrier_off(dev);	
		}
	}
	
	/*
	 * Poll the PHY to see what speed we should be running at
	 */

	if (sbmac_mii_poll(sc,1)) {
	    if (sc->sbm_state != sbmac_state_off) {
		/*
		 * something changed, restart the channel
		 */
		if (debug > 1) {
		    printk("%s: restarting channel because speed changed\n",
			   sc->sbm_dev->name);
		    }
		sbmac_channel_stop(sc);
		sbmac_channel_start(sc);
		}
	    }
	
	spin_unlock_irq (&sc->sbm_lock);
	
	sc->sbm_timer.expires = jiffies + next_tick;
	add_timer(&sc->sbm_timer);
}


static void sbmac_tx_timeout (struct net_device *dev)
{
	struct sbmac_softc *sc = (struct sbmac_softc *) dev->priv;
	
	spin_lock_irq (&sc->sbm_lock);
	
	
	dev->trans_start = jiffies;
	sc->sbm_stats.tx_errors++;
	
	spin_unlock_irq (&sc->sbm_lock);

	printk (KERN_WARNING "%s: Transmit timed out\n",dev->name);
}




static struct net_device_stats *sbmac_get_stats(struct net_device *dev)
{
	struct sbmac_softc *sc = (struct sbmac_softc *)dev->priv;
	unsigned long flags;
	
	spin_lock_irqsave(&sc->sbm_lock, flags);
	
	/* XXX update other stats here */
	
	spin_unlock_irqrestore(&sc->sbm_lock, flags);
	
	return &sc->sbm_stats;
}



static void sbmac_set_rx_mode(struct net_device *dev)
{
	unsigned long flags;
	int msg_flag = 0;
	struct sbmac_softc *sc = (struct sbmac_softc *)dev->priv;

	spin_lock_irqsave(&sc->sbm_lock, flags);
	if ((dev->flags ^ sc->sbm_devflags) & IFF_PROMISC) {
		/*
		 * Promiscuous changed.
		 */
		
		if (dev->flags & IFF_PROMISC) {	
			/* Unconditionally log net taps. */
			msg_flag = 1;
			sbmac_promiscuous_mode(sc,1);
		}
		else {
			msg_flag = 2;
			sbmac_promiscuous_mode(sc,0);
		}
	}
	spin_unlock_irqrestore(&sc->sbm_lock, flags);
	
	if (msg_flag) {
		printk(KERN_NOTICE "%s: Promiscuous mode %sabled.\n", dev->name,(msg_flag==1)?"en":"dis");
	}
	
	/*
	 * Program the multicasts.  Do this every time.
	 */
	
	sbmac_setmulti(sc);
	
}

static int sbmac_mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct sbmac_softc *sc = (struct sbmac_softc *)dev->priv;
	u16 *data = (u16 *)&rq->ifr_data;
	unsigned long flags;
	int retval;
	
	spin_lock_irqsave(&sc->sbm_lock, flags);
	retval = 0;
	
	switch(cmd) {
	case SIOCDEVPRIVATE:		/* Get the address of the PHY in use. */
		data[0] = sc->sbm_phys[0] & 0x1f;
		/* Fall Through */
	case SIOCDEVPRIVATE+1:		/* Read the specified MII register. */
		data[3] = sbmac_mii_read(sc, data[0] & 0x1f, data[1] & 0x1f);
		break;
	case SIOCDEVPRIVATE+2:		/* Write the specified MII register */
		if (!capable(CAP_NET_ADMIN)) {
			retval = -EPERM;
			break;
		}
		if (debug > 1) {
		    printk(KERN_DEBUG "%s: sbmac_mii_ioctl: write %02X %02X %02X\n",dev->name,
		       data[0],data[1],data[2]);
		    }
		sbmac_mii_write(sc, data[0] & 0x1f, data[1] & 0x1f, data[2]);
		break;
	default:
		retval = -EOPNOTSUPP;
	}
	
	spin_unlock_irqrestore(&sc->sbm_lock, flags);
	return retval;
}

static int sbmac_close(struct net_device *dev)
{
	struct sbmac_softc *sc = (struct sbmac_softc *)dev->priv;
	unsigned long flags;
	
	sbmac_set_channel_state(sc,sbmac_state_off);
	
	del_timer_sync(&sc->sbm_timer);
	
	spin_lock_irqsave(&sc->sbm_lock, flags);
	
	netif_stop_queue(dev);
	
	if (debug > 1) {
		printk(KERN_DEBUG "%s: Shutting down ethercard\n",dev->name);
	}
	
	spin_unlock_irqrestore(&sc->sbm_lock, flags);
	
	/* Make sure there is no irq-handler running on a different CPU. */
	synchronize_irq(dev->irq);
	
	free_irq(dev->irq, dev);
	
	sbdma_emptyring(&(sc->sbm_txdma));
	sbdma_emptyring(&(sc->sbm_rxdma));
	
	MOD_DEC_USE_COUNT;
	
	return 0;
}



#if defined(SBMAC_ETH0_HWADDR) || defined(SBMAC_ETH1_HWADDR) || defined(SBMAC_ETH2_HWADDR)
static void
sbmac_setup_hwaddr(int chan,char *addr)
{
	uint8_t eaddr[6];
	uint64_t val;
	sbmac_port_t port;

	port = A_MAC_CHANNEL_BASE(chan);
	sbmac_parse_hwaddr(addr,eaddr);
	val = sbmac_addr2reg(eaddr);
	SBMAC_WRITECSR(PKSEG1(port+R_MAC_ETHERNET_ADDR),val);
	val = SBMAC_READCSR(PKSEG1(port+R_MAC_ETHERNET_ADDR));
}
#endif

static struct net_device *dev_sbmac[MAX_UNITS] = {0,0,0};

static int __init
sbmac_init_module(void)
{
	int idx;
	int macidx = 0;
	struct net_device *dev;
	sbmac_port_t port;
	
	/*
	 * For bringup when not using the firmware, we can pre-fill
	 * the MAC addresses using the environment variables
	 * specified in this file (or maybe from the config file?)
	 */
#ifdef SBMAC_ETH0_HWADDR
	sbmac_setup_hwaddr(0,SBMAC_ETH0_HWADDR);
#endif
#ifdef SBMAC_ETH1_HWADDR
	sbmac_setup_hwaddr(1,SBMAC_ETH1_HWADDR);
#endif
#ifdef SBMAC_ETH2_HWADDR
	sbmac_setup_hwaddr(2,SBMAC_ETH2_HWADDR);
#endif

	/*
	 * Walk through the Ethernet controllers and find
	 * those who have their MAC addresses set.
	 */
	
	for (idx = 0; idx < MAX_UNITS; idx++) {

	        /*
	         * This is the base address of the MAC.
		 */

	        port = A_MAC_CHANNEL_BASE(idx);

		/*	
		 * The R_MAC_ETHERNET_ADDR register will be set to some nonzero
		 * value for us by the firmware if we're going to use this MAC.
		 * If we find a zero, skip this MAC.
		 */

		if (SBMAC_READCSR(PKSEG1(port+R_MAC_ETHERNET_ADDR)) == 0) {
		    continue;
		    }

		/*
		 * Okay, cool.  Initialize this MAC.
		 */

		dev = init_etherdev(NULL,sizeof(struct sbmac_softc));
		if (!dev) break;			/* problems, get out now. */
		dev->irq = K_INT_MAC_0 + idx;
		dev->base_addr = port;
		dev->mem_end = 0;
		/*dev->init = sbmac_init;*/
		sbmac_init(dev);

		dev_sbmac[macidx] = dev;
		macidx++;

	}

	/*
	 * Should we care, 'macidx' is the total number of enabled MACs.
	 */
	
	return 0;
}


static void __exit
sbmac_cleanup_module(void)
{
	int idx;
	struct net_device *dev;
	for (idx = 0; idx < MAX_UNITS; idx++) {
		dev = dev_sbmac[idx];
		if (dev == NULL) continue;
		if (dev->priv != NULL) {
			struct sbmac_softc *sc = (struct sbmac_softc *) dev->priv;
			
			unregister_netdev(dev);
			
			sbmac_uninitctx(sc);
			
			KFREE(sc);
		}
		KFREE(dev);
		dev_sbmac[idx] = NULL;
	}
}

module_init(sbmac_init_module);
module_exit(sbmac_cleanup_module);
