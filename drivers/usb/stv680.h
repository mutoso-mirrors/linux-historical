/****************************************************************************
 *
 *  Filename: stv680.h
 *
 *  Description:
 *     This is a USB driver for STV0680 based usb video cameras.
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
 ****************************************************************************/

/* size of usb transfers */
#define STV680_PACKETSIZE	4096

/* number of queued bulk transfers to use, may have problems if > 1 */
#define STV680_NUMSBUF		1

/* number of frames supported by the v4l part */
#define STV680_NUMFRAMES	2

/* scratch buffers for passing data to the decoders: 2 or 4 are good */
#define STV680_NUMSCRATCH	2

/* number of nul sized packets to receive before kicking the camera */
#define STV680_MAX_NULLPACKETS	200

/* number of decoding errors before kicking the camera */
#define STV680_MAX_ERRORS	100

#define USB_PENCAM_VENDOR_ID	0x0553
#define USB_PENCAM_PRODUCT_ID	0x0202
#define PENCAM_TIMEOUT          1000
/* fmt 4 */
#define STV_VIDEO_PALETTE    VIDEO_PALETTE_RGB24

static __devinitdata struct usb_device_id device_table[] = {
	{USB_DEVICE (USB_PENCAM_VENDOR_ID, USB_PENCAM_PRODUCT_ID)},
	{}
};
MODULE_DEVICE_TABLE (usb, device_table);

struct stv680_sbuf {
	unsigned char *data;
};

enum {
	FRAME_UNUSED,		/* Unused (no MCAPTURE) */
	FRAME_READY,		/* Ready to start grabbing */
	FRAME_GRABBING,		/* In the process of being grabbed into */
	FRAME_DONE,		/* Finished grabbing, but not been synced yet */
	FRAME_ERROR,		/* Something bad happened while processing */
};

enum {
	BUFFER_UNUSED,
	BUFFER_READY,
	BUFFER_BUSY,
	BUFFER_DONE,
};

/* raw camera data <- sbuf (urb transfer buf) */
struct stv680_scratch {
	unsigned char *data;
	volatile int state;
	int offset;
	int length;
};

/* processed data for display ends up here, after bayer */
struct stv680_frame {
	unsigned char *data;	/* Frame buffer */
	volatile int grabstate;	/* State of grabbing */
	unsigned char *curline;
	int curlinepix;
	int curpix;
};

/* this is almost the video structure uvd_t, with extra parameters for stv */
struct usb_stv {
	struct video_device vdev;

	struct usb_device *udev;

	unsigned char bulk_in_endpointAddr;	/* __u8  the address of the bulk in endpoint */
	char *camera_name;

	unsigned int VideoMode;	/* 0x0100 = VGA, 0x0000 = CIF, 0x0300 = QVGA */
	int SupportedModes;
	int CIF;
	int VGA;
	int QVGA;
	int cwidth;		/* camera width */
	int cheight;		/* camera height */
	int maxwidth;		/* max video width */
	int maxheight;		/* max video height */
	int vwidth;		/* current width for video window */
	int vheight;		/* current height for video window */
	unsigned long int rawbufsize;
	unsigned long int maxframesize;	/* rawbufsize * 3 for RGB */

	int origGain;
	int origMode;		/* original camera mode */

	struct semaphore lock;	/* to lock the structure */
	int user;		/* user count for exclusive use */
	int removed;		/* device disconnected */
	int streaming;		/* Are we streaming video? */
	char *fbuf;		/* Videodev buffer area */
	urb_t *urb[STV680_NUMSBUF];	/* # of queued bulk transfers */
	int curframe;		/* Current receiving frame */
	struct stv680_frame frame[STV680_NUMFRAMES];	/* # frames supported by v4l part */
	int readcount;
	int framecount;
	int error;
	int dropped;
	int scratch_next;
	int scratch_use;
	int scratch_overflow;
	struct stv680_scratch scratch[STV680_NUMSCRATCH];	/* for decoders */
	struct stv680_sbuf sbuf[STV680_NUMSBUF];

	unsigned int brightness;
	unsigned int chgbright;
	unsigned int whiteness;
	unsigned int colour;
	unsigned int contrast;
	unsigned int hue;
	unsigned int palette;
	unsigned int depth;	/* rgb24 in bits */

	wait_queue_head_t wq;	/* Processes waiting */

	struct proc_dir_entry *proc_entry;	/* /proc/stv680/videoX */
	int nullpackets;
};

