/* This file contains all the functions required for the standalone
   ip_conntrack module.

   These are not required by the compatibility layer.
*/

/* (C) 1999-2001 Paul `Rusty' Russell
 * (C) 2002-2004 Netfilter Core Team <coreteam@netfilter.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/percpu.h>
#ifdef CONFIG_SYSCTL
#include <linux/sysctl.h>
#endif
#include <net/checksum.h>
#include <net/ip.h>

#define ASSERT_READ_LOCK(x) MUST_BE_READ_LOCKED(&ip_conntrack_lock)
#define ASSERT_WRITE_LOCK(x) MUST_BE_WRITE_LOCKED(&ip_conntrack_lock)

#include <linux/netfilter_ipv4/ip_conntrack.h>
#include <linux/netfilter_ipv4/ip_conntrack_protocol.h>
#include <linux/netfilter_ipv4/ip_conntrack_core.h>
#include <linux/netfilter_ipv4/ip_conntrack_helper.h>
#include <linux/netfilter_ipv4/listhelp.h>

#if 0
#define DEBUGP printk
#else
#define DEBUGP(format, args...)
#endif

MODULE_LICENSE("GPL");

extern atomic_t ip_conntrack_count;
DECLARE_PER_CPU(struct ip_conntrack_stat, ip_conntrack_stat);

unsigned int ip_ct_log_invalid = 0;

static int kill_proto(const struct ip_conntrack *i, void *data)
{
	return (i->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum == 
			*((u_int8_t *) data));
}

#ifdef CONFIG_PROC_FS
static unsigned int
print_tuple(char *buffer, const struct ip_conntrack_tuple *tuple,
	    struct ip_conntrack_protocol *proto)
{
	int len;

	len = sprintf(buffer, "src=%u.%u.%u.%u dst=%u.%u.%u.%u ",
		      NIPQUAD(tuple->src.ip), NIPQUAD(tuple->dst.ip));

	len += proto->print_tuple(buffer + len, tuple);

	return len;
}

#ifdef CONFIG_IP_NF_CT_ACCT
static unsigned int
seq_print_counters(struct seq_file *s, struct ip_conntrack_counter *counter)
{
	return seq_printf(s, "packets=%llu bytes=%llu ",
			  (unsigned long long)counter->packets,
			  (unsigned long long)counter->bytes);
}
#else
#define seq_print_counters(x, y)	0
#endif

static void *ct_seq_start(struct seq_file *s, loff_t *pos)
{
	unsigned int *bucket;

	/* strange seq_file api calls stop even if we fail,
	 * thus we need to grab lock since stop unlocks */
	READ_LOCK(&ip_conntrack_lock);
  
	if (*pos >= ip_conntrack_htable_size)
		return NULL;

	bucket = kmalloc(sizeof(unsigned int), GFP_KERNEL);
	if (!bucket) {
		return ERR_PTR(-ENOMEM);
	}
  
	*bucket = *pos;
	return bucket;
}
  
static void *ct_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
	unsigned int *bucket = (unsigned int *) v;

	*pos = ++(*bucket);
	if (*pos >= ip_conntrack_htable_size) {
		kfree(v);
		return NULL;
	}
	return bucket;
}
  
static void ct_seq_stop(struct seq_file *s, void *v)
{
	READ_UNLOCK(&ip_conntrack_lock);
}

/* return 0 on success, 1 in case of error */
static int ct_seq_real_show(const struct ip_conntrack_tuple_hash *hash,
			    struct seq_file *s)
{
	struct ip_conntrack *conntrack = hash->ctrack;
	struct ip_conntrack_protocol *proto;
	char buffer[IP_CT_PRINT_BUFLEN];

	MUST_BE_READ_LOCKED(&ip_conntrack_lock);

	IP_NF_ASSERT(conntrack);

	/* we only want to print DIR_ORIGINAL */
	if (DIRECTION(hash))
		return 0;

	proto = __ip_ct_find_proto(conntrack->tuplehash[IP_CT_DIR_ORIGINAL]
			       .tuple.dst.protonum);
	IP_NF_ASSERT(proto);

