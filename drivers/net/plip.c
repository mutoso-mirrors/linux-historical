/*
 *	 Plip.c: A parallel port "network" driver for linux. 
 */

/*
 *	Developement History:
 *
 *	Original version and the name 'PLIP' from Donald Becker  <becker@super.org>
 *		inspired by Russ Nelson's parallel port packet driver.
 *	Further development by Tommy Thorn <thorn@daimi.aau.dk>
 *	Some changes by Tanabe Hiroyasu <hiro@sanpo.t.u-tokyo.ac.jp>
 *	Upgraded for PL12 by Donald Becker
 *	Minor hacks by Alan Cox <gw4pts@gw4pts.ampr.org> to get it working
 *		more reliably (Ha!)
 *     Changes even more Peter Bauer (100136.3530@compuserve.com)
 *     Protocol changed back to original plip as in crynwr's packet-drivers.
 *     Tested this against ncsa-telnet 2.3 and pcip_pkt using plip.com (which
 *     contains "version	equ	0" and ";History:562,1" in the firts 2
 *    source-lines						28-Mar-94
 *
 *	Modularised it (Alan Cox). Will upgrade to Niibe's PLIP once its settled
 *	down better.
 *	
 *
 *  This is parallel port packet pusher.  It's actually more general
 *  than the "IP" in its name suggests -- but 'plip' is just such a
 *  great name!
 * 
 *    
 *   Bugs: Please read this: The PLIP driver is a nasty hack and like all nasty hacks 
 *	   has some 'features'.
 *
 *	Can lock machines solid if one end goes down or crashes, or due to cable faults.
 *	Can lock both machines solid on a broadcast collision.
 *	Some laptops don't have all the wires we use.
 *	Doesn't match the original Russ Nelson protocol so won't talk to Amiga or PC drivers.
 *	Waits far too long with interrupts off [X is unbearable, forget action games, xntp is a joke]
 *	Doesn't work on some fast 486DX machines
 *
 *	If it works be thankful, if not fix it!
 *
 *  Info:
 *     	I <Alan> got 15K/second NFS throughput (about 20-25K second IP). I also got some ethernet cards
 *	so don't ask me for help. This code needs a real major rewrite. Any volunteers ?
 *
 ***** So we can all compare loads of different PLIP drivers for a bit I've modularised this beastie too.
 ***** In addition a seperate bidirectional plip module can be done.
 */

static char *version =
    "NET3 "
#ifdef MODULE
    "MODULAR "    
#endif    
    "PLIP.010 (from plip.c:v0.15 for 0.99pl12+, 8/11/93)\n";

#include <linux/config.h>

/*
  Sources:
	Ideas and protocols came from Russ Nelson's (nelson@crynwr.com)
	"parallel.asm" parallel port packet driver.
	TANABE Hiroyasu changes the protocol.
  The "Crynwr" parallel port standard specifies the following protocol:
   send header nibble '8'
   type octet '0xfd' or '0xfc'
   count-low octet
   count-high octet
   ... data octets
   checksum octet
Each octet is sent as <wait for rx. '0x1?'> <send 0x10+(octet&0x0F)>
			<wait for rx. '0x0?'> <send 0x00+((octet>>4)&0x0F)>

The cable used is a de facto standard parallel null cable -- sold as
a "LapLink" cable by various places.  You'll need a 10-conductor cable to
make one yourself.  The wiring is:
    INIT	16 - 16		SLCTIN	17 - 17
    GROUND	25 - 25
    D0->ERROR	2 - 15		15 - 2
    D1->SLCT	3 - 13		13 - 3
    D2->PAPOUT	4 - 12		12 - 4
    D3->ACK	5 - 10		10 - 5
    D4->BUSY	6 - 11		11 - 6
  Do not connect the other pins.  They are
    D5,D6,D7 are 7,8,9
    STROBE is 1, FEED is 14
    extra grounds are 18,19,20,21,22,23,24
*/

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/if_ether.h>
#include <asm/system.h>
#include <asm/io.h>
#include <netinet/in.h>
#include <errno.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#ifdef MODULE
#include <linux/module.h>
#include "../../tools/version.h"
#endif

