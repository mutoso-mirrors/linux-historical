/*
 * linux/drivers/net/ether3.c
 *
 * SEEQ nq8005 ethernet driver for Acorn/ANT Ether3 card
 *  for Acorn machines
 *
 * By Russell King, with some suggestions from borris@ant.co.uk
 *
 * Changelog:
 * 1.04	RMK	29/02/1996	Won't pass packets that are from our ethernet
 *				address up to the higher levels - they're
 *				silently ignored.  I/F can now be put into
 *				multicast mode.  Receiver routine optimised.
 * 1.05	RMK	30/02/1996	Now claims interrupt at open when part of
 *				the kernel rather than when a module.
 * 1.06	RMK	02/03/1996	Various code cleanups
 * 1.07	RMK	13/10/1996	Optimised interrupt routine and transmit
 *				routines.
 * 1.08	RMK	14/10/1996	Fixed problem with too many packets,
 *				prevented the kernel message about dropped
 *				packets appearing too many times a second.
 *				Now does not disable all IRQs, only the IRQ
 *				used by this card.
 * 1.09	RMK	10/11/1996	Only enables TX irq when buffer space is low,
 *				but we still service the TX queue if we get a
 *				RX interrupt.
 * 1.10	RMK	15/07/1997	Fixed autoprobing of NQ8004.
 * 1.11	RMK	16/11/1997	Fixed autoprobing of NQ8005A.
 * 1.12	RMK	31/12/1997	Removed reference to dev_tint for Linux 2.1.
 *
 * TODO:
 *  When we detect a fatal error on the interface, we should restart it.
 *  Reap transmit packets after some time even if the buffer never filled.
 */

static char *version = "ether3 ethernet driver (c) 1995-1998 R.M.King v1.12\n";

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/ecard.h>
#include <asm/delay.h>
#include <asm/io.h>
#include <asm/irq.h>

#include "ether3.h"

#ifndef MODULE
#define CLAIM_IRQ_AT_OPEN
#endif

static unsigned int net_debug = NET_DEBUG;
static const card_ids ether3_cids[] = {
	{ MANU_ANT2, PROD_ANT_ETHER3 },
	{ MANU_ANT,  PROD_ANT_ETHER3 },
	{ MANU_ANT,  PROD_ANT_ETHERB }, /* trial - will etherb work? */
	{ 0xffff, 0xffff }
};

static void ether3_setmulticastlist(struct device *dev);
static int  ether3_rx(struct device *dev, struct dev_priv *priv, unsigned int maxcnt);
static void ether3_tx(struct device *dev, struct dev_priv *priv);

extern int inswb(int reg, void *buffer, int len);
extern int outswb(int reg, void *buffer, int len);

#define BUS_16		2
#define BUS_8		1
#define BUS_UNKNOWN	0

/*
 * I'm not sure what address we should default to if the internal one
 * is corrupted...
 */
unsigned char def_eth_addr[6] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};

/* --------------------------------------------------------------------------- */

typedef enum {
	buffer_write,
	buffer_read
} buffer_rw_t;

static int
ether3_setbuffer(struct device *dev, buffer_rw_t read, int start)
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;
	int timeout = 1000;

	outw(priv->regs.config1 | CFG1_LOCBUFMEM, REG_CONFIG1);
	outw(priv->regs.command | CMD_FIFOWRITE, REG_COMMAND);
	while ((inw(REG_STATUS) & STAT_FIFOEMPTY) == 0) {
		if (!timeout--) {
			printk("%s: setbuffer broken\n", dev->name);
			priv->broken = 1;
			return 1;
		}
		udelay(1);
	}
	if (read == buffer_read) {
		outw(start, REG_DMAADDR);
		outw(priv->regs.command | CMD_FIFOREAD, REG_COMMAND);
	} else {
		outw(priv->regs.command | CMD_FIFOWRITE, REG_COMMAND);
		outw(start, REG_DMAADDR);
	}
	return 0;
}

/*
 * write data to the buffer memory
 */
#define ether3_writebuffer(dev,data,length)			\
	outswb(REG_BUFWIN, (data), (length))

#define ether3_writeword(dev,data)				\
	outw((data), REG_BUFWIN)

