/************************************************************************
 * s2io.h: A Linux PCI-X Ethernet driver for S2IO 10GbE Server NIC
 * Copyright 2002 Raghavendra Koushik (raghavendra.koushik@s2io.com)

 * This software may be used and distributed according to the terms of
 * the GNU General Public License (GPL), incorporated herein by reference.
 * Drivers based on or derived from this code fall under the GPL and must
 * retain the authorship, copyright and license notice.  This file is not
 * a complete program and may only be used when the entire operating
 * system is licensed under the GPL.
 * See the file COPYING in this distribution for more information.
 ************************************************************************/
#ifndef _S2IO_H
#define _S2IO_H

#define TBD 0
#define BIT(loc)		(0x8000000000000000ULL >> (loc))
#define vBIT(val, loc, sz)	(((u64)val) << (64-loc-sz))
#define INV(d)  ((d&0xff)<<24) | (((d>>8)&0xff)<<16) | (((d>>16)&0xff)<<8)| ((d>>24)&0xff)

#ifndef BOOL
#define BOOL    int
#endif

#ifndef TRUE
#define TRUE    1
#define FALSE   0
#endif

#undef SUCCESS
#define SUCCESS 0
#define FAILURE -1

/* Maximum outstanding splits to be configured into xena. */
typedef enum xena_max_outstanding_splits {
	XENA_ONE_SPLIT_TRANSACTION = 0,
	XENA_TWO_SPLIT_TRANSACTION = 1,
	XENA_THREE_SPLIT_TRANSACTION = 2,
	XENA_FOUR_SPLIT_TRANSACTION = 3,
	XENA_EIGHT_SPLIT_TRANSACTION = 4,
	XENA_TWELVE_SPLIT_TRANSACTION = 5,
	XENA_SIXTEEN_SPLIT_TRANSACTION = 6,
	XENA_THIRTYTWO_SPLIT_TRANSACTION = 7
} xena_max_outstanding_splits;
#define XENA_MAX_OUTSTANDING_SPLITS(n) (n << 4)

/*  OS concerned variables and constants */
#define WATCH_DOG_TIMEOUT   	5*HZ
#define EFILL       			0x1234
#define ALIGN_SIZE  			127
#define	PCIX_COMMAND_REGISTER	0x62

/*
 * Debug related variables.
 */
/* different debug levels. */
#define	ERR_DBG		0
#define	INIT_DBG	1
#define	INFO_DBG	2
#define	TX_DBG		3
#define	INTR_DBG	4

/* Global variable that defines the present debug level of the driver. */
static int debug_level = ERR_DBG;	/* Default level. */

/* DEBUG message print. */
#define DBG_PRINT(dbg_level, args...)  if(!(debug_level<dbg_level)) printk(args)

/* Protocol assist features of the NIC */
#define L3_CKSUM_OK 0xFFFF
#define L4_CKSUM_OK 0xFFFF
#define S2IO_JUMBO_SIZE 9600

