/*
 *  scsi_scan.c Copyright (C) 2000 Eric Youngdale
 *
 *  Bus scan logic.
 *
 *  This used to live in scsi.c, but that file was just a laundry basket
 *  full of misc stuff.  This got separated out in order to make things
 *  clearer.
 */

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>

#include <linux/blk.h>

#include "scsi.h"
#include "hosts.h"
#include "constants.h"

#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

/* The following devices are known not to tolerate a lun != 0 scan for
 * one reason or another.  Some will respond to all luns, others will
 * lock up.
 */

#define BLIST_NOLUN     	0x001
#define BLIST_FORCELUN  	0x002
#define BLIST_BORKEN    	0x004
#define BLIST_KEY       	0x008
#define BLIST_SINGLELUN 	0x010
#define BLIST_NOTQ		0x020
#define BLIST_SPARSELUN 	0x040
#define BLIST_MAX5LUN		0x080
#define BLIST_ISDISK    	0x100
#define BLIST_ISROM     	0x200
#define BLIST_GHOST     	0x400   

static void print_inquiry(unsigned char *data);
static int scan_scsis_single(int channel, int dev, int lun, int *max_scsi_dev,
		int *sparse_lun, Scsi_Device ** SDpnt, Scsi_Cmnd * SCpnt,
			     struct Scsi_Host *shpnt, char *scsi_result);

struct dev_info {
	const char *vendor;
	const char *model;
	const char *revision;	/* Latest revision known to be bad.  Not used yet */
	unsigned flags;
};

/*
 * This is what was previously known as the blacklist.  The concept
 * has been expanded so that we can specify other types of things we
 * need to be aware of.
 */
