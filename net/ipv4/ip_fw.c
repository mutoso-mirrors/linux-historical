/*
 *	IP firewalling code. This is taken from 4.4BSD. Please note the 
 *	copyright message below. As per the GPL it must be maintained
 *	and the licenses thus do not conflict. While this port is subject
 *	to the GPL I also place my modifications under the original 
 *	license in recognition of the original copyright. 
 *				-- Alan Cox.
 *
 *	Ported from BSD to Linux,
 *		Alan Cox 22/Nov/1994.
 *	Zeroing /proc and other additions
 *		Jos Vos 4/Feb/1995.
 *	Merged and included the FreeBSD-Current changes at Ugen's request
 *	(but hey it's a lot cleaner now). Ugen would prefer in some ways
 *	we waited for his final product but since Linux 1.2.0 is about to
 *	appear it's not practical - Read: It works, it's not clean but please
 *	don't consider it to be his standard of finished work.
 *		Alan Cox 12/Feb/1995
 *	Porting bidirectional entries from BSD, fixing accounting issues,
 *	adding struct ip_fwpkt for checking packets with interface address
 *		Jos Vos 5/Mar/1995.
 *	Established connections (ACK check), ACK check on bidirectional rules,
 *	ICMP type check.
 *		Wilfred Mollenvanger 7/7/1995.
 *	TCP attack protection.
 *		Alan Cox 25/8/95, based on information from bugtraq.
 *
 * Masquerading functionality
 *
 * Copyright (c) 1994 Pauline Middelink
 *
 * The pieces which added masquerading functionality are totaly
 * my responsibility and have nothing to with the original authors
 * copyright or doing.
 *
 * Parts distributed under GPL.
 *
 * Fixes:
 *	Pauline Middelink	:	Added masquerading.
 *	Alan Cox		:	Fixed an error in the merge.
 *	Thomas Quinot		:	Fixed port spoofing.
 *	Alan Cox		:	Cleaned up retransmits in spoofing.
 *	Alan Cox		:	Cleaned up length setting.
 *	Wouter Gadeyne		:	Fixed masquerading support of ftp PORT commands
 *
 *	All the real work was done by .....
 *
 */


/*
 * Copyright (c) 1993 Daniel Boulet
 * Copyright (c) 1994 Ugen J.S.Antsilevich
 *
 * Redistribution and use in source forms, with and without modification,
 * are permitted provided that this entire comment appears intact.
 *
 * Redistribution in binary form may occur without any restrictions.
 * Obviously, it would be nice if you gave credit where credit is due
 * but requiring it would be too onerous.
 *
 * This software is provided ``AS IS'' without any warranties of any kind.
 */

#include <linux/config.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/config.h>

#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/icmp.h>
#include <linux/udp.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <linux/ip_fw.h>
#include <net/checksum.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>

/*
 *	Implement IP packet firewall
 */

#ifdef CONFIG_IPFIREWALL_DEBUG 
#define dprintf1(a)		printk(a)
#define dprintf2(a1,a2)		printk(a1,a2)
#define dprintf3(a1,a2,a3)	printk(a1,a2,a3)
#define dprintf4(a1,a2,a3,a4)	printk(a1,a2,a3,a4)
#else
#define dprintf1(a)	
#define dprintf2(a1,a2)
#define dprintf3(a1,a2,a3)
#define dprintf4(a1,a2,a3,a4)
#endif

#define print_ip(a)	 printk("%ld.%ld.%ld.%ld",(ntohl(a)>>24)&0xFF,\
					      (ntohl(a)>>16)&0xFF,\
					      (ntohl(a)>>8)&0xFF,\
					      (ntohl(a))&0xFF);

#ifdef IPFIREWALL_DEBUG
#define dprint_ip(a)	print_ip(a)
#else
#define dprint_ip(a)	
#endif

#ifdef CONFIG_IP_FIREWALL
struct ip_fw *ip_fw_fwd_chain;
struct ip_fw *ip_fw_blk_chain;
int ip_fw_blk_policy=IP_FW_F_ACCEPT;
int ip_fw_fwd_policy=IP_FW_F_ACCEPT;
#endif
#ifdef CONFIG_IP_ACCT
struct ip_fw *ip_acct_chain;
#endif

#define IP_INFO_BLK	0
#define IP_INFO_FWD	1
#define IP_INFO_ACCT	2

#ifdef CONFIG_IP_MASQUERADE
/*
 *	Implement IP packet masquerading
 */

static unsigned short masq_port = PORT_MASQ_BEGIN;
static const char *strProt[] = {"UDP","TCP"};
struct ip_masq *ip_msq_hosts;

#endif

/*
 *	Returns 1 if the port is matched by the vector, 0 otherwise
 */

extern inline int port_match(unsigned short *portptr,int nports,unsigned short port,int range_flag)
{
	if (!nports)
		return 1;
	if ( range_flag ) 
	{
		if ( portptr[0] <= port && port <= portptr[1] ) 
		{
			return( 1 );
		}
		nports -= 2;
		portptr += 2;
	}
	while ( nports-- > 0 ) 
	{
		if ( *portptr++ == port ) 
		{
			return( 1 );
		}
	}
	return(0);
}

#if defined(CONFIG_IP_ACCT) || defined(CONFIG_IP_FIREWALL)


/*
 *	Returns 0 if packet should be dropped, 1 if it should be accepted,
 *	and -1 if an ICMP host unreachable packet should be sent.
 *	Also does accounting so you can feed it the accounting chain.
 *	If opt is set to 1, it means that we do this for accounting
 *	purposes (searches all entries and handles fragments different).
 *	If opt is set to 2, it doesn't count a matching packet, which
 *	is used when calling this for checking purposes (IP_FW_CHK_*).
 */


