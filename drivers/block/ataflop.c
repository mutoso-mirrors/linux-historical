/*
 *  drivers/block/ataflop.c
 *
 *  Copyright (C) 1993  Greg Harp
 *  Atari Support by Bjoern Brauel, Roman Hodek
 *
 *  Big cleanup Sep 11..14 1994 Roman Hodek:
 *   - Driver now works interrupt driven
 *   - Support for two drives; should work, but I cannot test that :-(
 *   - Reading is done in whole tracks and buffered to speed up things
 *   - Disk change detection and drive deselecting after motor-off
 *     similar to TOS
 *   - Autodetection of disk format (DD/HD); untested yet, because I
 *     don't have an HD drive :-(
 *
 *  Fixes Nov 13 1994 Martin Schaller:
 *   - Autodetection works now
 *   - Support for 5 1/4'' disks
 *   - Removed drive type (unknown on atari)
 *   - Do seeks with 8 Mhz
 *
 *  Changes by Andreas Schwab:
 *   - After errors in multiple read mode try again reading single sectors
 *  (Feb 1995):
 *   - Clean up error handling
 *   - Set blk_size for proper size checking
 *   - Initialize track register when testing presence of floppy
 *   - Implement some ioctl's
 *
 *  Changes by Torsten Lang:
 *   - When probing the floppies we should add the FDCCMDADD_H flag since
 *     the FDC will otherwise wait forever when no disk is inserted...
 *
 * ++ Freddi Aschwanden (fa) 20.9.95 fixes for medusa:
 *  - MFPDELAY() after each FDC access -> atari 
 *  - more/other disk formats
 *  - DMA to the block buffer directly if we have a 32bit DMA
 *  - for medusa, the step rate is always 3ms
 *  - on medusa, use only cache_push()
 * Roman:
 *  - Make disk format numbering independent from minors
 *  - Let user set max. supported drive type (speeds up format
 *    detection, saves buffer space)
 *
 * Roman 10/15/95:
 *  - implement some more ioctls
 *  - disk formatting
 *  
 * Andreas 95/12/12:
 *  - increase gap size at start of track for HD/ED disks
 *
 * Michael (MSch) 11/07/96:
 *  - implemented FDSETPRM and FDDEFPRM ioctl
 *
 * Andreas (97/03/19):
 *  - implemented missing BLK* ioctls
 *
 *  Things left to do:
 *   - Formatting
 *   - Maybe a better strategy for disk change detection (does anyone
 *     know one?)
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/fd.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/uaccess.h>

#include <asm/atafd.h>
#include <asm/atafdreg.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_stdma.h>

#define MAJOR_NR FLOPPY_MAJOR
#include <linux/blk.h>

#define	FD_MAX_UNITS 2

#undef DEBUG

/* Disk types: DD, HD, ED */
static struct atari_disk_type {
	const char	*name;
	unsigned	spt;		/* sectors per track */
	unsigned	blocks;		/* total number of blocks */
	unsigned	fdc_speed;	/* fdc_speed setting */
	unsigned 	stretch;	/* track doubling ? */
} disk_type[] = {
	{ "d360",  9, 720, 0, 0},	/*  0: 360kB diskette */
	{ "D360",  9, 720, 0, 1},	/*  1: 360kb in 720k or 1.2MB drive */
	{ "D720",  9,1440, 0, 0},	/*  2: 720kb in 720k or 1.2MB drive */
	{ "D820", 10,1640, 0, 0},	/*  3: DD disk with 82 tracks/10 sectors */
/* formats above are probed for type DD */
#define	MAX_TYPE_DD 3
	{ "h1200",15,2400, 3, 0},	/*  4: 1.2MB diskette */
	{ "H1440",18,2880, 3, 0},	/*  5: 1.4 MB diskette (HD) */
	{ "H1640",20,3280, 3, 0},	/*  6: 1.64MB diskette (fat HD) 82 tr 20 sec */
/* formats above are probed for types DD and HD */
#define	MAX_TYPE_HD 6
	{ "E2880",36,5760, 3, 0},	/*  7: 2.8 MB diskette (ED) */
	{ "E3280",40,6560, 3, 0},	/*  8: 3.2 MB diskette (fat ED) 82 tr 40 sec */
/* formats above are probed for types DD, HD and ED */
#define	MAX_TYPE_ED 8
/* types below are never autoprobed */
	{ "H1680",21,3360, 3, 0},	/*  9: 1.68MB diskette (fat HD) 80 tr 21 sec */
	{ "h410",10,820, 0, 1},		/* 10: 410k diskette 41 tr 10 sec, stretch */
	{ "h1476",18,2952, 3, 0},	/* 11: 1.48MB diskette 82 tr 18 sec */
	{ "H1722",21,3444, 3, 0},	/* 12: 1.72MB diskette 82 tr 21 sec */
	{ "h420",10,840, 0, 1},		/* 13: 420k diskette 42 tr 10 sec, stretch */
	{ "H830",10,1660, 0, 0},	/* 14: 820k diskette 83 tr 10 sec */
	{ "h1494",18,2952, 3, 0},	/* 15: 1.49MB diskette 83 tr 18 sec */
	{ "H1743",21,3486, 3, 0},	/* 16: 1.74MB diskette 83 tr 21 sec */
	{ "h880",11,1760, 0, 0},	/* 17: 880k diskette 80 tr 11 sec */
	{ "D1040",13,2080, 0, 0},	/* 18: 1.04MB diskette 80 tr 13 sec */
	{ "D1120",14,2240, 0, 0},	/* 19: 1.12MB diskette 80 tr 14 sec */
	{ "h1600",20,3200, 3, 0},	/* 20: 1.60MB diskette 80 tr 20 sec */
	{ "H1760",22,3520, 3, 0},	/* 21: 1.76MB diskette 80 tr 22 sec */
	{ "H1920",24,3840, 3, 0},	/* 22: 1.92MB diskette 80 tr 24 sec */
	{ "E3200",40,6400, 3, 0},	/* 23: 3.2MB diskette 80 tr 40 sec */
	{ "E3520",44,7040, 3, 0},	/* 24: 3.52MB diskette 80 tr 44 sec */
	{ "E3840",48,7680, 3, 0},	/* 25: 3.84MB diskette 80 tr 48 sec */
	{ "H1840",23,3680, 3, 0},	/* 26: 1.84MB diskette 80 tr 23 sec */
	{ "D800",10,1600, 0, 0},	/* 27: 800k diskette 80 tr 10 sec */
};

static int StartDiskType[] = {
	MAX_TYPE_DD,
	MAX_TYPE_HD,
	MAX_TYPE_ED
};

#define	TYPE_DD		0
#define	TYPE_HD		1
#define	TYPE_ED		2

static int DriveType = TYPE_HD;

/* Array for translating minors into disk formats */
static struct {
	int 	 index;
	unsigned drive_types;
} minor2disktype[] = {
	{  0, TYPE_DD },	/*  1: d360 */
	{  4, TYPE_HD },	/*  2: h1200 */
	{  1, TYPE_DD },	/*  3: D360 */
	{  2, TYPE_DD },	/*  4: D720 */
	{  1, TYPE_DD },	/*  5: h360 = D360 */
	{  2, TYPE_DD },	/*  6: h720 = D720 */
	{  5, TYPE_HD },	/*  7: H1440 */
	{  7, TYPE_ED },	/*  8: E2880 */
/* some PC formats :-) */
	{  8, TYPE_ED },	/*  9: E3280    <- was "CompaQ" == E2880 for PC */
	{  5, TYPE_HD },	/* 10: h1440 = H1440 */
	{  9, TYPE_HD },	/* 11: H1680 */
	{ 10, TYPE_DD },	/* 12: h410  */
	{  3, TYPE_DD },	/* 13: H820     <- == D820, 82x10 */
	{ 11, TYPE_HD },	/* 14: h1476 */
	{ 12, TYPE_HD },	/* 15: H1722 */
	{ 13, TYPE_DD },	/* 16: h420  */
	{ 14, TYPE_DD },	/* 17: H830  */
	{ 15, TYPE_HD },	/* 18: h1494 */
	{ 16, TYPE_HD },	/* 19: H1743 */
	{ 17, TYPE_DD },	/* 20: h880  */
	{ 18, TYPE_DD },	/* 21: D1040 */
	{ 19, TYPE_DD },	/* 22: D1120 */
	{ 20, TYPE_HD },	/* 23: h1600 */
	{ 21, TYPE_HD },	/* 24: H1760 */
	{ 22, TYPE_HD },	/* 25: H1920 */
	{ 23, TYPE_ED },	/* 26: E3200 */
	{ 24, TYPE_ED },	/* 27: E3520 */
	{ 25, TYPE_ED },	/* 28: E3840 */
	{ 26, TYPE_HD },	/* 29: H1840 */
	{ 27, TYPE_DD },	/* 30: D800  */
	{  6, TYPE_HD },	/* 31: H1640    <- was H1600 == h1600 for PC */
};

