/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the Interfaces handler.
 *
 * Version:	@(#)dev.h	1.0.10	08/12/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Corey Minyard <wf-rch!minyard@relay.EU.net>
 *		Donald J. Becker, <becker@cesdis.gsfc.nasa.gov>
 *		Alan Cox, <Alan.Cox@linux.org>
 *		Bjorn Ekwall. <bj0rn@blox.se>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *		Moved to /usr/include/linux for NET3
 */
#ifndef _LINUX_NETDEVICE_H
#define _LINUX_NETDEVICE_H

#include <linux/config.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include <asm/atomic.h>

#ifdef __KERNEL__
#ifdef CONFIG_NET_PROFILE
#include <net/profile.h>
#endif

#define NET_XMIT_SUCCESS	0
#define NET_XMIT_DROP		1	/* skb dropped			*/
#define NET_XMIT_CN		2	/* congestion notification	*/
#define NET_XMIT_POLICED	3	/* skb is shot by police	*/

#define net_xmit_errno(e)	((e) != NET_XMIT_CN ? -ENOBUFS : 0)

#endif

#define MAX_ADDR_LEN	7		/* Largest hardware address length */

/*
 *	Compute the worst case header length according to the protocols
 *	used.
 */
 
#if !defined(CONFIG_AX25) && !defined(CONFIG_AX25_MODULE) && !defined(CONFIG_TR)
#define LL_MAX_HEADER	32
#else
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
#define LL_MAX_HEADER	96
#else
#define LL_MAX_HEADER	48
#endif
#endif

#if !defined(CONFIG_NET_IPIP) && \
    !defined(CONFIG_IPV6) && !defined(CONFIG_IPV6_MODULE)
#define MAX_HEADER LL_MAX_HEADER
#else
#define MAX_HEADER (LL_MAX_HEADER + 48)
#endif

/*
 *	Network device statistics. Akin to the 2.0 ether stats but
 *	with byte counters.
 */
 
struct net_device_stats
{
	unsigned long	rx_packets;		/* total packets received	*/
	unsigned long	tx_packets;		/* total packets transmitted	*/
	unsigned long	rx_bytes;		/* total bytes received 	*/
	unsigned long	tx_bytes;		/* total bytes transmitted	*/
	unsigned long	rx_errors;		/* bad packets received		*/
	unsigned long	tx_errors;		/* packet transmit problems	*/
	unsigned long	rx_dropped;		/* no space in linux buffers	*/
	unsigned long	tx_dropped;		/* no space available in linux	*/
	unsigned long	multicast;		/* multicast packets received	*/
	unsigned long	collisions;

	/* detailed rx_errors: */
	unsigned long	rx_length_errors;
	unsigned long	rx_over_errors;		/* receiver ring buff overflow	*/
	unsigned long	rx_crc_errors;		/* recved pkt with crc error	*/
	unsigned long	rx_frame_errors;	/* recv'd frame alignment error */
	unsigned long	rx_fifo_errors;		/* recv'r fifo overrun		*/
	unsigned long	rx_missed_errors;	/* receiver missed packet	*/

	/* detailed tx_errors */
	unsigned long	tx_aborted_errors;
	unsigned long	tx_carrier_errors;
	unsigned long	tx_fifo_errors;
	unsigned long	tx_heartbeat_errors;
	unsigned long	tx_window_errors;
	
	/* for cslip etc */
	unsigned long	rx_compressed;
	unsigned long	tx_compressed;
};

#ifdef CONFIG_NET_FASTROUTE
struct net_fastroute_stats
{
	int		hits;
	int		succeed;
	int		deferred;
	int		latency_reduction;
};
#endif

/* Media selection options. */
enum {
        IF_PORT_UNKNOWN = 0,
        IF_PORT_10BASE2,
        IF_PORT_10BASET,
        IF_PORT_AUI,
        IF_PORT_100BASET,
        IF_PORT_100BASETX,
        IF_PORT_100BASEFX
};

#ifdef __KERNEL__

extern const char *if_port_text[];

#include <linux/skbuff.h>

struct neighbour;
struct neigh_parms;
struct sk_buff;

/*
 *	We tag multicasts with these structures.
 */
 
struct dev_mc_list
{	
	struct dev_mc_list	*next;
	__u8			dmi_addr[MAX_ADDR_LEN];
	unsigned char		dmi_addrlen;
	int			dmi_users;
	int			dmi_gusers;
};

struct hh_cache
{
	struct hh_cache *hh_next;	/* Next entry			     */
	atomic_t	hh_refcnt;	/* number of users                   */
	unsigned short  hh_type;	/* protocol identifier, f.e ETH_P_IP */
	int		hh_len;		/* length of header */
	int		(*hh_output)(struct sk_buff *skb);
	rwlock_t	hh_lock;
	/* cached hardware header; allow for machine alignment needs.        */
	unsigned long	hh_data[16/sizeof(unsigned long)];
};


/*
 *	The DEVICE structure.
 *	Actually, this whole structure is a big mistake.  It mixes I/O
 *	data with strictly "high-level" data, and it has to know about
 *	almost every data structure used in the INET module.
 *
 *	FIXME: cleanup struct net_device such that network protocol info
 *	moves out.
 */

struct net_device
{

	/*
	 * This is the first field of the "visible" part of this structure
	 * (i.e. as seen by users in the "Space.c" file).  It is the name
	 * the interface.
	 */
	char			*name;

	/*
	 *	I/O specific fields
	 *	FIXME: Merge these and struct ifmap into one
	 */
	unsigned long		rmem_end;	/* shmem "recv" end	*/
	unsigned long		rmem_start;	/* shmem "recv" start	*/
	unsigned long		mem_end;	/* shared mem end	*/
	unsigned long		mem_start;	/* shared mem start	*/
	unsigned long		base_addr;	/* device I/O address	*/
	unsigned int		irq;		/* device IRQ number	*/
	
	/* Low-level status flags. */
	volatile unsigned char	start;		/* start an operation	*/
	/*
	 * These two are just single-bit flags, but due to atomicity
	 * reasons they have to be inside a "unsigned long". However,
	 * they should be inside the SAME unsigned long instead of
	 * this wasteful use of memory..
	 */
	unsigned long		interrupt;	/* bitops.. */
	unsigned long		tbusy;		/* transmitter busy */
	
	struct net_device		*next;
	
	/* The device initialization function. Called only once. */
	int			(*init)(struct net_device *dev);

	/* Interface index. Unique device identifier	*/
	int			ifindex;
	int			iflink;

	/*
	 *	Some hardware also needs these fields, but they are not
	 *	part of the usual set specified in Space.c.
	 */

	unsigned char		if_port;	/* Selectable AUI, TP,..*/
	unsigned char		dma;		/* DMA channel		*/

	struct net_device_stats* (*get_stats)(struct net_device *dev);
	struct iw_statistics*	(*get_wireless_stats)(struct net_device *dev);

	/*
	 * This marks the end of the "visible" part of the structure. All
	 * fields hereafter are internal to the system, and may change at
	 * will (read: may be cleaned up at will).
	 */

	/* These may be needed for future network-power-down code. */
	unsigned long		trans_start;	/* Time (in jiffies) of last Tx	*/
	unsigned long		last_rx;	/* Time of last Rx	*/
	
	unsigned short		flags;	/* interface flags (a la BSD)	*/
	unsigned short		gflags;
	unsigned		mtu;	/* interface MTU value		*/
	unsigned short		type;	/* interface hardware type	*/
	unsigned short		hard_header_len;	/* hardware hdr length	*/
	void			*priv;	/* pointer to private data	*/
	
	/* Interface address info. */
	unsigned char		broadcast[MAX_ADDR_LEN];	/* hw bcast add	*/
	unsigned char		pad;		/* make dev_addr aligned to 8 bytes */
	unsigned char		dev_addr[MAX_ADDR_LEN];	/* hw address	*/
	unsigned char		addr_len;	/* hardware address length	*/

	struct dev_mc_list	*mc_list;	/* Multicast mac addresses	*/
	int			mc_count;	/* Number of installed mcasts	*/
	int			promiscuity;
	int			allmulti;
    
	/* For load balancing driver pair support */
  
	unsigned long		pkt_queue;	/* Packets queued	*/
	struct net_device		*slave;		/* Slave device		*/

	/* Protocol specific pointers */
	
	void 			*atalk_ptr;	/* AppleTalk link 	*/
	void			*ip_ptr;	/* IPv4 specific data	*/  
	void                    *dn_ptr;        /* DECnet specific data */
	void                    *ip6_ptr;       /* IPv6 specific data */

	struct Qdisc		*qdisc;
	struct Qdisc		*qdisc_sleeping;
	struct Qdisc		*qdisc_list;
	unsigned long		tx_queue_len;	/* Max frames per queue allowed */

	/* hard_start_xmit synchronizer */
	spinlock_t		xmit_lock;
	/* cpu id of processor entered to hard_start_xmit or -1,
	   if nobody entered there.
	 */
	int			xmit_lock_owner;
	/* device queue lock */
	spinlock_t		queue_lock;
	/* Number of references to this device */
	atomic_t		refcnt;
	/* The flag marking that device is unregistered, but held by an user */
	int			deadbeaf;
	/* New style devices allow asynchronous destruction;
	   netdevice_unregister for old style devices blocks until
	   the last user will dereference this device.
	 */
	int			new_style;
	/* Called after device is detached from network. */
	void			(*uninit)(struct net_device *dev);
	/* Called after last user reference disappears. */
	void			(*destructor)(struct net_device *dev);

	/* Pointers to interface service routines.	*/
	int			(*open)(struct net_device *dev);
	int			(*stop)(struct net_device *dev);
	int			(*hard_start_xmit) (struct sk_buff *skb,
						    struct net_device *dev);
	int			(*hard_header) (struct sk_buff *skb,
						struct net_device *dev,
						unsigned short type,
						void *daddr,
						void *saddr,
						unsigned len);
	int			(*rebuild_header)(struct sk_buff *skb);
#define HAVE_MULTICAST			 
	void			(*set_multicast_list)(struct net_device *dev);
#define HAVE_SET_MAC_ADDR  		 
	int			(*set_mac_address)(struct net_device *dev,
						   void *addr);
#define HAVE_PRIVATE_IOCTL
	int			(*do_ioctl)(struct net_device *dev,
					    struct ifreq *ifr, int cmd);
#define HAVE_SET_CONFIG
	int			(*set_config)(struct net_device *dev,
					      struct ifmap *map);
#define HAVE_HEADER_CACHE
	int			(*hard_header_cache)(struct neighbour *neigh,
						     struct hh_cache *hh);
	void			(*header_cache_update)(struct hh_cache *hh,
						       struct net_device *dev,
						       unsigned char *  haddr);
#define HAVE_CHANGE_MTU
	int			(*change_mtu)(struct net_device *dev, int new_mtu);

	int			(*hard_header_parse)(struct sk_buff *skb,
						     unsigned char *haddr);
	int			(*neigh_setup)(struct net_device *dev, struct neigh_parms *);
	int			(*accept_fastpath)(struct net_device *, struct dst_entry*);

#ifdef CONFIG_NET_FASTROUTE
	unsigned long		tx_semaphore;
#define NETDEV_FASTROUTE_HMASK 0xF
	/* Semi-private data. Keep it at the end of device struct. */
	rwlock_t		fastpath_lock;
	struct dst_entry	*fastpath[NETDEV_FASTROUTE_HMASK+1];
#endif
};


struct packet_type 
{
	unsigned short		type;	/* This is really htons(ether_type).	*/
	struct net_device		*dev;	/* NULL is wildcarded here		*/
	int			(*func) (struct sk_buff *, struct net_device *,
					 struct packet_type *);
	void			*data;	/* Private to the packet type		*/
	struct packet_type	*next;
};


#include <linux/interrupt.h>
#include <linux/notifier.h>

extern struct net_device		loopback_dev;		/* The loopback */
extern struct net_device		*dev_base;		/* All devices */
extern rwlock_t			dev_base_lock;		/* Device list lock */
extern int			netdev_dropping;
extern int			net_cpu_congestion;

extern struct net_device    *dev_getbyhwaddr(unsigned short type, char *hwaddr);
extern void		dev_add_pack(struct packet_type *pt);
extern void		dev_remove_pack(struct packet_type *pt);
extern int		dev_get(const char *name);
extern struct net_device	*dev_get_by_name(const char *name);
extern struct net_device	*__dev_get_by_name(const char *name);
extern struct net_device	*dev_alloc(const char *name, int *err);
extern int		dev_alloc_name(struct net_device *dev, const char *name);
extern int		dev_open(struct net_device *dev);
extern int		dev_close(struct net_device *dev);
extern int		dev_queue_xmit(struct sk_buff *skb);
extern void		dev_loopback_xmit(struct sk_buff *skb);
extern int		register_netdevice(struct net_device *dev);
extern int		unregister_netdevice(struct net_device *dev);
extern int 		register_netdevice_notifier(struct notifier_block *nb);
extern int		unregister_netdevice_notifier(struct notifier_block *nb);
extern int		dev_new_index(void);
extern struct net_device	*dev_get_by_index(int ifindex);
extern struct net_device	*__dev_get_by_index(int ifindex);
extern int		dev_restart(struct net_device *dev);

typedef int gifconf_func_t(struct net_device * dev, char * bufptr, int len);
extern int		register_gifconf(unsigned int family, gifconf_func_t * gifconf);
extern __inline__ int unregister_gifconf(unsigned int family)
{
	return register_gifconf(family, 0);
}

#define HAVE_NETIF_RX 1
extern void		netif_rx(struct sk_buff *skb);
extern void		net_bh(void);
extern int		dev_get_info(char *buffer, char **start, off_t offset, int length, int dummy);
extern int		dev_ioctl(unsigned int cmd, void *);
extern int		dev_change_flags(struct net_device *, unsigned);
extern void		dev_queue_xmit_nit(struct sk_buff *skb, struct net_device *dev);

extern void		dev_init(void);

extern int		netdev_nit;

extern __inline__ void dev_init_buffers(struct net_device *dev)
{
	/* DO NOTHING */
}

extern int netdev_finish_unregister(struct net_device *dev);

extern __inline__ void dev_put(struct net_device *dev)
{
	if (atomic_dec_and_test(&dev->refcnt))
		netdev_finish_unregister(dev);
}

#define __dev_put(dev) atomic_dec(&(dev)->refcnt)
#define dev_hold(dev) atomic_inc(&(dev)->refcnt)


/* These functions live elsewhere (drivers/net/net_init.c, but related) */

extern void		ether_setup(struct net_device *dev);
extern void		fddi_setup(struct net_device *dev);
extern void		tr_setup(struct net_device *dev);
extern void		fc_setup(struct net_device *dev);
extern void		tr_freedev(struct net_device *dev);
extern void		fc_freedev(struct net_device *dev);
extern int		ether_config(struct net_device *dev, struct ifmap *map);
/* Support for loadable net-drivers */
extern int		register_netdev(struct net_device *dev);
extern void		unregister_netdev(struct net_device *dev);
extern int		register_trdev(struct net_device *dev);
extern void		unregister_trdev(struct net_device *dev);
extern int		register_fcdev(struct net_device *dev);
extern void		unregister_fcdev(struct net_device *dev);
/* Functions used for multicast support */
extern void		dev_mc_upload(struct net_device *dev);
extern int 		dev_mc_delete(struct net_device *dev, void *addr, int alen, int all);
extern int		dev_mc_add(struct net_device *dev, void *addr, int alen, int newonly);
extern void		dev_mc_discard(struct net_device *dev);
extern void		dev_set_promiscuity(struct net_device *dev, int inc);
extern void		dev_set_allmulti(struct net_device *dev, int inc);
extern void		netdev_state_change(struct net_device *dev);
/* Load a device via the kmod */
extern void		dev_load(const char *name);
extern void		dev_mcast_init(void);
extern int		netdev_register_fc(struct net_device *dev, void (*stimul)(struct net_device *dev));
extern void		netdev_unregister_fc(int bit);
extern int		netdev_dropping;
extern int		netdev_max_backlog;
extern atomic_t		netdev_rx_dropped;
extern unsigned long	netdev_fc_xoff;
#ifdef CONFIG_NET_FASTROUTE
extern int		netdev_fastroute;
extern int		netdev_fastroute_obstacles;
extern void		dev_clear_fastroute(struct net_device *dev);
extern struct net_fastroute_stats dev_fastroute_stat;
#endif


#endif /* __KERNEL__ */

#endif	/* _LINUX_DEV_H */