#define ether3_writelong(dev,data)	{			\
	unsigned long reg_bufwin = REG_BUFWIN;			\
	outw((data), reg_bufwin);				\
	outw((data) >> 16, reg_bufwin);			\
}

/*
 * read data from the buffer memory
 */
#define ether3_readbuffer(dev,data,length)			\
	inswb(REG_BUFWIN, (data), (length))

#define ether3_readword(dev)					\
	inw(REG_BUFWIN)

#define ether3_readlong(dev)	 				\
	inw(REG_BUFWIN) | (inw(REG_BUFWIN) << 16)

/*
 * Switch LED off...
 */
static void
ether3_ledoff(unsigned long data)
{
	struct device *dev = (struct device *)data;
	struct dev_priv *priv = (struct dev_priv *)dev->priv;
	outw(priv->regs.config2 |= CFG2_CTRLO, REG_CONFIG2);
}

/*
 * switch LED on...
 */
static inline void
ether3_ledon(struct device *dev, struct dev_priv *priv)
{
	del_timer(&priv->timer);
	priv->timer.expires = jiffies + HZ / 50; /* leave on for 1/50th second */
	priv->timer.data = (unsigned long)dev;
	priv->timer.function = ether3_ledoff;
	add_timer(&priv->timer);
	if (priv->regs.config2 & CFG2_CTRLO)
		outw(priv->regs.config2 &= ~CFG2_CTRLO, REG_CONFIG2);
}

/*
 * Read the ethernet address string from the on board rom.
 * This is an ascii string!!!
 */
__initfunc(static void
ether3_addr(char *addr, struct expansion_card *ec))
{
	struct in_chunk_dir cd;
	char *s;
	
	if (ecard_readchunk(&cd, ec, 0xf5, 0) && (s = strchr(cd.d.string, '('))) {
		int i;
		for (i = 0; i<6; i++) {
			addr[i] = simple_strtoul(s + 1, &s, 0x10);
			if (*s != (i==5?')' : ':' ))
				break;
		}
		if (i == 6)
			return;
	}
	/* I wonder if we should even let the user continue in this case
	 *   - no, it would be better to disable the device
	 */
	printk(KERN_ERR "ether3: Couldn't read a valid MAC address from card.\n");
	memcpy(addr, def_eth_addr, 6);
}

/* --------------------------------------------------------------------------- */

__initfunc(static int
ether3_ramtest(struct device *dev, unsigned char byte))
{
	unsigned char *buffer = kmalloc(RX_END, GFP_KERNEL);
	int i,ret = 0;
	int max_errors = 4;
	int bad = -1;

	if (!buffer)
		return 1;

	memset(buffer, byte, RX_END);
	ether3_setbuffer(dev, buffer_write, 0);
	ether3_writebuffer(dev, buffer, TX_END);
	ether3_setbuffer(dev, buffer_write, RX_START);
	ether3_writebuffer(dev, buffer + RX_START, RX_LEN);
	memset(buffer, byte ^ 0xff, RX_END);
	ether3_setbuffer(dev, buffer_read, 0);
	ether3_readbuffer(dev, buffer, TX_END);
	ether3_setbuffer(dev, buffer_read, RX_START);
	ether3_readbuffer(dev, buffer + RX_START, RX_LEN);

	for (i = 0; i < RX_END; i++) {
		if (buffer[i] != byte) {
			if (max_errors > 0 && bad != buffer[i]) {
				printk("%s: RAM failed with (%02X instead of %02X) at 0x%04X",
				       dev->name, buffer[i], byte, i);
				ret = 2;
				max_errors--;
				bad = i;
			}
		} else {
			if (bad != -1) {
			    	if (bad != i - 1)
					printk(" - 0x%04X", i - 1);
				printk("\n");
				bad = -1;
			}
		}
	}
	if (bad != -1)
		printk(" - 0xffff\n");
	kfree(buffer);

	return ret;
}

/* ------------------------------------------------------------------------------- */

