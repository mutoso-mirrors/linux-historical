/* znet.c: An Zenith Z-Note ethernet driver for linux. */

static char *version = "znet.c:v0.04 5/10/94 becker@cesdis.gsfc.nasa.gov\n";

/*
	Written by Donald Becker.

	The author may be reached as becker@cesdis.gsfc.nasa.gov.
	This driver is based on the Linux skeleton driver.  The copyright of the
	skeleton driver is held by the United States Government, as represented
	by DIRNSA, and it is released under the GPL.

	Thanks to Mike Hollick for alpha testing and suggestions.

  References:
	   The Crynwr packet driver.

	  "82593 CSMA/CD Core LAN Controller" Intel datasheet, 1992
	  Intel Microcommunications Databook, Vol. 1, 1990.
    As usual with Intel, the documentation is incomplete and inaccurate.
	I had to read the Crynwr packet driver to figure out how to actually
	use the i82593, and guess at what register bits matched the loosely
	related i82586.

					Theory of Operation

	The i82593 used in the Zenith Z-Note series operates using two(!) slave
	DMA	channels, one interrupt, and one 8-bit I/O port.

	While there	several ways to configure '593 DMA system, I chose the one
	that seemed commesurate with the highest system performance in the face
	of moderate interrupt latency: Both DMA channels are configued as
	recirculating ring buffers, with one channel (#0) dedicated to Rx and
	the other channel (#1) to Tx and configuration.  (Note that this is
	different than the Crynwr driver, where the Tx DMA channel is initialized
	before each operation.  That approach simplifies operation and Tx error
	recovery, but requires additional I/O in normal operation and precludes
	transmit buffer	chaining.)

	Both rings are set to 8192 bytes using {TX,RX}_RING_SIZE.  This provides
	a reasonable ring size for Rx, while simplifying DMA buffer allocation --
	DMA buffers must not cross a 128K boundary.  (In truth the size selection
	was influenced by my lack of '593 documentation.  I thus was constrained
	to use the Crynwr '593 initialization table, which sets the Rx ring size
	to 8K.)

	Despite my usual low opinion about Intel-designed parts, I must admit
	that the bulk data handling of the i82593 is a good design for
	an integrated system, like a laptop, where using two slave DMA channels
	doesn't pose a problem.  I still take issue with using only a single I/O
	port.  In the same controlled environment there are essentially no
	limitations on I/O space, and using multiple locations would eliminate
	the	need for multiple operations when looking at status registers,
	setting the Rx ring boundary, or switching to promiscuous mode.

	I also question Zenith's selection of the '593: one of the advertised
	advantages of earlier Intel parts was that if you figured out the magic
	initialization incantation you could use the same part on many different
	network types.  Zenith's use of the "FriendlyNet" (sic) connector rather
	than an	on-board transceiver leads me to believe that they were planning
	to take advantage of this.  But, uhmmm, the '593 omits all but ethernet
	functionality from the serial subsystem.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_arp.h>

#ifndef HAVE_AUTOIRQ
/* From auto_irq.c, in ioport.h for later versions. */
extern void autoirq_setup(int waittime);
extern int autoirq_report(int waittime);
/* The map from IRQ number (as passed to the interrupt handler) to
   'struct device'. */
extern struct device *irq2dev_map[16];
#endif

#ifndef HAVE_ALLOC_SKB
#define alloc_skb(size, priority) (struct sk_buff *) kmalloc(size,priority)
#define kfree_skbmem(addr, size) kfree_s(addr,size);
#endif

#ifndef ZNET_DEBUG
#define ZNET_DEBUG 3
#endif
static unsigned int znet_debug = ZNET_DEBUG;

/* The DMA modes we need aren't in <dma.h>. */
#define DMA_RX_MODE		0x14	/* Auto init, I/O to mem, ++, demand. */
#define DMA_TX_MODE		0x18	/* Auto init, Mem to I/O, ++, demand. */
#define dma_page_eq(ptr1, ptr2) ((long)(ptr1)>>17 == (long)(ptr2)>>17)
#define DMA_BUF_SIZE 8192
#define RX_BUF_SIZE 8192
#define TX_BUF_SIZE 8192

/* Commands to the i82593 channel 0. */
#define CMD0_CHNL_0			0x00
#define CMD0_CHNL_1			0x10		/* Switch to channel 1. */
#define CMD0_NOP (CMD0_CHNL_0)
#define CMD0_PORT_1	CMD0_CHNL_1
#define CMD1_PORT_0	1
#define CMD0_IA_SETUP		1
#define CMD0_CONFIGURE		2
#define CMD0_MULTICAST_LIST 3
#define CMD0_TRANSMIT		4
#define CMD0_DUMP			6
#define CMD0_DIAGNOSE		7
#define CMD0_Rx_ENABLE		8
#define CMD0_Rx_DISABLE		10
#define CMD0_Rx_STOP		11
#define CMD0_RETRANSMIT		12
#define CMD0_ABORT			13
#define CMD0_RESET			14

