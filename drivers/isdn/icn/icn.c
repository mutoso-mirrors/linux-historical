/* $Id: icn.c,v 1.29 1996/08/29 20:34:54 fritz Exp $
 *
 * ISDN low-level module for the ICN active ISDN-Card.
 *
 * Copyright 1994,95,96 by Fritz Elfert (fritz@wuemaus.franken.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: icn.c,v $
 * Revision 1.29  1996/08/29 20:34:54  fritz
 * Bugfix in send queue management:
 * sndcount was not updated correctly.
 * Minor Bugfixes.
 *
 * Revision 1.28  1996/06/28 17:02:53  fritz
 * replaced memcpy_fromfs_toio.
 *
 * Revision 1.27  1996/06/25 18:38:59  fritz
 * Fixed function name in error message.
 *
 * Revision 1.26  1996/06/24 17:20:35  fritz
 * Bugfixes in pollbchan_send():
 *   - Using lock field of skbuff breaks networking.
 *   - Added channel locking
 *   - changed dequeuing scheme.
 * Eliminated misc. compiler warnings.
 *
 * Revision 1.25  1996/06/11 22:53:35  tsbogend
 * fixed problem with large array on stack
 * made the driver working on Linux/alpha
 *
 * Revision 1.24  1996/06/06 13:58:33  fritz
 * Changed code to be architecture independent
 *
 * Revision 1.23  1996/06/03 19:59:00  fritz
 * Fixed typos.
 *
 * Revision 1.22  1996/05/17 15:46:41  fritz
 * Removed own queue management.
 * Changed queue management to use sk_buffs.
 *
 * Revision 1.21  1996/05/02 04:01:20  fritz
 * Bugfix:
 *  - icn_addcard() evaluated wrong driverId.
 *
 * Revision 1.20  1996/05/02 00:40:27  fritz
 * Major rewrite to support more than one card
 * with a single module.
 * Support for new firmware.
 *
 * Revision 1.19  1996/04/21 17:43:32  fritz
 * Changes for Support of new Firmware BRV3.02
 *
 * Revision 1.18  1996/04/20 16:50:26  fritz
 * Fixed status-buffer overrun.
 * Misc. typos
 *
 * Revision 1.17  1996/02/11 02:39:04  fritz
 * Increased Buffer for status-messages.
 * Removed conditionals for HDLC-firmware.
 *
 * Revision 1.16  1996/01/22 05:01:55  fritz
 * Revert to GPL.
 *
 * Revision 1.15  1996/01/10 20:57:39  fritz
 * Bugfix: Loading firmware twice caused the device stop working.
 *
 * Revision 1.14  1995/12/18  18:23:37  fritz
 * Support for ICN-2B Cards.
 * Change for supporting user-settable service-octet.
 *
 * Revision 1.13  1995/10/29  21:41:07  fritz
 * Added support for DriverId's, added Jan's patches for Kernel versions.
 *
 * Revision 1.12  1995/04/29  13:07:35  fritz
 * Added support for new Euro-ISDN-firmware
 *
 * Revision 1.11  1995/04/23  13:40:45  fritz
 * Added support for SPV's.
 * Changed Dial-Command to support MSN's on DSS1-Lines.
 *
 * Revision 1.10  1995/03/25  23:23:24  fritz
 * Changed configurable Ports, to allow settings for DIP-Switch Cardversions.
 *
 * Revision 1.9  1995/03/25  23:17:30  fritz
 * Fixed race-condition in pollbchan_send
 *
 * Revision 1.8  1995/03/15  12:49:44  fritz
 * Added support for SPV's
 * Split pollbchan_work for calling send-routine directly
 *
 * Revision 1.7  1995/02/20  03:48:03  fritz
 * Added support of new request_region-function.
 * Minor bugfixes.
 *
 * Revision 1.6  1995/01/31  15:48:45  fritz
 * Added Cause-Messages to be signaled to upper layers.
 * Added Revision-Info on load.
 *
 * Revision 1.5  1995/01/29  23:34:59  fritz
 * Added stopdriver() and appropriate calls.
 * Changed printk-statements to support loglevels.
 *
 * Revision 1.4  1995/01/09  07:40:46  fritz
 * Added GPL-Notice
 *
 * Revision 1.3  1995/01/04  05:15:18  fritz
 * Added undocumented "bootload-finished"-command in download-code
 * to satisfy some brain-damaged icn card-versions.
 *
 * Revision 1.2  1995/01/02  02:14:45  fritz
 * Misc Bugfixes
 *
 * Revision 1.1  1994/12/14  17:56:06  fritz
 * Initial revision
 *
 */

#include "icn.h"

/*
 * Verbose bootcode- and protocol-downloading.
 */
#undef BOOT_DEBUG

/*
 * Verbose Shmem-Mapping.
 */
#undef MAP_DEBUG

static char
*revision = "$Revision: 1.29 $";

static int icn_addcard(int, char *, char *);

/*
 * Free queue completely.
 * Parameter:
 *   queue = pointer to queue-head
 */
static void icn_free_queue(struct sk_buff_head *queue)
{
        struct sk_buff *skb;
        unsigned long flags;
        
        save_flags(flags);
        cli();
        while ((skb = skb_dequeue(queue)))
                dev_kfree_skb(skb, FREE_WRITE);
        restore_flags(flags);
}

/* Put a value into a shift-register, highest bit first.
 * Parameters:
 *            port     = port for output (bit 0 is significant)
 *            val      = value to be output
 *            firstbit = Bit-Number of highest bit
 *            bitcount = Number of bits to output
 */
static inline void icn_shiftout(unsigned short port,
                     unsigned long val,
                     int firstbit,
                     int bitcount)
{

        register u_char s;
        register u_char c;

        for (s = firstbit, c = bitcount; c > 0; s--, c--)
                OUTB_P((u_char) ((val >> s) & 1) ? 0xff : 0, port);
}

/*
 * disable a cards shared memory
 */
static inline void icn_disable_ram(icn_card *card)
{
        OUTB_P(0, ICN_MAPRAM);
}

/*
 * enable a cards shared memory
 */
static inline void icn_enable_ram(icn_card *card)
{
        OUTB_P(0xff, ICN_MAPRAM);
}

/*
 * Map a cards channel0 (Bank0/Bank8) or channel1 (Bank4/Bank12)
 */
static inline void icn_map_channel(icn_card *card, int channel)
{
#ifdef MAP_DEBUG
        printk(KERN_DEBUG "icn_map_channel %d %d\n", dev->channel, channel);
#endif
        if ((channel == dev.channel) && (card == dev.mcard))
                return;
        if (dev.mcard)
                icn_disable_ram(dev.mcard);
        icn_shiftout(ICN_BANK, chan2bank[channel], 3, 4);        /* Select Bank          */
        icn_enable_ram(card);
        dev.mcard = card;
        dev.channel = channel;
#ifdef MAP_DEBUG
        printk(KERN_DEBUG "icn_map_channel done\n");
#endif
}

/*
 * Lock a cards channel.
 * Return 0 if requested card/channel is unmapped (failure).
 * Return 1 on success.
 */
