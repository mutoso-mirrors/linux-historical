/*
 * Universal Interface for Intel High Definition Audio Codec
 *
 * Generic widget tree parser
 *
 * Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <sound/driver.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <sound/core.h>
#include "hda_codec.h"
#include "hda_local.h"

/* widget node for parsing */
struct hda_gnode {
	hda_nid_t nid;		/* NID of this widget */
	unsigned short nconns;	/* number of input connections */
	hda_nid_t conn_list[HDA_MAX_CONNECTIONS]; /* input connections */
	unsigned int wid_caps;	/* widget capabilities */
	unsigned char type;	/* widget type */
	unsigned char pin_ctl;	/* pin controls */
	unsigned char checked;	/* the flag indicates that the node is already parsed */
	unsigned int pin_caps;	/* pin widget capabilities */
	unsigned int def_cfg;	/* default configuration */
	unsigned int amp_out_caps;	/* AMP out capabilities */
	unsigned int amp_in_caps;	/* AMP in capabilities */
	struct list_head list;
};

/* pathc-specific record */
struct hda_gspec {
	struct hda_gnode *dac_node;	/* DAC node */
	struct hda_gnode *out_pin_node;	/* Output pin (Line-Out) node */
	struct hda_gnode *pcm_vol_node;	/* Node for PCM volume */
	unsigned int pcm_vol_index;	/* connection of PCM volume */

	struct hda_gnode *adc_node;	/* ADC node */
	struct hda_gnode *cap_vol_node;	/* Node for capture volume */
	unsigned int cur_cap_src;	/* current capture source */
	struct hda_input_mux input_mux;
	char cap_labels[HDA_MAX_NUM_INPUTS][16];

	unsigned int def_amp_in_caps;
	unsigned int def_amp_out_caps;

	struct hda_pcm pcm_rec;		/* PCM information */

	struct list_head nid_list;	/* list of widgets */
};

/*
 * retrieve the default device type from the default config value
 */
#define get_defcfg_type(node) (((node)->def_cfg & AC_DEFCFG_DEVICE) >> AC_DEFCFG_DEVICE_SHIFT)
#define get_defcfg_location(node) (((node)->def_cfg & AC_DEFCFG_LOCATION) >> AC_DEFCFG_LOCATION_SHIFT)

/*
 * destructor
 */
static void snd_hda_generic_free(struct hda_codec *codec)
{
	struct hda_gspec *spec = codec->spec;
	struct list_head *p, *n;

	if (! spec)
		return;
	/* free all widgets */
	list_for_each_safe(p, n, &spec->nid_list) {
		struct hda_gnode *node = list_entry(p, struct hda_gnode, list);
		kfree(node);
	}
	kfree(spec);
}


/*
 * add a new widget node and read its attributes
 */
static int add_new_node(struct hda_codec *codec, struct hda_gspec *spec, hda_nid_t nid)
{
	struct hda_gnode *node;
	int nconns;

	node = kcalloc(1, sizeof(*node), GFP_KERNEL);
	if (node == NULL)
		return -ENOMEM;
	node->nid = nid;
	nconns = snd_hda_get_connections(codec, nid, node->conn_list, HDA_MAX_CONNECTIONS);
	if (nconns < 0) {
		kfree(node);
		return nconns;
	}
	node->nconns = nconns;
	node->wid_caps = snd_hda_param_read(codec, nid, AC_PAR_AUDIO_WIDGET_CAP);
	node->type = (node->wid_caps & AC_WCAP_TYPE) >> AC_WCAP_TYPE_SHIFT;

	if (node->type == AC_WID_PIN) {
		node->pin_caps = snd_hda_param_read(codec, node->nid, AC_PAR_PIN_CAP);
		node->pin_ctl = snd_hda_codec_read(codec, node->nid, 0, AC_VERB_GET_PIN_WIDGET_CONTROL, 0);
		node->def_cfg = snd_hda_codec_read(codec, node->nid, 0, AC_VERB_GET_CONFIG_DEFAULT, 0);
	}

	if (node->wid_caps & AC_WCAP_OUT_AMP) {
		if (node->wid_caps & AC_WCAP_AMP_OVRD)
			node->amp_out_caps = snd_hda_param_read(codec, node->nid, AC_PAR_AMP_OUT_CAP);
		if (! node->amp_out_caps)
			node->amp_out_caps = spec->def_amp_out_caps;
	}
	if (node->wid_caps & AC_WCAP_IN_AMP) {
		if (node->wid_caps & AC_WCAP_AMP_OVRD)
			node->amp_in_caps = snd_hda_param_read(codec, node->nid, AC_PAR_AMP_IN_CAP);
		if (! node->amp_in_caps)
			node->amp_in_caps = spec->def_amp_in_caps;
	}
	list_add_tail(&node->list, &spec->nid_list);
	return 0;
}