#define NUM_DISK_MINORS (sizeof(minor2disktype)/sizeof(*minor2disktype))

/*
 * Maximum disk size (in kilobytes). This default is used whenever the
 * current disk size is unknown.
 */
#define MAX_DISK_SIZE 3280

/*
 * MSch: User-provided type information. 'drive' points to
 * the respective entry of this array. Set by FDSETPRM ioctls.
 */
static struct atari_disk_type user_params[FD_MAX_UNITS];

/*
 * User-provided permanent type information. 'drive' points to
 * the respective entry of this array.  Set by FDDEFPRM ioctls, 
 * restored upon disk change by floppy_revalidate() if valid (as seen by
 * default_params[].blocks > 0 - a bit in unit[].flags might be used for this?)
 */
static struct atari_disk_type default_params[FD_MAX_UNITS] = {
	{ NULL,  0, 0, 0, 0},  };

static int floppy_sizes[256];
static int floppy_blocksizes[256] = { 0, };

/* current info on each unit */
static struct atari_floppy_struct {
	int connected;				/* !=0 : drive is connected */
	int autoprobe;				/* !=0 : do autoprobe	    */

	struct atari_disk_type	*disktype;	/* current type of disk */

	int track;		/* current head position or -1 if
				   unknown */
	unsigned int steprate;	/* steprate setting */
	unsigned int wpstat;	/* current state of WP signal (for
				   disk change detection) */
	int flags;		/* flags */
} unit[FD_MAX_UNITS];

#define	UD	unit[drive]
#define	UDT	unit[drive].disktype
#define	SUD	unit[SelectedDrive]
#define	SUDT	unit[SelectedDrive].disktype


#define FDC_READ(reg) ({			\
    /* unsigned long __flags; */		\
    unsigned short __val;			\
    /* save_flags(__flags); cli(); */		\
    dma_wd.dma_mode_status = 0x80 | (reg);	\
    udelay(25);					\
    __val = dma_wd.fdc_acces_seccount;		\
    MFPDELAY();					\
    /* restore_flags(__flags); */		\
    __val & 0xff;				\
})

#define FDC_WRITE(reg,val)			\
    do {					\
	/* unsigned long __flags; */		\
	/* save_flags(__flags); cli(); */	\
	dma_wd.dma_mode_status = 0x80 | (reg);	\
	udelay(25);				\
	dma_wd.fdc_acces_seccount = (val);	\
	MFPDELAY();				\
        /* restore_flags(__flags); */		\
    } while(0)


/* Buffering variables:
 * First, there is a DMA buffer in ST-RAM that is used for floppy DMA
 * operations. Second, a track buffer is used to cache a whole track
 * of the disk to save read operations. These are two separate buffers
 * because that allows write operations without clearing the track buffer.
 */

static int MaxSectors[] = {
	11, 22, 44
};
static int BufferSize[] = {
	15*512, 30*512, 60*512
};

#define	MAX_SECTORS	(MaxSectors[DriveType])
#define	BUFFER_SIZE	(BufferSize[DriveType])

unsigned char *DMABuffer;			  /* buffer for writes */
static unsigned long PhysDMABuffer;   /* physical address */

static int UseTrackbuffer = -1;		  /* Do track buffering? */
MODULE_PARM(UseTrackbuffer, "i");

unsigned char *TrackBuffer;			  /* buffer for reads */
static unsigned long PhysTrackBuffer; /* physical address */
static int BufferDrive, BufferSide, BufferTrack;
static int read_track;		/* non-zero if we are reading whole tracks */

#define	SECTOR_BUFFER(sec)	(TrackBuffer + ((sec)-1)*512)
#define	IS_BUFFERED(drive,side,track) \
    (BufferDrive == (drive) && BufferSide == (side) && BufferTrack == (track))

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
static int SelectedDrive = 0;
static int ReqCmd, ReqBlock;
static int ReqSide, ReqTrack, ReqSector, ReqCnt;
static int HeadSettleFlag = 0;
static unsigned char *ReqData, *ReqBuffer;
static int MotorOn = 0, MotorOffTrys;
static int IsFormatting = 0, FormatError;

static int UserSteprate[FD_MAX_UNITS] = { -1, -1 };
MODULE_PARM(UserSteprate, "1-" __MODULE_STRING(FD_MAX_UNITS) "i");

/* Synchronization of FDC access. */
static volatile int fdc_busy = 0;
static struct wait_queue *fdc_wait = NULL;
static struct wait_queue *format_wait = NULL;

static unsigned int changed_floppies = 0xff, fake_change = 0;
#define	CHECK_CHANGE_DELAY	HZ/2

#define	FD_MOTOR_OFF_DELAY	(3*HZ)
#define	FD_MOTOR_OFF_MAXTRY	(10*20)

#define FLOPPY_TIMEOUT		(6*HZ)
#define RECALIBRATE_ERRORS	4	/* After this many errors the drive
					 * will be recalibrated. */
#define MAX_ERRORS		8	/* After this many errors the driver
					 * will give up. */


#define	START_MOTOR_OFF_TIMER(delay)				\
    do {							\
        motor_off_timer.expires = jiffies + (delay);		\
        add_timer( &motor_off_timer );				\
        MotorOffTrys = 0;					\
	} while(0)

#define	START_CHECK_CHANGE_TIMER(delay)				\
    do {							\
        timer_table[FLOPPY_TIMER].expires = jiffies + (delay);	\
        timer_active |= (1 << FLOPPY_TIMER);			\
	} while(0)

#define	START_TIMEOUT()						\
    do {							\
        del_timer( &timeout_timer );				\
        timeout_timer.expires = jiffies + FLOPPY_TIMEOUT;	\
        add_timer( &timeout_timer );				\
	} while(0)

#define	STOP_TIMEOUT()						\
    do {							\
        del_timer( &timeout_timer );				\
	} while(0)


/*
 * The driver is trying to determine the correct media format
 * while Probing is set. fd_rwsec_done() clears it after a
 * successful access.
 */
static int Probing = 0;

/* This flag is set when a dummy seek is necessary to make the WP
 * status bit accessible.
 */
static int NeedSeek = 0;


#ifdef DEBUG
#define DPRINT(a)	printk a
#else
#define DPRINT(a)
#endif

/***************************** Prototypes *****************************/

static void fd_select_side( int side );
static void fd_select_drive( int drive );
static void fd_deselect( void );
static void fd_motor_off_timer( unsigned long dummy );
static void check_change( void );
static __inline__ void set_head_settle_flag( void );
static __inline__ int get_head_settle_flag( void );
static void floppy_irq (int irq, void *dummy, struct pt_regs *fp);
static void fd_error( void );
static int do_format(kdev_t drive, struct atari_format_descr *desc);
static void do_fd_action( int drive );
static void fd_calibrate( void );
static void fd_calibrate_done( int status );
static void fd_seek( void );
static void fd_seek_done( int status );
static void fd_rwsec( void );
static void fd_readtrack_check( unsigned long dummy );
static void fd_rwsec_done( int status );
static void fd_writetrack( void );
static void fd_writetrack_done( int status );
static void fd_times_out( unsigned long dummy );
static void finish_fdc( void );
static void finish_fdc_done( int dummy );
static void floppy_off( unsigned int nr);
static __inline__ void copy_buffer( void *from, void *to);
static void setup_req_params( int drive );
static void redo_fd_request( void);
static int invalidate_drive(kdev_t rdev);
static int fd_ioctl( struct inode *inode, struct file *filp, unsigned int
                     cmd, unsigned long param);
static void fd_probe( int drive );
static int fd_test_drive_present( int drive );
static void config_types( void );
static int floppy_open( struct inode *inode, struct file *filp );
static int floppy_release( struct inode * inode, struct file * filp );

/************************* End of Prototypes **************************/

static struct timer_list motor_off_timer =
	{ NULL, NULL, 0, 0, fd_motor_off_timer };
static struct timer_list readtrack_timer =
	{ NULL, NULL, 0, 0, fd_readtrack_check };

static struct timer_list timeout_timer =
	{ NULL, NULL, 0, 0, fd_times_out };



/* Select the side to use. */

static void fd_select_side( int side )
{
	unsigned long flags;

	save_flags(flags);
	cli(); /* protect against various other ints mucking around with the PSG */
  
	sound_ym.rd_data_reg_sel = 14; /* Select PSG Port A */
	sound_ym.wd_data = (side == 0) ? sound_ym.rd_data_reg_sel | 0x01 :
	                                 sound_ym.rd_data_reg_sel & 0xfe;

	restore_flags(flags);
}


/* Select a drive, update the FDC's track register and set the correct
 * clock speed for this disk's type.
 */

