/******************************************************************************
*                  QLOGIC LINUX SOFTWARE
*
* QLogic  QLA1280 (Ultra2)  and  QLA12160 (Ultra3) SCSI driver
* Copyright (C) 2000 Qlogic Corporation (www.qlogic.com)
* Copyright (C) 2001-2003 Jes Sorensen, Wild Open Source Inc.
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU General Public License as published by the
* Free Software Foundation; either version 2, or (at your option) any
* later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
*
******************************************************************************/
#define QLA1280_VERSION      "3.23.35"
/*****************************************************************************
    Revision History:
    Rev  3.23.35 August 14, 2003, Jes Sorensen
	- Build against 2.6
    Rev  3.23.34 July 23, 2003, Jes Sorensen
	- Remove pointless TRUE/FALSE macros
	- Clean up vchan handling
    Rev  3.23.33 July 3, 2003, Jes Sorensen
	- Don't define register access macros before define determining MMIO.
	  This just happend to work out on ia64 but not elsewhere.
	- Don't try and read from the card while it is in reset as
	  it won't respond and causes an MCA
    Rev  3.23.32 June 23, 2003, Jes Sorensen
	- Basic support for boot time arguments
    Rev  3.23.31 June 8, 2003, Jes Sorensen
	- Reduce boot time messages
    Rev  3.23.30 June 6, 2003, Jes Sorensen
	- Do not enable sync/wide/ppr before it has been determined
	  that the target device actually supports it
	- Enable DMA arbitration for multi channel controllers
    Rev  3.23.29 June 3, 2003, Jes Sorensen
	- Port to 2.5.69
    Rev  3.23.28 June 3, 2003, Jes Sorensen
	- Eliminate duplicate marker commands on bus resets
	- Handle outstanding commands appropriately on bus/device resets
    Rev  3.23.27 May 28, 2003, Jes Sorensen
	- Remove bogus input queue code, let the Linux SCSI layer do the work
	- Clean up NVRAM handling, only read it once from the card
	- Add a number of missing default nvram parameters
    Rev  3.23.26 Beta May 28, 2003, Jes Sorensen
	- Use completion queue for mailbox commands instead of busy wait
    Rev  3.23.25 Beta May 27, 2003, James Bottomley
	- Migrate to use new error handling code
    Rev  3.23.24 Beta May 21, 2003, James Bottomley
	- Big endian support
	- Cleanup data direction code
    Rev  3.23.23 Beta May 12, 2003, Jes Sorensen
	- Switch to using MMIO instead of PIO
    Rev  3.23.22 Beta April 15, 2003, Jes Sorensen
	- Fix PCI parity problem with 12160 during reset.
    Rev  3.23.21 Beta April 14, 2003, Jes Sorensen
	- Use pci_map_page()/pci_unmap_page() instead of map_single version.
    Rev  3.23.20 Beta April 9, 2003, Jes Sorensen
	- Remove < 2.4.x support
	- Introduce HOST_LOCK to make the spin lock changes portable.
	- Remove a bunch of idiotic and unnecessary typedef's
	- Kill all leftovers of target-mode support which never worked anyway
    Rev  3.23.19 Beta April 11, 2002, Linus Torvalds
	- Do qla1280_pci_config() before calling request_irq() and
	  request_region()
	- Use pci_dma_hi32() to handle upper word of DMA addresses instead
	  of large shifts
	- Hand correct arguments to free_irq() in case of failure
    Rev  3.23.18 Beta April 11, 2002, Jes Sorensen
	- Run source through Lindent and clean up the output
    Rev  3.23.17 Beta April 11, 2002, Jes Sorensen
	- Update SCSI firmware to qla1280 v8.15.00 and qla12160 v10.04.32
    Rev  3.23.16 Beta March 19, 2002, Jes Sorensen
	- Rely on mailbox commands generating interrupts - do not
	  run qla1280_isr() from ql1280_mailbox_command()
	- Remove device_reg_t
	- Integrate ql12160_set_target_parameters() with 1280 version
	- Make qla1280_setup() non static
	- Do not call qla1280_check_for_dead_scsi_bus() on every I/O request
	  sent to the card - this command pauses the firmare!!!
    Rev  3.23.15 Beta March 19, 2002, Jes Sorensen
	- Clean up qla1280.h - remove obsolete QL_DEBUG_LEVEL_x definitions
	- Remove a pile of pointless and confusing (srb_t **) and
	  (scsi_lu_t *) typecasts
	- Explicit mark that we do not use the new error handling (for now)
	- Remove scsi_qla_host_t and use 'struct' instead
	- Remove in_abort, watchdog_enabled, dpc, dpc_sched, bios_enabled,
	  pci_64bit_slot flags which weren't used for anything anyway
	- Grab host->host_lock while calling qla1280_isr() from abort()
	- Use spin_lock()/spin_unlock() in qla1280_intr_handler() - we
	  do not need to save/restore flags in the interrupt handler
	- Enable interrupts early (before any mailbox access) in preparation
	  for cleaning up the mailbox handling
    Rev  3.23.14 Beta March 14, 2002, Jes Sorensen
	- Further cleanups. Remove all trace of QL_DEBUG_LEVEL_x and replace
	  it with proper use of dprintk().
	- Make qla1280_print_scsi_cmd() and qla1280_dump_buffer() both take
	  a debug level argument to determine if data is to be printed
	- Add KERN_* info to printk()
    Rev  3.23.13 Beta March 14, 2002, Jes Sorensen
	- Significant cosmetic cleanups
	- Change debug code to use dprintk() and remove #if mess
    Rev  3.23.12 Beta March 13, 2002, Jes Sorensen
	- More cosmetic cleanups, fix places treating return as function
	- use cpu_relax() in qla1280_debounce_register()
    Rev  3.23.11 Beta March 13, 2002, Jes Sorensen
	- Make it compile under 2.5.5
    Rev  3.23.10 Beta October 1, 2001, Jes Sorensen
	- Do no typecast short * to long * in QL1280BoardTbl, this
	  broke miserably on big endian boxes
    Rev  3.23.9 Beta September 30, 2001, Jes Sorensen
	- Remove pre 2.2 hack for checking for reentrance in interrupt handler
	- Make data types used to receive from SCSI_{BUS,TCN,LUN}_32
	  unsigned int to match the types from struct scsi_cmnd
    Rev  3.23.8 Beta September 29, 2001, Jes Sorensen
	- Remove bogus timer_t typedef from qla1280.h
	- Remove obsolete pre 2.2 PCI setup code, use proper #define's
	  for PCI_ values, call pci_set_master()
	- Fix memleak of qla1280_buffer on module unload
	- Only compile module parsing code #ifdef MODULE - should be
	  changed to use individual MODULE_PARM's later
	- Remove dummy_buffer that was never modified nor printed
	- ENTER()/LEAVE() are noops unless QL_DEBUG_LEVEL_3, hence remove
	  #ifdef QL_DEBUG_LEVEL_3/#endif around ENTER()/LEAVE() calls
	- Remove \r from print statements, this is Linux, not DOS
	- Remove obsolete QLA1280_{SCSILU,INTR,RING}_{LOCK,UNLOCK}
	  dummy macros
	- Remove C++ compile hack in header file as Linux driver are not
	  supposed to be compiled as C++
	- Kill MS_64BITS macro as it makes the code more readable
	- Remove unnecessary flags.in_interrupts bit
    Rev  3.23.7 Beta August 20, 2001, Jes Sorensen
	- Dont' check for set flags on q->q_flag one by one in qla1280_next()
        - Check whether the interrupt was generated by the QLA1280 before
          doing any processing
	- qla1280_status_entry(): Only zero out part of sense_buffer that
	  is not being copied into
	- Remove more superflouous typecasts
	- qla1280_32bit_start_scsi() replace home-brew memcpy() with memcpy()
    Rev  3.23.6 Beta August 20, 2001, Tony Luck, Intel
        - Don't walk the entire list in qla1280_putq_t() just to directly
	  grab the pointer to the last element afterwards
    Rev  3.23.5 Beta August 9, 2001, Jes Sorensen
	- Don't use SA_INTERRUPT, it's use is deprecated for this kinda driver
    Rev  3.23.4 Beta August 8, 2001, Jes Sorensen
	- Set dev->max_sectors to 1024
    Rev  3.23.3 Beta August 6, 2001, Jes Sorensen
	- Provide compat macros for pci_enable_device(), pci_find_subsys()
	  and scsi_set_pci_device()
	- Call scsi_set_pci_device() for all devices
	- Reduce size of kernel version dependant device probe code
	- Move duplicate probe/init code to seperate function
	- Handle error if qla1280_mem_alloc() fails
	- Kill OFFSET() macro and use Linux's PCI definitions instead
        - Kill private structure defining PCI config space (struct config_reg)
	- Only allocate I/O port region if not in MMIO mode
	- Remove duplicate (unused) sanity check of sife of srb_t
    Rev  3.23.2 Beta August 6, 2001, Jes Sorensen
	- Change home-brew memset() implementations to use memset()
        - Remove all references to COMTRACE() - accessing a PC's COM2 serial
          port directly is not legal under Linux.
    Rev  3.23.1 Beta April 24, 2001, Jes Sorensen
        - Remove pre 2.2 kernel support
        - clean up 64 bit DMA setting to use 2.4 API (provide backwards compat)
        - Fix MMIO access to use readl/writel instead of directly
          dereferencing pointers
        - Nuke MSDOS debugging code
        - Change true/false data types to int from uint8_t
        - Use int for counters instead of uint8_t etc.
        - Clean up size & byte order conversion macro usage
    Rev  3.23 Beta January 11, 2001 BN Qlogic
        - Added check of device_id when handling non
          QLA12160s during detect().
    Rev  3.22 Beta January 5, 2001 BN Qlogic
        - Changed queue_task() to schedule_task()
          for kernels 2.4.0 and higher.
          Note: 2.4.0-testxx kernels released prior to
                the actual 2.4.0 kernel release on January 2001
                will get compile/link errors with schedule_task().
                Please update your kernel to released 2.4.0 level,
                or comment lines in this file flagged with  3.22
                to resolve compile/link error of schedule_task().
        - Added -DCONFIG_SMP in addition to -D__SMP__
          in Makefile for 2.4.0 builds of driver as module.
    Rev  3.21 Beta January 4, 2001 BN Qlogic
        - Changed criteria of 64/32 Bit mode of HBA
          operation according to BITS_PER_LONG rather
          than HBA's NVRAM setting of >4Gig memory bit;
          so that the HBA auto-configures without the need
          to setup each system individually.
    Rev  3.20 Beta December 5, 2000 BN Qlogic
        - Added priority handling to IA-64  onboard SCSI
          ISP12160 chip for kernels greater than 2.3.18.
        - Added irqrestore for qla1280_intr_handler.
        - Enabled /proc/scsi/qla1280 interface.
        - Clear /proc/scsi/qla1280 counters in detect().
    Rev  3.19 Beta October 13, 2000 BN Qlogic
        - Declare driver_template for new kernel
          (2.4.0 and greater) scsi initialization scheme.
        - Update /proc/scsi entry for 2.3.18 kernels and
          above as qla1280
    Rev  3.18 Beta October 10, 2000 BN Qlogic
        - Changed scan order of adapters to map
          the QLA12160 followed by the QLA1280.
    Rev  3.17 Beta September 18, 2000 BN Qlogic
        - Removed warnings for 32 bit 2.4.x compiles
        - Corrected declared size for request and response
          DMA addresses that are kept in each ha
    Rev. 3.16 Beta  August 25, 2000   BN  Qlogic
        - Corrected 64 bit addressing issue on IA-64
          where the upper 32 bits were not properly
          passed to the RISC engine.
    Rev. 3.15 Beta  August 22, 2000   BN  Qlogic
        - Modified qla1280_setup_chip to properly load
          ISP firmware for greater that 4 Gig memory on IA-64
    Rev. 3.14 Beta  August 16, 2000   BN  Qlogic
        - Added setting of dma_mask to full 64 bit
          if flags.enable_64bit_addressing is set in NVRAM
    Rev. 3.13 Beta  August 16, 2000   BN  Qlogic
        - Use new PCI DMA mapping APIs for 2.4.x kernel
    Rev. 3.12       July 18, 2000    Redhat & BN Qlogic
        - Added check of pci_enable_device to detect() for 2.3.x
        - Use pci_resource_start() instead of
          pdev->resource[0].start in detect() for 2.3.x
        - Updated driver version
    Rev. 3.11       July 14, 2000    BN  Qlogic
	- Updated SCSI Firmware to following versions:
	  qla1x80:   8.13.08
	  qla1x160:  10.04.08
	- Updated driver version to 3.11
    Rev. 3.10    June 23, 2000   BN Qlogic
        - Added filtering of AMI SubSys Vendor ID devices
    Rev. 3.9
        - DEBUG_QLA1280 undefined and  new version  BN Qlogic
    Rev. 3.08b      May 9, 2000    MD Dell
        - Added logic to check against AMI subsystem vendor ID
	Rev. 3.08       May 4, 2000    DG  Qlogic
        - Added logic to check for PCI subsystem ID.
	Rev. 3.07       Apr 24, 2000    DG & BN  Qlogic
	   - Updated SCSI Firmware to following versions:
	     qla12160:   10.01.19
		 qla1280:     8.09.00
	Rev. 3.06       Apr 12, 2000    DG & BN  Qlogic
	   - Internal revision; not released
    Rev. 3.05       Mar 28, 2000    DG & BN  Qlogic
       - Edit correction for virt_to_bus and PROC.
    Rev. 3.04       Mar 28, 2000    DG & BN  Qlogic
       - Merge changes from ia64 port.
    Rev. 3.03       Mar 28, 2000    BN  Qlogic
       - Increase version to reflect new code drop with compile fix
         of issue with inclusion of linux/spinlock for 2.3 kernels
    Rev. 3.02       Mar 15, 2000    BN  Qlogic
       - Merge qla1280_proc_info from 2.10 code base
    Rev. 3.01       Feb 10, 2000    BN  Qlogic
       - Corrected code to compile on a 2.2.x kernel.
    Rev. 3.00       Jan 17, 2000    DG  Qlogic
	   - Added 64-bit support.
    Rev. 2.07       Nov 9, 1999     DG  Qlogic
	   - Added new routine to set target parameters for ISP12160.
    Rev. 2.06       Sept 10, 1999     DG  Qlogic
       - Added support for ISP12160 Ultra 3 chip.
    Rev. 2.03       August 3, 1999    Fred Lewis, Intel DuPont
	- Modified code to remove errors generated when compiling with
	  Cygnus IA64 Compiler.
        - Changed conversion of pointers to unsigned longs instead of integers.
        - Changed type of I/O port variables from uint32_t to unsigned long.
        - Modified OFFSET macro to work with 64-bit as well as 32-bit.
        - Changed sprintf and printk format specifiers for pointers to %p.
        - Changed some int to long type casts where needed in sprintf & printk.
        - Added l modifiers to sprintf and printk format specifiers for longs.
        - Removed unused local variables.
    Rev. 1.20       June 8, 1999      DG,  Qlogic
         Changes to support RedHat release 6.0 (kernel 2.2.5).
       - Added SCSI exclusive access lock (io_request_lock) when accessing
         the adapter.
       - Added changes for the new LINUX interface template. Some new error
         handling routines have been added to the template, but for now we
         will use the old ones.
    -   Initial Beta Release.
*****************************************************************************/


#include <linux/config.h>
#include <linux/module.h>

#include <linux/version.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/slab.h>
#include <linux/pci_ids.h>
#include <linux/interrupt.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/byteorder.h>
#include <asm/processor.h>
#include <asm/types.h>
#include <asm/system.h>

#if LINUX_VERSION_CODE < 0x020545
#include <linux/blk.h>
#include "sd.h"
#else
#include <scsi/scsi_host.h>
#endif
#include "scsi.h"
#include "hosts.h"

#if LINUX_VERSION_CODE < 0x020407
#error "Kernels older than 2.4.7 are no longer supported"
#endif


/*
 * Compile time Options:
 *            0 - Disable and 1 - Enable
 */
#define  QL1280_LUN_SUPPORT	0
#define  WATCHDOGTIMER		0

#define  DEBUG_QLA1280_INTR	0
#define  DEBUG_PRINT_NVRAM	0
#define  DEBUG_QLA1280		0

#ifdef	CONFIG_SCSI_QLOGIC_1280_PIO
#define	MEMORY_MAPPED_IO	0
#else
#define	MEMORY_MAPPED_IO	1
#endif

#define UNIQUE_FW_NAME
#include "qla1280.h"
#include "ql12160_fw.h"		/* ISP RISC codes */
#include "ql1280_fw.h"


/*
 * Missing PCI ID's
 */
#ifndef PCI_DEVICE_ID_QLOGIC_ISP1080
#define PCI_DEVICE_ID_QLOGIC_ISP1080	0x1080
#endif
#ifndef PCI_DEVICE_ID_QLOGIC_ISP1240
#define PCI_DEVICE_ID_QLOGIC_ISP1240	0x1240
#endif
#ifndef PCI_DEVICE_ID_QLOGIC_ISP1280
#define PCI_DEVICE_ID_QLOGIC_ISP1280	0x1280
#endif
#ifndef PCI_DEVICE_ID_QLOGIC_ISP10160
#define PCI_DEVICE_ID_QLOGIC_ISP10160	0x1016
#endif
#ifndef PCI_DEVICE_ID_QLOGIC_ISP12160
#define PCI_DEVICE_ID_QLOGIC_ISP12160	0x1216
#endif

#ifndef PCI_VENDOR_ID_AMI
#define PCI_VENDOR_ID_AMI               0x101e
#endif

#ifndef BITS_PER_LONG
#error "BITS_PER_LONG not defined!"
#endif
#if (BITS_PER_LONG == 64) || defined CONFIG_HIGHMEM
#define QLA_64BIT_PTR	1
#endif

#if defined(CONFIG_IA64_GENERIC) || defined(CONFIG_IA64_SGI_SN2)
#include <asm/sn/pci/pciio.h>
/* Ugly hack needed for the virtual channel fix on SN2 */
extern int snia_pcibr_rrb_alloc(struct pci_dev *pci_dev,
				int *count_vchan0, int *count_vchan1);
#endif

#ifdef QLA_64BIT_PTR
#define pci_dma_hi32(a)			((a >> 16) >> 16)
#else
#define pci_dma_hi32(a)			0
#endif
#define pci_dma_lo32(a)			(a & 0xffffffff)

#define NVRAM_DELAY()			udelay(500)	/* 2 microseconds */

#if LINUX_VERSION_CODE < 0x020500
#define HOST_LOCK			&io_request_lock
#define irqreturn_t			void
#define IRQ_RETVAL(foo)
#define MSG_ORDERED_TAG			1
static inline void
scsi_adjust_queue_depth(Scsi_Device *device, int tag, int depth)
{
	if (tag) {
		device->tagged_queue = tag;
		device->current_tag = 0;
	}
	device->queue_depth = depth;
}
#else
#define HOST_LOCK			ha->host->host_lock
#endif
#if defined(__ia64__) && !defined(ia64_platform_is)
#define ia64_platform_is(foo)		(!strcmp(x, platform_name))
#endif

/*
 *  QLogic Driver Support Function Prototypes.
 */
static void qla1280_done(struct scsi_qla_host *, struct srb **, struct srb **);
static void qla1280_done_q_put(struct srb *, struct srb **, struct srb **);
static int qla1280_slave_configure(Scsi_Device *);
#if LINUX_VERSION_CODE < 0x020545
static void qla1280_select_queue_depth(struct Scsi_Host *, Scsi_Device *);
void qla1280_get_target_options(struct scsi_cmnd *, struct scsi_qla_host *);
#endif

static int qla1280_return_status(struct response * sts, Scsi_Cmnd * cp);
static void qla1280_mem_free(struct scsi_qla_host *ha);
void qla1280_do_dpc(void *p);
static int qla1280_get_token(char *);
static inline void qla1280_enable_intrs(struct scsi_qla_host *);
static inline void qla1280_disable_intrs(struct scsi_qla_host *);

/*
 *  QLogic ISP1280 Hardware Support Function Prototypes.
 */
static int qla1280_initialize_adapter(struct scsi_qla_host *ha);
static int qla1280_isp_firmware(struct scsi_qla_host *);
static int qla1280_pci_config(struct scsi_qla_host *);
static int qla1280_chip_diag(struct scsi_qla_host *);
static int qla1280_setup_chip(struct scsi_qla_host *);
static int qla1280_init_rings(struct scsi_qla_host *);
static int qla1280_nvram_config(struct scsi_qla_host *);
static int qla1280_mailbox_command(struct scsi_qla_host *,
				   uint8_t, uint16_t *);
static int qla1280_bus_reset(struct scsi_qla_host *, int);
static int qla1280_device_reset(struct scsi_qla_host *, int, int);
static int qla1280_abort_device(struct scsi_qla_host *, int, int, int);
static int qla1280_abort_command(struct scsi_qla_host *, struct srb *, int);
static int qla1280_abort_isp(struct scsi_qla_host *);
static int qla1280_64bit_start_scsi(struct scsi_qla_host *, struct srb *);
static int qla1280_32bit_start_scsi(struct scsi_qla_host *, struct srb *);
static void qla1280_nv_write(struct scsi_qla_host *, uint16_t);
static void qla1280_poll(struct scsi_qla_host *);
static void qla1280_reset_adapter(struct scsi_qla_host *);
static void qla1280_marker(struct scsi_qla_host *, int, int, int, u8);
static void qla1280_isp_cmd(struct scsi_qla_host *);
irqreturn_t qla1280_intr_handler(int, void *, struct pt_regs *);
static void qla1280_isr(struct scsi_qla_host *, struct srb **, struct srb **);
static void qla1280_rst_aen(struct scsi_qla_host *);
static void qla1280_status_entry(struct scsi_qla_host *, struct response *,
				 struct srb **, struct srb **);
static void qla1280_error_entry(struct scsi_qla_host *, struct response *,
				struct srb **, struct srb **);
static uint16_t qla1280_get_nvram_word(struct scsi_qla_host *, uint32_t);
static uint16_t qla1280_nvram_request(struct scsi_qla_host *, uint32_t);
static uint16_t qla1280_debounce_register(volatile uint16_t *);
static request_t *qla1280_req_pkt(struct scsi_qla_host *);
static int qla1280_check_for_dead_scsi_bus(struct scsi_qla_host *,
					   unsigned int);
static int qla1280_mem_alloc(struct scsi_qla_host *ha);