/*
 * build the AFG subtree
 */
static int build_afg_tree(struct hda_codec *codec)
{
	struct hda_gspec *spec = codec->spec;
	int i, nodes, err;
	hda_nid_t nid;

	snd_assert(spec, return -EINVAL);

	spec->def_amp_out_caps = snd_hda_param_read(codec, codec->afg, AC_PAR_AMP_OUT_CAP);
	spec->def_amp_in_caps = snd_hda_param_read(codec, codec->afg, AC_PAR_AMP_IN_CAP);

	nodes = snd_hda_get_sub_nodes(codec, codec->afg, &nid);
	if (! nid || nodes < 0) {
		printk(KERN_ERR "Invalid AFG subtree\n");
		return -EINVAL;
	}

	/* parse all nodes belonging to the AFG */
	for (i = 0; i < nodes; i++, nid++) {
		if ((err = add_new_node(codec, spec, nid)) < 0)
			return err;
	}

	return 0;
}


/*
 * look for the node record for the given NID
 */
/* FIXME: should avoid the braindead linear search */
static struct hda_gnode *hda_get_node(struct hda_gspec *spec, hda_nid_t nid)
{
	struct list_head *p;
	struct hda_gnode *node;

	list_for_each(p, &spec->nid_list) {
		node = list_entry(p, struct hda_gnode, list);
		if (node->nid == nid)
			return node;
	}
	return NULL;
}

/*
 * unmute (and set max vol) the output amplifier
 */
