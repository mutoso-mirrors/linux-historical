/*
 *  Copyright (c) 1999 by Uros Bizjak <uros@kss-loka.si>
 *                        Takashi Iwai <tiwai@suse.de>
 *
 *  SB16ASP/AWE32 CSP control
 *
 *  CSP microcode loader:
 *   alsa-tools/sb16_csp/ 
 *
 *   This program is free software; you can redistribute it and/or modify 
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <sound/driver.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/sb16_csp.h>
#include <sound/initval.h>

#define chip_t snd_sb_csp_t

MODULE_AUTHOR("Uros Bizjak <uros@kss-loka.si>");
MODULE_DESCRIPTION("ALSA driver for SB16 Creative Signal Processor");
MODULE_LICENSE("GPL");
MODULE_CLASSES("{sound}");

#ifdef SNDRV_LITTLE_ENDIAN
#define CSP_HDR_VALUE(a,b,c,d)	((a) | ((b)<<8) | ((c)<<16) | ((d)<<24))
#else
#define CSP_HDR_VALUE(a,b,c,d)	((d) | ((c)<<8) | ((b)<<16) | ((a)<<24))
#endif
#define LE_SHORT(v)		le16_to_cpu(v)
#define LE_INT(v)		le32_to_cpu(v)

#define RIFF_HEADER	CSP_HDR_VALUE('R', 'I', 'F', 'F')
#define CSP__HEADER	CSP_HDR_VALUE('C', 'S', 'P', ' ')
#define LIST_HEADER	CSP_HDR_VALUE('L', 'I', 'S', 'T')
#define FUNC_HEADER	CSP_HDR_VALUE('f', 'u', 'n', 'c')
#define CODE_HEADER	CSP_HDR_VALUE('c', 'o', 'd', 'e')
#define INIT_HEADER	CSP_HDR_VALUE('i', 'n', 'i', 't')
#define MAIN_HEADER	CSP_HDR_VALUE('m', 'a', 'i', 'n')

/*
 * RIFF data format
 */
typedef struct riff_header {
	__u32 name;
	__u32 len;
} riff_header_t;

typedef struct desc_header {
	riff_header_t info;
	__u16 func_nr;
	__u16 VOC_type;
	__u16 flags_play_rec;
	__u16 flags_16bit_8bit;
	__u16 flags_stereo_mono;
	__u16 flags_rates;
} desc_header_t;

/*
 * prototypes
 */
static void snd_sb_csp_free(snd_hwdep_t *hw);
static int snd_sb_csp_open(snd_hwdep_t * hw, struct file *file);
static int snd_sb_csp_ioctl(snd_hwdep_t * hw, struct file *file, unsigned int cmd, unsigned long arg);
static int snd_sb_csp_release(snd_hwdep_t * hw, struct file *file);

static int csp_detect(sb_t *chip, int *version);
static int set_codec_parameter(sb_t *chip, unsigned char par, unsigned char val);
static int set_register(sb_t *chip, unsigned char reg, unsigned char val);
static int read_register(sb_t *chip, unsigned char reg);
static int set_mode_register(sb_t *chip, unsigned char mode);
static int get_version(sb_t *chip);

static int snd_sb_csp_riff_load(snd_sb_csp_t * p, snd_sb_csp_microcode_t * code);
static int snd_sb_csp_unload(snd_sb_csp_t * p);
static int snd_sb_csp_load(snd_sb_csp_t * p, const unsigned char *buf, int size, int load_flags);
static int snd_sb_csp_autoload(snd_sb_csp_t * p, int pcm_sfmt, int play_rec_mode);
static int snd_sb_csp_check_version(snd_sb_csp_t * p);

static int snd_sb_csp_use(snd_sb_csp_t * p);
static int snd_sb_csp_unuse(snd_sb_csp_t * p);
static int snd_sb_csp_start(snd_sb_csp_t * p, int sample_width, int channels);
static int snd_sb_csp_stop(snd_sb_csp_t * p);
static int snd_sb_csp_pause(snd_sb_csp_t * p);
static int snd_sb_csp_restart(snd_sb_csp_t * p);

static int snd_sb_qsound_build(snd_sb_csp_t * p);
static void snd_sb_qsound_destroy(snd_sb_csp_t * p);
static int snd_sb_csp_qsound_transfer(snd_sb_csp_t * p);

static int init_proc_entry(snd_sb_csp_t * p, int device);
static void info_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer);

/*
 * Detect CSP chip and create a new instance
 */
int snd_sb_csp_new(sb_t *chip, int device, snd_hwdep_t ** rhwdep)
{
	snd_sb_csp_t *p;
	int version, err;
	snd_hwdep_t *hw;

	if (rhwdep)
		*rhwdep = NULL;

	if (csp_detect(chip, &version))
		return -ENODEV;

	if ((err = snd_hwdep_new(chip->card, "SB16-CSP", device, &hw)) < 0)
		return err;

	if ((p = snd_magic_kcalloc(snd_sb_csp_t, 0, GFP_KERNEL)) == NULL) {
		snd_device_free(chip->card, hw);
		return -ENOMEM;
	}
	p->chip = chip;
	p->version = version;

	/* CSP operators */
	p->ops.csp_use = snd_sb_csp_use;
	p->ops.csp_unuse = snd_sb_csp_unuse;
	p->ops.csp_autoload = snd_sb_csp_autoload;
	p->ops.csp_start = snd_sb_csp_start;
	p->ops.csp_stop = snd_sb_csp_stop;
	p->ops.csp_qsound_transfer = snd_sb_csp_qsound_transfer;

	init_MUTEX(&p->access_mutex);
	sprintf(hw->name, "CSP v%d.%d", (version >> 4), (version & 0x0f));
	hw->iface = SNDRV_HWDEP_IFACE_SB16CSP;
	hw->private_data = p;
	hw->private_free = snd_sb_csp_free;

	/* operators - only write/ioctl */
	hw->ops.open = snd_sb_csp_open;
	hw->ops.ioctl = snd_sb_csp_ioctl;
	hw->ops.release = snd_sb_csp_release;

	/* create a proc entry */
	init_proc_entry(p, device);
	if (rhwdep)
		*rhwdep = hw;
	return 0;
}

