/*
 *    seagate.c Copyright (C) 1992, 1993 Drew Eckhardt
 *      low level scsi driver for ST01/ST02, Future Domain TMC-885,
 *      TMC-950  by
 *
 *              Drew Eckhardt
 *
 *      <drew@colorado.edu>
 *
 *      Note : TMC-880 boards don't work because they have two bits in
 *              the status register flipped, I'll fix this "RSN"
 *
 *      This card does all the I/O via memory mapped I/O, so there is no need
 *      to check or allocate a region of the I/O address space.
 */

/* Modified 1996 to use new read{b,w,l}, write{b,w,l}, and phys_to_virt
   macros. This meant redefining st0x_cr_sr and st0x_dr, as well as
   replacing the "DATA = foo;" and "CONTROL = foo;" structures with 
   WRITE_DATA(foo) and WRITE_CONTROL(foo) macros.

   Replaced assembler routines with C. There's probably a performance hit,
   but I only have a cdrom and can't tell. Define SEAGATE_USE_ASM if you
   want the old assembler code.

   Look for the string "SJT" for details.

 */

/*
 * Configuration :
 * To use without BIOS -DOVERRIDE=base_address -DCONTROLLER=FD or SEAGATE
 * -DIRQ will override the default of 5.
 * Note: You can now set these options from the kernel's "command line".
 * The syntax is:
 *
 *     st0x=ADDRESS,IRQ                (for a Seagate controller)
 * or:
 *     tmc8xx=ADDRESS,IRQ              (for a TMC-8xx or TMC-950 controller)
 * eg:
 *     tmc8xx=0xC8000,15
 *
 * will configure the driver for a TMC-8xx style controller using IRQ 15
 * with a base address of 0xC8000.
 *
 * -DARBITRATE 
 *      Will cause the host adapter to arbitrate for the
 *      bus for better SCSI-II compatibility, rather than just
 *      waiting for BUS FREE and then doing its thing.  Should
 *      let us do one command per Lun when I integrate my
 *      reorganization changes into the distribution sources.
 *
 * -DDEBUG
 *      Will activate debug code.
 *
 * -DFAST or -DFAST32 
 *      Will use blind transfers where possible
 *
 * -DPARITY  
 *      This will enable parity.
 *
 * -DSEAGATE_USE_ASM
 *      Will use older seagate assembly code. should be (very small amount)
 *      Faster.
 *
 * -DSLOW_HANDSHAKE
 *      Will allow compatibility with broken devices that don't
 *      handshake fast enough (ie, some CD ROM's) for the Seagate
 *      code.
 *
 * -DSLOW_RATE=x
 *      x is some number, It will let you specify a default
 *      transfer rate if handshaking isn't working correctly.
 *
 * The following to options are patches from the SCSI.HOWTO
 *
 * -DSWAPSTAT  This will swap the definitions for STAT_MSG and STAT_CD.
 *
 * -DSWAPCNTDATA  This will swap the order that seagate.c messes with
 *                the CONTROL an DATA registers.
 */

#include <linux/module.h>

#include <asm/io.h>
#include <asm/system.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/config.h>
#include <linux/proc_fs.h>

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include "seagate.h"
#include "constants.h"
#include<linux/stat.h>
#include <asm/uaccess.h>
#include "sd.h"
#include <scsi/scsi_ioctl.h>

static struct proc_dir_entry proc_scsi_seagate =
{
  PROC_SCSI_SEAGATE, 7, "seagate",
  S_IFDIR | S_IRUGO | S_IXUGO, 2
};

#ifndef IRQ
#define IRQ 5
#endif

#if (defined(FAST32) && !defined(FAST))
#define FAST
#endif

#if defined(SLOW_RATE) && !defined(SLOW_HANDSHAKE)
#define SLOW_HANDSHAKE
#endif

#if defined(SLOW_HANDSHAKE) && !defined(SLOW_RATE)
#define SLOW_RATE 50
#endif

#if defined(LINKED)
#undef LINKED                           /* Linked commands are currently
                                           broken! */
#endif


/*
	Thanks to Brian Antoine for the example code in his Messy-Loss ST-01
		driver, and Mitsugu Suzuki for information on the ST-01
		SCSI host.
*/

/*
	CONTROL defines
*/

#define CMD_RST 		0x01
#define CMD_SEL 		0x02
#define CMD_BSY 		0x04
#define CMD_ATTN    		0x08
#define CMD_START_ARB		0x10
#define CMD_EN_PARITY		0x20
#define CMD_INTR		0x40
#define CMD_DRVR_ENABLE		0x80

/*
	STATUS
*/
#ifdef SWAPSTAT
	#define STAT_MSG		0x08
	#define STAT_CD			0x02
#else
	#define STAT_MSG		0x02
	#define STAT_CD			0x08
#endif

#define STAT_BSY		0x01
#define STAT_IO			0x04
#define STAT_REQ		0x10
#define STAT_SEL		0x20
#define STAT_PARITY		0x40
#define STAT_ARB_CMPL		0x80

/* 
	REQUESTS
*/

#define REQ_MASK (STAT_CD |  STAT_IO | STAT_MSG)
#define REQ_DATAOUT 0
#define REQ_DATAIN STAT_IO
#define REQ_CMDOUT STAT_CD
#define REQ_STATIN (STAT_CD | STAT_IO)
#define REQ_MSGOUT (STAT_MSG | STAT_CD)
#define REQ_MSGIN (STAT_MSG | STAT_CD | STAT_IO)

extern volatile int seagate_st0x_timeout;

#ifdef PARITY
	#define BASE_CMD CMD_EN_PARITY
#else
	#define BASE_CMD  0
#endif

/*
	Debugging code
*/

#define PHASE_BUS_FREE 1
#define PHASE_ARBITRATION 2
#define PHASE_SELECTION 4
#define PHASE_DATAIN 8 
#define PHASE_DATAOUT 0x10
#define PHASE_CMDOUT 0x20
#define PHASE_MSGIN 0x40
#define PHASE_MSGOUT 0x80
#define PHASE_STATUSIN 0x100
#define PHASE_ETC (PHASE_DATAIN | PHASE_DATA_OUT | PHASE_CMDOUT | PHASE_MSGIN | PHASE_MSGOUT | PHASE_STATUSIN)
#define PRINT_COMMAND 0x200
#define PHASE_EXIT 0x400
#define PHASE_RESELECT 0x800
#define DEBUG_FAST 0x1000
#define DEBUG_SG   0x2000
#define DEBUG_LINKED	0x4000
#define DEBUG_BORKEN	0x8000

/* 
 *	Control options - these are timeouts specified in .01 seconds.
 */

/* 30, 20 work */
#define ST0X_BUS_FREE_DELAY 25
#define ST0X_SELECTION_DELAY 25

#define eoi() __asm__("push %%eax\nmovb $0x20, %%al\noutb %%al, $0x20\npop %%eax"::)
	
#define SEAGATE 1	/* these determine the type of the controller */
#define FD	2

#define ST0X_ID_STR	"Seagate ST-01/ST-02"
#define FD_ID_STR	"TMC-8XX/TMC-950"


static int internal_command (unsigned char target, unsigned char lun,
                             const void *cmnd,
                             void *buff, int bufflen, int reselect);

static int incommand;                   /* set if arbitration has finished
                                           and we are in some command phase. */

static unsigned int base_address = 0;   /* Where the card ROM starts, used to 
                                           calculate memory mapped register
                                           location.  */
#ifdef notyet
static volatile int abort_confirm = 0;

#endif

static unsigned long st0x_cr_sr;        /* control register write, status
                                           register read.  256 bytes in
                                           length.
                                           Read is status of SCSI BUS, as per 
                                           STAT masks.  */

