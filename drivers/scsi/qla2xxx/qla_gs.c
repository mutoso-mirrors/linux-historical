/*
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include "qla_os.h"

#include "qla_def.h"

/* XXX(hch): this is ugly, but we don't want to pull in exioctl.h */
#ifndef EXT_DEF_FC4_TYPE_SCSI
#define EXT_DEF_FC4_TYPE_SCSI		0x1
#endif

static inline ms_iocb_entry_t *
qla2x00_prep_ms_iocb(scsi_qla_host_t *, uint32_t, uint32_t);

static inline struct ct_sns_req *
qla2x00_prep_ct_req(struct ct_sns_req *, uint16_t, uint16_t);

/**
 * qla2x00_prep_ms_iocb() - Prepare common MS IOCB fields for SNS CT query.
 * @ha: HA context
 * @req_size: request size in bytes
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the @ha's ms_iocb.
 */
static inline ms_iocb_entry_t *
qla2x00_prep_ms_iocb(scsi_qla_host_t *ha, uint32_t req_size, uint32_t rsp_size)
{
	ms_iocb_entry_t *ms_pkt;

	ms_pkt = ha->ms_iocb;
	memset(ms_pkt, 0, sizeof(ms_iocb_entry_t));

	ms_pkt->entry_type = MS_IOCB_TYPE;
	ms_pkt->entry_count = 1;
	SET_TARGET_ID(ha, ms_pkt->loop_id, SIMPLE_NAME_SERVER);
	ms_pkt->control_flags = __constant_cpu_to_le16(CF_READ | CF_HEAD_TAG);
	ms_pkt->timeout = __constant_cpu_to_le16(25);
	ms_pkt->cmd_dsd_count = __constant_cpu_to_le16(1);
	ms_pkt->total_dsd_count = __constant_cpu_to_le16(2);
	ms_pkt->rsp_bytecount = cpu_to_le32(rsp_size);
	ms_pkt->req_bytecount = cpu_to_le32(req_size);

	ms_pkt->dseg_req_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ms_pkt->dseg_req_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;

	ms_pkt->dseg_rsp_address[0] = cpu_to_le32(LSD(ha->ct_sns_dma));
	ms_pkt->dseg_rsp_address[1] = cpu_to_le32(MSD(ha->ct_sns_dma));
	ms_pkt->dseg_rsp_length = ms_pkt->rsp_bytecount;

	return (ms_pkt);
}

/**
 * qla2x00_prep_ct_req() - Prepare common CT request fields for SNS query.
 * @ct_req: CT request buffer
 * @cmd: GS command
 * @rsp_size: response size in bytes
 *
 * Returns a pointer to the intitialized @ct_req.
 */
static inline struct ct_sns_req *
qla2x00_prep_ct_req(struct ct_sns_req *ct_req, uint16_t cmd, uint16_t rsp_size)
{
	memset(ct_req, 0, sizeof(struct ct_sns_pkt));

	ct_req->header.revision = 0x01;
	ct_req->header.gs_type = 0xFC;
	ct_req->header.gs_subtype = 0x02;
	ct_req->command = cpu_to_be16(cmd);
	ct_req->max_rsp_size = cpu_to_be16((rsp_size - 16) / 4);

	return (ct_req);
}

/**
 * qla2x00_ga_nxt() - SNS scan for fabric devices via GA_NXT command.
 * @ha: HA context
 * @fcport: fcport entry to updated
 *
 * Returns 0 on success.
 */
