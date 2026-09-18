// SPDX-License-Identifier: GPL-2.0
/*
 * Wakelock for the runtime to explicitly claim it's going to use the EdgeTPU
 * device.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/string.h>
#include <linux/time64.h>
#include <linux/timekeeping.h>

#include "edgetpu.h"
#include "edgetpu-device-group.h"
#include "edgetpu-internal.h"
#include "edgetpu-wakelock.h"

/*
 * Clients holding wakelocks with total active time longer than this number of seconds are
 * tracked/reported and have their associated wakeup sources preserved for Android power accounting.
 */
#define WAKELOCK_TOTAL_PRESERVE_THRESHOLD_SEC	(60 * 5)

static const char *const event_name[] = {
#define X(name, _) #name
	EDGETPU_WAKELOCK_EVENTS
#undef X
};

/*
 * Loops through the events and warns if any event has a non-zero counter.
 * Returns true if at least one non-zero counter is found.
 *
 * Caller holds the @client wakelock lock.
 */
static bool wakelock_warn_non_zero_event(struct edgetpu_client *client)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;
	int i;
	bool ret = false;

	for (i = 0; i < EDGETPU_WAKELOCK_EVENT_END; i++) {
		if (wakelock->event_count[i]) {
			ret = true;
			etdev_warn(client->etdev,
				   "%s has non-zero counter=%d", event_name[i],
				   wakelock->event_count[i]);
		}
	}
	return ret;
}

static void edgetpu_wakelock_ws_acquire(struct edgetpu_client *client)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;

	if (!wakelock->etws) {
		char ws_name[80];
		uint tgid = client->tgid;

		wakelock->etws = kmalloc(sizeof(*wakelock->etws), GFP_KERNEL);
		if (!wakelock->etws)
			return;

		snprintf(ws_name, sizeof(ws_name), "tpu:%s-%u", client->name, tgid);
		wakelock->etws->ws = wakeup_source_register(client->etdev->dev, ws_name);
		if (!wakelock->etws->ws) {
			kfree(wakelock->etws);
			wakelock->etws = NULL;
			return;
		}

		mutex_lock(&client->etdev->wakeup_sources_lock);
		list_add(&wakelock->etws->ws_list, &client->etdev->wakeup_sources);
		mutex_unlock(&client->etdev->wakeup_sources_lock);
	}

	__pm_stay_awake(wakelock->etws->ws);
}

static void edgetpu_wakelock_ws_release(struct edgetpu_client *client)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;

	if (wakelock->etws)
		__pm_relax(wakelock->etws->ws);
}

void edgetpu_wakeup_source_destroy_all(struct edgetpu_dev *etdev)
{
	struct edgetpu_wakeup_source *etws, *next;

	mutex_lock(&etdev->wakeup_sources_lock);
	list_for_each_entry_safe(etws, next, &etdev->wakeup_sources, ws_list) {
		wakeup_source_unregister(etws->ws);
		list_del(&etws->ws_list);
		kfree(etws);
	}
	mutex_unlock(&etdev->wakeup_sources_lock);
}

static void edgetpu_wakeup_source_destroy(struct edgetpu_dev *etdev,
					  struct edgetpu_wakeup_source *etws)
{
	wakeup_source_unregister(etws->ws);
	mutex_lock(&etdev->wakeup_sources_lock);
	list_del(&etws->ws_list);
	mutex_unlock(&etdev->wakeup_sources_lock);
	kfree(etws);
}

static void edgetpu_wakelock_ws_client_exit(struct edgetpu_client *client)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;

	if (!wakelock->etws)
		return;

	if (ktime_to_ms(wakelock->etws->ws->prevent_sleep_time) >
	    WAKELOCK_TOTAL_PRESERVE_THRESHOLD_SEC * MSEC_PER_SEC) {
		etdev_info(client->etdev,
			   "long wakelock client %s total %lld prevent sleep %lld\n",
			   client->name, ktime_to_ms(wakelock->etws->ws->total_time),
			   ktime_to_ms(wakelock->etws->ws->prevent_sleep_time));
		/* Ensure the wakeup source is deactivated. */
		edgetpu_wakelock_ws_release(client);
	} else {
		edgetpu_wakeup_source_destroy(client->etdev, wakelock->etws);
	}

	wakelock->etws = NULL;
}

