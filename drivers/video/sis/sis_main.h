#ifndef _SISFB_MAIN
#define _SISFB_MAIN

/* Comments and changes marked with "TW" by Thomas Winischhofer <tw@webit.com> */

/* ------------------- Constant Definitions ------------------------- */

#undef LINUXBIOS   /* turn on when use LINUXBIOS */
#define AGPOFF     /* default is turn off AGP */

#define VER_MAJOR                 1
#define VER_MINOR                 4
#define VER_LEVEL                 1

/* TW: To be included in pci_ids.h */
#ifndef PCI_DEVICE_ID_SI_650_VGA
#define PCI_DEVICE_ID_SI_650_VGA  0x6325
#endif
#ifndef PCI_DEVICE_ID_SI_650
#define PCI_DEVICE_ID_SI_650      0x0650
#endif
/* TW end */

#define MAX_ROM_SCAN              0x10000

#define TURBO_QUEUE_CAP           0x80
#define HW_CURSOR_CAP             0x40
#define AGP_CMD_QUEUE_CAP         0x80
#define VM_CMD_QUEUE_CAP          0x20

/* For 300 series */
#ifdef CONFIG_FB_SIS_300
#define TURBO_QUEUE_AREA_SIZE     0x80000 /* 512K */
#endif

/* For 315 series */
#ifdef CONFIG_FB_SIS_315
#define COMMAND_QUEUE_AREA_SIZE   0x80000 /* 512K */
#define COMMAND_QUEUE_THRESHOLD   0x1F
#endif

/* TW */
#define HW_CURSOR_AREA_SIZE_315   0x4000  /* 16K */
#define HW_CURSOR_AREA_SIZE_300   0x1000  /* 4K */

#define OH_ALLOC_SIZE             4000
#define SENTINEL                  0x7fffffff

#define SEQ_ADR                   0x14
#define SEQ_DATA                  0x15
#define DAC_ADR                   0x18
#define DAC_DATA                  0x19
#define CRTC_ADR                  0x24
#define CRTC_DATA                 0x25
#define DAC2_ADR                  (0x16-0x30)
#define DAC2_DATA                 (0x17-0x30)
#define VB_PART1_ADR              (0x04-0x30)
#define VB_PART1_DATA             (0x05-0x30)
#define VB_PART2_ADR              (0x10-0x30)
#define VB_PART2_DATA             (0x11-0x30)
#define VB_PART3_ADR              (0x12-0x30)
#define VB_PART3_DATA             (0x13-0x30)
#define VB_PART4_ADR              (0x14-0x30)
#define VB_PART4_DATA             (0x15-0x30)

#define IND_SIS_PASSWORD          0x05  /* SRs */
#define IND_SIS_COLOR_MODE        0x06
#define IND_SIS_RAMDAC_CONTROL    0x07
#define IND_SIS_DRAM_SIZE         0x14
#define IND_SIS_SCRATCH_REG_16    0x16
#define IND_SIS_SCRATCH_REG_17    0x17
#define IND_SIS_SCRATCH_REG_1A    0x1A
#define IND_SIS_MODULE_ENABLE     0x1E
#define IND_SIS_PCI_ADDRESS_SET   0x20
#define IND_SIS_TURBOQUEUE_ADR    0x26
#define IND_SIS_TURBOQUEUE_SET    0x27
#define IND_SIS_POWER_ON_TRAP     0x38
#define IND_SIS_POWER_ON_TRAP2    0x39
#define IND_SIS_CMDQUEUE_SET      0x26
#define IND_SIS_CMDQUEUE_THRESHOLD  0x27

#define IND_SIS_SCRATCH_REG_CR30  0x30  /* CRs */
#define IND_SIS_SCRATCH_REG_CR31  0x31
#define IND_SIS_SCRATCH_REG_CR32  0x32
#define IND_SIS_SCRATCH_REG_CR33  0x33
#define IND_SIS_LCD_PANEL         0x36
#define IND_SIS_SCRATCH_REG_CR37  0x37
#define IND_SIS_AGP_IO_PAD        0x48

#define IND_BRI_DRAM_STATUS       0x63 /* PCI config memory size offset */

#define MMIO_QUEUE_PHYBASE        0x85C0
#define MMIO_QUEUE_WRITEPORT      0x85C4
#define MMIO_QUEUE_READPORT       0x85C8

/* Eden Chen; TW */
#define IND_SIS_CRT2_WRITE_ENABLE_300 0x24
#define IND_SIS_CRT2_WRITE_ENABLE_315 0x2F
/* ~Eden Chen; TW */