static inline int icn_lock_channel(icn_card *card, int channel)
{
        register int retval;
        ulong flags;

#ifdef MAP_DEBUG
        printk(KERN_DEBUG "icn_lock_channel %d\n", channel);
#endif
        save_flags(flags);
        cli();
        if ((dev.channel == channel) && (card == dev.mcard)) {
                dev.chanlock++;
                retval = 1;
#ifdef MAP_DEBUG
                printk(KERN_DEBUG "icn_lock_channel %d OK\n", channel);
#endif
        } else {
                retval = 0;
#ifdef MAP_DEBUG
                printk(KERN_DEBUG "icn_lock_channel %d FAILED, dc=%d\n", channel, device->channel);
#endif
        }
        restore_flags(flags);
        return retval;
}

/*
 * Release current card/channel lock
 */
static inline void icn_release_channel(void)
{
        ulong flags;

#ifdef MAP_DEBUG
        printk(KERN_DEBUG "icn_release_channel l=%d\n", device->chanlock);
#endif
        save_flags(flags);
        cli();
        if (dev.chanlock)
                dev.chanlock--;
        restore_flags(flags);
}

/*
 * Try to map and lock a cards channel.
 * Return 1 on success, 0 on failure.
 */
static inline int icn_trymaplock_channel(icn_card *card, int channel)
{
        ulong flags;

        save_flags(flags);
        cli();
#ifdef MAP_DEBUG
        printk(KERN_DEBUG "trymaplock c=%d dc=%d l=%d\n", channel, dev.channel,
               dev.chanlock);
#endif
        if ((!dev.chanlock) ||
            ((dev.channel == channel) && (dev.mcard == card))) {
                dev.chanlock++;
                icn_map_channel(card,channel);
                restore_flags(flags);
#ifdef MAP_DEBUG
                printk(KERN_DEBUG "trymaplock %d OK\n", channel);
#endif
                return 1;
        }
        restore_flags(flags);
#ifdef MAP_DEBUG
        printk(KERN_DEBUG "trymaplock %d FAILED\n", channel);
#endif
        return 0;
}

/*
 * Release current card/channel lock,
 * then map same or other channel without locking.
 */
static inline void icn_maprelease_channel(icn_card *card, int channel)
{
        ulong flags;

        save_flags(flags);
        cli();
#ifdef MAP_DEBUG
        printk(KERN_DEBUG "map_release c=%d l=%d\n", channel, dev.chanlock);
#endif
        if (dev.chanlock)
                dev.chanlock--;
        if (!dev.chanlock)
                icn_map_channel(card,channel);
        restore_flags(flags);
}

/* Get Data from the B-Channel, assemble fragmented packets and put them
 * into receive-queue. Wake up any B-Channel-reading processes.
 * This routine is called via timer-callback from icn_pollbchan().
 */

static void icn_pollbchan_receive(int channel, icn_card *card)
{
        int mch = channel + ((card->secondhalf) ? 2 : 0);
        int eflag;
        int cnt;
	struct sk_buff *skb;

        if (icn_trymaplock_channel(card,mch)) {
                while (rbavl) {
                        cnt = readb(&rbuf_l);
                        if ((card->rcvidx[channel] + cnt) > 4000) {
                                printk(KERN_WARNING 
                                       "icn: (%s) bogus packet on ch%d, dropping.\n",
                                       CID,
                                       channel + 1);
                                card->rcvidx[channel] = 0;
                                eflag = 0;
                        } else {
                                memcpy_fromio(&card->rcvbuf[channel][card->rcvidx[channel]],
                                              &rbuf_d, cnt);
                                card->rcvidx[channel] += cnt;
                                eflag = readb(&rbuf_f);
                        }
                        rbnext;
                        icn_maprelease_channel(card, mch & 2);
                        if (!eflag) {
                                if ((cnt = card->rcvidx[channel])) {
                                        if (!(skb = dev_alloc_skb(cnt))) {
                                                printk(KERN_WARNING "�cn: receive out of memory\n");
                                                break;
                                        }
                                        memcpy(skb_put(skb, cnt), card->rcvbuf[channel], cnt); 
                                        card->rcvidx[channel] = 0;
                                        card->interface.rcvcallb_skb(card->myid, channel, skb);
                                }
                        }
                        if (!icn_trymaplock_channel(card, mch))
                                break;
                }
                icn_maprelease_channel(card, mch & 2);
        }
}

/* Send data-packet to B-Channel, split it up into fragments of
 * ICN_FRAGSIZE length. If last fragment is sent out, signal
 * success to upper layers via statcallb with ISDN_STAT_BSENT argument.
 * This routine is called via timer-callback from icn_pollbchan() or
 * directly from icn_sendbuf().
 */

static void icn_pollbchan_send(int channel, icn_card *card)
{
        int mch = channel + ((card->secondhalf) ? 2 : 0);
        int cnt;
	unsigned long flags;
        struct sk_buff *skb;
        isdn_ctrl cmd;

        if (!card->sndcount[channel])
                return;
        if (icn_trymaplock_channel(card,mch)) {
                while (sbfree && card->sndcount[channel]) {
                        save_flags(flags);
                        cli();
                        if (card->xmit_lock[channel]) {
                                restore_flags(flags);
                                break;
                        }
                        card->xmit_lock[channel]++;
                        restore_flags(flags);
                        skb = skb_dequeue(&card->spqueue[channel]);
                        if (!skb)
                                break;
		        if (skb->len > ICN_FRAGSIZE) {
			    writeb (0xff, &sbuf_f);
			    cnt = ICN_FRAGSIZE;
			} else {
			    writeb (0x0, &sbuf_f);
			    cnt = skb->len;
			}
		        writeb (cnt, &sbuf_l);		    
                        memcpy_toio(&sbuf_d, skb->data, cnt);
                        skb_pull(skb, cnt);
                        card->sndcount[channel] -= cnt;
                        sbnext;        /* switch to next buffer        */
                        icn_maprelease_channel(card, mch & 2);
                        if (!skb->len) {
                                dev_kfree_skb(skb, FREE_WRITE);
                                cmd.command = ISDN_STAT_BSENT;
                                cmd.driver = card->myid;
                                cmd.arg = channel;
                                card->interface.statcallb(&cmd);
                        } else
                                skb_queue_head(&card->spqueue[channel], skb);
                        card->xmit_lock[channel] = 0;
                        if (!icn_trymaplock_channel(card, mch))
                                break;
                }
                icn_maprelease_channel(card, mch & 2);
        }
}

/* Send/Receive Data to/from the B-Channel.
 * This routine is called via timer-callback.
 * It schedules itself while any B-Channel is open.
 */

static void icn_pollbchan(unsigned long data)
{
        icn_card *card = (icn_card *)data;
        unsigned long flags;

                if (card->flags & ICN_FLAGS_B1ACTIVE) {
                        icn_pollbchan_receive(0, card);
                        icn_pollbchan_send(0, card);
                }
                if (card->flags & ICN_FLAGS_B2ACTIVE) {
                        icn_pollbchan_receive(1, card);
                        icn_pollbchan_send(1, card);
                }
                if (card->flags & (ICN_FLAGS_B1ACTIVE | ICN_FLAGS_B2ACTIVE)) {
                        /* schedule b-channel polling again */
                        save_flags(flags);
                        cli();
                        del_timer(&card->rb_timer);
                        card->rb_timer.expires = jiffies + ICN_TIMER_BCREAD;
                        add_timer(&card->rb_timer);
                        card->flags |= ICN_FLAGS_RBTIMER;
                        restore_flags(flags);
                } else
                        card->flags &= ~ICN_FLAGS_RBTIMER;
}

