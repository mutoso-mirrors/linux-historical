/*
 * IEEE 1394 for Linux
 *
 * Core support: hpsb_packet management, packet handling and forwarding to
 *               csr or lowlevel code
 *
 * Copyright (C) 1999 Andreas E. Bombe
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <asm/bitops.h>
#include <asm/byteorder.h>
#include <asm/semaphore.h>

#include "ieee1394_types.h"
#include "ieee1394.h"
#include "hosts.h"
#include "ieee1394_core.h"
#include "highlevel.h"
#include "ieee1394_transactions.h"
#include "csr.h"


atomic_t hpsb_generation = ATOMIC_INIT(0);


static void dump_packet(const char *text, quadlet_t *data, int size)
{
        int i;

        size /= 4;
        size = (size > 4 ? 4 : size);

        printk(KERN_DEBUG "ieee1394: %s", text);
        for (i = 0; i < size; i++) {
                printk(" %8.8x", data[i]);
        }
        printk("\n");
}


struct hpsb_packet *alloc_hpsb_packet(size_t data_size)
{
        struct hpsb_packet *packet = NULL;
        void *header = NULL, *data = NULL;
        int kmflags = in_interrupt() ? GFP_ATOMIC : GFP_KERNEL;

        packet = kmalloc(sizeof(struct hpsb_packet), kmflags);
        header = kmalloc(5 * 4, kmflags);
        if (header == NULL || packet == NULL) {
                kfree(header);
                kfree(packet);
                return NULL;
        }

        memset(packet, 0, sizeof(struct hpsb_packet));
        packet->header = header;

        if (data_size) {
                data = kmalloc(data_size + 4, kmflags);
                if (data == NULL) {
                        kfree(header);
                        kfree(packet);
                        return NULL;
                }

                packet->data = data;
                packet->data_size = data_size - 4;
        }

        INIT_LIST_HEAD(&packet->list);
        sema_init(&packet->state_change, 0);
        packet->state = unused;
        packet->generation = get_hpsb_generation();

#ifdef __BIG_ENDIAN
        /* set default */
        packet->data_be = 1;
#endif

        return packet;
}

void free_hpsb_packet(struct hpsb_packet *packet)
{
        if (packet == NULL) {
                return;
        }

        kfree(packet->data);
        kfree(packet->header);
        kfree(packet);
}


void reset_host_bus(struct hpsb_host *host)
{
        if (!host->initialized) {
                return;
        }

        hpsb_bus_reset(host);
        host->template->devctl(host, RESET_BUS, 0);
}


void hpsb_bus_reset(struct hpsb_host *host)
{
        if (!host->in_bus_reset) {
                abort_requests(host);
                host->in_bus_reset = 1;
                host->irm_id = -1;
                host->busmgr_id = -1;
                host->node_count = 0;
                host->selfid_count = 0;
        } else {
                HPSB_NOTICE(__FUNCTION__ 
                            " called while bus reset already in progress");
        }
}


/*
 * Verify num_of_selfids SelfIDs and return number of nodes.  Return zero in
 * case verification failed.
 */