#define SIS_PASSWORD              0x86  /* SR05 */
#define SIS_INTERLACED_MODE       0x20  /* SR06 */
#define SIS_8BPP_COLOR_MODE       0x0 
#define SIS_15BPP_COLOR_MODE      0x1 
#define SIS_16BPP_COLOR_MODE      0x2 
#define SIS_32BPP_COLOR_MODE      0x4 
#define SIS_DRAM_SIZE_MASK        0x3F  /* SR14 */
#define SIS_DRAM_SIZE_1MB         0x00
#define SIS_DRAM_SIZE_2MB         0x01
#define SIS_DRAM_SIZE_4MB         0x03
#define SIS_DRAM_SIZE_8MB         0x07
#define SIS_DRAM_SIZE_16MB        0x0F
#define SIS_DRAM_SIZE_32MB        0x1F
#define SIS_DRAM_SIZE_64MB        0x3F
#define SIS_DATA_BUS_MASK         0xC0
#define SIS_DATA_BUS_32           0x00
#define SIS_DATA_BUS_64           0x01
#define SIS_DATA_BUS_128          0x02

#define SIS315_DRAM_SIZE_MASK     0xF0  /* 315 SR14 */
#define SIS315_DRAM_SIZE_2MB      0x01
#define SIS315_DRAM_SIZE_4MB      0x02
#define SIS315_DRAM_SIZE_8MB      0x03
#define SIS315_DRAM_SIZE_16MB     0x04
#define SIS315_DRAM_SIZE_32MB     0x05
#define SIS315_DRAM_SIZE_64MB     0x06
#define SIS315_DRAM_SIZE_128MB    0x07
#define SIS315_DATA_BUS_MASK      0x02
#define SIS315_DATA_BUS_64        0x00
#define SIS315_DATA_BUS_128       0x01
#define SIS315_DUAL_CHANNEL_MASK  0x0C
#define SIS315_SINGLE_CHANNEL_1_RANK  	0x0
#define SIS315_SINGLE_CHANNEL_2_RANK  	0x1
#define SIS315_ASYM_DDR		  	0x02
#define SIS315_DUAL_CHANNEL_1_RANK    	0x3

#define SIS550_DRAM_SIZE_MASK     0x3F  /* 550/650/740 SR14 */
#define SIS550_DRAM_SIZE_4MB      0x00
#define SIS550_DRAM_SIZE_8MB      0x01
#define SIS550_DRAM_SIZE_16MB     0x03
#define SIS550_DRAM_SIZE_24MB     0x05
#define SIS550_DRAM_SIZE_32MB     0x07
#define SIS550_DRAM_SIZE_64MB     0x0F
#define SIS550_DRAM_SIZE_96MB     0x17
#define SIS550_DRAM_SIZE_128MB    0x1F
#define SIS550_DRAM_SIZE_256MB    0x3F

#define SIS_SCRATCH_REG_1A_MASK   0x10

#define SIS_ENABLE_2D             0x40  /* SR1E */

#define SIS_MEM_MAP_IO_ENABLE     0x01  /* SR20 */
#define SIS_PCI_ADDR_ENABLE       0x80

#define SIS_AGP_CMDQUEUE_ENABLE   0x80  /* 315/650/740 SR26 */
#define SIS_VRAM_CMDQUEUE_ENABLE  0x40
#define SIS_MMIO_CMD_ENABLE       0x20
#define SIS_CMD_QUEUE_SIZE_512k   0x00
#define SIS_CMD_QUEUE_SIZE_1M     0x04
#define SIS_CMD_QUEUE_SIZE_2M     0x08
#define SIS_CMD_QUEUE_SIZE_4M     0x0C
#define SIS_CMD_QUEUE_RESET       0x01
#define SIS_CMD_AUTO_CORR	  0x02

#define SIS_SIMULTANEOUS_VIEW_ENABLE  0x01  /* CR30 */
#define SIS_MODE_SELECT_CRT2      0x02
#define SIS_VB_OUTPUT_COMPOSITE   0x04
#define SIS_VB_OUTPUT_SVIDEO      0x08
#define SIS_VB_OUTPUT_SCART       0x10
#define SIS_VB_OUTPUT_LCD         0x20
#define SIS_VB_OUTPUT_CRT2        0x40
#define SIS_VB_OUTPUT_HIVISION    0x80

#define SIS_VB_OUTPUT_DISABLE     0x20  /* CR31 */
#define SIS_DRIVER_MODE           0x40

#define SIS_VB_COMPOSITE          0x01  /* CR32 */
#define SIS_VB_SVIDEO             0x02
#define SIS_VB_SCART              0x04
#define SIS_VB_LCD                0x08
#define SIS_VB_CRT2               0x10
#define SIS_CRT1                  0x20
#define SIS_VB_HIVISION           0x40
#define SIS_VB_DVI                0x80
#define SIS_VB_TV                 (SIS_VB_COMPOSITE | SIS_VB_SVIDEO | \
                                   SIS_VB_SCART | SIS_VB_HIVISION)

