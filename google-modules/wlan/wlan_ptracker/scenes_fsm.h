// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#ifndef __WLAN_SCENES_FSM_H
#define __WLAN_SCENES_FSM_H

#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/completion.h>

struct wlan_ptracker_core;

enum {
	WLAN_SCENE_IDLE,
	WLAN_SCENE_WEB,
	WLAN_SCENE_YOUTUBE,
	WLAN_SCENE_LOW_LATENCY,
	WLAN_SCENE_TPUT,
	WLAN_SCENE_MAX,
};

/* follow design spec to define the conditions */
enum {
	FSM_STATE_C0,
	FSM_STATE_C1,
	FSM_STATE_C2,
	FSM_STATE_C3,
	FSM_STATE_C4,
	FSM_STATE_MAX
};

enum {
	SCENE_TEST_SET_PARAM,
	SCENE_TEST_MAX,
};

struct wlan_state_condition {
	u32 scene;
	u32 ac_mask;
	/* Kbits */
	u32 min_tp_threshold;
	u32 max_tp_threshold;
};

#define WMM_AC_ALL_MASK 0xf

struct wlan_scene_event {
	struct mutex lock;
	u32 state;
	u32 src;
	u32 dst;
	u32 reason;
	u64 rate;
};

struct wlan_ptracker_fsm {
	int reset_cnt;
	bool thread_run;
	struct completion event;
	struct wlan_scene_event msg;
	struct task_struct *fsm_thread;
	const struct wlan_state_condition *conditions;
	/* debug usage */
	struct dentry *dir;
	u32 state;
	u32 min_tput;
	u32 max_tput;
	u32 ac_mask;
};

extern int scenes_fsm_init(struct wlan_ptracker_fsm *fsm);
extern void scenes_fsm_exit(struct wlan_ptracker_fsm *fsm);
#endif /* __WLAN_SCENES_FSM_H */
