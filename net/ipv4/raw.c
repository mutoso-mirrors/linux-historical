/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		RAW - implementation of IP "raw" sockets.
 *
 * Version:	$Id: raw.c,v 1.64 2002/02/01 22:01:04 davem Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Alan Cox	:	verify_area() fixed up
 *		Alan Cox	:	ICMP error handling
 *		Alan Cox	:	EMSGSIZE if you send too big a packet
 *		Alan Cox	: 	Now uses generic datagrams and shared
 *					skbuff library. No more peek crashes,
 *					no more backlogs
 *		Alan Cox	:	Checks sk->broadcast.
 *		Alan Cox	:	Uses skb_free_datagram/skb_copy_datagram
 *		Alan Cox	:	Raw passes ip options too
 *		Alan Cox	:	Setsocketopt added
 *		Alan Cox	:	Fixed error return for broadcasts
 *		Alan Cox	:	Removed wake_up calls
 *		Alan Cox	:	Use ttl/tos
 *		Alan Cox	:	Cleaned up old debugging
 *		Alan Cox	:	Use new kernel side addresses
 *	Arnt Gulbrandsen	:	Fixed MSG_DONTROUTE in raw sockets.
 *		Alan Cox	:	BSD style RAW socket demultiplexing.
 *		Alan Cox	:	Beginnings of mrouted support.
 *		Alan Cox	:	Added IP_HDRINCL option.
 *		Alan Cox	:	Skip broadcast check if BSDism set.
 *		David S. Miller	:	New socket lookup architecture.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
 
#include <linux/config.h> 
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/ioctls.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/mroute.h>
#include <net/tcp.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/raw.h>
#include <net/inet_common.h>
#include <net/checksum.h>
#include <linux/netfilter_ipv4.h>

struct sock *raw_v4_htable[RAWV4_HTABLE_SIZE];
rwlock_t raw_v4_lock = RW_LOCK_UNLOCKED;

static void raw_v4_hash(struct sock *sk)
{
	struct sock **skp = &raw_v4_htable[inet_sk(sk)->num &
					   (RAWV4_HTABLE_SIZE - 1)];

	write_lock_bh(&raw_v4_lock);
	if ((sk->next = *skp) != NULL)
		(*skp)->pprev = &sk->next;
	*skp = sk;
	sk->pprev = skp;
	sock_prot_inc_use(sk->prot);
 	sock_hold(sk);
	write_unlock_bh(&raw_v4_lock);
}

static void raw_v4_unhash(struct sock *sk)
{
 	write_lock_bh(&raw_v4_lock);
	if (sk->pprev) {
		if (sk->next)
			sk->next->pprev = sk->pprev;
		*sk->pprev = sk->next;
		sk->pprev = NULL;
		sock_prot_dec_use(sk->prot);
		__sock_put(sk);
	}
	write_unlock_bh(&raw_v4_lock);
}

struct sock *__raw_v4_lookup(struct sock *sk, unsigned short num,
			     unsigned long raddr, unsigned long laddr,
			     int dif)
{
	struct sock *s = sk;

	for (s = sk; s; s = s->next) {
		struct inet_opt *inet = inet_sk(s);

		if (inet->num == num 					&&
		    !(inet->daddr && inet->daddr != raddr) 		&&
		    !(inet->rcv_saddr && inet->rcv_saddr != laddr)	&&
		    !(s->bound_dev_if && s->bound_dev_if != dif))
			break; /* gotcha */
	}
	return s;
}

/*
 *	0 - deliver
 *	1 - block
 */
static __inline__ int icmp_filter(struct sock *sk, struct sk_buff *skb)
{
	int type;

	type = skb->h.icmph->type;
	if (type < 32) {
		__u32 data = raw4_sk(sk)->filter.data;

		return ((1 << type) & data) != 0;
	}

	/* Do not block unknown ICMP types */
	return 0;
}

/* IP input processing comes here for RAW socket delivery.
 * Caller owns SKB, so we must make clones.
 *
 * RFC 1122: SHOULD pass TOS value up to the transport layer.
 * -> It does. And not only TOS, but all IP header.
 */
