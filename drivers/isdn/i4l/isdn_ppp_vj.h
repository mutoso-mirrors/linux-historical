
#ifndef __ISDN_PPP_VJ_H__
#define __ISDN_PPP_VJ_H__

#include <linux/kernel.h>
#include <linux/isdn.h>


#ifdef CONFIG_ISDN_PPP_VJ


struct slcompress *
ippp_vj_alloc(void);

void
ippp_vj_free(struct slcompress *slcomp);

int
ippp_vj_set_maxcid(isdn_net_dev *idev, int val);

struct sk_buff *
ippp_vj_decompress(struct slcompress *slcomp, struct sk_buff *skb_old, 
		   u16 proto);

struct sk_buff *
ippp_vj_compress(isdn_net_dev *idev, struct sk_buff *skb_old, u16 *proto);


#else


static inline struct slcompress *
ippp_vj_alloc(void)
{ return (struct slcompress *) !NULL; }

static inline void
ippp_vj_free(struct slcompress *slcomp) 
{ }

static inline int
ippp_vj_set_maxcid(isdn_net_dev *idev, int val)
{ return -EINVAL; }

static inline struct sk_buff *
ippp_vj_decompress(struct slcompress *slcomp, struct sk_buff *skb_old, 
		   u16 proto)
{ return skb_old; }

static inline struct sk_buff *
ippp_vj_compress(isdn_net_dev *idev, struct sk_buff *skb_old, u16 *proto)
{ return skb_old; }


#endif

#endif