typedef struct icn_stat {
        char *statstr;
        int  command;
        int  action;
} icn_stat;

static icn_stat icn_stat_table[] = {
        {"BCON_",          ISDN_STAT_BCONN, 1}, /* B-Channel connected        */
        {"BDIS_",          ISDN_STAT_BHUP,  2}, /* B-Channel disconnected     */
        {"DCON_",          ISDN_STAT_DCONN, 0}, /* D-Channel connected        */
        {"DDIS_",          ISDN_STAT_DHUP,  0}, /* D-Channel disconnected     */
        {"DCAL_I",         ISDN_STAT_ICALL, 3}, /* Incoming call dialup-line  */
        {"DSCA_I",         ISDN_STAT_ICALL, 3}, /* Incoming call 1TR6-SPV     */
        {"FCALL",          ISDN_STAT_ICALL, 4}, /* Leased line connection up  */
        {"CIF",            ISDN_STAT_CINF,  5}, /* Charge-info, 1TR6-type     */
        {"AOC",            ISDN_STAT_CINF,  6}, /* Charge-info, DSS1-type     */
        {"CAU",            ISDN_STAT_CAUSE, 7}, /* Cause code                 */
        {"TEI OK",         ISDN_STAT_RUN,   0}, /* Card connected to wallplug */
        {"NO D-CHAN",      ISDN_STAT_NODCH, 0}, /* No D-channel available     */
        {"E_L1: ACT FAIL", ISDN_STAT_BHUP,  8}, /* Layer-1 activation failed  */
        {NULL,             0             , -1}
};

/*
 * Check Statusqueue-Pointer from isdn-cards.
 * If there are new status-replies from the interface, check
 * them against B-Channel-connects/disconnects and set flags accordingly.
 * Wake-Up any processes, who are reading the status-device.
 * If there are B-Channels open, initiate a timer-callback to
 * icn_pollbchan().
 * This routine is called periodically via timer.
 */

static int icn_parse_status(u_char *status, int channel, icn_card *card)
{
        icn_stat  *s = icn_stat_table;
        int       action = -1;
        int       dflag  = 0;
        unsigned long flags;
        isdn_ctrl cmd;

        while (s->statstr) {
                if (!strncmp(status,s->statstr,strlen(s->statstr))) {
                        cmd.command = s->command;
                        action = s->action;
                        break;
                }
                s++;
        }
        if (action==-1)
                return 0;
        cmd.driver = card->myid;
        cmd.arg = channel;
        switch (action) {
                case 1:
                        card->flags |= (channel)?
                                ICN_FLAGS_B2ACTIVE:ICN_FLAGS_B1ACTIVE;
                        break;
                case 2:
                        card->flags &= ~((channel)?
                                ICN_FLAGS_B2ACTIVE:ICN_FLAGS_B1ACTIVE);
                        icn_free_queue(&card->spqueue[channel]);
                        save_flags(flags);
                        cli();
                        card->rcvidx[channel] = 0;
                        restore_flags(flags);
                        dflag |= (channel+1);
                        break;
                case 3:
                        strncpy(cmd.num, status + 6, sizeof(cmd.num) - 1);
                        break;
                case 4:
                        sprintf(cmd.num,"LEASED%d,07,00,%d",
                                 card->myid,channel+1);
                        break;
                case 5:
                        strncpy(cmd.num, status + 3, sizeof(cmd.num) - 1);
                        break;
                case 6:
                        sprintf(cmd.num,"%d",
                                (int)simple_strtoul(status + 7,NULL,16));
                        break;
                case 7:
                        status += 3;
                        if (strlen(status)==4)
                                sprintf(cmd.num,"%s%c%c",
                                        status+2,*status,*(status+1));
                        else
                                strncpy(cmd.num, status+1, sizeof(cmd.num) - 1);
                        break;
                case 8:
                        cmd.arg = 0;
                        cmd.driver = card->myid;
                        card->interface.statcallb(&cmd);
                        cmd.command = ISDN_STAT_DHUP;
                        cmd.arg = 0;
                        cmd.driver = card->myid;
                        card->interface.statcallb(&cmd);
                        cmd.command = ISDN_STAT_BHUP;
                        cmd.arg = 1;
                        cmd.driver = card->myid;
                        card->interface.statcallb(&cmd);
                        cmd.command = ISDN_STAT_DHUP;
                        cmd.arg = 1;
                        cmd.driver = card->myid;
                        break;
        }
        card->interface.statcallb(&cmd);
        return dflag;
}

static void icn_polldchan(unsigned long data)
{
        icn_card *card = (icn_card *)data;
        int mch = card->secondhalf ? 2 : 0;
        int avail = 0;
        int dflag = 0;
        int left;
        u_char c;
        int ch;
        int flags;
        int i;
        u_char *p;
        isdn_ctrl cmd;

        if (icn_trymaplock_channel(card,mch)) {
                avail = msg_avail;
                for (left = avail, i = readb(&msg_o); left > 0; i++, left--) {
                        c = readb(&dev.shmem->comm_buffers.iopc_buf[i & 0xff]);
                        save_flags(flags);
                        cli();
                        *card->msg_buf_write++ = (c == 0xff) ? '\n' : c;
                        if (card->msg_buf_write == card->msg_buf_read) {
                                if (++card->msg_buf_read > card->msg_buf_end)
                                        card->msg_buf_read = card->msg_buf;
                        }
                        if (card->msg_buf_write > card->msg_buf_end)
                                card->msg_buf_write = card->msg_buf;
                        restore_flags(flags);
                        if (c == 0xff) {
                                card->imsg[card->iptr] = 0;
                                card->iptr = 0;
                                if (card->imsg[0] == '0' && card->imsg[1] >= '0' &&
                                    card->imsg[1] <= '2' && card->imsg[2] == ';') {
                                        ch = (card->imsg[1] - '0') - 1;
                                        p = &card->imsg[3];
                                        dflag |= icn_parse_status(p, ch, card);
                                } else {
                                        p = card->imsg;
                                        if (!strncmp(p, "DRV1.", 5)) {
                                                u_char vstr[10];
                                                u_char *q = vstr;

                                                printk(KERN_INFO "icn: (%s) %s\n",CID,p);
                                                if (!strncmp(p + 7, "TC", 2)) {
                                                        card->ptype = ISDN_PTYPE_1TR6;
                                                        card->interface.features |= ISDN_FEATURE_P_1TR6;
                                                        printk(KERN_INFO
                                                               "icn: (%s) 1TR6-Protocol loaded and running\n",CID);
                                                }
                                                if (!strncmp(p + 7, "EC", 2)) {
                                                        card->ptype = ISDN_PTYPE_EURO;
                                                        card->interface.features |= ISDN_FEATURE_P_EURO;
                                                        printk(KERN_INFO
                                                               "icn: (%s) Euro-Protocol loaded and running\n",CID);
                                                }
                                                p = strstr(card->imsg,"BRV") + 3;
                                                while (*p) {
                                                        if (*p>='0' && *p<='9')
                                                                *q++ = *p;
                                                        p++;
                                                }
                                                *q = '\0';
                                                strcat(vstr,"000");
                                                vstr[3] = '\0';
                                                card->fw_rev = (int)simple_strtoul(vstr,NULL,10);
                                                continue;
                                                
                                        }
                                }
                        } else {
                                card->imsg[card->iptr] = c;
                                if (card->iptr < 59)
                                        card->iptr++;
                        }
                }
                writeb((readb(&msg_o) + avail) & 0xff, &msg_o);
                icn_release_channel();
        }
        if (avail) {
                cmd.command = ISDN_STAT_STAVAIL;
                cmd.driver = card->myid;
                cmd.arg = avail;
                card->interface.statcallb(&cmd);
        }
        if (dflag & 1)
                card->interface.rcvcallb(card->myid, 0, card->rcvbuf[0], 0);
        if (dflag & 2)
                card->interface.rcvcallb(card->myid, 1, card->rcvbuf[1], 0);
        if (card->flags & (ICN_FLAGS_B1ACTIVE | ICN_FLAGS_B2ACTIVE))
                if (!(card->flags & ICN_FLAGS_RBTIMER)) {
                        /* schedule b-channel polling */
                        card->flags |= ICN_FLAGS_RBTIMER;
                        save_flags(flags);
                        cli();
                        del_timer(&card->rb_timer);
                        card->rb_timer.function = icn_pollbchan;
                        card->rb_timer.data = (unsigned long)card;
                        card->rb_timer.expires = jiffies + ICN_TIMER_BCREAD;
                        add_timer(&card->rb_timer);
                        restore_flags(flags);
                }
        /* schedule again */
        save_flags(flags);
        cli();
        del_timer(&card->st_timer);
        card->st_timer.expires = jiffies + ICN_TIMER_DCREAD;
        add_timer(&card->st_timer);
        restore_flags(flags);
}