#ifdef PRINTK
#undef PRINTK
#endif
#ifdef PRINTK2
#undef PRINTK2
#endif

#define PLIP_DEBUG	/* debugging */
#undef  PLIP_DEBUG2	/* debugging with more varbose report */

#ifdef PLIP_DEBUG
#define PRINTK(x) printk x
#else
#define PRINTK(x) /**/
#endif
#ifdef PLIP_DEBUG2
#define PRINTK2(x) printk x
#else
#define PRINTK2(x) /**/
#endif

/* The map from IRQ number (as passed to the interrupt handler) to
   'struct device'. */
extern struct device *irq2dev_map[16];

/* Network statistics, with the same names as 'struct enet_statistics'. */
#define netstats enet_statistics

/* constants */
#define PAR_DATA	0
#define PAR_STATUS	1
#define PAR_CONTROL	2
#define PLIP_MTU 1600
#define PLIP_HEADER_TYPE1 0xfd
#define PLIP_HEADER_TYPE2 0xfc

/* Index to functions, as function prototypes. */
extern int plip_probe(int ioaddr, struct device *dev);
static int plip_open(struct device *dev);
static int plip_close(struct device *dev);
static int plip_tx_packet(struct sk_buff *skb, struct device *dev);
static int plip_header (unsigned char *buff, struct device *dev,
		 unsigned short type, void *dest,
		 void *source, unsigned len, struct sk_buff *skb);

/* variables used internally. */
#define INITIALTIMEOUTFACTOR 4
#define MAXTIMEOUTFACTOR 20
static int timeoutfactor = INITIALTIMEOUTFACTOR;

/* Routines used internally. */
static void plip_device_clear(struct device *dev);
static void plip_receiver_error(struct device *dev);
static void plip_set_physicaladdr(struct device *dev, unsigned long ipaddr);
static int plip_addrcmp(struct ethhdr *eth);
static void cold_sleep(int tics);
static void plip_interrupt(int reg_ptr); /* Dispatch from interrupts. */
static int plip_receive_packet(struct device *dev);
static int plip_send_packet(struct device *dev, unsigned char *buf, int length);
static int plip_send_start(struct device *dev, struct ethhdr *eth);
static void double_timeoutfactor(void);
static struct enet_statistics *plip_get_stats(struct device *dev);

int
plip_init(struct device *dev)
{
    int port_base = dev->base_addr;
    int i;

    /* Check that there is something at base_addr. */
    outb(0x00, port_base + PAR_CONTROL);
    outb(0x55, port_base + PAR_DATA);
    if (inb(port_base + PAR_DATA) != 0x55)
	return -ENODEV;

    /* Alpha testers must have the version number to report bugs. */
#ifdef PLIP_DEBUG
    {
	static int version_shown = 0;
	if (! version_shown)
	    printk(version), version_shown++;
    }
#endif

    /* Initialize the device structure. */
    dev->priv = kmalloc(sizeof(struct netstats), GFP_KERNEL);
    memset(dev->priv, 0, sizeof(struct netstats));

    for (i = 0; i < DEV_NUMBUFFS; i++)
	skb_queue_head_init(&dev->buffs[i]);

    dev->hard_header = &plip_header;
    dev->rebuild_header = eth_rebuild_header;
    dev->type_trans = eth_type_trans;

    dev->open = &plip_open;
    dev->stop = &plip_close;
    dev->hard_start_xmit = &plip_tx_packet;
    dev->get_stats = &plip_get_stats;

    /* These are ethernet specific. */
    dev->type = ARPHRD_ETHER;
    dev->hard_header_len = ETH_HLEN;
    dev->mtu = PLIP_MTU;	/* PLIP may later negotiate max pkt size */
    dev->addr_len = ETH_ALEN;
    for (i = 0; i < dev->addr_len; i++) {
	dev->broadcast[i]=0xff;
	dev->dev_addr[i] = 0;
    }
    printk("%s: configured for parallel port at %#3x, IRQ %d.\n",
	   dev->name, dev->base_addr, dev->irq);

    /* initialize internal value */
    timeoutfactor = INITIALTIMEOUTFACTOR;
    return 0;
}