#define CMD0_ACK 0x80

#define CMD0_STAT0 (0 << 5)
#define CMD0_STAT1 (1 << 5)
#define CMD0_STAT2 (2 << 5)
#define CMD0_STAT3 (3 << 5)

#define net_local znet_private
struct znet_private {
	int rx_dma, tx_dma;
	struct enet_statistics stats;
	/* The starting, current, and end pointers for the packet buffers. */
	ushort *rx_start, *rx_cur, *rx_end;
	ushort *tx_start, *tx_cur, *tx_end;
	ushort tx_buf_len;			/* Tx buffer lenght, in words. */
};

/* Only one can be built-in;-> */
static struct znet_private zn;
static ushort dma_buffer1[DMA_BUF_SIZE/2];
static ushort dma_buffer2[DMA_BUF_SIZE/2];
static ushort dma_buffer3[DMA_BUF_SIZE/2 + 8];

/* The configuration block.  What an undocumented nightmare.  The first
   set of values are those suggested (without explaination) for ethernet
   in the Intel 82586 databook.	 The rest appear to be completely undocumented,
   except for cryptic notes in the Crynwr packet driver.  This driver uses
   the Crynwr values verbatim. */

static unsigned char i593_init[] = {
  0xAA,					/* 0: 16-byte input & 80-byte output FIFO. */
						/*	  threshhold, 96-byte FIFO, 82593 mode. */
  0x88,					/* 1: Continuous w/interrupts, 128-clock DMA.*/
  0x2E,					/* 2: 8-byte preamble, NO address insertion, */
						/*	  6-byte Ethernet address, loopback off.*/
  0x00,					/* 3: Default priorities & backoff methods. */
  0x60,					/* 4: 96-bit interframe spacing. */
  0x00,					/* 5: 512-bit slot time (low-order). */
  0xF2,					/* 6: Slot time (high-order), 15 COLL retries. */
  0x00,					/* 7: Promisc-off, broadcast-on, default CRC. */
  0x00,					/* 8: Default carrier-sense, collision-detect. */
  0x40,					/* 9: 64-byte minimum frame length. */
  0x5F,					/* A: Type/length checks OFF, no CRC input,
						   "jabber" termination, etc. */
  0x00,					/* B: Full-duplex disabled. */
  0x3F,					/* C: Default multicast addresses & backoff. */
  0x07,					/* D: Default IFS retriggering. */
  0x31,					/* E: Internal retransmit, drop "runt" packets,
						   synchr. DRQ deassertion, 6 status bytes. */
  0x22,					/* F: Receive ring-buffer size (8K), 
						   receive-stop register enable. */
};

struct netidblk {
	char magic[8];		/* The magic number (string) "NETIDBLK" */
	unsigned char netid[8]; /* The physical station address */
	char nettype, globalopt;
	char vendor[8];		/* The machine vendor and product name. */
	char product[8];
	char irq1, irq2;		/* Interrupts, only one is currently used.	*/
	char dma1, dma2;
	short dma_mem_misc[8];		/* DMA buffer locations (unused in Linux). */
	short iobase1, iosize1;
	short iobase2, iosize2;		/* Second iobase unused. */
	char driver_options;			/* Misc. bits */
	char pad;
};

int znet_probe(struct device *dev);
static int	znet_open(struct device *dev);
static int	znet_send_packet(struct sk_buff *skb, struct device *dev);
static void	znet_interrupt(int reg_ptr);
static void	znet_rx(struct device *dev);
static int	znet_close(struct device *dev);
static struct enet_statistics *net_get_stats(struct device *dev);
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs);
static void hardware_init(struct device *dev);
static int do_command(short ioaddr, int command, int length, ushort *buffer);
static int	wait_for_done(short ioaddr);
static void update_stop_hit(short ioaddr, unsigned short rx_stop_offset);

#ifdef notdef
static struct sigaction znet_sigaction = { &znet_interrupt, 0, 0, NULL, };
#endif


/* The Z-Note probe is pretty easy.  The NETIDBLK exists in the safe-to-probe
   BIOS area.  We just scan for the signature, and pull the vital parameters
   out of the structure. */