static int check_selfids(struct hpsb_host *host, unsigned int num_of_selfids)
{
        int nodeid = -1;
        int rest_of_selfids = num_of_selfids;
        quadlet_t *sidp = host->topology_map;
        quadlet_t sid = *sidp;
        int esid_seq = 23;
        int i;

        while (rest_of_selfids--) {
                sid = *(sidp++);

                if (!(sid & 0x00800000) /* !extended */) {
                        nodeid++;
                        esid_seq = 0;

                        if (((sid >> 24) & NODE_MASK) != nodeid) {
                                HPSB_INFO("SelfIDs failed monotony check with "
                                          "%d", (sid >> 24) & NODE_MASK);
                                return 0;
                        }

                        /* "if is contender and link active" */
                        if ((sid & (1<<11)) && (sid & (1<<22))) {
                                host->irm_id = LOCAL_BUS | ((sid >> 24) 
                                                            & NODE_MASK);
                        }
                } else {
                        if ((((sid >> 24) & NODE_MASK) != nodeid)
                            || (((sid >> 20) & 0x7) != esid_seq)) {
                                HPSB_INFO("SelfIDs failed monotony check with "
                                          "%d/%d", (sid >> 24) & NODE_MASK,
                                          (sid >> 20) & 0x7);
                                return 0;
                        }
                        esid_seq++;
                }
        }

        sidp--;
        while (sid & 0x00800000 /* extended */) {
                /* check that no ports go to a parent */
                for (i = 2; i < 18; i += 2) {
                        if ((sid & (0x3 << i)) == (0x2 << i)) {
                                HPSB_INFO("SelfIDs failed root check on "
                                          "extended SelfID");
                                return 0;
                        }
                }
                sid = *(sidp--);
        }

        for (i = 2; i < 8; i += 2) {
                if ((sid & (0x3 << i)) == (0x2 << i)) {
                        HPSB_INFO("SelfIDs failed root check");
                        return 0;
                }
        }

        return nodeid + 1;
}

void hpsb_selfid_received(struct hpsb_host *host, quadlet_t sid)
{
        if (host->in_bus_reset) {
                printk("including selfid 0x%x\n", sid);
                host->topology_map[host->selfid_count++] = sid;
        } else {
                /* FIXME - info on which host */
                HPSB_NOTICE("spurious selfid packet (0x%8.8x) received", sid);
        }
}

void hpsb_selfid_complete(struct hpsb_host *host, int phyid, int isroot)
{
        

        host->node_id = 0xffc0 | phyid;
        host->in_bus_reset = 0;
        host->is_root = isroot;

        host->node_count = check_selfids(host, host->selfid_count);
        if (!host->node_count) {
                if (host->reset_retries++ < 20) {
                        /* selfid stage did not complete without error */
                        HPSB_NOTICE("error in SelfID stage - resetting");
                        reset_host_bus(host);
                        return;
                } else {
                        HPSB_NOTICE("stopping out-of-control reset loop");
                        HPSB_NOTICE("warning - topology map will therefore not "
                                    "be valid");
                }
        }

        /* irm_id is kept up to date by check_selfids() */
        host->is_irm = (host->irm_id == host->node_id);

        host->reset_retries = 0;
        inc_hpsb_generation();
        highlevel_host_reset(host);
}


void hpsb_packet_sent(struct hpsb_host *host, struct hpsb_packet *packet, 
                      int ackcode)
{
        unsigned long flags;

        packet->ack_code = ackcode;

        if (packet->no_waiter) {
                /* must not have a tlabel allocated */
                free_hpsb_packet(packet);
                return;
        }

        if (ackcode != ACK_PENDING || !packet->expect_response) {
                packet->state = complete;
                up(&packet->state_change);
                up(&packet->state_change);
                run_task_queue(&packet->complete_tq);
                return;
        }

        packet->state = pending;
        packet->sendtime = jiffies;

        spin_lock_irqsave(&host->pending_pkt_lock, flags);
        list_add_tail(&packet->list, &host->pending_packets);
        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        up(&packet->state_change);
        queue_task(&host->timeout_tq, &tq_timer);
}

int hpsb_send_packet(struct hpsb_packet *packet)
{
        struct hpsb_host *host = packet->host;

        if (!host->initialized || host->in_bus_reset 
            || (packet->generation != get_hpsb_generation())) {
                return 0;
        }

        packet->state = queued;

        dump_packet("send packet:", packet->header, packet->header_size);

        return host->template->transmit_packet(host, packet);
}

static void send_packet_nocare(struct hpsb_packet *packet)
{
        if (!hpsb_send_packet(packet)) {
                free_hpsb_packet(packet);
        }
}