/* Open/initialize the board.  This is called (in the current kernel)
   sometime after booting when the 'config <dev->name>' program is
   run.

   This routine gets exclusive access to the parallel port by allocating
   its IRQ line.
   */
   
static int plip_open(struct device *dev)
{
    if (dev->irq == 0)
	dev->irq = 7;
    cli();
    if (request_irq(dev->irq , &plip_interrupt) != 0) {
    	sti();
	PRINTK(("%s: couldn't get IRQ %d.\n", dev->name, dev->irq));
	return -EAGAIN;
    }

    irq2dev_map[dev->irq] = dev;
    sti();
    plip_device_clear(dev);
    dev->tbusy = 0;
    dev->interrupt = 0;
    dev->start = 1;
#ifdef MODULE
    MOD_INC_USE_COUNT;
#endif        
    return 0;
}

/* The inverse routine to plip_open(). */
static int
plip_close(struct device *dev)
{
    dev->tbusy = 1;
    dev->start = 0;
    cli();
    free_irq(dev->irq);
    irq2dev_map[dev->irq] = NULL;
    sti();
    outb(0x00, dev->base_addr);		/* Release the interrupt. */
#ifdef MODULE
    MOD_DEC_USE_COUNT;
#endif        
    return 0;
}

static int
plip_tx_packet(struct sk_buff *skb, struct device *dev)
{
    int ret_val;

    if (dev->tbusy || dev->interrupt) {	/* Do timeouts, to avoid hangs. */
	int tickssofar = jiffies - dev->trans_start;
	if (tickssofar < 50)
	    return 1;
	printk("%s: transmit timed out\n", dev->name);
	/* Try to restart the adaptor. */
	plip_device_clear(dev);
	return 0;
    }

    /* If some higher layer thinks we've missed an tx-done interrupt
       we are passed NULL. Caution: dev_tint() handles the cli()/sti()
       itself. */
    if (skb == NULL) {
	dev_tint(dev);
	return 0;
    }

    dev->trans_start = jiffies;
    ret_val = plip_send_packet(dev, skb->data, skb->len);
    if (skb->free)
	kfree_skb (skb, FREE_WRITE);
    dev->tbusy = 0;
    mark_bh (NET_BH);
    return 0/*ret_val*/;
}

static int
plip_header (unsigned char *buff, struct device *dev,
	     unsigned short type, void *daddr	     	     ,
	     void *saddr, unsigned len, struct sk_buff *skb)
{
    if (dev->dev_addr[0] == 0) {
	/* set physical address */
	plip_set_physicaladdr(dev, dev->pa_addr);
    }
    return eth_header(buff, dev, type, daddr, saddr, len, skb);
}

static void
  plip_device_clear(struct device *dev)
{
    dev->interrupt = 0;
    dev->tbusy = 0;
    outb(0x00, dev->base_addr + PAR_DATA);
    outb(0x10, dev->base_addr + PAR_CONTROL);		/* Enable the rx interrupt. */
}

static void
  plip_receiver_error(struct device *dev)
{
    dev->interrupt = 0;
    dev->tbusy = 0;
    outb(0x02, dev->base_addr + PAR_DATA);
    outb(0x10, dev->base_addr + PAR_CONTROL);		/* Enable the rx interrupt. */
}

