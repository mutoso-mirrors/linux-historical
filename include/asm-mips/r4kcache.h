/* $Id: r4kcache.h,v 1.2 1997/06/25 17:04:19 ralf Exp $
 * r4kcache.h: Inline assembly cache operations.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#ifndef _MIPS_R4KCACHE_H
#define _MIPS_R4KCACHE_H

#include <asm/cacheops.h>

extern inline void flush_icache_line_indexed(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Index_Invalidate_I));
}

extern inline void flush_dcache_line_indexed(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Index_Writeback_Inv_D));
}

extern inline void flush_scache_line_indexed(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Index_Writeback_Inv_SD));
}

extern inline void flush_icache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Hit_Invalidate_I));
}

extern inline void flush_dcache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Hit_Writeback_Inv_D));
}

extern inline void flush_scache_line(unsigned long addr)
{
	__asm__ __volatile__(
		".set noreorder\n\t"
		".set mips3\n\t"
		"cache %1, (%0)\n\t"
		".set mips0\n\t"
		".set reorder"
		:
		: "r" (addr),
		  "i" (Hit_Writeback_Inv_SD));
}

extern inline void blast_dcache16(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + dcache_size);

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x010(%0)
			cache %1, 0x020(%0); cache %1, 0x030(%0)
			cache %1, 0x040(%0); cache %1, 0x050(%0)
			cache %1, 0x060(%0); cache %1, 0x070(%0)
			cache %1, 0x080(%0); cache %1, 0x090(%0)
			cache %1, 0x0a0(%0); cache %1, 0x0b0(%0)
			cache %1, 0x0c0(%0); cache %1, 0x0d0(%0)
			cache %1, 0x0e0(%0); cache %1, 0x0f0(%0)
			cache %1, 0x100(%0); cache %1, 0x110(%0)
			cache %1, 0x120(%0); cache %1, 0x130(%0)
			cache %1, 0x140(%0); cache %1, 0x150(%0)
			cache %1, 0x160(%0); cache %1, 0x170(%0)
			cache %1, 0x180(%0); cache %1, 0x190(%0)
			cache %1, 0x1a0(%0); cache %1, 0x1b0(%0)
			cache %1, 0x1c0(%0); cache %1, 0x1d0(%0)
			cache %1, 0x1e0(%0); cache %1, 0x1f0(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Index_Writeback_Inv_D));
		start += 0x200;
	}
}

extern inline void blast_dcache16_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x010(%0)
			cache %1, 0x020(%0); cache %1, 0x030(%0)
			cache %1, 0x040(%0); cache %1, 0x050(%0)
			cache %1, 0x060(%0); cache %1, 0x070(%0)
			cache %1, 0x080(%0); cache %1, 0x090(%0)
			cache %1, 0x0a0(%0); cache %1, 0x0b0(%0)
			cache %1, 0x0c0(%0); cache %1, 0x0d0(%0)
			cache %1, 0x0e0(%0); cache %1, 0x0f0(%0)
			cache %1, 0x100(%0); cache %1, 0x110(%0)
			cache %1, 0x120(%0); cache %1, 0x130(%0)
			cache %1, 0x140(%0); cache %1, 0x150(%0)
			cache %1, 0x160(%0); cache %1, 0x170(%0)
			cache %1, 0x180(%0); cache %1, 0x190(%0)
			cache %1, 0x1a0(%0); cache %1, 0x1b0(%0)
			cache %1, 0x1c0(%0); cache %1, 0x1d0(%0)
			cache %1, 0x1e0(%0); cache %1, 0x1f0(%0)
			cache %1, 0x200(%0); cache %1, 0x210(%0)
			cache %1, 0x220(%0); cache %1, 0x230(%0)
			cache %1, 0x240(%0); cache %1, 0x250(%0)
			cache %1, 0x260(%0); cache %1, 0x270(%0)
			cache %1, 0x280(%0); cache %1, 0x290(%0)
			cache %1, 0x2a0(%0); cache %1, 0x2b0(%0)
			cache %1, 0x2c0(%0); cache %1, 0x2d0(%0)
			cache %1, 0x2e0(%0); cache %1, 0x2f0(%0)
			cache %1, 0x300(%0); cache %1, 0x310(%0)
			cache %1, 0x320(%0); cache %1, 0x330(%0)
			cache %1, 0x340(%0); cache %1, 0x350(%0)
			cache %1, 0x360(%0); cache %1, 0x370(%0)
			cache %1, 0x380(%0); cache %1, 0x390(%0)
			cache %1, 0x3a0(%0); cache %1, 0x3b0(%0)
			cache %1, 0x3c0(%0); cache %1, 0x3d0(%0)
			cache %1, 0x3e0(%0); cache %1, 0x3f0(%0)
			cache %1, 0x400(%0); cache %1, 0x410(%0)
			cache %1, 0x420(%0); cache %1, 0x430(%0)
			cache %1, 0x440(%0); cache %1, 0x450(%0)
			cache %1, 0x460(%0); cache %1, 0x470(%0)
			cache %1, 0x480(%0); cache %1, 0x490(%0)
			cache %1, 0x4a0(%0); cache %1, 0x4b0(%0)
			cache %1, 0x4c0(%0); cache %1, 0x4d0(%0)
			cache %1, 0x4e0(%0); cache %1, 0x4f0(%0)
			cache %1, 0x500(%0); cache %1, 0x510(%0)
			cache %1, 0x520(%0); cache %1, 0x530(%0)
			cache %1, 0x540(%0); cache %1, 0x550(%0)
			cache %1, 0x560(%0); cache %1, 0x570(%0)
			cache %1, 0x580(%0); cache %1, 0x590(%0)
			cache %1, 0x5a0(%0); cache %1, 0x5b0(%0)
			cache %1, 0x5c0(%0); cache %1, 0x5d0(%0)
			cache %1, 0x5e0(%0); cache %1, 0x5f0(%0)
			cache %1, 0x600(%0); cache %1, 0x610(%0)
			cache %1, 0x620(%0); cache %1, 0x630(%0)
			cache %1, 0x640(%0); cache %1, 0x650(%0)
			cache %1, 0x660(%0); cache %1, 0x670(%0)
			cache %1, 0x680(%0); cache %1, 0x690(%0)
			cache %1, 0x6a0(%0); cache %1, 0x6b0(%0)
			cache %1, 0x6c0(%0); cache %1, 0x6d0(%0)
			cache %1, 0x6e0(%0); cache %1, 0x6f0(%0)
			cache %1, 0x700(%0); cache %1, 0x710(%0)
			cache %1, 0x720(%0); cache %1, 0x730(%0)
			cache %1, 0x740(%0); cache %1, 0x750(%0)
			cache %1, 0x760(%0); cache %1, 0x770(%0)
			cache %1, 0x780(%0); cache %1, 0x790(%0)
			cache %1, 0x7a0(%0); cache %1, 0x7b0(%0)
			cache %1, 0x7c0(%0); cache %1, 0x7d0(%0)
			cache %1, 0x7e0(%0); cache %1, 0x7f0(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Hit_Writeback_Inv_D));
		start += 0x800;
	}
}

extern inline void blast_dcache16_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x010(%0)
			cache %1, 0x020(%0); cache %1, 0x030(%0)
			cache %1, 0x040(%0); cache %1, 0x050(%0)
			cache %1, 0x060(%0); cache %1, 0x070(%0)
			cache %1, 0x080(%0); cache %1, 0x090(%0)
			cache %1, 0x0a0(%0); cache %1, 0x0b0(%0)
			cache %1, 0x0c0(%0); cache %1, 0x0d0(%0)
			cache %1, 0x0e0(%0); cache %1, 0x0f0(%0)
			cache %1, 0x100(%0); cache %1, 0x110(%0)
			cache %1, 0x120(%0); cache %1, 0x130(%0)
			cache %1, 0x140(%0); cache %1, 0x150(%0)
			cache %1, 0x160(%0); cache %1, 0x170(%0)
			cache %1, 0x180(%0); cache %1, 0x190(%0)
			cache %1, 0x1a0(%0); cache %1, 0x1b0(%0)
			cache %1, 0x1c0(%0); cache %1, 0x1d0(%0)
			cache %1, 0x1e0(%0); cache %1, 0x1f0(%0)
			cache %1, 0x200(%0); cache %1, 0x210(%0)
			cache %1, 0x220(%0); cache %1, 0x230(%0)
			cache %1, 0x240(%0); cache %1, 0x250(%0)
			cache %1, 0x260(%0); cache %1, 0x270(%0)
			cache %1, 0x280(%0); cache %1, 0x290(%0)
			cache %1, 0x2a0(%0); cache %1, 0x2b0(%0)
			cache %1, 0x2c0(%0); cache %1, 0x2d0(%0)
			cache %1, 0x2e0(%0); cache %1, 0x2f0(%0)
			cache %1, 0x300(%0); cache %1, 0x310(%0)
			cache %1, 0x320(%0); cache %1, 0x330(%0)
			cache %1, 0x340(%0); cache %1, 0x350(%0)
			cache %1, 0x360(%0); cache %1, 0x370(%0)
			cache %1, 0x380(%0); cache %1, 0x390(%0)
			cache %1, 0x3a0(%0); cache %1, 0x3b0(%0)
			cache %1, 0x3c0(%0); cache %1, 0x3d0(%0)
			cache %1, 0x3e0(%0); cache %1, 0x3f0(%0)
			cache %1, 0x400(%0); cache %1, 0x410(%0)
			cache %1, 0x420(%0); cache %1, 0x430(%0)
			cache %1, 0x440(%0); cache %1, 0x450(%0)
			cache %1, 0x460(%0); cache %1, 0x470(%0)
			cache %1, 0x480(%0); cache %1, 0x490(%0)
			cache %1, 0x4a0(%0); cache %1, 0x4b0(%0)
			cache %1, 0x4c0(%0); cache %1, 0x4d0(%0)
			cache %1, 0x4e0(%0); cache %1, 0x4f0(%0)
			cache %1, 0x500(%0); cache %1, 0x510(%0)
			cache %1, 0x520(%0); cache %1, 0x530(%0)
			cache %1, 0x540(%0); cache %1, 0x550(%0)
			cache %1, 0x560(%0); cache %1, 0x570(%0)
			cache %1, 0x580(%0); cache %1, 0x590(%0)
			cache %1, 0x5a0(%0); cache %1, 0x5b0(%0)
			cache %1, 0x5c0(%0); cache %1, 0x5d0(%0)
			cache %1, 0x5e0(%0); cache %1, 0x5f0(%0)
			cache %1, 0x600(%0); cache %1, 0x610(%0)
			cache %1, 0x620(%0); cache %1, 0x630(%0)
			cache %1, 0x640(%0); cache %1, 0x650(%0)
			cache %1, 0x660(%0); cache %1, 0x670(%0)
			cache %1, 0x680(%0); cache %1, 0x690(%0)
			cache %1, 0x6a0(%0); cache %1, 0x6b0(%0)
			cache %1, 0x6c0(%0); cache %1, 0x6d0(%0)
			cache %1, 0x6e0(%0); cache %1, 0x6f0(%0)
			cache %1, 0x700(%0); cache %1, 0x710(%0)
			cache %1, 0x720(%0); cache %1, 0x730(%0)
			cache %1, 0x740(%0); cache %1, 0x750(%0)
			cache %1, 0x760(%0); cache %1, 0x770(%0)
			cache %1, 0x780(%0); cache %1, 0x790(%0)
			cache %1, 0x7a0(%0); cache %1, 0x7b0(%0)
			cache %1, 0x7c0(%0); cache %1, 0x7d0(%0)
			cache %1, 0x7e0(%0); cache %1, 0x7f0(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Index_Writeback_Inv_D));
		start += 0x800;
	}
}

extern inline void blast_dcache32(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + dcache_size);

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x020(%0)
			cache %1, 0x040(%0); cache %1, 0x060(%0)
			cache %1, 0x080(%0); cache %1, 0x0a0(%0)
			cache %1, 0x0c0(%0); cache %1, 0x0e0(%0)
			cache %1, 0x100(%0); cache %1, 0x120(%0)
			cache %1, 0x140(%0); cache %1, 0x160(%0)
			cache %1, 0x180(%0); cache %1, 0x1a0(%0)
			cache %1, 0x1c0(%0); cache %1, 0x1e0(%0)
			cache %1, 0x200(%0); cache %1, 0x220(%0)
			cache %1, 0x240(%0); cache %1, 0x260(%0)
			cache %1, 0x280(%0); cache %1, 0x2a0(%0)
			cache %1, 0x2c0(%0); cache %1, 0x2e0(%0)
			cache %1, 0x300(%0); cache %1, 0x320(%0)
			cache %1, 0x340(%0); cache %1, 0x360(%0)
			cache %1, 0x380(%0); cache %1, 0x3a0(%0)
			cache %1, 0x3c0(%0); cache %1, 0x3e0(%0)
			cache %1, 0x400(%0); cache %1, 0x420(%0)
			cache %1, 0x440(%0); cache %1, 0x460(%0)
			cache %1, 0x480(%0); cache %1, 0x4a0(%0)
			cache %1, 0x4c0(%0); cache %1, 0x4e0(%0)
			cache %1, 0x500(%0); cache %1, 0x520(%0)
			cache %1, 0x540(%0); cache %1, 0x560(%0)
			cache %1, 0x580(%0); cache %1, 0x5a0(%0)
			cache %1, 0x5c0(%0); cache %1, 0x5e0(%0)
			cache %1, 0x600(%0); cache %1, 0x620(%0)
			cache %1, 0x640(%0); cache %1, 0x660(%0)
			cache %1, 0x680(%0); cache %1, 0x6a0(%0)
			cache %1, 0x6c0(%0); cache %1, 0x6e0(%0)
			cache %1, 0x700(%0); cache %1, 0x720(%0)
			cache %1, 0x740(%0); cache %1, 0x760(%0)
			cache %1, 0x780(%0); cache %1, 0x7a0(%0)
			cache %1, 0x7c0(%0); cache %1, 0x7e0(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Index_Writeback_Inv_D));
		start += 0x400;
	}
}

extern inline void blast_dcache32_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	__asm__ __volatile__("nop;nop;nop;nop");
	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x020(%0)
			cache %1, 0x040(%0); cache %1, 0x060(%0)
			cache %1, 0x080(%0); cache %1, 0x0a0(%0)
			cache %1, 0x0c0(%0); cache %1, 0x0e0(%0)
			cache %1, 0x100(%0); cache %1, 0x120(%0)
			cache %1, 0x140(%0); cache %1, 0x160(%0)
			cache %1, 0x180(%0); cache %1, 0x1a0(%0)
			cache %1, 0x1c0(%0); cache %1, 0x1e0(%0)
			cache %1, 0x200(%0); cache %1, 0x220(%0)
			cache %1, 0x240(%0); cache %1, 0x260(%0)
			cache %1, 0x280(%0); cache %1, 0x2a0(%0)
			cache %1, 0x2c0(%0); cache %1, 0x2e0(%0)
			cache %1, 0x300(%0); cache %1, 0x320(%0)
			cache %1, 0x340(%0); cache %1, 0x360(%0)
			cache %1, 0x380(%0); cache %1, 0x3a0(%0)
			cache %1, 0x3c0(%0); cache %1, 0x3e0(%0)
			cache %1, 0x400(%0); cache %1, 0x420(%0)
			cache %1, 0x440(%0); cache %1, 0x460(%0)
			cache %1, 0x480(%0); cache %1, 0x4a0(%0)
			cache %1, 0x4c0(%0); cache %1, 0x4e0(%0)
			cache %1, 0x500(%0); cache %1, 0x520(%0)
			cache %1, 0x540(%0); cache %1, 0x560(%0)
			cache %1, 0x580(%0); cache %1, 0x5a0(%0)
			cache %1, 0x5c0(%0); cache %1, 0x5e0(%0)
			cache %1, 0x600(%0); cache %1, 0x620(%0)
			cache %1, 0x640(%0); cache %1, 0x660(%0)
			cache %1, 0x680(%0); cache %1, 0x6a0(%0)
			cache %1, 0x6c0(%0); cache %1, 0x6e0(%0)
			cache %1, 0x700(%0); cache %1, 0x720(%0)
			cache %1, 0x740(%0); cache %1, 0x760(%0)
			cache %1, 0x780(%0); cache %1, 0x7a0(%0)
			cache %1, 0x7c0(%0); cache %1, 0x7e0(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Hit_Writeback_Inv_D));
		start += 0x800;
	}
}

extern inline void blast_dcache32_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x020(%0)
			cache %1, 0x040(%0); cache %1, 0x060(%0)
			cache %1, 0x080(%0); cache %1, 0x0a0(%0)
			cache %1, 0x0c0(%0); cache %1, 0x0e0(%0)
			cache %1, 0x100(%0); cache %1, 0x120(%0)
			cache %1, 0x140(%0); cache %1, 0x160(%0)
			cache %1, 0x180(%0); cache %1, 0x1a0(%0)
			cache %1, 0x1c0(%0); cache %1, 0x1e0(%0)
			cache %1, 0x200(%0); cache %1, 0x220(%0)
			cache %1, 0x240(%0); cache %1, 0x260(%0)
			cache %1, 0x280(%0); cache %1, 0x2a0(%0)
			cache %1, 0x2c0(%0); cache %1, 0x2e0(%0)
			cache %1, 0x300(%0); cache %1, 0x320(%0)
			cache %1, 0x340(%0); cache %1, 0x360(%0)
			cache %1, 0x380(%0); cache %1, 0x3a0(%0)
			cache %1, 0x3c0(%0); cache %1, 0x3e0(%0)
			cache %1, 0x400(%0); cache %1, 0x420(%0)
			cache %1, 0x440(%0); cache %1, 0x460(%0)
			cache %1, 0x480(%0); cache %1, 0x4a0(%0)
			cache %1, 0x4c0(%0); cache %1, 0x4e0(%0)
			cache %1, 0x500(%0); cache %1, 0x520(%0)
			cache %1, 0x540(%0); cache %1, 0x560(%0)
			cache %1, 0x580(%0); cache %1, 0x5a0(%0)
			cache %1, 0x5c0(%0); cache %1, 0x5e0(%0)
			cache %1, 0x600(%0); cache %1, 0x620(%0)
			cache %1, 0x640(%0); cache %1, 0x660(%0)
			cache %1, 0x680(%0); cache %1, 0x6a0(%0)
			cache %1, 0x6c0(%0); cache %1, 0x6e0(%0)
			cache %1, 0x700(%0); cache %1, 0x720(%0)
			cache %1, 0x740(%0); cache %1, 0x760(%0)
			cache %1, 0x780(%0); cache %1, 0x7a0(%0)
			cache %1, 0x7c0(%0); cache %1, 0x7e0(%0) 
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Index_Writeback_Inv_D));
		start += 0x800;
	}
}

extern inline void blast_icache16(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + icache_size);

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x010(%0)
			cache %1, 0x020(%0); cache %1, 0x030(%0)
			cache %1, 0x040(%0); cache %1, 0x050(%0)
			cache %1, 0x060(%0); cache %1, 0x070(%0)
			cache %1, 0x080(%0); cache %1, 0x090(%0)
			cache %1, 0x0a0(%0); cache %1, 0x0b0(%0)
			cache %1, 0x0c0(%0); cache %1, 0x0d0(%0)
			cache %1, 0x0e0(%0); cache %1, 0x0f0(%0)
			cache %1, 0x100(%0); cache %1, 0x110(%0)
			cache %1, 0x120(%0); cache %1, 0x130(%0)
			cache %1, 0x140(%0); cache %1, 0x150(%0)
			cache %1, 0x160(%0); cache %1, 0x170(%0)
			cache %1, 0x180(%0); cache %1, 0x190(%0)
			cache %1, 0x1a0(%0); cache %1, 0x1b0(%0)
			cache %1, 0x1c0(%0); cache %1, 0x1d0(%0)
			cache %1, 0x1e0(%0); cache %1, 0x1f0(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Index_Invalidate_I));
		start += 0x200;
	}
}

extern inline void blast_icache16_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x010(%0)
			cache %1, 0x020(%0); cache %1, 0x030(%0)
			cache %1, 0x040(%0); cache %1, 0x050(%0)
			cache %1, 0x060(%0); cache %1, 0x070(%0)
			cache %1, 0x080(%0); cache %1, 0x090(%0)
			cache %1, 0x0a0(%0); cache %1, 0x0b0(%0)
			cache %1, 0x0c0(%0); cache %1, 0x0d0(%0)
			cache %1, 0x0e0(%0); cache %1, 0x0f0(%0)
			cache %1, 0x100(%0); cache %1, 0x110(%0)
			cache %1, 0x120(%0); cache %1, 0x130(%0)
			cache %1, 0x140(%0); cache %1, 0x150(%0)
			cache %1, 0x160(%0); cache %1, 0x170(%0)
			cache %1, 0x180(%0); cache %1, 0x190(%0)
			cache %1, 0x1a0(%0); cache %1, 0x1b0(%0)
			cache %1, 0x1c0(%0); cache %1, 0x1d0(%0)
			cache %1, 0x1e0(%0); cache %1, 0x1f0(%0)
			cache %1, 0x200(%0); cache %1, 0x210(%0)
			cache %1, 0x220(%0); cache %1, 0x230(%0)
			cache %1, 0x240(%0); cache %1, 0x250(%0)
			cache %1, 0x260(%0); cache %1, 0x270(%0)
			cache %1, 0x280(%0); cache %1, 0x290(%0)
			cache %1, 0x2a0(%0); cache %1, 0x2b0(%0)
			cache %1, 0x2c0(%0); cache %1, 0x2d0(%0)
			cache %1, 0x2e0(%0); cache %1, 0x2f0(%0)
			cache %1, 0x300(%0); cache %1, 0x310(%0)
			cache %1, 0x320(%0); cache %1, 0x330(%0)
			cache %1, 0x340(%0); cache %1, 0x350(%0)
			cache %1, 0x360(%0); cache %1, 0x370(%0)
			cache %1, 0x380(%0); cache %1, 0x390(%0)
			cache %1, 0x3a0(%0); cache %1, 0x3b0(%0)
			cache %1, 0x3c0(%0); cache %1, 0x3d0(%0)
			cache %1, 0x3e0(%0); cache %1, 0x3f0(%0)
			cache %1, 0x400(%0); cache %1, 0x410(%0)
			cache %1, 0x420(%0); cache %1, 0x430(%0)
			cache %1, 0x440(%0); cache %1, 0x450(%0)
			cache %1, 0x460(%0); cache %1, 0x470(%0)
			cache %1, 0x480(%0); cache %1, 0x490(%0)
			cache %1, 0x4a0(%0); cache %1, 0x4b0(%0)
			cache %1, 0x4c0(%0); cache %1, 0x4d0(%0)
			cache %1, 0x4e0(%0); cache %1, 0x4f0(%0)
			cache %1, 0x500(%0); cache %1, 0x510(%0)
			cache %1, 0x520(%0); cache %1, 0x530(%0)
			cache %1, 0x540(%0); cache %1, 0x550(%0)
			cache %1, 0x560(%0); cache %1, 0x570(%0)
			cache %1, 0x580(%0); cache %1, 0x590(%0)
			cache %1, 0x5a0(%0); cache %1, 0x5b0(%0)
			cache %1, 0x5c0(%0); cache %1, 0x5d0(%0)
			cache %1, 0x5e0(%0); cache %1, 0x5f0(%0)
			cache %1, 0x600(%0); cache %1, 0x610(%0)
			cache %1, 0x620(%0); cache %1, 0x630(%0)
			cache %1, 0x640(%0); cache %1, 0x650(%0)
			cache %1, 0x660(%0); cache %1, 0x670(%0)
			cache %1, 0x680(%0); cache %1, 0x690(%0)
			cache %1, 0x6a0(%0); cache %1, 0x6b0(%0)
			cache %1, 0x6c0(%0); cache %1, 0x6d0(%0)
			cache %1, 0x6e0(%0); cache %1, 0x6f0(%0)
			cache %1, 0x700(%0); cache %1, 0x710(%0)
			cache %1, 0x720(%0); cache %1, 0x730(%0)
			cache %1, 0x740(%0); cache %1, 0x750(%0)
			cache %1, 0x760(%0); cache %1, 0x770(%0)
			cache %1, 0x780(%0); cache %1, 0x790(%0)
			cache %1, 0x7a0(%0); cache %1, 0x7b0(%0)
			cache %1, 0x7c0(%0); cache %1, 0x7d0(%0)
			cache %1, 0x7e0(%0); cache %1, 0x7f0(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Hit_Invalidate_I));
		start += 0x800;
	}
}

extern inline void blast_icache16_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		__asm__ __volatile__("
.set noreorder
.set mips3
cache 0, 0x000(%0); cache 0, 0x010(%0); cache 0, 0x020(%0); cache 0, 0x030(%0)
cache 0, 0x040(%0); cache 0, 0x050(%0); cache 0, 0x060(%0); cache 0, 0x070(%0)
cache 0, 0x080(%0); cache 0, 0x090(%0); cache 0, 0x0a0(%0); cache 0, 0x0b0(%0)
cache 0, 0x0c0(%0); cache 0, 0x0d0(%0); cache 0, 0x0e0(%0); cache 0, 0x0f0(%0)
cache 0, 0x100(%0); cache 0, 0x110(%0); cache 0, 0x120(%0); cache 0, 0x130(%0)
cache 0, 0x140(%0); cache 0, 0x150(%0); cache 0, 0x160(%0); cache 0, 0x170(%0)
cache 0, 0x180(%0); cache 0, 0x190(%0); cache 0, 0x1a0(%0); cache 0, 0x1b0(%0)
cache 0, 0x1c0(%0); cache 0, 0x1d0(%0); cache 0, 0x1e0(%0); cache 0, 0x1f0(%0)
cache 0, 0x200(%0); cache 0, 0x210(%0); cache 0, 0x220(%0); cache 0, 0x230(%0)
cache 0, 0x240(%0); cache 0, 0x250(%0); cache 0, 0x260(%0); cache 0, 0x270(%0)
cache 0, 0x280(%0); cache 0, 0x290(%0); cache 0, 0x2a0(%0); cache 0, 0x2b0(%0)
cache 0, 0x2c0(%0); cache 0, 0x2d0(%0); cache 0, 0x2e0(%0); cache 0, 0x2f0(%0)
cache 0, 0x300(%0); cache 0, 0x310(%0); cache 0, 0x320(%0); cache 0, 0x330(%0)
cache 0, 0x340(%0); cache 0, 0x350(%0); cache 0, 0x360(%0); cache 0, 0x370(%0)
cache 0, 0x380(%0); cache 0, 0x390(%0); cache 0, 0x3a0(%0); cache 0, 0x3b0(%0)
cache 0, 0x3c0(%0); cache 0, 0x3d0(%0); cache 0, 0x3e0(%0); cache 0, 0x3f0(%0)
cache 0, 0x400(%0); cache 0, 0x410(%0); cache 0, 0x420(%0); cache 0, 0x430(%0)
cache 0, 0x440(%0); cache 0, 0x450(%0); cache 0, 0x460(%0); cache 0, 0x470(%0)
cache 0, 0x480(%0); cache 0, 0x490(%0); cache 0, 0x4a0(%0); cache 0, 0x4b0(%0)
cache 0, 0x4c0(%0); cache 0, 0x4d0(%0); cache 0, 0x4e0(%0); cache 0, 0x4f0(%0)
cache 0, 0x500(%0); cache 0, 0x510(%0); cache 0, 0x520(%0); cache 0, 0x530(%0)
cache 0, 0x540(%0); cache 0, 0x550(%0); cache 0, 0x560(%0); cache 0, 0x570(%0)
cache 0, 0x580(%0); cache 0, 0x590(%0); cache 0, 0x5a0(%0); cache 0, 0x5b0(%0)
cache 0, 0x5c0(%0); cache 0, 0x5d0(%0); cache 0, 0x5e0(%0); cache 0, 0x5f0(%0)
cache 0, 0x600(%0); cache 0, 0x610(%0); cache 0, 0x620(%0); cache 0, 0x630(%0)
cache 0, 0x640(%0); cache 0, 0x650(%0); cache 0, 0x660(%0); cache 0, 0x670(%0)
cache 0, 0x680(%0); cache 0, 0x690(%0); cache 0, 0x6a0(%0); cache 0, 0x6b0(%0)
cache 0, 0x6c0(%0); cache 0, 0x6d0(%0); cache 0, 0x6e0(%0); cache 0, 0x6f0(%0)
cache 0, 0x700(%0); cache 0, 0x710(%0); cache 0, 0x720(%0); cache 0, 0x730(%0)
cache 0, 0x740(%0); cache 0, 0x750(%0); cache 0, 0x760(%0); cache 0, 0x770(%0)
cache 0, 0x780(%0); cache 0, 0x790(%0); cache 0, 0x7a0(%0); cache 0, 0x7b0(%0)
cache 0, 0x7c0(%0); cache 0, 0x7d0(%0); cache 0, 0x7e0(%0); cache 0, 0x7f0(%0)
.set mips0
.set reorder
                " : : "r" (start));
		start += 0x800;
	}
}

extern inline void blast_icache32(void)
{
	unsigned long start = KSEG0;
	unsigned long end = (start + icache_size);

	while(start < end) {
		__asm__ __volatile__("
.set noreorder
.set mips3
cache 0, 0x000(%0); cache 0, 0x020(%0); cache 0, 0x040(%0); cache 0, 0x060(%0);
cache 0, 0x080(%0); cache 0, 0x0a0(%0); cache 0, 0x0c0(%0); cache 0, 0x0e0(%0);
cache 0, 0x100(%0); cache 0, 0x120(%0); cache 0, 0x140(%0); cache 0, 0x160(%0);
cache 0, 0x180(%0); cache 0, 0x1a0(%0); cache 0, 0x1c0(%0); cache 0, 0x1e0(%0);
cache 0, 0x200(%0); cache 0, 0x220(%0); cache 0, 0x240(%0); cache 0, 0x260(%0);
cache 0, 0x280(%0); cache 0, 0x2a0(%0); cache 0, 0x2c0(%0); cache 0, 0x2e0(%0);
cache 0, 0x300(%0); cache 0, 0x320(%0); cache 0, 0x340(%0); cache 0, 0x360(%0);
cache 0, 0x380(%0); cache 0, 0x3a0(%0); cache 0, 0x3c0(%0); cache 0, 0x3e0(%0);
.set mips0
.set reorder
                " : : "r" (start));
		start += 0x400;
	}
}

extern inline void blast_icache32_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		__asm__ __volatile__("
.set noreorder
.set mips3
cache 16, 0x000(%0); cache 16, 0x020(%0); cache 16, 0x040(%0); cache 16, 0x060(%0);
cache 16, 0x080(%0); cache 16, 0x0a0(%0); cache 16, 0x0c0(%0); cache 16, 0x0e0(%0);
cache 16, 0x100(%0); cache 16, 0x120(%0); cache 16, 0x140(%0); cache 16, 0x160(%0);
cache 16, 0x180(%0); cache 16, 0x1a0(%0); cache 16, 0x1c0(%0); cache 16, 0x1e0(%0);
cache 16, 0x200(%0); cache 16, 0x220(%0); cache 16, 0x240(%0); cache 16, 0x260(%0);
cache 16, 0x280(%0); cache 16, 0x2a0(%0); cache 16, 0x2c0(%0); cache 16, 0x2e0(%0);
cache 16, 0x300(%0); cache 16, 0x320(%0); cache 16, 0x340(%0); cache 16, 0x360(%0);
cache 16, 0x380(%0); cache 16, 0x3a0(%0); cache 16, 0x3c0(%0); cache 16, 0x3e0(%0);
cache 16, 0x400(%0); cache 16, 0x420(%0); cache 16, 0x440(%0); cache 16, 0x460(%0);
cache 16, 0x480(%0); cache 16, 0x4a0(%0); cache 16, 0x4c0(%0); cache 16, 0x4e0(%0);
cache 16, 0x500(%0); cache 16, 0x520(%0); cache 16, 0x540(%0); cache 16, 0x560(%0);
cache 16, 0x580(%0); cache 16, 0x5a0(%0); cache 16, 0x5c0(%0); cache 16, 0x5e0(%0);
cache 16, 0x600(%0); cache 16, 0x620(%0); cache 16, 0x640(%0); cache 16, 0x660(%0);
cache 16, 0x680(%0); cache 16, 0x6a0(%0); cache 16, 0x6c0(%0); cache 16, 0x6e0(%0);
cache 16, 0x700(%0); cache 16, 0x720(%0); cache 16, 0x740(%0); cache 16, 0x760(%0);
cache 16, 0x780(%0); cache 16, 0x7a0(%0); cache 16, 0x7c0(%0); cache 16, 0x7e0(%0);
.set mips0
.set reorder
                " : : "r" (start));
		start += 0x800;
	}
}

extern inline void blast_icache32_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = (start + PAGE_SIZE);

	while(start < end) {
		__asm__ __volatile__("
.set noreorder
.set mips3
cache 0, 0x000(%0); cache 0, 0x020(%0); cache 0, 0x040(%0); cache 0, 0x060(%0);
cache 0, 0x080(%0); cache 0, 0x0a0(%0); cache 0, 0x0c0(%0); cache 0, 0x0e0(%0);
cache 0, 0x100(%0); cache 0, 0x120(%0); cache 0, 0x140(%0); cache 0, 0x160(%0);
cache 0, 0x180(%0); cache 0, 0x1a0(%0); cache 0, 0x1c0(%0); cache 0, 0x1e0(%0);
cache 0, 0x200(%0); cache 0, 0x220(%0); cache 0, 0x240(%0); cache 0, 0x260(%0);
cache 0, 0x280(%0); cache 0, 0x2a0(%0); cache 0, 0x2c0(%0); cache 0, 0x2e0(%0);
cache 0, 0x300(%0); cache 0, 0x320(%0); cache 0, 0x340(%0); cache 0, 0x360(%0);
cache 0, 0x380(%0); cache 0, 0x3a0(%0); cache 0, 0x3c0(%0); cache 0, 0x3e0(%0);
cache 0, 0x400(%0); cache 0, 0x420(%0); cache 0, 0x440(%0); cache 0, 0x460(%0);
cache 0, 0x480(%0); cache 0, 0x4a0(%0); cache 0, 0x4c0(%0); cache 0, 0x4e0(%0);
cache 0, 0x500(%0); cache 0, 0x520(%0); cache 0, 0x540(%0); cache 0, 0x560(%0);
cache 0, 0x580(%0); cache 0, 0x5a0(%0); cache 0, 0x5c0(%0); cache 0, 0x5e0(%0);
cache 0, 0x600(%0); cache 0, 0x620(%0); cache 0, 0x640(%0); cache 0, 0x660(%0);
cache 0, 0x680(%0); cache 0, 0x6a0(%0); cache 0, 0x6c0(%0); cache 0, 0x6e0(%0);
cache 0, 0x700(%0); cache 0, 0x720(%0); cache 0, 0x740(%0); cache 0, 0x760(%0);
cache 0, 0x780(%0); cache 0, 0x7a0(%0); cache 0, 0x7c0(%0); cache 0, 0x7e0(%0);
.set mips0
.set reorder
                " : : "r" (start));
		start += 0x800;
	}
}

extern inline void blast_scache16(void)
{
	unsigned long start = KSEG0;
	unsigned long end = KSEG0 + scache_size;

	while(start < end) {
		__asm__ __volatile__("
.set noreorder
.set mips3
cache 3, 0x000(%0); cache 3, 0x010(%0); cache 3, 0x020(%0); cache 3, 0x030(%0)
cache 3, 0x040(%0); cache 3, 0x050(%0); cache 3, 0x060(%0); cache 3, 0x070(%0)
cache 3, 0x080(%0); cache 3, 0x090(%0); cache 3, 0x0a0(%0); cache 3, 0x0b0(%0)
cache 3, 0x0c0(%0); cache 3, 0x0d0(%0); cache 3, 0x0e0(%0); cache 3, 0x0f0(%0)
cache 3, 0x100(%0); cache 3, 0x110(%0); cache 3, 0x120(%0); cache 3, 0x130(%0)
cache 3, 0x140(%0); cache 3, 0x150(%0); cache 3, 0x160(%0); cache 3, 0x170(%0)
cache 3, 0x180(%0); cache 3, 0x190(%0); cache 3, 0x1a0(%0); cache 3, 0x1b0(%0)
cache 3, 0x1c0(%0); cache 3, 0x1d0(%0); cache 3, 0x1e0(%0); cache 3, 0x1f0(%0)
.set mips0
.set reorder
                " : : "r" (start));
		start += 0x200;
	}
}

extern inline void blast_scache16_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		__asm__ __volatile__("
.set noreorder
.set mips3
cache 23, 0x000(%0); cache 23, 0x010(%0); cache 23, 0x020(%0); cache 23, 0x030(%0)
cache 23, 0x040(%0); cache 23, 0x050(%0); cache 23, 0x060(%0); cache 23, 0x070(%0)
cache 23, 0x080(%0); cache 23, 0x090(%0); cache 23, 0x0a0(%0); cache 23, 0x0b0(%0)
cache 23, 0x0c0(%0); cache 23, 0x0d0(%0); cache 23, 0x0e0(%0); cache 23, 0x0f0(%0)
cache 23, 0x100(%0); cache 23, 0x110(%0); cache 23, 0x120(%0); cache 23, 0x130(%0)
cache 23, 0x140(%0); cache 23, 0x150(%0); cache 23, 0x160(%0); cache 23, 0x170(%0)
cache 23, 0x180(%0); cache 23, 0x190(%0); cache 23, 0x1a0(%0); cache 23, 0x1b0(%0)
cache 23, 0x1c0(%0); cache 23, 0x1d0(%0); cache 23, 0x1e0(%0); cache 23, 0x1f0(%0)
.set mips0
.set reorder
                " : : "r" (start));
		start += 0x200;
	}
}

extern inline void blast_scache16_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		__asm__ __volatile__("
.set noreorder
.set mips3
cache 3, 0x000(%0); cache 3, 0x010(%0); cache 3, 0x020(%0); cache 3, 0x030(%0)
cache 3, 0x040(%0); cache 3, 0x050(%0); cache 3, 0x060(%0); cache 3, 0x070(%0)
cache 3, 0x080(%0); cache 3, 0x090(%0); cache 3, 0x0a0(%0); cache 3, 0x0b0(%0)
cache 3, 0x0c0(%0); cache 3, 0x0d0(%0); cache 3, 0x0e0(%0); cache 3, 0x0f0(%0)
cache 3, 0x100(%0); cache 3, 0x110(%0); cache 3, 0x120(%0); cache 3, 0x130(%0)
cache 3, 0x140(%0); cache 3, 0x150(%0); cache 3, 0x160(%0); cache 3, 0x170(%0)
cache 3, 0x180(%0); cache 3, 0x190(%0); cache 3, 0x1a0(%0); cache 3, 0x1b0(%0)
cache 3, 0x1c0(%0); cache 3, 0x1d0(%0); cache 3, 0x1e0(%0); cache 3, 0x1f0(%0)
.set mips0
.set reorder
                " : : "r" (start));
		start += 0x200;
	}
}

extern inline void blast_scache32(void)
{
	unsigned long start = KSEG0;
	unsigned long end = KSEG0 + scache_size;

	while(start < end) {
		__asm__ __volatile__("
.set noreorder
.set mips3
cache 3, 0x000(%0); cache 3, 0x020(%0); cache 3, 0x040(%0); cache 3, 0x060(%0);
cache 3, 0x080(%0); cache 3, 0x0a0(%0); cache 3, 0x0c0(%0); cache 3, 0x0e0(%0);
cache 3, 0x100(%0); cache 3, 0x120(%0); cache 3, 0x140(%0); cache 3, 0x160(%0);
cache 3, 0x180(%0); cache 3, 0x1a0(%0); cache 3, 0x1c0(%0); cache 3, 0x1e0(%0);
cache 3, 0x200(%0); cache 3, 0x220(%0); cache 3, 0x240(%0); cache 3, 0x260(%0);
cache 3, 0x280(%0); cache 3, 0x2a0(%0); cache 3, 0x2c0(%0); cache 3, 0x2e0(%0);
cache 3, 0x300(%0); cache 3, 0x320(%0); cache 3, 0x340(%0); cache 3, 0x360(%0);
cache 3, 0x380(%0); cache 3, 0x3a0(%0); cache 3, 0x3c0(%0); cache 3, 0x3e0(%0);
.set mips0
.set reorder
                " : : "r" (start));
		start += 0x400;
	}
}

extern inline void blast_scache32_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		__asm__ __volatile__("
.set noreorder
.set mips3
cache 23, 0x000(%0); cache 23, 0x020(%0); cache 23, 0x040(%0); cache 23, 0x060(%0);
cache 23, 0x080(%0); cache 23, 0x0a0(%0); cache 23, 0x0c0(%0); cache 23, 0x0e0(%0);
cache 23, 0x100(%0); cache 23, 0x120(%0); cache 23, 0x140(%0); cache 23, 0x160(%0);
cache 23, 0x180(%0); cache 23, 0x1a0(%0); cache 23, 0x1c0(%0); cache 23, 0x1e0(%0);
cache 23, 0x200(%0); cache 23, 0x220(%0); cache 23, 0x240(%0); cache 23, 0x260(%0);
cache 23, 0x280(%0); cache 23, 0x2a0(%0); cache 23, 0x2c0(%0); cache 23, 0x2e0(%0);
cache 23, 0x300(%0); cache 23, 0x320(%0); cache 23, 0x340(%0); cache 23, 0x360(%0);
cache 23, 0x380(%0); cache 23, 0x3a0(%0); cache 23, 0x3c0(%0); cache 23, 0x3e0(%0);
.set mips0
.set reorder
                " : : "r" (start));
		start += 0x400;
	}
}

extern inline void blast_scache32_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		__asm__ __volatile__("
.set noreorder
.set mips3
cache 3, 0x000(%0); cache 3, 0x020(%0); cache 3, 0x040(%0); cache 3, 0x060(%0);
cache 3, 0x080(%0); cache 3, 0x0a0(%0); cache 3, 0x0c0(%0); cache 3, 0x0e0(%0);
cache 3, 0x100(%0); cache 3, 0x120(%0); cache 3, 0x140(%0); cache 3, 0x160(%0);
cache 3, 0x180(%0); cache 3, 0x1a0(%0); cache 3, 0x1c0(%0); cache 3, 0x1e0(%0);
cache 3, 0x200(%0); cache 3, 0x220(%0); cache 3, 0x240(%0); cache 3, 0x260(%0);
cache 3, 0x280(%0); cache 3, 0x2a0(%0); cache 3, 0x2c0(%0); cache 3, 0x2e0(%0);
cache 3, 0x300(%0); cache 3, 0x320(%0); cache 3, 0x340(%0); cache 3, 0x360(%0);
cache 3, 0x380(%0); cache 3, 0x3a0(%0); cache 3, 0x3c0(%0); cache 3, 0x3e0(%0);
.set mips0
.set reorder
                " : : "r" (start));
		start += 0x400;
	}
}

extern inline void blast_scache64(void)
{
	unsigned long start = KSEG0;
	unsigned long end = KSEG0 + scache_size;

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x040(%0)
			cache %1, 0x080(%0); cache %1, 0x0c0(%0)
			cache %1, 0x100(%0); cache %1, 0x140(%0)
			cache %1, 0x180(%0); cache %1, 0x1c0(%0)
			cache %1, 0x200(%0); cache %1, 0x240(%0)
			cache %1, 0x280(%0); cache %1, 0x2c0(%0)
			cache %1, 0x300(%0); cache %1, 0x340(%0)
			cache %1, 0x380(%0); cache %1, 0x3c0(%0)
			cache %1, 0x400(%0); cache %1, 0x440(%0)
			cache %1, 0x480(%0); cache %1, 0x4c0(%0)
			cache %1, 0x500(%0); cache %1, 0x540(%0)
			cache %1, 0x580(%0); cache %1, 0x5c0(%0)
			cache %1, 0x600(%0); cache %1, 0x640(%0)
			cache %1, 0x680(%0); cache %1, 0x6c0(%0)
			cache %1, 0x700(%0); cache %1, 0x740(%0)
			cache %1, 0x780(%0); cache %1, 0x7c0(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Index_Writeback_Inv_SD));
		start += 0x800;
	}
}

extern inline void blast_scache64_page(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x040(%0)
			cache %1, 0x080(%0); cache %1, 0x0c0(%0)
			cache %1, 0x100(%0); cache %1, 0x140(%0)
			cache %1, 0x180(%0); cache %1, 0x1c0(%0)
			cache %1, 0x200(%0); cache %1, 0x240(%0)
			cache %1, 0x280(%0); cache %1, 0x2c0(%0)
			cache %1, 0x300(%0); cache %1, 0x340(%0)
			cache %1, 0x380(%0); cache %1, 0x3c0(%0)
			cache %1, 0x400(%0); cache %1, 0x440(%0)
			cache %1, 0x480(%0); cache %1, 0x4c0(%0)
			cache %1, 0x500(%0); cache %1, 0x540(%0)
			cache %1, 0x580(%0); cache %1, 0x5c0(%0)
			cache %1, 0x600(%0); cache %1, 0x640(%0)
			cache %1, 0x680(%0); cache %1, 0x6c0(%0)
			cache %1, 0x700(%0); cache %1, 0x740(%0)
			cache %1, 0x780(%0); cache %1, 0x7c0(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Hit_Writeback_Inv_D));
		start += 0x800;
	}
}

extern inline void blast_scache64_page_indexed(unsigned long page)
{
	unsigned long start = page;
	unsigned long end = page + PAGE_SIZE;

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x040(%0)
			cache %1, 0x080(%0); cache %1, 0x0c0(%0)
			cache %1, 0x100(%0); cache %1, 0x140(%0)
			cache %1, 0x180(%0); cache %1, 0x1c0(%0)
			cache %1, 0x200(%0); cache %1, 0x240(%0)
			cache %1, 0x280(%0); cache %1, 0x2c0(%0)
			cache %1, 0x300(%0); cache %1, 0x340(%0)
			cache %1, 0x380(%0); cache %1, 0x3c0(%0)
			cache %1, 0x400(%0); cache %1, 0x440(%0)
			cache %1, 0x480(%0); cache %1, 0x4c0(%0)
			cache %1, 0x500(%0); cache %1, 0x540(%0)
			cache %1, 0x580(%0); cache %1, 0x5c0(%0)
			cache %1, 0x600(%0); cache %1, 0x640(%0)
			cache %1, 0x680(%0); cache %1, 0x6c0(%0)
			cache %1, 0x700(%0); cache %1, 0x740(%0)
			cache %1, 0x780(%0); cache %1, 0x7c0(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Index_Writeback_Inv_SD));
		start += 0x800;
	}
}

extern inline void blast_scache128(void)
{
	unsigned long start = KSEG0;
	unsigned long end = KSEG0 + scache_size;

	while(start < end) {
		__asm__ __volatile__("
			.set noreorder
			.set mips3
			cache %1, 0x000(%0); cache %1, 0x080(%0)
			cache %1, 0x100(%0); cache %1, 0x180(%0)
			cache %1, 0x200(%0); cache %1, 0x280(%0)
			cache %1, 0x300(%0); cache %1, 0x380(%0)
			cache %1, 0x400(%0); cache %1, 0x480(%0)
			cache %1, 0x500(%0); cache %1, 0x580(%0)
			cache %1, 0x600(%0); cache %1, 0x680(%0)
			cache %1, 0x700(%0); cache %1, 0x780(%0)
			cache %1, 0x800(%0); cache %1, 0x880(%0)
			cache %1, 0x900(%0); cache %1, 0x980(%0)
			cache %1, 0xa00(%0); cache %1, 0xa80(%0)
			cache %1, 0xb00(%0); cache %1, 0xb80(%0)
			cache %1, 0xc00(%0); cache %1, 0xc80(%0)
			cache %1, 0xd00(%0); cache %1, 0xd80(%0)
			cache %1, 0xe00(%0); cache %1, 0xe80(%0)
			cache %1, 0xf00(%0); cache %1, 0xf80(%0)
			.set mips0
			.set reorder"
			:
			: "r" (start),
			  "i" (Index_Writeback_Inv_SD));
		start += 0x1000;
	}
}

extern inline void blast_scache128_page(unsigned long page)
{
	__asm__ __volatile__("
		.set noreorder
		.set mips3
		cache %1, 0x000(%0); cache %1, 0x080(%0)
		cache %1, 0x100(%0); cache %1, 0x180(%0)
		cache %1, 0x200(%0); cache %1, 0x280(%0)
		cache %1, 0x300(%0); cache %1, 0x380(%0)
		cache %1, 0x400(%0); cache %1, 0x480(%0)
		cache %1, 0x500(%0); cache %1, 0x580(%0)
		cache %1, 0x600(%0); cache %1, 0x680(%0)
		cache %1, 0x700(%0); cache %1, 0x780(%0)
		cache %1, 0x800(%0); cache %1, 0x880(%0)
		cache %1, 0x900(%0); cache %1, 0x980(%0)
		cache %1, 0xa00(%0); cache %1, 0xa80(%0)
		cache %1, 0xb00(%0); cache %1, 0xb80(%0)
		cache %1, 0xc00(%0); cache %1, 0xc80(%0)
		cache %1, 0xd00(%0); cache %1, 0xd80(%0)
		cache %1, 0xe00(%0); cache %1, 0xe80(%0)
		cache %1, 0xf00(%0); cache %1, 0xf80(%0)
		.set mips0
		.set reorder"
		:
		: "r" (page),
		  "i" (Hit_Writeback_Inv_D));
}

extern inline void blast_scache128_page_indexed(unsigned long page)
{
	__asm__ __volatile__("
		.set noreorder
		.set mips3
		cache %1, 0x000(%0); cache %1, 0x080(%0)
		cache %1, 0x100(%0); cache %1, 0x180(%0)
		cache %1, 0x200(%0); cache %1, 0x280(%0)
		cache %1, 0x300(%0); cache %1, 0x380(%0)
		cache %1, 0x400(%0); cache %1, 0x480(%0)
		cache %1, 0x500(%0); cache %1, 0x580(%0)
		cache %1, 0x600(%0); cache %1, 0x680(%0)
		cache %1, 0x700(%0); cache %1, 0x780(%0)
		cache %1, 0x800(%0); cache %1, 0x880(%0)
		cache %1, 0x900(%0); cache %1, 0x980(%0)
		cache %1, 0xa00(%0); cache %1, 0xa80(%0)
		cache %1, 0xb00(%0); cache %1, 0xb80(%0)
		cache %1, 0xc00(%0); cache %1, 0xc80(%0)
		cache %1, 0xd00(%0); cache %1, 0xd80(%0)
		cache %1, 0xe00(%0); cache %1, 0xe80(%0)
		cache %1, 0xf00(%0); cache %1, 0xf80(%0)
		.set mips0
		.set reorder"
		:
		: "r" (page),
		  "i" (Index_Writeback_Inv_SD));
}

#endif /* !(_MIPS_R4KCACHE_H) */
