// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for WiFi Performance Tracker
 *
 * Copyright 2022 Google LLC.
 *
 * Author: Star Chang <starchang@google.com>
 */
#include <linux/kernel.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/timekeeping.h>
#include <linux/rtc.h>
#include "core.h"
#include "debugfs.h"

static const char *const state2str[WLAN_SCENE_MAX] = {
	"Idle", "Web", "Youtube", "Low latency", "Throughput"
};

#define READ_BUF_SIZE 1024
static ssize_t action_read(struct file *file, char __user *userbuf, size_t count, loff_t *ppos)
{
	struct wlan_ptracker_core *core = file->private_data;
	char *buf;
	int len = 0;
	int i;
	ssize_t ret;

	buf = vmalloc(READ_BUF_SIZE);

	if (!buf)
		return 0;

	len += scnprintf(buf + len, READ_BUF_SIZE - len,
		"==== DSCP to AC mapping table ===\n");
	for (i = 0 ; i < DSCP_MAX; i++) {
		if (!core->dscp_to_ac[i])
			continue;
		len += scnprintf(buf + len, READ_BUF_SIZE - len,
			"dscp[%d]  : %u\n", i, core->dscp_to_ac[i]);
	}
	ret = simple_read_from_buffer(userbuf, count, ppos, buf, len);
	vfree(buf);
	return ret;
}

static void update_dscp(struct wlan_ptracker_core *core, u32 dscp, u32 ac)
{
	ptracker_info(core, "dscp %d, ac: %d\n", dscp, ac);
	if (dscp > DSCP_MASK)
		return;
	if (ac > WMM_AC_VO)
		return;

	core->dscp_to_ac[dscp] = ac;
}

static ssize_t action_write(struct file *file,
		const char __user *buf, size_t len, loff_t *ppos)
{
	struct wlan_ptracker_core *core = file->private_data;
	struct wlan_ptracker_debugfs *debugfs = &core->debugfs;
	u32 action;

	if (kstrtouint_from_user(buf, len, 10, &action))
		return -EFAULT;

	/* active action */
	switch (action) {
	case ACTION_DSCP_UPDATE:
		update_dscp(core, debugfs->dscp, debugfs->ac);
		break;
	default:
		ptracker_err(core, "action %d is not supported!\n", action);
		return -ENOTSUPP;
	}
	return len;
}

static const struct file_operations dscp_ops = {
	.open = simple_open,
	.read = action_read,
	.write = action_write,
	.llseek = generic_file_llseek,
};

static ssize_t ptracker_sysfs_show(struct kobject *kobj, struct attribute *attr, char *buf)
{
	struct wlan_ptracker_debugfs *debugfs = container_of(kobj, struct wlan_ptracker_debugfs,
		kobj);
	struct ptracker_kobj_attr *ptracker_attr = container_of(attr, struct ptracker_kobj_attr,
		attr);
	int ret = -EIO;

	if (ptracker_attr->show)
		ret = ptracker_attr->show(debugfs, buf);
	return ret;
}

static ssize_t ptracker_sysfs_store(struct kobject *kobj, struct attribute *attr, const char *buf,
	size_t count)
{
	struct wlan_ptracker_debugfs *debugfs =
		container_of(kobj, struct wlan_ptracker_debugfs, kobj);
	struct ptracker_kobj_attr *ptracker_attr =
		container_of(attr, struct ptracker_kobj_attr, attr);
	int ret = -EIO;

	if (ptracker_attr->store)
		ret = ptracker_attr->store(debugfs, buf, count);
	return ret;
}

static struct sysfs_ops ptracker_sysfs_ops = {
	.show = ptracker_sysfs_show,
	.store = ptracker_sysfs_store,
};

static struct kobj_type ptracker_ktype = {
	.sysfs_ops = &ptracker_sysfs_ops,
};

static int wlan_ptracker_sysfs_init(struct wlan_ptracker_debugfs *debugfs)
{
	int ret;

	ret = kobject_init_and_add(&debugfs->kobj, &ptracker_ktype, NULL, PTRACKER_PREFIX);
	if (ret)
		kobject_put(&debugfs->kobj);
	return ret;
}