/* Append a packet to the transmit buffer-queue.
 * Parameters:
 *   channel = Number of B-channel
 *   skb     = pointer to sk_buff
 *   card    = pointer to card-struct
 * Return:
 *   Number of bytes transferred, -E??? on error
 */

static int icn_sendbuf(int channel, struct sk_buff *skb, icn_card * card)
{
        int len = skb->len;
        unsigned long flags;
        struct sk_buff *nskb;

        if (len > 4000) {
                printk(KERN_WARNING
                       "icn: Send packet too large\n");
                return -EINVAL;
        }
        if (len) {
                if (!(card->flags & (channel)?ICN_FLAGS_B2ACTIVE:ICN_FLAGS_B1ACTIVE))
                        return 0;
                if (card->sndcount[channel] > ICN_MAX_SQUEUE)
                        return 0;
                save_flags(flags);
                cli();
                nskb = skb_clone(skb, GFP_ATOMIC);
                if (nskb) {
                        skb_queue_tail(&card->spqueue[channel], nskb);
                        dev_kfree_skb(skb, FREE_WRITE);
                } else
                        len = 0;
                card->sndcount[channel] += len;
                restore_flags(flags);
        }
        return len;
}

/*
 * Check card's status after starting the bootstrap loader.
 * On entry, the card's shared memory has already to be mapped.
 * Return:
 *   0 on success (Boot loader ready)
 *   -EIO on failure (timeout)
 */
static int icn_check_loader(int cardnumber)
{
        int timer = 0;

        while (1) {
#ifdef BOOT_DEBUG
                printk(KERN_DEBUG "Loader %d ?\n", cardnumber);
#endif
                if (readb(&dev.shmem->data_control.scns) ||
                    readb(&dev.shmem->data_control.scnr)) {
                        if (timer++ > 5) {
                                printk(KERN_WARNING
                                       "icn: Boot-Loader %d timed out.\n",
                                       cardnumber);
                                icn_release_channel();
                                return -EIO;
                        }
#ifdef BOOT_DEBUG
                        printk(KERN_DEBUG "Loader %d TO?\n", cardnumber);
#endif
                        current->state = TASK_INTERRUPTIBLE;
                        current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
                        schedule();
                } else {
#ifdef BOOT_DEBUG
                        printk(KERN_DEBUG "Loader %d OK\n", cardnumber);
#endif
                        icn_release_channel();
                        return 0;
                }
        }
}

/* Load the boot-code into the interface-card's memory and start it.
 * Always called from user-process.
 * 
 * Parameters:
 *            buffer = pointer to packet
 * Return:
 *        0 if successfully loaded
 */

#ifdef BOOT_DEBUG
#define SLEEP(sec) { \
int slsec = sec; \
  printk(KERN_DEBUG "SLEEP(%d)\n",slsec); \
  while (slsec) { \
    current->state = TASK_INTERRUPTIBLE; \
    current->timeout = jiffies + HZ; \
    schedule(); \
    slsec--; \
  } \
}
#else
#define SLEEP(sec)
#endif

