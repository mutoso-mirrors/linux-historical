/*
 *  Copyright (c) 1998-2002 by Paul Davis <pbd@op.net>
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#define __NO_VERSION__
#include <sound/driver.h>
#include <asm/io.h>
#include <linux/init.h>
#include <linux/time.h>
#include <linux/wait.h>
#include <sound/core.h>
#include <sound/snd_wavefront.h>
#include <sound/yss225.h>
#include <sound/initval.h>

/* Control bits for the Load Control Register
 */

#define FX_LSB_TRANSFER 0x01    /* transfer after DSP LSB byte written */
#define FX_MSB_TRANSFER 0x02    /* transfer after DSP MSB byte written */
#define FX_AUTO_INCR    0x04    /* auto-increment DSP address after transfer */

static inline void
dec_mod_count(struct module *module)
{
	if (module)
		__MOD_DEC_USE_COUNT(module);
}

static int
wavefront_fx_idle (snd_wavefront_t *dev) 
    
{
	int i;
	unsigned int x = 0x80;
    
	for (i = 0; i < 1000; i++) {
		x = inb (dev->fx_status);
		if ((x & 0x80) == 0) {
			break;
		}
	}
    
	if (x & 0x80) {
		snd_printk ("FX device never idle.\n");
		return 0;
	}
    
	return (1);
}

static void
wavefront_fx_mute (snd_wavefront_t *dev, int onoff)
    
{
	if (!wavefront_fx_idle(dev)) {
		return;
	}
    
	outb (onoff ? 0x02 : 0x00, dev->fx_op);
}

static int
wavefront_fx_memset (snd_wavefront_t *dev, 
		     int page,
		     int addr, 
		     int cnt, 
		     unsigned short *data)
{
	if (page < 0 || page > 7) {
		snd_printk ("FX memset: "
			"page must be >= 0 and <= 7\n");
		return -(EINVAL);
	}

	if (addr < 0 || addr > 0x7f) {
		snd_printk ("FX memset: "
			"addr must be >= 0 and <= 7f\n");
		return -(EINVAL);
	}

	if (cnt == 1) {

		outb (FX_LSB_TRANSFER, dev->fx_lcr);
		outb (page, dev->fx_dsp_page);
		outb (addr, dev->fx_dsp_addr);
		outb ((data[0] >> 8), dev->fx_dsp_msb);
		outb ((data[0] & 0xff), dev->fx_dsp_lsb);

		snd_printk ("FX: addr %d:%x set to 0x%x\n",
			page, addr, data[0]);
	
	} else {
		int i;

		outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
		outb (page, dev->fx_dsp_page);
		outb (addr, dev->fx_dsp_addr);

		for (i = 0; i < cnt; i++) {
			outb ((data[i] >> 8), dev->fx_dsp_msb);
			outb ((data[i] & 0xff), dev->fx_dsp_lsb);
			if (!wavefront_fx_idle (dev)) {
				break;
			}
		}

		if (i != cnt) {
			snd_printk ("FX memset "
				    "(0x%x, 0x%x, 0x%lx, %d) incomplete\n",
				    page, addr, (unsigned long) data, cnt);
			return -(EIO);
		}
	}

	return 0;
}

int 
snd_wavefront_fx_detect (snd_wavefront_t *dev)

{
	/* This is a crude check, but its the best one I have for now.
	   Certainly on the Maui and the Tropez, wavefront_fx_idle() will
	   report "never idle", which suggests that this test should
	   work OK.
	*/

	if (inb (dev->fx_status) & 0x80) {
		snd_printk ("Hmm, probably a Maui or Tropez.\n");
		return -1;
	}

	return 0;
}	

int 
snd_wavefront_fx_open (snd_hwdep_t *hw, struct file *file)

{
	MOD_INC_USE_COUNT;
	if (!try_inc_mod_count(hw->card->module)) {
		MOD_DEC_USE_COUNT;
		return -EFAULT;
	}
	file->private_data = hw;
	return 0;
}

int 
snd_wavefront_fx_release (snd_hwdep_t *hw, struct file *file)

{
	dec_mod_count(hw->card->module);
	MOD_DEC_USE_COUNT;
	return 0;
}

int
snd_wavefront_fx_ioctl (snd_hwdep_t *sdev, struct file *file,
			unsigned int cmd, unsigned long arg)

