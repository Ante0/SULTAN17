// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Wifi performance tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/debugfs.h>
#include <linux/workqueue.h>
#include <linux/delay.h>
#include "core.h"

#define DYMAIC_TWT_CONFIG_ID    3

/* for tcp one pair case */
#define TWT_IDLE_INTERVAL		(500 * 1024) 	/* 512000 */
#define TWT_IDLE_DURATION		(768 * 32)   	/* 24576 */
#define TWT_WEB_INTERVAL		(104 * 1024) 	/* 106496 */
#define TWT_WEB_DURATION		(256 * 32)   	/* 8192 */
#define TWT_YOUTUBE_INTERVAL		(10 * 1024)	/* 10240 */
#define TWT_YOUTUBE_DURATION		(256 * 32)	/* 8192 */

/* define reason*/
enum {
	TWT_SETUP_REASON_FRAMEWORK = WLAN_PTRACKER_NOTIFY_MAX,
	TWT_SETUP_REASON_FORCE,
	TWT_SETUP_REASON_RUNTIME,
	TWT_SETUP_REASON_MAX,
};

static const char *const reason2str[TWT_SETUP_REASON_MAX] = {
	"tp", "scene_change", "scene_prep", "suspend", "sta_connect",
	"sta_discont", "dytwt_enable", "dytwt_disable", "framework",
	"force", "runtime",
};

static const char *const state2str[WLAN_SCENE_MAX] = {
	"Idle", "Web", "Youtube", "Low latency", "Throughput"
};

static struct dytwt_scene_action dytwt_actions[WLAN_SCENE_MAX + 1] = {
	{
		.action = TWT_ACTION_SETUP,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
			.wake_duration = TWT_IDLE_DURATION,
			.wake_interval = TWT_IDLE_INTERVAL,
		},
	},
	{
		.action = TWT_ACTION_TEARDOWN,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
			.wake_duration = TWT_WEB_DURATION,
			.wake_interval = TWT_WEB_INTERVAL,
		},
	},
	{
		.action = TWT_ACTION_TEARDOWN,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
			.wake_duration = TWT_YOUTUBE_DURATION,
			.wake_interval = TWT_YOUTUBE_INTERVAL,
		},
	},
	{
		.action = TWT_ACTION_TEARDOWN,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
		},
	},
	{
		.action = TWT_ACTION_TEARDOWN,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
		},
	},
	/* used for force mode */
	{
		.action = TWT_ACTION_SETUP,
		.param = {
			.config_id = DYMAIC_TWT_CONFIG_ID,
			.nego_type = 0,
			.trigger_type = 0,
			.wake_duration = TWT_IDLE_DURATION,
			.wake_interval = TWT_IDLE_INTERVAL,
		},
	}
};
#define TWT_ACTION_SIZE ARRAY_SIZE(dytwt_actions)

static int dytwt_client_twt_setup(struct wlan_ptracker_client *client, u32 state)
{
	if (!client->dytwt_ops || !client->priv)
		return -EINVAL;

	if (!client->dytwt_ops->setup)
		return -EINVAL;

	if (state >= WLAN_SCENE_MAX)
		return -EINVAL;

	return client->dytwt_ops->setup(client->priv, &dytwt_actions[state].param);
}

static int dytwt_client_twt_teardown(struct wlan_ptracker_client *client, u32 state)
{
	if (!client->dytwt_ops || !client->priv)
		return -EINVAL;

	if (!client->dytwt_ops->teardown)
		return -EINVAL;

	if (state >= WLAN_SCENE_MAX)
		return -EINVAL;
	return client->dytwt_ops->teardown(client->priv, &dytwt_actions[state].param);
}