/* The statistics block of Xena */
typedef struct stat_block {
#ifdef  __BIG_ENDIAN
/* Tx MAC statistics counters. */
	u32 tmac_frms;
	u32 tmac_data_octets;
	u64 tmac_drop_frms;
	u32 tmac_mcst_frms;
	u32 tmac_bcst_frms;
	u64 tmac_pause_ctrl_frms;
	u32 tmac_ttl_octets;
	u32 tmac_ucst_frms;
	u32 tmac_nucst_frms;
	u32 tmac_any_err_frms;
	u64 tmac_ttl_less_fb_octets;
	u64 tmac_vld_ip_octets;
	u32 tmac_vld_ip;
	u32 tmac_drop_ip;
	u32 tmac_icmp;
	u32 tmac_rst_tcp;
	u64 tmac_tcp;
	u32 tmac_udp;
	u32 reserved_0;

/* Rx MAC Statistics counters. */
	u32 rmac_vld_frms;
	u32 rmac_data_octets;
	u64 rmac_fcs_err_frms;
	u64 rmac_drop_frms;
	u32 rmac_vld_mcst_frms;
	u32 rmac_vld_bcst_frms;
	u32 rmac_in_rng_len_err_frms;
	u32 rmac_out_rng_len_err_frms;
	u64 rmac_long_frms;
	u64 rmac_pause_ctrl_frms;
	u64 rmac_unsup_ctrl_frms;
	u32 rmac_ttl_octets;
	u32 rmac_accepted_ucst_frms;
	u32 rmac_accepted_nucst_frms;
	u32 rmac_discarded_frms;
	u32 rmac_drop_events;
	u32 reserved_1;
	u64 rmac_ttl_less_fb_octets;
	u64 rmac_ttl_frms;
	u64 reserved_2;
	u32 reserved_3;
	u32 rmac_usized_frms;
	u32 rmac_osized_frms;
	u32 rmac_frag_frms;
	u32 rmac_jabber_frms;
	u32 reserved_4;
	u64 rmac_ttl_64_frms;
	u64 rmac_ttl_65_127_frms;
	u64 reserved_5;
	u64 rmac_ttl_128_255_frms;
	u64 rmac_ttl_256_511_frms;
	u64 reserved_6;
	u64 rmac_ttl_512_1023_frms;
	u64 rmac_ttl_1024_1518_frms;
	u32 reserved_7;
	u32 rmac_ip;
	u64 rmac_ip_octets;
	u32 rmac_hdr_err_ip;
	u32 rmac_drop_ip;
	u32 rmac_icmp;
	u32 reserved_8;
	u64 rmac_tcp;
	u32 rmac_udp;
	u32 rmac_err_drp_udp;
	u64 rmac_xgmii_err_sym;
	u64 rmac_frms_q0;
	u64 rmac_frms_q1;
	u64 rmac_frms_q2;
	u64 rmac_frms_q3;
	u64 rmac_frms_q4;
	u64 rmac_frms_q5;
	u64 rmac_frms_q6;
	u64 rmac_frms_q7;
	u16 rmac_full_q0;
	u16 rmac_full_q1;
	u16 rmac_full_q2;
	u16 rmac_full_q3;
	u16 rmac_full_q4;
	u16 rmac_full_q5;
	u16 rmac_full_q6;
	u16 rmac_full_q7;
	u32 rmac_pause_cnt;
	u32 reserved_9;
	u64 rmac_xgmii_data_err_cnt;
	u64 rmac_xgmii_ctrl_err_cnt;
	u32 rmac_accepted_ip;
	u32 rmac_err_tcp;

/* PCI/PCI-X Read transaction statistics. */
	u32 rd_req_cnt;
	u32 new_rd_req_cnt;
	u32 new_rd_req_rtry_cnt;
	u32 rd_rtry_cnt;
	u32 wr_rtry_rd_ack_cnt;

/* PCI/PCI-X write transaction statistics. */
	u32 wr_req_cnt;
	u32 new_wr_req_cnt;
	u32 new_wr_req_rtry_cnt;
	u32 wr_rtry_cnt;
	u32 wr_disc_cnt;
	u32 rd_rtry_wr_ack_cnt;

/*	DMA Transaction statistics. */
	u32 txp_wr_cnt;
	u32 txd_rd_cnt;
	u32 txd_wr_cnt;
	u32 rxd_rd_cnt;
	u32 rxd_wr_cnt;
	u32 txf_rd_cnt;
	u32 rxf_wr_cnt;
#else
/* Tx MAC statistics counters. */
	u32 tmac_data_octets;
	u32 tmac_frms;
	u64 tmac_drop_frms;
	u32 tmac_bcst_frms;
	u32 tmac_mcst_frms;
	u64 tmac_pause_ctrl_frms;
	u32 tmac_ucst_frms;
	u32 tmac_ttl_octets;
	u32 tmac_any_err_frms;
	u32 tmac_nucst_frms;
	u64 tmac_ttl_less_fb_octets;
	u64 tmac_vld_ip_octets;
	u32 tmac_drop_ip;
	u32 tmac_vld_ip;
	u32 tmac_rst_tcp;
	u32 tmac_icmp;
	u64 tmac_tcp;
	u32 reserved_0;
	u32 tmac_udp;

/* Rx MAC Statistics counters. */
	u32 rmac_data_octets;
	u32 rmac_vld_frms;
	u64 rmac_fcs_err_frms;
	u64 rmac_drop_frms;
	u32 rmac_vld_bcst_frms;
	u32 rmac_vld_mcst_frms;
	u32 rmac_out_rng_len_err_frms;
	u32 rmac_in_rng_len_err_frms;
	u64 rmac_long_frms;
	u64 rmac_pause_ctrl_frms;
	u64 rmac_unsup_ctrl_frms;
	u32 rmac_accepted_ucst_frms;
	u32 rmac_ttl_octets;
	u32 rmac_discarded_frms;
	u32 rmac_accepted_nucst_frms;
	u32 reserved_1;
	u32 rmac_drop_events;
	u64 rmac_ttl_less_fb_octets;
	u64 rmac_ttl_frms;
	u64 reserved_2;
	u32 rmac_usized_frms;
	u32 reserved_3;
	u32 rmac_frag_frms;
	u32 rmac_osized_frms;
	u32 reserved_4;
	u32 rmac_jabber_frms;
	u64 rmac_ttl_64_frms;
	u64 rmac_ttl_65_127_frms;
	u64 reserved_5;
	u64 rmac_ttl_128_255_frms;
	u64 rmac_ttl_256_511_frms;
	u64 reserved_6;
	u64 rmac_ttl_512_1023_frms;
	u64 rmac_ttl_1024_1518_frms;
	u32 rmac_ip;
	u32 reserved_7;
	u64 rmac_ip_octets;
	u32 rmac_drop_ip;
	u32 rmac_hdr_err_ip;
	u32 reserved_8;
	u32 rmac_icmp;
	u64 rmac_tcp;
	u32 rmac_err_drp_udp;
	u32 rmac_udp;
	u64 rmac_xgmii_err_sym;
	u64 rmac_frms_q0;
	u64 rmac_frms_q1;
	u64 rmac_frms_q2;
	u64 rmac_frms_q3;
	u64 rmac_frms_q4;
	u64 rmac_frms_q5;
	u64 rmac_frms_q6;
	u64 rmac_frms_q7;
	u16 rmac_full_q3;
	u16 rmac_full_q2;
	u16 rmac_full_q1;
	u16 rmac_full_q0;
	u16 rmac_full_q7;
	u16 rmac_full_q6;
	u16 rmac_full_q5;
	u16 rmac_full_q4;
	u32 reserved_9;
	u32 rmac_pause_cnt;
	u64 rmac_xgmii_data_err_cnt;
	u64 rmac_xgmii_ctrl_err_cnt;
	u32 rmac_err_tcp;
	u32 rmac_accepted_ip;

/* PCI/PCI-X Read transaction statistics. */
	u32 new_rd_req_cnt;
	u32 rd_req_cnt;
	u32 rd_rtry_cnt;
	u32 new_rd_req_rtry_cnt;

/* PCI/PCI-X Write/Read transaction statistics. */
	u32 wr_req_cnt;
	u32 wr_rtry_rd_ack_cnt;
	u32 new_wr_req_rtry_cnt;
	u32 new_wr_req_cnt;
	u32 wr_disc_cnt;
	u32 wr_rtry_cnt;

/*	PCI/PCI-X Write / DMA Transaction statistics. */
	u32 txp_wr_cnt;
	u32 rd_rtry_wr_ack_cnt;
	u32 txd_wr_cnt;
	u32 txd_rd_cnt;
	u32 rxd_wr_cnt;
	u32 rxd_rd_cnt;
	u32 rxf_wr_cnt;
	u32 txf_rd_cnt;
#endif
} StatInfo_t;

