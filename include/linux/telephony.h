/*
 *              telephony.h
 *
 *              Basic Linux Telephony Interface
 *
 *              (c) Copyright 1999 Quicknet Technologies, Inc.
 *
 *              This program is free software; you can redistribute it and/or
 *              modify it under the terms of the GNU General Public License
 *              as published by the Free Software Foundation; either version
 *              2 of the License, or (at your option) any later version.
 *
 * Authors:       Ed Okerson, <eokerson@quicknet.net>
 *                Greg Herlein, <gherlein@quicknet.net>
 *
 * Contributors:  Alan Cox, <acox@redhat.com>
 *                David Erhart, <derhart@quicknet.net>
 *
 * Version:       0.1.0 - December 19, 1999
 *
 * Fixes:
 */

#ifndef TELEPHONY_H
#define TELEPHONY_H

/* vendor identification numbers */
#define PHONE_VENDOR_IXJ          1
#define PHONE_VENDOR_QUICKNET     PHONE_IXJ
#define PHONE_VENDOR_VOICETRONIX  2
#define PHONE_VENDOR_ACULAB       3
#define PHONE_VENDOR_DIGI         4
#define PHONE_VENDOR_FRANKLIN     5

/******************************************************************************
 *  Vendor Summary Information Area
 *
 *  Quicknet Technologies, Inc. - makes low density analog telephony cards
 *    with audio compression, POTS and PSTN interfaces (www.quicknet.net)
 *
 *  (other vendors following this API shuld add a short description of
 *  the telephony products they support under Linux)
 *
 *****************************************************************************/

 
/******************************************************************************
*
* The capabilities ioctls can inform you of the capabilities of each phone
* device installed in your system.  The PHONECTL_CAPABILITIES ioctl
* returns an integer value indicating the number of capabilities the   
* device has.  The PHONECTL_CAPABILITIES_LIST will fill an array of 
* capability structs with all of it's capabilities.  The
* PHONECTL_CAPABILITIES_CHECK takes a single capability struct and returns
* a TRUE if the device has that capability, otherwise it returns false.
* 
******************************************************************************/
typedef enum {
	vendor = 0,
	device,
	port,
	codec,
	dsp
} phone_cap;

struct phone_capability {
	char desc[80];
	phone_cap captype;
	int cap;
	int handle;
};

typedef enum {
	pots = 0,
	pstn,
	handset,
	speaker
} phone_ports;

#define PHONE_CAPABILITIES              _IO  ('q', 0x80)
#define PHONE_CAPABILITIES_LIST         _IOR ('q', 0x81, struct phone_capability *)
#define PHONE_CAPABILITIES_CHECK        _IOW ('q', 0x82, struct phone_capability *)

#define PHONE_RING			_IO  ('q', 0x83)
#define PHONE_HOOKSTATE			_IO  ('q', 0x84)
#define PHONE_MAXRINGS			_IOW ('q', 0x85, char)
#define PHONE_RING_CADENCE		_IOW ('q', 0x86, short)
#define PHONE_RING_START		_IO  ('q', 0x87)
#define PHONE_RING_STOP			_IO  ('q', 0x88)

#define USA_RING_CADENCE	 0xC0C0

#define PHONE_REC_CODEC			_IOW ('q', 0x89, int)
#define PHONE_REC_START			_IO  ('q', 0x8A)
#define PHONE_REC_STOP			_IO  ('q', 0x8B)
#define PHONE_REC_DEPTH			_IOW ('q', 0x8C, int)
#define PHONE_FRAME			_IOW ('q', 0x8D, int)
#define PHONE_REC_VOLUME		_IOW ('q', 0x8E, int)
#define PHONE_REC_LEVEL			_IO  ('q', 0x8F)