static void fd_select_drive( int drive )
{
	unsigned long flags;
	unsigned char tmp;
  
	if (drive == SelectedDrive)
	  return;

	save_flags(flags);
	cli(); /* protect against various other ints mucking around with the PSG */
	sound_ym.rd_data_reg_sel = 14; /* Select PSG Port A */
	tmp = sound_ym.rd_data_reg_sel;
	sound_ym.wd_data = (tmp | DSKDRVNONE) & ~(drive == 0 ? DSKDRV0 : DSKDRV1);
	restore_flags(flags);

	/* restore track register to saved value */
	FDC_WRITE( FDCREG_TRACK, UD.track );
	udelay(25);

	/* select 8/16 MHz */
	if (UDT)
		if (ATARIHW_PRESENT(FDCSPEED))
			dma_wd.fdc_speed = UDT->fdc_speed;
	
	SelectedDrive = drive;
}


/* Deselect both drives. */

static void fd_deselect( void )
{
	unsigned long flags;

	save_flags(flags);
	cli(); /* protect against various other ints mucking around with the PSG */
	sound_ym.rd_data_reg_sel=14;	/* Select PSG Port A */
	sound_ym.wd_data = sound_ym.rd_data_reg_sel | 7; /* no drives selected */
	SelectedDrive = -1;
	restore_flags(flags);
}


/* This timer function deselects the drives when the FDC switched the
 * motor off. The deselection cannot happen earlier because the FDC
 * counts the index signals, which arrive only if one drive is selected.
 */

static void fd_motor_off_timer( unsigned long dummy )
{
/*	unsigned long flags; */
	unsigned char status;
	int		      delay;

	del_timer( &motor_off_timer );
	
	if (SelectedDrive < 0)
		/* no drive selected, needn't deselect anyone */
		return;

/*	save_flags(flags);
	cli(); */
	
	if (stdma_islocked())
		goto retry;

	status = FDC_READ( FDCREG_STATUS );

	if (!(status & 0x80)) {
		/* motor already turned off by FDC -> deselect drives */
		MotorOn = 0;
		fd_deselect();
/*		restore_flags(flags); */
		return;
	}
	/* not yet off, try again */

  retry:
/*	restore_flags(flags); */
	/* Test again later; if tested too often, it seems there is no disk
	 * in the drive and the FDC will leave the motor on forever (or,
	 * at least until a disk is inserted). So we'll test only twice
	 * per second from then on...
	 */
	delay = (MotorOffTrys < FD_MOTOR_OFF_MAXTRY) ?
		        (++MotorOffTrys, HZ/20) : HZ/2;
	START_MOTOR_OFF_TIMER( delay );
}


/* This function is repeatedly called to detect disk changes (as good
 * as possible) and keep track of the current state of the write protection.
 */

static void check_change( void )
{
	static int    drive = 0;

	unsigned long flags;
	unsigned char old_porta;
	int			  stat;

	if (++drive > 1 || !UD.connected)
		drive = 0;

	save_flags(flags);
	cli(); /* protect against various other ints mucking around with the PSG */

	if (!stdma_islocked()) {
		sound_ym.rd_data_reg_sel = 14;
		old_porta = sound_ym.rd_data_reg_sel;
		sound_ym.wd_data = (old_porta | DSKDRVNONE) &
			               ~(drive == 0 ? DSKDRV0 : DSKDRV1);
		stat = !!(FDC_READ( FDCREG_STATUS ) & FDCSTAT_WPROT);
		sound_ym.wd_data = old_porta;

		if (stat != UD.wpstat) {
			DPRINT(( "wpstat[%d] = %d\n", drive, stat ));
			UD.wpstat = stat;
			set_bit (drive, &changed_floppies);
		}
	}
	restore_flags(flags);

	START_CHECK_CHANGE_TIMER( CHECK_CHANGE_DELAY );
}

 
/* Handling of the Head Settling Flag: This flag should be set after each
 * seek operation, because we don't use seeks with verify.
 */

static __inline__ void set_head_settle_flag( void )
{
	HeadSettleFlag = FDCCMDADD_E;
}

static __inline__ int get_head_settle_flag( void )
{
	int	tmp = HeadSettleFlag;
	HeadSettleFlag = 0;
	return( tmp );
}

  
  

/* General Interrupt Handling */

static void (*FloppyIRQHandler)( int status ) = NULL;

static void floppy_irq (int irq, void *dummy, struct pt_regs *fp)
{
	unsigned char status;
	void (*handler)( int );

	handler = FloppyIRQHandler;
	FloppyIRQHandler = NULL;

	if (handler) {
		nop();
		status = FDC_READ( FDCREG_STATUS );
		DPRINT(("FDC irq, status = %02x handler = %08lx\n",status,(unsigned long)handler));
		handler( status );
	}
	else {
		DPRINT(("FDC irq, no handler\n"));
	}
}


/* Error handling: If some error happened, retry some times, then
 * recalibrate, then try again, and fail after MAX_ERRORS.
 */

static void fd_error( void )
{
	if (IsFormatting) {
		IsFormatting = 0;
		FormatError = 1;
		wake_up( &format_wait );
		return;
	}
		
	if (!CURRENT) return;
	CURRENT->errors++;
	if (CURRENT->errors >= MAX_ERRORS) {
		printk(KERN_ERR "fd%d: too many errors.\n", SelectedDrive );
		end_request( 0 );
	}
	else if (CURRENT->errors == RECALIBRATE_ERRORS) {
		printk(KERN_WARNING "fd%d: recalibrating\n", SelectedDrive );
		if (SelectedDrive != -1)
			SUD.track = -1;
	}
	redo_fd_request();
}



#define	SET_IRQ_HANDLER(proc) do { FloppyIRQHandler = (proc); } while(0)


/* ---------- Formatting ---------- */

#define FILL(n,val)		\
    do {			\
	memset( p, val, n );	\
	p += n;			\
    } while(0)

static int do_format(kdev_t device, struct atari_format_descr *desc)
{
	unsigned char	*p;
	int sect, nsect;
	unsigned long	flags;
	int type, drive = MINOR(device) & 3;

	DPRINT(("do_format( dr=%d tr=%d he=%d offs=%d )\n",
		drive, desc->track, desc->head, desc->sect_offset ));

	save_flags(flags);
	cli();
	while( fdc_busy ) sleep_on( &fdc_wait );
	fdc_busy = 1;
	stdma_lock(floppy_irq, NULL);
	atari_turnon_irq( IRQ_MFP_FDC ); /* should be already, just to be sure */
	restore_flags(flags);

	type = MINOR(device) >> 2;
	if (type) {
		if (--type >= NUM_DISK_MINORS ||
		    minor2disktype[type].drive_types > DriveType) {
			redo_fd_request();
			return -EINVAL;
		}
		type = minor2disktype[type].index;
		UDT = &disk_type[type];
	}

	if (!UDT || desc->track >= UDT->blocks/UDT->spt/2 || desc->head >= 2) {
		redo_fd_request();
		return -EINVAL;
	}

	nsect = UDT->spt;
	p = TrackBuffer;
	/* The track buffer is used for the raw track data, so its
	   contents become invalid! */
	BufferDrive = -1;
	/* stop deselect timer */
	del_timer( &motor_off_timer );

	FILL( 60 * (nsect / 9), 0x4e );
	for( sect = 0; sect < nsect; ++sect ) {
		FILL( 12, 0 );
		FILL( 3, 0xf5 );
		*p++ = 0xfe;
		*p++ = desc->track;
		*p++ = desc->head;
		*p++ = (nsect + sect - desc->sect_offset) % nsect + 1;
		*p++ = 2;
		*p++ = 0xf7;
		FILL( 22, 0x4e );
		FILL( 12, 0 );
		FILL( 3, 0xf5 );
		*p++ = 0xfb;
		FILL( 512, 0xe5 );
		*p++ = 0xf7;
		FILL( 40, 0x4e );
	}
	FILL( TrackBuffer+BUFFER_SIZE-p, 0x4e );

	IsFormatting = 1;
	FormatError = 0;
	ReqTrack = desc->track;
	ReqSide  = desc->head;
	do_fd_action( drive );

	sleep_on( &format_wait );

	redo_fd_request();
	return( FormatError ? -EIO : 0 );	
}


/* do_fd_action() is the general procedure for a fd request: All
 * required parameter settings (drive select, side select, track
 * position) are checked and set if needed. For each of these
 * parameters and the actual reading or writing exist two functions:
 * one that starts the setting (or skips it if possible) and one
 * callback for the "done" interrupt. Each done func calls the next
 * set function to propagate the request down to fd_rwsec_done().
 */