int znet_probe(struct device *dev)
{
	int i;
	struct netidblk *netinfo;
	char *p;

	/* This code scans the region 0xf0000 to 0xfffff for a "NETIDBLK". */
	for(p = (char *)0xf0000; p < (char *)0x100000; p++)
		if (*p == 'N'  &&  strncmp(p, "NETIDBLK", 8) == 0)
			break;

	if (p >= (char *)0x100000) {
		if (znet_debug > 1)
			printk("No Z-Note ethernet adaptor found.\n");
		return ENODEV;
	}
	netinfo = (struct netidblk *)p;
	dev->base_addr = netinfo->iobase1;
	dev->irq = netinfo->irq1;

	printk("%s: ZNET at %#3x,", dev->name, dev->base_addr);

	/* The station address is in the "netidblk" at 0x0f0000. */
	for (i = 0; i < 6; i++)
		printk(" %2.2x", dev->dev_addr[i] = netinfo->netid[i]);

	printk(", using IRQ %d DMA %d and %d.\n", dev->irq, netinfo->dma1,
		netinfo->dma2);

	if (znet_debug > 1) {
		printk("%s: vendor '%16.16s' IRQ1 %d IRQ2 %d DMA1 %d DMA2 %d.\n",
			   dev->name, netinfo->vendor,
			   netinfo->irq1, netinfo->irq2,
			   netinfo->dma1, netinfo->dma2);
		printk("%s: iobase1 %#x size %d iobase2 %#x size %d net type %2.2x.\n",
			   dev->name, netinfo->iobase1, netinfo->iosize1,
			   netinfo->iobase2, netinfo->iosize2, netinfo->nettype);
	}

	if (znet_debug > 0)
		printk(version);

	dev->priv = (void *) &zn;
	zn.rx_dma = netinfo->dma1;
	zn.tx_dma = netinfo->dma2;

	/* These should never fail.  You can't add devices to a sealed box! */
	if (request_irq(dev->irq, &znet_interrupt)
		|| request_dma(zn.rx_dma)
		|| request_dma(zn.tx_dma)) {
		printk("Not opened -- resource busy?!?\n");
		return EBUSY;
	}
	irq2dev_map[dev->irq] = dev;

	/* Allocate buffer memory.	We can cross a 128K boundary, so we
	   must be careful about the allocation.  It's easiest to waste 8K. */
	if (dma_page_eq(dma_buffer1, &dma_buffer1[RX_BUF_SIZE/2-1]))
	  zn.rx_start = dma_buffer1;
	else 
	  zn.rx_start = dma_buffer2;

	if (dma_page_eq(dma_buffer3, &dma_buffer3[RX_BUF_SIZE/2-1]))
	  zn.tx_start = dma_buffer3;
	else
	  zn.tx_start = dma_buffer2;
	zn.rx_end = zn.rx_start + RX_BUF_SIZE/2;
	zn.tx_buf_len = TX_BUF_SIZE/2;
	zn.tx_end = zn.tx_start + zn.tx_buf_len;

	/* The ZNET-specific entries in the device structure. */
	dev->open = &znet_open;
	dev->hard_start_xmit = &znet_send_packet;
	dev->stop = &znet_close;
	dev->get_stats	= net_get_stats;
#ifdef HAVE_MULTICAST
	dev->set_multicast_list = &set_multicast_list;
#endif

	/* Fill in the generic field of the device structure. */
	for (i = 0; i < DEV_NUMBUFFS; i++)
		dev->buffs[i] = NULL;

	dev->hard_header	= eth_header;
	dev->add_arp		= eth_add_arp;
	dev->queue_xmit		= dev_queue_xmit;
	dev->rebuild_header	= eth_rebuild_header;
	dev->type_trans		= eth_type_trans;

	dev->type			= ARPHRD_ETHER;
	dev->hard_header_len = ETH_HLEN;
	dev->mtu			= 1500; /* eth_mtu */
	dev->addr_len		= ETH_ALEN;
	for (i = 0; i < ETH_ALEN; i++) {
		dev->broadcast[i]=0xff;
	}

	/* New-style flags. */
	dev->flags			= IFF_BROADCAST;
	dev->family			= AF_INET;
	dev->pa_addr		= 0;
	dev->pa_brdaddr		= 0;
	dev->pa_mask		= 0;
	dev->pa_alen		= sizeof(unsigned long);

	return 0;
}


static int znet_open(struct device *dev)
{
	int ioaddr = dev->base_addr;

	if (znet_debug > 2)
		printk("%s: znet_open() called.\n", dev->name);

	/* Turn on the 82501 SIA, using zenith-specific magic. */
	outb(0x10, 0xe6);					/* Select LAN control register */
	outb(inb(0xe7) | 0x84, 0xe7);		/* Turn on LAN power (bit 2). */
	/* According to the Crynwr driver we should wait 50 msec. for the
	   LAN clock to stabilize.  My experiments indicates that the '593 can
	   be initialized immediately.  The delay is probably needed for the
	   DC-to-DC converter to come up to full voltage, and for the oscillator
	   to be spot-on at 20Mhz before transmitting.
	   Until this proves to be a problem we rely on the higher layers for the
	   delay and save allocating a timer entry. */

	/* This follows the packet driver's lead, and checks for success. */
	if (inb(ioaddr) != 0x10 && inb(ioaddr) != 0x00)
		printk("%s: Problem turning on the transceiver power.\n", dev->name);

	dev->tbusy = 0;
	dev->interrupt = 0;
	hardware_init(dev);
	dev->start = 1;

	return 0;
}