static int icn_loadboot(u_char * buffer, icn_card * card)
{
        int ret;
        ulong flags;
	u_char *codebuf;

#ifdef BOOT_DEBUG
        printk(KERN_DEBUG "icn_loadboot called, buffaddr=%08lx\n", (ulong) buffer);
#endif
        if ((ret = verify_area(VERIFY_READ, (void *) buffer, ICN_CODE_STAGE1)))
                return ret;
        if (!(codebuf = kmalloc(ICN_CODE_STAGE1,GFP_KERNEL))) {
                printk(KERN_WARNING "icn: Could not allocate code buffer\n");
                return -ENOMEM;
        }
        save_flags(flags);
        cli();
        if (!card->rvalid) {
                if (check_region(card->port, ICN_PORTLEN)) {
                        printk(KERN_WARNING
                               "icn: (%s) ports 0x%03x-0x%03x in use.\n",
                               CID,
                               card->port,
                               card->port + ICN_PORTLEN);
                        restore_flags(flags);
                        kfree(codebuf);
                        return -EBUSY;
                }
                request_region(card->port, ICN_PORTLEN, card->regname);
                card->rvalid = 1;
                if (card->doubleS0)
                        card->other->rvalid = 1;
        }
        if (!dev.mvalid) {
                if (check_shmem((ulong) dev.shmem, 0x4000)) {
                        printk(KERN_WARNING
                               "icn: memory at 0x%08lx in use.\n",
                               (ulong) dev.shmem);
                        restore_flags(flags);
                        return -EBUSY;
                }
                request_shmem((ulong) dev.shmem, 0x4000, "icn");
                dev.mvalid = 1;
        }
        restore_flags(flags);
        OUTB_P(0, ICN_RUN);                                       /* Reset Controller */
        OUTB_P(0, ICN_MAPRAM);                                    /* Disable RAM      */
        icn_shiftout(ICN_CFG, 0x0f, 3, 4);                        /* Windowsize= 16k  */
        icn_shiftout(ICN_CFG, (unsigned long) dev.shmem, 23, 10); /* Set RAM-Addr.    */
#ifdef BOOT_DEBUG
        printk(KERN_DEBUG "shmem=%08lx\n", (ulong) dev.shmem);
#endif
        SLEEP(1);
        save_flags(flags);
        cli();
#ifdef BOOT_DEBUG
        printk(KERN_DEBUG "Map Bank 0\n");
#endif
        icn_map_channel(card,0);                                   /* Select Bank 0    */
        icn_lock_channel(card,0);                                  /* Lock Bank 0      */
        restore_flags(flags);
        SLEEP(1);
        if (copy_from_user(codebuf, buffer, ICN_CODE_STAGE1))
        	return -EFAULT;
        memcpy_toio(dev.shmem, codebuf, ICN_CODE_STAGE1);           /* Copy code        */
#ifdef BOOT_DEBUG
        printk(KERN_DEBUG "Bootloader transfered\n");
#endif
        if (card->doubleS0) {
                SLEEP(1);
                save_flags(flags);
                cli();
                icn_release_channel();
#ifdef BOOT_DEBUG
                printk(KERN_DEBUG "Map Bank 8\n");
#endif
                icn_map_channel(card,2);                           /* Select Bank 8   */
                icn_lock_channel(card,2);                          /* Lock Bank 8     */
                restore_flags(flags);
                SLEEP(1);
                memcpy_toio(dev.shmem, codebuf, ICN_CODE_STAGE1);           /* Copy code        */
#ifdef BOOT_DEBUG
                printk(KERN_DEBUG "Bootloader transfered\n");
#endif
        }
        kfree(codebuf);
        SLEEP(1);
        OUTB_P(0xff, ICN_RUN);                                     /* Start Boot-Code */
        if ((ret = icn_check_loader(card->doubleS0 ? 2 : 1)))
                return ret;
        if (!card->doubleS0)
                return 0;
        /* reached only, if we have a Double-S0-Card */
        save_flags(flags);
        cli();
#ifdef BOOT_DEBUG
        printk(KERN_DEBUG "Map Bank 0\n");
#endif
        icn_map_channel(card,0);                                   /* Select Bank 0   */
        icn_lock_channel(card,0);                                  /* Lock Bank 0     */
        restore_flags(flags);
        SLEEP(1);
        return (icn_check_loader(1));
}

static int icn_loadproto(u_char * buffer, icn_card * card)
{
        register u_char *p = buffer;
        u_char codebuf[256];
        uint left = ICN_CODE_STAGE2;
        uint cnt;
        int timer;
        int ret;
        unsigned long flags;

#ifdef BOOT_DEBUG
        printk(KERN_DEBUG "icn_loadproto called\n");
#endif
        if ((ret = verify_area(VERIFY_READ, (void *) buffer, ICN_CODE_STAGE2)))
                return ret;
        timer = 0;
        save_flags(flags);
        cli();
        if (card->secondhalf) {
                icn_map_channel(card, 2);
                icn_lock_channel(card, 2);
        } else {
                icn_map_channel(card, 0);
                icn_lock_channel(card, 0);
        }
        restore_flags(flags);
        while (left) {
                if (sbfree) {                           /* If there is a free buffer...  */
                        cnt = MIN(256, left);
                        if (copy_from_user(codebuf, p, cnt))
                        	/* FIXME -WRONG */return -EFAULT;
                        memcpy_toio(&sbuf_l, codebuf, cnt); /* copy data                     */ 
                        sbnext;                         /* switch to next buffer         */
                        p += cnt;
                        left -= cnt;
                        timer = 0;
                } else {
#ifdef BOOT_DEBUG
                        printk(KERN_DEBUG "boot 2 !sbfree\n");
#endif
                        if (timer++ > 5) {
                                icn_maprelease_channel(card, 0);
                                return -EIO;
                        }
                        current->state = TASK_INTERRUPTIBLE;
                        current->timeout = jiffies + 10;
                        schedule();
                }
        }
        writeb (0x20, &sbuf_n);
        timer = 0;
        while (1) {
                if (readb(&cmd_o) || readb(&cmd_i)) {
#ifdef BOOT_DEBUG
                        printk(KERN_DEBUG "Proto?\n");
#endif
                        if (timer++ > 5) {
                                printk(KERN_WARNING
                                       "icn: (%s) Protocol timed out.\n",
                                       CID);
#ifdef BOOT_DEBUG
                                printk(KERN_DEBUG "Proto TO!\n");
#endif
                                icn_maprelease_channel(card, 0);
                                return -EIO;
                        }
#ifdef BOOT_DEBUG
                        printk(KERN_DEBUG "Proto TO?\n");
#endif
                        current->state = TASK_INTERRUPTIBLE;
                        current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
                        schedule();
                } else {
                        if ((card->secondhalf) || (!card->doubleS0)) {
                                save_flags(flags);
                                cli();
#ifdef BOOT_DEBUG
                                printk(KERN_DEBUG "Proto loaded, install poll-timer %d\n",
                                       card->secondhalf);
#endif
                                init_timer(&card->st_timer);
                                card->st_timer.expires = jiffies + ICN_TIMER_DCREAD;
                                card->st_timer.function = icn_polldchan;
                                card->st_timer.data = (unsigned long)card;
                                add_timer(&card->st_timer);
                                card->flags |= ICN_FLAGS_RUNNING;
                                if (card->doubleS0) {
                                        init_timer(&card->other->st_timer);
                                        card->other->st_timer.expires = jiffies + ICN_TIMER_DCREAD;
                                        card->other->st_timer.function = icn_polldchan;
                                        card->other->st_timer.data = (unsigned long)card->other;
                                        add_timer(&card->other->st_timer);
                                        card->other->flags |= ICN_FLAGS_RUNNING;
                                }
                                restore_flags(flags);
                        }
                        icn_maprelease_channel(card, 0);
                        return 0;
                }
        }
}

/* Read the Status-replies from the Interface */
static int icn_readstatus(u_char * buf, int len, int user, icn_card * card)
{
        int count;
        u_char *p;

        for (p = buf, count = 0; count < len; p++, count++) {
                if (card->msg_buf_read == card->msg_buf_write)
                        return count;
                if (user)
                        put_user(*card->msg_buf_read++, p);
                else
                        *p = *card->msg_buf_read++;
                if (card->msg_buf_read > card->msg_buf_end)
                        card->msg_buf_read = card->msg_buf;
        }
        return count;
}