static unsigned long st0x_dr;           /* data register, read write 256
                                           bytes in length.  */

static volatile int st0x_aborted = 0;   /* set when we are aborted, ie by a
                                           time out, etc.  */

static unsigned char controller_type = 0;       /* set to SEAGATE for ST0x
                                                   boards or FD for TMC-8xx
                                                   boards */
static unsigned char irq = IRQ;

#define retcode(result) (((result) << 16) | (message << 8) | status)
#define STATUS (readb(st0x_cr_sr))
#define DATA (readb(st0x_dr))
/* SJT: Start. */
#define WRITE_CONTROL(d) writeb((d), st0x_cr_sr)
#define WRITE_DATA(d) writeb((d), st0x_dr)
/* SJT: End. */

void st0x_setup (char *str, int *ints)
{
  controller_type = SEAGATE;
  base_address = ints[1];
  irq = ints[2];
}

void tmc8xx_setup (char *str, int *ints)
{
  controller_type = FD;
  base_address = ints[1];
  irq = ints[2];
}

#ifndef OVERRIDE
static unsigned int seagate_bases[] =
{
  0xc8000, 0xca000, 0xcc000,
  0xce000, 0xdc000, 0xde000
};

typedef struct
{
  const unsigned char *signature;
  unsigned offset;
  unsigned length;
  unsigned char type;
}
Signature;

static const Signature signatures[] =
{
#ifdef CONFIG_SCSI_SEAGATE
  {"ST01 v1.7  (C) Copyright 1987 Seagate", 15, 37, SEAGATE},
  {"SCSI BIOS 2.00  (C) Copyright 1987 Seagate", 15, 40, SEAGATE},

/*
 * The following two lines are NOT mistakes.  One detects ROM revision
 * 3.0.0, the other 3.2.  Since seagate has only one type of SCSI adapter,
 * and this is not going to change, the "SEAGATE" and "SCSI" together
 * are probably "good enough"
 */

  {"SEAGATE SCSI BIOS ", 16, 17, SEAGATE},
  {"SEAGATE SCSI BIOS ", 17, 17, SEAGATE},

/*
 * However, future domain makes several incompatible SCSI boards, so specific
 * signatures must be used.
 */

  {"FUTURE DOMAIN CORP. (C) 1986-1989 V5.0C2/14/89", 5, 46, FD},
  {"FUTURE DOMAIN CORP. (C) 1986-1989 V6.0A7/28/89", 5, 46, FD},
  {"FUTURE DOMAIN CORP. (C) 1986-1990 V6.0105/31/90", 5, 47, FD},
  {"FUTURE DOMAIN CORP. (C) 1986-1990 V6.0209/18/90", 5, 47, FD},
  {"FUTURE DOMAIN CORP. (C) 1986-1990 V7.009/18/90", 5, 46, FD},
  {"FUTURE DOMAIN CORP. (C) 1992 V8.00.004/02/92", 5, 44, FD},
  {"IBM F1 BIOS V1.1004/30/92", 5, 25, FD},
  {"FUTURE DOMAIN TMC-950", 5, 21, FD},
#endif                                  /* CONFIG_SCSI_SEAGATE */
}
;

#define NUM_SIGNATURES (sizeof(signatures) / sizeof(Signature))
#endif /* n OVERRIDE */

/*
 * hostno stores the hostnumber, as told to us by the init routine.
 */

static int hostno = -1;
static void seagate_reconnect_intr (int, void *, struct pt_regs *);

#ifdef FAST
static int fast = 1;

#endif

#ifdef SLOW_HANDSHAKE
/*
 * Support for broken devices :
 * The Seagate board has a handshaking problem.  Namely, a lack
 * thereof for slow devices.  You can blast 600K/second through
 * it if you are polling for each byte, more if you do a blind
 * transfer.  In the first case, with a fast device, REQ will
 * transition high-low or high-low-high before your loop restarts
 * and you'll have no problems.  In the second case, the board
 * will insert wait states for up to 13.2 usecs for REQ to
 * transition low->high, and everything will work.
 *
 * However, there's nothing in the state machine that says
 * you *HAVE* to see a high-low-high set of transitions before
 * sending the next byte, and slow things like the Trantor CD ROMS
 * will break because of this.
 *
 * So, we need to slow things down, which isn't as simple as it
 * seems.  We can't slow things down period, because then people
 * who don't recompile their kernels will shoot me for ruining
 * their performance.  We need to do it on a case per case basis.
 *
 * The best for performance will be to, only for borken devices
 * (this is stored on a per-target basis in the scsi_devices array)
 *
 * Wait for a low->high transition before continuing with that
 * transfer.  If we timeout, continue anyways.  We don't need
 * a long timeout, because REQ should only be asserted until the
 * corresponding ACK is received and processed.
 *
 * Note that we can't use the system timer for this, because of
 * resolution, and we *really* can't use the timer chip since
 * gettimeofday() and the beeper routines use that.  So,
 * the best thing for us to do will be to calibrate a timing
 * loop in the initialization code using the timer chip before
 * gettimeofday() can screw with it.
 */

static int borken_calibration = 0;
static void borken_init (void)
{
  register int count = 0, start = jiffies + 1, stop = start + 25;

  while (jiffies < start) ;
  for (; jiffies < stop; ++count) ;

/*
 * Ok, we now have a count for .25 seconds.  Convert to a
 * count per second and divide by transfer rate in K.
 */

  borken_calibration = (count * 4) / (SLOW_RATE * 1024);

  if (borken_calibration < 1)
    borken_calibration = 1;
#if (DEBUG & DEBUG_BORKEN)
  printk ("scsi%d : borken calibrated to %dK/sec, %d cycles per transfer\n",
          hostno, BORKEN_RATE, borken_calibration);
#endif
}

static inline void borken_wait (void)
{
  register int count;

  for (count = borken_calibration; count && (STATUS & STAT_REQ);
       --count) ;
#if (DEBUG & DEBUG_BORKEN)
  if (count)
    printk ("scsi%d : borken timeout\n", hostno);
#endif
}

#endif /* def SLOW_HANDSHAKE */

