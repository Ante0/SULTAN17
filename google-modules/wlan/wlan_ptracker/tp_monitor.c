// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/debugfs.h>
#include <net/dsfield.h>
#include "core.h"

#define tp_to_core(_tp) container_of(_tp, struct wlan_ptracker_core, tp)

static void tp_rate_pps_update(struct tp_monitor_counts *counts)
{
	unsigned long cur_cnt, cur_bytes;
	struct tp_monitor_counts *count;
	int i;

	for (i = 0 ; i < TPM_SIZE_MAX; i++) {
		count = &counts[i];
		cur_bytes = count->packet_bytes;
		cur_cnt = count->packet_cnt;
		count->rate = (cur_bytes - count->pre_packet_bytes) << 3;
		count->pps = cur_cnt - count->pre_packet_cnt;
		count->pre_packet_cnt = cur_cnt;
		count->pre_packet_bytes = cur_bytes;
#ifdef TP_DEBUG
		count->max_packet_bytes = max(count->max_packet_bytes, count->packet_bytes);
		count->max_packet_cnt = max(count->max_packet_cnt, count->packet_cnt);
		count->max_pps = max(count->max_pps, count->pps);
		count->max_rate = max(count->max_rate, count->rate);
#endif /* TP_DEBUG */
	}
}

/* TODO: fine-tune period */
#define TPM_TIMER_PERIOD 1000
static void tp_timer_callback(struct timer_list *t)
{
	struct tp_monitor_stats *stats = from_timer(stats, t, tp_timer);
	struct wlan_ptracker_core *core = tp_to_core(stats);

	/* update tx */
	tp_rate_pps_update(stats->tx);
	/* update rx */
	tp_rate_pps_update(stats->rx);
	mod_timer(t, jiffies + msecs_to_jiffies(TPM_TIMER_PERIOD));
	/* adjust scenes */
	wlan_ptracker_call_chain(&core->notifier, WLAN_PTRACKER_NOTIFY_TP, core);
}

static inline void tp_timer_start(struct tp_monitor_stats *stats)
{
	/* update rate per second */
	timer_setup(&stats->tp_timer, tp_timer_callback, 0);
	mod_timer(&stats->tp_timer, jiffies + msecs_to_jiffies(TPM_TIMER_PERIOD));
}

static inline void tp_timer_stop(struct tp_monitor_stats *stats)
{
	del_timer_sync(&stats->tp_timer);
}

static void tp_update_counter(struct wlan_ptracker_core *core,
	struct tp_monitor_counts *counts, u8 dscp, struct sk_buff *skb)
{
	u8 wmm_ac = core->dscp_to_ac[dscp];

	/* update total counters */
	counts[WMM_AC_MAX].packet_cnt++;
	counts[WMM_AC_MAX].packet_bytes += skb->len;
	/* update ac counters */
	counts[wmm_ac].packet_cnt++;
	counts[wmm_ac].packet_bytes += skb->len;
}

static u32 tp_monitor_nf_input(void *priv, struct sk_buff *skb,
	const struct nf_hook_state *state)
{
	struct wlan_ptracker_core *core = priv;
	struct net_device *dev = skb->dev;
	u8 dscp;

	if (dev != core->dev)
		goto out;

	dscp = ip_hdr(skb)->version == 4 ?
		ipv4_get_dsfield(ip_hdr(skb)) >> DSCP_SHIFT :
		ipv6_get_dsfield(ipv6_hdr(skb)) >> DSCP_SHIFT;

	tp_info(&core->tp, "rx packets %s, dscp: %d, ip.ver: %d, len: %d, %d\n",
		dev->name, dscp, ip_hdr(skb)->version, skb->len, skb->data_len);
	tp_update_counter(core, core->tp.rx, dscp, skb);
out:
	return NF_ACCEPT;
}

