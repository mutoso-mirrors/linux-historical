/*+M*************************************************************************
 * Perceptive Solutions, Inc. PCI-2000 device driver proc support for Linux.
 *
 * Copyright (c) 1997 Perceptive Solutions, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 *	File Name:		pci2000i.c
 *
 *-M*************************************************************************/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <asm/dma.h>
#include <asm/system.h>
#include <asm/io.h>
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"

#include "pci2000.h"
#include "psi_roy.h"

#include<linux/stat.h>

struct proc_dir_entry Proc_Scsi_Pci2000 =
	{ PROC_SCSI_PCI2000, 7, "pci2000", S_IFDIR | S_IRUGO | S_IXUGO, 2 };

//#define DEBUG 1

#ifdef DEBUG
#define DEB(x) x
#define STOP_HERE	{int st;for(st=0;st<100;st++){st=1;}}
#else
#define DEB(x)
#define STOP_HERE
#endif

typedef struct
	{
	ULONG		address;
	ULONG		length;
	}	SCATGATH, *PSCATGATH;

typedef struct
	{
	Scsi_Cmnd	*SCpnt;
	SCATGATH	 scatGath[16];
	UCHAR		 tag;
	}	DEV2000, *PDEV2000;

typedef struct
	{
	USHORT		 basePort;
	USHORT		 mb0;
	USHORT		 mb1;
	USHORT		 mb2;
	USHORT		 mb3;
	USHORT		 mb4;
	USHORT		 cmd;
	USHORT		 tag;
	DEV2000	 	 dev[MAX_BUS][MAX_UNITS];
	}	ADAPTER2000, *PADAPTER2000;

#define HOSTDATA(host) ((PADAPTER2000)&host->hostdata)


static struct	Scsi_Host 	   *PsiHost[MAXADAPTER] = {NULL,};  // One for each adapter
static			int				NumAdapters = 0;

/****************************************************************
 *	Name:			WaitReady	:LOCAL
 *
 *	Description:	Wait for controller ready.
 *
 *	Parameters:		padapter - Pointer adapter data structure.
 *
 *	Returns:		TRUE on not ready.
 *
 ****************************************************************/
static int WaitReady (PADAPTER2000 padapter)
	{
	ULONG	timer;

	timer = jiffies + TIMEOUT_COMMAND;								// calculate the timeout value
	do	{
		if ( !inb_p (padapter->cmd) )
			return FALSE;
		}	while ( timer > jiffies );									// test for timeout
	return TRUE;
	}
/****************************************************************
 *	Name:	OpDone	:LOCAL
 *
 *	Description:	Clean up operation and issue done to caller.
 *
 *	Parameters:		SCpnt	- Pointer to SCSI command structure.
 *					status	- Caller status.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void OpDone (Scsi_Cmnd *SCpnt, ULONG status)
	{
	SCpnt->result = status;
	SCpnt->scsi_done (SCpnt);
	}
/****************************************************************
 *	Name:	Command		:LOCAL
 *
 *	Description:	Issue queued command to the PCI-2000.
 *
 *	Parameters:		padapter - Pointer to adapter information structure.
 *					cmd		 - PCI-2000 command byte.
 *
 *	Returns:		Non-zero command tag if operation is accepted.
 *
 ****************************************************************/
static UCHAR Command (PADAPTER2000 padapter, UCHAR cmd)
	{
	outb_p (cmd, padapter->cmd);
	if ( WaitReady (padapter) )
		return 0;

	if ( inw_p (padapter->mb0) )
		return 0;

	return inb_p (padapter->mb1);
	}
/****************************************************************
 *	Name:	BuildSgList		:LOCAL
 *
 *	Description:	Build the scatter gather list for controller.
 *
 *	Parameters:		SCpnt	 - Pointer to SCSI command structure.
 *					padapter - Pointer to adapter information structure.
 *					pdev	 - Pointer to adapter device structure.
 *
 *	Returns:		Non-zero in not scatter gather.
 *
 ****************************************************************/