{
	snd_card_t *card;
	snd_wavefront_card_t *acard;
	snd_wavefront_t *dev;
	wavefront_fx_info r;
	unsigned short page_data[256];
	unsigned short *pd;

	snd_assert(sdev->card != NULL, return -ENODEV);
	
	card = sdev->card;

	snd_assert(card->private_data != NULL, return -ENODEV);

	acard = card->private_data;
	dev = &acard->wavefront;

	if (copy_from_user (&r, (unsigned char *) arg, sizeof (wavefront_fx_info)))
		return -EFAULT;

	switch (r.request) {
	case WFFX_MUTE:
		wavefront_fx_mute (dev, r.data[0]);
		return -EIO;

	case WFFX_MEMSET:
		if (r.data[2] <= 0) {
			snd_printk ("cannot write "
				"<= 0 bytes to FX\n");
			return -EIO;
		} else if (r.data[2] == 1) {
			pd = (unsigned short *) &r.data[3];
		} else {
			if (r.data[2] > sizeof (page_data)) {
				snd_printk ("cannot write "
					    "> 255 bytes to FX\n");
				return -EIO;
			}
			if (copy_from_user (page_data,
					    (unsigned char *) r.data[3],
					    r.data[2]))
				return -EFAULT;
			pd = page_data;
		}

		wavefront_fx_memset (dev,
			     r.data[0], /* page */
			     r.data[1], /* addr */
			     r.data[2], /* cnt */
			     pd);
		break;

	default:
		snd_printk ("FX: ioctl %d not yet supported\n",
			    r.request);
		return -ENOTTY;
	}
	return 0;
}

/* YSS225 initialization.

   This code was developed using DOSEMU. The Turtle Beach SETUPSND
   utility was run with I/O tracing in DOSEMU enabled, and a reconstruction
   of the port I/O done, using the Yamaha faxback document as a guide
   to add more logic to the code. Its really pretty wierd.

   There was an alternative approach of just dumping the whole I/O
   sequence as a series of port/value pairs and a simple loop
   that output it. However, I hope that eventually I'll get more
   control over what this code does, and so I tried to stick with
   a somewhat "algorithmic" approach.
*/


int __init
snd_wavefront_fx_start (snd_wavefront_t *dev)