void raw_v4_input(struct sk_buff *skb, struct iphdr *iph, int hash)
{
	struct sock *sk;

	read_lock(&raw_v4_lock);
	if ((sk = raw_v4_htable[hash]) == NULL)
		goto out;
	sk = __raw_v4_lookup(sk, iph->protocol,
			     iph->saddr, iph->daddr,
			     skb->dev->ifindex);

	while (sk) {
		if (iph->protocol != IPPROTO_ICMP || !icmp_filter(sk, skb)) {
			struct sk_buff *clone = skb_clone(skb, GFP_ATOMIC);

			/* Not releasing hash table! */
			if (clone)
				raw_rcv(sk, clone);
		}
		sk = __raw_v4_lookup(sk->next, iph->protocol,
				     iph->saddr, iph->daddr,
				     skb->dev->ifindex);
	}
out:
	read_unlock(&raw_v4_lock);
}

void raw_err (struct sock *sk, struct sk_buff *skb, u32 info)
{
	struct inet_opt *inet = inet_sk(sk);
	int type = skb->h.icmph->type;
	int code = skb->h.icmph->code;
	int err = 0;
	int harderr = 0;

	/* Report error on raw socket, if:
	   1. User requested ip_recverr.
	   2. Socket is connected (otherwise the error indication
	      is useless without ip_recverr and error is hard.
	 */
	if (!inet->recverr && sk->state != TCP_ESTABLISHED)
		return;

	switch (type) {
	default:
	case ICMP_TIME_EXCEEDED:
		err = EHOSTUNREACH;
		break;
	case ICMP_SOURCE_QUENCH:
		return;
	case ICMP_PARAMETERPROB:
		err = EPROTO;
		harderr = 1;
		break;
	case ICMP_DEST_UNREACH:
		err = EHOSTUNREACH;
		if (code > NR_ICMP_UNREACH)
			break;
		err = icmp_err_convert[code].errno;
		harderr = icmp_err_convert[code].fatal;
		if (code == ICMP_FRAG_NEEDED) {
			harderr = inet->pmtudisc != IP_PMTUDISC_DONT;
			err = EMSGSIZE;
		}
	}

	if (inet->recverr) {
		struct iphdr *iph = (struct iphdr*)skb->data;
		u8 *payload = skb->data + (iph->ihl << 2);

		if (inet->hdrincl)
			payload = skb->data;
		ip_icmp_error(sk, skb, err, 0, info, payload);
	}

	if (inet->recverr || harderr) {
		sk->err = err;
		sk->error_report(sk);
	}
}

static int raw_rcv_skb(struct sock * sk, struct sk_buff * skb)
{
	/* Charge it to the socket. */
	
	if (sock_queue_rcv_skb(sk, skb) < 0) {
		IP_INC_STATS(IpInDiscards);
		kfree_skb(skb);
		return NET_RX_DROP;
	}

	IP_INC_STATS(IpInDelivers);
	return NET_RX_SUCCESS;
}

int raw_rcv(struct sock *sk, struct sk_buff *skb)
{
	skb_push(skb, skb->data - skb->nh.raw);

	raw_rcv_skb(sk, skb);
	return 0;
}

static int raw_send_hdrinc(struct sock *sk, void *from, int length,
			struct rtable *rt, 
			unsigned int flags)
{
	struct inet_opt *inet = inet_sk(sk);
	int hh_len;
	struct iphdr *iph;
	struct sk_buff *skb;
	int err;

	if (length > rt->u.dst.dev->mtu) {
		ip_local_error(sk, EMSGSIZE, rt->rt_dst, inet->dport,
			       rt->u.dst.dev->mtu);
		return -EMSGSIZE;
	}
	if (flags&MSG_PROBE)
		goto out;

	hh_len = (rt->u.dst.dev->hard_header_len&~15) + 16;

	skb = sock_alloc_send_skb(sk, length+hh_len+15,
				  flags&MSG_DONTWAIT, &err);
	if (skb == NULL)
		goto error; 
	skb_reserve(skb, hh_len);

	skb->priority = sk->priority;
	skb->dst = dst_clone(&rt->u.dst);

	skb->nh.iph = iph = (struct iphdr *)skb_put(skb, length);

	skb->ip_summed = CHECKSUM_NONE;

	skb->h.raw = skb->nh.raw;
	err = memcpy_fromiovecend((void *)iph, from, 0, length);
	if (err)
		goto error_fault;

	/* We don't modify invalid header */
	if (length >= sizeof(*iph) && iph->ihl * 4 <= length) {
		if (!iph->saddr)
			iph->saddr = rt->rt_src;
		iph->check   = 0;
		iph->tot_len = htons(length);
		if (!iph->id)
			ip_select_ident(iph, &rt->u.dst, NULL);

		iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
	}

	err = NF_HOOK(PF_INET, NF_IP_LOCAL_OUT, skb, NULL, rt->u.dst.dev,
		      dst_output);
	if (err > 0)
		err = inet->recverr ? net_xmit_errno(err) : 0;
	if (err)
		goto error;
out:
	return 0;

error_fault:
	err = -EFAULT;
	kfree_skb(skb);
error:
	IP_INC_STATS(IpOutDiscards);
	return err; 
}

