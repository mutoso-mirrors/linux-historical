/*
 * Copyright (c) 2004, Voltaire, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * $Id: mad.c 1389 2004-12-27 22:56:47Z roland $
 */

#include <linux/dma-mapping.h>
#include <linux/interrupt.h>

#include <ib_mad.h>

#include "mad_priv.h"
#include "smi.h"
#include "agent.h"


MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("kernel IB MAD API");
MODULE_AUTHOR("Hal Rosenstock");
MODULE_AUTHOR("Sean Hefty");


kmem_cache_t *ib_mad_cache;
static struct list_head ib_mad_port_list;
static u32 ib_mad_client_id = 0;

/* Port list lock */
static spinlock_t ib_mad_port_list_lock;


/* Forward declarations */
static int method_in_use(struct ib_mad_mgmt_method_table **method,
			 struct ib_mad_reg_req *mad_reg_req);
static void remove_mad_reg_req(struct ib_mad_agent_private *priv);
static int ib_mad_post_receive_mads(struct ib_mad_qp_info *qp_info,
				    struct ib_mad_private *mad);
static void cancel_mads(struct ib_mad_agent_private *mad_agent_priv);
static void ib_mad_complete_send_wr(struct ib_mad_send_wr_private *mad_send_wr,
				    struct ib_mad_send_wc *mad_send_wc);
static void timeout_sends(void *data);
static void local_completions(void *data);
static int solicited_mad(struct ib_mad *mad);
static int add_nonoui_reg_req(struct ib_mad_reg_req *mad_reg_req,
			      struct ib_mad_agent_private *agent_priv,
			      u8 mgmt_class);
static int add_oui_reg_req(struct ib_mad_reg_req *mad_reg_req,
			   struct ib_mad_agent_private *agent_priv);

/*
 * Returns a ib_mad_port_private structure or NULL for a device/port
 * Assumes ib_mad_port_list_lock is being held
 */
static inline struct ib_mad_port_private *
__ib_get_mad_port(struct ib_device *device, int port_num)
{
	struct ib_mad_port_private *entry;

	list_for_each_entry(entry, &ib_mad_port_list, port_list) {
		if (entry->device == device && entry->port_num == port_num)
			return entry;
	}
	return NULL;
}

/*
 * Wrapper function to return a ib_mad_port_private structure or NULL
 * for a device/port
 */
static inline struct ib_mad_port_private *
ib_get_mad_port(struct ib_device *device, int port_num)
{
	struct ib_mad_port_private *entry;
	unsigned long flags;

	spin_lock_irqsave(&ib_mad_port_list_lock, flags);
	entry = __ib_get_mad_port(device, port_num);
	spin_unlock_irqrestore(&ib_mad_port_list_lock, flags);

	return entry;
}

static inline u8 convert_mgmt_class(u8 mgmt_class)
{
	/* Alias IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE to 0 */
	return mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE ?
		0 : mgmt_class;
}

static int get_spl_qp_index(enum ib_qp_type qp_type)
{
	switch (qp_type)
	{
	case IB_QPT_SMI:
		return 0;
	case IB_QPT_GSI:
		return 1;
	default:
		return -1;
	}
}

static int vendor_class_index(u8 mgmt_class)
{
	return mgmt_class - IB_MGMT_CLASS_VENDOR_RANGE2_START;
}

static int is_vendor_class(u8 mgmt_class)
{
	if ((mgmt_class < IB_MGMT_CLASS_VENDOR_RANGE2_START) ||
	    (mgmt_class > IB_MGMT_CLASS_VENDOR_RANGE2_END))
		return 0;
	return 1;
}

static int is_vendor_oui(char *oui)
{
	if (oui[0] || oui[1] || oui[2])
		return 1;
	return 0;
}

static int is_vendor_method_in_use(
		struct ib_mad_mgmt_vendor_class *vendor_class,
		struct ib_mad_reg_req *mad_reg_req)
{
	struct ib_mad_mgmt_method_table *method;
	int i;

	for (i = 0; i < MAX_MGMT_OUI; i++) {
		if (!memcmp(vendor_class->oui[i], mad_reg_req->oui, 3)) {
			method = vendor_class->method_table[i];
			if (method) {
				if (method_in_use(&method, mad_reg_req))
					return 1;
				else
					break;
			}
		}
	}
	return 0;
}

/*
 * ib_register_mad_agent - Register to send/receive MADs
 */
struct ib_mad_agent *ib_register_mad_agent(struct ib_device *device,
					   u8 port_num,
					   enum ib_qp_type qp_type,
					   struct ib_mad_reg_req *mad_reg_req,
					   u8 rmpp_version,
					   ib_mad_send_handler send_handler,
					   ib_mad_recv_handler recv_handler,
					   void *context)
{
	struct ib_mad_port_private *port_priv;
	struct ib_mad_agent *ret = ERR_PTR(-EINVAL);
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_reg_req *reg_req = NULL;
	struct ib_mad_mgmt_class_table *class;
	struct ib_mad_mgmt_vendor_class_table *vendor;
	struct ib_mad_mgmt_vendor_class *vendor_class;
	struct ib_mad_mgmt_method_table *method;
	int ret2, qpn;
	unsigned long flags;
	u8 mgmt_class, vclass;

	/* Validate parameters */
	qpn = get_spl_qp_index(qp_type);
	if (qpn == -1)
		goto error1;

	if (rmpp_version)
		goto error1;	/* XXX: until RMPP implemented */

	/* Validate MAD registration request if supplied */
	if (mad_reg_req) {
		if (mad_reg_req->mgmt_class_version >= MAX_MGMT_VERSION)
			goto error1;
		if (!recv_handler)
			goto error1;
		if (mad_reg_req->mgmt_class >= MAX_MGMT_CLASS) {
			/*
			 * IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE is the only
			 * one in this range currently allowed
			 */
			if (mad_reg_req->mgmt_class !=
			    IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)
				goto error1;
		} else if (mad_reg_req->mgmt_class == 0) {
			/*
			 * Class 0 is reserved in IBA and is used for
			 * aliasing of IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE
			 */
			goto error1;
		} else if (is_vendor_class(mad_reg_req->mgmt_class)) {
			/*
			 * If class is in "new" vendor range,
			 * ensure supplied OUI is not zero
			 */
			if (!is_vendor_oui(mad_reg_req->oui))
				goto error1;
		}
		/* Make sure class supplied is consistent with QP type */
		if (qp_type == IB_QPT_SMI) {
			if ((mad_reg_req->mgmt_class !=
					IB_MGMT_CLASS_SUBN_LID_ROUTED) &&
			    (mad_reg_req->mgmt_class !=
					IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE))
				goto error1;
		} else {
			if ((mad_reg_req->mgmt_class ==
					IB_MGMT_CLASS_SUBN_LID_ROUTED) ||
			    (mad_reg_req->mgmt_class ==
					IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE))
				goto error1;
		}
	} else {
		/* No registration request supplied */
		if (!send_handler)
			goto error1;
	}

	/* Validate device and port */
	port_priv = ib_get_mad_port(device, port_num);
	if (!port_priv) {
		ret = ERR_PTR(-ENODEV);
		goto error1;
	}

	/* Allocate structures */
	mad_agent_priv = kmalloc(sizeof *mad_agent_priv, GFP_KERNEL);
	if (!mad_agent_priv) {
		ret = ERR_PTR(-ENOMEM);
		goto error1;
	}

	if (mad_reg_req) {
		reg_req = kmalloc(sizeof *reg_req, GFP_KERNEL);
		if (!reg_req) {
			ret = ERR_PTR(-ENOMEM);
			goto error2;
		}
		/* Make a copy of the MAD registration request */
		memcpy(reg_req, mad_reg_req, sizeof *reg_req);
	}

	/* Now, fill in the various structures */
	memset(mad_agent_priv, 0, sizeof *mad_agent_priv);
	mad_agent_priv->qp_info = &port_priv->qp_info[qpn];
	mad_agent_priv->reg_req = reg_req;
	mad_agent_priv->rmpp_version = rmpp_version;
	mad_agent_priv->agent.device = device;
	mad_agent_priv->agent.recv_handler = recv_handler;
	mad_agent_priv->agent.send_handler = send_handler;
	mad_agent_priv->agent.context = context;
	mad_agent_priv->agent.qp = port_priv->qp_info[qpn].qp;
	mad_agent_priv->agent.port_num = port_num;

	spin_lock_irqsave(&port_priv->reg_lock, flags);
	mad_agent_priv->agent.hi_tid = ++ib_mad_client_id;

	/*
	 * Make sure MAD registration (if supplied)
	 * is non overlapping with any existing ones
	 */
	if (mad_reg_req) {
		mgmt_class = convert_mgmt_class(mad_reg_req->mgmt_class);
		if (!is_vendor_class(mgmt_class)) {
			class = port_priv->version[mad_reg_req->
						   mgmt_class_version].class;
			if (class) {
				method = class->method_table[mgmt_class];
				if (method) {
					if (method_in_use(&method,
							   mad_reg_req))
						goto error3;
				}
			}
			ret2 = add_nonoui_reg_req(mad_reg_req, mad_agent_priv,
						  mgmt_class);
		} else {
			/* "New" vendor class range */
			vendor = port_priv->version[mad_reg_req->
						    mgmt_class_version].vendor;
			if (vendor) {
				vclass = vendor_class_index(mgmt_class);
				vendor_class = vendor->vendor_class[vclass];
				if (vendor_class) {
					if (is_vendor_method_in_use(
							vendor_class,
							mad_reg_req))
						goto error3;
				}
			}
			ret2 = add_oui_reg_req(mad_reg_req, mad_agent_priv);
		}
		if (ret2) {
			ret = ERR_PTR(ret2);
			goto error3;
		}
	}

	/* Add mad agent into port's agent list */
	list_add_tail(&mad_agent_priv->agent_list, &port_priv->agent_list);
	spin_unlock_irqrestore(&port_priv->reg_lock, flags);

	spin_lock_init(&mad_agent_priv->lock);
	INIT_LIST_HEAD(&mad_agent_priv->send_list);
	INIT_LIST_HEAD(&mad_agent_priv->wait_list);
	INIT_WORK(&mad_agent_priv->timed_work, timeout_sends, mad_agent_priv);
	INIT_LIST_HEAD(&mad_agent_priv->local_list);
	INIT_WORK(&mad_agent_priv->local_work, local_completions,
		   mad_agent_priv);
	atomic_set(&mad_agent_priv->refcount, 1);
	init_waitqueue_head(&mad_agent_priv->wait);

	return &mad_agent_priv->agent;

error3:
	spin_unlock_irqrestore(&port_priv->reg_lock, flags);
	kfree(reg_req);
error2:
	kfree(mad_agent_priv);
error1:
	return ret;
}
EXPORT_SYMBOL(ib_register_mad_agent);

static inline int is_snooping_sends(int mad_snoop_flags)
{
	return (mad_snoop_flags &
		(/*IB_MAD_SNOOP_POSTED_SENDS |
		 IB_MAD_SNOOP_RMPP_SENDS |*/
		 IB_MAD_SNOOP_SEND_COMPLETIONS /*|
		 IB_MAD_SNOOP_RMPP_SEND_COMPLETIONS*/));
}