int seagate_st0x_detect (Scsi_Host_Template * tpnt)
{
  struct Scsi_Host *instance;

#ifndef OVERRIDE
  int i, j;

#endif

  tpnt->proc_dir = &proc_scsi_seagate;
/*
 *    First, we try for the manual override.
 */
#ifdef DEBUG
  printk ("Autodetecting ST0x / TMC-8xx\n");
#endif

  if (hostno != -1)
  {
    printk ("ERROR : seagate_st0x_detect() called twice.\n");
    return 0;
  }

/* If the user specified the controller type from the command line,
   controller_type will be non-zero, so don't try to detect one */

  if (!controller_type)
  {
#ifdef OVERRIDE
    base_address = OVERRIDE;

/* CONTROLLER is used to override controller (SEAGATE or FD). PM: 07/01/93 */
#ifdef CONTROLLER
    controller_type = CONTROLLER;
#else
#error Please use -DCONTROLLER=SEAGATE or -DCONTROLLER=FD to override controller type
#endif /* CONTROLLER */
#ifdef DEBUG
    printk ("Base address overridden to %x, controller type is %s\n",
            base_address, controller_type == SEAGATE ? "SEAGATE" : "FD");
#endif
#else /* OVERRIDE */
/*
 *    To detect this card, we simply look for the signature
 *      from the BIOS version notice in all the possible locations
 *      of the ROM's.  This has a nice side effect of not trashing
 *      any register locations that might be used by something else.
 *
 * XXX - note that we probably should be probing the address
 * space for the on-board RAM instead.
 */

    for (i = 0; i < (sizeof (seagate_bases) / sizeof (unsigned int)); ++i)

      for (j = 0; !base_address && j < NUM_SIGNATURES; ++j)
        if (check_signature (seagate_bases[i] + signatures[j].offset,
                             signatures[j].signature, signatures[j].length))
        {
          base_address = seagate_bases[i];
          controller_type = signatures[j].type;
        }
#endif /* OVERRIDE */
  }    /* (! controller_type) */

  tpnt->this_id = (controller_type == SEAGATE) ? 7 : 6;
  tpnt->name = (controller_type == SEAGATE) ? ST0X_ID_STR : FD_ID_STR;

  if (base_address)
  {
    st0x_cr_sr = base_address + (controller_type == SEAGATE ? 0x1a00 : 0x1c00);
    st0x_dr = st0x_cr_sr + 0x200;
#ifdef DEBUG
    printk ("%s detected. Base address = %x, cr = %x, dr = %x\n",
            tpnt->name, base_address, st0x_cr_sr, st0x_dr);
#endif
/*
 *    At all times, we will use IRQ 5.  Should also check for IRQ3 if we
 *      loose our first interrupt.
 */
    instance = scsi_register (tpnt, 0);
    hostno = instance->host_no;
    if (request_irq ((int) irq, seagate_reconnect_intr, SA_INTERRUPT,
                (controller_type == SEAGATE) ? "seagate" : "tmc-8xx", NULL))
    {
      printk ("scsi%d : unable to allocate IRQ%d\n", hostno, (int) irq);
      return 0;
    }
    instance->irq = irq;
    instance->io_port = base_address;
#ifdef SLOW_HANDSHAKE
    borken_init ();
#endif

    printk ("%s options:"
#ifdef ARBITRATE
            " ARBITRATE"
#endif
#ifdef DEBUG
            " DEBUG"
#endif
#ifdef FAST
#ifdef FAST32
            " FAST32"
#else
            " FAST"
#endif
#endif
#ifdef LINKED
            " LINKED"
#endif
#ifdef PARITY
            " PARITY"
#endif
#ifdef SEAGATE_USE_ASM
            " SEAGATE_USE_ASM"
#endif
#ifdef SLOW_HANDSHAKE
            " SLOW_HANDSHAKE"
#endif
#ifdef SWAPSTAT
            " SWAPSTAT"
#endif
#ifdef SWAPCNTDATA
            " SWAPCNTDATA"
#endif
            "\n", tpnt->name);
    return 1;
  }
  else
  {
#ifdef DEBUG
    printk ("ST0x / TMC-8xx not detected.\n");
#endif
    return 0;
  }
}

const char *seagate_st0x_info (struct Scsi_Host *shpnt)
{
  static char buffer[64];

  sprintf (buffer, "%s at irq %d, address 0x%05X",
           (controller_type == SEAGATE) ? ST0X_ID_STR : FD_ID_STR,
           irq, base_address);
  return buffer;
}

/*
 * These are our saved pointers for the outstanding command that is
 * waiting for a reconnect
 */

static unsigned char current_target, current_lun;
static unsigned char *current_cmnd, *current_data;
static int current_nobuffs;
static struct scatterlist *current_buffer;
static int current_bufflen;

#ifdef LINKED

/*
 * linked_connected indicates whether or not we are currently connected to
 * linked_target, linked_lun and in an INFORMATION TRANSFER phase,
 * using linked commands.
 */

static int linked_connected = 0;
static unsigned char linked_target, linked_lun;

#endif

static void (*done_fn) (Scsi_Cmnd *) = NULL;
static Scsi_Cmnd *SCint = NULL;

/*
 * These control whether or not disconnect / reconnect will be attempted,
 * or are being attempted.
 */

#define NO_RECONNECT    0
#define RECONNECT_NOW   1
#define CAN_RECONNECT   2

#ifdef LINKED

/*
 * LINKED_RIGHT indicates that we are currently connected to the correct target
 * for this command, LINKED_WRONG indicates that we are connected to the wrong
 * target.  Note that these imply CAN_RECONNECT.
 */

#define LINKED_RIGHT    3
#define LINKED_WRONG    4
#endif

/*
 * This determines if we are expecting to reconnect or not.
 */

static int should_reconnect = 0;

/*
 * The seagate_reconnect_intr routine is called when a target reselects the
 * host adapter.  This occurs on the interrupt triggered by the target
 * asserting SEL.
 */

static void seagate_reconnect_intr (int irq, void *dev_id, 
                                    struct pt_regs *regs)
{
  int temp;
  Scsi_Cmnd *SCtmp;

/* enable all other interrupts. */
  sti ();
#if (DEBUG & PHASE_RESELECT)
  printk ("scsi%d : seagate_reconnect_intr() called\n", hostno);
#endif

  if (!should_reconnect)
    printk ("scsi%d: unexpected interrupt.\n", hostno);
  else
  {
    should_reconnect = 0;

#if (DEBUG & PHASE_RESELECT)
    printk ("scsi%d : internal_command("
            "%d, %08x, %08x, %d, RECONNECT_NOW\n", hostno,
            current_target, current_data, current_bufflen);
#endif

    temp = internal_command (current_target, current_lun, current_cmnd,
                             current_data, current_bufflen, RECONNECT_NOW);

    if (msg_byte (temp) != DISCONNECT)
    {
      if (done_fn)
      {
#if (DEBUG & PHASE_RESELECT)
        printk ("scsi%d : done_fn(%d,%08x)", hostno,
                hostno, temp);
#endif
        if (!SCint)
          panic ("SCint == NULL in seagate");
        SCtmp = SCint;
        SCint = NULL;
        SCtmp->result = temp;
        done_fn (SCtmp);
      }
      else
        printk ("done_fn() not defined.\n");
    }
  }
}

/*
 * The seagate_st0x_queue_command() function provides a queued interface
 * to the seagate SCSI driver.  Basically, it just passes control onto the
 * seagate_command() function, after fixing it so that the done_fn()
 * is set to the one passed to the function.  We have to be very careful,
 * because there are some commands on some devices that do not disconnect,
 * and if we simply call the done_fn when the command is done then another
 * command is started and queue_command is called again...  We end up
 * overflowing the kernel stack, and this tends not to be such a good idea.
 */

static int recursion_depth = 0;

int seagate_st0x_queue_command (Scsi_Cmnd * SCpnt, void (*done) (Scsi_Cmnd *))
{
  int result, reconnect;
  Scsi_Cmnd *SCtmp;

  done_fn = done;
  current_target = SCpnt->target;
  current_lun = SCpnt->lun;
  (const void *) current_cmnd = SCpnt->cmnd;
  current_data = (unsigned char *) SCpnt->request_buffer;
  current_bufflen = SCpnt->request_bufflen;
  SCint = SCpnt;
  if (recursion_depth)
  {
    return 0;
  };
  recursion_depth++;
  do
  {
#ifdef LINKED
/*
 * Set linked command bit in control field of SCSI command.
 */

    current_cmnd[SCpnt->cmd_len] |= 0x01;
    if (linked_connected)
    {
#if (DEBUG & DEBUG_LINKED)
      printk ("scsi%d : using linked commands, current I_T_L nexus is ",
              hostno);
#endif
      if ((linked_target == current_target) && (linked_lun == current_lun))
      {
#if (DEBUG & DEBUG_LINKED)
        printk ("correct\n");
#endif
        reconnect = LINKED_RIGHT;
      }
      else
      {
#if (DEBUG & DEBUG_LINKED)
        printk ("incorrect\n");
#endif
        reconnect = LINKED_WRONG;
      }
    }
    else
#endif /* LINKED */
      reconnect = CAN_RECONNECT;

    result = internal_command (SCint->target, SCint->lun, SCint->cmnd,
                               SCint->request_buffer, SCint->request_bufflen,
                               reconnect);
    if (msg_byte (result) == DISCONNECT)
      break;
    SCtmp = SCint;
    SCint = NULL;
    SCtmp->result = result;
    done_fn (SCtmp);
  }
  while (SCint);
  recursion_depth--;
  return 0;
}

