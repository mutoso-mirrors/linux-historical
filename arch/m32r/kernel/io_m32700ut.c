/*
 *  linux/arch/m32r/kernel/io_mappi.c
 *
 *  Typical I/O routines for M32700UT board.
 *
 *  Copyright (c) 2001, 2002  Hiroyuki Kondo, Hirokazu Takata,
 *                            Hitoshi Yamamoto, Takeo Takahashi
 *
 *  This file is subject to the terms and conditions of the GNU General
 *  Public License.  See the file "COPYING" in the main directory of this
 *  archive for more details.
 *
 */

#include <linux/config.h>
#include <asm/m32r.h>
#include <asm/page.h>
#include <asm/io.h>
#include <asm/byteorder.h>

#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
#include <linux/types.h>

#define M32R_PCC_IOMAP_SIZE 0x1000

#define M32R_PCC_IOSTART0 0x1000
#define M32R_PCC_IOEND0   (M32R_PCC_IOSTART0 + M32R_PCC_IOMAP_SIZE - 1)

extern void pcc_ioread_byte(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_ioread_word(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_iowrite_byte(int, unsigned long, void *, size_t, size_t, int);
extern void pcc_iowrite_word(int, unsigned long, void *, size_t, size_t, int);
#endif /* CONFIG_PCMCIA && CONFIG_M32R_CFC */

#define PORT2ADDR(port)  _port2addr(port)
#define PORT2ADDR_USB(port) _port2addr_usb(port)

static __inline__ void *_port2addr(unsigned long port)
{
	return (void *)(port + NONCACHE_OFFSET);
}

/*
 * M32700UT-LAN is located in the extended bus space
 * from 0x10000000 to 0x13ffffff on physical address.
 * The base address of LAN controller(LAN91C111) is 0x300.
 */
#define LAN_IOSTART	0x300
#define LAN_IOEND	0x320
static __inline__ void *_port2addr_ne(unsigned long port)
{
	return (void *)(port + NONCACHE_OFFSET + 0x10000000);
}
static __inline__ void *_port2addr_usb(unsigned long port)
{
  return (void *)((port & 0x0f) + NONCACHE_OFFSET + 0x10303000);
}

static __inline__ void delay(void)
{
	__asm__ __volatile__ ("push r0; \n\t pop r0;" : : :"memory");
}

/*
 * NIC I/O function
 */

#define PORT2ADDR_NE(port)  _port2addr_ne(port)

static __inline__ unsigned char _ne_inb(void *portp)
{
	return *(volatile unsigned char *)portp;
}

static __inline__ unsigned short _ne_inw(void *portp)
{
	return (unsigned short)le16_to_cpu(*(volatile unsigned short *)portp);
}

static __inline__ void _ne_insb(void *portp, void * addr, unsigned long count)
{
	unsigned char *buf = (unsigned char *)addr;

	while (count--) *buf++ = _ne_inb(portp);
}

static __inline__ void _ne_outb(unsigned char b, void *portp)
{
	*(volatile unsigned char *)portp = b;
}

static __inline__ void _ne_outw(unsigned short w, void *portp)
{
	*(volatile unsigned short *)portp = cpu_to_le16(w);
}

unsigned char _inb(unsigned long port)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		return _ne_inb(PORT2ADDR_NE(port));

#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   unsigned char b;
	   pcc_ioread_byte(0, port, &b, sizeof(b), 1, 0);
	   return b;
	} else
#endif

	return *(volatile unsigned char *)PORT2ADDR(port);
}

unsigned short _inw(unsigned long port)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		return _ne_inw(PORT2ADDR_NE(port));
#if defined(CONFIG_USB)
	else if(port >= 0x340 && port < 0x3a0)
	  return *(volatile unsigned short *)PORT2ADDR_USB(port);
#endif

#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	  else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   unsigned short w;
	   pcc_ioread_word(0, port, &w, sizeof(w), 1, 0);
	   return w;
	} else
#endif
	return *(volatile unsigned short *)PORT2ADDR(port);
}

unsigned long _inl(unsigned long port)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   unsigned long l;
	   pcc_ioread_word(0, port, &l, sizeof(l), 1, 0);
	   return l;
	} else
#endif
	return *(volatile unsigned long *)PORT2ADDR(port);
}

unsigned char _inb_p(unsigned long port)
{
	unsigned char  v;

	if (port >= LAN_IOSTART && port < LAN_IOEND)
		v = _ne_inb(PORT2ADDR_NE(port));
	else
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   unsigned char b;
	   pcc_ioread_byte(0, port, &b, sizeof(b), 1, 0);
	   return b;
	} else
#endif
		v = *(volatile unsigned char *)PORT2ADDR(port);

	delay();
	return (v);
}

unsigned short _inw_p(unsigned long port)
{
	unsigned short  v;

	if (port >= LAN_IOSTART && port < LAN_IOEND)
		v = _ne_inw(PORT2ADDR_NE(port));
	else
#if defined(CONFIG_USB)
	  if(port >= 0x340 && port < 0x3a0)
	    return *(volatile unsigned short *)PORT2ADDR_USB(port);
	else
#endif

#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   unsigned short w;
	   pcc_ioread_word(0, port, &w, sizeof(w), 1, 0);
	   return w;
	} else