{
	unsigned int i, j;

	/* Set all bits for all channels on the MOD unit to zero */
	/* XXX But why do this twice ? */

	for (j = 0; j < 2; j++) {
		for (i = 0x10; i <= 0xff; i++) {
	    
			if (!wavefront_fx_idle (dev)) {
				return (-1);
			}
	    
			outb (i, dev->fx_mod_addr);
			outb (0x0, dev->fx_mod_data);
		}
	}

	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x02, dev->fx_op);                        /* mute on */

	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x44, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x42, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x43, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x7c, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x7e, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x46, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x49, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x47, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x4a, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);

	/* either because of stupidity by TB's programmers, or because it
	   actually does something, rezero the MOD page.
	*/
	for (i = 0x10; i <= 0xff; i++) {
	
		if (!wavefront_fx_idle (dev)) {
			return (-1);
		}
	
		outb (i, dev->fx_mod_addr);
		outb (0x0, dev->fx_mod_data);
	}
	/* load page zero */

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x00, dev->fx_dsp_page);
	outb (0x00, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_zero); i += 2) {
		outb (page_zero[i], dev->fx_dsp_msb);
		outb (page_zero[i+1], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	/* Now load page one */

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x01, dev->fx_dsp_page);
	outb (0x00, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_one); i += 2) {
		outb (page_one[i], dev->fx_dsp_msb);
		outb (page_one[i+1], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x02, dev->fx_dsp_page);
	outb (0x00, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_two); i++) {
		outb (page_two[i], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x03, dev->fx_dsp_page);
	outb (0x00, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_three); i++) {
		outb (page_three[i], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x04, dev->fx_dsp_page);
	outb (0x00, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_four); i++) {
		outb (page_four[i], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	/* Load memory area (page six) */
    
	outb (FX_LSB_TRANSFER, dev->fx_lcr); 
	outb (0x06, dev->fx_dsp_page); 

	for (i = 0; i < sizeof (page_six); i += 3) {
		outb (page_six[i], dev->fx_dsp_addr);
		outb (page_six[i+1], dev->fx_dsp_msb);
		outb (page_six[i+2], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x07, dev->fx_dsp_page);
	outb (0x00, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_seven); i += 2) {
		outb (page_seven[i], dev->fx_dsp_msb);
		outb (page_seven[i+1], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	/* Now setup the MOD area. We do this algorithmically in order to
	   save a little data space. It could be done in the same fashion
	   as the "pages".
	*/

	for (i = 0x00; i <= 0x0f; i++) {
		outb (0x01, dev->fx_mod_addr);
		outb (i, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
		outb (0x02, dev->fx_mod_addr);
		outb (0x00, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	for (i = 0xb0; i <= 0xbf; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0x20, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	for (i = 0xf0; i <= 0xff; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0x20, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	for (i = 0x10; i <= 0x1d; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0xff, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	outb (0x1e, dev->fx_mod_addr);
	outb (0x40, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);

	for (i = 0x1f; i <= 0x2d; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0xff, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	outb (0x2e, dev->fx_mod_addr);
	outb (0x00, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);

	for (i = 0x2f; i <= 0x3e; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0x00, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	outb (0x3f, dev->fx_mod_addr);
	outb (0x20, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);

	for (i = 0x40; i <= 0x4d; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0x00, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	outb (0x4e, dev->fx_mod_addr);
	outb (0x0e, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x4f, dev->fx_mod_addr);
	outb (0x0e, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);


	for (i = 0x50; i <= 0x6b; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0x00, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	outb (0x6c, dev->fx_mod_addr);
	outb (0x40, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);

	outb (0x6d, dev->fx_mod_addr);
	outb (0x00, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);

	outb (0x6e, dev->fx_mod_addr);
	outb (0x40, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);

	outb (0x6f, dev->fx_mod_addr);
	outb (0x40, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);

	for (i = 0x70; i <= 0x7f; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0xc0, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}
    
	for (i = 0x80; i <= 0xaf; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0x00, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	for (i = 0xc0; i <= 0xdd; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0x00, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	outb (0xde, dev->fx_mod_addr);
	outb (0x10, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0xdf, dev->fx_mod_addr);
	outb (0x10, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);

	for (i = 0xe0; i <= 0xef; i++) {
		outb (i, dev->fx_mod_addr);
		outb (0x00, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	for (i = 0x00; i <= 0x0f; i++) {
		outb (0x01, dev->fx_mod_addr);
		outb (i, dev->fx_mod_data);
		outb (0x02, dev->fx_mod_addr);
		outb (0x01, dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	outb (0x02, dev->fx_op); /* mute on */

	/* Now set the coefficients and so forth for the programs above */

	for (i = 0; i < sizeof (coefficients); i += 4) {
		outb (coefficients[i], dev->fx_dsp_page);
		outb (coefficients[i+1], dev->fx_dsp_addr);
		outb (coefficients[i+2], dev->fx_dsp_msb);
		outb (coefficients[i+3], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	/* Some settings (?) that are too small to bundle into loops */

	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x1e, dev->fx_mod_addr);
	outb (0x14, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0xde, dev->fx_mod_addr);
	outb (0x20, dev->fx_mod_data);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0xdf, dev->fx_mod_addr);
	outb (0x20, dev->fx_mod_data);
    
	/* some more coefficients */

	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x06, dev->fx_dsp_page);
	outb (0x78, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x40, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x03, dev->fx_dsp_addr);
	outb (0x0f, dev->fx_dsp_msb);
	outb (0xff, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x0b, dev->fx_dsp_addr);
	outb (0x0f, dev->fx_dsp_msb);
	outb (0xff, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x02, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x0a, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x46, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
	if (!wavefront_fx_idle (dev)) return (-1);
	outb (0x07, dev->fx_dsp_page);
	outb (0x49, dev->fx_dsp_addr);
	outb (0x00, dev->fx_dsp_msb);
	outb (0x00, dev->fx_dsp_lsb);
    
	/* Now, for some strange reason, lets reload every page
	   and all the coefficients over again. I have *NO* idea
	   why this is done. I do know that no sound is produced
	   is this phase is omitted.
	*/

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x00, dev->fx_dsp_page);  
	outb (0x10, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_zero_v2); i += 2) {
		outb (page_zero_v2[i], dev->fx_dsp_msb);
		outb (page_zero_v2[i+1], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x01, dev->fx_dsp_page);
	outb (0x10, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_one_v2); i += 2) {
		outb (page_one_v2[i], dev->fx_dsp_msb);
		outb (page_one_v2[i+1], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}
    
	if (!wavefront_fx_idle (dev)) return (-1);
	if (!wavefront_fx_idle (dev)) return (-1);
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x02, dev->fx_dsp_page);
	outb (0x10, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_two_v2); i++) {
		outb (page_two_v2[i], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x03, dev->fx_dsp_page);
	outb (0x10, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_three_v2); i++) {
		outb (page_three_v2[i], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x04, dev->fx_dsp_page);
	outb (0x10, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_four_v2); i++) {
		outb (page_four_v2[i], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}
    
	outb (FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x06, dev->fx_dsp_page);

	/* Page six v.2 is algorithmic */
    
	for (i = 0x10; i <= 0x3e; i += 2) {
		outb (i, dev->fx_dsp_addr);
		outb (0x00, dev->fx_dsp_msb);
		outb (0x00, dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, dev->fx_lcr);
	outb (0x07, dev->fx_dsp_page);
	outb (0x10, dev->fx_dsp_addr);

	for (i = 0; i < sizeof (page_seven_v2); i += 2) {
		outb (page_seven_v2[i], dev->fx_dsp_msb);
		outb (page_seven_v2[i+1], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	for (i = 0x00; i < sizeof(mod_v2); i += 2) {
		outb (mod_v2[i], dev->fx_mod_addr);
		outb (mod_v2[i+1], dev->fx_mod_data);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	for (i = 0; i < sizeof (coefficients2); i += 4) {
		outb (coefficients2[i], dev->fx_dsp_page);
		outb (coefficients2[i+1], dev->fx_dsp_addr);
		outb (coefficients2[i+2], dev->fx_dsp_msb);
		outb (coefficients2[i+3], dev->fx_dsp_lsb);
		if (!wavefront_fx_idle (dev)) return (-1);
	}

	for (i = 0; i < sizeof (coefficients3); i += 2) {
		int x;

		outb (0x07, dev->fx_dsp_page);
		x = (i % 4) ? 0x4e : 0x4c;
		outb (x, dev->fx_dsp_addr);
		outb (coefficients3[i], dev->fx_dsp_msb);
		outb (coefficients3[i+1], dev->fx_dsp_lsb);
	}

	outb (0x00, dev->fx_op); /* mute off */
	if (!wavefront_fx_idle (dev)) return (-1);

	return (0);
}

/* wierd stuff, derived from port I/O tracing with dosemu */

static unsigned char page_zero[] __initdata = {
0x01, 0x7c, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x00, 0xf5, 0x00,
0x11, 0x00, 0x20, 0x00, 0x32, 0x00, 0x40, 0x00, 0x13, 0x00, 0x00,
0x00, 0x14, 0x02, 0x76, 0x00, 0x60, 0x00, 0x80, 0x02, 0x00, 0x00,
0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x00, 0x19,
0x01, 0x1a, 0x01, 0x20, 0x01, 0x40, 0x01, 0x17, 0x00, 0x00, 0x01,
0x80, 0x01, 0x20, 0x00, 0x10, 0x01, 0xa0, 0x03, 0xd1, 0x00, 0x00,
0x01, 0xf2, 0x02, 0x00, 0x00, 0x13, 0x00, 0x00, 0x00, 0xf4, 0x02,
0xe0, 0x00, 0x15, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x17,
0x00, 0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x50, 0x00, 0x00, 0x00,
0x40, 0x00, 0x00, 0x00, 0x71, 0x02, 0x00, 0x00, 0x60, 0x00, 0x00,
0x00, 0x92, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00, 0xb3, 0x02,
0x00, 0x00, 0xa0, 0x00, 0x00, 0x00, 0xd4, 0x00, 0x00, 0x00, 0x40,
0x00, 0x80, 0x00, 0xf5, 0x00, 0x20, 0x00, 0x70, 0x00, 0xa0, 0x02,
0x11, 0x00, 0x16, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20,
0x02, 0x00, 0x00, 0x20, 0x00, 0x10, 0x00, 0x17, 0x00, 0x1b, 0x00,
0x1d, 0x02, 0xdf
};    

static unsigned char page_one[] __initdata = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x19, 0x00,
0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xd8, 0x00, 0x00,
0x02, 0x20, 0x00, 0x19, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x01,
0xc0, 0x01, 0xfa, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x40, 0x02, 0x60,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0xc0, 0x02, 0x80, 0x00,
0x00, 0x02, 0xfb, 0x02, 0xa0, 0x00, 0x00, 0x00, 0x1b, 0x02, 0xd7,
0x00, 0x00, 0x02, 0xf7, 0x03, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00,
0x1c, 0x03, 0x3c, 0x00, 0x00, 0x03, 0x3f, 0x00, 0x00, 0x03, 0xc0,
0x00, 0x00, 0x03, 0xdf, 0x00, 0x00, 0x00, 0x00, 0x03, 0x5d, 0x00,
0x00, 0x03, 0xc0, 0x00, 0x00, 0x03, 0x7d, 0x00, 0x00, 0x03, 0xc0,
0x00, 0x00, 0x03, 0x9e, 0x00, 0x00, 0x03, 0xc0, 0x00, 0x00, 0x03,
0xbe, 0x00, 0x00, 0x03, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x1b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02,
0xdb, 0x00, 0x00, 0x02, 0xdb, 0x00, 0x00, 0x02, 0xe0, 0x00, 0x00,
0x02, 0xfb, 0x00, 0x00, 0x02, 0xc0, 0x02, 0x40, 0x02, 0xfb, 0x02,
0x60, 0x00, 0x1b
};

static unsigned char page_two[] __initdata = {
0xc4, 0x00, 0x44, 0x07, 0x44, 0x00, 0x40, 0x25, 0x01, 0x06, 0xc4,
0x07, 0x40, 0x25, 0x01, 0x00, 0x46, 0x46, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46, 0x07,
0x05, 0x05, 0x05, 0x04, 0x07, 0x05, 0x04, 0x07, 0x05, 0x44, 0x46,
0x44, 0x46, 0x46, 0x07, 0x05, 0x44, 0x46, 0x05, 0x46, 0x05, 0x46,
0x05, 0x46, 0x05, 0x44, 0x46, 0x05, 0x07, 0x44, 0x46, 0x05, 0x07,
0x44, 0x46, 0x05, 0x07, 0x44, 0x46, 0x05, 0x07, 0x44, 0x05, 0x05,
0x05, 0x44, 0x05, 0x05, 0x05, 0x46, 0x05, 0x46, 0x05, 0x46, 0x05,
0x46, 0x05, 0x46, 0x07, 0x46, 0x07, 0x44
};

static unsigned char page_three[] __initdata = {
0x07, 0x40, 0x00, 0x00, 0x00, 0x47, 0x00, 0x40, 0x00, 0x40, 0x06,
0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x80,
0xc0, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x00, 0x40, 0x00,
0x60, 0x00, 0x70, 0x00, 0x40, 0x00, 0x40, 0x00, 0x42, 0x00, 0x40,
0x00, 0x02, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00,
0x40, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00,
0x00, 0x42, 0x00, 0x40, 0x00, 0x42, 0x00, 0x02, 0x00, 0x02, 0x00,
0x02, 0x00, 0x42, 0x00, 0xc0, 0x00, 0x40
};

static unsigned char page_four[] __initdata = {
0x63, 0x03, 0x26, 0x02, 0x2c, 0x00, 0x24, 0x00, 0x2e, 0x02, 0x02,
0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
0x20, 0x00, 0x60, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20,
0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x20, 0x00, 0x60, 0x00,
0x20, 0x00, 0x60, 0x00, 0x20, 0x00, 0x60, 0x00, 0x20, 0x00, 0x60,
0x00, 0x20, 0x00, 0x60, 0x00, 0x20, 0x00, 0x60, 0x00, 0x20, 0x00,
0x20, 0x00, 0x22, 0x02, 0x22, 0x02, 0x20, 0x00, 0x60, 0x00, 0x22,
0x02, 0x62, 0x02, 0x20, 0x01, 0x21, 0x01
};

static unsigned char page_six[] __initdata = {
0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x04, 0x00, 0x00, 0x06, 0x00,
0x00, 0x08, 0x00, 0x00, 0x0a, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x0e,
0x00, 0x00, 0x10, 0x00, 0x00, 0x12, 0x00, 0x00, 0x14, 0x00, 0x00,
0x16, 0x00, 0x00, 0x18, 0x00, 0x00, 0x1a, 0x00, 0x00, 0x1c, 0x00,
0x00, 0x1e, 0x00, 0x00, 0x20, 0x00, 0x00, 0x22, 0x00, 0x00, 0x24,
0x00, 0x00, 0x26, 0x00, 0x00, 0x28, 0x00, 0x00, 0x2a, 0x00, 0x00,
0x2c, 0x00, 0x00, 0x2e, 0x00, 0x00, 0x30, 0x00, 0x00, 0x32, 0x00,
0x00, 0x34, 0x00, 0x00, 0x36, 0x00, 0x00, 0x38, 0x00, 0x00, 0x3a,
0x00, 0x00, 0x3c, 0x00, 0x00, 0x3e, 0x00, 0x00, 0x40, 0x00, 0x00,
0x42, 0x03, 0x00, 0x44, 0x01, 0x00, 0x46, 0x0a, 0x21, 0x48, 0x0d,
0x23, 0x4a, 0x23, 0x1b, 0x4c, 0x37, 0x8f, 0x4e, 0x45, 0x77, 0x50,
0x52, 0xe2, 0x52, 0x1c, 0x92, 0x54, 0x1c, 0x52, 0x56, 0x07, 0x00,
0x58, 0x2f, 0xc6, 0x5a, 0x0b, 0x00, 0x5c, 0x30, 0x06, 0x5e, 0x17,
0x00, 0x60, 0x3d, 0xda, 0x62, 0x29, 0x00, 0x64, 0x3e, 0x41, 0x66,
0x39, 0x00, 0x68, 0x4c, 0x48, 0x6a, 0x49, 0x00, 0x6c, 0x4c, 0x6c,
0x6e, 0x11, 0xd2, 0x70, 0x16, 0x0c, 0x72, 0x00, 0x00, 0x74, 0x00,
0x80, 0x76, 0x0f, 0x00, 0x78, 0x00, 0x80, 0x7a, 0x13, 0x00, 0x7c,
0x80, 0x00, 0x7e, 0x80, 0x80
};

static unsigned char page_seven[] __initdata = {
0x0f, 0xff, 0x00, 0x00, 0x08, 0x00, 0x08, 0x00, 0x02, 0x00, 0x00,
0x00, 0x00, 0x00, 0x0f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
0x08, 0x00, 0x00, 0x00, 0x0f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x0f,
0xff, 0x0f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xff, 0x0f, 0xff,
0x0f, 0xff, 0x0f, 0xff, 0x02, 0xe9, 0x06, 0x8c, 0x06, 0x8c, 0x0f,
0xff, 0x1a, 0x75, 0x0d, 0x8b, 0x04, 0xe9, 0x0b, 0x16, 0x1a, 0x38,
0x0d, 0xc8, 0x04, 0x6f, 0x0b, 0x91, 0x0f, 0xff, 0x06, 0x40, 0x06,
0x40, 0x02, 0x8f, 0x0f, 0xff, 0x06, 0x62, 0x06, 0x62, 0x02, 0x7b,
0x0f, 0xff, 0x06, 0x97, 0x06, 0x97, 0x02, 0x52, 0x0f, 0xff, 0x06,
0xf6, 0x06, 0xf6, 0x02, 0x19, 0x05, 0x55, 0x05, 0x55, 0x05, 0x55,
0x05, 0x55, 0x05, 0x55, 0x05, 0x55, 0x05, 0x55, 0x05, 0x55, 0x14,
0xda, 0x0d, 0x93, 0x04, 0xda, 0x05, 0x93, 0x14, 0xda, 0x0d, 0x93,
0x04, 0xda, 0x05, 0x93, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x02, 0x00
};

static unsigned char page_zero_v2[] __initdata = {
0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char page_one_v2[] __initdata = {
0x01, 0xc0, 0x01, 0xfa, 0x00, 0x1a, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char page_two_v2[] __initdata = {
0x46, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00
};
static unsigned char page_three_v2[] __initdata = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00
};
static unsigned char page_four_v2[] __initdata = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00
};

static unsigned char page_seven_v2[] __initdata = {
0x0f, 0xff, 0x0f, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char mod_v2[] __initdata = {
0x01, 0x00, 0x02, 0x00, 0x01, 0x01, 0x02, 0x00, 0x01, 0x02, 0x02,
0x00, 0x01, 0x03, 0x02, 0x00, 0x01, 0x04, 0x02, 0x00, 0x01, 0x05,
0x02, 0x00, 0x01, 0x06, 0x02, 0x00, 0x01, 0x07, 0x02, 0x00, 0xb0,
0x20, 0xb1, 0x20, 0xb2, 0x20, 0xb3, 0x20, 0xb4, 0x20, 0xb5, 0x20,
0xb6, 0x20, 0xb7, 0x20, 0xf0, 0x20, 0xf1, 0x20, 0xf2, 0x20, 0xf3,
0x20, 0xf4, 0x20, 0xf5, 0x20, 0xf6, 0x20, 0xf7, 0x20, 0x10, 0xff,
0x11, 0xff, 0x12, 0xff, 0x13, 0xff, 0x14, 0xff, 0x15, 0xff, 0x16,
0xff, 0x17, 0xff, 0x20, 0xff, 0x21, 0xff, 0x22, 0xff, 0x23, 0xff,
0x24, 0xff, 0x25, 0xff, 0x26, 0xff, 0x27, 0xff, 0x30, 0x00, 0x31,
0x00, 0x32, 0x00, 0x33, 0x00, 0x34, 0x00, 0x35, 0x00, 0x36, 0x00,
0x37, 0x00, 0x40, 0x00, 0x41, 0x00, 0x42, 0x00, 0x43, 0x00, 0x44,
0x00, 0x45, 0x00, 0x46, 0x00, 0x47, 0x00, 0x50, 0x00, 0x51, 0x00,
0x52, 0x00, 0x53, 0x00, 0x54, 0x00, 0x55, 0x00, 0x56, 0x00, 0x57,
0x00, 0x60, 0x00, 0x61, 0x00, 0x62, 0x00, 0x63, 0x00, 0x64, 0x00,
0x65, 0x00, 0x66, 0x00, 0x67, 0x00, 0x70, 0xc0, 0x71, 0xc0, 0x72,
0xc0, 0x73, 0xc0, 0x74, 0xc0, 0x75, 0xc0, 0x76, 0xc0, 0x77, 0xc0,
0x80, 0x00, 0x81, 0x00, 0x82, 0x00, 0x83, 0x00, 0x84, 0x00, 0x85,
0x00, 0x86, 0x00, 0x87, 0x00, 0x90, 0x00, 0x91, 0x00, 0x92, 0x00,
0x93, 0x00, 0x94, 0x00, 0x95, 0x00, 0x96, 0x00, 0x97, 0x00, 0xa0,
0x00, 0xa1, 0x00, 0xa2, 0x00, 0xa3, 0x00, 0xa4, 0x00, 0xa5, 0x00,
0xa6, 0x00, 0xa7, 0x00, 0xc0, 0x00, 0xc1, 0x00, 0xc2, 0x00, 0xc3,
0x00, 0xc4, 0x00, 0xc5, 0x00, 0xc6, 0x00, 0xc7, 0x00, 0xd0, 0x00,
0xd1, 0x00, 0xd2, 0x00, 0xd3, 0x00, 0xd4, 0x00, 0xd5, 0x00, 0xd6,
0x00, 0xd7, 0x00, 0xe0, 0x00, 0xe1, 0x00, 0xe2, 0x00, 0xe3, 0x00,
0xe4, 0x00, 0xe5, 0x00, 0xe6, 0x00, 0xe7, 0x00, 0x01, 0x00, 0x02,
0x01, 0x01, 0x01, 0x02, 0x01, 0x01, 0x02, 0x02, 0x01, 0x01, 0x03,
0x02, 0x01, 0x01, 0x04, 0x02, 0x01, 0x01, 0x05, 0x02, 0x01, 0x01,
0x06, 0x02, 0x01, 0x01, 0x07, 0x02, 0x01
};
static unsigned char coefficients[] __initdata = {
0x07, 0x46, 0x00, 0x00, 0x07, 0x49, 0x00, 0x00, 0x00, 0x4b, 0x03,
0x11, 0x00, 0x4d, 0x01, 0x32, 0x07, 0x46, 0x00, 0x00, 0x07, 0x49,
0x00, 0x00, 0x07, 0x40, 0x00, 0x00, 0x07, 0x41, 0x00, 0x00, 0x01,
0x40, 0x02, 0x40, 0x01, 0x41, 0x02, 0x60, 0x07, 0x40, 0x00, 0x00,
0x07, 0x41, 0x00, 0x00, 0x07, 0x47, 0x00, 0x00, 0x07, 0x4a, 0x00,
0x00, 0x00, 0x47, 0x01, 0x00, 0x00, 0x4a, 0x01, 0x20, 0x07, 0x47,
0x00, 0x00, 0x07, 0x4a, 0x00, 0x00, 0x07, 0x7c, 0x00, 0x00, 0x07,
0x7e, 0x00, 0x00, 0x00, 0x00, 0x01, 0x1c, 0x07, 0x7c, 0x00, 0x00,
0x07, 0x7e, 0x00, 0x00, 0x07, 0x44, 0x00, 0x00, 0x00, 0x44, 0x01,
0x00, 0x07, 0x44, 0x00, 0x00, 0x07, 0x42, 0x00, 0x00, 0x07, 0x43,
0x00, 0x00, 0x00, 0x42, 0x01, 0x1a, 0x00, 0x43, 0x01, 0x20, 0x07,
0x42, 0x00, 0x00, 0x07, 0x43, 0x00, 0x00, 0x07, 0x40, 0x00, 0x00,
0x07, 0x41, 0x00, 0x00, 0x01, 0x40, 0x02, 0x40, 0x01, 0x41, 0x02,
0x60, 0x07, 0x40, 0x00, 0x00, 0x07, 0x41, 0x00, 0x00, 0x07, 0x44,
0x0f, 0xff, 0x07, 0x42, 0x00, 0x00, 0x07, 0x43, 0x00, 0x00, 0x07,
0x40, 0x00, 0x00, 0x07, 0x41, 0x00, 0x00, 0x07, 0x51, 0x06, 0x40,
0x07, 0x50, 0x06, 0x40, 0x07, 0x4f, 0x03, 0x81, 0x07, 0x53, 0x1a,
0x76, 0x07, 0x54, 0x0d, 0x8b, 0x07, 0x55, 0x04, 0xe9, 0x07, 0x56,
0x0b, 0x17, 0x07, 0x57, 0x1a, 0x38, 0x07, 0x58, 0x0d, 0xc9, 0x07,
0x59, 0x04, 0x6f, 0x07, 0x5a, 0x0b, 0x91, 0x07, 0x73, 0x14, 0xda,
0x07, 0x74, 0x0d, 0x93, 0x07, 0x75, 0x04, 0xd9, 0x07, 0x76, 0x05,
0x93, 0x07, 0x77, 0x14, 0xda, 0x07, 0x78, 0x0d, 0x93, 0x07, 0x79,
0x04, 0xd9, 0x07, 0x7a, 0x05, 0x93, 0x07, 0x5e, 0x03, 0x68, 0x07,
0x5c, 0x04, 0x31, 0x07, 0x5d, 0x04, 0x31, 0x07, 0x62, 0x03, 0x52,
0x07, 0x60, 0x04, 0x76, 0x07, 0x61, 0x04, 0x76, 0x07, 0x66, 0x03,
0x2e, 0x07, 0x64, 0x04, 0xda, 0x07, 0x65, 0x04, 0xda, 0x07, 0x6a,
0x02, 0xf6, 0x07, 0x68, 0x05, 0x62, 0x07, 0x69, 0x05, 0x62, 0x06,
0x46, 0x0a, 0x22, 0x06, 0x48, 0x0d, 0x24, 0x06, 0x6e, 0x11, 0xd3,
0x06, 0x70, 0x15, 0xcb, 0x06, 0x52, 0x20, 0x93, 0x06, 0x54, 0x20,
0x54, 0x06, 0x4a, 0x27, 0x1d, 0x06, 0x58, 0x2f, 0xc8, 0x06, 0x5c,
0x30, 0x07, 0x06, 0x4c, 0x37, 0x90, 0x06, 0x60, 0x3d, 0xdb, 0x06,
0x64, 0x3e, 0x42, 0x06, 0x4e, 0x45, 0x78, 0x06, 0x68, 0x4c, 0x48,
0x06, 0x6c, 0x4c, 0x6c, 0x06, 0x50, 0x52, 0xe2, 0x06, 0x42, 0x02,
0xba
};
static unsigned char coefficients2[] __initdata = {
0x07, 0x46, 0x00, 0x00, 0x07, 0x49, 0x00, 0x00, 0x07, 0x45, 0x0f,
0xff, 0x07, 0x48, 0x0f, 0xff, 0x07, 0x7b, 0x04, 0xcc, 0x07, 0x7d,
0x04, 0xcc, 0x07, 0x7c, 0x00, 0x00, 0x07, 0x7e, 0x00, 0x00, 0x07,
0x46, 0x00, 0x00, 0x07, 0x49, 0x00, 0x00, 0x07, 0x47, 0x00, 0x00,
0x07, 0x4a, 0x00, 0x00, 0x07, 0x4c, 0x00, 0x00, 0x07, 0x4e, 0x00, 0x00
};
static unsigned char coefficients3[] __initdata = { 
0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x28, 0x00, 0x51, 0x00,
0x51, 0x00, 0x7a, 0x00, 0x7a, 0x00, 0xa3, 0x00, 0xa3, 0x00, 0xcc,
0x00, 0xcc, 0x00, 0xf5, 0x00, 0xf5, 0x01, 0x1e, 0x01, 0x1e, 0x01,
0x47, 0x01, 0x47, 0x01, 0x70, 0x01, 0x70, 0x01, 0x99, 0x01, 0x99,
0x01, 0xc2, 0x01, 0xc2, 0x01, 0xeb, 0x01, 0xeb, 0x02, 0x14, 0x02,
0x14, 0x02, 0x3d, 0x02, 0x3d, 0x02, 0x66, 0x02, 0x66, 0x02, 0x8f,
0x02, 0x8f, 0x02, 0xb8, 0x02, 0xb8, 0x02, 0xe1, 0x02, 0xe1, 0x03,
0x0a, 0x03, 0x0a, 0x03, 0x33, 0x03, 0x33, 0x03, 0x5c, 0x03, 0x5c,
0x03, 0x85, 0x03, 0x85, 0x03, 0xae, 0x03, 0xae, 0x03, 0xd7, 0x03,
0xd7, 0x04, 0x00, 0x04, 0x00, 0x04, 0x28, 0x04, 0x28, 0x04, 0x51,
0x04, 0x51, 0x04, 0x7a, 0x04, 0x7a, 0x04, 0xa3, 0x04, 0xa3, 0x04,
0xcc, 0x04, 0xcc, 0x04, 0xf5, 0x04, 0xf5, 0x05, 0x1e, 0x05, 0x1e,
0x05, 0x47, 0x05, 0x47, 0x05, 0x70, 0x05, 0x70, 0x05, 0x99, 0x05,
0x99, 0x05, 0xc2, 0x05, 0xc2, 0x05, 0xeb, 0x05, 0xeb, 0x06, 0x14,
0x06, 0x14, 0x06, 0x3d, 0x06, 0x3d, 0x06, 0x66, 0x06, 0x66, 0x06,
0x8f, 0x06, 0x8f, 0x06, 0xb8, 0x06, 0xb8, 0x06, 0xe1, 0x06, 0xe1,
0x07, 0x0a, 0x07, 0x0a, 0x07, 0x33, 0x07, 0x33, 0x07, 0x5c, 0x07,
0x5c, 0x07, 0x85, 0x07, 0x85, 0x07, 0xae, 0x07, 0xae, 0x07, 0xd7,
0x07, 0xd7, 0x08, 0x00, 0x08, 0x00, 0x08, 0x28, 0x08, 0x28, 0x08,
0x51, 0x08, 0x51, 0x08, 0x7a, 0x08, 0x7a, 0x08, 0xa3, 0x08, 0xa3,
0x08, 0xcc, 0x08, 0xcc, 0x08, 0xf5, 0x08, 0xf5, 0x09, 0x1e, 0x09,
0x1e, 0x09, 0x47, 0x09, 0x47, 0x09, 0x70, 0x09, 0x70, 0x09, 0x99,
0x09, 0x99, 0x09, 0xc2, 0x09, 0xc2, 0x09, 0xeb, 0x09, 0xeb, 0x0a,
0x14, 0x0a, 0x14, 0x0a, 0x3d, 0x0a, 0x3d, 0x0a, 0x66, 0x0a, 0x66,
0x0a, 0x8f, 0x0a, 0x8f, 0x0a, 0xb8, 0x0a, 0xb8, 0x0a, 0xe1, 0x0a,
0xe1, 0x0b, 0x0a, 0x0b, 0x0a, 0x0b, 0x33, 0x0b, 0x33, 0x0b, 0x5c,
0x0b, 0x5c, 0x0b, 0x85, 0x0b, 0x85, 0x0b, 0xae, 0x0b, 0xae, 0x0b,
0xd7, 0x0b, 0xd7, 0x0c, 0x00, 0x0c, 0x00, 0x0c, 0x28, 0x0c, 0x28,
0x0c, 0x51, 0x0c, 0x51, 0x0c, 0x7a, 0x0c, 0x7a, 0x0c, 0xa3, 0x0c,
0xa3, 0x0c, 0xcc, 0x0c, 0xcc, 0x0c, 0xf5, 0x0c, 0xf5, 0x0d, 0x1e,
0x0d, 0x1e, 0x0d, 0x47, 0x0d, 0x47, 0x0d, 0x70, 0x0d, 0x70, 0x0d,
0x99, 0x0d, 0x99, 0x0d, 0xc2, 0x0d, 0xc2, 0x0d, 0xeb, 0x0d, 0xeb,
0x0e, 0x14, 0x0e, 0x14, 0x0e, 0x3d, 0x0e, 0x3d, 0x0e, 0x66, 0x0e,
0x66, 0x0e, 0x8f, 0x0e, 0x8f, 0x0e, 0xb8, 0x0e, 0xb8, 0x0e, 0xe1,
0x0e, 0xe1, 0x0f, 0x0a, 0x0f, 0x0a, 0x0f, 0x33, 0x0f, 0x33, 0x0f,
0x5c, 0x0f, 0x5c, 0x0f, 0x85, 0x0f, 0x85, 0x0f, 0xae, 0x0f, 0xae,
0x0f, 0xd7, 0x0f, 0xd7, 0x0f, 0xff, 0x0f, 0xff
};

