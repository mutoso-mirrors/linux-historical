/* 
 *  FiberChannel transport specific attributes exported to sysfs.
 *
 *  Copyright (c) 2003 Silicon Graphics, Inc.  All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef SCSI_TRANSPORT_FC_H
#define SCSI_TRANSPORT_FC_H

#include <linux/config.h>

struct scsi_transport_template;

struct fc_starget_attrs {	/* aka fc_target_attrs */
	int port_id;
	uint64_t node_name;
	uint64_t port_name;
	uint32_t dev_loss_tmo;	/* Remote Port loss timeout in seconds. */
	struct work_struct dev_loss_work;
};

#define fc_starget_port_id(x) \
	(((struct fc_starget_attrs *)&(x)->starget_data)->port_id)
#define fc_starget_node_name(x) \
	(((struct fc_starget_attrs *)&(x)->starget_data)->node_name)
#define fc_starget_port_name(x)	\
	(((struct fc_starget_attrs *)&(x)->starget_data)->port_name)
#define fc_starget_dev_loss_tmo(x) \
	(((struct fc_starget_attrs *)&(x)->starget_data)->dev_loss_tmo)
#define fc_starget_dev_loss_work(x) \
	(((struct fc_starget_attrs *)&(x)->starget_data)->dev_loss_work)


/*
 * FC Local Port (Host) Statistics
 */

/* FC Statistics - Following FC HBAAPI v2.0 guidelines */
struct fc_host_statistics {
	/* port statistics */
	uint64_t seconds_since_last_reset;
	uint64_t tx_frames;
	uint64_t tx_words;
	uint64_t rx_frames;
	uint64_t rx_words;
	uint64_t lip_count;
	uint64_t nos_count;
	uint64_t error_frames;
	uint64_t dumped_frames;
	uint64_t link_failure_count;
	uint64_t loss_of_sync_count;
	uint64_t loss_of_signal_count;
	uint64_t prim_seq_protocol_err_count;
	uint64_t invalid_tx_word_count;
	uint64_t invalid_crc_count;
	
	/* fc4 statistics  (only FCP supported currently) */
	uint64_t fcp_input_requests;
	uint64_t fcp_output_requests;
	uint64_t fcp_control_requests;
	uint64_t fcp_input_megabytes;
	uint64_t fcp_output_megabytes;
};


struct fc_host_attrs {
	uint32_t link_down_tmo;	/* Link Down timeout in seconds. */
	struct work_struct link_down_work;
};

#define fc_host_link_down_tmo(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->link_down_tmo)
#define fc_host_link_down_work(x) \
	(((struct fc_host_attrs *)(x)->shost_data)->link_down_work)


/* The functions by which the transport class and the driver communicate */
struct fc_function_template {
	void 	(*get_starget_port_id)(struct scsi_target *);
	void	(*get_starget_node_name)(struct scsi_target *);
	void	(*get_starget_port_name)(struct scsi_target *);
	void    (*get_starget_dev_loss_tmo)(struct scsi_target *);
	void	(*set_starget_dev_loss_tmo)(struct scsi_target *, uint32_t);

	void    (*get_host_link_down_tmo)(struct Scsi_Host *);
	void	(*set_host_link_down_tmo)(struct Scsi_Host *, uint32_t);

	struct fc_host_statistics * (*get_fc_host_stats)(struct Scsi_Host *);
	void	(*reset_fc_host_stats)(struct Scsi_Host *);

	/* 
	 * The driver sets these to tell the transport class it
	 * wants the attributes displayed in sysfs.  If the show_ flag
	 * is not set, the attribute will be private to the transport
	 * class 
	 */
	unsigned long	show_starget_port_id:1;
	unsigned long	show_starget_node_name:1;
	unsigned long	show_starget_port_name:1;
	unsigned long   show_starget_dev_loss_tmo:1;

	unsigned long   show_host_link_down_tmo:1;

	/* Private Attributes */
};

struct scsi_transport_template *fc_attach_transport(struct fc_function_template *);
void fc_release_transport(struct scsi_transport_template *);
int fc_target_block(struct scsi_target *starget);
void fc_target_unblock(struct scsi_target *starget);
int fc_host_block(struct Scsi_Host *shost);
void fc_host_unblock(struct Scsi_Host *shost);

#endif /* SCSI_TRANSPORT_FC_H */