/*
 * free_private for hwdep instance
 */
static void snd_sb_csp_free(snd_hwdep_t *hwdep)
{
	snd_sb_csp_t *p = snd_magic_cast(snd_sb_csp_t, hwdep->private_data, return);
	if (p) {
		if (p->running & SNDRV_SB_CSP_ST_RUNNING)
			snd_sb_csp_stop(p);
		snd_magic_kfree(p);
	}
}

/* ------------------------------ */

/*
 * open the device exclusively
 */
static int snd_sb_csp_open(snd_hwdep_t * hw, struct file *file)
{
	snd_sb_csp_t *p = snd_magic_cast(snd_sb_csp_t, hw->private_data, return -ENXIO);
	return (snd_sb_csp_use(p));
}

/*
 * ioctl for hwdep device:
 */
static int snd_sb_csp_ioctl(snd_hwdep_t * hw, struct file *file, unsigned int cmd, unsigned long arg)
{
	snd_sb_csp_t *p = snd_magic_cast(snd_sb_csp_t, hw->private_data, return -ENXIO);
	snd_sb_csp_info_t info;
	snd_sb_csp_start_t start_info;
	int err;

	snd_assert(p != NULL, return -EINVAL);

	if (snd_sb_csp_check_version(p))
		return -ENODEV;

	switch (cmd) {
		/* get information */
	case SNDRV_SB_CSP_IOCTL_INFO:
		*info.codec_name = *p->codec_name;
		info.func_nr = p->func_nr;
		info.acc_format = p->acc_format;
		info.acc_channels = p->acc_channels;
		info.acc_width = p->acc_width;
		info.acc_rates = p->acc_rates;
		info.csp_mode = p->mode;
		info.run_channels = p->run_channels;
		info.run_width = p->run_width;
		info.version = p->version;
		info.state = p->running;
		if (copy_to_user((void *) arg, &info, sizeof(info)))
			err = -EFAULT;
		else
			err = 0;
		break;

		/* load CSP microcode */
	case SNDRV_SB_CSP_IOCTL_LOAD_CODE:
		err = (p->running & SNDRV_SB_CSP_ST_RUNNING ?
		       -EBUSY : snd_sb_csp_riff_load(p, (snd_sb_csp_microcode_t *) arg));
		break;
	case SNDRV_SB_CSP_IOCTL_UNLOAD_CODE:
		err = (p->running & SNDRV_SB_CSP_ST_RUNNING ?
		       -EBUSY : snd_sb_csp_unload(p));
		break;

		/* change CSP running state */
	case SNDRV_SB_CSP_IOCTL_START:
		if (copy_from_user(&start_info, (void *) arg, sizeof(start_info)))
			err = -EFAULT;
		else
			err = snd_sb_csp_start(p, start_info.sample_width, start_info.channels);
		break;
	case SNDRV_SB_CSP_IOCTL_STOP:
		err = snd_sb_csp_stop(p);
		break;
	case SNDRV_SB_CSP_IOCTL_PAUSE:
		err = snd_sb_csp_pause(p);
		break;
	case SNDRV_SB_CSP_IOCTL_RESTART:
		err = snd_sb_csp_restart(p);
		break;
	default:
		err = -ENOTTY;
		break;
	}

	return err;
}

/*
 * close the device
 */
static int snd_sb_csp_release(snd_hwdep_t * hw, struct file *file)
{
	snd_sb_csp_t *p = snd_magic_cast(snd_sb_csp_t, hw->private_data, return -ENXIO);
	return (snd_sb_csp_unuse(p));
}

/* ------------------------------ */

/*
 * acquire device
 */
static int snd_sb_csp_use(snd_sb_csp_t * p)
{
	down(&p->access_mutex);
	if (p->used) {
		up(&p->access_mutex);
		return -EAGAIN;
	}
	p->used++;
	up(&p->access_mutex);

	return 0;

}

/*
 * release device
 */
static int snd_sb_csp_unuse(snd_sb_csp_t * p)
{
	down(&p->access_mutex);
	p->used--;
	up(&p->access_mutex);

	return 0;
}

/*
 * load microcode via ioctl: 
 * code is user-space pointer
 */