static inline int is_snooping_recvs(int mad_snoop_flags)
{
	return (mad_snoop_flags &
		(IB_MAD_SNOOP_RECVS /*|
		 IB_MAD_SNOOP_RMPP_RECVS*/));
}

static int register_snoop_agent(struct ib_mad_qp_info *qp_info,
				struct ib_mad_snoop_private *mad_snoop_priv)
{
	struct ib_mad_snoop_private **new_snoop_table;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&qp_info->snoop_lock, flags);
	/* Check for empty slot in array. */
	for (i = 0; i < qp_info->snoop_table_size; i++)
		if (!qp_info->snoop_table[i])
			break;

	if (i == qp_info->snoop_table_size) {
		/* Grow table. */
		new_snoop_table = kmalloc(sizeof mad_snoop_priv *
					  qp_info->snoop_table_size + 1,
					  GFP_ATOMIC);
		if (!new_snoop_table) {
			i = -ENOMEM;
			goto out;
		}
		if (qp_info->snoop_table) {
			memcpy(new_snoop_table, qp_info->snoop_table,
			       sizeof mad_snoop_priv *
			       qp_info->snoop_table_size);
			kfree(qp_info->snoop_table);
		}
		qp_info->snoop_table = new_snoop_table;
		qp_info->snoop_table_size++;
	}
	qp_info->snoop_table[i] = mad_snoop_priv;
	atomic_inc(&qp_info->snoop_count);
out:
	spin_unlock_irqrestore(&qp_info->snoop_lock, flags);
	return i;
}

struct ib_mad_agent *ib_register_mad_snoop(struct ib_device *device,
					   u8 port_num,
					   enum ib_qp_type qp_type,
					   int mad_snoop_flags,
					   ib_mad_snoop_handler snoop_handler,
					   ib_mad_recv_handler recv_handler,
					   void *context)
{
	struct ib_mad_port_private *port_priv;
	struct ib_mad_agent *ret;
	struct ib_mad_snoop_private *mad_snoop_priv;
	int qpn;

	/* Validate parameters */
	if ((is_snooping_sends(mad_snoop_flags) && !snoop_handler) ||
	    (is_snooping_recvs(mad_snoop_flags) && !recv_handler)) {
		ret = ERR_PTR(-EINVAL);
		goto error1;
	}
	qpn = get_spl_qp_index(qp_type);
	if (qpn == -1) {
		ret = ERR_PTR(-EINVAL);
		goto error1;
	}
	port_priv = ib_get_mad_port(device, port_num);
	if (!port_priv) {
		ret = ERR_PTR(-ENODEV);
		goto error1;
	}
	/* Allocate structures */
	mad_snoop_priv = kmalloc(sizeof *mad_snoop_priv, GFP_KERNEL);
	if (!mad_snoop_priv) {
		ret = ERR_PTR(-ENOMEM);
		goto error1;
	}

	/* Now, fill in the various structures */
	memset(mad_snoop_priv, 0, sizeof *mad_snoop_priv);
	mad_snoop_priv->qp_info = &port_priv->qp_info[qpn];
	mad_snoop_priv->agent.device = device;
	mad_snoop_priv->agent.recv_handler = recv_handler;
	mad_snoop_priv->agent.snoop_handler = snoop_handler;
	mad_snoop_priv->agent.context = context;
	mad_snoop_priv->agent.qp = port_priv->qp_info[qpn].qp;
	mad_snoop_priv->agent.port_num = port_num;
	mad_snoop_priv->mad_snoop_flags = mad_snoop_flags;
	init_waitqueue_head(&mad_snoop_priv->wait);
	mad_snoop_priv->snoop_index = register_snoop_agent(
						&port_priv->qp_info[qpn],
						mad_snoop_priv);
	if (mad_snoop_priv->snoop_index < 0) {
		ret = ERR_PTR(mad_snoop_priv->snoop_index);
		goto error2;
	}

	atomic_set(&mad_snoop_priv->refcount, 1);
	return &mad_snoop_priv->agent;

error2:
	kfree(mad_snoop_priv);
error1:
	return ret;
}
EXPORT_SYMBOL(ib_register_mad_snoop);

static void unregister_mad_agent(struct ib_mad_agent_private *mad_agent_priv)
{
	struct ib_mad_port_private *port_priv;
	unsigned long flags;

	/* Note that we could still be handling received MADs */

	/*
	 * Canceling all sends results in dropping received response
	 * MADs, preventing us from queuing additional work
	 */
	cancel_mads(mad_agent_priv);

	port_priv = mad_agent_priv->qp_info->port_priv;
	cancel_delayed_work(&mad_agent_priv->timed_work);
	flush_workqueue(port_priv->wq);

	spin_lock_irqsave(&port_priv->reg_lock, flags);
	remove_mad_reg_req(mad_agent_priv);
	list_del(&mad_agent_priv->agent_list);
	spin_unlock_irqrestore(&port_priv->reg_lock, flags);

	/* XXX: Cleanup pending RMPP receives for this agent */

	atomic_dec(&mad_agent_priv->refcount);
	wait_event(mad_agent_priv->wait,
		   !atomic_read(&mad_agent_priv->refcount));

	if (mad_agent_priv->reg_req)
		kfree(mad_agent_priv->reg_req);
	kfree(mad_agent_priv);
}

static void unregister_mad_snoop(struct ib_mad_snoop_private *mad_snoop_priv)
{
	struct ib_mad_qp_info *qp_info;
	unsigned long flags;

	qp_info = mad_snoop_priv->qp_info;
	spin_lock_irqsave(&qp_info->snoop_lock, flags);
	qp_info->snoop_table[mad_snoop_priv->snoop_index] = NULL;
	atomic_dec(&qp_info->snoop_count);
	spin_unlock_irqrestore(&qp_info->snoop_lock, flags);

	atomic_dec(&mad_snoop_priv->refcount);
	wait_event(mad_snoop_priv->wait,
		   !atomic_read(&mad_snoop_priv->refcount));

	kfree(mad_snoop_priv);
}

/*
 * ib_unregister_mad_agent - Unregisters a client from using MAD services
 */
int ib_unregister_mad_agent(struct ib_mad_agent *mad_agent)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_snoop_private *mad_snoop_priv;

	/* If the TID is zero, the agent can only snoop. */
	if (mad_agent->hi_tid) {
		mad_agent_priv = container_of(mad_agent,
					      struct ib_mad_agent_private,
					      agent);
		unregister_mad_agent(mad_agent_priv);
	} else {
		mad_snoop_priv = container_of(mad_agent,
					      struct ib_mad_snoop_private,
					      agent);
		unregister_mad_snoop(mad_snoop_priv);
	}
	return 0;
}
EXPORT_SYMBOL(ib_unregister_mad_agent);

static void dequeue_mad(struct ib_mad_list_head *mad_list)
{
	struct ib_mad_queue *mad_queue;
	unsigned long flags;

	BUG_ON(!mad_list->mad_queue);
	mad_queue = mad_list->mad_queue;
	spin_lock_irqsave(&mad_queue->lock, flags);
	list_del(&mad_list->list);
	mad_queue->count--;
	spin_unlock_irqrestore(&mad_queue->lock, flags);
}

static void snoop_send(struct ib_mad_qp_info *qp_info,
		       struct ib_send_wr *send_wr,
		       struct ib_mad_send_wc *mad_send_wc,
		       int mad_snoop_flags)
{
	struct ib_mad_snoop_private *mad_snoop_priv;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&qp_info->snoop_lock, flags);
	for (i = 0; i < qp_info->snoop_table_size; i++) {
		mad_snoop_priv = qp_info->snoop_table[i];
		if (!mad_snoop_priv ||
		    !(mad_snoop_priv->mad_snoop_flags & mad_snoop_flags))
			continue;

		atomic_inc(&mad_snoop_priv->refcount);
		spin_unlock_irqrestore(&qp_info->snoop_lock, flags);
		mad_snoop_priv->agent.snoop_handler(&mad_snoop_priv->agent,
						    send_wr, mad_send_wc);
		if (atomic_dec_and_test(&mad_snoop_priv->refcount))
			wake_up(&mad_snoop_priv->wait);
		spin_lock_irqsave(&qp_info->snoop_lock, flags);
	}
	spin_unlock_irqrestore(&qp_info->snoop_lock, flags);
}

static void snoop_recv(struct ib_mad_qp_info *qp_info,
		       struct ib_mad_recv_wc *mad_recv_wc,
		       int mad_snoop_flags)
{
	struct ib_mad_snoop_private *mad_snoop_priv;
	unsigned long flags;
	int i;

	spin_lock_irqsave(&qp_info->snoop_lock, flags);
	for (i = 0; i < qp_info->snoop_table_size; i++) {
		mad_snoop_priv = qp_info->snoop_table[i];
		if (!mad_snoop_priv ||
		    !(mad_snoop_priv->mad_snoop_flags & mad_snoop_flags))
			continue;

		atomic_inc(&mad_snoop_priv->refcount);
		spin_unlock_irqrestore(&qp_info->snoop_lock, flags);
		mad_snoop_priv->agent.recv_handler(&mad_snoop_priv->agent,
						   mad_recv_wc);
		if (atomic_dec_and_test(&mad_snoop_priv->refcount))
			wake_up(&mad_snoop_priv->wait);
		spin_lock_irqsave(&qp_info->snoop_lock, flags);
	}
	spin_unlock_irqrestore(&qp_info->snoop_lock, flags);
}

/*
 * Return 0 if SMP is to be sent
 * Return 1 if SMP was consumed locally (whether or not solicited)
 * Return < 0 if error
 */
static int handle_outgoing_smp(struct ib_mad_agent_private *mad_agent_priv,
			       struct ib_smp *smp,
			       struct ib_send_wr *send_wr)
{
	int ret, alloc_flags;
	unsigned long flags;
	struct ib_mad_local_private *local;
	struct ib_mad_private *mad_priv;
	struct ib_device *device = mad_agent_priv->agent.device;
	u8 port_num = mad_agent_priv->agent.port_num;

	if (!smi_handle_dr_smp_send(smp, device->node_type, port_num)) {
		ret = -EINVAL;
		printk(KERN_ERR PFX "Invalid directed route\n");
		goto out;
	}
	/* Check to post send on QP or process locally */
	ret = smi_check_local_dr_smp(smp, device, port_num);
	if (!ret || !device->process_mad)
		goto out;

	if (in_atomic() || irqs_disabled())
		alloc_flags = GFP_ATOMIC;
	else
		alloc_flags = GFP_KERNEL;
	local = kmalloc(sizeof *local, alloc_flags);
	if (!local) {
		ret = -ENOMEM;
		printk(KERN_ERR PFX "No memory for ib_mad_local_private\n");
		goto out;
	}
	local->mad_priv = NULL;
	mad_priv = kmem_cache_alloc(ib_mad_cache, alloc_flags);
	if (!mad_priv) {
		ret = -ENOMEM;
		printk(KERN_ERR PFX "No memory for local response MAD\n");
		kfree(local);
		goto out;
	}
	ret = device->process_mad(device, 0, port_num, smp->dr_slid,
				  (struct ib_mad *)smp,
				  (struct ib_mad *)&mad_priv->mad);
	switch (ret)
	{
	case IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY:
		/*
		 * See if response is solicited and
		 * there is a recv handler
		 */
		if (solicited_mad(&mad_priv->mad.mad) &&
		    mad_agent_priv->agent.recv_handler)
			local->mad_priv = mad_priv;
		else
			kmem_cache_free(ib_mad_cache, mad_priv);
		break;
	case IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_CONSUMED:
		kmem_cache_free(ib_mad_cache, mad_priv);
		break;
	case IB_MAD_RESULT_SUCCESS:
		kmem_cache_free(ib_mad_cache, mad_priv);
		kfree(local);
		ret = 0;
		goto out;
	default:
		kmem_cache_free(ib_mad_cache, mad_priv);
		kfree(local);
		ret = -EINVAL;
		goto out;
	}

