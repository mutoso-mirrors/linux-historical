#ifndef __ASM_MIPS_IO_H
#define __ASM_MIPS_IO_H

/*
 * Slowdown I/O port space accesses for antique hardware.
 */
#undef CONF_SLOWDOWN_IO

#include <asm/mipsconfig.h>
#include <asm/addrspace.h>

/*
 * This file contains the definitions for the MIPS counterpart of the
 * x86 in/out instructions. This heap of macros and C results in much
 * better code than the approach of doing it in plain C.  The macros
 * result in code that is to fast for certain hardware.  On the other
 * side the performance of the string functions should be improved for
 * sake of certain devices like EIDE disks that do highspeed polled I/O.
 *
 *   Ralf
 *
 * This file contains the definitions for the x86 IO instructions
 * inb/inw/inl/outb/outw/outl and the "string versions" of the same
 * (insb/insw/insl/outsb/outsw/outsl). You can also use "pausing"
 * versions of the single-IO instructions (inb_p/inw_p/..).
 *
 * This file is not meant to be obfuscating: it's just complicated
 * to (a) handle it all in a way that makes gcc able to optimize it
 * as well as possible and (b) trying to avoid writing the same thing
 * over and over again with slight variations and possibly making a
 * mistake somewhere.
 */

/*
 * On MIPS I/O ports are memory mapped, so we access them using normal
 * load/store instructions. mips_io_port_base is the virtual address to
 * which all ports are being mapped.  For sake of efficiency some code
 * assumes that this is an address that can be loaded with a single lui
 * instruction, so the lower 16 bits must be zero.  Should be true on
 * on any sane architecture; generic code does not use this assumption.
 */
extern unsigned long mips_io_port_base;

/*
 * Thanks to James van Artsdalen for a better timing-fix than
 * the two short jumps: using outb's to a nonexistent port seems
 * to guarantee better timings even on fast machines.
 *
 * On the other hand, I'd like to be sure of a non-existent port:
 * I feel a bit unsafe about using 0x80 (should be safe, though)
 *
 *		Linus
 *
 */

#define __SLOW_DOWN_IO \
	__asm__ __volatile__( \
		"sb\t$0,0x80(%0)" \
		: : "r" (mips_io_port_base));

#ifdef CONF_SLOWDOWN_IO
#ifdef REALLY_SLOW_IO
#define SLOW_DOWN_IO { __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; __SLOW_DOWN_IO; }
#else
#define SLOW_DOWN_IO __SLOW_DOWN_IO
#endif
#else
#define SLOW_DOWN_IO
#endif

/*
 * Change virtual addresses to physical addresses and vv.
 * These are trivial on the 1:1 Linux/MIPS mapping
 */
extern inline unsigned long virt_to_phys(volatile void * address)
{
	return PHYSADDR(address);
}

extern inline void * phys_to_virt(unsigned long address)
{
	return (void *)KSEG0ADDR(address);
}

extern void * ioremap(unsigned long phys_addr, unsigned long size);
extern void iounmap(void *addr);

/*
 * IO bus memory addresses are also 1:1 with the physical address
 */
extern inline unsigned long virt_to_bus(volatile void * address)
{
	return PHYSADDR(address);
}

extern inline void * bus_to_virt(unsigned long address)
{
	return (void *)KSEG0ADDR(address);
}

/*
 * isa_slot_offset is the address where E(ISA) busaddress 0 is is mapped
 * for the processor.
 */
extern unsigned long isa_slot_offset;

/*
 * readX/writeX() are used to access memory mapped devices. On some
 * architectures the memory mapped IO stuff needs to be accessed
 * differently. On the x86 architecture, we just read/write the
 * memory location directly.
 *
 * On MIPS, we have the whole physical address space mapped at all
 * times, so "ioremap()" and "iounmap()" do not need to do anything.
 * (This isn't true for all machines but we still handle these cases
 * with wired TLB entries anyway ...)
 *
 * We cheat a bit and always return uncachable areas until we've fixed
 * the drivers to handle caching properly.
 */
extern inline void * ioremap(unsigned long offset, unsigned long size)
{
	return (void *) KSEG1ADDR(offset);
}

/*
 * This one maps high address device memory and turns off caching for that area.
 * it's useful if some control registers are in such an area and write combining
 * or read caching is not desirable:
 */
extern inline void * ioremap_nocache (unsigned long offset, unsigned long size)
{
	return (void *) KSEG1ADDR(offset);
}

