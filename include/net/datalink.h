#ifndef _NET_INET_DATALINK_H_
#define _NET_INET_DATALINK_H_

struct datalink_proto {
        unsigned short  type_len;
        unsigned char   type[8];
        const char      *string_name;

        union {
                struct llc_pinfo *llc;
        } ll_pinfo;

	struct llc_sc_info *llc_sc;
	struct sock *sock;

        unsigned short  header_length;

        int     (*rcvfunc)(struct sk_buff *, struct net_device *,
                                struct packet_type *);
        void    (*datalink_header)(struct datalink_proto *, struct sk_buff *,
                                        unsigned char *);
        struct datalink_proto   *next;
};

#endif