static void qla12160_get_target_parameters(struct scsi_qla_host *,
					   Scsi_Device *);
static int qla12160_set_target_parameters(struct scsi_qla_host *, int, int);


static struct qla_driver_setup driver_setup __initdata;

/*
 * convert scsi data direction to request_t control flags
 */
static inline uint16_t
qla1280_data_direction(struct scsi_cmnd *cmnd)
{
	uint16_t flags = 0;

	switch(cmnd->sc_data_direction) {

	case SCSI_DATA_NONE:
		flags = 0;
		break;

	case SCSI_DATA_READ:
		flags = BIT_5;
		break;

	case SCSI_DATA_WRITE:
		flags = BIT_6;
		break;

	case SCSI_DATA_UNKNOWN:
	default:
		flags = BIT_5 | BIT_6;
		break;
	}
	return flags;
}
		
#if QL1280_LUN_SUPPORT
static void qla1280_enable_lun(struct scsi_qla_host *, int, int);
#endif

#if DEBUG_QLA1280
static void __qla1280_print_scsi_cmd(Scsi_Cmnd * cmd);
static void __qla1280_dump_buffer(char *, int);
#endif


/*
 * insmod needs to find the variable and make it point to something
 */
#ifdef MODULE
static char *qla1280;

/* insmod qla1280 options=verbose" */
MODULE_PARM(qla1280, "s");
#else
__setup("qla1280=", qla1280_setup);
#endif

MODULE_LICENSE("GPL");


/* We use the Scsi_Pointer structure that's included with each command
 * SCSI_Cmnd as a scratchpad for our SRB.
 *
 * SCp will always point to the SRB structure (defined in qla1280.h).
 * It is define as follows:
 *  - SCp.ptr  -- > pointer back to the cmd
 *  - SCp.this_residual --> used as forward pointer to next srb
 *  - SCp.buffer --> used as backward pointer to next srb
 *  - SCp.buffers_residual --> used as flags field
 *  - SCp.have_data_in --> not used
 *  - SCp.sent_command --> not used
 *  - SCp.phase --> not used
 */

#define	CMD_SP(Cmnd)		&Cmnd->SCp
#define	CMD_CDBLEN(Cmnd)	Cmnd->cmd_len
#define	CMD_CDBP(Cmnd)		Cmnd->cmnd
#define	CMD_SNSP(Cmnd)		Cmnd->sense_buffer
#define	CMD_SNSLEN(Cmnd)	sizeof(Cmnd->sense_buffer)
#define	CMD_RESULT(Cmnd)	Cmnd->result
#define	CMD_HANDLE(Cmnd)	Cmnd->host_scribble
#if LINUX_VERSION_CODE < 0x020545
#define	CMD_HOST(Cmnd)		Cmnd->host
#define CMD_REQUEST(Cmnd)	Cmnd->request.cmd
#define SCSI_BUS_32(Cmnd)	Cmnd->channel
#define SCSI_TCN_32(Cmnd)	Cmnd->target
#define SCSI_LUN_32(Cmnd)	Cmnd->lun
#else
#define	CMD_HOST(Cmnd)		Cmnd->device->host
#define CMD_REQUEST(Cmnd)	Cmnd->request->cmd
#define SCSI_BUS_32(Cmnd)	Cmnd->device->channel
#define SCSI_TCN_32(Cmnd)	Cmnd->device->id
#define SCSI_LUN_32(Cmnd)	Cmnd->device->lun
#endif

/*****************************************/
/*   ISP Boards supported by this driver */
/*****************************************/

#define NUM_OF_ISP_DEVICES	6

struct qla_boards {
	unsigned char name[9];	/* Board ID String */
	unsigned long device_id;	/* Device PCI ID   */
	int numPorts;		/* Number of SCSI ports */
	unsigned short *fwcode;	/* pointer to FW array         */
	unsigned short *fwlen;	/* number of words in array    */
	unsigned short *fwstart;	/* start address for F/W       */
	unsigned char *fwver;	/* Ptr to F/W version array    */
};

struct qla_boards ql1280_board_tbl[NUM_OF_ISP_DEVICES] = {
	/* Name ,  Board PCI Device ID,         Number of ports */
	{"QLA12160", PCI_DEVICE_ID_QLOGIC_ISP12160, 2,
	 &fw12160i_code01[0], &fw12160i_length01,
	 &fw12160i_addr01, &fw12160i_version_str[0]},
	{"QLA1080", PCI_DEVICE_ID_QLOGIC_ISP1080, 1,
	 &fw1280ei_code01[0], &fw1280ei_length01,
	 &fw1280ei_addr01, &fw1280ei_version_str[0]},
	{"QLA1240", PCI_DEVICE_ID_QLOGIC_ISP1240, 2,
	 &fw1280ei_code01[0], &fw1280ei_length01,
	 &fw1280ei_addr01, &fw1280ei_version_str[0]},
	{"QLA1280", PCI_DEVICE_ID_QLOGIC_ISP1280, 2,
	 &fw1280ei_code01[0], &fw1280ei_length01,
	 &fw1280ei_addr01, &fw1280ei_version_str[0]},
	{"QLA10160", PCI_DEVICE_ID_QLOGIC_ISP10160, 1,
	 &fw12160i_code01[0], &fw12160i_length01,
	 &fw12160i_addr01, &fw12160i_version_str[0]},
	{"        ", 0, 0}
};

static int qla1280_verbose = 1;
static struct scsi_qla_host *qla1280_hostlist;
static int qla1280_buffer_size;
static char *qla1280_buffer;