void handle_packet_response(struct hpsb_host *host, int tcode, quadlet_t *data,
                            size_t size)
{
        struct hpsb_packet *packet = NULL;
        struct list_head *lh;
        int tcode_match = 0;
        int tlabel;
        unsigned long flags;

        tlabel = (data[0] >> 10) & 0x3f;

        spin_lock_irqsave(&host->pending_pkt_lock, flags);

        lh = host->pending_packets.next;
        while (lh != &host->pending_packets) {
                packet = list_entry(lh, struct hpsb_packet, list);
                if ((packet->tlabel == tlabel)
                    && (packet->node_id == (data[1] >> 16))){
                        break;
                }
                lh = lh->next;
        }

        if (lh == &host->pending_packets) {
                HPSB_INFO("unsolicited response packet received - np");
                dump_packet("contents:", data, 16);
                spin_unlock_irqrestore(&host->pending_pkt_lock, flags);
                return;
        }

        switch (packet->tcode) {
        case TCODE_WRITEQ:
        case TCODE_WRITEB:
                if (tcode == TCODE_WRITE_RESPONSE) tcode_match = 1;
                break;
        case TCODE_READQ:
                if (tcode == TCODE_READQ_RESPONSE) tcode_match = 1;
                break;
        case TCODE_READB:
                if (tcode == TCODE_READB_RESPONSE) tcode_match = 1;
                break;
        case TCODE_LOCK_REQUEST:
                if (tcode == TCODE_LOCK_RESPONSE) tcode_match = 1;
                break;
        }

        if (!tcode_match || (packet->tlabel != tlabel)
            || (packet->node_id != (data[1] >> 16))) {
                HPSB_INFO("unsolicited response packet received");
                dump_packet("contents:", data, 16);

                spin_unlock_irqrestore(&host->pending_pkt_lock, flags);
                return;
        }

        list_del(&packet->list);

        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        /* FIXME - update size fields? */
        switch (tcode) {
        case TCODE_WRITE_RESPONSE:
                memcpy(packet->header, data, 12);
                break;
        case TCODE_READQ_RESPONSE:
                memcpy(packet->header, data, 16);
                break;
        case TCODE_READB_RESPONSE:
                memcpy(packet->header, data, 16);
                memcpy(packet->data, data + 4, size - 16);
                break;
        case TCODE_LOCK_RESPONSE:
                memcpy(packet->header, data, 16);
                memcpy(packet->data, data + 4, (size - 16) > 8 ? 8 : size - 16);
                break;
        }

        packet->state = complete;
        up(&packet->state_change);
        run_task_queue(&packet->complete_tq);
}


struct hpsb_packet *create_reply_packet(struct hpsb_host *host, quadlet_t *data,
                                        size_t dsize)
{
        struct hpsb_packet *p;

        p = alloc_hpsb_packet(dsize);
        if (p == NULL) {
                /* FIXME - send data_error response */
                return NULL;
        }
                
        p->type = async;
        p->state = unused;
        p->host = host;
        p->node_id = data[1] >> 16;
        p->tlabel = (data[0] >> 10) & 0x3f;
        p->no_waiter = 1;

        return p;
}

#define PREP_REPLY_PACKET(length) \
                packet = create_reply_packet(host, data, length); \
                if (packet == NULL) break

inline void swap_quadlets_on_le(quadlet_t *q)
{
#ifdef __LITTLE_ENDIAN
        quadlet_t saved = q[0];
        q[0] = q[1];
        q[1] = saved;
#endif
}


