#ifndef IEEE1394_RAW1394_H
#define IEEE1394_RAW1394_H

#define RAW1394_DEVICE_MAJOR      171
#define RAW1394_DEVICE_NAME       "raw1394"

#define RAW1394_KERNELAPI_VERSION 4

/* state: opened */
#define RAW1394_REQ_INITIALIZE    1

/* state: initialized */
#define RAW1394_REQ_LIST_CARDS    2
#define RAW1394_REQ_SET_CARD      3

/* state: connected */
#define RAW1394_REQ_ASYNC_READ      100
#define RAW1394_REQ_ASYNC_WRITE     101
#define RAW1394_REQ_LOCK            102
#define RAW1394_REQ_LOCK64          103
#define RAW1394_REQ_ISO_SEND        104
#define RAW1394_REQ_ASYNC_SEND      105

#define RAW1394_REQ_ISO_LISTEN      200
#define RAW1394_REQ_FCP_LISTEN      201
#define RAW1394_REQ_RESET_BUS       202
#define RAW1394_REQ_GET_ROM         203
#define RAW1394_REQ_UPDATE_ROM      204
#define RAW1394_REQ_ECHO            205

#define RAW1394_REQ_ARM_REGISTER    300
#define RAW1394_REQ_ARM_UNREGISTER  301

#define RAW1394_REQ_RESET_NOTIFY    400

#define RAW1394_REQ_PHYPACKET       500

/* kernel to user */
#define RAW1394_REQ_BUS_RESET        10000
#define RAW1394_REQ_ISO_RECEIVE      10001
#define RAW1394_REQ_FCP_REQUEST      10002
#define RAW1394_REQ_ARM              10003
#define RAW1394_REQ_RAWISO_ACTIVITY  10004

/* error codes */
#define RAW1394_ERROR_NONE        0
#define RAW1394_ERROR_COMPAT      (-1001)
#define RAW1394_ERROR_STATE_ORDER (-1002)
#define RAW1394_ERROR_GENERATION  (-1003)
#define RAW1394_ERROR_INVALID_ARG (-1004)
#define RAW1394_ERROR_MEMFAULT    (-1005)
#define RAW1394_ERROR_ALREADY     (-1006)

#define RAW1394_ERROR_EXCESSIVE   (-1020)
#define RAW1394_ERROR_UNTIDY_LEN  (-1021)

#define RAW1394_ERROR_SEND_ERROR  (-1100)
#define RAW1394_ERROR_ABORTED     (-1101)
#define RAW1394_ERROR_TIMEOUT     (-1102)

/* arm_codes */
#define ARM_READ   1
#define ARM_WRITE  2
#define ARM_LOCK   4

#define RAW1394_LONG_RESET  0
#define RAW1394_SHORT_RESET 1

/* busresetnotify ... */
#define RAW1394_NOTIFY_OFF 0
#define RAW1394_NOTIFY_ON  1

#include <asm/types.h>

struct raw1394_request {
        __u32 type;
        __s32 error;
        __u32 misc;

        __u32 generation;
        __u32 length;

        __u64 address;

        __u64 tag;

        __u64 sendb;
        __u64 recvb;
};

struct raw1394_khost_list {
        __u32 nodes;
        __u8 name[32];
};

typedef struct arm_request {
        nodeid_t        destination_nodeid;
        nodeid_t        source_nodeid;
        nodeaddr_t      destination_offset;
        u8              tlabel;
        u8              tcode;
        u_int8_t        extended_transaction_code;
        u_int32_t       generation;
        arm_length_t    buffer_length;
        byte_t          *buffer;
} *arm_request_t;

typedef struct arm_response {
        int             response_code;
        arm_length_t    buffer_length;
        byte_t          *buffer;
} *arm_response_t;

typedef struct arm_request_response {
        struct arm_request  *request;
        struct arm_response *response;
} *arm_request_response_t;

/* rawiso API */

