/*
 *  Copyright (c) by Uros Bizjak <uros@kss-loka.si>
 *                   
 *  Routines for OPL2/OPL3/OPL4 control
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

#include <sound/opl3.h>
#include <sound/asound_fm.h>

/*
 *    There is 18 possible 2 OP voices
 *      (9 in the left and 9 in the right).
 *      The first OP is the modulator and 2nd is the carrier.
 *
 *      The first three voices in the both sides may be connected
 *      with another voice to a 4 OP voice. For example voice 0
 *      can be connected with voice 3. The operators of voice 3 are
 *      used as operators 3 and 4 of the new 4 OP voice.
 *      In this case the 2 OP voice number 0 is the 'first half' and
 *      voice 3 is the second.
 */


/*
 *    Register offset table for OPL2/3 voices,
 *    OPL2 / one OPL3 register array side only
 */

char snd_opl3_regmap[MAX_OPL2_VOICES][4] =
{
/*	  OP1   OP2   OP3   OP4		*/
/*	 ------------------------	*/
	{ 0x00, 0x03, 0x08, 0x0b },
	{ 0x01, 0x04, 0x09, 0x0c },
	{ 0x02, 0x05, 0x0a, 0x0d },

	{ 0x08, 0x0b, 0x00, 0x00 },
	{ 0x09, 0x0c, 0x00, 0x00 },
	{ 0x0a, 0x0d, 0x00, 0x00 },

	{ 0x10, 0x13, 0x00, 0x00 },	/* used by percussive voices */
	{ 0x11, 0x14, 0x00, 0x00 },	/* if the percussive mode */
	{ 0x12, 0x15, 0x00, 0x00 }	/* is selected (only left reg block) */
};

/*
 * prototypes
 */
static int snd_opl3_play_note(opl3_t * opl3, snd_dm_fm_note_t * note);
static int snd_opl3_set_voice(opl3_t * opl3, snd_dm_fm_voice_t * voice);
static int snd_opl3_set_params(opl3_t * opl3, snd_dm_fm_params_t * params);
static int snd_opl3_set_mode(opl3_t * opl3, int mode);
static int snd_opl3_set_connection(opl3_t * opl3, int connection);

/* ------------------------------ */

/*
 * open the device exclusively
 */
int snd_opl3_open(snd_hwdep_t * hw, struct file *file)
{
	opl3_t *opl3 = snd_magic_cast(opl3_t, hw->private_data, return -ENXIO);

	down(&opl3->access_mutex);
	if (opl3->used) {
		up(&opl3->access_mutex);
		return -EAGAIN;
	}
	opl3->used++;
	up(&opl3->access_mutex);

	return 0;
}

/*
 * ioctl for hwdep device:
 */
int snd_opl3_ioctl(snd_hwdep_t * hw, struct file *file,
		   unsigned int cmd, unsigned long arg)
{
	opl3_t *opl3 = snd_magic_cast(opl3_t, hw->private_data, return -ENXIO);
	void __user *argp = (void __user *)arg;

	snd_assert(opl3 != NULL, return -EINVAL);

	switch (cmd) {
		/* get information */
	case SNDRV_DM_FM_IOCTL_INFO:
		{
			snd_dm_fm_info_t info;

			info.fm_mode = opl3->fm_mode;
			info.rhythm = opl3->rhythm;
			if (copy_to_user(argp, &info, sizeof(snd_dm_fm_info_t)))
				return -EFAULT;
			return 0;
		}

	case SNDRV_DM_FM_IOCTL_RESET:
#ifdef CONFIG_SND_OSSEMUL
	case SNDRV_DM_FM_OSS_IOCTL_RESET:
#endif
		snd_opl3_reset(opl3);
		return 0;

	case SNDRV_DM_FM_IOCTL_PLAY_NOTE:
#ifdef CONFIG_SND_OSSEMUL
	case SNDRV_DM_FM_OSS_IOCTL_PLAY_NOTE:
#endif
		{
			snd_dm_fm_note_t note;
			if (copy_from_user(&note, argp, sizeof(snd_dm_fm_note_t)))
				return -EFAULT;
			return snd_opl3_play_note(opl3, &note);
		}

	case SNDRV_DM_FM_IOCTL_SET_VOICE:
#ifdef CONFIG_SND_OSSEMUL
	case SNDRV_DM_FM_OSS_IOCTL_SET_VOICE:
#endif
		{
			snd_dm_fm_voice_t voice;
			if (copy_from_user(&voice, argp, sizeof(snd_dm_fm_voice_t)))
				return -EFAULT;
			return snd_opl3_set_voice(opl3, &voice);
		}

	case SNDRV_DM_FM_IOCTL_SET_PARAMS:
#ifdef CONFIG_SND_OSSEMUL
	case SNDRV_DM_FM_OSS_IOCTL_SET_PARAMS:
#endif
		{
			snd_dm_fm_params_t params;
			if (copy_from_user(&params, argp, sizeof(snd_dm_fm_params_t)))
				return -EFAULT;
			return snd_opl3_set_params(opl3, &params);
		}

	case SNDRV_DM_FM_IOCTL_SET_MODE:
#ifdef CONFIG_SND_OSSEMUL
	case SNDRV_DM_FM_OSS_IOCTL_SET_MODE:
#endif
		return snd_opl3_set_mode(opl3, (int) arg);

	case SNDRV_DM_FM_IOCTL_SET_CONNECTION:
#ifdef CONFIG_SND_OSSEMUL
	case SNDRV_DM_FM_OSS_IOCTL_SET_OPL:
#endif
		return snd_opl3_set_connection(opl3, (int) arg);

#ifdef CONFIG_SND_DEBUG
	default:
		snd_printk("unknown IOCTL: 0x%x\n", cmd);
#endif
	}
	return -ENOTTY;
}