	local->send_wr = *send_wr;
	local->send_wr.sg_list = local->sg_list;
	memcpy(local->sg_list, send_wr->sg_list,
	       sizeof *send_wr->sg_list * send_wr->num_sge);
	local->send_wr.next = NULL;
	local->tid = send_wr->wr.ud.mad_hdr->tid;
	local->wr_id = send_wr->wr_id;
	/* Reference MAD agent until local completion handled */
	atomic_inc(&mad_agent_priv->refcount);
	/* Queue local completion to local list */
	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	list_add_tail(&local->completion_list, &mad_agent_priv->local_list);
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
	queue_work(mad_agent_priv->qp_info->port_priv->wq,
		  &mad_agent_priv->local_work);
	ret = 1;
out:
	return ret;
}

static int ib_send_mad(struct ib_mad_agent_private *mad_agent_priv,
		       struct ib_mad_send_wr_private *mad_send_wr)
{
	struct ib_mad_qp_info *qp_info;
	struct ib_send_wr *bad_send_wr;
	unsigned long flags;
	int ret;

	/* Replace user's WR ID with our own to find WR upon completion */
	qp_info = mad_agent_priv->qp_info;
	mad_send_wr->wr_id = mad_send_wr->send_wr.wr_id;
	mad_send_wr->send_wr.wr_id = (unsigned long)&mad_send_wr->mad_list;
	mad_send_wr->mad_list.mad_queue = &qp_info->send_queue;

	spin_lock_irqsave(&qp_info->send_queue.lock, flags);
	if (qp_info->send_queue.count++ < qp_info->send_queue.max_active) {
		list_add_tail(&mad_send_wr->mad_list.list,
			      &qp_info->send_queue.list);
		spin_unlock_irqrestore(&qp_info->send_queue.lock, flags);
		ret = ib_post_send(mad_agent_priv->agent.qp,
				   &mad_send_wr->send_wr, &bad_send_wr);
		if (ret) {
			printk(KERN_ERR PFX "ib_post_send failed: %d\n", ret);
			dequeue_mad(&mad_send_wr->mad_list);
		}
	} else {
		list_add_tail(&mad_send_wr->mad_list.list,
			      &qp_info->overflow_list);
		spin_unlock_irqrestore(&qp_info->send_queue.lock, flags);
		ret = 0;
	}
	return ret;
}

/*
 * ib_post_send_mad - Posts MAD(s) to the send queue of the QP associated
 *  with the registered client
 */
int ib_post_send_mad(struct ib_mad_agent *mad_agent,
		     struct ib_send_wr *send_wr,
		     struct ib_send_wr **bad_send_wr)
{
	int ret = -EINVAL;
	struct ib_mad_agent_private *mad_agent_priv;

	/* Validate supplied parameters */
	if (!bad_send_wr)
		goto error1;

	if (!mad_agent || !send_wr)
		goto error2;

	if (!mad_agent->send_handler)
		goto error2;

	mad_agent_priv = container_of(mad_agent,
				      struct ib_mad_agent_private,
				      agent);

	/* Walk list of send WRs and post each on send list */
	while (send_wr) {
		unsigned long			flags;
		struct ib_send_wr		*next_send_wr;
		struct ib_mad_send_wr_private	*mad_send_wr;
		struct ib_smp			*smp;

		/* Validate more parameters */
		if (send_wr->num_sge > IB_MAD_SEND_REQ_MAX_SG)
			goto error2;

		if (send_wr->wr.ud.timeout_ms && !mad_agent->recv_handler)
			goto error2;

		if (!send_wr->wr.ud.mad_hdr) {
			printk(KERN_ERR PFX "MAD header must be supplied "
			       "in WR %p\n", send_wr);
			goto error2;
		}

		/*
		 * Save pointer to next work request to post in case the
		 * current one completes, and the user modifies the work
		 * request associated with the completion
		 */
		next_send_wr = (struct ib_send_wr *)send_wr->next;

		smp = (struct ib_smp *)send_wr->wr.ud.mad_hdr;
		if (smp->mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) {
			ret = handle_outgoing_smp(mad_agent_priv, smp, send_wr);
			if (ret < 0)		/* error */
				goto error2;
			else if (ret == 1)	/* locally consumed */
				goto next;
		}

		/* Allocate MAD send WR tracking structure */
		mad_send_wr = kmalloc(sizeof *mad_send_wr,
				      (in_atomic() || irqs_disabled()) ?
				      GFP_ATOMIC : GFP_KERNEL);
		if (!mad_send_wr) {
			printk(KERN_ERR PFX "No memory for "
			       "ib_mad_send_wr_private\n");
			ret = -ENOMEM;
			goto error2;
		}

		mad_send_wr->send_wr = *send_wr;
		mad_send_wr->send_wr.sg_list = mad_send_wr->sg_list;
		memcpy(mad_send_wr->sg_list, send_wr->sg_list,
		       sizeof *send_wr->sg_list * send_wr->num_sge);
		mad_send_wr->send_wr.next = NULL;
		mad_send_wr->tid = send_wr->wr.ud.mad_hdr->tid;
		mad_send_wr->agent = mad_agent;
		/* Timeout will be updated after send completes */
		mad_send_wr->timeout = msecs_to_jiffies(send_wr->wr.
							ud.timeout_ms);
		mad_send_wr->retry = 0;
		/* One reference for each work request to QP + response */
		mad_send_wr->refcount = 1 + (mad_send_wr->timeout > 0);
		mad_send_wr->status = IB_WC_SUCCESS;

		/* Reference MAD agent until send completes */
		atomic_inc(&mad_agent_priv->refcount);
		spin_lock_irqsave(&mad_agent_priv->lock, flags);
		list_add_tail(&mad_send_wr->agent_list,
			      &mad_agent_priv->send_list);
		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

		ret = ib_send_mad(mad_agent_priv, mad_send_wr);
		if (ret) {
			/* Fail send request */
			spin_lock_irqsave(&mad_agent_priv->lock, flags);
			list_del(&mad_send_wr->agent_list);
			spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
			atomic_dec(&mad_agent_priv->refcount);
			goto error2;
		}
next:
		send_wr = next_send_wr;
	}
	return 0;

error2:
	*bad_send_wr = send_wr;
error1:
	return ret;
}
EXPORT_SYMBOL(ib_post_send_mad);

/*
 * ib_free_recv_mad - Returns data buffers used to receive
 *  a MAD to the access layer
 */
void ib_free_recv_mad(struct ib_mad_recv_wc *mad_recv_wc)
{
	struct ib_mad_recv_buf *entry;
	struct ib_mad_private_header *mad_priv_hdr;
	struct ib_mad_private *priv;

	mad_priv_hdr = container_of(mad_recv_wc,
				    struct ib_mad_private_header,
				    recv_wc);
	priv = container_of(mad_priv_hdr, struct ib_mad_private, header);

	/*
	 * Walk receive buffer list associated with this WC
	 * No need to remove them from list of receive buffers
	 */
	list_for_each_entry(entry, &mad_recv_wc->recv_buf.list, list) {
		/* Free previous receive buffer */
		kmem_cache_free(ib_mad_cache, priv);
		mad_priv_hdr = container_of(mad_recv_wc,
					    struct ib_mad_private_header,
					    recv_wc);
		priv = container_of(mad_priv_hdr, struct ib_mad_private,
				    header);
	}

	/* Free last buffer */
	kmem_cache_free(ib_mad_cache, priv);
}
EXPORT_SYMBOL(ib_free_recv_mad);

void ib_coalesce_recv_mad(struct ib_mad_recv_wc *mad_recv_wc,
			  void *buf)
{
	printk(KERN_ERR PFX "ib_coalesce_recv_mad() not implemented yet\n");
}
EXPORT_SYMBOL(ib_coalesce_recv_mad);

struct ib_mad_agent *ib_redirect_mad_qp(struct ib_qp *qp,
					u8 rmpp_version,
					ib_mad_send_handler send_handler,
					ib_mad_recv_handler recv_handler,
					void *context)
{
	return ERR_PTR(-EINVAL);	/* XXX: for now */
}
EXPORT_SYMBOL(ib_redirect_mad_qp);

int ib_process_mad_wc(struct ib_mad_agent *mad_agent,
		      struct ib_wc *wc)
{
	printk(KERN_ERR PFX "ib_process_mad_wc() not implemented yet\n");
	return 0;
}
EXPORT_SYMBOL(ib_process_mad_wc);

static int method_in_use(struct ib_mad_mgmt_method_table **method,
			 struct ib_mad_reg_req *mad_reg_req)
{
	int i;

	for (i = find_first_bit(mad_reg_req->method_mask, IB_MGMT_MAX_METHODS);
	     i < IB_MGMT_MAX_METHODS;
	     i = find_next_bit(mad_reg_req->method_mask, IB_MGMT_MAX_METHODS,
			       1+i)) {
		if ((*method)->agent[i]) {
			printk(KERN_ERR PFX "Method %d already in use\n", i);
			return -EINVAL;
		}
	}
	return 0;
}

static int allocate_method_table(struct ib_mad_mgmt_method_table **method)
{
	/* Allocate management method table */
	*method = kmalloc(sizeof **method, GFP_ATOMIC);
	if (!*method) {
		printk(KERN_ERR PFX "No memory for "
		       "ib_mad_mgmt_method_table\n");
		return -ENOMEM;
	}
	/* Clear management method table */
	memset(*method, 0, sizeof **method);

	return 0;
}

/*
 * Check to see if there are any methods still in use
 */
static int check_method_table(struct ib_mad_mgmt_method_table *method)
{
	int i;

	for (i = 0; i < IB_MGMT_MAX_METHODS; i++)
		if (method->agent[i])
			return 1;
	return 0;
}

/*
 * Check to see if there are any method tables for this class still in use
 */
static int check_class_table(struct ib_mad_mgmt_class_table *class)
{
	int i;

	for (i = 0; i < MAX_MGMT_CLASS; i++)
		if (class->method_table[i])
			return 1;
	return 0;
}

static int check_vendor_class(struct ib_mad_mgmt_vendor_class *vendor_class)
{
	int i;

	for (i = 0; i < MAX_MGMT_OUI; i++)
		if (vendor_class->method_table[i])
			return 1;
	return 0;
}

static int find_vendor_oui(struct ib_mad_mgmt_vendor_class *vendor_class,
			   char *oui)
{
	int i;

	for (i = 0; i < MAX_MGMT_OUI; i++)
                /* Is there matching OUI for this vendor class ? */
                if (!memcmp(vendor_class->oui[i], oui, 3))
			return i;

	return -1;
}

static int check_vendor_table(struct ib_mad_mgmt_vendor_class_table *vendor)
{
	int i;

	for (i = 0; i < MAX_MGMT_VENDOR_RANGE2; i++)
		if (vendor->vendor_class[i])
			return 1;

	return 0;
}

