/*
 *	buslogic.c	(C) 1993 David B. Gentzel
 *	Low-level scsi driver for BusLogic adapters
 *	by David B. Gentzel, Whitfield Software Services, Carnegie, PA
 *	    (gentzel@nova.enet.dec.com)
 *	Thanks to BusLogic for providing the necessary documentation
 *
 *	The original version of this driver was derived from aha1542.[ch] which
 *	is Copyright (C) 1992 Tommy Thorn.  Much has been reworked, but most of
 *	basic structure and substantial chunks of code still remain.
 */

/*
 * TODO:
 *	1. Cleanup error handling & reporting.
 *	2. Find out why scatter/gather is limited to 16 requests per command.
 *	3. Add multiple outstanding requests.
 *	4. See if we can make good use of having more than one command per lun.
 *	5. Test/improve/fix abort & reset functions.
 *	6. Look at command linking.
 */

/*
 * NOTES:
 *    BusLogic (formerly BusTek) manufactures an extensive family of
 *    intelligent, high performance SCSI-2 host adapters.  They all support
 *    command queueing and scatter/gather I/O.  Most importantly, they all
 *    support identical programming interfaces, so a single driver can be used
 *    for all boards.
 *
 *    Actually, they all support TWO identical programming interfaces!  They
 *    have an Adaptec 154x compatible interface (complete with 24 bit
 *    addresses) as well as a "native" 32 bit interface.  As such, the Linux
 *    aha1542 driver can be used to drive them, but with less than optimal
 *    performance (at least for the EISA, VESA, and MCA boards).
 *
 *    Here is the scoop on the various models:
 *	BT-542B - ISA first-party DMA with floppy support.
 *	BT-545S - 542B + FAST SCSI and active termination.
 *	BT-545D - 545S + differential termination.
 *	BT-445S - VESA bus-master FAST SCSI with active termination and floppy
 *		  support.
 *	BT-640A - MCA bus-master with floppy support.
 *	BT-646S - 640A + FAST SCSI and active termination.
 *	BT-646D - 646S + differential termination.
 *	BT-742A - EISA bus-master with floppy support.
 *	BT-747S - 742A + FAST SCSI, active termination, and 2.88M floppy.
 *	BT-747D - 747S + differential termination.
 *	BT-757S - 747S + WIDE SCSI.
 *	BT-757D - 747D + WIDE SCSI.
 *
 *    Should you require further information on any of these boards, BusLogic
 *    can be reached at (408)492-9090.
 *
 *    This driver SHOULD support all of these boards.  It has only been tested
 *    with a 747S.  An earlier version was tested with a 445S.
 *
 *    Places flagged with a triple question-mark are things which are either
 *    unfinished, questionable, or wrong.
 */

#include <linux/string.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/types.h>
#include <linux/ioport.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/dma.h>

#include "../block/blk.h"
#include "scsi.h"
#include "hosts.h"
# include "sd.h"
#define BUSLOGIC_PRIVATE_H	/* Get the "private" stuff */
#include "buslogic.h"

#ifndef BUSLOGIC_DEBUG
# define BUSLOGIC_DEBUG UD_ABORT
#endif

#define BUSLOGIC_VERSION "1.00"

/* ??? This *MAY* work to properly report the geometry of disks > 1G when the
   alternate geometry is enabled on the host adapter.  It is completely
   untested as I have no such disk to experiment with.  I rarely refuse gifts,
   however... */
/* Check out the stuff in aha1542.c - is this the same as how buslogic does
   it? - ERY */
/* ??? Not Yet Implemented */
/*#ifdef BUSLOGIC_ALTERNATE_MAPPING*/

/* Not a random value - if this is too large, the system hangs for a long time
   waiting for something to happen if a board is not installed. */
#define WAITNEXTTIMEOUT 3000000

/* This is for the scsi_malloc call in buslogic_queuecommand. */
/* ??? I'd up this to 4096, but would we be in danger of using up the
   scsi_malloc memory pool? */
/* This could be a concern, I guess.  It may be possible to fix things so that
   the table generated in sd.c is compatible with the low-level code, but
   don't hold your breath.  -ERY */
#define BUSLOGIC_SG_MALLOC 512

/* Since the SG list is malloced, we have to limit the length. */
#define BUSLOGIC_MAX_SG (BUSLOGIC_SG_MALLOC / sizeof (struct chain))

/* The DMA-Controller.  We need to fool with this because we want to be able to
   use an ISA BusLogic without having to have the BIOS enabled. */
#define DMA_MODE_REG 0xD6
#define DMA_MASK_REG 0xD4
#define	CASCADE 0xC0

#define BUSLOGIC_MAILBOXES 16	/* ??? Arbitrary? */

/* BusLogic boards can be configured for quite a number of port addresses (six
   to be exact), but I generally do not want the driver poking around at
   random.  We allow two port addresses - this allows people to use a BusLogic
   with a MIDI card, which frequently also used 0x330.  If different port
   addresses are needed (e.g. to install more than two cards), you must define
   BUSLOGIC_PORT_OVERRIDE to be a list of the addresses which will be checked.
   This can also be used to resolve a conflict if the port-probing at a
   standard port causes problems with another board. */
static const unsigned int bases[] = {
#ifdef BUSLOGIC_PORT_OVERRIDE
    BUSLOGIC_PORT_OVERRIDE
#else
    0x330, 0x334, /* 0x130, 0x134, 0x230, 0x234 */
#endif
};

