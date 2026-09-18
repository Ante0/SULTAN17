// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/debugfs.h>
#include "core.h"

#define fsm_to_core(fsm) (container_of(fsm, struct wlan_ptracker_core, fsm))

static struct wlan_state_condition conditions[FSM_STATE_MAX] = {
	{
		.scene = WLAN_SCENE_IDLE,
		.ac_mask = WMM_AC_ALL_MASK,
		.min_tp_threshold = 0,
		.max_tp_threshold = 1000,
	},
	{
		.scene = WLAN_SCENE_WEB,
		.ac_mask = WMM_AC_ALL_MASK,
		.min_tp_threshold = 1000,
		.max_tp_threshold = 9000,
	},
	{
		.scene = WLAN_SCENE_YOUTUBE,
		.ac_mask = WMM_AC_ALL_MASK,
		.min_tp_threshold = 9000,
		.max_tp_threshold = 60000,
	},
	{
		.scene = WLAN_SCENE_LOW_LATENCY,
		.ac_mask = BIT(WMM_AC_VO),
		/*  VO >= 1 Mbps */
		.min_tp_threshold = 1000,
		.max_tp_threshold = __INT_MAX__,
	},
	{
		.scene = WLAN_SCENE_TPUT,
		.ac_mask = WMM_AC_ALL_MASK,
		.min_tp_threshold = 60000,
		.max_tp_threshold = __INT_MAX__,
	},
};

static int fsm_thread(void *param)
{
	struct wlan_ptracker_fsm *fsm = param;
	struct wlan_scene_event *msg = &fsm->msg;
	struct wlan_ptracker_core *core = fsm_to_core(fsm);

	while (fsm->thread_run) {
		if (kthread_should_stop()) {
			ptracker_info(core, "kthread is stopped\n");
			break;
		}
		wait_for_completion(&fsm->event);

		ptracker_dbg(core, "state: %d, trans state %d -> %d, rate %llu\n",
			msg->state, msg->src, msg->dst, msg->rate);
		wlan_ptracker_call_chain(&core->notifier, WLAN_PTRACKER_NOTIFY_SCENE_CHANGE_PREPARE, core);
		wlan_ptracker_call_chain(&core->notifier, WLAN_PTRACKER_NOTIFY_SCENE_CHANGE, core);
		msg->state = msg->dst;
	}
	return 0;
}

static bool scenes_check(u64 rate, const struct wlan_state_condition *cond,
	struct wlan_scene_event *msg)
{
	/* change bits rate to Kbits rate */
	u64 krate = rate / 1000;

	if (krate >= cond->min_tp_threshold && krate < cond->max_tp_threshold) {
		msg->rate = rate;
		return true;
	}
	return false;
}

static u32 scenes_condition_get(struct wlan_ptracker_fsm *fsm)
{
	const struct wlan_state_condition *cond;
	struct wlan_ptracker_core *core = fsm_to_core(fsm);
	struct tp_monitor_stats *stats = &core->tp;
	struct wlan_scene_event *msg = &fsm->msg;
	int i, j;

	/* check from higher restriction to lower */
	for (i = FSM_STATE_MAX - 1 ; i >= 0 ; i--) {
		cond = &fsm->conditions[i];
		if (cond->ac_mask == WMM_AC_ALL_MASK) {
			if (scenes_check(
				stats->tx[WMM_AC_MAX].rate + stats->rx[WMM_AC_MAX].rate,
				cond, msg))
				return cond->scene;
		} else {
			u64 total_tx = 0;
			u64 total_rx = 0;

			for (j = 0 ; j < WMM_AC_MAX; j++) {
				if (cond->ac_mask & BIT(j)) {
					total_tx += stats->tx[j].rate;
					total_rx += stats->rx[j].rate;
				}
				if (scenes_check(total_tx + total_rx, cond, msg))
					return cond->scene;
			}
		}
	}
	return fsm->msg.state;
}