static void remove_methods_mad_agent(struct ib_mad_mgmt_method_table *method,
				     struct ib_mad_agent_private *agent)
{
	int i;

	/* Remove any methods for this mad agent */
	for (i = 0; i < IB_MGMT_MAX_METHODS; i++) {
		if (method->agent[i] == agent) {
			method->agent[i] = NULL;
		}
	}
}

static int add_nonoui_reg_req(struct ib_mad_reg_req *mad_reg_req,
			      struct ib_mad_agent_private *agent_priv,
			      u8 mgmt_class)
{
	struct ib_mad_port_private *port_priv;
	struct ib_mad_mgmt_class_table **class;
	struct ib_mad_mgmt_method_table **method;
	int i, ret;

	port_priv = agent_priv->qp_info->port_priv;
	class = &port_priv->version[mad_reg_req->mgmt_class_version].class;
	if (!*class) {
		/* Allocate management class table for "new" class version */
		*class = kmalloc(sizeof **class, GFP_ATOMIC);
		if (!*class) {
			printk(KERN_ERR PFX "No memory for "
			       "ib_mad_mgmt_class_table\n");
			ret = -ENOMEM;
			goto error1;
		}
		/* Clear management class table */
		memset(*class, 0, sizeof(**class));
		/* Allocate method table for this management class */
		method = &(*class)->method_table[mgmt_class];
		if ((ret = allocate_method_table(method)))
			goto error2;
	} else {
		method = &(*class)->method_table[mgmt_class];
		if (!*method) {
			/* Allocate method table for this management class */
			if ((ret = allocate_method_table(method)))
				goto error1;
		}
	}

	/* Now, make sure methods are not already in use */
	if (method_in_use(method, mad_reg_req))
		goto error3;

	/* Finally, add in methods being registered */
	for (i = find_first_bit(mad_reg_req->method_mask,
				IB_MGMT_MAX_METHODS);
	     i < IB_MGMT_MAX_METHODS;
	     i = find_next_bit(mad_reg_req->method_mask, IB_MGMT_MAX_METHODS,
			       1+i)) {
		(*method)->agent[i] = agent_priv;
	}
	return 0;

error3:
	/* Remove any methods for this mad agent */
	remove_methods_mad_agent(*method, agent_priv);
	/* Now, check to see if there are any methods in use */
	if (!check_method_table(*method)) {
		/* If not, release management method table */
		kfree(*method);
		*method = NULL;
	}
	ret = -EINVAL;
	goto error1;
error2:
	kfree(*class);
	*class = NULL;
error1:
	return ret;
}

static int add_oui_reg_req(struct ib_mad_reg_req *mad_reg_req,
			   struct ib_mad_agent_private *agent_priv)
{
	struct ib_mad_port_private *port_priv;
	struct ib_mad_mgmt_vendor_class_table **vendor_table;
	struct ib_mad_mgmt_vendor_class_table *vendor = NULL;
	struct ib_mad_mgmt_vendor_class *vendor_class = NULL;
	struct ib_mad_mgmt_method_table **method;
	int i, ret = -ENOMEM;
	u8 vclass;

	/* "New" vendor (with OUI) class */
	vclass = vendor_class_index(mad_reg_req->mgmt_class);
	port_priv = agent_priv->qp_info->port_priv;
	vendor_table = &port_priv->version[
				mad_reg_req->mgmt_class_version].vendor;
	if (!*vendor_table) {
		/* Allocate mgmt vendor class table for "new" class version */
		vendor = kmalloc(sizeof *vendor, GFP_ATOMIC);
		if (!vendor) {
			printk(KERN_ERR PFX "No memory for "
			       "ib_mad_mgmt_vendor_class_table\n");
			goto error1;
		}
		/* Clear management vendor class table */
		memset(vendor, 0, sizeof(*vendor));
		*vendor_table = vendor;
	}
	if (!(*vendor_table)->vendor_class[vclass]) {
		/* Allocate table for this management vendor class */
		vendor_class = kmalloc(sizeof *vendor_class, GFP_ATOMIC);
		if (!vendor_class) {
			printk(KERN_ERR PFX "No memory for "
			       "ib_mad_mgmt_vendor_class\n");
			goto error2;
		}
		memset(vendor_class, 0, sizeof(*vendor_class));
		(*vendor_table)->vendor_class[vclass] = vendor_class;
	}
	for (i = 0; i < MAX_MGMT_OUI; i++) {
		/* Is there matching OUI for this vendor class ? */
		if (!memcmp((*vendor_table)->vendor_class[vclass]->oui[i],
			    mad_reg_req->oui, 3)) {
			method = &(*vendor_table)->vendor_class[
						vclass]->method_table[i];
			BUG_ON(!*method);
			goto check_in_use;
		}
	}
	for (i = 0; i < MAX_MGMT_OUI; i++) {
		/* OUI slot available ? */
		if (!is_vendor_oui((*vendor_table)->vendor_class[
				vclass]->oui[i])) {
			method = &(*vendor_table)->vendor_class[
				vclass]->method_table[i];
			BUG_ON(*method);
			/* Allocate method table for this OUI */
			if ((ret = allocate_method_table(method)))
				goto error3;
			memcpy((*vendor_table)->vendor_class[vclass]->oui[i],
			       mad_reg_req->oui, 3);
			goto check_in_use;
		}
	}
	printk(KERN_ERR PFX "All OUI slots in use\n");
	goto error3;

check_in_use:
	/* Now, make sure methods are not already in use */
	if (method_in_use(method, mad_reg_req))
		goto error4;

	/* Finally, add in methods being registered */
	for (i = find_first_bit(mad_reg_req->method_mask,
				IB_MGMT_MAX_METHODS);
	     i < IB_MGMT_MAX_METHODS;
	     i = find_next_bit(mad_reg_req->method_mask, IB_MGMT_MAX_METHODS,
			       1+i)) {
		(*method)->agent[i] = agent_priv;
	}
	return 0;

error4:
	/* Remove any methods for this mad agent */
	remove_methods_mad_agent(*method, agent_priv);
	/* Now, check to see if there are any methods in use */
	if (!check_method_table(*method)) {
		/* If not, release management method table */
		kfree(*method);
		*method = NULL;
	}
	ret = -EINVAL;
error3:
	if (vendor_class) {
		(*vendor_table)->vendor_class[vclass] = NULL;
		kfree(vendor_class);
	}
error2:
	if (vendor) {
		*vendor_table = NULL;
		kfree(vendor);
	}
error1:
	return ret;
}

static void remove_mad_reg_req(struct ib_mad_agent_private *agent_priv)
{
	struct ib_mad_port_private *port_priv;
	struct ib_mad_mgmt_class_table *class;
	struct ib_mad_mgmt_method_table *method;
	struct ib_mad_mgmt_vendor_class_table *vendor;
	struct ib_mad_mgmt_vendor_class *vendor_class;
	int index;
	u8 mgmt_class;

	/*
	 * Was MAD registration request supplied
	 * with original registration ?
	 */
	if (!agent_priv->reg_req) {
		goto out;
	}

	port_priv = agent_priv->qp_info->port_priv;
	class = port_priv->version[
			agent_priv->reg_req->mgmt_class_version].class;
	if (!class)
		goto vendor_check;

	mgmt_class = convert_mgmt_class(agent_priv->reg_req->mgmt_class);
	method = class->method_table[mgmt_class];
	if (method) {
		/* Remove any methods for this mad agent */
		remove_methods_mad_agent(method, agent_priv);
		/* Now, check to see if there are any methods still in use */
		if (!check_method_table(method)) {
			/* If not, release management method table */
			 kfree(method);
			 class->method_table[mgmt_class] = NULL;
			 /* Any management classes left ? */
			if (!check_class_table(class)) {
				/* If not, release management class table */
				kfree(class);
				port_priv->version[
					agent_priv->reg_req->
					mgmt_class_version].class = NULL;
			}
		}
	}

vendor_check:
	vendor = port_priv->version[
			agent_priv->reg_req->mgmt_class_version].vendor;
	if (!vendor)
		goto out;

	mgmt_class = vendor_class_index(agent_priv->reg_req->mgmt_class);
	vendor_class = vendor->vendor_class[mgmt_class];
	if (vendor_class) {
		index = find_vendor_oui(vendor_class, agent_priv->reg_req->oui);
		if (index == -1)
			goto out;
		method = vendor_class->method_table[index];
		if (method) {
			/* Remove any methods for this mad agent */
			remove_methods_mad_agent(method, agent_priv);
			/*
			 * Now, check to see if there are
			 * any methods still in use
			 */
			if (!check_method_table(method)) {
				/* If not, release management method table */
				kfree(method);
				vendor_class->method_table[index] = NULL;
				memset(vendor_class->oui[index], 0, 3);
				/* Any OUIs left ? */
				if (!check_vendor_class(vendor_class)) {
					/* If not, release vendor class table */
					kfree(vendor_class);
					vendor->vendor_class[mgmt_class] = NULL;
					/* Any other vendor classes left ? */
					if (!check_vendor_table(vendor)) {
						kfree(vendor);
						port_priv->version[
							agent_priv->reg_req->
							mgmt_class_version].
							vendor = NULL;
					}
				}
			}
		}
	}

out:
	return;
}

static int response_mad(struct ib_mad *mad)
{
	/* Trap represses are responses although response bit is reset */
	return ((mad->mad_hdr.method == IB_MGMT_METHOD_TRAP_REPRESS) ||
		(mad->mad_hdr.method & IB_MGMT_METHOD_RESP));
}

static int solicited_mad(struct ib_mad *mad)
{
	/* CM MADs are never solicited */
	if (mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_CM) {
		return 0;
	}

	/* XXX: Determine whether MAD is using RMPP */

	/* Not using RMPP */
	/* Is this MAD a response to a previous MAD ? */
	return response_mad(mad);
}

static struct ib_mad_agent_private *
find_mad_agent(struct ib_mad_port_private *port_priv,
	       struct ib_mad *mad,
	       int solicited)
{
	struct ib_mad_agent_private *mad_agent = NULL;
	unsigned long flags;

	spin_lock_irqsave(&port_priv->reg_lock, flags);

	/*
	 * Whether MAD was solicited determines type of routing to
	 * MAD client.
	 */
	if (solicited) {
		u32 hi_tid;
		struct ib_mad_agent_private *entry;

		/*
		 * Routing is based on high 32 bits of transaction ID
		 * of MAD.
		 */
		hi_tid = be64_to_cpu(mad->mad_hdr.tid) >> 32;
		list_for_each_entry(entry, &port_priv->agent_list,
				    agent_list) {
			if (entry->agent.hi_tid == hi_tid) {
				mad_agent = entry;
				break;
			}
		}
	} else {
		struct ib_mad_mgmt_class_table *class;
		struct ib_mad_mgmt_method_table *method;
		struct ib_mad_mgmt_vendor_class_table *vendor;
		struct ib_mad_mgmt_vendor_class *vendor_class;
		struct ib_vendor_mad *vendor_mad;
		int index;

		/*
		 * Routing is based on version, class, and method
		 * For "newer" vendor MADs, also based on OUI
		 */
		if (mad->mad_hdr.class_version >= MAX_MGMT_VERSION)
			goto out;
		if (!is_vendor_class(mad->mad_hdr.mgmt_class)) {
			class = port_priv->version[
					mad->mad_hdr.class_version].class;
			if (!class)
				goto out;
			method = class->method_table[convert_mgmt_class(
							mad->mad_hdr.mgmt_class)];
			if (method)
				mad_agent = method->agent[mad->mad_hdr.method &
							  ~IB_MGMT_METHOD_RESP];
		} else {
			vendor = port_priv->version[
					mad->mad_hdr.class_version].vendor;
			if (!vendor)
				goto out;
			vendor_class = vendor->vendor_class[vendor_class_index(
						mad->mad_hdr.mgmt_class)];
			if (!vendor_class)
				goto out;
			/* Find matching OUI */
			vendor_mad = (struct ib_vendor_mad *)mad;
			index = find_vendor_oui(vendor_class, vendor_mad->oui);
			if (index == -1)
				goto out;
			method = vendor_class->method_table[index];
			if (method) {
				mad_agent = method->agent[mad->mad_hdr.method &
							  ~IB_MGMT_METHOD_RESP];
			}
		}
	}

	if (mad_agent) {
		if (mad_agent->agent.recv_handler)
			atomic_inc(&mad_agent->refcount);
		else {
			printk(KERN_NOTICE PFX "No receive handler for client "
			       "%p on port %d\n",
			       &mad_agent->agent, port_priv->port_num);
			mad_agent = NULL;
		}
	}
out:
	spin_unlock_irqrestore(&port_priv->reg_lock, flags);

	return mad_agent;
}