int seagate_st0x_command (Scsi_Cmnd * SCpnt)
{
  return internal_command (SCpnt->target, SCpnt->lun, SCpnt->cmnd,
                           SCpnt->request_buffer, SCpnt->request_bufflen,
                           (int) NO_RECONNECT);
}

static int internal_command (unsigned char target, unsigned char lun, 
                             const void *cmnd, void *buff, int bufflen, 
                             int reselect)
{
  int len = 0;
  unsigned char *data = NULL;
  struct scatterlist *buffer = NULL;
  int nobuffs = 0;
  int clock;
  int temp;

#ifdef SLOW_HANDSHAKE
  int borken;                           /* Does the current target require
                                           Very Slow I/O ?  */
#endif

#if (DEBUG & PHASE_DATAIN) || (DEBUG & PHASE_DATOUT)
  int transfered = 0;

#endif

#if (((DEBUG & PHASE_ETC) == PHASE_ETC) || (DEBUG & PRINT_COMMAND) || \
     (DEBUG & PHASE_EXIT))
  int i;

#endif

#if ((DEBUG & PHASE_ETC) == PHASE_ETC)
  int phase = 0, newphase;

#endif

  int done = 0;
  unsigned char status = 0;
  unsigned char message = 0;
  register unsigned char status_read;

  unsigned transfersize = 0, underflow = 0;

  incommand = 0;
  st0x_aborted = 0;

#ifdef SLOW_HANDSHAKE
  borken = (int) SCint->device->borken;
#endif

#if (DEBUG & PRINT_COMMAND)
  printk ("scsi%d : target = %d, command = ", hostno, target);
  print_command ((unsigned char *) cmnd);
  printk ("\n");
#endif

#if (DEBUG & PHASE_RESELECT)
  switch (reselect)
  {
    case RECONNECT_NOW:
      printk ("scsi%d : reconnecting\n", hostno);
      break;
#ifdef LINKED
    case LINKED_RIGHT:
      printk ("scsi%d : connected, can reconnect\n", hostno);
      break;
    case LINKED_WRONG:
      printk ("scsi%d : connected to wrong target, can reconnect\n", hostno);
      break;
#endif
    case CAN_RECONNECT:
      printk ("scsi%d : allowed to reconnect\n", hostno);
      break;
    default:
      printk ("scsi%d : not allowed to reconnect\n", hostno);
  }
#endif

  if (target == (controller_type == SEAGATE ? 7 : 6))
    return DID_BAD_TARGET;

/*
 *    We work it differently depending on if this is is "the first time,"
 *      or a reconnect.  If this is a reselect phase, then SEL will
 *      be asserted, and we must skip selection / arbitration phases.
 */

  switch (reselect)
  {
    case RECONNECT_NOW:
#if (DEBUG & PHASE_RESELECT)
      printk ("scsi%d : phase RESELECT \n", hostno);
#endif

/*
 *    At this point, we should find the logical or of our ID and the original
 *      target's ID on the BUS, with BSY, SEL, and I/O signals asserted.
 *
 *      After ARBITRATION phase is completed, only SEL, BSY, and the
 *      target ID are asserted.  A valid initiator ID is not on the bus
 *      until IO is asserted, so we must wait for that.
 */
      clock = jiffies + 10;
      for (;;)
      {
        temp = STATUS;
        if ((temp & STAT_IO) && !(temp & STAT_BSY))
          break;

        if (jiffies > clock)
        {
#if (DEBUG & PHASE_RESELECT)
          printk ("scsi%d : RESELECT timed out while waiting for IO .\n",
                  hostno);
#endif
          return (DID_BAD_INTR << 16);
        }
      }

/*
 *    After I/O is asserted by the target, we can read our ID and its
 *      ID off of the BUS.
 */

      if (!((temp = DATA) & (controller_type == SEAGATE ? 0x80 : 0x40)))
      {
#if (DEBUG & PHASE_RESELECT)
        printk ("scsi%d : detected reconnect request to different target.\n"
                "\tData bus = %d\n", hostno, temp);
#endif
        return (DID_BAD_INTR << 16);
      }

      if (!(temp & (1 << current_target)))
      {
        printk ("scsi%d : Unexpected reselect interrupt.  Data bus = %d\n",
                hostno, temp);
        return (DID_BAD_INTR << 16);
      }

      buffer = current_buffer;
      cmnd = current_cmnd;              /* WDE add */
      data = current_data;              /* WDE add */
      len = current_bufflen;            /* WDE add */
      nobuffs = current_nobuffs;

/*
 *    We have determined that we have been selected.  At this point,
 *      we must respond to the reselection by asserting BSY ourselves
 */

#if 1
      WRITE_CONTROL (BASE_CMD | CMD_DRVR_ENABLE | CMD_BSY);
#else
      WRITE_CONTROL (BASE_CMD | CMD_BSY);
#endif

/*
 *    The target will drop SEL, and raise BSY, at which time we must drop
 *      BSY.
 */

      for (clock = jiffies + 10; (jiffies < clock) && (STATUS & STAT_SEL);) ;

      if (jiffies >= clock)
      {
        WRITE_CONTROL (BASE_CMD | CMD_INTR);
#if (DEBUG & PHASE_RESELECT)
        printk ("scsi%d : RESELECT timed out while waiting for SEL.\n", 
                hostno);
#endif
        return (DID_BAD_INTR << 16);
      }

      WRITE_CONTROL (BASE_CMD);

/*
 *    At this point, we have connected with the target and can get
 *      on with our lives.
 */
      break;
    case CAN_RECONNECT:

#ifdef LINKED
/*
 * This is a bletcherous hack, just as bad as the Unix #! interpreter stuff.
 * If it turns out we are using the wrong I_T_L nexus, the easiest way to deal
 * with it is to go into our INFORMATION TRANSFER PHASE code, send a ABORT
 * message on MESSAGE OUT phase, and then loop back to here.
 */

    connect_loop:

#endif

#if (DEBUG & PHASE_BUS_FREE)
      printk ("scsi%d : phase = BUS FREE \n", hostno);
#endif

/*
 *    BUS FREE PHASE
 *
 *      On entry, we make sure that the BUS is in a BUS FREE
 *      phase, by insuring that both BSY and SEL are low for
 *      at least one bus settle delay.  Several reads help
 *      eliminate wire glitch.
 */

      clock = jiffies + ST0X_BUS_FREE_DELAY;

#if !defined (ARBITRATE)
      while (((STATUS | STATUS | STATUS) &
              (STAT_BSY | STAT_SEL)) &&
             (!st0x_aborted) && (jiffies < clock)) ;

      if (jiffies > clock)
        return retcode (DID_BUS_BUSY);
      else if (st0x_aborted)
        return retcode (st0x_aborted);
#endif

#if (DEBUG & PHASE_SELECTION)
      printk ("scsi%d : phase = SELECTION\n", hostno);
#endif

      clock = jiffies + ST0X_SELECTION_DELAY;

/*
 * Arbitration/selection procedure :
 * 1.  Disable drivers
 * 2.  Write HOST adapter address bit
 * 3.  Set start arbitration.
 * 4.  We get either ARBITRATION COMPLETE or SELECT at this
 *     point.
 * 5.  OR our ID and targets on bus.
 * 6.  Enable SCSI drivers and asserted SEL and ATTN
 */

#if defined(ARBITRATE)
      cli ();
      WRITE_CONTROL (0);
      WRITE_DATA ((controller_type == SEAGATE) ? 0x80 : 0x40);
      WRITE_CONTROL (CMD_START_ARB);
      sti ();
      while (!((status_read = STATUS) & (STAT_ARB_CMPL | STAT_SEL)) &&
             (jiffies < clock) && !st0x_aborted) ;

      if (!(status_read & STAT_ARB_CMPL))
      {
#if (DEBUG & PHASE_SELECTION)
        if (status_read & STAT_SEL)
          printk ("scsi%d : arbitration lost\n", hostno);
        else
          printk ("scsi%d : arbitration timeout.\n", hostno);
#endif
        WRITE_CONTROL (BASE_CMD);
        return retcode (DID_NO_CONNECT);
      };

#if (DEBUG & PHASE_SELECTION)
      printk ("scsi%d : arbitration complete\n", hostno);
#endif
#endif

/*
 *    When the SCSI device decides that we're gawking at it, it will
 *    respond by asserting BUSY on the bus.
 *
 *    Note : the Seagate ST-01/02 product manual says that we should
 *    twiddle the DATA register before the control register.    However,
 *    this does not work reliably so we do it the other way around.
 *
 *    Probably could be a problem with arbitration too, we really should
 *    try this with a SCSI protocol or logic analyzer to see what is
 *    going on.
 */
#ifdef SWAPCNTDATA
	cli();
      WRITE_CONTROL (BASE_CMD | CMD_DRVR_ENABLE | CMD_SEL |
                     (reselect ? CMD_ATTN : 0));
      WRITE_DATA ((unsigned char) ((1 << target) |
                               (controller_type == SEAGATE ? 0x80 : 0x40)));
	sti();
#else
      cli ();
      WRITE_DATA ((unsigned char) ((1 << target) |
                               (controller_type == SEAGATE ? 0x80 : 0x40)));
      WRITE_CONTROL (BASE_CMD | CMD_DRVR_ENABLE | CMD_SEL |
                     (reselect ? CMD_ATTN : 0));
      sti ();
#endif
      while (!((status_read = STATUS) & STAT_BSY) && (jiffies < clock)
             && !st0x_aborted)
#if 0 && (DEBUG & PHASE_SELECTION)
      {
        temp = clock - jiffies;

        if (!(jiffies % 5))
          printk ("seagate_st0x_timeout : %d            \r", temp);
      }
      printk ("Done.                                             \n");
      printk ("scsi%d : status = %02x, seagate_st0x_timeout = %d, aborted = %02x \n",
              hostno, status_read, temp, st0x_aborted);
#else
        ;
#endif

      if ((jiffies >= clock) && !(status_read & STAT_BSY))
      {
#if (DEBUG & PHASE_SELECTION)
        printk ("scsi%d : NO CONNECT with target %d, status = %x \n",
                hostno, target, STATUS);
#endif
        return retcode (DID_NO_CONNECT);
      }

/*
 *    If we have been aborted, and we have a command in progress, IE the
 *      target still has BSY asserted, then we will reset the bus, and
 *      notify the midlevel driver to expect sense.
 */

      if (st0x_aborted)
      {
        WRITE_CONTROL (BASE_CMD);
        if (STATUS & STAT_BSY)
        {
          printk ("scsi%d : BST asserted after we've been aborted.\n",
                  hostno);
          seagate_st0x_reset (NULL, 0);
          return retcode (DID_RESET);
        }
        return retcode (st0x_aborted);
      }

/* Establish current pointers.  Take into account scatter / gather */

      if ((nobuffs = SCint->use_sg))
      {
#if (DEBUG & DEBUG_SG)
        {
          int i;

          printk ("scsi%d : scatter gather requested, using %d buffers.\n",
                  hostno, nobuffs);
          for (i = 0; i < nobuffs; ++i)
            printk ("scsi%d : buffer %d address = %08x length = %d\n",
                    hostno, i, buffer[i].address, buffer[i].length);
        }
#endif

        buffer = (struct scatterlist *) SCint->buffer;
        len = buffer->length;
        data = (unsigned char *) buffer->address;
      }
      else
      {
#if (DEBUG & DEBUG_SG)
        printk ("scsi%d : scatter gather not requested.\n", hostno);
#endif
        buffer = NULL;
        len = SCint->request_bufflen;
        data = (unsigned char *) SCint->request_buffer;
      }

#if (DEBUG & (PHASE_DATAIN | PHASE_DATAOUT))
      printk ("scsi%d : len = %d\n", hostno, len);
#endif

      break;
#ifdef LINKED
    case LINKED_RIGHT:
      break;
    case LINKED_WRONG:
      break;
#endif
  }                                     /* end of switch(reselect) */

