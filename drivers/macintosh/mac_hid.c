/*
 * drivers/macintosh/mac_hid.c
 *
 * HID support stuff for Macintosh computers.
 *
 * Copyright (C) 2000 Franz Sirl.
 *
 * Stuff inside CONFIG_MAC_ADBKEYCODES should go away during 2.5 when all
 * major distributions are using the Linux keycodes.
 * Stuff inside CONFIG_MAC_EMUMOUSEBTN should really be moved to userspace.
 */

#include <linux/config.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/sysctl.h>
#include <linux/input.h>
#include <linux/module.h>

#ifdef CONFIG_MAC_ADBKEYCODES
#include <linux/keyboard.h>
#include <asm/keyboard.h>
#include <asm/machdep.h>
#endif

#ifdef CONFIG_MAC_ADBKEYCODES
/* Simple translation table for the SysRq keys */

#ifdef CONFIG_MAGIC_SYSRQ
unsigned char mac_hid_kbd_sysrq_xlate[128] =
	"asdfhgzxcv\000bqwer"				/* 0x00 - 0x0f */
	"yt123465=97-80o]"				/* 0x10 - 0x1f */
	"u[ip\rlj'k;\\,/nm."				/* 0x20 - 0x2f */
	"\t `\177\000\033\000\000\000\000\000\000\000\000\000\000"
							/* 0x30 - 0x3f */
	"\000\000\000*\000+\000\000\000\000\000/\r\000-\000"
							/* 0x40 - 0x4f */
	"\000\0000123456789\000\000\000"		/* 0x50 - 0x5f */
	"\205\206\207\203\210\211\000\213\000\215\000\000\000\000\000\212\000\214";
							/* 0x60 - 0x6f */
extern unsigned char pckbd_sysrq_xlate[128];
#endif