	if (seq_printf(s, "%-8s %u %lu ",
		      proto->name,
		      conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple.dst.protonum,
		      timer_pending(&conntrack->timeout)
		      ? (conntrack->timeout.expires - jiffies)/HZ : 0) != 0)
		return 1;

	proto->print_conntrack(buffer, conntrack);
	if (seq_puts(s, buffer))
		return 1;
  
	print_tuple(buffer, &conntrack->tuplehash[IP_CT_DIR_ORIGINAL].tuple,
		    proto);

 	if (seq_print_counters(s, &conntrack->counters[IP_CT_DIR_ORIGINAL]))
		return 1;

	if (!(test_bit(IPS_SEEN_REPLY_BIT, &conntrack->status)))
		if (seq_printf(s, "[UNREPLIED] "))
			return 1;

	print_tuple(buffer, &conntrack->tuplehash[IP_CT_DIR_REPLY].tuple,
		    proto);
	if (seq_puts(s, buffer))
		return 1;

 	if (seq_print_counters(s, &conntrack->counters[IP_CT_DIR_REPLY]))
		return 1;

	if (test_bit(IPS_ASSURED_BIT, &conntrack->status))
		if (seq_printf(s, "[ASSURED] "))
			return 1;

	if (seq_printf(s, "use=%u\n", atomic_read(&conntrack->ct_general.use)))
		return 1;

	return 0;
}


static int ct_seq_show(struct seq_file *s, void *v)
{
	unsigned int *bucket = (unsigned int *) v;

	if (LIST_FIND(&ip_conntrack_hash[*bucket], ct_seq_real_show,
		      struct ip_conntrack_tuple_hash *, s)) {
		/* buffer was filled and unable to print that tuple */
		return 1;
	}
	return 0;
}
	
static struct seq_operations ct_seq_ops = {
	.start = ct_seq_start,
	.next  = ct_seq_next,
	.stop  = ct_seq_stop,
	.show  = ct_seq_show
};
  
static int ct_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ct_seq_ops);
}

static struct file_operations ct_file_ops = {
	.owner   = THIS_MODULE,
	.open    = ct_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};
  
/* expects */
static void *exp_seq_start(struct seq_file *s, loff_t *pos)
{
	struct list_head *e = &ip_conntrack_expect_list;
	loff_t i;

	/* strange seq_file api calls stop even if we fail,
	 * thus we need to grab lock since stop unlocks */
	READ_LOCK(&ip_conntrack_lock);
	READ_LOCK(&ip_conntrack_expect_tuple_lock);

	if (list_empty(e))
		return NULL;

	for (i = 0; i <= *pos; i++) {
		e = e->next;
		if (e == &ip_conntrack_expect_list)
			return NULL;
	}
	return e;
}

static void *exp_seq_next(struct seq_file *s, void *v, loff_t *pos)
{
 	struct list_head *e = v;

	e = e->next;

	if (e == &ip_conntrack_expect_list)
		return NULL;

	return e;
}

static void exp_seq_stop(struct seq_file *s, void *v)
{
	READ_UNLOCK(&ip_conntrack_expect_tuple_lock);
	READ_UNLOCK(&ip_conntrack_lock);
}

static int exp_seq_show(struct seq_file *s, void *v)
{
	struct ip_conntrack_expect *expect = v;
	char buffer[IP_CT_PRINT_BUFLEN];

	if (expect->expectant->helper->timeout)
		seq_printf(s, "%lu ", timer_pending(&expect->timeout)
			   ? (expect->timeout.expires - jiffies)/HZ : 0);
	else
		seq_printf(s, "- ");

	seq_printf(s, "use=%u proto=%u ", atomic_read(&expect->use),
		   expect->tuple.dst.protonum);

	print_tuple(buffer, &expect->tuple,
		    __ip_ct_find_proto(expect->tuple.dst.protonum));
	return seq_printf(s, "%s\n", buffer);
}