int
qla2x00_ga_nxt(scsi_qla_host_t *ha, fc_port_t *fcport)
{
	int		rval;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	/* Issue GA_NXT */
	/* Prepare common MS IOCB */
	ms_pkt = qla2x00_prep_ms_iocb(ha, GA_NXT_REQ_SIZE, GA_NXT_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, GA_NXT_CMD,
	    GA_NXT_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id */
	ct_req->req.port_id.port_id[0] = fcport->d_id.b.domain;
	ct_req->req.port_id.port_id[1] = fcport->d_id.b.area;
	ct_req->req.port_id.port_id[2] = fcport->d_id.b.al_pa;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): GA_NXT issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (ct_rsp->header.response !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_3(printk("scsi(%ld): GA_NXT failed, rejected request, "
		    "ga_nxt_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
		    sizeof(struct ct_rsp_hdr)));
		rval = QLA_FUNCTION_FAILED;
	} else {
		/* Populate fc_port_t entry. */
		fcport->d_id.b.domain = ct_rsp->rsp.ga_nxt.port_id[0];
		fcport->d_id.b.area = ct_rsp->rsp.ga_nxt.port_id[1];
		fcport->d_id.b.al_pa = ct_rsp->rsp.ga_nxt.port_id[2];

		memcpy(fcport->node_name, ct_rsp->rsp.ga_nxt.node_name,
		    WWN_SIZE);
		memcpy(fcport->port_name, ct_rsp->rsp.ga_nxt.port_name,
		    WWN_SIZE);

		if (ct_rsp->rsp.ga_nxt.port_type != NS_N_PORT_TYPE &&
		    ct_rsp->rsp.ga_nxt.port_type != NS_NL_PORT_TYPE)
			fcport->d_id.b.domain = 0xf0;

		DEBUG2_3(printk("scsi(%ld): GA_NXT entry - "
		    "nn %02x%02x%02x%02x%02x%02x%02x%02x "
		    "pn %02x%02x%02x%02x%02x%02x%02x%02x "
		    "portid=%02x%02x%02x.\n",
		    ha->host_no,
		    fcport->node_name[0], fcport->node_name[1],
		    fcport->node_name[2], fcport->node_name[3],
		    fcport->node_name[4], fcport->node_name[5],
		    fcport->node_name[6], fcport->node_name[7],
		    fcport->port_name[0], fcport->port_name[1],
		    fcport->port_name[2], fcport->port_name[3],
		    fcport->port_name[4], fcport->port_name[5],
		    fcport->port_name[6], fcport->port_name[7],
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa));
	}

	return (rval);
}

/**
 * qla2x00_gid_pt() - SNS scan for fabric devices via GID_PT command.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * NOTE: Non-Nx_Ports are not requested.
 *
 * Returns 0 on success.
 */
int
qla2x00_gid_pt(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	struct ct_sns_gid_pt_data *gid_data;

	gid_data = NULL;

	/* Issue GID_PT */
	/* Prepare common MS IOCB */
	ms_pkt = qla2x00_prep_ms_iocb(ha, GID_PT_REQ_SIZE, GID_PT_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, GID_PT_CMD,
	    GID_PT_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_type */
	ct_req->req.gid_pt.port_type = NS_NX_PORT_TYPE;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): GID_PT issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (ct_rsp->header.response !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_3(printk("scsi(%ld): GID_PT failed, rejected request, "
		    "gid_pt_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
		    sizeof(struct ct_rsp_hdr)));
		rval = QLA_FUNCTION_FAILED;
	} else {
		/* Set port IDs in switch info list. */
		for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
			gid_data = &ct_rsp->rsp.gid_pt.entries[i];
			list[i].d_id.b.domain = gid_data->port_id[0];
			list[i].d_id.b.area = gid_data->port_id[1];
			list[i].d_id.b.al_pa = gid_data->port_id[2];

			/* Last one exit. */
			if (gid_data->control_byte & BIT_7) {
				list[i].d_id.b.rsvd_1 = gid_data->control_byte;
				break;
			}
		}

		/*
		 * If we've used all available slots, then the switch is
		 * reporting back more devices that we can handle with this
		 * single call.  Return a failed status, and let GA_NXT handle
		 * the overload.
		 */
		if (i == MAX_FIBRE_DEVICES) 
			rval = QLA_FUNCTION_FAILED;
	}

	return (rval);
}