/* Structures representing different init time configuration
 * parameters of the NIC.
 */

/* Maintains Per FIFO related information. */
typedef struct tx_fifo_config {
#define	MAX_AVAILABLE_TXDS	8192
	u32 fifo_len;		/* specifies len of FIFO upto 8192, ie no of TxDLs */
/* Priority definition */
#define TX_FIFO_PRI_0               0	/*Highest */
#define TX_FIFO_PRI_1               1
#define TX_FIFO_PRI_2               2
#define TX_FIFO_PRI_3               3
#define TX_FIFO_PRI_4               4
#define TX_FIFO_PRI_5               5
#define TX_FIFO_PRI_6               6
#define TX_FIFO_PRI_7               7	/*lowest */
	u8 fifo_priority;	/* specifies pointer level for FIFO */
	/* user should not set twos fifos with same pri */
	u8 f_no_snoop;
#define NO_SNOOP_TXD                0x01
#define NO_SNOOP_TXD_BUFFER          0x02
} tx_fifo_config_t;


/* Maintains per Ring related information */
typedef struct rx_ring_config {
	u32 num_rxd;		/*No of RxDs per Rx Ring */
#define RX_RING_PRI_0               0	/* highest */
#define RX_RING_PRI_1               1
#define RX_RING_PRI_2               2
#define RX_RING_PRI_3               3
#define RX_RING_PRI_4               4
#define RX_RING_PRI_5               5
#define RX_RING_PRI_6               6
#define RX_RING_PRI_7               7	/* lowest */

	u8 ring_priority;	/*Specifies service priority of ring */
	/* OSM should not set any two rings with same priority */
	u8 ring_org;		/*Organization of ring */
#define RING_ORG_BUFF1		0x01
#define RX_RING_ORG_BUFF3	0x03
#define RX_RING_ORG_BUFF5	0x05

	u8 f_no_snoop;
#define NO_SNOOP_RXD                0x01
#define NO_SNOOP_RXD_BUFFER         0x02
} rx_ring_config_t;

