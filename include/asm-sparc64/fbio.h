#ifndef __LINUX_FBIO_H
#define __LINUX_FBIO_H

/* Constants used for fbio SunOS compatibility */
/* (C) 1996 Miguel de Icaza */

/* Frame buffer types */
#define FBTYPE_NOTYPE           -1
#define FBTYPE_SUN1BW           0   /* mono */
#define FBTYPE_SUN1COLOR        1 
#define FBTYPE_SUN2BW           2 
#define FBTYPE_SUN2COLOR        3 
#define FBTYPE_SUN2GP           4 
#define FBTYPE_SUN5COLOR        5 
#define FBTYPE_SUN3COLOR        6 
#define FBTYPE_MEMCOLOR         7 
#define FBTYPE_SUN4COLOR        8 
 
#define FBTYPE_NOTSUN1          9 
#define FBTYPE_NOTSUN2          10
#define FBTYPE_NOTSUN3          11
 
#define FBTYPE_SUNFAST_COLOR    12  /* cg6 */
#define FBTYPE_SUNROP_COLOR     13
#define FBTYPE_SUNFB_VIDEO      14
#define FBTYPE_SUNGIFB          15
#define FBTYPE_SUNGPLAS         16
#define FBTYPE_SUNGP3           17
#define FBTYPE_SUNGT            18
#define FBTYPE_SUNLEO           19      /* zx Leo card */
#define FBTYPE_MDICOLOR         20      /* cg14 */
#define FBTYPE_TCXCOLOR		21	/* SUNW,tcx card */

#define FBTYPE_LASTPLUSONE      21	/* This is not last + 1 in fact... */

/* fbio ioctls */
/* Returned by FBIOGTYPE */
struct  fbtype {
        int     fb_type;        /* fb type, see above */
        int     fb_height;      /* pixels */
        int     fb_width;       /* pixels */
        int     fb_depth;
        int     fb_cmsize;      /* color map entries */
        int     fb_size;        /* fb size in bytes */
};
#define FBIOGTYPE _IOR('F', 0, struct fbtype)

/* Used by FBIOPUTCMAP
 *
 * XXX 32-bit binary compatability item... -DaveM
 */
struct  fbcmap {
        int             index;          /* first element (0 origin) */
        int             count;
        unsigned char   *red;
        unsigned char   *green;
        unsigned char   *blue;
};

#define FBIOPUTCMAP _IOW('F', 3, struct fbcmap)
#define FBIOGETCMAP _IOW('F', 4, struct fbcmap)

/* # of device specific values */
#define FB_ATTR_NDEVSPECIFIC    8
/* # of possible emulations */
#define FB_ATTR_NEMUTYPES       4
 
struct fbsattr {
        int     flags;
        int     emu_type;	/* -1 if none */
        int     dev_specific[FB_ATTR_NDEVSPECIFIC];
};
 
struct fbgattr {
        int     real_type;	/* real frame buffer type */
        int     owner;		/* unknown */
        struct fbtype fbtype;	/* real frame buffer fbtype */
        struct fbsattr sattr;   
        int     emu_types[FB_ATTR_NEMUTYPES]; /* supported emulations */
};
#define FBIOSATTR  _IOW('F', 5, struct fbgattr) /* Unsupported: */
#define FBIOGATTR  _IOR('F', 6, struct fbgattr)	/* supported */

#define FBIOSVIDEO _IOW('F', 7, int)
#define FBIOGVIDEO _IOR('F', 8, int)

/* Cursor position */
struct fbcurpos {
#ifdef __KERNEL__
	short fbx, fby;
#else
        short x, y;
#endif
};

/* Cursor operations */
#define FB_CUR_SETCUR   0x01	/* Enable/disable cursor display */
#define FB_CUR_SETPOS   0x02	/* set cursor position */
#define FB_CUR_SETHOT   0x04	/* set cursor hotspot */
#define FB_CUR_SETCMAP  0x08	/* set color map for the cursor */
#define FB_CUR_SETSHAPE 0x10	/* set shape */
#define FB_CUR_SETALL   0x1F	/* all of the above */