static int unmute_output(struct hda_codec *codec, struct hda_gnode *node)
{
	unsigned int val, ofs;
	snd_printdd("UNMUTE OUT: NID=0x%x\n", node->nid);
	val = (node->amp_out_caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT;
	ofs = (node->amp_out_caps & AC_AMPCAP_OFFSET) >> AC_AMPCAP_OFFSET_SHIFT;
	if (val >= ofs)
		val -= ofs;
	val |= AC_AMP_SET_LEFT | AC_AMP_SET_RIGHT;
	val |= AC_AMP_SET_OUTPUT;
	return snd_hda_codec_write(codec, node->nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, val);
}

/*
 * unmute (and set max vol) the input amplifier
 */
static int unmute_input(struct hda_codec *codec, struct hda_gnode *node, unsigned int index)
{
	unsigned int val, ofs;
	snd_printdd("UNMUTE IN: NID=0x%x IDX=0x%x\n", node->nid, index);
	val = (node->amp_in_caps & AC_AMPCAP_NUM_STEPS) >> AC_AMPCAP_NUM_STEPS_SHIFT;
	ofs = (node->amp_in_caps & AC_AMPCAP_OFFSET) >> AC_AMPCAP_OFFSET_SHIFT;
	if (val >= ofs)
		val -= ofs;
	val |= AC_AMP_SET_LEFT | AC_AMP_SET_RIGHT;
	val |= AC_AMP_SET_INPUT;
	// awk added - fixed to allow unmuting of indexed amps
	val |= index << AC_AMP_SET_INDEX_SHIFT;
	return snd_hda_codec_write(codec, node->nid, 0, AC_VERB_SET_AMP_GAIN_MUTE, val);
}

/*
 * select the input connection of the given node.
 */
static int select_input_connection(struct hda_codec *codec, struct hda_gnode *node,
				   unsigned int index)
{
	snd_printdd("CONNECT: NID=0x%x IDX=0x%x\n", node->nid, index);
	return snd_hda_codec_write(codec, node->nid, 0, AC_VERB_SET_CONNECT_SEL, index);
}

/*
 * clear checked flag of each node in the node list
 */
static void clear_check_flags(struct hda_gspec *spec)
{
	struct list_head *p;
	struct hda_gnode *node;

	list_for_each(p, &spec->nid_list) {
		node = list_entry(p, struct hda_gnode, list);
		node->checked = 0;
	}
}

/*
 * parse the output path recursively until reach to an audio output widget
 *
 * returns 0 if not found, 1 if found, or a negative error code.
 */
static int parse_output_path(struct hda_codec *codec, struct hda_gspec *spec,
			     struct hda_gnode *node)
{
	int i, err;
	struct hda_gnode *child;

	if (node->checked)
		return 0;

	node->checked = 1;
	if (node->type == AC_WID_AUD_OUT) {
		if (node->wid_caps & AC_WCAP_DIGITAL) {
			snd_printdd("Skip Digital OUT node %x\n", node->nid);
			return 0;
		}
		snd_printdd("AUD_OUT found %x\n", node->nid);
		if (spec->dac_node) {
			/* already DAC node is assigned, just unmute & connect */
			return node == spec->dac_node;
		}
		spec->dac_node = node;
		if (node->wid_caps & AC_WCAP_OUT_AMP) {
			spec->pcm_vol_node = node;
			spec->pcm_vol_index = 0;
		}
		return 1; /* found */
	}

	for (i = 0; i < node->nconns; i++) {
		child = hda_get_node(spec, node->conn_list[i]);
		if (! child)
			continue;
		err = parse_output_path(codec, spec, child);
		if (err < 0)
			return err;
		else if (err > 0) {
			/* found one,
			 * select the path, unmute both input and output
			 */
			if (node->nconns > 1)
				select_input_connection(codec, node, i);
			unmute_input(codec, node, i);
			unmute_output(codec, node);
			if (! spec->pcm_vol_node) {
				if (node->wid_caps & AC_WCAP_IN_AMP) {
					spec->pcm_vol_node = node;
					spec->pcm_vol_index = i;
				} else if (node->wid_caps & AC_WCAP_OUT_AMP) {
					spec->pcm_vol_node = node;
					spec->pcm_vol_index = 0;
				}
			}
			return 1;
		}
	}
	return 0;
}

/*
 * Look for the output PIN widget with the given jack type
 * and parse the output path to that PIN.
 *
 * Returns the PIN node when the path to DAC is established.
 */
static struct hda_gnode *parse_output_jack(struct hda_codec *codec,
					   struct hda_gspec *spec,
					   int jack_type)
{
	struct list_head *p;
	struct hda_gnode *node;
	int err;

	list_for_each(p, &spec->nid_list) {
		node = list_entry(p, struct hda_gnode, list);
		if (node->type != AC_WID_PIN)
			continue;
		/* output capable? */
		if (! (node->pin_caps & AC_PINCAP_OUT))
			continue;
		if (jack_type >= 0) {
			if (jack_type != get_defcfg_type(node))
				continue;
			if (node->wid_caps & AC_WCAP_DIGITAL)
				continue; /* skip SPDIF */
		} else {
			/* output as default? */
			if (! (node->pin_ctl & AC_PINCTL_OUT_EN))
				continue;
		}
		clear_check_flags(spec);
		err = parse_output_path(codec, spec, node);
		if (err < 0)
			return NULL;
		else if (err > 0) {
			/* unmute the PIN output */
			unmute_output(codec, node);
			/* set PIN-Out enable */
			snd_hda_codec_write(codec, node->nid, 0,
					    AC_VERB_SET_PIN_WIDGET_CONTROL,
					    AC_PINCTL_OUT_EN | AC_PINCTL_HP_EN);
			return node;
		}
	}
	return NULL;
}


/*
 * parse outputs
 */
static int parse_output(struct hda_codec *codec)
{
	struct hda_gspec *spec = codec->spec;
	struct hda_gnode *node;

	/*
	 * Look for the output PIN widget
	 */
	/* first, look for the line-out pin */
	node = parse_output_jack(codec, spec, AC_JACK_LINE_OUT);
	if (node) /* found, remember the PIN node */
		spec->out_pin_node = node;
	/* look for the HP-out pin */
	node = parse_output_jack(codec, spec, AC_JACK_HP_OUT);
	if (node) {
		if (! spec->out_pin_node)
			spec->out_pin_node = node;
	}

	if (! spec->out_pin_node) {
		/* no line-out or HP pins found,
		 * then choose for the first output pin
		 */
		spec->out_pin_node = parse_output_jack(codec, spec, -1);
		if (! spec->out_pin_node)
			snd_printd("hda_generic: no proper output path found\n");
	}

	return 0;
}

/*
 * input MUX
 */

/* control callbacks */
static int capture_source_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gspec *spec = codec->spec;
	return snd_hda_input_mux_info(&spec->input_mux, uinfo);
}