int ip_fw_chk(struct iphdr *ip, struct device *rif, struct ip_fw *chain, int policy, int opt)
{
	struct ip_fw *f;
	struct tcphdr		*tcp=(struct tcphdr *)((unsigned long *)ip+ip->ihl);
	struct udphdr		*udp=(struct udphdr *)((unsigned long *)ip+ip->ihl);
	struct icmphdr		*icmp=(struct icmphdr *)((unsigned long *)ip+ip->ihl);
	__u32			src, dst;
	__u16			src_port=0, dst_port=0, icmp_type=0;
	unsigned short		f_prt=0, prt;
	char			notcpsyn=1, notcpack=1, match;
	unsigned short		f_flag;
	unsigned short		offset;

	/*
	 *	If the chain is empty follow policy. The BSD one
	 *	accepts anything giving you a time window while
	 *	flushing and rebuilding the tables.
	 */
	 
	src = ip->saddr;
	dst = ip->daddr;

	/* 
	 *	This way we handle fragmented packets.
	 *	we ignore all fragments but the first one
	 *	so the whole packet can't be reassembled.
	 *	This way we relay on the full info which
	 *	stored only in first packet.
	 *
	 *	Note that this theoretically allows partial packet
	 *	spoofing. Not very dangerous but paranoid people may
	 *	wish to play with this. It also allows the so called
	 *	"fragment bomb" denial of service attack on some types
	 *	of system.
	 */

	offset = ntohs(ip->frag_off) & IP_OFFSET;
	
	/*
	 *	Don't allow a fragment of TCP 8 bytes in. Nobody
	 *	normal causes this. Its a cracker trying to break
	 *	in by doing a flag overwrite to pass the direction
	 *	checks.
	 */
	 
	if (offset == 1 && ip->protocol == IPPROTO_TCP)
		return FW_BLOCK;
		
	if (offset!=0 && (opt != 1) && (ip->protocol == IPPROTO_TCP ||
			ip->protocol == IPPROTO_UDP))
		return FW_ACCEPT;
		
	/*
	 *	 Header fragment for TCP is too small to check the bits.
	 */
	 
	if(ip->protocol==IPPROTO_TCP && (ip->ihl<<2)+16 > ntohs(ip->tot_len))
		return FW_BLOCK;
	
	/*
	 *	Too short.
	 */
	 
	else if(ntohs(ip->tot_len)<8+(ip->ihl<<2))
		return FW_BLOCK;
		
	src = ip->saddr;
	dst = ip->daddr;

	/*
	 *	If we got interface from which packet came
	 *	we can use the address directly. This is unlike
	 *	4.4BSD derived systems that have an address chain
	 *	per device. We have a device per address with dummy
	 *	devices instead.
	 */
	 
	dprintf1("Packet ");
	switch(ip->protocol) 
	{
		case IPPROTO_TCP:
			dprintf1("TCP ");
			/* ports stay 0 if it is not the first fragment */
			if (!offset) {
				src_port=ntohs(tcp->source);
				dst_port=ntohs(tcp->dest);
				if(tcp->ack)
					/* We *DO* have ACK, value FALSE */
					notcpack=0;
				if(tcp->syn && notcpack)
					/* We *DO* have SYN, value FALSE */
					notcpsyn=0;
			}
			prt=IP_FW_F_TCP;
			break;
		case IPPROTO_UDP:
			dprintf1("UDP ");
			/* ports stay 0 if it is not the first fragment */
			if (!offset) {
				src_port=ntohs(udp->source);
				dst_port=ntohs(udp->dest);
			}
			prt=IP_FW_F_UDP;
			break;
		case IPPROTO_ICMP:
			icmp_type=(__u16)(icmp->type);
			dprintf2("ICMP:%d ",icmp_type);
			prt=IP_FW_F_ICMP;
			break;
		default:
			dprintf2("p=%d ",ip->protocol);
			prt=IP_FW_F_ALL;
			break;
	}
#ifdef CONFIG_IP_FIREWALL_DEBUG
	dprint_ip(ip->saddr);
	
	if (ip->protocol==IPPROTO_TCP || ip->protocol==IPPROTO_UDP)
		/* This will print 0 when it is not the first fragment! */
		dprintf2(":%d ", src_port);
	dprint_ip(ip->daddr);
	if (ip->protocol==IPPROTO_TCP || ip->protocol==IPPROTO_UDP)
		/* This will print 0 when it is not the first fragment! */
		dprintf2(":%d ",dst_port);
	dprintf1("\n");
#endif	

	for (f=chain;f;f=f->fw_next) 
	{
		/*
		 *	This is a bit simpler as we don't have to walk
		 *	an interface chain as you do in BSD - same logic
		 *	however.
		 */

		/*
		 *	Match can become 0x01 (a "normal" match was found),
		 *	0x02 (a reverse match was found), and 0x03 (the
		 *	IP addresses match in both directions).
		 *	Now we know in which direction(s) we should look
		 *	for a match for the TCP/UDP ports.  Both directions
		 *	might match (e.g., when both addresses are on the
		 *	same network for which an address/mask is given), but
		 *	the ports might only match in one direction.
		 *	This was obviously wrong in the original BSD code.
		 */
		match = 0x00;

		if ((src&f->fw_smsk.s_addr)==f->fw_src.s_addr
		&&  (dst&f->fw_dmsk.s_addr)==f->fw_dst.s_addr)
			/* normal direction */
			match |= 0x01;

		if ((f->fw_flg & IP_FW_F_BIDIR) &&
		    (dst&f->fw_smsk.s_addr)==f->fw_src.s_addr
		&&  (src&f->fw_dmsk.s_addr)==f->fw_dst.s_addr)
			/* reverse direction */
			match |= 0x02;

		if (match)
		{
			/*
			 *	Look for a VIA match 
			 */
			if(f->fw_via.s_addr && rif)
			{
				if(rif->pa_addr!=f->fw_via.s_addr)
					continue;	/* Mismatch */
			}
			/*
			 *	Drop through - this is a match
			 */
		}
		else
			continue;

		/*
		 *	Ok the chain addresses match.
		 */

		f_prt=f->fw_flg&IP_FW_F_KIND;
		if (f_prt!=IP_FW_F_ALL) 
		{
			/*
			 * This is actually buggy as if you set SYN flag 
			 * on UDP or ICMP firewall it will never work,but 
			 * actually it is a concern of software which sets
			 * firewall entries.
			 */
			 
			 if((f->fw_flg&IP_FW_F_TCPSYN) && notcpsyn)
			 	continue;

			/*
			 * When a bidirectional rule is used we only check
			 * for ack bits on reverse matches. This way it's
			 * easy to set up rules which only allow connections
			 * initiated from "normal" match adresses.
			 */

			if((f->fw_flg&IP_FW_F_TCPACK) && notcpack)
				if(f->fw_flg&IP_FW_F_BIDIR) {
					if(match & 0x02)
						continue;
				} else
					continue;

			/*
			 *	Specific firewall - packet's protocol
			 *	must match firewall's.
			 */

			if(prt!=f_prt)
				continue;
				
			if((prt==IP_FW_F_ICMP &&
				! port_match(&f->fw_pts[0], f->fw_nsp,
					icmp_type,f->fw_flg&IP_FW_F_SRNG)) ||
			    !(prt==IP_FW_F_ICMP || ((match & 0x01) &&
				port_match(&f->fw_pts[0], f->fw_nsp, src_port,
					f->fw_flg&IP_FW_F_SRNG) &&
				port_match(&f->fw_pts[f->fw_nsp], f->fw_ndp, dst_port,
					f->fw_flg&IP_FW_F_DRNG)) || ((match & 0x02) &&
				port_match(&f->fw_pts[0], f->fw_nsp, dst_port,
					f->fw_flg&IP_FW_F_SRNG) &&
				port_match(&f->fw_pts[f->fw_nsp], f->fw_ndp, src_port,
					f->fw_flg&IP_FW_F_DRNG))))
			{
				continue;
			}
		}
#ifdef CONFIG_IP_FIREWALL_VERBOSE
		/*
		 * VERY ugly piece of code which actually
		 * makes kernel printf for denied packets...
		 */

		if (f->fw_flg & IP_FW_F_PRN)
		{
			if(opt != 1) {
				if(f->fw_flg&IP_FW_F_ACCEPT)
					printk("Accept ");
				else if(f->fw_flg&IP_FW_F_ICMPRPL)
					printk("Reject ");
				else
					printk("Deny ");
			}
			switch(ip->protocol)
			{
				case IPPROTO_TCP:
					printk("TCP ");
					break;
				case IPPROTO_UDP:
					printk("UDP ");
				case IPPROTO_ICMP:
					printk("ICMP ");
					break;
				default:
					printk("p=%d ",ip->protocol);
					break;
			}
			print_ip(ip->saddr);
			if(ip->protocol == IPPROTO_TCP || ip->protocol == IPPROTO_UDP)
				printk(":%d", src_port);
			printk(" ");
			print_ip(ip->daddr);
			if(ip->protocol == IPPROTO_TCP || ip->protocol == IPPROTO_UDP)
				printk(":%d",dst_port);
			printk("\n");
		}
#endif		
		if (opt != 2) {
			f->fw_bcnt+=ntohs(ip->tot_len);
			f->fw_pcnt++;
		}
		if (opt != 1)
			break;
	} /* Loop */
	
	if(opt == 1)
		return 0;

	/*
	 * We rely on policy defined in the rejecting entry or, if no match
	 * was found, we rely on the general policy variable for this type
	 * of firewall.
	 */

	if(f!=NULL)	/* A match was found */
		f_flag=f->fw_flg;
	else
		f_flag=policy;
	if(f_flag&IP_FW_F_ACCEPT)
		return ((f_flag&IP_FW_F_MASQ)?FW_MASQUERADE:FW_ACCEPT);
	if(f_flag&IP_FW_F_ICMPRPL)
		return FW_REJECT;
	return FW_BLOCK;
}