static int
  get_byte(struct device *dev)
{
    unsigned char val, oldval;
    unsigned char low_nibble;
    int timeout;
    int error = 0;
    val = inb(dev->base_addr + PAR_STATUS);
    timeout = jiffies + timeoutfactor * 2;
    do {
	oldval = val;
	val = inb(dev->base_addr + PAR_STATUS);
	if ( oldval != val ) continue; /* it's unstable */
	if ( timeout < jiffies ) {
	    error++;
	    break;
	}
    } while ( (val & 0x80) );
    val = inb(dev->base_addr + PAR_STATUS);
    low_nibble = (val >> 3) & 0x0f;
    outb(0x10, dev->base_addr + PAR_DATA);
    timeout = jiffies + timeoutfactor * 2;
    do {
	oldval = val;
	val = inb(dev->base_addr + PAR_STATUS);
	if (oldval != val) continue; /* it's unstable */
	if ( timeout < jiffies ) {
	    error++;
	    break;
	}
    } while ( !(val & 0x80) );
    val = inb(dev->base_addr + PAR_STATUS);
    PRINTK2(("%02x %s ", low_nibble | ((val << 1) & 0xf0),
	       error ? "t":""));
    outb(0x00, dev->base_addr + PAR_DATA);
    if (error) {
	/* timeout error */
	double_timeoutfactor();
	return -1;
    }
    return low_nibble | ((val << 1) & 0xf0);
}

/* The typical workload of the driver:
   Handle the parallel port interrupts. */
static void
  plip_interrupt(int reg_ptr)
{
    int irq = -(((struct pt_regs *)reg_ptr)->orig_eax+2);
    struct device *dev = irq2dev_map[irq];
    struct netstats *localstats;

    if (dev == NULL) {
	PRINTK(("plip_interrupt(): irq %d for unknown device.\n", irq));
	return;
    }
    localstats = (struct netstats*) dev->priv;
    if (dev->tbusy || dev->interrupt) return;
    dev->interrupt = 1;
    outb(0x00, dev->base_addr + PAR_CONTROL);  /* Disable the rx interrupt. */
    sti(); /* Allow other interrupts. */
    PRINTK2(("%s: interrupt.  ", dev->name));

    {
	/* check whether the interrupt is valid or not.*/
	int timeout = jiffies + timeoutfactor;
	while ((inb(dev->base_addr + PAR_STATUS) & 0xf8) != 0xc0) {
	    if ( timeout < jiffies ) {
		PRINTK2(("%s: No interrupt (status=%#02x)!\n",
			 dev->name, inb(dev->base_addr + PAR_STATUS)));
		plip_device_clear(dev);
		return;
	    }
	}
    }
    if (plip_receive_packet(dev)) {
	/* get some error while receiving data */
	localstats->rx_errors++;
	plip_receiver_error(dev);
    } else {
	plip_device_clear(dev);
    }
}