void edgetpu_wakelock_init(struct edgetpu_client *client)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;

	memset(wakelock, 0, sizeof(*wakelock));
	mutex_init(&wakelock->lock);
	/* Initialize client wakelock state to "released" */
	wakelock->req_count = 0;
}

void edgetpu_wakelock_destroy(struct edgetpu_client *client)
{
	edgetpu_wakelock_ws_client_exit(client);
}

bool edgetpu_wakelock_inc_event_locked(struct edgetpu_client *client,
				       enum edgetpu_wakelock_event evt)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;
	bool ret = true;

	if (!wakelock->req_count) {
		ret = false;
		etdev_warn(client->etdev,
			   "invalid increase event %d when wakelock is released",
			   evt);
	} else {
		++wakelock->event_count[evt];
		/* integer overflow.. */
		if (unlikely(wakelock->event_count[evt] == 0)) {
			--wakelock->event_count[evt];
			ret = false;
			etdev_warn_once(client->etdev,
					"int overflow on increasing event %d",
					evt);
		}
	}
	return ret;
}

bool edgetpu_wakelock_inc_event(struct edgetpu_client *client, enum edgetpu_wakelock_event evt)
{
	bool ret;

	edgetpu_wakelock_lock(client);
	ret = edgetpu_wakelock_inc_event_locked(client, evt);
	edgetpu_wakelock_unlock(client);
	return ret;
}

bool edgetpu_wakelock_dec_event_locked(struct edgetpu_client *client,
				       enum edgetpu_wakelock_event evt)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;
	bool ret = true;

	if (!wakelock->event_count[evt]) {
		ret = false;
		etdev_warn(client->etdev, "event %d unbalanced decreasing",
			   evt);
	} else {
		--wakelock->event_count[evt];
	}
	return ret;
}

bool edgetpu_wakelock_dec_event(struct edgetpu_client *client,
				enum edgetpu_wakelock_event evt)
{
	bool ret;

	edgetpu_wakelock_lock(client);
	ret = edgetpu_wakelock_dec_event_locked(client, evt);
	edgetpu_wakelock_unlock(client);
	return ret;
}

uint edgetpu_wakelock_count_locked(struct edgetpu_client *client)
{
	return client->wakelock.req_count;
}

uint edgetpu_wakelock_lock(struct edgetpu_client *client)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;

	mutex_lock(&wakelock->lock);
	return wakelock->req_count;
}

void edgetpu_wakelock_unlock(struct edgetpu_client *client)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;

	mutex_unlock(&wakelock->lock);
}

int edgetpu_wakelock_acquire(struct edgetpu_client *client, u32 flags)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;
	int ret;

	ret = wakelock->req_count++;
	/* integer overflow */
	if (unlikely(ret < 0)) {
		wakelock->req_count--;
		return -EOVERFLOW;
	}
	if (flags & EDGETPU_ACQUIRE_WAKELOCK_FLAG_SUSPEND)
		wakelock->suspendable = true;
	if (!ret) {
		ktime_get_ts64(&wakelock->current_acquire_timestamp);

		if (!wakelock->suspendable)
			edgetpu_wakelock_ws_acquire(client);
	}
	return ret;
}

int edgetpu_wakelock_release(struct edgetpu_client *client)
{
	struct edgetpu_wakelock *wakelock = &client->wakelock;
	struct timespec64 curr;

	if (!wakelock->req_count) {
		etdev_warn(client->etdev, "invalid wakelock release");
		return -EINVAL;
	}
	/* only need to check events when this is the last reference */
	if (wakelock->req_count == 1) {
		if (wakelock_warn_non_zero_event(client)) {
			etdev_warn(client->etdev,
				   "detected non-zero events, refusing wakelock release");
			return -EAGAIN;
		}

		ktime_get_ts64(&curr);
		curr = timespec64_sub(curr, wakelock->current_acquire_timestamp);
		wakelock->total_acquired_time = timespec64_add(wakelock->total_acquired_time, curr);
		if (!wakelock->suspendable)
			edgetpu_wakelock_ws_release(client);
		/* Clear "suspendable" flag on last release. */
		wakelock->suspendable = false;
	}

	return --wakelock->req_count;
}