static int validate_mad(struct ib_mad *mad, u32 qp_num)
{
	int valid = 0;

	/* Make sure MAD base version is understood */
	if (mad->mad_hdr.base_version != IB_MGMT_BASE_VERSION) {
		printk(KERN_ERR PFX "MAD received with unsupported base "
		       "version %d\n", mad->mad_hdr.base_version);
		goto out;
	}

	/* Filter SMI packets sent to other than QP0 */
	if ((mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_LID_ROUTED) ||
	    (mad->mad_hdr.mgmt_class == IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE)) {
		if (qp_num == 0)
			valid = 1;
	} else {
		/* Filter GSI packets sent to QP0 */
		if (qp_num != 0)
			valid = 1;
	}

out:
	return valid;
}

/*
 * Return start of fully reassembled MAD, or NULL, if MAD isn't assembled yet
 */
static struct ib_mad_private *
reassemble_recv(struct ib_mad_agent_private *mad_agent_priv,
		struct ib_mad_private *recv)
{
	/* Until we have RMPP, all receives are reassembled!... */
	INIT_LIST_HEAD(&recv->header.recv_wc.recv_buf.list);
	return recv;
}

static struct ib_mad_send_wr_private*
find_send_req(struct ib_mad_agent_private *mad_agent_priv,
	      u64 tid)
{
	struct ib_mad_send_wr_private *mad_send_wr;

	list_for_each_entry(mad_send_wr, &mad_agent_priv->wait_list,
			    agent_list) {
		if (mad_send_wr->tid == tid)
			return mad_send_wr;
	}

	/*
	 * It's possible to receive the response before we've
	 * been notified that the send has completed
	 */
	list_for_each_entry(mad_send_wr, &mad_agent_priv->send_list,
			    agent_list) {
		if (mad_send_wr->tid == tid && mad_send_wr->timeout) {
			/* Verify request has not been canceled */
			return (mad_send_wr->status == IB_WC_SUCCESS) ?
				mad_send_wr : NULL;
		}
	}
	return NULL;
}

static void ib_mad_complete_recv(struct ib_mad_agent_private *mad_agent_priv,
				 struct ib_mad_private *recv,
				 int solicited)
{
	struct ib_mad_send_wr_private *mad_send_wr;
	struct ib_mad_send_wc mad_send_wc;
	unsigned long flags;

	/* Fully reassemble receive before processing */
	recv = reassemble_recv(mad_agent_priv, recv);
	if (!recv) {
		if (atomic_dec_and_test(&mad_agent_priv->refcount))
			wake_up(&mad_agent_priv->wait);
		return;
	}

	/* Complete corresponding request */
	if (solicited) {
		spin_lock_irqsave(&mad_agent_priv->lock, flags);
		mad_send_wr = find_send_req(mad_agent_priv,
					    recv->mad.mad.mad_hdr.tid);
		if (!mad_send_wr) {
			spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
			ib_free_recv_mad(&recv->header.recv_wc);
			if (atomic_dec_and_test(&mad_agent_priv->refcount))
				wake_up(&mad_agent_priv->wait);
			return;
		}
		/* Timeout = 0 means that we won't wait for a response */
		mad_send_wr->timeout = 0;
		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

		/* Defined behavior is to complete response before request */
		recv->header.recv_wc.wc->wr_id = mad_send_wr->wr_id;
		mad_agent_priv->agent.recv_handler(
						&mad_agent_priv->agent,
						&recv->header.recv_wc);
		atomic_dec(&mad_agent_priv->refcount);

		mad_send_wc.status = IB_WC_SUCCESS;
		mad_send_wc.vendor_err = 0;
		mad_send_wc.wr_id = mad_send_wr->wr_id;
		ib_mad_complete_send_wr(mad_send_wr, &mad_send_wc);
	} else {
		mad_agent_priv->agent.recv_handler(
						&mad_agent_priv->agent,
						&recv->header.recv_wc);
		if (atomic_dec_and_test(&mad_agent_priv->refcount))
			wake_up(&mad_agent_priv->wait);
	}
}

static void ib_mad_recv_done_handler(struct ib_mad_port_private *port_priv,
				     struct ib_wc *wc)
{
	struct ib_mad_qp_info *qp_info;
	struct ib_mad_private_header *mad_priv_hdr;
	struct ib_mad_private *recv, *response;
	struct ib_mad_list_head *mad_list;
	struct ib_mad_agent_private *mad_agent;
	int solicited;

	response = kmem_cache_alloc(ib_mad_cache, GFP_KERNEL);
	if (!response)
		printk(KERN_ERR PFX "ib_mad_recv_done_handler no memory "
		       "for response buffer\n");

	mad_list = (struct ib_mad_list_head *)(unsigned long)wc->wr_id;
	qp_info = mad_list->mad_queue->qp_info;
	dequeue_mad(mad_list);

	mad_priv_hdr = container_of(mad_list, struct ib_mad_private_header,
				    mad_list);
	recv = container_of(mad_priv_hdr, struct ib_mad_private, header);
	dma_unmap_single(port_priv->device->dma_device,
			 pci_unmap_addr(&recv->header, mapping),
			 sizeof(struct ib_mad_private) -
			 sizeof(struct ib_mad_private_header),
			 DMA_FROM_DEVICE);

	/* Setup MAD receive work completion from "normal" work completion */
	recv->header.recv_wc.wc = wc;
	recv->header.recv_wc.mad_len = sizeof(struct ib_mad);
	recv->header.recv_wc.recv_buf.mad = &recv->mad.mad;
	recv->header.recv_wc.recv_buf.grh = &recv->grh;

	if (atomic_read(&qp_info->snoop_count))
		snoop_recv(qp_info, &recv->header.recv_wc, IB_MAD_SNOOP_RECVS);

	/* Validate MAD */
	if (!validate_mad(&recv->mad.mad, qp_info->qp->qp_num))
		goto out;

	if (recv->mad.mad.mad_hdr.mgmt_class ==
	    IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE) {
		if (!smi_handle_dr_smp_recv(&recv->mad.smp,
					    port_priv->device->node_type,
					    port_priv->port_num,
					    port_priv->device->phys_port_cnt))
			goto out;
		if (!smi_check_forward_dr_smp(&recv->mad.smp))
			goto local;
		if (!smi_handle_dr_smp_send(&recv->mad.smp,
					    port_priv->device->node_type,
					    port_priv->port_num))
			goto out;
		if (!smi_check_local_dr_smp(&recv->mad.smp,
					    port_priv->device,
					    port_priv->port_num))
			goto out;
	}

local:
	/* Give driver "right of first refusal" on incoming MAD */
	if (port_priv->device->process_mad) {
		int ret;

		if (!response) {
			printk(KERN_ERR PFX "No memory for response MAD\n");
			/*
			 * Is it better to assume that
			 * it wouldn't be processed ?
			 */
			goto out;
		}

		ret = port_priv->device->process_mad(port_priv->device, 0,
						     port_priv->port_num,
						     wc->slid,
						     &recv->mad.mad,
						     &response->mad.mad);
		if (ret & IB_MAD_RESULT_SUCCESS) {
			if (ret & IB_MAD_RESULT_CONSUMED)
				goto out;
			if (ret & IB_MAD_RESULT_REPLY) {
				/* Send response */
				if (!agent_send(response, &recv->grh, wc,
						port_priv->device,
						port_priv->port_num))
					response = NULL;
				goto out;
			}
		}
	}

	/* Determine corresponding MAD agent for incoming receive MAD */
	solicited = solicited_mad(&recv->mad.mad);
	mad_agent = find_mad_agent(port_priv, &recv->mad.mad, solicited);
	if (mad_agent) {
		ib_mad_complete_recv(mad_agent, recv, solicited);
		/*
		 * recv is freed up in error cases in ib_mad_complete_recv
		 * or via recv_handler in ib_mad_complete_recv()
		 */
		recv = NULL;
	}

out:
	/* Post another receive request for this QP */
	if (response) {
		ib_mad_post_receive_mads(qp_info, response);
		if (recv)
			kmem_cache_free(ib_mad_cache, recv);
	} else
		ib_mad_post_receive_mads(qp_info, recv);
}

static void adjust_timeout(struct ib_mad_agent_private *mad_agent_priv)
{
	struct ib_mad_send_wr_private *mad_send_wr;
	unsigned long delay;

	if (list_empty(&mad_agent_priv->wait_list)) {
		cancel_delayed_work(&mad_agent_priv->timed_work);
	} else {
		mad_send_wr = list_entry(mad_agent_priv->wait_list.next,
					 struct ib_mad_send_wr_private,
					 agent_list);

		if (time_after(mad_agent_priv->timeout,
			       mad_send_wr->timeout)) {
			mad_agent_priv->timeout = mad_send_wr->timeout;
			cancel_delayed_work(&mad_agent_priv->timed_work);
			delay = mad_send_wr->timeout - jiffies;
			if ((long)delay <= 0)
				delay = 1;
			queue_delayed_work(mad_agent_priv->qp_info->
					   port_priv->wq,
					   &mad_agent_priv->timed_work, delay);
		}
	}
}

static void wait_for_response(struct ib_mad_agent_private *mad_agent_priv,
			      struct ib_mad_send_wr_private *mad_send_wr )
{
	struct ib_mad_send_wr_private *temp_mad_send_wr;
	struct list_head *list_item;
	unsigned long delay;

	list_del(&mad_send_wr->agent_list);

	delay = mad_send_wr->timeout;
	mad_send_wr->timeout += jiffies;

	list_for_each_prev(list_item, &mad_agent_priv->wait_list) {
		temp_mad_send_wr = list_entry(list_item,
					      struct ib_mad_send_wr_private,
					      agent_list);
		if (time_after(mad_send_wr->timeout,
			       temp_mad_send_wr->timeout))
			break;
	}
	list_add(&mad_send_wr->agent_list, list_item);

