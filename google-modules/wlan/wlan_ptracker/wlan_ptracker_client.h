// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __WLAN_PTRACKER_CLIENT_H
#define __WLAN_PTRACKER_CLIENT_H

#include "dynamic_twt_manager.h"

#define IFNAME_MAX 16

enum {
	WLAN_PTRACKER_NOTIFY_TP,
	WLAN_PTRACKER_NOTIFY_SCENE_CHANGE,
	WLAN_PTRACKER_NOTIFY_SCENE_CHANGE_PREPARE,
	WLAN_PTRACKER_NOTIFY_SUSPEND,
	WLAN_PTRACKER_NOTIFY_STA_CONNECT,
	WLAN_PTRACKER_NOTIFY_STA_DISCONNECT,
	WLAN_PTRACKER_NOTIFY_DYTWT_ENABLE,
	WLAN_PTRACKER_NOTIFY_DYTWT_DISABLE,
	WLAN_PTRACKER_NOTIFY_MAX,
};

/* backword compatible */
#define WLAN_PTRACKER_NOTIFY_SUSPEN WLAN_PTRACKER_NOTIFY_SUSPEND

struct wlan_ptracker_client {
	void *priv;
	void *core;
	char ifname[IFNAME_MAX];
	struct dytwt_client_ops *dytwt_ops;
	int (*cb)(void *priv, u32 event);
};

extern int wlan_ptracker_register_client(struct wlan_ptracker_client *client);
extern void wlan_ptracker_unregister_client(struct wlan_ptracker_client *client);

#endif /*__WLAN_PTRACKER_CLIENT_H*/
