/* SCTP kernel reference Implementation
 * Copyright (c) 1999-2000 Cisco, Inc.
 * Copyright (c) 1999-2001 Motorola, Inc.
 * Copyright (c) 2001 International Business Machines, Corp.
 * 
 * This file is part of the SCTP kernel reference Implementation
 * 
 * $Header: /cvsroot/lksctp/lksctp/sctp_cvs/include/net/sctp/sctp_user.h,v 1.17 2002/07/29 21:04:23 jgrimm Exp $
 * 
 * This header represents the structures and constants needed to support
 * the SCTP Extension to the Sockets API. 
 * 
 * The SCTP reference implementation is free software; 
 * you can redistribute it and/or modify it under the terms of 
 * the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * The SCTP reference implementation is distributed in the hope that it 
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 *                 ************************
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with GNU CC; see the file COPYING.  If not, write to
 * the Free Software Foundation, 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.  
 * 
 * Please send any bug reports or fixes you make to the
 * email address(es):
 *    lksctp developers <lksctp-developers@lists.sourceforge.net>
 * 
 * Or submit a bug report through the following website:
 *    http://www.sf.net/projects/lksctp
 *
 * Written or modified by: 
 *    La Monte H.P. Yarroll    <piggy@acm.org>
 *    R. Stewart               <randall@sctp.chicago.il.us>
 *    K. Morneau               <kmorneau@cisco.com>
 *    Q. Xie                   <qxie1@email.mot.com>
 *    Karl Knutson             <karl@athena.chicago.il.us>
 *    Jon Grimm                <jgrimm@us.ibm.com>
 *    Daisy Chang              <daisyc@us.ibm.com>
 * 
 * 
 * Any bugs reported given to us we will try to fix... any fixes shared will
 * be incorporated into the next SCTP release.
 */
#include <linux/types.h>
#include <linux/socket.h>

#ifndef __net_sctp_user_h__
#define __net_sctp_user_h__


typedef void * sctp_assoc_t;

/* The following symbols come from the Sockets API Extensions for 
 * SCTP <draft-ietf-tsvwg-sctpsocket-04.txt>.
 */
enum sctp_optname {
	SCTP_RTOINFO,
#define SCTP_RTOINFO SCTP_RTOINFO
	SCTP_ASSOCRTXINFO,
#define SCTP_ASSOCRTXINFO SCTP_ASSOCRTXINFO
	SCTP_INITMSG,
#define SCTP_INITMSG SCTP_INITMSG
	SCTP_AUTO_CLOSE,
#define SCTP_AUTO_CLOSE SCTP_AUTO_CLOSE
	SCTP_SET_PRIMARY_ADDR,
#define SCTP_SET_PRIMARY_ADDR SCTP_SET_PRIMARY_ADDR
	SCTP_SET_PEER_PRIMARY_ADDR, 
#define SCTP_SET_PEER_PRIMARY_ADDR SCTP_SET_PEER_PRIMARY_ADDR
	SCTP_SET_ADAPTATION_LAYER,      
#define SCTP_SET_ADAPTATION_LAYER SCTP_SET_ADAPTATION_LAYER
	SCTP_SET_STREAM_TIMEOUTS,
#define SCTP_SET_STREAM_TIMEOUTS SCTP_SET_STREAM_TIMEOUTS
	SCTP_DISABLE_FRAGMENTS,
#define SCTP_DISABLE_FRAGMENTS SCTP_DISABLE_FRAGMENTS
	SCTP_SET_PEER_ADDR_PARAMS,
#define SCTP_SET_PEER_ADDR_PARAMS SCTP_SET_PEER_ADDR_PARAMS
	SCTP_GET_PEER_ADDR_PARAMS,
#define SCTP_GET_PEER_ADDR_PARAMS SCTP_GET_PEER_ADDR_PARAMS
	SCTP_STATUS,
#define SCTP_STATUS SCTP_STATUS
	SCTP_GET_PEER_ADDR_INFO,
#define SCTP_GET_PEER_ADDR_INFO SCTP_GET_PEER_ADDR_INFO
	SCTP_SET_EVENTS,
#define SCTP_SET_EVENTS SCTP_SET_EVENTS
	SCTP_AUTOCLOSE,
#define SCTP_AUTOCLOSE SCTP_AUTOCLOSE
	SCTP_SET_DEFAULT_SEND_PARAM,
#define SCTP_SET_DEFAULT_SEND_PARAM SCTP_SET_DEFAULT_SEND_PARAM

