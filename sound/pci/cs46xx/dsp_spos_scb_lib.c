/*
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

/*
 * 2002-07 Benny Sjostrand benny@hostmobility.com
 */


#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/info.h>
#include <sound/cs46xx.h>

#include "cs46xx_lib.h"
#include "dsp_spos.h"

typedef struct _proc_scb_info_t {
	dsp_scb_descriptor_t * scb_desc;
	cs46xx_t *chip;
} proc_scb_info_t;

static void remove_symbol (cs46xx_t * chip,symbol_entry_t * symbol)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	int symbol_index = (int)(symbol - ins->symbol_table.symbols);

	snd_assert(ins->symbol_table.nsymbols > 0,return);
	snd_assert(symbol_index >= 0 && symbol_index < ins->symbol_table.nsymbols, return);

	ins->symbol_table.symbols[symbol_index].deleted = 1;

	if (symbol_index < ins->symbol_table.highest_frag_index) {
		ins->symbol_table.highest_frag_index = symbol_index;
	}
  
	if (symbol_index == ins->symbol_table.nsymbols - 1)
		ins->symbol_table.nsymbols --;

	if (ins->symbol_table.highest_frag_index > ins->symbol_table.nsymbols) {
		ins->symbol_table.highest_frag_index = ins->symbol_table.nsymbols;
	}

}

static void cs46xx_dsp_proc_scb_info_read (snd_info_entry_t *entry, snd_info_buffer_t * buffer)
{
	proc_scb_info_t * scb_info  = (proc_scb_info_t *)entry->private_data;
	dsp_scb_descriptor_t * scb = scb_info->scb_desc;
	dsp_spos_instance_t * ins;
	cs46xx_t *chip = snd_magic_cast(cs46xx_t, scb_info->chip, return);
	int j,col;
	unsigned long dst = chip->region.idx[1].remap_addr + DSP_PARAMETER_BYTE_OFFSET;

	ins = chip->dsp_spos_instance;

	down(&ins->scb_mutex);
	snd_iprintf(buffer,"%04x %s:\n",scb->address,scb->scb_name);

	for (col = 0,j = 0;j < 0x10; j++,col++) {
		if (col == 4) {
			snd_iprintf(buffer,"\n");
			col = 0;
		}
		snd_iprintf(buffer,"%08x ",readl(dst + (scb->address + j) * sizeof(u32)));
	}
  
	snd_iprintf(buffer,"\n");

	if (scb->parent_scb_ptr != NULL) {
		snd_iprintf(buffer,"parent [%s:%04x] ", 
			    scb->parent_scb_ptr->scb_name,
			    scb->parent_scb_ptr->address);
	} else snd_iprintf(buffer,"parent [none] ");
  
	snd_iprintf(buffer,"sub_list_ptr [%s:%04x]\nnext_scb_ptr [%s:%04x]  task_entry [%s:%04x]\n",
		    scb->sub_list_ptr->scb_name,
		    scb->sub_list_ptr->address,
		    scb->next_scb_ptr->scb_name,
		    scb->next_scb_ptr->address,
		    scb->task_entry->symbol_name,
		    scb->task_entry->address);

	snd_iprintf(buffer,"index [%d] ref_count [%d]\n",scb->index,scb->ref_count);  
	up(&ins->scb_mutex);
}

static void _dsp_unlink_scb (cs46xx_t *chip,dsp_scb_descriptor_t * scb)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	unsigned long flags;

	if ( scb->parent_scb_ptr ) {
		/* unlink parent SCB */
		snd_assert ((scb->parent_scb_ptr->sub_list_ptr == scb ||
			     scb->parent_scb_ptr->next_scb_ptr == scb),return);
  
		if (scb->parent_scb_ptr->sub_list_ptr == scb) {

			if (scb->next_scb_ptr == ins->the_null_scb) {
				/* last and only node in parent sublist */
				scb->parent_scb_ptr->sub_list_ptr = scb->sub_list_ptr;

				if (scb->sub_list_ptr != ins->the_null_scb) {
					scb->sub_list_ptr->parent_scb_ptr = scb->parent_scb_ptr;
				}
				scb->sub_list_ptr = ins->the_null_scb;
			} else {
				/* first node in parent sublist */
				scb->parent_scb_ptr->sub_list_ptr = scb->next_scb_ptr;

				if (scb->next_scb_ptr != ins->the_null_scb) {
					/* update next node parent ptr. */
					scb->next_scb_ptr->parent_scb_ptr = scb->parent_scb_ptr;
				}
				scb->next_scb_ptr = ins->the_null_scb;
			}
		} else {
			snd_assert ( (scb->sub_list_ptr == ins->the_null_scb), return);
			scb->parent_scb_ptr->next_scb_ptr = scb->next_scb_ptr;

			if (scb->next_scb_ptr != ins->the_null_scb) {
				/* update next node parent ptr. */
				scb->next_scb_ptr->parent_scb_ptr = scb->parent_scb_ptr;
			}
			scb->next_scb_ptr = ins->the_null_scb;
		}

		spin_lock_irqsave(&chip->reg_lock, flags);    

		/* update parent first entry in DSP RAM */
		snd_cs46xx_poke(chip,
				(scb->parent_scb_ptr->address + SCBsubListPtr) << 2,
				(scb->parent_scb_ptr->sub_list_ptr->address << 0x10) |
				(scb->parent_scb_ptr->next_scb_ptr->address));

		/* then update entry in DSP RAM */
		snd_cs46xx_poke(chip,
				(scb->address + SCBsubListPtr) << 2,
				(scb->sub_list_ptr->address << 0x10) |
				(scb->next_scb_ptr->address));
    
		scb->parent_scb_ptr = NULL;
		spin_unlock_irqrestore(&chip->reg_lock, flags);
	}
}

void cs46xx_dsp_remove_scb (cs46xx_t *chip, dsp_scb_descriptor_t * scb)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;

	down(&ins->scb_mutex);
	/* check integrety */
	snd_assert ( (scb->index >= 0 && 
		      scb->index < ins->nscb && 
		      (ins->scbs + scb->index) == scb), goto _end );

#if 0
	/* cant remove a SCB with childs before 
	   removing childs first  */
	snd_assert ( (scb->sub_list_ptr == ins->the_null_scb &&
		      scb->next_scb_ptr == ins->the_null_scb),
		     goto _end);