/* ioctls */
#define RAW1394_ISO_XMIT_INIT        1  /* arg: raw1394_iso_status* */
#define RAW1394_ISO_RECV_INIT        2  /* arg: raw1394_iso_status* */
#define RAW1394_ISO_RECV_START       3  /* arg: int, starting cycle */
#define RAW1394_ISO_XMIT_START       8  /* arg: int[2], { starting cycle, prebuffer } */
#define RAW1394_ISO_STOP             4
#define RAW1394_ISO_GET_STATUS       5  /* arg: raw1394_iso_status* */
#define RAW1394_ISO_PRODUCE_CONSUME  6  /* arg: int, # of packets */
#define RAW1394_ISO_SHUTDOWN         7

/* per-packet metadata embedded in the ringbuffer */
/* must be identical to hpsb_iso_packet_info in iso.h! */
struct raw1394_iso_packet_info {
	unsigned short len;
	unsigned short cycle;
	unsigned char channel; /* recv only */
	unsigned char tag;
	unsigned char sy;
};

struct raw1394_iso_config {
	unsigned int buf_packets;
	unsigned int max_packet_size;
	int channel;
	int speed; /* xmit only */
	int irq_interval;
};

/* argument to RAW1394_ISO_XMIT/RECV_INIT and RAW1394_ISO_GET_STATUS */
struct raw1394_iso_status {
	/* current settings */
	struct raw1394_iso_config config;
	
	/* byte offset between successive packets in the buffer */
	int buf_stride;

	/* byte offset of data payload within each packet */
	int packet_data_offset;
	
	/* byte offset of struct iso_packet_info within each packet */
	int packet_info_offset;

	/* index of next packet to fill with data (ISO transmission)
	   or next packet containing data recieved (ISO reception) */
	unsigned int first_packet;

	/* number of packets waiting to be filled with data (ISO transmission)
	   or containing data received (ISO reception) */
	unsigned int n_packets;

	/* approximate number of packets dropped due to overflow or
	   underflow of the packet buffer (a value of zero guarantees
	   that no packets have been dropped) */
	unsigned int overflows;
};

#ifdef __KERNEL__

struct iso_block_store {
        atomic_t refcount;
        size_t data_size;
        quadlet_t data[0];
};

enum raw1394_iso_state { RAW1394_ISO_INACTIVE = 0,
			 RAW1394_ISO_RECV = 1,
			 RAW1394_ISO_XMIT = 2 };

struct file_info {
        struct list_head list;

        enum { opened, initialized, connected } state;
        unsigned int protocol_version;

        struct hpsb_host *host;

        struct list_head req_pending;
        struct list_head req_complete;
        struct semaphore complete_sem;
        spinlock_t reqlists_lock;
        wait_queue_head_t poll_wait_complete;

        struct list_head addr_list;           

        u8 *fcp_buffer;

	/* old ISO API */
        u64 listen_channels;
        quadlet_t *iso_buffer;
        size_t iso_buffer_length;

        u8 notification; /* (busreset-notification) RAW1394_NOTIFY_OFF/ON */

	/* new rawiso API */
	enum raw1394_iso_state iso_state;
	struct hpsb_iso *iso_handle;
};

struct arm_addr {
        struct list_head addr_list; /* file_info list */
        u64    start, end;
        u64    arm_tag;
        u8     access_rights;
        u8     notification_options;
        u8     client_transactions;
        u64    recvb;
        u16    rec_length;
        u8     *addr_space_buffer; /* accessed by read/write/lock */
};

struct pending_request {
        struct list_head list;
        struct file_info *file_info;
        struct hpsb_packet *packet;
        struct iso_block_store *ibs;
        quadlet_t *data;
        int free_data;
        struct raw1394_request req;
};

struct host_info {
        struct list_head list;
        struct hpsb_host *host;
        struct list_head file_info_list;
};

#endif /* __KERNEL__ */

#endif /* IEEE1394_RAW1394_H */