static int znet_send_packet(struct sk_buff *skb, struct device *dev)
{
	int ioaddr = dev->base_addr;

	if (znet_debug > 4)
		printk("%s: ZNet_send_packet(%d).\n", dev->name, dev->tbusy);

	/* Transmitter timeout, could be a serious problems. */
	if (dev->tbusy) {
		ushort event, tx_status, rx_offset, state;
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 10)
			return 1;
		outb(CMD0_STAT0, ioaddr); event = inb(ioaddr);
		outb(CMD0_STAT1, ioaddr); tx_status = inw(ioaddr);
		outb(CMD0_STAT2, ioaddr); rx_offset = inw(ioaddr);
		outb(CMD0_STAT3, ioaddr); state = inb(ioaddr);
		printk("%s: transmit timed out, status %02x %04x %04x %02x,"
			   " resetting.\n", dev->name, event, tx_status, rx_offset, state);
		if (tx_status == 0x0400)
		  printk("%s: Tx carrier error, check transceiver cable.\n",
				 dev->name);
		outb(CMD0_RESET, ioaddr);
		hardware_init(dev);
	}

	if (skb == NULL) {
		dev_tint(dev);
		return 0;
	}

	/* Fill in the ethernet header. */
	if (!skb->arp  &&  dev->rebuild_header(skb+1, dev)) {
		skb->dev = dev;
		arp_queue (skb);
		return 0;
	}

	/* Check that the part hasn't reset itself, probably from suspend. */
	outb(CMD0_STAT0, ioaddr);
	if (inw(ioaddr) == 0x0010
		&& inw(ioaddr) == 0x0000
		&& inw(ioaddr) == 0x0010)
	  hardware_init(dev);

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
	if (set_bit(0, (void*)&dev->tbusy) != 0)
		printk("%s: Transmitter access conflict.\n", dev->name);
	else {
		short length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
		unsigned char *buf = (void *)(skb+1);
		ushort *tx_link = zn.tx_cur - 1;
		ushort rnd_len = (length + 1)>>1;

		{
			short dma_port = ((zn.tx_dma&3)<<2) + IO_DMA2_BASE;
			unsigned addr = inb(dma_port);
			addr |= inb(dma_port) << 8;
			addr <<= 1;
			if (((int)zn.tx_cur & 0x1ffff) != addr)
			  printk("Address mismatch at Tx: %#x vs %#x.\n",
					 (int)zn.tx_cur & 0xffff, addr);
			zn.tx_cur = (ushort *)(((int)zn.tx_cur & 0xfe0000) | addr);
		}

		if (zn.tx_cur >= zn.tx_end)
		  zn.tx_cur = zn.tx_start;
		*zn.tx_cur++ = length;
		if (zn.tx_cur + rnd_len + 1 > zn.tx_end) {
			int semi_cnt = (zn.tx_end - zn.tx_cur)<<1; /* Cvrt to byte cnt. */
			memcpy(zn.tx_cur, buf, semi_cnt);
			rnd_len -= semi_cnt>>1;
			memcpy(zn.tx_start, buf + semi_cnt, length - semi_cnt);
			zn.tx_cur = zn.tx_start + rnd_len;
		} else {
			memcpy(zn.tx_cur, buf, skb->len);
			zn.tx_cur += rnd_len;
		}
		*zn.tx_cur++ = 0;
		cli(); {
			*tx_link = CMD0_TRANSMIT + CMD0_CHNL_1;
			/* Is this always safe to do? */
			outb(CMD0_TRANSMIT + CMD0_CHNL_1,ioaddr);
		} sti();

		dev->trans_start = jiffies;
		if (znet_debug > 4)
		  printk("%s: Transmitter queued, length %d.\n", dev->name, length);
	}
	dev_kfree_skb(skb, FREE_WRITE); 
	return 0;
}