static int
plip_receive_packet(struct device *dev)
{
    unsigned length;
    int checksum = 0;
    struct sk_buff *skb;
    struct netstats *localstats;
    struct ethhdr eth;

    localstats = (struct netstats*) dev->priv;
    
    outb(1, dev->base_addr + PAR_DATA);		/* Ack: 'Ready' */

    {
	/* get header octet and length of packet */
        
	length = get_byte(dev);
	length |= get_byte(dev) << 8;
        {
	 int i;
	 unsigned char *eth_p = (unsigned char*)&eth;
	 for ( i = 0; i < sizeof(eth); i++, eth_p++) {
	     *eth_p = get_byte(dev);
	 }
        }
	PRINTK2(("length = %d\n", length));
	if (length > dev->mtu || length < 8) {
	    PRINTK2(("%s: bogus packet size %d.\n", dev->name, length));
	    return 1;
	}
    }
    {
	/* get skb area from kernel and 
	 * set appropriate values to skb
	 */
	skb = alloc_skb(length, GFP_ATOMIC);
	if (skb == NULL) {
	    PRINTK(("%s: Couldn't allocate a sk_buff of size %d.\n",
		    dev->name,length));
	    return 1;
	}
	skb->lock = 0;
    }
    {
	/* phase of receiving the data */
	/* 'skb->data' points to the start of sk_buff data area. */
	unsigned char *buf = skb->data;
	unsigned char *eth_p = (unsigned char *)&eth;
	int i;
	for ( i = 0; i < sizeof(eth); i++) {
	    checksum += *eth_p;
	    *buf++ = *eth_p++;
	}
	for ( i = 0; i < length - sizeof(eth); i++) {
	    unsigned char new_byte = get_byte(dev);
	    checksum += new_byte;
	    *buf++ = new_byte;
	}
	checksum &= 0xff;
	if (checksum != get_byte(dev)) {
	    localstats->rx_crc_errors++;
	    PRINTK(("checksum error\n"));
	    return 1;
	} else if(dev_rint((unsigned char *)skb, length, IN_SKBUFF, dev)) {
	    printk("%s: rcv buff full.\n", dev->name);
	    localstats->rx_dropped++;
	    return 1;
	}
    }
    {
	/* phase of terminating this connection */
	int timeout;

	timeout = jiffies + length * timeoutfactor / 16;
	outb(0x00, dev->base_addr + PAR_DATA);
	/* Wait for the remote end to reset. */
	while ( (inb(dev->base_addr + PAR_STATUS) & 0xf8) != 0x80 ) {
	    if (timeout < jiffies ) {
		double_timeoutfactor();
		PRINTK(("Remote has not reset.\n"));
		break;
	    }
	}
    }
    localstats->rx_packets++;
    return 0;
}


static int send_byte(struct device *dev, unsigned char val)
{
    int timeout;
    int error = 0;
    PRINTK2((" S%02x", val));
    outb((val & 0xf), dev->base_addr); /* this makes data bits more stable */
                                       /* (especially the &0xf :-> PB )    */
    outb(0x10 | (val & 0xf), dev->base_addr);
    timeout = jiffies + timeoutfactor;
    while( inb(dev->base_addr+PAR_STATUS) & 0x80 )
	if ( timeout < jiffies ) {
	    error++;
	    break;
	}
    outb(0x10 | (val >> 4), dev->base_addr);
    outb(val >> 4, dev->base_addr);
    timeout = jiffies + timeoutfactor;
    while( (inb(dev->base_addr+PAR_STATUS) & 0x80) == 0 )
	if ( timeout < jiffies ) {
	    error++;
	    break;
	}
    if (error) {
	/* timeout error */
	double_timeoutfactor();
	PRINTK2(("t"));
	return -1;
    }
    return 0;
}
/*
 * plip_send_start
 * trigger remoto rx interrupt and establish a connection.
 * 
 * return value
 * 0 : establish the connection
 * -1 : connection failed.
 */
