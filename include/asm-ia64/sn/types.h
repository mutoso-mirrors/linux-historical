/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1999,2001-2003 Silicon Graphics, Inc.  All Rights Reserved.
 * Copyright (C) 1999 by Ralf Baechle
 */
#ifndef _ASM_IA64_SN_TYPES_H
#define _ASM_IA64_SN_TYPES_H

typedef unsigned long 	cpuid_t;
typedef signed short	nasid_t;	/* node id in numa-as-id space */
typedef signed char	partid_t;	/* partition ID type */
typedef unsigned int    moduleid_t;     /* user-visible module number type */
typedef unsigned int    cmoduleid_t;    /* kernel compact module id type */
typedef signed char     slabid_t;
typedef unsigned char	clusterid_t;	/* Clusterid of the cell */

typedef unsigned long iopaddr_t;
typedef unsigned long paddr_t;
typedef unsigned long pfn_t;
typedef short cnodeid_t;

#endif /* _ASM_IA64_SN_TYPES_H */
