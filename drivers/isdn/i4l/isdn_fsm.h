/* $Id: fsm.h,v 1.3.2.2 2001/09/23 22:24:47 kai Exp $
 *
 * Finite state machine
 *
 * Author       Karsten Keil
 * Copyright    by Karsten Keil      <keil@isdn4linux.de>
 *              by Kai Germaschewski <kai.germaschewski@gmx.de>
 * 
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __FSM_H__
#define __FSM_H__

#include <linux/timer.h>

struct fsm_inst;

typedef void (*fsm_fn)(struct fsm_inst *, int, void *);

struct fsm {
	fsm_fn *jumpmatrix;
	int st_cnt, ev_cnt, fn_cnt;
	char **st_str, **ev_str;
	struct fsm_node *fn_tbl;
};

struct fsm_inst {
	struct fsm *fsm;
	int state;
	int debug;
	void *userdata;
	int userint;
	void (*printdebug) (struct fsm_inst *, char *, ...);
};

struct fsm_node {
	int st, ev;
	void (*routine) (struct fsm_inst *, int, void *);
};

struct fsm_timer {
	struct fsm_inst *fi;
	struct timer_list tl;
	int ev;
	void *arg;
};

int  fsm_new(struct fsm *fsm);
void fsm_free(struct fsm *fsm);
int  fsm_event(struct fsm_inst *fi, int event, void *arg);
void fsm_change_state(struct fsm_inst *fi, int newstate);
void fsm_init_timer(struct fsm_inst *fi, struct fsm_timer *ft);
int  fsm_add_timer(struct fsm_timer *ft, int timeout, int event);
void fsm_mod_timer(struct fsm_timer *ft, int timeout, int event);
void fsm_del_timer(struct fsm_timer *ft);

#endif
