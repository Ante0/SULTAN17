// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __TP_TRACKER_DYNAMIC_TWT_SETUP_H
#define __TP_TRACKER_DYNAMIC_TWT_SETUP_H

#include "debugfs.h"

struct wlan_ptracker_client;
struct wlan_ptracker_core;

struct dytwt_setup_param {
	u8 config_id;
	u8 nego_type;
	u8 trigger_type;
	u32 wake_duration;
	u32 wake_interval;
};

struct dytwt_cap {
	u16 device_cap;
	u16 peer_cap;
	u32 rssi;
	int link_speed;
};

struct dytwt_pwr_state {
	u64 awake;
	u64 asleep;
	u64 count;
};

struct dytwt_status {
	u32 config_id;
	u32 flow_id;
	u32 flow_flags;
	u32 setup_cmd;
	u32 channel;
	u32 nego_type;
	u32 wake_dur;
	u32 wake_int;
};

struct dytwt_stats {
	u32 config_id;
	u32 sp_seq;		/* sequence number of the service period */
	u32 tx_ucast_pkts;	/* Number of unicast Tx packets in TWT SPs */
	u32 tx_pkts_min;	/* Minimum number of Tx packets in a TWT SP */
	u32 tx_pkts_max;	/* Maximum number of Tx packets in a TWT SP */
	u32 tx_pkts_avg;	/* Average number of Tx packets in each TWT SP */
	u32 tx_failures;	/* Tx packets failure count */
	u32 rx_ucast_pkts;	/* Number of unicast Rx packets in TWT SPs */
	u32 rx_pkts_min;	/* Minimum number of Rx packets in a TWT SP */
	u32 rx_pkts_max;	/* Maximum number of Rx packets in a TWT SP */
	u32 rx_pkts_avg;	/* Average number of Rx packets in each TWT SP */
	u32 rx_pkts_retried;	/* retried Rx packets count */
	u32 tx_pkt_sz_avg;	/* Average Tx packet size in TWT SPs */
	u32 rx_pkt_sz_avg;	/* Average Rx Packet size in TWT SPs */
	u32 eosp_dur_avg;	/* Average Wake duration in SPs ended due to EOSP */
	u32 eosp_count;		/* Count of TWT SPs ended due to EOSP */
};

struct dytwt_client_ops {
	int (*setup)(void *priv, struct dytwt_setup_param *param);
	int (*teardown)(void *priv, struct dytwt_setup_param *param);
	int (*get_cap)(void *priv, struct dytwt_cap *cap);
	int (*get_pwrstates)(void *priv, struct dytwt_pwr_state *state);
	int (*get_stats)(void *priv, struct dytwt_stats *stats);
	int (*get_status)(void *priv, struct dytwt_status *status);
};

enum {
	TWT_ACTION_SETUP,
	TWT_ACTION_TEARDOWN,
	TWT_ACTION_MAX,
};

enum {
	TWT_TEST_FORCE_STATE = 1,
	TWT_TEST_CAP,
	TWT_TEST_PWRSTATS,
	TWT_TEST_ONOFF,
	TWT_TEST_SET_PARAM,
	TWT_TEST_DUMP_STATS,
	TWT_TEST_DUMP_STATUS,
	TWT_TEST_MAX,
};

struct dytwt_scene_action {
	u32 action;
	struct dytwt_setup_param param;
};

struct dytwt_entry {
	/* base should put as first membor */
	struct history_entry base;
	bool apply;
	u32 rate;
	u32 reason;
	struct dytwt_pwr_state pwr;
} __align(void *);

struct dytwt_statistic {
	u64 awake;
	u64 asleep;
};

#define DYTWT_COUNTER_MAX 6
#define DYTWT_COUNTER_TOTAL 5
struct dytwt_counters {
	u64 total_awake;
	u64 total_sleep;
	u64 total_sleep_cnt;
	u64 prev_awake;
	u64 prev_asleep;
	u64 prev_asleep_cnt;
	struct dytwt_statistic scene[DYTWT_COUNTER_MAX];
};

struct dytwt_manager {
	u32 prev;
	u32 feature_flag;
	u32 state;
	struct history_manager *hm;
	u32 rssi_threshold;
	u32 link_threshold;
	bool twt_cap;
	struct delayed_work wq;
	struct delayed_work setup_wq;
	struct wlan_ptracker_core *core;
	struct dytwt_counters counters;
	struct kobject kobj;
	struct dentry *dir;
};

struct dytwt_kobj_attr {
	struct attribute attr;
	ssize_t (*show)(struct dytwt_manager *, char *);
	ssize_t (*store)(struct dytwt_manager *, const char *, size_t count);
};

extern int dytwt_init(struct wlan_ptracker_core *core);
extern void dytwt_exit(struct wlan_ptracker_core *core);
#endif /* __TP_TRACKER_DYNAMIC_TWT_SETUP_H */