/*
 *    There are several conditions under which we wish to send a message :
 *      1.  When we are allowing disconnect / reconnect, and need to establish
 *          the I_T_L nexus via an IDENTIFY with the DiscPriv bit set.
 *
 *      2.  When we are doing linked commands, are have the wrong I_T_L nexus
 *          established and want to send an ABORT message.
 */

/* GCC does not like an ifdef inside a macro, so do it the hard way. */
#ifdef LINKED
  WRITE_CONTROL (BASE_CMD | CMD_DRVR_ENABLE |
                 (((reselect == CAN_RECONNECT)
                   || (reselect == LINKED_WRONG)
                  )? CMD_ATTN : 0));
#else
  WRITE_CONTROL (BASE_CMD | CMD_DRVR_ENABLE |
                 (((reselect == CAN_RECONNECT)
                  )? CMD_ATTN : 0));
#endif

/*
 *    INFORMATION TRANSFER PHASE
 *
 *      The nasty looking read / write inline assembler loops we use for
 *      DATAIN and DATAOUT phases are approximately 4-5 times as fast as
 *      the 'C' versions - since we're moving 1024 bytes of data, this
 *      really adds up.
 *
 *      SJT: The nasty-looking assembler is gone, so it's slower.
 *
 */

#if ((DEBUG & PHASE_ETC) == PHASE_ETC)
  printk ("scsi%d : phase = INFORMATION TRANSFER\n", hostno);
#endif

  incommand = 1;
  transfersize = SCint->transfersize;
  underflow = SCint->underflow;

