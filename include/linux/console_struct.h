/*
 * console_struct.h
 *
 * Data structure and defines shared between console.c, vga.c and tga.c
 */

/*
 * You can set here how should the cursor look by default.
 * In case you set CONFIG_SOFTCURSOR, this might be really interesting.
 */
#define CUR_DEFAULT CUR_UNDERLINE

#include <linux/config.h>

#define NPAR 16

struct vc_data {
	unsigned short	vc_num;			/* Console number */
	unsigned int	vc_cols;		/* Console size */
	unsigned int	vc_rows;
	unsigned int	vc_size_row;		/* Bytes per row */
	struct consw	*vc_sw;
	unsigned short	*vc_screenbuf;		/* In-memory character/attribute buffer */
	unsigned int	vc_screenbuf_size;
	unsigned short	vc_video_erase_char;	/* Background erase character */
	unsigned char	vc_attr;		/* Current attributes */
	unsigned char	vc_def_color;		/* Default colors */
	unsigned char	vc_color;		/* Foreground & background */
	unsigned char	vc_s_color;		/* Saved foreground & background */
	unsigned char	vc_ulcolor;		/* Color for underline mode */
	unsigned char	vc_halfcolor;		/* Color for half intensity mode */
	unsigned int	vc_x, vc_y;		/* Cursor position */
	unsigned int	vc_top, vc_bottom;	/* Scrolling region */
	unsigned int	vc_state;		/* Escape sequence parser state */
	unsigned int	vc_npar,vc_par[NPAR];	/* Parameters of current escape sequence */
	unsigned long	vc_scr_top;		/* Top of video memory */
	unsigned long	vc_origin;		/* Start of real screen */
	unsigned long	vc_scr_end;		/* End of real screen */
	unsigned long	vc_visible_origin;	/* Top of visible window */
	unsigned long	vc_pos;			/* Cursor address */
	unsigned int	vc_saved_x;
	unsigned int	vc_saved_y;
	/* mode flags */
	unsigned int	vc_charset	: 1;	/* Character set G0 / G1 */
	unsigned int	vc_s_charset	: 1;	/* Saved character set */
	unsigned int	vc_disp_ctrl	: 1;	/* Display chars < 32? */
	unsigned int	vc_toggle_meta	: 1;	/* Toggle high bit? */
	unsigned int	vc_decscnm	: 1;	/* Screen Mode */
	unsigned int	vc_decom	: 1;	/* Origin Mode */
	unsigned int	vc_decawm	: 1;	/* Autowrap Mode */
	unsigned int	vc_deccm	: 1;	/* Cursor Visible */
	unsigned int	vc_decim	: 1;	/* Insert Mode */
	unsigned int	vc_deccolm	: 1;	/* 80/132 Column Mode */
	/* attribute flags */
	unsigned int	vc_intensity	: 2;	/* 0=half-bright, 1=normal, 2=bold */
	unsigned int	vc_underline	: 1;
	unsigned int	vc_blink	: 1;
	unsigned int	vc_reverse	: 1;
	unsigned int	vc_s_intensity	: 2;	/* saved rendition */
	unsigned int	vc_s_underline	: 1;
	unsigned int	vc_s_blink	: 1;
	unsigned int	vc_s_reverse	: 1;
	/* misc */
	unsigned int	vc_ques		: 1;
	unsigned int	vc_need_wrap	: 1;
	unsigned int	vc_can_do_color	: 1;
	unsigned int	vc_report_mouse : 2;
	unsigned int	vc_kmalloced	: 1;
	unsigned char	vc_utf		: 1;	/* Unicode UTF-8 encoding */
	unsigned char	vc_utf_count;
		 int	vc_utf_char;
	unsigned int	vc_tab_stop[5];		/* Tab stops. 160 columns. */
	unsigned char   vc_palette[16*3];       /* Colour palette for VGA+ */
	unsigned short * vc_translate;
	unsigned char 	vc_G0_charset;
	unsigned char 	vc_G1_charset;
	unsigned char 	vc_saved_G0;
	unsigned char 	vc_saved_G1;
	unsigned int	vc_bell_pitch;		/* Console bell pitch */
	unsigned int	vc_bell_duration;	/* Console bell duration */
	unsigned int	vc_cursor_type;
	/* additional information is in vt_kern.h */
};

struct vc {
	struct vc_data *d;

	/* might add  scrmem, vt_struct, kbd  at some time,
	   to have everything in one place - the disadvantage
	   would be that vc_cons etc can no longer be static */
};

extern struct vc vc_cons [MAX_NR_CONSOLES];

#define CUR_DEF		0
#define CUR_NONE	1
#define CUR_UNDERLINE	2
#define CUR_LOWER_THIRD	3
#define CUR_LOWER_HALF	4
#define CUR_TWO_THIRDS	5
#define CUR_BLOCK	6
#define CUR_HWMASK	0x0f
#define CUR_SWMASK	0xfff0
