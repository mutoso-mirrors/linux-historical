/*
 * Linux/MIPS memory manager definitions
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Ralf Baechle
 */
#ifndef __ASM_MIPS_MMAN_H
#define __ASM_MIPS_MMAN_H

/*
 * Protections are chosen from these bits, OR'd together.  The
 * implementation does not necessarily support PROT_EXEC or PROT_WRITE
 * without PROT_READ.  The only guarantees are that no writing will be
 * allowed without PROT_WRITE and no access will be allowed for PROT_NONE.
 */
#define PROT_NONE	0x0		/* page can not be accessed */
#define PROT_READ	0x1		/* page can be read */
#define PROT_WRITE	0x2		/* page can be written */
#define PROT_EXEC	0x4		/* page can be executed */

/*
 * Flags for mmap
 */
#define MAP_SHARED	0x001		/* Share changes */
#define MAP_PRIVATE	0x002		/* Changes are private */
#define MAP_TYPE	0x00f		/* Mask for type of mapping */
#define MAP_FIXED	0x010		/* Interpret addr exactly */

/* not used by linux, but here to make sure we don't clash with ABI defines */
#define MAP_RENAME	0x020		/* Assign page to file */
#define MAP_AUTOGROW	0x040		/* File may grow by writing */
#define MAP_LOCAL	0x080		/* Copy on fork/sproc */
#define MAP_AUTORSRV	0x100		/* Logical swap reserved on demand */

/* These are linux-specific */
#define MAP_NORESERVE	0x0400		/* don't check for reservations */
#define MAP_ANONYMOUS	0x0800		/* don't use a file */
#define MAP_GROWSDOWN	0x1000		/* stack-like segment */
#define MAP_DENYWRITE	0x2000		/* ETXTBSY */
#define MAP_EXECUTABLE	0x4000		/* mark it as an executable */
#define MAP_LOCKED	0x8000		/* pages are locked */

/*
 * Flags for msync
 */
#define MS_SYNC		0		/* synchronous memory sync */
#define MS_ASYNC	1		/* sync memory asynchronously */
#define MS_INVALIDATE	2		/* invalidate mappings & caches */

/*
 * Flags for mlockall
 */
#define MCL_CURRENT	1		/* lock all current mappings */
#define MCL_FUTURE	2		/* lock all future mappings */

/* compatibility flags */
#define MAP_ANON       MAP_ANONYMOUS
#define MAP_FILE       0

#endif /* __ASM_MIPS_MMAN_H */