#if DEBUG_QLA1280
static int ql_debug_level = 1;
#define dprintk(level, format, a...)	\
	do { if (ql_debug_level >= level) printk(KERN_ERR format, ##a); } while(0)
#define qla1280_dump_buffer(level, buf, size)	\
	if (ql_debug_level >= level) __qla1280_dump_buffer(buf, size)
#define qla1280_print_scsi_cmd(level, cmd)	\
	if (ql_debug_level >= level) __qla1280_print_scsi_cmd(cmd)
#else
#define ql_debug_level			0
#define dprintk(level, format, a...)	do{}while(0)
#define qla1280_dump_buffer(a, b, c)	do{}while(0)
#define qla1280_print_scsi_cmd(a, b)	do{}while(0)
#endif

#define ENTER(x)		dprintk(3, "qla1280 : Entering %s()\n", x);
#define LEAVE(x)		dprintk(3, "qla1280 : Leaving %s()\n", x);
#define ENTER_INTR(x)		dprintk(4, "qla1280 : Entering %s()\n", x);
#define LEAVE_INTR(x)		dprintk(4, "qla1280 : Leaving %s()\n", x);


/*************************************************************************
 * qla1280_proc_info
 *
 * Description:
 *   Return information to handle /proc support for the driver.
 *
 * buffer - ptrs to a page buffer
 *
 * Returns:
 *************************************************************************/
#define	PROC_BUF	&qla1280_buffer[len]

#if LINUX_VERSION_CODE < 0x020600
int qla1280_proc_info(char *buffer, char **start, off_t offset, int length,
		      int hostno, int inout)
#else
int qla1280_proc_info(struct Scsi_Host *host, char *buffer, char **start,
		      off_t offset, int length, int inout)
#endif
{
	struct scsi_qla_host *ha;
	int size = 0;
	int len = 0;
	struct qla_boards *bdp;
#ifdef BOGUS_QUEUE
	struct scsi_lu *up;
	uint32_t b, t, l;
#endif
#if LINUX_VERSION_CODE >= 0x020600
	ha = (struct scsi_qla_host *)host->hostdata;
#else
	struct Scsi_Host *host;
	/* Find the host that was specified */
	for (ha = qla1280_hostlist; (ha != NULL)
		     && ha->host->host_no != hostno; ha = ha->next) ;

	/* if host wasn't found then exit */
	if (!ha) {
		size =  sprintf(buffer, "Can't find adapter for host "
				"number %d\n", hostno);
		if (size > length) {
			return size;
		} else {
			return 0;
		}
	}

	host = ha->host;
#endif

	if (inout)
		return -ENOSYS;

	/*
	 * if our old buffer is the right size use it otherwise
	 * allocate a new one.
	 */
	if (qla1280_buffer_size != PAGE_SIZE) {
		/* deallocate this buffer and get a new one */
		if (qla1280_buffer != NULL) {
			free_page((unsigned long)qla1280_buffer);
			qla1280_buffer_size = 0;
		}
		qla1280_buffer = (char *)get_zeroed_page(GFP_KERNEL);
	}
	if (qla1280_buffer == NULL) {
		size = sprintf(buffer, "qla1280 - kmalloc error at line %d\n",
			       __LINE__);
		return size;
	}
	/* save the size of our buffer */
	qla1280_buffer_size = PAGE_SIZE;

	/* 3.20 clear the buffer we use for proc display */
	memset(qla1280_buffer, 0, PAGE_SIZE);

	/* start building the print buffer */
	bdp = &ql1280_board_tbl[ha->devnum];
	size = sprintf(PROC_BUF,
		       "QLogic PCI to SCSI Adapter for ISP 1280/12160:\n"
		       "        Firmware version: %2d.%02d.%02d, Driver version %s\n",
		       bdp->fwver[0], bdp->fwver[1], bdp->fwver[2],
		       QLA1280_VERSION);

	len += size;

	size = sprintf(PROC_BUF, "SCSI Host Adapter Information: %s\n",
		       bdp->name);
	len += size;
	size = sprintf(PROC_BUF, "Request Queue count= 0x%x, Response "
		       "Queue count= 0x%x\n",
		       REQUEST_ENTRY_CNT, RESPONSE_ENTRY_CNT);
	len += size;
	size = sprintf(PROC_BUF, "Number of pending commands = 0x%lx\n",
		       ha->actthreads);
	len += size;
	size = sprintf(PROC_BUF, "Number of free request entries = %d\n",
		       ha->req_q_cnt);
	len += size;
	size = sprintf(PROC_BUF, "\n");	/* 1       */
	len += size;

	size = sprintf(PROC_BUF, "SCSI device Information:\n");
	len += size;
#ifdef BOGUS_QUEUE
	/* scan for all equipment stats */
	for (b = 0; b < MAX_BUSES; b++)
		for (t = 0; t < MAX_TARGETS; t++) {
			for (l = 0; l < MAX_LUNS; l++) {
				up = LU_Q(ha, b, t, l);
				if (up == NULL)
					continue;
				/* unused device/lun */
				if (up->io_cnt == 0 || up->io_cnt < 2)
					continue;
				/* total reads since boot */
				/* total writes since boot */
				/* total requests since boot  */
				size = sprintf (PROC_BUF,
						"(%2d:%2d:%2d): Total reqs %ld,",
						b, t, l, up->io_cnt);
				len += size;
				/* current number of pending requests */
				size =	sprintf(PROC_BUF, " Pend reqs %d,",
						up->q_outcnt);
				len += size;
#if 0
				/* avg response time */
				size = sprintf(PROC_BUF, " Avg resp time %ld%%,",
					       (up->resp_time / up->io_cnt) *
					       100);
				len += size;

				/* avg active time */
				size = sprintf(PROC_BUF,
					       " Avg active time %ld%%\n",
					       (up->act_time / up->io_cnt) * 100);
#else
				size = sprintf(PROC_BUF, "\n");
#endif
				len += size;
			}
			if (len >= qla1280_buffer_size)
				break;
		}
#endif

	if (len >= qla1280_buffer_size) {
		printk(KERN_WARNING
		       "qla1280: Overflow buffer in qla1280_proc.c\n");
	}

	if (offset > len - 1) {
		free_page((unsigned long) qla1280_buffer);
		qla1280_buffer = NULL;
		qla1280_buffer_size = length = 0;
		*start = NULL;
	} else {
		*start = &qla1280_buffer[offset];	/* Start of wanted data */
		if (len - offset < length) {
			length = len - offset;
		}
	}
	return length;
}


static int qla1280_read_nvram(struct scsi_qla_host *ha)
{
	uint16_t *wptr;
	uint8_t chksum;
	int cnt;
	struct nvram *nv;

	ENTER("qla1280_read_nvram");

	if (driver_setup.no_nvram)
		return 1;

	printk(KERN_INFO "scsi(%ld): Reading NVRAM\n", ha->host_no);

	wptr = (uint16_t *)&ha->nvram;
	nv = &ha->nvram;
	chksum = 0;
	for (cnt = 0; cnt < 3; cnt++) {
		*wptr = qla1280_get_nvram_word(ha, cnt);
		chksum += *wptr & 0xff;
		chksum += (*wptr >> 8) & 0xff;
		wptr++;
	}

	if (nv->id0 != 'I' || nv->id1 != 'S' ||
	    nv->id2 != 'P' || nv->id3 != ' ' || nv->version < 1) {
		dprintk(2, "Invalid nvram ID or version!\n");
		chksum = 1;
	} else {
		for (; cnt < sizeof(struct nvram); cnt++) {
			*wptr = qla1280_get_nvram_word(ha, cnt);
			chksum += *wptr & 0xff;
			chksum += (*wptr >> 8) & 0xff;
			wptr++;
		}
	}

	dprintk(3, "qla1280_read_nvram: NVRAM Magic ID= %c %c %c %02x"
	       " version %i\n", nv->id0, nv->id1, nv->id2, nv->id3,
	       nv->version);


	if (chksum) {
		if (!driver_setup.no_nvram)
			printk(KERN_WARNING "scsi(%ld): Unable to identify or "
			       "validate NVRAM checksum, using default "
			       "settings\n", ha->host_no);
		ha->nvram_valid = 0;
	} else
		ha->nvram_valid = 1;

	dprintk(1, "qla1280_read_nvram: Completed Reading NVRAM\n");
	LEAVE("qla1280_read_nvram");

	return chksum;
}


/**************************************************************************
 * qla1280_do_device_init
 *    This routine will register the device with the SCSI subsystem,
 *    initialize the host adapter structure and call the device init
 *    routines.
 *
 * Input:
 *     pdev      - pointer to struct pci_dev for adapter
 *     template  - pointer to SCSI template
 *     devnum    - the device number
 *     bdp       - pointer to struct _qlaboards
 *     num_hosts - the host number
 *
 * Returns:
 *  host - pointer to SCSI host structure
 **************************************************************************/
struct Scsi_Host *
qla1280_do_device_init(struct pci_dev *pdev, Scsi_Host_Template * template,
		       int devnum, struct qla_boards *bdp, int num_hosts)
{
	struct Scsi_Host *host;
	struct scsi_qla_host *ha;

	printk(KERN_INFO "qla1280: %s found on PCI bus %i, dev %i\n",
	       bdp->name, pdev->bus->number, PCI_SLOT(pdev->devfn));

#if LINUX_VERSION_CODE >= 0x020545
	template->slave_configure = qla1280_slave_configure;
#endif
	host = scsi_register(template, sizeof(struct scsi_qla_host));
	if (!host) {
		printk(KERN_WARNING
		       "qla1280: Failed to register host, aborting.\n");
		goto error;
	}

#if LINUX_VERSION_CODE < 0x020545
	scsi_set_pci_device(host, pdev);
#else
	scsi_set_device(host, &pdev->dev);
#endif
	ha = (struct scsi_qla_host *)host->hostdata;
	/* Clear our data area */
	memset(ha, 0, sizeof(struct scsi_qla_host));
	/* Sanitize the information from PCI BIOS.  */
	host->irq = pdev->irq;
	ha->pci_bus = pdev->bus->number;
	ha->pci_device_fn = pdev->devfn;
	ha->pdev = pdev;
	ha->device_id = bdp->device_id;
	ha->devnum = devnum;	/* specifies microcode load address */

	if (qla1280_mem_alloc(ha)) {
		printk(KERN_INFO "qla1x160: Failed to get memory\n");
		goto error;
	}

	ha->ports = bdp->numPorts;
	/* following needed for all cases of OS versions */
	ha->host = host;
	ha->host_no = host->host_no;

	host->can_queue = 0xfffff;	/* unlimited  */
	host->cmd_per_lun = 1;
	host->base = (unsigned long)ha->mmpbase;
	host->max_channel = bdp->numPorts - 1;
	host->max_lun = MAX_LUNS - 1;
	host->max_id = MAX_TARGETS;
	host->max_sectors = 1024;
#if LINUX_VERSION_CODE < 0x020545
	host->select_queue_depths = qla1280_select_queue_depth;
#endif

	ha->instance = num_hosts;
	host->unique_id = ha->instance;

	if (qla1280_pci_config(ha)) {
		printk(KERN_INFO "qla1x160: Unable to configure PCI\n");
		goto error_mem_alloced;
	}

	/* Disable ISP interrupts. */
	qla1280_disable_intrs(ha);

	/* Register the IRQ with Linux (sharable) */
	if (request_irq(host->irq, qla1280_intr_handler, SA_SHIRQ,
			"qla1280", ha)) {
		printk("qla1280 : Failed to reserve interrupt %d already "
		       "in use\n", host->irq);
		goto error_mem_alloced;
	}
#if !MEMORY_MAPPED_IO
	/* Register the I/O space with Linux */
	if (check_region(host->io_port, 0xff)) {
		printk("qla1280: Failed to reserve i/o region 0x%04lx-0x%04lx"
		       " already in use\n",
		       host->io_port, host->io_port + 0xff);
		free_irq(host->irq, ha);
		goto error_mem_alloced;
	}

	request_region(host->io_port, 0xff, "qla1280");
#endif

	/* load the F/W, read paramaters, and init the H/W */
	if (qla1280_initialize_adapter(ha)) {
		printk(KERN_INFO "qla1x160: Failed to initialize adapter\n");
		goto error_mem_alloced;
	}

	/* set our host ID  (need to do something about our two IDs) */
	host->this_id = ha->bus_settings[0].id;

	return host;

 error_mem_alloced:
	qla1280_mem_free(ha);

 error:
	if (host) {
		scsi_unregister(host);
	}
	return NULL;
}

/**************************************************************************
 * qla1280_detect
 *    This routine will probe for Qlogic 1280 SCSI host adapters.
 *    It returns the number of host adapters of a particular
 *    type that were found.	 It also initialize all data necessary for
 *    the driver.  It is passed-in the host number, so that it
 *    knows where its first entry is in the scsi_hosts[] array.
 *
 * Input:
 *     template - pointer to SCSI template
 *
 * Returns:
 *  num - number of host adapters found.
 **************************************************************************/
int
qla1280_detect(Scsi_Host_Template * template)
{
	struct pci_dev *pdev = NULL;
	struct Scsi_Host *host;
	struct scsi_qla_host *ha, *cur_ha;
	struct qla_boards *bdp;
	uint16_t subsys_vendor, subsys_device;
	int num_hosts = 0;
	int devnum = 0;

	ENTER("qla1280_detect");

	if (sizeof(struct srb) > sizeof(Scsi_Pointer)) {
		printk(KERN_WARNING
		       "qla1280_detect: [WARNING] struct srb too big\n");
		return 0;
	}
#ifdef MODULE
	/*
	 * If we are called as a module, the qla1280 pointer may not be null
	 * and it would point to our bootup string, just like on the lilo
	 * command line.  IF not NULL, then process this config string with
	 * qla1280_setup
	 *
	 * Boot time Options
	 * To add options at boot time add a line to your lilo.conf file like:
	 * append="qla1280=verbose,max_tags:{{255,255,255,255},{255,255,255,255}}"
	 * which will result in the first four devices on the first two
	 * controllers being set to a tagged queue depth of 32.
	 */
	if (qla1280)
		qla1280_setup(qla1280);
#endif

	bdp = &ql1280_board_tbl[0];
	qla1280_hostlist = NULL;
	template->proc_name = "qla1280";

	/* First Initialize QLA12160 on PCI Bus 1 Dev 2 */
	while ((pdev = pci_find_subsys(PCI_VENDOR_ID_QLOGIC, bdp->device_id,
				       PCI_ANY_ID, PCI_ANY_ID, pdev))) {

		/* find QLA12160 device on PCI bus=1 slot=2 */
		if ((pdev->bus->number != 1) || (PCI_SLOT(pdev->devfn) != 2))
			continue;

		/* Bypass all AMI SUBSYS VENDOR IDs */
		if (pdev->subsystem_vendor == PCI_VENDOR_ID_AMI) {
			printk(KERN_INFO
			       "qla1x160: Skip AMI SubSys Vendor ID Chip\n");
			continue;
		}

		if (pci_enable_device(pdev))
			goto find_devices;

		host = qla1280_do_device_init(pdev, template, devnum,
					      bdp, num_hosts);
		if (!host)
			continue;
		ha = (struct scsi_qla_host *)host->hostdata;

		/* this preferred device will always be the first one found */
		cur_ha = qla1280_hostlist = ha;
		num_hosts++;
	}

 find_devices:

	pdev = NULL;
	/* Try and find each different type of adapter we support */
	for (devnum = 0; bdp->device_id != 0 && devnum < NUM_OF_ISP_DEVICES;
	     devnum++, bdp++) {
		/* PCI_SUBSYSTEM_IDS supported */
		while ((pdev = pci_find_subsys(PCI_VENDOR_ID_QLOGIC,
					       bdp->device_id, PCI_ANY_ID,
					       PCI_ANY_ID, pdev))) {
			if (pci_enable_device(pdev))
				continue;
			/* found an adapter */
			subsys_vendor = pdev->subsystem_vendor;
			subsys_device = pdev->subsystem_device;

			/*
			 * skip QLA12160 already initialized on
			 * PCI Bus 1 Dev 2 since we already initialized
			 * and presented it
			 */
			if ((bdp->device_id == PCI_DEVICE_ID_QLOGIC_ISP12160)&&
			    (pdev->bus->number == 1) &&
			    (PCI_SLOT(pdev->devfn) == 2))
				continue;

			/* Bypass all AMI SUBSYS VENDOR IDs */
			if (subsys_vendor == PCI_VENDOR_ID_AMI) {
				printk(KERN_INFO
				       "qla1x160: Skip AMI SubSys Vendor ID Chip\n");
				continue;
			}
			dprintk(1, "qla1x160: Supported Device Found VID=%x "
			       "DID=%x SSVID=%x SSDID=%x\n", pdev->vendor,
			       pdev->device, subsys_vendor, subsys_device);

			host = qla1280_do_device_init(pdev, template,
						      devnum, bdp, num_hosts);
			if (!host)
				continue;
			ha = (struct scsi_qla_host *)host->hostdata;

			if (qla1280_hostlist == NULL) {
				cur_ha = qla1280_hostlist = ha;
			} else {
				cur_ha = qla1280_hostlist;
				while (cur_ha->next != NULL)
					cur_ha = cur_ha->next;
				cur_ha->next = ha;
			}
			num_hosts++;
		}		/* end of WHILE */
	}			/* end of FOR */

	LEAVE("qla1280_detect");
	return num_hosts;
}

/**************************************************************************
 *   qla1280_release
 *   Free the passed in Scsi_Host memory structures prior to unloading the
 *   module.
 **************************************************************************/
int
qla1280_release(struct Scsi_Host *host)
{
	struct scsi_qla_host *ha = (struct scsi_qla_host *)host->hostdata;

	ENTER("qla1280_release");

	if (!ha->flags.online)
		return 0;

	/* turn-off interrupts on the card */
	WRT_REG_WORD(&ha->iobase->ictrl, 0);

	/* Detach interrupts */
	if (host->irq)
		free_irq(host->irq, ha);

#if MEMORY_MAPPED_IO
	if (ha->mmpbase)
		iounmap(ha->mmpbase);
#else
	/* release io space registers  */
	if (host->io_port)
		release_region(host->io_port, 0xff);
#endif				/* MEMORY_MAPPED_IO */

	qla1280_mem_free(ha);

	ENTER("qla1280_release");
	return 0;
}

/**************************************************************************
 *   qla1280_info
 *     Return a string describing the driver.
 **************************************************************************/
const char *
qla1280_info(struct Scsi_Host *host)
{
	static char qla1280_scsi_name_buffer[125];
	char *bp;
	struct scsi_qla_host *ha;
	struct qla_boards *bdp;

	bp = &qla1280_scsi_name_buffer[0];
	ha = (struct scsi_qla_host *)host->hostdata;
	bdp = &ql1280_board_tbl[ha->devnum];
	memset(bp, 0, sizeof(qla1280_scsi_name_buffer));
	sprintf (bp,
		 "QLogic %s PCI to SCSI Host Adapter: bus %d device %d irq %d\n"
		 "       Firmware version: %2d.%02d.%02d, Driver version %s",
		 &bdp->name[0], ha->pci_bus, (ha->pci_device_fn & 0xf8) >> 3,
		 host->irq, bdp->fwver[0], bdp->fwver[1], bdp->fwver[2],
		 QLA1280_VERSION);
	return bp;
}

/**************************************************************************
 *   qla1200_queuecommand
 *     Queue a command to the controller.
 *
 * Note:
 * The mid-level driver tries to ensures that queuecommand never gets invoked
 * concurrently with itself or the interrupt handler (although the
 * interrupt handler may call this routine as part of request-completion
 * handling).   Unfortunely, it sometimes calls the scheduler in interrupt
 * context which is a big NO! NO!.
 **************************************************************************/
int
qla1280_queuecommand(Scsi_Cmnd * cmd, void (*fn) (Scsi_Cmnd *))
{
	struct scsi_qla_host *ha;
	struct srb *sp;
	struct Scsi_Host *host;
	int bus, target, lun;
	int status;

	/*ENTER("qla1280_queuecommand");
	 */
	dprintk(2, "qla1280_queuecommand(): jiffies %li\n", jiffies);

	host = CMD_HOST(cmd);
	ha = (struct scsi_qla_host *)host->hostdata;

	/* send command to adapter */
	sp = (struct srb *)CMD_SP(cmd);
	sp->cmd = cmd;
	cmd->scsi_done = fn;
	if (cmd->flags == 0) {	/* new command */
		sp->flags = 0;
	}

	qla1280_print_scsi_cmd(5, cmd);

	/* Generate LU queue on bus, target, LUN */
	bus = SCSI_BUS_32(cmd);
	target = SCSI_TCN_32(cmd);
	lun = SCSI_LUN_32(cmd);
	if (ha->flags.enable_64bit_addressing)
		status = qla1280_64bit_start_scsi(ha, sp);
	else
		status = qla1280_32bit_start_scsi(ha, sp);

	/*LEAVE("qla1280_queuecommand"); */
	return status;
}

enum action {
	ABORT_COMMAND,
	ABORT_DEVICE,
	DEVICE_RESET,
	BUS_RESET,
	ADAPTER_RESET,
	FAIL
};

/* timer action for error action processor */
static void qla1280_error_wait_timeout(unsigned long __data)
{
	struct scsi_cmnd *cmd = (struct scsi_cmnd *)__data;
	struct srb *sp = (struct srb *)CMD_SP(cmd);

	complete(sp->wait);
}

static void qla1280_mailbox_timeout(unsigned long __data)
{
	struct scsi_qla_host *ha = (struct scsi_qla_host *)__data;
	struct device_reg *reg;
	reg = ha->iobase;

	ha->mailbox_out[0] = RD_REG_WORD(&reg->mailbox0);
	printk(KERN_ERR "scsi(%ld): mailbox timed out, mailbox0 %04x, "
	       "ictrl %04x, istatus %04x\n", ha->host_no, ha->mailbox_out[0],
	       RD_REG_WORD(&reg->ictrl), RD_REG_WORD(&reg->istatus));
	complete(ha->mailbox_wait);
}

/**************************************************************************
 * qla1200_error_action
 *    The function will attempt to perform a specified error action and
 *    wait for the results (or time out).
 *
 * Input:
 *      cmd = Linux SCSI command packet of the command that cause the
 *            bus reset.
 *      action = error action to take (see action_t)
 *
 * Returns:
 *      SUCCESS or FAILED
 *
 * Note:
 *      Resetting the bus always succeeds - is has to, otherwise the
 *      kernel will panic! Try a surgical technique - sending a BUS
 *      DEVICE RESET message - on the offending target before pulling
 *      the SCSI bus reset line.
 **************************************************************************/
int
qla1280_error_action(Scsi_Cmnd * cmd, enum action action)
{
	struct scsi_qla_host *ha;
	int bus, target, lun;
	struct srb *sp;
	uint16_t data;
	unsigned char *handle;
	int result, i;
	DECLARE_COMPLETION(wait);
	struct timer_list timer;

	ha = (struct scsi_qla_host *)(CMD_HOST(cmd)->hostdata);

	dprintk(4, "error_action %i, istatus 0x%04x\n", action,
		RD_REG_WORD(&ha->iobase->istatus));

	dprintk(4, "host_cmd 0x%04x, ictrl 0x%04x, jiffies %li\n",
		RD_REG_WORD(&ha->iobase->host_cmd),
		RD_REG_WORD(&ha->iobase->ictrl), jiffies);

	ENTER("qla1280_error_action");
	if (qla1280_verbose)
		printk(KERN_INFO "scsi(%li): Resetting Cmnd=0x%p, "
		       "Handle=0x%p, action=0x%x\n",
		       ha->host_no, cmd, CMD_HANDLE(cmd), action);

	if (cmd == NULL) {
		printk(KERN_WARNING "(scsi?:?:?:?) Reset called with NULL "
		       "si_Cmnd pointer, failing.\n");
		LEAVE("qla1280_error_action");
		return FAILED;
	}

	ha = (struct scsi_qla_host *)cmd->device->host->hostdata;
	sp = (struct srb *)CMD_SP(cmd);
	handle = CMD_HANDLE(cmd);

	/* Check for pending interrupts. */
	data = qla1280_debounce_register(&ha->iobase->istatus);
	/*
	 * The io_request_lock is held when the reset handler is called, hence
	 * the interrupt handler cannot be running in parallel as it also
	 * grabs the lock. /Jes
	 */
	if (data & RISC_INT)
		qla1280_isr(ha, &ha->done_q_first, &ha->done_q_last);

	/*
	 * Determine the suggested action that the mid-level driver wants
	 * us to perform.
	 */
	if (handle == (unsigned char *)INVALID_HANDLE || handle == NULL) {
		if(action == ABORT_COMMAND) {
			/* we never got this command */
			printk(KERN_INFO "qla1280: Aborting a NULL handle\n");
			return SUCCESS;	/* no action - we don't have command */
		}
	} else {
		sp->wait = &wait;
	}

	bus = SCSI_BUS_32(cmd);
	target = SCSI_TCN_32(cmd);
	lun = SCSI_LUN_32(cmd);

	/* Overloading result.  Here it means the success or fail of the
	 * *issue* of the action.  When we return from the routine, it must
	 * mean the actual success or fail of the action */
	result = FAILED;
	switch (action) {
	case FAIL:
		break;

	case ABORT_COMMAND:
		if ((sp->flags & SRB_ABORT_PENDING)) {
			printk(KERN_WARNING
			       "scsi(): Command has a pending abort "
			       "message - ABORT_PENDING.\n");
			/* This should technically be impossible since we
			 * now wait for abort completion */
			break;
		}

		for (i = 0; i < MAX_OUTSTANDING_COMMANDS; i++) {
			if (sp == ha->outstanding_cmds[i]) {
				dprintk(1, "qla1280: RISC aborting command\n");
				if (qla1280_abort_command(ha, sp, i) == 0)
					result = SUCCESS;
				else {
					/*
					 * Since we don't know what might
					 * have happend to the command, it
					 * is unsafe to remove it from the
					 * device's queue at this point.
					 * Wait and let the escalation
					 * process take care of it.
					 */
					printk(KERN_WARNING
					       "scsi(%li:%i:%i:%i): Unable"
					       " to abort command!\n",
					       ha->host_no, bus, target, lun);
				}
			}
		}
		break;

	case ABORT_DEVICE:
		ha->flags.in_reset = 1;
		if (qla1280_verbose)
			printk(KERN_INFO
			       "scsi(%ld:%d:%d:%d): Queueing abort device "
			       "command.\n", ha->host_no, bus, target, lun);
		if (qla1280_abort_device(ha, bus, target, lun) == 0)
			result = SUCCESS;
		break;

	case DEVICE_RESET:
		if (qla1280_verbose)
			printk(KERN_INFO
			       "scsi(%ld:%d:%d:%d): Queueing device reset "
			       "command.\n", ha->host_no, bus, target, lun);
		ha->flags.in_reset = 1;
		if (qla1280_device_reset(ha, bus, target) == 0)
			result = SUCCESS;
		break;

	case BUS_RESET:
		if (qla1280_verbose)
			printk(KERN_INFO "qla1280(%ld:%d): Issuing BUS "
			       "DEVICE RESET\n", ha->host_no, bus);
		ha->flags.in_reset = 1;
		if (qla1280_bus_reset(ha, bus == 0))
			result = SUCCESS;

		break;

	case ADAPTER_RESET:
	default:
		if (qla1280_verbose) {
			printk(KERN_INFO
			       "scsi(%ld): Issued ADAPTER RESET\n",
			       ha->host_no);
			printk(KERN_INFO "scsi(%ld): I/O processing will "
			       "continue automatically\n", ha->host_no);
		}
		ha->flags.reset_active = 1;
		/*
		 * We restarted all of the commands automatically, so the
		 * mid-level code can expect completions momentitarily.
		 */
		if (qla1280_abort_isp(ha) == 0)
			result = SUCCESS;

		ha->flags.reset_active = 0;
	}

	if (ha->done_q_first)
		qla1280_done(ha, &ha->done_q_first, &ha->done_q_last);
	ha->flags.in_reset = 0;

	/* If we didn't manage to issue the action, or we have no
	 * command to wait for, exit here */
	if (result == FAILED || handle == NULL ||
	    handle == (unsigned char *)INVALID_HANDLE)
		goto leave;

	/* set up a timer just in case we're really jammed */
	init_timer(&timer);
	timer.expires = jiffies + 4*HZ;
	timer.data = (unsigned long)cmd;
	timer.function = qla1280_error_wait_timeout;
	add_timer(&timer);

	/* wait for the action to complete (or the timer to expire) */
	spin_unlock_irq(HOST_LOCK);
	wait_for_completion(&wait);
	del_timer_sync(&timer);
	spin_lock_irq(HOST_LOCK);
	sp->wait = NULL;

	/* the only action we might get a fail for is abort */
	if (action == ABORT_COMMAND) {
		if(sp->flags & SRB_ABORTED)
			result = SUCCESS;
		else
			result = FAILED;
	}

 leave:
	dprintk(1, "RESET returning %d\n", result);

	LEAVE("qla1280_error_action");
	return result;
}

/**************************************************************************
 *   qla1280_abort
 *     Abort the specified SCSI command(s).
 **************************************************************************/
int
qla1280_eh_abort(struct scsi_cmnd * cmd)
{
	return qla1280_error_action(cmd, ABORT_COMMAND);
}

/**************************************************************************
 *   qla1280_device_reset
 *     Reset the specified SCSI device
 **************************************************************************/
int
qla1280_eh_device_reset(struct scsi_cmnd *cmd)
{
	return qla1280_error_action(cmd, DEVICE_RESET);
}

/**************************************************************************
 *   qla1280_bus_reset
 *     Reset the specified bus.
 **************************************************************************/
int
qla1280_eh_bus_reset(struct scsi_cmnd *cmd)
{
	return qla1280_error_action(cmd, BUS_RESET);
}

/**************************************************************************
 *   qla1280_adapter_reset
 *     Reset the specified adapter (both channels)
 **************************************************************************/
int
qla1280_eh_adapter_reset(struct scsi_cmnd *cmd)
{
	return qla1280_error_action(cmd, ADAPTER_RESET);
}

/**************************************************************************
 * qla1280_biosparam
 *   Return the disk geometry for the given SCSI device.
 **************************************************************************/
int
#if LINUX_VERSION_CODE < 0x020545
qla1280_biosparam(Disk * disk, kdev_t dev, int geom[])
#else
qla1280_biosparam(struct scsi_device *sdev, struct block_device *bdev,
		  sector_t capacity, int geom[])
#endif
{
	int heads, sectors, cylinders;
#if LINUX_VERSION_CODE < 0x020545
	unsigned long capacity = disk->capacity;
#endif

	heads = 64;
	sectors = 32;
	cylinders = (unsigned long)capacity / (heads * sectors);
	if (cylinders > 1024) {
		heads = 255;
		sectors = 63;
		cylinders = (unsigned long)capacity / (heads * sectors);
		/* if (cylinders > 1023)
		   cylinders = 1023; */
	}

	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;

	return 0;
}

/**************************************************************************
 * qla1280_intr_handler
 *   Handles the H/W interrupt
 **************************************************************************/
irqreturn_t
qla1280_intr_handler(int irq, void *dev_id, struct pt_regs *regs)
{
	struct scsi_qla_host *ha;
	struct device_reg *reg;
	u16 data;
	int handled = 0;

	ENTER_INTR ("qla1280_intr_handler");
	ha = (struct scsi_qla_host *)dev_id;

	spin_lock(HOST_LOCK);

	ha->isr_count++;
	reg = ha->iobase;

	WRT_REG_WORD(&reg->ictrl, 0);	/* disable our interrupt. */

	data = qla1280_debounce_register(&reg->istatus);
	/* Check for pending interrupts. */
	if (data & RISC_INT) {
		qla1280_isr(ha, &ha->done_q_first, &ha->done_q_last);
		handled = 1;
	}
	if (ha->done_q_first)
		qla1280_done(ha, &ha->done_q_first, &ha->done_q_last);

	spin_unlock(HOST_LOCK);

	/* enable our interrupt. */
	WRT_REG_WORD(&reg->ictrl, (ISP_EN_INT | ISP_EN_RISC));

	LEAVE_INTR("qla1280_intr_handler");
	return IRQ_RETVAL(handled);
}

/**************************************************************************
 *   qla1280_do_dpc
 *
 * Description:
 * This routine is a task that is schedule by the interrupt handler
 * to perform the background processing for interrupts.  We put it
 * on a task queue that is consumed whenever the scheduler runs; that's
 * so you can do anything (i.e. put the process to sleep etc).  In fact, the
 * mid-level tries to sleep when it reaches the driver threshold
 * "host->can_queue". This can cause a panic if we were in our interrupt
 * code .
 **************************************************************************/
void
qla1280_do_dpc(void *p)
{
	struct scsi_qla_host *ha = (struct scsi_qla_host *) p;
	unsigned long cpu_flags;

	spin_lock_irqsave(HOST_LOCK, cpu_flags);

	if (ha->flags.reset_marker)
		qla1280_rst_aen(ha);

	if (ha->done_q_first)
		qla1280_done(ha, &ha->done_q_first, &ha->done_q_last);

	spin_unlock_irqrestore(HOST_LOCK, cpu_flags);
}


static int
qla12160_set_target_parameters(struct scsi_qla_host *ha, int bus, int target)
{
	uint8_t mr;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	struct nvram *nv;
	int is1x160, status;

	nv = &ha->nvram;

	if (ha->device_id == PCI_DEVICE_ID_QLOGIC_ISP12160 ||
	    ha->device_id == PCI_DEVICE_ID_QLOGIC_ISP10160)
		is1x160 = 1;
	else
		is1x160 = 0;

	mr = BIT_3 | BIT_2 | BIT_1 | BIT_0;

	/* Set Target Parameters. */
	mb[0] = MBC_SET_TARGET_PARAMETERS;
	mb[1] = (uint16_t) (bus ? target | BIT_7 : target);
	mb[1] <<= 8;

	mb[2] = (nv->bus[bus].target[target].parameter.c << 8);

	if (is1x160)
		mb[3] =	nv->bus[bus].target[target].flags.flags1x160.sync_offset << 8;
	else
		mb[3] =	nv->bus[bus].target[target].flags.flags1x80.sync_offset << 8;
	mb[3] |= nv->bus[bus].target[target].sync_period;

	if (is1x160) {
		mb[2] |= nv->bus[bus].target[target].ppr_1x160.flags.enable_ppr << 5;
		mb[6] =	nv->bus[bus].target[target].ppr_1x160.flags.ppr_options << 8;
		mb[6] |= nv->bus[bus].target[target].ppr_1x160.flags.ppr_bus_width;
		mr |= BIT_6;
	}

	status = qla1280_mailbox_command(ha, mr, &mb[0]);

	if (status)
		printk(KERN_WARNING "scsi(%ld:%i:%i): "
		       "qla1280_set_target_parameters() failed\n",
		       ha->host_no, bus, target);
	return status;
}


/**************************************************************************
 *   qla1280_slave_configure
 *
 * Description:
 *   Determines the queue depth for a given device.  There are two ways
 *   a queue depth can be obtained for a tagged queueing device.  One
 *   way is the default queue depth which is determined by whether
 *   If it is defined, then it is used
 *   as the default queue depth.  Otherwise, we use either 4 or 8 as the
 *   default queue depth (dependent on the number of hardware SCBs).
 **************************************************************************/
static int
qla1280_slave_configure(Scsi_Device *device)
{
	struct scsi_qla_host *ha;
	int default_depth = 3;
	int bus = device->channel;
	int target = device->id;
	int status = 0;
	struct nvram *nv;
#if LINUX_VERSION_CODE < 0x020500
	unsigned long flags;
#endif

	ha = (struct scsi_qla_host *)device->host->hostdata;
	nv = &ha->nvram;

	if (qla1280_check_for_dead_scsi_bus(ha, bus))
		return 1;

	if (device->tagged_supported &&
	    (ha->bus_settings[bus].qtag_enables & (BIT_0 << target))) {
		scsi_adjust_queue_depth(device, MSG_ORDERED_TAG,
					ha->bus_settings[bus].hiwat);
	} else {
		scsi_adjust_queue_depth(device, 0, default_depth);
	}

#if LINUX_VERSION_CODE > 0x020500
	nv->bus[bus].target[target].parameter.f.enable_sync = device->sdtr;
	nv->bus[bus].target[target].parameter.f.enable_wide = device->wdtr;
	nv->bus[bus].target[target].ppr_1x160.flags.enable_ppr = device->ppr;
#endif

	if (driver_setup.no_sync ||
	    (driver_setup.sync_mask &&
	     (~driver_setup.sync_mask & (1 << target))))
		nv->bus[bus].target[target].parameter.f.enable_sync = 0;
	if (driver_setup.no_wide ||
	    (driver_setup.wide_mask &&
	     (~driver_setup.wide_mask & (1 << target))))
		nv->bus[bus].target[target].parameter.f.enable_wide = 0;
	if (ha->device_id == PCI_DEVICE_ID_QLOGIC_ISP12160 ||
	    ha->device_id == PCI_DEVICE_ID_QLOGIC_ISP10160) {
		if (driver_setup.no_ppr ||
		    (driver_setup.ppr_mask &&
		     (~driver_setup.ppr_mask & (1 << target))))
			nv->bus[bus].target[target].ppr_1x160.flags.enable_ppr = 0;
	}

#if LINUX_VERSION_CODE < 0x020500
	spin_lock_irqsave(HOST_LOCK, flags);
#endif
	if (nv->bus[bus].target[target].parameter.f.enable_sync) {
		status = qla12160_set_target_parameters(ha, bus, target);
	}

	qla12160_get_target_parameters(ha, device);
#if LINUX_VERSION_CODE < 0x020500
	spin_unlock_irqrestore(HOST_LOCK, flags);
#endif
	return status;
}

#if LINUX_VERSION_CODE < 0x020545
/**************************************************************************
 *   qla1280_select_queue_depth
 *
 *   Sets the queue depth for each SCSI device hanging off the input
 *   host adapter.  We use a queue depth of 2 for devices that do not
 *   support tagged queueing.
 **************************************************************************/
static void
qla1280_select_queue_depth(struct Scsi_Host *host, Scsi_Device *scsi_devs)
{
	Scsi_Device *device;
	struct scsi_qla_host *ha = (struct scsi_qla_host *)host->hostdata;

	ENTER("qla1280_select_queue_depth");
	for (device = scsi_devs; device != NULL; device = device->next) {
		if (device->host == host)
			qla1280_slave_configure(device);
	}

	if (scsi_devs)
		qla1280_check_for_dead_scsi_bus(ha, scsi_devs->channel);

	LEAVE("qla1280_select_queue_depth");
}
#endif

/*
 * Driver Support Routines
 */

/*
 * qla1280_done
 *      Process completed commands.
 *
 * Input:
 *      ha           = adapter block pointer.
 *      done_q_first = done queue first pointer.
 *      done_q_last  = done queue last pointer.
 */
static void
qla1280_done(struct scsi_qla_host *ha, struct srb ** done_q_first,
	     struct srb ** done_q_last)
{
	struct srb *sp;
	int bus, target, lun;
	Scsi_Cmnd *cmd;

	ENTER("qla1280_done");

	while (*done_q_first != NULL) {
		/* remove command from done list */
		sp = *done_q_first;
		if (!(*done_q_first = sp->s_next))
			*done_q_last = NULL;
		else
			(*done_q_first)->s_prev = NULL;

		cmd = sp->cmd;
		bus = SCSI_BUS_32(cmd);
		target = SCSI_TCN_32(cmd);
		lun = SCSI_LUN_32(cmd);

		switch ((CMD_RESULT(cmd) >> 16)) {
		case DID_RESET:
			/* Issue marker command. */
			qla1280_marker(ha, bus, target, 0, MK_SYNC_ID);
			break;
		case DID_ABORT:
			sp->flags &= ~SRB_ABORT_PENDING;
			sp->flags |= SRB_ABORTED;
			if (sp->flags & SRB_TIMEOUT)
				CMD_RESULT(sp->cmd) = DID_TIME_OUT << 16;
			break;
		default:
			break;
		}

		/* Release memory used for this I/O */
		if (cmd->use_sg) {
			dprintk(3, "S/G unmap_sg cmd=%p\n", cmd);

			pci_unmap_sg(ha->pdev, cmd->request_buffer,
				     cmd->use_sg,
				     scsi_to_pci_dma_dir(cmd->sc_data_direction));
		} else if (cmd->request_bufflen) {
			/*dprintk(1, "No S/G unmap_single cmd=%x saved_dma_handle=%lx\n",
			  cmd, sp->saved_dma_handle); */

			pci_unmap_page(ha->pdev, sp->saved_dma_handle,
				       cmd->request_bufflen,
				       scsi_to_pci_dma_dir(cmd->sc_data_direction));
		}

		/* Call the mid-level driver interrupt handler */
		CMD_HANDLE(sp->cmd) = (unsigned char *)INVALID_HANDLE;
		ha->actthreads--;

#if LINUX_VERSION_CODE < 0x020500
		if (cmd->cmnd[0] == INQUIRY)
			qla1280_get_target_options(cmd, ha);
#endif
		(*(cmd)->scsi_done)(cmd);

		if(sp->wait != NULL)
			complete(sp->wait);

	}
	LEAVE("qla1280_done");
}

/*
 * Translates a ISP error to a Linux SCSI error
 */
static int
qla1280_return_status(struct response * sts, Scsi_Cmnd * cp)
{
	int host_status = DID_ERROR;
#if DEBUG_QLA1280_INTR
	static char *reason[] = {
		"DID_OK",
		"DID_NO_CONNECT",
		"DID_BUS_BUSY",
		"DID_TIME_OUT",
		"DID_BAD_TARGET",
		"DID_ABORT",
		"DID_PARITY",
		"DID_ERROR",
		"DID_RESET",
		"DID_BAD_INTR"
	};
#endif				/* DEBUG_QLA1280_INTR */

	ENTER("qla1280_return_status");

#if DEBUG_QLA1280_INTR
	/*
	  dprintk(1, "qla1280_return_status: compl status = 0x%04x\n",
	  sts->comp_status);
	*/
#endif
	switch (sts->comp_status) {
	case CS_COMPLETE:
		host_status = DID_OK;
		break;

	case CS_INCOMPLETE:
		if (!(sts->state_flags & SF_GOT_BUS))
			host_status = DID_NO_CONNECT;
		else if (!(sts->state_flags & SF_GOT_TARGET))
			host_status = DID_BAD_TARGET;
		else if (!(sts->state_flags & SF_SENT_CDB))
			host_status = DID_ERROR;
		else if (!(sts->state_flags & SF_TRANSFERRED_DATA))
			host_status = DID_ERROR;
		else if (!(sts->state_flags & SF_GOT_STATUS))
			host_status = DID_ERROR;
		else if (!(sts->state_flags & SF_GOT_SENSE))
			host_status = DID_ERROR;
		break;

	case CS_RESET:
		host_status = DID_RESET;
		break;

	case CS_ABORTED:
		host_status = DID_ABORT;
		break;

	case CS_TIMEOUT:
		host_status = DID_TIME_OUT;
		break;

	case CS_DATA_OVERRUN:
		dprintk(2, "Data overrun 0x%x\n", sts->residual_length);
		dprintk(2, "qla1280_isr: response packet data\n");
		qla1280_dump_buffer(2, (char *)sts, RESPONSE_ENTRY_SIZE);
		host_status = DID_ERROR;
		break;

	case CS_DATA_UNDERRUN:
		if ((cp->request_bufflen - sts->residual_length) <
		    cp->underflow) {
			printk(KERN_WARNING
			       "scsi: Underflow detected - retrying "
			       "command.\n");
			host_status = DID_ERROR;
		} else
			host_status = DID_OK;
		break;

	default:
		host_status = DID_ERROR;
		break;
	}

#if DEBUG_QLA1280_INTR
	dprintk(1, "qla1280 ISP status: host status (%s) scsi status %x\n",
		reason[host_status], sts->scsi_status);
#endif

	LEAVE("qla1280_return_status");

	return (sts->scsi_status & 0xff) | (host_status << 16);
}

/*
 * qla1280_done_q_put
 *      Place SRB command on done queue.
 *
 * Input:
 *      sp           = srb pointer.
 *      done_q_first = done queue first pointer.
 *      done_q_last  = done queue last pointer.
 */
static void
qla1280_done_q_put(struct srb * sp, struct srb ** done_q_first,
		   struct srb ** done_q_last)
{
	ENTER("qla1280_put_done_q");

	/* Place block on done queue */
	sp->s_next = NULL;
	sp->s_prev = *done_q_last;
	if (!*done_q_first)
		*done_q_first = sp;
	else
		(*done_q_last)->s_next = sp;
	*done_q_last = sp;

	LEAVE("qla1280_put_done_q");
}


/*
* qla1280_mem_alloc
*      Allocates adapter memory.
*
* Returns:
*      0  = success.
*      1  = failure.
*/
static int
qla1280_mem_alloc(struct scsi_qla_host *ha)
{
	int status = 1;
	dma_addr_t dma_handle;

	ENTER("qla1280_mem_alloc");

	/* get consistent memory allocated for request and response rings */
	ha->request_ring = pci_alloc_consistent(ha->pdev,
						((REQUEST_ENTRY_CNT + 1) *
						 (sizeof(request_t))),
						&dma_handle);
	if (!ha->request_ring)
		goto error;
	ha->request_dma = dma_handle;
	ha->response_ring = pci_alloc_consistent(ha->pdev,
						 ((RESPONSE_ENTRY_CNT + 1) *
						  (sizeof(struct response))),
						 &dma_handle);
	if (!ha->response_ring)
		goto error;
	ha->response_dma = dma_handle;
	status = 0;
	goto finish;

 error:
	if (status)
		dprintk(2, "qla1280_mem_alloc: **** FAILED ****\n");

	if (ha->request_ring)
		pci_free_consistent(ha->pdev,
                                    ((REQUEST_ENTRY_CNT + 1) *
                                     (sizeof(request_t))),
                                    ha->request_ring, ha->request_dma);
 finish:
	LEAVE("qla1280_mem_alloc");
	return status;
}

/*
 * qla1280_mem_free
 *      Frees adapter allocated memory.
 *
 * Input:
 *      ha = adapter block pointer.
 */
static void
qla1280_mem_free(struct scsi_qla_host *ha)
{
	ENTER("qlc1280_mem_free");
	/* free consistent memory allocated for request and response rings */
	if (ha->request_ring)
		pci_free_consistent(ha->pdev,
				    ((REQUEST_ENTRY_CNT + 1) *
				     (sizeof(request_t))),
				    ha->request_ring, ha->request_dma);

	if (ha->response_ring)
		pci_free_consistent(ha->pdev,
				    ((RESPONSE_ENTRY_CNT + 1) *
				     (sizeof(struct response))),
				    ha->response_ring, ha->response_dma);

	if (qla1280_buffer) {
		free_page((unsigned long) qla1280_buffer);
		qla1280_buffer = NULL;
	}

	LEAVE("qlc1280_mem_free");
}

/****************************************************************************/
/*                QLogic ISP1280 Hardware Support Functions.                */
/****************************************************************************/

 /*
    * qla2100_enable_intrs
    * qla2100_disable_intrs
    *
    * Input:
    *      ha = adapter block pointer.
    *
    * Returns:
    *      None
  */
static inline void
qla1280_enable_intrs(struct scsi_qla_host *ha)
{
	struct device_reg *reg;

	reg = ha->iobase;
	/* enable risc and host interrupts */
	WRT_REG_WORD(&reg->ictrl, (ISP_EN_INT | ISP_EN_RISC));
	RD_REG_WORD(&reg->ictrl);	/* PCI Posted Write flush */
	ha->flags.ints_enabled = 1;
}

static inline void
qla1280_disable_intrs(struct scsi_qla_host *ha)
{
	struct device_reg *reg;

	reg = ha->iobase;
	/* disable risc and host interrupts */
	WRT_REG_WORD(&reg->ictrl, 0);
	RD_REG_WORD(&reg->ictrl);	/* PCI Posted Write flush */
	ha->flags.ints_enabled = 0;
}

/*
 * qla1280_initialize_adapter
 *      Initialize board.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_initialize_adapter(struct scsi_qla_host *ha)
{
	struct device_reg *reg;
	int status;
	int bus;

	ENTER("qla1280_initialize_adapter");

	/* Clear adapter flags. */
	ha->flags.online = 0;
	ha->flags.disable_host_adapter = 0;
	ha->flags.reset_active = 0;
	ha->flags.abort_isp_active = 0;

	ha->flags.ints_enabled = 0;
#if defined(CONFIG_IA64_GENERIC) || defined(CONFIG_IA64_SGI_SN2)
	if (ia64_platform_is("sn2")) {
		int count1, count2;
		int c;

		count1 = 3;
		count2 = 3;
		printk(KERN_INFO "scsi(%li): Enabling SN2 PCI DMA "
		       "dual channel lockup workaround\n", ha->host_no);
		if ((c = snia_pcibr_rrb_alloc(ha->pdev, &count1, &count2)) < 0)
			printk(KERN_ERR "scsi(%li): Unable to allocate SN2 "
			       "virtual DMA channels\n", ha->host_no);
		ha->flags.use_pci_vchannel = 1;

		driver_setup.no_nvram = 1;
	}
#endif

	dprintk(1, "Configure PCI space for adapter...\n");

	reg = ha->iobase;

	/* Insure mailbox registers are free. */
	WRT_REG_WORD(&reg->semaphore, 0);
	WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
	WRT_REG_WORD(&reg->host_cmd, HC_CLR_HOST_INT);
	RD_REG_WORD(&reg->host_cmd);

	if (qla1280_read_nvram(ha)) {
		dprintk(2, "qla1280_initialize_adapter: failed to read "
			"NVRAM\n");
	}

	/* If firmware needs to be loaded */
	if (qla1280_isp_firmware(ha)) {
		if (!(status = qla1280_chip_diag (ha))) {
			status = qla1280_setup_chip(ha);
		}
	} else {
		printk(KERN_ERR "scsi(%li): isp_firmware() failed!\n",
		       ha->host_no);
		status = 1;
	}

	if (status) {
		printk(KERN_ERR "scsi(%li): initialize: pci probe failed!\n",
		       ha->host_no);
		goto out;
	}

	/* Setup adapter based on NVRAM parameters. */
	dprintk(1, "scsi(%ld): Configure NVRAM parameters\n", ha->host_no);
	qla1280_nvram_config(ha);

	if (!ha->flags.disable_host_adapter && !qla1280_init_rings(ha)) {
		/* Issue SCSI reset. */
		/* dg 03/13 if we can't reset twice then bus is dead */
		for (bus = 0; bus < ha->ports; bus++) {
			if (!ha->bus_settings[bus].disable_scsi_reset){
				if (qla1280_bus_reset(ha, bus)) {
					if (qla1280_bus_reset(ha, bus)) {
						ha->bus_settings[bus].scsi_bus_dead = 1;
					}
				}
			}
		}

		/*
		 * qla1280_bus_reset() will take care of issueing markers,
		 * no need to do that here as well!
		 */
#if 0
		/* Issue marker command. */
		ha->flags.reset_marker = 0;
		for (bus = 0; bus < ha->ports; bus++) {
			ha->bus_settings[bus].reset_marker = 0;
			qla1280_marker(ha, bus, 0, 0, MK_SYNC_ALL);
		}
#endif

		ha->flags.online = 1;
	} else
		status = 1;

 out:
	if (status)
		dprintk(2, "qla1280_initialize_adapter: **** FAILED ****\n");

	LEAVE("qla1280_initialize_adapter");
	return status;
}


/*
 * ISP Firmware Test
 *      Checks if present version of RISC firmware is older than
 *      driver firmware.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = firmware does not need to be loaded.
 */
static int
qla1280_isp_firmware(struct scsi_qla_host *ha)
{
	struct nvram *nv = (struct nvram *) ha->response_ring;
	int status = 0;		/* dg 2/27 always loads RISC */
	uint16_t mb[MAILBOX_REGISTER_COUNT];

	ENTER("qla1280_isp_firmware");

	dprintk(1, "scsi(%li): Determining if RISC is loaded\n", ha->host_no);

	/* Bad NVRAM data, load RISC code. */
	if (!ha->nvram_valid) {
		ha->flags.disable_risc_code_load = 0;
	} else
		ha->flags.disable_risc_code_load =
			nv->cntr_flags_1.disable_loading_risc_code;

	if (ha->flags.disable_risc_code_load) {
		dprintk(3, "qla1280_isp_firmware: Telling RISC to verify "
			"checksum of loaded BIOS code.\n");

		/* Verify checksum of loaded RISC code. */
		mb[0] = MBC_VERIFY_CHECKSUM;
		/* mb[1] = ql12_risc_code_addr01; */
		mb[1] = *ql1280_board_tbl[ha->devnum].fwstart;

		if (!(status =
		      qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]))) {
			/* Start firmware execution. */
			dprintk(3, "qla1280_isp_firmware: Startng F/W "
				"execution.\n");

			mb[0] = MBC_EXECUTE_FIRMWARE;
			/* mb[1] = ql12_risc_code_addr01; */
			mb[1] = *ql1280_board_tbl[ha->devnum].fwstart;
			qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);
		} else
			printk(KERN_INFO "qla1280: RISC checksum failed.\n");
	} else {
		dprintk(1, "qla1280: NVRAM configured to load RISC load.\n");
		status = 1;
	}

	if (status)
		dprintk(2, "qla1280_isp_firmware: **** Load RISC code ****\n");

	LEAVE("qla1280_isp_firmware");
	return status;
}