static void do_fd_action( int drive )
{
	DPRINT(("do_fd_action\n"));
	
	if (UseTrackbuffer && !IsFormatting) {
	repeat:
	    if (IS_BUFFERED( drive, ReqSide, ReqTrack )) {
		if (ReqCmd == READ) {
		    copy_buffer( SECTOR_BUFFER(ReqSector), ReqData );
		    if (++ReqCnt < CURRENT->current_nr_sectors) {
			/* read next sector */
			setup_req_params( drive );
			goto repeat;
		    }
		    else {
			/* all sectors finished */
			CURRENT->nr_sectors -= CURRENT->current_nr_sectors;
			CURRENT->sector += CURRENT->current_nr_sectors;
			end_request( 1 );
			redo_fd_request();
			return;
		    }
		}
		else {
		    /* cmd == WRITE, pay attention to track buffer
		     * consistency! */
		    copy_buffer( ReqData, SECTOR_BUFFER(ReqSector) );
		}
	    }
	}

	if (SelectedDrive != drive)
		fd_select_drive( drive );
    
	if (UD.track == -1)
		fd_calibrate();
	else if (UD.track != ReqTrack << UDT->stretch)
		fd_seek();
	else if (IsFormatting)
		fd_writetrack();
	else
		fd_rwsec();
}


/* Seek to track 0 if the current track is unknown */

static void fd_calibrate( void )
{
	if (SUD.track >= 0) {
		fd_calibrate_done( 0 );
		return;
	}

	if (ATARIHW_PRESENT(FDCSPEED))
		dma_wd.fdc_speed = 0; 	/* always seek with 8 Mhz */;
	DPRINT(("fd_calibrate\n"));
	SET_IRQ_HANDLER( fd_calibrate_done );
	/* we can't verify, since the speed may be incorrect */
	FDC_WRITE( FDCREG_CMD, FDCCMD_RESTORE | SUD.steprate );

	NeedSeek = 1;
	MotorOn = 1;
	START_TIMEOUT();
	/* wait for IRQ */
}


static void fd_calibrate_done( int status )
{
	DPRINT(("fd_calibrate_done()\n"));
	STOP_TIMEOUT();
    
	/* set the correct speed now */
	if (ATARIHW_PRESENT(FDCSPEED))
		dma_wd.fdc_speed = SUDT->fdc_speed;
	if (status & FDCSTAT_RECNF) {
		printk(KERN_ERR "fd%d: restore failed\n", SelectedDrive );
		fd_error();
	}
	else {
		SUD.track = 0;
		fd_seek();
	}
}
  
  
/* Seek the drive to the requested track. The drive must have been
 * calibrated at some point before this.
 */
  
static void fd_seek( void )
{
	if (SUD.track == ReqTrack << SUDT->stretch) {
		fd_seek_done( 0 );
		return;
	}

	if (ATARIHW_PRESENT(FDCSPEED)) {
		dma_wd.fdc_speed = 0;	/* always seek witch 8 Mhz */
		MFPDELAY();
	}

	DPRINT(("fd_seek() to track %d\n",ReqTrack));
	FDC_WRITE( FDCREG_DATA, ReqTrack << SUDT->stretch);
	udelay(25);
	SET_IRQ_HANDLER( fd_seek_done );
	FDC_WRITE( FDCREG_CMD, FDCCMD_SEEK | SUD.steprate );

	MotorOn = 1;
	set_head_settle_flag();
	START_TIMEOUT();
	/* wait for IRQ */
}


static void fd_seek_done( int status )
{
	DPRINT(("fd_seek_done()\n"));
	STOP_TIMEOUT();
	
	/* set the correct speed */
	if (ATARIHW_PRESENT(FDCSPEED))
		dma_wd.fdc_speed = SUDT->fdc_speed;
	if (status & FDCSTAT_RECNF) {
		printk(KERN_ERR "fd%d: seek error (to track %d)\n",
				SelectedDrive, ReqTrack );
		/* we don't know exactly which track we are on now! */
		SUD.track = -1;
		fd_error();
	}
	else {
		SUD.track = ReqTrack << SUDT->stretch;
		NeedSeek = 0;
		if (IsFormatting)
			fd_writetrack();
		else
			fd_rwsec();
	}
}


/* This does the actual reading/writing after positioning the head
 * over the correct track.
 */

static int MultReadInProgress = 0;


static void fd_rwsec( void )
{
	unsigned long paddr, flags;
	unsigned int  rwflag, old_motoron;
	unsigned int track;
	
	DPRINT(("fd_rwsec(), Sec=%d, Access=%c\n",ReqSector, ReqCmd == WRITE ? 'w' : 'r' ));
	if (ReqCmd == WRITE) {
		if (ATARIHW_PRESENT(EXTD_DMA)) {
			paddr = (unsigned long)VTOP(ReqData);
		}
		else {
			copy_buffer( ReqData, DMABuffer );
			paddr = PhysDMABuffer;
		}
		dma_cache_maintenance( paddr, 512, 1 );
		rwflag = 0x100;
	}
	else {
		if (read_track)
			paddr = PhysTrackBuffer;
		else
			paddr = ATARIHW_PRESENT(EXTD_DMA) ? VTOP(ReqData) : PhysDMABuffer;
		rwflag = 0;
	}

	fd_select_side( ReqSide );
  
	/* Start sector of this operation */
	FDC_WRITE( FDCREG_SECTOR, read_track ? 1 : ReqSector );
	MFPDELAY();
	/* Cheat for track if stretch != 0 */
	if (SUDT->stretch) {
		track = FDC_READ( FDCREG_TRACK);
		MFPDELAY();
		FDC_WRITE( FDCREG_TRACK, track >> SUDT->stretch);
	}
	udelay(25);
  
	/* Setup DMA */
	save_flags(flags);  
	cli();
	dma_wd.dma_lo = (unsigned char)paddr;
	MFPDELAY();
	paddr >>= 8;
	dma_wd.dma_md = (unsigned char)paddr;
	MFPDELAY();
	paddr >>= 8;
	if (ATARIHW_PRESENT(EXTD_DMA))
		st_dma_ext_dmahi = (unsigned short)paddr;
	else
		dma_wd.dma_hi = (unsigned char)paddr;
	MFPDELAY();
	restore_flags(flags);
  
	/* Clear FIFO and switch DMA to correct mode */  
	dma_wd.dma_mode_status = 0x90 | rwflag;  
	MFPDELAY();
	dma_wd.dma_mode_status = 0x90 | (rwflag ^ 0x100);  
	MFPDELAY();
	dma_wd.dma_mode_status = 0x90 | rwflag;
	MFPDELAY();
  
	/* How many sectors for DMA */
	dma_wd.fdc_acces_seccount = read_track ? SUDT->spt : 1;
  
	udelay(25);  
  
	/* Start operation */
	dma_wd.dma_mode_status = FDCSELREG_STP | rwflag;
	udelay(25);
	SET_IRQ_HANDLER( fd_rwsec_done );
	dma_wd.fdc_acces_seccount =
	  (get_head_settle_flag() |
	   (rwflag ? FDCCMD_WRSEC : (FDCCMD_RDSEC | (read_track ? FDCCMDADD_M : 0))));

	old_motoron = MotorOn;
	MotorOn = 1;
	NeedSeek = 1;
	/* wait for interrupt */

	if (read_track) {
		/* If reading a whole track, wait about one disk rotation and
		 * then check if all sectors are read. The FDC will even
		 * search for the first non-existent sector and need 1 sec to
		 * recognise that it isn't present :-(
		 */
		readtrack_timer.expires =
		  jiffies + HZ/5 + (old_motoron ? 0 : HZ);
		       /* 1 rot. + 5 rot.s if motor was off  */
		add_timer( &readtrack_timer );
		MultReadInProgress = 1;
	}
	START_TIMEOUT();
}

    
static void fd_readtrack_check( unsigned long dummy )
{
	unsigned long flags, addr, addr2;

	save_flags(flags);  
	cli();

	del_timer( &readtrack_timer );

	if (!MultReadInProgress) {
		/* This prevents a race condition that could arise if the
		 * interrupt is triggered while the calling of this timer
		 * callback function takes place. The IRQ function then has
		 * already cleared 'MultReadInProgress'  when flow of control
		 * gets here.
		 */
		restore_flags(flags);
		return;
	}

	/* get the current DMA address */
	/* ++ f.a. read twice to avoid being fooled by switcher */
	addr = 0;
	do {
		addr2 = addr;
		addr = dma_wd.dma_lo & 0xff;
		MFPDELAY();
		addr |= (dma_wd.dma_md & 0xff) << 8;
		MFPDELAY();
		if (ATARIHW_PRESENT( EXTD_DMA ))
			addr |= (st_dma_ext_dmahi & 0xffff) << 16;
		else
			addr |= (dma_wd.dma_hi & 0xff) << 16;
		MFPDELAY();
	} while(addr != addr2);
  
	if (addr >= PhysTrackBuffer + SUDT->spt*512) {
		/* already read enough data, force an FDC interrupt to stop
		 * the read operation
		 */
		SET_IRQ_HANDLER( NULL );
		restore_flags(flags);
		DPRINT(("fd_readtrack_check(): done\n"));
		FDC_WRITE( FDCREG_CMD, FDCCMD_FORCI );
		udelay(25);

		/* No error until now -- the FDC would have interrupted
		 * otherwise!
		 */
		fd_rwsec_done( 0 );
	}
	else {
		/* not yet finished, wait another tenth rotation */
		restore_flags(flags);
		DPRINT(("fd_readtrack_check(): not yet finished\n"));
		readtrack_timer.expires = jiffies + HZ/5/10;
		add_timer( &readtrack_timer );
	}
}


