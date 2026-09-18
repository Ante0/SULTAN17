// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */

#ifndef __WLAN_TP_MONITOR_H
#define __WLAN_TP_MONITOR_H

#include <linux/timer.h>

enum {
	WMM_AC_BE,
	WMM_AC_BK,
	WMM_AC_VI,
	WMM_AC_VO,
	WMM_AC_MAX
};

#define TPM_SIZE_MAX (WMM_AC_MAX + 1)

struct tp_monitor_counts {
	u64 packet_cnt;
	u64 packet_bytes;
	u64 pre_packet_bytes;
	u64 pre_packet_cnt;
	u64 rate;
	u64 pps;
	u64 max_pps;
	u64 max_packet_cnt;
	u64 max_packet_bytes;
	u64 max_rate;
};

struct tp_monitor_stats {
	struct tp_monitor_counts tx[TPM_SIZE_MAX];
	struct tp_monitor_counts rx[TPM_SIZE_MAX];
	struct timer_list tp_timer;
	struct dentry *dir;
	u32 debug;
};

extern int tp_monitor_init(struct tp_monitor_stats *stats);
extern void tp_monitor_exit(struct tp_monitor_stats *stats);
#endif /* __WLAN_TP_MONITOR_H */