static int snd_sb_csp_riff_load(snd_sb_csp_t * p, snd_sb_csp_microcode_t * mcode)
{
	snd_sb_csp_mc_header_t info;

	unsigned char *data_ptr, *data_end;
	unsigned short func_nr = 0;

	riff_header_t file_h, item_h, code_h;
	__u32 item_type;
	desc_header_t funcdesc_h;

	unsigned long flags;
	int err;

	if (copy_from_user(&info, mcode, sizeof(info)))
		return -EFAULT;
	data_ptr = mcode->data;

	if (copy_from_user(&file_h, data_ptr, sizeof(file_h)))
		return -EFAULT;
	if ((file_h.name != RIFF_HEADER) ||
	    (LE_INT(file_h.len) >= SNDRV_SB_CSP_MAX_MICROCODE_FILE_SIZE - sizeof(file_h))) {
		snd_printd("%s: Invalid RIFF header\n", __FUNCTION__);
		return -EINVAL;
	}
	data_ptr += sizeof(file_h);
	data_end = data_ptr + LE_INT(file_h.len);

	if (copy_from_user(&item_type, data_ptr, sizeof(item_type)))
		return -EFAULT;
	if (item_type != CSP__HEADER) {
		snd_printd("%s: Invalid RIFF file type\n", __FUNCTION__);
		return -EINVAL;
	}
	data_ptr += sizeof (item_type);

	for (; data_ptr < data_end; data_ptr += LE_INT(item_h.len)) {
		if (copy_from_user(&item_h, data_ptr, sizeof(item_h)))
			return -EFAULT;
		data_ptr += sizeof(item_h);
		if (item_h.name != LIST_HEADER)
			continue;

		if (copy_from_user(&item_type, data_ptr, sizeof(item_type)))
			 return -EFAULT;
		switch (item_type) {
		case FUNC_HEADER:
			if (copy_from_user(&funcdesc_h, data_ptr + sizeof(item_type), sizeof(funcdesc_h)))
				return -EFAULT;
			func_nr = LE_SHORT(funcdesc_h.func_nr);
			break;
		case CODE_HEADER:
			if (func_nr != info.func_req)
				break;	/* not required function, try next */
			data_ptr += sizeof(item_type);

			/* destroy QSound mixer element */
			if (p->mode == SNDRV_SB_CSP_MODE_QSOUND) {
				snd_sb_qsound_destroy(p);
			}
			/* Clear all flags */
			p->running = 0;
			p->mode = 0;

			/* load microcode blocks */
			for (;;) {
				if (data_ptr >= data_end)
					return -EINVAL;
				if (copy_from_user(&code_h, data_ptr, sizeof(code_h)))
					return -EFAULT;

				/* init microcode blocks */
				if (code_h.name != INIT_HEADER)
					break;
				data_ptr += sizeof(code_h);
				err = snd_sb_csp_load(p, data_ptr, LE_INT(code_h.len),
						      SNDRV_SB_CSP_LOAD_INITBLOCK | SNDRV_SB_CSP_LOAD_FROMUSER);
				if (err)
					return err;
				data_ptr += LE_INT(code_h.len);
			}
			/* main microcode block */
			if (copy_from_user(&code_h, data_ptr, sizeof(code_h)))
				return -EFAULT;

			if (code_h.name != MAIN_HEADER) {
				snd_printd("%s: Missing 'main' microcode\n", __FUNCTION__);
				return -EINVAL;
			}
			data_ptr += sizeof(code_h);
			err = snd_sb_csp_load(p, data_ptr, LE_INT(code_h.len),
					      SNDRV_SB_CSP_LOAD_FROMUSER);
			if (err)
				return err;

			/* fill in codec header */
			strlcpy(p->codec_name, info.codec_name, sizeof(p->codec_name));
			p->func_nr = func_nr;
			p->mode = LE_SHORT(funcdesc_h.flags_play_rec);
			switch (LE_SHORT(funcdesc_h.VOC_type)) {
			case 0x0001:	/* QSound decoder */
				if (LE_SHORT(funcdesc_h.flags_play_rec) == SNDRV_SB_CSP_MODE_DSP_WRITE) {
					if (snd_sb_qsound_build(p) == 0)
						/* set QSound flag and clear all other mode flags */
						p->mode = SNDRV_SB_CSP_MODE_QSOUND;
				}
				p->acc_format = 0;
				break;
			case 0x0006:	/* A Law codec */
				p->acc_format = SNDRV_PCM_FMTBIT_A_LAW;
				break;
			case 0x0007:	/* Mu Law codec */
				p->acc_format = SNDRV_PCM_FMTBIT_MU_LAW;
				break;
			case 0x0011:	/* what Creative thinks is IMA ADPCM codec */
			case 0x0200:	/* Creative ADPCM codec */
				p->acc_format = SNDRV_PCM_FMTBIT_IMA_ADPCM;
				break;
			case    201:	/* Text 2 Speech decoder */
				/* TODO: Text2Speech handling routines */
				p->acc_format = 0;
				break;
			case 0x0202:	/* Fast Speech 8 codec */
			case 0x0203:	/* Fast Speech 10 codec */
				p->acc_format = SNDRV_PCM_FMTBIT_SPECIAL;
				break;
			default:	/* other codecs are unsupported */
				p->acc_format = p->acc_width = p->acc_rates = 0;
				p->mode = 0;
				snd_printd("%s: Unsupported CSP codec type: 0x%04x\n",
					   __FUNCTION__,
					   LE_SHORT(funcdesc_h.VOC_type));
				return -EINVAL;
			}
			p->acc_channels = LE_SHORT(funcdesc_h.flags_stereo_mono);
			p->acc_width = LE_SHORT(funcdesc_h.flags_16bit_8bit);
			p->acc_rates = LE_SHORT(funcdesc_h.flags_rates);

			/* Decouple CSP from IRQ and DMAREQ lines */
			spin_lock_irqsave(&p->chip->reg_lock, flags);
			set_mode_register(p->chip, 0xfc);
			set_mode_register(p->chip, 0x00);
			spin_unlock_irqrestore(&p->chip->reg_lock, flags);

			/* finished loading successfully */
			p->running = SNDRV_SB_CSP_ST_LOADED;	/* set LOADED flag */
			return 0;
		}
	}
	snd_printd("%s: Function #%d not found\n", __FUNCTION__, info.func_req);
	return -EINVAL;
}