/*
 * PCI configuration
 *      Setup device PCI configuration registers.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success.
 */
static int
qla1280_pci_config(struct scsi_qla_host *ha)
{
#if MEMORY_MAPPED_IO
	unsigned long base;
	int size;
#endif
	uint16_t buf_wd;
	int status = 1;

	ENTER("qla1280_pci_config");

	pci_set_master(ha->pdev);
	/*
	 * Set Bus Master Enable, Memory Address Space Enable and
	 * reset any error bits, in the command register.
	 */
	pci_read_config_word (ha->pdev, PCI_COMMAND, &buf_wd);
#if MEMORY_MAPPED_IO
	buf_wd |= PCI_COMMAND_MEMORY;
#endif
	buf_wd |= PCI_COMMAND_IO;
	pci_write_config_word (ha->pdev, PCI_COMMAND, buf_wd);
	/*
	 * Reset expansion ROM address decode enable.
	 */
	pci_read_config_word(ha->pdev, PCI_ROM_ADDRESS, &buf_wd);
	buf_wd &= ~PCI_ROM_ADDRESS_ENABLE;
	pci_write_config_word (ha->pdev, PCI_ROM_ADDRESS, buf_wd);

	ha->host->io_port = pci_resource_start(ha->pdev, 0);
	ha->host->io_port &= PCI_BASE_ADDRESS_IO_MASK;
	ha->iobase = (struct device_reg *) ha->host->io_port;

#if MEMORY_MAPPED_IO
	/*
	 * Find proper memory chunk for memory map I/O reg.
	 */
	base = pci_resource_start(ha->pdev, 1);
	size = pci_resource_len(ha->pdev, 1);
	/*
	 * Get virtual address for I/O registers.
	 */
	ha->mmpbase = ioremap(base, size);
	if (ha->mmpbase) {
		ha->iobase = (struct device_reg *)ha->mmpbase;
		status = 0;
	}
#else				/* MEMORY_MAPPED_IO */
	status = 0;
#endif				/* MEMORY_MAPPED_IO */

	LEAVE("qla1280_pci_config");
	return status;
}

/*
 * Chip diagnostics
 *      Test chip for proper operation.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success.
 */
static int
qla1280_chip_diag(struct scsi_qla_host *ha)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	struct device_reg *reg = ha->iobase;
	int status = 0;
	int cnt;
	uint16_t data;

	dprintk(3, "qla1280_chip_diag: testing device at 0x%p \n", &reg->id_l);

	dprintk(1, "scsi(%ld): Verifying chip\n", ha->host_no);

	/* Soft reset chip and wait for it to finish. */
	WRT_REG_WORD(&reg->ictrl, ISP_RESET);
	/*
	 * We can't do a traditional PCI write flush here by reading
	 * back the register. The card will not respond once the reset
	 * is in action and we end up with a machine check exception
	 * instead. Nothing to do but wait and hope for the best.
	 * A portable pci_write_flush(pdev) call would be very useful here.
	 */
	udelay(20);
	data = qla1280_debounce_register(&reg->ictrl);
	/*
	 * Yet another QLogic gem ;-(
	 */
	for (cnt = 1000000; cnt && data & ISP_RESET; cnt--) {
		udelay(5);
		data = RD_REG_WORD(&reg->ictrl);
	}

	if (cnt) {
		/* Reset register cleared by chip reset. */
		dprintk(3, "qla1280_chip_diag: reset register cleared by "
			"chip reset\n");

		WRT_REG_WORD(&reg->cfg_1, 0);

		/* Reset RISC and disable BIOS which
		   allows RISC to execute out of RAM. */
#if 0
		WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC);
		RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
		WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);
		RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
		WRT_REG_WORD(&reg->host_cmd, HC_DISABLE_BIOS);
#else
		WRT_REG_WORD(&reg->host_cmd, HC_RESET_RISC |
			     HC_RELEASE_RISC | HC_DISABLE_BIOS);
#endif
		RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
		data = qla1280_debounce_register(&reg->mailbox0);
		/*
		 * I *LOVE* this code!
		 */
		for (cnt = 1000000; cnt && data == MBS_BUSY; cnt--) {
			udelay(5);
			data = RD_REG_WORD(&reg->mailbox0);
		}

		if (cnt) {
			/* Check product ID of chip */
			dprintk(3, "qla1280_chip_diag: Checking product "
				"ID of chip\n");

			if (RD_REG_WORD(&reg->mailbox1) != PROD_ID_1 ||
			    (RD_REG_WORD(&reg->mailbox2) != PROD_ID_2 &&
			     RD_REG_WORD(&reg->mailbox2) != PROD_ID_2a) ||
			    RD_REG_WORD(&reg->mailbox3) != PROD_ID_3 ||
			    RD_REG_WORD(&reg->mailbox4) != PROD_ID_4) {
				printk(KERN_INFO "qla1280: Wrong product ID = "
				       "0x%x,0x%x,0x%x,0x%x\n",
				       RD_REG_WORD(&reg->mailbox1),
				       RD_REG_WORD(&reg->mailbox2),
				       RD_REG_WORD(&reg->mailbox3),
				       RD_REG_WORD(&reg->mailbox4));
				status = 1;
			} else {
				/*
				 * Enable ints early!!!
				 */
				qla1280_enable_intrs(ha);

				dprintk(1, "qla1280_chip_diag: Checking "
					"mailboxes of chip\n");
				/* Wrap Incoming Mailboxes Test. */
				mb[0] = MBC_MAILBOX_REGISTER_TEST;
				mb[1] = 0xAAAA;
				mb[2] = 0x5555;
				mb[3] = 0xAA55;
				mb[4] = 0x55AA;
				mb[5] = 0xA5A5;
				mb[6] = 0x5A5A;
				mb[7] = 0x2525;
				if (!(status = qla1280_mailbox_command(ha,
								       0xff,
								       &mb
								       [0]))) {
					if (mb[1] != 0xAAAA ||
					    mb[2] != 0x5555 ||
					    mb[3] != 0xAA55 ||
					    mb[4] != 0x55AA ||
					    mb[5] != 0xA5A5 ||
					    mb[6] != 0x5A5A ||
					    mb[7] != 0x2525) {
						status = 1;
						printk(KERN_INFO "qla1280: "
						       "Failed mbox check\n");
					}
				}
			}
		} else
			status = 1;
	} else
		status = 1;

	if (status)
		dprintk(2, "qla1280_chip_diag: **** FAILED ****\n");
	else
		dprintk(3, "qla1280_chip_diag: exiting normally\n");

	return status;
}

/*
 * Setup chip
 *      Load and start RISC firmware.
 *
 * Input:
 *      ha = adapter block pointer.
 *
 * Returns:
 *      0 = success.
 */