/* The ZNET interrupt handler. */
static void	znet_interrupt(int reg_ptr)
{
	int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
	struct device *dev = irq2dev_map[irq];
	int ioaddr;
	int boguscnt = 20;

	if (dev == NULL) {
		printk ("znet_interrupt(): IRQ %d for unknown device.\n", irq);
		return;
	}

	dev->interrupt = 1;
	ioaddr = dev->base_addr;

	outb(CMD0_STAT0, ioaddr);
	do {
		ushort status = inb(ioaddr);
		if (znet_debug > 5) {
			ushort result, rx_ptr, running;
			outb(CMD0_STAT1, ioaddr);
			result = inw(ioaddr);
			outb(CMD0_STAT2, ioaddr);
			rx_ptr = inw(ioaddr);
			outb(CMD0_STAT3, ioaddr);
			running = inb(ioaddr);
			printk("%s: interrupt, status %02x, %04x %04x %02x serial %d.\n",
				 dev->name, status, result, rx_ptr, running, boguscnt);
		}
		if ((status & 0x80) == 0)
			break;

		if ((status & 0x0F) == 4) {	/* Transmit done. */
			struct net_local *lp = (struct net_local *)dev->priv;
			int tx_status;
			outb(CMD0_STAT1, ioaddr);
			tx_status = inw(ioaddr);
			/* It's undocumented, but tx_status seems to match the i82586. */
			if (tx_status & 0x2000) {
				lp->stats.tx_packets++;
				lp->stats.collisions += tx_status & 0xf;
			} else {
				if (tx_status & 0x0600)  lp->stats.tx_carrier_errors++;
				if (tx_status & 0x0100)  lp->stats.tx_fifo_errors++;
				if (!(tx_status & 0x0040)) lp->stats.tx_heartbeat_errors++;
				if (tx_status & 0x0020)  lp->stats.tx_aborted_errors++;
				/* ...and the catch-all. */
				if (tx_status | 0x0760 != 0x0760)
				  lp->stats.tx_errors++;
			}
			dev->tbusy = 0;
			mark_bh(INET_BH);	/* Inform upper layers. */
		}

		if ((status & 0x40)
			|| (status & 0x0f) == 11) {
			znet_rx(dev);
		}
		/* Clear the interrupts we've handled. */
		outb(CMD0_ACK,ioaddr);
	} while (boguscnt--);

	dev->interrupt = 0;
	return;
}

static void znet_rx(struct device *dev)
{
	struct net_local *lp = (struct net_local *)dev->priv;
	int ioaddr = dev->base_addr;
	int boguscount = 1;
	short next_frame_end_offset = 0; 		/* Offset of next frame start. */
	short *cur_frame_end;
	short cur_frame_end_offset;

	outb(CMD0_STAT2, ioaddr);
	cur_frame_end_offset = inw(ioaddr);

	if (cur_frame_end_offset == zn.rx_cur - zn.rx_start) {
		printk("%s: Interrupted, but nothing to receive, offset %03x.\n",
			   dev->name, cur_frame_end_offset);
		return;
	}

	/* Use same method as the Crynwr driver: construct a forward list in
	   the same area of the backwards links we now have.  This allows us to
	   pass packets to the upper layers in the order they were received --
	   important for fast-path sequential operations. */
	 while (zn.rx_start + cur_frame_end_offset != zn.rx_cur
			&& ++boguscount < 5) {
		unsigned short hi_cnt, lo_cnt, hi_status, lo_status;
		int count, status;

		if (cur_frame_end_offset < 4) {
			/* Oh no, we have a special case: the frame trailer wraps around
			   the end of the ring buffer.  We've saved space at the end of
			   the ring buffer for just this problem. */
			memcpy(zn.rx_end, zn.rx_start, 8);
			cur_frame_end_offset += (RX_BUF_SIZE/2);
		}
		cur_frame_end = zn.rx_start + cur_frame_end_offset - 4;

		lo_status = *cur_frame_end++;
		hi_status = *cur_frame_end++;
		status = ((hi_status & 0xff) << 8) + (lo_status & 0xff);
		lo_cnt = *cur_frame_end++;
		hi_cnt = *cur_frame_end++;
		count = ((hi_cnt & 0xff) << 8) + (lo_cnt & 0xff);

		if (znet_debug > 5)
		  printk("Constructing trailer at location %03x, %04x %04x %04x %04x"
				 " count %#x status %04x.\n",
				 cur_frame_end_offset<<1, lo_status, hi_status, lo_cnt, hi_cnt,
				 count, status);
		cur_frame_end[-4] = status;
		cur_frame_end[-3] = next_frame_end_offset;
		cur_frame_end[-2] = count;
		next_frame_end_offset = cur_frame_end_offset;
		cur_frame_end_offset -= ((count + 1)>>1) + 3;
		if (cur_frame_end_offset < 0)
		  cur_frame_end_offset += RX_BUF_SIZE/2;
	};

	/* Now step  forward through the list. */
	do {
		ushort *this_rfp_ptr = zn.rx_start + next_frame_end_offset;
		int status = this_rfp_ptr[-4];
		int pkt_len = this_rfp_ptr[-2];
	  
		if (znet_debug > 5)
		  printk("Looking at trailer ending at %04x status %04x length %03x"
				 " next %04x.\n", next_frame_end_offset<<1, status, pkt_len,
				 this_rfp_ptr[-3]<<1);
		/* Once again we must assume that the i82586 docs apply. */
		if ( ! (status & 0x2000)) {				/* There was an error. */
			lp->stats.rx_errors++;
			if (status & 0x0800) lp->stats.rx_crc_errors++;
			if (status & 0x0400) lp->stats.rx_frame_errors++;
			if (status & 0x0200) lp->stats.rx_over_errors++; /* Wrong. */
			if (status & 0x0100) lp->stats.rx_fifo_errors++;
			if (status & 0x0080) lp->stats.rx_length_errors++;
		} else if (pkt_len > 1536) {
			lp->stats.rx_length_errors++;
		} else {
			/* Malloc up new buffer. */
			int sksize = sizeof(struct sk_buff) + pkt_len;
			struct sk_buff *skb;

			skb = alloc_skb(sksize, GFP_ATOMIC);
			if (skb == NULL) {
				if (znet_debug)
				  printk("%s: Memory squeeze, dropping packet.\n", dev->name);
				lp->stats.rx_dropped++;
				break;
			}
			skb->mem_len = sksize;
			skb->mem_addr = skb;
			skb->len = pkt_len;
			skb->dev = dev;

			if (&zn.rx_cur[(pkt_len+1)>>1] > zn.rx_end) {
				int semi_cnt = (zn.rx_end - zn.rx_cur)<<1;
				memcpy((unsigned char *) (skb + 1), zn.rx_cur, semi_cnt);
				memcpy((unsigned char *) (skb + 1) + semi_cnt, zn.rx_start,
					   pkt_len - semi_cnt);
			} else {
				memcpy((unsigned char *) (skb + 1), zn.rx_cur, pkt_len);
				if (znet_debug > 6) {
					unsigned int *packet = (unsigned int *) (skb + 1);
					printk("Packet data is %08x %08x %08x %08x.\n", packet[0],
						   packet[1], packet[2], packet[3]);
				}
		  }

#ifdef HAVE_NETIF_RX
			netif_rx(skb);
#else
			skb->lock = 0;
			if (dev_rint((unsigned char*)skb, pkt_len, IN_SKBUFF, dev) != 0) {
				kfree_s(skb, sksize);
				lp->stats.rx_dropped++;
				break;
			}
#endif
			lp->stats.rx_packets++;
		}
		zn.rx_cur = this_rfp_ptr;
		if (zn.rx_cur >= zn.rx_end)
			zn.rx_cur -= RX_BUF_SIZE/2;
		update_stop_hit(ioaddr, (zn.rx_cur - zn.rx_start)<<1);
		next_frame_end_offset = this_rfp_ptr[-3];
		if (next_frame_end_offset == 0)		/* Read all the frames? */
			break;			/* Done for now */
		this_rfp_ptr = zn.rx_start + next_frame_end_offset;
	} while (--boguscount);

	/* If any worth-while packets have been received, dev_rint()
	   has done a mark_bh(INET_BH) for us and will work on them
	   when we get to the bottom-half routine. */
	return;
}