static u32 tp_monitor_nf_output(void *priv, struct sk_buff *skb,
	const struct nf_hook_state *state)
{
	struct wlan_ptracker_core *core = priv;
	struct net_device *dev = skb->dev;
	u8 dscp;

	if (dev != core->dev)
		goto out;

	dscp = ip_hdr(skb)->version == 4 ?
		ipv4_get_dsfield(ip_hdr(skb)) >> DSCP_SHIFT :
		ipv6_get_dsfield(ipv6_hdr(skb)) >> DSCP_SHIFT;

	tp_info(&core->tp, "tx packets %s, dscp:%d, ip.ver: %d, len: %d\n",
		dev->name, dscp, ip_hdr(skb)->version, skb->data_len);
	tp_update_counter(core, core->tp.tx, dscp, skb);
out:
	return NF_ACCEPT;
}

static struct nf_hook_ops wlan_ptracker_nfops[] = {
	{
		.hook     = tp_monitor_nf_input,
		.pf       = NFPROTO_INET,
		.hooknum  = NF_INET_PRE_ROUTING,
		.priority = INT_MAX,
	},
	{
		.hook     = tp_monitor_nf_output,
		.pf       = NFPROTO_INET,
		.hooknum  = NF_INET_POST_ROUTING,
		.priority = INT_MAX,
	},
};
#define WLAN_PTRACKER_NF_LEN ARRAY_SIZE(wlan_ptracker_nfops)

static int tp_show(struct seq_file *s, void *unused)
{
	struct tp_monitor_counts *counter, *counters = s->private;
	int i;

	for (i = 0 ; i < TPM_SIZE_MAX; i++) {
		counter = &counters[i];
		if (i < WMM_AC_MAX)
			seq_printf(s, "AC %d ->\n", i);
		else
			seq_puts(s, "Total ->\n");
		seq_printf(s, "packet_cnt   : %llu (%llu)\n",
			counter->packet_cnt, counter->max_packet_cnt);
		seq_printf(s, "packet_bytes : %llu (%llu)\n",
			counter->packet_bytes, counter->max_packet_bytes);
		seq_printf(s, "rate (Kbits) : %llu (%llu)\n",
			counter->rate / 1000, counter->max_rate / 1000);
		seq_printf(s, "pps          : %llu (%llu)\n",
			counter->pps, counter->pps);
	}
	return 0;
}

static int counters_open(struct inode *inode, struct file *file)
{
	return single_open(file, tp_show, inode->i_private);
}

static const struct file_operations counter_ops = {
	.open = counters_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int tp_monitor_debugfs_init(struct wlan_ptracker_core *core)
{
	struct wlan_ptracker_debugfs *debugfs = &core->debugfs;
	struct tp_monitor_stats *stats = &core->tp;
	struct wlan_ptracker_client *client = core->client;

	stats->dir = debugfs_create_dir(client->ifname, debugfs->root);
	if (!stats->dir)
		return -ENODEV;

	debugfs_create_u32("log_level", 0600, stats->dir, &stats->debug);
	debugfs_create_file("tx", 0400, stats->dir, &stats->tx, &counter_ops);
	debugfs_create_file("rx", 0400, stats->dir, &stats->rx, &counter_ops);
	return 0;
}

int tp_monitor_init(struct tp_monitor_stats *stats)
{
	struct wlan_ptracker_core *core = tp_to_core(stats);
	struct net *net = dev_net(core->dev);
	int err = 0;
	int i;

	/* debugfs */
	tp_monitor_debugfs_init(core);
	/* assign net_device for ingress check and filter */
	for (i = 0 ; i < WLAN_PTRACKER_NF_LEN; i++) {
		wlan_ptracker_nfops[i].dev = core->dev;
		wlan_ptracker_nfops[i].priv = core;
	}

	/* register hook function to netfilter */
	err = nf_register_net_hooks(net, wlan_ptracker_nfops, WLAN_PTRACKER_NF_LEN);
	if (err)
		goto out;

	/* start a timer to update rate and pps */
	tp_timer_start(stats);
	return 0;
out:
	ptracker_err(core, "initial err (%d)\n", err);
	return err;
}

void tp_monitor_exit(struct tp_monitor_stats *stats)
{
	struct wlan_ptracker_core *core = tp_to_core(stats);
	struct net *net = dev_net(core->dev);

	if (stats->dir)
		debugfs_remove_recursive(stats->dir);
	tp_timer_stop(stats);
	nf_unregister_net_hooks(net, wlan_ptracker_nfops, WLAN_PTRACKER_NF_LEN);
}

