/* $Id: isdn_audio.c,v 1.6 1996/06/06 14:43:31 fritz Exp $
 *
 * Linux ISDN subsystem, audio conversion and compression (linklevel).
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 * DTMF code (c) 1996 by Christian Mock (cm@kukuruz.ping.at)
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: isdn_audio.c,v $
 * Revision 1.6  1996/06/06 14:43:31  fritz
 * Changed to support DTMF decoding on audio playback also.
 *
 * Revision 1.5  1996/06/05 02:24:08  fritz
 * Added DTMF decoder for audio mode.
 *
 * Revision 1.4  1996/05/17 03:48:01  fritz
 * Removed some test statements.
 * Added revision string.
 *
 * Revision 1.3  1996/05/10 08:48:11  fritz
 * Corrected adpcm bugs.
 *
 * Revision 1.2  1996/04/30 09:31:17  fritz
 * General rewrite.
 *
 * Revision 1.1.1.1  1996/04/28 12:25:40  fritz
 * Taken under CVS control
 *
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/isdn.h>
#include "isdn_audio.h"
#include "isdn_common.h"

char *isdn_audio_revision        = "$Revision: 1.6 $";

/*
 * Misc. lookup-tables.
 */

/* ulaw -> signed 16-bit */
static short isdn_audio_ulaw_to_s16[] = {
        0x8284, 0x8684, 0x8a84, 0x8e84, 0x9284, 0x9684, 0x9a84, 0x9e84,
        0xa284, 0xa684, 0xaa84, 0xae84, 0xb284, 0xb684, 0xba84, 0xbe84,
        0xc184, 0xc384, 0xc584, 0xc784, 0xc984, 0xcb84, 0xcd84, 0xcf84,
        0xd184, 0xd384, 0xd584, 0xd784, 0xd984, 0xdb84, 0xdd84, 0xdf84,
        0xe104, 0xe204, 0xe304, 0xe404, 0xe504, 0xe604, 0xe704, 0xe804,
        0xe904, 0xea04, 0xeb04, 0xec04, 0xed04, 0xee04, 0xef04, 0xf004,
        0xf0c4, 0xf144, 0xf1c4, 0xf244, 0xf2c4, 0xf344, 0xf3c4, 0xf444,
        0xf4c4, 0xf544, 0xf5c4, 0xf644, 0xf6c4, 0xf744, 0xf7c4, 0xf844,
        0xf8a4, 0xf8e4, 0xf924, 0xf964, 0xf9a4, 0xf9e4, 0xfa24, 0xfa64,
        0xfaa4, 0xfae4, 0xfb24, 0xfb64, 0xfba4, 0xfbe4, 0xfc24, 0xfc64,
        0xfc94, 0xfcb4, 0xfcd4, 0xfcf4, 0xfd14, 0xfd34, 0xfd54, 0xfd74,
        0xfd94, 0xfdb4, 0xfdd4, 0xfdf4, 0xfe14, 0xfe34, 0xfe54, 0xfe74,
        0xfe8c, 0xfe9c, 0xfeac, 0xfebc, 0xfecc, 0xfedc, 0xfeec, 0xfefc,
        0xff0c, 0xff1c, 0xff2c, 0xff3c, 0xff4c, 0xff5c, 0xff6c, 0xff7c,
        0xff88, 0xff90, 0xff98, 0xffa0, 0xffa8, 0xffb0, 0xffb8, 0xffc0,
        0xffc8, 0xffd0, 0xffd8, 0xffe0, 0xffe8, 0xfff0, 0xfff8, 0x0000,
        0x7d7c, 0x797c, 0x757c, 0x717c, 0x6d7c, 0x697c, 0x657c, 0x617c,
        0x5d7c, 0x597c, 0x557c, 0x517c, 0x4d7c, 0x497c, 0x457c, 0x417c,
        0x3e7c, 0x3c7c, 0x3a7c, 0x387c, 0x367c, 0x347c, 0x327c, 0x307c,
        0x2e7c, 0x2c7c, 0x2a7c, 0x287c, 0x267c, 0x247c, 0x227c, 0x207c,
        0x1efc, 0x1dfc, 0x1cfc, 0x1bfc, 0x1afc, 0x19fc, 0x18fc, 0x17fc,
        0x16fc, 0x15fc, 0x14fc, 0x13fc, 0x12fc, 0x11fc, 0x10fc, 0x0ffc,
        0x0f3c, 0x0ebc, 0x0e3c, 0x0dbc, 0x0d3c, 0x0cbc, 0x0c3c, 0x0bbc,
        0x0b3c, 0x0abc, 0x0a3c, 0x09bc, 0x093c, 0x08bc, 0x083c, 0x07bc,
        0x075c, 0x071c, 0x06dc, 0x069c, 0x065c, 0x061c, 0x05dc, 0x059c,
        0x055c, 0x051c, 0x04dc, 0x049c, 0x045c, 0x041c, 0x03dc, 0x039c,
        0x036c, 0x034c, 0x032c, 0x030c, 0x02ec, 0x02cc, 0x02ac, 0x028c,
        0x026c, 0x024c, 0x022c, 0x020c, 0x01ec, 0x01cc, 0x01ac, 0x018c,
        0x0174, 0x0164, 0x0154, 0x0144, 0x0134, 0x0124, 0x0114, 0x0104,
        0x00f4, 0x00e4, 0x00d4, 0x00c4, 0x00b4, 0x00a4, 0x0094, 0x0084,
        0x0078, 0x0070, 0x0068, 0x0060, 0x0058, 0x0050, 0x0048, 0x0040,
        0x0038, 0x0030, 0x0028, 0x0020, 0x0018, 0x0010, 0x0008, 0x0000
};