/*
 * unload CSP microcode
 */
static int snd_sb_csp_unload(snd_sb_csp_t * p)
{
	if (p->running & SNDRV_SB_CSP_ST_RUNNING)
		return -EBUSY;
	if (!(p->running & SNDRV_SB_CSP_ST_LOADED))
		return -ENXIO;

	/* clear supported formats */
	p->acc_format = 0;
	p->acc_channels = p->acc_width = p->acc_rates = 0;
	/* destroy QSound mixer element */
	if (p->mode == SNDRV_SB_CSP_MODE_QSOUND) {
		snd_sb_qsound_destroy(p);
	}
	/* clear all flags */
	p->running = 0;
	p->mode = 0;
	return 0;
}

/*
 * send command sequence to DSP
 */
static inline int command_seq(sb_t *chip, const unsigned char *seq, int size)
{
	int i;
	for (i = 0; i < size; i++) {
		if (!snd_sbdsp_command(chip, seq[i]))
			return -EIO;
	}
	return 0;
}

/*
 * set CSP codec parameter
 */
static int set_codec_parameter(sb_t *chip, unsigned char par, unsigned char val)
{
	unsigned char dsp_cmd[3];

	dsp_cmd[0] = 0x05;	/* CSP set codec parameter */
	dsp_cmd[1] = val;	/* Parameter value */
	dsp_cmd[2] = par;	/* Parameter */
	command_seq(chip, dsp_cmd, 3);
	snd_sbdsp_command(chip, 0x03);	/* DSP read? */
	if (snd_sbdsp_get_byte(chip) != par)
		return -EIO;
	return 0;
}

/*
 * set CSP register
 */
static int set_register(sb_t *chip, unsigned char reg, unsigned char val)
{
	unsigned char dsp_cmd[3];

	dsp_cmd[0] = 0x0e;	/* CSP set register */
	dsp_cmd[1] = reg;	/* CSP Register */
	dsp_cmd[2] = val;	/* value */
	return command_seq(chip, dsp_cmd, 3);
}

/*
 * read CSP register
 * return < 0 -> error
 */
static int read_register(sb_t *chip, unsigned char reg)
{
	unsigned char dsp_cmd[2];

	dsp_cmd[0] = 0x0f;	/* CSP read register */
	dsp_cmd[1] = reg;	/* CSP Register */
	command_seq(chip, dsp_cmd, 2);
	return snd_sbdsp_get_byte(chip);	/* Read DSP value */
}

/*
 * set CSP mode register
 */
static int set_mode_register(sb_t *chip, unsigned char mode)
{
	unsigned char dsp_cmd[2];

	dsp_cmd[0] = 0x04;	/* CSP set mode register */
	dsp_cmd[1] = mode;	/* mode */
	return command_seq(chip, dsp_cmd, 2);
}

/*
 * Detect CSP
 * return 0 if CSP exists.
 */
static int csp_detect(sb_t *chip, int *version)
{
	unsigned char csp_test1, csp_test2;
	unsigned long flags;
	int result = -ENODEV;

	spin_lock_irqsave(&chip->reg_lock, flags);

	set_codec_parameter(chip, 0x00, 0x00);
	set_mode_register(chip, 0xfc);		/* 0xfc = ?? */

	csp_test1 = read_register(chip, 0x83);
	set_register(chip, 0x83, ~csp_test1);
	csp_test2 = read_register(chip, 0x83);
	if (csp_test2 != (csp_test1 ^ 0xff))
		goto __fail;

	set_register(chip, 0x83, csp_test1);
	csp_test2 = read_register(chip, 0x83);
	if (csp_test2 != csp_test1)
		goto __fail;

	set_mode_register(chip, 0x00);		/* 0x00 = ? */

	*version = get_version(chip);
	snd_sbdsp_reset(chip);	/* reset DSP after getversion! */
	if (*version >= 0x10 && *version <= 0x1f)
		result = 0;		/* valid version id */

      __fail:
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return result;
}

/*
 * get CSP version number
 */
static int get_version(sb_t *chip)
{
	unsigned char dsp_cmd[2];

	dsp_cmd[0] = 0x08;	/* SB_DSP_!something! */
	dsp_cmd[1] = 0x03;	/* get chip version id? */
	command_seq(chip, dsp_cmd, 2);

	return (snd_sbdsp_get_byte(chip));
}

/*
 * check if the CSP version is valid
 */
static int snd_sb_csp_check_version(snd_sb_csp_t * p)
{
	if (p->version < 0x10 || p->version > 0x1f) {
		snd_printd("%s: Invalid CSP version: 0x%x\n", __FUNCTION__, p->version);
		return 1;
	}
	return 0;
}

/*
 * download microcode to CSP (microcode should have one "main" block).
 */