static bool dytwt_client_twt_cap(struct wlan_ptracker_client *client)
{
	struct dytwt_cap param;
	struct wlan_ptracker_core *core = client->core;
	struct dytwt_manager *dytwt = core->dytwt;
	int ret;

	if (!client->dytwt_ops || !client->priv)
		return false;

	if (!client->dytwt_ops->get_cap)
		return false;

	ret = client->dytwt_ops->get_cap(client->priv, &param);

	ptracker_dbg(core, "%d, %d, %d, %d\n", param.device_cap, param.peer_cap,
		param.link_speed, param.rssi);
	if (ret)
		return false;

	if (!param.peer_cap || !param.device_cap) {
		ptracker_err(core, "dytwt is not enabled due to capability: %d, %d\n",
			param.device_cap, param.peer_cap);
		return false;
	}

	if (param.rssi != 0 && param.rssi < dytwt->rssi_threshold) {
		ptracker_err(dytwt->core, "dytwt is not enabled due to rssi %d < %d\n",
			param.rssi, dytwt->rssi_threshold);
		return false;
	}

	if (param.link_speed < dytwt->link_threshold) {
		ptracker_err(dytwt->core, "dytwt is not enabled due to linkspeed %d < %d\n",
			param.link_speed, dytwt->link_threshold);
		return false;
	}
	return true;
}

static int dytwt_client_twt_pwrstates(struct wlan_ptracker_client *client,
	struct dytwt_pwr_state *state)
{
	if (!client->dytwt_ops || !client->priv)
		return -EINVAL;

	if (!client->dytwt_ops->get_pwrstates)
		return -EINVAL;

	return client->dytwt_ops->get_pwrstates(client->priv, state);
}

static int dytwt_client_twt_get_stats(struct wlan_ptracker_client *client,
	struct dytwt_stats *stats)
{
	if (!client->dytwt_ops || !client->priv)
		return -EINVAL;

	if (!client->dytwt_ops->get_stats)
		return -EINVAL;

	return client->dytwt_ops->get_stats(client->priv, stats);
}

static int dytwt_client_twt_get_status(struct wlan_ptracker_client *client,
	struct dytwt_status *status)
{
	if (!client->dytwt_ops || !client->priv)
		return -EINVAL;

	if (!client->dytwt_ops->get_status)
		return -EINVAL;

	return client->dytwt_ops->get_status(client->priv, status);
}

static inline void dytwt_record_get_pwr(u64 asleep, u64 awake, u64 *total, int *percent)
{
	/* for percent */
	*total = (asleep + awake) / 100;
	*percent = (*total == 0) ? 0 : (asleep / *total);
	/* trans 100 us to ms */
	*total /= 10;
}

static int dytwt_record_priv_read(struct wlan_ptracker_core *core, void *cur, void *next,
	char *buf, int len)
{
	struct dytwt_entry *c = cur;
	struct dytwt_entry *n = next;
	int period_percent, total_percent;
	u64 period_time, total_time;
	u64 awake, asleep;

	/* next is the current state */
	if (n->pwr.asleep < c->pwr.asleep) {
		struct dytwt_pwr_state pwr;
		dytwt_client_twt_pwrstates(core->client, &pwr);
		awake = pwr.awake - c->pwr.awake;
		asleep = pwr.asleep - c->pwr.asleep;
		/* get total */
		dytwt_record_get_pwr(pwr.asleep, pwr.awake, &total_time, &total_percent);
	} else {
		/* get period */
		awake = n->pwr.awake - c->pwr.awake;
		asleep = n->pwr.asleep - c->pwr.asleep;
		/* get total */
		dytwt_record_get_pwr(c->pwr.asleep, c->pwr.awake, &total_time, &total_percent);
	}
	dytwt_record_get_pwr(asleep, awake, &period_time, &period_percent);
	return scnprintf(buf, len,
		"Applied: %s, Time: %llu (%llu) ms, Percent: %d%% (%d%%) Reason: %s, Rate: %d",
		c->apply ? "TRUE" : "FALSE", period_time, total_time, period_percent, total_percent,
		reason2str[c->reason], c->rate);
}