unsigned char red[256] = {
	0, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
	21, 21, 21, 21, 21, 21, 21, 28, 34, 39, 43, 47,
	50, 53, 56, 59, 61, 64, 66, 68, 71, 73, 75, 77,
	79, 80, 82, 84, 86, 87, 89, 91, 92, 94, 95, 97,
	98, 100, 101, 102, 104, 105, 106, 108, 109, 110, 111, 113,
	114, 115, 116, 117, 118, 120, 121, 122, 123, 124, 125, 126,
	127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138,
	139, 140, 141, 142, 143, 144, 144, 145, 146, 147, 148, 149,
	150, 151, 151, 152, 153, 154, 155, 156, 156, 157, 158, 159,
	160, 160, 161, 162, 163, 164, 164, 165, 166, 167, 167, 168,
	169, 170, 170, 171, 172, 172, 173, 174, 175, 175, 176, 177,
	177, 178, 179, 179, 180, 181, 182, 182, 183, 184, 184, 185,
	186, 186, 187, 187, 188, 189, 189, 190, 191, 191, 192, 193,
	193, 194, 194, 195, 196, 196, 197, 198, 198, 199, 199, 200,
	201, 201, 202, 202, 203, 204, 204, 205, 205, 206, 206, 207,
	208, 208, 209, 209, 210, 210, 211, 212, 212, 213, 213, 214,
	214, 215, 215, 216, 217, 217, 218, 218, 219, 219, 220, 220,
	221, 221, 222, 222, 223, 224, 224, 225, 225, 226, 226, 227,
	227, 228, 228, 229, 229, 230, 230, 231, 231, 232, 232, 233,
	233, 234, 234, 235, 235, 236, 236, 237, 237, 238, 238, 239,
	239, 240, 240, 241, 241, 242, 242, 243, 243, 243, 244, 244,
	245, 245, 246, 246
};

unsigned char green[256] = {
	0, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24, 24,
	24, 24, 24, 24, 24, 24, 24, 32, 39, 45, 50, 54,
	58, 62, 65, 69, 71, 74, 77, 79, 83, 85, 87, 90,
	92, 93, 95, 98, 100, 101, 104, 106, 107, 109, 111, 113,
	114, 116, 118, 119, 121, 122, 124, 126, 127, 128, 129, 132,
	133, 134, 135, 136, 138, 140, 141, 142, 143, 145, 146, 147,
	148, 149, 150, 152, 153, 154, 155, 156, 157, 159, 160, 161,
	162, 163, 164, 166, 167, 168, 168, 169, 170, 171, 173, 174,
	175, 176, 176, 177, 179, 180, 181, 182, 182, 183, 184, 186,
	187, 187, 188, 189, 190, 191, 191, 193, 194, 195, 195, 196,
	197, 198, 198, 200, 201, 201, 202, 203, 204, 204, 205, 207,
	207, 208, 209, 209, 210, 211, 212, 212, 214, 215, 215, 216,
	217, 217, 218, 218, 219, 221, 221, 222, 223, 223, 224, 225,
	225, 226, 226, 228, 229, 229, 230, 231, 231, 232, 232, 233,
	235, 235, 236, 236, 237, 238, 238, 239, 239, 241, 241, 242,
	243, 243, 244, 244, 245, 245, 246, 248, 248, 249, 249, 250,
	250, 251, 251, 252, 253, 253, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255
};

unsigned char blue[256] = {
	0, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 31,
	31, 31, 31, 31, 31, 31, 31, 41, 50, 57, 63, 69,
	74, 78, 82, 87, 90, 94, 97, 100, 105, 108, 111, 113,
	116, 118, 121, 124, 127, 128, 131, 134, 136, 139, 140, 143,
	145, 148, 149, 150, 153, 155, 156, 159, 161, 162, 164, 167,
	168, 170, 171, 173, 174, 177, 179, 180, 182, 183, 185, 186,
	187, 189, 190, 192, 193, 195, 196, 198, 199, 201, 202, 204,
	205, 207, 208, 210, 211, 213, 213, 214, 216, 217, 219, 220,
	222, 223, 223, 224, 226, 227, 229, 230, 230, 232, 233, 235,
	236, 236, 238, 239, 241, 242, 242, 244, 245, 247, 247, 248,
	250, 251, 251, 253, 254, 254, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255,
	255, 255, 255, 255
};