__initfunc(static int
ether3_init_2(struct device *dev))
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;
	int i;

	priv->regs.config1 = CFG1_RECVCOMPSTAT0|CFG1_DMABURST8;
	priv->regs.config2 = CFG2_CTRLO|CFG2_RECVCRC|CFG2_ERRENCRC;
	priv->regs.command = 0;
	/*
	 * Set up our hardware address
	 */
	outw(priv->regs.config1 | CFG1_BUFSELSTAT0, REG_CONFIG1);
	for (i = 0; i < 6; i++)
		outb(dev->dev_addr[i], REG_BUFWIN);

	if (dev->flags & IFF_PROMISC)
		priv->regs.config1 |= CFG1_RECVPROMISC;
	else if (dev->flags & IFF_MULTICAST)
		priv->regs.config1 |= CFG1_RECVSPECBRMULTI;
	else
		priv->regs.config1 |= CFG1_RECVSPECBROAD;

	/*
	 * There is a problem with the NQ8005 in that it occasionally loses the
	 * last two bytes.  To get round this problem, we receive the CRC as
	 * well.  That way, if we do loose the last two, then it doesn't matter.
	 */
	outw(priv->regs.config1 | CFG1_TRANSEND, REG_CONFIG1);
	outw((TX_END>>8) - 1, REG_BUFWIN);
	outw(priv->rx_head, REG_RECVPTR);
	outw(0, REG_TRANSMITPTR);
	outw(priv->rx_head >> 8, REG_RECVEND);
	outw(priv->regs.config2, REG_CONFIG2);
	outw(priv->regs.config1 | CFG1_LOCBUFMEM, REG_CONFIG1);
	outw(priv->regs.command, REG_COMMAND);

	i = ether3_ramtest(dev, 0x5A);
	if(i)
		return i;
	i = ether3_ramtest(dev, 0x1E);
	if(i)
		return i;

	ether3_setbuffer(dev, buffer_write, 0);
	ether3_writelong(dev, 0);
	return 0;
}

static void
ether3_init_for_open(struct device *dev)
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;
	int i;

	memset(&priv->stats, 0, sizeof(struct enet_statistics));

	priv->regs.command = 0;
	outw(CMD_RXOFF|CMD_TXOFF, REG_COMMAND);
	while (inw(REG_STATUS) & (STAT_RXON|STAT_TXON));

	outw(priv->regs.config1 | CFG1_BUFSELSTAT0, REG_CONFIG1);
	for (i = 0; i < 6; i++)
		outb(dev->dev_addr[i], REG_BUFWIN);

	priv->tx_used	= 0;
	priv->tx_head	= 0;
	priv->tx_tail	= 0;
	priv->regs.config2 |= CFG2_CTRLO;
	priv->rx_head	= RX_START;

	outw(priv->regs.config1 | CFG1_TRANSEND, REG_CONFIG1);
	outw((TX_END>>8) - 1, REG_BUFWIN);
	outw(priv->rx_head, REG_RECVPTR);
	outw(priv->rx_head >> 8, REG_RECVEND);
	outw(0, REG_TRANSMITPTR);
	outw(priv->regs.config2, REG_CONFIG2);
	outw(priv->regs.config1 | CFG1_LOCBUFMEM, REG_CONFIG1);

	ether3_setbuffer(dev, buffer_write, 0);
	ether3_writelong(dev, 0);

	priv->regs.command = CMD_ENINTRX;
	outw(priv->regs.command | CMD_RXON, REG_COMMAND);
}

static inline int
ether3_probe_bus_8(struct device *dev, int val)
{
	outb(val & 255, REG_RECVPTR);
	outb(val >> 8, REG_RECVPTR + 1);

	return inb(REG_RECVPTR) == (val & 255) && inb(REG_RECVPTR + 1) == (val >> 8);
}

static inline int
ether3_probe_bus_16(struct device *dev, int val)
{
	outw(val, REG_RECVPTR);

	return inw(REG_RECVPTR) == val;
}

/*
 * This is the real probe routine.
 */
