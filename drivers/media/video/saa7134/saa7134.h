/*
 * v4l2 device driver for philips saa7134 based TV cards
 *
 * (c) 2001,02 Gerd Knorr <kraxel@bytesex.org>
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
 */

#include <linux/pci.h>
#include <linux/i2c.h>
#include <linux/videodev.h>
#include <linux/kdev_t.h>

#include <media/video-buf.h>
#include <media/tuner.h>
#include <media/audiochip.h>
#include <media/id.h>

#define SAA7134_VERSION_CODE KERNEL_VERSION(0,2,2)

#ifndef TRUE
# define TRUE (1==1)
#endif
#ifndef FALSE
# define FALSE (1==0)
#endif

/* 2.4 / 2.5 driver compatibility stuff */

/* ----------------------------------------------------------- */
/* enums                                                       */

enum saa7134_tvaudio_mode {
	TVAUDIO_FM_MONO       = 1,
	TVAUDIO_FM_BG_STEREO  = 2,
	TVAUDIO_FM_SAT_STEREO = 3,
	TVAUDIO_FM_K_STEREO   = 4,
	TVAUDIO_NICAM_AM      = 5,
	TVAUDIO_NICAM_FM      = 6,
};

enum saa7134_audio_in {
	TV    = 1,
	LINE1 = 2,
	LINE2 = 3,
};

enum saa7134_video_out {
	CCIR656 = 1,
};

/* ----------------------------------------------------------- */
/* static data                                                 */

struct saa7134_tvnorm {
	char          *name;
	v4l2_std_id   id;
	int           width;
	int           height;

	/* video decoder */
	int           sync_control;
	int           luma_control;
	int           chroma_ctrl1;
	int           chroma_gain;
	int           chroma_ctrl2;

	/* video scaler */
	int           h_start;
	int           h_stop;
	int           video_v_start;
	int           video_v_stop;
	int           vbi_v_start;
	int           vbi_v_stop;
};

struct saa7134_tvaudio {
	char  *name;
	int   std;
	enum  saa7134_tvaudio_mode mode;
	int   carr1;
	int   carr2;
};

struct saa7134_format {
	char  *name;
	int   fourcc;
	int   depth;
	int   pm;
	int   vshift;   /* vertical downsampling (for planar yuv) */
	int   hshift;   /* horizontal downsampling (for planar yuv) */
	int   bswap:1;
	int   wswap:1;
	int   yuv:1;
	int   planar:1;
};

/* ----------------------------------------------------------- */
/* card configuration                                          */

#define SAA7134_BOARD_NOAUTO           -1
#define SAA7134_BOARD_UNKNOWN           0
#define SAA7134_BOARD_PROTEUS_PRO       1
#define SAA7134_BOARD_FLYVIDEO3000      2
#define SAA7134_BOARD_FLYVIDEO2000      3
#define SAA7134_BOARD_EMPRESS           4
#define SAA7134_BOARD_MONSTERTV         5
#define SAA7134_BOARD_MD9717            6
#define SAA7134_BOARD_TVSTATION_RDS     7
#define SAA7134_BOARD_CINERGY400	8
#define SAA7134_BOARD_MD5044		9
#define SAA7134_BOARD_KWORLD           10
#define SAA7134_BOARD_CINERGY600       11
#define SAA7134_BOARD_MD7134           12

#define SAA7134_INPUT_MAX 8

struct saa7134_input {
	char                    *name;
	int                     vmux;
	enum saa7134_audio_in   amux;
	int                     gpio;
	int                     tv:1;
};

struct saa7134_board {
	char                    *name;
	int                     audio_clock;

	/* input switching */
	int                     gpiomask;
	struct saa7134_input    inputs[SAA7134_INPUT_MAX];
	struct saa7134_input    radio;
	struct saa7134_input    mute;
	
	/* peripheral I/O */
	int                     i2s_rate;
	int                     has_ts;
	enum saa7134_video_out  video_out;

	/* i2c chip info */
	int                     tuner_type;
	int                     need_tda9887:1;
};

#define card_has_audio(dev)   (dev->pci->device == PCI_DEVICE_ID_PHILIPS_SAA7134)
#define card_has_radio(dev)   (NULL != saa7134_boards[dev->board].radio.name)
#define card_has_ts(dev)      (saa7134_boards[dev->board].has_ts)
#define card(dev)             (saa7134_boards[dev->board])
#define card_in(dev,n)        (saa7134_boards[dev->board].inputs[n])

/* ----------------------------------------------------------- */
/* device / file handle status                                 */

#define RESOURCE_OVERLAY       1
#define RESOURCE_VIDEO         2
#define RESOURCE_VBI           4

#define INTERLACE_AUTO         0
#define INTERLACE_ON           1
#define INTERLACE_OFF          2

#define BUFFER_TIMEOUT     (HZ/2)  /* 0.5 seconds */

struct saa7134_dev;
struct saa7134_dma;