#ifdef CONFIG_IP_MASQUERADE

static void masq_expire(unsigned long data)
{
	struct ip_masq *ms = (struct ip_masq *)data;
	struct ip_masq *old,*cur;
	unsigned long flags;

#ifdef DEBUG_MASQ
	printk("Masqueraded %s %lX:%X expired\n",
			strProt[ms->protocol==IPPROTO_TCP],
			ntohl(ms->src),ntohs(ms->sport));
#endif
	
	save_flags(flags);
	cli();

	/* delete from list of hosts */
	old = NULL;
	cur = ip_msq_hosts;
	while (cur!=NULL) {
		if (cur==ms) {
			if (old==NULL) ip_msq_hosts = ms->next;
			else old->next = ms->next;
			kfree_s(ms,sizeof(*ms));
			break;
		}
		old = cur;
		cur=cur->next;
	}
	restore_flags(flags);
}

/*
 * Create a new masquerade list entry, also allocate an
 * unused mport, keeping the portnumber between the
 * given boundaries MASQ_BEGIN and MASQ_END.
 *
 * FIXME: possible deadlock if all free ports are exhausted! 
 */
static struct ip_masq *alloc_masq_entry(void)
{
	struct ip_masq *ms, *mst;
	unsigned long flags;

	ms = (struct ip_masq *) kmalloc(sizeof(struct ip_masq), GFP_ATOMIC);
	if (ms==NULL) 
		return NULL;

	memset(ms,0,sizeof(*ms));
	init_timer(&ms->timer);
	ms->timer.data     = (unsigned long)ms;
	ms->timer.function = masq_expire;

	save_flags(flags);
	cli();
	do 
	{
		/* Try the next available port number */
		ms->mport = htons(masq_port++);
		if (masq_port==PORT_MASQ_END)
			masq_port = PORT_MASQ_BEGIN;

		/* Now hunt through the used ports to see if
		 * this port is in use... */
		mst = ip_msq_hosts;
		while (mst && mst->mport!=ms->mport)
			mst = mst->next;
	}
	while (mst!=NULL); 

	/* add new entry in front of list to minimize lookup-time */
	ms->next  = ip_msq_hosts;
	ip_msq_hosts = ms;
	restore_flags(flags);

	return ms;
}

/*
 * When passing an FTP 'PORT' command, try to replace the IP
 * address with an newly assigned (masquereded) port on this
 * host, so the ftp-data connect FROM the site will succeed...
 *
 * Also, when the size of the packet changes, create an delta
 * offset, which will be added to every th->seq (and subtracted for
 * (th->acqseq) whose seq > init_seq.
 *
 * Not for the faint of heart!
 */

static struct sk_buff *revamp(struct sk_buff *skb, struct device *dev, struct ip_masq *ftp)
{
	struct iphdr *iph = skb->h.iph;
	struct tcphdr *th = (struct tcphdr *)&(((char *)iph)[iph->ihl*4]);
	struct sk_buff *skb2;
	char *p, *data = (char *)&th[1];
	unsigned char p1,p2,p3,p4,p5,p6;
	unsigned long from;
	unsigned short port;
	struct ip_masq *ms;
	char buf[24];		/* xxx.xxx.xxx.xxx,ppp,ppp\000 */
	int diff;
	__u32 seq;
	
	/*
	 * Adjust seq with delta-offset for all packets after the most recent resized PORT command
	 * and with previous_delta offset for all packets before most recent resized PORT
	 */
	
	/*
	 * seq & seq_ack are in network byte order; need conversion before comparing
	 */
	seq=ntohl(th->seq);
	if (ftp->delta || ftp->previous_delta)
	{
		if(after(seq,ftp->init_seq) ) 
		{
			th->seq = htonl(seq + ftp->delta);
#ifdef DEBUG_MASQ
			printk("masq_revamp : added delta (%d) to seq\n",ftp->delta);
#endif
		}
		else
		{
			th->seq = htonl(seq + ftp->previous_delta);
#ifdef DEBUG_MASQ
	 		printk("masq_revamp : added previous_delta (%d) to seq\n",ftp->previous_delta);
#endif
		}
	}