/*
 *    Now, we poll the device for status information,
 *      and handle any requests it makes.  Note that since we are unsure of
 *      how much data will be flowing across the system, etc and cannot
 *      make reasonable timeouts, that we will instead have the midlevel
 *      driver handle any timeouts that occur in this phase.
 */

  while (((status_read = STATUS) & STAT_BSY) && !st0x_aborted && !done)
  {
#ifdef PARITY
    if (status_read & STAT_PARITY)
    {
      printk ("scsi%d : got parity error\n", hostno);
      st0x_aborted = DID_PARITY;
    }
#endif

    if (status_read & STAT_REQ)
    {
#if ((DEBUG & PHASE_ETC) == PHASE_ETC)
      if ((newphase = (status_read & REQ_MASK)) != phase)
      {
        phase = newphase;
        switch (phase)
        {
          case REQ_DATAOUT:
            printk ("scsi%d : phase = DATA OUT\n", hostno);
            break;
          case REQ_DATAIN:
            printk ("scsi%d : phase = DATA IN\n", hostno);
            break;
          case REQ_CMDOUT:
            printk ("scsi%d : phase = COMMAND OUT\n", hostno);
            break;
          case REQ_STATIN:
            printk ("scsi%d : phase = STATUS IN\n", hostno);
            break;
          case REQ_MSGOUT:
            printk ("scsi%d : phase = MESSAGE OUT\n", hostno);
            break;
          case REQ_MSGIN:
            printk ("scsi%d : phase = MESSAGE IN\n", hostno);
            break;
          default:
            printk ("scsi%d : phase = UNKNOWN\n", hostno);
            st0x_aborted = DID_ERROR;
        }
      }
#endif
      switch (status_read & REQ_MASK)
      {
        case REQ_DATAOUT:
/*
 * If we are in fast mode, then we simply splat the data out
 * in word-sized chunks as fast as we can.
 */

#ifdef FAST
          if (!len)
          {
#if 0
            printk ("scsi%d: underflow to target %d lun %d \n", hostno,
                    target, lun);
            st0x_aborted = DID_ERROR;
            fast = 0;
#endif
            break;
          }

          if (fast && transfersize && !(len % transfersize)
              && (len >= transfersize)
#ifdef FAST32
              && !(transfersize % 4)
#endif
            )
          {
#if (DEBUG & DEBUG_FAST)
            printk ("scsi%d : FAST transfer, underflow = %d, transfersize = %d\n"
                    "         len = %d, data = %08x\n",
                  hostno, SCint->underflow, SCint->transfersize, len, data);
#endif

/* SJT: Start. Fast Write */
#ifdef SEAGATE_USE_ASM
            __asm__(
                "cld\n\t"
#ifdef FAST32
                "shr $2, %%ecx\n\t"
                "1:\t"
                "lodsl\n\t"
                "movl %%eax, (%%edi)\n\t"
#else
                "1:\t"
                "lodsb\n\t"
                "movb %%al, (%%edi)\n\t"
#endif
                "loop 1b;"
/* output */    :
/* input */     : "D" (phys_to_virt(st0x_dr)), "S" (data), "c" (SCint->transfersize) 
/* clobbered */ : "eax", "ecx", "esi" );
#else /* SEAGATE_USE_ASM */
            {
#ifdef FAST32
              unsigned int *iop = phys_to_virt (st0x_dr);
              const unsigned int *dp = (unsigned int *) data;
              int xferlen = transfersize >> 2;
#else
              unsigned char *iop = phys_to_virt (st0x_dr);
              const unsigned char *dp = data;
              int xferlen = transfersize;
#endif
              for (; xferlen; --xferlen)
                *iop = *dp++;
            }
#endif /* SEAGATE_USE_ASM */
/* SJT: End */
            len -= transfersize;
            data += transfersize;
#if (DEBUG & DEBUG_FAST)
            printk ("scsi%d : FAST transfer complete len = %d data = %08x\n",
                    hostno, len, data);
#endif
          }
          else
#endif /* ifdef FAST */
          {
/*
 *    We loop as long as we are in a data out phase, there is data to send,
 *      and BSY is still active.
 */

/* SJT: Start. Slow Write. */
#ifdef SEAGATE_USE_ASM
/*
 *      We loop as long as we are in a data out phase, there is data to send, 
 *      and BSY is still active.
 */
/* Local variables : len = ecx , data = esi, 
                     st0x_cr_sr = ebx, st0x_dr =  edi
*/
            __asm__ (
            /* Test for any data here at all. */
                    "orl %%ecx, %%ecx\n\t"
                    "jz 2f\n\t"
                    "cld\n\t"
/*                    "movl " SYMBOL_NAME_STR(st0x_cr_sr) ", %%ebx\n\t"  */
/*                    "movl " SYMBOL_NAME_STR(st0x_dr) ", %%edi\n\t"  */
                "1:\t"
                    "movb (%%ebx), %%al\n\t"
            /* Test for BSY */
                    "test $1, %%al\n\t"
                    "jz 2f\n\t"
            /* Test for data out phase - STATUS & REQ_MASK should be 
               REQ_DATAOUT, which is 0. */
                    "test $0xe, %%al\n\t"
                    "jnz 2f\n\t"
            /* Test for REQ */      
                    "test $0x10, %%al\n\t"
                    "jz 1b\n\t"
                    "lodsb\n\t"
                    "movb %%al, (%%edi)\n\t"
                    "loop 1b\n\t"
                "2:\n"
/* output */    : "=S" (data), "=c" (len) 
/* input */     : "0" (data), "1" (len), "b" (phys_to_virt(st0x_cr_sr)), "D" (phys_to_virt(st0x_dr)) 
/* clobbered */ : "eax", "ebx", "edi"); 
#else /* SEAGATE_USE_ASM */
            while (len)
            {
              unsigned char stat;

              stat = STATUS;
              if (!(stat & STAT_BSY) || ((stat & REQ_MASK) != REQ_DATAOUT))
                break;
              if (stat & STAT_REQ)
              {
                WRITE_DATA (*data++);
                --len;
              }
            }
#endif /* SEAGATE_USE_ASM */
/* SJT: End. */
          }

          if (!len && nobuffs)
          {
            --nobuffs;
            ++buffer;
            len = buffer->length;
            data = (unsigned char *) buffer->address;
#if (DEBUG & DEBUG_SG)
            printk ("scsi%d : next scatter-gather buffer len = %d address = %08x\n",
                    hostno, len, data);
#endif
          }
          break;

        case REQ_DATAIN:
#ifdef SLOW_HANDSHAKE
          if (borken)
          {
#if (DEBUG & (PHASE_DATAIN))
            transfered += len;
#endif
            for (;
                 len && (STATUS & (REQ_MASK | STAT_REQ)) == (REQ_DATAIN |
                                                             STAT_REQ)
                 ; --len)
            {
              *data++ = DATA;
              borken_wait ();
            }
#if (DEBUG & (PHASE_DATAIN))
            transfered -= len;
#endif
          }
          else
#endif
#ifdef FAST
            if (fast && transfersize && !(len % transfersize) &&
                (len >= transfersize)
#ifdef FAST32
                && !(transfersize % 4)
#endif
            )
          {
#if (DEBUG & DEBUG_FAST)
            printk ("scsi%d : FAST transfer, underflow = %d, transfersize = %d\n"
                    "         len = %d, data = %08x\n",
                  hostno, SCint->underflow, SCint->transfersize, len, data);
#endif
/* SJT: Start. Fast Read */
#ifdef SEAGATE_USE_ASM
            __asm__(
                    "cld\n\t"
#ifdef FAST32
                    "shr $2, %%ecx\n\t"
                "1:\t"
                    "movl (%%esi), %%eax\n\t"
                    "stosl\n\t"
#else
                "1:\t"
                    "movb (%%esi), %%al\n\t"
                    "stosb\n\t"
#endif
                    "loop 1b\n\t"
/* output */        : 
/* input */         : "S" (phys_to_virt(st0x_dr)), "D" (data), "c" (SCint->transfersize) 
/* clobbered */     : "eax", "ecx", "edi");
#else /* SEAGATE_USE_ASM */
            {
#ifdef FAST32
              const unsigned int *iop = phys_to_virt (st0x_dr);
              unsigned int *dp = (unsigned int *) data;
              int xferlen = len >> 2;
#else
              const unsigned char *iop = phys_to_virt (st0x_dr);
              unsigned char *dp = data;
              int xferlen = len;
#endif
              for (; xferlen; --xferlen)
                *dp++ = *iop;
            }
#endif /* SEAGATE_USE_ASM */
/* SJT: End */
            len -= transfersize;
            data += transfersize;
#if (DEBUG & PHASE_DATAIN)
            printk ("scsi%d: transfered += %d\n", hostno, transfersize);
            transfered += transfersize;
#endif

#if (DEBUG & DEBUG_FAST)
            printk ("scsi%d : FAST transfer complete len = %d data = %08x\n",
                    hostno, len, data);
#endif
          }
          else
#endif
          {

#if (DEBUG & PHASE_DATAIN)
            printk ("scsi%d: transfered += %d\n", hostno, len);
            transfered += len;          /* Assume we'll transfer it all, then
                                           subtract what we *didn't* transfer */
#endif

/*
 *    We loop as long as we are in a data in phase, there is room to read,
 *      and BSY is still active
 */

/* SJT: Start. */
#ifdef SEAGATE_USE_ASM
/*
 *      We loop as long as we are in a data in phase, there is room to read, 
 *      and BSY is still active
 */
            /* Local variables : ecx = len, edi = data
                                 esi = st0x_cr_sr, ebx = st0x_dr */
            __asm__ (
            /* Test for room to read */
                "orl %%ecx, %%ecx\n\t"
                "jz 2f\n\t"
                "cld\n\t"
/*                "movl " SYMBOL_NAME_STR(st0x_cr_sr) ", %%esi\n\t"  */
/*                "movl " SYMBOL_NAME_STR(st0x_dr) ", %%ebx\n\t"  */
            "1:\t"
                "movb (%%esi), %%al\n\t"
            /* Test for BSY */
                "test $1, %%al\n\t"
                "jz 2f\n\t"
            /* Test for data in phase - STATUS & REQ_MASK should be REQ_DATAIN, 
               = STAT_IO, which is 4. */
                "movb $0xe, %%ah\n\t"      
                "andb %%al, %%ah\n\t"
                "cmpb $0x04, %%ah\n\t"
                "jne 2f\n\t"
            /* Test for REQ */      
                "test $0x10, %%al\n\t"
                "jz 1b\n\t"
                "movb (%%ebx), %%al\n\t"      
                "stosb\n\t"   
                "loop 1b\n\t"
            "2:\n"
/* output */    : "=D" (data), "=c" (len) 
/* input */     : "0" (data), "1" (len), "S" (phys_to_virt(st0x_cr_sr)), "b" (phys_to_virt(st0x_dr)) 
/* clobbered */ : "eax","ebx", "esi"); 
#else /* SEAGATE_USE_ASM */
            while (len)
            {
              unsigned char stat;

              stat = STATUS;
              if (!(stat & STAT_BSY) || ((stat & REQ_MASK) != REQ_DATAIN))
                break;
              if (stat & STAT_REQ)
              {
                *data++ = DATA;
                --len;
              }
            }
#endif /* SEAGATE_USE_ASM */
/* SJT: End. */
#if (DEBUG & PHASE_DATAIN)
            printk ("scsi%d: transfered -= %d\n", hostno, len);
            transfered -= len;          /* Since we assumed all of Len got  *
                                           transfered, correct our mistake */
#endif
          }

          if (!len && nobuffs)
          {
            --nobuffs;
            ++buffer;
            len = buffer->length;
            data = (unsigned char *) buffer->address;
#if (DEBUG & DEBUG_SG)
            printk ("scsi%d : next scatter-gather buffer len = %d address = %08x\n",
                    hostno, len, data);
#endif
          }

          break;

        case REQ_CMDOUT:
          while (((status_read = STATUS) & STAT_BSY) &&
                 ((status_read & REQ_MASK) == REQ_CMDOUT))
            if (status_read & STAT_REQ)
            {
              WRITE_DATA (*(const unsigned char *) cmnd);
              cmnd = 1 + (const unsigned char *) cmnd;
#ifdef SLOW_HANDSHAKE
              if (borken)
                borken_wait ();
#endif
            }
          break;

        case REQ_STATIN:
          status = DATA;
          break;

        case REQ_MSGOUT:
/*
 *    We can only have sent a MSG OUT if we requested to do this
 *      by raising ATTN.  So, we must drop ATTN.
 */

          WRITE_CONTROL (BASE_CMD | CMD_DRVR_ENABLE);
/*
 *    If we are reconnecting, then we must send an IDENTIFY message in
 *       response  to MSGOUT.
 */
          switch (reselect)
          {
            case CAN_RECONNECT:
              WRITE_DATA (IDENTIFY (1, lun));

#if (DEBUG & (PHASE_RESELECT | PHASE_MSGOUT))
              printk ("scsi%d : sent IDENTIFY message.\n", hostno);
#endif
              break;
#ifdef LINKED
            case LINKED_WRONG:
              WRITE_DATA (ABORT);
              linked_connected = 0;
              reselect = CAN_RECONNECT;
              goto connect_loop;
#if (DEBUG & (PHASE_MSGOUT | DEBUG_LINKED))
              printk ("scsi%d : sent ABORT message to cancel incorrect I_T_L nexus.\n", hostno);
#endif
#endif /* LINKED */
#if (DEBUG & DEBUG_LINKED)
              printk ("correct\n");
#endif
            default:
              WRITE_DATA (NOP);
              printk ("scsi%d : target %d requested MSGOUT, sent NOP message.\n", hostno, target);
          }
          break;

        case REQ_MSGIN:
          switch (message = DATA)
          {
            case DISCONNECT:
              should_reconnect = 1;
              current_data = data;      /* WDE add */
              current_buffer = buffer;
              current_bufflen = len;    /* WDE add */
              current_nobuffs = nobuffs;
#ifdef LINKED
              linked_connected = 0;
#endif
              done = 1;
#if (DEBUG & (PHASE_RESELECT | PHASE_MSGIN))
              printk ("scsi%d : disconnected.\n", hostno);
#endif
              break;

#ifdef LINKED
            case LINKED_CMD_COMPLETE:
            case LINKED_FLG_CMD_COMPLETE:
#endif
            case COMMAND_COMPLETE:
/*
 * Note : we should check for underflow here.
 */
#if (DEBUG & PHASE_MSGIN)
              printk ("scsi%d : command complete.\n", hostno);
#endif
              done = 1;
              break;
            case ABORT:
#if (DEBUG & PHASE_MSGIN)
              printk ("scsi%d : abort message.\n", hostno);
#endif
              done = 1;
              break;
            case SAVE_POINTERS:
              current_buffer = buffer;
              current_bufflen = len;    /* WDE add */
              current_data = data;      /* WDE mod */
              current_nobuffs = nobuffs;
#if (DEBUG & PHASE_MSGIN)
              printk ("scsi%d : pointers saved.\n", hostno);
#endif
              break;
            case RESTORE_POINTERS:
              buffer = current_buffer;
              cmnd = current_cmnd;
              data = current_data;      /* WDE mod */
              len = current_bufflen;
              nobuffs = current_nobuffs;
#if (DEBUG & PHASE_MSGIN)
              printk ("scsi%d : pointers restored.\n", hostno);
#endif
              break;
            default:

/*
 *    IDENTIFY distinguishes itself from the other messages by setting the
 *      high byte.
 *
 *      Note : we need to handle at least one outstanding command per LUN,
 *      and need to hash the SCSI command for that I_T_L nexus based on the
 *      known ID (at this point) and LUN.
 */

              if (message & 0x80)
              {
#if (DEBUG & PHASE_MSGIN)
                printk ("scsi%d : IDENTIFY message received from id %d, lun %d.\n",
                        hostno, target, message & 7);
#endif
              }
              else
              {

/*
 *      We should go into a MESSAGE OUT phase, and send  a MESSAGE_REJECT
 *      if we run into a message that we don't like.  The seagate driver
 *      needs some serious restructuring first though.
 */

#if (DEBUG & PHASE_MSGIN)
                printk ("scsi%d : unknown message %d from target %d.\n",
                        hostno, message, target);
#endif
              }
          }
          break;

        default:
          printk ("scsi%d : unknown phase.\n", hostno);
          st0x_aborted = DID_ERROR;
      }                                 /* end of switch (status_read &
                                           REQ_MASK) */

#ifdef SLOW_HANDSHAKE
/*
 * I really don't care to deal with borken devices in each single
 * byte transfer case (ie, message in, message out, status), so
 * I'll do the wait here if necessary.
 */
      if (borken)
        borken_wait ();
#endif

    }                                   /* if(status_read & STAT_REQ) ends */
  }                                     /* while(((status_read = STATUS)...)
                                           ends */