#endif

	_dsp_unlink_scb (chip,scb);
  
	cs46xx_dsp_proc_free_scb_desc(scb);
	snd_assert (scb->scb_symbol != NULL, goto _end);
	remove_symbol (chip,scb->scb_symbol);

	ins->scbs[scb->index].deleted = 1;

	if (scb->index < ins->scb_highest_frag_index)
		ins->scb_highest_frag_index = scb->index;

	if (scb->index == ins->nscb - 1) {
		ins->nscb --;
	}

	if (ins->scb_highest_frag_index > ins->nscb) {
		ins->scb_highest_frag_index = ins->nscb;
	}

#if 0
	/* !!!! THIS IS A PIECE OF SHIT MADE BY ME !!! */
	for(i = scb->index + 1;i < ins->nscb; ++i) {
		ins->scbs[i - 1].index = i - 1;
	}
#endif

#ifdef CONFIG_SND_DEBUG
 _end:
#endif
	up(&ins->scb_mutex);
}


void cs46xx_dsp_proc_free_scb_desc (dsp_scb_descriptor_t * scb)
{
	if (scb->proc_info) {
		proc_scb_info_t * scb_info  = (proc_scb_info_t *)scb->proc_info->private_data;

		snd_printdd("cs46xx_dsp_proc_free_scb_desc: freeing %s\n",scb->scb_name);

		snd_info_unregister(scb->proc_info);
		scb->proc_info = NULL;

		snd_assert (scb_info != NULL, return);
		kfree (scb_info);
	}
}

void cs46xx_dsp_proc_register_scb_desc (cs46xx_t *chip,dsp_scb_descriptor_t * scb)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	snd_info_entry_t * entry;
	proc_scb_info_t * scb_info;

	down(&ins->scb_mutex);
	/* register to proc */
	if (ins->snd_card != NULL && ins->proc_dsp_dir != NULL &&
	    scb->proc_info == NULL) {
  
		if ((entry = snd_info_create_card_entry(ins->snd_card, scb->scb_name, 
							ins->proc_dsp_dir)) != NULL) {
			scb_info = kmalloc(sizeof(proc_scb_info_t), GFP_KERNEL);
			scb_info->chip = chip;
			scb_info->scb_desc = scb;
      
			entry->content = SNDRV_INFO_CONTENT_TEXT;
			entry->private_data = scb_info;
			entry->mode = S_IFREG | S_IRUGO | S_IWUSR;
      
			entry->c.text.read_size = 512;
			entry->c.text.read = cs46xx_dsp_proc_scb_info_read;
      
			if (snd_info_register(entry) < 0) {
				snd_info_free_entry(entry);
				kfree (scb_info);
				entry = NULL;
			}
		}

		scb->proc_info = entry;
	}
	up(&ins->scb_mutex);
}

static dsp_scb_descriptor_t * 
_dsp_create_generic_scb (cs46xx_t *chip,char * name, u32 * scb_data,u32 dest,
                         symbol_entry_t * task_entry,
                         dsp_scb_descriptor_t * parent_scb,
                         int scb_child_type)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	dsp_scb_descriptor_t * scb;
  
	unsigned long flags;

	down(&ins->scb_mutex);
	snd_assert (ins->the_null_scb != NULL,goto _fail_end);

	/* fill the data that will be wroten to DSP */
	scb_data[SCBsubListPtr] = 
		(ins->the_null_scb->address << 0x10) | ins->the_null_scb->address;

	scb_data[SCBfuncEntryPtr] &= 0xFFFF0000;
	scb_data[SCBfuncEntryPtr] |= task_entry->address;

	snd_printdd("dsp_spos: creating SCB <%s>\n",name);

	scb = cs46xx_dsp_create_scb(chip,name,scb_data,dest);


	scb->sub_list_ptr = ins->the_null_scb;
	scb->next_scb_ptr = ins->the_null_scb;

	scb->parent_scb_ptr = parent_scb;
	scb->task_entry = task_entry;

  
	/* update parent SCB */
	if (scb->parent_scb_ptr) {
#if 0
		printk ("scb->parent_scb_ptr = %s\n",scb->parent_scb_ptr->scb_name);
		printk ("scb->parent_scb_ptr->next_scb_ptr = %s\n",scb->parent_scb_ptr->next_scb_ptr->scb_name);
		printk ("scb->parent_scb_ptr->sub_list_ptr = %s\n",scb->parent_scb_ptr->sub_list_ptr->scb_name);
#endif
		/* link to  parent SCB */
		if (scb_child_type == SCB_ON_PARENT_NEXT_SCB) {
			snd_assert ( (scb->parent_scb_ptr->next_scb_ptr == ins->the_null_scb),
				     goto _fail_end);

			scb->parent_scb_ptr->next_scb_ptr = scb;

		} else if (scb_child_type == SCB_ON_PARENT_SUBLIST_SCB) {
			snd_assert ( (scb->parent_scb_ptr->sub_list_ptr == ins->the_null_scb),
				     goto _fail_end);

			scb->parent_scb_ptr->sub_list_ptr = scb;
		} else {
			snd_assert (0,goto _fail_end);
		}

		spin_lock_irqsave(&chip->reg_lock, flags);
		/* update entry in DSP RAM */
		snd_cs46xx_poke(chip,
				(scb->parent_scb_ptr->address + SCBsubListPtr) << 2,
				(scb->parent_scb_ptr->sub_list_ptr->address << 0x10) |
				(scb->parent_scb_ptr->next_scb_ptr->address));

		spin_unlock_irqrestore(&chip->reg_lock, flags);
	}


	up(&ins->scb_mutex);

	cs46xx_dsp_proc_register_scb_desc (chip,scb);

	return scb;
#ifdef CONFIG_SND_DEBUG
 _fail_end:

	up(&ins->scb_mutex);
	return NULL;
#endif
}

dsp_scb_descriptor_t * 
cs46xx_dsp_create_generic_scb (cs46xx_t *chip,char * name, u32 * scb_data,u32 dest,
                               char * task_entry_name,
                               dsp_scb_descriptor_t * parent_scb,
                               int scb_child_type)
{
	symbol_entry_t * task_entry;

	task_entry = cs46xx_dsp_lookup_symbol (chip,task_entry_name,
					       SYMBOL_CODE);
  
	if (task_entry == NULL) {
		snd_printk (KERN_ERR "dsp_spos: symbol %s not found\n",task_entry_name);
		return NULL;
	}
  
	return _dsp_create_generic_scb (chip,name,scb_data,dest,task_entry,
					parent_scb,scb_child_type);
}

