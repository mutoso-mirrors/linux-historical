/*
 * Definitions for the MIPS sysmips(2) call
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995 by Ralf Baechle
 */
#ifndef __ASM_MIPS_SYSMIPS_H
#define __ASM_MIPS_SYSMIPS_H

/*
 * Commands for the sysmips(2) call
 *
 * sysmips(2) is deprecated - though some existing software uses it.
 * We only support the following commands.
 */
#define SETNAME                    1	/* set hostname                  */
#define FLUSH_CACHE		   3	/* writeback and invalide caches */
#define MIPS_FIXADE                7	/* control address error fixing  */
#define MIPS_ATOMIC_SET		2001	/* atomically set variable       */

#endif /* __ASM_MIPS_SYSMIPS_H */