#define DUMP_IT_BACK 0		/* for debug of RISC loading */
static int
qla1280_setup_chip(struct scsi_qla_host *ha)
{
	int status = 0;
	uint16_t risc_address;
	uint16_t *risc_code_address;
	int risc_code_size;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint16_t cnt;
	int num, i;
#if DUMP_IT_BACK
	uint8_t *sp;
	uint8_t *tbuf;
	dma_addr_t p_tbuf;
#endif

	ENTER("qla1280_setup_chip");

	dprintk(1, "scsi(%ld): Setup chip\n", ha->host_no);

#if DUMP_IT_BACK
	/* get consistent memory allocated for setup_chip */
	tbuf = pci_alloc_consistent(ha->pdev, 8000, &p_tbuf);
#endif

	/* Load RISC code. */
	risc_address = *ql1280_board_tbl[ha->devnum].fwstart;
	risc_code_address = ql1280_board_tbl[ha->devnum].fwcode;
	risc_code_size = (int) *ql1280_board_tbl[ha->devnum].fwlen;

	dprintk(1, "qla1280_setup_chip: DMA RISC code (%i) words\n",
		risc_code_size);

	num = 0;
	while (risc_code_size > 0 && !status) {
		int warn __attribute__((unused)) = 0;

		cnt = 2000 >> 1;

		if (cnt > risc_code_size)
			cnt = risc_code_size;

		dprintk(2, "qla1280_setup_chip:  loading risc @ =(0x%p),"
			"%d,%d(0x%x)\n",
			risc_code_address, cnt, num, risc_address);
		for(i = 0; i < cnt; i++)
			((uint16_t *)ha->request_ring)[i] =
				cpu_to_le16(risc_code_address[i]);

		flush_cache_all();

		mb[0] = MBC_LOAD_RAM;
		mb[1] = risc_address;
		mb[4] = cnt;
		mb[3] = ha->request_dma & 0xffff;
		mb[2] = (ha->request_dma >> 16) & 0xffff;
		mb[7] = pci_dma_hi32(ha->request_dma) & 0xffff;
		mb[6] = pci_dma_hi32(ha->request_dma) >> 16;
		dprintk(2, "qla1280_setup_chip: op=%d  0x%p = 0x%4x,0x%4x,"
			"0x%4x,0x%4x\n", mb[0], (void *)(long)ha->request_dma,
			mb[6], mb[7], mb[2], mb[3]);
		if ((status = qla1280_mailbox_command(ha, BIT_4 | BIT_3 |
						      BIT_2 | BIT_1 | BIT_0,
						      &mb[0]))) {
			printk(KERN_ERR "scsi(%li): Failed to load partial "
			       "segment of f\n", ha->host_no);
			break;
		}

#if DUMP_IT_BACK
		mb[0] = MBC_DUMP_RAM;
		mb[1] = risc_address;
		mb[4] = cnt;
		mb[3] = p_tbuf & 0xffff;
		mb[2] = (p_tbuf >> 16) & 0xffff;
		mb[7] = pci_dma_hi32(p_tbuf) & 0xffff;
		mb[6] = pci_dma_hi32(p_tbuf) >> 16;

		if ((status = qla1280_mailbox_command(ha,
						      BIT_4 | BIT_3 | BIT_2 |
						      BIT_1 | BIT_0,
						      &mb[0]))) {
			printk(KERN_ERR
			       "Failed to dump partial segment of f/w\n");
			break;
		}
		sp = (uint8_t *)ha->request_ring;
		for (i = 0; i < (cnt << 1); i++) {
			if (tbuf[i] != sp[i] && warn++ < 10) {
				printk(KERN_ERR "qla1280_setup_chip: FW "
				       "compare error @ byte(0x%x) loop#=%x\n",
				       i, num);
				printk(KERN_ERR "setup_chip: FWbyte=%x  "
				       "FWfromChip=%x\n", sp[i], tbuf[i]);
				/*break; */
			}
		}
#endif
		risc_address += cnt;
		risc_code_size = risc_code_size - cnt;
		risc_code_address = risc_code_address + cnt;
		num++;
	}

	/* Verify checksum of loaded RISC code. */
	if (!status) {
		dprintk(1, "qla1280_setup_chip: Verifying checksum of "
			"loaded RISC code.\n");
		mb[0] = MBC_VERIFY_CHECKSUM;
		/* mb[1] = ql12_risc_code_addr01; */
		mb[1] = *ql1280_board_tbl[ha->devnum].fwstart;

		if (!(status =
		      qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]))) {
			/* Start firmware execution. */
			dprintk(1,
				"qla1280_setup_chip: start firmware running.\n");
			mb[0] = MBC_EXECUTE_FIRMWARE;
			mb[1] = *ql1280_board_tbl[ha->devnum].fwstart;
			qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);
		} else
			printk(KERN_ERR "scsi(%li): qla1280_setup_chip: "
			       "Failed checksum\n", ha->host_no);
	}

#if DUMP_IT_BACK
	/* free consistent memory allocated for setup_chip */
	pci_free_consistent(ha->pdev, 8000, tbuf, p_tbuf);
#endif

	if (status)
		dprintk(2, "qla1280_setup_chip: **** FAILED ****\n");

	LEAVE("qla1280_setup_chip");
	return status;
}

/*
 * Initialize rings
 *
 * Input:
 *      ha                = adapter block pointer.
 *      ha->request_ring  = request ring virtual address
 *      ha->response_ring = response ring virtual address
 *      ha->request_dma   = request ring physical address
 *      ha->response_dma  = response ring physical address
 *
 * Returns:
 *      0 = success.
 */
static int
qla1280_init_rings(struct scsi_qla_host *ha)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int status = 0;

	ENTER("qla1280_init_rings");

	/* Clear outstanding commands array. */
	memset(ha->outstanding_cmds, 0,
	       sizeof(struct srb *) * MAX_OUTSTANDING_COMMANDS);

	/* Initialize request queue. */
	ha->request_ring_ptr = ha->request_ring;
	ha->req_ring_index = 0;
	ha->req_q_cnt = REQUEST_ENTRY_CNT;
	/* mb[0] = MBC_INIT_REQUEST_QUEUE; */
	mb[0] = MBC_INIT_REQUEST_QUEUE_A64;
	mb[1] = REQUEST_ENTRY_CNT;
	mb[3] = ha->request_dma & 0xffff;
	mb[2] = (ha->request_dma >> 16) & 0xffff;
	mb[4] = 0;
	mb[7] = pci_dma_hi32(ha->request_dma) & 0xffff;
	mb[6] = pci_dma_hi32(ha->request_dma) >> 16;
	if (!(status = qla1280_mailbox_command(ha, BIT_7 | BIT_6 | BIT_4 |
					       BIT_3 | BIT_2 | BIT_1 | BIT_0,
					       &mb[0]))) {
		/* Initialize response queue. */
		ha->response_ring_ptr = ha->response_ring;
		ha->rsp_ring_index = 0;
		/* mb[0] = MBC_INIT_RESPONSE_QUEUE; */
		mb[0] = MBC_INIT_RESPONSE_QUEUE_A64;
		mb[1] = RESPONSE_ENTRY_CNT;
		mb[3] = ha->response_dma & 0xffff;
		mb[2] = (ha->response_dma >> 16) & 0xffff;
		mb[5] = 0;
		mb[7] = pci_dma_hi32(ha->response_dma) & 0xffff;
		mb[6] = pci_dma_hi32(ha->response_dma) >> 16;
		status = qla1280_mailbox_command(ha, BIT_7 | BIT_6 | BIT_5 |
						 BIT_3 | BIT_2 | BIT_1 | BIT_0,
						 &mb[0]);
	}

	if (status)
		dprintk(2, "qla1280_init_rings: **** FAILED ****\n");

	LEAVE("qla1280_init_rings");
	return status;
}

/*
 * NVRAM configuration.
 *
 * Input:
 *      ha                = adapter block pointer.
 *      ha->request_ring  = request ring virtual address
 *
 * Output:
 *      host adapters parameters in host adapter block
 *
 * Returns:
 *      0 = success.
 */
static int
qla1280_nvram_config(struct scsi_qla_host *ha)
{
	struct device_reg *reg = ha->iobase;
	struct nvram *nv;
	int is1x160, status = 0;
	int bus, target, lun;
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint16_t mask;

	ENTER("qla1280_nvram_config");

	if (ha->device_id == PCI_DEVICE_ID_QLOGIC_ISP12160 ||
	    ha->device_id == PCI_DEVICE_ID_QLOGIC_ISP10160)
		is1x160 = 1;
	else
		is1x160 = 0;

	nv = &ha->nvram;
	if (!ha->nvram_valid) {
		dprintk(1, "Using defaults for NVRAM: \n");
		memset(nv, 0, sizeof(struct nvram));

		/* nv->cntr_flags_1.disable_loading_risc_code = 1; */
		nv->firmware_feature.f.enable_fast_posting = 1;
		nv->firmware_feature.f.disable_synchronous_backoff = 1;

		nv->termination.f.scsi_bus_0_control = 3;
		nv->termination.f.scsi_bus_1_control = 3;
		nv->termination.f.auto_term_support = 1;

		/*
		 * Set default FIFO magic - What appropriate values
		 * would be here is unknown. This is what I have found
		 * testing with 12160s.
		 * Now, I would love the magic decoder ring for this one,
		 * the header file provided by QLogic seems to be bogus
		 * or incomplete at best.
		 */
		nv->isp_config.c = 0x44;

		if (is1x160)
			nv->isp_parameter = 0x01;

		for (bus = 0; bus < MAX_BUSES; bus++) {
			nv->bus[bus].config_1.initiator_id = 7;
			nv->bus[bus].bus_reset_delay = 5;
			/* 8 = 5.0 clocks */
			nv->bus[bus].config_2.async_data_setup_time = 8;
			nv->bus[bus].config_2.req_ack_active_negation = 1;
			nv->bus[bus].config_2.data_line_active_negation = 1;
			nv->bus[bus].selection_timeout = 250;
			nv->bus[bus].max_queue_depth = 256;

			for (target = 0; target < MAX_TARGETS; target++) {
				nv->bus[bus].target[target].parameter.f.
					renegotiate_on_error = 1;
				nv->bus[bus].target[target].parameter.f.
					auto_request_sense = 1;
				nv->bus[bus].target[target].parameter.f.
					tag_queuing = 1;
				nv->bus[bus].target[target].parameter.f.
					enable_sync = 1;
#if 1	/* Some SCSI Processors do not seem to like this */
				nv->bus[bus].target[target].parameter.f.
					enable_wide = 1;
#endif
				nv->bus[bus].target[target].parameter.f.
					parity_checking = 1;
				nv->bus[bus].target[target].parameter.f.
					disconnect_allowed = 1;
				nv->bus[bus].target[target].execution_throttle=
					nv->bus[bus].max_queue_depth - 1;
				if (is1x160) {
					nv->bus[bus].target[target].flags.
						flags1x160.device_enable = 1;
					nv->bus[bus].target[target].flags.
						flags1x160.sync_offset = 0x0e;
					nv->bus[bus].target[target].
						sync_period = 9;
					nv->bus[bus].target[target].
						ppr_1x160.flags.enable_ppr = 1;
					nv->bus[bus].target[target].ppr_1x160.
						flags.ppr_options = 2;
					nv->bus[bus].target[target].ppr_1x160.
						flags.ppr_bus_width = 1;
				} else {
					nv->bus[bus].target[target].flags.
						flags1x80.device_enable = 1;
					nv->bus[bus].target[target].flags.
						flags1x80.sync_offset = 0x8;
					nv->bus[bus].target[target].
						sync_period = 10;
				}
			}
		}
	} else {
		/* Always force AUTO sense for LINUX SCSI */
		for (bus = 0; bus < MAX_BUSES; bus++)
			for (target = 0; target < MAX_TARGETS; target++) {
				nv->bus[bus].target[target].parameter.f.
					auto_request_sense = 1;
			}
	}
	dprintk(1, "qla1280 : initiator scsi id bus[0]=%d\n",
		nv->bus[0].config_1.initiator_id);
	dprintk(1, "qla1280 : initiator scsi id bus[1]=%d\n",
		nv->bus[1].config_1.initiator_id);

	dprintk(1, "qla1280 : bus reset delay[0]=%d\n",
		nv->bus[0].bus_reset_delay);
	dprintk(1, "qla1280 : bus reset delay[1]=%d\n",
		nv->bus[1].bus_reset_delay);

	dprintk(1, "qla1280 : retry count[0]=%d\n", nv->bus[0].retry_count);
	dprintk(1, "qla1280 : retry delay[0]=%d\n", nv->bus[0].retry_delay);
	dprintk(1, "qla1280 : retry count[1]=%d\n", nv->bus[1].retry_count);
	dprintk(1, "qla1280 : retry delay[1]=%d\n", nv->bus[1].retry_delay);

	dprintk(1, "qla1280 : async data setup time[0]=%d\n",
		nv->bus[0].config_2.async_data_setup_time);
	dprintk(1, "qla1280 : async data setup time[1]=%d\n",
		nv->bus[1].config_2.async_data_setup_time);

	dprintk(1, "qla1280 : req/ack active negation[0]=%d\n",
		nv->bus[0].config_2.req_ack_active_negation);
	dprintk(1, "qla1280 : req/ack active negation[1]=%d\n",
		nv->bus[1].config_2.req_ack_active_negation);

	dprintk(1, "qla1280 : data line active negation[0]=%d\n",
		nv->bus[0].config_2.data_line_active_negation);
	dprintk(1, "qla1280 : data line active negation[1]=%d\n",
		nv->bus[1].config_2.data_line_active_negation);

	dprintk(1, "qla1280 : disable loading risc code=%d\n",
		nv->cntr_flags_1.disable_loading_risc_code);

	dprintk(1, "qla1280 : enable 64bit addressing=%d\n",
		nv->cntr_flags_1.enable_64bit_addressing);

	dprintk(1, "qla1280 : selection timeout limit[0]=%d\n",
		nv->bus[0].selection_timeout);
	dprintk(1, "qla1280 : selection timeout limit[1]=%d\n",
		nv->bus[1].selection_timeout);

	dprintk(1, "qla1280 : max queue depth[0]=%d\n",
		nv->bus[0].max_queue_depth);
	dprintk(1, "qla1280 : max queue depth[1]=%d\n",
		nv->bus[1].max_queue_depth);

	/* Disable RISC load of firmware. */
	ha->flags.disable_risc_code_load =
		nv->cntr_flags_1.disable_loading_risc_code;

#ifdef QLA_64BIT_PTR
	/* Enable 64bit addressing for OS/System combination supporting it   */
	/* actual NVRAM bit is: nv->cntr_flags_1.enable_64bit_addressing     */
	/* but we will ignore it and use BITS_PER_LONG macro to setup for    */
	/* 64 or 32 bit access of host memory in all x86/ia-64/Alpha systems */
	ha->flags.enable_64bit_addressing = 1;
#else
	ha->flags.enable_64bit_addressing = 0;
#endif

	if (ha->flags.enable_64bit_addressing) {
		dprintk(2, "scsi(%li): 64 Bit PCI Addressing Enabled\n",
			ha->host_no);

		pci_set_dma_mask(ha->pdev, (dma_addr_t) ~ 0ULL);
	}

	/* Set ISP hardware DMA burst */
	mb[0] = nv->isp_config.c;
	/* Enable DMA arbitration on dual channel controllers */
	if (ha->ports > 1)
		mb[0] |= BIT_13;
	WRT_REG_WORD(&reg->cfg_1, mb[0]);

#if 1	/* Is this safe? */
	/* Set SCSI termination. */
	WRT_REG_WORD(&reg->gpio_enable, (BIT_3 + BIT_2 + BIT_1 + BIT_0));
	mb[0] = nv->termination.c & (BIT_3 + BIT_2 + BIT_1 + BIT_0);
	WRT_REG_WORD(&reg->gpio_data, mb[0]);
#endif

	/* ISP parameter word. */
	mb[0] = MBC_SET_SYSTEM_PARAMETER;
	mb[1] = nv->isp_parameter;
	status |= qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);

#if 0
	/* clock rate - for qla1240 and older, only */
	mb[0] = MBC_SET_CLOCK_RATE;
	mb[1] = 0x50;
 	status |= qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);
#endif
	/* Firmware feature word. */
	mb[0] = MBC_SET_FIRMWARE_FEATURES;
	mask = BIT_5 | BIT_1 | BIT_0;
	mb[1] = le16_to_cpu(nv->firmware_feature.w) & (mask);
#if defined(CONFIG_IA64_GENERIC) || defined (CONFIG_IA64_SGI_SN2)
	if (ia64_platform_is("sn2")) {
		printk(KERN_INFO "scsi(%li): Enabling SN2 PCI DMA "
		       "workaround\n", ha->host_no);
		mb[1] |= BIT_9;
	}
#endif
	status |= qla1280_mailbox_command(ha, mask, &mb[0]);

	/* Retry count and delay. */
	mb[0] = MBC_SET_RETRY_COUNT;
	mb[1] = nv->bus[0].retry_count;
	mb[2] = nv->bus[0].retry_delay;
	mb[6] = nv->bus[1].retry_count;
	mb[7] = nv->bus[1].retry_delay;
	status |= qla1280_mailbox_command(ha, BIT_7 | BIT_6 | BIT_2 |
					  BIT_1 | BIT_0, &mb[0]);

	/* ASYNC data setup time. */
	mb[0] = MBC_SET_ASYNC_DATA_SETUP;
	mb[1] = nv->bus[0].config_2.async_data_setup_time;
	mb[2] = nv->bus[1].config_2.async_data_setup_time;
	status |= qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	/* Active negation states. */
	mb[0] = MBC_SET_ACTIVE_NEGATION;
	mb[1] = 0;
	if (nv->bus[0].config_2.req_ack_active_negation)
		mb[1] |= BIT_5;
	if (nv->bus[0].config_2.data_line_active_negation)
		mb[1] |= BIT_4;
	mb[2] = 0;
	if (nv->bus[1].config_2.req_ack_active_negation)
		mb[2] |= BIT_5;
	if (nv->bus[1].config_2.data_line_active_negation)
		mb[2] |= BIT_4;
	status |= qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	mb[0] = MBC_SET_DATA_OVERRUN_RECOVERY;
	mb[1] = 2;	/* Reset SCSI bus and return all outstanding IO */
	status |= qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);

	/* thingy */
	mb[0] = MBC_SET_PCI_CONTROL;
	mb[1] = 2;	/* Data DMA Channel Burst Enable */
	mb[2] = 2;	/* Command DMA Channel Burst Enable */
	status |= qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	/* Selection timeout. */
	mb[0] = MBC_SET_SELECTION_TIMEOUT;
	mb[1] = nv->bus[0].selection_timeout;
	mb[2] = nv->bus[1].selection_timeout;
	status |= qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	for (bus = 0; bus < ha->ports; bus++) {
		/* SCSI Reset Disable. */
		ha->bus_settings[bus].disable_scsi_reset =
			nv->bus[bus].config_1.scsi_reset_disable;

		/* Initiator ID. */
		ha->bus_settings[bus].id = nv->bus[bus].config_1.initiator_id;
		mb[0] = MBC_SET_INITIATOR_ID;
		mb[1] = bus ? ha->bus_settings[bus].id | BIT_7 :
			ha->bus_settings[bus].id;
		status |= qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);

		/* Reset Delay. */
		ha->bus_settings[bus].bus_reset_delay =
			nv->bus[bus].bus_reset_delay;

		/* Command queue depth per device. */
		ha->bus_settings[bus].hiwat = nv->bus[bus].max_queue_depth - 1;

		/* Set target parameters. */
		for (target = 0; target < MAX_TARGETS; target++) {
			uint8_t mr = BIT_2 | BIT_1 | BIT_0;

			/* Set Target Parameters. */
			mb[0] = MBC_SET_TARGET_PARAMETERS;
			mb[1] = (uint16_t) (bus ? target | BIT_7 : target);
			mb[1] <<= 8;
			/*
			 * Do not enable wide, sync, and ppr for the initial
			 * INQUIRY run. We enable this later if we determine
			 * the target actually supports it.
			 */
			nv->bus[bus].target[target].parameter.f.
				auto_request_sense = 1;
			nv->bus[bus].target[target].parameter.f.
				stop_queue_on_check = 0;

			if (is1x160)
				nv->bus[bus].target[target].ppr_1x160.
					flags.enable_ppr = 0;
			/*
			 * No sync, wide, etc. while probing
			 */
			mb[2] = (nv->bus[bus].target[target].parameter.c << 8)&
				~(TP_SYNC /*| TP_WIDE | TP_PPR*/);

			if (is1x160)
				mb[3] =	nv->bus[bus].target[target].flags.flags1x160.sync_offset << 8;
			else
				mb[3] =	nv->bus[bus].target[target].flags.flags1x80.sync_offset << 8;
			mb[3] |= nv->bus[bus].target[target].sync_period;
			mr |= BIT_3;

			/*
			 * We don't want to enable ppr etc. before we have 
			 * determined that the target actually supports it
			 */
#if 0
			if (is1x160) {
				mb[2] |= nv->bus[bus].target[target].ppr_1x160.flags.enable_ppr << 5;

				mb[6] =	nv->bus[bus].target[target].ppr_1x160.flags.ppr_options << 8;
				mb[6] |= nv->bus[bus].target[target].ppr_1x160.flags.ppr_bus_width;
				mr |= BIT_6;
			}
#endif

			status = qla1280_mailbox_command(ha, mr, &mb[0]);

			/* Save Tag queuing enable flag. */
			mb[0] = BIT_0 << target;
			if (nv->bus[bus].target[target].parameter.f.tag_queuing)
				ha->bus_settings[bus].qtag_enables |= mb[0];

			/* Save Device enable flag. */
			if (is1x160) {
				if (nv->bus[bus].target[target].flags.flags1x160.device_enable)
					ha->bus_settings[bus].device_enables |= mb[0];
				ha->bus_settings[bus].lun_disables |= 0;
			} else {
				if (nv->bus[bus].target[target].flags.flags1x80.device_enable)
					ha->bus_settings[bus].device_enables |= mb[0];
				/* Save LUN disable flag. */
				if (nv->bus[bus].target[target].flags.flags1x80.lun_disable)
				ha->bus_settings[bus].lun_disables |= mb[0];
			}


			/* Set Device Queue Parameters. */
			for (lun = 0; lun < MAX_LUNS; lun++) {
				mb[0] = MBC_SET_DEVICE_QUEUE;
				mb[1] = (uint16_t)(bus ? target | BIT_7 : target);
				mb[1] = mb[1] << 8 | lun;
				mb[2] = nv->bus[bus].max_queue_depth;
				mb[3] = nv->bus[bus].target[target].execution_throttle;
				status |= qla1280_mailbox_command(ha, 0x0f,
								  &mb[0]);
			}
		}
	}

	if (status)
		dprintk(2, "qla1280_nvram_config: **** FAILED ****\n");

	LEAVE("qla1280_nvram_config");
	return status;
}

/*
 * Get NVRAM data word
 *      Calculates word position in NVRAM and calls request routine to
 *      get the word from NVRAM.
 *
 * Input:
 *      ha      = adapter block pointer.
 *      address = NVRAM word address.
 *
 * Returns:
 *      data word.
 */
static uint16_t
qla1280_get_nvram_word(struct scsi_qla_host *ha, uint32_t address)
{
	uint32_t nv_cmd;
	uint16_t data;

	nv_cmd = address << 16;
	nv_cmd |= NV_READ_OP;

	data = le16_to_cpu(qla1280_nvram_request(ha, nv_cmd));

	dprintk(8, "qla1280_get_nvram_word: exiting normally NVRAM data = "
		"0x%x", data);

	return data;
}

/*
 * NVRAM request
 *      Sends read command to NVRAM and gets data from NVRAM.
 *
 * Input:
 *      ha     = adapter block pointer.
 *      nv_cmd = Bit 26     = start bit
 *               Bit 25, 24 = opcode
 *               Bit 23-16  = address
 *               Bit 15-0   = write data
 *
 * Returns:
 *      data word.
 */
