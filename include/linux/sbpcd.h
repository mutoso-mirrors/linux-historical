/*
 * sbpcd.h   Specify interface address and interface type here.
 */

/*
 * the definitions for the first controller can get overridden by
 * the kernel command line ("lilo boot option").
 * Examples:
 *                                 sbpcd=0x230,SoundBlaster
 *                             or
 *                                 sbpcd=0x300,LaserMate
 *                             or
 *                                 sbpcd=0x330,SPEA
 *
 * These strings are case sensitive !!!
 */

/*
 * put your CDROM port base address into CDROM_PORT
 * and specify the type of your interface in SBPRO.
 *
 * SBPRO addresses typically are 0x0230 (=0x220+0x10), 0x0250, ...
 * LASERMATE (CI-101P, WDH-7001C) addresses typically are 0x0300, 0x0310, ...
 * SPEA addresses are 0x320, 0x330, 0x340, 0x350
 * there are some soundcards on the market with 0x0630, 0x0650, ...
 *
 * example: if your SBPRO audio address is 0x220, specify 0x230.
 *
 *
 * set SBPRO to 1 for "true" SoundBlaster card
 * set SBPRO to 0 for "poor" (no sound) interface cards
 *                and for "compatible" soundcards.
 * set SBPRO to 2 for the SPEA Media FX card
 *
 * most "compatible" sound boards like Galaxy need to set SBPRO to 0 !!!
 * if SBPRO gets set wrong, the drive will get found - but any
 * data access will give errors (audio access will work).
 * The OmniCD interface card from CreativeLabs needs SBPRO 1.
 *
 * mail to emoenke@gwdg.de if your "compatible" card needs SBPRO 1
 * (currently I do not know any "compatible" with SBPRO 1)
 * then I can include better information with the next release.
 */
#if !(SBPCD_ISSUE-1) /* first (or if you have only one) interface board: */
#define CDROM_PORT 0x0230
#define SBPRO     1
#endif

/*
 * If you have a "compatible" soundcard of type "SBPRO 0" or "SBPRO 2",
 * enter your sound card's base address here if you want sbpcd to turn
 * the CD sound channels on.
 *
 * Example: #define SOUND_BASE 0x220 enables the sound card's CD channels
 *          #define SOUND_BASE 0     leaves the soundcard untouched
 */
#define SOUND_BASE 0

/* ignore the rest if you have only one interface board & driver */

#if !(SBPCD_ISSUE-2) /* second interface board: */
#define CDROM_PORT 0x0370
#define SBPRO     0
#endif
#if !(SBPCD_ISSUE-3) /* third interface board: */
#define CDROM_PORT 0x0330
#define SBPRO     0
#endif
#if !(SBPCD_ISSUE-4) /* fourth interface board: */
#define CDROM_PORT 0x0340
#define SBPRO     0
#endif

/*==========================================================================*/
/*==========================================================================*/
/*
 * nothing to change below here if you are not experimenting
 */
#ifndef _LINUX_SBPCD_H

#define _LINUX_SBPCD_H

/*==========================================================================*/
/*==========================================================================*/
/*
 * Debug output levels
 */
#define DBG_INF		1	/* necessary information */
#define DBG_BSZ		2	/* BLOCK_SIZE trace */
#define DBG_REA		3	/* "read" status trace */
#define DBG_CHK		4	/* "media check" trace */
#define DBG_TIM		5	/* datarate timer test */
#define DBG_INI		6	/* initialization trace */
#define DBG_TOC		7	/* tell TocEntry values */
#define DBG_IOC         8	/* ioctl trace */
#define DBG_STA		9	/* "ResponseStatus" trace */
#define DBG_ERR		10	/* "xx_ReadError" trace */
#define DBG_CMD		11	/* "cmd_out" trace */
#define DBG_WRN		12	/* give explanation before auto-probing */
#define DBG_MUL         13      /* multi session code test */
#define DBG_ID		14	/* "drive_id !=0" test code */
#define DBG_IOX		15	/* some special information */
#define DBG_DID		16	/* drive ID test */
#define DBG_RES		17	/* drive reset info */
#define DBG_SPI		18	/* SpinUp test */
#define DBG_IOS		19	/* ioctl trace: "subchannel" */
#define DBG_IO2		20	/* ioctl trace: general */
#define DBG_UPC		21	/* show UPC information */
#define DBG_XA 		22	/* XA mode debugging */
#define DBG_LCK		23	/* door (un)lock info */
#define DBG_SQ 		24	/* dump SubQ frame */
#define DBG_AUD		25      /* "read audio" debugging */
#define DBG_SEQ		26      /* Sequoia interface configuration trace */
#define DBG_000		27      /* unnecessary information */