/* saa7134 page table */
struct saa7134_pgtable {
	unsigned int               size;
	u32                        *cpu;
	dma_addr_t                 dma;
};

/* tvaudio thread status */
struct saa7134_thread {
	struct task_struct         *task;
	wait_queue_head_t          wq;
	struct semaphore           *notify;
	int                        exit;
	int                        scan1;
	int                        scan2;
};

/* buffer for one video/vbi/ts frame */
struct saa7134_buf {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer vb;

	/* saa7134 specific */
	struct saa7134_format   *fmt;
	int                     top_seen;
	int (*activate)(struct saa7134_dev *dev,
			struct saa7134_buf *buf,
			struct saa7134_buf *next);

	/* page tables */
	struct saa7134_pgtable  *pt;
};

struct saa7134_dmaqueue {
	struct saa7134_dev         *dev;
	struct saa7134_buf         *curr;
	struct list_head           queue;
	struct timer_list          timeout;
};

/* video filehandle status */
struct saa7134_fh {
	struct saa7134_dev         *dev;
	int                        radio;
	enum v4l2_buf_type         type;

	struct v4l2_window         win;
	struct v4l2_clip           clips[8];
	int                        nclips;
	int                        resources;

	/* video capture */
	struct saa7134_format      *fmt;
	int                        width,height;
	struct videobuf_queue      cap;
	struct saa7134_pgtable     pt_cap;

	/* vbi capture */
	struct videobuf_queue      vbi;
	struct saa7134_pgtable     pt_vbi;
};

/* TS status */
struct saa7134_ts {
	int                        users;

	/* TS capture */
	struct videobuf_queue      ts;
	struct saa7134_pgtable     pt_ts;
};

/* oss dsp status */
struct saa7134_oss {
        struct semaphore           lock;
	int                        minor_mixer;
	int                        minor_dsp;
	int                        users_dsp;

	/* mixer */
	enum saa7134_audio_in      input;
	int                        count;
	int                        line1;
	int                        line2;

	/* dsp */
	int                        afmt;
	int                        rate;
	int                        channels;
	int                        recording;
	int                        blocks;
	int                        blksize;
	int                        bufsize;
	struct saa7134_pgtable     pt;
	struct videobuf_dmabuf     dma;
	wait_queue_head_t          wq;
	int                        dma_blk;
	int                        read_offset;
	int                        read_count;
};

/* global device status */
struct saa7134_dev {
	struct list_head           devlist;
        struct semaphore           lock;
       	spinlock_t                 slock;

	/* various device info */
	int                        resources;
	struct video_device        video_dev;
	struct video_device        ts_dev;
	struct video_device        radio_dev;
	struct video_device        vbi_dev;
	struct saa7134_oss         oss;
	struct saa7134_ts          ts;

	/* pci i/o */
	char                       name[32];
	struct pci_dev             *pci;
	unsigned char              pci_rev,pci_lat;
	__u32                      *lmmio;
	__u8                       *bmmio;

	/* config info */
	int                        board;
	int                        tuner_type;

	/* i2c i/o */
	struct i2c_adapter         i2c_adap;
	struct i2c_client          i2c_client;
	unsigned char              eedata[64];

	/* video overlay */
	struct v4l2_framebuffer    ovbuf;
	struct saa7134_format      *ovfmt;
	int                        ovenable;
	enum v4l2_field            ovfield;

	/* video+ts+vbi capture */
	struct saa7134_dmaqueue    video_q;
	struct saa7134_dmaqueue    ts_q;
	struct saa7134_dmaqueue    vbi_q;
	int                        vbi_fieldcount;

	/* various v4l controls */
	struct saa7134_tvnorm      *tvnorm;              /* video */
	struct saa7134_tvaudio     *tvaudio;
	int                        ctl_input;
	int                        ctl_bright;
	int                        ctl_contrast;
	int                        ctl_hue;
	int                        ctl_saturation;
	int                        ctl_freq;
	int                        ctl_mute;             /* audio */
	int                        ctl_volume;
	int                        ctl_invert;           /* private */
	int                        ctl_mirror;
	int                        ctl_y_odd;
	int                        ctl_y_even;

	/* other global state info */
	int                        automute;
	struct saa7134_thread      thread;
	struct saa7134_input       *input;
	struct saa7134_input       *hw_input;
	int                        hw_mute;
};

/* ----------------------------------------------------------- */

#define saa_readl(reg)             readl(dev->lmmio + (reg))
#define saa_writel(reg,value)      writel((value), dev->lmmio + (reg));
#define saa_andorl(reg,mask,value) \
  writel((readl(dev->lmmio+(reg)) & ~(mask)) |\
  ((value) & (mask)), dev->lmmio+(reg))
#define saa_setl(reg,bit)          saa_andorl((reg),(bit),(bit))
#define saa_clearl(reg,bit)        saa_andorl((reg),(bit),0)