static uint16_t
qla1280_nvram_request(struct scsi_qla_host *ha, uint32_t nv_cmd)
{
	struct device_reg *reg = ha->iobase;
	int cnt;
	uint16_t data = 0;
	uint16_t reg_data;

	/* Send command to NVRAM. */

	nv_cmd <<= 5;
	for (cnt = 0; cnt < 11; cnt++) {
		if (nv_cmd & BIT_31)
			qla1280_nv_write(ha, NV_DATA_OUT);
		else
			qla1280_nv_write(ha, 0);
		nv_cmd <<= 1;
	}

	/* Read data from NVRAM. */

	for (cnt = 0; cnt < 16; cnt++) {
		WRT_REG_WORD(&reg->nvram, (NV_SELECT | NV_CLOCK));
		RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
		NVRAM_DELAY();
		data <<= 1;
		reg_data = RD_REG_WORD(&reg->nvram);
		if (reg_data & NV_DATA_IN)
			data |= BIT_0;
		WRT_REG_WORD(&reg->nvram, NV_SELECT);
		RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
		NVRAM_DELAY();
	}

	/* Deselect chip. */

	WRT_REG_WORD(&reg->nvram, NV_DESELECT);
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
	NVRAM_DELAY();

	return data;
}

static void
qla1280_nv_write(struct scsi_qla_host *ha, uint16_t data)
{
	struct device_reg *reg = ha->iobase;

	WRT_REG_WORD(&reg->nvram, data | NV_SELECT);
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
	NVRAM_DELAY();
	WRT_REG_WORD(&reg->nvram, data | NV_SELECT | NV_CLOCK);
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
	NVRAM_DELAY();
	WRT_REG_WORD(&reg->nvram, data | NV_SELECT);
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */
	NVRAM_DELAY();
}

/*
 * Mailbox Command
 *      Issue mailbox command and waits for completion.
 *
 * Input:
 *      ha = adapter block pointer.
 *      mr = mailbox registers to load.
 *      mb = data pointer for mailbox registers.
 *
 * Output:
 *      mb[MAILBOX_REGISTER_COUNT] = returned mailbox data.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_mailbox_command(struct scsi_qla_host *ha, uint8_t mr, uint16_t *mb)
{
	struct device_reg *reg = ha->iobase;
#if 0
	struct srb *done_q_first = 0;
	struct srb *done_q_last = 0;
#endif
	int status = 0;
	int cnt;
	uint16_t *optr, *iptr;
	uint16_t data;
	DECLARE_COMPLETION(wait);
	struct timer_list timer;

	ENTER("qla1280_mailbox_command");

	ha->flags.mbox_busy = 1;

	if (ha->mailbox_wait) {
		printk(KERN_ERR "Warning mailbox wait already in use!\n");
	}
	ha->mailbox_wait = &wait;

	/*
	 * We really should start out by verifying that the mailbox is
	 * available before starting sending the command data
	 */
	/* Load mailbox registers. */
	optr = (uint16_t *) &reg->mailbox0;
	iptr = mb;
	for (cnt = 0; cnt < MAILBOX_REGISTER_COUNT; cnt++) {
		if (mr & BIT_0) {
			WRT_REG_WORD(optr, (*iptr));
		}

		mr >>= 1;
		optr++;
		iptr++;
	}

	/* Issue set host interrupt command. */
	ha->flags.mbox_busy = 0;

	/* set up a timer just in case we're really jammed */
	init_timer(&timer);
	timer.expires = jiffies + 20*HZ;
	timer.data = (unsigned long)ha;
	timer.function = qla1280_mailbox_timeout;
	add_timer(&timer);

#if LINUX_VERSION_CODE < 0x020500
	spin_unlock_irq(HOST_LOCK);
#endif
	WRT_REG_WORD(&reg->host_cmd, HC_SET_HOST_INT);
	data = qla1280_debounce_register(&reg->istatus);

	wait_for_completion(&wait);
	del_timer_sync(&timer);

#if LINUX_VERSION_CODE < 0x020500
	spin_lock_irq(HOST_LOCK);
#endif

	ha->mailbox_wait = NULL;

	/* Check for mailbox command timeout. */
	if (ha->mailbox_out[0] != MBS_CMD_CMP) {
		printk(KERN_WARNING "qla1280_mailbox_command: Command failed, "
		       "mailbox0 = 0x%04x, mailbox_out0 = 0x%04x, istatus = "
		       "0x%04x\n", 
		       mb[0], ha->mailbox_out[0], RD_REG_WORD(&reg->istatus));
		printk(KERN_WARNING "m0 %04x, m1 %04x, m2 %04x, m3 %04x\n",
		       RD_REG_WORD(&reg->mailbox0), RD_REG_WORD(&reg->mailbox1),
		       RD_REG_WORD(&reg->mailbox2), RD_REG_WORD(&reg->mailbox3));
		printk(KERN_WARNING "m4 %04x, m5 %04x, m6 %04x, m7 %04x\n",
		       RD_REG_WORD(&reg->mailbox4), RD_REG_WORD(&reg->mailbox5),
		       RD_REG_WORD(&reg->mailbox6), RD_REG_WORD(&reg->mailbox7));
		status = 1;
	}

	/* Load return mailbox registers. */
	optr = mb;
	iptr = (uint16_t *) &ha->mailbox_out[0];
	mr = MAILBOX_REGISTER_COUNT;
	memcpy(optr, iptr, MAILBOX_REGISTER_COUNT * sizeof(uint16_t));

#if 0
	/* Go check for any response interrupts pending. */
	qla1280_isr(ha, &done_q_first, &done_q_last);
#endif

	if (ha->flags.reset_marker)
		qla1280_rst_aen(ha);

#if 0
	if (done_q_first)
		qla1280_done (ha, &done_q_first, &done_q_last);
#endif

	if (status)
		dprintk(2, "qla1280_mailbox_command: **** FAILED, mailbox0 = "
			"0x%x ****\n", mb[0]);

	LEAVE("qla1280_mailbox_command");
	return status;
}

/*
 * qla1280_poll
 *      Polls ISP for interrupts.
 *
 * Input:
 *      ha = adapter block pointer.
 */
static void
qla1280_poll(struct scsi_qla_host *ha)
{
	struct device_reg *reg = ha->iobase;
	uint16_t data;
	struct srb *done_q_first = 0;
	struct srb *done_q_last = 0;

	/* ENTER("qla1280_poll"); */

	/* Check for pending interrupts. */
	data = RD_REG_WORD(&reg->istatus);
	if (data & RISC_INT)
		qla1280_isr(ha, &done_q_first, &done_q_last);

	if (!ha->flags.mbox_busy) {
		if (ha->flags.reset_marker)
			qla1280_rst_aen(ha);
	}

	if (done_q_first)
		qla1280_done(ha, &done_q_first, &done_q_last);

	/* LEAVE("qla1280_poll"); */
}

/*
 * qla1280_bus_reset
 *      Issue SCSI bus reset.
 *
 * Input:
 *      ha  = adapter block pointer.
 *      bus = SCSI bus number.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_bus_reset(struct scsi_qla_host *ha, int bus)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	uint16_t reset_delay;
	int status;

	dprintk(3, "qla1280_bus_reset: entered\n");

	if (qla1280_verbose)
		printk(KERN_INFO "scsi(%li:%i): Resetting SCSI BUS\n",
		       ha->host_no, bus);

	reset_delay = ha->bus_settings[bus].bus_reset_delay;
	mb[0] = MBC_BUS_RESET;
	mb[1] = reset_delay;
	mb[2] = (uint16_t) bus;
	status = qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	if (status) {
		if (ha->bus_settings[bus].failed_reset_count > 2)
			ha->bus_settings[bus].scsi_bus_dead = 1;
		ha->bus_settings[bus].failed_reset_count++;
	} else {
		spin_unlock_irq(HOST_LOCK);
		schedule_timeout(reset_delay * HZ);
		spin_lock_irq(HOST_LOCK);

		ha->bus_settings[bus].scsi_bus_dead = 0;
		ha->bus_settings[bus].failed_reset_count = 0;
		ha->bus_settings[bus].reset_marker = 0;
		/* Issue marker command. */
		qla1280_marker(ha, bus, 0, 0, MK_SYNC_ALL);
	}

	/*
	 * We should probably call qla1280_set_target_parameters()
	 * here as well for all devices on the bus.
	 */

	if (status)
		dprintk(2, "qla1280_bus_reset: **** FAILED ****\n");
	else
		dprintk(3, "qla1280_bus_reset: exiting normally\n");

	return status;
}

/*
 * qla1280_device_reset
 *      Issue bus device reset message to the target.
 *
 * Input:
 *      ha      = adapter block pointer.
 *      bus     = SCSI BUS number.
 *      target  = SCSI ID.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_device_reset(struct scsi_qla_host *ha, int bus, int target)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int status;

	ENTER("qla1280_device_reset");

	mb[0] = MBC_ABORT_TARGET;
	mb[1] = (bus ? (target | BIT_7) : target) << 8;
	mb[2] = 1;
	status = qla1280_mailbox_command(ha, BIT_2 | BIT_1 | BIT_0, &mb[0]);

	/* Issue marker command. */
	qla1280_marker(ha, bus, target, 0, MK_SYNC_ID);

	if (status)
		dprintk(2, "qla1280_device_reset: **** FAILED ****\n");

	LEAVE("qla1280_device_reset");
	return status;
}

/*
 * qla1280_abort_device
 *      Issue an abort message to the device
 *
 * Input:
 *      ha     = adapter block pointer.
 *      bus    = SCSI BUS.
 *      target = SCSI ID.
 *      lun    = SCSI LUN.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_abort_device(struct scsi_qla_host *ha, int bus, int target, int lun)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int status;

	ENTER("qla1280_abort_device");

	mb[0] = MBC_ABORT_DEVICE;
	mb[1] = (bus ? target | BIT_7 : target) << 8 | lun;
	status = qla1280_mailbox_command(ha, BIT_1 | BIT_0, &mb[0]);

	/* Issue marker command. */
	qla1280_marker(ha, bus, target, lun, MK_SYNC_ID_LUN);

	if (status)
		dprintk(2, "qla1280_abort_device: **** FAILED ****\n");

	LEAVE("qla1280_abort_device");
	return status;
}

/*
 * qla1280_abort_command
 *      Abort command aborts a specified IOCB.
 *
 * Input:
 *      ha = adapter block pointer.
 *      sp = SB structure pointer.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_abort_command(struct scsi_qla_host *ha, struct srb * sp, int handle)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	unsigned int bus, target, lun;
	int status;

	ENTER("qla1280_abort_command");

	bus = SCSI_BUS_32(sp->cmd);
	target = SCSI_TCN_32(sp->cmd);
	lun = SCSI_LUN_32(sp->cmd);

	sp->flags |= SRB_ABORT_PENDING;

	mb[0] = MBC_ABORT_COMMAND;
	mb[1] = (bus ? target | BIT_7 : target) << 8 | lun;
	mb[2] = handle >> 16;
	mb[3] = handle & 0xffff;
	status = qla1280_mailbox_command(ha, 0x0f, &mb[0]);

	if (status) {
		dprintk(2, "qla1280_abort_command: **** FAILED ****\n");
		sp->flags &= ~SRB_ABORT_PENDING;
	}


	LEAVE("qla1280_abort_command");
	return status;
}

/*
 * qla1280_reset_adapter
 *      Reset adapter.
 *
 * Input:
 *      ha = adapter block pointer.
 */
static void
qla1280_reset_adapter(struct scsi_qla_host *ha)
{
	struct device_reg *reg = ha->iobase;

	ENTER("qla1280_reset_adapter");

	/* Disable ISP chip */
	ha->flags.online = 0;
	WRT_REG_WORD(&reg->ictrl, ISP_RESET);
	WRT_REG_WORD(&reg->host_cmd,
		     HC_RESET_RISC | HC_RELEASE_RISC | HC_DISABLE_BIOS);
	RD_REG_WORD(&reg->id_l);	/* Flush PCI write */

	LEAVE("qla1280_reset_adapter");
}

/*
 *  Issue marker command.
 *      Function issues marker IOCB.
 *
 * Input:
 *      ha   = adapter block pointer.
 *      bus  = SCSI BUS number
 *      id   = SCSI ID
 *      lun  = SCSI LUN
 *      type = marker modifier
 */
static void
qla1280_marker(struct scsi_qla_host *ha, int bus, int id, int lun, u8 type)
{
	struct mrk_entry *pkt;

	ENTER("qla1280_marker");

	/* Get request packet. */
	if ((pkt = (struct mrk_entry *) qla1280_req_pkt(ha))) {
		pkt->entry_type = MARKER_TYPE;
		pkt->lun = (uint8_t) lun;
		pkt->target = (uint8_t) (bus ? (id | BIT_7) : id);
		pkt->modifier = type;
		pkt->entry_status = 0;

		/* Issue command to ISP */
		qla1280_isp_cmd(ha);
	}

	LEAVE("qla1280_marker");
}


/*
 * qla1280_64bit_start_scsi
 *      The start SCSI is responsible for building request packets on
 *      request ring and modifying ISP input pointer.
 *
 * Input:
 *      ha = adapter block pointer.
 *      sp = SB structure pointer.
 *
 * Returns:
 *      0 = success, was able to issue command.
 */
static int
qla1280_64bit_start_scsi(struct scsi_qla_host *ha, struct srb * sp)
{
	struct device_reg *reg = ha->iobase;
	Scsi_Cmnd *cmd = sp->cmd;
	cmd_a64_entry_t *pkt;
	struct scatterlist *sg = NULL;
	u32 *dword_ptr;
	dma_addr_t dma_handle;
	int status = 0;
	int cnt;
	int req_cnt;
	u16 seg_cnt;

	ENTER("qla1280_64bit_start_scsi:");

	/* Calculate number of entries and segments required. */
	req_cnt = 1;
	if (cmd->use_sg) {
		sg = (struct scatterlist *) cmd->request_buffer;
		seg_cnt = pci_map_sg(ha->pdev, sg, cmd->use_sg,
				     scsi_to_pci_dma_dir(cmd->sc_data_direction));

		if (seg_cnt > 2) {
			req_cnt += (seg_cnt - 2) / 5;
			if ((seg_cnt - 2) % 5)
				req_cnt++;
		}
	} else if (cmd->request_bufflen) {	/* If data transfer. */
		seg_cnt = 1;
	} else {
		seg_cnt = 0;
	}

	if ((req_cnt + 2) >= ha->req_q_cnt) {
		/* Calculate number of free request entries. */
		cnt = RD_REG_WORD(&reg->mailbox4);
		if (ha->req_ring_index < cnt)
			ha->req_q_cnt = cnt - ha->req_ring_index;
		else
			ha->req_q_cnt =
				REQUEST_ENTRY_CNT - (ha->req_ring_index - cnt);
	}

	/* If room for request in request ring. */
	if ((req_cnt + 2) >= ha->req_q_cnt) {
		status = 1;
		dprintk(2, "qla1280_64bit_start_scsi: in-ptr=0x%x  req_q_cnt="
			"0x%xreq_cnt=0x%x", ha->req_ring_index, ha->req_q_cnt,
			req_cnt);
		goto out;
	}

	/* Check for room in outstanding command list. */
	for (cnt = 0; cnt < MAX_OUTSTANDING_COMMANDS &&
		     ha->outstanding_cmds[cnt] != 0; cnt++);

	if (cnt >= MAX_OUTSTANDING_COMMANDS) {
		status = 1;
		dprintk(2, "qla1280_64bit_start_scsi: NO ROOM IN "
			"OUTSTANDING ARRAY, req_q_cnt=0x%x", ha->req_q_cnt);
		goto out;
	}

	ha->outstanding_cmds[cnt] = sp;
	ha->req_q_cnt -= req_cnt;
	CMD_HANDLE(sp->cmd) = (unsigned char *)(unsigned long)(cnt + 1);

	dprintk(2, "64bit_start: cmd=%p sp=%p CDB=%xm, handle %lx\n", cmd, sp,
		cmd->cmnd[0], (long)CMD_HANDLE(sp->cmd));
	dprintk(2, "             bus %i, target %i, lun %i\n",
		SCSI_BUS_32(cmd), SCSI_TCN_32(cmd), SCSI_LUN_32(cmd));
	qla1280_dump_buffer(2, cmd->cmnd, MAX_COMMAND_SIZE);

	/*
	 * Build command packet.
	 */
	pkt = (cmd_a64_entry_t *) ha->request_ring_ptr;

	pkt->entry_type = COMMAND_A64_TYPE;
	pkt->entry_count = (uint8_t) req_cnt;
	pkt->sys_define = (uint8_t) ha->req_ring_index;
	pkt->entry_status = 0;
	pkt->handle = cpu_to_le32(cnt);

	/* Zero out remaining portion of packet. */
	memset(((char *)pkt + 8), 0, (REQUEST_ENTRY_SIZE - 8));

	/* Set ISP command timeout. */
	pkt->timeout = cpu_to_le16(30);

	/* Set device target ID and LUN */
	pkt->lun = SCSI_LUN_32(cmd);
	pkt->target = SCSI_BUS_32(cmd) ?
		(SCSI_TCN_32(cmd) | BIT_7) : SCSI_TCN_32(cmd);

	/* Enable simple tag queuing if device supports it. */
	if (cmd->device->simple_tags)
		pkt->control_flags |= cpu_to_le16(BIT_3);

	/* Load SCSI command packet. */
	pkt->cdb_len = cpu_to_le16(CMD_CDBLEN(cmd));
	memcpy(pkt->scsi_cdb, &(CMD_CDBP(cmd)), CMD_CDBLEN(cmd));
	/* dprintk(1, "Build packet for command[0]=0x%x\n",pkt->scsi_cdb[0]); */

	/* Set transfer direction. */
	sp->dir = qla1280_data_direction(cmd);
	pkt->control_flags |= cpu_to_le16(sp->dir);

	/* Set total data segment count. */
	pkt->dseg_count = cpu_to_le16(seg_cnt);

	/*
	 * Load data segments.
	 */
	if (seg_cnt) {	/* If data transfer. */
		/* Setup packet address segment pointer. */
		dword_ptr = (u32 *)&pkt->dseg_0_address;

		if (cmd->use_sg) {	/* If scatter gather */
			/* Load command entry data segments. */
			for (cnt = 0; cnt < 2 && seg_cnt; cnt++, seg_cnt--) {
				dma_handle = sg_dma_address(sg);
#if defined(CONFIG_IA64_GENERIC) || defined(CONFIG_IA64_SGI_SN2)
				if (ha->flags.use_pci_vchannel)
					sn_pci_set_vchan(ha->pdev, &dma_handle,
							 SCSI_BUS_32(cmd));
#endif
				*dword_ptr++ =
					cpu_to_le32(pci_dma_lo32(dma_handle));
				*dword_ptr++ =
					cpu_to_le32(pci_dma_hi32(dma_handle));
				*dword_ptr++ = cpu_to_le32(sg_dma_len(sg));
				sg++;
				dprintk(3, "S/G Segment phys_addr=%x %x, len=0x%x\n",
					cpu_to_le32(pci_dma_hi32(dma_handle)),
					cpu_to_le32(pci_dma_lo32(dma_handle)),
					cpu_to_le32(sg_dma_len(sg)));
			}
			dprintk(5, "qla1280_64bit_start_scsi: Scatter/gather "
				"command packet data - b %i, t %i, l %i \n",
				SCSI_BUS_32(cmd), SCSI_TCN_32(cmd),
				SCSI_LUN_32(cmd));
			qla1280_dump_buffer(5, (char *)pkt,
					    REQUEST_ENTRY_SIZE);

			/*
			 * Build continuation packets.
			 */
			dprintk(3, "S/G Building Continuation...seg_cnt=0x%x "
				"remains\n", seg_cnt);

			while (seg_cnt > 0) {
				/* Adjust ring index. */
				ha->req_ring_index++;
				if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
					ha->req_ring_index = 0;
					ha->request_ring_ptr =
						ha->request_ring;
				} else
						ha->request_ring_ptr++;

				pkt = (cmd_a64_entry_t *)ha->request_ring_ptr;

				/* Zero out packet. */
				memset(pkt, 0, REQUEST_ENTRY_SIZE);

				/* Load packet defaults. */
				((struct cont_a64_entry *) pkt)->entry_type =
					CONTINUE_A64_TYPE;
				((struct cont_a64_entry *) pkt)->entry_count = 1;
				((struct cont_a64_entry *) pkt)->sys_define =
					(uint8_t)ha->req_ring_index;
				/* Setup packet address segment pointer. */
				dword_ptr =
					(u32 *)&((struct cont_a64_entry *) pkt)->dseg_0_address;

				/* Load continuation entry data segments. */
				for (cnt = 0; cnt < 5 && seg_cnt;
				     cnt++, seg_cnt--) {
					dma_handle = sg_dma_address(sg);
#if defined(CONFIG_IA64_GENERIC) || defined(CONFIG_IA64_SGI_SN2)
				if (ha->flags.use_pci_vchannel)
					sn_pci_set_vchan(ha->pdev, &dma_handle,
							 SCSI_BUS_32(cmd));
#endif
					*dword_ptr++ =
						cpu_to_le32(pci_dma_lo32(dma_handle));
					*dword_ptr++ =
						cpu_to_le32(pci_dma_hi32(dma_handle));
					*dword_ptr++ =
						cpu_to_le32(sg_dma_len(sg));
					dprintk(3, "S/G Segment Cont. phys_addr=%x %x, len=0x%x\n",
						cpu_to_le32(pci_dma_hi32(dma_handle)),
						cpu_to_le32(pci_dma_lo32(dma_handle)),
						cpu_to_le32(sg_dma_len(sg)));
					sg++;
				}
				dprintk(5, "qla1280_64bit_start_scsi: "
					"continuation packet data - b %i, t "
					"%i, l %i \n", SCSI_BUS_32(cmd),
					SCSI_TCN_32(cmd), SCSI_LUN_32(cmd));
				qla1280_dump_buffer(5, (char *)pkt,
						    REQUEST_ENTRY_SIZE);
			}
		} else {	/* No scatter gather data transfer */
			struct page *page = virt_to_page(cmd->request_buffer);
			unsigned long off = (unsigned long)cmd->request_buffer & ~PAGE_MASK;

			dma_handle = pci_map_page(ha->pdev, page, off,
						  cmd->request_bufflen,
						  scsi_to_pci_dma_dir(cmd->sc_data_direction));

			/* save dma_handle for pci_unmap_page */
			sp->saved_dma_handle = dma_handle;
#if defined(CONFIG_IA64_GENERIC) || defined(CONFIG_IA64_SGI_SN2)
			if (ha->flags.use_pci_vchannel)
				sn_pci_set_vchan(ha->pdev, &dma_handle,
						 SCSI_BUS_32(cmd));
#endif
			*dword_ptr++ = cpu_to_le32(pci_dma_lo32(dma_handle));
			*dword_ptr++ = cpu_to_le32(pci_dma_hi32(dma_handle));
			*dword_ptr = (uint32_t)cmd->request_bufflen;

			dprintk(5, "qla1280_64bit_start_scsi: No scatter/"
				"gather command packet data - b %i, t %i, "
				"l %i \n", SCSI_BUS_32(cmd), SCSI_TCN_32(cmd),
				SCSI_LUN_32(cmd));
			qla1280_dump_buffer(5, (char *)pkt,
					    REQUEST_ENTRY_SIZE);
		}
	} else {	/* No data transfer */
		dword_ptr = (uint32_t *)(pkt + 1);
		*dword_ptr++ = 0;
		*dword_ptr++ = 0;
		*dword_ptr = 0;
		dprintk(5, "qla1280_64bit_start_scsi: No data, command "
			"packet data - b %i, t %i, l %i \n",
			SCSI_BUS_32(cmd), SCSI_TCN_32(cmd), SCSI_LUN_32(cmd));
		qla1280_dump_buffer(5, (char *)pkt, REQUEST_ENTRY_SIZE);
	}
	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	/* Set chip new ring index. */
	dprintk(2,
		"qla1280_64bit_start_scsi: Wakeup RISC for pending command\n");
	sp->flags |= SRB_SENT;
	ha->actthreads++;
	WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);

 out:
	if (status)
		dprintk(2, "qla1280_64bit_start_scsi: **** FAILED ****\n");
	else
		dprintk(3, "qla1280_64bit_start_scsi: exiting normally\n");

	return status;
}