/* This structure provides contains values of the tunable parameters 
 * of the H/W 
 */
struct config_param {
/* Tx Side */
	u32 tx_fifo_num;	/*Number of Tx FIFOs */
#define MAX_TX_FIFOS 8

	tx_fifo_config_t tx_cfg[MAX_TX_FIFOS];	/*Per-Tx FIFO config */
	u32 max_txds;		/*Max no. of Tx buffer descriptor per TxDL */
	u64 tx_intr_type;
	/* Specifies if Tx Intr is UTILZ or PER_LIST type. */

/* Rx Side */
	u32 rx_ring_num;	/*Number of receive rings */
#define MAX_RX_RINGS 8
#define MAX_RX_BLOCKS_PER_RING  150

	rx_ring_config_t rx_cfg[MAX_RX_RINGS];	/*Per-Rx Ring config */

#define HEADER_ETHERNET_II_802_3_SIZE 14
#define HEADER_802_2_SIZE              3
#define HEADER_SNAP_SIZE               5
#define HEADER_VLAN_SIZE               4

#define MIN_MTU                       46
#define MAX_PYLD                    1500
#define MAX_MTU                     (MAX_PYLD+18)
#define MAX_MTU_VLAN                (MAX_PYLD+22)
#define MAX_PYLD_JUMBO              9600
#define MAX_MTU_JUMBO               (MAX_PYLD_JUMBO+18)
#define MAX_MTU_JUMBO_VLAN          (MAX_PYLD_JUMBO+22)
};

/* Structure representing MAC Addrs */
typedef struct mac_addr {
	u8 mac_addr[ETH_ALEN];
} macaddr_t;

/* Structure that represent every FIFO element in the BAR1
 * Address location. 
 */
typedef struct _TxFIFO_element {
	u64 TxDL_Pointer;

	u64 List_Control;
#define TX_FIFO_LAST_TXD_NUM( val)     vBIT(val,0,8)
#define TX_FIFO_FIRST_LIST             BIT(14)
#define TX_FIFO_LAST_LIST              BIT(15)
#define TX_FIFO_FIRSTNLAST_LIST        vBIT(3,14,2)
#define TX_FIFO_SPECIAL_FUNC           BIT(23)
#define TX_FIFO_DS_NO_SNOOP            BIT(31)
#define TX_FIFO_BUFF_NO_SNOOP          BIT(30)
} TxFIFO_element_t;

/* Tx descriptor structure */
typedef struct _TxD {
	u64 Control_1;
/* bit mask */
#define TXD_LIST_OWN_XENA       BIT(7)
#define TXD_T_CODE              (BIT(12)|BIT(13)|BIT(14)|BIT(15))
#define TXD_T_CODE_OK(val)      (|(val & TXD_T_CODE))
#define GET_TXD_T_CODE(val)     ((val & TXD_T_CODE)<<12)
#define TXD_GATHER_CODE         (BIT(22) | BIT(23))
#define TXD_GATHER_CODE_FIRST   BIT(22)
#define TXD_GATHER_CODE_LAST    BIT(23)
#define TXD_TCP_LSO_EN          BIT(30)
#define TXD_UDP_COF_EN          BIT(31)
#define TXD_TCP_LSO_MSS(val)    vBIT(val,34,14)
#define TXD_BUFFER0_SIZE(val)   vBIT(val,48,16)

	u64 Control_2;
#define TXD_TX_CKO_CONTROL      (BIT(5)|BIT(6)|BIT(7))
#define TXD_TX_CKO_IPV4_EN      BIT(5)
#define TXD_TX_CKO_TCP_EN       BIT(6)
#define TXD_TX_CKO_UDP_EN       BIT(7)
#define TXD_VLAN_ENABLE         BIT(15)
#define TXD_VLAN_TAG(val)       vBIT(val,16,16)
#define TXD_INT_NUMBER(val)     vBIT(val,34,6)
#define TXD_INT_TYPE_PER_LIST   BIT(47)
#define TXD_INT_TYPE_UTILZ      BIT(46)
#define TXD_SET_MARKER         vBIT(0x6,0,4)

	u64 Buffer_Pointer;
	u64 Host_Control;	/* reserved for host */
} TxD_t;