void handle_incoming_packet(struct hpsb_host *host, int tcode, quadlet_t *data,
                            size_t size)
{
        struct hpsb_packet *packet;
        int length, rcode, extcode;
        u64 addr;

        /* big FIXME - no error checking is done for an out of bounds length */

        switch (tcode) {
        case TCODE_WRITEQ:
                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
                rcode = highlevel_write(host, data+3, addr, 4);

                if (((data[0] >> 16) & NODE_MASK) != NODE_MASK) {
                        /* not a broadcast write, reply */
                        PREP_REPLY_PACKET(0);
                        fill_async_write_resp(packet, rcode);
                        send_packet_nocare(packet);
                }
                break;

        case TCODE_WRITEB:
                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
                rcode = highlevel_write(host, data+4, addr, data[3]>>16);

                if (((data[0] >> 16) & NODE_MASK) != NODE_MASK) {
                        /* not a broadcast write, reply */
                        PREP_REPLY_PACKET(0);
                        fill_async_write_resp(packet, rcode);
                        send_packet_nocare(packet);
                }
                break;

        case TCODE_READQ:
                PREP_REPLY_PACKET(0);

                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
                rcode = highlevel_read(host, data, addr, 4);
                fill_async_readquad_resp(packet, rcode, *data);
                send_packet_nocare(packet);
                break;

        case TCODE_READB:
                length = data[3] >> 16;
                PREP_REPLY_PACKET(length);

                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];
                rcode = highlevel_read(host, packet->data, addr, length);
                fill_async_readblock_resp(packet, rcode, length);
                send_packet_nocare(packet);
                break;

        case TCODE_LOCK_REQUEST:
                length = data[3] >> 16;
                extcode = data[3] & 0xffff;
                addr = (((u64)(data[1] & 0xffff)) << 32) | data[2];

                PREP_REPLY_PACKET(8);

                if ((extcode == 0) || (extcode >= 7)) {
                        /* let switch default handle error */
                        length = 0;
                }

                switch (length) {
                case 4:
                        rcode = highlevel_lock(host, packet->data, addr, 
                                               data[4], 0, extcode);
                        fill_async_lock_resp(packet, rcode, extcode, 4);
                        break;
                case 8:
                        if ((extcode != EXTCODE_FETCH_ADD) 
                            && (extcode != EXTCODE_LITTLE_ADD)) {
                                rcode = highlevel_lock(host, packet->data, addr,
                                                       data[5], data[4], 
                                                       extcode);
                                fill_async_lock_resp(packet, rcode, extcode, 4);
                        } else {
                                swap_quadlets_on_le(data + 4);
                                rcode = highlevel_lock64(host,
                                             (octlet_t *)packet->data, addr,
                                             *(octlet_t *)(data + 4), 0ULL,
                                             extcode);
                                swap_quadlets_on_le(packet->data);
                                fill_async_lock_resp(packet, rcode, extcode, 8);
                        }
                        break;
                case 16:
                        swap_quadlets_on_le(data + 4);
                        swap_quadlets_on_le(data + 6);
                        rcode = highlevel_lock64(host, (octlet_t *)packet->data,
                                                 addr, *(octlet_t *)(data + 6),
                                                 *(octlet_t *)(data + 4), 
                                                 extcode);
                        swap_quadlets_on_le(packet->data);
                        fill_async_lock_resp(packet, rcode, extcode, 8);
                        break;
                default:
                        fill_async_lock_resp(packet, RCODE_TYPE_ERROR,
                                             extcode, 0);
                }

                send_packet_nocare(packet);
                break;
        }

}
#undef PREP_REPLY_PACKET


void hpsb_packet_received(struct hpsb_host *host, quadlet_t *data, size_t size)
{
        int tcode;

        if (host->in_bus_reset) {
                HPSB_INFO("received packet during reset; ignoring");
                return;
        }

        dump_packet("received packet:", data, size);

        tcode = (data[0] >> 4) & 0xf;

        switch (tcode) {
        case TCODE_WRITE_RESPONSE:
        case TCODE_READQ_RESPONSE:
        case TCODE_READB_RESPONSE:
        case TCODE_LOCK_RESPONSE:
                handle_packet_response(host, tcode, data, size);
                break;

        case TCODE_WRITEQ:
        case TCODE_WRITEB:
        case TCODE_READQ:
        case TCODE_READB:
        case TCODE_LOCK_REQUEST:
                handle_incoming_packet(host, tcode, data, size);
                break;


        case TCODE_ISO_DATA:
                highlevel_iso_receive(host, data, size);
                break;

        case TCODE_CYCLE_START:
                /* simply ignore this packet if it is passed on */
                break;

        default:
                HPSB_NOTICE("received packet with bogus transaction code %d", 
                            tcode);
                break;
        }
}