#if (DEBUG & (PHASE_DATAIN | PHASE_DATAOUT | PHASE_EXIT))
  printk ("scsi%d : Transfered %d bytes\n", hostno, transfered);
#endif

#if (DEBUG & PHASE_EXIT)
#if 0                                   /* Doesn't work for scatter/gather */
  printk ("Buffer : \n");
  for (i = 0; i < 20; ++i)
    printk ("%02x  ", ((unsigned char *) data)[i]);     /* WDE mod */
  printk ("\n");
#endif
  printk ("scsi%d : status = ", hostno);
  print_status (status);
  printk ("message = %02x\n", message);
#endif

/* We shouldn't reach this until *after* BSY has been deasserted */
#ifdef notyet
  if (st0x_aborted)
  {
    if (STATUS & STAT_BSY)
    {
      seagate_st0x_reset (NULL);
      st0x_aborted = DID_RESET;
    }
    abort_confirm = 1;
  }
#endif

#ifdef LINKED
  else
  {
/*
 * Fix the message byte so that unsuspecting high level drivers don't
 * puke when they see a LINKED COMMAND message in place of the COMMAND
 * COMPLETE they may be expecting.  Shouldn't be necessary, but it's
 * better to be on the safe side.
 *
 * A non LINKED* message byte will indicate that the command completed,
 * and we are now disconnected.
 */

    switch (message)
    {
      case LINKED_CMD_COMPLETE:
      case LINKED_FLG_CMD_COMPLETE:
        message = COMMAND_COMPLETE;
        linked_target = current_target;
        linked_lun = current_lun;
        linked_connected = 1;
#if (DEBUG & DEBUG_LINKED)
        printk ("scsi%d : keeping I_T_L nexus established for linked command.\n",
                hostno);
#endif
    /* We also will need to adjust status to accommodate intermediate
       conditions. */
        if ((status == INTERMEDIATE_GOOD) ||
            (status == INTERMEDIATE_C_GOOD))
          status = GOOD;

        break;
/*
 * We should also handle what are "normal" termination messages
 * here (ABORT, BUS_DEVICE_RESET?, and COMMAND_COMPLETE individually,
 * and flake if things aren't right.
 */
      default:
#if (DEBUG & DEBUG_LINKED)
        printk ("scsi%d : closing I_T_L nexus.\n", hostno);
#endif
        linked_connected = 0;
    }
  }