static void fd_rwsec_done( int status )
{
	unsigned int track;

	DPRINT(("fd_rwsec_done()\n"));

	STOP_TIMEOUT();
	
	if (read_track) {
		if (!MultReadInProgress)
			return;
		MultReadInProgress = 0;
		del_timer( &readtrack_timer );
	}

	/* Correct the track if stretch != 0 */
	if (SUDT->stretch) {
		track = FDC_READ( FDCREG_TRACK);
		MFPDELAY();
		FDC_WRITE( FDCREG_TRACK, track << SUDT->stretch);
	}

	if (!UseTrackbuffer) {
		dma_wd.dma_mode_status = 0x90;
		MFPDELAY();
		if (!(dma_wd.dma_mode_status & 0x01)) {
			printk(KERN_ERR "fd%d: DMA error\n", SelectedDrive );
			goto err_end;
		}
	}
	MFPDELAY();

	if (ReqCmd == WRITE && (status & FDCSTAT_WPROT)) {
		printk(KERN_NOTICE "fd%d: is write protected\n", SelectedDrive );
		goto err_end;
	}	
	if ((status & FDCSTAT_RECNF) &&
	    /* RECNF is no error after a multiple read when the FDC
	       searched for a non-existent sector! */
	    !(read_track && FDC_READ(FDCREG_SECTOR) > SUDT->spt)) {
		if (Probing) {
			if (SUDT > disk_type) {
			    if (SUDT[-1].blocks > ReqBlock) {
				/* try another disk type */
				SUDT--;
				floppy_sizes[SelectedDrive] = SUDT->blocks >> 1;
			    } else
				Probing = 0;
			}
			else {
				if (SUD.flags & FTD_MSG)
					printk(KERN_INFO "fd%d: Auto-detected floppy type %s\n",
					       SelectedDrive, SUDT->name );
				Probing=0;
			}
		} else {	
/* record not found, but not probing. Maybe stretch wrong ? Restart probing */
			if (SUD.autoprobe) {
				SUDT = disk_type + StartDiskType[DriveType];
				floppy_sizes[SelectedDrive] = SUDT->blocks >> 1;
				Probing = 1;
			}
		}
		if (Probing) {
			if (ATARIHW_PRESENT(FDCSPEED)) {
				dma_wd.fdc_speed = SUDT->fdc_speed;
				MFPDELAY();
			}
			setup_req_params( SelectedDrive );
			BufferDrive = -1;
			do_fd_action( SelectedDrive );
			return;
		}

		printk(KERN_ERR "fd%d: sector %d not found (side %d, track %d)\n",
		       SelectedDrive, FDC_READ (FDCREG_SECTOR), ReqSide, ReqTrack );
		goto err_end;
	}
	if (status & FDCSTAT_CRC) {
		printk(KERN_ERR "fd%d: CRC error (side %d, track %d, sector %d)\n",
		       SelectedDrive, ReqSide, ReqTrack, FDC_READ (FDCREG_SECTOR) );
		goto err_end;
	}
	if (status & FDCSTAT_LOST) {
		printk(KERN_ERR "fd%d: lost data (side %d, track %d, sector %d)\n",
		       SelectedDrive, ReqSide, ReqTrack, FDC_READ (FDCREG_SECTOR) );
		goto err_end;
	}

	Probing = 0;
	
	if (ReqCmd == READ) {
		if (!read_track) {
			void *addr;
			addr = ATARIHW_PRESENT( EXTD_DMA ) ? ReqData : DMABuffer;
			dma_cache_maintenance( VTOP(addr), 512, 0 );
			if (!ATARIHW_PRESENT( EXTD_DMA ))
				copy_buffer (addr, ReqData);
		} else {
			dma_cache_maintenance( PhysTrackBuffer, MAX_SECTORS * 512, 0 );
			BufferDrive = SelectedDrive;
			BufferSide  = ReqSide;
			BufferTrack = ReqTrack;
			copy_buffer (SECTOR_BUFFER (ReqSector), ReqData);
		}
	}
  
	if (++ReqCnt < CURRENT->current_nr_sectors) {
		/* read next sector */
		setup_req_params( SelectedDrive );
		do_fd_action( SelectedDrive );
	}
	else {
		/* all sectors finished */
		CURRENT->nr_sectors -= CURRENT->current_nr_sectors;
		CURRENT->sector += CURRENT->current_nr_sectors;
		end_request( 1 );
		redo_fd_request();
	}
	return;
  
  err_end:
	BufferDrive = -1;
	fd_error();
}


static void fd_writetrack( void )
{
	unsigned long paddr, flags;
	unsigned int track;
	
	DPRINT(("fd_writetrack() Tr=%d Si=%d\n", ReqTrack, ReqSide ));

	paddr = PhysTrackBuffer;
	dma_cache_maintenance( paddr, BUFFER_SIZE, 1 );

	fd_select_side( ReqSide );
  
	/* Cheat for track if stretch != 0 */
	if (SUDT->stretch) {
		track = FDC_READ( FDCREG_TRACK);
		MFPDELAY();
		FDC_WRITE(FDCREG_TRACK,track >> SUDT->stretch);
	}
	udelay(40);
  
	/* Setup DMA */
	save_flags(flags);  
	cli();
	dma_wd.dma_lo = (unsigned char)paddr;
	MFPDELAY();
	paddr >>= 8;
	dma_wd.dma_md = (unsigned char)paddr;
	MFPDELAY();
	paddr >>= 8;
	if (ATARIHW_PRESENT( EXTD_DMA ))
		st_dma_ext_dmahi = (unsigned short)paddr;
	else
		dma_wd.dma_hi = (unsigned char)paddr;
	MFPDELAY();
	restore_flags(flags);
  
	/* Clear FIFO and switch DMA to correct mode */  
	dma_wd.dma_mode_status = 0x190;  
	MFPDELAY();
	dma_wd.dma_mode_status = 0x90;  
	MFPDELAY();
	dma_wd.dma_mode_status = 0x190;
	MFPDELAY();
  
	/* How many sectors for DMA */
	dma_wd.fdc_acces_seccount = BUFFER_SIZE/512;
	udelay(40);  
  
	/* Start operation */
	dma_wd.dma_mode_status = FDCSELREG_STP | 0x100;
	udelay(40);
	SET_IRQ_HANDLER( fd_writetrack_done );
	dma_wd.fdc_acces_seccount = FDCCMD_WRTRA | get_head_settle_flag(); 

	MotorOn = 1;
	START_TIMEOUT();
	/* wait for interrupt */
}


static void fd_writetrack_done( int status )
{
	DPRINT(("fd_writetrack_done()\n"));

	STOP_TIMEOUT();

	if (status & FDCSTAT_WPROT) {
		printk(KERN_NOTICE "fd%d: is write protected\n", SelectedDrive );
		goto err_end;
	}	
	if (status & FDCSTAT_LOST) {
		printk(KERN_ERR "fd%d: lost data (side %d, track %d)\n",
				SelectedDrive, ReqSide, ReqTrack );
		goto err_end;
	}

	wake_up( &format_wait );
	return;

  err_end:
	fd_error();
}

static void fd_times_out( unsigned long dummy )
{
	atari_disable_irq( IRQ_MFP_FDC );
	if (!FloppyIRQHandler) goto end; /* int occurred after timer was fired, but
					  * before we came here... */

	SET_IRQ_HANDLER( NULL );
	/* If the timeout occurred while the readtrack_check timer was
	 * active, we need to cancel it, else bad things will happen */
	if (UseTrackbuffer)
		del_timer( &readtrack_timer );
	FDC_WRITE( FDCREG_CMD, FDCCMD_FORCI );
	udelay( 25 );
	
	printk(KERN_ERR "floppy timeout\n" );
	fd_error();
  end:
	atari_enable_irq( IRQ_MFP_FDC );
}


/* The (noop) seek operation here is needed to make the WP bit in the
 * FDC status register accessible for check_change. If the last disk
 * operation would have been a RDSEC, this bit would always read as 0
 * no matter what :-( To save time, the seek goes to the track we're
 * already on.
 */