__initfunc(static int
ether3_probe1(struct device *dev))
{
	static unsigned version_printed = 0;
	struct dev_priv *priv;
	unsigned int i, bus_type, error = ENODEV;

	if (net_debug  &&  version_printed++ == 0)
		printk(version);

	if (!dev->priv) {
		dev->priv = kmalloc(sizeof (struct dev_priv), GFP_KERNEL);
		if (!dev->priv)
			return -ENOMEM;
	}

	priv = (struct dev_priv *) dev->priv;
	memset(priv, 0, sizeof(struct dev_priv));

	request_region(dev->base_addr, 128, "ether3");

	/* Reset card...
	 */
	outb(0x80, REG_CONFIG2 + 1);
	bus_type = BUS_UNKNOWN;
	udelay(4);

	/* Test using Receive Pointer (16-bit register) to find out
	 * how the ether3 is connected to the bus...
	 */
	if (ether3_probe_bus_8(dev, 0x100) &&
	    ether3_probe_bus_8(dev, 0x201))
		bus_type = BUS_8;

	if (bus_type == BUS_UNKNOWN &&
	    ether3_probe_bus_16(dev, 0x101) &&
	    ether3_probe_bus_16(dev, 0x201))
		bus_type = BUS_16;

	switch (bus_type) {
	case BUS_UNKNOWN:
		printk(KERN_ERR "%s: unable to identify podule bus width\n", dev->name);
		goto failed;

	case BUS_8:
		printk(KERN_ERR "%s: ether3 found, but is an unsupported 8-bit card\n", dev->name);
		goto failed;

	default:
		break;
	}

	printk("%s: ether3 found at %lx, IRQ%d, ether address ", dev->name, dev->base_addr, dev->irq);
	for (i = 0; i < 6; i++)
		printk(i == 5 ? "%2.2x\n" : "%2.2x:", dev->dev_addr[i]);

	if (!ether3_init_2(dev)) {
		dev->open = ether3_open;
		dev->stop = ether3_close;
		dev->hard_start_xmit = ether3_sendpacket;
		dev->get_stats = ether3_getstats;
		dev->set_multicast_list = ether3_setmulticastlist;

		/* Fill in the fields of the device structure with ethernet values. */
		ether_setup(dev);
#ifndef CLAIM_IRQ_AT_OPEN
		if (request_irq(dev->irq, ether3_interrupt, 0, "ether3", dev))
			error = EAGAIN;
		else
#endif
			return 0;
	}

failed:
	kfree(dev->priv);
	dev->priv = NULL;
	release_region(dev->base_addr, 128);
	return error;
}

#ifndef MODULE
__initfunc(int
ether3_probe(struct device *dev))
{
	struct expansion_card *ec;

	if (!dev)
		return ENODEV;

	ecard_startfind();

	if ((ec = ecard_find(0, ether3_cids)) == NULL)
		return ENODEV;

	dev->base_addr = ecard_address(ec, ECARD_MEMC, 0);
	dev->irq = ec->irq;

	ecard_claim(ec);

	ether3_addr(dev->dev_addr, ec);
	return ether3_probe1(dev);
}
#endif

/*
 * Open/initialize the board.  This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
static int
ether3_open(struct device *dev)
{
	ether3_init_for_open(dev);

	MOD_INC_USE_COUNT;

#ifdef CLAIM_IRQ_AT_OPEN
	if (request_irq(dev->irq, ether3_interrupt, 0, "ether3", dev)) {
	    	MOD_DEC_USE_COUNT;
		return -EAGAIN;
	}
#endif

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;
	return 0;
}

/*
 * The inverse routine to ether3_open().
 */
static int
ether3_close(struct device *dev)
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;

	dev->tbusy = 1;
	dev->start = 0;

	disable_irq(dev->irq);

	outw(CMD_RXOFF|CMD_TXOFF, REG_COMMAND);
	priv->regs.command = 0;
	while (inw(REG_STATUS) & (STAT_RXON|STAT_TXON));
	outb(0x80, REG_CONFIG2 + 1);
	outw(0, REG_COMMAND);

	enable_irq(dev->irq);
#ifdef CLAIM_IRQ_AT_OPEN
	free_irq(dev->irq, dev);
#endif

	MOD_DEC_USE_COUNT;
	return 0;
}

/*
 * Get the current statistics.	This may be called with the card open or
 * closed.
 */
static struct enet_statistics *ether3_getstats(struct device *dev)
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;
	return &priv->stats;
}

/*
 * Set or clear promiscuous/multicast mode filter for this adaptor.
 *
 * We don't attempt any packet filtering.  The card may have a SEEQ 8004
 * in which does not have the other ethernet address registers present...
 */