	while (skb->len - ((unsigned char *)data - skb->h.raw) > 18)
	{
		if (memcmp(data,"PORT ",5) && memcmp(data,"port ",5)) 
		{
			data ++;
			continue;
		}
		p = data+5;
 		p1 = simple_strtoul(data+5,&data,10);
		if (*data!=',')
			continue;
		p2 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p3 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p4 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p5 = simple_strtoul(data+1,&data,10);
		if (*data!=',')
			continue;
		p6 = simple_strtoul(data+1,&data,10);
		if (*data!='\r' && *data!='\n')
			continue;

		from = (p1<<24) | (p2<<16) | (p3<<8) | p4;
		port = (p5<<8) | p6;
#ifdef MASQ_DEBUG
		printk("PORT %lX:%X detected\n",from,port);
#endif	
		/*
		 * Now create an masquerade entry for it
		 */
		ms = alloc_masq_entry();
		if (ms==NULL)
			return skb;
		ms->protocol = IPPROTO_TCP;
		ms->src      = htonl(from);	/* derived from PORT cmd */
		ms->sport    = htons(port);	/* derived from PORT cmd */
		ms->dst      = iph->daddr;
		/*
		 * Hardcoding 20 as dport is not always correct
		 * At least 1 Windows ftpd uses a random port number instead of 20
		 * Leave it undefined for now & wait for the first connection request to fill it out
		 */ 
		ms->dport    = htons(FTP_DPORT_TBD);	/* ftp-data */
		ms->timer.expires = jiffies+MASQUERADE_EXPIRE_TCP_FIN;
		add_timer(&ms->timer);

		/*
		 * Replace the old PORT with the new one
		 */
		from = ntohl(dev->pa_addr);
		port = ntohs(ms->mport);
		sprintf(buf,"%ld,%ld,%ld,%ld,%d,%d",
			from>>24&255,from>>16&255,from>>8&255,from&255,
			port>>8&255,port&255);

		/*
		 * Calculate required delta-offset to keep TCP happy
		 */
		
		diff = strlen(buf) - (data-p);
		
		/*
		 *	No shift.
		 */
		 
		if (diff==0) 
		{
			/*
			 * simple case, just replace the old PORT cmd
 			 */
 			memcpy(p,buf,strlen(buf));
 			return skb;
 		}
 
 		/*
 		 *	If the PORT command we have fiddled is the first, or is a
 		 *	resend don't do the delta shift again. Doesn't work for
 		 *	pathological cases, but we would need a history for that.
 		 *	Also fails if you send 2^31 bytes of data down the link 
 		 *	after the first port command.
 		 *
 		 *	FIXME: use ftp->init_seq_valid - 0 is a valid sequence.
 		 */
 		 
 		if(!ftp->init_seq || after(seq,ftp->init_seq) )
 		{
 			ftp->previous_delta=ftp->delta;
 			ftp->delta+=diff;
 			ftp->init_seq = seq;
 		}
 		
 		/*
 		 * Sizes differ, make a copy
 		 */
#ifdef DEBUG_MASQ
                printk("MASQUERADE: resizing needed for %d bytes (%ld)\n",diff, skb->len);
#endif
		skb2 = alloc_skb(MAX_HEADER + skb->len+diff, GFP_ATOMIC);
 		if (skb2 == NULL) {
 			printk("MASQUERADE: No memory available\n");
 			return skb;
 		}
 		skb2->free = skb->free;
 		skb_reserve(skb2,MAX_HEADER);
 		skb_put(skb2,skb->len + diff);
		skb2->h.raw = skb2->data + (skb->h.raw - skb->data);
		iph=skb2->h.iph;
		/*
		 *	Mend the IP header too
		 */
		iph->tot_len = htons(diff+ntohs(iph->tot_len));
		iph->check = 0;
		iph->check = ip_fast_csum((unsigned char *)iph, iph->ihl);
 
 		/*
 		 *	Copy the packet data into the new buffer.
 		 *	Thereby replacing the PORT cmd.
 		 */
 		memcpy(skb2->data, skb->data, (p - (char *)skb->data));
 		memcpy(&skb2->data[(p - (char *)skb->data)], buf, strlen(buf));
		memcpy(&skb2->data[(p - (char *)skb->data) + strlen(buf)], data,
			skb->len - (data-(char *)skb->data));

		/*
		 * Update tot_len field in ip header !
		 * Sequence numbers were allready modified in original packet
		 */
		iph->tot_len = htons(skb->len + diff);

		/*
		 * Problem, how to replace the new skb with old one,
		 * preferably inplace, so all the pointers in the
		 * calling tree keep ok :(
		 */
		kfree_skb(skb, FREE_WRITE);
		return skb2;
	}
	return skb;
}

static void recalc_check(struct udphdr *uh, unsigned long saddr,
	unsigned long daddr, int len)
{
	uh->check=0;
	uh->check=csum_tcpudp_magic(saddr,daddr,len,
		IPPROTO_UDP, csum_partial((char *)uh,len,0));
	if(uh->check==0)
		uh->check=0xFFFF;
}
	
void ip_fw_masquerade(struct sk_buff **skb_ptr, struct device *dev)
{
	struct sk_buff  *skb=*skb_ptr;
	struct iphdr	*iph = skb->h.iph;
	unsigned short	*portptr;
	struct ip_masq	*ms;
	int		size;

	/*
	 * We can only masquerade protocols with ports...
	 */

	if (iph->protocol!=IPPROTO_UDP && iph->protocol!=IPPROTO_TCP)
		return;
 
	/*
	 *	Now hunt the list to see if we have an old entry
	 */

	portptr = (unsigned short *)&(((char *)iph)[iph->ihl*4]);
	ms = ip_msq_hosts;

#ifdef DEBUG_MASQ
	printk("Outgoing %s %lX:%X -> %lX:%X\n",
		strProt[iph->protocol==IPPROTO_TCP],
		ntohl(iph->saddr), ntohs(portptr[0]),
		ntohl(iph->daddr), ntohs(portptr[1]));
#endif
	while (ms!=NULL) 
	{
		if (iph->protocol == ms->protocol &&
		    iph->saddr == ms->src   && iph->daddr == ms->dst &&
		    portptr[0] == ms->sport && portptr[1] == ms->dport) 
		{
			del_timer(&ms->timer);
			break;
 		}
		ms = ms->next;
	}

	/*
	 *	Nope, not found, create a new entry for it
	 */
	 
	if (ms==NULL) 
	{
		ms = alloc_masq_entry();
		if (ms==NULL) 
		{
			printk("MASQUERADE: no memory left !\n");
			return;
		}
		ms->protocol = iph->protocol;
		ms->src      = iph->saddr;
 		ms->dst      = iph->daddr;
 		ms->sport    = portptr[0];
 		ms->dport    = portptr[1];
 	}
 
 	/*
 	 *	Change the fragments origin
 	 */
 	 
 	size = skb->len - ((unsigned char *)portptr - skb->h.raw);
 	iph->saddr = dev->pa_addr; /* my own address */
 	portptr[0] = ms->mport;
 
 	/*
 	 *	Adjust packet accordingly to protocol
 	 */
 	 
 	if (iph->protocol==IPPROTO_UDP) 
 	{
 		ms->timer.expires = jiffies+MASQUERADE_EXPIRE_UDP;
 		recalc_check((struct udphdr *)portptr,iph->saddr,iph->daddr,size);
 	}
 	else 
 	{
 		struct tcphdr *th;
 		if (portptr[1]==htons(21)) 
 		{
 			skb = revamp(*skb_ptr, dev, ms);
 			*skb_ptr = skb;
 			iph = skb->h.iph;
 			portptr = (unsigned short *)&(((char *)iph)[iph->ihl*4]);
 			size = skb->len - ((unsigned char *)portptr-skb->h.raw);
 		}
 		th = (struct tcphdr *)portptr;
 
 		/*
 		 *	Timeout depends if FIN packet was seen
 		 */
 		if (ms->sawfin || th->fin) 
 		{
 			ms->timer.expires = jiffies+MASQUERADE_EXPIRE_TCP_FIN;
 			ms->sawfin = 1;
 		}
 		else ms->timer.expires = jiffies+MASQUERADE_EXPIRE_TCP;
 
 		tcp_send_check(th,iph->saddr,iph->daddr,size,skb->sk);
 	}
 	add_timer(&ms->timer);
 	ip_send_check(iph);
 
 #ifdef DEBUG_MASQ
 	printk("O-routed from %lX:%X over %s\n",ntohl(dev->pa_addr),ntohs(ms->mport),dev->name);
 #endif
 }
 
 /*
  *	Check if it's an masqueraded port, look it up,
  *	and send it on it's way...
  *
  *	Better not have many hosts using the designated portrange
  *	as 'normal' ports, or you'll be spending lots of time in
  *	this function.
  */