/**
 * qla2x00_gpn_id() - SNS Get Port Name (GPN_ID) query.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * Returns 0 on success.
 */
int
qla2x00_gpn_id(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GPN_ID */
		/* Prepare common MS IOCB */
		ms_pkt = qla2x00_prep_ms_iocb(ha, GPN_ID_REQ_SIZE,
		    GPN_ID_RSP_SIZE);

		/* Prepare CT request */
		ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, GPN_ID_CMD,
		    GPN_ID_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GPN_ID issue IOCB failed "
			    "(%d).\n", ha->host_no, rval));
		} else if (ct_rsp->header.response !=
		    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
			DEBUG2_3(printk("scsi(%ld): GPN_ID failed, rejected "
			    "request, gpn_id_rsp:\n", ha->host_no));
			DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
			    sizeof(struct ct_rsp_hdr)));
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Save portname */
			memcpy(list[i].port_name,
			    ct_rsp->rsp.gpn_id.port_name, WWN_SIZE);
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}

	return (rval);
}

/**
 * qla2x00_gnn_id() - SNS Get Node Name (GPN_ID) query.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * Returns 0 on success.
 */
int
qla2x00_gnn_id(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GNN_ID */
		/* Prepare common MS IOCB */
		ms_pkt = qla2x00_prep_ms_iocb(ha, GNN_ID_REQ_SIZE,
		    GNN_ID_RSP_SIZE);

		/* Prepare CT request */
		ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, GNN_ID_CMD,
		    GNN_ID_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GNN_ID issue IOCB failed "
			    "(%d).\n", ha->host_no, rval));
		} else if (ct_rsp->header.response !=
		    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
			DEBUG2_3(printk("scsi(%ld): GNN_ID failed, rejected "
			    "request, gnn_id_rsp:\n", ha->host_no));
			DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
			    sizeof(struct ct_rsp_hdr)));
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Save nodename */
			memcpy(list[i].node_name,
			    ct_rsp->rsp.gnn_id.node_name, WWN_SIZE);

			DEBUG2_3(printk("scsi(%ld): GID_PT entry - "
			    "nn %02x%02x%02x%02x%02x%02x%02x%02x "
			    "pn %02x%02x%02x%02x%02x%02x%02x%02x "
			    "portid=%02x%02x%02x.\n",
			    ha->host_no,
			    list[i].node_name[0], list[i].node_name[1],
			    list[i].node_name[2], list[i].node_name[3],
			    list[i].node_name[4], list[i].node_name[5],
			    list[i].node_name[6], list[i].node_name[7],
			    list[i].port_name[0], list[i].port_name[1],
			    list[i].port_name[2], list[i].port_name[3],
			    list[i].port_name[4], list[i].port_name[5],
			    list[i].port_name[6], list[i].port_name[7],
			    list[i].d_id.b.domain, list[i].d_id.b.area,
			    list[i].d_id.b.al_pa));
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}

	return (rval);
}

/**
 * qla2x00_gft_id() - SNS Get FC-4 TYPEs (GFT_ID) query.
 * @ha: HA context
 * @list: switch info entries to populate
 *
 * Returns 0 on success.
 */