dsp_scb_descriptor_t * 
cs46xx_dsp_create_timing_master_scb (cs46xx_t *chip)
{
	dsp_scb_descriptor_t * scb;
  
	timing_master_scb_t timing_master_scb = {
		{ 0,
		  0,
		  0,
		  0
		},
		{ 0,
		  0,
		  0,
		  0,
		  0
		},
		0,0,
		0,NULL_SCB_ADDR,
		0,0,             /* extraSampleAccum:TMreserved */
		0,0,             /* codecFIFOptr:codecFIFOsyncd */
		0x0001,0x8000,   /* fracSampAccumQm1:TMfrmsLeftInGroup */
		0x0001,0x0000,   /* fracSampCorrectionQm1:TMfrmGroupLength */
		0x00060000       /* nSampPerFrmQ15 */
	};    
  
	scb = cs46xx_dsp_create_generic_scb(chip,"TimingMasterSCBInst",(u32 *)&timing_master_scb,
					    TIMINGMASTER_SCB_ADDR,
					    "TIMINGMASTER",NULL,SCB_NO_PARENT);

	return scb;
}


dsp_scb_descriptor_t * 
cs46xx_dsp_create_codec_out_scb(cs46xx_t * chip,char * codec_name,
                                u16 channel_disp,u16 fifo_addr,
                                u16 child_scb_addr,
                                u32 dest,dsp_scb_descriptor_t * parent_scb,
                                int scb_child_type)
{
	dsp_scb_descriptor_t * scb;
  
	codec_output_scb_t codec_out_scb = {
		{ 0,
		  0,
		  0,
		  0
		},
		{
			0,
			0,
			0,
			0,
			0
		},
		0,0,
		0,NULL_SCB_ADDR,
		0,                      /* COstrmRsConfig */
		0,                      /* COstrmBufPtr */
		channel_disp,fifo_addr, /* leftChanBaseIOaddr:rightChanIOdisp */
		0x0000,0x0080,          /* (!AC97!) COexpVolChangeRate:COscaleShiftCount */
		0,child_scb_addr        /* COreserved - need child scb to work with rom code */
	};
  
  
	scb = cs46xx_dsp_create_generic_scb(chip,codec_name,(u32 *)&codec_out_scb,
					    dest,"S16_CODECOUTPUTTASK",parent_scb,
					    scb_child_type);
  
	return scb;
}

dsp_scb_descriptor_t * 
cs46xx_dsp_create_codec_in_scb(cs46xx_t * chip,char * codec_name,
                                u16 channel_disp,u16 fifo_addr,
                                u16 sample_buffer_addr,
                                u32 dest,dsp_scb_descriptor_t * parent_scb,
                                int scb_child_type)
{

	dsp_scb_descriptor_t * scb;
	codec_input_scb_t codec_input_scb = {
		{ 0,
		  0,
		  0,
		  0
		},
		{
			0,
			0,
			0,
			0,
			0
		},
    
#if 0  /* cs4620 */
		SyncIOSCB,NULL_SCB_ADDR
#else
		0 , 0,
#endif
		0,0,

		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_64,  /* strmRsConfig */
		sample_buffer_addr << 0x10,       /* strmBufPtr; defined as a dword ptr, used as a byte ptr */
		channel_disp,fifo_addr,           /* (!AC97!) leftChanBaseINaddr=AC97primary 
						     link input slot 3 :rightChanINdisp=""slot 4 */
		0x0000,0x0000,                    /* (!AC97!) ????:scaleShiftCount; no shift needed 
						     because AC97 is already 20 bits */
		0x80008000                        /* ??clw cwcgame.scb has 0 */
	};
  
	scb = cs46xx_dsp_create_generic_scb(chip,codec_name,(u32 *)&codec_input_scb,
					    dest,"S16_CODECINPUTTASK",parent_scb,
					    scb_child_type);
	return scb;
}


