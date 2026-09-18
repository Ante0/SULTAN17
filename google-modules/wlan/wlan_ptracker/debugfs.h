/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */

#ifndef _WLAN_PTRACKER_DEBUGFS_H
#define _WLAN_PTRACKER_DEBUGFS_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/time64.h>
#include <linux/sysfs.h>
#include <linux/kobject.h>

struct wlan_ptracker_core;

struct wlan_ptracker_debugfs {
	struct dentry *root;
	struct kobject kobj;
	u32 dscp;
	u32 ac;
	u32 action;
	u32 log_level;
};

struct ptracker_kobj_attr {
	struct attribute attr;
	ssize_t (*show)(struct wlan_ptracker_debugfs *, char *);
	ssize_t (*store)(struct wlan_ptracker_debugfs *, const char *, size_t count);
};

enum {
	FEATURE_FLAG_TWT,
	FEATURE_FLAG_MAX
};

enum {
	ACTION_DSCP_UPDATE,
	ACTION_MAX,
};

struct scene_statistic {
	u64 awake;
	u64 asleep;
};

struct history_entry {
	u32 state;
	bool valid;
	struct timespec64 ts;
};

#define MODULE_NAME_MAX 64
struct history_manager {
	char name[MODULE_NAME_MAX];
	int cur;
	int round;
	int entry_count;
	int entry_size;
	struct mutex mutex;
	int (*priv_read)(struct wlan_ptracker_core *core, void *cur, void *next, char *buf, int len);
	u8 entries[0];
};

extern int wlan_ptracker_debugfs_init(struct wlan_ptracker_debugfs *debugfs);
extern void wlan_ptracker_debugfs_exit(struct wlan_ptracker_debugfs *debugfs);
extern struct history_manager *wlan_ptracker_history_create(int entry_count, int entry_size);
extern void wlan_ptracker_history_destroy(struct history_manager *hm);
extern void *wlan_ptracker_history_store(struct history_manager *hm, u32 state);
extern size_t wlan_ptracker_history_read(struct wlan_ptracker_core *core,
	struct history_manager *hm, char *buf, int len);

#endif  /* _WLAN_PTRACKER_DEBUGFS_H */