/* Put command-strings into the command-queue of the Interface */
static int icn_writecmd(const u_char * buf, int len, int user, icn_card * card, int waitflg)
{
        int mch = card->secondhalf ? 2 : 0;
        int avail;
        int pp;
        int i;
        int count;
        int ocount;
        unsigned long flags;
        u_char *p;
        isdn_ctrl cmd;
        u_char msg[0x100];

        while (1) {
                if (icn_trymaplock_channel(card, mch)) {
                        avail = cmd_free;
                        count = MIN(avail, len);
                        if (user)
                        {
                                if (copy_from_user(msg, buf, count) != 0)
                                {
                                	icn_release_channel();
                                	return -EFAULT;
                                }
                        }
                        else
                                memcpy(msg, buf, count);
                        save_flags(flags);
                        cli();
                        ocount = 1;
                        *card->msg_buf_write++ = '>';
                        if (card->msg_buf_write > card->msg_buf_end)
                                card->msg_buf_write = card->msg_buf;
                        for (p = msg, pp = readb(&cmd_i), i = count; i > 0; i--, p++, pp++) {
                                writeb((*p == '\n') ? 0xff : *p,
                                       &dev.shmem->comm_buffers.pcio_buf[pp & 0xff]);
                                *card->msg_buf_write++ = *p;
                                if ((*p == '\n') && (i > 1)) {
                                        *card->msg_buf_write++ = '>';
                                        if (card->msg_buf_write > card->msg_buf_end)
                                                card->msg_buf_write = card->msg_buf;
                                        ocount++;
                                }
                                /* No checks for buffer overflow of raw-status-device */
                                if (card->msg_buf_write > card->msg_buf_end)
                                        card->msg_buf_write = card->msg_buf;
                                ocount++;
                        }
                        restore_flags(flags);
                        cmd.command = ISDN_STAT_STAVAIL;
                        cmd.driver = card->myid;
                        cmd.arg = ocount;
                        card->interface.statcallb(&cmd);
                        writeb((readb(&cmd_i) + count) & 0xff, &cmd_i);
                        icn_release_channel();
                        waitflg = 0;
                } else
                        count = 0;
                if (!waitflg)
                        break;
                current->timeout = jiffies + 10;
                schedule();
        }
        return count;
}

/*
 * Delete card's pending timers, send STOP to linklevel
 */
static void icn_stopcard(icn_card * card)
{
        unsigned long flags;
        isdn_ctrl cmd;

        save_flags(flags);
        cli();
        if (card->flags & ICN_FLAGS_RUNNING) {
                card->flags &= ~ICN_FLAGS_RUNNING;
                del_timer(&card->st_timer);
                del_timer(&card->rb_timer);
                cmd.command = ISDN_STAT_STOP;
                cmd.driver  = card->myid;
                card->interface.statcallb(&cmd);
                if (card->doubleS0)
                        icn_stopcard(card->other);
        }
        restore_flags(flags);
}

static void icn_stopallcards(void)
{
        icn_card *p = cards;

        while (p) {
                icn_stopcard(p);
                p = p->next;
        }
}

