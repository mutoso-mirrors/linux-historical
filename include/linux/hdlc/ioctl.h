#ifndef __HDLC_IOCTL_H__
#define __HDLC_IOCTL_H__

typedef struct { 
	unsigned int clock_rate; /* bits per second */
	unsigned int clock_type; /* internal, external, TX-internal etc. */
	unsigned short loopback;
} sync_serial_settings;          /* V.35, V.24, X.21 */

typedef struct { 
	unsigned int clock_rate; /* bits per second */
	unsigned int clock_type; /* internal, external, TX-internal etc. */
	unsigned short loopback;
	unsigned int slot_map;
} te1_settings;                  /* T1, E1 */

typedef struct {
	unsigned short encoding;
	unsigned short parity;
} raw_hdlc_proto;

typedef struct {
	unsigned int t391;
	unsigned int t392;
	unsigned int n391;
	unsigned int n392;
	unsigned int n393;
	unsigned short lmi;
	unsigned short dce; /* 1 for DCE (network side) operation */
} fr_proto;

typedef struct {
	unsigned int dlci;
} fr_proto_pvc;          /* for creating/deleting FR PVCs */

typedef struct {
    unsigned int interval;
    unsigned int timeout;
} cisco_proto;

/* PPP doesn't need any info now - supply length = 0 to ioctl */

union hdlc_settings {
	raw_hdlc_proto		raw_hdlc;
	cisco_proto		cisco;
	fr_proto		fr;
	fr_proto_pvc		fr_pvc;
};

union line_settings {
	sync_serial_settings	sync;
	te1_settings		te1;
};

#endif /* __HDLC_IOCTL_H__ */