/* TODO: fine-tune period threshold */
#define RESET_THRESHOLD 1
static void scenes_fsm_decision(struct wlan_ptracker_core *core, u32 type)
{
	struct wlan_ptracker_fsm *fsm = &core->fsm;
	struct wlan_scene_event *msg = &fsm->msg;
	u32 new_state;
	bool except = false;

	if (!fsm->fsm_thread)
		return;

	/* condition check */
	new_state = scenes_condition_get(fsm);

	/* reset check */
	if (type == WLAN_PTRACKER_NOTIFY_SUSPEND) {
		fsm->reset_cnt++;
		except = (fsm->reset_cnt >= RESET_THRESHOLD) ? true : false;
	}

	/* check state isn't change and not first time do nothing */
	if (new_state == msg->state &&
		type != WLAN_PTRACKER_NOTIFY_STA_CONNECT)
		return;
	/* new state must higher then current state */
	if (new_state < msg->state && !except) {
		ptracker_dbg(core,
			"state not change since new state %d < old state %d and reset_cnt is %d\n",
			new_state, msg->state, fsm->reset_cnt);
		return;
	}

	ptracker_dbg(core, "type %d, reset_cnt %d, %d -> %d\n",
				  type, fsm->reset_cnt, msg->state, new_state);

	/* clear reset cnt*/
	fsm->reset_cnt = 0;
	/* decide to trans state */
	mutex_lock(&msg->lock);
	msg->src = msg->state;
	msg->dst = new_state;
	msg->reason = type;
	mutex_unlock(&msg->lock);

	/* send complete to wake up thread to handle fsm */
	complete(&fsm->event);
}