int ip_fw_demasquerade(struct sk_buff *skb_ptr)
{
 	struct iphdr	*iph = skb_ptr->h.iph;
 	unsigned short	*portptr;
 	struct ip_masq	*ms;
 	struct tcphdr   *th = (struct tcphdr *)(skb_ptr->h.raw+(iph->ihl<<2));
 
 	if (iph->protocol!=IPPROTO_UDP && iph->protocol!=IPPROTO_TCP)
 		return 0;
 
 	portptr = (unsigned short *)&(((char *)iph)[iph->ihl*4]);
 	if (ntohs(portptr[1]) < PORT_MASQ_BEGIN ||
 	    ntohs(portptr[1]) > PORT_MASQ_END)
 		return 0;
 
#ifdef DEBUG_MASQ
 	printk("Incoming %s %lX:%X -> %lX:%X\n",
 		strProt[iph->protocol==IPPROTO_TCP],
 		ntohl(iph->saddr), ntohs(portptr[0]),
 		ntohl(iph->daddr), ntohs(portptr[1]));
#endif

 	/*
 	 * reroute to original host:port if found...
 	 *
 	 * NB. Cannot check destination address, just for the incoming port.
 	 * reason: archie.doc.ac.uk has 6 interfaces, you send to
 	 * phoenix and get a reply from any other interface(==dst)!
 	 *
 	 * [Only for UDP] - AC
 	 */
 	ms = ip_msq_hosts;
 	while (ms!=NULL) 
 	{
 		if (iph->protocol==ms->protocol &&
		    (iph->saddr==ms->dst || iph->protocol==IPPROTO_UDP) && 
 		    (ms->dport==htons(FTP_DPORT_TBD) || portptr[0]==ms->dport) &&
 		    portptr[1]==ms->mport)
 		{
 		
 			int size = skb_ptr->len - ((unsigned char *)portptr - skb_ptr->h.raw);
 			iph->daddr = ms->src;
 			portptr[1] = ms->sport;
 			
 			if(ms->dport==htons(FTP_DPORT_TBD))
 			{
 				ms->dport=portptr[0];
#ifdef DEBUG_MASQ
	 			printk("demasq : Filled out dport entry (%d) based on initial connect attempt from FTP deamon\n",ntohs(ms->dport));
#endif
			}

 			/*
 			 * Yug! adjust UDP/TCP and IP checksums
 			 */
 			if (iph->protocol==IPPROTO_UDP)
 				recalc_check((struct udphdr *)portptr,iph->saddr,iph->daddr,size);
 			else
 			{
 				__u32 ack_seq;
 				/*
				 * Adjust ack_seq with delta-offset for
				 * the packets AFTER most recent PORT command has caused a shift
				 * for packets before most recent PORT command, use previous_delta
				 */
#ifdef DEBUG_MASQ
	 			printk("demasq : delta=%d ; previous_delta=%d ; init_seq=%lX ; ack_seq=%lX ; after=%d\n",ms->delta,ms->previous_delta,ntohl(ms->init_seq),ntohl(th->ack_seq),after(ntohl(th->ack_seq),ntohl(ms->init_seq)));
#endif
				ack_seq=ntohl(th->ack_seq);
				if (ms->delta || ms->previous_delta)
				{
					if(after(ack_seq,ms->init_seq))
					{
				 		th->ack_seq = htonl(ack_seq-ms->delta);
#ifdef DEBUG_MASQ
						printk("demasq : substracted delta (%d) from ack_seq\n",ms->delta);
#endif
					}
					else
					{
				 		th->ack_seq = htonl(ack_seq-ms->previous_delta);
#ifdef DEBUG_MASQ
						printk("demasq : substracted previous_delta (%d) from ack_seq\n",ms->previous_delta);
#endif
					}
				}
 				tcp_send_check((struct tcphdr *)portptr,iph->saddr,iph->daddr,size,skb_ptr->sk);
 			}
 			ip_send_check(iph);
#ifdef DEBUG_MASQ
 			printk("I-routed to %lX:%X\n",ntohl(iph->daddr),ntohs(portptr[1]));
#endif
 			return 1;
 		}
 		ms = ms->next;
 	}
 
 	/* sorry, all this trouble for a no-hit :) */
 	return 0;
}
#endif
  


static void zero_fw_chain(struct ip_fw *chainptr)
{
	struct ip_fw *ctmp=chainptr;
	while(ctmp) 
	{
		ctmp->fw_pcnt=0L;
		ctmp->fw_bcnt=0L;
		ctmp=ctmp->fw_next;
	}
}

static void free_fw_chain(struct ip_fw *volatile* chainptr)
{
	unsigned long flags;
	save_flags(flags);
	cli();
	while ( *chainptr != NULL ) 
	{
		struct ip_fw *ftmp;
		ftmp = *chainptr;
		*chainptr = ftmp->fw_next;
		kfree_s(ftmp,sizeof(*ftmp));
	}
	restore_flags(flags);
}

/* Volatiles to keep some of the compiler versions amused */