/*
 * close the device
 */
int snd_opl3_release(snd_hwdep_t * hw, struct file *file)
{
	opl3_t *opl3 = snd_magic_cast(opl3_t, hw->private_data, return -ENXIO);

	snd_opl3_reset(opl3);
	down(&opl3->access_mutex);
	opl3->used--;
	up(&opl3->access_mutex);

	return 0;
}

/* ------------------------------ */

void snd_opl3_reset(opl3_t * opl3)
{
	unsigned short opl3_reg;

	unsigned short reg_side;
	unsigned char voice_offset;

	int max_voices, i;

	max_voices = (opl3->hardware < OPL3_HW_OPL3) ?
		MAX_OPL2_VOICES : MAX_OPL3_VOICES;

	for (i = 0; i < max_voices; i++) {
		/* Get register array side and offset of voice */
		if (i < MAX_OPL2_VOICES) {
			/* Left register block for voices 0 .. 8 */
			reg_side = OPL3_LEFT;
			voice_offset = i;
		} else {
			/* Right register block for voices 9 .. 17 */
			reg_side = OPL3_RIGHT;
			voice_offset = i - MAX_OPL2_VOICES;
		}
		opl3_reg = reg_side | (OPL3_REG_KSL_LEVEL + snd_opl3_regmap[voice_offset][0]);
		opl3->command(opl3, opl3_reg, OPL3_TOTAL_LEVEL_MASK); /* Operator 1 volume */
		opl3_reg = reg_side | (OPL3_REG_KSL_LEVEL + snd_opl3_regmap[voice_offset][1]);
		opl3->command(opl3, opl3_reg, OPL3_TOTAL_LEVEL_MASK); /* Operator 2 volume */

		opl3_reg = reg_side | (OPL3_REG_KEYON_BLOCK + voice_offset);
		opl3->command(opl3, opl3_reg, 0x00);	/* Note off */
	}

	opl3->max_voices = MAX_OPL2_VOICES;
	opl3->fm_mode = SNDRV_DM_FM_MODE_OPL2;

	opl3->command(opl3, OPL3_LEFT | OPL3_REG_TEST, OPL3_ENABLE_WAVE_SELECT);
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_PERCUSSION, 0x00);	/* Melodic mode */
	opl3->rhythm = 0;
}