/*==========================================================================*/
/*==========================================================================*/

/*
 * bits of flags_cmd_out:
 */
#define f_respo3 0x100
#define f_putcmd 0x80
#define f_respo2 0x40
#define f_lopsta 0x20
#define f_getsta 0x10
#define f_ResponseStatus 0x08
#define f_obey_p_check 0x04
#define f_bit1 0x02
#define f_wait_if_busy 0x01

/*
 * diskstate_flags:
 */
#define upc_bit 0x40
#define volume_bit 0x20
#define toc_bit 0x10
#define cd_size_bit 0x04
#define subq_bit 0x02
#define frame_size_bit 0x01

/*
 * disk states (bits of diskstate_flags):
 */
#define upc_valid (DriveStruct[d].diskstate_flags&upc_bit)
#define volume_valid (DriveStruct[d].diskstate_flags&volume_bit)
#define toc_valid (DriveStruct[d].diskstate_flags&toc_bit)
#define cd_size_valid (DriveStruct[d].diskstate_flags&cd_size_bit)
#define subq_valid (DriveStruct[d].diskstate_flags&subq_bit)
#define frame_size_valid (DriveStruct[d].diskstate_flags&frame_size_bit)


/*
 * bits of the status_byte (result of xx_ReadStatus):
 */
#define p_door_closed 0x80
#define p_caddy_in 0x40
#define p_spinning 0x20
#define p_check 0x10
#define p_busy_new 0x08
#define p_door_locked 0x04
#define p_bit_1 0x02
#define p_disk_ok 0x01
/*
 * "old" drives status result bits:
 */
#define p_caddin_old 0x40
#define p_success_old 0x08
#define p_busy_old 0x04

/*
 * used drive states:
 */
#define st_door_closed (DriveStruct[d].status_byte&p_door_closed)
#define st_caddy_in (DriveStruct[d].status_byte&p_caddy_in)
#define st_spinning (DriveStruct[d].status_byte&p_spinning)
#define st_check (DriveStruct[d].status_byte&p_check)
#define st_busy (DriveStruct[d].status_byte&p_busy_new)
#define st_door_locked (DriveStruct[d].status_byte&p_door_locked)
#define st_diskok (DriveStruct[d].status_byte&p_disk_ok)

/*
 * bits of the CDi_status register:
 */
#define s_not_result_ready 0x04  /* 0: "result ready" */
#define s_not_data_ready 0x02    /* 0: "data ready"   */
#define s_attention 0x01         /* 1: "attention required" */
/*
 * usable as:
 */
#define DRV_ATTN               ((inb(CDi_status)&s_attention)!=0)
#define DATA_READY             ((inb(CDi_status)&s_not_data_ready)==0)
#define RESULT_READY           ((inb(CDi_status)&s_not_result_ready)==0)

/*
 * drive types (firmware versions):
 */
#define drv_old 0x10    /* CR-52x family */
#define drv_199 0x11    /* <200 */
#define drv_200 0x12    /* <201 */
#define drv_201 0x13    /* <210 */
#define drv_210 0x14    /* <211 */
#define drv_211 0x15    /* <300 */
#define drv_300 0x16    /* >=300 */

#define drv_lcs 0x20    /* Longshine family */
#define drv_260 0x21    /* LCS-7260 */

#define drv_new 0x40    /* CR-56x family */
#define drv_099 0x41    /* <100 */
#define drv_100 0x42    /* >=100 */