static int snd_sb_csp_load(snd_sb_csp_t * p, const unsigned char *buf, int size, int load_flags)
{
	int status, i;
	int err;
	int result = -EIO;
	unsigned long flags;

	spin_lock_irqsave(&p->chip->reg_lock, flags);
	snd_sbdsp_command(p->chip, 0x01);	/* CSP download command */
	if (snd_sbdsp_get_byte(p->chip)) {
		snd_printd("%s: Download command failed\n", __FUNCTION__);
		goto __fail;
	}
	/* Send CSP low byte (size - 1) */
	snd_sbdsp_command(p->chip, (unsigned char)(size - 1));
	/* Send high byte */
	snd_sbdsp_command(p->chip, (unsigned char)((size - 1) >> 8));
	/* send microcode sequence */
	if (load_flags & SNDRV_SB_CSP_LOAD_FROMUSER) {
		/* copy microcode from user space */
		unsigned char *kbuf, *_kbuf;
		_kbuf = kbuf = kmalloc (size, GFP_KERNEL);
		if (copy_from_user(kbuf, buf, size)) {
			result = -EFAULT;
			kfree (_kbuf);
			goto __fail;
		}
		while (size--) {
			if (!snd_sbdsp_command(p->chip, *kbuf++)) {
				kfree (_kbuf);
				goto __fail;
			}
		}
		kfree (_kbuf);
	} else {
		/* load from kernel space */
		while (size--) {
			if (!snd_sbdsp_command(p->chip, *buf++))
				goto __fail;
		}
	}
	if (snd_sbdsp_get_byte(p->chip))
		goto __fail;

	if (load_flags & SNDRV_SB_CSP_LOAD_INITBLOCK) {
		i = 0;
		/* some codecs (FastSpeech) take some time to initialize */
		while (1) {
			snd_sbdsp_command(p->chip, 0x03);
			status = snd_sbdsp_get_byte(p->chip);
			if (status == 0x55 || ++i >= 10)
				break;
			udelay (10);
		}
		if (status != 0x55) {
			snd_printd("%s: Microcode initialization failed\n", __FUNCTION__);
			goto __fail;
		}
	} else {
		/*
		 * Read mixer register SB_DSP4_DMASETUP after loading 'main' code.
		 * Start CSP chip if no 16bit DMA channel is set - some kind
		 * of autorun or perhaps a bugfix?
		 */
		spin_lock(&p->chip->mixer_lock);
		status = snd_sbmixer_read(p->chip, SB_DSP4_DMASETUP);
		spin_unlock(&p->chip->mixer_lock);
		if (!(status & (SB_DMASETUP_DMA7 | SB_DMASETUP_DMA6 | SB_DMASETUP_DMA5))) {
			err = (set_codec_parameter(p->chip, 0xaa, 0x00) ||
			       set_codec_parameter(p->chip, 0xff, 0x00));
			snd_sbdsp_reset(p->chip);		/* really! */
			if (err)
				goto __fail;
			set_mode_register(p->chip, 0xc0);	/* c0 = STOP */
			set_mode_register(p->chip, 0x70);	/* 70 = RUN */
		}
	}
	result = 0;

      __fail:
	spin_unlock_irqrestore(&p->chip->reg_lock, flags);
	return result;
}

#include "sb16_csp_codecs.h"

/*
 * autoload hardware codec if necessary
 * return 0 if CSP is loaded and ready to run (p->running != 0)
 */
static int snd_sb_csp_autoload(snd_sb_csp_t * p, int pcm_sfmt, int play_rec_mode)
{
	unsigned long flags;
	int err = 0;

	/* if CSP is running or manually loaded then exit */
	if (p->running & (SNDRV_SB_CSP_ST_RUNNING | SNDRV_SB_CSP_ST_LOADED)) 
		return -EBUSY;

	/* autoload microcode only if requested hardware codec is not already loaded */
	if (((1 << pcm_sfmt) & p->acc_format) && (play_rec_mode & p->mode)) {
		p->running = SNDRV_SB_CSP_ST_AUTO;
	} else {
		switch (pcm_sfmt) {
		case SNDRV_PCM_FORMAT_MU_LAW:
			err = snd_sb_csp_load(p, &mulaw_main[0], sizeof(mulaw_main), 0);
			p->acc_format = SNDRV_PCM_FMTBIT_MU_LAW;
			p->mode = SNDRV_SB_CSP_MODE_DSP_READ | SNDRV_SB_CSP_MODE_DSP_WRITE;
			break;
		case SNDRV_PCM_FORMAT_A_LAW:
			err = snd_sb_csp_load(p, &alaw_main[0], sizeof(alaw_main), 0);
			p->acc_format = SNDRV_PCM_FMTBIT_A_LAW;
			p->mode = SNDRV_SB_CSP_MODE_DSP_READ | SNDRV_SB_CSP_MODE_DSP_WRITE;
			break;
		case SNDRV_PCM_FORMAT_IMA_ADPCM:
			err = snd_sb_csp_load(p, &ima_adpcm_init[0], sizeof(ima_adpcm_init),
					      SNDRV_SB_CSP_LOAD_INITBLOCK);
			if (err)
				break;
			if (play_rec_mode == SNDRV_SB_CSP_MODE_DSP_WRITE) {
				err = snd_sb_csp_load(p, &ima_adpcm_playback[0],
						      sizeof(ima_adpcm_playback), 0);
				p->mode = SNDRV_SB_CSP_MODE_DSP_WRITE;
			} else {
				err = snd_sb_csp_load(p, &ima_adpcm_capture[0],
						      sizeof(ima_adpcm_capture), 0);
				p->mode = SNDRV_SB_CSP_MODE_DSP_READ;
			}
			p->acc_format = SNDRV_PCM_FMTBIT_IMA_ADPCM;
			break;				  
		default:
			/* Decouple CSP from IRQ and DMAREQ lines */
			if (p->running & SNDRV_SB_CSP_ST_AUTO) {
				spin_lock_irqsave(&p->chip->reg_lock, flags);
				set_mode_register(p->chip, 0xfc);
				set_mode_register(p->chip, 0x00);
				spin_unlock_irqrestore(&p->chip->reg_lock, flags);
				p->running = 0;			/* clear autoloaded flag */
			}
			return -EINVAL;
		}
		if (err) {
			p->acc_format = 0;
			p->acc_channels = p->acc_width = p->acc_rates = 0;

			p->running = 0;				/* clear autoloaded flag */
			p->mode = 0;
			return (err);
		} else {
			p->running = SNDRV_SB_CSP_ST_AUTO;	/* set autoloaded flag */
			p->acc_width = SNDRV_SB_CSP_SAMPLE_16BIT;	/* only 16 bit data */
			p->acc_channels = SNDRV_SB_CSP_MONO | SNDRV_SB_CSP_STEREO;
			p->acc_rates = SNDRV_SB_CSP_RATE_ALL;	/* HW codecs accept all rates */
		}   

	}
	return (p->running & SNDRV_SB_CSP_ST_AUTO) ? 0 : -ENXIO;
}