#define BIOS_TRANSLATION_6432 1		/* Default case */
#define BIOS_TRANSLATION_25563 2	/* Big disk case */

struct hostdata {
    unsigned char bus_type;
    int bios_translation;	/* Mapping bios uses - for compatibility */
    size_t last_mbi_used;
    size_t last_mbo_used;
    Scsi_Cmnd *SCint[BUSLOGIC_MAILBOXES];
    struct mailbox mb[2 * BUSLOGIC_MAILBOXES];
    struct ccb ccbs[BUSLOGIC_MAILBOXES];
};

#define HOSTDATA(host) ((struct hostdata *)&(host)->hostdata)

/* One for each IRQ level (9-15), although 13 will never be used. */
static struct Scsi_Host *host[7] = { NULL, };

static int setup_mailboxes(unsigned int base, struct Scsi_Host *SHpnt);

#define INTR_RESET(base) outb(RINT, CONTROL(base))

#define buslogic_printk buslogic_prefix(),printk

#define CHECK(cond) if (cond) ; else goto fail

#define WAIT(port, mask, allof, noneof) CHECK(wait(port, mask, allof, noneof))
#define WAIT_WHILE(port, mask) WAIT(port, mask, 0, mask)
#define WAIT_UNTIL(port, mask) WAIT(port, mask, mask, 0)

static __inline__ int wait(unsigned short port, unsigned char mask,
			   unsigned char allof, unsigned char noneof)
{
    int bits;
    unsigned int timeout = WAITNEXTTIMEOUT;

    for (;;) {
	bits = inb(port) & mask;
	if ((bits & allof) == allof && (bits & noneof) == 0)
	    break;
	if (--timeout == 0)
	    return FALSE;
    }
    return TRUE;
}

static void buslogic_prefix(void)
{
    printk("BusLogic SCSI: ");
}

#if 0
static void buslogic_stat(unsigned int base)
{
    int s = inb(STATUS(base)), i = inb(INTERRUPT(base));

    printk("status=%02X intrflags=%02X\n", s, i);
}
#else
# define buslogic_stat(base)
#endif

/* This is a bit complicated, but we need to make sure that an interrupt
   routine does not send something out while we are in the middle of this.
   Fortunately, it is only at boot time that multi-byte messages are ever
   sent. */
static int buslogic_out(unsigned int base, const unsigned char *cmdp, size_t len)
{
    if (len == 1) {
	for (;;) {
	    WAIT_WHILE(STATUS(base), CPRBSY);
	    cli();
	    if (!(inb(STATUS(base)) & CPRBSY)) {
		outb(*cmdp, COMMAND_PARAMETER(base));
		sti();
		return FALSE;
	    }
	    sti();
	}
    } else {
	cli();
	while (len--) {
	    WAIT_WHILE(STATUS(base), CPRBSY);
	    outb(*cmdp++, COMMAND_PARAMETER(base));
	}
	sti();
    }
    return FALSE;
  fail:
    sti();
    buslogic_printk("buslogic_out failed(%u): ", len + 1);
    buslogic_stat(base);
    return TRUE;
}

static int buslogic_in(unsigned int base, unsigned char *cmdp, size_t len)
{
    cli();
    while (len--) {
	WAIT_UNTIL(STATUS(base), DIRRDY);
	*cmdp++ = inb(DATA_IN(base));
    }
    sti();
    return FALSE;
  fail:
    sti();
    buslogic_printk("buslogic_in failed(%u): ", len + 1);
    buslogic_stat(base);
    return TRUE;
}

static unsigned int makecode(unsigned int hosterr, unsigned int scsierr)
{
    switch (hosterr) {
      case 0x00:	/* Normal completion. */
      case 0x0A:	/* Linked command complete without error and linked
			   normally. */
      case 0x0B:	/* Linked command complete without error, interrupt
			   generated. */
	hosterr = DID_OK;
	break;

      case 0x11:	/* Selection time out: the initiator selection or
			   target reselection was not complete within the SCSI
			   Time out period. */
	hosterr = DID_TIME_OUT;
	break;

      case 0x14:	/* Target bus phase sequence failure - An invalid bus
			   phase or bus phase sequence was requested by the
			   target.  The host adapter will generate a SCSI
			   Reset Condition, notifying the host with a RSTS
			   interrupt. */
	hosterr = DID_RESET;	/* ??? Is this right? */
	break;

      case 0x12:	/* Data overrun/underrun: the target attempted to
			   transfer more data than was allocated by the Data
			   Length field or the sum of the Scatter/Gather Data
			   Length fields. */
      case 0x13:	/* Unexpected bus free - The target dropped the SCSI
			   BSY at an unexpected time. */
      case 0x15:	/* MBO command was not 00, 01, or 02 - The first byte
			   of the MB was invalid.  This usually indicates a
			   software failure. */
      case 0x16:	/* Invalid CCB Operation Code - The first byte of the
			   CCB was invalid.  This usually indicates a software
			   failure. */
      case 0x17:	/* Linked CCB does not have the same LUN - A
			   subsequent CCB of a set of linked CCB's does not
			   specify the same logical unit number as the
			   first. */
      case 0x18:	/* Invalid Target Direction received from Host - The
			   direction of a Target Mode CCB was invalid. */
      case 0x19:	/* Duplicate CCB Received in Target Mode - More than
			   once CCB was received to service data transfer
			   between the same target LUN and initiator SCSI ID
			   in the same direction. */
      case 0x1A:	/* Invalid CCB or Segment List Parameter - A segment
			   list with a zero length segment or invalid segment
			   list boundaries was received.  A CCB parameter was
			   invalid. */
      case 0x1B:	/* Auto request sense failed. */
      case 0x1C:	/* SCSI-2 tagged queueing message was rejected by the
			   target. */
      case 0x20:	/* The host adapter hardware failed. */
      case 0x21:	/* The target did not respond to SCSI ATN and the host
			   adapter consequently issued a SCSI bus reset to
			   clear up the failure. */
      case 0x22:	/* The host adapter asserted a SCSI bus reset. */
      case 0x23:	/* Other SCSI devices asserted a SCSI bus reset. */
#if BUSLOGIC_DEBUG
	buslogic_printk("%X %X\n", hosterr, scsierr);
#endif
	hosterr = DID_ERROR;	/* ??? Couldn't find any better. */
	break;

      default:
	buslogic_printk("makecode: unknown hoststatus %X\n", hosterr);
	break;
    }
    return (hosterr << 16) | scsierr;
}