/* alaw -> signed 16-bit */
static short isdn_audio_alaw_to_s16[] = {
        0x13fc, 0xec04, 0x0144, 0xfebc, 0x517c, 0xae84, 0x051c, 0xfae4,
        0x0a3c, 0xf5c4, 0x0048, 0xffb8, 0x287c, 0xd784, 0x028c, 0xfd74,
        0x1bfc, 0xe404, 0x01cc, 0xfe34, 0x717c, 0x8e84, 0x071c, 0xf8e4,
        0x0e3c, 0xf1c4, 0x00c4, 0xff3c, 0x387c, 0xc784, 0x039c, 0xfc64,
        0x0ffc, 0xf004, 0x0104, 0xfefc, 0x417c, 0xbe84, 0x041c, 0xfbe4,
        0x083c, 0xf7c4, 0x0008, 0xfff8, 0x207c, 0xdf84, 0x020c, 0xfdf4,
        0x17fc, 0xe804, 0x018c, 0xfe74, 0x617c, 0x9e84, 0x061c, 0xf9e4,
        0x0c3c, 0xf3c4, 0x0084, 0xff7c, 0x307c, 0xcf84, 0x030c, 0xfcf4,
        0x15fc, 0xea04, 0x0164, 0xfe9c, 0x597c, 0xa684, 0x059c, 0xfa64,
        0x0b3c, 0xf4c4, 0x0068, 0xff98, 0x2c7c, 0xd384, 0x02cc, 0xfd34,
        0x1dfc, 0xe204, 0x01ec, 0xfe14, 0x797c, 0x8684, 0x07bc, 0xf844,
        0x0f3c, 0xf0c4, 0x00e4, 0xff1c, 0x3c7c, 0xc384, 0x03dc, 0xfc24,
        0x11fc, 0xee04, 0x0124, 0xfedc, 0x497c, 0xb684, 0x049c, 0xfb64,
        0x093c, 0xf6c4, 0x0028, 0xffd8, 0x247c, 0xdb84, 0x024c, 0xfdb4,
        0x19fc, 0xe604, 0x01ac, 0xfe54, 0x697c, 0x9684, 0x069c, 0xf964,
        0x0d3c, 0xf2c4, 0x00a4, 0xff5c, 0x347c, 0xcb84, 0x034c, 0xfcb4,
        0x12fc, 0xed04, 0x0134, 0xfecc, 0x4d7c, 0xb284, 0x04dc, 0xfb24,
        0x09bc, 0xf644, 0x0038, 0xffc8, 0x267c, 0xd984, 0x026c, 0xfd94,
        0x1afc, 0xe504, 0x01ac, 0xfe54, 0x6d7c, 0x9284, 0x06dc, 0xf924,
        0x0dbc, 0xf244, 0x00b4, 0xff4c, 0x367c, 0xc984, 0x036c, 0xfc94,
        0x0f3c, 0xf0c4, 0x00f4, 0xff0c, 0x3e7c, 0xc184, 0x03dc, 0xfc24,
        0x07bc, 0xf844, 0x0008, 0xfff8, 0x1efc, 0xe104, 0x01ec, 0xfe14,
        0x16fc, 0xe904, 0x0174, 0xfe8c, 0x5d7c, 0xa284, 0x05dc, 0xfa24,
        0x0bbc, 0xf444, 0x0078, 0xff88, 0x2e7c, 0xd184, 0x02ec, 0xfd14,
        0x14fc, 0xeb04, 0x0154, 0xfeac, 0x557c, 0xaa84, 0x055c, 0xfaa4,
        0x0abc, 0xf544, 0x0058, 0xffa8, 0x2a7c, 0xd584, 0x02ac, 0xfd54,
        0x1cfc, 0xe304, 0x01cc, 0xfe34, 0x757c, 0x8a84, 0x075c, 0xf8a4,
        0x0ebc, 0xf144, 0x00d4, 0xff2c, 0x3a7c, 0xc584, 0x039c, 0xfc64,
        0x10fc, 0xef04, 0x0114, 0xfeec, 0x457c, 0xba84, 0x045c, 0xfba4,
        0x08bc, 0xf744, 0x0018, 0xffe8, 0x227c, 0xdd84, 0x022c, 0xfdd4,
        0x18fc, 0xe704, 0x018c, 0xfe74, 0x657c, 0x9a84, 0x065c, 0xf9a4,
        0x0cbc, 0xf344, 0x0094, 0xff6c, 0x327c, 0xcd84, 0x032c, 0xfcd4
};