static struct dev_info device_list[] =
{
	{"Aashima", "IMAGERY 2400SP", "1.03", BLIST_NOLUN},	/* Locks up if polled for lun != 0 */
	{"CHINON", "CD-ROM CDS-431", "H42", BLIST_NOLUN},	/* Locks up if polled for lun != 0 */
	{"CHINON", "CD-ROM CDS-535", "Q14", BLIST_NOLUN},	/* Locks up if polled for lun != 0 */
	{"DENON", "DRD-25X", "V", BLIST_NOLUN},			/* Locks up if probed for lun != 0 */
	{"HITACHI", "DK312C", "CM81", BLIST_NOLUN},		/* Responds to all lun - dtg */
	{"HITACHI", "DK314C", "CR21", BLIST_NOLUN},		/* responds to all lun */
	{"IMS", "CDD521/10", "2.06", BLIST_NOLUN},		/* Locks-up when LUN>0 polled. */
	{"MAXTOR", "XT-3280", "PR02", BLIST_NOLUN},		/* Locks-up when LUN>0 polled. */
	{"MAXTOR", "XT-4380S", "B3C", BLIST_NOLUN},		/* Locks-up when LUN>0 polled. */
	{"MAXTOR", "MXT-1240S", "I1.2", BLIST_NOLUN},		/* Locks up when LUN>0 polled */
	{"MAXTOR", "XT-4170S", "B5A", BLIST_NOLUN},		/* Locks-up sometimes when LUN>0 polled. */
	{"MAXTOR", "XT-8760S", "B7B", BLIST_NOLUN},		/* guess what? */
	{"MEDIAVIS", "RENO CD-ROMX2A", "2.03", BLIST_NOLUN},	/*Responds to all lun */
	{"MICROP", "4110", "*", BLIST_NOTQ},			/* Buggy Tagged Queuing */
	{"NEC", "CD-ROM DRIVE:841", "1.0", BLIST_NOLUN},	/* Locks-up when LUN>0 polled. */
	{"PHILIPS", "PCA80SC", "V4-2", BLIST_NOLUN},		/* Responds to all lun */
	{"RODIME", "RO3000S", "2.33", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"SANYO", "CRD-250S", "1.20", BLIST_NOLUN},		/* causes failed REQUEST SENSE on lun 1
								 * for aha152x controller, which causes
								 * SCSI code to reset bus.*/
	{"SEAGATE", "ST157N", "\004|j", BLIST_NOLUN},		/* causes failed REQUEST SENSE on lun 1
								 * for aha152x controller, which causes
								 * SCSI code to reset bus.*/
	{"SEAGATE", "ST296", "921", BLIST_NOLUN},		/* Responds to all lun */
	{"SEAGATE", "ST1581", "6538", BLIST_NOLUN},		/* Responds to all lun */
	{"SONY", "CD-ROM CDU-541", "4.3d", BLIST_NOLUN},	
	{"SONY", "CD-ROM CDU-55S", "1.0i", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-561", "1.7x", BLIST_NOLUN},
	{"SONY", "CD-ROM CDU-8012", "*", BLIST_NOLUN},
	{"TANDBERG", "TDC 3600", "U07", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"TEAC", "CD-R55S", "1.0H", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"TEAC", "CD-ROM", "1.06", BLIST_NOLUN},		/* causes failed REQUEST SENSE on lun 1
								 * for seagate controller, which causes
								 * SCSI code to reset bus.*/
	{"TEAC", "MT-2ST/45S2-27", "RV M", BLIST_NOLUN},	/* Responds to all lun */
	{"TEXEL", "CD-ROM", "1.06", BLIST_NOLUN},		/* causes failed REQUEST SENSE on lun 1
								 * for seagate controller, which causes
								 * SCSI code to reset bus.*/
	{"QUANTUM", "LPS525S", "3110", BLIST_NOLUN},		/* Locks sometimes if polled for lun != 0 */
	{"QUANTUM", "PD1225S", "3110", BLIST_NOLUN},		/* Locks sometimes if polled for lun != 0 */
	{"QUANTUM", "FIREBALL ST4.3S", "0F0C", BLIST_NOLUN},	/* Locks up when polled for lun != 0 */
	{"MEDIAVIS", "CDR-H93MV", "1.31", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"SANKYO", "CP525", "6.64", BLIST_NOLUN},		/* causes failed REQ SENSE, extra reset */
	{"HP", "C1750A", "3226", BLIST_NOLUN},			/* scanjet iic */
	{"HP", "C1790A", "", BLIST_NOLUN},			/* scanjet iip */
	{"HP", "C2500A", "", BLIST_NOLUN},			/* scanjet iicx */
	{"YAMAHA", "CDR100", "1.00", BLIST_NOLUN},		/* Locks up if polled for lun != 0 */
	{"YAMAHA", "CDR102", "1.00", BLIST_NOLUN},		/* Locks up if polled for lun != 0  
								 * extra reset */
	{"RELISYS", "Scorpio", "*", BLIST_NOLUN},		/* responds to all LUN */
	{"MICROTEK", "ScanMaker II", "5.61", BLIST_NOLUN},	/* responds to all LUN */

/*
 * Other types of devices that have special flags.
 */
	{"SONY", "CD-ROM CDU-8001", "*", BLIST_BORKEN},
	{"TEXEL", "CD-ROM", "1.06", BLIST_BORKEN},
	{"IOMEGA", "Io20S         *F", "*", BLIST_KEY},
	{"INSITE", "Floptical   F*8I", "*", BLIST_KEY},
	{"INSITE", "I325VM", "*", BLIST_KEY},
	{"NRC", "MBR-7", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NRC", "MBR-7.4", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"REGAL", "CDC-4X", "*", BLIST_MAX5LUN | BLIST_SINGLELUN},
	{"NAKAMICH", "MJ-4.8S", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"NAKAMICH", "MJ-5.16S", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
    {"PIONEER", "CD-ROM DRM-600", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
   {"PIONEER", "CD-ROM DRM-602X", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
   {"PIONEER", "CD-ROM DRM-604X", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"EMULEX", "MD21/S2     ESDI", "*", BLIST_SINGLELUN},
	{"CANON", "IPUBJD", "*", BLIST_SPARSELUN},
	{"nCipher", "Fastness Crypto", "*", BLIST_FORCELUN},
	{"NEC", "PD-1 ODX654P", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"MATSHITA", "PD-1", "*", BLIST_FORCELUN | BLIST_SINGLELUN},
	{"iomega", "jaz 1GB", "J.86", BLIST_NOTQ | BLIST_NOLUN},
 	{"CREATIVE","DVD-RAM RAM","*", BLIST_GHOST},
 	{"MATSHITA","PD-2 LF-D100","*", BLIST_GHOST},
 	{"HITACHI", "GF-1050","*", BLIST_GHOST},  /* Hitachi SCSI DVD-RAM */
 	{"TOSHIBA","CDROM","*", BLIST_ISROM},
	{"TOSHIBA","DVD-RAM SD-W1101","*", BLIST_GHOST},
	{"TOSHIBA","DVD-RAM SD-W1111","*", BLIST_GHOST},

	/*
	 * Must be at end of list...
	 */
	{NULL, NULL, NULL}
};

#ifdef CONFIG_SCSI_MULTI_LUN
static int max_scsi_luns = 8;
#else
static int max_scsi_luns = 1;
#endif

#ifdef MODULE

MODULE_PARM(max_scsi_luns, "i");
MODULE_PARM_DESC(max_scsi_luns, "last scsi LUN (should be between 1 and 8)");

#else

static int __init scsi_luns_setup(char *str)
{
	int tmp;

	if (get_option(&str, &tmp) == 1) {
		max_scsi_luns = tmp;
		return 1;
	} else {
		printk("scsi_luns_setup : usage max_scsi_luns=n "
		       "(n should be between 1 and 8)\n");
		return 0;
	}
}

__setup("max_scsi_luns=", scsi_luns_setup);

#endif

static void print_inquiry(unsigned char *data)
{
	int i;

	printk("  Vendor: ");
	for (i = 8; i < 16; i++) {
		if (data[i] >= 0x20 && i < data[4] + 5)
			printk("%c", data[i]);
		else
			printk(" ");
	}

	printk("  Model: ");
	for (i = 16; i < 32; i++) {
		if (data[i] >= 0x20 && i < data[4] + 5)
			printk("%c", data[i]);
		else
			printk(" ");
	}

	printk("  Rev: ");
	for (i = 32; i < 36; i++) {
		if (data[i] >= 0x20 && i < data[4] + 5)
			printk("%c", data[i]);
		else
			printk(" ");
	}

	printk("\n");

	i = data[0] & 0x1f;

	printk("  Type:   %s ",
	       i < MAX_SCSI_DEVICE_CODE ? scsi_device_types[i] : "Unknown          ");
	printk("                 ANSI SCSI revision: %02x", data[2] & 0x07);
	if ((data[2] & 0x07) == 1 && (data[3] & 0x0f) == 1)
		printk(" CCS\n");
	else
		printk("\n");
}

static int get_device_flags(unsigned char *response_data)
{
	int i = 0;
	unsigned char *pnt;
	for (i = 0; 1; i++) {
		if (device_list[i].vendor == NULL)
			return 0;
		pnt = &response_data[8];
		while (*pnt && *pnt == ' ')
			pnt++;
		if (memcmp(device_list[i].vendor, pnt,
			   strlen(device_list[i].vendor)))
			continue;
		pnt = &response_data[16];
		while (*pnt && *pnt == ' ')
			pnt++;
		if (memcmp(device_list[i].model, pnt,
			   strlen(device_list[i].model)))
			continue;
		return device_list[i].flags;
	}
	return 0;
}

/*
 *  Detecting SCSI devices :
 *  We scan all present host adapter's busses,  from ID 0 to ID (max_id).
 *  We use the INQUIRY command, determine device type, and pass the ID /
 *  lun address of all sequential devices to the tape driver, all random
 *  devices to the disk driver.
 */
void scan_scsis(struct Scsi_Host *shpnt,
		       unchar hardcoded,
		       unchar hchannel,
		       unchar hid,
		       unchar hlun)
{
	int channel;
	int dev;
	int lun;
	int max_dev_lun;
	Scsi_Cmnd *SCpnt;
	unsigned char *scsi_result;
	unsigned char scsi_result0[256];
	Scsi_Device *SDpnt;
	Scsi_Device *SDtail;
	int sparse_lun;

	scsi_result = NULL;
	SCpnt = (Scsi_Cmnd *) kmalloc(sizeof(Scsi_Cmnd),
					       GFP_ATOMIC | GFP_DMA);
	if (SCpnt) {
                memset(SCpnt, 0, sizeof(Scsi_Cmnd));
		SDpnt = (Scsi_Device *) kmalloc(sizeof(Scsi_Device),
							 GFP_ATOMIC);
		if (SDpnt) {
                        memset(SDpnt, 0, sizeof(Scsi_Device));
			/*
			 * Register the queue for the device.  All I/O requests will come
			 * in through here.  We also need to register a pointer to
			 * ourselves, since the queue handler won't know what device
			 * the queue actually represents.   We could look it up, but it
			 * is pointless work.
			 */
			blk_init_queue(&SDpnt->request_queue, scsi_get_request_handler(SDpnt, shpnt));
			blk_queue_headactive(&SDpnt->request_queue, 0);
			SDpnt->request_queue.queuedata = (void *) SDpnt;
			/* Make sure we have something that is valid for DMA purposes */
			scsi_result = ((!shpnt->unchecked_isa_dma)
				       ? &scsi_result0[0] : kmalloc(512, GFP_DMA));
		}
	}
	if (scsi_result == NULL) {
		printk("Unable to obtain scsi_result buffer\n");
		goto leave;
	}
	/*
	 * We must chain ourself in the host_queue, so commands can time out 
	 */
	SCpnt->next = NULL;
	SDpnt->device_queue = SCpnt;
	SDpnt->host = shpnt;
	SDpnt->online = TRUE;

	initialize_merge_fn(SDpnt);

        /*
         * Initialize the object that we will use to wait for command blocks.
         */
	init_waitqueue_head(&SDpnt->scpnt_wait);

	/*
	 * Next, hook the device to the host in question.
	 */
	SDpnt->prev = NULL;
	SDpnt->next = NULL;
	if (shpnt->host_queue != NULL) {
		SDtail = shpnt->host_queue;
		while (SDtail->next != NULL)
			SDtail = SDtail->next;

		SDtail->next = SDpnt;
		SDpnt->prev = SDtail;
	} else {
		shpnt->host_queue = SDpnt;
	}

	/*
	 * We need to increment the counter for this one device so we can track when
	 * things are quiet.
	 */
	atomic_inc(&shpnt->host_active);
	atomic_inc(&SDpnt->device_active);

	if (hardcoded == 1) {
		Scsi_Device *oldSDpnt = SDpnt;
		struct Scsi_Device_Template *sdtpnt;
		channel = hchannel;
		if (channel > shpnt->max_channel)
			goto leave;
		dev = hid;
		if (dev >= shpnt->max_id)
			goto leave;
		lun = hlun;
		if (lun >= shpnt->max_lun)
			goto leave;
		scan_scsis_single(channel, dev, lun, &max_dev_lun, &sparse_lun,
				  &SDpnt, SCpnt, shpnt, scsi_result);
		if (SDpnt != oldSDpnt) {

			/* it could happen the blockdevice hasn't yet been inited */
			for (sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next)
				if (sdtpnt->init && sdtpnt->dev_noticed)
					(*sdtpnt->init) ();

			for (sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next) {
				if (sdtpnt->attach) {
					(*sdtpnt->attach) (oldSDpnt);
					if (oldSDpnt->attached) {
						scsi_build_commandblocks(oldSDpnt);
						if (0 == oldSDpnt->has_cmdblocks) {
							printk("scan_scsis: DANGER, no command blocks\n");
							/* What to do now ?? */
						}
					}
				}
			}
			scsi_resize_dma_pool();

			for (sdtpnt = scsi_devicelist; sdtpnt; sdtpnt = sdtpnt->next) {
				if (sdtpnt->finish && sdtpnt->nr_dev) {
					(*sdtpnt->finish) ();
				}
			}
		}
	} else {
		/* Actual LUN. PC ordering is 0->n IBM/spec ordering is n->0 */
		int order_dev;

		for (channel = 0; channel <= shpnt->max_channel; channel++) {
			for (dev = 0; dev < shpnt->max_id; ++dev) {
				if (shpnt->reverse_ordering)
					/* Shift to scanning 15,14,13... or 7,6,5,4, */
					order_dev = shpnt->max_id - dev - 1;
				else
					order_dev = dev;

				if (shpnt->this_id != order_dev) {

					/*
					 * We need the for so our continue, etc. work fine. We put this in
					 * a variable so that we can override it during the scan if we
					 * detect a device *KNOWN* to have multiple logical units.
					 */
					max_dev_lun = (max_scsi_luns < shpnt->max_lun ?
					 max_scsi_luns : shpnt->max_lun);
					sparse_lun = 0;
					for (lun = 0; lun < max_dev_lun; ++lun) {
						if (!scan_scsis_single(channel, order_dev, lun, &max_dev_lun,
								       &sparse_lun, &SDpnt, SCpnt, shpnt,
							     scsi_result)
						    && !sparse_lun)
							break;	/* break means don't probe further for luns!=0 */
					}	/* for lun ends */
				}	/* if this_id != id ends */
			}	/* for dev ends */
		}		/* for channel ends */
	}			/* if/else hardcoded */

	/*
	 * We need to decrement the counter for this one device
	 * so we know when everything is quiet.
	 */
	atomic_dec(&shpnt->host_active);
	atomic_dec(&SDpnt->device_active);

      leave:

	{			/* Unchain SCpnt from host_queue */
		Scsi_Device *prev, *next;
		Scsi_Device *dqptr;

		for (dqptr = shpnt->host_queue; dqptr != SDpnt; dqptr = dqptr->next)
			continue;
		if (dqptr) {
			prev = dqptr->prev;
			next = dqptr->next;
			if (prev)
				prev->next = next;
			else
				shpnt->host_queue = next;
			if (next)
				next->prev = prev;
		}
	}

	/* Last device block does not exist.  Free memory. */
	if (SDpnt != NULL)
		kfree((char *) SDpnt);

	if (SCpnt != NULL)
		kfree((char *) SCpnt);

	/* If we allocated a buffer so we could do DMA, free it now */
	if (scsi_result != &scsi_result0[0] && scsi_result != NULL) {
		kfree(scsi_result);
	} {
		Scsi_Device *sdev;
		Scsi_Cmnd *scmd;

		SCSI_LOG_SCAN_BUS(4, printk("Host status for host %p:\n", shpnt));
		for (sdev = shpnt->host_queue; sdev; sdev = sdev->next) {
			SCSI_LOG_SCAN_BUS(4, printk("Device %d %p: ", sdev->id, sdev));
			for (scmd = sdev->device_queue; scmd; scmd = scmd->next) {
				SCSI_LOG_SCAN_BUS(4, printk("%p ", scmd));
			}
			SCSI_LOG_SCAN_BUS(4, printk("\n"));
		}
	}
}

/*
 * The worker for scan_scsis.
 * Returning 0 means Please don't ask further for lun!=0, 1 means OK go on.
 * Global variables used : scsi_devices(linked list)
 */
int scan_scsis_single(int channel, int dev, int lun, int *max_dev_lun,
	       int *sparse_lun, Scsi_Device ** SDpnt2, Scsi_Cmnd * SCpnt,
		      struct Scsi_Host *shpnt, char *scsi_result)
{
	unsigned char scsi_cmd[MAX_COMMAND_SIZE];
	struct Scsi_Device_Template *sdtpnt;
	Scsi_Device *SDtail, *SDpnt = *SDpnt2;
	int bflags, type = -1;
	static int ghost_channel=-1, ghost_dev=-1;
	int org_lun = lun;

	SDpnt->host = shpnt;
	SDpnt->id = dev;
	SDpnt->lun = lun;
	SDpnt->channel = channel;
	SDpnt->online = TRUE;

 
	if ((channel == ghost_channel) && (dev == ghost_dev) && (lun == 1)) {
		SDpnt->lun = 0;
	} else {
		ghost_channel = ghost_dev = -1;
	}
	     

	/* Some low level driver could use device->type (DB) */
	SDpnt->type = -1;

	/*
	 * Assume that the device will have handshaking problems, and then fix this
	 * field later if it turns out it doesn't
	 */
	SDpnt->borken = 1;
	SDpnt->was_reset = 0;
	SDpnt->expecting_cc_ua = 0;
	SDpnt->starved = 0;

	scsi_cmd[0] = TEST_UNIT_READY;
	scsi_cmd[1] = lun << 5;
	scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[4] = scsi_cmd[5] = 0;

	SCpnt->host = SDpnt->host;
	SCpnt->device = SDpnt;
	SCpnt->target = SDpnt->id;
	SCpnt->lun = SDpnt->lun;
	SCpnt->channel = SDpnt->channel;

	scsi_wait_cmd (SCpnt, (void *) scsi_cmd,
	          (void *) NULL,
	          0, SCSI_TIMEOUT + 4 * HZ, 5);

	SCSI_LOG_SCAN_BUS(3, printk("scsi: scan_scsis_single id %d lun %d. Return code 0x%08x\n",
				    dev, lun, SCpnt->result));
	SCSI_LOG_SCAN_BUS(3, print_driverbyte(SCpnt->result));
	SCSI_LOG_SCAN_BUS(3, print_hostbyte(SCpnt->result));
	SCSI_LOG_SCAN_BUS(3, printk("\n"));

	if (SCpnt->result) {
		if (((driver_byte(SCpnt->result) & DRIVER_SENSE) ||
		     (status_byte(SCpnt->result) & CHECK_CONDITION)) &&
		    ((SCpnt->sense_buffer[0] & 0x70) >> 4) == 7) {
			if (((SCpnt->sense_buffer[2] & 0xf) != NOT_READY) &&
			    ((SCpnt->sense_buffer[2] & 0xf) != UNIT_ATTENTION) &&
			    ((SCpnt->sense_buffer[2] & 0xf) != ILLEGAL_REQUEST || lun > 0))
				return 1;
		} else
			return 0;
	}
	SCSI_LOG_SCAN_BUS(3, printk("scsi: performing INQUIRY\n"));
	/*
	 * Build an INQUIRY command block.
	 */
	scsi_cmd[0] = INQUIRY;
	scsi_cmd[1] = (lun << 5) & 0xe0;
	scsi_cmd[2] = 0;
	scsi_cmd[3] = 0;
	scsi_cmd[4] = 255;
	scsi_cmd[5] = 0;
	SCpnt->cmd_len = 0;

	scsi_wait_cmd (SCpnt, (void *) scsi_cmd,
	          (void *) scsi_result,
	          256, SCSI_TIMEOUT, 3);

	SCSI_LOG_SCAN_BUS(3, printk("scsi: INQUIRY %s with code 0x%x\n",
		SCpnt->result ? "failed" : "successful", SCpnt->result));

	if (SCpnt->result)
		return 0;	/* assume no peripheral if any sort of error */

	/*
	 * Check the peripheral qualifier field - this tells us whether LUNS
	 * are supported here or not.
	 */
	if ((scsi_result[0] >> 5) == 3) {
		return 0;	/* assume no peripheral if any sort of error */
	}

	/*
	 * Get any flags for this device.  
	 */
	bflags = get_device_flags (scsi_result);


	 /*   The Toshiba ROM was "gender-changed" here as an inline hack.
	      This is now much more generic.
	      This is a mess: What we really want is to leave the scsi_result
	      alone, and just change the SDpnt structure. And the SDpnt is what
	      we want print_inquiry to print.  -- REW
	 */
	if (bflags & BLIST_ISDISK) {
		scsi_result[0] = TYPE_DISK;                                                
		scsi_result[1] |= 0x80;     /* removable */
	}

	if (bflags & BLIST_ISROM) {
		scsi_result[0] = TYPE_ROM;
		scsi_result[1] |= 0x80;     /* removable */
	}
    
  	if (bflags & BLIST_GHOST) {
		if ((ghost_channel == channel) && (ghost_dev == dev) && (org_lun == 1)) {
			lun=1;
		} else {
			ghost_channel = channel;
			ghost_dev = dev;
			scsi_result[0] = TYPE_MOD;
			scsi_result[1] |= 0x80;     /* removable */
		}
	}
       

	memcpy(SDpnt->vendor, scsi_result + 8, 8);
	memcpy(SDpnt->model, scsi_result + 16, 16);
	memcpy(SDpnt->rev, scsi_result + 32, 4);

	SDpnt->removable = (0x80 & scsi_result[1]) >> 7;
	SDpnt->online = TRUE;
	SDpnt->lockable = SDpnt->removable;
	SDpnt->changed = 0;
	SDpnt->access_count = 0;
	SDpnt->busy = 0;
	SDpnt->has_cmdblocks = 0;
	/*
	 * Currently, all sequential devices are assumed to be tapes, all random
	 * devices disk, with the appropriate read only flags set for ROM / WORM
	 * treated as RO.
	 */
	switch (type = (scsi_result[0] & 0x1f)) {
	case TYPE_TAPE:
	case TYPE_DISK:
	case TYPE_MOD:
	case TYPE_PROCESSOR:
	case TYPE_SCANNER:
	case TYPE_MEDIUM_CHANGER:
	case TYPE_ENCLOSURE:
		SDpnt->writeable = 1;
		break;
	case TYPE_WORM:
	case TYPE_ROM:
		SDpnt->writeable = 0;
		break;
	default:
		printk("scsi: unknown type %d\n", type);
	}

	SDpnt->device_blocked = FALSE;
	SDpnt->device_busy = 0;
	SDpnt->single_lun = 0;
	SDpnt->soft_reset =
	    (scsi_result[7] & 1) && ((scsi_result[3] & 7) == 2);
	SDpnt->random = (type == TYPE_TAPE) ? 0 : 1;
	SDpnt->type = (type & 0x1f);

	print_inquiry(scsi_result);

	for (sdtpnt = scsi_devicelist; sdtpnt;
	     sdtpnt = sdtpnt->next)
		if (sdtpnt->detect)
			SDpnt->attached +=
			    (*sdtpnt->detect) (SDpnt);

	SDpnt->scsi_level = scsi_result[2] & 0x07;
	if (SDpnt->scsi_level >= 2 ||
	    (SDpnt->scsi_level == 1 &&
	     (scsi_result[3] & 0x0f) == 1))
		SDpnt->scsi_level++;

	/*
	 * Accommodate drivers that want to sleep when they should be in a polling
	 * loop.
	 */
	SDpnt->disconnect = 0;


	/*
	 * Set the tagged_queue flag for SCSI-II devices that purport to support
	 * tagged queuing in the INQUIRY data.
	 */
	SDpnt->tagged_queue = 0;
	if ((SDpnt->scsi_level >= SCSI_2) &&
	    (scsi_result[7] & 2) &&
	    !(bflags & BLIST_NOTQ)) {
		SDpnt->tagged_supported = 1;
		SDpnt->current_tag = 0;
	}
	/*
	 * Some revisions of the Texel CD ROM drives have handshaking problems when
	 * used with the Seagate controllers.  Before we know what type of device
	 * we're talking to, we assume it's borken and then change it here if it
	 * turns out that it isn't a TEXEL drive.
	 */
	if ((bflags & BLIST_BORKEN) == 0)
		SDpnt->borken = 0;

	/*
	 * If we want to only allow I/O to one of the luns attached to this device
	 * at a time, then we set this flag.
	 */
	if (bflags & BLIST_SINGLELUN)
		SDpnt->single_lun = 1;

	/*
	 * These devices need this "key" to unlock the devices so we can use it
	 */
	if ((bflags & BLIST_KEY) != 0) {
		printk("Unlocked floptical drive.\n");
		SDpnt->lockable = 0;
		scsi_cmd[0] = MODE_SENSE;
		scsi_cmd[1] = (lun << 5) & 0xe0;
		scsi_cmd[2] = 0x2e;
		scsi_cmd[3] = 0;
		scsi_cmd[4] = 0x2a;
		scsi_cmd[5] = 0;
		SCpnt->cmd_len = 0;
		scsi_wait_cmd (SCpnt, (void *) scsi_cmd,
	        	(void *) scsi_result, 0x2a,
	        	SCSI_TIMEOUT, 3);
	}
	/*
	 * Detach the command from the device. It was just a temporary to be used while
	 * scanning the bus - the real ones will be allocated later.
	 */
	SDpnt->device_queue = NULL;

	/*
	 * This device was already hooked up to the host in question,
	 * so at this point we just let go of it and it should be fine.  We do need to
	 * allocate a new one and attach it to the host so that we can further scan the bus.
	 */
	SDpnt = (Scsi_Device *) kmalloc(sizeof(Scsi_Device), GFP_ATOMIC);
	*SDpnt2 = SDpnt;
	if (!SDpnt) {
		printk("scsi: scan_scsis_single: Cannot malloc\n");
		return 0;
	}
        memset(SDpnt, 0, sizeof(Scsi_Device));

	/*
	 * Register the queue for the device.  All I/O requests will come
	 * in through here.  We also need to register a pointer to
	 * ourselves, since the queue handler won't know what device
	 * the queue actually represents.   We could look it up, but it
	 * is pointless work.
	 */
	blk_init_queue(&SDpnt->request_queue, scsi_get_request_handler(SDpnt, shpnt));
	blk_queue_headactive(&SDpnt->request_queue, 0);
	SDpnt->request_queue.queuedata = (void *) SDpnt;
	SDpnt->host = shpnt;
	initialize_merge_fn(SDpnt);

	/*
	 * And hook up our command block to the new device we will be testing
	 * for.
	 */
	SDpnt->device_queue = SCpnt;
	SDpnt->online = TRUE;

        /*
         * Initialize the object that we will use to wait for command blocks.
         */
	init_waitqueue_head(&SDpnt->scpnt_wait);

	/*
	 * Since we just found one device, there had damn well better be one in the list
	 * already.
	 */
	if (shpnt->host_queue == NULL)
		panic("scan_scsis_single: Host queue == NULL\n");

	SDtail = shpnt->host_queue;
	while (SDtail->next) {
		SDtail = SDtail->next;
	}

	/* Add this device to the linked list at the end */
	SDtail->next = SDpnt;
	SDpnt->prev = SDtail;
	SDpnt->next = NULL;

	/*
	 * Some scsi devices cannot be polled for lun != 0 due to firmware bugs
	 */
	if (bflags & BLIST_NOLUN)
		return 0;	/* break; */

	/*
	 * If this device is known to support sparse multiple units, override the
	 * other settings, and scan all of them.
	 */
	if (bflags & BLIST_SPARSELUN) {
		*max_dev_lun = 8;
		*sparse_lun = 1;
		return 1;
	}
	/*
	 * If this device is known to support multiple units, override the other
	 * settings, and scan all of them.
	 */
	if (bflags & BLIST_FORCELUN) {
		*max_dev_lun = 8;
		return 1;
	}
	/*
	 * REGAL CDC-4X: avoid hang after LUN 4
	 */
	if (bflags & BLIST_MAX5LUN) {
		*max_dev_lun = 5;
		return 1;
	}

	/*
	 * If this device is Ghosted, scan upto two luns. (It physically only
	 * has one). -- REW
	 */
	if (bflags & BLIST_GHOST) {
	        *max_dev_lun = 2;
	        return 1;
	}  


	/*
	 * We assume the device can't handle lun!=0 if: - it reports scsi-0 (ANSI
	 * SCSI Revision 0) (old drives like MAXTOR XT-3280) or - it reports scsi-1
	 * (ANSI SCSI Revision 1) and Response Data Format 0
	 */
	if (((scsi_result[2] & 0x07) == 0)
	    ||
	    ((scsi_result[2] & 0x07) == 1 &&
	     (scsi_result[3] & 0x0f) == 0))
		return 0;
	return 1;
}