	/* Reschedule a work item if we have a shorter timeout */
	if (mad_agent_priv->wait_list.next == &mad_send_wr->agent_list) {
		cancel_delayed_work(&mad_agent_priv->timed_work);
		queue_delayed_work(mad_agent_priv->qp_info->port_priv->wq,
				   &mad_agent_priv->timed_work, delay);
	}
}

/*
 * Process a send work completion
 */
static void ib_mad_complete_send_wr(struct ib_mad_send_wr_private *mad_send_wr,
				    struct ib_mad_send_wc *mad_send_wc)
{
	struct ib_mad_agent_private	*mad_agent_priv;
	unsigned long			flags;

	mad_agent_priv = container_of(mad_send_wr->agent,
				      struct ib_mad_agent_private, agent);

	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	if (mad_send_wc->status != IB_WC_SUCCESS &&
	    mad_send_wr->status == IB_WC_SUCCESS) {
		mad_send_wr->status = mad_send_wc->status;
		mad_send_wr->refcount -= (mad_send_wr->timeout > 0);
	}

	if (--mad_send_wr->refcount > 0) {
		if (mad_send_wr->refcount == 1 && mad_send_wr->timeout &&
		    mad_send_wr->status == IB_WC_SUCCESS) {
			wait_for_response(mad_agent_priv, mad_send_wr);
		}
		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
		return;
	}

	/* Remove send from MAD agent and notify client of completion */
	list_del(&mad_send_wr->agent_list);
	adjust_timeout(mad_agent_priv);
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

	if (mad_send_wr->status != IB_WC_SUCCESS )
		mad_send_wc->status = mad_send_wr->status;
	mad_agent_priv->agent.send_handler(&mad_agent_priv->agent,
					    mad_send_wc);

	/* Release reference on agent taken when sending */
	if (atomic_dec_and_test(&mad_agent_priv->refcount))
		wake_up(&mad_agent_priv->wait);

	kfree(mad_send_wr);
}

static void ib_mad_send_done_handler(struct ib_mad_port_private *port_priv,
				     struct ib_wc *wc)
{
	struct ib_mad_send_wr_private	*mad_send_wr, *queued_send_wr;
	struct ib_mad_list_head		*mad_list;
	struct ib_mad_qp_info		*qp_info;
	struct ib_mad_queue		*send_queue;
	struct ib_send_wr		*bad_send_wr;
	unsigned long flags;
	int ret;

	mad_list = (struct ib_mad_list_head *)(unsigned long)wc->wr_id;
	mad_send_wr = container_of(mad_list, struct ib_mad_send_wr_private,
				   mad_list);
	send_queue = mad_list->mad_queue;
	qp_info = send_queue->qp_info;

retry:
	queued_send_wr = NULL;
	spin_lock_irqsave(&send_queue->lock, flags);
	list_del(&mad_list->list);

	/* Move queued send to the send queue */
	if (send_queue->count-- > send_queue->max_active) {
		mad_list = container_of(qp_info->overflow_list.next,
					struct ib_mad_list_head, list);
		queued_send_wr = container_of(mad_list,
					struct ib_mad_send_wr_private,
					mad_list);
		list_del(&mad_list->list);
		list_add_tail(&mad_list->list, &send_queue->list);
	}
	spin_unlock_irqrestore(&send_queue->lock, flags);

	/* Restore client wr_id in WC and complete send */
	wc->wr_id = mad_send_wr->wr_id;
	if (atomic_read(&qp_info->snoop_count))
		snoop_send(qp_info, &mad_send_wr->send_wr,
			   (struct ib_mad_send_wc *)wc,
			   IB_MAD_SNOOP_SEND_COMPLETIONS);
	ib_mad_complete_send_wr(mad_send_wr, (struct ib_mad_send_wc *)wc);

	if (queued_send_wr) {
		ret = ib_post_send(qp_info->qp, &queued_send_wr->send_wr,
				&bad_send_wr);
		if (ret) {
			printk(KERN_ERR PFX "ib_post_send failed: %d\n", ret);
			mad_send_wr = queued_send_wr;
			wc->status = IB_WC_LOC_QP_OP_ERR;
			goto retry;
		}
	}
}

static void mark_sends_for_retry(struct ib_mad_qp_info *qp_info)
{
	struct ib_mad_send_wr_private *mad_send_wr;
	struct ib_mad_list_head *mad_list;
	unsigned long flags;

	spin_lock_irqsave(&qp_info->send_queue.lock, flags);
	list_for_each_entry(mad_list, &qp_info->send_queue.list, list) {
		mad_send_wr = container_of(mad_list,
					   struct ib_mad_send_wr_private,
					   mad_list);
		mad_send_wr->retry = 1;
	}
	spin_unlock_irqrestore(&qp_info->send_queue.lock, flags);
}

static void mad_error_handler(struct ib_mad_port_private *port_priv,
			      struct ib_wc *wc)
{
	struct ib_mad_list_head *mad_list;
	struct ib_mad_qp_info *qp_info;
	struct ib_mad_send_wr_private *mad_send_wr;
	int ret;

	/* Determine if failure was a send or receive */
	mad_list = (struct ib_mad_list_head *)(unsigned long)wc->wr_id;
	qp_info = mad_list->mad_queue->qp_info;
	if (mad_list->mad_queue == &qp_info->recv_queue)
		/*
		 * Receive errors indicate that the QP has entered the error
		 * state - error handling/shutdown code will cleanup
		 */
		return;

	/*
	 * Send errors will transition the QP to SQE - move
	 * QP to RTS and repost flushed work requests
	 */
	mad_send_wr = container_of(mad_list, struct ib_mad_send_wr_private,
				   mad_list);
	if (wc->status == IB_WC_WR_FLUSH_ERR) {
		if (mad_send_wr->retry) {
			/* Repost send */
			struct ib_send_wr *bad_send_wr;

			mad_send_wr->retry = 0;
			ret = ib_post_send(qp_info->qp, &mad_send_wr->send_wr,
					&bad_send_wr);
			if (ret)
				ib_mad_send_done_handler(port_priv, wc);
		} else
			ib_mad_send_done_handler(port_priv, wc);
	} else {
		struct ib_qp_attr *attr;

		/* Transition QP to RTS and fail offending send */
		attr = kmalloc(sizeof *attr, GFP_KERNEL);
		if (attr) {
			attr->qp_state = IB_QPS_RTS;
			attr->cur_qp_state = IB_QPS_SQE;
			ret = ib_modify_qp(qp_info->qp, attr,
					   IB_QP_STATE | IB_QP_CUR_STATE);
			kfree(attr);
			if (ret)
				printk(KERN_ERR PFX "mad_error_handler - "
				       "ib_modify_qp to RTS : %d\n", ret);
			else
				mark_sends_for_retry(qp_info);
		}
		ib_mad_send_done_handler(port_priv, wc);
	}
}

/*
 * IB MAD completion callback
 */
static void ib_mad_completion_handler(void *data)
{
	struct ib_mad_port_private *port_priv;
	struct ib_wc wc;

	port_priv = (struct ib_mad_port_private *)data;
	ib_req_notify_cq(port_priv->cq, IB_CQ_NEXT_COMP);

	while (ib_poll_cq(port_priv->cq, 1, &wc) == 1) {
		if (wc.status == IB_WC_SUCCESS) {
			switch (wc.opcode) {
			case IB_WC_SEND:
				ib_mad_send_done_handler(port_priv, &wc);
				break;
			case IB_WC_RECV:
				ib_mad_recv_done_handler(port_priv, &wc);
				break;
			default:
				BUG_ON(1);
				break;
			}
		} else
			mad_error_handler(port_priv, &wc);
	}
}

static void cancel_mads(struct ib_mad_agent_private *mad_agent_priv)
{
	unsigned long flags;
	struct ib_mad_send_wr_private *mad_send_wr, *temp_mad_send_wr;
	struct ib_mad_send_wc mad_send_wc;
	struct list_head cancel_list;

	INIT_LIST_HEAD(&cancel_list);

	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	list_for_each_entry_safe(mad_send_wr, temp_mad_send_wr,
				 &mad_agent_priv->send_list, agent_list) {
		if (mad_send_wr->status == IB_WC_SUCCESS) {
 			mad_send_wr->status = IB_WC_WR_FLUSH_ERR;
			mad_send_wr->refcount -= (mad_send_wr->timeout > 0);
		}
	}

	/* Empty wait list to prevent receives from finding a request */
	list_splice_init(&mad_agent_priv->wait_list, &cancel_list);
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

	/* Report all cancelled requests */
	mad_send_wc.status = IB_WC_WR_FLUSH_ERR;
	mad_send_wc.vendor_err = 0;

	list_for_each_entry_safe(mad_send_wr, temp_mad_send_wr,
				 &cancel_list, agent_list) {
		mad_send_wc.wr_id = mad_send_wr->wr_id;
		mad_agent_priv->agent.send_handler(&mad_agent_priv->agent,
						   &mad_send_wc);

		list_del(&mad_send_wr->agent_list);
		kfree(mad_send_wr);
		atomic_dec(&mad_agent_priv->refcount);
	}
}

static struct ib_mad_send_wr_private*
find_send_by_wr_id(struct ib_mad_agent_private *mad_agent_priv,
		   u64 wr_id)
{
	struct ib_mad_send_wr_private *mad_send_wr;

	list_for_each_entry(mad_send_wr, &mad_agent_priv->wait_list,
			    agent_list) {
		if (mad_send_wr->wr_id == wr_id)
			return mad_send_wr;
	}

	list_for_each_entry(mad_send_wr, &mad_agent_priv->send_list,
			    agent_list) {
		if (mad_send_wr->wr_id == wr_id)
			return mad_send_wr;
	}
	return NULL;
}

void ib_cancel_mad(struct ib_mad_agent *mad_agent,
		  u64 wr_id)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_send_wr_private *mad_send_wr;
	struct ib_mad_send_wc mad_send_wc;
	unsigned long flags;

	mad_agent_priv = container_of(mad_agent, struct ib_mad_agent_private,
				      agent);
	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	mad_send_wr = find_send_by_wr_id(mad_agent_priv, wr_id);
	if (!mad_send_wr) {
		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
		goto out;
	}

	if (mad_send_wr->status == IB_WC_SUCCESS)
		mad_send_wr->refcount -= (mad_send_wr->timeout > 0);

	if (mad_send_wr->refcount != 0) {
		mad_send_wr->status = IB_WC_WR_FLUSH_ERR;
		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
		goto out;
	}

	list_del(&mad_send_wr->agent_list);
	adjust_timeout(mad_agent_priv);
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

	mad_send_wc.status = IB_WC_WR_FLUSH_ERR;
	mad_send_wc.vendor_err = 0;
	mad_send_wc.wr_id = mad_send_wr->wr_id;
	mad_agent_priv->agent.send_handler(&mad_agent_priv->agent,
					   &mad_send_wc);

	kfree(mad_send_wr);
	if (atomic_dec_and_test(&mad_agent_priv->refcount))
		wake_up(&mad_agent_priv->wait);

out:
	return;
}
EXPORT_SYMBOL(ib_cancel_mad);