static void ether3_setmulticastlist(struct device *dev)
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;

	priv->regs.config1 &= ~CFG1_RECVPROMISC;

	if (dev->flags & IFF_PROMISC) {
		/* promiscuous mode */
		priv->regs.config1 |= CFG1_RECVPROMISC;
	} else
	if (dev->flags & IFF_ALLMULTI) {
		priv->regs.config1 |= CFG1_RECVSPECBRMULTI;
	} else
		priv->regs.config1 |= CFG1_RECVSPECBROAD;

	outw(priv->regs.config1 | CFG1_LOCBUFMEM, REG_CONFIG1);
}

/*
 * Transmit a packet
 */
static int
ether3_sendpacket(struct sk_buff *skb, struct device *dev)
{
	struct dev_priv *priv = (struct dev_priv *)dev->priv;
retry:
	if (!dev->tbusy) {
		/* Block a timer-based transmit from overlapping.  This could better be
		 * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
		 */
		if (!test_and_set_bit(0, (void *)&dev->tbusy)) {
			unsigned long flags;
			unsigned int length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;
			unsigned int ptr, nextptr;

			length = (length + 1) & ~1;

			if (priv->broken) {
				dev_kfree_skb(skb);
				priv->stats.tx_dropped ++;
				dev->tbusy = 0;
				return 0;
			}

			ptr = priv->tx_head;
			nextptr = ptr + 0x600;
			if (nextptr >= TX_END)
				nextptr = 0;
			if (nextptr == priv->tx_tail)
				return 1;		/* unable to queue */
			priv->tx_head = nextptr;

			save_flags_cli(flags);
			ether3_setbuffer(dev, buffer_write, nextptr);
			ether3_writelong(dev, 0);
			ether3_setbuffer(dev, buffer_write, ptr + 4);
			ether3_writebuffer(dev, skb->data, length);
			ether3_writeword(dev, htons(nextptr));
			ether3_writeword(dev, (TXHDR_TRANSMIT|TXHDR_CHAINCONTINUE) >> 16);
			ether3_setbuffer(dev, buffer_write, ptr);
#define TXHDR_FLAGS (TXHDR_TRANSMIT|TXHDR_CHAINCONTINUE|TXHDR_DATAFOLLOWS|TXHDR_ENSUCCESS)
			ether3_writeword(dev, htons(ptr + length + 4));
			ether3_writeword(dev, (TXHDR_FLAGS >> 16));
			ether3_ledon(dev, priv);
			priv->tx_used ++;
			if (priv->tx_used < MAX_TX_BUFFERED)
				dev->tbusy = 0;
			if (priv->tx_used >= (MAX_TX_BUFFERED * 3 / 4)) {
				priv->regs.command |= CMD_ENINTTX;
				outw(priv->regs.command, REG_COMMAND);
			}
			restore_flags(flags);

			dev->trans_start = jiffies;
			dev_kfree_skb(skb);
			if (!(inw(REG_STATUS) & STAT_TXON)) {
				outw(ptr, REG_TRANSMITPTR);
				outw(priv->regs.command | CMD_TXON, REG_COMMAND);
			}
			return 0;
		} else {
			printk("%s: transmitter access conflict.\n", dev->name);
			return 1;
		}
	} else {
		/* If we get here, some higher level has decided we are broken.
		 * There should really be a "kick me" function call instead.
		 */
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 5)
			return 1;
		del_timer(&priv->timer);
		printk("%s: transmit timed out, network cable problem?\n", dev->name);
		dev->tbusy = 0;
		priv->regs.config2 |= CFG2_CTRLO;
		outw(priv->regs.config2 , REG_CONFIG2);
		dev->trans_start = jiffies;
		goto retry;
	}
}

static void
ether3_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct dev_priv *priv;
	unsigned int status;

#if NET_DEBUG > 1
	if(net_debug & DEBUG_INT)
		printk("eth3irq: %d ", irq);