	SCTP_SOCKOPT_DEBUG_NAME = 42, /* FIXME */
#define SCTP_SOCKOPT_DEBUG_NAME	SCTP_SOCKOPT_DEBUG_NAME

	SCTP_SOCKOPT_BINDX_ADD, /* BINDX requests for adding addresses. */
#define SCTP_SOCKOPT_BINDX_ADD	SCTP_SOCKOPT_BINDX_ADD
	SCTP_SOCKOPT_BINDX_REM, /* BINDX requests for removing addresses. */
#define SCTP_SOCKOPT_BINDX_REM	SCTP_SOCKOPT_BINDX_REM
	SCTP_SOCKOPT_PEELOFF, 	/* peel off association. */
#define SCTP_SOCKOPT_PEELOFF	SCTP_SOCKOPT_PEELOFF
}; /* enum sctp_optname */


/* 
 * 5.2 SCTP msg_control Structures
 *
 * A key element of all SCTP-specific socket extensions is the use of
 * ancillary data to specify and access SCTP-specific data via the
 * struct msghdr's msg_control member used in sendmsg() and recvmsg().
 * Fine-grained control over initialization and sending parameters are
 * handled with ancillary data.
 *
 * Each ancillary data item is preceeded by a struct cmsghdr (see
 * Section 5.1), which defines the function and purpose of the data
 * contained in in the cmsg_data[] member.
 */

/* 
 * 5.2.1 SCTP Initiation Structure (SCTP_INIT)
 * 
 *   This cmsghdr structure provides information for initializing new
 *   SCTP associations with sendmsg().  The SCTP_INITMSG socket option
 *   uses this same data structure.  This structure is not used for
 *   recvmsg().
 *
 *   cmsg_level    cmsg_type      cmsg_data[]
 *   ------------  ------------   ----------------------
 *   IPPROTO_SCTP  SCTP_INIT      struct sctp_initmsg
 *
 */
struct sctp_initmsg {
        uint16_t sinit_num_ostreams;
        uint16_t sinit_max_instreams;
        uint16_t sinit_max_attempts;
        uint16_t sinit_max_init_timeo;
}; /* struct sctp_initmsg */


/* 
 * 5.2.2 SCTP Header Information Structure (SCTP_SNDRCV)
 * 
 *   This cmsghdr structure specifies SCTP options for sendmsg() and
 *   describes SCTP header information about a received message through
 *   recvmsg().
 *
 *   cmsg_level    cmsg_type      cmsg_data[]
 *   ------------  ------------   ----------------------
 *   IPPROTO_SCTP  SCTP_SNDRCV    struct sctp_sndrcvinfo
 *
 */
struct sctp_sndrcvinfo {
        uint16_t sinfo_stream;
        uint16_t sinfo_ssn;
        uint16_t sinfo_flags;
        uint32_t sinfo_ppid;
        uint32_t sinfo_context;
	uint32_t sinfo_timetolive;
	uint32_t sinfo_tsn;
        sctp_assoc_t sinfo_assoc_id;
}; /* struct sctp_sndrcvinfo */

/*
 *  sinfo_flags: 16 bits (unsigned integer)
 *
 *   This field may contain any of the following flags and is composed of
 *   a bitwise OR of these values.
 */

enum sctp_sinfo_flags {
	MSG_UNORDERED = 1,  /* Send/recieve message unordered. */
	MSG_ADDR_OVER = 2,  /* Override the primary destination. */
	MSG_ABORT=4,        /* Send an ABORT message to the peer. */
	/* MSG_EOF is already defined per socket.h */
}; /* enum sctp_sinfo_flags */


typedef union {
        u_int8_t   		raw;
        struct sctp_initmsg	init;
        struct sctp_sndrcvinfo	sndrcv;
} sctp_cmsg_data_t;

/* These are cmsg_types.  */
typedef enum sctp_cmsg_type {
        SCTP_INIT,              /* 5.2.1 SCTP Initiation Structure */
        SCTP_SNDRCV,            /* 5.2.2 SCTP Header Information Structure */
} sctp_cmsg_t;


/* 
 * 5.3.1.1 SCTP_ASSOC_CHANGE
 *
 *   Communication notifications inform the ULP that an SCTP association
 *   has either begun or ended. The identifier for a new association is
 *   provided by this notificaion. The notification information has the
 *   following format:
 *
 */