static int raw_sendmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		       int len)
{
	struct inet_opt *inet = inet_sk(sk);
	struct ipcm_cookie ipc;
	struct rtable *rt = NULL;
	int free = 0;
	u32 daddr;
	u32 saddr;
	u8  tos;
	int err;

	/* This check is ONLY to check for arithmetic overflow
	   on integer(!) len. Not more! Real check will be made
	   in ip_build_xmit --ANK

	   BTW socket.c -> af_*.c -> ... make multiple
	   invalid conversions size_t -> int. We MUST repair it f.e.
	   by replacing all of them with size_t and revise all
	   the places sort of len += sizeof(struct iphdr)
	   If len was ULONG_MAX-10 it would be cathastrophe  --ANK
	 */

	err = -EMSGSIZE;
	if (len < 0 || len > 0xFFFF)
		goto out;

	/*
	 *	Check the flags.
	 */

	err = -EOPNOTSUPP;
	if (msg->msg_flags & MSG_OOB)	/* Mirror BSD error message */
		goto out;               /* compatibility */
			 
	/*
	 *	Get and verify the address. 
	 */

	if (msg->msg_namelen) {
		struct sockaddr_in *usin = (struct sockaddr_in*)msg->msg_name;
		err = -EINVAL;
		if (msg->msg_namelen < sizeof(*usin))
			goto out;
		if (usin->sin_family != AF_INET) {
			static int complained;
			if (!complained++)
				printk(KERN_INFO "%s forgot to set AF_INET in "
						 "raw sendmsg. Fix it!\n",
						 current->comm);
			err = -EINVAL;
			if (usin->sin_family)
				goto out;
		}
		daddr = usin->sin_addr.s_addr;
		/* ANK: I did not forget to get protocol from port field.
		 * I just do not know, who uses this weirdness.
		 * IP_HDRINCL is much more convenient.
		 */
	} else {
		err = -EINVAL;
		if (sk->state != TCP_ESTABLISHED) 
			goto out;
		daddr = inet->daddr;
	}

	ipc.addr = inet->saddr;
	ipc.opt = NULL;
	ipc.oif = sk->bound_dev_if;

	if (msg->msg_controllen) {
		err = ip_cmsg_send(msg, &ipc);
		if (err)
			goto out;
		if (ipc.opt)
			free = 1;
	}

	saddr = ipc.addr;
	ipc.addr = daddr;

	if (!ipc.opt)
		ipc.opt = inet->opt;

	if (ipc.opt) {
		err = -EINVAL;
		/* Linux does not mangle headers on raw sockets,
		 * so that IP options + IP_HDRINCL is non-sense.
		 */
		if (inet->hdrincl)
			goto done;
		if (ipc.opt->srr) {
			if (!daddr)
				goto done;
			daddr = ipc.opt->faddr;
		}
	}
	tos = RT_TOS(inet->tos) | sk->localroute;
	if (msg->msg_flags & MSG_DONTROUTE)
		tos |= RTO_ONLINK;

	if (MULTICAST(daddr)) {
		if (!ipc.oif)
			ipc.oif = inet->mc_index;
		if (!saddr)
			saddr = inet->mc_addr;
	}

	{
		struct flowi fl = { .nl_u = { .ip4_u =
					      { .daddr = daddr,
						.saddr = saddr,
						.tos = tos } },
				    .oif = ipc.oif };
		err = ip_route_output_key(&rt, &fl);
	}
	if (err)
		goto done;

	err = -EACCES;
	if (rt->rt_flags & RTCF_BROADCAST && !sk->broadcast)
		goto done;

	if (msg->msg_flags & MSG_CONFIRM)
		goto do_confirm;
back_from_confirm:

	if (inet->hdrincl)
		err = raw_send_hdrinc(sk, msg->msg_iov, len, 
					rt, msg->msg_flags);
	
	 else {
		if (!ipc.addr)
			ipc.addr = rt->rt_dst;
		lock_sock(sk);
		err = ip_append_data(sk, ip_generic_getfrag, msg->msg_iov, len, 0,
					&ipc, rt, msg->msg_flags);
		if (err)
			ip_flush_pending_frames(sk);
		else if (!(msg->msg_flags & MSG_MORE))
			err = ip_push_pending_frames(sk);
		release_sock(sk);
	}
done:
	if (free)
		kfree(ipc.opt);
	ip_rt_put(rt);

out:	return err < 0 ? err : len;

do_confirm:
	dst_confirm(&rt->u.dst);
	if (!(msg->msg_flags & MSG_PROBE) || len)
		goto back_from_confirm;
	err = 0;
	goto done;
}

