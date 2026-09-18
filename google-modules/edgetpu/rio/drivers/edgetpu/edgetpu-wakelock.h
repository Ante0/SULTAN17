/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Wakelock for the runtime to explicitly claim it's going to use the EdgeTPU
 * device.
 *
 * Copyright (C) 2021 Google, Inc.
 */
#ifndef __EDGETPU_WAKELOCK_H__
#define __EDGETPU_WAKELOCK_H__

#include <linux/device.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/time64.h>

struct edgetpu_dev;
struct edgetpu_client;

/*
 * Events that could block the wakelock from being released.
 * Use inc_event() and dec_event() to increase and decrease the counters when
 * the event happens.
 *
 * Defined with X macros to support fetching event names from values.
 */
#define EDGETPU_WAKELOCK_EVENTS                                                \
	X(EDGETPU_WAKELOCK_EVENT_EXT_MAILBOX, 0),                              \
	X(EDGETPU_WAKELOCK_EVENT_END, 1)

enum edgetpu_wakelock_event {
#define X(name, val) name = val
	EDGETPU_WAKELOCK_EVENTS
#undef X
};

/* Wrapper for a wakeup source in the etdev's list of wakeup sources. */
struct edgetpu_wakeup_source {
	struct wakeup_source *ws;
	struct list_head ws_list;
};

struct edgetpu_wakelock {
	/* Protects every field below */
	struct mutex lock;
	/*
	 * The request counter, increments on "acquire" and decrements on
	 * "release".
	 */
	uint req_count;
	/* This client's wakelock has been marked "suspendable", allowing system suspend. */
	bool suspendable;
	/*
	 * Events counter.
	 * release() would fail if one of the slots is not zero.
	 */
	uint event_count[EDGETPU_WAKELOCK_EVENT_END];
	/* Current first (req_count == 1) acquire timestamp (monotonic clock). */
	struct timespec64 current_acquire_timestamp;
	/* Total time acquired as of last release (not including current; monotonic clock). */
	struct timespec64 total_acquired_time;

	/* Wakeup source held when non-suspendable wakelock held, created on first acquire. */
	struct edgetpu_wakeup_source *etws;
};

/* Initialize the wakelock object for @client. */
void edgetpu_wakelock_init(struct edgetpu_client *client);

/*
 * Destroy the wakelock object for @client (the associated wakeup source may be preserved for power
 * analysis later).
 */
void edgetpu_wakelock_destroy(struct edgetpu_client *client);

/* Destroy all preserved wakeup sources at device remote time. */
void edgetpu_wakeup_source_destroy_all(struct edgetpu_dev *etdev);

/*
 * Increases the event counter of @evt by one for the wakelock of @client.
 *
 * Returns true if the counter is increased successfully.
 * Returns false when one of the following errors happens:
 *   - the wakelock is released
 *   - integer overflow on the counter
 */
bool edgetpu_wakelock_inc_event(struct edgetpu_client *client, enum edgetpu_wakelock_event evt);

/*
 * A version of the above where the caller holds the wakelock internal lock
 * by calling edgetpu_wakelock_lock.
 */
bool edgetpu_wakelock_inc_event_locked(struct edgetpu_client *client,
				       enum edgetpu_wakelock_event evt);
/*
 * Decreases the event counter of @evt by one for the wakelock of @client.
 *
 * Returns true if the counter is decreased successfully.
 * Returns false when one of the following errors happens:
 *   - the counter is zero
 */
bool edgetpu_wakelock_dec_event(struct edgetpu_client *client,
				enum edgetpu_wakelock_event evt);

/*
 * A version of the above where the caller holds the wakelock internal lock
 * by calling edgetpu_wakelock_lock.
 */
bool edgetpu_wakelock_dec_event_locked(struct edgetpu_client *client,
				       enum edgetpu_wakelock_event evt);

/*
 * Holds the internal lock of the wakelock for @client.
 *
 * Returns the non-negative request counter of the wakelock.
 *
 * Example:
 *   if (edgetpu_wakelock_lock(client)) {
 *      <..work that requires exclusive access to protected wakelock fields..>
 *   }
 *   edgetpu_wakelock_unlock(client);
 */
uint edgetpu_wakelock_lock(struct edgetpu_client *client);
void edgetpu_wakelock_unlock(struct edgetpu_client *client);

/*
 * Returns the request counter of the wakelock for @client.
 *
 * Caller calls edgetpu_wakelock_lock() before calling this function.
 */
uint edgetpu_wakelock_count_locked(struct edgetpu_client *client);

/*
 * Acquires the wakelock for the @client, increasing the req_count by one.
 * @flags: Bitmask of EDGETPU_ACQUIRE_WAKELOCK_FLAG_* flags, such as to allow suspend.
 *
 * This function must be surrounded by edgetpu_wakelock_lock() and edgetpu_wakelock_unlock().
 *
 * Returns the value of request counter *before* being increased.
 * Returns -EOVERFLOW if the request counter would overflow after increment.
 */
int edgetpu_wakelock_acquire(struct edgetpu_client *client, u32 flags);

/*
 * Requests to release the wakelock for @client, decreasing the req_count by one on success.
 *
 * This function must be surrounded by edgetpu_wakelock_lock() and edgetpu_wakelock_unlock().
 *
 * Returns the value of request counter *after* being decreased.
 * Returns -EINVAL if the request counter is already zero.
 * Returns -EAGAIN when there are events blocking wakelock from being released.
 */
int edgetpu_wakelock_release(struct edgetpu_client *client);

#endif /* __EDGETPU_WAKELOCK_H__ */