#define old_drive (DriveStruct[d].drv_type&drv_old)
#define lcs_drive (DriveStruct[d].drv_type&drv_lcs)
#define new_drive (DriveStruct[d].drv_type&drv_new)

/*
 * audio states:
 */
#define audio_playing 2
#define audio_pausing 1

/*
 * drv_pattern, drv_options:
 */
#define speed_auto 0x80
#define speed_300 0x40
#define speed_150 0x20
#define sax_a 0x04
#define sax_xn2 0x02
#define sax_xn1 0x01

/*
 * values of cmd_type (0 else):
 */
#define READ_M1  0x01 /* "data mode 1": 2048 bytes per frame */
#define READ_M2  0x02 /* "data mode 2": 12+2048+280 bytes per frame */
#define READ_SC  0x04 /* "subchannel info": 96 bytes per frame */
#define READ_AU  0x08 /* "audio frame": 2352 bytes per frame */

/*
 * sense byte: used only if new_drive
 *                  only during cmd 09 00 xx ah al 00 00
 *
 *          values: 00
 *                  82 "raw audio" mode
 *                  xx from infobuf[0] after 85 00 00 00 00 00 00
 */

/* audio status (bin) */
#define aud_00 0x00 /* Audio status byte not supported or not valid */
#define audx11 0x0b /* Audio play operation in progress             */
#define audx12 0x0c /* Audio play operation paused                  */
#define audx13 0x0d /* Audio play operation successfully completed  */
#define audx14 0x0e /* Audio play operation stopped due to error    */
#define audx15 0x0f /* No current audio status to return            */

/* audio status (bcd) */
#define aud_11 0x11 /* Audio play operation in progress             */
#define aud_12 0x12 /* Audio play operation paused                  */
#define aud_13 0x13 /* Audio play operation successfully completed  */
#define aud_14 0x14 /* Audio play operation stopped due to error    */
#define aud_15 0x15 /* No current audio status to return            */