static void raw_close(struct sock *sk, long timeout)
{
        /*
	 * Raw sockets may have direct kernel refereneces. Kill them.
	 */
	ip_ra_control(sk, 0, NULL);

	inet_sock_release(sk);
}

/* This gets rid of all the nasties in af_inet. -DaveM */
static int raw_bind(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct inet_opt *inet = inet_sk(sk);
	struct sockaddr_in *addr = (struct sockaddr_in *) uaddr;
	int ret = -EINVAL;
	int chk_addr_ret;

	if (sk->state != TCP_CLOSE || addr_len < sizeof(struct sockaddr_in))
		goto out;
	chk_addr_ret = inet_addr_type(addr->sin_addr.s_addr);
	ret = -EADDRNOTAVAIL;
	if (addr->sin_addr.s_addr && chk_addr_ret != RTN_LOCAL &&
	    chk_addr_ret != RTN_MULTICAST && chk_addr_ret != RTN_BROADCAST)
		goto out;
	inet->rcv_saddr = inet->saddr = addr->sin_addr.s_addr;
	if (chk_addr_ret == RTN_MULTICAST || chk_addr_ret == RTN_BROADCAST)
		inet->saddr = 0;  /* Use device */
	sk_dst_reset(sk);
	ret = 0;
out:	return ret;
}

/*
 *	This should be easy, if there is something there
 *	we return it, otherwise we block.
 */

int raw_recvmsg(struct kiocb *iocb, struct sock *sk, struct msghdr *msg,
		int len, int noblock, int flags, int *addr_len)
{
	struct inet_opt *inet = inet_sk(sk);
	int copied = 0;
	int err = -EOPNOTSUPP;
	struct sockaddr_in *sin = (struct sockaddr_in *)msg->msg_name;
	struct sk_buff *skb;

	if (flags & MSG_OOB)
		goto out;

	if (addr_len)
		*addr_len = sizeof(*sin);

	if (flags & MSG_ERRQUEUE) {
		err = ip_recv_error(sk, msg, len);
		goto out;
	}

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto out;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	err = skb_copy_datagram_iovec(skb, 0, msg->msg_iov, copied);
	if (err)
		goto done;

	sock_recv_timestamp(msg, sk, skb);

	/* Copy the address. */
	if (sin) {
		sin->sin_family = AF_INET;
		sin->sin_addr.s_addr = skb->nh.iph->saddr;
		memset(&sin->sin_zero, 0, sizeof(sin->sin_zero));
	}
	if (inet->cmsg_flags)
		ip_cmsg_recv(msg, skb);
done:
	skb_free_datagram(sk, skb);
out:	return err ? : copied;
}

static int raw_init(struct sock *sk)
{
	struct raw_opt *tp = raw4_sk(sk);
	if (inet_sk(sk)->num == IPPROTO_ICMP)
		memset(&tp->filter, 0, sizeof(tp->filter));
	return 0;
}

static int raw_seticmpfilter(struct sock *sk, char *optval, int optlen)
{
	if (optlen > sizeof(struct icmp_filter))
		optlen = sizeof(struct icmp_filter);
	if (copy_from_user(&raw4_sk(sk)->filter, optval, optlen))
		return -EFAULT;
	return 0;
}

static int raw_geticmpfilter(struct sock *sk, char *optval, int *optlen)
{
	int len, ret = -EFAULT;

	if (get_user(len, optlen))
		goto out;
	ret = -EINVAL;
	if (len < 0)
		goto out;
	if (len > sizeof(struct icmp_filter))
		len = sizeof(struct icmp_filter);
	ret = -EFAULT;
	if (put_user(len, optlen) ||
	    copy_to_user(optval, &raw4_sk(sk)->filter, len))
		goto out;
	ret = 0;
out:	return ret;
}