struct sctp_assoc_change {
	uint16_t sac_type;
	uint16_t sac_flags;
	uint32_t sac_length;
	uint16_t sac_state;
	uint16_t sac_error;
	uint16_t sac_outbound_streams;
	uint16_t sac_inbound_streams;
	sctp_assoc_t sac_assoc_id;
}; /* sctp_assoc_change */ 

/*
 *   sac_state: 32 bits (signed integer)
 *
 *   This field holds one of a number of values that communicate the
 *   event that happened to the association.  They include:
 *   
 *   Note:  The following state names deviate from the API draft as
 *   the names clash too easily with other kernel symbols.
 */
enum sctp_sac_state {
	SCTP_COMM_UP,
	SCTP_COMM_LOST,
	SCTP_RESTART,
	SCTP_SHUTDOWN_COMP,
	SCTP_CANT_STR_ASSOC,
}; /* sctp_sac_state */

/* 
 * 5.3.1.2 SCTP_PEER_ADDR_CHANGE
 *
 *   When a destination address on a multi-homed peer encounters a change
 *   an interface details event is sent.  The information has the
 *   following structure:
 */
struct sctp_paddr_change{
	uint16_t spc_type;
	uint16_t spc_flags;
	uint32_t spc_length;
	struct sockaddr_storage spc_aaddr;
	int spc_state;
	int spc_error;
	sctp_assoc_t spc_assoc_id;
}; /* sctp_paddr_change */

/*
 *    spc_state:  32 bits (signed integer)
 *
 *   This field holds one of a number of values that communicate the
 *   event that happened to the address.  They include:
 */
enum sctp_spc_state {
	ADDRESS_AVAILABLE,
	ADDRESS_UNREACHABLE,
	ADDRESS_REMOVED,
	ADDRESS_ADDED,
	ADDRESS_MADE_PRIM,
}; /* sctp_spc_state */


/* 
 * 5.3.1.3 SCTP_REMOTE_ERROR
 *
 *   A remote peer may send an Operational Error message to its peer.
 *   This message indicates a variety of error conditions on an
 *   association. The entire error TLV as it appears on the wire is
 *   included in a SCTP_REMOTE_ERROR event.  Please refer to the SCTP
 *   specification [SCTP] and any extensions for a list of possible
 *   error formats. SCTP error TLVs have the format:
 */
struct sctp_remote_error {
        uint16_t sre_type;
        uint16_t sre_flags;
        uint32_t sre_length;
        uint16_t sre_error;
        uint16_t sre_len;
        sctp_assoc_t sre_assoc_id;
        uint8_t sre_data[0];
};


/* 
 * 5.3.1.4 SCTP_SEND_FAILED
 *
 *   If SCTP cannot deliver a message it may return the message as a
 *   notification.
 */
struct sctp_send_failed {
        uint16_t ssf_type;
        uint16_t ssf_flags;
        uint32_t ssf_length;
        uint32_t ssf_error;
        struct sctp_sndrcvinfo ssf_info;
        sctp_assoc_t ssf_assoc_id;
        uint8_t ssf_data[0];
};

/*
 *   ssf_flags: 16 bits (unsigned integer)
 *
 *   The flag value will take one of the following values
 *
 *   SCTP_DATA_UNSENT  - Indicates that the data was never put on
 *                       the wire.
 *
 *   SCTP_DATA_SENT    - Indicates that the data was put on the wire.
 *                       Note that this does not necessarily mean that the
 *                       data was (or was not) successfully delivered.
 */

enum sctp_ssf_flags {
	SCTP_DATA_UNSENT,
	SCTP_DATA_SENT,
};

/*
 * 5.3.1.5 SCTP_SHUTDOWN_EVENT
 *
 *   When a peer sends a SHUTDOWN, SCTP delivers this notification to
 *   inform the application that it should cease sending data.
 */

struct sctp_shutdown_event {
        uint16_t sse_type;
        uint16_t sse_flags;
        uint32_t sse_length;
        sctp_assoc_t sse_assoc_id;
};

/* 
 * 5.3.1.6 SCTP_ADAPTION_INDICATION
 *
 *   When a peer sends a Adaption Layer Indication parameter , SCTP
 *   delivers this notification to inform the application 
 *   that of the peers requested adaption layer.
 */
struct sctp_adaption_event {
        uint16_t sai_type;
	uint16_t sai_flags;
        uint32_t sai_length;
        uint32_t sai_adaptation_bits;
        sctp_assoc_t sse_assoc_id;
};

