/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Definitions for the TCP protocol.
 *
 * Version:	@(#)tcp.h	1.0.2	04/28/93
 *
 * Author:	Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#ifndef _LINUX_TCP_H
#define _LINUX_TCP_H

#include <asm/types.h>

#define HEADER_SIZE	64		/* maximum header size		*/


struct tcphdr {
	u16	source;
	u16	dest;
	u32	seq;
	u32	ack_seq;
#if defined(__i386__)
	u16	res1:4,
		doff:4,
		fin:1,
		syn:1,
		rst:1,
		psh:1,
		ack:1,
		urg:1,
		res2:2;
#elif defined(__mc68000__)
	u16	res2:2,
		urg:1,
		ack:1,
		psh:1,
		rst:1,
		syn:1,
		fin:1,
		doff:4,
		res1:4;
#elif defined(__alpha__)
	u16	res1:4,
		doff:4,
		fin:1,
		syn:1,
		rst:1,
		psh:1,
		ack:1,
		urg:1,
		res2:2;
#else
#error	"Adjust this structure for your cpu alignment rules"
#endif	
	u16	window;
	u16	check;
	u16	urg_ptr;
};


enum {
  TCP_ESTABLISHED = 1,
  TCP_SYN_SENT,
  TCP_SYN_RECV,
  TCP_FIN_WAIT1,
  TCP_FIN_WAIT2,
  TCP_TIME_WAIT,
  TCP_CLOSE,
  TCP_CLOSE_WAIT,
  TCP_LAST_ACK,
  TCP_LISTEN,
  TCP_CLOSING	/* now a valid state */
};

#endif	/* _LINUX_TCP_H */