/*
 * qla1280_32bit_start_scsi
 *      The start SCSI is responsible for building request packets on
 *      request ring and modifying ISP input pointer.
 *
 *      The Qlogic firmware interface allows every queue slot to have a SCSI
 *      command and up to 4 scatter/gather (SG) entries.  If we need more
 *      than 4 SG entries, then continuation entries are used that can
 *      hold another 7 entries each.  The start routine determines if there
 *      is eought empty slots then build the combination of requests to
 *      fulfill the OS request.
 *
 * Input:
 *      ha = adapter block pointer.
 *      sp = SCSI Request Block structure pointer.
 *
 * Returns:
 *      0 = success, was able to issue command.
 */
static int
qla1280_32bit_start_scsi(struct scsi_qla_host *ha, struct srb * sp)
{
	struct device_reg *reg = ha->iobase;
	Scsi_Cmnd *cmd = sp->cmd;
	struct cmd_entry *pkt;
	struct scatterlist *sg = NULL;
	uint32_t *dword_ptr;
	int status = 0;
	int cnt;
	int req_cnt;
	uint16_t seg_cnt;
	dma_addr_t dma_handle;

	ENTER("qla1280_32bit_start_scsi");

	dprintk(1, "32bit_start: cmd=%p sp=%p CDB=%x\n", cmd, sp,
		cmd->cmnd[0]);

	/* Calculate number of entries and segments required. */
	req_cnt = 1;
	if (cmd->use_sg) {
		/*
		 * We must build an SG list in adapter format, as the kernel's
		 * SG list cannot be used directly because of data field size
		 * (__alpha__) differences and the kernel SG list uses virtual
		 * addresses where we need physical addresses.
		 */
		sg = (struct scatterlist *) cmd->request_buffer;
		seg_cnt = pci_map_sg(ha->pdev, sg, cmd->use_sg,
				     scsi_to_pci_dma_dir(cmd->sc_data_direction));

		/*
		 * if greater than four sg entries then we need to allocate
		 * continuation entries
		 */
		if (seg_cnt > 4) {
			req_cnt += (seg_cnt - 4) / 7;
			if ((seg_cnt - 4) % 7)
				req_cnt++;
		}
		dprintk(3, "S/G Transfer cmd=%p seg_cnt=0x%x, req_cnt=%x\n",
			cmd, seg_cnt, req_cnt);
	} else if (cmd->request_bufflen) {	/* If data transfer. */
		dprintk(3, "No S/G transfer t=%x cmd=%p len=%x CDB=%x\n",
			SCSI_TCN_32(cmd), cmd, cmd->request_bufflen,
			cmd->cmnd[0]);
		seg_cnt = 1;
	} else {
		/* dprintk(1, "No data transfer \n"); */
		seg_cnt = 0;
	}

	if ((req_cnt + 2) >= ha->req_q_cnt) {
		/* Calculate number of free request entries. */
		cnt = RD_REG_WORD(&reg->mailbox4);
		if (ha->req_ring_index < cnt)
			ha->req_q_cnt = cnt - ha->req_ring_index;
		else
			ha->req_q_cnt =
				REQUEST_ENTRY_CNT - (ha->req_ring_index - cnt);
	}

	dprintk(3, "Number of free entries=(%d) seg_cnt=0x%x\n",
		ha->req_q_cnt, seg_cnt);
	/* If room for request in request ring. */
	if ((req_cnt + 2) >= ha->req_q_cnt) {
		status = 1;
		dprintk(2, "qla1280_32bit_start_scsi: in-ptr=0x%x, "
			"req_q_cnt=0x%x, req_cnt=0x%x", ha->req_ring_index,
			ha->req_q_cnt, req_cnt);
		goto out;
	}

	/* Check for empty slot in outstanding command list. */
	for (cnt = 0; cnt < MAX_OUTSTANDING_COMMANDS &&
		     (ha->outstanding_cmds[cnt] != 0); cnt++) ;

	if (cnt >= MAX_OUTSTANDING_COMMANDS) {
		status = 1;
		dprintk(2, "qla1280_32bit_start_scsi: NO ROOM IN OUTSTANDING "
			"ARRAY, req_q_cnt=0x%x\n", ha->req_q_cnt);
		goto out;
	}

	CMD_HANDLE(sp->cmd) = (unsigned char *) (unsigned long)(cnt + 1);
	ha->outstanding_cmds[cnt] = sp;
	ha->req_q_cnt -= req_cnt;

	/*
	 * Build command packet.
	 */
	pkt = (struct cmd_entry *) ha->request_ring_ptr;

	pkt->entry_type = COMMAND_TYPE;
	pkt->entry_count = (uint8_t) req_cnt;
	pkt->sys_define = (uint8_t) ha->req_ring_index;
	pkt->entry_status = 0;
	pkt->handle = cpu_to_le32(cnt);

	/* Zero out remaining portion of packet. */
	memset(((char *)pkt + 8), 0, (REQUEST_ENTRY_SIZE - 8));

	/* Set ISP command timeout. */
	pkt->timeout = cpu_to_le16(30);

	/* Set device target ID and LUN */
	pkt->lun = SCSI_LUN_32(cmd);
	pkt->target = SCSI_BUS_32(cmd) ?
		(SCSI_TCN_32(cmd) | BIT_7) : SCSI_TCN_32(cmd);

	/* Enable simple tag queuing if device supports it. */
	if (cmd->device->simple_tags)
		pkt->control_flags |= cpu_to_le16(BIT_3);

	/* Load SCSI command packet. */
	pkt->cdb_len = cpu_to_le16(CMD_CDBLEN(cmd));
	memcpy(pkt->scsi_cdb, &(CMD_CDBP(cmd)), CMD_CDBLEN(cmd));

	/*dprintk(1, "Build packet for command[0]=0x%x\n",pkt->scsi_cdb[0]); */
	/* Set transfer direction. */
	sp->dir = qla1280_data_direction(cmd);
	pkt->control_flags |= cpu_to_le16(sp->dir);

	/* Set total data segment count. */
	pkt->dseg_count = cpu_to_le16(seg_cnt);

	/*
	 * Load data segments.
	 */
	if (seg_cnt) {
		/* Setup packet address segment pointer. */
		dword_ptr = &pkt->dseg_0_address;

		if (cmd->use_sg) {	/* If scatter gather */
			dprintk(3, "Building S/G data segments..\n");
			qla1280_dump_buffer(1, (char *)sg, 4 * 16);

			/* Load command entry data segments. */
			for (cnt = 0; cnt < 4 && seg_cnt; cnt++, seg_cnt--) {
				*dword_ptr++ =
					cpu_to_le32(pci_dma_lo32(sg_dma_address(sg)));
				*dword_ptr++ =
					cpu_to_le32(sg_dma_len(sg));
				dprintk(3, "S/G Segment phys_addr=0x%lx, len=0x%x\n",
					(pci_dma_lo32(sg_dma_address(sg))),
					(sg_dma_len(sg)));
				sg++;
			}
			/*
			 * Build continuation packets.
			 */
			dprintk(3, "S/G Building Continuation"
				"...seg_cnt=0x%x remains\n", seg_cnt);
			while (seg_cnt > 0) {
				/* Adjust ring index. */
				ha->req_ring_index++;
				if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
					ha->req_ring_index = 0;
					ha->request_ring_ptr =
						ha->request_ring;
				} else
					ha->request_ring_ptr++;

				pkt = (struct cmd_entry *)ha->request_ring_ptr;

				/* Zero out packet. */
				memset(pkt, 0, REQUEST_ENTRY_SIZE);

				/* Load packet defaults. */
				((struct cont_entry *) pkt)->
					entry_type = CONTINUE_TYPE;
				((struct cont_entry *) pkt)->entry_count = 1;

				((struct cont_entry *) pkt)->sys_define =
					(uint8_t) ha->req_ring_index;

				/* Setup packet address segment pointer. */
				dword_ptr =
					&((struct cont_entry *) pkt)->dseg_0_address;

				/* Load continuation entry data segments. */
				for (cnt = 0; cnt < 7 && seg_cnt;
				     cnt++, seg_cnt--) {
					*dword_ptr++ =
						cpu_to_le32(pci_dma_lo32(sg_dma_address(sg)));
					*dword_ptr++ =
						cpu_to_le32(sg_dma_len(sg));
					dprintk(1,
						"S/G Segment Cont. phys_addr=0x%x, "
						"len=0x%x\n",
						cpu_to_le32(pci_dma_lo32(sg_dma_address(sg))),
						cpu_to_le32(sg_dma_len(sg)));
					sg++;
				}
				dprintk(5, "qla1280_32bit_start_scsi: "
					"continuation packet data - "
					"scsi(%i:%i:%i)\n", SCSI_BUS_32(cmd),
					SCSI_TCN_32(cmd), SCSI_LUN_32(cmd));
				qla1280_dump_buffer(5, (char *)pkt,
						    REQUEST_ENTRY_SIZE);
			}
		} else {	/* No S/G data transfer */
			struct page *page = virt_to_page(cmd->request_buffer);
			unsigned long off = (unsigned long)cmd->request_buffer & ~PAGE_MASK;
			dma_handle = pci_map_page(ha->pdev, page, off,
						  cmd->request_bufflen,
						  scsi_to_pci_dma_dir(cmd->sc_data_direction));
			sp->saved_dma_handle = dma_handle;

			*dword_ptr++ = cpu_to_le32(pci_dma_lo32(dma_handle));
			*dword_ptr = cpu_to_le32(cmd->request_bufflen);
		}
	} else {	/* No data transfer at all */
		dword_ptr = (uint32_t *)(pkt + 1);
		*dword_ptr++ = 0;
		*dword_ptr = 0;
		dprintk(5, "qla1280_32bit_start_scsi: No data, command "
			"packet data - \n");
		qla1280_dump_buffer(5, (char *)pkt, REQUEST_ENTRY_SIZE);
	}
	dprintk(5, "qla1280_32bit_start_scsi: First IOCB block:\n");
	qla1280_dump_buffer(5, (char *)ha->request_ring_ptr,
			    REQUEST_ENTRY_SIZE);

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	/* Set chip new ring index. */
	dprintk(2, "qla1280_32bit_start_scsi: Wakeup RISC "
		"for pending command\n");
	sp->flags |= SRB_SENT;
	ha->actthreads++;
	WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);

out:
	if (status)
		dprintk(2, "qla1280_32bit_start_scsi: **** FAILED ****\n");

	LEAVE("qla1280_32bit_start_scsi");

	return status;
}

/*
 * qla1280_req_pkt
 *      Function is responsible for locking ring and
 *      getting a zeroed out request packet.
 *
 * Input:
 *      ha  = adapter block pointer.
 *
 * Returns:
 *      0 = failed to get slot.
 */
static request_t *
qla1280_req_pkt(struct scsi_qla_host *ha)
{
	struct device_reg *reg = ha->iobase;
	request_t *pkt = 0;
	int cnt;
	uint32_t timer;

	ENTER("qla1280_req_pkt");

	/*
	 * This can be called from interrupt context, damn it!!!
	 */
	/* Wait for 30 seconds for slot. */
	for (timer = 15000000; timer; timer--) {
		if (ha->req_q_cnt > 0) {
			/* Calculate number of free request entries. */
			cnt = RD_REG_WORD(&reg->mailbox4);
			if (ha->req_ring_index < cnt)
				ha->req_q_cnt = cnt - ha->req_ring_index;
			else
				ha->req_q_cnt =
					REQUEST_ENTRY_CNT - (ha->req_ring_index - cnt);
		}

		/* Found empty request ring slot? */
		if (ha->req_q_cnt > 0) {
			ha->req_q_cnt--;
			pkt = ha->request_ring_ptr;

			/* Zero out packet. */
			memset(pkt, 0, REQUEST_ENTRY_SIZE);

			/*
			 * How can this be right when we have a ring
			 * size of 512???
			 */
			/* Set system defined field. */
			pkt->sys_define = (uint8_t) ha->req_ring_index;

			/* Set entry count. */
			pkt->entry_count = 1;

			break;
		}

		udelay(2);	/* 10 */

		/* Check for pending interrupts. */
		qla1280_poll(ha);
	}

	if (!pkt)
		dprintk(2, "qla1280_req_pkt: **** FAILED ****\n");
	else
		dprintk(3, "qla1280_req_pkt: exiting normally\n");

	return pkt;
}

/*
 * qla1280_isp_cmd
 *      Function is responsible for modifying ISP input pointer.
 *      Releases ring lock.
 *
 * Input:
 *      ha  = adapter block pointer.
 */
static void
qla1280_isp_cmd(struct scsi_qla_host *ha)
{
	struct device_reg *reg = ha->iobase;

	ENTER("qla1280_isp_cmd");

	dprintk(5, "qla1280_isp_cmd: IOCB data:\n");
	qla1280_dump_buffer(5, (char *)ha->request_ring_ptr,
			    REQUEST_ENTRY_SIZE);

	/* Adjust ring index. */
	ha->req_ring_index++;
	if (ha->req_ring_index == REQUEST_ENTRY_CNT) {
		ha->req_ring_index = 0;
		ha->request_ring_ptr = ha->request_ring;
	} else
		ha->request_ring_ptr++;

	/* Set chip new ring index. */
	WRT_REG_WORD(&reg->mailbox4, ha->req_ring_index);

	LEAVE("qla1280_isp_cmd");
}

#if QL1280_LUN_SUPPORT
/*
 * qla1280_enable_lun
 *      Issue enable LUN entry IOCB.
 *
 * Input:
 *      ha  = adapter block pointer.
 *      bus = SCSI BUS number.
 *      lun  = LUN number.
 */
static void
qla1280_enable_lun(struct scsi_qla_host *ha, int bus, int lun)
{
	struct elun_entry *pkt;

	ENTER("qla1280_enable_lun");

	/* Get request packet. */
	/*
	  if (pkt = (struct elun_entry *)qla1280_req_pkt(ha))
	  {
	  pkt->entry_type = ENABLE_LUN_TYPE;
	  pkt->lun = cpu_to_le16(bus ? lun | BIT_15 : lun);
	  pkt->command_count = 32;
	  pkt->immed_notify_count = 1;
	  pkt->group_6_length = MAX_CMDSZ;
	  pkt->group_7_length = MAX_CMDSZ;
	  pkt->timeout = cpu_to_le16(0x30);

	  qla1280_isp_cmd(ha);
	  }
	*/
	pkt = (struct elun_entry *) 1;

	if (!pkt)
		dprintk(2, "qla1280_enable_lun: **** FAILED ****\n");
	else
		dprintk(3, "qla1280_enable_lun: exiting normally\n");
}
#endif


/****************************************************************************/
/*                        Interrupt Service Routine.                        */
/****************************************************************************/

/****************************************************************************
 *  qla1280_isr
 *      Calls I/O done on command completion.
 *
 * Input:
 *      ha           = adapter block pointer.
 *      done_q_first = done queue first pointer.
 *      done_q_last  = done queue last pointer.
 ****************************************************************************/
static void
qla1280_isr(struct scsi_qla_host *ha, struct srb ** done_q_first,
	    struct srb ** done_q_last)
{
	struct device_reg *reg = ha->iobase;
	struct response *pkt;
	struct srb *sp = 0;
	uint16_t mailbox[MAILBOX_REGISTER_COUNT];
	uint16_t *wptr;
	uint32_t index;
	u16 istatus;

	ENTER("qla1280_isr");

	istatus = RD_REG_WORD(&reg->istatus);
	if (!(istatus & (RISC_INT | PCI_INT)))
		return;

	/* Save mailbox register 5 */
	mailbox[5] = RD_REG_WORD(&reg->mailbox5);

	/* Check for mailbox interrupt. */

	mailbox[0] = RD_REG_WORD(&reg->semaphore);

	if (mailbox[0] & BIT_0) {
		/* Get mailbox data. */
		/* dprintk(1, "qla1280_isr: In Get mailbox data \n"); */

		wptr = &mailbox[0];
		*wptr++ = RD_REG_WORD(&reg->mailbox0);
		*wptr++ = RD_REG_WORD(&reg->mailbox1);
		*wptr = RD_REG_WORD(&reg->mailbox2);
		if (mailbox[0] != MBA_SCSI_COMPLETION) {
			wptr++;
			*wptr++ = RD_REG_WORD(&reg->mailbox3);
			*wptr++ = RD_REG_WORD(&reg->mailbox4);
			wptr++;
			*wptr++ = RD_REG_WORD(&reg->mailbox6);
			*wptr = RD_REG_WORD(&reg->mailbox7);
		}

		/* Release mailbox registers. */

		WRT_REG_WORD(&reg->semaphore, 0);
		WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);

		dprintk(5, "qla1280_isr: mailbox interrupt mailbox[0] = 0x%x",
			mailbox[0]);

		/* Handle asynchronous event */
		switch (mailbox[0]) {
		case MBA_SCSI_COMPLETION:	/* Response completion */
			dprintk(5, "qla1280_isr: mailbox SCSI response "
				"completion\n");

			if (ha->flags.online) {
				/* Get outstanding command index. */
				index = mailbox[2] << 16 | mailbox[1];

				/* Validate handle. */
				if (index < MAX_OUTSTANDING_COMMANDS)
					sp = ha->outstanding_cmds[index];
				else
					sp = 0;

				if (sp) {
					/* Free outstanding command slot. */
					ha->outstanding_cmds[index] = 0;

					/* Save ISP completion status */
					CMD_RESULT(sp->cmd) = 0;

					/* Place block on done queue */
					sp->s_next = NULL;
					sp->s_prev = *done_q_last;
					if (!*done_q_first)
						*done_q_first = sp;
					else
						(*done_q_last)->s_next = sp;
					*done_q_last = sp;
				} else {
					/*
					 * If we get here we have a real problem!
					 */
					printk(KERN_WARNING
					       "qla1280: ISP invalid handle");
				}
			}
			break;

		case MBA_BUS_RESET:	/* SCSI Bus Reset */
			ha->flags.reset_marker = 1;
			index = mailbox[6] & BIT_0;
			ha->bus_settings[index].reset_marker = 1;

			printk(KERN_DEBUG "qla1280_isr(): index %i "
			       "asynchronous BUS_RESET\n", index);
			break;

		case MBA_SYSTEM_ERR:	/* System Error */
			printk(KERN_WARNING
			       "qla1280: ISP System Error - mbx1=%xh, mbx2="
			       "%xh, mbx3=%xh\n", mailbox[1], mailbox[2],
			       mailbox[3]);
			break;

		case MBA_REQ_TRANSFER_ERR:	/* Request Transfer Error */
			printk(KERN_WARNING
			       "qla1280: ISP Request Transfer Error\n");
			break;

		case MBA_RSP_TRANSFER_ERR:	/* Response Transfer Error */
			printk(KERN_WARNING
			       "qla1280: ISP Response Transfer Error\n");
			break;

		case MBA_WAKEUP_THRES:	/* Request Queue Wake-up */
			dprintk(2, "qla1280_isr: asynchronous WAKEUP_THRES\n");
			break;

		case MBA_TIMEOUT_RESET:	/* Execution Timeout Reset */
			dprintk(2,
				"qla1280_isr: asynchronous TIMEOUT_RESET\n");
			break;

		case MBA_DEVICE_RESET:	/* Bus Device Reset */
			printk(KERN_INFO "qla1280_isr(): asynchronous "
			       "BUS_DEVICE_RESET\n");

			ha->flags.reset_marker = 1;
			index = mailbox[6] & BIT_0;
			ha->bus_settings[index].reset_marker = 1;
			break;

		case MBA_BUS_MODE_CHANGE:
			dprintk(2,
				"qla1280_isr: asynchronous BUS_MODE_CHANGE\n");
			break;

		default:
			/* dprintk(1, "qla1280_isr: default case of switch MB \n"); */
			if (mailbox[0] < MBA_ASYNC_EVENT) {
				wptr = &mailbox[0];
				memcpy((uint16_t *) ha->mailbox_out, wptr,
				       MAILBOX_REGISTER_COUNT *
				       sizeof(uint16_t));

				if(ha->mailbox_wait != NULL)
					complete(ha->mailbox_wait);
			}
			break;
		}
	} else {
		WRT_REG_WORD(&reg->host_cmd, HC_CLR_RISC_INT);
	}

	/*
	 * Response ring - waiting for the mbox_busy flag here seems
	 * unnecessary as the mailbox data has been copied to ha->mailbox_out
	 * by the time we actually get here!
	 */
	if (!(ha->flags.online
#if 0
	    && !ha->flags.mbox_busy
#endif
		)) {
		dprintk(2, "qla1280_isr: Response pointer Error\n");
		goto out;
	}

	if (mailbox[5] >= RESPONSE_ENTRY_CNT)
		goto out;

	while (ha->rsp_ring_index != mailbox[5]) {
		pkt = ha->response_ring_ptr;

		dprintk(5, "qla1280_isr: ha->rsp_ring_index = 0x%x, mailbox[5]"
			" = 0x%x\n", ha->rsp_ring_index, mailbox[5]);
		dprintk(5,"qla1280_isr: response packet data\n");
		qla1280_dump_buffer(5, (char *)pkt, RESPONSE_ENTRY_SIZE);

		if (pkt->entry_type == STATUS_TYPE) {
			if ((le16_to_cpu(pkt->scsi_status) & 0xff)
			    || pkt->comp_status || pkt->entry_status) {
				dprintk(2, "qla1280_isr: ha->rsp_ring_index = "
					"0x%x mailbox[5] = 0x%x, comp_status "
					"= 0x%x, scsi_status = 0x%x\n",
					ha->rsp_ring_index, mailbox[5],
					le16_to_cpu(pkt->comp_status),
					le16_to_cpu(pkt->scsi_status));
			}
		} else {
			dprintk(2, "qla1280_isr: ha->rsp_ring_index = "
				"0x%x, mailbox[5] = 0x%x\n",
				ha->rsp_ring_index, mailbox[5]);
			dprintk(2, "qla1280_isr: response packet data\n");
			qla1280_dump_buffer(2, (char *)pkt,
					    RESPONSE_ENTRY_SIZE);
		}

		if (pkt->entry_type == STATUS_TYPE || pkt->entry_status) {
			dprintk(2, "status: Cmd %p, handle %i\n",
				ha->outstanding_cmds[pkt->handle]->cmd,
				pkt->handle);
			if (pkt->entry_type == STATUS_TYPE)
				qla1280_status_entry(ha, pkt, done_q_first,
						     done_q_last);
			else
				qla1280_error_entry(ha, pkt, done_q_first,
						    done_q_last);

			/* Adjust ring index. */
			ha->rsp_ring_index++;
			if (ha->rsp_ring_index == RESPONSE_ENTRY_CNT) {
				ha->rsp_ring_index = 0;
				ha->response_ring_ptr =	ha->response_ring;
			} else
				ha->response_ring_ptr++;
			WRT_REG_WORD(&reg->mailbox5, ha->rsp_ring_index);
		}
	}
	
 out:
	LEAVE("qla1280_isr");
}