static int capture_source_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gspec *spec = codec->spec;

	ucontrol->value.enumerated.item[0] = spec->cur_cap_src;
	return 0;
}

static int capture_source_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_gspec *spec = codec->spec;
	return snd_hda_input_mux_put(codec, &spec->input_mux, ucontrol,
				     spec->adc_node->nid, &spec->cur_cap_src);
}

/*
 * return the string name of the given input PIN widget
 */
static const char *get_input_type(struct hda_gnode *node, unsigned int *pinctl)
{
	unsigned int location = get_defcfg_location(node);
	switch (get_defcfg_type(node)) {
	case AC_JACK_LINE_IN:
		if ((location & 0x0f) == AC_JACK_LOC_FRONT)
			return "Front Line";
		return "Line";
	case AC_JACK_CD:
		if (pinctl)
			*pinctl |= AC_PIN_VREF_GRD;
		return "CD";
	case AC_JACK_AUX:
		if ((location & 0x0f) == AC_JACK_LOC_FRONT)
			return "Front Aux";
		return "Aux";
	case AC_JACK_MIC_IN:
		if ((location & 0x0f) == AC_JACK_LOC_FRONT)
			return "Front Mic";
		return "Mic";
	case AC_JACK_SPDIF_IN:
		return "SPDIF";
	case AC_JACK_DIG_OTHER_IN:
		return "Digital";
	}
	return NULL;
}

/*
 * parse the nodes recursively until reach to the input PIN
 *
 * returns 0 if not found, 1 if found, or a negative error code.
 */
static int parse_adc_sub_nodes(struct hda_codec *codec, struct hda_gspec *spec,
			       struct hda_gnode *node)
{
	int i, err;
	unsigned int pinctl;
	char *label;
	const char *type;

	if (node->checked)
		return 0;

	node->checked = 1;
	if (node->type != AC_WID_PIN) {
		for (i = 0; i < node->nconns; i++) {
			struct hda_gnode *child;
			child = hda_get_node(spec, node->conn_list[i]);
			if (! child)
				continue;
			err = parse_adc_sub_nodes(codec, spec, child);
			if (err < 0)
				return err;
			if (err > 0) {
				/* found one,
				 * select the path, unmute both input and output
				 */
				if (node->nconns > 1)
					select_input_connection(codec, node, i);
				unmute_input(codec, node, i);
				unmute_output(codec, node);
				return err;
			}
		}
		return 0;
	}

	/* input capable? */
	if (! (node->pin_caps & AC_PINCAP_IN))
		return 0;

	if (node->wid_caps & AC_WCAP_DIGITAL)
		return 0; /* skip SPDIF */

	if (spec->input_mux.num_items >= HDA_MAX_NUM_INPUTS) {
		snd_printk(KERN_ERR "hda_generic: Too many items for capture\n");
		return -EINVAL;
	}

	pinctl = AC_PINCTL_IN_EN;
	/* create a proper capture source label */
	type = get_input_type(node, &pinctl);
	if (! type) {
		/* input as default? */
		if (! (node->pin_ctl & AC_PINCTL_IN_EN))
			return 0;
		type = "Input";
	}
	label = spec->cap_labels[spec->input_mux.num_items];
	strcpy(label, type);
	spec->input_mux.items[spec->input_mux.num_items].label = label;