static void dytwt_counter_update(struct dytwt_manager *dytwt, struct dytwt_pwr_state *pwr)
{
	struct dytwt_counters *counter = &dytwt->counters;
	struct dytwt_statistic *stat = &counter->scene[dytwt->prev];
	u64 asleep = pwr->asleep - counter->prev_asleep;
	u64 awake = pwr->awake - counter->prev_awake;
	u64 count = pwr->count - counter->prev_asleep_cnt;

	stat->asleep += asleep;
	stat->awake += awake;
	counter->prev_asleep = pwr->asleep;
	counter->prev_awake = pwr->awake;
	counter->prev_asleep_cnt = pwr->count;
	counter->total_awake += awake;
	counter->total_sleep += asleep;
	counter->total_sleep_cnt += count;
}

static void dytwt_mgmt_history_store(struct wlan_ptracker_client *client,
	struct dytwt_manager *dytwt, struct wlan_scene_event *msg, bool apply, u32 reason)
{
	struct dytwt_entry *entry;

	/* record assign base*/
	entry = wlan_ptracker_history_store(dytwt->hm, msg->dst);
	if (!entry)
		return;
	/* record private values */
	entry->apply = apply;
	entry->reason = reason;
	entry->rate = msg->rate;
	dytwt_client_twt_pwrstates(client, &entry->pwr);
	dytwt_counter_update(dytwt, &entry->pwr);
	/* prev will be used for decided teardown or not. */
	dytwt->prev = msg->dst;
}

/* This function is running in thread context */
static int _dytwt_scene_change_handler(struct dytwt_manager *dytwt,
	struct wlan_ptracker_client *client)
{
	struct wlan_ptracker_core *core = client->core;
	struct wlan_scene_event *msg = &core->fsm.msg;
	struct dytwt_scene_action *act;
	bool apply = false;
	u32 state = msg->dst;
	int ret = 0;

	if (!(dytwt->feature_flag & BIT(FEATURE_FLAG_TWT)))
		goto out;

	if (!dytwt_client_twt_cap(client)) {
		ptracker_dbg(dytwt->core, "twt is not supported on device or peer\n");
		goto out;
	}
	act = &dytwt_actions[state];

	/* follow action to setup */
	if (act->action == TWT_ACTION_SETUP)
		ret = dytwt_client_twt_setup(client, state);
	apply = ret ? false : true;
out:
	/* store record of history even twt is not applied */
	dytwt_mgmt_history_store(client, dytwt, msg, apply, msg->reason);
	ptracker_dbg(dytwt->core, "twt setup for state: %d, reason: %s, ret: %d\n",
		state, reason2str[msg->reason], ret);
	return ret;
}

static void dytwt_delay_setup(struct work_struct *work)
{
	struct dytwt_manager *dytwt = container_of(work, struct dytwt_manager, setup_wq.work);
	struct wlan_ptracker_core *core = dytwt->core;
	struct wlan_ptracker_client *client;

	if (!core)
		return;

	client = core->client;
	/* for first time update value is required*/
	dytwt->twt_cap = dytwt_client_twt_cap(client);
	_dytwt_scene_change_handler(dytwt, client);
}

#define TWT_WAIT_STA_READY_TIME 2000
static int dytwt_scene_change_handler(struct wlan_ptracker_client *client)
{
	struct wlan_ptracker_core *core = client->core;
	struct dytwt_manager *dytwt = core->dytwt;
	struct wlan_scene_event *msg = &core->fsm.msg;

	if (msg->reason == WLAN_PTRACKER_NOTIFY_STA_CONNECT)
		schedule_delayed_work(&dytwt->setup_wq, msecs_to_jiffies(TWT_WAIT_STA_READY_TIME));
	else
		_dytwt_scene_change_handler(dytwt, client);
	return 0;
}

#define TWT_HISTORY_BUF_SIZE 10240
static ssize_t twt_read(struct file *file, char __user *userbuf, size_t count, loff_t *ppos)
{
	struct wlan_ptracker_core *core = file->private_data;
	struct dytwt_manager *dytwt = core->dytwt;
	char *buf;
	int len;
	ssize_t ret;

	buf = vmalloc(TWT_HISTORY_BUF_SIZE);

	if (!buf)
		return -ENOMEM;

	len = wlan_ptracker_history_read(core, dytwt->hm, buf, TWT_HISTORY_BUF_SIZE);
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, len);
	vfree(buf);
	return ret;
}

