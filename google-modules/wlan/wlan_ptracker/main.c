// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <net/net_namespace.h>
#include "core.h"

#define client_to_core(client) ((struct wlan_ptracker_core *)((client)->core))
/* Default mapping rule follow 802.11e */
static const int dscp_trans[WMM_AC_MAX][DSCP_MAP_MAX] = {
	{0, 24, 26, 28, 30, -1}, /* AC_BE */
	{8, 10, 12, 14, 16, 18, 20, 22, -1}, /* AC_BK */
	{32, 34, 36, 38, 40, 46, -1}, /* AC_VI */
	{48, 56, -1}, /* AC_VO */
};

static void dscp_to_ac_init(u8 *dscp_to_ac)
{
	int i, j;

	for (i = 0 ; i < WMM_AC_MAX; i++) {
		for (j = 0 ; j < DSCP_MAP_MAX; j++) {
			int dscp = dscp_trans[i][j];

			if (dscp == -1)
				break;
			dscp_to_ac[dscp] = i;
		}
	}
}

static struct wlan_ptracker_core *wlan_ptracker_core_init(struct wlan_ptracker_client *client)
{
	struct wlan_ptracker_core *core;
        int ret;

	core = kzalloc(sizeof(struct wlan_ptracker_core), GFP_KERNEL);
	if (!core)
		return NULL;

	core->client = client;
	device_initialize(&core->device);
	dev_set_name(&core->device, PTRACKER_PREFIX);
	ret = device_add(&core->device);
        if (ret)
	  goto err_add;

	dscp_to_ac_init(core->dscp_to_ac);
	wlan_ptracker_debugfs_init(&core->debugfs);
	wlan_ptracker_notifier_init(&core->notifier);
	scenes_fsm_init(&core->fsm);
	dytwt_init(core);
	return core;
err_add:
	kfree(core);
	return NULL;
}

static void wlan_ptracker_core_exit(struct wlan_ptracker_core *core)
{
	dytwt_exit(core);
	scenes_fsm_exit(&core->fsm);
	wlan_ptracker_notifier_exit(&core->notifier);
	wlan_ptracker_debugfs_exit(&core->debugfs);
	device_del(&core->device);
	kfree(core);
}

static int client_event_handler(void *priv, u32 event)
{
	struct wlan_ptracker_client *client = priv;
	struct wlan_ptracker_core *core = client_to_core(client);

	return wlan_ptracker_call_chain(&core->notifier, event, core);
}

int wlan_ptracker_register_client(struct wlan_ptracker_client *client)
{
	client->core = wlan_ptracker_core_init(client);
	if (!client->core)
		return -ENOMEM;
	client->cb = client_event_handler;
	return 0;
}
EXPORT_SYMBOL_GPL(wlan_ptracker_register_client);

void wlan_ptracker_unregister_client(struct wlan_ptracker_client *client)
{
	struct wlan_ptracker_core *core = client_to_core(client);

	if (!core)
		return;
	client->cb = NULL;
	client->core = NULL;
	wlan_ptracker_core_exit(core);
}
EXPORT_SYMBOL_GPL(wlan_ptracker_unregister_client);

static int __init wlan_ptracker_init(void)
{
	pr_debug("module init: %s\n", PTRACKER_PREFIX);
	return 0;
}

static void __exit wlan_ptracker_exit(void)
{
	pr_debug("module exit: %s\n", PTRACKER_PREFIX);
}

module_init(wlan_ptracker_init);
module_exit(wlan_ptracker_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Star Chang <starchang@google.com>");
MODULE_DESCRIPTION("WiFi Performance Tracker");