#define SIS_EXTERNAL_CHIP_MASK    	   0x0E  /* CR37 */
#define SIS_EXTERNAL_CHIP_SIS301           0x01  /* in CR37 << 1 ! */
#define SIS_EXTERNAL_CHIP_LVDS             0x02  /* in CR37 << 1 ! */
#define SIS_EXTERNAL_CHIP_TRUMPION         0x03  /* in CR37 << 1 ! */
#define SIS_EXTERNAL_CHIP_LVDS_CHRONTEL    0x04  /* in CR37 << 1 ! */
#define SIS_EXTERNAL_CHIP_CHRONTEL         0x05  /* in CR37 << 1 ! */
#define SIS310_EXTERNAL_CHIP_LVDS          0x02  /* in CR37 << 1 ! */
#define SIS310_EXTERNAL_CHIP_LVDS_CHRONTEL 0x03  /* in CR37 << 1 ! */

#define SIS_AGP_2X                0x20  /* CR48 */

#define BRI_DRAM_SIZE_MASK        0x70  /* PCI bridge config data */
#define BRI_DRAM_SIZE_2MB         0x00
#define BRI_DRAM_SIZE_4MB         0x01
#define BRI_DRAM_SIZE_8MB         0x02
#define BRI_DRAM_SIZE_16MB        0x03
#define BRI_DRAM_SIZE_32MB        0x04
#define BRI_DRAM_SIZE_64MB        0x05

// Eden Chen
#define HW_DEVICE_EXTENSION	  SIS_HW_DEVICE_INFO
#define PHW_DEVICE_EXTENSION      PSIS_HW_DEVICE_INFO

#define SR_BUFFER_SIZE            5
#define CR_BUFFER_SIZE            5
// ~Eden Chen

/* ------------------- Global Variables ----------------------------- */

/* Fbcon variables */
static struct fb_info fb_info;
static struct display disp;
static int video_type = FB_TYPE_PACKED_PIXELS;
static int video_linelength;
static int video_cmap_len;
static struct display_switch sisfb_sw;
static struct fb_var_screeninfo default_var = {
	xres:           0,
	yres:           0,
	xres_virtual:   0,
	yres_virtual:   0,
	xoffset:        0,
	yoffset:        0,
	bits_per_pixel: 0,
	grayscale:      0,
	red:            {0, 8, 0},
	green:          {0, 8, 0},
	blue:           {0, 8, 0},
	transp:         {0, 0, 0},
	nonstd:         0,
	activate:       FB_ACTIVATE_NOW,
	height:         -1,
	width:          -1,
	accel_flags:    0,
	pixclock:       0,
	left_margin:    0,
	right_margin:   0,
	upper_margin:   0,
	lower_margin:   0,
	hsync_len:      0,
	vsync_len:      0,
	sync:           0,
	vmode:          FB_VMODE_NONINTERLACED,
	reserved:       {0, 0, 0, 0, 0, 0}
};

static struct {
	u16 blue, green, red, pad;
} palette[256];
static union {
#ifdef FBCON_HAS_CFB16
	u16 cfb16[16];
#endif
#ifdef FBCON_HAS_CFB24
	u32 cfb24[16];
#endif
#ifdef FBCON_HAS_CFB32
	u32 cfb32[16];
#endif
} fbcon_cmap;

/* display status */
static int sisfb_off = 0;
static int sisfb_crt1off = 0;
static int sisfb_forcecrt1 = -1;
static int sisfb_inverse = 0;
static int sisvga_enabled = 0;
static int currcon = 0;
static int sisfb_tvmode = 0;
static int sisfb_mem = 0;
static int sisfb_pdc = 0;
static int enable_dstn = 0;

static enum _VGA_ENGINE {
	UNKNOWN_VGA = 0,
	SIS_300_VGA,
	SIS_315_VGA,
} sisvga_engine = UNKNOWN_VGA;

/* TW: These are to adapted according to VGA_ENGINE type */
static int sisfb_hwcursor_size = 0;
static int sisfb_CRT2_write_enable = 0;

int sisfb_crt2type = -1;	/* TW: CRT2 type (for overriding autodetection) */

int sisfb_queuemode = -1; 	/* TW: Use MMIO queue mode by default (310 series only) */

/* data for sis components*/
struct video_info ivideo;

/* TW: For ioctl SISFB_GET_INFO */
sisfb_info sisfbinfo;