/* Structure to hold the phy and virt addr of every TxDL. */
typedef struct list_info_hold {
	dma_addr_t list_phy_addr;
	void *list_virt_addr;
} list_info_hold_t;

/* Rx descriptor structure */
typedef struct _RxD_t {
	u64 Host_Control;	/* reserved for host */
	u64 Control_1;
#define RXD_OWN_XENA            BIT(7)
#define RXD_T_CODE              (BIT(12)|BIT(13)|BIT(14)|BIT(15))
#define RXD_FRAME_PROTO         vBIT(0xFFFF,24,8)
#define RXD_FRAME_PROTO_IPV4    BIT(27)
#define RXD_FRAME_PROTO_IPV6    BIT(28)
#define RXD_FRAME_PROTO_TCP     BIT(30)
#define RXD_FRAME_PROTO_UDP     BIT(31)
#define TCP_OR_UDP_FRAME        (RXD_FRAME_PROTO_TCP | RXD_FRAME_PROTO_UDP)
#define RXD_GET_L3_CKSUM(val)   ((u16)(val>> 16) & 0xFFFF)
#define RXD_GET_L4_CKSUM(val)   ((u16)(val) & 0xFFFF)

	u64 Control_2;
#ifndef CONFIG_2BUFF_MODE
#define MASK_BUFFER0_SIZE       vBIT(0xFFFF,0,16)
#define SET_BUFFER0_SIZE(val)   vBIT(val,0,16)
#else
#define MASK_BUFFER0_SIZE       vBIT(0xFF,0,16)
#define MASK_BUFFER1_SIZE       vBIT(0xFFFF,16,16)
#define MASK_BUFFER2_SIZE       vBIT(0xFFFF,32,16)
#define SET_BUFFER0_SIZE(val)   vBIT(val,8,8)
#define SET_BUFFER1_SIZE(val)   vBIT(val,16,16)
#define SET_BUFFER2_SIZE(val)   vBIT(val,32,16)
#endif

#define MASK_VLAN_TAG           vBIT(0xFFFF,48,16)
#define SET_VLAN_TAG(val)       vBIT(val,48,16)
#define SET_NUM_TAG(val)       vBIT(val,16,32)

#ifndef CONFIG_2BUFF_MODE
#define RXD_GET_BUFFER0_SIZE(Control_2) (u64)((Control_2 & vBIT(0xFFFF,0,16)))
#else
#define RXD_GET_BUFFER0_SIZE(Control_2) (u8)((Control_2 & MASK_BUFFER0_SIZE) \
							>> 48)
#define RXD_GET_BUFFER1_SIZE(Control_2) (u16)((Control_2 & MASK_BUFFER1_SIZE) \
							>> 32)
#define RXD_GET_BUFFER2_SIZE(Control_2) (u16)((Control_2 & MASK_BUFFER2_SIZE) \
							>> 16)
#define BUF0_LEN	40
#define BUF1_LEN	1
#endif

	u64 Buffer0_ptr;
#ifdef CONFIG_2BUFF_MODE
	u64 Buffer1_ptr;
	u64 Buffer2_ptr;
#endif
} RxD_t;

/* Structure that represents the Rx descriptor block which contains 
 * 128 Rx descriptors.
 */