#endif /* LINKED */

  if (should_reconnect)
  {
#if (DEBUG & PHASE_RESELECT)
    printk ("scsi%d : exiting seagate_st0x_queue_command() with reconnect enabled.\n",
            hostno);
#endif
    WRITE_CONTROL (BASE_CMD | CMD_INTR);
  }
  else
    WRITE_CONTROL (BASE_CMD);

  return retcode (st0x_aborted);
}                                       /* end of internal_command */

int seagate_st0x_abort (Scsi_Cmnd * SCpnt)
{
  st0x_aborted = DID_ABORT;
  return SCSI_ABORT_PENDING;
}

/*
   the seagate_st0x_reset function resets the SCSI bus */

int seagate_st0x_reset (Scsi_Cmnd * SCpnt, unsigned int reset_flags)
{
  unsigned clock;

/* No timeouts - this command is going to fail because it was reset. */
#ifdef DEBUG
  printk ("In seagate_st0x_reset()\n");
#endif

/* assert  RESET signal on SCSI bus.  */
  WRITE_CONTROL (BASE_CMD | CMD_RST);
  clock = jiffies + 2;

/* Wait.  */
  while (jiffies < clock) ;

  WRITE_CONTROL (BASE_CMD);
  st0x_aborted = DID_RESET;

#ifdef DEBUG
  printk ("SCSI bus reset.\n");
#endif
  return SCSI_RESET_WAKEUP;
}


int seagate_st0x_biosparam (Disk * disk, kdev_t dev, int *ip)
{
  unsigned char buf[256 + sizeof (Scsi_Ioctl_Command)], 
                cmd[6], *data, *page;
  Scsi_Ioctl_Command *sic = (Scsi_Ioctl_Command *) buf;
  int result, formatted_sectors, total_sectors;
  int cylinders, heads, sectors;
  int capacity;

/*
 * Only SCSI-I CCS drives and later implement the necessary mode sense
 * pages.
 */

  if (disk->device->scsi_level < 2)
    return -1;

  data = sic->data;

  cmd[0] = MODE_SENSE;
  cmd[1] = (disk->device->lun << 5) & 0xe5;
  cmd[2] = 0x04;                        /* Read page 4, rigid disk geometry
                                           page current values */
  cmd[3] = 0;
  cmd[4] = 255;
  cmd[5] = 0;

/*
 * We are transferring 0 bytes in the out direction, and expect to get back
 * 24 bytes for each mode page.
 */
  sic->inlen = 0;
  sic->outlen = 256;

  memcpy (data, cmd, 6);

  if (!(result = kernel_scsi_ioctl (disk->device, SCSI_IOCTL_SEND_COMMAND,
                                    sic)))
  {
/*
 * The mode page lies beyond the MODE SENSE header, with length 4, and
 * the BLOCK DESCRIPTOR, with length header[3].
 */
    page = data + 4 + data[3];
    heads = (int) page[5];
    cylinders = (page[2] << 16) | (page[3] << 8) | page[4];

    cmd[2] = 0x03;                      /* Read page 3, format page current
                                           values */
    memcpy (data, cmd, 6);

    if (!(result = kernel_scsi_ioctl (disk->device, SCSI_IOCTL_SEND_COMMAND,
                                      sic)))
    {
      page = data + 4 + data[3];
      sectors = (page[10] << 8) | page[11];
/*
 * Get the total number of formatted sectors from the block descriptor,
 * so we can tell how many are being used for alternates.
 */
      formatted_sectors = (data[4 + 1] << 16) | (data[4 + 2] << 8) 
                          | data[4 + 3];

      total_sectors = (heads * cylinders * sectors);

/*
 * Adjust the real geometry by subtracting
 * (spare sectors / (heads * tracks)) cylinders from the number of cylinders.
 *
 * It appears that the CE cylinder CAN be a partial cylinder.
 */

      printk ("scsi%d : heads = %d cylinders = %d sectors = %d total = %d formatted = %d\n",
              hostno, heads, cylinders, sectors, total_sectors,
              formatted_sectors);

      if (!heads || !sectors || !cylinders)
        result = -1;
      else
        cylinders -= ((total_sectors - formatted_sectors) / (heads * sectors));

/*
 * Now, we need to do a sanity check on the geometry to see if it is
 * BIOS compatible.  The maximum BIOS geometry is 1024 cylinders *
 * 256 heads * 64 sectors.
 */

      if ((cylinders > 1024) || (sectors > 64))
      {
        /* The Seagate's seem to have some mapping.  Multiply
           heads*sectors*cyl to get capacity.  Then start rounding down.
         */
        capacity = heads * sectors * cylinders;         
      
        /* Old MFM Drives use this, so does the Seagate */
        sectors = 17;
        heads = 2;
        capacity = capacity / sectors;
        while (cylinders > 1024)
        {
          heads *= 2;                   /* For some reason, they go in
                                           multiples */
          cylinders = capacity / heads;
        }
      }
      ip[0] = heads;
      ip[1] = sectors;
      ip[2] = cylinders;
/*
 * There should be an alternate mapping for things the seagate doesn't
 * understand, but I couldn't say what it is with reasonable certainty.
 */
    }
  }

  return result;
}

#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = SEAGATE_ST0X;

#include "scsi_module.c"
#endif