static void local_completions(void *data)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_local_private *local;
	unsigned long flags;
	struct ib_wc wc;
	struct ib_mad_send_wc mad_send_wc;

	mad_agent_priv = (struct ib_mad_agent_private *)data;

	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	while (!list_empty(&mad_agent_priv->local_list)) {
		local = list_entry(mad_agent_priv->local_list.next,
				   struct ib_mad_local_private,
				   completion_list);
		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
		if (local->mad_priv) {
			/*
			 * Defined behavior is to complete response
			 * before request
			 */
			wc.wr_id = local->wr_id;
			wc.status = IB_WC_SUCCESS;
			wc.opcode = IB_WC_RECV;
			wc.vendor_err = 0;
			wc.byte_len = sizeof(struct ib_mad) +
				      sizeof(struct ib_grh);
			wc.src_qp = IB_QP0;
			wc.wc_flags = 0;	/* No GRH */
			wc.pkey_index = 0;
			wc.slid = IB_LID_PERMISSIVE;
			wc.sl = 0;
			wc.dlid_path_bits = 0;
			local->mad_priv->header.recv_wc.wc = &wc;
			local->mad_priv->header.recv_wc.mad_len =
						sizeof(struct ib_mad);
			INIT_LIST_HEAD(&local->mad_priv->header.recv_wc.recv_buf.list);
			local->mad_priv->header.recv_wc.recv_buf.grh = NULL;
			local->mad_priv->header.recv_wc.recv_buf.mad =
						&local->mad_priv->mad.mad;
			if (atomic_read(&mad_agent_priv->qp_info->snoop_count))
				snoop_recv(mad_agent_priv->qp_info,
					  &local->mad_priv->header.recv_wc,
					   IB_MAD_SNOOP_RECVS);
			mad_agent_priv->agent.recv_handler(
						&mad_agent_priv->agent,
						&local->mad_priv->header.recv_wc);
		}

		/* Complete send */
		mad_send_wc.status = IB_WC_SUCCESS;
		mad_send_wc.vendor_err = 0;
		mad_send_wc.wr_id = local->wr_id;
		if (atomic_read(&mad_agent_priv->qp_info->snoop_count))
			snoop_send(mad_agent_priv->qp_info, &local->send_wr,
				  &mad_send_wc,
				   IB_MAD_SNOOP_SEND_COMPLETIONS);
		mad_agent_priv->agent.send_handler(&mad_agent_priv->agent,
						   &mad_send_wc);

		spin_lock_irqsave(&mad_agent_priv->lock, flags);
		list_del(&local->completion_list);
		atomic_dec(&mad_agent_priv->refcount);
		kfree(local);
	}
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
}

static void timeout_sends(void *data)
{
	struct ib_mad_agent_private *mad_agent_priv;
	struct ib_mad_send_wr_private *mad_send_wr;
	struct ib_mad_send_wc mad_send_wc;
	unsigned long flags, delay;

	mad_agent_priv = (struct ib_mad_agent_private *)data;

	mad_send_wc.status = IB_WC_RESP_TIMEOUT_ERR;
	mad_send_wc.vendor_err = 0;

	spin_lock_irqsave(&mad_agent_priv->lock, flags);
	while (!list_empty(&mad_agent_priv->wait_list)) {
		mad_send_wr = list_entry(mad_agent_priv->wait_list.next,
					 struct ib_mad_send_wr_private,
					 agent_list);

		if (time_after(mad_send_wr->timeout, jiffies)) {
			delay = mad_send_wr->timeout - jiffies;
			if ((long)delay <= 0)
				delay = 1;
			queue_delayed_work(mad_agent_priv->qp_info->
					   port_priv->wq,
					   &mad_agent_priv->timed_work, delay);
			break;
		}

		list_del(&mad_send_wr->agent_list);
		spin_unlock_irqrestore(&mad_agent_priv->lock, flags);

		mad_send_wc.wr_id = mad_send_wr->wr_id;
		mad_agent_priv->agent.send_handler(&mad_agent_priv->agent,
						   &mad_send_wc);

		kfree(mad_send_wr);
		atomic_dec(&mad_agent_priv->refcount);
		spin_lock_irqsave(&mad_agent_priv->lock, flags);
	}
	spin_unlock_irqrestore(&mad_agent_priv->lock, flags);
}

static void ib_mad_thread_completion_handler(struct ib_cq *cq)
{
	struct ib_mad_port_private *port_priv = cq->cq_context;

	queue_work(port_priv->wq, &port_priv->work);
}

/*
 * Allocate receive MADs and post receive WRs for them
 */
static int ib_mad_post_receive_mads(struct ib_mad_qp_info *qp_info,
				    struct ib_mad_private *mad)
{
	unsigned long flags;
	int post, ret;
	struct ib_mad_private *mad_priv;
	struct ib_sge sg_list;
	struct ib_recv_wr recv_wr, *bad_recv_wr;
	struct ib_mad_queue *recv_queue = &qp_info->recv_queue;

	/* Initialize common scatter list fields */
	sg_list.length = sizeof *mad_priv - sizeof mad_priv->header;
	sg_list.lkey = (*qp_info->port_priv->mr).lkey;

	/* Initialize common receive WR fields */
	recv_wr.next = NULL;
	recv_wr.sg_list = &sg_list;
	recv_wr.num_sge = 1;
	recv_wr.recv_flags = IB_RECV_SIGNALED;

	do {
		/* Allocate and map receive buffer */
		if (mad) {
			mad_priv = mad;
			mad = NULL;
		} else {
			mad_priv = kmem_cache_alloc(ib_mad_cache, GFP_KERNEL);
			if (!mad_priv) {
				printk(KERN_ERR PFX "No memory for receive buffer\n");
				ret = -ENOMEM;
				break;
			}
		}
		sg_list.addr = dma_map_single(qp_info->port_priv->
						device->dma_device,
					&mad_priv->grh,
					sizeof *mad_priv -
						sizeof mad_priv->header,
					DMA_FROM_DEVICE);
		pci_unmap_addr_set(&mad_priv->header, mapping, sg_list.addr);
		recv_wr.wr_id = (unsigned long)&mad_priv->header.mad_list;
		mad_priv->header.mad_list.mad_queue = recv_queue;

		/* Post receive WR */
		spin_lock_irqsave(&recv_queue->lock, flags);
		post = (++recv_queue->count < recv_queue->max_active);
		list_add_tail(&mad_priv->header.mad_list.list, &recv_queue->list);
		spin_unlock_irqrestore(&recv_queue->lock, flags);
		ret = ib_post_recv(qp_info->qp, &recv_wr, &bad_recv_wr);
		if (ret) {
			spin_lock_irqsave(&recv_queue->lock, flags);
			list_del(&mad_priv->header.mad_list.list);
			recv_queue->count--;
			spin_unlock_irqrestore(&recv_queue->lock, flags);
			dma_unmap_single(qp_info->port_priv->device->dma_device,
					 pci_unmap_addr(&mad_priv->header,
							mapping),
					 sizeof *mad_priv -
					   sizeof mad_priv->header,
					 DMA_FROM_DEVICE);
			kmem_cache_free(ib_mad_cache, mad_priv);
			printk(KERN_ERR PFX "ib_post_recv failed: %d\n", ret);
			break;
		}
	} while (post);

	return ret;
}

/*
 * Return all the posted receive MADs
 */
static void cleanup_recv_queue(struct ib_mad_qp_info *qp_info)
{
	struct ib_mad_private_header *mad_priv_hdr;
	struct ib_mad_private *recv;
	struct ib_mad_list_head *mad_list;

	while (!list_empty(&qp_info->recv_queue.list)) {

		mad_list = list_entry(qp_info->recv_queue.list.next,
				      struct ib_mad_list_head, list);
		mad_priv_hdr = container_of(mad_list,
					    struct ib_mad_private_header,
					    mad_list);
		recv = container_of(mad_priv_hdr, struct ib_mad_private,
				    header);

		/* Remove from posted receive MAD list */
		list_del(&mad_list->list);

		/* Undo PCI mapping */
		dma_unmap_single(qp_info->port_priv->device->dma_device,
				 pci_unmap_addr(&recv->header, mapping),
				 sizeof(struct ib_mad_private) -
				 sizeof(struct ib_mad_private_header),
				 DMA_FROM_DEVICE);
		kmem_cache_free(ib_mad_cache, recv);
	}

	qp_info->recv_queue.count = 0;
}

/*
 * Start the port
 */
static int ib_mad_port_start(struct ib_mad_port_private *port_priv)
{
	int ret, i;
	struct ib_qp_attr *attr;
	struct ib_qp *qp;

	attr = kmalloc(sizeof *attr, GFP_KERNEL);
 	if (!attr) {
		printk(KERN_ERR PFX "Couldn't kmalloc ib_qp_attr\n");
		return -ENOMEM;
	}

	for (i = 0; i < IB_MAD_QPS_CORE; i++) {
		qp = port_priv->qp_info[i].qp;
		/*
		 * PKey index for QP1 is irrelevant but
		 * one is needed for the Reset to Init transition
		 */
		attr->qp_state = IB_QPS_INIT;
		attr->pkey_index = 0;
		attr->qkey = (qp->qp_num == 0) ? 0 : IB_QP1_QKEY;
		ret = ib_modify_qp(qp, attr, IB_QP_STATE |
					     IB_QP_PKEY_INDEX | IB_QP_QKEY);
		if (ret) {
			printk(KERN_ERR PFX "Couldn't change QP%d state to "
			       "INIT: %d\n", i, ret);
			goto out;
		}

		attr->qp_state = IB_QPS_RTR;
		ret = ib_modify_qp(qp, attr, IB_QP_STATE);
		if (ret) {
			printk(KERN_ERR PFX "Couldn't change QP%d state to "
			       "RTR: %d\n", i, ret);
			goto out;
		}

		attr->qp_state = IB_QPS_RTS;
		attr->sq_psn = IB_MAD_SEND_Q_PSN;
		ret = ib_modify_qp(qp, attr, IB_QP_STATE | IB_QP_SQ_PSN);
		if (ret) {
			printk(KERN_ERR PFX "Couldn't change QP%d state to "
			       "RTS: %d\n", i, ret);
			goto out;
		}
	}

	ret = ib_req_notify_cq(port_priv->cq, IB_CQ_NEXT_COMP);
	if (ret) {
		printk(KERN_ERR PFX "Failed to request completion "
		       "notification: %d\n", ret);
		goto out;
	}

	for (i = 0; i < IB_MAD_QPS_CORE; i++) {
		ret = ib_mad_post_receive_mads(&port_priv->qp_info[i], NULL);
		if (ret) {
			printk(KERN_ERR PFX "Couldn't post receive WRs\n");
			goto out;
		}
	}
out:
	kfree(attr);
	return ret;
}

static void qp_event_handler(struct ib_event *event, void *qp_context)
{
	struct ib_mad_qp_info	*qp_info = qp_context;

	/* It's worse than that! He's dead, Jim! */
	printk(KERN_ERR PFX "Fatal error (%d) on MAD QP (%d)\n",
		event->event, qp_info->qp->qp_num);
}

static void init_mad_queue(struct ib_mad_qp_info *qp_info,
			   struct ib_mad_queue *mad_queue)
{
	mad_queue->qp_info = qp_info;
	mad_queue->count = 0;
	spin_lock_init(&mad_queue->lock);
	INIT_LIST_HEAD(&mad_queue->list);
}