static int snd_opl3_play_note(opl3_t * opl3, snd_dm_fm_note_t * note)
{
	unsigned short reg_side;
	unsigned char voice_offset;

	unsigned short opl3_reg;
	unsigned char reg_val;

	/* Voices 0 -  8 in OPL2 mode */
	/* Voices 0 - 17 in OPL3 mode */
	if (note->voice >= ((opl3->fm_mode == SNDRV_DM_FM_MODE_OPL3) ?
			    MAX_OPL3_VOICES : MAX_OPL2_VOICES))
		return -EINVAL;

	/* Get register array side and offset of voice */
	if (note->voice < MAX_OPL2_VOICES) {
		/* Left register block for voices 0 .. 8 */
		reg_side = OPL3_LEFT;
		voice_offset = note->voice;
	} else {
		/* Right register block for voices 9 .. 17 */
		reg_side = OPL3_RIGHT;
		voice_offset = note->voice - MAX_OPL2_VOICES;
	}

	/* Set lower 8 bits of note frequency */
	reg_val = (unsigned char) note->fnum;
	opl3_reg = reg_side | (OPL3_REG_FNUM_LOW + voice_offset);
	opl3->command(opl3, opl3_reg, reg_val);
	
	reg_val = 0x00;
	/* Set output sound flag */
	if (note->key_on)
		reg_val |= OPL3_KEYON_BIT;
	/* Set octave */
	reg_val |= (note->octave << 2) & OPL3_BLOCKNUM_MASK;
	/* Set higher 2 bits of note frequency */
	reg_val |= (unsigned char) (note->fnum >> 8) & OPL3_FNUM_HIGH_MASK;

	/* Set OPL3 KEYON_BLOCK register of requested voice */ 
	opl3_reg = reg_side | (OPL3_REG_KEYON_BLOCK + voice_offset);
	opl3->command(opl3, opl3_reg, reg_val);

	return 0;
}


static int snd_opl3_set_voice(opl3_t * opl3, snd_dm_fm_voice_t * voice)
{
	unsigned short reg_side;
	unsigned char op_offset;
	unsigned char voice_offset;

	unsigned short opl3_reg;
	unsigned char reg_val;

	/* Only operators 1 and 2 */
	if (voice->op > 1)
		return -EINVAL;
	/* Voices 0 -  8 in OPL2 mode */
	/* Voices 0 - 17 in OPL3 mode */
	if (voice->voice >= ((opl3->fm_mode == SNDRV_DM_FM_MODE_OPL3) ?
			     MAX_OPL3_VOICES : MAX_OPL2_VOICES))
		return -EINVAL;

	/* Get register array side and offset of voice */
	if (voice->voice < MAX_OPL2_VOICES) {
		/* Left register block for voices 0 .. 8 */
		reg_side = OPL3_LEFT;
		voice_offset = voice->voice;
	} else {
		/* Right register block for voices 9 .. 17 */
		reg_side = OPL3_RIGHT;
		voice_offset = voice->voice - MAX_OPL2_VOICES;
	}
	/* Get register offset of operator */
	op_offset = snd_opl3_regmap[voice_offset][voice->op];

	reg_val = 0x00;
	/* Set amplitude modulation (tremolo) effect */
	if (voice->am)
		reg_val |= OPL3_TREMOLO_ON;
	/* Set vibrato effect */
	if (voice->vibrato)
		reg_val |= OPL3_VIBRATO_ON;
	/* Set sustaining sound phase */
	if (voice->do_sustain)
		reg_val |= OPL3_SUSTAIN_ON;
	/* Set keyboard scaling bit */ 
	if (voice->kbd_scale)
		reg_val |= OPL3_KSR;
	/* Set harmonic or frequency multiplier */
	reg_val |= voice->harmonic & OPL3_MULTIPLE_MASK;

	/* Set OPL3 AM_VIB register of requested voice/operator */ 
	opl3_reg = reg_side | (OPL3_REG_AM_VIB + op_offset);
	opl3->command(opl3, opl3_reg, reg_val);

	/* Set decreasing volume of higher notes */
	reg_val = (voice->scale_level << 6) & OPL3_KSL_MASK;
	/* Set output volume */
	reg_val |= ~voice->volume & OPL3_TOTAL_LEVEL_MASK;

	/* Set OPL3 KSL_LEVEL register of requested voice/operator */ 
	opl3_reg = reg_side | (OPL3_REG_KSL_LEVEL + op_offset);
	opl3->command(opl3, opl3_reg, reg_val);

	/* Set attack phase level */
	reg_val = (voice->attack << 4) & OPL3_ATTACK_MASK;
	/* Set decay phase level */
	reg_val |= voice->decay & OPL3_DECAY_MASK;

	/* Set OPL3 ATTACK_DECAY register of requested voice/operator */ 
	opl3_reg = reg_side | (OPL3_REG_ATTACK_DECAY + op_offset);
	opl3->command(opl3, opl3_reg, reg_val);

	/* Set sustain phase level */
	reg_val = (voice->sustain << 4) & OPL3_SUSTAIN_MASK;
	/* Set release phase level */
	reg_val |= voice->release & OPL3_RELEASE_MASK;

	/* Set OPL3 SUSTAIN_RELEASE register of requested voice/operator */ 
	opl3_reg = reg_side | (OPL3_REG_SUSTAIN_RELEASE + op_offset);
	opl3->command(opl3, opl3_reg, reg_val);

	/* Set inter-operator feedback */
	reg_val = (voice->feedback << 1) & OPL3_FEEDBACK_MASK;
	/* Set inter-operator connection */
	if (voice->connection)
		reg_val |= OPL3_CONNECTION_BIT;
	/* OPL-3 only */
	if (opl3->fm_mode == SNDRV_DM_FM_MODE_OPL3) {
		if (voice->left)
			reg_val |= OPL3_VOICE_TO_LEFT;
		if (voice->right)
			reg_val |= OPL3_VOICE_TO_RIGHT;
	}
	/* Feedback/connection bits are applicable to voice */
	opl3_reg = reg_side | (OPL3_REG_FEEDBACK_CONNECTION + voice_offset);
	opl3->command(opl3, opl3_reg, reg_val);

	/* Select waveform */
	reg_val = voice->waveform & OPL3_WAVE_SELECT_MASK;
	opl3_reg = reg_side | (OPL3_REG_WAVE_SELECT + op_offset);
	opl3->command(opl3, opl3_reg, reg_val);

	return 0;
}