static int test_port(unsigned int base, struct Scsi_Host *SHpnt)
{
    unsigned int i;
    unsigned char inquiry_cmd[] = { CMD_INQUIRY };
    unsigned char inquiry_result[4];
    unsigned char *cmdp;
    int len;
    volatile int debug = 0;

    /* Quick and dirty test for presence of the card. */
    if (inb(STATUS(base)) == 0xFF)
	return TRUE;

    /* Reset the adapter.  I ought to make a hard reset, but it's not really
       necessary. */

#if BUSLOGIC_DEBUG
    buslogic_printk("test_port called\n");
#endif

    outb(RSOFT | RINT/* | RSBUS*/, CONTROL(base));

    /* Wait a little bit for things to settle down. */
    i = jiffies + 2;
    while (i > jiffies);

    debug = 1;
    /* Expect INREQ and HARDY, any of the others are bad. */
    WAIT(STATUS(base), STATMASK, INREQ | HARDY,
	 DACT | DFAIL | CMDINV | DIRRDY | CPRBSY);

    debug = 2;
    /* Shouldn't have generated any interrupts during reset. */
    if (inb(INTERRUPT(base)) & INTRMASK)
	goto fail;

    /* Perform a host adapter inquiry instead so we do not need to set up the
       mailboxes ahead of time. */
    buslogic_out(base, inquiry_cmd, 1);

    debug = 3;
    len = 4;
    cmdp = &inquiry_result[0];
    while (len--) {
	WAIT(STATUS(base), DIRRDY, DIRRDY, 0);
	*cmdp++ = inb(DATA_IN(base));
    }

    debug = 4;
    /* Reading port should reset DIRRDY. */
    if (inb(STATUS(base)) & DIRRDY)
	goto fail;

    debug = 5;
    /* When CMDC, command is completed, and we're though testing. */
    WAIT_UNTIL(INTERRUPT(base), CMDC);

    /* now initialize adapter. */

    debug = 6;
    /* Clear interrupts. */
    outb(RINT, CONTROL(base));

    debug = 7;

    return FALSE;				/* 0 = ok */
  fail:
    return TRUE;				/* 1 = not ok */
}

const char *buslogic_info(void)
{
    return "BusLogic SCSI Driver version " BUSLOGIC_VERSION;
}