static int BuildSgList (Scsi_Cmnd *SCpnt, PADAPTER2000 padapter, PDEV2000 pdev)
	{
	int	z;

	if ( SCpnt->use_sg )
		{
		for ( z = 0;  z < SCpnt->use_sg;  z++ )
			{
			pdev->scatGath[z].address = virt_to_bus (((struct scatterlist *)SCpnt->request_buffer)[z].address);
			pdev->scatGath[z].length = ((struct scatterlist *)SCpnt->request_buffer)[z].length;
			}
		outl (virt_to_bus (pdev->scatGath), padapter->mb2);
		outl ((SCpnt->use_sg << 24) | SCpnt->request_bufflen, padapter->mb3);
		return FALSE;
		}
	outl (virt_to_bus (SCpnt->request_buffer), padapter->mb2);
	outl (SCpnt->request_bufflen, padapter->mb3);
	return TRUE;
	}
/****************************************************************
 *	Name:	Irq_Handler	:LOCAL
 *
 *	Description:	Interrupt handler.
 *
 *	Parameters:		irq		- Hardware IRQ number.
 *					dev_id	-
 *					regs	-
 *
 *	Returns:		TRUE if drive is not ready in time.
 *
 ****************************************************************/
static void Irq_Handler (int irq, void *dev_id, struct pt_regs *regs)
	{
	struct Scsi_Host   *shost = NULL;	// Pointer to host data block
	PADAPTER2000		padapter;		// Pointer to adapter control structure
	PDEV2000			pdev;
	Scsi_Cmnd		   *SCpnt;
	UCHAR				tag = 0;
	UCHAR				tag0;
	ULONG				error;
	int					pun;
	int					bus;
	int					z;

	DEB(printk ("\npci2000 recieved interrupt "));
	for ( z = 0; z < NumAdapters;  z++ )										// scan for interrupt to process
		{
		if ( PsiHost[z]->irq == (UCHAR)(irq & 0xFF) )
			{
			tag = inb_p (HOSTDATA(PsiHost[z])->tag);
			if (  tag )
				{
				shost = PsiHost[z];
				break;
				}
			}
		}

	if ( !shost )
		{
		DEB (printk ("\npci2000: not my interrupt"));
		return;
		}

	padapter = HOSTDATA(shost);

	tag0 = tag & 0x7F;															// mask off the error bit
	for ( bus = 0;  bus < MAX_BUS;  bus++ )										// scan the busses
    	{
		for ( pun = 0;  pun < MAX_UNITS;  pun++ )								// scan the targets
    		{
			pdev = &padapter->dev[bus][pun];
			if ( !pdev->tag )
    			continue;
			if ( pdev->tag == tag0 )											// is this it?
				{
				pdev->tag = 0;
				SCpnt = pdev->SCpnt;
				goto irqProceed;
    			}
			}
    	}

	outb_p (0xFF, padapter->tag);												// clear the op interrupt
	outb_p (CMD_DONE, padapter->cmd);											// complete the op
	return;																		// done, but, with what?

irqProceed:;
	if ( tag & ERR08_TAGGED )												// is there an error here?
		{
		if ( WaitReady (padapter) )
			{
			OpDone (SCpnt, DID_TIME_OUT << 16);
			return;
			}

		outb_p (tag0, padapter->mb0);										// get real error code
		outb_p (CMD_ERROR, padapter->cmd);
		if ( WaitReady (padapter) )											// wait for controller to suck up the op
			{
			OpDone (SCpnt, DID_TIME_OUT << 16);
			return;
			}

		error = inl (padapter->mb0);										// get error data
		outb_p (0xFF, padapter->tag);										// clear the op interrupt
		outb_p (CMD_DONE, padapter->cmd);									// complete the op

		DEB (printk ("status: %lX ", error));
		if ( error == 0x00020002 )											// is this error a check condition?
			{
			if ( bus )														// are we doint SCSI commands?
				{
				OpDone (SCpnt, (DID_OK << 16) | 2);
				return;
				}
			if ( *SCpnt->cmnd == SCSIOP_TEST_UNIT_READY )
				OpDone (SCpnt, (DRIVER_SENSE << 24) | (DID_OK << 16) | 2);	// test caller we have sense data too
			else
				OpDone (SCpnt, DID_ERROR << 16);
			return;
			}
		OpDone (SCpnt, DID_ERROR << 16);
		return;
		}

	outb_p (0xFF, padapter->tag);											// clear the op interrupt
	outb_p (CMD_DONE, padapter->cmd);										// complete the op
	OpDone (SCpnt, DID_OK << 16);
	}
/****************************************************************
 *	Name:	Pci2220i_QueueCommand
 *
 *	Description:	Process a queued command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *					done  - Pointer to done function to call.
 *
 *	Returns:		Status code.
 *
 ****************************************************************/