#endif

	priv = (struct dev_priv *)dev->priv;

	dev->interrupt = 1;
	status = inw(REG_STATUS);
	/*
	 * Dispite we disable the TX interrupt when the packet buffer is
	 * mostly empty, if we happen to get a RX interrupt, we might as
	 * well handle the TX packets as well.
	 */
	if (status & STAT_INTTX) { /* Packets transmitted */
		outw(CMD_ACKINTTX | priv->regs.command, REG_COMMAND);
		ether3_tx(dev, priv);
	}

	status = inw(REG_STATUS);
	if (status & STAT_INTRX && ether3_rx(dev, priv, 12)) { /* Got packet(s). */
		/*
		 * We only acknowledge the interrupt if we have received all packets
		 * in the buffer or else we run out of memory. This is to allow the
		 * bh routines to run.
		 */
		outw(CMD_ACKINTRX | priv->regs.command, REG_COMMAND);
		/*
		 * Receive again if some have become available - we may have cleared
		 * a pending IRQ
		 */
		ether3_rx(dev, priv, 4);
	}
	dev->interrupt = 0;

#if NET_DEBUG > 1
	if(net_debug & DEBUG_INT)
		printk("done\n");
#endif
}

/*
 * If we have a good packet(s), get it/them out of the buffers.
 */
static int
ether3_rx(struct device *dev, struct dev_priv *priv, unsigned int maxcnt)
{
	unsigned int next_ptr = priv->rx_head, received = 0;
	ether3_ledon(dev, priv);

	do {
		unsigned int this_ptr, status;
		unsigned char addrs[16];

		/*
		 * read the first 16 bytes from the buffer.
		 * This contains the status bytes etc and ethernet addresses,
		 * and we also check the source ethernet address to see if
		 * it originated from us.
		 */
		{
			unsigned int temp_ptr;
			ether3_setbuffer(dev, buffer_read, next_ptr);
			temp_ptr = ether3_readword(dev);
			status = ether3_readword(dev);
			if (!(status & RXSTAT_DONE) || !temp_ptr)
				break;

			this_ptr = next_ptr + 4;
			next_ptr = ntohs(temp_ptr);
		}
		ether3_setbuffer(dev, buffer_read, this_ptr);
		ether3_readbuffer(dev, addrs+2, 12);

		/*
 		 * ignore our own packets...
	 	 */
		if (!(*(unsigned long *)&dev->dev_addr[0] ^ *(unsigned long *)&addrs[2+6]) &&
		    !(*(unsigned short *)&dev->dev_addr[4] ^ *(unsigned short *)&addrs[2+10])) {
			maxcnt ++; /* compensate for loopedback packet */
			outw(next_ptr >> 8, REG_RECVEND);
		} else
		if (!(status & (RXSTAT_OVERSIZE|RXSTAT_CRCERROR|RXSTAT_DRIBBLEERROR|RXSTAT_SHORTPACKET))) {
			unsigned int length = next_ptr - this_ptr;
			struct sk_buff *skb;

			if (next_ptr <= this_ptr)
				length += RX_END - RX_START;

			skb = dev_alloc_skb(length + 2);
			if (skb) {
				unsigned char *buf;

				skb->dev = dev;
				skb_reserve(skb, 2);
				buf = skb_put(skb, length);
				ether3_readbuffer(dev, buf + 12, length - 12);
				outw(next_ptr >> 8, REG_RECVEND);
				*(unsigned short *)(buf + 0)	= *(unsigned short *)(addrs + 2);
				*(unsigned long *)(buf + 2)	= *(unsigned long *)(addrs + 4);
				*(unsigned long *)(buf + 6)	= *(unsigned long *)(addrs + 8);
				*(unsigned short *)(buf + 10)	= *(unsigned short *)(addrs + 12);
				skb->protocol = eth_type_trans(skb, dev);
				netif_rx(skb);
				received ++;
			} else
				goto dropping;
		} else {
			struct enet_statistics *stats = &priv->stats;
			outw(next_ptr >> 8, REG_RECVEND);
			if (status & RXSTAT_OVERSIZE)	  stats->rx_length_errors ++;
			if (status & RXSTAT_CRCERROR)	  stats->rx_crc_errors ++;
			if (status & RXSTAT_DRIBBLEERROR) stats->rx_fifo_errors ++;
			if (status & RXSTAT_SHORTPACKET)  stats->rx_length_errors ++;
			stats->rx_errors++;
		}
	}
	while (-- maxcnt);

done:
	priv->stats.rx_packets += received;
	priv->rx_head = next_ptr;
	/*
	 * If rx went off line, then that means that the buffer may be full.  We
	 * have dropped at least one packet.
	 */
	if (!(inw(REG_STATUS) & STAT_RXON)) {
		priv->stats.rx_dropped ++;
    		outw(next_ptr, REG_RECVPTR);
		outw(priv->regs.command | CMD_RXON, REG_COMMAND);
	}

	return maxcnt;

dropping:{
	static unsigned long last_warned;

	outw(next_ptr >> 8, REG_RECVEND);
	/*
	 * Don't print this message too many times...
	 */
	if (jiffies - last_warned > 30 * HZ) {
		last_warned = jiffies;
		printk("%s: memory squeeze, dropping packet.\n", dev->name);
	}
	priv->stats.rx_dropped ++;
	goto done;
	}
}