#ifndef CONFIG_2BUFF_MODE
typedef struct _RxD_block {
#define MAX_RXDS_PER_BLOCK             127
	RxD_t rxd[MAX_RXDS_PER_BLOCK];

	u64 reserved_0;
#define END_OF_BLOCK    0xFEFFFFFFFFFFFFFFULL
	u64 reserved_1;		/* 0xFEFFFFFFFFFFFFFF to mark last 
				 * Rxd in this blk */
	u64 reserved_2_pNext_RxD_block;	/* Logical ptr to next */
	u64 pNext_RxD_Blk_physical;	/* Buff0_ptr.In a 32 bit arch
					 * the upper 32 bits should 
					 * be 0 */
} RxD_block_t;
#else
typedef struct _RxD_block {
#define MAX_RXDS_PER_BLOCK             85
	RxD_t rxd[MAX_RXDS_PER_BLOCK];

#define END_OF_BLOCK    0xFEFFFFFFFFFFFFFFULL
	u64 reserved_1;		/* 0xFEFFFFFFFFFFFFFF to mark last Rxd 
				 * in this blk */
	u64 pNext_RxD_Blk_physical;	/* Phy ponter to next blk. */
} RxD_block_t;
#define SIZE_OF_BLOCK	4096

/* Structure to hold virtual addresses of Buf0 and Buf1 in 
 * 2buf mode. */
typedef struct bufAdd {
	void *ba_0_org;
	void *ba_1_org;
	void *ba_0;
	void *ba_1;
} buffAdd_t;
#endif

/* Structure which stores all the MAC control parameters */

/* This structure stores the offset of the RxD in the ring 
 * from which the Rx Interrupt processor can start picking 
 * up the RxDs for processing.
 */
typedef struct _rx_curr_get_info_t {
	u32 block_index;
	u32 offset;
	u32 ring_len;
} rx_curr_get_info_t;

typedef rx_curr_get_info_t rx_curr_put_info_t;

/* This structure stores the offset of the TxDl in the FIFO
 * from which the Tx Interrupt processor can start picking 
 * up the TxDLs for send complete interrupt processing.
 */
typedef struct {
	u32 offset;
	u32 fifo_len;
} tx_curr_get_info_t;

typedef tx_curr_get_info_t tx_curr_put_info_t;

/* Infomation related to the Tx and Rx FIFOs and Rings of Xena
 * is maintained in this structure.
 */
typedef struct mac_info {
/* rx side stuff */
	/* Put pointer info which indictes which RxD has to be replenished 
	 * with a new buffer.
	 */
	rx_curr_put_info_t rx_curr_put_info[MAX_RX_RINGS];

	/* Get pointer info which indictes which is the last RxD that was 
	 * processed by the driver.
	 */
	rx_curr_get_info_t rx_curr_get_info[MAX_RX_RINGS];

	u16 rmac_pause_time;
	u16 mc_pause_threshold_q0q3;
	u16 mc_pause_threshold_q4q7;

/* tx side stuff */
	/* logical pointer of start of each Tx FIFO */
	TxFIFO_element_t __iomem *tx_FIFO_start[MAX_TX_FIFOS];

/* Current offset within tx_FIFO_start, where driver would write new Tx frame*/
	tx_curr_put_info_t tx_curr_put_info[MAX_TX_FIFOS];
	tx_curr_get_info_t tx_curr_get_info[MAX_TX_FIFOS];

	void *stats_mem;	/* orignal pointer to allocated mem */
	dma_addr_t stats_mem_phy;	/* Physical address of the stat block */
	u32 stats_mem_sz;
	StatInfo_t *stats_info;	/* Logical address of the stat block */
} mac_info_t;

/* structure representing the user defined MAC addresses */
typedef struct {
	char addr[ETH_ALEN];
	int usage_cnt;
} usr_addr_t;

/* Structure that holds the Phy and virt addresses of the Blocks */
typedef struct rx_block_info {
	RxD_t *block_virt_addr;
	dma_addr_t block_dma_addr;
} rx_block_info_t;

/* Default Tunable parameters of the NIC. */
#define DEFAULT_FIFO_LEN 4096
#define SMALL_RXD_CNT	30 * (MAX_RXDS_PER_BLOCK+1)
#define LARGE_RXD_CNT	100 * (MAX_RXDS_PER_BLOCK+1)
#define SMALL_BLK_CNT	30
#define LARGE_BLK_CNT	100