static int add_to_chain(struct ip_fw *volatile* chainptr, struct ip_fw *frwl)
{
	struct ip_fw *ftmp;
	struct ip_fw *chtmp=NULL;
	struct ip_fw *volatile chtmp_prev=NULL;
	unsigned long flags;
	unsigned long m_src_mask,m_dst_mask;
	unsigned long n_sa,n_da,o_sa,o_da,o_sm,o_dm,n_sm,n_dm;
	unsigned short n_sr,n_dr,o_sr,o_dr; 
	unsigned short oldkind,newkind;
	int addb4=0;
	int n_o,n_n;

	save_flags(flags);

	ftmp = kmalloc( sizeof(struct ip_fw), GFP_ATOMIC );
	if ( ftmp == NULL ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl:  malloc said no\n");
#endif
		return( ENOMEM );
	}

	memcpy(ftmp, frwl, sizeof( struct ip_fw ) );

	ftmp->fw_pcnt=0L;
	ftmp->fw_bcnt=0L;

	ftmp->fw_next = NULL;

	cli();

	if (*chainptr==NULL)
	{
		*chainptr=ftmp;
	}
	else
	{
		chtmp_prev=NULL;
		for (chtmp=*chainptr;chtmp!=NULL;chtmp=chtmp->fw_next) 
		{
			addb4=0;
			newkind=ftmp->fw_flg & IP_FW_F_KIND;
			oldkind=chtmp->fw_flg & IP_FW_F_KIND;
	
			if (newkind!=IP_FW_F_ALL 
				&&  oldkind!=IP_FW_F_ALL
				&&  oldkind!=newkind) 
			{
				chtmp_prev=chtmp;
				continue;
			}

			/*
			 *	Very very *UGLY* code...
			 *	Sorry,but i had to do this....
			 */

			n_sa=ntohl(ftmp->fw_src.s_addr);
			n_da=ntohl(ftmp->fw_dst.s_addr);
			n_sm=ntohl(ftmp->fw_smsk.s_addr);
			n_dm=ntohl(ftmp->fw_dmsk.s_addr);

			o_sa=ntohl(chtmp->fw_src.s_addr);
			o_da=ntohl(chtmp->fw_dst.s_addr);
			o_sm=ntohl(chtmp->fw_smsk.s_addr);
			o_dm=ntohl(chtmp->fw_dmsk.s_addr);

			m_src_mask = o_sm & n_sm;
			m_dst_mask = o_dm & n_dm;

			if ((o_sa & m_src_mask) == (n_sa & m_src_mask)) 
			{
				if (n_sm > o_sm) 
					addb4++;
				if (n_sm < o_sm) 
					addb4--;
			}

			if ((o_da & m_dst_mask) == (n_da & m_dst_mask)) 
			{
				if (n_dm > o_dm)
					addb4++;
				if (n_dm < o_dm)
					addb4--;
			}

			if (((o_da & o_dm) == (n_da & n_dm))
               			&&((o_sa & o_sm) == (n_sa & n_sm)))
			{
				if (newkind!=IP_FW_F_ALL &&
					oldkind==IP_FW_F_ALL)
					addb4++;
				if (newkind==oldkind && (oldkind==IP_FW_F_TCP
					||  oldkind==IP_FW_F_UDP)) 
				{

					/*
					 * 	Here the main idea is to check the size
					 * 	of port range which the frwl covers
					 * 	We actually don't check their values but
					 *	just the wideness of range they have
					 *	so that less wide ranges or single ports
					 *	go first and wide ranges go later. No ports
					 *	at all treated as a range of maximum number
					 *	of ports.
					 */

					if (ftmp->fw_flg & IP_FW_F_SRNG) 
						n_sr=ftmp->fw_pts[1]-ftmp->fw_pts[0];
					else 
						n_sr=(ftmp->fw_nsp)?
							ftmp->fw_nsp : 0xFFFF;
						
					if (chtmp->fw_flg & IP_FW_F_SRNG) 
						o_sr=chtmp->fw_pts[1]-chtmp->fw_pts[0];
					else 
						o_sr=(chtmp->fw_nsp)?chtmp->fw_nsp : 0xFFFF;

					if (n_sr<o_sr)
						addb4++;
					if (n_sr>o_sr)
						addb4--;
					
					n_n=ftmp->fw_nsp;
					n_o=chtmp->fw_nsp;
	
					/*
					 * Actually this cannot happen as the frwl control
					 * procedure checks for number of ports in source and
					 * destination range but we will try to be more safe.
					 */
					 
					if ((n_n>(IP_FW_MAX_PORTS-2)) ||
						(n_o>(IP_FW_MAX_PORTS-2)))
						goto skip_check;

					if (ftmp->fw_flg & IP_FW_F_DRNG) 
					       n_dr=ftmp->fw_pts[n_n+1]-ftmp->fw_pts[n_n];
					else 
					       n_dr=(ftmp->fw_ndp)? ftmp->fw_ndp : 0xFFFF;

					if (chtmp->fw_flg & IP_FW_F_DRNG) 
						o_dr=chtmp->fw_pts[n_o+1]-chtmp->fw_pts[n_o];
					else 
						o_dr=(chtmp->fw_ndp)? chtmp->fw_ndp : 0xFFFF;
					if (n_dr<o_dr)
						addb4++;
					if (n_dr>o_dr)
						addb4--;
skip_check:
				}
				/* finally look at the interface address */
				if ((addb4 == 0) && ftmp->fw_via.s_addr &&
						!(chtmp->fw_via.s_addr))
					addb4++;
			}
			if (addb4>0) 
			{
				if (chtmp_prev) 
				{
					chtmp_prev->fw_next=ftmp; 
					ftmp->fw_next=chtmp;
				} 
				else 
				{
					*chainptr=ftmp;
					ftmp->fw_next=chtmp;
				}
				restore_flags(flags);
				return 0;
			}
			chtmp_prev=chtmp;
		}
	}
	
	if (chtmp_prev)
		chtmp_prev->fw_next=ftmp;
	else
        	*chainptr=ftmp;
	restore_flags(flags);
	return(0);
}