/* The inverse routine to znet_open(). */
static int znet_close(struct device *dev)
{
	int ioaddr = dev->base_addr;

	dev->tbusy = 1;
	dev->start = 0;

	outb(CMD0_RESET, ioaddr);			/* CMD0_RESET */

	disable_dma(zn.rx_dma);
	disable_dma(zn.tx_dma);

	free_irq(dev->irq);

	if (znet_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);
	/* Turn off transceiver power. */
	outb(0x10, 0xe6);					/* Select LAN control register */
	outb(inb(0xe7) & ~0x84, 0xe7);		/* Turn on LAN power (bit 2). */

	return 0;
}

/* Get the current statistics.	This may be called with the card open or
   closed. */
static struct enet_statistics *net_get_stats(struct device *dev)
{
		struct net_local *lp = (struct net_local *)dev->priv;

		return &lp->stats;
}

#ifdef HAVE_MULTICAST
/* Set or clear the multicast filter for this adaptor.
   num_addrs == -1	Promiscuous mode, receive all packets
   num_addrs == 0	Normal mode, clear multicast list
   num_addrs > 0	Multicast mode, receive normal and MC packets, and do
			best-effort filtering.
   As a side effect this routine must also initialize the device parameters.
   This is taken advantage of in open().

   N.B. that we change i593_init[] in place.  This (properly) makes the
   mode change persistent, but must be changed if this code is moved to
   a multiple adaptor environment.
 */