	/* unmute the PIN external input */
	unmute_input(codec, node, 0); /* index = 0? */
	/* set PIN-In enable */
	snd_hda_codec_write(codec, node->nid, 0, AC_VERB_SET_PIN_WIDGET_CONTROL, pinctl);

	return 1; /* found */
}

/*
 * parse input
 */
static int parse_input_path(struct hda_codec *codec, struct hda_gnode *adc_node)
{
	struct hda_gspec *spec = codec->spec;
	struct hda_gnode *node;
	int i, err;

	snd_printdd("AUD_IN = %x\n", adc_node->nid);
	clear_check_flags(spec);

	// awk added - fixed no recording due to muted widget
	unmute_input(codec, adc_node, 0);
	
	/*
	 * check each connection of the ADC
	 * if it reaches to a proper input PIN, add the path as the
	 * input path.
	 */
	for (i = 0; i < adc_node->nconns; i++) {
		node = hda_get_node(spec, adc_node->conn_list[i]);
		if (! node)
			continue;
		err = parse_adc_sub_nodes(codec, spec, node);
		if (err < 0)
			return err;
		else if (err > 0) {
			struct hda_input_mux_item *csrc = &spec->input_mux.items[spec->input_mux.num_items];
			char *buf = spec->cap_labels[spec->input_mux.num_items];
			int ocap;
			for (ocap = 0; ocap < spec->input_mux.num_items; ocap++) {
				if (! strcmp(buf, spec->cap_labels[ocap])) {
					/* same label already exists,
					 * put the index number to be unique
					 */
					sprintf(buf, "%s %d", spec->cap_labels[ocap],
						spec->input_mux.num_items);
				}
			}
			csrc->index = i;
			spec->input_mux.num_items++;
		}
	}

	if (! spec->input_mux.num_items)
		return 0; /* no input path found... */

	snd_printdd("[Capture Source] NID=0x%x, #SRC=%d\n", adc_node->nid, spec->input_mux.num_items);
	for (i = 0; i < spec->input_mux.num_items; i++)
		snd_printdd("  [%s] IDX=0x%x\n", spec->input_mux.items[i].label,
			    spec->input_mux.items[i].index);

	spec->adc_node = adc_node;
	return 1;
}

/*
 * parse input
 */
static int parse_input(struct hda_codec *codec)
{
	struct hda_gspec *spec = codec->spec;
	struct list_head *p;
	struct hda_gnode *node;
	int err;

	/*
	 * At first we look for an audio input widget.
	 * If it reaches to certain input PINs, we take it as the
	 * input path.
	 */
	list_for_each(p, &spec->nid_list) {
		node = list_entry(p, struct hda_gnode, list);
		if (node->wid_caps & AC_WCAP_DIGITAL)
			continue; /* skip SPDIF */
		if (node->type == AC_WID_AUD_IN) {
			err = parse_input_path(codec, node);
			if (err < 0)
				return err;
			else if (err > 0)
				return 0;
		}
	}
	snd_printd("hda_generic: no proper input path found\n");
	return 0;
}

/*
 * create mixer controls if possible
 */
#define DIR_OUT		0x1
#define DIR_IN		0x2