static int del_from_chain(struct ip_fw *volatile*chainptr, struct ip_fw *frwl)
{
	struct ip_fw 	*ftmp,*ltmp;
	unsigned short	tport1,tport2,tmpnum;
	char		matches,was_found;
	unsigned long 	flags;

	save_flags(flags);
	cli();

	ftmp=*chainptr;

	if ( ftmp == NULL ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl:  chain is empty\n");
#endif
		restore_flags(flags);
		return( EINVAL );
	}

	ltmp=NULL;
	was_found=0;

	while( ftmp != NULL )
	{
		matches=1;
	     if (ftmp->fw_src.s_addr!=frwl->fw_src.s_addr 
		     ||  ftmp->fw_dst.s_addr!=frwl->fw_dst.s_addr
		     ||  ftmp->fw_smsk.s_addr!=frwl->fw_smsk.s_addr
		     ||  ftmp->fw_dmsk.s_addr!=frwl->fw_dmsk.s_addr
		     ||  ftmp->fw_via.s_addr!=frwl->fw_via.s_addr
		     ||  ftmp->fw_flg!=frwl->fw_flg)
        		matches=0;

		tport1=ftmp->fw_nsp+ftmp->fw_ndp;
		tport2=frwl->fw_nsp+frwl->fw_ndp;
		if (tport1!=tport2)
		        matches=0;
		else if (tport1!=0)
		{
			for (tmpnum=0;tmpnum < tport1 && tmpnum < IP_FW_MAX_PORTS;tmpnum++)
        		if (ftmp->fw_pts[tmpnum]!=frwl->fw_pts[tmpnum])
				matches=0;
		}
		if(matches)
		{
			was_found=1;
			if (ltmp)
			{
				ltmp->fw_next=ftmp->fw_next;
				kfree_s(ftmp,sizeof(*ftmp));
				ftmp=ltmp->fw_next;
        		}
      			else
      			{
      				*chainptr=ftmp->fw_next; 
	 			kfree_s(ftmp,sizeof(*ftmp));
				ftmp=*chainptr;
			}       
		}
		else
		{
			ltmp = ftmp;
			ftmp = ftmp->fw_next;
		 }
	}
	restore_flags(flags);
	if (was_found)
		return 0;
	else
		return(EINVAL);
}

#endif  /* CONFIG_IP_ACCT || CONFIG_IP_FIREWALL */

struct ip_fw *check_ipfw_struct(struct ip_fw *frwl, int len)
{

	if ( len != sizeof(struct ip_fw) )
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: len=%d, want %d\n",len, sizeof(struct ip_fw));
#endif
		return(NULL);
	}

	if ( (frwl->fw_flg & ~IP_FW_F_MASK) != 0 )
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: undefined flag bits set (flags=%x)\n",
			frwl->fw_flg);
#endif
		return(NULL);
	}

	if ( (frwl->fw_flg & IP_FW_F_SRNG) && frwl->fw_nsp < 2 ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: src range set but fw_nsp=%d\n",
			frwl->fw_nsp);
#endif
		return(NULL);
	}

	if ( (frwl->fw_flg & IP_FW_F_DRNG) && frwl->fw_ndp < 2 ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: dst range set but fw_ndp=%d\n",
			frwl->fw_ndp);
#endif
		return(NULL);
	}

	if ( frwl->fw_nsp + frwl->fw_ndp > IP_FW_MAX_PORTS ) 
	{
#ifdef DEBUG_CONFIG_IP_FIREWALL
		printk("ip_fw_ctl: too many ports (%d+%d)\n",
			frwl->fw_nsp,frwl->fw_ndp);
#endif
		return(NULL);
	}

	return frwl;
}




#ifdef CONFIG_IP_ACCT

#if 0
void ip_acct_cnt(struct iphdr *iph, struct device *dev, struct ip_fw *f)
{
	(void) ip_fw_chk(iph, dev, f, 0, 1);
	return;
}
#endif

int ip_acct_ctl(int stage, void *m, int len)
{
	if ( stage == IP_ACCT_FLUSH )
	{
		free_fw_chain(&ip_acct_chain);
		return(0);
	}  
	if ( stage == IP_ACCT_ZERO )
	{
		zero_fw_chain(ip_acct_chain);
		return(0);
	}
	if ( stage == IP_ACCT_ADD
	  || stage == IP_ACCT_DEL
	   )
	{
		struct ip_fw *frwl;

		if (!(frwl=check_ipfw_struct(m,len)))
			return (EINVAL);

		switch (stage) 
		{
			case IP_ACCT_ADD:
				return( add_to_chain(&ip_acct_chain,frwl));
		    	case IP_ACCT_DEL:
				return( del_from_chain(&ip_acct_chain,frwl));
			default:
				/*
 				 *	Should be panic but... (Why ??? - AC)
				 */
#ifdef DEBUG_CONFIG_IP_FIREWALL
				printk("ip_acct_ctl:  unknown request %d\n",stage);
#endif
				return(EINVAL);
		}
	}
#ifdef DEBUG_CONFIG_IP_FIREWALL
	printk("ip_acct_ctl:  unknown request %d\n",stage);
#endif
	return(EINVAL);
}
#endif

#ifdef CONFIG_IP_FIREWALL
int ip_fw_ctl(int stage, void *m, int len)
{
	int ret;

	if ( stage == IP_FW_FLUSH_BLK )
	{
		free_fw_chain(&ip_fw_blk_chain);
		return(0);
	}  

	if ( stage == IP_FW_FLUSH_FWD )
	{
		free_fw_chain(&ip_fw_fwd_chain);
		return(0);
	}  

	if ( stage == IP_FW_ZERO_BLK )
	{
		zero_fw_chain(ip_fw_blk_chain);
		return(0);
	}  

	if ( stage == IP_FW_ZERO_FWD )
	{
		zero_fw_chain(ip_fw_fwd_chain);
		return(0);
	}  

	if ( stage == IP_FW_POLICY_BLK || stage == IP_FW_POLICY_FWD )
	{
		int *tmp_policy_ptr;
		tmp_policy_ptr=(int *)m;
		if ( stage == IP_FW_POLICY_BLK )
			ip_fw_blk_policy=*tmp_policy_ptr;
		else
			ip_fw_fwd_policy=*tmp_policy_ptr;
		return 0;
	}

	if ( stage == IP_FW_CHK_BLK || stage == IP_FW_CHK_FWD )
	{
		struct device viadev;
		struct ip_fwpkt *ipfwp;
		struct iphdr *ip;

		if ( len < sizeof(struct ip_fwpkt) )
		{
#ifdef DEBUG_CONFIG_IP_FIREWALL
			printk("ip_fw_ctl: length=%d, expected %d\n",
				len, sizeof(struct ip_fwpkt));
#endif
			return( EINVAL );
		}

	 	ipfwp = (struct ip_fwpkt *)m;
	 	ip = &(ipfwp->fwp_iph);

		if ( ip->ihl != sizeof(struct iphdr) / sizeof(int))
		{
#ifdef DEBUG_CONFIG_IP_FIREWALL
			printk("ip_fw_ctl: ip->ihl=%d, want %d\n",ip->ihl,
					sizeof(struct iphdr)/sizeof(int));
#endif
			return(EINVAL);
		}

		viadev.pa_addr = ipfwp->fwp_via.s_addr;

		if ((ret = ip_fw_chk(ip, &viadev,
			stage == IP_FW_CHK_BLK ?
	                ip_fw_blk_chain : ip_fw_fwd_chain,
			stage == IP_FW_CHK_BLK ?
	                ip_fw_blk_policy : ip_fw_fwd_policy, 2 )) > 0
		   )
			return(0);
	    	else if (ret == -1)	
			return(ECONNREFUSED);
		else
			return(ETIMEDOUT);
	}

/*
 *	Here we really working hard-adding new elements
 *	to blocking/forwarding chains or deleting 'em
 */

	if ( stage == IP_FW_ADD_BLK || stage == IP_FW_ADD_FWD
		|| stage == IP_FW_DEL_BLK || stage == IP_FW_DEL_FWD
		)
	{
		struct ip_fw *frwl;
		frwl=check_ipfw_struct(m,len);
		if (frwl==NULL)
			return (EINVAL);
		
		switch (stage) 
		{
			case IP_FW_ADD_BLK:
				return(add_to_chain(&ip_fw_blk_chain,frwl));
			case IP_FW_ADD_FWD:
				return(add_to_chain(&ip_fw_fwd_chain,frwl));
			case IP_FW_DEL_BLK:
				return(del_from_chain(&ip_fw_blk_chain,frwl));
			case IP_FW_DEL_FWD: 
				return(del_from_chain(&ip_fw_fwd_chain,frwl));
			default:
			/*
	 		 *	Should be panic but... (Why are BSD people panic obsessed ??)
			 */
#ifdef DEBUG_CONFIG_IP_FIREWALL
				printk("ip_fw_ctl:  unknown request %d\n",stage);
#endif
				return(EINVAL);
		}
	} 

#ifdef DEBUG_CONFIG_IP_FIREWALL
	printk("ip_fw_ctl:  unknown request %d\n",stage);
#endif
	return(EINVAL);
}
#endif /* CONFIG_IP_FIREWALL */