dsp_scb_descriptor_t * 
cs46xx_dsp_create_pcm_reader_scb(cs46xx_t * chip,char * scb_name,
                                 u16 sample_buffer_addr,u32 dest,
                                 int virtual_channel, u32 playback_hw_addr,
                                 dsp_scb_descriptor_t * parent_scb,
                                 int scb_child_type)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	dsp_scb_descriptor_t * scb;
  
	generic_scb_t pcm_reader_scb = {
    
		/*
		  Play DMA Task xfers data from host buffer to SP buffer
		  init/runtime variables:
		  PlayAC: Play Audio Data Conversion - SCB loc: 2nd dword, mask: 0x0000F000L
		  DATA_FMT_16BIT_ST_LTLEND(0x00000000L)   from 16-bit stereo, little-endian
		  DATA_FMT_8_BIT_ST_SIGNED(0x00001000L)   from 8-bit stereo, signed
		  DATA_FMT_16BIT_MN_LTLEND(0x00002000L)   from 16-bit mono, little-endian
		  DATA_FMT_8_BIT_MN_SIGNED(0x00003000L)   from 8-bit mono, signed
		  DATA_FMT_16BIT_ST_BIGEND(0x00004000L)   from 16-bit stereo, big-endian
		  DATA_FMT_16BIT_MN_BIGEND(0x00006000L)   from 16-bit mono, big-endian
		  DATA_FMT_8_BIT_ST_UNSIGNED(0x00009000L) from 8-bit stereo, unsigned
		  DATA_FMT_8_BIT_MN_UNSIGNED(0x0000b000L) from 8-bit mono, unsigned
		  ? Other combinations possible from:
		  DMA_RQ_C2_AUDIO_CONVERT_MASK    0x0000F000L
		  DMA_RQ_C2_AC_NONE               0x00000000L
		  DMA_RQ_C2_AC_8_TO_16_BIT        0x00001000L
		  DMA_RQ_C2_AC_MONO_TO_STEREO     0x00002000L
		  DMA_RQ_C2_AC_ENDIAN_CONVERT     0x00004000L
		  DMA_RQ_C2_AC_SIGNED_CONVERT     0x00008000L
        
		  HostBuffAddr: Host Buffer Physical Byte Address - SCB loc:3rd dword, Mask: 0xFFFFFFFFL
		  aligned to dword boundary
		*/
		/* Basic (non scatter/gather) DMA requestor (4 ints) */
		{ DMA_RQ_C1_SOURCE_ON_HOST +        /* source buffer is on the host */
		  DMA_RQ_C1_SOURCE_MOD1024 +        /* source buffer is 1024 dwords (4096 bytes) */
		  DMA_RQ_C1_DEST_MOD32 +            /* dest buffer(PCMreaderBuf) is 32 dwords*/
		  DMA_RQ_C1_WRITEBACK_SRC_FLAG +    /* ?? */
		  DMA_RQ_C1_WRITEBACK_DEST_FLAG +   /* ?? */
		  15,                             /* DwordCount-1: picked 16 for DwordCount because Jim */
		  /*        Barnette said that is what we should use since */
		  /*        we are not running in optimized mode? */
		  DMA_RQ_C2_AC_NONE +
		  DMA_RQ_C2_SIGNAL_SOURCE_PINGPONG + /* set play interrupt (bit0) in HISR when source */
		  /*   buffer (on host) crosses half-way point */
		  virtual_channel,                   /* Play DMA channel arbitrarily set to 0 */
		  playback_hw_addr,                  /* HostBuffAddr (source) */
		  DMA_RQ_SD_SP_SAMPLE_ADDR +         /* destination buffer is in SP Sample Memory */
		  sample_buffer_addr                 /* SP Buffer Address (destination) */
		},
		/* Scatter/gather DMA requestor extension   (5 ints) */
		{
			0,
			0,
			0,
			0,
			0 
		},
		/* Sublist pointer & next stream control block (SCB) link. */
		NULL_SCB_ADDR,NULL_SCB_ADDR,
		/* Pointer to this tasks parameter block & stream function pointer */
		0,NULL_SCB_ADDR,
		/* rsConfig register for stream buffer (rsDMA reg. is loaded from basicReq.daw */
		/*   for incoming streams, or basicReq.saw, for outgoing streams) */
		RSCONFIG_DMA_ENABLE +                 /* enable DMA */
		(19 << RSCONFIG_MAX_DMA_SIZE_SHIFT) + /* MAX_DMA_SIZE picked to be 19 since SPUD  */
		/*  uses it for some reason */
		((dest >> 4) << RSCONFIG_STREAM_NUM_SHIFT) + /* stream number = SCBaddr/16 */
		RSCONFIG_SAMPLE_16STEREO +
		RSCONFIG_MODULO_32,             /* dest buffer(PCMreaderBuf) is 32 dwords (256 bytes) */
		/* Stream sample pointer & MAC-unit mode for this stream */
		(sample_buffer_addr << 0x10),
		/* Fractional increment per output sample in the input sample buffer */
		0, 
		{
			/* Standard stereo volume control */
			0x8000,0x8000,
			0x8000,0x8000
		}
	};

	if (ins->null_algorithm == NULL) {
		ins->null_algorithm =  cs46xx_dsp_lookup_symbol (chip,"NULLALGORITHM",
								 SYMBOL_CODE);
    
		if (ins->null_algorithm == NULL) {
			snd_printk (KERN_ERR "dsp_spos: symbol NULLALGORITHM not found\n");
			return NULL;
		}    
	}

	scb = _dsp_create_generic_scb(chip,scb_name,(u32 *)&pcm_reader_scb,
				      dest,ins->null_algorithm,parent_scb,
				      scb_child_type);
  
	return scb;
}

dsp_scb_descriptor_t * 
cs46xx_dsp_create_src_task_scb(cs46xx_t * chip,char * scb_name,
                               u16 src_buffer_addr,
                               u16 src_delay_buffer_addr,u32 dest,
                               dsp_scb_descriptor_t * parent_scb,
                               int scb_child_type)
{

	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	dsp_scb_descriptor_t * scb;
    
	src_task_scb_t src_task_scb = {
		0x0028,0x00c8,
		0x5555,0x0000,
		0x0000,0x0000,
		src_buffer_addr,1,
		0x0028,0x00c8,
		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_32,  
		0x0000,src_delay_buffer_addr,                  
		0x0,                                            
		0x80,(src_delay_buffer_addr + (24 * 4)),
		0,0, /* next_scb, sub_list_ptr */
		0,0, /* entry, this_spb */
		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_8,
		src_buffer_addr << 0x10,
		0x04000000,
		{ 
			0x8000,0x8000,
			0x8000,0x8000
		}
	};

	if (ins->s16_up == NULL) {
		ins->s16_up =  cs46xx_dsp_lookup_symbol (chip,"S16_UPSRC",
							 SYMBOL_CODE);
    
		if (ins->s16_up == NULL) {
			snd_printk (KERN_ERR "dsp_spos: symbol S16_UPSRC not found\n");
			return NULL;
		}    
	}

	scb = _dsp_create_generic_scb(chip,scb_name,(u32 *)&src_task_scb,
				      dest,ins->s16_up,parent_scb,
				      scb_child_type);

	return scb;
}


dsp_scb_descriptor_t * 
cs46xx_dsp_create_mix_only_scb(cs46xx_t * chip,char * scb_name,
                               u16 mix_buffer_addr,u32 dest,
                               dsp_scb_descriptor_t * parent_scb,
                               int scb_child_type)
{
	dsp_scb_descriptor_t * scb;
  
	mix_only_scb_t master_mix_scb = {
		/* 0 */ { 0,
			  /* 1 */   0,
			  /* 2 */  mix_buffer_addr,
			  /* 3 */  0
			  /*   */ },
		{
			/* 4 */  0,
			/* 5 */  0,
			/* 6 */  0,
			/* 7 */  0,
			/* 8 */  0x00000080
		},
		/* 9 */ 0,0,
		/* A */ 0,0,
		/* B */ RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_64,
		/* C */ (mix_buffer_addr  + (32 * 4)) << 0x10, 
		/* D */ 0,
		{
			/* E */ 0x8000,0x8000,
			/* F */ 0x8000,0x8000
		}
	};


	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&master_mix_scb,
					    dest,"S16_MIX",parent_scb,
					    scb_child_type);
	return scb;
}