void abort_requests(struct hpsb_host *host)
{
        unsigned long flags;
        struct hpsb_packet *packet;
        struct list_head *lh;
        LIST_HEAD(llist);

        host->template->devctl(host, CANCEL_REQUESTS, 0);

        spin_lock_irqsave(&host->pending_pkt_lock, flags);
        list_splice(&host->pending_packets, &llist);
        INIT_LIST_HEAD(&host->pending_packets);
        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        lh = llist.next;

        while (lh != &llist) {
                packet = list_entry(lh, struct hpsb_packet, list);
                lh = lh->next;
                packet->state = complete;
                packet->ack_code = ACKX_ABORTED;
                up(&packet->state_change);
                run_task_queue(&packet->complete_tq);
        }
}

void abort_timedouts(struct hpsb_host *host)
{
        unsigned long flags;
        struct hpsb_packet *packet;
        unsigned long expire;
        struct list_head *lh;
        LIST_HEAD(expiredlist);

        spin_lock_irqsave(&host->csr.lock, flags);
        expire = (host->csr.split_timeout_hi * 8000 
                  + (host->csr.split_timeout_lo >> 19))
                * HZ / 8000;
        /* Avoid shortening of timeout due to rounding errors: */
        expire++;
        spin_unlock_irqrestore(&host->csr.lock, flags);


        spin_lock_irqsave(&host->pending_pkt_lock, flags);
        lh = host->pending_packets.next;

        while (lh != &host->pending_packets) {
                packet = list_entry(lh, struct hpsb_packet, list);
                lh = lh->next;
                if (time_before(packet->sendtime + expire, jiffies)) {
                        list_del(&packet->list);
                        list_add(&packet->list, &expiredlist);
                }
        }

        if (!list_empty(&host->pending_packets)) {
                queue_task(&host->timeout_tq, &tq_timer);
        }
        spin_unlock_irqrestore(&host->pending_pkt_lock, flags);

        lh = expiredlist.next;
        while (lh != &expiredlist) {
                packet = list_entry(lh, struct hpsb_packet, list);
                lh = lh->next;
                packet->state = complete;
                packet->ack_code = ACKX_TIMEOUT;
                up(&packet->state_change);
                run_task_queue(&packet->complete_tq);
        }
}


#if 0
int hpsb_host_thread(void *hostPointer)
{
        struct hpsb_host *host = (struct hpsb_host *)hostPointer;

        /* I don't understand why, but I just want to be on the safe side. */
        lock_kernel();

        HPSB_INFO(__FUNCTION__ " starting for one %s adapter",
                  host->template->name);

        exit_mm(current);
        exit_files(current);
        exit_fs(current);

        strcpy(current->comm, "ieee1394 thread");

        /* ... but then again, I think the following is safe. */
        unlock_kernel();

        for (;;) {
                siginfo_t info;
                unsigned long signr;

                if (signal_pending(current)) {
                        spin_lock_irq(&current->sigmask_lock);
                        signr = dequeue_signal(&current->blocked, &info);
                        spin_unlock_irq(&current->sigmask_lock);

                        break;
                }

                abort_timedouts(host);
        }

        HPSB_INFO(__FUNCTION__ " exiting");
        return 0;
}
#endif


#ifndef MODULE

void __init ieee1394_init(void)
{
        register_builtin_lowlevels();
        init_hpsb_highlevel();
        init_csr();
}

#else

int init_module(void)
{
        init_hpsb_highlevel();
        init_csr();
        return 0;
}

#endif