static void set_multicast_list(struct device *dev, int num_addrs, void *addrs)
{
	short ioaddr = dev->base_addr;

	if (num_addrs < 0) {
		/* Enable promiscuous mode */
		i593_init[7] &= ~3;		i593_init[7] |= 1;
		i593_init[13] &= ~8;	i593_init[13] |= 8;
	} else if (num_addrs > 0) {
		/* Enable accept-all-multicast mode */
		i593_init[7] &= ~3;		i593_init[7] |= 0;
		i593_init[13] &= ~8;	i593_init[13] |= 8;
	} else {					/* Enable normal mode. */
		i593_init[7] &= ~3;		i593_init[7] |= 0;
		i593_init[13] &= ~8;	i593_init[13] |= 0;
	}
	*zn.tx_cur++ = sizeof(i593_init);
	memcpy(zn.tx_cur, i593_init, sizeof(i593_init));
	zn.tx_cur += sizeof(i593_init)/2;
	outb(CMD0_CONFIGURE+CMD0_CHNL_1, ioaddr);
#ifdef not_tested
	if (num_addrs > 0) {
		int addrs_len = 6*num_addrs;
		*zn.tx_cur++ = addrs_len;
		memcpy(zn.tx_cur, addrs, addrs_len);
		outb(CMD0_MULTICAST_LIST+CMD0_CHNL_1, ioaddr);
		zn.tx_cur += addrs_len>>1;
	}
#endif
}
#endif

void show_dma(void)
{
	short dma_port = ((zn.tx_dma&3)<<2) + IO_DMA2_BASE;
	unsigned addr = inb(dma_port);
	addr |= inb(dma_port) << 8;
	printk("Addr: %04x cnt:%3x...", addr<<1,
		   get_dma_residue(zn.tx_dma));
}

/* Initialize the hardware.  We have to do this when the board is open()ed
   or when we come out of suspend mode. */
static void hardware_init(struct device *dev)
{
	short ioaddr = dev->base_addr;

	zn.rx_cur = zn.rx_start;
	zn.tx_cur = zn.tx_start;

	/* Reset the chip, and start it up. */
	outb(CMD0_RESET, ioaddr);

	cli(); {							/* Protect against a DMA flip-flop */
		disable_dma(zn.rx_dma); 		/* reset by an interrupting task. */
		clear_dma_ff(zn.rx_dma);
		set_dma_mode(zn.rx_dma, DMA_RX_MODE);
		set_dma_addr(zn.rx_dma, (unsigned int) zn.rx_start);
		set_dma_count(zn.rx_dma, RX_BUF_SIZE);
		enable_dma(zn.rx_dma);
		/* Now set up the Tx channel. */
		disable_dma(zn.tx_dma);
		clear_dma_ff(zn.tx_dma);
		set_dma_mode(zn.tx_dma, DMA_TX_MODE);
		set_dma_addr(zn.tx_dma, (unsigned int) zn.tx_start);
		set_dma_count(zn.tx_dma, zn.tx_buf_len<<1);
		enable_dma(zn.tx_dma);
	} sti();

	if (znet_debug > 1)
	  printk("%s: Initializing the i82593, tx buf %p... ", dev->name,
			 zn.tx_start);
	/* Do an empty configure command, just like the Crynwr driver.  This
	   resets to chip to its default values. */
	*zn.tx_cur++ = 0;
	*zn.tx_cur++ = 0;
	printk("stat:%02x ", inb(ioaddr)); show_dma();
	outb(CMD0_CONFIGURE+CMD0_CHNL_1, ioaddr);
	*zn.tx_cur++ = sizeof(i593_init);
	memcpy(zn.tx_cur, i593_init, sizeof(i593_init));
	zn.tx_cur += sizeof(i593_init)/2;
	printk("stat:%02x ", inb(ioaddr)); show_dma();
	outb(CMD0_CONFIGURE+CMD0_CHNL_1, ioaddr);
	*zn.tx_cur++ = 6;
	memcpy(zn.tx_cur, dev->dev_addr, 6);
	zn.tx_cur += 3;
	printk("stat:%02x ", inb(ioaddr)); show_dma();
	outb(CMD0_IA_SETUP + CMD0_CHNL_1, ioaddr);
	printk("stat:%02x ", inb(ioaddr)); show_dma();

	update_stop_hit(ioaddr, 8192);
	if (znet_debug > 1)  printk("enabling Rx.\n");
	outb(CMD0_Rx_ENABLE+CMD0_CHNL_0, ioaddr);
	dev->tbusy = 0;
}