/* alaw -> ulaw */
static char isdn_audio_alaw_to_ulaw[] = {
        0xab, 0x2b, 0xe3, 0x63, 0x8b, 0x0b, 0xc9, 0x49,
        0xba, 0x3a, 0xf6, 0x76, 0x9b, 0x1b, 0xd7, 0x57,
        0xa3, 0x23, 0xdd, 0x5d, 0x83, 0x03, 0xc1, 0x41,
        0xb2, 0x32, 0xeb, 0x6b, 0x93, 0x13, 0xcf, 0x4f,
        0xaf, 0x2f, 0xe7, 0x67, 0x8f, 0x0f, 0xcd, 0x4d,
        0xbe, 0x3e, 0xfe, 0x7e, 0x9f, 0x1f, 0xdb, 0x5b,
        0xa7, 0x27, 0xdf, 0x5f, 0x87, 0x07, 0xc5, 0x45,
        0xb6, 0x36, 0xef, 0x6f, 0x97, 0x17, 0xd3, 0x53,
        0xa9, 0x29, 0xe1, 0x61, 0x89, 0x09, 0xc7, 0x47,
        0xb8, 0x38, 0xf2, 0x72, 0x99, 0x19, 0xd5, 0x55,
        0xa1, 0x21, 0xdc, 0x5c, 0x81, 0x01, 0xbf, 0x3f,
        0xb0, 0x30, 0xe9, 0x69, 0x91, 0x11, 0xce, 0x4e,
        0xad, 0x2d, 0xe5, 0x65, 0x8d, 0x0d, 0xcb, 0x4b,
        0xbc, 0x3c, 0xfa, 0x7a, 0x9d, 0x1d, 0xd9, 0x59,
        0xa5, 0x25, 0xde, 0x5e, 0x85, 0x05, 0xc3, 0x43,
        0xb4, 0x34, 0xed, 0x6d, 0x95, 0x15, 0xd1, 0x51,
        0xac, 0x2c, 0xe4, 0x64, 0x8c, 0x0c, 0xca, 0x4a,
        0xbb, 0x3b, 0xf8, 0x78, 0x9c, 0x1c, 0xd8, 0x58,
        0xa4, 0x24, 0xde, 0x5e, 0x84, 0x04, 0xc2, 0x42,
        0xb3, 0x33, 0xec, 0x6c, 0x94, 0x14, 0xd0, 0x50,
        0xb0, 0x30, 0xe8, 0x68, 0x90, 0x10, 0xce, 0x4e,
        0xbf, 0x3f, 0xfe, 0x7e, 0xa0, 0x20, 0xdc, 0x5c,
        0xa8, 0x28, 0xe0, 0x60, 0x88, 0x08, 0xc6, 0x46,
        0xb7, 0x37, 0xf0, 0x70, 0x98, 0x18, 0xd4, 0x54,
        0xaa, 0x2a, 0xe2, 0x62, 0x8a, 0x0a, 0xc8, 0x48,
        0xb9, 0x39, 0xf4, 0x74, 0x9a, 0x1a, 0xd6, 0x56,
        0xa2, 0x22, 0xdd, 0x5d, 0x82, 0x02, 0xc0, 0x40,
        0xb1, 0x31, 0xea, 0x6a, 0x92, 0x12, 0xcf, 0x4f,
        0xae, 0x2e, 0xe6, 0x66, 0x8e, 0x0e, 0xcc, 0x4c,
        0xbd, 0x3d, 0xfc, 0x7c, 0x9e, 0x1e, 0xda, 0x5a,
        0xa6, 0x26, 0xdf, 0x5f, 0x86, 0x06, 0xc4, 0x44,
        0xb5, 0x35, 0xee, 0x6e, 0x96, 0x16, 0xd2, 0x52
};