dsp_scb_descriptor_t * 
cs46xx_dsp_create_mix_to_ostream_scb(cs46xx_t * chip,char * scb_name,
                                     u16 mix_buffer_addr,u16 writeback_spb,u32 dest,
                                     dsp_scb_descriptor_t * parent_scb,
                                     int scb_child_type)
{
	dsp_scb_descriptor_t * scb;

	mix2_ostream_scb_t mix2_ostream_scb = {
		/* Basic (non scatter/gather) DMA requestor (4 ints) */
		{ 
			DMA_RQ_C1_SOURCE_MOD64 +
			DMA_RQ_C1_DEST_ON_HOST +
			DMA_RQ_C1_DEST_MOD1024 +
			DMA_RQ_C1_WRITEBACK_SRC_FLAG + 
			DMA_RQ_C1_WRITEBACK_DEST_FLAG +
			15,                            
      
			DMA_RQ_C2_AC_NONE +
			DMA_RQ_C2_SIGNAL_DEST_PINGPONG + 
      
			CS46XX_DSP_CAPTURE_CHANNEL,                                 
			DMA_RQ_SD_SP_SAMPLE_ADDR + 
			mix_buffer_addr, 
			0x0                   
		},
    
		{ 0, 0, 0, 0, 0, },
		0,0,
		0,writeback_spb,
    
		RSCONFIG_DMA_ENABLE + 
		(19 << RSCONFIG_MAX_DMA_SIZE_SHIFT) + 
    
		((dest >> 4) << RSCONFIG_STREAM_NUM_SHIFT) +
		RSCONFIG_DMA_TO_HOST + 
		RSCONFIG_SAMPLE_16STEREO +
		RSCONFIG_MODULO_64,    
		(mix_buffer_addr + (32 * 4)) << 0x10,
		1,0,            
		0x0001,0x0080,
		0xFFFF,0
	};


	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&mix2_ostream_scb,
					    dest,"S16_MIX_TO_OSTREAM",parent_scb,
					    scb_child_type);
  
	return scb;
}


dsp_scb_descriptor_t * 
cs46xx_dsp_create_vari_decimate_scb(cs46xx_t * chip,char * scb_name,
                                    u16 vari_buffer_addr0,
                                    u16 vari_buffer_addr1,
                                    u32 dest,
                                    dsp_scb_descriptor_t * parent_scb,
                                    int scb_child_type)
{

	dsp_scb_descriptor_t * scb;
  
	vari_decimate_scb_t vari_decimate_scb = {
		0x0028,0x00c8,
		0x5555,0x0000,
		0x0000,0x0000,
		vari_buffer_addr0,vari_buffer_addr1,
    
		0x0028,0x00c8,
		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_256, 
    
		0xFF800000,   
		0,
		0x0080,vari_buffer_addr1 + (25 * 4), 
    
		0,0, 
		0,0,

		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_8,
		vari_buffer_addr0 << 0x10,   
		0x04000000,                   
		{
			0x8000,0x8000, 
			0xFFFF,0xFFFF
		}
	};

	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&vari_decimate_scb,
					    dest,"VARIDECIMATE",parent_scb,
					    scb_child_type);
  
	return scb;
}


dsp_scb_descriptor_t * 
cs46xx_dsp_create_pcm_serial_input_scb(cs46xx_t * chip,char * scb_name,u32 dest,
                                       dsp_scb_descriptor_t * input_scb,
                                       dsp_scb_descriptor_t * parent_scb,
                                       int scb_child_type)
{

	dsp_scb_descriptor_t * scb;


	pcm_serial_input_scb_t pcm_serial_input_scb = {
		{ 0,
		  0,
		  0,
		  0
		},
		{
			0,
			0,
			0,
			0,
			0
		},

		0,0,
		0,0,

		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_16,
		0,
		0,input_scb->address, 
		{
			0x8000,0x8000,
			0x8000,0x8000
		}
	};

	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&pcm_serial_input_scb,
					    dest,"PCMSERIALINPUTTASK",parent_scb,
					    scb_child_type);
	return scb;
}


dsp_scb_descriptor_t * 
cs46xx_dsp_create_asynch_fg_tx_scb(cs46xx_t * chip,char * scb_name,u32 dest,
                                   u16 hfg_scb_address,
                                   u16 asynch_buffer_address,
                                   dsp_scb_descriptor_t * parent_scb,
                                   int scb_child_type)
{

	dsp_scb_descriptor_t * scb;

	asynch_fg_tx_scb_t asynch_fg_tx_scb = {
		0xfc00,0x03ff,      /*  Prototype sample buffer size of 256 dwords */
		0x0058,0x0028,      /* Min Delta 7 dwords == 28 bytes */
		/* : Max delta 25 dwords == 100 bytes */
		0,hfg_scb_address,  /* Point to HFG task SCB */
		0,0,				/* Initialize current Delta and Consumer ptr adjustment count */
		0,                  /* Initialize accumulated Phi to 0 */
		0,0x2aab,           /* Const 1/3 */
    
		{
			0,                /* Define the unused elements */
			0,
			0
		},
    
		0,0,
		0,dest + AFGTxAccumPhi,
    
		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_256, /* Stereo, 256 dword */
		(asynch_buffer_address) << 0x10,  /* This should be automagically synchronized
                                             to the producer pointer */
    
		/* There is no correct initial value, it will depend upon the detected
		   rate etc  */
		0x18000000,                     /* Phi increment for approx 32k operation */
		0x8000,0x8000,                  /* Volume controls are unused at this time */
		0x8000,0x8000
	};
  
	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&asynch_fg_tx_scb,
					    dest,"ASYNCHFGTXCODE",parent_scb,
					    scb_child_type);

	return scb;
}


dsp_scb_descriptor_t * 
cs46xx_dsp_create_asynch_fg_rx_scb(cs46xx_t * chip,char * scb_name,u32 dest,
                                   u16 hfg_scb_address,
                                   u16 asynch_buffer_address,
                                   dsp_scb_descriptor_t * parent_scb,
                                   int scb_child_type)
{

	dsp_scb_descriptor_t * scb;

	asynch_fg_rx_scb_t asynch_fg_rx_scb = {
		0xfe00,0x01ff,      /*  Prototype sample buffer size of 128 dwords */
		0x0064,0x001c,      /* Min Delta 7 dwords == 28 bytes */
		                    /* : Max delta 25 dwords == 100 bytes */
		0,hfg_scb_address,  /* Point to HFG task SCB */
		0,0,				/* Initialize current Delta and Consumer ptr adjustment count */
		{
			0,                /* Define the unused elements */
			0,
			0,
			0,
			0
		},
      
		0,0,
		0,dest,
    
		RSCONFIG_MODULO_128 |
        RSCONFIG_SAMPLE_16STEREO,                         /* Stereo, 128 dword */
		( (asynch_buffer_address + (16 * 4))  << 0x10),   /* This should be automagically 
							                                  synchrinized to the producer pointer */
    
		/* There is no correct initial value, it will depend upon the detected
		   rate etc  */
		0x18000000,         
		0x8000,0x8000,       
		0xFFFF,0xFFFF
	};

	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&asynch_fg_rx_scb,
					    dest,"ASYNCHFGRXCODE",parent_scb,
					    scb_child_type);

	return scb;
}


