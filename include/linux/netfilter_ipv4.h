#ifndef __LINUX_IP_NETFILTER_H
#define __LINUX_IP_NETFILTER_H

/* IPv4-specific defines for netfilter. 
 * (C)1998 Rusty Russell -- This code is GPL.
 */

#include <linux/config.h>
#include <linux/netfilter.h>

/* IP Cache bits. */
/* Src IP address. */
#define NFC_IP_SRC		0x0001
/* Dest IP address. */
#define NFC_IP_DST		0x0002
/* Input device. */
#define NFC_IP_IF_IN		0x0004
/* Output device. */
#define NFC_IP_IF_OUT		0x0008
/* TOS. */
#define NFC_IP_TOS		0x0010
/* Protocol. */
#define NFC_IP_PROTO		0x0020
/* IP options. */
#define NFC_IP_OPTIONS		0x0040
/* Frag & flags. */
#define NFC_IP_FRAG		0x0080

/* Per-protocol information: only matters if proto match. */
/* TCP flags. */
#define NFC_IP_TCPFLAGS		0x0100
/* Source port. */
#define NFC_IP_SRC_PT		0x0200
/* Dest port. */
#define NFC_IP_DST_PT		0x0400
/* Something else about the proto */
#define NFC_IP_PROTO_UNKNOWN	0x2000

/* IP Hooks */
/* After promisc drops, checksum checks. */
#define NF_IP_PRE_ROUTING	0
/* If the packet is destined for this box. */
#define NF_IP_LOCAL_IN		1
/* If the packet is destined for another interface. */
#define NF_IP_FORWARD		2
/* Packets coming from a local process. */
#define NF_IP_LOCAL_OUT		3
/* Packets about to hit the wire. */
#define NF_IP_POST_ROUTING	4
#define NF_IP_NUMHOOKS		5

#ifdef CONFIG_NETFILTER_DEBUG
void debug_print_hooks_ip(unsigned int nf_debug);
void nf_debug_ip_local_deliver(struct sk_buff *skb);
void nf_debug_ip_loopback_xmit(struct sk_buff *newskb);
void nf_debug_ip_finish_output2(struct sk_buff *skb);
#endif

#endif /*__LINUX_IP_NETFILTER_H*/