/*
 * start CSP
 */
static int snd_sb_csp_start(snd_sb_csp_t * p, int sample_width, int channels)
{
	unsigned char s_type;	/* sample type */
	unsigned char mixL, mixR;
	int result = -EIO;
	unsigned long flags;

	if (!(p->running & (SNDRV_SB_CSP_ST_LOADED | SNDRV_SB_CSP_ST_AUTO))) {
		snd_printd("%s: Microcode not loaded\n", __FUNCTION__);
		return -ENXIO;
	}
	if (p->running & SNDRV_SB_CSP_ST_RUNNING) {
		snd_printd("%s: CSP already running\n", __FUNCTION__);
		return -EBUSY;
	}
	if (!(sample_width & p->acc_width)) {
		snd_printd("%s: Unsupported PCM sample width\n", __FUNCTION__);
		return -EINVAL;
	}
	if (!(channels & p->acc_channels)) {
		snd_printd("%s: Invalid number of channels\n", __FUNCTION__);
		return -EINVAL;
	}

	/* Mute PCM volume */
	spin_lock_irqsave(&p->chip->mixer_lock, flags);
	mixL = snd_sbmixer_read(p->chip, SB_DSP4_PCM_DEV);
	mixR = snd_sbmixer_read(p->chip, SB_DSP4_PCM_DEV + 1);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV, mixL & 0x7);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV + 1, mixR & 0x7);

	spin_lock(&p->chip->reg_lock);
	set_mode_register(p->chip, 0xc0);	/* c0 = STOP */
	set_mode_register(p->chip, 0x70);	/* 70 = RUN */

	s_type = 0x00;
	if (channels == SNDRV_SB_CSP_MONO)
		s_type = 0x11;	/* 000n 000n    (n = 1 if mono) */
	if (sample_width == SNDRV_SB_CSP_SAMPLE_8BIT)
		s_type |= 0x22;	/* 00dX 00dX    (d = 1 if 8 bit samples) */

	if (set_codec_parameter(p->chip, 0x81, s_type)) {
		snd_printd("%s: Set sample type command failed\n", __FUNCTION__);
		goto __fail;
	}
	if (set_codec_parameter(p->chip, 0x80, 0x00)) {
		snd_printd("%s: Codec start command failed\n", __FUNCTION__);
		goto __fail;
	}
	p->run_width = sample_width;
	p->run_channels = channels;

	p->running |= SNDRV_SB_CSP_ST_RUNNING;

	if (p->mode & SNDRV_SB_CSP_MODE_QSOUND) {
		set_codec_parameter(p->chip, 0xe0, 0x01);
		/* enable QSound decoder */
		set_codec_parameter(p->chip, 0x00, 0xff);
		set_codec_parameter(p->chip, 0x01, 0xff);
		p->running |= SNDRV_SB_CSP_ST_QSOUND;
		/* set QSound startup value */
		snd_sb_csp_qsound_transfer(p);
	}
	result = 0;

      __fail:
	spin_unlock(&p->chip->reg_lock);

	/* restore PCM volume */
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV, mixL);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV + 1, mixR);
	spin_unlock_irqrestore(&p->chip->mixer_lock, flags);

	return result;
}

/*
 * stop CSP
 */
static int snd_sb_csp_stop(snd_sb_csp_t * p)
{
	int result;
	unsigned char mixL, mixR;
	unsigned long flags;

	if (!(p->running & SNDRV_SB_CSP_ST_RUNNING))
		return 0;

	/* Mute PCM volume */
	spin_lock_irqsave(&p->chip->mixer_lock, flags);
	mixL = snd_sbmixer_read(p->chip, SB_DSP4_PCM_DEV);
	mixR = snd_sbmixer_read(p->chip, SB_DSP4_PCM_DEV + 1);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV, mixL & 0x7);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV + 1, mixR & 0x7);

	spin_lock(&p->chip->reg_lock);
	if (p->running & SNDRV_SB_CSP_ST_QSOUND) {
		set_codec_parameter(p->chip, 0xe0, 0x01);
		/* disable QSound decoder */
		set_codec_parameter(p->chip, 0x00, 0x00);
		set_codec_parameter(p->chip, 0x01, 0x00);

		p->running &= ~SNDRV_SB_CSP_ST_QSOUND;
	}
	result = set_mode_register(p->chip, 0xc0);	/* c0 = STOP */
	spin_unlock(&p->chip->reg_lock);

	/* restore PCM volume */
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV, mixL);
	snd_sbmixer_write(p->chip, SB_DSP4_PCM_DEV + 1, mixR);
	spin_unlock_irqrestore(&p->chip->mixer_lock, flags);

	if (!(result))
		p->running &= ~(SNDRV_SB_CSP_ST_PAUSED | SNDRV_SB_CSP_ST_RUNNING);
	return result;
}