static int scene_notifier_handler(struct notifier_block *nb,
	unsigned long event, void *ptr)
{
	struct wlan_ptracker_core *core = ptr;
	struct wlan_ptracker_notifier *notifier = &core->notifier;

	/*
	 * Events of suspen and sta change will block wlan driver
	 * should not spend too much time. Move complex part to thread handle.
	 */
	switch (event) {
	case WLAN_PTRACKER_NOTIFY_SUSPEND:
#ifdef TP_DEBUG
		ptracker_dbg(core, "update time (%d)\n",
			jiffies_to_msecs(jiffies - notifier->prev_event));
#endif
		notifier->prev_event = jiffies;
		fallthrough;
	case WLAN_PTRACKER_NOTIFY_STA_CONNECT:
	case WLAN_PTRACKER_NOTIFY_TP:
		scenes_fsm_decision(core, event);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block scene_nb = {
	.priority = 0,
	.notifier_call = scene_notifier_handler,
};

static int scene_cond_set(struct wlan_ptracker_fsm *fsm)
{
	struct wlan_state_condition *param = &conditions[fsm->state];

	param->ac_mask = fsm->ac_mask;
	param->max_tp_threshold = fsm->max_tput;
	param->min_tp_threshold = fsm->min_tput;
	return 0;
}

static int scene_debugfs_action(struct wlan_ptracker_core *core, u32 action)
{
	struct wlan_ptracker_fsm *fsm = &core->fsm;
	switch (action) {
	case SCENE_TEST_SET_PARAM:
		scene_cond_set(fsm);
		break;
	default:
		ptracker_err(core, "action %d is not supported\n", action);
		break;
	}
	return 0;
}

static ssize_t scene_params_write(struct file *file,
	const char __user *buf, size_t len, loff_t *ppos)
{
	struct wlan_ptracker_core *core = file->private_data;
	u32 action;

	if (kstrtouint_from_user(buf, len, 10, &action))
		return -EFAULT;

	/* active action */
	scene_debugfs_action(core, action);
	return 0;
}

static int _scene_params_read(char *buf, int len)
{
	struct wlan_state_condition *param;
	int count = 0;
	int i;

	count += scnprintf(buf + count, len - count,
			"===================\n");
	for (i = 0 ; i < FSM_STATE_MAX; i++) {
		param = &conditions[i];
		count += scnprintf(buf + count, len - count,
			"state: %d, ac_mask: %#0X\n", i, param->ac_mask);
		count += scnprintf(buf + count, len - count,
			"min_tp_threshold: %u\n", param->min_tp_threshold);
		count += scnprintf(buf + count, len - count,
			"max_tp_threshold: %u\n", param->max_tp_threshold);
		count += scnprintf(buf + count, len - count,
			"===================\n");
	}
	return count;
}

#define SCENE_PARAM_BUF_SIZE 1024
static ssize_t scene_params_read(struct file *file, char __user *userbuf,
	size_t count, loff_t *ppos)
{
	char *buf;
	int len;
	int ret;

	buf = vmalloc(SCENE_PARAM_BUF_SIZE);
	if (!buf)
		return -ENOMEM;
	len = _scene_params_read(buf, SCENE_PARAM_BUF_SIZE);
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, len);
	vfree(buf);
	return ret;
}

static const struct file_operations scene_params_ops = {
	.open = simple_open,
	.read = scene_params_read,
	.write = scene_params_write,
	.llseek = generic_file_llseek,
};

static int scene_debugfs_init(struct wlan_ptracker_core *core)
{
	struct wlan_ptracker_debugfs *debugfs = &core->debugfs;
	struct wlan_ptracker_fsm *fsm = &core->fsm;

	fsm->dir = debugfs_create_dir("scene", debugfs->root);
	if (!fsm->dir)
		return -ENODEV;

	debugfs_create_file("scene_params", 0600, fsm->dir, core, &scene_params_ops);
	debugfs_create_u32("state", 0600, fsm->dir, &fsm->state);
	debugfs_create_u32("min_tput", 0600, fsm->dir, &fsm->min_tput);
	debugfs_create_u32("max_tput", 0600, fsm->dir, &fsm->max_tput);
	debugfs_create_u32("ac_mask", 0600, fsm->dir, &fsm->ac_mask);
	return 0;
}

int scenes_fsm_init(struct wlan_ptracker_fsm *fsm)
{
	struct wlan_scene_event *msg = &fsm->msg;
	struct wlan_ptracker_core *core = fsm_to_core(fsm);
	int ret = 0;

	/* assign scenes and conditions */
	fsm->conditions = &conditions[0];
	fsm->reset_cnt = 0;
	/* init msg for receiving event */
	msg->dst = WLAN_SCENE_IDLE;
	msg->src = WLAN_SCENE_IDLE;
	msg->state = WLAN_SCENE_IDLE;
	mutex_init(&msg->lock);
	scene_debugfs_init(core);

	/*scene event notifier handler from client */
	ret = wlan_ptracker_register_notifier(&core->notifier, &scene_nb);
	if (ret)
		return ret;

	/* initial thread for listening event */
	init_completion(&fsm->event);
	fsm->fsm_thread = kthread_create(fsm_thread, fsm, "wlan_ptracker_thread");
	if (IS_ERR(fsm->fsm_thread)) {
		ret = PTR_ERR(fsm->fsm_thread);
		fsm->fsm_thread = NULL;
		ptracker_err(core, "unable to start kernel thread %d\n", ret);
		return ret;
	}
	fsm->thread_run = true;
	wake_up_process(fsm->fsm_thread);
	return 0;
}

void scenes_fsm_exit(struct wlan_ptracker_fsm *fsm)
{
	struct wlan_ptracker_core *core = fsm_to_core(fsm);

	if (fsm->dir)
		debugfs_remove_recursive(fsm->dir);

	wlan_ptracker_unregister_notifier(&core->notifier, &scene_nb);
	fsm->thread_run = false;
	complete(&fsm->event);
	if (fsm->fsm_thread) {
		int ret = kthread_stop(fsm->fsm_thread);
		fsm->fsm_thread = NULL;
		if (ret)
			ptracker_err(core, "stop thread fail: %d\n", ret);
	}
	fsm->conditions = NULL;
	fsm->reset_cnt = 0;
}