dsp_scb_descriptor_t * 
cs46xx_dsp_create_output_snoop_scb(cs46xx_t * chip,char * scb_name,u32 dest,
                                   u16 snoop_buffer_address,
                                   dsp_scb_descriptor_t * snoop_scb,
                                   dsp_scb_descriptor_t * parent_scb,
                                   int scb_child_type)
{

	dsp_scb_descriptor_t * scb;
  
	output_snoop_scb_t output_snoop_scb = {
		{ 0,	/*  not used.  Zero */
		  0,
		  0,
		  0,
		},
		{
			0, /* not used.  Zero */
			0,
			0,
			0,
			0
		},
    
		0,0,
		0,0,
    
		RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_64,
		snoop_buffer_address << 0x10,  
		0,0,
		0,
		0,snoop_scb->address
	};
  
	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&output_snoop_scb,
					    dest,"OUTPUTSNOOP",parent_scb,
					    scb_child_type);
	return scb;
}


dsp_scb_descriptor_t * 
cs46xx_dsp_create_spio_write_scb(cs46xx_t * chip,char * scb_name,u32 dest,
                                 dsp_scb_descriptor_t * parent_scb,
                                 int scb_child_type)
{
	dsp_scb_descriptor_t * scb;
  
	spio_write_scb_t spio_write_scb = {
		0,0,         /*   SPIOWAddress2:SPIOWAddress1; */
		0,           /*   SPIOWData1; */
		0,           /*   SPIOWData2; */
		0,0,         /*   SPIOWAddress4:SPIOWAddress3; */
		0,           /*   SPIOWData3; */
		0,           /*   SPIOWData4; */
		0,0,         /*   SPIOWDataPtr:Unused1; */
		{ 0,0 },     /*   Unused2[2]; */
    
		0,0,	     /*   SPIOWChildPtr:SPIOWSiblingPtr; */
		0,0,         /*   SPIOWThisPtr:SPIOWEntryPoint; */
    
		{ 
			0,
			0,
			0,
			0,
			0          /*   Unused3[5];  */
		}
	};

	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&spio_write_scb,
					    dest,"SPIOWRITE",parent_scb,
					    scb_child_type);

	return scb;
}

dsp_scb_descriptor_t *  cs46xx_dsp_create_magic_snoop_scb(cs46xx_t * chip,char * scb_name,u32 dest,
                                                          u16 snoop_buffer_address,
                                                          dsp_scb_descriptor_t * snoop_scb,
                                                          dsp_scb_descriptor_t * parent_scb,
                                                          int scb_child_type)
{
	dsp_scb_descriptor_t * scb;
  
	magic_snoop_task_t magic_snoop_scb = {
		/* 0 */ 0, /* i0 */
		/* 1 */ 0, /* i1 */
		/* 2 */ snoop_buffer_address << 0x10,
		/* 3 */ 0,snoop_scb->address,
		/* 4 */ 0, /* i3 */
		/* 5 */ 0, /* i4 */
		/* 6 */ 0, /* i5 */
		/* 7 */ 0, /* i6 */
		/* 8 */ 0, /* i7 */
		/* 9 */ 0,0, /* next_scb, sub_list_ptr */
		/* A */ 0,0, /* entry_point, this_ptr */
		/* B */ RSCONFIG_SAMPLE_16STEREO + RSCONFIG_MODULO_64,
		/* C */ snoop_buffer_address  << 0x10,
		/* D */ 0,
		/* E */ { 0x8000,0x8000,
			  /* F */   0xffff,0xffff
		}
	};

	scb = cs46xx_dsp_create_generic_scb(chip,scb_name,(u32 *)&magic_snoop_scb,
					    dest,"MAGICSNOOPTASK",parent_scb,
					    scb_child_type);

	return scb;
}

static dsp_scb_descriptor_t * find_next_free_scb (cs46xx_t * chip,dsp_scb_descriptor_t * from)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	dsp_scb_descriptor_t * scb = from;

	while (scb->next_scb_ptr != ins->the_null_scb) {
		snd_assert (scb->next_scb_ptr != NULL, return NULL);

		scb = scb->next_scb_ptr;
	}

	return scb;
}

static u32 pcm_reader_buffer_addr[DSP_MAX_PCM_CHANNELS] = {
	0x0600, /* 1 */
	0x1500, /* 2 */
	0x1580, /* 3 */
	0x1600, /* 4 */
	0x1680, /* 5 */
	0x1700, /* 6 */
	0x1780, /* 7 */
	0x1800, /* 8 */
	0x1880, /* 9 */
	0x1900, /* 10 */
	0x1980, /* 11 */
	0x1A00, /* 12 */
	0x1A80, /* 13 */
	0x1B00, /* 14 */
	0x1B80, /* 15 */
	0x1C00, /* 16 */
	0x1C80, /* 17 */
	0x1D00, /* 18 */
	0x1D80, /* 19 */
	0x1E00, /* 20 */
	0x1E80, /* 21 */
	0x1F00, /* 22 */
	0x1F80, /* 23 */
	0x2000, /* 24 */
	0x2080, /* 25 */
	0x2100, /* 26 */
	0x2180, /* 27 */
	0x2200, /* 28 */
	0x2280, /* 29 */
	0x2300, /* 30 */
	0x2380, /* 31 */
	0x2400, /* 32 */
};

static u32 src_output_buffer_addr[DSP_MAX_SRC_NR] = {
	0x2580,
	0x2680,
	0x2780,
	0x2980,  
	0x2A80,  
	0x2B80,  
};