static void finish_fdc( void )
{
	if (!NeedSeek) {
		finish_fdc_done( 0 );
	}
	else {
		DPRINT(("finish_fdc: dummy seek started\n"));
		FDC_WRITE (FDCREG_DATA, SUD.track);
		SET_IRQ_HANDLER( finish_fdc_done );
		FDC_WRITE (FDCREG_CMD, FDCCMD_SEEK);
		MotorOn = 1;
		START_TIMEOUT();
		/* we must wait for the IRQ here, because the ST-DMA
		   is released immediately afterwards and the interrupt
		   may be delivered to the wrong driver. */
	  }
}


static void finish_fdc_done( int dummy )
{
	unsigned long flags;

	DPRINT(("finish_fdc_done entered\n"));
	STOP_TIMEOUT();
	NeedSeek = 0;

	if ((timer_active & (1 << FLOPPY_TIMER)) &&
	    timer_table[FLOPPY_TIMER].expires < jiffies + 5)
		/* If the check for a disk change is done too early after this
		 * last seek command, the WP bit still reads wrong :-((
		 */
		timer_table[FLOPPY_TIMER].expires = jiffies + 5;
	else
		START_CHECK_CHANGE_TIMER( CHECK_CHANGE_DELAY );
	del_timer( &motor_off_timer );
	START_MOTOR_OFF_TIMER( FD_MOTOR_OFF_DELAY );

	save_flags(flags);
	cli();
	stdma_release();
	fdc_busy = 0;
	wake_up( &fdc_wait );
	restore_flags(flags);

	DPRINT(("finish_fdc() finished\n"));
}


/* Prevent "aliased" accesses. */
static int fd_ref[4] = { 0,0,0,0 };
static int fd_device[4] = { 0,0,0,0 };

/*
 * Current device number. Taken either from the block header or from the
 * format request descriptor.
 */
#define CURRENT_DEVICE (CURRENT->rq_dev)

/* Current error count. */
#define CURRENT_ERRORS (CURRENT->errors)


/* dummy for blk.h */
static void floppy_off( unsigned int nr) {}


/* The detection of disk changes is a dark chapter in Atari history :-(
 * Because the "Drive ready" signal isn't present in the Atari
 * hardware, one has to rely on the "Write Protect". This works fine,
 * as long as no write protected disks are used. TOS solves this
 * problem by introducing tri-state logic ("maybe changed") and
 * looking at the serial number in block 0. This isn't possible for
 * Linux, since the floppy driver can't make assumptions about the
 * filesystem used on the disk and thus the contents of block 0. I've
 * chosen the method to always say "The disk was changed" if it is
 * unsure whether it was. This implies that every open or mount
 * invalidates the disk buffers if you work with write protected
 * disks. But at least this is better than working with incorrect data
 * due to unrecognised disk changes.
 */

static int check_floppy_change (kdev_t dev)
{
	unsigned int drive = MINOR(dev) & 0x03;

	if (MAJOR(dev) != MAJOR_NR) {
		printk(KERN_ERR "floppy_changed: not a floppy\n");
		return 0;
	}
	
	if (test_bit (drive, &fake_change)) {
		/* simulated change (e.g. after formatting) */
		return 1;
	}
	if (test_bit (drive, &changed_floppies)) {
		/* surely changed (the WP signal changed at least once) */
		return 1;
	}
	if (UD.wpstat) {
		/* WP is on -> could be changed: to be sure, buffers should be
		 * invalidated...
		 */
		return 1;
	}

	return 0;
}

static int floppy_revalidate (kdev_t dev)
{
  int drive = MINOR(dev) & 3;

  if (test_bit (drive, &changed_floppies) || test_bit (drive, &fake_change)
      || unit[drive].disktype == 0)
    {
      if (UD.flags & FTD_MSG)
          printk (KERN_ERR "floppy: clear format %p!\n", UDT);
      BufferDrive = -1;
      clear_bit (drive, &fake_change);
      clear_bit (drive, &changed_floppies);
      /* 
       * MSch: clearing geometry makes sense only for autoprobe formats, 
       * for 'permanent user-defined' parameter: restore default_params[] 
       * here if flagged valid!
       */
      if (default_params[drive].blocks == 0)
      UDT = 0;
      else
	UDT = &default_params[drive];
    }
  return 0;
}

static __inline__ void copy_buffer(void *from, void *to)
{
	ulong	*p1 = (ulong *)from, *p2 = (ulong *)to;
	int		cnt;

	for( cnt = 512/4; cnt; cnt-- )
		*p2++ = *p1++;
}


/* This sets up the global variables describing the current request. */

static void setup_req_params( int drive )
{
	int block = ReqBlock + ReqCnt;

	ReqTrack = block / UDT->spt;
	ReqSector = block - ReqTrack * UDT->spt + 1;
	ReqSide = ReqTrack & 1;
	ReqTrack >>= 1;
	ReqData = ReqBuffer + 512 * ReqCnt;

	if (UseTrackbuffer)
		read_track = (ReqCmd == READ && CURRENT_ERRORS == 0);
	else
		read_track = 0;

	DPRINT(("Request params: Si=%d Tr=%d Se=%d Data=%08lx\n",ReqSide,
			ReqTrack, ReqSector, (unsigned long)ReqData ));
}


static void redo_fd_request(void)
{
	int device, drive, type;
  
	DPRINT(("redo_fd_request: CURRENT=%08lx CURRENT->dev=%04x CURRENT->sector=%ld\n",
		(unsigned long)CURRENT, CURRENT ? CURRENT->rq_dev : 0,
		CURRENT ? CURRENT->sector : 0 ));

	IsFormatting = 0;

	if (CURRENT && CURRENT->rq_status == RQ_INACTIVE){
		return;
	}

repeat:
    
	if (!CURRENT)
		goto the_end;

	if (MAJOR(CURRENT->rq_dev) != MAJOR_NR)
		panic(DEVICE_NAME ": request list destroyed");

	if (CURRENT->bh && !buffer_locked(CURRENT->bh))
		panic(DEVICE_NAME ": block not locked");

	device = MINOR(CURRENT_DEVICE);
	drive = device & 3;
	type = device >> 2;
	
	if (!UD.connected) {
		/* drive not connected */
		printk(KERN_ERR "Unknown Device: fd%d\n", drive );
		end_request(0);
		goto repeat;
	}
		
	if (type == 0) {
		if (!UDT) {
			Probing = 1;
			UDT = disk_type + StartDiskType[DriveType];
			floppy_sizes[drive] = UDT->blocks >> 1;
			UD.autoprobe = 1;
		}
	} 
	else {
		/* user supplied disk type */
		if (--type >= NUM_DISK_MINORS) {
			printk(KERN_WARNING "fd%d: invalid disk format", drive );
			end_request( 0 );
			goto repeat;
		}
		if (minor2disktype[type].drive_types > DriveType)  {
			printk(KERN_WARNING "fd%d: unsupported disk format", drive );
			end_request( 0 );
			goto repeat;
		}
		type = minor2disktype[type].index;
		UDT = &disk_type[type];
		floppy_sizes[drive] = UDT->blocks >> 1;
		UD.autoprobe = 0;
	}
	
	if (CURRENT->sector + 1 > UDT->blocks) {
		end_request(0);
		goto repeat;
	}

	/* stop deselect timer */
	del_timer( &motor_off_timer );
		
	ReqCnt = 0;
	ReqCmd = CURRENT->cmd;
	ReqBlock = CURRENT->sector;
	ReqBuffer = CURRENT->buffer;
	setup_req_params( drive );
	do_fd_action( drive );

	return;

  the_end:
	finish_fdc();
}


void do_fd_request(void)
{
 	unsigned long flags;

	DPRINT(("do_fd_request for pid %d\n",current->pid));
	while( fdc_busy ) sleep_on( &fdc_wait );
	fdc_busy = 1;
	stdma_lock(floppy_irq, NULL);

	atari_disable_irq( IRQ_MFP_FDC );
	save_flags(flags);	/* The request function is called with ints
	sti();				 * disabled... so must save the IPL for later */ 
	redo_fd_request();
	restore_flags(flags);
	atari_enable_irq( IRQ_MFP_FDC );
}


static int
invalidate_drive (kdev_t rdev)
{
  /* invalidate the buffer track to force a reread */
  BufferDrive = -1;
  set_bit (MINOR(rdev) & 3, &fake_change);
  check_disk_change (rdev);
  return 0;
}

