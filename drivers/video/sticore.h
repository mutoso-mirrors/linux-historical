#ifndef STICORE_H
#define STICORE_H

/* generic STI structures & functions */

#if 0
#define DPRINTK(x)	printk x
#else
#define DPRINTK(x) 
#endif

#define MAX_STI_ROMS 4		/* max no. of ROMs which this driver handles */

#define STI_REGION_MAX 8	/* hardcoded STI constants */
#define STI_DEV_NAME_LENGTH 32
#define STI_MONITOR_MAX 256

#define STI_FONT_HPROMAN8 1
#define STI_FONT_KANA8 2

/* The latency of the STI functions cannot really be reduced by setting
 * this to 0;  STI doesn't seem to be designed to allow calling a different
 * function (or the same function with different arguments) after a
 * function exited with 1 as return value.
 *
 * As all of the functions below could be called from interrupt context,
 * we have to spin_lock_irqsave around the do { ret = bla(); } while(ret==1)
 * block.  Really bad latency there.
 *
 * Probably the best solution to all this is have the generic code manage
 * the screen buffer and a kernel thread to call STI occasionally.
 * 
 * Luckily, the frame buffer guys have the same problem so we can just wait
 * for them to fix it and steal their solution.   prumpf
 */
 
#define STI_WAIT 1

#include <asm/io.h> /* for USE_HPPA_IOREMAP */

#if USE_HPPA_IOREMAP

#define STI_PTR(p)	(p)
#define PTR_STI(p)	(p)
static inline int STI_CALL( unsigned long func, 
		void *flags, void *inptr, void *outptr, void *glob_cfg )
{
       int (*f)(void *,void *,void *,void *);
       f = (void*)func;
       return f(flags, inptr, outptr, glob_cfg);
}

#else /* !USE_HPPA_IOREMAP */

#define STI_PTR(p)	( virt_to_phys(p) )
#define PTR_STI(p)	( phys_to_virt((long)p) )
#define STI_CALL(func, flags, inptr, outptr, glob_cfg) \
       ({                                                      \
               pdc_sti_call( func, (unsigned long)STI_PTR(flags), \
                                   (unsigned long)STI_PTR(inptr), \
                                   (unsigned long)STI_PTR(outptr), \
                                   (unsigned long)STI_PTR(glob_cfg)); \
       })

#endif /* USE_HPPA_IOREMAP */


#define sti_onscreen_x(sti) (sti->glob_cfg->onscreen_x)
#define sti_onscreen_y(sti) (sti->glob_cfg->onscreen_y)

/* sti_font_xy() use the native font ROM ! */
#define sti_font_x(sti) (PTR_STI(sti->font)->width)
#define sti_font_y(sti) (PTR_STI(sti->font)->height)


/* STI function configuration structs */

typedef union region {
	struct { 
		u32 offset	: 14;	/* offset in 4kbyte page */
		u32 sys_only	: 1;	/* don't map to user space */
		u32 cache	: 1;	/* map to data cache */
		u32 btlb	: 1;	/* map to block tlb */
		u32 last	: 1;	/* last region in list */
		u32 length	: 14;	/* length in 4kbyte page */
	} region_desc;

	u32 region;			/* complete region value */
} region_t;

#define REGION_OFFSET_TO_PHYS( rt, hpa ) \
	(((rt).region_desc.offset << 12) + (hpa))

struct sti_glob_cfg_ext {
	 u8 curr_mon;			/* current monitor configured */
	 u8 friendly_boot;		/* in friendly boot mode */
	s16 power;			/* power calculation (in Watts) */
	s32 freq_ref;			/* frequency refrence */
	u32 sti_mem_addr;		/* pointer to global sti memory (size=sti_mem_request) */
	u32 future_ptr; 		/* pointer to future data */
};

struct sti_glob_cfg {
	s32 text_planes;		/* number of planes used for text */
	s16 onscreen_x;			/* screen width in pixels */
	s16 onscreen_y;			/* screen height in pixels */
	s16 offscreen_x;		/* offset width in pixels */
	s16 offscreen_y;		/* offset height in pixels */
	s16 total_x;			/* frame buffer width in pixels */
	s16 total_y;			/* frame buffer height in pixels */
	u32 region_ptrs[STI_REGION_MAX]; /* region pointers */
	s32 reent_lvl;			/* storage for reentry level value */
	u32 save_addr;			/* where to save or restore reentrant state */
	u32 ext_ptr;			/* pointer to extended glob_cfg data structure */
};