static void init_mad_qp(struct ib_mad_port_private *port_priv,
			struct ib_mad_qp_info *qp_info)
{
	qp_info->port_priv = port_priv;
	init_mad_queue(qp_info, &qp_info->send_queue);
	init_mad_queue(qp_info, &qp_info->recv_queue);
	INIT_LIST_HEAD(&qp_info->overflow_list);
	spin_lock_init(&qp_info->snoop_lock);
	qp_info->snoop_table = NULL;
	qp_info->snoop_table_size = 0;
	atomic_set(&qp_info->snoop_count, 0);
}

static int create_mad_qp(struct ib_mad_qp_info *qp_info,
			 enum ib_qp_type qp_type)
{
	struct ib_qp_init_attr	qp_init_attr;
	int ret;

	memset(&qp_init_attr, 0, sizeof qp_init_attr);
	qp_init_attr.send_cq = qp_info->port_priv->cq;
	qp_init_attr.recv_cq = qp_info->port_priv->cq;
	qp_init_attr.sq_sig_type = IB_SIGNAL_ALL_WR;
	qp_init_attr.rq_sig_type = IB_SIGNAL_ALL_WR;
	qp_init_attr.cap.max_send_wr = IB_MAD_QP_SEND_SIZE;
	qp_init_attr.cap.max_recv_wr = IB_MAD_QP_RECV_SIZE;
	qp_init_attr.cap.max_send_sge = IB_MAD_SEND_REQ_MAX_SG;
	qp_init_attr.cap.max_recv_sge = IB_MAD_RECV_REQ_MAX_SG;
	qp_init_attr.qp_type = qp_type;
	qp_init_attr.port_num = qp_info->port_priv->port_num;
	qp_init_attr.qp_context = qp_info;
	qp_init_attr.event_handler = qp_event_handler;
	qp_info->qp = ib_create_qp(qp_info->port_priv->pd, &qp_init_attr);
	if (IS_ERR(qp_info->qp)) {
		printk(KERN_ERR PFX "Couldn't create ib_mad QP%d\n",
		       get_spl_qp_index(qp_type));
		ret = PTR_ERR(qp_info->qp);
		goto error;
	}
	/* Use minimum queue sizes unless the CQ is resized */
	qp_info->send_queue.max_active = IB_MAD_QP_SEND_SIZE;
	qp_info->recv_queue.max_active = IB_MAD_QP_RECV_SIZE;
	return 0;

error:
	return ret;
}

static void destroy_mad_qp(struct ib_mad_qp_info *qp_info)
{
	ib_destroy_qp(qp_info->qp);
	if (qp_info->snoop_table)
		kfree(qp_info->snoop_table);
}

/*
 * Open the port
 * Create the QP, PD, MR, and CQ if needed
 */
static int ib_mad_port_open(struct ib_device *device,
			    int port_num)
{
	int ret, cq_size;
	struct ib_mad_port_private *port_priv;
	unsigned long flags;
	char name[sizeof "ib_mad123"];

	/* First, check if port already open at MAD layer */
	port_priv = ib_get_mad_port(device, port_num);
	if (port_priv) {
		printk(KERN_DEBUG PFX "%s port %d already open\n",
		       device->name, port_num);
		return 0;
	}

	/* Create new device info */
	port_priv = kmalloc(sizeof *port_priv, GFP_KERNEL);
	if (!port_priv) {
		printk(KERN_ERR PFX "No memory for ib_mad_port_private\n");
		return -ENOMEM;
	}
	memset(port_priv, 0, sizeof *port_priv);
	port_priv->device = device;
	port_priv->port_num = port_num;
	spin_lock_init(&port_priv->reg_lock);
	INIT_LIST_HEAD(&port_priv->agent_list);
	init_mad_qp(port_priv, &port_priv->qp_info[0]);
	init_mad_qp(port_priv, &port_priv->qp_info[1]);

	cq_size = (IB_MAD_QP_SEND_SIZE + IB_MAD_QP_RECV_SIZE) * 2;
	port_priv->cq = ib_create_cq(port_priv->device,
				     (ib_comp_handler)
					ib_mad_thread_completion_handler,
				     NULL, port_priv, cq_size);
	if (IS_ERR(port_priv->cq)) {
		printk(KERN_ERR PFX "Couldn't create ib_mad CQ\n");
		ret = PTR_ERR(port_priv->cq);
		goto error3;
	}

	port_priv->pd = ib_alloc_pd(device);
	if (IS_ERR(port_priv->pd)) {
		printk(KERN_ERR PFX "Couldn't create ib_mad PD\n");
		ret = PTR_ERR(port_priv->pd);
		goto error4;
	}

	port_priv->mr = ib_get_dma_mr(port_priv->pd, IB_ACCESS_LOCAL_WRITE);
	if (IS_ERR(port_priv->mr)) {
		printk(KERN_ERR PFX "Couldn't get ib_mad DMA MR\n");
		ret = PTR_ERR(port_priv->mr);
		goto error5;
	}

	ret = create_mad_qp(&port_priv->qp_info[0], IB_QPT_SMI);
	if (ret)
		goto error6;
	ret = create_mad_qp(&port_priv->qp_info[1], IB_QPT_GSI);
	if (ret)
		goto error7;

	snprintf(name, sizeof name, "ib_mad%d", port_num);
	port_priv->wq = create_singlethread_workqueue(name);
	if (!port_priv->wq) {
		ret = -ENOMEM;
		goto error8;
	}
	INIT_WORK(&port_priv->work, ib_mad_completion_handler, port_priv);

	ret = ib_mad_port_start(port_priv);
	if (ret) {
		printk(KERN_ERR PFX "Couldn't start port\n");
		goto error9;
	}

	spin_lock_irqsave(&ib_mad_port_list_lock, flags);
	list_add_tail(&port_priv->port_list, &ib_mad_port_list);
	spin_unlock_irqrestore(&ib_mad_port_list_lock, flags);
	return 0;

error9:
	destroy_workqueue(port_priv->wq);
error8:
	destroy_mad_qp(&port_priv->qp_info[1]);
error7:
	destroy_mad_qp(&port_priv->qp_info[0]);
error6:
	ib_dereg_mr(port_priv->mr);
error5:
	ib_dealloc_pd(port_priv->pd);
error4:
	ib_destroy_cq(port_priv->cq);
	cleanup_recv_queue(&port_priv->qp_info[1]);
	cleanup_recv_queue(&port_priv->qp_info[0]);
error3:
	kfree(port_priv);

	return ret;
}

/*
 * Close the port
 * If there are no classes using the port, free the port
 * resources (CQ, MR, PD, QP) and remove the port's info structure
 */
static int ib_mad_port_close(struct ib_device *device, int port_num)
{
	struct ib_mad_port_private *port_priv;
	unsigned long flags;

	spin_lock_irqsave(&ib_mad_port_list_lock, flags);
	port_priv = __ib_get_mad_port(device, port_num);
	if (port_priv == NULL) {
		spin_unlock_irqrestore(&ib_mad_port_list_lock, flags);
		printk(KERN_ERR PFX "Port %d not found\n", port_num);
		return -ENODEV;
	}
	list_del(&port_priv->port_list);
	spin_unlock_irqrestore(&ib_mad_port_list_lock, flags);

	/* Stop processing completions. */
	flush_workqueue(port_priv->wq);
	destroy_workqueue(port_priv->wq);
	destroy_mad_qp(&port_priv->qp_info[1]);
	destroy_mad_qp(&port_priv->qp_info[0]);
	ib_dereg_mr(port_priv->mr);
	ib_dealloc_pd(port_priv->pd);
	ib_destroy_cq(port_priv->cq);
	cleanup_recv_queue(&port_priv->qp_info[1]);
	cleanup_recv_queue(&port_priv->qp_info[0]);
	/* XXX: Handle deallocation of MAD registration tables */

	kfree(port_priv);

	return 0;
}

static void ib_mad_init_device(struct ib_device *device)
{
	int ret, num_ports, cur_port, i, ret2;

	if (device->node_type == IB_NODE_SWITCH) {
		num_ports = 1;
		cur_port = 0;
	} else {
		num_ports = device->phys_port_cnt;
		cur_port = 1;
	}
	for (i = 0; i < num_ports; i++, cur_port++) {
		ret = ib_mad_port_open(device, cur_port);
		if (ret) {
			printk(KERN_ERR PFX "Couldn't open %s port %d\n",
			       device->name, cur_port);
			goto error_device_open;
		}
		ret = ib_agent_port_open(device, cur_port);
		if (ret) {
			printk(KERN_ERR PFX "Couldn't open %s port %d "
			       "for agents\n",
			       device->name, cur_port);
			goto error_device_open;
		}
	}

	goto error_device_query;

error_device_open:
	while (i > 0) {
		cur_port--;
		ret2 = ib_agent_port_close(device, cur_port);
		if (ret2) {
			printk(KERN_ERR PFX "Couldn't close %s port %d "
			       "for agents\n",
			       device->name, cur_port);
		}
		ret2 = ib_mad_port_close(device, cur_port);
		if (ret2) {
			printk(KERN_ERR PFX "Couldn't close %s port %d\n",
			       device->name, cur_port);
		}
		i--;
	}

error_device_query:
	return;
}

static void ib_mad_remove_device(struct ib_device *device)
{
	int ret = 0, i, num_ports, cur_port, ret2;

	if (device->node_type == IB_NODE_SWITCH) {
		num_ports = 1;
		cur_port = 0;
	} else {
		num_ports = device->phys_port_cnt;
		cur_port = 1;
	}
	for (i = 0; i < num_ports; i++, cur_port++) {
		ret2 = ib_agent_port_close(device, cur_port);
		if (ret2) {
			printk(KERN_ERR PFX "Couldn't close %s port %d "
			       "for agents\n",
			       device->name, cur_port);
			if (!ret)
				ret = ret2;
		}
		ret2 = ib_mad_port_close(device, cur_port);
		if (ret2) {
			printk(KERN_ERR PFX "Couldn't close %s port %d\n",
			       device->name, cur_port);
			if (!ret)
				ret = ret2;
		}
	}
}

static struct ib_client mad_client = {
	.name   = "mad",
	.add = ib_mad_init_device,
	.remove = ib_mad_remove_device
};

static int __init ib_mad_init_module(void)
{
	int ret;

	spin_lock_init(&ib_mad_port_list_lock);
	spin_lock_init(&ib_agent_port_list_lock);

	ib_mad_cache = kmem_cache_create("ib_mad",
					 sizeof(struct ib_mad_private),
					 0,
					 SLAB_HWCACHE_ALIGN,
					 NULL,
					 NULL);
	if (!ib_mad_cache) {
		printk(KERN_ERR PFX "Couldn't create ib_mad cache\n");
		ret = -ENOMEM;
		goto error1;
	}

	INIT_LIST_HEAD(&ib_mad_port_list);

	if (ib_register_client(&mad_client)) {
		printk(KERN_ERR PFX "Couldn't register ib_mad client\n");
		ret = -EINVAL;
		goto error2;
	}

	return 0;

error2:
	kmem_cache_destroy(ib_mad_cache);
error1:
	return ret;
}

static void __exit ib_mad_cleanup_module(void)
{
	ib_unregister_client(&mad_client);

	if (kmem_cache_destroy(ib_mad_cache)) {
		printk(KERN_DEBUG PFX "Failed to destroy ib_mad cache\n");
	}
}

module_init(ib_mad_init_module);
module_exit(ib_mad_cleanup_module);