#define saa_readb(reg)             readb(dev->bmmio + (reg))
#define saa_writeb(reg,value)      writeb((value), dev->bmmio + (reg));
#define saa_andorb(reg,mask,value) \
  writeb((readb(dev->bmmio+(reg)) & ~(mask)) |\
  ((value) & (mask)), dev->bmmio+(reg))
#define saa_setb(reg,bit)          saa_andorb((reg),(bit),(bit))
#define saa_clearb(reg,bit)        saa_andorb((reg),(bit),0)


/* ----------------------------------------------------------- */
/* saa7134-core.c                                              */

extern struct list_head  saa7134_devlist;
extern int               saa7134_devcount;

void saa7134_print_ioctl(char *name, unsigned int cmd);
void saa7134_track_gpio(struct saa7134_dev *dev, char *msg);

#define SAA7134_PGTABLE_SIZE 4096

int saa7134_pgtable_alloc(struct pci_dev *pci, struct saa7134_pgtable *pt);
int  saa7134_pgtable_build(struct pci_dev *pci, struct saa7134_pgtable *pt,
			   struct scatterlist *list, int length,
			   int startpage);
void saa7134_pgtable_free(struct pci_dev *pci, struct saa7134_pgtable *pt);

int saa7134_buffer_count(int size, int count);
int saa7134_buffer_startpage(struct saa7134_buf *buf);
unsigned long saa7134_buffer_base(struct saa7134_buf *buf);

int saa7134_buffer_queue(struct saa7134_dev *dev, struct saa7134_dmaqueue *q,
			 struct saa7134_buf *buf);
void saa7134_buffer_finish(struct saa7134_dev *dev, struct saa7134_dmaqueue *q,
			   int state);
void saa7134_buffer_next(struct saa7134_dev *dev, struct saa7134_dmaqueue *q);
void saa7134_buffer_timeout(unsigned long data);
void saa7134_dma_free(struct saa7134_dev *dev,struct saa7134_buf *buf);

int saa7134_set_dmabits(struct saa7134_dev *dev);

/* ----------------------------------------------------------- */
/* saa7134-cards.c                                             */

extern struct saa7134_board saa7134_boards[];
extern const int saa7134_bcount;
extern struct pci_device_id __devinitdata saa7134_pci_tbl[];


/* ----------------------------------------------------------- */
/* saa7134-i2c.c                                               */

int saa7134_i2c_register(struct saa7134_dev *dev);
int saa7134_i2c_unregister(struct saa7134_dev *dev);
void saa7134_i2c_call_clients(struct saa7134_dev *dev,
			      unsigned int cmd, void *arg);


/* ----------------------------------------------------------- */
/* saa7134-video.c                                             */

extern struct video_device saa7134_video_template;
extern struct video_device saa7134_radio_template;

int saa7134_common_ioctl(struct saa7134_dev *dev,
			 unsigned int cmd, void *arg);

int saa7134_video_init(struct saa7134_dev *dev);
int saa7134_video_fini(struct saa7134_dev *dev);
void saa7134_irq_video_intl(struct saa7134_dev *dev);
void saa7134_irq_video_done(struct saa7134_dev *dev, unsigned long status);


/* ----------------------------------------------------------- */
/* saa7134-ts.c                                                */

extern struct video_device saa7134_ts_template;
int saa7134_ts_init(struct saa7134_dev *dev);
int saa7134_ts_fini(struct saa7134_dev *dev);
void saa7134_irq_ts_done(struct saa7134_dev *dev, unsigned long status);


/* ----------------------------------------------------------- */
/* saa7134-vbi.c                                               */

extern struct videobuf_queue_ops saa7134_vbi_qops;
extern struct video_device saa7134_vbi_template;

int saa7134_vbi_init(struct saa7134_dev *dev);
int saa7134_vbi_fini(struct saa7134_dev *dev);
void saa7134_irq_vbi_done(struct saa7134_dev *dev, unsigned long status);


/* ----------------------------------------------------------- */
/* saa7134-tvaudio.c                                           */

void saa7134_tvaudio_setmute(struct saa7134_dev *dev);
void saa7134_tvaudio_setinput(struct saa7134_dev *dev,
			      struct saa7134_input *in);
void saa7134_tvaudio_setvolume(struct saa7134_dev *dev, int level);
int saa7134_tvaudio_getstereo(struct saa7134_dev *dev,
			      struct saa7134_tvaudio *audio);

int saa7134_tvaudio_init(struct saa7134_dev *dev);
int saa7134_tvaudio_fini(struct saa7134_dev *dev);
int saa7134_tvaudio_do_scan(struct saa7134_dev *dev);


/* ----------------------------------------------------------- */
/* saa7134-oss.c                                               */

extern struct file_operations saa7134_dsp_fops;
extern struct file_operations saa7134_mixer_fops;

int saa7134_oss_init(struct saa7134_dev *dev);
int saa7134_oss_fini(struct saa7134_dev *dev);
void saa7134_irq_oss_done(struct saa7134_dev *dev, unsigned long status);


/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
