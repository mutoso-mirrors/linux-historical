/* 
   BlueZ - Bluetooth protocol stack for Linux
   Copyright (C) 2000-2001 Qualcomm Incorporated

   Written 2000,2001 by Maxim Krasnyansky <maxk@qualcomm.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 2 as
   published by the Free Software Foundation;

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
   IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
   CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES 
   WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN 
   ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF 
   OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

   ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS, 
   COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS 
   SOFTWARE IS DISCLAIMED.
*/

/*
 *  $Id: bluetooth.h,v 1.8 2002/04/17 17:37:20 maxk Exp $
 */

#ifndef __BLUETOOTH_H
#define __BLUETOOTH_H

#include <asm/types.h>
#include <asm/byteorder.h>
#include <linux/poll.h>
#include <net/sock.h>

#ifndef AF_BLUETOOTH
#define AF_BLUETOOTH	31
#define PF_BLUETOOTH	AF_BLUETOOTH
#endif

/* Reserv for core and drivers use */
#define BT_SKB_RESERVE       8

#define BTPROTO_L2CAP   0
#define BTPROTO_HCI     1
#define BTPROTO_SCO   	2
#define BTPROTO_RFCOMM	3
#define BTPROTO_BNEP    4

#define SOL_HCI     0
#define SOL_L2CAP   6
#define SOL_SCO     17
#define SOL_RFCOMM  18

#define BT_INFO(fmt, arg...) printk(KERN_INFO "Bluetooth: " fmt "\n" , ## arg)
#define BT_DBG(fmt, arg...)  printk(KERN_INFO "%s: " fmt "\n" , __FUNCTION__ , ## arg)
#define BT_ERR(fmt, arg...)  printk(KERN_ERR  "%s: " fmt "\n" , __FUNCTION__ , ## arg)

#ifdef HCI_DATA_DUMP
#define BT_DMP(buf, len)    bt_dump(__FUNCTION__, buf, len)
#else
#define BT_DMP(D...)
#endif

/* Connection and socket states */
enum {
	BT_CONNECTED = 1, /* Equal to TCP_ESTABLISHED to make net code happy */
	BT_OPEN,
	BT_BOUND,
	BT_LISTEN,
	BT_CONNECT,
	BT_CONNECT2,
	BT_CONFIG,
	BT_DISCONN,
	BT_CLOSED
};

/* Endianness conversions */
#define htobs(a)	__cpu_to_le16(a)
#define htobl(a)	__cpu_to_le32(a)
#define btohs(a)	__le16_to_cpu(a)
#define btohl(a)	__le32_to_cpu(a)

/* BD Address */
typedef struct {
	__u8 b[6];
} __attribute__((packed)) bdaddr_t;

#define BDADDR_ANY   (&(bdaddr_t) {{0, 0, 0, 0, 0, 0}})
#define BDADDR_LOCAL (&(bdaddr_t) {{0, 0, 0, 0xff, 0xff, 0xff}})

/* Copy, swap, convert BD Address */
static inline int bacmp(bdaddr_t *ba1, bdaddr_t *ba2)
{
	return memcmp(ba1, ba2, sizeof(bdaddr_t));
}
static inline void bacpy(bdaddr_t *dst, bdaddr_t *src)
{
	memcpy(dst, src, sizeof(bdaddr_t));
}

void baswap(bdaddr_t *dst, bdaddr_t *src);
char *batostr(bdaddr_t *ba);
bdaddr_t *strtoba(char *str);

/* Common socket structures and functions */

#define bt_sk(__sk) ((struct bt_sock *) __sk)

struct bt_sock {
	struct sock sk;
	bdaddr_t    src;
	bdaddr_t    dst;
	struct list_head accept_q;
	struct sock *parent;
};

struct bt_sock_list {
	struct sock *head;
	rwlock_t     lock;
};

int  bt_sock_register(int proto, struct net_proto_family *ops);
int  bt_sock_unregister(int proto);
struct sock *bt_sock_alloc(struct socket *sock, int proto, int pi_size, int prio);
void bt_sock_link(struct bt_sock_list *l, struct sock *s);
void bt_sock_unlink(struct bt_sock_list *l, struct sock *s);
int  bt_sock_recvmsg(struct kiocb *iocb, struct socket *sock, struct msghdr *msg, int len, int flags, struct scm_cookie *scm);
uint bt_sock_poll(struct file * file, struct socket *sock, poll_table *wait);
int  bt_sock_w4_connect(struct sock *sk, int flags);

void bt_accept_enqueue(struct sock *parent, struct sock *sk);
struct sock *bt_accept_dequeue(struct sock *parent, struct socket *newsock);

/* Skb helpers */
struct bt_skb_cb {
	int    incoming;
};
#define bt_cb(skb) ((struct bt_skb_cb *)(skb->cb)) 

static inline struct sk_buff *bt_skb_alloc(unsigned int len, int how)
{
	struct sk_buff *skb;

	if ((skb = alloc_skb(len + BT_SKB_RESERVE, how))) {
		skb_reserve(skb, BT_SKB_RESERVE);
		bt_cb(skb)->incoming  = 0;
	}
	return skb;
}

static inline struct sk_buff *bt_skb_send_alloc(struct sock *sk, unsigned long len, 
						       int nb, int *err)
{
	struct sk_buff *skb;

	if ((skb = sock_alloc_send_skb(sk, len + BT_SKB_RESERVE, nb, err))) {
		skb_reserve(skb, BT_SKB_RESERVE);
		bt_cb(skb)->incoming  = 0;
	}

	return skb;
}

static inline int skb_frags_no(struct sk_buff *skb)
{
	register struct sk_buff *frag = skb_shinfo(skb)->frag_list;
	register int n = 1;

	for (; frag; frag=frag->next, n++);
	return n;
}

void bt_dump(char *pref, __u8 *buf, int count);

int  bt_err(__u16 code);

#endif /* __BLUETOOTH_H */
