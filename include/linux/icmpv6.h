#ifndef _LINUX_ICMPV6_H
#define _LINUX_ICMPV6_H

#include <asm/byteorder.h>

struct icmpv6hdr {

	__u8		type;
	__u8		code;
	__u16		checksum;


	union {
		struct icmpv6_echo {
			__u16		identifier;
			__u16		sequence;
		} u_echo;
		__u32			pointer;
		__u32			mtu;
		__u32			unused;

                struct icmpv6_nd_advt {
#if defined(__LITTLE_ENDIAN_BITFIELD)
                        __u32		reserved:5,
                        		override:1,
                        		solicited:1,
                        		router:1,
					reserved2:24;
#elif defined(__BIG_ENDIAN_BITFIELD)
                        __u32		router:1,
					solicited:1,
                        		override:1,
                        		reserved:29;
#else
#error	"Please fix <asm/byteorder.h>"
#endif						
                } u_nd_advt;

                struct icmpv6_nd_ra {
			__u8		hop_limit;
#if defined(__LITTLE_ENDIAN_BITFIELD)
			__u8		reserved:6,
					other:1,
					managed:1;

#elif defined(__BIG_ENDIAN_BITFIELD)
			__u8		managed:1,
					other:1,
					reserved:6;
#else
#error	"Please fix <asm/byteorder.h>"
#endif
			__u16		rt_lifetime;
                } u_nd_ra;

	} u;

#define icmp6_identifier	u.u_echo.identifier
#define icmp6_sequence		u.u_echo.sequence
#define icmp6_pointer		u.pointer
#define icmp6_mtu		u.mtu
#define icmp6_unused		u.unused
#define icmp6_router		u.u_nd_advt.router
#define icmp6_solicited		u.u_nd_advt.solicited
#define icmp6_override		u.u_nd_advt.override
#define icmp6_ndiscreserved	u.u_nd_advt.reserved
#define icmp6_hop_limit		u.u_nd_ra.hop_limit
#define icmp6_addrconf_managed	u.u_nd_ra.managed
#define icmp6_addrconf_other	u.u_nd_ra.other
#define icmp6_rt_lifetime	u.u_nd_ra.rt_lifetime
};


#define ICMPV6_DEST_UNREACH		1
#define ICMPV6_PKT_TOOBIG		2
#define ICMPV6_TIME_EXCEEDED		3
#define ICMPV6_PARAMETER_PROB		4

#define ICMPV6_ECHO_REQUEST		128
#define ICMPV6_ECHO_REPLY		129
#define ICMPV6_MEMBERSHIP_QUERY		130
#define ICMPV6_MEMBERSHIP_REPORT       	131
#define ICMPV6_MEMBERSHIP_REDUCTION    	132

/*
 *	Codes for Destination Unreachable
 */
#define ICMPV6_NOROUTE			0
#define ICMPV6_ADM_PROHIBITED		1
#define ICMPV6_NOT_NEIGHBOUR		2
#define ICMPV6_ADDR_UNREACH		3
#define ICMPV6_PORT_UNREACH		4

/*
 *	Codes for Time Exceeded
 */
#define ICMPV6_EXC_HOPLIMIT		0
#define ICMPV6_EXC_FRAGTIME		1

/*
 *	Codes for Parameter Problem
 */
#define ICMPV6_HDR_FIELD		0
#define ICMPV6_UNK_NEXTHDR		1
#define ICMPV6_UNK_OPTION		2

/*
 *	constants for (set|get)sockopt
 */

#define RAW_CHECKSUM			1
#define ICMPV6_FILTER			256

/*
 *	ICMPV6 filter
 */

#define ICMPV6_FILTER_BLOCK		1
#define ICMPV6_FILTER_PASS		2
#define ICMPV6_FILTER_BLOCKOTHERS	3
#define ICMPV6_FILTER_PASSONLY		4

struct icmp6_filter {
	__u32		data[8];
};

#ifdef __KERNEL__

#include <linux/netdevice.h>
#include <linux/skbuff.h>


extern void				icmpv6_send(struct sk_buff *skb,
						    int type, int code,
						    __u32 info, 
						    struct device *dev);

extern void				icmpv6_init(struct proto_ops *ops);
extern int				icmpv6_err_convert(int type, int code,
							   int *err);
#endif

#endif