static int icn_command(isdn_ctrl * c, icn_card * card)
{
        ulong a;
        ulong flags;
        int i;
        char cbuf[60];
        isdn_ctrl cmd;
        icn_cdef cdef;

        switch (c->command) {
        case ISDN_CMD_IOCTL:
                memcpy(&a, c->num, sizeof(ulong));
                switch (c->arg) {
                        case ICN_IOCTL_SETMMIO:
                                if ((unsigned long) dev.shmem != (a & 0x0ffc000)) {
                                        if (check_shmem((ulong) (a & 0x0ffc000), 0x4000)) {
                                                printk(KERN_WARNING
                                                       "icn: memory at 0x%08lx in use.\n",
                                                       (ulong) (a & 0x0ffc000));
                                                return -EINVAL;
                                        }
                                        icn_stopallcards();
                                        save_flags(flags);
                                        cli();
                                        if (dev.mvalid)
                                                release_shmem((ulong) dev.shmem, 0x4000);
                                        dev.mvalid = 0;
                                        dev.shmem = (icn_shmem *) (a & 0x0ffc000);
                                        restore_flags(flags);
                                        printk(KERN_INFO
                                               "icn: (%s) mmio set to 0x%08lx\n",
                                               CID,
                                               (unsigned long) dev.shmem);
                                }
                                break;
                        case ICN_IOCTL_GETMMIO:
                                return (long) dev.shmem;
                        case ICN_IOCTL_SETPORT:
                                if (a == 0x300 || a == 0x310 || a == 0x320 || a == 0x330
                                    || a == 0x340 || a == 0x350 || a == 0x360 ||
                                    a == 0x308 || a == 0x318 || a == 0x328 || a == 0x338
                                    || a == 0x348 || a == 0x358 || a == 0x368) {
                                        if (card->port != (unsigned short) a) {
                                                if (check_region((unsigned short) a, ICN_PORTLEN)) {
                                                        printk(KERN_WARNING
                                                               "icn: (%s) ports 0x%03x-0x%03x in use.\n",
                                                               CID, (int) a, (int) a + ICN_PORTLEN);
                                                        return -EINVAL;
                                                }
                                                icn_stopcard(card);
                                                save_flags(flags);
                                                cli();
                                                if (card->rvalid)
                                                        release_region(card->port, ICN_PORTLEN);
                                                card->port = (unsigned short) a;
                                                card->rvalid = 0;
                                                if (card->doubleS0) {
                                                        card->other->port = (unsigned short) a;
                                                        card->other->rvalid = 0;
                                                }
                                                restore_flags(flags);
                                                printk(KERN_INFO
                                                       "icn: (%s) port set to 0x%03x\n",
                                                       CID, card->port);
                                        }
                                } else
                                        return -EINVAL;
                                break;
                        case ICN_IOCTL_GETPORT:
                                return (int) card->port;
                        case ICN_IOCTL_GETDOUBLE:
                                return (int) card->doubleS0;
                        case ICN_IOCTL_DEBUGVAR:
				if ((i = verify_area(VERIFY_WRITE,
                                                    (void *) a,
                                                    sizeof(ulong) * 2)))
                                        return i;
                                if (copy_to_user((char *)a,
                                            (char *)&card, sizeof(ulong)))
                                            	return -EFAULT;
				a += sizeof(ulong);
				{
                                        ulong l = (ulong)&dev;
                                        if (copy_to_user((char *)a,
                                                    (char *)&l, sizeof(ulong)))
                                                    return -EFAULT;
                                }
                                return 0;
                        case ICN_IOCTL_LOADBOOT:
                                icn_stopcard(card);
                                return (icn_loadboot((u_char *) a, card));
                        case ICN_IOCTL_LOADPROTO:
                                icn_stopcard(card);
                                if ((i = (icn_loadproto((u_char *) a, card))))
                                        return i;
                                if (card->doubleS0)
                                        i = icn_loadproto((u_char *) (a + ICN_CODE_STAGE2), card->other);
                                return i;
                                break;
                        case ICN_IOCTL_ADDCARD:
                                if ((i = verify_area(VERIFY_READ, (void *) a, sizeof(icn_cdef))))
                                        return i;
                                if (copy_from_user((char *)&cdef, (char *)a, sizeof(cdef)))
                                	return -EFAULT;
                                return (icn_addcard(cdef.port, cdef.id1, cdef.id2));
                                break;
                        case ICN_IOCTL_LEASEDCFG:
                                if (a) {
                                        if (!card->leased) {
                                                card->leased = 1;
                                                while (card->ptype == ISDN_PTYPE_UNKNOWN) {
                                                        current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
                                                        schedule();
                                                }
                                                current->timeout = jiffies + ICN_BOOT_TIMEOUT1;
                                                schedule();
                                                sprintf(cbuf, "00;FV2ON\n01;EAZ1\n");
                                                i = icn_writecmd(cbuf, strlen(cbuf), 0, card, 1);
                                                printk(KERN_INFO
                                                       "icn: (%s) Leased-line mode enabled\n",
                                                       CID);
                                                cmd.command = ISDN_STAT_RUN;
                                                cmd.driver = card->myid;
                                                cmd.arg = 0;
                                                card->interface.statcallb(&cmd);
                                        }
                                } else {
                                        if (card->leased) {
                                                card->leased = 0;
                                                sprintf(cbuf, "00;FV2OFF\n");
                                                i = icn_writecmd(cbuf, strlen(cbuf), 0, card, 1);
                                                printk(KERN_INFO
                                                       "icn: (%s) Leased-line mode disabled\n",
                                                       CID);
                                                cmd.command = ISDN_STAT_RUN;
                                                cmd.driver = card->myid;
                                                cmd.arg = 0;
                                                card->interface.statcallb(&cmd);
                                        }
                                }
                                return 0;
                        default:
                                return -EINVAL;
                }
                break;
                case ISDN_CMD_DIAL:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        if (card->leased)
                                break;
                        if ((c->arg & 255) < ICN_BCH) {
                                char *p;
                                char *p2;
                                char dial[50];
                                char sis[50];
                                char dcode[4];
                                int si1, si2;
                                
                                a = c->arg;
                                strcpy(sis, c->num);
                                p = strrchr(sis, ',');
                                *p++ = '\0';
                                si2 = simple_strtoul(p,NULL,10);
                                p = strrchr(sis, ',') + 1;
                                si1 = simple_strtoul(p,NULL,10);
                                p = c->num;
                                if (*p == 's' || *p == 'S') {
                                        /* Dial for SPV */
                                        p++;
                                        strcpy(dcode, "SCA");
                                } else
                                        /* Normal Dial */
                                        strcpy(dcode, "CAL");
                                strcpy(dial, p);
                                p = strchr(dial, ',');
                                *p++ = '\0';
                                p2 = strchr(p, ',');
                                *p2 = '\0';
                                sprintf(cbuf, "%02d;D%s_R%s,%02d,%02d,%s\n", (int) (a + 1), dcode, dial, si1,
                                        si2, p);
                                i = icn_writecmd(cbuf, strlen(cbuf), 0, card, 1);
                        }
                        break;
                case ISDN_CMD_ACCEPTD:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        if (c->arg < ICN_BCH) {
                                a = c->arg + 1;
                                if (card->fw_rev >= 300) {
                                        switch (card->l2_proto[a-1]) {
                                                case ISDN_PROTO_L2_X75I:
                                                        sprintf(cbuf, "%02d;BX75\n", (int) a);
                                                        break;
                                                case ISDN_PROTO_L2_HDLC:
                                                        sprintf(cbuf, "%02d;BTRA\n", (int) a);
                                                        break;
                                        }
                                        i = icn_writecmd(cbuf, strlen(cbuf), 0, card, 1);
                                }
                                sprintf(cbuf, "%02d;DCON_R\n", (int) a);
                                i = icn_writecmd(cbuf, strlen(cbuf), 0, card, 1);
                        }
                        break;
                case ISDN_CMD_ACCEPTB:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        if (c->arg < ICN_BCH) {
                                a = c->arg + 1;
                                if (card->fw_rev >= 300)
                                        switch (card->l2_proto[a-1]) {
                                                case ISDN_PROTO_L2_X75I:
                                                        sprintf(cbuf, "%02d;BCON_R,BX75\n", (int) a);
                                                        break;
                                                case ISDN_PROTO_L2_HDLC:
                                                        sprintf(cbuf, "%02d;BCON_R,BTRA\n", (int) a);
                                                        break;
                                        }
                                else
                                        sprintf(cbuf, "%02d;BCON_R\n", (int) a);
                                i = icn_writecmd(cbuf, strlen(cbuf), 0, card, 1);
                        }
                        break;
                case ISDN_CMD_HANGUP:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        if (c->arg < ICN_BCH) {
                                a = c->arg + 1;
                                sprintf(cbuf, "%02d;BDIS_R\n%02d;DDIS_R\n", (int) a, (int) a);
                                i = icn_writecmd(cbuf, strlen(cbuf), 0, card, 1);
                        }
                        break;
                case ISDN_CMD_SETEAZ:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        if (card->leased)
                                break;
                        if (c->arg < ICN_BCH) {
                                a = c->arg + 1;
                                if (card->ptype == ISDN_PTYPE_EURO) {
                                        sprintf(cbuf, "%02d;MS%s%s\n", (int) a,
                                                c->num[0] ? "N" : "ALL", c->num);
                                } else
                                        sprintf(cbuf, "%02d;EAZ%s\n", (int) a,
                                                c->num[0] ? c->num : "0123456789");
                                i = icn_writecmd(cbuf, strlen(cbuf), 0, card, 1);
                        }
                        break;
                case ISDN_CMD_CLREAZ:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        if (card->leased)
                                break;
                        if (c->arg < ICN_BCH) {
                                a = c->arg + 1;
                                if (card->ptype == ISDN_PTYPE_EURO)
                                        sprintf(cbuf, "%02d;MSNC\n", (int) a);
                                else
                                        sprintf(cbuf, "%02d;EAZC\n", (int) a);
                                i = icn_writecmd(cbuf, strlen(cbuf), 0, card, 1);
                        }
                        break;
                case ISDN_CMD_SETL2:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        if ((c->arg & 255) < ICN_BCH) {
                                a = c->arg;
                                switch (a >> 8) {
                                        case ISDN_PROTO_L2_X75I:
                                                sprintf(cbuf, "%02d;BX75\n", (int) (a & 255) + 1);
                                                break;
                                        case ISDN_PROTO_L2_HDLC:
                                                sprintf(cbuf, "%02d;BTRA\n", (int) (a & 255) + 1);
                                                break;
                                        default:
                                                return -EINVAL;
                                }
                                i = icn_writecmd(cbuf, strlen(cbuf), 0, card, 1);
                                card->l2_proto[a & 255] = (a >> 8);
                        }
                        break;
                case ISDN_CMD_GETL2:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        if ((c->arg & 255) < ICN_BCH)
                                return card->l2_proto[c->arg & 255];
                        else
                                return -ENODEV;
                case ISDN_CMD_SETL3:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        return 0;
                case ISDN_CMD_GETL3:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        if ((c->arg & 255) < ICN_BCH)
                                return ISDN_PROTO_L3_TRANS;
                        else
                                return -ENODEV;
                case ISDN_CMD_GETEAZ:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        break;
                case ISDN_CMD_SETSIL:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        break;
                case ISDN_CMD_GETSIL:
                        if (!card->flags & ICN_FLAGS_RUNNING)
                                return -ENODEV;
                        break;
                case ISDN_CMD_LOCK:
                        MOD_INC_USE_COUNT;
                        break;
                case ISDN_CMD_UNLOCK:
                        MOD_DEC_USE_COUNT;
                        break;
                default:
                        return -EINVAL;
        }
        return 0;
}

/*
 * Find card with given driverId
 */
static inline icn_card *
  icn_findcard(int driverid)
{
        icn_card *p = cards;

        while (p) {
                if (p->myid == driverid)
                        return p;
                p = p->next;
        }
        return (icn_card *)0;
}

