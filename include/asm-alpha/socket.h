#ifndef _ASM_SOCKET_H
#define _ASM_SOCKET_H

#include <asm/sockios.h>

/* For setsockoptions(2) */
/*
 * Note: we only bother about making the SOL_SOCKET options
 * same as OSF/1, as that's all that "normal" programs are
 * likely to set.  We don't necessarily want to be binary
 * compatible with _everything_. 
 */
#define SOL_SOCKET	0xffff

#define SO_DEBUG	0x0001
#define SO_REUSEADDR	0x0004
#define SO_KEEPALIVE	0x0008
#define SO_DONTROUTE	0x0010
#define SO_BROADCAST	0x0020
#define SO_LINGER	0x0080
#define SO_OOBINLINE	0x0100
/* To add :#define SO_REUSEPORT 0x0200 */

#define SO_TYPE		0x1008
#define SO_ERROR	0x1007
#define SO_SNDBUF	0x1001
#define SO_RCVBUF	0x1002
#define	SO_RCVLOWAT	0x1010
#define	SO_SNDLOWAT	0x1011
#define	SO_RCVTIMEO	0x1012
#define	SO_SNDTIMEO	0x1013

/* linux-specific, might as well be the same as on i386 */
#define SO_NO_CHECK	11
#define SO_PRIORITY	12
#define SO_BSDCOMPAT	14

#define SO_PASSCRED	17
#define SO_PEERCRED	18

/* Security levels - as per NRL IPv6 - don't actually do anything */
#define SO_SECURITY_AUTHENTICATION		19
#define SO_SECURITY_ENCRYPTION_TRANSPORT	20
#define SO_SECURITY_ENCRYPTION_NETWORK		21

#endif /* _ASM_SOCKET_H */