/* ulaw -> alaw */
static char isdn_audio_ulaw_to_alaw[] = {
        0xab, 0x55, 0xd5, 0x15, 0x95, 0x75, 0xf5, 0x35,
        0xb5, 0x45, 0xc5, 0x05, 0x85, 0x65, 0xe5, 0x25,
        0xa5, 0x5d, 0xdd, 0x1d, 0x9d, 0x7d, 0xfd, 0x3d,
        0xbd, 0x4d, 0xcd, 0x0d, 0x8d, 0x6d, 0xed, 0x2d,
        0xad, 0x51, 0xd1, 0x11, 0x91, 0x71, 0xf1, 0x31,
        0xb1, 0x41, 0xc1, 0x01, 0x81, 0x61, 0xe1, 0x21,
        0x59, 0xd9, 0x19, 0x99, 0x79, 0xf9, 0x39, 0xb9,
        0x49, 0xc9, 0x09, 0x89, 0x69, 0xe9, 0x29, 0xa9,
        0xd7, 0x17, 0x97, 0x77, 0xf7, 0x37, 0xb7, 0x47,
        0xc7, 0x07, 0x87, 0x67, 0xe7, 0x27, 0xa7, 0xdf,
        0x9f, 0x7f, 0xff, 0x3f, 0xbf, 0x4f, 0xcf, 0x0f,
        0x8f, 0x6f, 0xef, 0x2f, 0x53, 0x13, 0x73, 0x33,
        0xb3, 0x43, 0xc3, 0x03, 0x83, 0x63, 0xe3, 0x23,
        0xa3, 0x5b, 0xdb, 0x1b, 0x9b, 0x7b, 0xfb, 0x3b,
        0xbb, 0xbb, 0x4b, 0x4b, 0xcb, 0xcb, 0x0b, 0x0b,
        0x8b, 0x8b, 0x6b, 0x6b, 0xeb, 0xeb, 0x2b, 0x2b,
        0xab, 0x54, 0xd4, 0x14, 0x94, 0x74, 0xf4, 0x34,
        0xb4, 0x44, 0xc4, 0x04, 0x84, 0x64, 0xe4, 0x24,
        0xa4, 0x5c, 0xdc, 0x1c, 0x9c, 0x7c, 0xfc, 0x3c,
        0xbc, 0x4c, 0xcc, 0x0c, 0x8c, 0x6c, 0xec, 0x2c,
        0xac, 0x50, 0xd0, 0x10, 0x90, 0x70, 0xf0, 0x30,
        0xb0, 0x40, 0xc0, 0x00, 0x80, 0x60, 0xe0, 0x20,
        0x58, 0xd8, 0x18, 0x98, 0x78, 0xf8, 0x38, 0xb8,
        0x48, 0xc8, 0x08, 0x88, 0x68, 0xe8, 0x28, 0xa8,
        0xd6, 0x16, 0x96, 0x76, 0xf6, 0x36, 0xb6, 0x46,
        0xc6, 0x06, 0x86, 0x66, 0xe6, 0x26, 0xa6, 0xde,
        0x9e, 0x7e, 0xfe, 0x3e, 0xbe, 0x4e, 0xce, 0x0e,
        0x8e, 0x6e, 0xee, 0x2e, 0x52, 0x12, 0x72, 0x32,
        0xb2, 0x42, 0xc2, 0x02, 0x82, 0x62, 0xe2, 0x22,
        0xa2, 0x5a, 0xda, 0x1a, 0x9a, 0x7a, 0xfa, 0x3a,
        0xba, 0xba, 0x4a, 0x4a, 0xca, 0xca, 0x0a, 0x0a,
        0x8a, 0x8a, 0x6a, 0x6a, 0xea, 0xea, 0x2a, 0x2a
};

