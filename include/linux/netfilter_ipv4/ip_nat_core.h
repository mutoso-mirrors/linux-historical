#ifndef _IP_NAT_CORE_H
#define _IP_NAT_CORE_H
#include <linux/list.h>
#include <linux/netfilter_ipv4/ip_conntrack.h>

/* This header used to share core functionality between the standalone
   NAT module, and the compatibility layer's use of NAT for masquerading. */
extern int ip_nat_init(void);
extern void ip_nat_cleanup(void);

extern unsigned int do_bindings(struct ip_conntrack *ct,
				enum ip_conntrack_info conntrackinfo,
				struct ip_nat_info *info,
				unsigned int hooknum,
				struct sk_buff **pskb);

extern int icmp_reply_translation(struct sk_buff **pskb,
				  struct ip_conntrack *conntrack,
				  unsigned int hooknum,
				  int dir);


#endif /* _IP_NAT_CORE_H */