static u32 src_delay_buffer_addr[DSP_MAX_SRC_NR] = {
	0x2600,
	0x2700,
	0x2800,
	0x2900,
	0x2A00,
	0x2B00,
};


pcm_channel_descriptor_t * cs46xx_dsp_create_pcm_channel (cs46xx_t * chip,u32 sample_rate, void * private_data)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	dsp_scb_descriptor_t * src_scb = NULL,* pcm_scb;
	dsp_scb_descriptor_t * pcm_parent_scb;
	char scb_name[DSP_MAX_SCB_NAME];
	int i,pcm_index = -1, insert_point, src_index = -1;
	unsigned long flags;

	down(&ins->pcm_mutex); 

	/* default sample rate is 44100 */
	if (!sample_rate) sample_rate = 44100;

	/* search for a already created SRC SCB with the same sample rate */
	for (i = 0; i < DSP_MAX_PCM_CHANNELS && 
		     (pcm_index == -1 || src_scb == NULL); ++i) {

		/* virtual channel reserved 
		   for capture */
		if (i == CS46XX_DSP_CAPTURE_CHANNEL) continue;

		if (ins->pcm_channels[i].active) {
			if (!src_scb && ins->pcm_channels[i].sample_rate == sample_rate) {
				src_scb = ins->pcm_channels[i].src_scb;
				ins->pcm_channels[i].src_scb->ref_count ++;
				src_index = ins->pcm_channels[i].src_slot;
			}
		} else if (pcm_index == -1) {
			pcm_index = i;
		}
	}

	if (pcm_index == -1) {
		snd_printk (KERN_ERR "dsp_spos: no free PCM channel\n");
		goto _end;
	}

	if (src_scb == NULL) {
		dsp_scb_descriptor_t * src_parent_scb;

		if (ins->nsrc_scb >= DSP_MAX_SRC_NR) {
			snd_printk(KERN_ERR "dsp_spos: to many SRC instances\n!");
			goto _end;
		}

		/* find a free slot */
		for (i = 0; i < DSP_MAX_SRC_NR; ++i) {
			if (ins->src_scb_slots[i] == 0) {
				src_index = i;
				ins->src_scb_slots[i] = 1;
				break;
			}
		}
		snd_assert (src_index != -1,goto _end);

		/* we need to create a new SRC SCB */
		if (ins->master_mix_scb->sub_list_ptr == ins->the_null_scb) {
			src_parent_scb = ins->master_mix_scb;
			insert_point = SCB_ON_PARENT_SUBLIST_SCB;
		} else {
			src_parent_scb = find_next_free_scb(chip,ins->master_mix_scb->sub_list_ptr);
			insert_point = SCB_ON_PARENT_NEXT_SCB;
		}

		snprintf (scb_name,DSP_MAX_SCB_NAME,"SrcTask_SCB%d",src_index);

		snd_printdd( "dsp_spos: creating SRC \"%s\"\n",scb_name);
		src_scb = cs46xx_dsp_create_src_task_scb(chip,scb_name,
							 src_output_buffer_addr[src_index],
							 src_delay_buffer_addr[src_index],
							 /* 0x400 - 0x600 source SCBs */
							 0x400 + (src_index * 0x10) ,
							 src_parent_scb,
							 insert_point);

		if (!src_scb) {
			snd_printk (KERN_ERR "dsp_spos: failed to create SRCtaskSCB\n");
			goto _end;
		}

		cs46xx_dsp_set_src_sample_rate(chip,src_scb,sample_rate);

		ins->nsrc_scb ++;

		/* insert point for the PCMreader task */
		pcm_parent_scb = src_scb;
		insert_point = SCB_ON_PARENT_SUBLIST_SCB;
	} else {
      
      /* if channel is unlinked then src_scb->sub_list_ptr == null_scb, and
         that's a correct state.
        snd_assert (src_scb->sub_list_ptr != ins->the_null_scb, goto _end); 
      */
      if (src_scb->sub_list_ptr != ins->the_null_scb) {
		pcm_parent_scb = find_next_free_scb(chip,src_scb->sub_list_ptr);
        
		insert_point = SCB_ON_PARENT_NEXT_SCB;
      } else {
        pcm_parent_scb = src_scb;
		insert_point = SCB_ON_PARENT_SUBLIST_SCB;
      }
	}
  
  
  
	snprintf (scb_name,DSP_MAX_SCB_NAME,"PCMReader_SCB%d",pcm_index);

	snd_printdd( "dsp_spos: creating PCM \"%s\"\n",scb_name);

	pcm_scb = cs46xx_dsp_create_pcm_reader_scb(chip,scb_name,
						   pcm_reader_buffer_addr[pcm_index],
						   /* 0x200 - 400 PCMreader SCBs */
						   (pcm_index * 0x10) + 0x200,
						   pcm_index, /* virtual channel 0-31 */
						   0, /* pcm hw addr */
						   pcm_parent_scb,
						   insert_point);

	if (!pcm_scb) {
		snd_printk (KERN_ERR "dsp_spos: failed to create PCMreaderSCB\n");
		goto _end;
	}

	spin_lock_irqsave(&chip->reg_lock, flags);
	ins->pcm_channels[pcm_index].sample_rate = sample_rate;
	ins->pcm_channels[pcm_index].pcm_reader_scb = pcm_scb;
	ins->pcm_channels[pcm_index].src_scb = src_scb;
	ins->pcm_channels[pcm_index].unlinked = 0;  
	ins->pcm_channels[pcm_index].private_data = private_data;
	ins->pcm_channels[pcm_index].src_slot = src_index;
	ins->pcm_channels[pcm_index].active = 1;
	ins->pcm_channels[pcm_index].pcm_slot = pcm_index;
	ins->npcm_channels ++;
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	up(&ins->pcm_mutex);  
	return (ins->pcm_channels + pcm_index);
 _end:

	up(&ins->pcm_mutex);
	return NULL;
}