/* Structure representing one instance of the NIC */
typedef struct s2io_nic {
#define MAX_MAC_SUPPORTED   16
#define MAX_SUPPORTED_MULTICASTS MAX_MAC_SUPPORTED

	macaddr_t def_mac_addr[MAX_MAC_SUPPORTED];
	macaddr_t pre_mac_addr[MAX_MAC_SUPPORTED];

	struct net_device_stats stats;
	void __iomem *bar0;
	void __iomem *bar1;
	struct config_param config;
	mac_info_t mac_control;
	int high_dma_flag;
	int device_close_flag;
	int device_enabled_once;

	char name[32];
	struct tasklet_struct task;
	volatile unsigned long tasklet_status;
	struct timer_list timer;
	struct net_device *dev;
	struct pci_dev *pdev;

	u16 vendor_id;
	u16 device_id;
	u16 ccmd;
	u32 cbar0_1;
	u32 cbar0_2;
	u32 cbar1_1;
	u32 cbar1_2;
	u32 cirq;
	u8 cache_line;
	u32 rom_expansion;
	u16 pcix_cmd;
	u32 irq;
	atomic_t rx_bufs_left[MAX_RX_RINGS];

	spinlock_t tx_lock;
#ifndef CONFIG_S2IO_NAPI
	spinlock_t put_lock;
#endif

#define PROMISC     1
#define ALL_MULTI   2

#define MAX_ADDRS_SUPPORTED 64
	u16 usr_addr_count;
	u16 mc_addr_count;
	usr_addr_t usr_addrs[MAX_ADDRS_SUPPORTED];

	u16 m_cast_flg;
	u16 all_multi_pos;
	u16 promisc_flg;

	u16 tx_pkt_count;
	u16 rx_pkt_count;
	u16 tx_err_count;
	u16 rx_err_count;

#ifndef CONFIG_S2IO_NAPI
	/* Index to the absolute position of the put pointer of Rx ring. */
	int put_pos[MAX_RX_RINGS];
#endif

	/*
	 *  Place holders for the virtual and physical addresses of 
	 *  all the Rx Blocks
	 */
	rx_block_info_t rx_blocks[MAX_RX_RINGS][MAX_RX_BLOCKS_PER_RING];
	int block_count[MAX_RX_RINGS];
	int pkt_cnt[MAX_RX_RINGS];

	/* Place holder of all the TX List's Phy and Virt addresses. */
	list_info_hold_t *list_info[MAX_TX_FIFOS];

	/*  Id timer, used to blink NIC to physically identify NIC. */
	struct timer_list id_timer;

	/*  Restart timer, used to restart NIC if the device is stuck and
	 *  a schedule task that will set the correct Link state once the 
	 *  NIC's PHY has stabilized after a state change.
	 */
#ifdef INIT_TQUEUE
	struct tq_struct rst_timer_task;
	struct tq_struct set_link_task;
#else
	struct work_struct rst_timer_task;
	struct work_struct set_link_task;
#endif

	/* Flag that can be used to turn on or turn off the Rx checksum 
	 * offload feature.
	 */
	int rx_csum;

	/*  after blink, the adapter must be restored with original 
	 *  values.
	 */
	u64 adapt_ctrl_org;

	/* Last known link state. */
	u16 last_link_state;
#define	LINK_DOWN	1
#define	LINK_UP		2

#ifdef CONFIG_2BUFF_MODE
	/* Buffer Address store. */
	buffAdd_t **ba[MAX_RX_RINGS];
#endif
	int task_flag;
#define CARD_DOWN 1
#define CARD_UP 2
	atomic_t card_state;
	volatile unsigned long link_state;
} nic_t;

#define RESET_ERROR 1;
#define CMD_ERROR   2;

/*  OS related system calls */
#ifndef readq
static inline u64 readq(void __iomem *addr)
{
	u64 ret = readl(addr + 4);
	ret <<= 32;
	ret |= readl(addr);

	return ret;
}
#endif

#ifndef writeq
static inline void writeq(u64 val, void __iomem *addr)
{
	writel((u32) (val), addr);
	writel((u32) (val >> 32), (addr + 4));
}

/* In 32 bit modes, some registers have to be written in a 
 * particular order to expect correct hardware operation. The
 * macro SPECIAL_REG_WRITE is used to perform such ordered 
 * writes. Defines UF (Upper First) and LF (Lower First) will 
 * be used to specify the required write order.
 */
#define UF	1
#define LF	2
static inline void SPECIAL_REG_WRITE(u64 val, void __iomem *addr, int order)
{
	if (order == LF) {
		writel((u32) (val), addr);
		writel((u32) (val >> 32), (addr + 4));
	} else {
		writel((u32) (val >> 32), (addr + 4));
		writel((u32) (val), addr);
	}
}
#else
#define SPECIAL_REG_WRITE(val, addr, dummy) writeq(val, addr)
#endif

/*  Interrupt related values of Xena */