extern inline void iounmap(void *addr)
{
}

/*
 * XXX We need system specific versions of these to handle EISA address bits
 * 24-31 on SNI.
 * XXX more SNI hacks.
 */
#define readb(addr) (*(volatile unsigned char *) (0xa0000000 + (unsigned long)(addr)))
#define readw(addr) (*(volatile unsigned short *) (0xa0000000 + (unsigned long)(addr)))
#define readl(addr) (*(volatile unsigned int *) (0xa0000000 + (unsigned long)(addr)))

#define writeb(b,addr) (*(volatile unsigned char *) (0xa0000000 + (unsigned long)(addr)) = (b))
#define writew(b,addr) (*(volatile unsigned short *) (0xa0000000 + (unsigned long)(addr)) = (b))
#define writel(b,addr) (*(volatile unsigned int *) (0xa0000000 + (unsigned long)(addr)) = (b))

#define memset_io(a,b,c)	memset((void *)(0xa0000000 + (unsigned long)a),(b),(c))
#define memcpy_fromio(a,b,c)	memcpy((a),(void *)(0xa0000000 + (unsigned long)(b)),(c))
#define memcpy_toio(a,b,c)	memcpy((void *)(0xa0000000 + (unsigned long)(a)),(b),(c))

/* END SNI HACKS ... */

/*
 * We don't have csum_partial_copy_fromio() yet, so we cheat here and
 * just copy it. The net code will then do the checksum later.
 */
#define eth_io_copy_and_sum(skb,src,len,unused)	memcpy_fromio((skb)->data,(src),(len))

static inline int check_signature(unsigned long io_addr,
                                  const unsigned char *signature, int length)
{
	int retval = 0;
	do {
		if (readb(io_addr) != *signature)
			goto out;
		io_addr++;
		signature++;
		length--;
	} while (length);
	retval = 1;
out:
	return retval;
}

/*
 * Talk about misusing macros..
 */

#define __OUT1(s) \
extern inline void __out##s(unsigned int value, unsigned int port) {

#define __OUT2(m) \
__asm__ __volatile__ ("s" #m "\t%0,%1(%2)"

#define __OUT(m,s) \
__OUT1(s) __OUT2(m) : : "r" (value), "i" (0), "r" (mips_io_port_base+port)); } \
__OUT1(s##c) __OUT2(m) : : "r" (value), "ir" (port), "r" (mips_io_port_base)); } \
__OUT1(s##_p) __OUT2(m) : : "r" (value), "i" (0), "r" (mips_io_port_base+port)); \
	SLOW_DOWN_IO; } \
__OUT1(s##c_p) __OUT2(m) : : "r" (value), "ir" (port), "r" (mips_io_port_base)); \
	SLOW_DOWN_IO; }

#define __IN1(t,s) \
extern __inline__ t __in##s(unsigned int port) { t _v;

/*
 * Required nops will be inserted by the assembler
 */
#define __IN2(m) \
__asm__ __volatile__ ("l" #m "\t%0,%1(%2)"

#define __IN(t,m,s) \
__IN1(t,s) __IN2(m) : "=r" (_v) : "i" (0), "r" (mips_io_port_base+port)); return _v; } \
__IN1(t,s##c) __IN2(m) : "=r" (_v) : "ir" (port), "r" (mips_io_port_base)); return _v; } \
__IN1(t,s##_p) __IN2(m) : "=r" (_v) : "i" (0), "r" (mips_io_port_base+port)); SLOW_DOWN_IO; return _v; } \
__IN1(t,s##c_p) __IN2(m) : "=r" (_v) : "ir" (port), "r" (mips_io_port_base)); SLOW_DOWN_IO; return _v; }

#define __INS1(s) \
extern inline void __ins##s(unsigned int port, void * addr, unsigned long count) {

#define __INS2(m) \
if (count) \
__asm__ __volatile__ ( \
	".set\tnoreorder\n\t" \
	".set\tnoat\n" \
	"1:\tl" #m "\t$1,%4(%5)\n\t" \
	"subu\t%1,1\n\t" \
	"s" #m "\t$1,(%0)\n\t" \
	"bne\t$0,%1,1b\n\t" \
	"addiu\t%0,%6\n\t" \
	".set\tat\n\t" \
	".set\treorder"

#define __INS(m,s,i) \
__INS1(s) __INS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "i" (0), "r" (mips_io_port_base+port), "I" (i) \
	: "$1");} \
__INS1(s##c) __INS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "ir" (port), "r" (mips_io_port_base), "I" (i) \
	: "$1");}

#define __OUTS1(s) \
extern inline void __outs##s(unsigned int port, const void * addr, unsigned long count) {

#define __OUTS2(m) \
if (count) \
__asm__ __volatile__ ( \
        ".set\tnoreorder\n\t" \
        ".set\tnoat\n" \
        "1:\tl" #m "\t$1,(%0)\n\t" \
        "subu\t%1,1\n\t" \
        "s" #m "\t$1,%4(%5)\n\t" \
        "bne\t$0,%1,1b\n\t" \
        "addiu\t%0,%6\n\t" \
        ".set\tat\n\t" \
        ".set\treorder"

#define __OUTS(m,s,i) \
__OUTS1(s) __OUTS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "i" (0), "r" (mips_io_port_base+port), "I" (i) \
	: "$1");} \