/*============================================================================
==============================================================================

COMMAND SET of "old" drives like CR-521, CR-522
               (the CR-562 family is different):

No.	Command			       Code
--------------------------------------------

Drive Commands:
 1	Seek				01	
 2	Read Data			02
 3	Read XA-Data			03
 4	Read Header			04
 5	Spin Up				05
 6	Spin Down			06
 7	Diagnostic			07
 8	Read UPC			08
 9	Read ISRC			09
10	Play Audio			0A
11	Play Audio MSF			0B
12	Play Audio Track/Index		0C

Status Commands:
13	Read Status			81	
14	Read Error			82
15	Read Drive Version		83
16	Mode Select			84
17	Mode Sense			85
18	Set XA Parameter		86
19	Read XA Parameter		87
20	Read Capacity			88
21	Read SUB_Q			89
22	Read Disc Code			8A
23	Read Disc Information		8B
24	Read TOC			8C
25	Pause/Resume			8D
26	Read Packet			8E
27	Read Path Check			00
 
 
all numbers (lba, msf-bin, msf-bcd, counts) to transfer high byte first

mnemo     7-byte command        #bytes response (r0...rn)
________ ____________________  ____ 

Read Status:
status:  81.                    (1)  one-byte command, gives the main
                                                          status byte
Read Error:
check1:  82 00 00 00 00 00 00.  (6)  r1: audio status

Read Packet:
check2:  8e xx 00 00 00 00 00. (xx)  gets xx bytes response, relating
                                        to commands 01 04 05 07 08 09

Play Audio:
play:    0a ll-bb-aa nn-nn-nn.  (0)  play audio, ll-bb-aa: starting block (lba),
                                                 nn-nn-nn: #blocks
Play Audio MSF:
         0b mm-ss-ff mm-ss-ff   (0)  play audio from/to

Play Audio Track/Index:
         0c ...

Pause/Resume:
pause:   8d pr 00 00 00 00 00.  (0)  pause (pr=00) 
                                     resume (pr=80) audio playing

Mode Select:
         84 00 nn-nn ??-?? 00   (0)  nn-nn: 2048 or 2340
                                     possibly defines transfer size

set_vol: 84 83 00 00 sw le 00.  (0)  sw(itch): lrxxxxxx (off=1)
                                     le(vel): min=0, max=FF, else half
				     (firmware 2.11)

Mode Sense:
get_vol: 85 03 00 00 00 00 00.  (2)  tell current audio volume setting

Read Disc Information:
tocdesc: 8b 00 00 00 00 00 00.  (6)  read the toc descriptor ("msf-bin"-format)

Read TOC:
tocent:  8c fl nn 00 00 00 00.  (8)  read toc entry #nn
                                       (fl=0:"lba"-, =2:"msf-bin"-format)

Read Capacity:
capacit: 88 00 00 00 00 00 00.  (5)  "read CD-ROM capacity"


Read Path Check:
ping:    00 00 00 00 00 00 00.  (2)  r0=AA, r1=55
                                     ("ping" if the drive is connected)

Read Drive Version:
ident:   83 00 00 00 00 00 00. (12)  gives "MATSHITAn.nn" 
                                     (n.nn = 2.01, 2.11., 3.00, ...)

Seek:
seek:    01 00 ll-bb-aa 00 00.  (0)  
seek:    01 02 mm-ss-ff 00 00.  (0)  

Read Data:
read:    02 xx-xx-xx nn-nn fl. (??)  read nn-nn blocks of 2048 bytes,
                                     starting at block xx-xx-xx  
                                     fl=0: "lba"-, =2:"msf-bcd"-coded xx-xx-xx

Read XA-Data:
read:    03 xx-xx-xx nn-nn fl. (??)  read nn-nn blocks of 2340 bytes, 
                                     starting at block xx-xx-xx
                                     fl=0: "lba"-, =2:"msf-bcd"-coded xx-xx-xx

Read SUB_Q:
         89 fl 00 00 00 00 00. (13)  r0: audio status, r4-r7: lba/msf, 
                                       fl=0: "lba", fl=2: "msf"

Read Disc Code:
         8a 00 00 00 00 00 00. (14)  possibly extended "check condition"-info

Read Header:
         04 00 ll-bb-aa 00 00.  (0)   4 bytes response with "check2"
         04 02 mm-ss-ff 00 00.  (0)   4 bytes response with "check2"

Spin Up:
         05 00 ll-bb-aa 00 00.  (0)  possibly implies a "seek"

Spin Down:
         06 ...

Diagnostic:
         07 00 ll-bb-aa 00 00.  (2)   2 bytes response with "check2"
         07 02 mm-ss-ff 00 00.  (2)   2 bytes response with "check2"

Read UPC:
         08 00 ll-bb-aa 00 00. (16)  
         08 02 mm-ss-ff 00 00. (16)  

Read ISRC:
         09 00 ll-bb-aa 00 00. (15)  15 bytes response with "check2"
         09 02 mm-ss-ff 00 00. (15)  15 bytes response with "check2"

Set XA Parameter:
         86 ...

Read XA Parameter:
         87 ...

==============================================================================
============================================================================*/

/*==========================================================================*/
/*==========================================================================*/

/*
 * highest allowed drive number (MINOR+1)
 */
#define NR_SBPCD 4

/*
 * we try to never disable interrupts - seems to work
 */
#define SBPCD_DIS_IRQ 0

/*
 * "write byte to port"
 */
#define OUT(x,y) outb(y,x)

/*
 * use "REP INSB" for strobing the data in:
 */
#define READ_DATA(port, buf, nr) insb(port, buf, nr)

/*==========================================================================*/

#define MIXER_CD_Volume	0x28 /* internal SB Pro register address */

/*==========================================================================*/
/*
 * Creative Labs Programmers did this:
 */
#define MAX_TRACKS	120 /* why more than 99? */

/*==========================================================================*/
/*
 * To make conversions easier (machine dependent!)
 */
typedef union _msf
{
  u_int n;
  u_char c[4];
}
MSF;

typedef union _blk
{
  u_int n;
  u_char c[4];
}
BLK;

/*==========================================================================*/
#endif _LINUX_SBPCD_H