int
qla2x00_gft_id(scsi_qla_host_t *ha, sw_info_t *list)
{
	int		rval;
	uint16_t	i;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	for (i = 0; i < MAX_FIBRE_DEVICES; i++) {
		/* Issue GFT_ID */
		/* Prepare common MS IOCB */
		ms_pkt = qla2x00_prep_ms_iocb(ha, GFT_ID_REQ_SIZE,
		    GFT_ID_RSP_SIZE);

		/* Prepare CT request */
		ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, GFT_ID_CMD,
		    GFT_ID_RSP_SIZE);
		ct_rsp = &ha->ct_sns->p.rsp;

		/* Prepare CT arguments -- port_id */
		ct_req->req.port_id.port_id[0] = list[i].d_id.b.domain;
		ct_req->req.port_id.port_id[1] = list[i].d_id.b.area;
		ct_req->req.port_id.port_id[2] = list[i].d_id.b.al_pa;

		/* Execute MS IOCB */
		rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
		    sizeof(ms_iocb_entry_t));
		if (rval != QLA_SUCCESS) {
			/*EMPTY*/
			DEBUG2_3(printk("scsi(%ld): GFT_ID issue IOCB failed "
			    "(%d).\n", ha->host_no, rval));
		} else if (ct_rsp->header.response !=
		    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
			DEBUG2_3(printk("scsi(%ld): GFT_ID failed, rejected "
			    "request, gft_id_rsp:\n", ha->host_no));
			DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
			    sizeof(struct ct_rsp_hdr)));
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* FCP-3 check necessary?  No, assume FCP-3 */
			/*if (ct_rsp->rsp.gft_id.fc4_types[2] & 0x01)*/
			list[i].type = SW_TYPE_SCSI;
			if (ct_rsp->rsp.gft_id.fc4_types[3] & 0x20)
				list[i].type |= SW_TYPE_IP;
		}

		/* Last device exit. */
		if (list[i].d_id.b.rsvd_1 != 0)
			break;
	}

	return (rval);
}

