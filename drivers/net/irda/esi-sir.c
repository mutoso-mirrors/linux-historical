/*********************************************************************
 *                
 * Filename:      esi.c
 * Version:       1.6
 * Description:   Driver for the Extended Systems JetEye PC dongle
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sat Feb 21 18:54:38 1998
 * Modified at:   Sun Oct 27 22:01:04 2002
 * Modified by:   Martin Diehl <mad@mdiehl.de>
 * 
 *     Copyright (c) 1999 Dag Brattli, <dagb@cs.uit.no>,
 *     Copyright (c) 1998 Thomas Davis, <ratbert@radiks.net>,
 *     Copyright (c) 2002 Martin Diehl, <mad@mdiehl.de>,
 *     All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *     
 ********************************************************************/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <net/irda/irda.h>

#include "sir-dev.h"

static int esi_open(struct sir_dev *);
static int esi_close(struct sir_dev *);
static int esi_change_speed(struct sir_dev *, unsigned);
static int esi_reset(struct sir_dev *);

static struct dongle_driver esi = {
	.owner		= THIS_MODULE,
	.driver_name	= "JetEye PC ESI-9680 PC",
	.type		= IRDA_ESI_DONGLE,
	.open		= esi_open,
	.close		= esi_close,
	.reset		= esi_reset,
	.set_speed	= esi_change_speed,
};

static int __init esi_sir_init(void)
{
	return irda_register_dongle(&esi);
}

static void __exit esi_sir_cleanup(void)
{
	irda_unregister_dongle(&esi);
}

static int esi_open(struct sir_dev *dev)
{
	struct qos_info *qos = &dev->qos;

	qos->baud_rate.bits &= IR_9600|IR_19200|IR_115200;
	qos->min_turn_time.bits = 0x01; /* Needs at least 10 ms */
	irda_qos_bits_to_value(qos);

	/* shouldn't we do set_dtr_rts(FALSE, TRUE) here (power up at 9600)? */

	return 0;
}

static int esi_close(struct sir_dev *dev)
{
	/* Power off dongle */
	dev->set_dtr_rts(dev, FALSE, FALSE);

	return 0;
}

/*
 * Function esi_change_speed (task)
 *
 *    Set the speed for the Extended Systems JetEye PC ESI-9680 type dongle
 *
 */
static int esi_change_speed(struct sir_dev *dev, unsigned speed)
{
	int dtr, rts;
	
	switch (speed) {
	case 19200:
		dtr = TRUE;
		rts = FALSE;
		break;
	case 115200:
		dtr = rts = TRUE;
		break;
	default:
		speed = 9600;
		/* fall through */
	case 9600:
		dtr = FALSE;
		rts = TRUE;
		break;
	}

	/* Change speed of dongle */
	dev->set_dtr_rts(dev, dtr, rts);
	dev->speed = speed;

	/* do we need some delay for power stabilization? */

	return 0;
}

/*
 * Function esi_reset (task)
 *
 *    Reset dongle;
 *
 */
static int esi_reset(struct sir_dev *dev)
{
	dev->set_dtr_rts(dev, FALSE, FALSE);

	/* Hm, probably repower to 9600 and some delays? */

	return 0;
}

MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no>");
MODULE_DESCRIPTION("Extended Systems JetEye PC dongle driver");
MODULE_LICENSE("GPL");

module_init(esi_sir_init);
module_exit(esi_sir_cleanup);

