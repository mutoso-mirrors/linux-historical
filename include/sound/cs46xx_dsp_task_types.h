/*
 *  The driver for the Cirrus Logic's Sound Fusion CS46XX based soundcards
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
 *
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
 *
 * NOTE: comments are copy/paste from cwcemb80.lst 
 * provided by Tom Woller at Cirrus (my only
 * documentation about the SP OS running inside
 * the DSP) 
 */

#ifndef __CS46XX_DSP_TASK_TYPES_H__
#define __CS46XX_DSP_TASK_TYPES_H__

/*********************************************************************************************
Example hierarchy of stream control blocks in the SP

hfgTree
Ptr____Call (c)
       \
 -------+------         -------------      -------------      -------------      -----
| SBlaster IF  |______\| Foreground  |___\| Middlegr'nd |___\| Background  |___\| Nul |
|              |Goto  /| tree header |g  /| tree header |g  /| tree header |g  /| SCB |r
 -------------- (g)     -------------      -------------      -------------      -----
       |c                     |c                 |c                 |c
       |                      |                  |                  |
      \/                  -------------      -------------      -------------   
                       | Foreground  |_\  | Middlegr'nd |_\  | Background  |_\
                       |     tree    |g/  |    tree     |g/  |     tree    |g/
                        -------------      -------------      -------------   
                              |c                 |c                 |c
                              |                  |                  | 
                             \/                 \/                 \/ 

*********************************************************************************************/

#define		HFG_FIRST_EXECUTE_MODE			0x0001
#define		HFG_FIRST_EXECUTE_MODE_BIT		0
#define		HFG_CONTEXT_SWITCH_MODE			0x0002
#define		HFG_CONTEXT_SWITCH_MODE_BIT		1

#define MAX_FG_STACK_SIZE 	32				// THESE NEED TO BE COMPUTED PROPERLY
#define MAX_MG_STACK_SIZE 	16
#define MAX_BG_STACK_SIZE 	9
#define MAX_HFG_STACK_SIZE	4

#define SLEEP_ACTIVE_INCREMENT		0		/* Enable task tree thread to go to sleep
											   This should only ever be used on the Background thread */
#define STANDARD_ACTIVE_INCREMENT	1		/* Task tree thread normal operation */
#define SUSPEND_ACTIVE_INCREMENT	2		/* Cause execution to suspend in the task tree thread
                                               This should only ever be used on the Background thread */

#define HOSTFLAGS_DISABLE_BG_SLEEP  0       /* Host-controlled flag that determines whether we go to sleep
                                               at the end of BG */

/* Minimal context save area for Hyper Forground */
typedef struct _hf_save_area_t {
	u32	r10_save;
	u32	r54_save;
	u32	r98_save;

	u16 status_save;
	u16 ind_save;

	u16 rci1_save;
	u16 rci0_save;

	u32	r32_save;
	u32	r76_save;
	u32	rsd2_save;

	u16   rsi2_save;	  /* See TaskTreeParameterBlock for 
				     remainder of registers  */
	u16	rsa2Save;
	/* saved as part of HFG context  */
} hf_save_area_t;


/* Task link data structure */
typedef struct _tree_link_t {
	/* Pointer to sibling task control block */
	u16 next_scb;
	/* Pointer to child task control block */
	u16 sub_ptr;
  
	/* Pointer to code entry point */
	u16 entry_point; 
	/* Pointer to local data */
	u16 this_spb;
} tree_link_t;


typedef struct _task_tree_data_t {
	/* Initial tock count; controls task tree execution rate */
	u16 tock_count_limit;
	/* Tock down counter */
	u16 tock_count;
  
	/* Add to ActiveCount when TockCountLimit reached: 
	   Subtract on task tree termination */
	u16 active_tncrement;		
	/* Number of pending activations for task tree */
	u16 active_count;

	/* BitNumber to enable modification of correct bit in ActiveTaskFlags */
	u16 active_bit;	    
	/* Pointer to OS location for indicating current activity on task level */
	u16 active_task_flags_ptr;

	/* Data structure for controlling movement of memory blocks:- 
	   currently unused */
	u16 mem_upd_ptr;
	/* Data structure for controlling synchronous link update */
	u16 link_upd_ptr;
  
	/* Save area for remainder of full context. */
	u16 save_area;  
	/* Address of start of local stack for data storage */
	u16 data_stack_base_ptr;

} task_tree_data_t;



typedef struct _interval_timer_data_t
{
	/* These data items have the same relative locations to those */
	u16  interval_timer_period;
	u16  itd_unused;

	/* used for this data in the SPOS control block for SPOS 1.0 */
	u16  num_FG_ticks_this_interval;        
	u16  num_intervals;
} interval_timer_data_t;    


/* This structure contains extra storage for the task tree
   Currently, this additional data is related only to a full context save */
typedef struct _task_tree_context_block_t {
	/* Up to 10 values are saved onto the stack.  8 for the task tree, 1 for
	   The access to the context switch (call or interrupt), and 1 spare that
	   users should never use.  This last may be required by the system */
	u16       stack1;		
	u16		stack0;
	u16       stack3;		
	u16		stack2;
	u16       stack5;		
	u16		stack4;
	u16       stack7;
	u16		stack6;
	u16       stack9;
	u16		stack8;

	u32		saverfe;					

	/* Value may be overwriten by stack save algorithm.
	   Retain the size of the stack data saved here if used */
	u16       reserved1;	
	u16		stack_size;
	u32		saverba;		  /* (HFG) */
	u32		saverdc;
	u32		savers_config_23; /* (HFG) */
	u32		savers_DMA23;	  /* (HFG) */
	u32		saversa0;
	u32		saversi0;
	u32		saversa1;
	u32		saversi1;
	u32		saversa3;
	u32		saversd0;
	u32		saversd1;
	u32		saversd3;
	u32		savers_config01;
	u32		savers_DMA01;
	u32		saveacc0hl;
	u32		saveacc1hl;
	u32		saveacc0xacc1x;
	u32		saveacc2hl;
	u32		saveacc3hl;
	u32		saveacc2xacc3x;
	u32		saveaux0hl;
	u32		saveaux1hl;
	u32		saveaux0xaux1x;
	u32		saveaux2hl;
	u32		saveaux3hl;
	u32		saveaux2xaux3x;
	u32		savershouthl;
	u32		savershoutxmacmode;
} task_tree_context_block_t;						  
                

typedef struct _task_tree_control_block_t	{
	hf_save_area_t		 	context;
	tree_link_t				links;
	task_tree_data_t			data;
	task_tree_context_block_t	context_blk;
	interval_timer_data_t		int_timer;
} task_tree_control_block_t;


#endif /* __DSP_TASK_TYPES_H__ */