/*
 * pause CSP codec and hold DMA transfer
 */
static int snd_sb_csp_pause(snd_sb_csp_t * p)
{
	int result;
	unsigned long flags;

	if (!(p->running & SNDRV_SB_CSP_ST_RUNNING))
		return -EBUSY;

	spin_lock_irqsave(&p->chip->reg_lock, flags);
	result = set_codec_parameter(p->chip, 0x80, 0xff);
	spin_unlock_irqrestore(&p->chip->reg_lock, flags);
	if (!(result))
		p->running |= SNDRV_SB_CSP_ST_PAUSED;

	return result;
}

/*
 * restart CSP codec and resume DMA transfer
 */
static int snd_sb_csp_restart(snd_sb_csp_t * p)
{
	int result;
	unsigned long flags;

	if (!(p->running & SNDRV_SB_CSP_ST_PAUSED))
		return -EBUSY;

	spin_lock_irqsave(&p->chip->reg_lock, flags);
	result = set_codec_parameter(p->chip, 0x80, 0x00);
	spin_unlock_irqrestore(&p->chip->reg_lock, flags);
	if (!(result))
		p->running &= ~SNDRV_SB_CSP_ST_PAUSED;

	return result;
}

/* ------------------------------ */

/*
 * QSound mixer control for PCM
 */

static int snd_sb_qsound_switch_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_sb_qsound_switch_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_sb_csp_t *p = snd_kcontrol_chip(kcontrol);
	
	ucontrol->value.integer.value[0] = p->q_enabled ? 1 : 0;
	return 0;
}

static int snd_sb_qsound_switch_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_sb_csp_t *p = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned char nval;
	
	nval = ucontrol->value.integer.value[0] & 0x01;
	spin_lock_irqsave(&p->q_lock, flags);
	change = p->q_enabled != nval;
	p->q_enabled = nval;
	spin_unlock_irqrestore(&p->q_lock, flags);
	return change;
}

static int snd_sb_qsound_space_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = SNDRV_SB_CSP_QSOUND_MAX_RIGHT;
	return 0;
}

static int snd_sb_qsound_space_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_sb_csp_t *p = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&p->q_lock, flags);
	ucontrol->value.integer.value[0] = p->qpos_left;
	ucontrol->value.integer.value[1] = p->qpos_right;
	spin_unlock_irqrestore(&p->q_lock, flags);
	return 0;
}

static int snd_sb_qsound_space_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	snd_sb_csp_t *p = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned char nval1, nval2;
	
	nval1 = ucontrol->value.integer.value[0];
	if (nval1 > SNDRV_SB_CSP_QSOUND_MAX_RIGHT)
		nval1 = SNDRV_SB_CSP_QSOUND_MAX_RIGHT;
	nval2 = ucontrol->value.integer.value[1];
	if (nval2 > SNDRV_SB_CSP_QSOUND_MAX_RIGHT)
		nval2 = SNDRV_SB_CSP_QSOUND_MAX_RIGHT;
	spin_lock_irqsave(&p->q_lock, flags);
	change = p->qpos_left != nval1 || p->qpos_right != nval2;
	p->qpos_left = nval1;
	p->qpos_right = nval2;
	p->qpos_changed = change;
	spin_unlock_irqrestore(&p->q_lock, flags);
	return change;
}

static snd_kcontrol_new_t snd_sb_qsound_switch = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "3D Control - Switch",
	.info = snd_sb_qsound_switch_info,
	.get = snd_sb_qsound_switch_get,
	.put = snd_sb_qsound_switch_put
};

static snd_kcontrol_new_t snd_sb_qsound_space = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "3D Control - Space",
	.info = snd_sb_qsound_space_info,
	.get = snd_sb_qsound_space_get,
	.put = snd_sb_qsound_space_put
};

static int snd_sb_qsound_build(snd_sb_csp_t * p)
{
	snd_card_t * card;
	int err;

	snd_assert(p != NULL, return -EINVAL);

	card = p->chip->card;
	p->qpos_left = p->qpos_right = SNDRV_SB_CSP_QSOUND_MAX_RIGHT / 2;
	p->qpos_changed = 0;

	spin_lock_init(&p->q_lock);

	if ((err = snd_ctl_add(card, p->qsound_switch = snd_ctl_new1(&snd_sb_qsound_switch, p))) < 0)
		goto __error;
	if ((err = snd_ctl_add(card, p->qsound_space = snd_ctl_new1(&snd_sb_qsound_space, p))) < 0)
		goto __error;

	return 0;

     __error:
	snd_sb_qsound_destroy(p);
	return err;
}