static int create_mixer(struct hda_codec *codec, struct hda_gnode *node,
			unsigned int index, const char *type, const char *dir_sfx)
{
	char name[32];
	int err;
	int created = 0;

	if (type)
		sprintf(name, "%s %s Switch", type, dir_sfx);
	else
		sprintf(name, "%s Switch", dir_sfx);
	if ((node->wid_caps & AC_WCAP_IN_AMP) &&
	    (node->amp_in_caps & AC_AMPCAP_MUTE)) {
		snd_kcontrol_new_t knew =
			HDA_CODEC_MUTE(name, node->nid, index, HDA_INPUT);
		snd_printdd("[%s] NID=0x%x, DIR=IN, IDX=0x%x\n", name, node->nid, index);
		if ((err = snd_ctl_add(codec->bus->card, snd_ctl_new1(&knew, codec))) < 0)
			return err;
		created = 1;
	} else if ((node->wid_caps & AC_WCAP_OUT_AMP) &&
		   (node->amp_out_caps & AC_AMPCAP_MUTE)) {
		snd_kcontrol_new_t knew =
			HDA_CODEC_MUTE(name, node->nid, 0, HDA_OUTPUT);
		snd_printdd("[%s] NID=0x%x, DIR=OUT\n", name, node->nid);
		if ((err = snd_ctl_add(codec->bus->card, snd_ctl_new1(&knew, codec))) < 0)
			return err;
		created = 1;
	}

	if (type)
		sprintf(name, "%s %s Volume", type, dir_sfx);
	else
		sprintf(name, "%s Volume", dir_sfx);
	if ((node->wid_caps & AC_WCAP_IN_AMP) &&
	    (node->amp_in_caps & AC_AMPCAP_NUM_STEPS)) {
		snd_kcontrol_new_t knew =
			HDA_CODEC_VOLUME(name, node->nid, index, HDA_INPUT);
		snd_printdd("[%s] NID=0x%x, DIR=IN, IDX=0x%x\n", name, node->nid, index);
		if ((err = snd_ctl_add(codec->bus->card, snd_ctl_new1(&knew, codec))) < 0)
			return err;
		created = 1;
	} else if ((node->wid_caps & AC_WCAP_OUT_AMP) &&
		   (node->amp_out_caps & AC_AMPCAP_NUM_STEPS)) {
		snd_kcontrol_new_t knew =
			HDA_CODEC_VOLUME(name, node->nid, 0, HDA_OUTPUT);
		snd_printdd("[%s] NID=0x%x, DIR=OUT\n", name, node->nid);
		if ((err = snd_ctl_add(codec->bus->card, snd_ctl_new1(&knew, codec))) < 0)
			return err;
		created = 1;
	}

	return created;
}

/*
 * check whether the controls with the given name and direction suffix already exist
 */
static int check_existing_control(struct hda_codec *codec, const char *type, const char *dir)
{
	snd_ctl_elem_id_t id;
	memset(&id, 0, sizeof(id));
	sprintf(id.name, "%s %s Volume", type, dir);
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	if (snd_ctl_find_id(codec->bus->card, &id))
		return 1;
	sprintf(id.name, "%s %s Switch", type, dir);
	id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
	if (snd_ctl_find_id(codec->bus->card, &id))
		return 1;
	return 0;
}

/*
 * build output mixer controls
 */
static int build_output_controls(struct hda_codec *codec)
{
	struct hda_gspec *spec = codec->spec;
	int err;

	err = create_mixer(codec, spec->pcm_vol_node, spec->pcm_vol_index,
			   "PCM", "Playback");
	if (err < 0)
		return err;
	return 0;
}

/* create capture volume/switch */
static int build_input_controls(struct hda_codec *codec)
{
	struct hda_gspec *spec = codec->spec;
	struct hda_gnode *adc_node = spec->adc_node;
	int err;

	if (! adc_node)
		return 0; /* not found */

	/* create capture volume and switch controls if the ADC has an amp */
	err = create_mixer(codec, adc_node, 0, NULL, "Capture");

	/* create input MUX if multiple sources are available */
	if (spec->input_mux.num_items > 1) {
		static snd_kcontrol_new_t cap_sel = {
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.name = "Capture Source",
			.info = capture_source_info,
			.get = capture_source_get,
			.put = capture_source_put,
		};
		if ((err = snd_ctl_add(codec->bus->card, snd_ctl_new1(&cap_sel, codec))) < 0)
			return err;
		spec->cur_cap_src = 0;
		select_input_connection(codec, adc_node, spec->input_mux.items[0].index);
	}
	return 0;
}


/*
 * parse the nodes recursively until reach to the output PIN.
 *
 * returns 0 - if not found,
 *         1 - if found, but no mixer is created
 *         2 - if found and mixer was already created, (just skip)
 *         a negative error code
 */