static int snd_opl3_set_params(opl3_t * opl3, snd_dm_fm_params_t * params)
{
	unsigned char reg_val;

	reg_val = 0x00;
	/* Set keyboard split method */
	if (params->kbd_split)
		reg_val |= OPL3_KEYBOARD_SPLIT;
	opl3->command(opl3, OPL3_LEFT | OPL3_REG_KBD_SPLIT, reg_val);

	reg_val = 0x00;
	/* Set amplitude modulation (tremolo) depth */
	if (params->am_depth)
		reg_val |= OPL3_TREMOLO_DEPTH;
	/* Set vibrato depth */
	if (params->vib_depth)
		reg_val |= OPL3_VIBRATO_DEPTH;
	/* Set percussion mode */
	if (params->rhythm) {
		reg_val |= OPL3_PERCUSSION_ENABLE;
		opl3->rhythm = 1;
	} else {
		opl3->rhythm = 0;
	}
	/* Play percussion instruments */
	if (params->bass)
		reg_val |= OPL3_BASSDRUM_ON;
	if (params->snare)
		reg_val |= OPL3_SNAREDRUM_ON;
	if (params->tomtom)
		reg_val |= OPL3_TOMTOM_ON;
	if (params->cymbal)
		reg_val |= OPL3_CYMBAL_ON;
	if (params->hihat)
		reg_val |= OPL3_HIHAT_ON;

	opl3->command(opl3, OPL3_LEFT | OPL3_REG_PERCUSSION, reg_val);
	return 0;
}

static int snd_opl3_set_mode(opl3_t * opl3, int mode)
{
	if ((mode == SNDRV_DM_FM_MODE_OPL3) && (opl3->hardware < OPL3_HW_OPL3))
		return -EINVAL;

	opl3->fm_mode = mode;
	if (opl3->hardware >= OPL3_HW_OPL3)
		opl3->command(opl3, OPL3_RIGHT | OPL3_REG_CONNECTION_SELECT, 0x00);	/* Clear 4-op connections */

	return 0;
}

static int snd_opl3_set_connection(opl3_t * opl3, int connection)
{
	unsigned char reg_val;

	/* OPL-3 only */
	if (opl3->fm_mode != SNDRV_DM_FM_MODE_OPL3)
		return -EINVAL;

	reg_val = connection & (OPL3_RIGHT_4OP_0 | OPL3_RIGHT_4OP_1 | OPL3_RIGHT_4OP_2 |
				OPL3_LEFT_4OP_0 | OPL3_LEFT_4OP_1 | OPL3_LEFT_4OP_2);
	/* Set 4-op connections */
	opl3->command(opl3, OPL3_RIGHT | OPL3_REG_CONNECTION_SELECT, reg_val);

	return 0;
}