static u_short macplain_map[NR_KEYS] = {
	0xfb61,	0xfb73,	0xfb64,	0xfb66,	0xfb68,	0xfb67,	0xfb7a,	0xfb78,
	0xfb63,	0xfb76,	0xf200,	0xfb62,	0xfb71,	0xfb77,	0xfb65,	0xfb72,
	0xfb79,	0xfb74,	0xf031,	0xf032,	0xf033,	0xf034,	0xf036,	0xf035,
	0xf03d,	0xf039,	0xf037,	0xf02d,	0xf038,	0xf030,	0xf05d,	0xfb6f,
	0xfb75,	0xf05b,	0xfb69,	0xfb70,	0xf201,	0xfb6c,	0xfb6a,	0xf027,
	0xfb6b,	0xf03b,	0xf05c,	0xf02c,	0xf02f,	0xfb6e,	0xfb6d,	0xf02e,
	0xf009,	0xf020,	0xf060,	0xf07f,	0xf200,	0xf01b,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf200,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf104,	0xf105,	0xf106,	0xf102,	0xf107,	0xf108,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf209,	0xf200,	0xf109,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf103,	0xf117,
	0xf101,	0xf119,	0xf100,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static u_short macshift_map[NR_KEYS] = {
	0xfb41,	0xfb53,	0xfb44,	0xfb46,	0xfb48,	0xfb47,	0xfb5a,	0xfb58,
	0xfb43,	0xfb56,	0xf200,	0xfb42,	0xfb51,	0xfb57,	0xfb45,	0xfb52,
	0xfb59,	0xfb54,	0xf021,	0xf040,	0xf023,	0xf024,	0xf05e,	0xf025,
	0xf02b,	0xf028,	0xf026,	0xf05f,	0xf02a,	0xf029,	0xf07d,	0xfb4f,
	0xfb55,	0xf07b,	0xfb49,	0xfb50,	0xf201,	0xfb4c,	0xfb4a,	0xf022,
	0xfb4b,	0xf03a,	0xf07c,	0xf03c,	0xf03f,	0xfb4e,	0xfb4d,	0xf03e,
	0xf009,	0xf020,	0xf07e,	0xf07f,	0xf200,	0xf01b,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf200,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf10e,	0xf10f,	0xf110,	0xf10c,	0xf111,	0xf112,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf203,	0xf200,	0xf113,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf20b,	0xf116,	0xf10d,	0xf117,
	0xf10b,	0xf20a,	0xf10a,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static u_short macaltgr_map[NR_KEYS] = {
	0xf914,	0xfb73,	0xf917,	0xf919,	0xfb68,	0xfb67,	0xfb7a,	0xfb78,
	0xf916,	0xfb76,	0xf200,	0xf915,	0xfb71,	0xfb77,	0xf918,	0xfb72,
	0xfb79,	0xfb74,	0xf200,	0xf040,	0xf200,	0xf024,	0xf200,	0xf200,
	0xf200,	0xf05d,	0xf07b,	0xf05c,	0xf05b,	0xf07d,	0xf07e,	0xfb6f,
	0xfb75,	0xf200,	0xfb69,	0xfb70,	0xf201,	0xfb6c,	0xfb6a,	0xf200,
	0xfb6b,	0xf200,	0xf200,	0xf200,	0xf200,	0xfb6e,	0xfb6d,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf90a,	0xf90b,	0xf90c,	0xf90d,	0xf90e,	0xf90f,
	0xf910,	0xf911,	0xf200,	0xf912,	0xf913,	0xf200,	0xf200,	0xf200,
	0xf510,	0xf511,	0xf512,	0xf50e,	0xf513,	0xf514,	0xf200,	0xf516,
	0xf200,	0xf10c,	0xf200,	0xf202,	0xf200,	0xf515,	0xf200,	0xf517,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf50f,	0xf117,
	0xf50d,	0xf119,	0xf50c,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static u_short macctrl_map[NR_KEYS] = {
	0xf001,	0xf013,	0xf004,	0xf006,	0xf008,	0xf007,	0xf01a,	0xf018,
	0xf003,	0xf016,	0xf200,	0xf002,	0xf011,	0xf017,	0xf005,	0xf012,
	0xf019,	0xf014,	0xf200,	0xf000,	0xf01b,	0xf01c,	0xf01e,	0xf01d,
	0xf200,	0xf200,	0xf01f,	0xf01f,	0xf07f,	0xf200,	0xf01d,	0xf00f,
	0xf015,	0xf01b,	0xf009,	0xf010,	0xf201,	0xf00c,	0xf00a,	0xf007,
	0xf00b,	0xf200,	0xf01c,	0xf200,	0xf07f,	0xf00e,	0xf00d,	0xf20e,
	0xf200,	0xf000,	0xf000,	0xf008,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf200,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf104,	0xf105,	0xf106,	0xf102,	0xf107,	0xf108,	0xf200,	0xf10a,
	0xf200,	0xf10c,	0xf200,	0xf204,	0xf200,	0xf109,	0xf200,	0xf10b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf103,	0xf117,
	0xf101,	0xf119,	0xf100,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static u_short macshift_ctrl_map[NR_KEYS] = {
	0xf001,	0xf013,	0xf004,	0xf006,	0xf008,	0xf007,	0xf01a,	0xf018,
	0xf003,	0xf016,	0xf200,	0xf002,	0xf011,	0xf017,	0xf005,	0xf012,
	0xf019,	0xf014,	0xf200,	0xf000,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf01f,	0xf200,	0xf200,	0xf200,	0xf00f,
	0xf015,	0xf200,	0xf009,	0xf010,	0xf201,	0xf00c,	0xf00a,	0xf200,
	0xf00b,	0xf200,	0xf200,	0xf200,	0xf200,	0xf00e,	0xf00d,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf200,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf10c,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf200,	0xf117,
	0xf200,	0xf119,	0xf200,	0xf700,	0xf701,	0xf702,	0xf200,	0xf20c,
};

static u_short macalt_map[NR_KEYS] = {
	0xf861,	0xf873,	0xf864,	0xf866,	0xf868,	0xf867,	0xf87a,	0xf878,
	0xf863,	0xf876,	0xf200,	0xf862,	0xf871,	0xf877,	0xf865,	0xf872,
	0xf879,	0xf874,	0xf831,	0xf832,	0xf833,	0xf834,	0xf836,	0xf835,
	0xf83d,	0xf839,	0xf837,	0xf82d,	0xf838,	0xf830,	0xf85d,	0xf86f,
	0xf875,	0xf85b,	0xf869,	0xf870,	0xf80d,	0xf86c,	0xf86a,	0xf827,
	0xf86b,	0xf83b,	0xf85c,	0xf82c,	0xf82f,	0xf86e,	0xf86d,	0xf82e,
	0xf809,	0xf820,	0xf860,	0xf87f,	0xf200,	0xf81b,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf210,	0xf211,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf900,	0xf901,	0xf902,	0xf903,	0xf904,	0xf905,
	0xf906,	0xf907,	0xf200,	0xf908,	0xf909,	0xf200,	0xf200,	0xf200,
	0xf504,	0xf505,	0xf506,	0xf502,	0xf507,	0xf508,	0xf200,	0xf50a,
	0xf200,	0xf10c,	0xf200,	0xf209,	0xf200,	0xf509,	0xf200,	0xf50b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf503,	0xf117,
	0xf501,	0xf119,	0xf500,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static u_short macctrl_alt_map[NR_KEYS] = {
	0xf801,	0xf813,	0xf804,	0xf806,	0xf808,	0xf807,	0xf81a,	0xf818,
	0xf803,	0xf816,	0xf200,	0xf802,	0xf811,	0xf817,	0xf805,	0xf812,
	0xf819,	0xf814,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf80f,
	0xf815,	0xf200,	0xf809,	0xf810,	0xf201,	0xf80c,	0xf80a,	0xf200,
	0xf80b,	0xf200,	0xf200,	0xf200,	0xf200,	0xf80e,	0xf80d,	0xf200,
	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf200,	0xf702,	0xf703,
	0xf700,	0xf207,	0xf701,	0xf601,	0xf602,	0xf600,	0xf603,	0xf200,
	0xf200,	0xf310,	0xf200,	0xf30c,	0xf200,	0xf30a,	0xf200,	0xf208,
	0xf200,	0xf200,	0xf200,	0xf30d,	0xf30e,	0xf200,	0xf30b,	0xf200,
	0xf200,	0xf200,	0xf300,	0xf301,	0xf302,	0xf303,	0xf304,	0xf305,
	0xf306,	0xf307,	0xf200,	0xf308,	0xf309,	0xf200,	0xf200,	0xf200,
	0xf504,	0xf505,	0xf506,	0xf502,	0xf507,	0xf508,	0xf200,	0xf50a,
	0xf200,	0xf10c,	0xf200,	0xf200,	0xf200,	0xf509,	0xf200,	0xf50b,
	0xf200,	0xf11d,	0xf115,	0xf114,	0xf118,	0xf116,	0xf503,	0xf117,
	0xf501,	0xf119,	0xf500,	0xf700,	0xf701,	0xf702,	0xf200,	0xf200,
};

static unsigned short *mac_key_maps_save[MAX_NR_KEYMAPS] = {
	macplain_map, macshift_map, macaltgr_map, 0,
	macctrl_map, macshift_ctrl_map, 0, 0,
	macalt_map, 0, 0, 0,
	macctrl_alt_map,   0
};

static unsigned short *pc_key_maps_save[MAX_NR_KEYMAPS];

int mac_hid_kbd_translate(unsigned char keycode, unsigned char *keycodep,
			  char raw_mode);
static int mac_hid_sysctl_keycodes(ctl_table *ctl, int write, struct file * filp,
				   void *buffer, size_t *lenp);
char mac_hid_kbd_unexpected_up(unsigned char keycode);

static int keyboard_lock_keycodes = 0;
int keyboard_sends_linux_keycodes = 0;
#else
int keyboard_sends_linux_keycodes = 1;
#endif


static unsigned char e0_keys[128] = {
	0, 0, 0, KEY_KPCOMMA, 0, KEY_INTL3, 0, 0,		/* 0x00-0x07 */
	0, 0, 0, 0, KEY_LANG1, KEY_LANG2, 0, 0,			/* 0x08-0x0f */
	0, 0, 0, 0, 0, 0, 0, 0,					/* 0x10-0x17 */
	0, 0, 0, 0, KEY_KPENTER, KEY_RIGHTCTRL, KEY_VOLUMEUP, 0,/* 0x18-0x1f */
	0, 0, 0, 0, 0, KEY_VOLUMEDOWN, KEY_MUTE, 0,		/* 0x20-0x27 */
	0, 0, 0, 0, 0, 0, 0, 0,					/* 0x28-0x2f */
	0, 0, 0, 0, 0, KEY_KPSLASH, 0, KEY_SYSRQ,		/* 0x30-0x37 */
	KEY_RIGHTALT, KEY_BRIGHTNESSUP, KEY_BRIGHTNESSDOWN, 
		KEY_EJECTCD, 0, 0, 0, 0,			/* 0x38-0x3f */
	0, 0, 0, 0, 0, 0, 0, KEY_HOME,				/* 0x40-0x47 */
	KEY_UP, KEY_PAGEUP, 0, KEY_LEFT, 0, KEY_RIGHT, 0, KEY_END, /* 0x48-0x4f */
	KEY_DOWN, KEY_PAGEDOWN, KEY_INSERT, KEY_DELETE, 0, 0, 0, 0, /* 0x50-0x57 */
	0, 0, 0, KEY_LEFTMETA, KEY_RIGHTMETA, KEY_COMPOSE, KEY_POWER, 0, /* 0x58-0x5f */
	0, 0, 0, 0, 0, 0, 0, 0,					/* 0x60-0x67 */
	0, 0, 0, 0, 0, 0, 0, KEY_MACRO,				/* 0x68-0x6f */
	0, 0, 0, 0, 0, 0, 0, 0,					/* 0x70-0x77 */
	0, 0, 0, 0, 0, 0, 0, 0					/* 0x78-0x7f */
};

#ifdef CONFIG_MAC_EMUMOUSEBTN
static struct input_dev emumousebtn;
static void emumousebtn_input_register(void);
static int mouse_emulate_buttons = 0;
static int mouse_button2_keycode = KEY_RIGHTCTRL;	/* right control key */
static int mouse_button3_keycode = KEY_RIGHTALT;	/* right option key */
static int mouse_last_keycode = 0;
#endif

extern void pckbd_init_hw(void);

#if defined CONFIG_SYSCTL && (defined(CONFIG_MAC_ADBKEYCODES) || defined(CONFIG_MAC_EMUMOUSEBTN))
/* file(s) in /proc/sys/dev/mac_hid */
ctl_table mac_hid_files[] =
{
#ifdef CONFIG_MAC_ADBKEYCODES
  {
    DEV_MAC_HID_KEYBOARD_SENDS_LINUX_KEYCODES,
    "keyboard_sends_linux_keycodes", &keyboard_sends_linux_keycodes, sizeof(int),
    0644, NULL, &mac_hid_sysctl_keycodes
  },
  {
    DEV_MAC_HID_KEYBOARD_LOCK_KEYCODES,
    "keyboard_lock_keycodes", &keyboard_lock_keycodes, sizeof(int),
    0644, NULL, &proc_dointvec
  },
#endif
#ifdef CONFIG_MAC_EMUMOUSEBTN
  {
    DEV_MAC_HID_MOUSE_BUTTON_EMULATION,
    "mouse_button_emulation", &mouse_emulate_buttons, sizeof(int),
    0644, NULL, &proc_dointvec
  },
  {
    DEV_MAC_HID_MOUSE_BUTTON2_KEYCODE,
    "mouse_button2_keycode", &mouse_button2_keycode, sizeof(int),
    0644, NULL, &proc_dointvec
  },
  {
    DEV_MAC_HID_MOUSE_BUTTON3_KEYCODE,
    "mouse_button3_keycode", &mouse_button3_keycode, sizeof(int),
    0644, NULL, &proc_dointvec
  },
#endif
  { 0 }
};

/* dir in /proc/sys/dev */
ctl_table mac_hid_dir[] =
{
  { DEV_MAC_HID, "mac_hid", NULL, 0, 0555, mac_hid_files },
  { 0 }
};

/* /proc/sys/dev itself, in case that is not there yet */
ctl_table mac_hid_root_dir[] =
{
  { CTL_DEV, "dev", NULL, 0, 0555, mac_hid_dir },
  { 0 }
};

static struct ctl_table_header *mac_hid_sysctl_header;

#ifdef CONFIG_MAC_ADBKEYCODES
static
int mac_hid_sysctl_keycodes(ctl_table *ctl, int write, struct file * filp,
			    void *buffer, size_t *lenp)
{
	int val = keyboard_sends_linux_keycodes;
	int ret = 0;

	if (!write
	    || (write && !keyboard_lock_keycodes))
		ret = proc_dointvec(ctl, write, filp, buffer, lenp);

	if (write
	    && keyboard_sends_linux_keycodes != val) {
		if (!keyboard_sends_linux_keycodes) {
#ifdef CONFIG_MAGIC_SYSRQ
			ppc_md.ppc_kbd_sysrq_xlate   = mac_hid_kbd_sysrq_xlate;
			SYSRQ_KEY                = 0x69;
#endif
			memcpy(pc_key_maps_save, key_maps, sizeof(key_maps));
			memcpy(key_maps, mac_key_maps_save, sizeof(key_maps));
		} else {
#ifdef CONFIG_MAGIC_SYSRQ
			ppc_md.ppc_kbd_sysrq_xlate   = pckbd_sysrq_xlate;
			SYSRQ_KEY                = 0x54;
#endif
			memcpy(mac_key_maps_save, key_maps, sizeof(key_maps));
			memcpy(key_maps, pc_key_maps_save, sizeof(key_maps));
		}
	}

	return ret;
}
#endif
#endif /* endif CONFIG_SYSCTL */

int mac_hid_kbd_translate(unsigned char scancode, unsigned char *keycode,
			  char raw_mode)
{
#ifdef CONFIG_MAC_ADBKEYCODES
	if (!keyboard_sends_linux_keycodes) {
		if (!raw_mode) {
		/*
		 * Convert R-shift/control/option to L version.
		 */
			switch (scancode) {
			case 0x7b: scancode = 0x38; break; /* R-shift */
			case 0x7c: scancode = 0x3a; break; /* R-option */
			case 0x7d: scancode = 0x36; break; /* R-control */
			}
		}
		*keycode = scancode;
		return 1;
	} else
#endif
	{
		/* This code was copied from char/pc_keyb.c and will be
		 * superflous when the input layer is fully integrated.
		 * We don't need the high_keys handling, so this part
		 * has been removed.
		 */
		static int prev_scancode = 0;

		/* special prefix scancodes.. */
		if (scancode == 0xe0 || scancode == 0xe1) {
			prev_scancode = scancode;
			return 0;
		}

		scancode &= 0x7f;

		if (prev_scancode) {
			if (prev_scancode != 0xe0) {
				if (prev_scancode == 0xe1 && scancode == 0x1d) {
					prev_scancode = 0x100;
					return 0;
				} else if (prev_scancode == 0x100 && scancode == 0x45) {
					*keycode = KEY_PAUSE;
					prev_scancode = 0;
				} else {
					if (!raw_mode)
						printk(KERN_INFO "keyboard: unknown e1 escape sequence\n");
					prev_scancode = 0;
					return 0;
				}
			} else {
				prev_scancode = 0;
				if (scancode == 0x2a || scancode == 0x36)
					return 0;
			}
			if (e0_keys[scancode])
				*keycode = e0_keys[scancode];
			else {
				if (!raw_mode)
					printk(KERN_INFO "keyboard: unknown scancode e0 %02x\n",
					       scancode);
				return 0;
			}
		} else {
			switch (scancode) {
			case  91: scancode = KEY_LINEFEED; break;
			case  92: scancode = KEY_KPEQUAL; break;
			case 125: scancode = KEY_INTL1; break;
			}
			*keycode = scancode;
		}
		return 1;
	}
}

char mac_hid_kbd_unexpected_up(unsigned char keycode)
{
	if (keyboard_sends_linux_keycodes && keycode == KEY_F13)
		return 0;
	else
		return 0x80;
}

#ifdef CONFIG_MAC_ADBKEYCODES
int mac_hid_keyboard_sends_linux_keycodes(void)
{
	return keyboard_sends_linux_keycodes;
}

EXPORT_SYMBOL(mac_hid_keyboard_sends_linux_keycodes);

static int __init mac_hid_setup(char *str)
{
	int ints[2];

	str = get_options(str, ARRAY_SIZE(ints), ints);
	if (ints[0] == 1) {
		keyboard_sends_linux_keycodes = ints[1] != 0;
		keyboard_lock_keycodes = 1;
	}
	return 1;
}

__setup("keyboard_sends_linux_keycodes=", mac_hid_setup);

#endif

#ifdef CONFIG_MAC_EMUMOUSEBTN
int mac_hid_mouse_emulate_buttons(int caller, unsigned int keycode, int down)
{
	switch (caller) {
	case 1:
		/* Called from keybdev.c */
		if (mouse_emulate_buttons
		    && (keycode == mouse_button2_keycode
			|| keycode == mouse_button3_keycode)) {
			if (mouse_emulate_buttons == 1) {
			 	input_report_key(&emumousebtn,
						 keycode == mouse_button2_keycode ? BTN_MIDDLE : BTN_RIGHT,
						 down);
				return 1;
			}
			mouse_last_keycode = down ? keycode : 0;
		}
		break;
	case 2:
		/* Called from mousedev.c */
		if (mouse_emulate_buttons == 2 && keycode == 0) {
			if (mouse_last_keycode == mouse_button2_keycode)
				return 1; /* map to middle button */
			if (mouse_last_keycode == mouse_button3_keycode)
				return 2; /* map to right button */
		}
		return keycode; /* keep button */
	}
	return 0;
}

EXPORT_SYMBOL(mac_hid_mouse_emulate_buttons);

static void emumousebtn_input_register(void)
{
	emumousebtn.name = "Macintosh mouse button emulation";

	emumousebtn.evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	emumousebtn.keybit[LONG(BTN_MOUSE)] = BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);
	emumousebtn.relbit[0] = BIT(REL_X) | BIT(REL_Y);

	emumousebtn.idbus = BUS_ADB;
	emumousebtn.idvendor = 0x0001;
	emumousebtn.idproduct = 0x0001;
	emumousebtn.idversion = 0x0100;

	input_register_device(&emumousebtn);

	printk(KERN_INFO "input%d: Macintosh mouse button emulation\n", emumousebtn.number);
}
#endif

void __init mac_hid_init_hw(void)
{

#ifdef CONFIG_MAC_ADBKEYCODES
	memcpy(pc_key_maps_save, key_maps, sizeof(key_maps));

	if (!keyboard_sends_linux_keycodes) {
#ifdef CONFIG_MAGIC_SYSRQ
		ppc_md.ppc_kbd_sysrq_xlate   = mac_hid_kbd_sysrq_xlate;
		SYSRQ_KEY                = 0x69;
#endif
		memcpy(key_maps, mac_key_maps_save, sizeof(key_maps));
	} else {
#ifdef CONFIG_MAGIC_SYSRQ
		ppc_md.ppc_kbd_sysrq_xlate   = pckbd_sysrq_xlate;
		SYSRQ_KEY                = 0x54;
#endif
	}
#endif /* CONFIG_MAC_ADBKEYCODES */

#ifdef CONFIG_MAC_EMUMOUSEBTN
	emumousebtn_input_register();
#endif

#if CONFIG_PPC
	if (_machine != _MACH_Pmac)
		pckbd_init_hw();
#endif

#if defined(CONFIG_SYSCTL) && (defined(CONFIG_MAC_ADBKEYCODES) || defined(CONFIG_MAC_EMUMOUSEBTN))
	mac_hid_sysctl_header = register_sysctl_table(mac_hid_root_dir, 1);
#endif /* CONFIG_SYSCTL */
}