#define NCOEFF           16     /* number of frequencies to be analyzed       */
#define DTMF_TRESH    50000     /* above this is dtmf                         */
#define SILENCE_TRESH   100     /* below this is silence                      */
#define H2_TRESH      10000     /* 2nd harmonic                               */
#define AMP_BITS          9     /* bits per sample, reduced to avoid overflow */
#define LOGRP             0
#define HIGRP             1

typedef struct {
        int grp;        /* low/high group     */
        int k;          /* k                  */
        int k2;         /* k fuer 2. harmonic */
} dtmf_t;

/* For DTMF recognition:
 * 2 * cos(2 * PI * k / N) precalculated for all k
 */
static int cos2pik[NCOEFF] = {
        55812,  29528, 53603,  24032, 51193,  14443, 48590,   6517,
        38113, -21204, 33057, -32186, 25889, -45081, 18332, -55279
};

static dtmf_t dtmf_tones[8] = {
        { LOGRP,  0,  1 }, /*  697 Hz */
        { LOGRP,  2,  3 }, /*  770 Hz */
        { LOGRP,  4,  5 }, /*  852 Hz */
        { LOGRP,  6,  7 }, /*  941 Hz */
        { HIGRP,  8,  9 }, /* 1209 Hz */
        { HIGRP, 10, 11 }, /* 1336 Hz */
        { HIGRP, 12, 13 }, /* 1477 Hz */
        { HIGRP, 14, 15 }  /* 1633 Hz */
};

static char dtmf_matrix[4][4] = {
        {'1', '2', '3', 'A'},
        {'4', '5', '6', 'B'},
        {'7', '8', '9', 'C'},
        {'*', '0', '#', 'D'}
};

#if ((CPU == 386) || (CPU == 486) || (CPU == 586))
static inline void
isdn_audio_tlookup(const void *table, void *buff, unsigned long n)
{
        __asm__("cld\n"
                "1:\tlodsb\n\t"
                "xlatb\n\t"
                "stosb\n\t"
                "loop 1b\n\t"
                ::"b" ((long)table), "c" (n), "D" ((long)buff), "S" ((long)buff)
                :"bx","cx","di","si","ax");
}
#else
static inline void
isdn_audio_tlookup(const char *table, char *buff, unsigned long n)
{
        while (n--) 
                *buff++ = table[*buff];
}
#endif