static struct seq_operations exp_seq_ops = {
	.start = exp_seq_start,
	.next = exp_seq_next,
	.stop = exp_seq_stop,
	.show = exp_seq_show
};

static int exp_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &exp_seq_ops);
}
  
static struct file_operations exp_file_ops = {
	.owner   = THIS_MODULE,
	.open    = exp_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release
};

static void *ct_cpu_seq_start(struct seq_file *seq, loff_t *pos)
{
	int cpu;

	for (cpu = *pos; cpu < NR_CPUS; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu;
		return &per_cpu(ip_conntrack_stat, cpu);
	}

	return NULL;
}

static void *ct_cpu_seq_next(struct seq_file *seq, void *v, loff_t *pos)
{
	int cpu;

	for (cpu = *pos + 1; cpu < NR_CPUS; ++cpu) {
		if (!cpu_possible(cpu))
			continue;
		*pos = cpu;
		return &per_cpu(ip_conntrack_stat, cpu);
	}

	return NULL;
}

static void ct_cpu_seq_stop(struct seq_file *seq, void *v)
{
}

static int ct_cpu_seq_show(struct seq_file *seq, void *v)
{
	unsigned int nr_conntracks = atomic_read(&ip_conntrack_count);
	struct ip_conntrack_stat *st = v;

	seq_printf(seq, "%08x  %08x %08x %08x %08x %08x %08x %08x "
			"%08x %08x %08x %08x %08x  %08x %08x %08x \n",
		   nr_conntracks,
		   st->searched,
		   st->found,
		   st->new,
		   st->invalid,
		   st->ignore,
		   st->delete,
		   st->delete_list,
		   st->insert,
		   st->insert_failed,
		   st->drop,
		   st->early_drop,
		   st->icmp_error,

		   st->expect_new,
		   st->expect_create,
		   st->expect_delete
		);
	return 0;
}

static struct seq_operations ct_cpu_seq_ops = {
	.start  = ct_cpu_seq_start,
	.next   = ct_cpu_seq_next,
	.stop   = ct_cpu_seq_stop,
	.show   = ct_cpu_seq_show,
};

static int ct_cpu_seq_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &ct_cpu_seq_ops);
}

static struct file_operations ct_cpu_seq_fops = {
	.owner   = THIS_MODULE,
	.open    = ct_cpu_seq_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release_private,
};
#endif

static unsigned int ip_confirm(unsigned int hooknum,
			       struct sk_buff **pskb,
			       const struct net_device *in,
			       const struct net_device *out,
			       int (*okfn)(struct sk_buff *))
{
	/* We've seen it coming out the other side: confirm it */
	return ip_conntrack_confirm(*pskb);
}

static unsigned int ip_conntrack_defrag(unsigned int hooknum,
				        struct sk_buff **pskb,
				        const struct net_device *in,
				        const struct net_device *out,
				        int (*okfn)(struct sk_buff *))
{
	/* Previously seen (loopback)?  Ignore.  Do this before
           fragment check. */
	if ((*pskb)->nfct)
		return NF_ACCEPT;

	/* Gather fragments. */
	if ((*pskb)->nh.iph->frag_off & htons(IP_MF|IP_OFFSET)) {
		*pskb = ip_ct_gather_frags(*pskb);
		if (!*pskb)
			return NF_STOLEN;
	}
	return NF_ACCEPT;
}

static unsigned int ip_refrag(unsigned int hooknum,
			      struct sk_buff **pskb,
			      const struct net_device *in,
			      const struct net_device *out,
			      int (*okfn)(struct sk_buff *))
{
	struct rtable *rt = (struct rtable *)(*pskb)->dst;

	/* We've seen it coming out the other side: confirm */
	if (ip_confirm(hooknum, pskb, in, out, okfn) != NF_ACCEPT)
		return NF_DROP;

	/* Local packets are never produced too large for their
	   interface.  We degfragment them at LOCAL_OUT, however,
	   so we have to refragment them here. */
	if ((*pskb)->len > dst_pmtu(&rt->u.dst) &&
	    !skb_shinfo(*pskb)->tso_size) {
		/* No hook can be after us, so this should be OK. */
		ip_fragment(*pskb, okfn);
		return NF_STOLEN;
	}
	return NF_ACCEPT;
}