#endif
		v = *(volatile unsigned short *)PORT2ADDR(port);

	delay();
	return (v);
}

unsigned long _inl_p(unsigned long port)
{
	unsigned long  v;

	v = *(volatile unsigned long *)PORT2ADDR(port);
	delay();
	return (v);
}

void _outb(unsigned char b, unsigned long port)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		_ne_outb(b, PORT2ADDR_NE(port));
	else
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite_byte(0, port, &b, sizeof(b), 1, 0);
	} else
#endif
		*(volatile unsigned char *)PORT2ADDR(port) = b;
}

void _outw(unsigned short w, unsigned long port)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		_ne_outw(w, PORT2ADDR_NE(port));
	else
#if defined(CONFIG_USB)
	if(port >= 0x340 && port < 0x3a0)
	  *(volatile unsigned short *)PORT2ADDR_USB(port) = w;
	else
#endif

#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite_word(0, port, &w, sizeof(w), 1, 0);
	} else
#endif
		*(volatile unsigned short *)PORT2ADDR(port) = w;
}

void _outl(unsigned long l, unsigned long port)
{
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite_word(0, port, &l, sizeof(l), 1, 0);
	} else
#endif
	*(volatile unsigned long *)PORT2ADDR(port) = l;
}

void _outb_p(unsigned char b, unsigned long port)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		_ne_outb(b, PORT2ADDR_NE(port));
	else
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite_byte(0, port, &b, sizeof(b), 1, 0);
	} else
#endif
		*(volatile unsigned char *)PORT2ADDR(port) = b;

	delay();
}

void _outw_p(unsigned short w, unsigned long port)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		_ne_outw(w, PORT2ADDR_NE(port));
	else
#if defined(CONFIG_USB)
	  if(port >= 0x340 && port < 0x3a0)
	    *(volatile unsigned short *)PORT2ADDR_USB(port) = w;
	else
#endif

#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite_word(0, port, &w, sizeof(w), 1, 0);
	} else
#endif
		*(volatile unsigned short *)PORT2ADDR(port) = w;

	delay();
}

void _outl_p(unsigned long l, unsigned long port)
{
	*(volatile unsigned long *)PORT2ADDR(port) = l;
	delay();
}

void _insb(unsigned int port, void * addr, unsigned long count)
{
	if (port >= LAN_IOSTART && port < LAN_IOEND)
		_ne_insb(PORT2ADDR_NE(port), addr, count);
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	  else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_ioread_byte(0, port, (void *)addr, sizeof(unsigned char), count, 1);
	}
#endif
	else {
		unsigned char *buf = addr;
		unsigned char *portp = PORT2ADDR(port);
		while(count--) *buf++ = *(volatile unsigned char *)portp;
	}
}

void _insw(unsigned int port, void * addr, unsigned long count)
{
	unsigned short *buf = addr;
	unsigned short *portp;

	if (port >= LAN_IOSTART && port < LAN_IOEND) {
		/*
		 * This portion is only used by smc91111.c to read data
		 * from the DATA_REG. Do not swap the data.
		 */
		portp = PORT2ADDR_NE(port);
		while (count--) *buf++ = *(volatile unsigned short *)portp;
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	} else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_ioread_word(9, port, (void *)addr, sizeof(unsigned short), count, 1);
#endif
	} else {
		portp = PORT2ADDR(port);
		while (count--) *buf++ = *(volatile unsigned short *)portp;
	}
}

void _insl(unsigned int port, void * addr, unsigned long count)
{
	unsigned long *buf = addr;
	unsigned long *portp;

	portp = PORT2ADDR(port);
	while (count--) *buf++ = *(volatile unsigned long *)portp;
}

void _outsb(unsigned int port, const void * addr, unsigned long count)
{
	const unsigned char *buf = addr;
	unsigned char *portp;

	if (port >= LAN_IOSTART && port < LAN_IOEND) {
		portp = PORT2ADDR_NE(port);
		while (count--) _ne_outb(*buf++, portp);
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	} else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite_byte(0, port, (void *)addr, sizeof(unsigned char), count, 1);
#endif
	} else {
		portp = PORT2ADDR(port);
		while(count--) *(volatile unsigned char *)portp = *buf++;
	}
}

void _outsw(unsigned int port, const void * addr, unsigned long count)
{
	const unsigned short *buf = addr;
	unsigned short *portp;

	if (port >= LAN_IOSTART && port < LAN_IOEND) {
		/*
		 * This portion is only used by smc91111.c to write data
		 * into the DATA_REG. Do not swap the data.
		 */
		portp = PORT2ADDR_NE(port);
		while(count--) *(volatile unsigned short *)portp = *buf++;
#if defined(CONFIG_PCMCIA) && defined(CONFIG_M32R_CFC)
	} else if (port >= M32R_PCC_IOSTART0 && port <= M32R_PCC_IOEND0) {
	   pcc_iowrite_word(9, port, (void *)addr, sizeof(unsigned short), count, 1);
#endif
	} else {
		portp = PORT2ADDR(port);
		while(count--) *(volatile unsigned short *)portp = *buf++;
	}
}

void _outsl(unsigned int port, const void * addr, unsigned long count)
{
	const unsigned long *buf = addr;
	unsigned char *portp;

	portp = PORT2ADDR(port);
	while(count--) *(volatile unsigned long *)portp = *buf++;
}
