// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __TP_TRACKER_NOTIFIER_H
#define __TP_TRACKER_NOTIFIER_H

#include <linux/notifier.h>
#include <linux/inetdevice.h>
#include <linux/netdevice.h>


struct wlan_ptracker_notifier {
	struct notifier_block nb;
	unsigned long prev_event;
	struct blocking_notifier_head notifier_head;
};

extern void wlan_ptracker_notifier_init(struct wlan_ptracker_notifier *nb);
extern void wlan_ptracker_notifier_exit(struct wlan_ptracker_notifier *nb);

extern int wlan_ptracker_register_notifier(struct wlan_ptracker_notifier *notifier,
	struct notifier_block *nb);
extern void wlan_ptracker_unregister_notifier(struct wlan_ptracker_notifier *notifier,
	struct notifier_block *nb);
extern int wlan_ptracker_call_chain(struct wlan_ptracker_notifier *notifier,
	unsigned long event, void *priv);

#endif /* __TP_TRACKER_NOTIFIER_H */