static int fd_ioctl(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long param)
{
#define IOCTL_MODE_BIT 8
#define OPEN_WRITE_BIT 16
#define IOCTL_ALLOWED (filp && (filp->f_mode & IOCTL_MODE_BIT))

	int drive, type;
	kdev_t device;
	struct atari_format_descr fmt_desc;
	struct atari_disk_type *dtp;
	struct floppy_struct getprm;
	int settype;
	struct floppy_struct setprm;

	device = inode->i_rdev;
	switch (cmd) {
		RO_IOCTLS (device, param);
	}
	drive = MINOR (device);
	type  = drive >> 2;
	drive &= 3;
	switch (cmd) {
	case FDGETPRM:
	case BLKGETSIZE:
		if (type) {
			if (--type >= NUM_DISK_MINORS)
				return -ENODEV;
			if (minor2disktype[type].drive_types > DriveType)
				return -ENODEV;
			type = minor2disktype[type].index;
			dtp = &disk_type[type];
			if (UD.flags & FTD_MSG)
			    printk (KERN_ERR "floppy%d: found dtp %p name %s!\n",
			        drive, dtp, dtp->name);
		}
		else {
			if (!UDT)
				return -ENXIO;
			else
				dtp = UDT;
		}
		if (cmd == BLKGETSIZE)
			return put_user(dtp->blocks, (long *)param);

		memset((void *)&getprm, 0, sizeof(getprm));
		getprm.size = dtp->blocks;
		getprm.sect = dtp->spt;
		getprm.head = 2;
		getprm.track = dtp->blocks/dtp->spt/2;
		getprm.stretch = dtp->stretch;
		if (copy_to_user((void *)param, &getprm, sizeof(getprm)))
			return -EFAULT;
		return 0;
	case BLKRASET:
		if (!suser())
			return -EACCES;
		if (param > 0xff)
			return -EINVAL;
		read_ahead[MAJOR(inode->i_rdev)] = param;
		return 0;
	case BLKRAGET:
		return put_user(read_ahead[MAJOR(inode->i_rdev)],
				(int *) param);
	case BLKFLSBUF:
		if (!suser())
			return -EACCES;
		fsync_dev(inode->i_rdev);
		invalidate_buffers(inode->i_rdev);
		return 0;
	}
	if (!IOCTL_ALLOWED)
		return -EPERM;
	switch (cmd) {
	case FDSETPRM:
	case FDDEFPRM:
	        /* 
		 * MSch 7/96: simple 'set geometry' case: just set the
		 * 'default' device params (minor == 0).
		 * Currently, the drive geometry is cleared after each
		 * disk change and subsequent revalidate()! simple
		 * implementation of FDDEFPRM: save geometry from a
		 * FDDEFPRM call and restore it in floppy_revalidate() !
		 */

		/* get the parameters from user space */
		if (fd_ref[drive] != 1 && fd_ref[drive] != -1)
			return -EBUSY;
		if (copy_from_user(&setprm, (void *) param, sizeof(setprm)))
			return -EFAULT;
		/* 
		 * first of all: check for floppy change and revalidate, 
		 * or the next access will revalidate - and clear UDT :-(
		 */

		if (check_floppy_change(device))
		        floppy_revalidate(device);

		if (UD.flags & FTD_MSG)
		    printk (KERN_INFO "floppy%d: setting size %d spt %d str %d!\n",
			drive, setprm.size, setprm.sect, setprm.stretch);

		/* what if type > 0 here? Overwrite specified entry ? */
		if (type) {
		        /* refuse to re-set a predefined type for now */
			redo_fd_request();
			return -EINVAL;
		}

		/* 
		 * type == 0: first look for a matching entry in the type list,
		 * and set the UD.disktype field to use the perdefined entry.
		 * TODO: add user-defined format to head of autoprobe list ? 
		 * Useful to include the user-type for future autodetection!
		 */

		for (settype = 0; settype < NUM_DISK_MINORS; settype++) {
			int setidx = 0;
			if (minor2disktype[settype].drive_types > DriveType) {
				/* skip this one, invalid for drive ... */
				continue;
			}
			setidx = minor2disktype[settype].index;
			dtp = &disk_type[setidx];

			/* found matching entry ?? */
			if (   dtp->blocks  == setprm.size 
			    && dtp->spt     == setprm.sect
			    && dtp->stretch == setprm.stretch ) {
				if (UD.flags & FTD_MSG)
				    printk (KERN_INFO "floppy%d: setting %s %p!\n",
				        drive, dtp->name, dtp);
				UDT = dtp;
				floppy_sizes[drive] = UDT->blocks >> 1;

				if (cmd == FDDEFPRM) {
				  /* save settings as permanent default type */
				  default_params[drive].name    = dtp->name;
				  default_params[drive].spt     = dtp->spt;
				  default_params[drive].blocks  = dtp->blocks;
				  default_params[drive].fdc_speed = dtp->fdc_speed;
				  default_params[drive].stretch = dtp->stretch;
				}
				
				return 0;
			}

		}

		/* no matching disk type found above - setting user_params */

	       	if (cmd == FDDEFPRM) {
		  /* set permanent type */
		  dtp = &default_params[drive];
		} else
		  /* set user type (reset by disk change!) */
		  dtp = &user_params[drive];

		dtp->name   = "user format";
		dtp->blocks = setprm.size;
		dtp->spt    = setprm.sect;
		if (setprm.sect > 14) 
		  dtp->fdc_speed = 3;
		else
		  dtp->fdc_speed = 0;
		dtp->stretch = setprm.stretch;

		if (UD.flags & FTD_MSG)
		    printk (KERN_INFO "floppy%d: blk %d spt %d str %d!\n",
		        drive, dtp->blocks, dtp->spt, dtp->stretch);

		/* sanity check */
		if (!dtp || setprm.track != dtp->blocks/dtp->spt/2 
		    || setprm.head != 2) {
			redo_fd_request();
		return -EINVAL;
		}

		UDT = dtp;
		floppy_sizes[drive] = UDT->blocks >> 1;

		return 0;
	case FDMSGON:
		UD.flags |= FTD_MSG;
		return 0;
	case FDMSGOFF:
		UD.flags &= ~FTD_MSG;
		return 0;
	case FDSETEMSGTRESH:
		return -EINVAL;
	case FDFMTBEG:
		return 0;
	case FDFMTTRK:
		if (fd_ref[drive] != 1 && fd_ref[drive] != -1)
			return -EBUSY;
		if (copy_from_user(&fmt_desc, (void *) param, sizeof(fmt_desc)))
			return -EFAULT;
		return do_format(device, &fmt_desc);
	case FDCLRPRM:
		UDT = NULL;
		/* MSch: invalidate default_params */
		default_params[drive].blocks  = 0;
		floppy_sizes[drive] = MAX_DISK_SIZE;
		return invalidate_drive (device);
	case FDFMTEND:
	case FDFLUSH:
		return invalidate_drive (drive);
	}
	return -EINVAL;
}


/* Initialize the 'unit' variable for drive 'drive' */

__initfunc(static void fd_probe( int drive ))
{
	UD.connected = 0;
	UDT  = NULL;

	if (!fd_test_drive_present( drive ))
		return;

	UD.connected = 1;
	UD.track     = 0;
	switch( UserSteprate[drive] ) {
	case 2:
		UD.steprate = FDCSTEP_2;
		break;
	case 3:
		UD.steprate = FDCSTEP_3;
		break;
	case 6:
		UD.steprate = FDCSTEP_6;
		break;
	case 12:
		UD.steprate = FDCSTEP_12;
		break;
	default: /* should be -1 for "not set by user" */
		if (ATARIHW_PRESENT( FDCSPEED ) || is_medusa)
			UD.steprate = FDCSTEP_3;
		else
			UD.steprate = FDCSTEP_6;
		break;
	}
	MotorOn = 1;	/* from probe restore operation! */
}


/* This function tests the physical presence of a floppy drive (not
 * whether a disk is inserted). This is done by issuing a restore
 * command, waiting max. 2 seconds (that should be enough to move the
 * head across the whole disk) and looking at the state of the "TR00"
 * signal. This should now be raised if there is a drive connected
 * (and there is no hardware failure :-) Otherwise, the drive is
 * declared absent.
 */

__initfunc(static int fd_test_drive_present( int drive ))
{
	unsigned long timeout;
	unsigned char status;
	int ok;
	
	if (drive > 1) return( 0 );
	fd_select_drive( drive );

	/* disable interrupt temporarily */
	atari_turnoff_irq( IRQ_MFP_FDC );
	FDC_WRITE (FDCREG_TRACK, 0xff00);
	FDC_WRITE( FDCREG_CMD, FDCCMD_RESTORE | FDCCMDADD_H | FDCSTEP_6 );

	for( ok = 0, timeout = jiffies + 2*HZ+HZ/2; jiffies < timeout; ) {
		if (!(mfp.par_dt_reg & 0x20))
			break;
	}

	status = FDC_READ( FDCREG_STATUS );
	ok = (status & FDCSTAT_TR00) != 0;

	/* force interrupt to abort restore operation (FDC would try
	 * about 50 seconds!) */
	FDC_WRITE( FDCREG_CMD, FDCCMD_FORCI );
	udelay(500);
	status = FDC_READ( FDCREG_STATUS );
	udelay(20);

	if (ok) {
		/* dummy seek command to make WP bit accessible */
		FDC_WRITE( FDCREG_DATA, 0 );
		FDC_WRITE( FDCREG_CMD, FDCCMD_SEEK );
		while( mfp.par_dt_reg & 0x20 )
			;
		status = FDC_READ( FDCREG_STATUS );
	}

	atari_turnon_irq( IRQ_MFP_FDC );
	return( ok );
}