#ifdef notdef
foo()
{
	/*do_command(ioaddr, CMD0_CONFIGURE+CMD0_CHNL_1, sizeof(i593_init) + 2,
			   zn.tx_buffer);*/
	/*do_command(ioaddr, CMD0_CONFIGURE+CMD0_CHNL_1, 32, zn.tx_buffer);*/
	/*outb(CMD0_CONFIGURE+CMD0_CHNL_1, ioaddr);*/

	if (znet_debug > 1)  printk("Set Address... ");
	*zn.tx_cur++ = 6;
	memcpy(zn.tx_cur, dev->dev_addr, 6);
	zn.tx_cur += 3;
	outb(CMD0_IA_SETUP + CMD0_CHNL_1, ioaddr);
	{
		unsigned stop_time = jiffies + 3; 
		while (jiffies < stop_time);
	}
	if (znet_debug > 2) {
		short dma_port = ((zn.tx_dma&3)<<2) + IO_DMA2_BASE;
		unsigned addr = inb(dma_port);
		addr |= inb(dma_port) << 8;
		printk("Terminal addr is %04x, cnt. %03x...", addr<<1,
			   get_dma_residue(zn.tx_dma));
	}
	*zn.tx_cur++ = 6;
	memcpy(zn.tx_cur, dev->dev_addr, 6);
	zn.tx_cur += 3;
	outb(CMD0_IA_SETUP + CMD0_CHNL_1, ioaddr);
	{
		unsigned stop_time = jiffies + 2; 
		while (jiffies < stop_time);
	}
	if (znet_debug > 2) {
		short dma_port = ((zn.tx_dma&3)<<2) + IO_DMA2_BASE;
		unsigned addr = inb(dma_port);
		addr |= inb(dma_port) << 8;
		printk("Terminal addr is %04x, cnt. %03x...", addr<<1,
			   get_dma_residue(zn.tx_dma));
	}
	wait_for_done(ioaddr);

	if (znet_debug > 1)  printk("Set Mode... ");
	set_multicast_list(dev, 0, 0);
	{
		unsigned stop_time = jiffies + 3; 
		while (jiffies < stop_time);
	}
	if (znet_debug > 2) {
		short dma_port = ((zn.tx_dma&3)<<2) + IO_DMA2_BASE;
		unsigned addr = inb(dma_port);
		addr |= inb(dma_port) << 8;
		printk("Terminal addr is %04x, cnt. %03x...", addr<<1,
			   get_dma_residue(zn.tx_dma));
	}
	if (znet_debug > 2) {
		int i;
		outb(CMD0_DUMP+CMD0_CHNL_0, ioaddr);
		printk("Dumping state:");
		for (i = 0; i < 16; i++)
			printk(" %04x", *zn.rx_cur++);
		printk("\n             :");
		for (;i < 32; i++)
			printk(" %04x", *zn.rx_cur++);
		printk("\n");
		wait_for_done(ioaddr);
	}
}

static int do_command(short ioaddr, int command, int length, ushort *buffer)
{
	/* This isn't needed, but is here for safety. */
	outb(CMD0_NOP+CMD0_STAT3,ioaddr);
	if (inb(ioaddr) & 3)
	  printk("znet: do_command() while the i82593 is busy.\n");

	cli();
	disable_dma(zn.tx_dma);
	clear_dma_ff(zn.tx_dma);
	set_dma_mode(zn.tx_dma,DMA_MODE_WRITE);
	set_dma_addr(zn.tx_dma,(unsigned int) zn.tx_start);
	set_dma_count(zn.tx_dma,length);
	sti();
	enable_dma(zn.tx_dma);
	outb(command, ioaddr);
	return 0;
}

/* wait_for_done - this is a blatent rip-off of the wait_for_done routine
 ** from the Crynwr packet driver.	It does not work correctly - doesn't
 ** acknowledge the interrupts it gets or something.  It does determine
 ** when the command is done, or if there are none executing, though...
 **		-Mike
 */
static int wait_for_done(short ioaddr)
{
  unsigned int stat;
  unsigned stop_time = jiffies + 10;
  int ticks = 0;

  /* check to see if we are busy */
  outb(CMD0_NOP+CMD0_STAT3,ioaddr);
  stat = inb(ioaddr);

  /* check if busy */
  if ((stat&3)==0) {
	if (znet_debug > 5)
	  printk("wait_for_done(): Not busy, status %02x.\n", stat);
	return 0;
  }

  while (jiffies < stop_time) {
	  /* now check */
	  outb(CMD0_NOP+CMD0_STAT3,ioaddr);
	  stat = inb(ioaddr);
	  if ((stat&3)==0) {
		  if (znet_debug > 5)
			printk("Command completed after %d ticks status %02x.\n",
				   ticks, stat);
		  outb((CMD0_NOP|CMD0_ACK),ioaddr);
		  return 0;
	  }
	  ticks++;
  }
  outb(CMD0_ABORT, ioaddr);
  if (znet_debug)
	printk("wait_for_done: command not ACK'd, status %02x after abort %02x.\n",
		   stat, inb(ioaddr));

  /* should re-initialize here... */
  return 1;
}
#endif /* notdef */

static void update_stop_hit(short ioaddr, unsigned short rx_stop_offset)
{
	outb(CMD0_PORT_1, ioaddr);
	if (znet_debug > 5)
	  printk("Updating stop hit with value %02x.\n",
			 (rx_stop_offset >> 6) | 0x80);
	outb((rx_stop_offset >> 6) | 0x80, ioaddr);
	outb(CMD1_PORT_0, ioaddr);
}

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c znet.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */
