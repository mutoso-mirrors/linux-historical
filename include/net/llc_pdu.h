#ifndef LLC_PDU_H
#define LLC_PDU_H
/*
 * Copyright (c) 1997 by Procom Technology,Inc.
 * 		 2001 by Arnaldo Carvalho de Melo <acme@conectiva.com.br>
 *
 * This program can be redistributed or modified under the terms of the
 * GNU General Public License as published by the Free Software Foundation.
 * This program is distributed without any warranty or implied warranty
 * of merchantability or fitness for a particular purpose.
 *
 * See the GNU General Public License for more details.
 */
/* LLC PDU structure */
/* Lengths of frame formats */
#define LLC_PDU_LEN_I	4       /* header and 2 control bytes */
#define LLC_PDU_LEN_S	4
#define LLC_PDU_LEN_U	3       /* header and 1 control byte */
/* Known SAP addresses */
#define LLC_GLOBAL_SAP	0xFF
#define LLC_NULL_SAP	0x00	/* not network-layer visible */
#define LLC_MGMT_INDIV	0x02	/* station LLC mgmt indiv addr */
#define LLC_MGMT_GRP	0x03	/* station LLC mgmt group addr */
#define LLC_RDE_SAP	0xA6	/* route ... */

/* SAP field bit masks */
#define LLC_ISO_RESERVED_SAP	0x02
#define LLC_SAP_GROUP_DSAP	0x01
#define LLC_SAP_RESP_SSAP	0x01

/* Group/individual DSAP indicator is DSAP field */
#define LLC_PDU_GROUP_DSAP_MASK    0x01
#define LLC_PDU_IS_GROUP_DSAP(pdu)      \
	((pdu->dsap & LLC_PDU_GROUP_DSAP_MASK) ? 0 : 1)
#define LLC_PDU_IS_INDIV_DSAP(pdu)      \
	(!(pdu->dsap & LLC_PDU_GROUP_DSAP_MASK) ? 0 : 1)

/* Command/response PDU indicator in SSAP field */
#define LLC_PDU_CMD_RSP_MASK	0x01
#define LLC_PDU_CMD		0
#define LLC_PDU_RSP		1
#define LLC_PDU_IS_CMD(pdu)    ((pdu->ssap & LLC_PDU_RSP) ? 1 : 0)
#define LLC_PDU_IS_RSP(pdu)    ((pdu->ssap & LLC_PDU_RSP) ? 0 : 1)

/* Get PDU type from 2 lowest-order bits of control field first byte */
#define LLC_PDU_TYPE_I_MASK    0x01	/* 16-bit control field */
#define LLC_PDU_TYPE_S_MASK    0x03
#define LLC_PDU_TYPE_U_MASK    0x03	/* 8-bit control field */
#define LLC_PDU_TYPE_MASK      0x03

#define LLC_PDU_TYPE_I	0	/* first bit */
#define LLC_PDU_TYPE_S	1	/* first two bits */
#define LLC_PDU_TYPE_U	3	/* first two bits */

#define LLC_PDU_TYPE_IS_I(pdu) \
	((!(pdu->ctrl_1 & LLC_PDU_TYPE_I_MASK)) ? 0 : 1)

#define LLC_PDU_TYPE_IS_U(pdu) \
	(((pdu->ctrl_1 & LLC_PDU_TYPE_U_MASK) == LLC_PDU_TYPE_U) ? 0 : 1)

#define LLC_PDU_TYPE_IS_S(pdu) \
	(((pdu->ctrl_1 & LLC_PDU_TYPE_S_MASK) == LLC_PDU_TYPE_S) ? 0 : 1)

/* U-format PDU control field masks */
#define LLC_U_PF_BIT_MASK      0x10	/* P/F bit mask */
#define LLC_U_PF_IS_1(pdu)     ((pdu->ctrl_1 & LLC_U_PF_BIT_MASK) ? 0 : 1)
#define LLC_U_PF_IS_0(pdu)     ((!(pdu->ctrl_1 & LLC_U_PF_BIT_MASK)) ? 0 : 1)

#define LLC_U_PDU_CMD_MASK     0xEC	/* cmd/rsp mask */
#define LLC_U_PDU_CMD(pdu)     (pdu->ctrl_1 & LLC_U_PDU_CMD_MASK)
#define LLC_U_PDU_RSP(pdu)     (pdu->ctrl_1 & LLC_U_PDU_CMD_MASK)

#define LLC_1_PDU_CMD_UI       0x00	/* Type 1 cmds/rsps */
#define LLC_1_PDU_CMD_XID      0xAC
#define LLC_1_PDU_CMD_TEST     0xE0

#define LLC_2_PDU_CMD_SABME    0x6C	/* Type 2 cmds/rsps */
#define LLC_2_PDU_CMD_DISC     0x40
#define LLC_2_PDU_RSP_UA       0x60
#define LLC_2_PDU_RSP_DM       0x0C
#define LLC_2_PDU_RSP_FRMR     0x84

/* Type 1 operations */

/* XID information field bit masks */

/* LLC format identifier (byte 1) */
#define LLC_XID_FMT_ID		0x81	/* first byte must be this */

/* LLC types/classes identifier (byte 2) */
#define LLC_XID_CLASS_ZEROS_MASK	0xE0	/* these must be zeros */
#define LLC_XID_CLASS_MASK		0x1F	/* AND with byte to get below */

#define LLC_XID_NULL_CLASS_1	0x01	/* if NULL LSAP...use these */
#define LLC_XID_NULL_CLASS_2	0x03
#define LLC_XID_NULL_CLASS_3	0x05
#define LLC_XID_NULL_CLASS_4	0x07

#define LLC_XID_NNULL_TYPE_1	0x01	/* if non-NULL LSAP...use these */
#define LLC_XID_NNULL_TYPE_2	0x02
#define LLC_XID_NNULL_TYPE_3	0x04
#define LLC_XID_NNULL_TYPE_1_2	0x03
#define LLC_XID_NNULL_TYPE_1_3	0x05
#define LLC_XID_NNULL_TYPE_2_3	0x06
#define LLC_XID_NNULL_ALL		0x07

/* Sender Receive Window (byte 3) */
#define LLC_XID_RW_MASK	0xFE	/* AND with value to get below */

#define LLC_XID_MIN_RW	0x02	/* lowest-order bit always zero */

/* Type 2 operations */

#define LLC_2_SEQ_NBR_MODULO   ((u8) 128)

/* I-PDU masks ('ctrl' is I-PDU control word) */
#define LLC_I_GET_NS(pdu)     (u8)((pdu->ctrl_1 & 0xFE) >> 1)
#define LLC_I_GET_NR(pdu)     (u8)((pdu->ctrl_2 & 0xFE) >> 1)

#define LLC_I_PF_BIT_MASK      0x01

#define LLC_I_PF_IS_0(pdu)     ((!(pdu->ctrl_2 & LLC_I_PF_BIT_MASK)) ? 0 : 1)
#define LLC_I_PF_IS_1(pdu)     ((pdu->ctrl_2 & LLC_I_PF_BIT_MASK) ? 0 : 1)

/* S-PDU supervisory commands and responses */

#define LLC_S_PDU_CMD_MASK     0x0C
#define LLC_S_PDU_CMD(pdu)     (pdu->ctrl_1 & LLC_S_PDU_CMD_MASK)
#define LLC_S_PDU_RSP(pdu)     (pdu->ctrl_1 & LLC_S_PDU_CMD_MASK)

#define LLC_2_PDU_CMD_RR       0x00	/* rx ready cmd */
#define LLC_2_PDU_RSP_RR       0x00	/* rx ready rsp */
#define LLC_2_PDU_CMD_REJ      0x08	/* reject PDU cmd */
#define LLC_2_PDU_RSP_REJ      0x08	/* reject PDU rsp */
#define LLC_2_PDU_CMD_RNR      0x04	/* rx not ready cmd */
#define LLC_2_PDU_RSP_RNR      0x04	/* rx not ready rsp */

#define LLC_S_PF_BIT_MASK      0x01
#define LLC_S_PF_IS_0(pdu)     ((!(pdu->ctrl_2 & LLC_S_PF_BIT_MASK)) ? 0 : 1)
#define LLC_S_PF_IS_1(pdu)     ((pdu->ctrl_2 & LLC_S_PF_BIT_MASK) ? 0 : 1)

#define PDU_SUPV_GET_Nr(pdu)   ((pdu->ctrl_2 & 0xFE) >> 1)
#define PDU_GET_NEXT_Vr(sn)    (++sn & ~LLC_2_SEQ_NBR_MODULO)

/* FRMR information field macros */

#define FRMR_INFO_LENGTH       5	/* 5 bytes of information */

/*
 * info is pointer to FRMR info field structure; 'rej_ctrl' is byte pointer
 * (if U-PDU) or word pointer to rejected PDU control field
 */
#define FRMR_INFO_SET_REJ_CNTRL(info,rej_ctrl) \
	info->rej_pdu_ctrl = ((*((u8 *) rej_ctrl) & \
				LLC_PDU_TYPE_U) != LLC_PDU_TYPE_U ? \
				(u16)*((u16 *) rej_ctrl) : \
				(((u16) *((u8 *) rej_ctrl)) & 0x00FF))

/*
 * Info is pointer to FRMR info field structure; 'vs' is a byte containing
 * send state variable value in low-order 7 bits (insure the lowest-order
 * bit remains zero (0))
 */
#define FRMR_INFO_SET_Vs(info,vs) (info->curr_ssv = (((u8) vs) << 1))
#define FRMR_INFO_SET_Vr(info,vr) (info->curr_rsv = (((u8) vr) << 1))

/*
 * Info is pointer to FRMR info field structure; 'cr' is a byte containing
 * the C/R bit value in the low-order bit
 */
#define FRMR_INFO_SET_C_R_BIT(info, cr)  (info->curr_rsv |= (((u8) cr) & 0x01))

/*
 * In the remaining five macros, 'info' is pointer to FRMR info field
 * structure; 'ind' is a byte containing the bit value to set in the
 * lowest-order bit)
 */
