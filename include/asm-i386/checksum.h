#ifndef _I386_CHECKSUM_H
#define _I386_CHECKSUM_H

#include <asm/uaccess.h>

/*
 * computes the checksum of a memory block at buff, length len,
 * and adds in "sum" (32-bit)
 *
 * returns a 32-bit number suitable for feeding into itself
 * or csum_tcpudp_magic
 *
 * this function must be called with even lengths, except
 * for the last fragment, which may be odd
 *
 * it's best to have buff aligned on a 32-bit boundary
 */

extern
unsigned int csum_partial (const unsigned char * buff, int len, unsigned int sum);

/*
 * the same as csum_partial, but copies from src while it
 * checksums, and handles user-space pointer exceptions correctly, when needed.
 *
 * here even more important to align src and dst on a 32-bit (or even
 * better 64-bit) boundary
 */

unsigned int __csum_partial_copy_i386_generic( const char *src, char *dst, int len, int sum,
					int *src_err_ptr, int *dst_err_ptr);

extern __inline__
unsigned int csum_partial_copy_nocheck ( const char *src, char *dst,
					int len, int sum)
{
	int *src_err_ptr=NULL, *dst_err_ptr=NULL;

	return __csum_partial_copy_i386_generic ( src, dst, len, sum, src_err_ptr, dst_err_ptr);
}

extern __inline__
unsigned int csum_partial_copy_from_user ( const char *src, char *dst,
						int len, int sum, int *err_ptr)
{
	int *dst_err_ptr=NULL;

/*
 * If the source address is invalid, force an exception via NULL pointer.
 * The point of this solution is to make the code smaller. The exception path
 * doesnt have to be fast.
 */
	if (!access_ok(VERIFY_READ, src, len))
		src = NULL;

	return __csum_partial_copy_i386_generic ( src, dst, len, sum, err_ptr, dst_err_ptr);
}

/*
 * This combination is currently not used, but possible:
 */

extern __inline__
unsigned int csum_partial_copy_to_user ( const char *src, char *dst,
					int len, int sum, int *err_ptr)
{
	int *src_err_ptr=NULL;

	if (!access_ok(VERIFY_WRITE, dst, len))
		dst = NULL;

	return __csum_partial_copy_i386_generic ( src, dst, len, sum, src_err_ptr, err_ptr);
}

/*
 * These are the old (and unsafe) way of doing checksums, a warning message will be
 * printed if they are used and an exeption occurs.
 *
 * these functions should go away after some time.
 */

#define csum_partial_copy_fromuser csum_partial_copy
unsigned int csum_partial_copy( const char *src, char *dst, int len, int sum);

/*
 *	This is a version of ip_compute_csum() optimized for IP headers,
 *	which always checksum on 4 octet boundaries.
 *
 *	By Jorge Cwik <jorge@laser.satlink.net>, adapted for linux by
 *	Arnt Gulbrandsen.
 */
static inline unsigned short ip_fast_csum(unsigned char * iph,
					  unsigned int ihl) {
	unsigned int sum;

	__asm__ __volatile__("
	    movl (%1), %0
	    subl $4, %2
	    jbe 2f
	    addl 4(%1), %0
	    adcl 8(%1), %0
	    adcl 12(%1), %0
1:	    adcl 16(%1), %0
	    lea 4(%1), %1
	    decl %2
	    jne	1b
	    adcl $0, %0
	    movl %0, %2
	    shrl $16, %0
	    addw %w2, %w0
	    adcl $0, %0
	    notl %0
2:
	    "
	/* Since the input registers which are loaded with iph and ipl
	   are modified, we must also specify them as outputs, or gcc
	   will assume they contain their original values. */
	: "=r" (sum), "=r" (iph), "=r" (ihl)
	: "1" (iph), "2" (ihl));
	return(sum);
}

/*
 *	Fold a partial checksum
 */

static inline unsigned int csum_fold(unsigned int sum)
{
	__asm__("
		addl %1, %0
		adcl $0xffff, %0
		"
		: "=r" (sum)
		: "r" (sum << 16), "0" (sum & 0xffff0000)
	);
	return (~sum) >> 16;
}
 
/*
 * computes the checksum of the TCP/UDP pseudo-header
 * returns a 16-bit checksum, already complemented
 */

static inline unsigned short int csum_tcpudp_magic(unsigned long saddr,
						   unsigned long daddr,
						   unsigned short len,
						   unsigned short proto,
						   unsigned int sum) {
    __asm__("
	addl %1, %0
	adcl %2, %0
	adcl %3, %0
	adcl $0, %0
	"
	: "=r" (sum)
	: "g" (daddr), "g"(saddr), "g"((ntohs(len)<<16)+proto*256), "0"(sum));
	return csum_fold(sum);
}
/*
 * this routine is used for miscellaneous IP-like checksums, mainly
 * in icmp.c
 */

static inline unsigned short ip_compute_csum(unsigned char * buff, int len) {
    return csum_fold (csum_partial(buff, len, 0));
}

#define _HAVE_ARCH_IPV6_CSUM
static __inline__ unsigned short int csum_ipv6_magic(struct in6_addr *saddr,
						     struct in6_addr *daddr,
						     __u16 len,
						     unsigned short proto,
						     unsigned int sum) 
{
	__asm__("
		addl 0(%1), %0
		adcl 4(%1), %0
		adcl 8(%1), %0
		adcl 12(%1), %0
		adcl 0(%2), %0
		adcl 4(%2), %0
		adcl 8(%2), %0
		adcl 12(%2), %0
		adcl %3, %0
		adcl %4, %0
		adcl $0, %0
		"
		: "=&r" (sum)
		: "r" (saddr), "r" (daddr), 
		  "r"(htonl((__u32) (len))), "r"(htonl(proto)), "0"(sum));

	return csum_fold(sum);
}

#endif