static unsigned int ip_conntrack_local(unsigned int hooknum,
				       struct sk_buff **pskb,
				       const struct net_device *in,
				       const struct net_device *out,
				       int (*okfn)(struct sk_buff *))
{
	/* root is playing with raw sockets. */
	if ((*pskb)->len < sizeof(struct iphdr)
	    || (*pskb)->nh.iph->ihl * 4 < sizeof(struct iphdr)) {
		if (net_ratelimit())
			printk("ipt_hook: happy cracking.\n");
		return NF_ACCEPT;
	}
	return ip_conntrack_in(hooknum, pskb, in, out, okfn);
}

/* Connection tracking may drop packets, but never alters them, so
   make it the first hook. */
static struct nf_hook_ops ip_conntrack_defrag_ops = {
	.hook		= ip_conntrack_defrag,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum	= NF_IP_PRE_ROUTING,
	.priority	= NF_IP_PRI_CONNTRACK_DEFRAG,
};

static struct nf_hook_ops ip_conntrack_in_ops = {
	.hook		= ip_conntrack_in,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum	= NF_IP_PRE_ROUTING,
	.priority	= NF_IP_PRI_CONNTRACK,
};

static struct nf_hook_ops ip_conntrack_defrag_local_out_ops = {
	.hook		= ip_conntrack_defrag,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum	= NF_IP_LOCAL_OUT,
	.priority	= NF_IP_PRI_CONNTRACK_DEFRAG,
};

static struct nf_hook_ops ip_conntrack_local_out_ops = {
	.hook		= ip_conntrack_local,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum	= NF_IP_LOCAL_OUT,
	.priority	= NF_IP_PRI_CONNTRACK,
};

/* Refragmenter; last chance. */
static struct nf_hook_ops ip_conntrack_out_ops = {
	.hook		= ip_refrag,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum	= NF_IP_POST_ROUTING,
	.priority	= NF_IP_PRI_LAST,
};

static struct nf_hook_ops ip_conntrack_local_in_ops = {
	.hook		= ip_confirm,
	.owner		= THIS_MODULE,
	.pf		= PF_INET,
	.hooknum	= NF_IP_LOCAL_IN,
	.priority	= NF_IP_PRI_LAST-1,
};

/* Sysctl support */

#ifdef CONFIG_SYSCTL

/* From ip_conntrack_core.c */
extern int ip_conntrack_max;
extern unsigned int ip_conntrack_htable_size;

/* From ip_conntrack_proto_tcp.c */
extern unsigned long ip_ct_tcp_timeout_syn_sent;
extern unsigned long ip_ct_tcp_timeout_syn_recv;
extern unsigned long ip_ct_tcp_timeout_established;
extern unsigned long ip_ct_tcp_timeout_fin_wait;
extern unsigned long ip_ct_tcp_timeout_close_wait;
extern unsigned long ip_ct_tcp_timeout_last_ack;
extern unsigned long ip_ct_tcp_timeout_time_wait;
extern unsigned long ip_ct_tcp_timeout_close;
extern unsigned long ip_ct_tcp_timeout_max_retrans;
extern int ip_ct_tcp_loose;
extern int ip_ct_tcp_be_liberal;
extern int ip_ct_tcp_max_retrans;

/* From ip_conntrack_proto_udp.c */
extern unsigned long ip_ct_udp_timeout;
extern unsigned long ip_ct_udp_timeout_stream;

/* From ip_conntrack_proto_icmp.c */
extern unsigned long ip_ct_icmp_timeout;

/* From ip_conntrack_proto_icmp.c */
extern unsigned long ip_ct_generic_timeout;

/* Log invalid packets of a given protocol */
static int log_invalid_proto_min = 0;
static int log_invalid_proto_max = 255;

static struct ctl_table_header *ip_ct_sysctl_header;