/* TW: Hardware extension; contains data on hardware */
HW_DEVICE_EXTENSION sishw_ext = {
	NULL, NULL, NULL, NULL,
	0, 0, 0, 0, 0, 0, 0, 0, 0,
	NULL, NULL, NULL, NULL,
	{0, 0, 0, 0},
	0
};

/* card parameters */
static unsigned long sisfb_mmio_size = 0;
static u8 sisfb_caps = 0;

typedef enum _SIS_CMDTYPE {
	MMIO_CMD = 0,
	AGP_CMD_QUEUE,
	VM_CMD_QUEUE,
} SIS_CMDTYPE;

/* Supported SiS Chips list */
static struct board {
	u16 vendor, device;
	const char *name;
} sisdev_list[] = {
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_300,     "SIS 300"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_540_VGA, "SIS 540"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_630_VGA, "SIS 630/730"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_315H,    "SIS 315H"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_315,     "SIS 315"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_315PRO,  "SIS 315PRO"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_550_VGA, "SIS 550"},
	{PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_650_VGA, "SIS 650/M650/740 VGA"},
	{0, 0, NULL}
};

/* mode table */
struct _sisbios_mode {
	char name[15];
	u8 mode_no;
	u16 xres;
	u16 yres;
	u16 bpp;
	u16 rate_idx;
	u16 cols;
	u16 rows;
} sisbios_mode[] = {
#define MODE_INDEX_NONE           0  /* TW: index for mode=none */
	{"none",         0xFF,    0,    0,  0, 0,   0,  0},  /* TW: for mode "none" */
	{"320x240x16",   0x56,  320,  240, 16, 1,  40, 15},
	{"320x480x8",    0x5A,  320,  480,  8, 1,  40, 30},  /* TW: FSTN */
	{"320x480x16",   0x5B,  320,  480, 16, 1,  40, 30},  /* TW: FSTN */
	{"640x480x8",    0x2E,  640,  480,  8, 1,  80, 30},
	{"640x480x16",   0x44,  640,  480, 16, 1,  80, 30},
	{"640x480x24",   0x62,  640,  480, 32, 1,  80, 30},  /* TW: That's for people who mix up color- and fb depth */
	{"640x480x32",   0x62,  640,  480, 32, 1,  80, 30},
	{"720x480x8",    0x31,  720,  480,  8, 1,  90, 30},
	{"720x480x16",   0x33,  720,  480, 16, 1,  90, 30},
	{"720x480x24",   0x35,  720,  480, 32, 1,  90, 30},
	{"720x480x32",   0x35,  720,  480, 32, 1,  90, 30},
	{"720x576x8",    0x32,  720,  576,  8, 1,  90, 36},
	{"720x576x16",   0x34,  720,  576, 16, 1,  90, 36},
	{"720x576x24",   0x36,  720,  576, 32, 1,  90, 36},
	{"720x576x32",   0x36,  720,  576, 32, 1,  90, 36},
	{"800x480x8",    0x70,  800,  480,  8, 1, 100, 30},  /* TW: 310/325 series only */
	{"800x480x16",   0x7a,  800,  480, 16, 1, 100, 30},
	{"800x480x24",   0x76,  800,  480, 32, 1, 100, 30},
	{"800x480x32",   0x76,  800,  480, 32, 1, 100, 30},
#define DEFAULT_MODE              20 /* TW: index for 800x600x8 */
#define DEFAULT_LCDMODE           20 /* TW: index for 800x600x8 */
#define DEFAULT_TVMODE            20 /* TW: index for 800x600x8 */
	{"800x600x8",    0x30,  800,  600,  8, 2, 100, 37},
	{"800x600x16",   0x47,  800,  600, 16, 2, 100, 37},
	{"800x600x24",   0x63,  800,  600, 32, 2, 100, 37},
	{"800x600x32",   0x63,  800,  600, 32, 2, 100, 37},
	{"1024x576x8",   0x71, 1024,  576,  8, 1, 128, 36},  /* TW: 310/325 series only */
	{"1024x576x16",  0x74, 1024,  576, 16, 1, 128, 36},
	{"1024x576x24",  0x77, 1024,  576, 32, 1, 128, 36},
	{"1024x576x32",  0x77, 1024,  576, 32, 1, 128, 36},
	{"1024x600x8",   0x20, 1024,  600,  8, 1, 128, 37},  /* TW: 300 series only */
	{"1024x600x16",  0x21, 1024,  600, 16, 1, 128, 37},
	{"1024x600x24",  0x22, 1024,  600, 32, 1, 128, 37},
	{"1024x600x32",  0x22, 1024,  600, 32, 1, 128, 37},
	{"1024x768x8",   0x38, 1024,  768,  8, 2, 128, 48},
	{"1024x768x16",  0x4A, 1024,  768, 16, 2, 128, 48},
	{"1024x768x24",  0x64, 1024,  768, 32, 2, 128, 48},
	{"1024x768x32",  0x64, 1024,  768, 32, 2, 128, 48},
	{"1152x768x8",   0x23, 1152,  768,  8, 1, 144, 48},  /* TW: 300 series only */
	{"1152x768x16",  0x24, 1152,  768, 16, 1, 144, 48},
	{"1152x768x24",  0x25, 1152,  768, 32, 1, 144, 48},
	{"1152x768x32",  0x25, 1152,  768, 32, 1, 144, 48},
	{"1280x720x8",   0x79, 1280,  720,  8, 1, 160, 45},  /* TW: 310/325 series only */
	{"1280x720x16",  0x75, 1280,  720, 16, 1, 160, 45},
	{"1280x720x24",  0x78, 1280,  720, 32, 1, 160, 45},
	{"1280x720x32",  0x78, 1280,  720, 32, 1, 160, 45},
	{"1280x768x8",   0x23, 1280,  768,  8, 1, 160, 48},  /* TW: 3107325 series only */
	{"1280x768x16",  0x24, 1280,  768, 16, 1, 160, 48},
	{"1280x768x24",  0x25, 1280,  768, 32, 1, 160, 48},
	{"1280x768x32",  0x25, 1280,  768, 32, 1, 160, 48},
#define MODEINDEX_1280x960 48
	{"1280x960x8",   0x7C, 1280,  960,  8, 1, 160, 60},  /* TW: Modenumbers being patched */
	{"1280x960x16",  0x7D, 1280,  960, 16, 1, 160, 60},
	{"1280x960x24",  0x7E, 1280,  960, 32, 1, 160, 60},
	{"1280x960x32",  0x7E, 1280,  960, 32, 1, 160, 60},
	{"1280x1024x8",  0x3A, 1280, 1024,  8, 2, 160, 64},
	{"1280x1024x16", 0x4D, 1280, 1024, 16, 2, 160, 64},
	{"1280x1024x24", 0x65, 1280, 1024, 32, 2, 160, 64},
	{"1280x1024x32", 0x65, 1280, 1024, 32, 2, 160, 64},
	{"1400x1050x8",  0x26, 1400, 1050,  8, 1, 175, 65},  /* TW: 310/325 series only */
	{"1400x1050x16", 0x27, 1400, 1050, 16, 1, 175, 65},
	{"1400x1050x24", 0x28, 1400, 1050, 32, 1, 175, 65},
	{"1400x1050x32", 0x28, 1400, 1050, 32, 1, 175, 65},
	{"1600x1200x8",  0x3C, 1600, 1200,  8, 1, 200, 75},
	{"1600x1200x16", 0x3D, 1600, 1200, 16, 1, 200, 75},
	{"1600x1200x24", 0x66, 1600, 1200, 32, 1, 200, 75},
	{"1600x1200x32", 0x66, 1600, 1200, 32, 1, 200, 75},
	{"1920x1440x8",  0x68, 1920, 1440,  8, 1, 240, 75},
	{"1920x1440x16", 0x69, 1920, 1440, 16, 1, 240, 75},
	{"1920x1440x24", 0x6B, 1920, 1440, 32, 1, 240, 75},
	{"1920x1440x32", 0x6B, 1920, 1440, 32, 1, 240, 75},
	{"2048x1536x8",  0x6c, 2048, 1536,  8, 1, 256, 96},  /* TW: 310/325 series only */
	{"2048x1536x16", 0x6d, 2048, 1536, 16, 1, 256, 96},
	{"2048x1536x24", 0x6e, 2048, 1536, 32, 1, 256, 96},
	{"2048x1536x32", 0x6e, 2048, 1536, 32, 1, 256, 96},
	{"\0", 0x00, 0, 0, 0, 0, 0, 0}
};