int Pci2000_QueueCommand (Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *))
	{
	UCHAR		   *cdb = (UCHAR *)SCpnt->cmnd;					// Pointer to SCSI CDB
	PADAPTER2000	padapter = HOSTDATA(SCpnt->host);			// Pointer to adapter control structure
	int				rc		 = -1;								// command return code
	UCHAR			bus		 = SCpnt->channel;
	UCHAR			pun		 = SCpnt->target;
	UCHAR			lun		 = SCpnt->lun;
	UCHAR			cmd;
	PDEV2000		pdev	 = &padapter->dev[bus][pun];

	if ( !done )
		{
		printk("pci2000_queuecommand: %02X: done can't be NULL\n", *cdb);
		return 0;
		}

	SCpnt->scsi_done = done;
	pdev->SCpnt = SCpnt;  									// Save this command data

	if ( WaitReady (padapter) )
		{
		rc = DID_ERROR;
		goto finished;
		}

	outw_p (pun | (lun << 8), padapter->mb0);

	if ( bus )
		{
		DEB (if(*cdb) printk ("\nCDB: %X-  %X %X %X %X %X %X %X %X %X %X ", SCpnt->cmd_len, cdb[0], cdb[1], cdb[2], cdb[3], cdb[4], cdb[5], cdb[6], cdb[7], cdb[8], cdb[9]));
		DEB (if(*cdb) printk ("\ntimeout_per_command: %d, timeout_total: %d, timeout: %d, internal_timout: %d", SCpnt->timeout_per_command,
							  SCpnt->timeout_total, SCpnt->timeout, SCpnt->internal_timeout));
		outl (SCpnt->timeout_per_command, padapter->mb1);
		outb_p (CMD_SCSI_TIMEOUT, padapter->cmd);
		if ( WaitReady (padapter) )
			{
			rc = DID_ERROR;
			goto finished;
			}

		outw_p (pun | (lun << 8), padapter->mb0);
		outw_p (SCpnt->cmd_len << 8, padapter->mb0 + 2);
		outl (virt_to_bus (cdb), padapter->mb1);
		if ( BuildSgList (SCpnt, padapter, pdev) )
			cmd = CMD_SCSI_THRU;
		else
			cmd = CMD_SCSI_THRU_SG;
		if ( (pdev->tag = Command (padapter, cmd)) == 0 )
			rc = DID_TIME_OUT;
		goto finished;
		}
	else
		{
		if ( lun )
			{
			rc = DID_BAD_TARGET;
			goto finished;
			}
		}

	switch ( *cdb )
		{
		case SCSIOP_INQUIRY:   					// inquiry CDB
			{
			if ( SCpnt->use_sg )
				{
				outl (virt_to_bus (((struct scatterlist *)(SCpnt->request_buffer))->address), padapter->mb2);
				}
			else
				{
				outl (virt_to_bus (SCpnt->request_buffer), padapter->mb2);
				}
			outl (SCpnt->request_bufflen, padapter->mb3);
			cmd = CMD_DASD_SCSI_INQ;
			break;
			}

		case SCSIOP_TEST_UNIT_READY:			// test unit ready CDB
			outl (virt_to_bus (SCpnt->sense_buffer), padapter->mb2);
			outl (sizeof (SCpnt->sense_buffer), padapter->mb3);
			cmd = CMD_TEST_READY;
			break;

		case SCSIOP_READ_CAPACITY:			  	// read capctiy CDB
			if ( SCpnt->use_sg )
				{
				outl (virt_to_bus (((struct scatterlist *)(SCpnt->request_buffer))->address), padapter->mb2);
				}
			else
				{
				outl (virt_to_bus (SCpnt->request_buffer), padapter->mb2);
				}
			outl (8, padapter->mb3);
			cmd = CMD_DASD_CAP;
			break;
		case SCSIOP_VERIFY:						// verify CDB
			outw_p ((USHORT)cdb[8] | ((USHORT)cdb[7] << 8), padapter->mb0 + 2);
			outl (XSCSI2LONG (&cdb[2]), padapter->mb1);
			cmd = CMD_READ_SG;
			break;
		case SCSIOP_READ:						// read10 CDB
			outw_p ((USHORT)cdb[8] | ((USHORT)cdb[7] << 8), padapter->mb0 + 2);
			outl (XSCSI2LONG (&cdb[2]), padapter->mb1);
			if ( BuildSgList (SCpnt, padapter, pdev) )
				cmd = CMD_READ;
			else
				cmd = CMD_READ_SG;
			break;
		case SCSIOP_READ6:						// read6  CDB
			outw_p (cdb[4], padapter->mb0 + 2);
			outl ((SCSI2LONG (&cdb[1])) & 0x001FFFFF, padapter->mb1);
			if ( BuildSgList (SCpnt, padapter, pdev) )
				cmd = CMD_READ;
			else
				cmd = CMD_READ_SG;
			break;
		case SCSIOP_WRITE:						// write10 CDB
			outw_p ((USHORT)cdb[8] | ((USHORT)cdb[7] << 8), padapter->mb0 + 2);
			outl (XSCSI2LONG (&cdb[2]), padapter->mb1);
			if ( BuildSgList (SCpnt, padapter, pdev) )
				cmd = CMD_WRITE;
			else
				cmd = CMD_WRITE_SG;
			break;
		case SCSIOP_WRITE6:						// write6  CDB
			outw_p (cdb[4], padapter->mb0 + 2);
			outl ((SCSI2LONG (&cdb[1])) & 0x001FFFFF, padapter->mb1);
			if ( BuildSgList (SCpnt, padapter, pdev) )
				cmd = CMD_WRITE;
			else
				cmd = CMD_WRITE_SG;
			break;
		case SCSIOP_START_STOP_UNIT:
			cmd = CMD_EJECT_MEDIA;
			break;
		case SCSIOP_MEDIUM_REMOVAL:
			switch ( cdb[4] )
				{
				case 0:
					cmd = CMD_UNLOCK_DOOR;
					break;
				case 1:
					cmd = CMD_LOCK_DOOR;
					break;
				default:
					cmd = 0;
					break;
				}
			if ( cmd )
				break;
		default:
			DEB (printk ("pci2220i_queuecommand: Unsupported command %02X\n", *cdb));
			OpDone (SCpnt, DID_ERROR << 16);
			return 0;
		}

	if ( (pdev->tag = Command (padapter, cmd)) == 0 )
		rc = DID_TIME_OUT;
finished:;
	if ( rc != -1 )
		OpDone (SCpnt, rc << 16);
	return 0;
	}
