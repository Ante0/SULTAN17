// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include "core.h"

#define notifier_to_core(notifier) container_of(notifier, struct wlan_ptracker_core, notifier)

#define nb_to_notifier(nb) container_of(nb, struct wlan_ptracker_notifier, nb)

static int up_event_handler(struct wlan_ptracker_core *core, struct net_device *dev)
{
	core->dev = dev;
	dev_hold(dev);
	core->client->priv = dev;
	return tp_monitor_init(&core->tp);
}

static void down_event_handler(struct wlan_ptracker_core *core)
{
	struct net_device *dev = core->dev;
	tp_monitor_exit(&core->tp);
	core->dev = NULL;
	core->client->priv = NULL;
	if (dev)
		dev_put(dev);
}

static int netdevice_notifier_handler(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct net_device *netdev = netdev_notifier_info_to_dev(ptr);
	struct wlan_ptracker_notifier *notifier = nb_to_notifier(nb);
	struct wlan_ptracker_core *core = notifier_to_core(notifier);

	if (!core->client)
		return NOTIFY_DONE;

	if (strcmp(netdev->name, core->client->ifname))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_UP:
		ptracker_info(core, "interface up (%s)\n", netdev->name);
		up_event_handler(core, netdev);
		break;
	case NETDEV_DOWN:
		ptracker_info(core, "interface down (%s)\n", netdev->name);
		down_event_handler(core);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

int wlan_ptracker_register_notifier(struct wlan_ptracker_notifier *notifier,
	struct notifier_block *nb)
{
	return  blocking_notifier_chain_register(&notifier->notifier_head, nb);
}

void wlan_ptracker_unregister_notifier(struct wlan_ptracker_notifier *notifier,
	struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&notifier->notifier_head, nb);
}

int wlan_ptracker_call_chain(struct wlan_ptracker_notifier *notifier,
	unsigned long event, void *priv)
{
	struct wlan_ptracker_core *core = priv;
	int ret;

	ret = blocking_notifier_call_chain(&notifier->notifier_head, event, priv);
	if (ret & NOTIFY_STOP_MASK)
		ptracker_err(core, "notifier chain fail with status %#x\n", ret);

	return notifier_to_errno(ret);
}

void wlan_ptracker_notifier_init(struct wlan_ptracker_notifier *notifier)
{
	notifier->prev_event = jiffies;
	/* register to device notifier */
	notifier->nb.priority = 0;
	notifier->nb.notifier_call = netdevice_notifier_handler;
	register_netdevice_notifier(&notifier->nb);
	/* init notifier chain to notify plugin modules */
	BLOCKING_INIT_NOTIFIER_HEAD(&notifier->notifier_head);
}

void wlan_ptracker_notifier_exit(struct wlan_ptracker_notifier *notifier)
{
	/* reset notifier */
	BLOCKING_INIT_NOTIFIER_HEAD(&notifier->notifier_head);
	/* unregister netdevice notifier*/
	unregister_netdevice_notifier(&notifier->nb);
	notifier->prev_event = 0;
}