/* mode-related variables */
int sisfb_mode_idx = MODE_INDEX_NONE;
u8  sisfb_mode_no = 0;
u8  sisfb_rate_idx = 0;

/* TW: CR36 evaluation */
USHORT sis300paneltype[] =
    { LCD_UNKNOWN,   LCD_800x600,  LCD_1024x768,  LCD_1280x1024,
      LCD_1280x960,  LCD_640x480,  LCD_1024x600,  LCD_1152x768,
      LCD_320x480,   LCD_1024x768, LCD_1024x768,  LCD_1024x768,
      LCD_1024x768,  LCD_1024x768, LCD_1024x768,  LCD_1024x768 };

USHORT sis310paneltype[] =
    { LCD_UNKNOWN,   LCD_800x600,  LCD_1024x768,  LCD_1280x1024,
      LCD_640x480,   LCD_1024x600, LCD_1152x864,  LCD_1280x960,
      LCD_1152x768,  LCD_1400x1050,LCD_1280x768,  LCD_1600x1200,
      LCD_320x480,   LCD_1024x768, LCD_1024x768,  LCD_1024x768 };

static const struct _sis_crt2type {
	char name[6];
	int type_no;
} sis_crt2type[] = {
	{"NONE", 0},
	{"LCD",  DISPTYPE_LCD},
	{"TV",   DISPTYPE_TV},
	{"VGA",  DISPTYPE_CRT2},
	{"none", 0},	/* TW: make it fool-proof */
	{"lcd",  DISPTYPE_LCD},
	{"tv",   DISPTYPE_TV},
	{"vga",  DISPTYPE_CRT2},
	{"\0",  -1}
};