/**
 * qla2x00_rft_id() - SNS Register FC-4 TYPEs (RFT_ID) supported by the HBA.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_rft_id(scsi_qla_host_t *ha)
{
	int		rval;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	/* Issue RFT_ID */
	/* Prepare common MS IOCB */
	ms_pkt = qla2x00_prep_ms_iocb(ha, RFT_ID_REQ_SIZE, RFT_ID_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, RFT_ID_CMD,
	    RFT_ID_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id, FC-4 types */
	ct_req->req.rft_id.port_id[0] = ha->d_id.b.domain;
	ct_req->req.rft_id.port_id[1] = ha->d_id.b.area;
	ct_req->req.rft_id.port_id[2] = ha->d_id.b.al_pa;

	ct_req->req.rft_id.fc4_types[2] = 0x01;		/* FCP-3 */
	ha->active_fc4_types = EXT_DEF_FC4_TYPE_SCSI;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RFT_ID issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (ct_rsp->header.response !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_3(printk("scsi(%ld): RFT_ID failed, rejected "
		    "request, rft_id_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
		    sizeof(struct ct_rsp_hdr)));
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RFT_ID exiting normally.\n",
		    ha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_rff_id() - SNS Register FC-4 Features (RFF_ID) supported by the HBA.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_rff_id(scsi_qla_host_t *ha)
{
	int		rval;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	/* Issue RFF_ID */
	/* Prepare common MS IOCB */
	ms_pkt = qla2x00_prep_ms_iocb(ha, RFF_ID_REQ_SIZE, RFF_ID_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, RFF_ID_CMD,
	    RFF_ID_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id, FC-4 feature, FC-4 type */
	ct_req->req.rff_id.port_id[0] = ha->d_id.b.domain;
	ct_req->req.rff_id.port_id[1] = ha->d_id.b.area;
	ct_req->req.rff_id.port_id[2] = ha->d_id.b.al_pa;

	ct_req->req.rff_id.fc4_type = 0x08;		/* SCSI - FCP */

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RFF_ID issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (ct_rsp->header.response !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_3(printk("scsi(%ld): RFF_ID failed, rejected "
		    "request, rff_id_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
		    sizeof(struct ct_rsp_hdr)));
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RFF_ID exiting normally.\n",
		    ha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_rnn_id() - SNS Register Node Name (RNN_ID) of the HBA.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_rnn_id(scsi_qla_host_t *ha)
{
	int		rval;

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	/* Issue RNN_ID */
	/* Prepare common MS IOCB */
	ms_pkt = qla2x00_prep_ms_iocb(ha, RNN_ID_REQ_SIZE, RNN_ID_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, RNN_ID_CMD,
	    RNN_ID_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- port_id, node_name */
	ct_req->req.rnn_id.port_id[0] = ha->d_id.b.domain;
	ct_req->req.rnn_id.port_id[1] = ha->d_id.b.area;
	ct_req->req.rnn_id.port_id[2] = ha->d_id.b.al_pa;

	memcpy(ct_req->req.rnn_id.node_name, ha->init_cb->node_name, WWN_SIZE);

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RNN_ID issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (ct_rsp->header.response !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_3(printk("scsi(%ld): RNN_ID failed, rejected "
		    "request, rnn_id_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
		    sizeof(struct ct_rsp_hdr)));
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RNN_ID exiting normally.\n",
		    ha->host_no));
	}

	return (rval);
}

/**
 * qla2x00_rsnn_nn() - SNS Register Symbolic Node Name (RSNN_NN) of the HBA.
 * @ha: HA context
 *
 * Returns 0 on success.
 */
int
qla2x00_rsnn_nn(scsi_qla_host_t *ha)
{
	int		rval;
	uint8_t		*snn;
	uint8_t		version[20];

	ms_iocb_entry_t	*ms_pkt;
	struct ct_sns_req	*ct_req;
	struct ct_sns_rsp	*ct_rsp;

	/* Issue RSNN_NN */
	/* Prepare common MS IOCB */
	/*   Request size adjusted after CT preparation */
	ms_pkt = qla2x00_prep_ms_iocb(ha, 0, RSNN_NN_RSP_SIZE);

	/* Prepare CT request */
	ct_req = qla2x00_prep_ct_req(&ha->ct_sns->p.req, RSNN_NN_CMD,
	    RSNN_NN_RSP_SIZE);
	ct_rsp = &ha->ct_sns->p.rsp;

	/* Prepare CT arguments -- node_name, symbolic node_name, size */
	memcpy(ct_req->req.rsnn_nn.node_name, ha->init_cb->node_name, WWN_SIZE);
	
	/* Prepare the Symbolic Node Name */
	/* Board type */
	snn = ct_req->req.rsnn_nn.sym_node_name;
	strcpy(snn, ha->model_number);
	/* Firmware version */
	strcat(snn, " FW:v");
	sprintf(version, "%d.%02d.%02d", ha->fw_major_version,
	    ha->fw_minor_version, ha->fw_subminor_version);
	strcat(snn, version);
	/* Driver version */
	strcat(snn, " DVR:v");
	strcat(snn, qla2x00_version_str);

	/* Calculate SNN length */
	ct_req->req.rsnn_nn.name_len = (uint8_t)strlen(snn);

	/* Update MS IOCB request */
	ms_pkt->req_bytecount =
	    cpu_to_le32(24 + 1 + ct_req->req.rsnn_nn.name_len);
	ms_pkt->dseg_req_length = ms_pkt->req_bytecount;

	/* Execute MS IOCB */
	rval = qla2x00_issue_iocb(ha, ha->ms_iocb, ha->ms_iocb_dma,
	    sizeof(ms_iocb_entry_t));
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3(printk("scsi(%ld): RSNN_NN issue IOCB failed (%d).\n",
		    ha->host_no, rval));
	} else if (ct_rsp->header.response !=
	    __constant_cpu_to_be16(CT_ACCEPT_RESPONSE)) {
		DEBUG2_3(printk("scsi(%ld): RSNN_NN failed, rejected "
		    "request, rsnn_id_rsp:\n", ha->host_no));
		DEBUG2_3(qla2x00_dump_buffer((uint8_t *)&ct_rsp->header,
		    sizeof(struct ct_rsp_hdr)));
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG2(printk("scsi(%ld): RSNN_NN exiting normally.\n",
		    ha->host_no));
	}

	return (rval);
}