static int raw_setsockopt(struct sock *sk, int level, int optname, 
			  char *optval, int optlen)
{
	if (level != SOL_RAW)
		return ip_setsockopt(sk, level, optname, optval, optlen);

	if (optname == ICMP_FILTER) {
		if (inet_sk(sk)->num != IPPROTO_ICMP)
			return -EOPNOTSUPP;
		else
			return raw_seticmpfilter(sk, optval, optlen);
	}
	return -ENOPROTOOPT;
}

static int raw_getsockopt(struct sock *sk, int level, int optname, 
			  char *optval, int *optlen)
{
	if (level != SOL_RAW)
		return ip_getsockopt(sk, level, optname, optval, optlen);

	if (optname == ICMP_FILTER) {
		if (inet_sk(sk)->num != IPPROTO_ICMP)
			return -EOPNOTSUPP;
		else
			return raw_geticmpfilter(sk, optval, optlen);
	}
	return -ENOPROTOOPT;
}

static int raw_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	switch (cmd) {
		case SIOCOUTQ: {
			int amount = atomic_read(&sk->wmem_alloc);
			return put_user(amount, (int *)arg);
		}
		case SIOCINQ: {
			struct sk_buff *skb;
			int amount = 0;

			spin_lock_irq(&sk->receive_queue.lock);
			skb = skb_peek(&sk->receive_queue);
			if (skb != NULL)
				amount = skb->len;
			spin_unlock_irq(&sk->receive_queue.lock);
			return put_user(amount, (int *)arg);
		}

		default:
#ifdef CONFIG_IP_MROUTE
			return ipmr_ioctl(sk, cmd, arg);
#else
			return -ENOIOCTLCMD;
#endif
	}
}

static void get_raw_sock(struct sock *sp, char *tmpbuf, int i)
{
	struct inet_opt *inet = inet_sk(sp);
	unsigned int dest = inet->daddr,
		     src = inet->rcv_saddr;
	__u16 destp = 0,
	      srcp  = inet->num;

	sprintf(tmpbuf, "%4d: %08X:%04X %08X:%04X"
		" %02X %08X:%08X %02X:%08lX %08X %5d %8d %lu %d %p",
		i, src, srcp, dest, destp, sp->state, 
		atomic_read(&sp->wmem_alloc), atomic_read(&sp->rmem_alloc),
		0, 0L, 0,
		sock_i_uid(sp), 0,
		sock_i_ino(sp),
		atomic_read(&sp->refcnt), sp);
}

int raw_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len = 0, num = 0, i;
	off_t pos = 128;
	off_t begin;
	char tmpbuf[129];

	if (offset < 128) 
		len += sprintf(buffer, "%-127s\n",
			       "  sl  local_address rem_address   st tx_queue "
			       "rx_queue tr tm->when retrnsmt   uid  timeout "
			       "inode");
	read_lock(&raw_v4_lock);
	for (i = 0; i < RAWV4_HTABLE_SIZE; i++) {
		struct sock *sk;

		for (sk = raw_v4_htable[i]; sk; sk = sk->next, num++) {
			if (sk->family != PF_INET)
				continue;
			pos += 128;
			if (pos <= offset)
				continue;
			get_raw_sock(sk, tmpbuf, i);
			len += sprintf(buffer + len, "%-127s\n", tmpbuf);
			if (len >= length)
				goto out;
		}
	}
out:
	read_unlock(&raw_v4_lock);
	begin = len - (pos - offset);
	*start = buffer + begin;
	len -= begin;
	if (len > length)
		len = length;
	if (len < 0)
		len = 0; 
	return len;
}

struct proto raw_prot = {
	.name =		"RAW",
	.close =	raw_close,
	.connect =	udp_connect,
	.disconnect =	udp_disconnect,
	.ioctl =	raw_ioctl,
	.init =		raw_init,
	.setsockopt =	raw_setsockopt,
	.getsockopt =	raw_getsockopt,
	.sendmsg =	raw_sendmsg,
	.recvmsg =	raw_recvmsg,
	.bind =		raw_bind,
	.backlog_rcv =	raw_rcv_skb,
	.hash =		raw_v4_hash,
	.unhash =	raw_v4_unhash,
};