/* 
 * 5.3.1.7 SCTP_PARTIAL_DELIVERY_EVENT
 *
 *   When a reciever is engaged in a partial delivery of a
 *   message this notification will be used to inidicate
 *   various events. 
 */

struct sctp_rcv_pdapi_event {
        uint16_t pdapi_type;
	uint16_t pdapi_flags;
        uint32_t pdapi_length;
	uint32_t pdapi_indication;
        sctp_assoc_t pdapi_assoc_id;
};


/* 
 * Described in Section 7.3 
 *   Ancillary Data and Notification Interest Options
 */
struct sctp_event_subscribe {
	uint8_t sctp_data_io_event;
	uint8_t sctp_association_event;
	uint8_t sctp_address_event;
	uint8_t sctp_send_failure_event;
	uint8_t sctp_peer_error_event;
	uint8_t sctp_shutdown_event;
	uint8_t sctp_partial_delivery_event;
	uint8_t sctp_adaption_layer_event;
}; /* struct sctp_event_subscribe */

/* 
 * 5.3.1 SCTP Notification Structure
 *
 *   The notification structure is defined as the union of all
 *   notification types.
 *
 */
union sctp_notification {
	struct {
		uint16_t sn_type;             /* Notification type. */
		uint16_t sn_flags;
		uint32_t sn_length;
	} h;
        struct sctp_assoc_change sn_assoc_change;
        struct sctp_paddr_change sn_padr_change;
        struct sctp_remote_error sn_remote_error;
	struct sctp_send_failed sn_send_failed;
        struct sctp_shutdown_event sn_shutdown_event;
	struct sctp_adaption_event sn_adaption_event;
	struct sctp_rcv_pdapi_event sn_rcv_pdapi_event;        
}; 

/* Section 5.3.1
 * All standard values for sn_type flags are greater than 2^15.
 * Values from 2^15 and down are reserved.
 */

enum sctp_sn_type {
	SCTP_SN_TYPE_BASE     = (1<<15),
	SCTP_ASSOC_CHANGE,
	SCTP_PEER_ADDR_CHANGE,
	SCTP_REMOTE_ERROR,
	SCTP_SEND_FAILED,
	SCTP_SHUTDOWN_EVENT,
	SCTP_PARTIAL_DELIVERY_EVENT,
	SCTP_ADAPTION_INDICATION,
};

/* Notification error codes used to fill up the error fields in some 
 * notifications.
 * SCTP_PEER_ADDRESS_CHAGE 	: spc_error
 * SCTP_ASSOC_CHANGE		: sac_error
 * These names should be potentially included in the draft 04 of the SCTP 
 * sockets API specification.
 */ 
typedef enum sctp_sn_error {
	SCTP_FAILED_THRESHOLD,
	SCTP_RECEIVED_SACK,
	SCTP_HEARTBEAT_SUCCESS,
	SCTP_RESPONSE_TO_USER_REQ,
	SCTP_INTERNAL_ERROR,
	SCTP_SHUTDOWN_GUARD_EXPIRES,
	SCTP_PEER_FAULTY,
} sctp_sn_error_t;

/* 
 *
 * 7.1.14 Peer Address Parameters
 *
 *   Applications can enable or disable heartbeats for any peer address
 *   of an association, modify an address's heartbeat interval, force a
 *   heartbeat to be sent immediately, and adjust the address's maximum
 *   number of retransmissions sent before an address is considered
 *   unreachable. The following structure is used to access and modify an
 *   address's parameters:
 */

struct sctp_paddrparams {
        struct sockaddr_storage spp_address;
        uint32_t                spp_hbinterval;
        uint16_t                spp_pathmaxrxt;
        sctp_assoc_t            spp_assoc_id;
};

/* 
 * 7.2.2 Peer Address Information
 *
 *   Applications can retrieve information about a specific peer address
 *   of an association, including its reachability state, congestion
 *   window, and retransmission timer values.  This information is
 *   read-only. The following structure is used to access this
 *   information:
 */

struct sctp_paddrinfo {
        sctp_assoc_t    spinfo_assoc_id;
        struct sockaddr_storage spinfo_address;
        int32_t         spinfo_state;
        uint32_t        spinfo_cwnd;
        uint32_t        spinfo_srtt;
        uint32_t        spinfo_rto;
        uint32_t        spinfo_mtu;
};