static int parse_loopback_path(struct hda_codec *codec, struct hda_gspec *spec,
			       struct hda_gnode *node, struct hda_gnode *dest_node,
			       const char *type)
{
	int i, err;

	if (node->checked)
		return 0;

	node->checked = 1;
	if (node == dest_node) {
		/* loopback connection found */
		return 1;
	}

	for (i = 0; i < node->nconns; i++) {
		struct hda_gnode *child = hda_get_node(spec, node->conn_list[i]);
		if (! child)
			continue;
		err = parse_loopback_path(codec, spec, child, dest_node, type);
		if (err < 0)
			return err;
		else if (err >= 1) {
			if (err == 1) {
				err = create_mixer(codec, node, i, type, "Playback");
				if (err < 0)
					return err;
				if (err > 0)
					return 2; /* ok, created */
				/* not created, maybe in the lower path */
				err = 1;
			}
			/* connect and unmute */
			if (node->nconns > 1)
				select_input_connection(codec, node, i);
			unmute_input(codec, node, i);
			unmute_output(codec, node);
			return err;
		}
	}
	return 0;
}

/*
 * parse the tree and build the loopback controls
 */
static int build_loopback_controls(struct hda_codec *codec)
{
	struct hda_gspec *spec = codec->spec;
	struct list_head *p;
	struct hda_gnode *node;
	int err;
	const char *type;

	if (! spec->out_pin_node)
		return 0;

	list_for_each(p, &spec->nid_list) {
		node = list_entry(p, struct hda_gnode, list);
		if (node->type != AC_WID_PIN)
			continue;
		/* input capable? */
		if (! (node->pin_caps & AC_PINCAP_IN))
			return 0;
		type = get_input_type(node, NULL);
		if (type) {
			if (check_existing_control(codec, type, "Playback"))
				continue;
			clear_check_flags(spec);
			err = parse_loopback_path(codec, spec, spec->out_pin_node,
						  node, type);
			if (err < 0)
				return err;
			if (! err)
				continue;
		}
	}
	return 0;
}

/*
 * build mixer controls
 */
static int build_generic_controls(struct hda_codec *codec)
{
	int err;

	if ((err = build_input_controls(codec)) < 0 ||
	    (err = build_output_controls(codec)) < 0 ||
	    (err = build_loopback_controls(codec)) < 0)
		return err;

	return 0;
}

/*
 * PCM
 */
static struct hda_pcm_stream generic_pcm_playback = {
	.substreams = 1,
	.channels_min = 2,
	.channels_max = 2,
};

static int build_generic_pcms(struct hda_codec *codec)
{
	struct hda_gspec *spec = codec->spec;
	struct hda_pcm *info = &spec->pcm_rec;

	if (! spec->dac_node && ! spec->adc_node) {
		snd_printd("hda_generic: no PCM found\n");
		return 0;
	}

	codec->num_pcms = 1;
	codec->pcm_info = info;

	info->name = "HDA Generic";
	if (spec->dac_node) {
		info->stream[0] = generic_pcm_playback;
		info->stream[0].nid = spec->dac_node->nid;
	}
	if (spec->adc_node) {
		info->stream[1] = generic_pcm_playback;
		info->stream[1].nid = spec->adc_node->nid;
	}

	return 0;
}


/*
 */
static struct hda_codec_ops generic_patch_ops = {
	.build_controls = build_generic_controls,
	.build_pcms = build_generic_pcms,
	.free = snd_hda_generic_free,
};

/*
 * the generic parser
 */
int snd_hda_parse_generic_codec(struct hda_codec *codec)
{
	struct hda_gspec *spec;
	int err;

	spec = kcalloc(1, sizeof(*spec), GFP_KERNEL);
	if (spec == NULL) {
		printk(KERN_ERR "hda_generic: can't allocate spec\n");
		return -ENOMEM;
	}
	codec->spec = spec;
	INIT_LIST_HEAD(&spec->nid_list);

	if ((err = build_afg_tree(codec)) < 0)
		goto error;

	if ((err = parse_input(codec)) < 0 ||
	    (err = parse_output(codec)) < 0)
		goto error;

	codec->patch_ops = generic_patch_ops;

	return 0;

 error:
	snd_hda_generic_free(codec);
	return err;
}