static int
plip_send_start(struct device *dev, struct ethhdr *eth)
{	
    int timeout;
    int status;
    int lasttrigger;
    struct netstats *localstats = (struct netstats*) dev->priv;

    /* This starts the packet protocol by triggering a remote IRQ. */
    timeout = jiffies + timeoutfactor * 16;
    lasttrigger = jiffies;
    while ( ((status = inb(dev->base_addr+PAR_STATUS)) & 0x08) == 0 ) {
	dev->tbusy = 1;
	outb(0x00, dev->base_addr + PAR_CONTROL); /* Disable my rx intr. */
	outb(0x08, dev->base_addr + PAR_DATA); 	/* Trigger remote rx intr. */
	if (status & 0x40) {
	    /* The remote end is also trying to send a packet.
	     * Only one end may go to the receiving phase,
	     * so we use the "ethernet" address (set from the IP address)
	     * to determine which end dominates.
	     */
	    if ( plip_addrcmp(eth) > 0 ) {
		localstats->collisions++;
		PRINTK2(("both ends are trying to send a packet.\n"));
		if (plip_receive_packet(dev)) {
		    /* get some error while receiving data */
		    localstats->rx_errors++;
		    outb(0x02, dev->base_addr + PAR_DATA);
		} else {
		    outb(0x00, dev->base_addr + PAR_DATA);
		}
		cold_sleep(2); /* make sure that remote end is ready */
	    }
	    continue; /* restart send sequence */
	}
	if (lasttrigger != jiffies) {
	    /* trigger again */
	    outb(0x00, dev->base_addr + PAR_DATA);
	    cold_sleep(1);
	    lasttrigger = jiffies;
	}
	if (timeout < jiffies) {
	    double_timeoutfactor();
	    plip_device_clear(dev);
	    localstats->tx_errors++;
	    PRINTK(("%s: Connect failed in send_packet().\n",
		    dev->name));
	    /* We failed to send the packet.  To emulate the ethernet we
	       should pretent the send worked fine */
	    return -1;
	}
    }
    return 0;
}
static int
plip_send_packet(struct device *dev, unsigned char *buf, int length)
{
    int error = 0;
    struct netstats *localstats;

    PRINTK2(("%s: plip_send_packet(%d) %02x %02x %02x %02x %02x...",
	   dev->name, length, buf[0], buf[1], buf[2], buf[3], buf[4]));
    if (length > dev->mtu) {
	printk("%s: packet too big, %d.\n", dev->name, length);
	return 0;
    }
    localstats = (struct netstats*) dev->priv;

    {
	/* phase of checking remote status */
	int i;
	int timeout = jiffies + timeoutfactor * 8;
	while ( (i = (inb(dev->base_addr+PAR_STATUS) & 0xe8)) != 0x80 ) {
	    if (i == 0x78) {
		/* probably cable is not connected */
		/* Implementation Note:
		 * This status should result in 'Network unreachable'.
		 * but I don't know the way.
		 */
		return 0;
	    }
	    if (timeout < jiffies) {
		/* remote end is not ready */
		double_timeoutfactor();
		localstats->tx_errors++;
		PRINTK(("remote end is not ready.\n"));
		return 1; /* Failed to send the packet */
	    }
	}
    }
    /* phase of making a connection */
    if (plip_send_start(dev, (struct ethhdr *)buf) < 0)
	return 1;

    {
	/* send packet's length 
	   the byte order has changed now and then. Today it's sent as in 
           the original crynwr-plip ...
           Gruss PB
	 */
	send_byte(dev, length); 
        send_byte(dev, length >> 8);
    }
    {
	/* phase of sending data */
	int i;
	int checksum = 0;

	for ( i = 0; i < sizeof(struct ethhdr); i++ ) {
            send_byte(dev, *buf);
	    checksum += *buf++;
	}

	for (i = 0; i < length - sizeof(struct ethhdr); i++) {
	    checksum += buf[i];
	    if (send_byte(dev, buf[i]) < 0) {
		error++;
		break;
	    }
	}
	send_byte(dev, checksum & 0xff);
    }
    {
	/* phase of terminating this connection */
	int timeout;
	
	outb(0x00, dev->base_addr + PAR_DATA);
	/* Wait for the remote end to reset. */
	timeout = jiffies + ((length * timeoutfactor) >> 4);
	while ((inb(dev->base_addr + PAR_STATUS) & 0xe8) != 0x80) {
	    if (timeout < jiffies ) {	
		double_timeoutfactor();
		PRINTK(("Remote end has not reset.\n"));
		error++;
		break;
	    }
	}
	if (inb(dev->base_addr + PAR_STATUS) & 0x10) {
	    /* receiver reports error */
	    error++;
	}
    }
    plip_device_clear(dev);
    localstats->tx_packets++;
    PRINTK2(("plip_send_packet(%d) done.\n", length));
    return error?1:0;
}

/*
 * some trivial functions
 */
