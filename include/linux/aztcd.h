/* $Id$
 * Definitions for a AztechCD268 CD-ROM interface
 *	Copyright (C) 1994, 1995  Werner Zimmermann
 *
 *	based on Mitsumi CDROM driver by Martin Harriss
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  History:	W.Zimmermann adaption to Aztech CD268-01A Version 1.3
 *		Oktober 1994 Email: zimmerma@rz.fht-esslingen.de
 *  Note:	Points marked with ??? are questionable !
 */

/* *** change this to set the I/O port address */
#define AZT_BASE_ADDR		0x320

/* Increase this if you get lots of timeouts; if you get kernel panic, replace
   STEN_LOW_WAIT by STEN_LOW in the source code */
#define AZT_STATUS_DELAY	400       /*for timer wait, STEN_LOW_WAIT*/
#define AZT_TIMEOUT		8000000   /*for busy wait STEN_LOW, DTEN_LOW*/
#define AZT_FAST_TIMEOUT	10000     /*for reading the version string*/

/* number of times to retry a command before giving up */
#define AZT_RETRY_ATTEMPTS	3

/*defines for compatibility with mcd.c/mcd.h for Mitsumi drive, will probably
  go away, when the AZTECH driver is integrated in the standard Linux kernel*/
#ifdef CONFIG_AZTCD
#else
#define AZTCD_TIMER		    MCD_TIMER
#define aztcd_init		    mcd_init
#define do_aztcd_request	    do_mcd_request
#define aztcd_setup		    mcd_setup
#define check_aztcd_media_change    check_mcd_media_change
#endif

/* port access macros */
#define CMD_PORT		azt_port
#define DATA_PORT		azt_port
#define STATUS_PORT		azt_port+1
#define MODE_PORT		azt_port+2

/* status bits */
#define AST_CMD_CHECK		0x80		/* command error */
#define AST_DSK_CHG		0x20		/* disk removed or changed */
#define AST_NOT_READY		0x02		/* no disk in the drive */
#define AST_DOOR_OPEN		0x40		/* door is open */
#define AST_MODE_BITS		0x1C		/* Mode Bits */
#define AST_INITIAL		0x0C		/* initial, only valid ... */
#define AST_BUSY		0x04		/* now playing, only valid
						   in combination with mode
						   bits */
/* flag bits */
#define AFL_DATA		0x02		/* data available if low */
#define AFL_STATUS		0x04		/* status available if low */
#define AFL_OP_OK		0x01		/* OP_OK command correct*/
#define AFL_PA_OK		0x02		/* PA_OK parameter correct*/
#define AFL_OP_ERR		0x05		/* error in command*/
#define AFL_PA_ERR		0x06		/* error in parameters*/
#define POLLED			0x04		/* polled mode */

/* commands */
#define ACMD_SOFT_RESET		0x10		/* reset drive */
#define ACMD_PLAY_READ		0x20		/* read data track in cooked mode */
#define ACMD_DATA_READ_RAW      0x21		/* reading in raw mode*/
#define ACMD_SEEK_TO_LEADIN     0x31		/* seek to leadin track*/
#define ACMD_GET_ERROR		0x40		/* get error code */
#define ACMD_GET_STATUS		0x41		/* get status */
#define ACMD_GET_Q_CHANNEL      0x50		/* read info from q channel */
#define ACMD_EJECT		0x60		/* eject/open */
#define ACMD_PAUSE		0x80		/* pause */
#define ACMD_STOP		0x81		/* stop play */
#define ACMD_PLAY_AUDIO		0x90		/* play audio track */
#define ACMD_GET_VERSION	0xA0		/* get firmware version */
#define ACMD_SET_MODE		0xA1		/* set drive mode */
#define ACMD_SET_VOLUME		0xAE		/* set audio level */

/* borrowed from hd.c */
#define SET_TIMER(func, jifs) \
	((timer_table[AZTCD_TIMER].expires = jiffies + jifs), \
	(timer_table[AZTCD_TIMER].fn = func), \
	(timer_active |= 1<<AZTCD_TIMER))

#define CLEAR_TIMER		timer_active &= ~(1<<AZTCD_TIMER)

#define MAX_TRACKS		104

struct msf {
	unsigned char	min;
	unsigned char	sec;
	unsigned char	frame;
};

struct azt_Play_msf {
	struct msf	start;
	struct msf	end;
};

struct azt_DiskInfo {
	unsigned char	first;
	unsigned char	last;
	struct msf	diskLength;
	struct msf	firstTrack;
};

struct azt_Toc {
	unsigned char	ctrl_addr;
	unsigned char	track;
	unsigned char	pointIndex;
	struct msf	trackTime;
	struct msf	diskTime;
};
