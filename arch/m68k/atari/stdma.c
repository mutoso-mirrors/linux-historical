
/*
 *  linux/atari/stmda.c
 *
 *  Copyright (C) 1994 Roman Hodek
 *
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */


/* This file contains some function for controlling the access to the  */
/* ST-DMA chip that may be shared between devices. Currently we have:  */
/*   TT:     Floppy and ACSI bus                                       */
/*   Falcon: Floppy and SCSI                                           */
/*                                                                     */
/* The controlling functions set up a wait queue for access to the     */
/* ST-DMA chip. Callers to stdma_lock() that cannot granted access are */
/* put onto a queue and waked up later if the owner calls              */
/* stdma_release(). Additionally, the caller gives his interrupt       */
/* service routine to stdma_lock().                                    */
/*                                                                     */
/* On the Falcon, the IDE bus uses just the ACSI/Floppy interrupt, but */
/* not the ST-DMA chip itself. So falhd.c needs not to lock the        */
/* chip. The interrupt is routed to falhd.c if IDE is configured, the  */
/* model is a Falcon and the interrupt was caused by the HD controller */
/* (can be determined by looking at its status register).              */


#include <linux/types.h>
#include <linux/genhd.h>
#include <linux/sched.h>
#include <asm/bootinfo.h>
#include <asm/atari_stdma.h>
#include <asm/atariints.h>
#include <asm/atarihw.h>
#include <asm/atarihdreg.h>
#include <asm/io.h>
#include <asm/irq.h>

static int stdma_locked = 0;			/* the semaphore */
static isrfunc	stdma_isr = NULL;		/* int func to be called */
static void	*stdma_isr_data = NULL;		/* data passed to isr */
static struct wait_queue *stdma_wait = NULL;	/* wait queue for ST-DMA */




/***************************** Prototypes *****************************/

static void stdma_int (int irq, struct pt_regs *fp, void *dummy);

/************************* End of Prototypes **************************/



/*
 * Function: void stdma_lock( isrfunc isr, void *data )
 *
 * Purpose: Trys to get a lock on the ST-DMA chip that is used by more
 *   then one device driver. Waits on stdma_wait until lock is free.
 *   stdma_lock() may not be called from an interrupt! You have to
 *   get the lock in your main routine and release it when your
 *   request is finished.
 *
 * Inputs: A interupt function that is called until the lock is
 *   released.
 *
 * Returns: nothing
 *
 */

void stdma_lock(isrfunc isr, void *data)
{
	unsigned long	oldflags;

	save_flags(oldflags);
	cli();		/* protect lock */

	while(stdma_locked)
		/* Since the DMA is used for file system purposes, we
		 have to sleep uninterruptible (there may be locked
		 buffers) */
		sleep_on(&stdma_wait);

	stdma_locked   = 1;
	stdma_isr      = isr;
	stdma_isr_data = data;
	restore_flags(oldflags);
}


/*
 * Function: void stdma_release( void )
 *
 * Purpose: Releases the lock on the ST-DMA chip. 
 *
 * Inputs: none
 *
 * Returns: nothing
 *
 */

void stdma_release(void)
{
	unsigned long	oldflags;

	save_flags(oldflags);
	cli();
	
	stdma_locked   = 0;
	stdma_isr      = NULL;
	stdma_isr_data = NULL;
	wake_up(&stdma_wait);

	restore_flags(oldflags);
}


/*
 * Function: int stdma_others_waiting( void )
 *
 * Purpose: Check if someone waits for the ST-DMA lock.
 *
 * Inputs: none
 *
 * Returns: 0 if noone is waiting, != 0 otherwise
 *
 */

int stdma_others_waiting(void)
{
	return stdma_wait != NULL;
}


/*
 * Function: int stdma_islocked( void )
 *
 * Purpose: Check if the ST-DMA is currently locked.
 * Note: Returned status is only valid if ints are disabled while calling and
 *       as long as they remain disabled.
 *       If called with ints enabled, status can change only from locked to
 *       unlocked, because ints may not lock the ST-DMA.
 *
 * Inputs: none
 *
 * Returns: != 0 if locked, 0 otherwise
 *
 */

int stdma_islocked(void)
{
	return stdma_locked;
}


/*
 * Function: void stdma_init( void )
 *
 * Purpose: Initialize the ST-DMA chip access controlling.
 *   It sets up the interrupt and its service routine. The int is registered
 *   as slow int, client devices have to live with that (no problem
 *   currently).
 *
 * Inputs: none
 *
 * Return: nothing
 *
 */

void stdma_init(void)
{
	stdma_isr = NULL;
	add_isr(IRQ_MFP_FDC, stdma_int, IRQ_TYPE_SLOW, NULL,
		"ST-DMA: floppy/ACSI/IDE/Falcon-SCSI");
}


/*
 * Function: void stdma_int()
 *
 * Purpose: The interupt routine for the ST-DMA. It calls the isr
 *   registered by stdma_lock().
 *
 */

static void stdma_int(int irq, struct pt_regs *fp, void *dummy)
{
  if (stdma_isr)
      (*stdma_isr)(irq, fp, stdma_isr_data);
}