/* Look how many and which kind of drives are connected. If there are
 * floppies, additionally start the disk-change and motor-off timers.
 */

__initfunc(static void config_types( void ))
{
	int drive, cnt = 0;

	/* for probing drives, set the FDC speed to 8 MHz */
	if (ATARIHW_PRESENT(FDCSPEED))
		dma_wd.fdc_speed = 0;

	printk(KERN_INFO "Probing floppy drive(s):\n");
	for( drive = 0; drive < FD_MAX_UNITS; drive++ ) {
		fd_probe( drive );
		if (UD.connected) {
			printk(KERN_INFO "fd%d\n", drive);
			++cnt;
		}
	}

	if (FDC_READ( FDCREG_STATUS ) & FDCSTAT_BUSY) {
		/* If FDC is still busy from probing, give it another FORCI
		 * command to abort the operation. If this isn't done, the FDC
		 * will interrupt later and its IRQ line stays low, because
		 * the status register isn't read. And this will block any
		 * interrupts on this IRQ line :-(
		 */
		FDC_WRITE( FDCREG_CMD, FDCCMD_FORCI );
		udelay(500);
		FDC_READ( FDCREG_STATUS );
		udelay(20);
	}
	
	if (cnt > 0) {
		START_MOTOR_OFF_TIMER( FD_MOTOR_OFF_DELAY );
		if (cnt == 1) fd_select_drive( 0 );
		START_CHECK_CHANGE_TIMER( CHECK_CHANGE_DELAY );
	}
}

/*
 * floppy_open check for aliasing (/dev/fd0 can be the same as
 * /dev/PS0 etc), and disallows simultaneous access to the same
 * drive with different device numbers.
 */

static int floppy_open( struct inode *inode, struct file *filp )
{
  int drive, type;
  int old_dev;

  if (!filp)
    {
      DPRINT (("Weird, open called with filp=0\n"));
      return -EIO;
    }

  drive = MINOR (inode->i_rdev) & 3;
  type  = MINOR(inode->i_rdev) >> 2;
  DPRINT(("fd_open: type=%d\n",type));
  if (drive >= FD_MAX_UNITS || type > NUM_DISK_MINORS)
	return -ENXIO;

  old_dev = fd_device[drive];

  if (fd_ref[drive])
    if (old_dev != inode->i_rdev)
      return -EBUSY;

  if (fd_ref[drive] == -1 || (fd_ref[drive] && filp->f_flags & O_EXCL))
    return -EBUSY;

  if (filp->f_flags & O_EXCL)
    fd_ref[drive] = -1;
  else
    fd_ref[drive]++;

  fd_device[drive] = inode->i_rdev;

  if (old_dev && old_dev != inode->i_rdev)
    invalidate_buffers(old_dev);

  /* Allow ioctls if we have write-permissions even if read-only open */
  if (filp->f_mode & 2 || permission (inode, 2) == 0)
    filp->f_mode |= IOCTL_MODE_BIT;
  if (filp->f_mode & 2)
    filp->f_mode |= OPEN_WRITE_BIT;

  MOD_INC_USE_COUNT;

  if (filp->f_flags & O_NDELAY)
    return 0;

  if (filp->f_mode & 3) {
	  check_disk_change( inode->i_rdev );
	  if (filp->f_mode & 2) {
		  if (UD.wpstat) {
			  floppy_release(inode, filp);
			  return -EROFS;
		  }
	  }
  }

  return 0;
}


static int floppy_release( struct inode * inode, struct file * filp )
{
  int drive;

  drive = inode->i_rdev & 3;

  if (!filp || (filp->f_mode & (2 | OPEN_WRITE_BIT)))
    /* if the file is mounted OR (writable now AND writable at open
       time) Linus: Does this cover all cases? */
    block_fsync (filp, filp->f_dentry);

  if (fd_ref[drive] < 0)
    fd_ref[drive] = 0;
  else if (!fd_ref[drive]--)
    {
      printk(KERN_ERR "floppy_release with fd_ref == 0");
      fd_ref[drive] = 0;
    }

  MOD_DEC_USE_COUNT;
  return 0;
}

static struct file_operations floppy_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	fd_ioctl,		/* ioctl */
	NULL,			/* mmap */
	floppy_open,		/* open */
	floppy_release, 	/* release */
	block_fsync,		/* fsync */
	NULL,			/* fasync */
	check_floppy_change,	/* media_change */
	floppy_revalidate,	/* revalidate */
};

__initfunc(int atari_floppy_init (void))
{
	int i;

	if (register_blkdev(MAJOR_NR,"fd",&floppy_fops)) {
		printk(KERN_ERR "Unable to get major %d for floppy\n",MAJOR_NR);
		return -EBUSY;
	}

	if (UseTrackbuffer < 0)
		/* not set by user -> use default: for now, we turn
		   track buffering off for all Medusas, though it
		   could be used with ones that have a counter
		   card. But the test is too hard :-( */
		UseTrackbuffer = !is_medusa;

	/* initialize variables */
	SelectedDrive = -1;
	BufferDrive = -1;

	/* initialize check_change timer */
	timer_table[FLOPPY_TIMER].fn = check_change;
	timer_active &= ~(1 << FLOPPY_TIMER);

	DMABuffer = kmalloc(BUFFER_SIZE + 512, GFP_KERNEL | GFP_DMA);
	if (!DMABuffer) {
		printk(KERN_ERR "atari_floppy_init: cannot get dma buffer\n");
		unregister_blkdev(MAJOR_NR, "fd");
		return -ENOMEM;
	}
	TrackBuffer = DMABuffer + 512;
	PhysDMABuffer = (unsigned long) VTOP(DMABuffer);
	PhysTrackBuffer = (unsigned long) VTOP(TrackBuffer);
	BufferDrive = BufferSide = BufferTrack = -1;

	for (i = 0; i < FD_MAX_UNITS; i++) {
		unit[i].track = -1;
		unit[i].flags = 0;
	}

	for (i = 0; i < 256; i++)
		if ((i >> 2) > 0 && (i >> 2) <= NUM_DISK_MINORS) {
			int type = minor2disktype[(i >> 2) - 1].index;
			floppy_sizes[i] = disk_type[type].blocks >> 1;
		} else
			floppy_sizes[i] = MAX_DISK_SIZE;

	blk_size[MAJOR_NR] = floppy_sizes;
	blksize_size[MAJOR_NR] = floppy_blocksizes;
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;

	printk(KERN_INFO "Atari floppy driver: max. %cD, %strack buffering\n",
	       DriveType == 0 ? 'D' : DriveType == 1 ? 'H' : 'E',
	       UseTrackbuffer ? "" : "no ");
	config_types();

	return 0;
}


__initfunc(void atari_floppy_setup( char *str, int *ints ))
{
	int i;
	
	if (ints[0] < 1) {
		printk(KERN_ERR "ataflop_setup: no arguments!\n" );
		return;
	}
	else if (ints[0] > 2+FD_MAX_UNITS) {
		printk(KERN_ERR "ataflop_setup: too many arguments\n" );
	}

	if (ints[1] < 0 || ints[1] > 2)
		printk(KERN_ERR "ataflop_setup: bad drive type\n" );
	else
		DriveType = ints[1];

	if (ints[0] >= 2)
		UseTrackbuffer = (ints[2] > 0);

	for( i = 3; i <= ints[0] && i-3 < FD_MAX_UNITS; ++i ) {
		if (ints[i] != 2 && ints[i] != 3 && ints[i] != 6 && ints[i] != 12)
			printk(KERN_ERR "ataflop_setup: bad steprate\n" );
		else
			UserSteprate[i-3] = ints[i];
	}
}

#ifdef MODULE
int init_module (void)
{
	if (!MACH_IS_ATARI)
		return -ENXIO;
	return atari_floppy_init ();
}

void cleanup_module (void)
{
	unregister_blkdev(MAJOR_NR, "fd");

	blk_dev[MAJOR_NR].request_fn = 0;
	timer_active &= ~(1 << FLOPPY_TIMER);
	timer_table[FLOPPY_TIMER].fn = 0;
	kfree (DMABuffer);
}
#endif

