#ifndef _ASM_I386_NODE_H_
#define _ASM_I386_NODE_H_

#include <linux/device.h>
#include <linux/mmzone.h>
#include <linux/node.h>

#include <asm/topology.h>

struct i386_node {
	struct node node;
};
extern struct i386_node node_devices[MAX_NUMNODES];

static inline void arch_register_node(int num){
	int p_node = __parent_node(num);

	if (p_node != num)
		register_node(&node_devices[num].node, num, 
			&node_devices[p_node].node);
	else
		register_node(&node_devices[num].node, num, 
			(struct node *) NULL);
}

#endif /* _ASM_I386_NODE_H_ */