#if defined(CONFIG_IP_FIREWALL) || defined(CONFIG_IP_ACCT)

static int ip_chain_procinfo(int stage, char *buffer, char **start,
			     off_t offset, int length, int reset)
{
	off_t pos=0, begin=0;
	struct ip_fw *i;
	unsigned long flags;
	int len, p;
	

	switch(stage)
	{
#ifdef CONFIG_IP_FIREWALL
		case IP_INFO_BLK:
			i = ip_fw_blk_chain;
			len=sprintf(buffer, "IP firewall block rules, default %d\n",
				ip_fw_blk_policy);
			break;
		case IP_INFO_FWD:
			i = ip_fw_fwd_chain;
			len=sprintf(buffer, "IP firewall forward rules, default %d\n",
				ip_fw_fwd_policy);
			break;
#endif
#ifdef CONFIG_IP_ACCT
		case IP_INFO_ACCT:
			i = ip_acct_chain;
			len=sprintf(buffer,"IP accounting rules\n");
			break;
#endif
		default:
			/* this should never be reached, but safety first... */
			i = NULL;
			len=0;
			break;
	}

	save_flags(flags);
	cli();
	
	while(i!=NULL)
	{
		len+=sprintf(buffer+len,"%08lX/%08lX->%08lX/%08lX %08lX %X ",
			ntohl(i->fw_src.s_addr),ntohl(i->fw_smsk.s_addr),
			ntohl(i->fw_dst.s_addr),ntohl(i->fw_dmsk.s_addr),
			ntohl(i->fw_via.s_addr),i->fw_flg);
		len+=sprintf(buffer+len,"%u %u %-9lu %-9lu",
			i->fw_nsp,i->fw_ndp, i->fw_pcnt,i->fw_bcnt);
		for (p = 0; p < IP_FW_MAX_PORTS; p++)
			len+=sprintf(buffer+len, " %u", i->fw_pts[p]);
		buffer[len++]='\n';
		buffer[len]='\0';
		pos=begin+len;
		if(pos<offset)
		{
			len=0;
			begin=pos;
		}
		else if(reset)
		{
			/* This needs to be done at this specific place! */
			i->fw_pcnt=0L;
			i->fw_bcnt=0L;
		}
		if(pos>offset+length)
			break;
		i=i->fw_next;
	}
	restore_flags(flags);
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;	
	return len;
}
#endif

#ifdef CONFIG_IP_ACCT

static int ip_acct_procinfo(char *buffer, char **start, off_t offset,
			    int length, int reset)
{
	return ip_chain_procinfo(IP_INFO_ACCT, buffer,start, offset,length,
				 reset);
}

#endif

#ifdef CONFIG_IP_FIREWALL

static int ip_fw_blk_procinfo(char *buffer, char **start, off_t offset,
			      int length, int reset)
{
	return ip_chain_procinfo(IP_INFO_BLK, buffer,start,offset,length,
				 reset);
}

static int ip_fw_fwd_procinfo(char *buffer, char **start, off_t offset,
			      int length, int reset)
{
	return ip_chain_procinfo(IP_INFO_FWD, buffer,start,offset,length,
				 reset);
}
#endif

#ifdef CONFIG_IP_MASQUERADE

static int ip_msqhst_procinfo(char *buffer, char **start, off_t offset,
			      int length, int unused)
{
	off_t pos=0, begin=0;
	struct ip_masq *ms;
	unsigned long flags;
	int len=0;
	
	len=sprintf(buffer,"Prc FromIP   FPrt ToIP     TPrt Masq Init-seq Delta PDelta Expires\n"); 
	save_flags(flags);
	cli();
	
	ms=ip_msq_hosts;
	while (ms!=NULL) 
	{
		int timer_active = del_timer(&ms->timer);
		if (!timer_active)
			ms->timer.expires = jiffies;
		len+=sprintf(buffer+len,"%s %08lX:%04X %08lX:%04X %04X %08X %5d %5d %lu\n",
			strProt[ms->protocol==IPPROTO_TCP],
			ntohl(ms->src),ntohs(ms->sport),
			ntohl(ms->dst),ntohs(ms->dport),
			ntohs(ms->mport),
			ms->init_seq,ms->delta,ms->previous_delta,ms->timer.expires-jiffies);
		if (timer_active)
			add_timer(&ms->timer);

		pos=begin+len;
		if(pos<offset) 
		{
 			len=0;
			begin=pos;
		}
		if(pos>offset+length)
			break;
		ms=ms->next;
	}
	restore_flags(flags);
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;
	return len;
}
  
#endif

void ip_fw_init(void)
{
#ifdef CONFIG_IP_ACCT
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPACCT, 7, "ip_acct",
		S_IFREG | S_IRUGO | S_IWUSR, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_acct_procinfo
	});
#endif
#ifdef CONFIG_IP_FIREWALL
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPFWBLK, 8, "ip_block",
		S_IFREG | S_IRUGO | S_IWUSR, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_fw_blk_procinfo
	});
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPFWFWD, 10, "ip_forward",
		S_IFREG | S_IRUGO | S_IWUSR, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_fw_fwd_procinfo
	});
#endif
#ifdef CONFIG_IP_MASQUERADE
	proc_net_register(&(struct proc_dir_entry) {
		PROC_NET_IPMSQHST, 13, "ip_masquerade",
		S_IFREG | S_IRUGO, 1, 0, 0,
		0, &proc_net_inode_operations,
		ip_msqhst_procinfo
	});
#endif
}