static void snd_sb_qsound_destroy(snd_sb_csp_t * p)
{
	snd_card_t * card;
	unsigned long flags;

	snd_assert(p != NULL, return);

	card = p->chip->card;	
	
	down_write(&card->controls_rwsem);
	if (p->qsound_switch)
		snd_ctl_remove(card, p->qsound_switch);
	if (p->qsound_space)
		snd_ctl_remove(card, p->qsound_space);
	up_write(&card->controls_rwsem);

	/* cancel pending transfer of QSound parameters */
	spin_lock_irqsave (&p->q_lock, flags);
	p->qpos_changed = 0;
	spin_unlock_irqrestore (&p->q_lock, flags);
}

/*
 * Transfer qsound parameters to CSP,
 * function should be called from interrupt routine
 */
static int snd_sb_csp_qsound_transfer(snd_sb_csp_t * p)
{
	int err = -ENXIO;

	spin_lock(&p->q_lock);
	if (p->running & SNDRV_SB_CSP_ST_QSOUND) {
		set_codec_parameter(p->chip, 0xe0, 0x01);
		/* left channel */
		set_codec_parameter(p->chip, 0x00, p->qpos_left);
		set_codec_parameter(p->chip, 0x02, 0x00);
		/* right channel */
		set_codec_parameter(p->chip, 0x00, p->qpos_right);
		set_codec_parameter(p->chip, 0x03, 0x00);
		err = 0;
	}
	p->qpos_changed = 0;
	spin_unlock(&p->q_lock);
	return err;
}

/* ------------------------------ */

/*
 * proc interface
 */
static int init_proc_entry(snd_sb_csp_t * p, int device)
{
	char name[16];
	snd_info_entry_t *entry;
	sprintf(name, "cspD%d", device);
	if (! snd_card_proc_new(p->chip->card, name, &entry))
		snd_info_set_text_ops(entry, p, info_read);
	return 0;
}

static void info_read(snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	snd_sb_csp_t *p = snd_magic_cast(snd_sb_csp_t, entry->private_data, return);

	snd_iprintf(buffer, "Creative Signal Processor [v%d.%d]\n", (p->version >> 4), (p->version & 0x0f));
	snd_iprintf(buffer, "State: %cx%c%c%c\n", ((p->running & SNDRV_SB_CSP_ST_QSOUND) ? 'Q' : '-'),
		    ((p->running & SNDRV_SB_CSP_ST_PAUSED) ? 'P' : '-'),
		    ((p->running & SNDRV_SB_CSP_ST_RUNNING) ? 'R' : '-'),
		    ((p->running & SNDRV_SB_CSP_ST_LOADED) ? 'L' : '-'));
	if (p->running & SNDRV_SB_CSP_ST_LOADED) {
		snd_iprintf(buffer, "Codec: %s [func #%d]\n", p->codec_name, p->func_nr);
		snd_iprintf(buffer, "Sample rates: ");
		if (p->acc_rates == SNDRV_SB_CSP_RATE_ALL) {
			snd_iprintf(buffer, "All\n");
		} else {
			snd_iprintf(buffer, "%s%s%s%s\n",
				    ((p->acc_rates & SNDRV_SB_CSP_RATE_8000) ? "8000Hz " : ""),
				    ((p->acc_rates & SNDRV_SB_CSP_RATE_11025) ? "11025Hz " : ""),
				    ((p->acc_rates & SNDRV_SB_CSP_RATE_22050) ? "22050Hz " : ""),
				    ((p->acc_rates & SNDRV_SB_CSP_RATE_44100) ? "44100Hz" : ""));
		}
		if (p->mode == SNDRV_SB_CSP_MODE_QSOUND) {
			snd_iprintf(buffer, "QSound decoder %sabled\n",
				    p->q_enabled ? "en" : "dis");
		} else {
			snd_iprintf(buffer, "PCM format ID: 0x%x (%s/%s) [%s/%s] [%s/%s]\n",
				    p->acc_format,
				    ((p->acc_width & SNDRV_SB_CSP_SAMPLE_16BIT) ? "16bit" : "-"),
				    ((p->acc_width & SNDRV_SB_CSP_SAMPLE_8BIT) ? "8bit" : "-"),
				    ((p->acc_channels & SNDRV_SB_CSP_MONO) ? "mono" : "-"),
				    ((p->acc_channels & SNDRV_SB_CSP_STEREO) ? "stereo" : "-"),
				    ((p->mode & SNDRV_SB_CSP_MODE_DSP_WRITE) ? "playback" : "-"),
				    ((p->mode & SNDRV_SB_CSP_MODE_DSP_READ) ? "capture" : "-"));
		}
	}
	if (p->running & SNDRV_SB_CSP_ST_AUTO) {
		snd_iprintf(buffer, "Autoloaded Mu-Law, A-Law or Ima-ADPCM hardware codec\n");
	}
	if (p->running & SNDRV_SB_CSP_ST_RUNNING) {
		snd_iprintf(buffer, "Processing %dbit %s PCM samples\n",
			    ((p->run_width & SNDRV_SB_CSP_SAMPLE_16BIT) ? 16 : 8),
			    ((p->run_channels & SNDRV_SB_CSP_MONO) ? "mono" : "stereo"));
	}
	if (p->running & SNDRV_SB_CSP_ST_QSOUND) {
		snd_iprintf(buffer, "Qsound position: left = 0x%x, right = 0x%x\n",
			    p->qpos_left, p->qpos_right);
	}
}

/* */

EXPORT_SYMBOL(snd_sb_csp_new);

/*
 * INIT part
 */

static int __init alsa_sb_csp_init(void)
{
	return 0;
}

static void __exit alsa_sb_csp_exit(void)
{
}

module_init(alsa_sb_csp_init)
module_exit(alsa_sb_csp_exit)