static const struct file_operations twt_ops = {
	.open = simple_open,
	.read = twt_read,
	.llseek = generic_file_llseek,
};

static void dytwt_force_twt_setup(struct wlan_ptracker_client *client, struct dytwt_manager *dytwt,
	u32 reason)
{
	int ret = 0;
	bool apply = false;
	u32 state = dytwt->state;
	struct wlan_scene_event msg;
	struct dytwt_scene_action *act = &dytwt_actions[state];

	switch(act->action) {
	case TWT_ACTION_SETUP:
		ret = dytwt_client_twt_setup(client, state);
		break;
	case TWT_ACTION_TEARDOWN:
		ret = dytwt_client_twt_teardown(client, state);
		break;
	default:
		break;
	}
	apply = ret ? false : true;
	msg.dst = dytwt->state;
	/* store record of history even twt is not applied */
	dytwt_mgmt_history_store(client, dytwt, &msg, apply, reason);
}

static inline void twt_enable(struct wlan_ptracker_client *client, bool enable, u32 reason)
{
	struct wlan_ptracker_core *core = client->core;
	struct dytwt_manager *dytwt = core->dytwt;

	if (enable) {
		dytwt->feature_flag |= BIT(FEATURE_FLAG_TWT);
		dytwt_scene_change_handler(client);
	} else {
		dytwt->state = WLAN_SCENE_TPUT;
		dytwt_force_twt_setup(client, dytwt, reason);
		dytwt->feature_flag &= ~BIT(FEATURE_FLAG_TWT);
	}
}

#define DYTWT_RUNTIME_TIMER 2000
static void dytwt_runtime(struct work_struct *work)
{
	struct dytwt_manager *dytwt = container_of(work, struct dytwt_manager, wq.work);
	struct dytwt_scene_action *act;
	struct wlan_ptracker_client *client;

	if (!dytwt->core)
		goto end;

	if (dytwt->prev == WLAN_SCENE_MAX)
		goto end;

	client = dytwt->core->client;
	act = &dytwt_actions[dytwt->prev];
	/* update twt_cap periodically */
	dytwt->twt_cap = dytwt_client_twt_cap(client);
	if (act->action == TWT_ACTION_SETUP && !dytwt->twt_cap) {
		dytwt->state = WLAN_SCENE_TPUT;
		ptracker_dbg(dytwt->core, "teardown twt due to hit threshold\n");
		dytwt_force_twt_setup(client, dytwt, TWT_SETUP_REASON_RUNTIME);
	}
end:
	schedule_delayed_work(&dytwt->wq, msecs_to_jiffies(DYTWT_RUNTIME_TIMER));
}

static void update_twt_flag(struct wlan_ptracker_core *core, struct dytwt_manager *dytwt)
{
	twt_enable(core->client, !(dytwt->feature_flag & BIT(FEATURE_FLAG_TWT)),
		TWT_SETUP_REASON_FORCE);
}

static void update_twt_parameters(struct dytwt_manager *dytwt)
{
	u32 state = dytwt->state;
	struct dytwt_scene_action *cfg_act = &dytwt_actions[WLAN_SCENE_MAX];
	struct dytwt_scene_action *cur_act = &dytwt_actions[state];

	cur_act->param.wake_duration = cfg_act->param.wake_duration;
	cur_act->param.wake_interval = cfg_act->param.wake_interval;
	cur_act->action = cfg_act->action;
}

