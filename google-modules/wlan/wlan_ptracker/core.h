// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef _WLAN_PTRACKER_CORE_H
#define _WLAN_PTRACKER_CORE_H

#include "debugfs.h"
#include "debug.h"
#include "tp_monitor.h"
#include "notifier.h"
#include "scenes_fsm.h"
#include "wlan_ptracker_client.h"
#include "dynamic_twt_manager.h"

#define DSCP_MASK  0xfc
#define DSCP_MAX (DSCP_MASK + 1)
#define DSCP_SHIFT 2
#define DSCP_MAP_MAX 10

struct wlan_ptracker_core {
	struct device device;
	struct tp_monitor_stats tp;
	struct wlan_ptracker_notifier notifier;
	struct wlan_ptracker_debugfs debugfs;
	struct wlan_ptracker_fsm fsm;
	struct net_device *dev;
	struct dytwt_manager *dytwt;
	struct wlan_ptracker_client *client;
	u8 dscp_to_ac[DSCP_MAX];
};
#endif /* _WLAN_PTRACKER_CORE_H */