/* Queue mode selection for 310 series */
static const struct _sis_queuemode {
	char name[6];
	int type_no;
} sis_queuemode[] = {
	{"AGP",  AGP_CMD_QUEUE},
	{"VRAM", VM_CMD_QUEUE},
	{"MMIO", MMIO_CMD},
	{"agp",  AGP_CMD_QUEUE},
	{"vram", VM_CMD_QUEUE},
	{"mmio", MMIO_CMD},
	{"\0",  -1}
};

static struct _sis_vrate {
	u16 idx;
	u16 xres;
	u16 yres;
	u16 refresh;
} sisfb_vrate[] = {
	{1,  640,  480, 60}, {2,  640,  480,  72}, {3, 640,   480,  75}, {4,  640, 480,  85},
	{5,  640,  480,100}, {6,  640,  480, 120}, {7, 640,   480, 160}, {8,  640, 480, 200},
	{1,  720,  480, 60},
	{1,  720,  576, 58},
	{1,  800,  480, 60}, {2,  800,  480,  75}, {3, 800,   480,  85},
	{1,  800,  600, 56}, {2,  800,  600,  60}, {3, 800,   600,  72}, {4,  800, 600,  75},
	{5,  800,  600, 85}, {6,  800,  600, 100}, {7, 800,   600, 120}, {8,  800, 600, 160},
	{1, 1024,  768, 43}, {2, 1024,  768,  60}, {3, 1024,  768,  70}, {4, 1024, 768,  75},
	{5, 1024,  768, 85}, {6, 1024,  768, 100}, {7, 1024,  768, 120},
	{1, 1024,  576, 60}, {2, 1024,  576,  65}, {3, 1024,  576,  75},
	{1, 1024,  600, 60},
	{1, 1152,  768, 60},
	{1, 1280,  720, 60}, {2, 1280,  720,  75}, {3, 1280,  720,  85},
	{1, 1280,  768, 60},
	{1, 1280, 1024, 43}, {2, 1280, 1024,  60}, {3, 1280, 1024,  75}, {4, 1280, 1024,  85},
	{1, 1280,  960, 60},
	{1, 1400, 1050, 60},
	{1, 1600, 1200, 60}, {2, 1600, 1200,  65}, {3, 1600, 1200,  70}, {4, 1600, 1200,  75},
	{5, 1600, 1200, 85}, {6, 1600, 1200, 100}, {7, 1600, 1200, 120},
	/* TW: Clock values for 1920x1440 guessed (except for the first one) */
	{1, 1920, 1440, 60}, {2, 1920, 1440,  70}, {3, 1920, 1440,  75}, {4, 1920, 1440,  85},
	{5, 1920, 1440,100}, {6, 1920, 1440, 120},
	/* TW: Clock values for 2048x1536 guessed */
	{1, 2048, 1536, 60}, {2, 2048, 1536,  70}, {3, 2048, 1536,  75}, {4, 2048, 1536,  85},
	{5, 2048, 1536,100},
	{0, 0, 0, 0}
};

/* Offscreen layout */
typedef struct _SIS_GLYINFO {
	unsigned char ch;
	int fontwidth;
	int fontheight;
	u8 gmask[72];
	int ngmask;
} SIS_GLYINFO;

typedef struct _SIS_OH {
	struct _SIS_OH *poh_next;
	struct _SIS_OH *poh_prev;
	unsigned long offset;
	unsigned long size;
} SIS_OH;

typedef struct _SIS_OHALLOC {
	struct _SIS_OHALLOC *poha_next;
	SIS_OH aoh[1];
} SIS_OHALLOC;

typedef struct _SIS_HEAP {
	SIS_OH oh_free;
	SIS_OH oh_used;
	SIS_OH *poh_freelist;
	SIS_OHALLOC *poha_chain;
	unsigned long max_freesize;
} SIS_HEAP;

static unsigned long sisfb_hwcursor_vbase;