static void dytwt_stats_dump(struct wlan_ptracker_client *client, struct dytwt_manager *dytwt)
{
	struct dytwt_stats stats;
	struct wlan_ptracker_core *core = dytwt->core;

	stats.config_id = DYMAIC_TWT_CONFIG_ID;
	dytwt_client_twt_get_stats(client, &stats);

	ptracker_info(core, "rx_ucast_pkts: %d, rx_pkts_retried: %d\n",
		stats.rx_ucast_pkts, stats.rx_pkts_retried);
	ptracker_info(core, "rx_pkt_sz_avg: %d, rx_pkts_avg: %d\n",
		stats.rx_pkt_sz_avg, stats.rx_pkts_avg);
	ptracker_info(core, "rx_pkts_min: %d, rx_pkts_max: %d\n",
		stats.rx_pkts_min, stats.rx_pkts_max);
	ptracker_info(core, "tx_ucast_pkts: %d, tx_failures: %d\n",
		stats.tx_ucast_pkts, stats.tx_failures);
	ptracker_info(core, "tx_pkt_sz_avg: %d, tx_pkts_avg: %d\n",
		stats.tx_pkt_sz_avg, stats.tx_pkts_avg);
	ptracker_info(core, "tx_pkts_min: %d, tx_pkts_max: %d\n",
		stats.tx_pkts_min, stats.tx_pkts_max);
	ptracker_info(core, "sp_seq: %d, eosp_count: %d, eosp_dur_avg: %d\n",
		stats.sp_seq, stats.eosp_count, stats.eosp_dur_avg);
}

static void dytwt_status_dump(struct wlan_ptracker_client *client, struct dytwt_manager *dytwt)
{
	struct dytwt_status status;

	status.config_id = DYMAIC_TWT_CONFIG_ID;
	dytwt_client_twt_get_status(client, &status);

	ptracker_info(dytwt->core, "config_id: %d, flow_id: %d, flow_flags: %x\n",
		status.config_id, status.flow_id, status.flow_flags);
	ptracker_info(dytwt->core, "setup_cmd: %d, channel: %d, nego_type: %d\n",
		status.setup_cmd, status.channel, status.nego_type);
	ptracker_info(dytwt->core, "wake_dur: %d, wake_int: %d\n",
		status.wake_dur, status.wake_int);
}

static int dytwt_debugfs_action(struct wlan_ptracker_core *core, u32 action)
{
	struct dytwt_pwr_state pwr_state;
	struct dytwt_manager *dytwt = core->dytwt;
	struct wlan_ptracker_client *client = core->client;

	switch (action) {
	case TWT_TEST_FORCE_STATE:
		dytwt_force_twt_setup(client, dytwt, TWT_SETUP_REASON_FORCE);
		break;
	case TWT_TEST_CAP:
		dytwt_client_twt_cap(client);
		break;
	case TWT_TEST_PWRSTATS:
		dytwt_client_twt_pwrstates(client, &pwr_state);
		break;
	case TWT_TEST_ONOFF:
		update_twt_flag(core, dytwt);
		break;
	case TWT_TEST_SET_PARAM:
		update_twt_parameters(dytwt);
		break;
	case TWT_TEST_DUMP_STATS:
		dytwt_stats_dump(client, dytwt);
		break;
	case TWT_TEST_DUMP_STATUS:
		dytwt_status_dump(client, dytwt);
		break;
	default:
		ptracker_err(core, "action %d is not supported\n", action);
		return -ENOTSUPP;
	}
	return 0;
}

static ssize_t twt_params_write(struct file *file, const char __user *buf, size_t len,
	loff_t *ppos)
{
	struct wlan_ptracker_core *core = file->private_data;
	u32 action;

	if (kstrtouint_from_user(buf, len, 10, &action))
		return -EFAULT;

	dytwt_debugfs_action(core, action);
	return len;
}

static int dytwt_params_read(char *buf, int len)
{
	struct dytwt_scene_action *act;
	struct dytwt_setup_param *param;
	int count = 0;
	int i;

	count += scnprintf(buf + count, len - count,
			"===================\n");
	for (i = 0 ; i < TWT_ACTION_SIZE; i++) {
		act = &dytwt_actions[i];
		param = &act->param;
		count += scnprintf(buf + count, len - count,
			"state: %d, action: %d\n", i, act->action);
		count += scnprintf(buf + count, len - count,
			"config_id: %d, nego_type: %d\n",
			param->config_id, param->nego_type);
		count += scnprintf(buf + count, len - count,
			"wake_interval: %u\n", param->wake_interval);
		count += scnprintf(buf + count, len - count,
			"wake_duration: %u\n", param->wake_duration);
		count += scnprintf(buf + count, len - count,
			"===================\n");
	}
	return count;
}