/*
 * Wrapper functions for interface to linklevel
 */
static int if_command(isdn_ctrl * c)
{
        icn_card *card = icn_findcard(c->driver);

        if (card)
                return (icn_command(c, card));
        printk(KERN_ERR
               "icn: if_command %d called with invalid driverId %d!\n",
		c->command, c->driver);
        return -ENODEV;
}

static int if_writecmd(const u_char * buf, int len, int user, int id, int channel)
{
        icn_card *card = icn_findcard(id);

        if (card) {
                if (!card->flags & ICN_FLAGS_RUNNING)
                        return -ENODEV;
                return (icn_writecmd(buf, len, user, card, 0));
        }
        printk(KERN_ERR
               "icn: if_writecmd called with invalid driverId!\n");
        return -ENODEV;
}

static int if_readstatus(u_char * buf, int len, int user, int id, int channel)
{
        icn_card *card = icn_findcard(id);

        if (card) {
                if (!card->flags & ICN_FLAGS_RUNNING)
                        return -ENODEV;
                return (icn_readstatus(buf, len, user, card));
        }
        printk(KERN_ERR
               "icn: if_readstatus called with invalid driverId!\n");
        return -ENODEV;
}

static int if_sendbuf(int id, int channel, struct sk_buff *skb)
{
        icn_card *card = icn_findcard(id);

        if (card) {
                if (!card->flags & ICN_FLAGS_RUNNING)
                        return -ENODEV;
                return (icn_sendbuf(channel, skb, card));
        }
        printk(KERN_ERR
               "icn: if_sendbuf called with invalid driverId!\n");
        return -ENODEV;
}

/*
 * Allocate a new card-struct, initialize it
 * link it into cards-list and register it at linklevel.
 */
static icn_card *icn_initcard(int port, char *id) {
        icn_card *card;
        int i;

        if (!(card = (icn_card *) kmalloc(sizeof(icn_card), GFP_KERNEL))) {
                printk(KERN_WARNING
                       "icn: (%s) Could not allocate card-struct.\n", id);
                return (icn_card *)0;
        }
        memset((char *) card, 0, sizeof(icn_card));
        card->port = port;
        card->interface.channels = ICN_BCH;
        card->interface.maxbufsize = 4000;
        card->interface.command = if_command;
	card->interface.writebuf_skb = if_sendbuf;
        card->interface.writecmd = if_writecmd;
        card->interface.readstat = if_readstatus;
        card->interface.features = ISDN_FEATURE_L2_X75I |
                ISDN_FEATURE_L2_HDLC |
                ISDN_FEATURE_L3_TRANS |
                ISDN_FEATURE_P_UNKNOWN;
        card->ptype = ISDN_PTYPE_UNKNOWN;
        strncpy(card->interface.id, id, sizeof(card->interface.id) - 1);
        card->msg_buf_write = card->msg_buf;
        card->msg_buf_read = card->msg_buf;
        card->msg_buf_end = &card->msg_buf[sizeof(card->msg_buf) - 1];
        for (i=0;i<ICN_BCH;i++) {
                card->l2_proto[i] = ISDN_PROTO_L2_X75I;
                skb_queue_head_init(&card->spqueue[i]);
        }
        card->next = cards;
        cards = card;
        if (!register_isdn(&card->interface)) {
                cards = cards->next;
                printk(KERN_WARNING
                       "icn: Unable to register %s\n", id);
                kfree(card);
                return (icn_card*)0;
        }
        card->myid = card->interface.channels;
        sprintf(card->regname, "icn-isdn (%s)", card->interface.id);
        return card;
}

static int icn_addcard(int port, char *id1, char *id2)
{
        ulong flags;
        icn_card *card;
        icn_card *card2;

        save_flags(flags);
        cli();
        if (!(card = icn_initcard(port,id1))) {
                restore_flags(flags);
                return -EIO;
        }
        if (!strlen(id2)) {
                restore_flags(flags);
                printk(KERN_INFO
                       "icn: (%s) ICN-2B, port 0x%x added\n",
                       card->interface.id, port);
                return 0;
        }
        if (!(card2 = icn_initcard(port,id2))) {
                restore_flags(flags);
                printk(KERN_INFO
                       "icn: (%s) half ICN-4B, port 0x%x added\n",
                       card2->interface.id, port);
                return 0;
        }
        card->doubleS0 = 1;
        card->secondhalf = 0;
        card->other = card2;
        card2->doubleS0 = 1;
        card2->secondhalf = 1;
        card2->other = card;
        restore_flags(flags);
        printk(KERN_INFO
               "icn: (%s and %s) ICN-4B, port 0x%x added\n",
               card->interface.id, card2->interface.id, port);
        return 0;
}

#ifdef MODULE
#define icn_init init_module
#else
void icn_setup(char *str, int *ints)
{
        char *p;
        static char sid[20];
        static char sid2[20];

        if (ints[0])
                portbase = ints[1];
        if (ints[0]>1)
                membase = ints[2];
        if (strlen(str)) {
                strcpy(sid,str);
                icn_id = sid;
                if ((p = strchr(sid,','))) {
                        *p++ = 0;
                        strcpy(sid2,p);
                        icn_id2 = sid2;
                }
        }
}
#endif

int icn_init(void)
{
        char *p;
        char rev[10];

        memset(&dev, 0, sizeof(icn_dev));
        dev.shmem = (icn_shmem *) ((unsigned long)membase & 0x0ffc000);
        dev.channel = -1;
        dev.mcard   = NULL;

        /* No symbols to export, hide all symbols */
        register_symtab(NULL);

        if ((p = strchr(revision, ':'))) {
                strcpy(rev, p + 1);
                p = strchr(rev, '$');
                *p = 0;
        } else
                strcpy(rev, " ??? ");
        printk(KERN_NOTICE "ICN-ISDN-driver Rev%smem=0x%08lx\n", rev,
               (ulong) dev.shmem);
        return (icn_addcard(portbase,icn_id,icn_id2));
}

#ifdef MODULE
void cleanup_module(void)
{
        isdn_ctrl cmd;
        icn_card *card = cards;
        icn_card *last;
        int i;

        icn_stopallcards();
        while (card) {
                cmd.command = ISDN_STAT_UNLOAD;
                cmd.driver = card->myid;
                card->interface.statcallb(&cmd);
                if (card->rvalid) {
                        OUTB_P(0, ICN_RUN);    /* Reset Controller     */
                        OUTB_P(0, ICN_MAPRAM); /* Disable RAM          */
                        if (card->secondhalf || (!card->doubleS0)) {
                                release_region(card->port, ICN_PORTLEN);
                                card->rvalid = 0;
                        }
                        for (i = 0; i < ICN_BCH; i++)
                                icn_free_queue(&card->spqueue[i]);
                }
                card = card->next;
        }
        card = cards;
        while (card) {
                last = card;
                card = card->next;
                kfree(last);
        }
        if (dev.mvalid)
                release_shmem((ulong) dev.shmem, 0x4000);
        printk(KERN_NOTICE "ICN-ISDN-driver unloaded\n");
}
#endif