void
isdn_audio_ulaw2alaw(unsigned char *buff, unsigned long len)
{
        isdn_audio_tlookup(isdn_audio_ulaw_to_alaw, buff, len);
}

void
isdn_audio_alaw2ulaw(unsigned char *buff, unsigned long len)
{
        isdn_audio_tlookup(isdn_audio_alaw_to_ulaw, buff, len);
}

/*
 * linear <-> adpcm conversion stuff
 * Most parts from the mgetty-package.
 * (C) by Gert Doering and Klaus Weidner
 * Used by permission of Gert Doering
 */


#define ZEROTRAP    /* turn on the trap as per the MIL-STD */
#undef ZEROTRAP
#define BIAS 0x84   /* define the add-in bias for 16 bit samples */
#define CLIP 32635

static unsigned char
isdn_audio_linear2ulaw(int sample) {
        static int exp_lut[256] = {
                0,0,1,1,2,2,2,2,3,3,3,3,3,3,3,3,
                4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,4,
                5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,5,
                6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,6,
                7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,
                7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7
        };
        int sign, exponent, mantissa;
        unsigned char ulawbyte;

        /* Get the sample into sign-magnitude. */
        sign = (sample >> 8) & 0x80;      /* set aside the sign  */
        if(sign != 0) sample = -sample;   /* get magnitude       */
        if(sample > CLIP) sample = CLIP;  /* clip the magnitude  */

        /* Convert from 16 bit linear to ulaw. */
        sample = sample + BIAS;
        exponent = exp_lut[( sample >> 7 ) & 0xFF];
        mantissa = (sample >> (exponent + 3)) & 0x0F;
        ulawbyte = ~(sign | (exponent << 4) | mantissa);
#ifdef ZEROTRAP
        /* optional CCITT trap */
        if (ulawbyte == 0) ulawbyte = 0x02;
#endif
        return(ulawbyte);
}


static int Mx[3][8] = {
        { 0x3800, 0x5600, 0,0,0,0,0,0 },
        { 0x399a, 0x3a9f, 0x4d14, 0x6607, 0,0,0,0 },
        { 0x3556, 0x3556, 0x399A, 0x3A9F, 0x4200, 0x4D14, 0x6607, 0x6607 },
};

static int bitmask[9] = {
        0, 0x01, 0x03, 0x07, 0x0f, 0x1f, 0x3f, 0x7f, 0xff
}; 

static int
isdn_audio_get_bits (adpcm_state *s, unsigned char **in, int *len)
{
        while( s->nleft < s->nbits) {
                int d = *((*in)++);
                (*len)--;
                s->word = (s->word << 8) | d;
                s->nleft += 8;
        }
        s->nleft -= s->nbits;
        return (s->word >> s->nleft) & bitmask[s->nbits];
}

static void
isdn_audio_put_bits (int data, int nbits, adpcm_state *s,
                     unsigned char **out, int *len)
{
        s->word = (s->word << nbits) | (data & bitmask[nbits]);
        s->nleft += nbits;
        while(s->nleft >= 8) {
                int d = (s->word >> (s->nleft-8));
                *(out[0]++) = d & 255;
                (*len)++;
                s->nleft -= 8;
        }
}

adpcm_state *
isdn_audio_adpcm_init(adpcm_state *s, int nbits)
{
        if (!s)
                s = (adpcm_state *) kmalloc(sizeof(adpcm_state), GFP_ATOMIC);
        if (s) {
                s->a     = 0;
                s->d     = 5;
                s->word  = 0;
                s->nleft = 0;
                s->nbits = nbits;
        }
        return s;
}