/* STI init function structs */

struct sti_init_flags {
	u32 wait : 1;		/* should routine idle wait or not */
	u32 reset : 1;		/* hard reset the device? */
	u32 text : 1;		/* turn on text display planes? */
	u32 nontext : 1;	/* turn on non-text display planes? */
	u32 clear : 1;		/* clear text display planes? */
	u32 cmap_blk : 1;	/* non-text planes cmap black? */
	u32 enable_be_timer : 1; /* enable bus error timer */
	u32 enable_be_int : 1;	/* enable bus error timer interrupt */
	u32 no_chg_tx : 1;	/* don't change text settings */
	u32 no_chg_ntx : 1;	/* don't change non-text settings */
	u32 no_chg_bet : 1;	/* don't change berr timer settings */
	u32 no_chg_bei : 1;	/* don't change berr int settings */
	u32 init_cmap_tx : 1;	/* initialize cmap for text planes */
	u32 cmt_chg : 1;	/* change current monitor type */
	u32 retain_ie : 1;	/* don't allow reset to clear int enables */
	u32 caller_bootrom : 1;	/* set only by bootrom for each call */
	u32 caller_kernel : 1;	/* set only by kernel for each call */
	u32 caller_other : 1;	/* set only by non-[BR/K] caller */
	u32 pad	: 14;		/* pad to word boundary */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_init_inptr_ext {
	u8  config_mon_type;	/* configure to monitor type */
	u8  pad[1];		/* pad to word boundary */
	u16 inflight_data;	/* inflight data possible on PCI */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_init_inptr {
	s32 text_planes;	/* number of planes to use for text */
	u32 ext_ptr;		/* pointer to extended init_graph inptr data structure*/
};


struct sti_init_outptr {
	s32 errno;		/* error number on failure */
	s32 text_planes;	/* number of planes used for text */
	u32 future_ptr; 	/* pointer to future data */
};



/* STI configuration function structs */

struct sti_conf_flags {
	u32 wait : 1;		/* should routine idle wait or not */
	u32 pad : 31;		/* pad to word boundary */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_conf_inptr {
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_conf_outptr_ext {
	u32 crt_config[3];	/* hardware specific X11/OGL information */	
	u32 crt_hdw[3];
	u32 future_ptr;
};

struct sti_conf_outptr {
	s32 errno;		/* error number on failure */
	s16 onscreen_x;		/* screen width in pixels */
	s16 onscreen_y;		/* screen height in pixels */
	s16 offscreen_x;	/* offscreen width in pixels */
	s16 offscreen_y;	/* offscreen height in pixels */
	s16 total_x;		/* frame buffer width in pixels */
	s16 total_y;		/* frame buffer height in pixels */
	s32 bits_per_pixel;	/* bits/pixel device has configured */
	s32 bits_used;		/* bits which can be accessed */
	s32 planes;		/* number of fb planes in system */
	 u8 dev_name[STI_DEV_NAME_LENGTH]; /* null terminated product name */
	u32 attributes;		/* flags denoting attributes */
	u32 ext_ptr;		/* pointer to future data */
};

struct sti_rom {
	 u8 type[4];
	 u8 res004;
	 u8 num_mons;
	 u8 revno[2];
	u32 graphics_id[2];

	u32 font_start;
	u32 statesize;
	u32 last_addr;
	u32 region_list;

	u16 reentsize;
	u16 maxtime;
	u32 mon_tbl_addr;
	u32 user_data_addr;
	u32 sti_mem_req;

	u32 user_data_size;
	u16 power;
	 u8 bus_support;
	 u8 ext_bus_support;
	 u8 alt_code_type;
	 u8 ext_dd_struct[3];
	u32 cfb_addr;

	u32 init_graph;
	u32 state_mgmt;
	u32 font_unpmv;
	u32 block_move;
	u32 self_test;
	u32 excep_hdlr;
	u32 inq_conf;
	u32 set_cm_entry;
	u32 dma_ctrl;
	 u8 res040[7 * 4];
	
	u32 init_graph_addr;
	u32 state_mgmt_addr;
	u32 font_unp_addr;
	u32 block_move_addr;
	u32 self_test_addr;
	u32 excep_hdlr_addr;
	u32 inq_conf_addr;
	u32 set_cm_entry_addr;
	u32 image_unpack_addr;
	u32 pa_risx_addrs[7];
};

struct sti_rom_font {
	u16 first_char;
	u16 last_char;
	 u8 width;
	 u8 height;
	 u8 font_type;		/* language type */
	 u8 bytes_per_char;
	u32 next_font;
	 u8 underline_height;
	 u8 underline_pos;
	 u8 res008[2];
};

/* sticore internal font handling */

struct sti_cooked_font {
        struct sti_rom_font *raw;
	struct sti_cooked_font *next_font;
};

struct sti_cooked_rom {
        struct sti_rom *raw;
	struct sti_cooked_font *font_start;
};

/* STI font printing function structs */

struct sti_font_inptr {
	u32 font_start_addr;	/* address of font start */
	s16 index;		/* index into font table of character */
	u8 fg_color;		/* foreground color of character */
	u8 bg_color;		/* background color of character */
	s16 dest_x;		/* X location of character upper left */
	s16 dest_y;		/* Y location of character upper left */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_font_flags {
	u32 wait : 1;		/* should routine idle wait or not */
	u32 non_text : 1;	/* font unpack/move in non_text planes =1, text =0 */
	u32 pad : 30;		/* pad to word boundary */
	u32 future_ptr; 	/* pointer to future data */
};
	
struct sti_font_outptr {
	s32 errno;		/* error number on failure */
	u32 future_ptr; 	/* pointer to future data */
};

/* STI blockmove structs */

struct sti_blkmv_flags {
	u32 wait : 1;		/* should routine idle wait or not */
	u32 color : 1;		/* change color during move? */
	u32 clear : 1;		/* clear during move? */
	u32 non_text : 1;	/* block move in non_text planes =1, text =0 */
	u32 pad : 28;		/* pad to word boundary */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_blkmv_inptr {
	u8 fg_color;		/* foreground color after move */
	u8 bg_color;		/* background color after move */
	s16 src_x;		/* source upper left pixel x location */
	s16 src_y;		/* source upper left pixel y location */
	s16 dest_x;		/* dest upper left pixel x location */
	s16 dest_y;		/* dest upper left pixel y location */
	s16 width;		/* block width in pixels */
	s16 height;		/* block height in pixels */
	u32 future_ptr; 	/* pointer to future data */
};

struct sti_blkmv_outptr {
	s32 errno;		/* error number on failure */
	u32 future_ptr; 	/* pointer to future data */
};


/* internal generic STI struct */

struct sti_struct {
	spinlock_t lock;
		
	/* the following fields needs to be filled in by the word/byte routines */
	int font_width;	
	int font_height;
	/* char **mon_strings; */
	int sti_mem_request;
	u32 graphics_id[2];

	struct sti_cooked_rom *rom;

	unsigned long font_unpmv;
	unsigned long block_move;
	unsigned long init_graph;
	unsigned long inq_conf;

	/* all following fields are initialized by the generic routines */
	int text_planes;
	region_t regions[STI_REGION_MAX];
	unsigned long regions_phys[STI_REGION_MAX];

	struct sti_glob_cfg *glob_cfg;
	struct sti_cooked_font *font;	/* ptr to selected font (cooked) */

	struct sti_conf_outptr outptr; /* configuration */
	struct sti_conf_outptr_ext outptr_ext;

	/* PCI data structures (pg. 17ff from sti.pdf) */
	struct pci_dev *pd;
	u8 rm_entry[16]; /* pci region mapper array == pci config space offset */

	/* pointer to the fb_info where this STI device is used */
	struct fb_info *info;
};


/* sticore interface functions */

struct sti_struct *sti_get_rom(unsigned int index); /* 0: default sti */

/* functions to call the STI ROM directly */

int  sti_init_graph(struct sti_struct *sti);
void sti_inq_conf(struct sti_struct *sti);
void sti_putc(struct sti_struct *sti, int c, int y, int x);
void sti_set(struct sti_struct *sti, int src_y, int src_x,
	     int height, int width, u8 color);
void sti_clear(struct sti_struct *sti, int src_y, int src_x,
	       int height, int width, int c);
void sti_bmove(struct sti_struct *sti, int src_y, int src_x,
	       int dst_y, int dst_x, int height, int width);

#endif	/* STICORE_H */