#define TWT_PARAM_BUF_SIZE 1024
static ssize_t twt_params_read(struct file *file, char __user *userbuf, size_t count, loff_t *ppos)
{
	char *buf;
	int len;
	int ret;

	buf = vmalloc(TWT_PARAM_BUF_SIZE);
	if (!buf)
		return -ENOMEM;
	len = dytwt_params_read(buf, TWT_PARAM_BUF_SIZE);
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, len);
	vfree(buf);
	return ret;
}

static const struct file_operations twt_params_ops = {
	.open = simple_open,
	.read = twt_params_read,
	.write = twt_params_write,
	.llseek = generic_file_llseek,
};

static int dytwt_statistic_read(struct wlan_ptracker_core *core, char *buf, int len)
{
	struct dytwt_manager *dytwt = core->dytwt;
	struct dytwt_counters *counter = &dytwt->counters;
	struct dytwt_statistic *ds;
	struct dytwt_pwr_state pwr;
	int buf_count = 0;
	int i, percent;
	u64 total, awake, asleep, count;

	buf_count += scnprintf(buf + buf_count, len - buf_count,
		"==== Dynamic TWT Setup Statistics ===\n");
	dytwt_client_twt_pwrstates(dytwt->core->client, &pwr);
	for (i = 0 ; i < WLAN_SCENE_MAX; i++) {
		ds = &counter->scene[i];
		awake = ds->awake;
		asleep = ds->asleep;
		if (i == dytwt->prev) {
			awake += pwr.awake - counter->prev_awake;
			asleep += pwr.asleep - counter->prev_asleep;
		}
		dytwt_record_get_pwr(asleep, awake, &total, &percent);
		buf_count += scnprintf(buf + buf_count, len - buf_count,
			"%s, total: %llu, awake: %llu, asleep: %llu (%d%%)\n", state2str[i], total,
			awake / 1000, asleep / 1000, percent);
	}

	awake = counter->total_awake + pwr.awake - counter->prev_awake;
	asleep = counter->total_sleep + pwr.asleep - counter->prev_asleep;
	count = counter->total_sleep_cnt + pwr.count - counter->prev_asleep_cnt;
	dytwt_record_get_pwr(asleep, awake, &total, &percent);
	buf_count += scnprintf(buf + buf_count, len - buf_count,
		"All, total: %llu, awake: %llu, asleep: %llu (%d%%), sleep cnt: %llu\n",
		total, awake / 1000, asleep / 1000, percent, count);
	return buf_count;
}

#define TWT_STATISTIC_SIZE 512
static ssize_t twt_statistic_read(struct file *file, char __user *userbuf, size_t count,
	loff_t *ppos)
{
	struct wlan_ptracker_core *core = file->private_data;
	char *buf;
	int len;
	int ret;

	buf = vmalloc(TWT_STATISTIC_SIZE);
	if (!buf)
		return -ENOMEM;

	len = dytwt_statistic_read(core, buf, TWT_STATISTIC_SIZE);
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, len);
	vfree(buf);
	return ret;
}

static const struct file_operations twt_statistic_ops = {
	.open = simple_open,
	.read = twt_statistic_read,
	.llseek = generic_file_llseek,
};

static void dytwt_scene_change_prepare_handler(struct wlan_ptracker_client *client)
{
	struct wlan_ptracker_core *core = client->core;
	struct dytwt_manager *dytwt = core->dytwt;
	u32 prev_state = dytwt->prev;

	if (!(dytwt->feature_flag & BIT(FEATURE_FLAG_TWT)))
		return;

	/*
	 * prepare to change state, teardown the original setup first.
	 * This change is not recorded in history.
	 */
	if (dytwt_actions[prev_state].action == TWT_ACTION_SETUP)
		dytwt_client_twt_teardown(client, dytwt->prev);
}