static unsigned long sisfb_heap_start;
static unsigned long sisfb_heap_end;
static unsigned long sisfb_heap_size;
static SIS_HEAP sisfb_heap;

// Eden Chen
static struct _sis_TV_filter {
	u8 filter[9][4];
} sis_TV_filter[] = {
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_0 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_1 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_2 */
	   {0xF5,0xEE,0x1B,0x44},
	   {0xF8,0xF4,0x18,0x38},
	   {0xEB,0x04,0x25,0x18},
	   {0xF1,0x05,0x1F,0x16},
	   {0xF6,0x06,0x1A,0x14},
	   {0xFA,0x06,0x16,0x14},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_3 */
	   {0xF1,0x04,0x1F,0x18},
	   {0xEE,0x0D,0x22,0x06},
	   {0xF7,0x06,0x19,0x14},
	   {0xF4,0x0B,0x1C,0x0A},
	   {0xFA,0x07,0x16,0x12},
	   {0xF9,0x0A,0x17,0x0C},
	   {0x00,0x07,0x10,0x12}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_4 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_5 */
	   {0xF5,0xEE,0x1B,0x44},
	   {0xF8,0xF4,0x18,0x38},
	   {0xEB,0x04,0x25,0x18},
	   {0xF1,0x05,0x1F,0x16},
	   {0xF6,0x06,0x1A,0x14},
	   {0xFA,0x06,0x16,0x14},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_6 */
	   {0xEB,0x04,0x25,0x18},
	   {0xE7,0x0E,0x29,0x04},
	   {0xEE,0x0C,0x22,0x08},
	   {0xF6,0x0B,0x1A,0x0A},
	   {0xF9,0x0A,0x17,0x0C},
	   {0xFC,0x0A,0x14,0x0C},
	   {0x00,0x08,0x10,0x10}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* NTSCFilter_7 */
	   {0xEC,0x02,0x24,0x1C},
	   {0xF2,0x04,0x1E,0x18},
	   {0xEB,0x15,0x25,0xF6},
	   {0xF4,0x10,0x1C,0x00},
	   {0xF8,0x0F,0x18,0x02},
	   {0x00,0x04,0x10,0x18},
	   {0x01,0x06,0x0F,0x14}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_0 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_1 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_2 */
	   {0xF5,0xEE,0x1B,0x44},
	   {0xF8,0xF4,0x18,0x38},
	   {0xF1,0xF7,0x01,0x32},
	   {0xF5,0xFB,0x1B,0x2A},
	   {0xF9,0xFF,0x17,0x22},
	   {0xFB,0x01,0x15,0x1E},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_3 */
	   {0xF5,0xFB,0x1B,0x2A},
	   {0xEE,0xFE,0x22,0x24},
	   {0xF3,0x00,0x1D,0x20},
	   {0xF9,0x03,0x17,0x1A},
	   {0xFB,0x02,0x14,0x1E},
	   {0xFB,0x04,0x15,0x18},
	   {0x00,0x06,0x10,0x14}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_4 */
	   {0x00,0xE0,0x10,0x60},
	   {0x00,0xEE,0x10,0x44},
	   {0x00,0xF4,0x10,0x38},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0x00,0x00,0x10,0x20},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_5 */
	   {0xF5,0xEE,0x1B,0x44},
	   {0xF8,0xF4,0x18,0x38},
	   {0xF1,0xF7,0x1F,0x32},
	   {0xF5,0xFB,0x1B,0x2A},
	   {0xF9,0xFF,0x17,0x22},
	   {0xFB,0x01,0x15,0x1E},
	   {0x00,0x04,0x10,0x18}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_6 */
	   {0xF5,0xEE,0x1B,0x2A},
	   {0xEE,0xFE,0x22,0x24},
	   {0xF3,0x00,0x1D,0x20},
	   {0xF9,0x03,0x17,0x1A},
	   {0xFB,0x02,0x14,0x1E},
	   {0xFB,0x04,0x15,0x18},
	   {0x00,0x06,0x10,0x14}, 
	   {0xFF,0xFF,0xFF,0xFF} }},
	{ {{0x00,0x00,0x00,0x40},  /* PALFilter_7 */
	   {0xF5,0xEE,0x1B,0x44},
	   {0xF8,0xF4,0x18,0x38},
	   {0xFC,0xFB,0x14,0x2A},
	   {0xEB,0x05,0x25,0x16},
	   {0xF1,0x05,0x1F,0x16},
	   {0xFA,0x07,0x16,0x12},
	   {0x00,0x07,0x10,0x12}, 
	   {0xFF,0xFF,0xFF,0xFF} }}
};

static int filter = -1;
static unsigned char filter_tb;
//~Eden Chen

/* ---------------------- Routine Prototype ------------------------- */