/* XXX 32-bit binary compatability item... -DaveM */
struct fbcursor {
        short set;              /* what to set, choose from the list above */
        short enable;           /* cursor on/off */
        struct fbcurpos pos;    /* cursor position */
        struct fbcurpos hot;    /* cursor hot spot */
        struct fbcmap cmap;     /* color map info */
        struct fbcurpos size;   /* cursor bit map size */
        char *image;            /* cursor image bits */
        char *mask;             /* cursor mask bits */
};

/* set/get cursor attributes/shape */
#define FBIOSCURSOR     _IOW('F', 24, struct fbcursor)
#define FBIOGCURSOR     _IOWR('F', 25, struct fbcursor)
 
/* set/get cursor position */
#define FBIOSCURPOS     _IOW('F', 26, struct fbcurpos)
#define FBIOGCURPOS     _IOW('F', 27, struct fbcurpos)
 
/* get max cursor size */
#define FBIOGCURMAX     _IOR('F', 28, struct fbcurpos)

/* wid manipulation */
struct fb_wid_alloc {
#define FB_WID_SHARED_8		0
#define FB_WID_SHARED_24	1
#define FB_WID_DBL_8		2
#define FB_WID_DBL_24		3
	__u32	wa_type;
	__s32	wa_index;	/* Set on return */
	__u32	wa_count;	
};
struct fb_wid_item {
	__u32	wi_type;
	__s32	wi_index;
	__u32	wi_attrs;
	__u32	wi_values[32];
};
/* XXX 32-bit binary compatability item... -DaveM */
struct fb_wid_list {
	__u32	wl_flags;
	__u32	wl_count;
	struct fb_wid_item	*wl_list;
};

#define FBIO_WID_ALLOC	_IOWR('F', 30, struct fb_wid_alloc)
#define FBIO_WID_FREE	_IOW('F', 31, struct fb_wid_alloc)
#define FBIO_WID_PUT	_IOW('F', 32, struct fb_wid_list)
#define FBIO_WID_GET	_IOWR('F', 33, struct fb_wid_list)

/* Cg14 ioctls */
#define MDI_IOCTL          ('M'<<8)
#define MDI_RESET          (MDI_IOCTL|1)
#define MDI_GET_CFGINFO    (MDI_IOCTL|2)
#define MDI_SET_PIXELMODE  (MDI_IOCTL|3)
#    define MDI_32_PIX     32
#    define MDI_16_PIX     16
#    define MDI_8_PIX      8

struct mdi_cfginfo {
	int     mdi_ncluts;     /* Number of implemented CLUTs in this MDI */
        int     mdi_type;       /* FBTYPE name */
        int     mdi_height;     /* height */
        int     mdi_width;      /* widht */
        int     mdi_size;       /* available ram */
        int     mdi_mode;       /* 8bpp, 16bpp or 32bpp */
        int     mdi_pixfreq;    /* pixel clock (from PROM) */
};

/* SparcLinux specific ioctl for the MDI, should be replaced for
 * the SET_XLUT/SET_CLUTn ioctls instead
 */
#define MDI_CLEAR_XLUT       (MDI_IOCTL|9)

/* leo ioctls */
struct leo_clut_alloc {
	__u32	clutid;	/* Set on return */
 	__u32	flag;
 	__u32	index;
};

/* XXX 32-bit binary compatability item... -DaveM */
struct leo_clut {
#define LEO_CLUT_WAIT	0x00000001	/* Not yet implemented */
 	__u32	flag;
 	__u32	clutid;
 	__u32	offset;
 	__u32	count;
 	char *	red;
 	char *	green;
 	char *	blue;
};
#define LEO_CLUTALLOC	_IOWR('L', 53, struct leo_clut_alloc)
#define LEO_CLUTFREE	_IOW('L', 54, struct leo_clut_alloc)
#define LEO_CLUTREAD	_IOW('L', 55, struct leo_clut)
#define LEO_CLUTPOST	_IOW('L', 56, struct leo_clut)
#define LEO_SETGAMMA	_IOW('L', 68, int) /* Not yet implemented */
#define LEO_GETGAMMA	_IOR('L', 69, int) /* Not yet implemented */