#define ENABLE_INTRS    1
#define DISABLE_INTRS   2

/*  Highest level interrupt blocks */
#define TX_PIC_INTR     (0x0001<<0)
#define TX_DMA_INTR     (0x0001<<1)
#define TX_MAC_INTR     (0x0001<<2)
#define TX_XGXS_INTR    (0x0001<<3)
#define TX_TRAFFIC_INTR (0x0001<<4)
#define RX_PIC_INTR     (0x0001<<5)
#define RX_DMA_INTR     (0x0001<<6)
#define RX_MAC_INTR     (0x0001<<7)
#define RX_XGXS_INTR    (0x0001<<8)
#define RX_TRAFFIC_INTR (0x0001<<9)
#define MC_INTR         (0x0001<<10)
#define ENA_ALL_INTRS    (   TX_PIC_INTR     | \
                            TX_DMA_INTR     | \
                            TX_MAC_INTR     | \
                            TX_XGXS_INTR    | \
                            TX_TRAFFIC_INTR | \
                            RX_PIC_INTR     | \
                            RX_DMA_INTR     | \
                            RX_MAC_INTR     | \
                            RX_XGXS_INTR    | \
                            RX_TRAFFIC_INTR | \
                            MC_INTR )

/*  Interrupt masks for the general interrupt mask register */
#define DISABLE_ALL_INTRS   0xFFFFFFFFFFFFFFFFULL

#define TXPIC_INT_M         BIT(0)
#define TXDMA_INT_M         BIT(1)
#define TXMAC_INT_M         BIT(2)
#define TXXGXS_INT_M        BIT(3)
#define TXTRAFFIC_INT_M     BIT(8)
#define PIC_RX_INT_M        BIT(32)
#define RXDMA_INT_M         BIT(33)
#define RXMAC_INT_M         BIT(34)
#define MC_INT_M            BIT(35)
#define RXXGXS_INT_M        BIT(36)
#define RXTRAFFIC_INT_M     BIT(40)

/*  PIC level Interrupts TODO*/

/*  DMA level Inressupts */
#define TXDMA_PFC_INT_M     BIT(0)
#define TXDMA_PCC_INT_M     BIT(2)

/*  PFC block interrupts */
#define PFC_MISC_ERR_1      BIT(0)	/* Interrupt to indicate FIFO full */

/* PCC block interrupts. */
#define	PCC_FB_ECC_ERR	   vBIT(0xff, 16, 8)	/* Interrupt to indicate
						   PCC_FB_ECC Error. */

/*
 * Prototype declaration.
 */
static int __devinit s2io_init_nic(struct pci_dev *pdev,
				   const struct pci_device_id *pre);
static void __devexit s2io_rem_nic(struct pci_dev *pdev);
static int init_shared_mem(struct s2io_nic *sp);
static void free_shared_mem(struct s2io_nic *sp);
static int init_nic(struct s2io_nic *nic);
#ifndef CONFIG_S2IO_NAPI
static void rx_intr_handler(struct s2io_nic *sp);
#endif
static void tx_intr_handler(struct s2io_nic *sp);
static void alarm_intr_handler(struct s2io_nic *sp);

static int s2io_starter(void);
static void s2io_closer(void);
static void s2io_tx_watchdog(struct net_device *dev);
static void s2io_tasklet(unsigned long dev_addr);
static void s2io_set_multicast(struct net_device *dev);
#ifndef CONFIG_2BUFF_MODE
static int rx_osm_handler(nic_t * sp, u16 len, RxD_t * rxdp, int ring_no);
#else
static int rx_osm_handler(nic_t * sp, RxD_t * rxdp, int ring_no,
			  buffAdd_t * ba);
#endif
static void s2io_link(nic_t * sp, int link);
static void s2io_reset(nic_t * sp);
#ifdef CONFIG_S2IO_NAPI
static int s2io_poll(struct net_device *dev, int *budget);
#endif
static void s2io_init_pci(nic_t * sp);
static int s2io_set_mac_addr(struct net_device *dev, u8 * addr);
static irqreturn_t s2io_isr(int irq, void *dev_id, struct pt_regs *regs);
static int verify_xena_quiescence(u64 val64, int flag);
static struct ethtool_ops netdev_ethtool_ops;
static void s2io_set_link(unsigned long data);
static void s2io_card_down(nic_t * nic);
static int s2io_card_up(nic_t * nic);

#endif				/* _S2IO_H */