/* A "high" level interrupt handler. */
static void buslogic_interrupt(int junk)
{
    void (*my_done)(Scsi_Cmnd *) = NULL;
    int errstatus, mbistatus = 0, number_serviced, found;
    size_t mbi, mbo = 0;
    struct Scsi_Host *SHpnt;
    Scsi_Cmnd *SCtmp;
    int irqno, base;
    struct mailbox *mb;
    struct ccb *ccb;

    /* Magic - this -2 is only required for slow interrupt handlers */
    irqno = ((int *)junk)[-2];

    SHpnt = host[irqno - 9];
    if (!SHpnt)
	panic("buslogic.c: NULL SCSI host entry");

    mb = HOSTDATA(SHpnt)->mb;
    ccb = HOSTDATA(SHpnt)->ccbs;
    base = SHpnt->io_port;

#if BUSLOGIC_DEBUG
    {
	int flag = inb(INTERRUPT(base));

	buslogic_printk("buslogic_interrupt: ");
	if (!(flag & INTV))
	    printk("no interrupt? ");
	if (flag & IMBL)
	    printk("IMBL ");
	if (flag & MBOR)
	    printk("MBOR ");
	if (flag & CMDC)
	    printk("CMDC ");
	if (flag & RSTS)
	    printk("RSTS ");
	printk("status %02X\n", inb(STATUS(base)));
    }
#endif

    number_serviced = 0;

    for (;;) {
	INTR_RESET(base);

	cli();

	mbi = HOSTDATA(SHpnt)->last_mbi_used + 1;
	if (mbi >= 2 * BUSLOGIC_MAILBOXES)
	    mbi = BUSLOGIC_MAILBOXES;

	/* I use the "found" variable as I like to keep cli/sti pairs at the
	   same block level.  Debugging dropped sti's is no fun... */

	found = FALSE;
	do {
	    if (mb[mbi].status != MBX_NOT_IN_USE) {
		found = TRUE;
		break;
	    }
	    mbi++;
	    if (mbi >= 2 * BUSLOGIC_MAILBOXES)
		mbi = BUSLOGIC_MAILBOXES;
	} while (mbi != HOSTDATA(SHpnt)->last_mbi_used);

	if (found) {
	    mbo = (struct ccb *)mb[mbi].ccbptr - ccb;
	    mbistatus = mb[mbi].status;
	    mb[mbi].status = MBX_NOT_IN_USE;
	    HOSTDATA(SHpnt)->last_mbi_used = mbi;
	}

	sti();

	if (!found) {
	    /* Hmm, no mail.  Must have read it the last time around. */
	    if (number_serviced)
		return;
	    buslogic_printk("interrupt received, but no mail\n");
	    return;
	}

#if BUSLOGIC_DEBUG
	if (ccb[mbo].tarstat || ccb[mbo].hastat)
	    buslogic_printk("buslogic_interrupt: returning %08X (status %d)\n",
			    ((int)ccb[mbo].hastat << 16) | ccb[mbo].tarstat,
			    mb[mbi].status);
#endif

	if (mbistatus == 0x03)	/* ??? 0x03 == Aborted CCB not found. */
	    continue;

#if BUSLOGIC_DEBUG
	buslogic_printk("...done %u %u\n", mbo, mbi);
#endif

	SCtmp = HOSTDATA(SHpnt)->SCint[mbo];

	if (!SCtmp || !SCtmp->scsi_done) {
	    buslogic_printk("buslogic_interrupt: Unexpected interrupt\n");
	    return;
	}

	my_done = SCtmp->scsi_done;
	if (SCtmp->host_scribble)
	    scsi_free(SCtmp->host_scribble, BUSLOGIC_SG_MALLOC);

	/* ??? more error checking left out here */
	if (mbistatus != 1)
	    /* ??? This is surely wrong, but I don't know what's right. */
	    errstatus = makecode(ccb[mbo].hastat, ccb[mbo].tarstat);
	else
	    errstatus = 0;

#if BUSLOGIC_DEBUG
	if (errstatus)
	    buslogic_printk("error: %08X %04X %04X\n",
			    errstatus, ccb[mbo].hastat, ccb[mbo].tarstat);

	if (status_byte(ccb[mbo].tarstat) == CHECK_CONDITION) {
	    size_t i;

	    buslogic_printk("buslogic_interrupt: sense: ");
	    for (i = 0; i < sizeof SCtmp->sense_buffer; i++)
		printk(" %02X", SCtmp->sense_buffer[i]);
	    printk("\n");
	}

	if (errstatus)
	    buslogic_printk("buslogic_interrupt: returning %08X\n", errstatus);
#endif

	SCtmp->result = errstatus;
	HOSTDATA(SHpnt)->SCint[mbo] = NULL;	/* This effectively frees up
						   the mailbox slot, as far as
						   queuecommand is concerned. */
	my_done(SCtmp);
	number_serviced++;
    }
}