dtmf_state *
isdn_audio_dtmf_init(dtmf_state *s)
{
        if (!s)
                s = (dtmf_state *) kmalloc(sizeof(dtmf_state), GFP_ATOMIC);
        if (s) {
                s->idx   = 0;
                s->last  = ' ';
        }
        return s;
}

/*
 * Decompression of adpcm data to a/u-law
 *
 */
 
int
isdn_audio_adpcm2xlaw (adpcm_state *s, int fmt, unsigned char *in,
                      unsigned char *out, int len)
{
        int a = s->a;
        int d = s->d;
        int nbits = s->nbits;
        int olen = 0;
        
        while (len) {
                int e = isdn_audio_get_bits(s, &in, &len);
                int sign;

                if (nbits == 4 && e == 0)
                        d = 4;
                sign = (e >> (nbits-1))?-1:1;
                e &= bitmask[nbits-1];
                a += sign * ((e << 1) + 1) * d >> 1;
                if (d & 1)
                        a++;
                if (fmt)
                        *out++ = isdn_audio_ulaw_to_alaw[
                                 isdn_audio_linear2ulaw(a << 2)];
                else
                        *out++ = isdn_audio_linear2ulaw(a << 2);
                olen++;
                d = (d * Mx[nbits-2][ e ] + 0x2000) >> 14;
                if ( d < 5 )
                        d = 5;     
        }
        s->a = a;
        s->d = d;
        return olen;
}

int
isdn_audio_2adpcm_flush (adpcm_state *s, unsigned char *out)
{
	int olen = 0;

        if (s->nleft)
                isdn_audio_put_bits(0, 8-s->nleft, s, &out, &olen);
        return olen;
}

int
isdn_audio_xlaw2adpcm (adpcm_state *s, int fmt, unsigned char *in,
                      unsigned char *out, int len)
{
        int a = s->a;
        int d = s->d;
        int nbits = s->nbits;
        int olen = 0;

        while (len--) {
                int e = 0, nmax = 1 << (nbits - 1);
                int sign, delta;
              
                if (fmt)
                        delta = (isdn_audio_alaw_to_s16[*in++] >> 2) - a;
                else
                        delta = (isdn_audio_ulaw_to_s16[*in++] >> 2) - a;
                if (delta < 0) {
                        e = nmax;
                        delta = -delta;
                }
                while( --nmax && delta > d ) {
                        delta -= d;
                        e++;
                }
                if (nbits == 4 && ((e & 0x0f) == 0))
                        e = 8;
                isdn_audio_put_bits(e, nbits, s, &out, &olen);
                sign = (e >> (nbits-1))?-1:1 ;
                e &= bitmask[nbits-1];
                
                a += sign * ((e << 1) + 1) * d >> 1;
                if (d & 1)
                        a++;
                d = (d * Mx[nbits-2][ e ] + 0x2000) >> 14;
                if (d < 5)
                        d=5;
        }
	s->a = a;
	s->d = d;
        return olen;
}

/*
 * Goertzel algorithm.
 * See http://ptolemy.eecs.berkeley.edu/~pino/Ptolemy/papers/96/dtmf_ict/
 * for more info.
 * Result is stored into an sk_buff and queued up for later
 * evaluation.
 */
void
isdn_audio_goertzel(int *sample, modem_info *info) {
        int sk, sk1, sk2;
        int k, n;
        struct sk_buff *skb;
        int *result;

        skb = dev_alloc_skb(sizeof(int) * NCOEFF);
        if (!skb) {
                printk(KERN_WARNING
                       "isdn_audio: Could not alloc DTMF result for ttyI%d\n",
                       info->line);
                return;
        }
        result = (int *)skb_put(skb, sizeof(int) * NCOEFF);
        skb->free = 1;
        skb->users = 0;
        for (k = 0; k < NCOEFF; k++) {
                sk = sk1 = sk2 = 0;
                for (n = 0; n < DTMF_NPOINTS; n++) {
                        sk = sample[n] + ((cos2pik[k] * sk1) >> 15) - sk2;
                        sk2 = sk1;
                        sk1 = sk;
                }
                result[k] =
                        ((sk * sk) >> AMP_BITS) - 
                        ((((cos2pik[k] * sk) >> 15) * sk2) >> AMP_BITS) +
                        ((sk2 * sk2) >> AMP_BITS);
        }
        skb_queue_tail(&info->dtmf_queue, skb);
        isdn_timer_ctrl(ISDN_TIMER_MODEMREAD, 1);
}