void cs46xx_dsp_destroy_pcm_channel (cs46xx_t * chip,pcm_channel_descriptor_t * pcm_channel)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	unsigned long flags;

	down(&ins->pcm_mutex);
  
	snd_assert(pcm_channel->active,goto _end);
	snd_assert(ins->npcm_channels > 0,goto _end);
	snd_assert(pcm_channel->src_scb->ref_count > 0,goto _end);

	spin_lock_irqsave(&chip->reg_lock, flags);
	pcm_channel->unlinked = 1;
	pcm_channel->active = 0;
	pcm_channel->private_data = NULL;
	pcm_channel->src_scb->ref_count --;
	ins->npcm_channels --;
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	cs46xx_dsp_remove_scb(chip,pcm_channel->pcm_reader_scb);

	if (!pcm_channel->src_scb->ref_count) {
		cs46xx_dsp_remove_scb(chip,pcm_channel->src_scb);

		snd_assert (pcm_channel->src_slot >= 0 && pcm_channel->src_slot <= DSP_MAX_SRC_NR,
			    goto _end);

		ins->src_scb_slots[pcm_channel->src_slot] = 0;
		ins->nsrc_scb --;
	}


#ifdef CONFIG_SND_DEBUG
 _end:
#endif

	up(&ins->pcm_mutex);
}

int cs46xx_dsp_pcm_unlink (cs46xx_t * chip,pcm_channel_descriptor_t * pcm_channel)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	unsigned long flags;

	down(&ins->pcm_mutex);
	down(&ins->scb_mutex);

	snd_assert(pcm_channel->active,goto _end);
	snd_assert(ins->npcm_channels > 0,goto _end);

	if (pcm_channel->unlinked)
		goto _end;

	spin_lock_irqsave(&chip->reg_lock, flags);
	pcm_channel->unlinked = 1;
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	_dsp_unlink_scb (chip,pcm_channel->pcm_reader_scb);

 _end:
	up(&ins->scb_mutex);
	up(&ins->pcm_mutex);

	return 0;
}

int cs46xx_dsp_pcm_link (cs46xx_t * chip,pcm_channel_descriptor_t * pcm_channel)
{
	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	dsp_scb_descriptor_t * parent_scb;
	dsp_scb_descriptor_t * src_scb = pcm_channel->src_scb;
	unsigned long flags;

	down(&ins->pcm_mutex);
	down(&ins->scb_mutex);

	if (pcm_channel->unlinked == 0)
		goto _end;

	if (src_scb->sub_list_ptr == ins->the_null_scb) {
		parent_scb = src_scb;
		parent_scb->sub_list_ptr = pcm_channel->pcm_reader_scb;
	} else {
		parent_scb = find_next_free_scb(chip,src_scb->sub_list_ptr);
		parent_scb->next_scb_ptr = pcm_channel->pcm_reader_scb;
	}
  
	snd_assert (pcm_channel->pcm_reader_scb->parent_scb_ptr == NULL, ; );
	pcm_channel->pcm_reader_scb->parent_scb_ptr = parent_scb;

	/* update entry in DSP RAM */
	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_cs46xx_poke(chip,
			(parent_scb->address + SCBsubListPtr) << 2,
			(parent_scb->sub_list_ptr->address << 0x10) |
			(parent_scb->next_scb_ptr->address));

	pcm_channel->unlinked = 0;
	spin_unlock_irqrestore(&chip->reg_lock, flags);

 _end:
	up(&ins->scb_mutex);
	up(&ins->pcm_mutex);

	return 0;
}

#define GOF_PER_SEC 200
  
void cs46xx_dsp_set_src_sample_rate(cs46xx_t *chip,dsp_scb_descriptor_t * src, u32 rate)
{
	unsigned long flags;
	unsigned int tmp1, tmp2;
	unsigned int phiIncr;
	unsigned int correctionPerGOF, correctionPerSec;

	snd_printdd( "dsp_spos: setting SRC rate to %u\n",rate);
	/*
	 *  Compute the values used to drive the actual sample rate conversion.
	 *  The following formulas are being computed, using inline assembly
	 *  since we need to use 64 bit arithmetic to compute the values:
	 *
	 *  phiIncr = floor((Fs,in * 2^26) / Fs,out)
	 *  correctionPerGOF = floor((Fs,in * 2^26 - Fs,out * phiIncr) /
	 *                                   GOF_PER_SEC)
	 *  ulCorrectionPerSec = Fs,in * 2^26 - Fs,out * phiIncr -M
	 *                       GOF_PER_SEC * correctionPerGOF
	 *
	 *  i.e.
	 *
	 *  phiIncr:other = dividend:remainder((Fs,in * 2^26) / Fs,out)
	 *  correctionPerGOF:correctionPerSec =
	 *      dividend:remainder(ulOther / GOF_PER_SEC)
	 */
	tmp1 = rate << 16;
	phiIncr = tmp1 / 48000;
	tmp1 -= phiIncr * 48000;
	tmp1 <<= 10;
	phiIncr <<= 10;
	tmp2 = tmp1 / 48000;
	phiIncr += tmp2;
	tmp1 -= tmp2 * 48000;
	correctionPerGOF = tmp1 / GOF_PER_SEC;
	tmp1 -= correctionPerGOF * GOF_PER_SEC;
	correctionPerSec = tmp1;

	/*
	 *  Fill in the SampleRateConverter control block.
	 */
	spin_lock_irqsave(&chip->reg_lock, flags);

	snd_cs46xx_poke(chip, (src->address + SRCCorPerGof) << 2,
	  ((correctionPerSec << 16) & 0xFFFF0000) | (correctionPerGOF & 0xFFFF));

	snd_cs46xx_poke(chip, (src->address + SRCPhiIncr6Int26Frac) << 2, phiIncr);

	spin_unlock_irqrestore(&chip->reg_lock, flags);
}

dsp_scb_descriptor_t *
cs46xx_add_record_source (cs46xx_t *chip,dsp_scb_descriptor_t * source,
			  u16 addr,char * scb_name)
{
  	dsp_spos_instance_t * ins = chip->dsp_spos_instance;
	dsp_scb_descriptor_t * parent;
	dsp_scb_descriptor_t * pcm_input;
	int insert_point;

	snd_assert (ins->record_mixer_scb != NULL,return NULL);

	if (ins->record_mixer_scb->sub_list_ptr != ins->the_null_scb) {
		parent = find_next_free_scb (chip,ins->record_mixer_scb->sub_list_ptr);
		insert_point = SCB_ON_PARENT_NEXT_SCB;
	} else {
		parent = ins->record_mixer_scb;
		insert_point = SCB_ON_PARENT_SUBLIST_SCB;
	}

	pcm_input = cs46xx_dsp_create_pcm_serial_input_scb(chip,scb_name,addr,
							   source, parent,
							   insert_point);

	return pcm_input;
}