/*
 *  qla1280_rst_aen
 *      Processes asynchronous reset.
 *
 * Input:
 *      ha  = adapter block pointer.
 */
static void
qla1280_rst_aen(struct scsi_qla_host *ha)
{
	uint8_t bus;

	ENTER("qla1280_rst_aen");

	if (ha->flags.online && !ha->flags.reset_active &&
	    !ha->flags.abort_isp_active) {
		ha->flags.reset_active = 1;
		while (ha->flags.reset_marker) {
			/* Issue marker command. */
			ha->flags.reset_marker = 0;
			for (bus = 0; bus < ha->ports &&
				     !ha->flags.reset_marker; bus++) {
				if (ha->bus_settings[bus].reset_marker) {
					ha->bus_settings[bus].reset_marker = 0;
					qla1280_marker(ha, bus, 0, 0,
						       MK_SYNC_ALL);
				}
			}
		}
	}

	LEAVE("qla1280_rst_aen");
}


#if LINUX_VERSION_CODE < 0x020500
/*
 *
 */
void
qla1280_get_target_options(struct scsi_cmnd *cmd, struct scsi_qla_host *ha)
{
	unsigned char *result;
	struct nvram *n;
	int bus, target, lun;

	bus = SCSI_BUS_32(cmd);
	target = SCSI_TCN_32(cmd);
	lun = SCSI_LUN_32(cmd);

	/*
	 * Make sure to not touch anything if someone is using the
	 * sg interface.
	 */
	if (cmd->use_sg || (CMD_RESULT(cmd) >> 16) != DID_OK || lun)
		return;

	result = cmd->request_buffer;
	n = &ha->nvram;

	n->bus[bus].target[target].parameter.f.enable_wide = 0;
	n->bus[bus].target[target].parameter.f.enable_sync = 0;
	n->bus[bus].target[target].ppr_1x160.flags.enable_ppr = 0;

        if (result[7] & 0x60)
		n->bus[bus].target[target].parameter.f.enable_wide = 1;
        if (result[7] & 0x10)
		n->bus[bus].target[target].parameter.f.enable_sync = 1;
	if ((result[2] >= 3) && (result[4] + 5 > 56) &&
	    (result[56] & 0x4))
		n->bus[bus].target[target].ppr_1x160.flags.enable_ppr = 1;

	dprintk(2, "get_target_options(): wide %i, sync %i, ppr %i\n",
		n->bus[bus].target[target].parameter.f.enable_wide,
		n->bus[bus].target[target].parameter.f.enable_sync,
		n->bus[bus].target[target].ppr_1x160.flags.enable_ppr);
}
#endif

/*
 *  qla1280_status_entry
 *      Processes received ISP status entry.
 *
 * Input:
 *      ha           = adapter block pointer.
 *      pkt          = entry pointer.
 *      done_q_first = done queue first pointer.
 *      done_q_last  = done queue last pointer.
 */
static void
qla1280_status_entry(struct scsi_qla_host *ha, struct response *pkt,
		     struct srb **done_q_first, struct srb **done_q_last)
{
	unsigned int bus, target, lun;
	int sense_sz;
	struct srb *sp;
	Scsi_Cmnd *cmd;
	uint32_t handle = le32_to_cpu(pkt->handle);
	uint16_t scsi_status = le16_to_cpu(pkt->scsi_status);
	uint16_t comp_status = le16_to_cpu(pkt->comp_status);

	ENTER("qla1280_status_entry");

	/* Validate handle. */
	if (handle < MAX_OUTSTANDING_COMMANDS)
		sp = ha->outstanding_cmds[handle];
	else
		sp = NULL;

	if (!sp) {
		printk(KERN_WARNING "qla1280: Status Entry invalid handle\n");
		goto out;
	}

	/* Free outstanding command slot. */
	ha->outstanding_cmds[handle] = 0;

	cmd = sp->cmd;

	/* Generate LU queue on cntrl, target, LUN */
	bus = SCSI_BUS_32(cmd);
	target = SCSI_TCN_32(cmd);
	lun = SCSI_LUN_32(cmd);

	if (comp_status || scsi_status) {
		dprintk(3, "scsi: comp_status = 0x%x, scsi_status = "
			"0x%x, handle = 0x%x\n", comp_status,
			scsi_status, handle);
	}

	/* Target busy */
	if (scsi_status & SS_BUSY_CONDITION &&
	    scsi_status != SS_RESERVE_CONFLICT) {
		CMD_RESULT(cmd) =
			DID_BUS_BUSY << 16 | (scsi_status & 0xff);
	} else {

		/* Save ISP completion status */
		CMD_RESULT(cmd) = qla1280_return_status(pkt, cmd);

		if (scsi_status & SS_CHECK_CONDITION) {
			if (comp_status != CS_ARS_FAILED) {
				uint16_t req_sense_length =
					le16_to_cpu(pkt->req_sense_length);
				if (req_sense_length < CMD_SNSLEN(cmd))
					sense_sz = req_sense_length;
				else
					/*
					 * Scsi_Cmnd->sense_buffer is
					 * 64 bytes, why only copy 63?
					 * This looks wrong! /Jes
					 */
					sense_sz = CMD_SNSLEN(cmd) - 1;

				memcpy(cmd->sense_buffer,
				       &pkt->req_sense_data, sense_sz);
			} else
				sense_sz = 0;
			memset(cmd->sense_buffer + sense_sz, 0,
			       sizeof(cmd->sense_buffer) - sense_sz);

			dprintk(2, "qla1280_status_entry: Check "
				"condition Sense data, b %i, t %i, "
				"l %i\n", bus, target, lun);
			if (sense_sz)
				qla1280_dump_buffer(2,
						    (char *)cmd->sense_buffer,
						    sense_sz);
		}
	}
	/* Place command on done queue. */
	qla1280_done_q_put(sp, done_q_first, done_q_last);

 out:
	LEAVE("qla1280_status_entry");
}

/*
 *  qla1280_error_entry
 *      Processes error entry.
 *
 * Input:
 *      ha           = adapter block pointer.
 *      pkt          = entry pointer.
 *      done_q_first = done queue first pointer.
 *      done_q_last  = done queue last pointer.
 */
static void
qla1280_error_entry(struct scsi_qla_host *ha, struct response * pkt,
		    struct srb ** done_q_first, struct srb ** done_q_last)
{
	struct srb *sp;
	uint32_t handle = le32_to_cpu(pkt->handle);

	ENTER("qla1280_error_entry");

	if (pkt->entry_status & BIT_3)
		dprintk(2, "qla1280_error_entry: BAD PAYLOAD flag error\n");
	else if (pkt->entry_status & BIT_2)
		dprintk(2, "qla1280_error_entry: BAD HEADER flag error\n");
	else if (pkt->entry_status & BIT_1)
		dprintk(2, "qla1280_error_entry: FULL flag error\n");
	else
		dprintk(2, "qla1280_error_entry: UNKNOWN flag error\n");

	/* Validate handle. */
	if (handle < MAX_OUTSTANDING_COMMANDS)
		sp = ha->outstanding_cmds[handle];
	else
		sp = 0;

	if (sp) {
		/* Free outstanding command slot. */
		ha->outstanding_cmds[handle] = 0;

		/* Bad payload or header */
		if (pkt->entry_status & (BIT_3 + BIT_2)) {
			/* Bad payload or header, set error status. */
			/* CMD_RESULT(sp->cmd) = CS_BAD_PAYLOAD; */
			CMD_RESULT(sp->cmd) = DID_ERROR << 16;
		} else if (pkt->entry_status & BIT_1) {	/* FULL flag */
			CMD_RESULT(sp->cmd) = DID_BUS_BUSY << 16;
		} else {
			/* Set error status. */
			CMD_RESULT(sp->cmd) = DID_ERROR << 16;
		}
		/* Place command on done queue. */
		qla1280_done_q_put(sp, done_q_first, done_q_last);
	}
#ifdef QLA_64BIT_PTR
	else if (pkt->entry_type == COMMAND_A64_TYPE) {
		printk(KERN_WARNING "!qla1280: Error Entry invalid handle");
	}
#endif

	LEAVE("qla1280_error_entry");
}

/*
 *  qla1280_abort_isp
 *      Resets ISP and aborts all outstanding commands.
 *
 * Input:
 *      ha           = adapter block pointer.
 *
 * Returns:
 *      0 = success
 */
static int
qla1280_abort_isp(struct scsi_qla_host *ha)
{
	struct srb *sp;
	int status = 0;
	int cnt;
	int bus;

	ENTER("qla1280_abort_isp");

	if (!ha->flags.abort_isp_active && ha->flags.online) {
		struct device_reg *reg = ha->iobase;
		ha->flags.abort_isp_active = 1;

		/* Disable ISP interrupts. */
		qla1280_disable_intrs(ha);
		WRT_REG_WORD(&reg->host_cmd, HC_PAUSE_RISC);
		RD_REG_WORD(&reg->id_l);

		printk(KERN_INFO "scsi(%li): dequeuing outstanding commands\n",
		       ha->host_no);
		/* Dequeue all commands in outstanding command list. */
		for (cnt = 0; cnt < MAX_OUTSTANDING_COMMANDS; cnt++) {
			Scsi_Cmnd *cmd;
			sp = ha->outstanding_cmds[cnt];
			if (sp) {

				cmd = sp->cmd;
				CMD_RESULT(cmd) = DID_RESET << 16;

				sp->cmd = NULL;
				ha->outstanding_cmds[cnt] = NULL;

				(*cmd->scsi_done)(cmd);

				sp->flags = 0;
			}
		}

		/* If firmware needs to be loaded */
		if (qla1280_isp_firmware (ha)) {
			if (!(status = qla1280_chip_diag(ha)))
				status = qla1280_setup_chip(ha);
		}

		if (!status) {
			/* Setup adapter based on NVRAM parameters. */
			qla1280_nvram_config (ha);

			if (!(status = qla1280_init_rings(ha))) {
				/* Issue SCSI reset. */
				for (bus = 0; bus < ha->ports; bus++) {
					qla1280_bus_reset(ha, bus);
				}
				/*
				 * qla1280_bus_reset() will do the marker
				 * dance - no reason to repeat here!
				 */
#if  0
				/* Issue marker command. */
				ha->flags.reset_marker = 0;
				for (bus = 0; bus < ha->ports; bus++) {
					ha->bus_settings[bus].
						reset_marker = 0;
					qla1280_marker(ha, bus, 0, 0,
						       MK_SYNC_ALL);
				}
#endif
				ha->flags.abort_isp_active = 0;
			}
		}
	}

	if (status) {
		printk(KERN_WARNING
		       "qla1280: ISP error recovery failed, board disabled");
		qla1280_reset_adapter(ha);
		dprintk(2, "qla1280_abort_isp: **** FAILED ****\n");
	}

	LEAVE("qla1280_abort_isp");
	return status;
}


/*
 * qla1280_debounce_register
 *      Debounce register.
 *
 * Input:
 *      port = register address.
 *
 * Returns:
 *      register value.
 */
static u16
qla1280_debounce_register(volatile u16 * addr)
{
	volatile u16 ret;
	volatile u16 ret2;

	ret = RD_REG_WORD(addr);
	ret2 = RD_REG_WORD(addr);

	if (ret == ret2)
		return ret;

	do {
		cpu_relax();
		ret = RD_REG_WORD(addr);
		ret2 = RD_REG_WORD(addr);
	} while (ret != ret2);

	return ret;
}


static Scsi_Host_Template driver_template = QLA1280_LINUX_TEMPLATE;
#include "scsi_module.c"


/************************************************************************
 * qla1280_check_for_dead_scsi_bus                                      *
 *                                                                      *
 *    This routine checks for a dead SCSI bus                           *
 ************************************************************************/
#define SET_SXP_BANK            0x0100
#define SCSI_PHASE_INVALID      0x87FF
static int
qla1280_check_for_dead_scsi_bus(struct scsi_qla_host *ha, unsigned int bus)
{
	uint16_t config_reg, scsi_control;
	struct device_reg *reg = ha->iobase;

	if (ha->bus_settings[bus].scsi_bus_dead) {
		WRT_REG_WORD(&reg->host_cmd, HC_PAUSE_RISC);
		config_reg = RD_REG_WORD(&reg->cfg_1);
		WRT_REG_WORD(&reg->cfg_1, SET_SXP_BANK);
		scsi_control = RD_REG_WORD(&reg->scsiControlPins);
		WRT_REG_WORD(&reg->cfg_1, config_reg);
		WRT_REG_WORD(&reg->host_cmd, HC_RELEASE_RISC);

		if (scsi_control == SCSI_PHASE_INVALID) {
			ha->bus_settings[bus].scsi_bus_dead = 1;
#if 0
			CMD_RESULT(cp) = DID_NO_CONNECT << 16;
			CMD_HANDLE(cp) = INVALID_HANDLE;
			/* ha->actthreads--; */

			(*(cp)->scsi_done)(cp);
#endif
			return 1;	/* bus is dead */
		} else {
			ha->bus_settings[bus].scsi_bus_dead = 0;
			ha->bus_settings[bus].failed_reset_count = 0;
		}
	}
	return 0;		/* bus is not dead */
}

static void
qla12160_get_target_parameters(struct scsi_qla_host *ha, Scsi_Device *device)
{
	uint16_t mb[MAILBOX_REGISTER_COUNT];
	int bus, target, lun;

	bus = device->channel;
	target = device->id;
	lun = device->lun;


	mb[0] = MBC_GET_TARGET_PARAMETERS;
	mb[1] = (uint16_t) (bus ? target | BIT_7 : target);
	mb[1] <<= 8;
	qla1280_mailbox_command(ha, BIT_6 | BIT_3 | BIT_2 | BIT_1 | BIT_0,
				&mb[0]);

	printk(KERN_INFO "scsi(%li:%d:%d:%d):", ha->host_no, bus, target, lun);

	if (mb[3] != 0) {
		printk(" Sync: period %d, offset %d",
		       (mb[3] & 0xff), (mb[3] >> 8));
		if (mb[2] & BIT_13)
			printk(", Wide");
		if ((mb[2] & BIT_5) && ((mb[6] >> 8) & 0xff) >= 2)
			printk(", DT");
	} else
		printk(" Async");

	if (device->simple_tags)
		printk(", Tagged queuing: depth %d", device->queue_depth);
	printk("\n");
}


#if DEBUG_QLA1280
static void
__qla1280_dump_buffer(char *b, int size)
{
	int cnt;
	u8 c;

	printk(KERN_DEBUG " 0   1   2   3   4   5   6   7   8   9   Ah  "
	       "Bh  Ch  Dh  Eh  Fh\n");
	printk(KERN_DEBUG "---------------------------------------------"
	       "------------------\n");

	for (cnt = 0; cnt < size;) {
		c = *b++;

		printk("0x%02x", c);
		cnt++;
		if (!(cnt % 16))
			printk("\n");
		else
			printk(" ");
	}
	if (cnt % 16)
		printk("\n");
}

/**************************************************************************
 *   ql1280_print_scsi_cmd
 *
 **************************************************************************/
static void
__qla1280_print_scsi_cmd(Scsi_Cmnd * cmd)
{
	struct scsi_qla_host *ha;
	struct Scsi_Host *host = CMD_HOST(cmd);
	struct srb *sp;
	/* struct scatterlist *sg; */

	int i;
	ha = (struct scsi_qla_host *)host->hostdata;

	sp = (struct srb *)CMD_SP(cmd);
	printk("SCSI Command @= 0x%p, Handle=0x%p\n", cmd, CMD_HANDLE(cmd));
	printk("  chan=%d, target = 0x%02x, lun = 0x%02x, cmd_len = 0x%02x\n",
	       SCSI_BUS_32(cmd), SCSI_TCN_32(cmd), SCSI_LUN_32(cmd),
	       CMD_CDBLEN(cmd));
	printk(" CDB = ");
	for (i = 0; i < cmd->cmd_len; i++) {
		printk("0x%02x ", cmd->cmnd[i]);
	}
	printk("  seg_cnt =%d\n", cmd->use_sg);
	printk("  request buffer=0x%p, request buffer len=0x%x\n",
	       cmd->request_buffer, cmd->request_bufflen);
	/* if (cmd->use_sg)
	   {
	   sg = (struct scatterlist *) cmd->request_buffer;
	   printk("  SG buffer: \n");
	   qla1280_dump_buffer(1, (char *)sg, (cmd->use_sg*sizeof(struct scatterlist)));
	   } */
	printk("  tag=%d, flags=0x%x, transfersize=0x%x \n",
	       cmd->tag, cmd->flags, cmd->transfersize);
	printk("  Pid=%li, SP=0x%p\n", cmd->pid, CMD_SP(cmd));
	printk(" underflow size = 0x%x, direction=0x%x\n",
	       cmd->underflow, sp->dir);
}

/**************************************************************************
 *   ql1280_dump_device
 *
 **************************************************************************/
void
ql1280_dump_device(struct scsi_qla_host *ha)
{

	Scsi_Cmnd *cp;
	struct srb *sp;
	int i;

	printk(KERN_DEBUG "Outstanding Commands on controller:\n");

	for (i = 0; i < MAX_OUTSTANDING_COMMANDS; i++) {
		if ((sp = ha->outstanding_cmds[i]) == NULL)
			continue;
		if ((cp = sp->cmd) == NULL)
			continue;
		qla1280_print_scsi_cmd(1, cp);
	}
}
#endif


enum tokens {
	TOKEN_NVRAM,
	TOKEN_SYNC,
	TOKEN_WIDE,
	TOKEN_PPR,
	TOKEN_VERBOSE,
	TOKEN_DEBUG,
};

struct setup_tokens {
	char *token;
	int val;
};

static struct setup_tokens setup_token[] __initdata = 
{
	{ "nvram", TOKEN_NVRAM },
	{ "sync", TOKEN_SYNC },
	{ "wide", TOKEN_WIDE },
	{ "ppr", TOKEN_PPR },
	{ "verbose", TOKEN_VERBOSE },
	{ "debug", TOKEN_DEBUG },
};


/**************************************************************************
 *   qla1280_setup
 *
 *   Handle boot parameters. This really needs to be changed so one
 *   can specify per adapter parameters.
 **************************************************************************/
int __init
qla1280_setup(char *s)
{
	char *cp, *ptr;
	unsigned long val;
	int toke;

	cp = s;

	while (cp && (ptr = strchr(cp, ':'))) {
		ptr++;
		if (!strcmp(ptr, "yes")) {
			val = 0x10000;
			ptr += 3;
		} else if (!strcmp(ptr, "no")) {
 			val = 0;
			ptr += 2;
		} else
			val = simple_strtoul(ptr, &ptr, 0);

		switch ((toke = qla1280_get_token(cp))) {
		case TOKEN_NVRAM:
			if (!val)
				driver_setup.no_nvram = 1;
			break;
		case TOKEN_SYNC:
			if (!val)
				driver_setup.no_sync = 1;
			else if (val != 0x10000)
				driver_setup.sync_mask = val;
			break;
		case TOKEN_WIDE:
			if (!val)
				driver_setup.no_wide = 1;
			else if (val != 0x10000)
				driver_setup.wide_mask = val;
			break;
		case TOKEN_PPR:
			if (!val)
				driver_setup.no_ppr = 1;
			else if (val != 0x10000)
				driver_setup.ppr_mask = val;
			break;
		case TOKEN_VERBOSE:
			qla1280_verbose = val;
			break;
		default:
			printk(KERN_INFO "qla1280: unknown boot option %s\n",
			       cp);
		}

		cp = strchr(ptr, ';');
		if (cp)
			cp++;
		else {
			break;
		}
	}
	return 1;
}


static int
qla1280_get_token(char *str)
{
	char *sep;
	long ret = -1;
	int i, len;

	len = sizeof(setup_token)/sizeof(struct setup_tokens);

	sep = strchr(str, ':');

	if (sep) {
		for (i = 0; i < len; i++){

			if (!strncmp(setup_token[i].token, str, (sep - str))) {
				ret =  setup_token[i].val;
				break;
			}
		}
	}

	return ret;
}


/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * End:
 */