/****************************************************************
 *	Name:	internal_done :LOCAL
 *
 *	Description:	Done handler for non-queued commands
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *
 *	Returns:		Nothing.
 *
 ****************************************************************/
static void internal_done (Scsi_Cmnd * SCpnt)
	{
	SCpnt->SCp.Status++;
	}
/****************************************************************
 *	Name:	Pci2220i_Command
 *
 *	Description:	Process a command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *
 *	Returns:		Status code.
 *
 ****************************************************************/
int Pci2000_Command (Scsi_Cmnd *SCpnt)
	{
	DEB(printk("pci2000_command: ..calling pci2000_queuecommand\n"));

	Pci2000_QueueCommand (SCpnt, internal_done);

    SCpnt->SCp.Status = 0;
	while (!SCpnt->SCp.Status)
		barrier ();
	return SCpnt->result;
	}
/****************************************************************
 *	Name:	Pci2220i_Detect
 *
 *	Description:	Detect and initialize our boards.
 *
 *	Parameters:		tpnt - Pointer to SCSI host template structure.
 *
 *	Returns:		Number of adapters found.
 *
 ****************************************************************/
int Pci2000_Detect (Scsi_Host_Template *tpnt)
	{
	int					pci_index = 0;
	struct Scsi_Host   *pshost;
	PADAPTER2000	    padapter;
	int					z;
	int					setirq;

	if ( pcibios_present () )
		{
		for ( pci_index = 0;  pci_index <= MAXADAPTER;  ++pci_index )
			{
			UCHAR	pci_bus, pci_device_fn;

			if ( pcibios_find_device (VENDOR_PSI, DEVICE_ROY_1, pci_index, &pci_bus, &pci_device_fn) != 0 )
				break;

			pshost = scsi_register (tpnt, sizeof(ADAPTER2000));
			padapter = HOSTDATA(pshost);

			pcibios_read_config_word (pci_bus, pci_device_fn, PCI_BASE_ADDRESS_1, &padapter->basePort);
			padapter->basePort &= 0xFFFE;
			DEB (printk ("\nBase Regs = %#04X", padapter->basePort));			// get the base I/O port address
			padapter->mb0	= padapter->basePort + RTR_MAILBOX;		   			// get the 32 bit mail boxes
			padapter->mb1	= padapter->basePort + RTR_MAILBOX + 4;
			padapter->mb2	= padapter->basePort + RTR_MAILBOX + 8;
			padapter->mb3	= padapter->basePort + RTR_MAILBOX + 12;
			padapter->mb4	= padapter->basePort + RTR_MAILBOX + 16;
			padapter->cmd	= padapter->basePort + RTR_LOCAL_DOORBELL;			// command register
			padapter->tag	= padapter->basePort + RTR_PCI_DOORBELL;			// tag/response register

			if ( WaitReady (padapter) )
				goto unregister;
			outb_p (0x84, padapter->mb0);
			outb_p (CMD_SPECIFY, padapter->cmd);
			if ( WaitReady (padapter) )
				goto unregister;

			pcibios_read_config_byte (pci_bus, pci_device_fn, PCI_INTERRUPT_LINE, &pshost->irq);
			setirq = 1;
			for ( z = 0;  z < pci_index;  z++ )											// scan for shared interrupts
				{
				if ( PsiHost[z]->irq == pshost->irq )						// if shared then, don't posses
					setirq = 0;
				}
			if ( setirq )																// if not shared, posses
				{
				if ( request_irq (pshost->irq, Irq_Handler, 0, "pci2000", NULL) )
					{
					printk ("Unable to allocate IRQ for PSI-2000 controller.\n");
					goto unregister;
					}
				}
			PsiHost[pci_index]	= pshost;												// save SCSI_HOST pointer

			pshost->unique_id	= padapter->basePort;
			pshost->max_id		= 16;
			pshost->max_channel	= 1;

			printk("\nPSI-2000 EIDE CONTROLLER: at I/O = %X  IRQ = %d\n", padapter->basePort, pshost->irq);
			printk("(C) 1997 Perceptive Solutions, Inc. All rights reserved\n\n");
			continue;
unregister:;
			scsi_unregister (pshost);
			}
		}
	NumAdapters = pci_index;
	return pci_index;
	}
