/*
 *	Linux INET6 implementation 
 *
 *	Authors:
 *	Pedro Roque		<roque@di.fc.ul.pt>	
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

#ifndef _IP6_FIB_H
#define _IP6_FIB_H

#ifdef __KERNEL__

#include <linux/ipv6_route.h>

#include <net/dst.h>
#include <net/flow.h>
#include <linux/rtnetlink.h>

struct rt6_info;

struct fib6_node {
	struct fib6_node	*parent;
	struct fib6_node	*left;
	struct fib6_node	*right;

	struct fib6_node	*subtree;

	struct rt6_info		*leaf;

	__u16			fn_bit;		/* bit key */
	__u16			fn_flags;
	__u32			fn_sernum;
};


/*
 *	routing information
 *
 */

struct rt6key {
	struct in6_addr	addr;
	int		plen;
};

struct rt6_info {
	union {
		struct dst_entry	dst;
		struct rt6_info		*next;
	} u;

#define rt6i_dev			u.dst.dev
#define rt6i_nexthop			u.dst.neighbour
#define rt6i_use			u.dst.use
#define rt6i_ref			u.dst.refcnt

#define rt6i_tstamp			u.dst.lastuse

	struct fib6_node		*rt6i_node;

	struct in6_addr			rt6i_gateway;
	
	int				rt6i_keylen;

	u32				rt6i_flags;
	u32				rt6i_metric;
	unsigned long			rt6i_expires;

	union {
		struct flow_rule	*rt6iu_flowr;
		struct flow_filter	*rt6iu_filter;
	} flow_u;

#define rt6i_flowr			flow_u.rt6iu_flowr
#define rt6i_filter			flow_u.rt6iu_filter

	struct rt6key			rt6i_dst;
	struct rt6key			rt6i_src;
};


struct rt6_statistics {
	__u32		fib_nodes;
	__u32		fib_route_nodes;
	__u32		fib_rt_alloc;		/* permanet routes	*/
	__u32		fib_rt_entries;		/* rt entries in table	*/
	__u32		fib_rt_cache;		/* cache routes		*/
};

#define RTN_TL_ROOT	0x0001
#define RTN_ROOT	0x0002		/* tree root node		*/
#define RTN_RTINFO	0x0004		/* node with valid routing info	*/

#define RTN_TAG		0x0100

/*
 *	priority levels (or metrics)
 *
 */

#define RTPRI_FIREWALL	8		/* Firewall control information	*/
#define RTPRI_FLOW	16		/* Flow based forwarding rules	*/
#define RTPRI_KERN_CTL	32		/* Kernel control routes	*/

#define RTPRI_USER_MIN	256		/* Mimimum user priority	*/
#define RTPRI_USER_MAX	1024		/* Maximum user priority	*/

#define RTPRI_KERN_DFLT	4096		/* Kernel default routes	*/

#define	MAX_FLOW_BACKTRACE	32


typedef void			(*f_pnode)(struct fib6_node *fn, void *);

extern struct fib6_node		ip6_routing_table;

/*
 *	exported functions
 */

extern struct fib6_node		*fib6_lookup(struct fib6_node *root,
					     struct in6_addr *daddr,
					     struct in6_addr *saddr);

#define RT6_FILTER_RTNODES	1

extern void			fib6_walk_tree(struct fib6_node *root,
					       f_pnode func, void *arg,
					       int filter);

extern int			fib6_add(struct fib6_node *root,
					 struct rt6_info *rt);

extern int			fib6_del(struct rt6_info *rt);

extern void			inet6_rt_notify(int event, struct rt6_info *rt);

#endif
#endif