static int dytwt_notifier_handler(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct wlan_ptracker_core *core = ptr;
	struct wlan_ptracker_client *client = core->client;
	struct dytwt_manager *dytwt = core->dytwt;

	switch (event) {
	case WLAN_PTRACKER_NOTIFY_SCENE_CHANGE:
		dytwt_scene_change_handler(client);
		break;
	case WLAN_PTRACKER_NOTIFY_SCENE_CHANGE_PREPARE:
		dytwt_scene_change_prepare_handler(client);
		break;
	case WLAN_PTRACKER_NOTIFY_STA_CONNECT:
		schedule_delayed_work(&dytwt->wq, msecs_to_jiffies(DYTWT_RUNTIME_TIMER));
		break;
	case WLAN_PTRACKER_NOTIFY_STA_DISCONNECT:
		cancel_delayed_work_sync(&dytwt->wq);
		break;
	case WLAN_PTRACKER_NOTIFY_DYTWT_ENABLE:
		twt_enable(client, true, TWT_SETUP_REASON_FRAMEWORK);
		break;
	case WLAN_PTRACKER_NOTIFY_DYTWT_DISABLE:
		twt_enable(client, false, TWT_SETUP_REASON_FRAMEWORK);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static ssize_t dytwt_dumpstate_statistic(struct dytwt_manager *dytwt, char *buf)
{
	return dytwt_statistic_read(dytwt->core, buf, PAGE_SIZE);
}

static ssize_t dytwt_dumpstate_history(struct dytwt_manager *dytwt, char *buf)
{
	return wlan_ptracker_history_read(dytwt->core, dytwt->hm, buf, PAGE_SIZE);
}

static struct dytwt_kobj_attr attr_twt_history =
	__ATTR(history, 0664, dytwt_dumpstate_history, NULL);

static struct dytwt_kobj_attr attr_twt_statistic =
	__ATTR(statistic, 0664, dytwt_dumpstate_statistic, NULL);

static struct attribute *default_file_attrs[] = {
	&attr_twt_history.attr,
	&attr_twt_statistic.attr,
	NULL,
};
ATTRIBUTE_GROUPS(default_file);

static ssize_t dytwt_sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct dytwt_manager *dytwt;
	struct dytwt_kobj_attr *dytwt_attr;
	int ret = -EIO;

	dytwt = container_of(kobj, struct dytwt_manager, kobj);
	dytwt_attr = container_of(attr, struct dytwt_kobj_attr, attr);

	if (dytwt_attr->show)
		ret = dytwt_attr->show(dytwt, buf);
	return ret;
}

static ssize_t dytwt_sysfs_store(struct kobject *kobj, struct attribute *attr, const char *buf,
	size_t count)
{
	struct dytwt_manager *dytwt;
	struct dytwt_kobj_attr *dytwt_attr;
	int ret = -EIO;

	dytwt = container_of(kobj, struct dytwt_manager, kobj);
	dytwt_attr = container_of(attr, struct dytwt_kobj_attr, attr);

	if (dytwt_attr->show)
		ret = dytwt_attr->store(dytwt, buf, count);
	return ret;

}

static struct sysfs_ops dytwt_sysfs_ops = {
	.show = dytwt_sysfs_show,
	.store = dytwt_sysfs_store,
};

static struct kobj_type dytwt_ktype = {
	.sysfs_ops = &dytwt_sysfs_ops,
	.default_groups = default_file_groups,
};

static int dytwt_sysfs_init(struct dytwt_manager *dytwt, struct wlan_ptracker_debugfs *debugfs)
{
	int ret;

	ret = kobject_init_and_add(&dytwt->kobj, &dytwt_ktype, &debugfs->kobj, "twt");
	if (ret)
		kobject_put(&dytwt->kobj);
	return ret;
}

static void dytwt_sysfs_exit(struct dytwt_manager *dytwt)
{
	kobject_del(&dytwt->kobj);
	kobject_put(&dytwt->kobj);
}

static int dytwt_debugfs_init(struct wlan_ptracker_core *core)
{
	struct wlan_ptracker_debugfs *debugfs = &core->debugfs;
	struct dytwt_manager *dytwt = core->dytwt;
	struct dytwt_scene_action *act = &dytwt_actions[WLAN_SCENE_MAX];

	dytwt->feature_flag |= BIT(FEATURE_FLAG_TWT);
	dytwt->dir = debugfs_create_dir("twt", debugfs->root);
	if (!dytwt->dir)
		return -ENODEV;

	debugfs_create_file("history", 0666, dytwt->dir, core, &twt_ops);
	debugfs_create_file("statistics", 0666, dytwt->dir, core, &twt_statistic_ops);
	debugfs_create_file("twt_params", 0666, dytwt->dir, core, &twt_params_ops);
	debugfs_create_u32("state", 0666, dytwt->dir, &dytwt->state);
	debugfs_create_u32("wake_interval", 0666, dytwt->dir, &act->param.wake_interval);
	debugfs_create_u32("wake_duration", 0666, dytwt->dir, &act->param.wake_duration);
	debugfs_create_u32("action", 0666, dytwt->dir, &act->action);
	debugfs_create_u32("feature_flag", 0666, dytwt->dir, &dytwt->feature_flag);
	dytwt_sysfs_init(dytwt, debugfs);
	return 0;
}

static void dytwt_debugfs_exit(struct dytwt_manager *dytwt)
{
	if (dytwt->dir)
		debugfs_remove_recursive(dytwt->dir);
	dytwt_sysfs_exit(dytwt);
}

#define TWT_DEFAULT_MIN_LINK_SPEED (180000)
#define TWT_DEFAULT_MIN_RSSI (-70)
#define DYTWT_RECORD_MAX 30
static struct dytwt_manager *dytwt_mgmt_init(struct wlan_ptracker_core *core)
{
	struct history_manager *hm;
	struct dytwt_manager *dytwt = kzalloc(sizeof(struct dytwt_manager), GFP_KERNEL);

	if (!dytwt)
		return NULL;

	dytwt->state = WLAN_SCENE_IDLE;
	dytwt->prev = WLAN_SCENE_MAX;
	dytwt->core = core;
	dytwt->link_threshold = TWT_DEFAULT_MIN_LINK_SPEED;
	dytwt->rssi_threshold = TWT_DEFAULT_MIN_RSSI;
	INIT_DELAYED_WORK(&dytwt->wq, dytwt_runtime);
	INIT_DELAYED_WORK(&dytwt->setup_wq, dytwt_delay_setup);
	hm =  wlan_ptracker_history_create(DYTWT_RECORD_MAX, sizeof(struct dytwt_entry));
	if (!hm) {
		kfree(dytwt);
		return NULL;
	}
	strncpy(hm->name, "Dynamic TWT Setup", sizeof(hm->name));
	hm->priv_read = dytwt_record_priv_read;
	dytwt->hm = hm;

	return dytwt;
}

static void dytwt_mgmt_exit(struct dytwt_manager *dytwt)
{
	cancel_delayed_work_sync(&dytwt->wq);
	cancel_delayed_work_sync(&dytwt->setup_wq);
	wlan_ptracker_history_destroy(dytwt->hm);
	kfree(dytwt);
}

static struct notifier_block twt_nb = {
	.priority = 0,
	.notifier_call = dytwt_notifier_handler,
};

int dytwt_init(struct wlan_ptracker_core *core)
{
	core->dytwt = dytwt_mgmt_init(core);
	dytwt_debugfs_init(core);
	return wlan_ptracker_register_notifier(&core->notifier, &twt_nb);
}

void dytwt_exit(struct wlan_ptracker_core *core)
{
	struct dytwt_manager *dytwt = core->dytwt;

	core->dytwt = NULL;
	wlan_ptracker_unregister_notifier(&core->notifier, &twt_nb);

	if (!dytwt)
		return;

	dytwt_debugfs_exit(dytwt);
	dytwt_mgmt_exit(dytwt);
}