static void
plip_set_physicaladdr(struct device *dev, unsigned long ipaddr)
{
    /*
     * set physical address to
     *  0xfd.0xfd.ipaddr
     */

    unsigned char *addr = dev->dev_addr;
    int i;

    if ((ipaddr >> 24) == 0 || (ipaddr >> 24) == 0xff) return;
    PRINTK2(("%s: set physical address to %08x\n", dev->name, ipaddr));
    for (i=0; i < ETH_ALEN - sizeof(unsigned long); i++) {
	addr[i] = 0xfd;
    }
    memcpy(&(addr[i]), &ipaddr, sizeof(unsigned long));
}

static int
plip_addrcmp(struct ethhdr *eth)
{
    int i;
    for ( i = ETH_ALEN - 1; i >= 0; i-- ) {
	if (eth->h_dest[i] > eth->h_source[i]) return -1;
	if (eth->h_dest[i] < eth->h_source[i]) return 1;
    }
    PRINTK2(("h_dest = %08x%04x h_source = %08x%04x\n",
	    *(long*)&eth->h_dest[2],*(short*)&eth->h_dest[0],
	    *(long*)&eth->h_source[2],*(short*)&eth->h_source[0]));
    return 0;
}

/* This function is evil, evil, evil.  This should be a
   _kernel_, rescheduling sleep!. */
static void
cold_sleep(int tics)
{
    int start = jiffies;
    while(jiffies < start + tics)
	; /* do nothing */
    return;
}

static void
  double_timeoutfactor()
{
    timeoutfactor *= 2;
    if (timeoutfactor >= MAXTIMEOUTFACTOR) {
	timeoutfactor = MAXTIMEOUTFACTOR;
    }
    return;
}

static struct enet_statistics *
plip_get_stats(struct device *dev)
{
    struct netstats *localstats = (struct netstats*) dev->priv;
    return localstats;
}

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -Wall -O6 -fomit-frame-pointer -x c++ -c plip.c"
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */

#ifdef MODULE
char kernel_version[] = UTS_RELEASE;

static struct device dev_plip0 = 
{
	"plip0" /*"plip"*/,
	0, 0, 0, 0,		/* memory */
	0x3BC, 5,		/* base, irq */
	0, 0, 0, NULL, plip_init 
};

static struct device dev_plip1 = 
{
	"plip1" /*"plip"*/,
	0, 0, 0, 0,		/* memory */
	0x378, 7,		/* base, irq */
	0, 0, 0, NULL, plip_init 
};

static struct device dev_plip2 = 
{
	"plip2" /*"plip"*/,
	0, 0, 0, 0,		/* memory */
	0x278, 2,		/* base, irq */
	0, 0, 0, NULL, plip_init 
};

int
init_module(void)
{
	int err;

	if ( ((err=register_netdev(&dev_plip0)) == 0) &&
	     ((err=register_netdev(&dev_plip1)) == 0) &&
	     ((err=register_netdev(&dev_plip2)) == 0)
	   )
	{
		if(err==-EEXIST)
			printk("plip devices already present. Module not loaded.\n");
		return err;
	}
	return 0;
}

void
cleanup_module(void)
{
	if (MOD_IN_USE)
		printk("plip: device busy, remove delayed\n");
	else
	{
		unregister_netdev(&dev_plip0);
		if(dev_plip0.priv)
		{
			kfree_s(dev_plip0.priv,sizeof(struct netstats));
			dev_plip0.priv=NULL;
		}
		unregister_netdev(&dev_plip1);
		if(dev_plip1.priv)
		{
			kfree_s(dev_plip1.priv,sizeof(struct netstats));
			dev_plip0.priv=NULL;
		}
		unregister_netdev(&dev_plip2);
		if(dev_plip2.priv)
		{
			kfree_s(dev_plip2.priv,sizeof(struct netstats));
			dev_plip2.priv=NULL;
		}
	}
}
#endif /* MODULE */