/* Interface used by the world */
int sisfb_setup(char *options);
static int sisfb_get_fix(struct fb_fix_screeninfo *fix, int con,
			 struct fb_info *info);
static int sisfb_get_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int sisfb_set_var(struct fb_var_screeninfo *var, int con,
			 struct fb_info *info);
static int sisfb_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int sisfb_set_cmap(struct fb_cmap *cmap, int kspc, int con,
			  struct fb_info *info);
static int sisfb_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg, int con,
		       struct fb_info *info);

/* Interface to the low level console driver */
int sisfb_init(void);
static int sisfb_update_var(int con, struct fb_info *info);
static int sisfb_switch(int con, struct fb_info *info);
static void sisfb_blank(int blank, struct fb_info *info);

/* hardware access routines */
void sisfb_set_reg1(u16 port, u16 index, u16 data);
void sisfb_set_reg3(u16 port, u16 data);
void sisfb_set_reg4(u16 port, unsigned long data);
u8   sisfb_get_reg1(u16 port, u16 index);
u8   sisfb_get_reg2(u16 port);
u32  sisfb_get_reg3(u16 port);

/* Internal routines */
static void sisfb_search_mode(const char *name);
static void sisfb_validate_mode(void);
static u8   sisfb_search_refresh_rate(unsigned int rate);
static int  sis_getcolreg(unsigned regno, unsigned *red, unsigned *green,
			 unsigned *blue, unsigned *transp,
			 struct fb_info *fb_info);
static int  sisfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			 unsigned blue, unsigned transp,
			 struct fb_info *fb_info);
static int  sisfb_do_set_var(struct fb_var_screeninfo *var, int isactive,
		      struct fb_info *info);
static void sisfb_set_disp(int con, struct fb_var_screeninfo *var);
static void sisfb_do_install_cmap(int con, struct fb_info *info);

/* Chip-dependent Routines */
#ifdef CONFIG_FB_SIS_300
static int sisfb_get_dram_size_300(void);
static void sisfb_detect_VB_connect_300(void);
static void sisfb_get_VB_type_300(void);
static int sisfb_has_VB_300(void);
#endif
#ifdef CONFIG_FB_SIS_315
static int sisfb_get_dram_size_315(void);
static void sisfb_detect_VB_connect_315(void);
static void sisfb_get_VB_type_315(void);
static int sisfb_has_VB_315(void);
#endif

/* Routines from init.c/init301.c */

extern void 	SiSRegInit(USHORT BaseAddr);
extern BOOLEAN  SiSInit(PSIS_HW_DEVICE_INFO HwDeviceExtension);
extern BOOLEAN  SiSSetMode(PSIS_HW_DEVICE_INFO HwDeviceExtension, USHORT ModeNo);
extern void     SetEnableDstn(void);

/* TW: Chrontel TV functions */
extern USHORT   SiS_IF_DEF_CH70xx;
extern USHORT 	SiS_GetCH700x(USHORT tempbx);
extern void 	SiS_SetCH700x(USHORT tempbx);
extern USHORT 	SiS_GetCH701x(USHORT tempbx);
extern void 	SiS_SetCH701x(USHORT tempbx);
extern void     SiS_SetCH70xxANDOR(USHORT tempax,USHORT tempbh);
extern void     SiS_DDC2Delay(USHORT delaytime);

static void sisfb_pre_setmode(void);
static void sisfb_post_setmode(void);
static void sisfb_crtc_to_var(struct fb_var_screeninfo *var);

/* Export functions  */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,5,23)
static void sis_get_glyph(SIS_GLYINFO *gly);
#else
static void sis_get_glyph(struct fb_info *info, SIS_GLYINFO *gly);
#endif
void sis_dispinfo(struct ap_data *rec);
void sis_malloc(struct sis_memreq *req);
void sis_free(unsigned long base);

/* heap routines */
static int sisfb_heap_init(void);
static SIS_OH *sisfb_poh_new_node(void);
static SIS_OH *sisfb_poh_allocate(unsigned long size);
static void sisfb_delete_node(SIS_OH *poh);
static void sisfb_insert_node(SIS_OH *pohList, SIS_OH *poh);
static SIS_OH *sisfb_poh_free(unsigned long base);
static void sisfb_free_node(SIS_OH *poh);

/* routines to access PCI configuration space */
BOOLEAN sisfb_query_VGA_config_space(PSIS_HW_DEVICE_INFO psishw_ext, 
	unsigned long offset, unsigned long set, unsigned long *value);
BOOLEAN sisfb_query_north_bridge_space(PSIS_HW_DEVICE_INFO psishw_ext, 
	unsigned long offset, unsigned long set, unsigned long *value);

#endif