static ctl_table ip_ct_sysctl_table[] = {
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_MAX,
		.procname	= "ip_conntrack_max",
		.data		= &ip_conntrack_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_BUCKETS,
		.procname	= "ip_conntrack_buckets",
		.data		= &ip_conntrack_htable_size,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0444,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_SYN_SENT,
		.procname	= "ip_conntrack_tcp_timeout_syn_sent",
		.data		= &ip_ct_tcp_timeout_syn_sent,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_SYN_RECV,
		.procname	= "ip_conntrack_tcp_timeout_syn_recv",
		.data		= &ip_ct_tcp_timeout_syn_recv,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_ESTABLISHED,
		.procname	= "ip_conntrack_tcp_timeout_established",
		.data		= &ip_ct_tcp_timeout_established,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_FIN_WAIT,
		.procname	= "ip_conntrack_tcp_timeout_fin_wait",
		.data		= &ip_ct_tcp_timeout_fin_wait,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_CLOSE_WAIT,
		.procname	= "ip_conntrack_tcp_timeout_close_wait",
		.data		= &ip_ct_tcp_timeout_close_wait,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_LAST_ACK,
		.procname	= "ip_conntrack_tcp_timeout_last_ack",
		.data		= &ip_ct_tcp_timeout_last_ack,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_TIME_WAIT,
		.procname	= "ip_conntrack_tcp_timeout_time_wait",
		.data		= &ip_ct_tcp_timeout_time_wait,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_CLOSE,
		.procname	= "ip_conntrack_tcp_timeout_close",
		.data		= &ip_ct_tcp_timeout_close,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_UDP_TIMEOUT,
		.procname	= "ip_conntrack_udp_timeout",
		.data		= &ip_ct_udp_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_UDP_TIMEOUT_STREAM,
		.procname	= "ip_conntrack_udp_timeout_stream",
		.data		= &ip_ct_udp_timeout_stream,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_ICMP_TIMEOUT,
		.procname	= "ip_conntrack_icmp_timeout",
		.data		= &ip_ct_icmp_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_GENERIC_TIMEOUT,
		.procname	= "ip_conntrack_generic_timeout",
		.data		= &ip_ct_generic_timeout,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_LOG_INVALID,
		.procname	= "ip_conntrack_log_invalid",
		.data		= &ip_ct_log_invalid,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_minmax,
		.strategy	= &sysctl_intvec,
		.extra1		= &log_invalid_proto_min,
		.extra2		= &log_invalid_proto_max,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_TIMEOUT_MAX_RETRANS,
		.procname	= "ip_conntrack_tcp_timeout_max_retrans",
		.data		= &ip_ct_tcp_timeout_max_retrans,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec_jiffies,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_LOOSE,
		.procname	= "ip_conntrack_tcp_loose",
		.data		= &ip_ct_tcp_loose,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_BE_LIBERAL,
		.procname	= "ip_conntrack_tcp_be_liberal",
		.data		= &ip_ct_tcp_be_liberal,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{
		.ctl_name	= NET_IPV4_NF_CONNTRACK_TCP_MAX_RETRANS,
		.procname	= "ip_conntrack_tcp_max_retrans",
		.data		= &ip_ct_tcp_max_retrans,
		.maxlen		= sizeof(unsigned int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec,
	},
	{ .ctl_name = 0 }
};

#define NET_IP_CONNTRACK_MAX 2089

static ctl_table ip_ct_netfilter_table[] = {
	{
		.ctl_name	= NET_IPV4_NETFILTER,
		.procname	= "netfilter",
		.mode		= 0555,
		.child		= ip_ct_sysctl_table,
	},
	{
		.ctl_name	= NET_IP_CONNTRACK_MAX,
		.procname	= "ip_conntrack_max",
		.data		= &ip_conntrack_max,
		.maxlen		= sizeof(int),
		.mode		= 0644,
		.proc_handler	= &proc_dointvec
	},
	{ .ctl_name = 0 }
};

static ctl_table ip_ct_ipv4_table[] = {
	{
		.ctl_name	= NET_IPV4,
		.procname	= "ipv4",
		.mode		= 0555,
		.child		= ip_ct_netfilter_table,
	},
	{ .ctl_name = 0 }
};

static ctl_table ip_ct_net_table[] = {
	{
		.ctl_name	= CTL_NET,
		.procname	= "net",
		.mode		= 0555, 
		.child		= ip_ct_ipv4_table,
	},
	{ .ctl_name = 0 }
};

EXPORT_SYMBOL(ip_ct_log_invalid);
#endif /* CONFIG_SYSCTL */

static int init_or_cleanup(int init)
{
#ifdef CONFIG_PROC_FS
	struct proc_dir_entry *proc, *proc_exp, *proc_stat;
#endif
	int ret = 0;

	if (!init) goto cleanup;

	ret = ip_conntrack_init();
	if (ret < 0)
		goto cleanup_nothing;

#ifdef CONFIG_PROC_FS
	proc = proc_net_fops_create("ip_conntrack", 0440, &ct_file_ops);
	if (!proc) goto cleanup_init;

	proc_exp = proc_net_fops_create("ip_conntrack_expect", 0440,
					&exp_file_ops);
	if (!proc_exp) goto cleanup_proc;

	proc_stat = proc_net_fops_create("ip_conntrack_stat", S_IRUGO,
					 &ct_cpu_seq_fops);
	if (!proc_stat)
		goto cleanup_proc_exp;
	proc_stat->owner = THIS_MODULE;
#endif

	ret = nf_register_hook(&ip_conntrack_defrag_ops);
	if (ret < 0) {
		printk("ip_conntrack: can't register pre-routing defrag hook.\n");
		goto cleanup_proc_stat;
	}
	ret = nf_register_hook(&ip_conntrack_defrag_local_out_ops);
	if (ret < 0) {
		printk("ip_conntrack: can't register local_out defrag hook.\n");
		goto cleanup_defragops;
	}
	ret = nf_register_hook(&ip_conntrack_in_ops);
	if (ret < 0) {
		printk("ip_conntrack: can't register pre-routing hook.\n");
		goto cleanup_defraglocalops;
	}
	ret = nf_register_hook(&ip_conntrack_local_out_ops);
	if (ret < 0) {
		printk("ip_conntrack: can't register local out hook.\n");
		goto cleanup_inops;
	}
	ret = nf_register_hook(&ip_conntrack_out_ops);
	if (ret < 0) {
		printk("ip_conntrack: can't register post-routing hook.\n");
		goto cleanup_inandlocalops;
	}
	ret = nf_register_hook(&ip_conntrack_local_in_ops);
	if (ret < 0) {
		printk("ip_conntrack: can't register local in hook.\n");
		goto cleanup_inoutandlocalops;
	}
#ifdef CONFIG_SYSCTL
	ip_ct_sysctl_header = register_sysctl_table(ip_ct_net_table, 0);
	if (ip_ct_sysctl_header == NULL) {
		printk("ip_conntrack: can't register to sysctl.\n");
		goto cleanup;
	}
#endif

	return ret;

 cleanup:
#ifdef CONFIG_SYSCTL
 	unregister_sysctl_table(ip_ct_sysctl_header);
#endif
	nf_unregister_hook(&ip_conntrack_local_in_ops);
 cleanup_inoutandlocalops:
	nf_unregister_hook(&ip_conntrack_out_ops);
 cleanup_inandlocalops:
	nf_unregister_hook(&ip_conntrack_local_out_ops);
 cleanup_inops:
	nf_unregister_hook(&ip_conntrack_in_ops);
 cleanup_defraglocalops:
	nf_unregister_hook(&ip_conntrack_defrag_local_out_ops);
 cleanup_defragops:
	/* Frag queues may hold fragments with skb->dst == NULL */
	ip_ct_no_defrag = 1;
	synchronize_net();
	local_bh_disable();
	ipfrag_flush();
	local_bh_enable();
	nf_unregister_hook(&ip_conntrack_defrag_ops);
 cleanup_proc_stat:
#ifdef CONFIG_PROC_FS
	proc_net_remove("ip_conntrack_stat");
cleanup_proc_exp:
	proc_net_remove("ip_conntrack_exp");
 cleanup_proc:
	proc_net_remove("ip_conntrack");
 cleanup_init:
#endif /* CONFIG_PROC_FS */
	ip_conntrack_cleanup();
 cleanup_nothing:
	return ret;
}

/* FIXME: Allow NULL functions and sub in pointers to generic for
   them. --RR */
int ip_conntrack_protocol_register(struct ip_conntrack_protocol *proto)
{
	int ret = 0;
	struct list_head *i;

	WRITE_LOCK(&ip_conntrack_lock);
	list_for_each(i, &protocol_list) {
		if (((struct ip_conntrack_protocol *)i)->proto
		    == proto->proto) {
			ret = -EBUSY;
			goto out;
		}
	}

	list_prepend(&protocol_list, proto);

 out:
	WRITE_UNLOCK(&ip_conntrack_lock);
	return ret;
}

void ip_conntrack_protocol_unregister(struct ip_conntrack_protocol *proto)
{
	WRITE_LOCK(&ip_conntrack_lock);

	/* ip_ct_find_proto() returns proto_generic in case there is no protocol 
	 * helper. So this should be enough - HW */
	LIST_DELETE(&protocol_list, proto);
	WRITE_UNLOCK(&ip_conntrack_lock);
	
	/* Somebody could be still looking at the proto in bh. */
	synchronize_net();

	/* Remove all contrack entries for this protocol */
	ip_ct_selective_cleanup(kill_proto, &proto->proto);
}

static int __init init(void)
{
	return init_or_cleanup(1);
}

static void __exit fini(void)
{
	init_or_cleanup(0);
}

module_init(init);
module_exit(fini);

/* Some modules need us, but don't depend directly on any symbol.
   They should call this. */
void need_ip_conntrack(void)
{
}

EXPORT_SYMBOL(ip_conntrack_protocol_register);
EXPORT_SYMBOL(ip_conntrack_protocol_unregister);
EXPORT_SYMBOL(invert_tuplepr);
EXPORT_SYMBOL(ip_conntrack_alter_reply);
EXPORT_SYMBOL(ip_conntrack_destroyed);
EXPORT_SYMBOL(ip_conntrack_get);
EXPORT_SYMBOL(need_ip_conntrack);
EXPORT_SYMBOL(ip_conntrack_helper_register);
EXPORT_SYMBOL(ip_conntrack_helper_unregister);
EXPORT_SYMBOL(ip_ct_selective_cleanup);
EXPORT_SYMBOL(ip_ct_refresh_acct);
EXPORT_SYMBOL(ip_ct_find_proto);
EXPORT_SYMBOL(__ip_ct_find_proto);
EXPORT_SYMBOL(ip_ct_find_helper);
EXPORT_SYMBOL(ip_conntrack_expect_alloc);
EXPORT_SYMBOL(ip_conntrack_expect_related);
EXPORT_SYMBOL(ip_conntrack_change_expect);
EXPORT_SYMBOL(ip_conntrack_unexpect_related);
EXPORT_SYMBOL_GPL(ip_conntrack_expect_find_get);
EXPORT_SYMBOL_GPL(ip_conntrack_expect_put);
EXPORT_SYMBOL(ip_conntrack_tuple_taken);
EXPORT_SYMBOL(ip_ct_gather_frags);
EXPORT_SYMBOL(ip_conntrack_htable_size);
EXPORT_SYMBOL(ip_conntrack_expect_list);
EXPORT_SYMBOL(ip_conntrack_lock);
EXPORT_SYMBOL(ip_conntrack_hash);
EXPORT_SYMBOL(ip_conntrack_untracked);
EXPORT_SYMBOL_GPL(ip_conntrack_find_get);
EXPORT_SYMBOL_GPL(ip_conntrack_put);