#ifdef __KERNEL__
/* Addresses on the fd of a cgsix that are mappable */
#define CG6_FBC    0x70000000
#define CG6_TEC    0x70001000
#define CG6_BTREGS 0x70002000
#define CG6_FHC    0x70004000
#define CG6_THC    0x70005000
#define CG6_ROM    0x70006000
#define CG6_RAM    0x70016000
#define CG6_DHC    0x80000000

#define CG3_MMAP_OFFSET 0x4000000

/* Addresses on the fd of a tcx that are mappable */
#define TCX_RAM8BIT   		0x00000000
#define TCX_RAM24BIT   		0x01000000
#define TCX_UNK3   		0x10000000
#define TCX_UNK4   		0x20000000
#define TCX_CONTROLPLANE   	0x28000000
#define TCX_UNK6   		0x30000000
#define TCX_UNK7   		0x38000000
#define TCX_TEC    		0x70000000
#define TCX_BTREGS 		0x70002000
#define TCX_THC    		0x70004000
#define TCX_DHC    		0x70008000
#define TCX_ALT	   		0x7000a000
#define TCX_SYNC   		0x7000e000
#define TCX_UNK2    		0x70010000

/* CG14 definitions */

/* Offsets into the OBIO space: */
#define CG14_REGS        0       /* registers */
#define CG14_CURSORREGS  0x1000  /* cursor registers */
#define CG14_DACREGS     0x2000  /* DAC registers */
#define CG14_XLUT        0x3000  /* X Look Up Table -- ??? */
#define CG14_CLUT1       0x4000  /* Color Look Up Table */
#define CG14_CLUT2       0x5000  /* Color Look Up Table */
#define CG14_CLUT3       0x6000  /* Color Look Up Table */
#define CG14_AUTO	 0xf000

#endif /* KERNEL */

/* These are exported to userland for applications to use */
/* Mappable offsets for the cg14: control registers */
#define MDI_DIRECT_MAP 0x10000000
#define MDI_CTLREG_MAP 0x20000000
#define MDI_CURSOR_MAP 0x30000000
#define MDI_SHDW_VRT_MAP 0x40000000

/* Mappable offsets for the cg14: frame buffer resolutions */
/* 32 bits */
#define MDI_CHUNKY_XBGR_MAP 0x50000000
#define MDI_CHUNKY_BGR_MAP 0x60000000

/* 16 bits */
#define MDI_PLANAR_X16_MAP 0x70000000
#define MDI_PLANAR_C16_MAP 0x80000000

/* 8 bit is done as CG3 MMAP offset */
/* 32 bits, planar */
#define MDI_PLANAR_X32_MAP 0x90000000
#define MDI_PLANAR_B32_MAP 0xa0000000
#define MDI_PLANAR_G32_MAP 0xb0000000
#define MDI_PLANAR_R32_MAP 0xc0000000

/* Mappable offsets on leo */
#define LEO_SS0_MAP            0x00000000
#define LEO_LC_SS0_USR_MAP     0x00800000
#define LEO_LD_SS0_MAP         0x00801000
#define LEO_LX_CURSOR_MAP      0x00802000
#define LEO_SS1_MAP            0x00803000
#define LEO_LC_SS1_USR_MAP     0x01003000
#define LEO_LD_SS1_MAP         0x01004000
#define LEO_UNK_MAP            0x01005000
#define LEO_LX_KRN_MAP         0x01006000
#define LEO_LC_SS0_KRN_MAP     0x01007000
#define LEO_LC_SS1_KRN_MAP     0x01008000
#define LEO_LD_GBL_MAP         0x01009000
#define LEO_UNK2_MAP           0x0100a000

#endif /* __LINUX_FBIO_H */
