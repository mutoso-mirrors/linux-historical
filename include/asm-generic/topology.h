/*
 * linux/include/asm-generic/topology.h
 *
 * Written by: Matthew Dobson, IBM Corporation
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <colpatch@us.ibm.com>
 */
#ifndef _ASM_GENERIC_TOPOLOGY_H
#define _ASM_GENERIC_TOPOLOGY_H

/* Other architectures wishing to use this simple topology API should fill
   in the below functions as appropriate in their own <asm/topology.h> file. */
#ifndef __cpu_to_node
#define __cpu_to_node(cpu)		(0)
#endif
#ifndef __memblk_to_node
#define __memblk_to_node(memblk)	(0)
#endif
#ifndef __parent_node
#define __parent_node(node)		(0)
#endif
#ifndef __node_to_first_cpu
#define __node_to_first_cpu(node)	(0)
#endif
#ifndef __node_to_cpu_mask
#define __node_to_cpu_mask(node)	(cpu_online_map)
#endif
#ifndef __node_to_memblk
#define __node_to_memblk(node)		(0)
#endif

/* Cross-node load balancing interval. */
#ifndef NODE_BALANCE_RATE
#define NODE_BALANCE_RATE 10
#endif

#endif /* _ASM_GENERIC_TOPOLOGY_H */