/* 
 * 7.1.1 Retransmission Timeout Parameters (SCTP_RTOINFO)
 *
 *   The protocol parameters used to initialize and bound retransmission
 *   timeout (RTO) are tunable.  See [SCTP] for more information on how
 *   these parameters are used in RTO calculation.  The peer address
 *   parameter is ignored for TCP style socket.
 */

struct sctp_rtoinfo {
        uint32_t              srto_initial;
        uint32_t              srto_max;
        uint32_t              srto_min;
        sctp_assoc_t          srto_assoc_id;
};

/* 
 * 7.1.2 Association Retransmission Parameter (SCTP_ASSOCRTXINFO)
 *
 *   The protocol parameter used to set the number of retransmissions
 *   sent before an association is considered unreachable.
 *   See [SCTP] for more information on how this parameter is used.  The
 *   peer address parameter is ignored for TCP style socket.
 */

struct sctp_assocparams {
        uint16_t        sasoc_asocmaxrxt;
        sctp_assoc_t    sasoc_assoc_id;
};


/* 
 * 7.1.9 Set Primary Address (SCTP_SET_PRIMARY_ADDR)
 *
 *  Requests that the peer mark the enclosed address as the association
 *  primary. The enclosed address must be one of the association's
 *  locally bound addresses. The following structure is used to make a
 *   set primary request:
 */

struct sctp_setprim {
        struct sockaddr_storage ssp_addr;
        sctp_assoc_t            ssp_assoc_id;
};

/* 
 * 7.1.10 Set Peer Primary Address (SCTP_SET_PEER_PRIMARY_ADDR)
 *
 *  Requests that the local SCTP stack use the enclosed peer address as
 *  the association primary. The enclosed address must be one of the
 *  association peer's addresses. The following structure is used to
 *  make a set peer primary request:
 */

struct sctp_setpeerprim {
        struct sockaddr_storage sspp_addr;
        sctp_assoc_t            sspp_assoc_id;
};

/* 
 * 7.2.1 Association Status (SCTP_STATUS)
 *
 *   Applications can retrieve current status information about an
 *   association, including association state, peer receiver window size,
 *   number of unacked data chunks, and number of data chunks pending
 *   receipt.  This information is read-only.  The following structure is
 *   used to access this information:
 */
struct sctp_status {
        sctp_assoc_t    sstat_assoc_id;
        int32_t         sstat_state;
        uint32_t        sstat_rwnd;
        uint16_t        sstat_unackdata;
        uint16_t        sstat_penddata;
        uint16_t        sstat_instrms;
        uint16_t        sstat_outstrms;
        uint32_t        sstat_fragmentation_point;
        struct sctp_paddrinfo sstat_primary;
};


/* 
 * 7.1.12 Set Adaption Layer Indicator 
 * 
 * Requests that the local endpoint set the specified Adaption Layer 
 * Indication parameter for all future
 *  INIT and INIT-ACK exchanges. 
 */

struct sctp_setadaption {
        u_int32_t   ssb_adaption_ind;
};

/* 
 * 7.1.12 Set default message time outs (SCTP_SET_STREAM_TIMEOUTS)
 * 
 * This option requests that the requested stream apply a
 *  default time-out for messages in queue.
 */
struct sctp_setstrm_timeout   {
	 sctp_assoc_t		 ssto_assoc_id;
	 u_int32_t		 ssto_timeout;
	 u_int16_t		 ssto_streamid_start;
	 u_int16_t		 ssto_streamid_end;
};


/* These are bit fields for msghdr->msg_flags.  See section 5.1.  */
/* On user space Linux, these live in <bits/socket.h> as an enum.  */
enum sctp_msg_flags {
	MSG_NOTIFICATION = 0x8000,
#define MSG_NOTIFICATION MSG_NOTIFICATION
}; /* enum sctp_msg_flags */

/* 
 * 8.1 sctp_bindx()
 *
 * The flags parameter is formed from the bitwise OR of zero or more of the 
 * following currently defined flags:
 */
#define BINDX_ADD_ADDR 0x01
#define BINDX_REM_ADDR 0x02

/* This is the structure that is passed as an argument(optval) to 
 * getsockopt(SCTP_SOCKOPT_PEELOFF).
 */
typedef struct {
	sctp_assoc_t associd;
	int sd;
} sctp_peeloff_arg_t;

#endif /* __net_sctp_user_h__ */