#define FRMR_INFO_SET_INVALID_PDU_CTRL_IND(info, ind) \
       (info->ind_bits = ((info->ind_bits & 0xFE) | (((u8) ind) & 0x01)))

#define FRMR_INFO_SET_INVALID_PDU_INFO_IND(info, ind) \
       (info->ind_bits = ( (info->ind_bits & 0xFD) | (((u8) ind) & 0x02)))

#define FRMR_INFO_SET_PDU_INFO_2LONG_IND(info, ind) \
       (info->ind_bits = ( (info->ind_bits & 0xFB) | (((u8) ind) & 0x04)))

#define FRMR_INFO_SET_PDU_INVALID_Nr_IND(info, ind) \
       (info->ind_bits = ( (info->ind_bits & 0xF7) | (((u8) ind) & 0x08)))

#define FRMR_INFO_SET_PDU_INVALID_Ns_IND(info, ind) \
       (info->ind_bits = ( (info->ind_bits & 0xEF) | (((u8) ind) & 0x10)))

/* Sequence-numbered PDU format (4 bytes in length) */
typedef struct llc_pdu_sn {
	u8 dsap;
	u8 ssap;
	u8 ctrl_1;
	u8 ctrl_2;
} llc_pdu_sn_t;

/* Un-numbered PDU format (3 bytes in length) */
typedef struct llc_pdu_un {
	u8 dsap;
	u8 ssap;
	u8 ctrl_1;
} llc_pdu_un_t;

/* LLC Type 1 XID command/response information fields format */
typedef struct llc_xid_info {
	u8 fmt_id;	/* always 0x18 for LLC */
	u8 type;	/* different if NULL/non-NULL LSAP */
	u8 rw;		/* sender receive window */
} llc_xid_info_t;

/* LLC Type 2 FRMR response information field format */
typedef struct llc_frmr_info {
	u16 rej_pdu_ctrl;	/* bits 1-8 if U-PDU */
	u8  curr_ssv;		/* current send state variable val */
	u8  curr_rsv;		/* current receive state variable */
	u8  ind_bits;		/* indicator bits set with macro */
} llc_frmr_info_t;

extern void llc_pdu_set_cmd_rsp(struct sk_buff *skb, u8 type);
extern void llc_pdu_set_pf_bit(struct sk_buff *skb, u8 bit_value);
extern int llc_pdu_decode_pf_bit(struct sk_buff *skb, u8 *pf_bit);
extern int llc_pdu_decode_cr_bit(struct sk_buff *skb, u8 *cr_bit);
extern int llc_pdu_decode_sa(struct sk_buff *skb, u8 *sa);
extern int llc_pdu_decode_da(struct sk_buff *skb, u8 *ds);
extern int llc_pdu_decode_dsap(struct sk_buff *skb, u8 *dsap);
extern int llc_pdu_decode_ssap(struct sk_buff *skb, u8 *ssap);
extern int llc_decode_pdu_type(struct sk_buff *skb, u8 *destination);
extern void llc_pdu_header_init(struct sk_buff *skb, u8 pdu_type, u8 ssap,
				u8 dsap, u8 cr);
extern int llc_pdu_init_as_ui_cmd(struct sk_buff *skb);
extern int llc_pdu_init_as_xid_cmd(struct sk_buff *skb, u8 svcs_supported,
				   u8 rx_window);
extern int llc_pdu_init_as_test_cmd(struct sk_buff *skb);
extern int llc_pdu_init_as_disc_cmd(struct sk_buff *skb, u8 p_bit);
extern int llc_pdu_init_as_i_cmd(struct sk_buff *skb, u8 p_bit, u8 ns, u8 nr);
extern int llc_pdu_init_as_rej_cmd(struct sk_buff *skb, u8 p_bit, u8 nr);
extern int llc_pdu_init_as_rnr_cmd(struct sk_buff *skb, u8 p_bit, u8 nr);
extern int llc_pdu_init_as_rr_cmd(struct sk_buff *skb, u8 p_bit, u8 nr);
extern int llc_pdu_init_as_sabme_cmd(struct sk_buff *skb, u8 p_bit);
extern int llc_pdu_init_as_dm_rsp(struct sk_buff *skb, u8 f_bit);
extern int llc_pdu_init_as_xid_rsp(struct sk_buff *skb, u8 svcs_supported,
				   u8 rx_window);
extern int llc_pdu_init_as_test_rsp(struct sk_buff *skb,
				    struct sk_buff *ev_skb);
extern int llc_pdu_init_as_frmr_rsp(struct sk_buff *skb, llc_pdu_sn_t *prev_pdu,
				    u8 f_bit, u8 vs, u8 vr, u8 vzyxw);
extern int llc_pdu_init_as_rr_rsp(struct sk_buff *skb, u8 f_bit, u8 nr);
extern int llc_pdu_init_as_rej_rsp(struct sk_buff *skb, u8 f_bit, u8 nr);
extern int llc_pdu_init_as_rnr_rsp(struct sk_buff *skb, u8 f_bit, u8 nr);
extern int llc_pdu_init_as_ua_rsp(struct sk_buff *skb, u8 f_bit);
#endif /* LLC_PDU_H */
