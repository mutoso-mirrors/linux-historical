/*********************************************************************
 *                
 * Filename:      irsyms.c
 * Version:       0.9
 * Description:   IrDA module symbols
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Mon Dec 15 13:55:39 1997
 * Modified at:   Wed Jan  5 15:12:41 2000
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1997, 1999-2000 Dag Brattli, All Rights Reserved.
 *     Copyright (c) 2000-2001 Jean Tourrilhes <jt@hpl.hp.com>
 *      
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *  
 *     Neither Dag Brattli nor University of Troms� admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *     
 ********************************************************************/

#include <linux/config.h>
#include <linux/module.h>

#include <linux/init.h>
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/smp_lock.h>
#include <linux/if_arp.h>		/* ARPHRD_IRDA */

#include <net/irda/irda.h>
#include <net/irda/irlap.h>
#include <net/irda/irlmp.h>
#include <net/irda/iriap.h>
#include <net/irda/irias_object.h>
#include <net/irda/irttp.h>
#include <net/irda/irda_device.h>
#include <net/irda/wrapper.h>
#include <net/irda/timer.h>
#include <net/irda/parameters.h>
#include <net/irda/crc.h>

extern struct proc_dir_entry *proc_irda;

extern void irda_proc_register(void);
extern void irda_proc_unregister(void);
extern int  irda_sysctl_register(void);
extern void irda_sysctl_unregister(void);

extern int irda_proto_init(void);
extern void irda_proto_cleanup(void);

extern int irda_device_init(void);
extern int irlan_init(void);
extern int irlan_client_init(void);
extern int irlan_server_init(void);
extern int ircomm_init(void);
extern int ircomm_tty_init(void);
extern int irlpt_client_init(void);
extern int irlpt_server_init(void);

extern int  irsock_init(void);
extern void irsock_cleanup(void);
extern int  irlap_driver_rcv(struct sk_buff *, struct net_device *, 
			     struct packet_type *);

/* Main IrDA module */
#ifdef CONFIG_IRDA_DEBUG
EXPORT_SYMBOL(irda_debug);
#endif
EXPORT_SYMBOL(irda_notify_init);
EXPORT_SYMBOL(irda_param_insert);
EXPORT_SYMBOL(irda_param_extract);
EXPORT_SYMBOL(irda_param_extract_all);
EXPORT_SYMBOL(irda_param_pack);
EXPORT_SYMBOL(irda_param_unpack);

/* IrLAP */
EXPORT_SYMBOL(irda_init_max_qos_capabilies);
EXPORT_SYMBOL(irda_qos_bits_to_value);
EXPORT_SYMBOL(irda_device_setup);
EXPORT_SYMBOL(alloc_irdadev);
EXPORT_SYMBOL(irda_device_set_media_busy);
EXPORT_SYMBOL(irda_device_txqueue_empty);

EXPORT_SYMBOL(irda_device_dongle_init);
EXPORT_SYMBOL(irda_device_dongle_cleanup);
EXPORT_SYMBOL(irda_device_register_dongle);
EXPORT_SYMBOL(irda_device_unregister_dongle);
EXPORT_SYMBOL(irda_task_execute);
EXPORT_SYMBOL(irda_task_next_state);
EXPORT_SYMBOL(irda_task_delete);


#ifdef CONFIG_IRDA_DEBUG
__u32 irda_debug = IRDA_DEBUG_LEVEL;
#endif

/* Packet type handler.
 * Tell the kernel how IrDA packets should be handled.
 */
static struct packet_type irda_packet_type = {
	.type	= __constant_htons(ETH_P_IRDA),
	.func	= irlap_driver_rcv,	/* Packet type handler irlap_frame.c */
};

/*
 * Function irda_notify_init (notify)
 *
 *    Used for initializing the notify structure
 *
 */
void irda_notify_init(notify_t *notify)
{
	notify->data_indication = NULL;
	notify->udata_indication = NULL;
	notify->connect_confirm = NULL;
	notify->connect_indication = NULL;
	notify->disconnect_indication = NULL;
	notify->flow_indication = NULL;
	notify->status_indication = NULL;
	notify->instance = NULL;
	strlcpy(notify->name, "Unknown", sizeof(notify->name));
}

/*
 * Function irda_init (void)
 *
 *  Protocol stack initialisation entry point.
 *  Initialise the various components of the IrDA stack
 */
int __init irda_init(void)
{
	IRDA_DEBUG(0, "%s()\n", __FUNCTION__);

	/* Lower layer of the stack */
 	irlmp_init();
	irlap_init();
	
	/* Higher layers of the stack */
	iriap_init();
 	irttp_init();
	irsock_init();
	
	/* Add IrDA packet type (Start receiving packets) */
        dev_add_pack(&irda_packet_type);

	/* External APIs */
#ifdef CONFIG_PROC_FS
	irda_proc_register();
#endif
#ifdef CONFIG_SYSCTL
	irda_sysctl_register();
#endif

	/* Driver/dongle support */
 	irda_device_init();

	return 0;
}

/*
 * Function irda_cleanup (void)
 *
 *  Protocol stack cleanup/removal entry point.
 *  Cleanup the various components of the IrDA stack
 */
void __exit irda_cleanup(void)
{
	/* Remove External APIs */
#ifdef CONFIG_SYSCTL
	irda_sysctl_unregister();
#endif	
#ifdef CONFIG_PROC_FS
	irda_proc_unregister();
#endif

	/* Remove IrDA packet type (stop receiving packets) */
        dev_remove_pack(&irda_packet_type);
	
	/* Remove higher layers */
	irsock_cleanup();
	irttp_cleanup();
	iriap_cleanup();

	/* Remove lower layers */
	irda_device_cleanup();
	irlap_cleanup(); /* Must be done before irlmp_cleanup()! DB */

	/* Remove middle layer */
	irlmp_cleanup();
}

/*
 * The IrDA stack must be initialised *before* drivers get initialised,
 * and *before* higher protocols (IrLAN/IrCOMM/IrNET) get initialised,
 * otherwise bad things will happen (hashbins will be NULL for example).
 * Those modules are at module_init()/device_initcall() level.
 *
 * On the other hand, it needs to be initialised *after* the basic
 * networking, the /proc/net filesystem and sysctl module. Those are
 * currently initialised in .../init/main.c (before initcalls).
 * Also, IrDA drivers needs to be initialised *after* the random number
 * generator (main stack and higher layer init don't need it anymore).
 *
 * Jean II
 */
subsys_initcall(irda_init);
module_exit(irda_cleanup);
 
MODULE_AUTHOR("Dag Brattli <dagb@cs.uit.no> & Jean Tourrilhes <jt@hpl.hp.com>");
MODULE_DESCRIPTION("The Linux IrDA Protocol Stack"); 
MODULE_LICENSE("GPL");
#ifdef CONFIG_IRDA_DEBUG
MODULE_PARM(irda_debug, "1l");
#endif
MODULE_ALIAS_NETPROTO(PF_IRDA);