/*
 * Update stats for the transmitted packet(s)
 */
static void
ether3_tx(struct device *dev, struct dev_priv *priv)
{
	unsigned int tx_tail = priv->tx_tail;

	do {
	    	unsigned long status;
    		/*
	    	 * Read the packet header
    		 */
	    	ether3_setbuffer(dev, buffer_read, tx_tail);
    		status = ether3_readlong(dev);

		/*
		 * Check to see if this packet has been transmitted
		 */
		if (!(status & TXSTAT_DONE) || !(status & TXHDR_TRANSMIT))
			break;

		/*
		 * Update errors
		 */
		if (!(status & (TXSTAT_BABBLED | TXSTAT_16COLLISIONS)))
			priv->stats.tx_packets++;
		else {
			priv->stats.tx_errors ++;
			if (status & TXSTAT_16COLLISIONS) priv->stats.collisions += 16;
			if (status & TXSTAT_BABBLED) priv->stats.tx_fifo_errors ++;
		}

		/*
		 * Get next packet address
		 */
		tx_tail += 0x600;
		if (tx_tail >= TX_END)
			tx_tail = 0;

		if (priv->tx_used)
			priv->tx_used--;
	} while (1);

	if (priv->tx_tail != tx_tail) {
		priv->tx_tail = tx_tail;
		if (priv->tx_used <= MAX_TX_BUFFERED) {
			dev->tbusy = 0;
			mark_bh(NET_BH);	/* Inform upper layers. */
		}
	}
	priv->regs.command &= ~CMD_ENINTTX;
	outw(priv->regs.command, REG_COMMAND);
}

#ifdef MODULE

char ethernames[MAX_ECARDS][9];

static struct device *my_ethers[MAX_ECARDS];
static struct expansion_card *ec[MAX_ECARDS];

int
init_module(void)
{
	int i;

	for(i = 0; i < MAX_ECARDS; i++) {
		my_ethers[i] = NULL;
		ec[i] = NULL;
		strcpy(ethernames[i], "        ");
	}

	i = 0;

	ecard_startfind();

	do {
		if ((ec[i] = ecard_find(0, ether3_cids)) == NULL)
			break;

		my_ethers[i] = (struct device *)kmalloc(sizeof(struct device), GFP_KERNEL);
		memset(my_ethers[i], 0, sizeof(struct device));

		my_ethers[i]->irq = ec[i]->irq;
		my_ethers[i]->base_addr= ecard_address(ec[i], ECARD_MEMC, 0);
		my_ethers[i]->init = ether3_probe1;
		my_ethers[i]->name = ethernames[i];

		ether3_addr(my_ethers[i]->dev_addr, ec[i]);

		ecard_claim(ec[i]);

		if(register_netdev(my_ethers[i]) != 0) {
			for (i = 0; i < 4; i++) {
				if(my_ethers[i]) {
					kfree(my_ethers[i]);
					my_ethers[i] = NULL;
				}
				if(ec[i]) {
					ecard_release(ec[i]);
					ec[i] = NULL;
				}
			}
			return -EIO;
		}
		i++;
	}
	while(i < MAX_ECARDS);

	return i != 0 ? 0 : -ENODEV;
}

void
cleanup_module(void)
{
	int i;
	for (i = 0; i < MAX_ECARDS; i++) {
		if (my_ethers[i]) {
		  	release_region(my_ethers[i]->base_addr, 128);
			unregister_netdev(my_ethers[i]);
			my_ethers[i] = NULL;
		}
		if (ec[i]) {
			ecard_release(ec[i]);
			ec[i] = NULL;
		}
	}
}
#endif /* MODULE */
