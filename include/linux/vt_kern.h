#ifndef _VT_KERN_H
#define _VT_KERN_H

/*
 * this really is an extension of the vc_cons structure in console.c, but
 * with information needed by the vt package
 */

#include <linux/vt.h>

/*
 * Presently, a lot of graphics programs do not restore the contents of
 * the higher font pages.  Defining this flag will avoid use of them, but
 * will lose support for PIO_FONTRESET.  Note that many font operations are
 * not likely to work with these programs anyway; they need to be
 * fixed.  The linux/Documentation directory includes a code snippet
 * to save and restore the text font.
 */
#define BROKEN_GRAPHICS_PROGRAMS 1

extern struct vt_struct {
	int vc_num;				/* The console number */
	unsigned char	vc_mode;		/* KD_TEXT, ... */
#if 0	/* FIXME: Does anyone use these? */
	unsigned char	vc_kbdraw;
	unsigned char	vc_kbde0;
	unsigned char   vc_kbdleds;
#endif
	struct vt_mode	vt_mode;
	int		vt_pid;
	int		vt_newvt;
	struct wait_queue *paste_wait;
} *vt_cons[MAX_NR_CONSOLES];

void (*kd_mksound)(unsigned int hz, unsigned int ticks);

/* console.c */

int vc_allocate(unsigned int console, int init);
int vc_cons_allocated(unsigned int console);
int vc_resize(unsigned int lines, unsigned int cols,
	      unsigned int first, unsigned int last);
#define vc_resize_all(l, c) vc_resize(l, c, 0, MAX_NR_CONSOLES-1)
#define vc_resize_con(l, c, x) vc_resize(l, c, x, x)
void vc_disallocate(unsigned int console);
void poke_blanked_console(void);
void set_vesa_blanking(unsigned long arg);
void vesa_blank(void);
void vesa_powerdown(void);
void reset_palette(int currcons);
void set_palette(void);
void do_blank_screen(int nopowersave);
int con_set_font(char * fontmap, int w, int h, int chars);
int con_get_font(char * fontmap, int *w, int *h, int *chars);
int con_set_cmap(unsigned char *cmap);
int con_get_cmap(unsigned char *cmap);
void scrollback(int);
void scrollfront(int);

struct tty_struct;
int tioclinux(struct tty_struct *tty, unsigned long arg);

/* consolemap.c */

struct unimapinit;
struct unipair;

int con_set_trans_old(unsigned char * table);
int con_get_trans_old(unsigned char * table);
int con_set_trans_new(unsigned short * table);
int con_get_trans_new(unsigned short * table);
void con_clear_unimap(struct unimapinit *ui);
int con_set_unimap(ushort ct, struct unipair *list);
int con_get_unimap(ushort ct, ushort *uct, struct unipair *list);
void con_set_default_unimap(void);

/* vt.c */

extern unsigned int video_mode_512ch;
extern unsigned int video_font_height;
extern unsigned int default_font_height;
extern unsigned int video_scan_lines;

void complete_change_console(unsigned int new_console);
int vt_waitactive(int vt);
void change_console(unsigned int);
void reset_vc(unsigned int new_console);
int vt_waitactive(int vt);

#endif /* _VT_KERN_H */