void
isdn_audio_eval_dtmf(modem_info *info)
{
        struct sk_buff *skb;
        int *result;
        dtmf_state *s;
        int silence;
        int i;
        int di;
        int ch;
        unsigned long flags;
        int grp[2];
        char what;
        char *p;

        while ((skb = skb_dequeue(&info->dtmf_queue))) {
                result = (int *)skb->data;
                s = info->dtmf_state;
                grp[LOGRP] = grp[HIGRP] = -2;
                silence = 0;
                for(i = 0; i < 8; i++) {
                        if ((result[dtmf_tones[i].k] > DTMF_TRESH) &&
                            (result[dtmf_tones[i].k2] < H2_TRESH)    )
                                grp[dtmf_tones[i].grp] = (grp[dtmf_tones[i].grp] == -2)?i:-1;
                        else
                                if ((result[dtmf_tones[i].k] < SILENCE_TRESH) &&
                                    (result[dtmf_tones[i].k2] < SILENCE_TRESH)  )
                                        silence++;
                }
                if(silence == 8)
                        what = ' ';
                else {
                        if((grp[LOGRP] >= 0) && (grp[HIGRP] >= 0)) {
                                what = dtmf_matrix[grp[LOGRP]][grp[HIGRP] - 4];
                                if(s->last != ' ' && s->last != '.')
                                        s->last = what;  /* min. 1 non-DTMF between DTMF */
                        } else
                                what = '.';
                }
                if ((what != s->last) && (what != ' ') && (what != '.')) {
                        printk(KERN_DEBUG "dtmf: tt='%c'\n", what);
                        p = skb->data;
                        *p++ = 0x10;
                        *p = what;
                        skb_trim(skb, 2);
                        save_flags(flags);
                        cli();
                        di = info->isdn_driver;
                        ch = info->isdn_channel;
                        __skb_queue_tail(&dev->drv[di]->rpqueue[ch], skb);
                        dev->drv[di]->rcvcount[ch] += 2;
                        restore_flags(flags);
                        /* Schedule dequeuing */
                        if ((dev->modempoll) && (info->rcvsched))
                                isdn_timer_ctrl(ISDN_TIMER_MODEMREAD, 1);
                        wake_up_interruptible(&dev->drv[di]->rcv_waitq[ch]);
                } else
                        kfree_skb(skb, FREE_READ);
                s->last = what;
        }
}

/*
 * Decode DTMF tones, queue result in separate sk_buf for
 * later examination.
 * Parameters:
 *   s    = pointer to state-struct.
 *   buf  = input audio data
 *   len  = size of audio data.
 *   fmt  = audio data format (0 = ulaw, 1 = alaw)
 */
void
isdn_audio_calc_dtmf(modem_info *info, unsigned char *buf, int len, int fmt)
{
        dtmf_state *s = info->dtmf_state;
        int i;
        int c;

        while (len) {
                c = MIN(len, (DTMF_NPOINTS - s->idx));
                if (c <= 0)
                        break;
                for (i = 0; i < c; i++) {
                        if (fmt)
                                s->buf[s->idx++] =
                                        isdn_audio_alaw_to_s16[*buf++] >> (15 - AMP_BITS);
                        else
                                s->buf[s->idx++] =
                                        isdn_audio_ulaw_to_s16[*buf++] >> (15 - AMP_BITS);
                }
                if (s->idx == DTMF_NPOINTS) {
                        isdn_audio_goertzel(s->buf, info);
                        s->idx = 0;
                }
                len -= c;
        }
}