/****************************************************************
 *	Name:	Pci2220i_Abort
 *
 *	Description:	Process the Abort command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *
 *	Returns:		Allways snooze.
 *
 ****************************************************************/
int Pci2000_Abort (Scsi_Cmnd *SCpnt)
	{
	DEB (printk ("pci2000_abort\n"));
	return SCSI_ABORT_SNOOZE;
	}
/****************************************************************
 *	Name:	Pci2220i_Reset
 *
 *	Description:	Process the Reset command from the SCSI manager.
 *
 *	Parameters:		SCpnt - Pointer to SCSI command structure.
 *					flags - Flags about the reset command
 *
 *	Returns:		No active command at this time, so this means
 *					that each time we got some kind of response the
 *					last time through.  Tell the mid-level code to
 *					request sense information in order to decide what
 *					to do next.
 *
 ****************************************************************/
int Pci2000_Reset (Scsi_Cmnd *SCpnt, unsigned int reset_flags)
	{
	return SCSI_RESET_PUNT;
	}

#include "sd.h"

/****************************************************************
 *	Name:	Pci2220i_BiosParam
 *
 *	Description:	Process the biosparam request from the SCSI manager to
 *					return C/H/S data.
 *
 *	Parameters:		disk - Pointer to SCSI disk structure.
 *					dev	 - Major/minor number from kernel.
 *					geom - Pointer to integer array to place geometry data.
 *
 *	Returns:		zero.
 *
 ****************************************************************/
int Pci2000_BiosParam (Scsi_Disk *disk, kdev_t dev, int geom[])
	{
	PADAPTER2000	    padapter;

	padapter = HOSTDATA(disk->device->host);

	if ( WaitReady (padapter) )
		return 0;
	outb_p (disk->device->id, padapter->mb0);
	outb_p (CMD_GET_PARMS, padapter->cmd);
	if ( WaitReady (padapter) )
		return 0;

	geom[0] = inb_p (padapter->mb2 + 3);
	geom[1] = inb_p (padapter->mb2 + 2);
	geom[2] = inw_p (padapter->mb2);
	return 0;
	}


#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = PCI2220I;

#include "scsi_module.c"
#endif