#define PHONE_PLAY_CODEC		_IOW ('q', 0x90, int)
#define PHONE_PLAY_START		_IO  ('q', 0x91)
#define PHONE_PLAY_STOP			_IO  ('q', 0x92)
#define PHONE_PLAY_DEPTH		_IOW ('q', 0x93, int)
#define PHONE_PLAY_VOLUME		_IOW ('q', 0x94, int)
#define PHONE_PLAY_LEVEL		_IO  ('q', 0x95)
#define PHONE_DTMF_READY		_IOR ('q', 0x96, int)
#define PHONE_GET_DTMF			_IOR ('q', 0x97, int)
#define PHONE_GET_DTMF_ASCII		_IOR ('q', 0x98, int)
#define PHONE_DTMF_OOB			_IOW ('q', 0x99, int)
#define PHONE_EXCEPTION			_IOR ('q', 0x9A, int)
#define PHONE_PLAY_TONE			_IOW ('q', 0x9B, char)
#define PHONE_SET_TONE_ON_TIME		_IOW ('q', 0x9C, int)
#define PHONE_SET_TONE_OFF_TIME		_IOW ('q', 0x9D, int)
#define PHONE_GET_TONE_ON_TIME		_IO  ('q', 0x9E)
#define PHONE_GET_TONE_OFF_TIME		_IO  ('q', 0x9F)
#define PHONE_GET_TONE_STATE		_IO  ('q', 0xA0)
#define PHONE_BUSY			_IO  ('q', 0xA1)
#define PHONE_RINGBACK			_IO  ('q', 0xA2)
#define PHONE_DIALTONE			_IO  ('q', 0xA3)
#define PHONE_CPT_STOP			_IO  ('q', 0xA4)

#define PHONE_PSTN_SET_STATE		_IOW ('q', 0xA4, int)
#define PHONE_PSTN_GET_STATE		_IO  ('q', 0xA5)

#define PSTN_ON_HOOK		0
#define PSTN_RINGING		1
#define PSTN_OFF_HOOK		2
#define PSTN_PULSE_DIAL		3

/******************************************************************************
* 
* The wink duration is tunable with this ioctl.  The default wink duration  
* is 320ms.  You do not need to use this ioctl if you do not require a
* different wink duration.
* 
******************************************************************************/
#define PHONE_WINK_DURATION		_IOW ('q', 0xA6, int)


/******************************************************************************
* 
*  Codec Definitions
* 
******************************************************************************/
typedef enum {
	G723_63 = 1,
	G723_53 = 2,
	TS85 = 3,
	TS48 = 4,
	TS41 = 5,
	G728 = 6,
	G729 = 7,
	ULAW = 8,
	ALAW = 9,
	LINEAR16 = 10,
	LINEAR8 = 11,
	WSS = 12
} phone_codec;

/******************************************************************************
*
* The exception structure allows us to multiplex multiple events onto the
* select() exception set.  If any of these flags are set select() will
* return with a positive indication on the exception set.  The dtmf_ready
* bit indicates if there is data waiting in the DTMF buffer.  The
* hookstate bit is set if there is a change in hookstate status, it does not
* indicate the current state of the hookswitch.  The pstn_ring bit
* indicates that the DAA on a LineJACK card has detected ring voltage on
* the PSTN port.  The caller_id bit indicates that caller_id data has been
* recieved and is available.  The pstn_wink bit indicates that the DAA on
* the LineJACK has recieved a wink from the telco switch.  The f0, f1, f2
* and f3 bits indicate that the filter has been triggered by detecting the
* frequency programmed into that filter.
*
* The remaining bits should be set to zero. They will become defined over time
* for other interface cards and their needs.
*
******************************************************************************/
struct phone_except
{
	unsigned int dtmf_ready:1;
	unsigned int hookstate:1;
	unsigned int pstn_ring:1;
	unsigned int caller_id:1;
	unsigned int pstn_wink:1;
	unsigned int f0:1;
	unsigned int f1:1;
	unsigned int f2:1;
	unsigned int f3:1;
	unsigned int reserved:23;
};

union telephony_exception {
	struct phone_except bits;
	unsigned int bytes;
};


#endif				/* TELEPHONY_H */