static void wlan_ptracker_sysfs_exit(struct wlan_ptracker_debugfs *debugfs)
{
	kobject_del(&debugfs->kobj);
	kobject_put(&debugfs->kobj);
}

int wlan_ptracker_debugfs_init(struct wlan_ptracker_debugfs *debugfs)
{
	struct wlan_ptracker_core *core = container_of(
		debugfs, struct wlan_ptracker_core, debugfs);

	debugfs->root = debugfs_create_dir(PTRACKER_PREFIX, NULL);
	if (!debugfs->root)
		return -ENODEV;
	debugfs_create_file("action", 0600, debugfs->root, core, &dscp_ops);
	debugfs_create_u32("dscp", 0600, debugfs->root, &debugfs->dscp);
	debugfs_create_u32("ac", 0600, debugfs->root, &debugfs->ac);
	wlan_ptracker_sysfs_init(debugfs);
	return 0;
}

void wlan_ptracker_debugfs_exit(struct wlan_ptracker_debugfs *debugfs)
{
	debugfs_remove_recursive(debugfs->root);
	debugfs->root = NULL;
	wlan_ptracker_sysfs_exit(debugfs);
}

struct history_manager *wlan_ptracker_history_create(int entry_count, int entry_size)
{
	struct history_manager *hm;

	if (entry_count < 0 || entry_size < sizeof(struct history_entry))
		return NULL;

	hm = kzalloc(sizeof(struct history_manager) + entry_size * entry_count, GFP_KERNEL);
	if (!hm)
		return NULL;

	/* initial manager */
	hm->entry_count = entry_count;
	hm->entry_size = entry_size;
	hm->cur = 0;
	hm->round = 0;
	mutex_init(&hm->mutex);
	return hm;
}

void wlan_ptracker_history_destroy(struct history_manager *hm)
{
	if (hm)
		kfree(hm);
}

void * wlan_ptracker_history_store(struct history_manager *hm, u32 state)
{
	struct history_entry *entry;

	if (!hm->entry_count)
		return NULL;

	entry = (struct history_entry *)(hm->entries + (hm->cur * hm->entry_size));
	entry->state = state;
	entry->valid = true;
	ktime_get_real_ts64(&entry->ts);

	/* update dytwt history */
	mutex_lock(&hm->mutex);
	hm->cur++;
	if (hm->cur / hm->entry_count)
		hm->round++;
	hm->cur %= hm->entry_count;
	mutex_unlock(&hm->mutex);
	return entry;
}

static int history_get_tm(struct history_entry *entry, char *time, size_t len)
{
	struct rtc_time tm;

	rtc_time64_to_tm(entry->ts.tv_sec - (sys_tz.tz_minuteswest * 60), &tm);
	return scnprintf(time, len, "%ptRs", &tm);
}

size_t wlan_ptracker_history_read(struct wlan_ptracker_core *core, struct history_manager *hm,
	char *buf, int buf_len)
{
	u8 *ptr;
	struct history_entry *cur, *next;
	int len = 0;
	int i, j;

	len += scnprintf(buf + len, buf_len - len,
		"==== %s History ===\n", hm->name);
	len += scnprintf(buf + len, buf_len - len,
		"round: %d, cur: %d, entry len: %d,  size: %d\n",
		hm->round, hm->cur, hm->entry_count, hm->entry_size);

	ptr = hm->entries;
	for (i = 0 ; i < hm->entry_count; i++) {
		cur = (struct history_entry *) ptr;
		if (!cur->valid)
			break;
		j = (i + 1) % hm->entry_count;
		next = (struct history_entry *)(hm->entries + (j * hm->entry_size));
		len += scnprintf(buf + len, buf_len - len, "%02d: ", i);
		len += history_get_tm(cur, buf + len, buf_len - len);
		len += scnprintf(buf + len, buf_len - len, "%12s =>", state2str[cur->state]);
		if (hm->priv_read)
			len += hm->priv_read(core, cur, next, buf + len, buf_len - len);
		len += scnprintf(buf + len, buf_len - len, "\n");
		ptr += hm->entry_size;
	}
	return len;
}