int buslogic_queuecommand(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
{
    static const unsigned char buscmd[] = { CMD_START_SCSI };
    unsigned char direction;
    unsigned char *cmd = (unsigned char *)SCpnt->cmnd;
    unsigned char target = SCpnt->target;
    unsigned char lun = SCpnt->lun;
    void *buff = SCpnt->request_buffer;
    int bufflen = SCpnt->request_bufflen;
    int mbo;
    struct mailbox *mb;
    struct ccb *ccb;

#if BUSLOGIC_DEBUG
    if (target > 1) {
	SCpnt->result = DID_TIME_OUT << 16;
	done(SCpnt);
	return 0;
    }
#endif

    if (*cmd == REQUEST_SENSE) {
#ifndef DEBUG
	if (bufflen != sizeof SCpnt->sense_buffer) {
	    buslogic_printk("Wrong buffer length supplied for request sense (%d)\n",
			    bufflen);
	}
#endif
	SCpnt->result = 0;
	done(SCpnt);
	return 0;
    }

#if BUSLOGIC_DEBUG
    {
	int i;

	if (*cmd == READ_10 || *cmd == WRITE_10)
	    i = xscsi2int(cmd + 2);
	else if (*cmd == READ_6 || *cmd == WRITE_6)
	    i = scsi2int(cmd + 2);
	else
	    i = -1;
	buslogic_printk("buslogic_queuecommand: dev %d cmd %02X pos %d len %d ",
			target, *cmd, i, bufflen);
	buslogic_stat(SCpnt->host->io_port);
	buslogic_printk("buslogic_queuecommand: dumping scsi cmd: ");
	for (i = 0; i < (COMMAND_SIZE(*cmd)); i++)
	    printk(" %02X", cmd[i]);
	printk("\n");
	if (*cmd == WRITE_10 || *cmd == WRITE_6)
	    return 0;	/* we are still testing, so *don't* write */
    }
#endif

    mb = HOSTDATA(SCpnt->host)->mb;
    ccb = HOSTDATA(SCpnt->host)->ccbs;

    /* Use the outgoing mailboxes in a round-robin fashion, because this
       is how the host adapter will scan for them. */

    cli();

    mbo = HOSTDATA(SCpnt->host)->last_mbo_used + 1;
    if (mbo >= BUSLOGIC_MAILBOXES)
	mbo = 0;

    do {
	if (mb[mbo].status == MBX_NOT_IN_USE
	    && HOSTDATA(SCpnt->host)->SCint[mbo] == NULL)
	    break;
	mbo++;
	if (mbo >= BUSLOGIC_MAILBOXES)
	    mbo = 0;
    } while (mbo != HOSTDATA(SCpnt->host)->last_mbo_used);

    if (mb[mbo].status != MBX_NOT_IN_USE || HOSTDATA(SCpnt->host)->SCint[mbo]) {
	/* ??? Instead of panicing, we should enable OMBR interrupts and
	   sleep until we get one. */
	panic("buslogic.c: unable to find empty mailbox");
    }

    HOSTDATA(SCpnt->host)->SCint[mbo] = SCpnt;	/* This will effectively
						   prevent someone else from
						   screwing with this cdb. */

    HOSTDATA(SCpnt->host)->last_mbo_used = mbo;

    sti();

#if BUSLOGIC_DEBUG
    buslogic_printk("sending command (%d %08X)...", mbo, done);
#endif

    /* This gets trashed for some reason */
    mb[mbo].ccbptr = &ccb[mbo];

    memset(&ccb[mbo], 0, sizeof (struct ccb));

    ccb[mbo].cdblen = COMMAND_SIZE(*cmd);	/* SCSI Command Descriptor
						   Block Length */

    direction = 0;
    if (*cmd == READ_10 || *cmd == READ_6)
	direction = 8;
    else if (*cmd == WRITE_10 || *cmd == WRITE_6)
	direction = 16;

    memcpy(ccb[mbo].cdb, cmd, ccb[mbo].cdblen);

    if (SCpnt->use_sg) {
	struct scatterlist *sgpnt;
	struct chain *cptr;
	size_t i;

	ccb[mbo].op = CCB_OP_INIT_SG;	/* SCSI Initiator Command w/scatter-gather */
	SCpnt->host_scribble = (unsigned char *)scsi_malloc(BUSLOGIC_SG_MALLOC);
	if (SCpnt->host_scribble == NULL)
	    panic("buslogic.c: unable to allocate DMA memory");
	sgpnt = (struct scatterlist *)SCpnt->request_buffer;
	cptr = (struct chain *)SCpnt->host_scribble;
	if (SCpnt->use_sg > SCpnt->host->sg_tablesize) {
	    buslogic_printk("buslogic_queuecommand bad segment list, %d > %d\n",
			    SCpnt->use_sg, SCpnt->host->sg_tablesize);
	    panic("buslogic.c: bad segment list");
	}
	for (i = 0; i < SCpnt->use_sg; i++) {
	    cptr[i].dataptr = sgpnt[i].address;
	    cptr[i].datalen = sgpnt[i].length;
	}
	ccb[mbo].datalen = SCpnt->use_sg * sizeof (struct chain);
	ccb[mbo].dataptr = cptr;
#if BUSLOGIC_DEBUG
	{
	    unsigned char *ptr;

	    buslogic_printk("cptr %08X: ", cptr);
	    ptr = (unsigned char *)cptr;
	    for (i = 0; i < 18; i++)
		printk(" %02X", ptr[i]);
	    printk("\n");
	}
#endif
    } else {
	ccb[mbo].op = CCB_OP_INIT;	/* SCSI Initiator Command */
	SCpnt->host_scribble = NULL;
	ccb[mbo].datalen = bufflen;
	ccb[mbo].dataptr = buff;
    }
    ccb[mbo].id = target;
    ccb[mbo].lun = lun;
    ccb[mbo].dir = direction;
    ccb[mbo].rsalen = sizeof SCpnt->sense_buffer;
    ccb[mbo].senseptr = SCpnt->sense_buffer;
    ccb[mbo].linkptr = NULL;
    ccb[mbo].commlinkid = 0;

#if BUSLOGIC_DEBUG
    {
	size_t i;

	buslogic_printk("buslogic_queuecommand: sending...");
	for (i = 0; i < sizeof ccb[mbo]; i++)
	    printk(" %02X", ((unsigned char *)&ccb[mbo])[i]);
	printk("\n");
    }
#endif

    if (done) {
#if BUSLOGIC_DEBUG
	buslogic_printk("buslogic_queuecommand: now waiting for interrupt: ");
	buslogic_stat(SCpnt->host->io_port);
#endif
	SCpnt->scsi_done = done;
	mb[mbo].status = MBX_ACTION_START;
	/* start scsi command */
	buslogic_out(SCpnt->host->io_port, buscmd, sizeof buscmd);
#if BUSLOGIC_DEBUG
	buslogic_printk("buslogic_queuecommand: status: ");
	buslogic_stat(SCpnt->host->io_port);
#endif
    } else
	buslogic_printk("buslogic_queuecommand: done can't be NULL\n");

    return 0;
}

#if 0
static void internal_done(Scsi_Cmnd *SCpnt)
{
    SCpnt->SCp.Status++;
}

int buslogic_command(Scsi_Cmnd *SCpnt)
{
#if BUSLOGIC_DEBUG
    buslogic_printk("buslogic_command: ..calling buslogic_queuecommand\n");
#endif

    buslogic_queuecommand(SCpnt, internal_done);

    SCpnt->SCp.Status = 0;
    while (!SCpnt->SCp.Status)
	continue;
    return SCpnt->result;
    return internal_done_errcode;
}
#endif

/* Initialize mailboxes. */
static int setup_mailboxes(unsigned int base, struct Scsi_Host *SHpnt)
{
    size_t i;
    int ok = FALSE;		/* Innocent until proven guilty... */
    struct mailbox *mb = HOSTDATA(SHpnt)->mb;
    struct ccb *ccb = HOSTDATA(SHpnt)->ccbs;
    struct {
	unsigned char cmd, count;
	void *base PACKED;
    } cmd = { CMD_INITEXTMB, BUSLOGIC_MAILBOXES, mb };

    for (i = 0; i < BUSLOGIC_MAILBOXES; i++) {
	mb[i].status = mb[BUSLOGIC_MAILBOXES + i].status = MBX_NOT_IN_USE;
	mb[i].ccbptr = &ccb[i];
    }
    INTR_RESET(base);	/* reset interrupts, so they don't block */

    /* If this fails, this must be an Adaptec board */
    if (buslogic_out(base, (unsigned char *)&cmd, sizeof cmd))
	goto must_be_adaptec;

    /* Wait until host adapter is done messing around, and then check to see
       if the command was accepted.  If it failed, this must be an Adaptec
       board. */
    WAIT_UNTIL(STATUS(base), HARDY);
    if (inb(STATUS(base)) & CMDINV)
	goto must_be_adaptec;

    WAIT_UNTIL(INTERRUPT(base), CMDC);
    while (0) {
      fail:
	buslogic_printk("buslogic_detect: failed setting up mailboxes\n");
    }
    ok = TRUE;
    return ok;
  must_be_adaptec:
    INTR_RESET(base);
    printk("- must be Adaptec\n"); /* So that the adaptec detect looks clean */
    return ok;
}

static int getconfig(unsigned int base, unsigned char *irq,
		     unsigned char *dma, unsigned char *id,
		     unsigned char *bus_type, unsigned short *max_sg)
{
    unsigned char inquiry_cmd[2];
    unsigned char inquiry_result[4];
    int i;

    i = inb(STATUS(base));
    if (i & DIRRDY)
	i = inb(DATA_IN(base));
    inquiry_cmd[0] = CMD_RETCONF;
    buslogic_out(base, inquiry_cmd, 1);
    buslogic_in(base, inquiry_result, 3);
    WAIT_UNTIL(INTERRUPT(base), CMDC);
    INTR_RESET(base);
    /* Defer using the DMA value until we know the bus type. */
    *dma = inquiry_result[0];
    switch (inquiry_result[1]) {
      case 0x01:
	*irq = 9;
	break;
      case 0x02:
	*irq = 10;
	break;
      case 0x04:
	*irq = 11;
	break;
      case 0x08:
	*irq = 12;
	break;
      case 0x20:
	*irq = 14;
	break;
      case 0x40:
	*irq = 15;
	break;
      default:
	buslogic_printk("Unable to determine BusLogic IRQ level.  Disabling board.\n");
	return TRUE;
    }
    *id = inquiry_result[2] & 0x7;

    inquiry_cmd[0] = CMD_INQEXTSETUP;
    inquiry_cmd[1] = 4;
    if (buslogic_out(base, inquiry_cmd, 2)
	|| buslogic_in(base, inquiry_result, 4))
	return TRUE;
    WAIT_UNTIL(INTERRUPT(base), CMDC);
    INTR_RESET(base);

#ifdef BUSLOGIC_BUS_TYPE_OVERRIDE
    *bus_type = BUS_TYPE_OVERRIDE;
#else
    *bus_type = inquiry_result[0];
#endif
    CHECK(*bus_type == 'A' || *bus_type == 'E' || *bus_type == 'M');
#ifdef BUSLOGIC_BUS_TYPE_OVERRIDE
    if (inquiry_result[0] != BUS_TYPE_OVERRIDE)
	buslogic_printk("Overriding bus type %c with %c\n",
			inquiry_result[0], BUS_TYPE_OVERRIDE);
#endif
    *max_sg = (inquiry_result[3] << 8) | inquiry_result[2];

    /* We only need a DMA channel for ISA boards.  Some other types of boards
       (such as the 747S) have an option to report a DMA channel even though
       none is used (for compatability with Adaptec drivers which require a
       DMA channel).  We ignore this. */
    if (*bus_type == 'A')
	switch (*dma) {
	  case 0:	/* This indicates a that no DMA channel is used. */
	    *dma = 0;
	    break;
	  case 0x20:
	    *dma = 5;
	    break;
	  case 0x40:
	    *dma = 6;
	    break;
	  case 0x80:
	    *dma = 7;
	    break;
	  default:
	    buslogic_printk("Unable to determine BusLogic DMA channel.  Disabling board.\n");
	    return TRUE;
	}
    else
	*dma = 0;

    while (0) {
      fail:
	buslogic_printk("buslogic_detect: query board settings\n");
	return TRUE;
    }

    return FALSE;
}

/* Query the board to find out the model. */
static int buslogic_query(unsigned int base, int *trans)
{
    unsigned const char inquiry_cmd[] = { CMD_INQUIRY };
    unsigned char inquiry_result[4];
    int i;

    i = inb(STATUS(base));
    if (i & DIRRDY)
	i = inb(DATA_IN(base));
    buslogic_out(base, inquiry_cmd, sizeof inquiry_cmd);
    buslogic_in(base, inquiry_result, 4);
    WAIT_UNTIL(INTERRUPT(base), CMDC);
    INTR_RESET(base);

    buslogic_printk("Inquiry Bytes: %X %X %X %X\n",
		    inquiry_result[0],inquiry_result[1],
		    inquiry_result[2],inquiry_result[3]);
    while (0) {
      fail:
	buslogic_printk("buslogic_query: query board settings\n");
	return TRUE;
    }

    *trans = BIOS_TRANSLATION_6432;     /* Default case */

    return FALSE;
}

/* return non-zero on detection */
int buslogic_detect(Scsi_Host_Template * tpnt)
{
    unsigned char dma;
    unsigned char irq;
    unsigned int base = 0;
    unsigned char id;
    unsigned char bus_type;
    unsigned short max_sg;
    int trans;
    struct Scsi_Host *SHpnt = NULL;
    int count = 0;
    int indx;
    int val;

#if BUSLOGIC_DEBUG
    buslogic_printk("buslogic_detect:\n");
#endif

    for (indx = 0; indx < ARRAY_SIZE(bases); indx++)
	if (!check_region(bases[indx], 3)) {
	    SHpnt = scsi_register(tpnt, sizeof (struct hostdata));

	    base = bases[indx];

	    if (test_port(base, SHpnt))
		goto unregister;

	    /* Set the Bus on/off-times as not to ruin floppy performance. */
	    {
		/* The default ON/OFF times for BusLogic adapters is 7/4. */
		static const unsigned char oncmd[] = { CMD_BUSON_TIME, 7 };
		static const unsigned char offcmd[] = { CMD_BUSOFF_TIME, 5 };

		INTR_RESET(base);
		buslogic_out(base, oncmd, sizeof oncmd);
		WAIT_UNTIL(INTERRUPT(base), CMDC);
		/* CMD_BUSOFF_TIME is a noop for EISA boards, but as there is
		   no way to to differentiate EISA from VESA we send it
		   unconditionally. */
		INTR_RESET(base);
		buslogic_out(base, offcmd, sizeof offcmd);
		WAIT_UNTIL(INTERRUPT(base), CMDC);
		while (0) {
		  fail:
		    buslogic_printk("buslogic_detect: setting bus on/off-time failed\n");
		}
		INTR_RESET(base);
	    }

	    if (buslogic_query(base, &trans))
		goto unregister;

	    if (getconfig(base, &irq, &dma, &id, &bus_type, &max_sg))
		goto unregister;

#if BUSLOGIC_DEBUG
	    buslogic_stat(base);
#endif
	    /* Here is where we tell the men from the boys (i.e. an Adaptec
	       will fail in setup_mailboxes, the men will not :-) */
	    if (!setup_mailboxes(base, SHpnt))
		goto unregister;

	    printk("Configuring BusLogic %s HA at port 0x%03X, IRQ %u",
		   (bus_type == 'A' ? "ISA"
		    : (bus_type == 'E' ? "EISA/VESA" : "MCA")),
		   base, irq);
	    if (dma != 0)
		printk(", DMA %u", dma);
	    printk(", ID %u\n", id);

#if BUSLOGIC_DEBUG
	    buslogic_stat(base);
#endif

#if BUSLOGIC_DEBUG
	    buslogic_printk("buslogic_detect: enable interrupt channel %d\n",
			    irq);
#endif

	    cli();
	    val = request_irq(irq, buslogic_interrupt);
	    if (val) {
		buslogic_printk("Unable to allocate IRQ for "
				"BusLogic controller.\n");
		sti();
		goto unregister;
	    }

	    if (dma) {
		if (request_dma(dma)) {
		    buslogic_printk("Unable to allocate DMA channel for "
				    "BusLogic controller.\n");
		    free_irq(irq);
		    sti();
		    goto unregister;
		}

		if (dma >= 5) {
		    outb((dma - 4) | CASCADE, DMA_MODE_REG);
		    outb(dma - 4, DMA_MASK_REG);
		}
	    }

	    host[irq - 9] = SHpnt;
	    SHpnt->this_id = id;
#ifdef CONFIG_NO_BUGGY_BUSLOGIC
	    /* Only type 'A' (AT/ISA) bus adapters use unchecked DMA. */
	    SHpnt->unchecked_isa_dma = (bus_type == 'A');
#else
	    /* bugs in the firmware with 16M+. Gaah */
	    SHpnt->unchecked_isa_dma = 1;
#endif
	    SHpnt->sg_tablesize = max_sg;
	    if (SHpnt->sg_tablesize > BUSLOGIC_MAX_SG)
		SHpnt->sg_tablesize = BUSLOGIC_MAX_SG;
	    /* ??? If we can dynamically allocate the mailbox arrays, I'll
	       probably bump up this number. */
	    SHpnt->hostt->can_queue = BUSLOGIC_MAILBOXES;
	    /*SHpnt->base = ???;*/
	    SHpnt->io_port = base;
	    SHpnt->dma_channel = dma;
	    SHpnt->irq = irq;
	    HOSTDATA(SHpnt)->bios_translation = trans;
	    if (trans == BIOS_TRANSLATION_25563)
		buslogic_printk("Using extended bios translation.\n");
	    HOSTDATA(SHpnt)->last_mbi_used  = 2 * BUSLOGIC_MAILBOXES - 1;
	    HOSTDATA(SHpnt)->last_mbo_used  = BUSLOGIC_MAILBOXES - 1;
	    memset(HOSTDATA(SHpnt)->SCint, 0, sizeof HOSTDATA(SHpnt)->SCint);
	    sti();

#if 0
	    {
		unsigned char buf[8];
		unsigned char cmd[]
		    = { READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		size_t i;

#if BUSLOGIC_DEBUG
		buslogic_printk("*** READ CAPACITY ***\n");
#endif
		for (i = 0; i < sizeof buf; i++)
		    buf[i] = 0x87;
		for (i = 0; i < 2; i++)
		    if (!buslogic_command(i, cmd, buf, sizeof buf)) {
			buslogic_printk("bus_detect: LU %u sector_size %d "
					"device_size %d\n",
					i, xscsi2int(buf + 4), xscsi2int(buf));
		    }

#if BUSLOGIC_DEBUG
		buslogic_printk("*** NOW RUNNING MY OWN TEST ***\n");
#endif
		for (i = 0; i < 4; i++) {
		    static buffer[512];

		    cmd[0] = READ_10;
		    cmd[1] = 0;
		    xany2scsi(cmd + 2, i);
		    cmd[6] = 0;
		    cmd[7] = 0;
		    cmd[8] = 1;
		    cmd[9] = 0;
		    buslogic_command(0, cmd, buffer, sizeof buffer);
		}
	    }
#endif

	    snarf_region(bases[indx], 3);	/* Register the IO ports that
						   we use */
	    count++;
	    continue;
	  unregister:
	    scsi_unregister(SHpnt);
	}
    return count;
}

/* ??? The abort command for the aha1542 does not leave the device in a clean
   state where it is available to be used again.  As it is not clear whether
   the same problem exists with BusLogic boards, we will enable this and see
   if it works. */
int buslogic_abort(Scsi_Cmnd *SCpnt)
{
    static const unsigned char buscmd[] = { CMD_START_SCSI };
    struct mailbox *mb;
    int mbi, mbo, i;

    buslogic_printk("buslogic_abort: %X %X\n",
		    inb(STATUS(SCpnt->host->io_port)),
		    inb(INTERRUPT(SCpnt->host->io_port)));

    cli();
    mb = HOSTDATA(SCpnt->host)->mb;
    mbi = HOSTDATA(SCpnt->host)->last_mbi_used + 1;
    if (mbi >= 2 * BUSLOGIC_MAILBOXES)
	mbi = BUSLOGIC_MAILBOXES;

    do {
	if (mb[mbi].status != MBX_NOT_IN_USE)
	    break;
	mbi++;
	if (mbi >= 2 * BUSLOGIC_MAILBOXES)
	    mbi = BUSLOGIC_MAILBOXES;
    } while (mbi != HOSTDATA(SCpnt->host)->last_mbi_used);
    sti();

    if (mb[mbi].status != MBX_NOT_IN_USE) {
	buslogic_printk("Lost interrupt discovered on irq %d - attempting to recover\n",
			SCpnt->host->irq);
	{
	    int intval[3];

	    intval[0] = SCpnt->host->irq;
	    buslogic_interrupt((int)&intval[2]);
	    return SCSI_ABORT_SUCCESS;
	}
    }

    /* OK, no lost interrupt.  Try looking to see how many pending commands we
       think we have. */
    for (i = 0; i < BUSLOGIC_MAILBOXES; i++)
	if (HOSTDATA(SCpnt->host)->SCint[i]) {
	    if (HOSTDATA(SCpnt->host)->SCint[i] == SCpnt) {
		buslogic_printk("Timed out command pending for %4.4X\n",
				SCpnt->request.dev);
		if (HOSTDATA(SCpnt->host)->mb[i].status != MBX_NOT_IN_USE) {
		    buslogic_printk("OGMB still full - restarting\n");
		    buslogic_out(SCpnt->host->io_port, buscmd, sizeof buscmd);
		}
	    } else
		buslogic_printk("Other pending command %4.4X\n",
				SCpnt->request.dev);
	}

#if (BUSLOGIC_DEBUG & BD_ABORT)
    buslogic_printk("buslogic_abort\n");
#endif

#if 1
    /* This section of code should be used carefully - some devices cannot
       abort a command, and this merely makes it worse. */
    cli();
    for (mbo = 0; mbo < BUSLOGIC_MAILBOXES; mbo++)
	if (SCpnt == HOSTDATA(SCpnt->host)->SCint[mbo]) {
	    HOSTDATA(SCpnt->host)->mb[mbo].status = MBX_ACTION_ABORT;
	    buslogic_out(SCpnt->host->io_port, buscmd, sizeof buscmd);
	    break;
	}
    sti();
#endif

    return SCSI_ABORT_PENDING;
}

/* We do not implement a reset function here, but the upper level code assumes
   that it will get some kind of response for the command in SCpnt.  We must
   oblige, or the command will hang the SCSI system. */
int buslogic_reset(Scsi_Cmnd *SCpnt)
{
#if BUSLOGIC_DEBUG
    buslogic_printk("buslogic_reset\n");
#endif
    return SCSI_RESET_PUNT;
}

int buslogic_biosparam(Disk * disk, int dev, int *ip)
{
  int size = disk->capacity;
  int translation_algorithm;

  /* ??? This is wrong if disk is configured for > 1G mapping.
     Unfortunately, unlike UltraStor, I see know way of determining whether
     > 1G mapping has been enabled. */


    translation_algorithm = HOSTDATA(disk->device->host)->bios_translation;
    /* ??? Should this be > 1024, or >= 1024?  Enquiring minds want to know. */
    if ((size >> 11) > 1024
	&& translation_algorithm == BIOS_TRANSLATION_25563) {
	/* Please verify that this is the same as what DOS returns */
	ip[0] = 255;
	ip[1] = 63;
	ip[2] = size / 255 / 63;
    } else {
	ip[0] = 64;
	ip[1] = 32;
	ip[2] = size >> 11;
    }
/*    if (ip[2] > 1024)
      ip[2] = 1024; */
    return 0;
}