__OUTS1(s##c) __OUTS2(m) \
	: "=r" (addr), "=r" (count) \
	: "0" (addr), "1" (count), "ir" (port), "r" (mips_io_port_base), "I" (i) \
	: "$1");}

__IN(unsigned char,b,b)
__IN(unsigned short,h,w)
__IN(unsigned int,w,l)

__OUT(b,b)
__OUT(h,w)
__OUT(w,l)

__INS(b,b,1)
__INS(h,w,2)
__INS(w,l,4)

__OUTS(b,b,1)
__OUTS(h,w,2)
__OUTS(w,l,4)

/*
 * Note that due to the way __builtin_constant_p() works, you
 *  - can't use it inside an inline function (it will never be true)
 *  - you don't have to worry about side effects within the __builtin..
 */
#define outb(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outbc((val),(port)) : \
	__outb((val),(port)))

#define inb(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inbc(port) : \
	__inb(port))

#define outb_p(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outbc_p((val),(port)) : \
	__outb_p((val),(port)))

#define inb_p(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inbc_p(port) : \
	__inb_p(port))

#define outw(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outwc((val),(port)) : \
	__outw((val),(port)))

#define inw(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inwc(port) : \
	__inw(port))

#define outw_p(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outwc_p((val),(port)) : \
	__outw_p((val),(port)))

#define inw_p(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inwc_p(port) : \
	__inw_p(port))

#define outl(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outlc((val),(port)) : \
	__outl((val),(port)))

#define inl(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inlc(port) : \
	__inl(port))

#define outl_p(val,port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outlc_p((val),(port)) : \
	__outl_p((val),(port)))

#define inl_p(port) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inlc_p(port) : \
	__inl_p(port))


#define outsb(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outsbc((port),(addr),(count)) : \
	__outsb ((port),(addr),(count)))

#define insb(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__insbc((port),(addr),(count)) : \
	__insb((port),(addr),(count)))

#define outsw(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outswc((port),(addr),(count)) : \
	__outsw ((port),(addr),(count)))

#define insw(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inswc((port),(addr),(count)) : \
	__insw((port),(addr),(count)))

#define outsl(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__outslc((port),(addr),(count)) : \
	__outsl ((port),(addr),(count)))

#define insl(port,addr,count) \
((__builtin_constant_p((port)) && (port) < 32768) ? \
	__inslc((port),(addr),(count)) : \
	__insl((port),(addr),(count)))

#define IO_SPACE_LIMIT 0xffff

/*
 * The caches on some architectures aren't dma-coherent and have need to
 * handle this in software.  There are three types of operations that
 * can be applied to dma buffers.
 *
 *  - dma_cache_wback_inv(start, size) makes caches and coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_wback(start, size) makes caches and coherent by
 *    writing the content of the caches back to memory, if necessary.
 *    The function also invalidates the affected part of the caches as
 *    necessary before DMA transfers from outside to memory.
 *  - dma_cache_inv(start, size) invalidates the affected parts of the
 *    caches.  Dirty lines of the caches may be written back or simply
 *    be discarded.  This operation is necessary before dma operations
 *    to the memory.
 */
extern void (*dma_cache_wback_inv)(unsigned long start, unsigned long size);
extern void (*dma_cache_wback)(unsigned long start, unsigned long size);
extern void (*dma_cache_inv)(unsigned long start, unsigned long size);

#endif /* __ASM_MIPS_IO_H */
