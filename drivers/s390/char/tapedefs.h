/***********************************************************************
 *  drivers/s390/char/tapedefs.h
 *    tape device driver for S/390 tapes.
 *
 *  S390 version
 *    Copyright (C) 2000 IBM Corporation
 *    Author(s): Tuan Ngo-Anh <ngoanh@de.ibm.com>
 *               Carsten Otte <cotte@de.ibm.com>
 *
 *  UNDER CONSTRUCTION: Work in progress... :-)
 ***********************************************************************
 */

#define TAPE_DEBUG
#define CONFIG_S390_TAPE_DYNAMIC //use dyn. dev. attach/detach
#define TAPEBLOCK_RETRIES 20


/* Kernel Version Compatibility section */
#include <linux/version.h>
#include <linux/blkdev.h>
#include <linux/blk.h>
#if (LINUX_VERSION_CODE > KERNEL_VERSION(2,3,98))
#define INIT_BLK_DEV(d_major,d_request_fn,d_queue_fn,d_current) \
do { \
        blk_dev[d_major].queue = d_queue_fn; \
} while(0)
static inline struct request * 
tape_next_request( request_queue_t *queue ) 
{
        return blkdev_entry_next_request(&queue->queue_head);
}
static inline void 
tape_dequeue_request( request_queue_t * q, struct request *req )
{
        blkdev_dequeue_request (req);
}
#else 
typedef struct request *request_queue_t;
#define init_waitqueue_head(x) do { *x = NULL; } while(0)
#define blk_init_queue(x,y) do {} while(0)
#define blk_queue_headactive(x,y) do {} while(0)
#define INIT_BLK_DEV(d_major,d_request_fn,d_queue_fn,d_current) \
do { \
        blk_dev[d_major].request_fn = d_request_fn; \
        blk_dev[d_major].queue = d_queue_fn; \
        blk_dev[d_major].current_request = d_current; \
} while(0)
static inline struct request *
tape_next_request( request_queue_t *queue ) 
{
    return *queue;
}
static inline void 
tape_dequeue_request( request_queue_t * q, struct request *req )
{
        *q = req->next;
        req->next = NULL;
}
#endif 