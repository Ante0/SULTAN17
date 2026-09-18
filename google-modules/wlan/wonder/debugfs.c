// SPDX-License-Identifier: GPL-2.0
/*
 * Google Wonder WiFi Virtual Soft-MAC Driver
 *
 * Debugfs implementation for the Wonder driver.
 */
#define LOG_MODULE_NAME "debugfs"

#include <linux/debugfs.h>
#include <linux/netdevice.h>
#include <linux/uaccess.h>
#include <linux/etherdevice.h>
#include <linux/seq_file.h>

#include "core.h"
#include "wonder_log.h"
#include "mac80211.h"
#include "wondertap_internal.h"



static struct dentry *wonder_debugfs_root;

static int wonder_channel_status_report_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_data *wondertap = &wonder->wondertap_data;
	struct wondertap_channel_status_report *report;
	u32 num_channels;
	size_t size;
	int ret, i;

	mutex_lock(&wondertap->lock);
	num_channels = wondertap->cached_channel_schedule.channel_list_len;
	mutex_unlock(&wondertap->lock);

	if (num_channels == 0) {
		seq_puts(m, "Channel hopping list is empty.\n");
		return 0;
	}

	size = sizeof(*report) + num_channels * sizeof(struct wondertap_channel_status);
	report = kzalloc(size, GFP_KERNEL);
	if (!report)
		return -ENOMEM;

	report->channel_status_len = num_channels;
	ret = wondertap_get_channel_status_report(wondertap, report);
	if (ret) {
		seq_printf(m, "Failed to get channel status report: %d\n", ret);
		kfree(report);
		return 0;
	}

	seq_printf(m, "Current Hopping Request TSF: 0x%08x\n",
		report->current_channel_hopping_request_tsf);
	seq_printf(m, "Current Channel Index: %u\n", report->current_channel_index);
	seq_printf(m, "Channel Status Length: %u\n", report->channel_status_len);

	for (i = 0; i < report->channel_status_len; i++) {
		struct wondertap_channel_status *status = &report->status[i];

		seq_printf(m, "\n  [%d] Freq: %u MHz\n", i, status->freq);
		seq_printf(m, "      Switch TSF: 0x%08x\n", status->channel_switch_tsf);
		seq_printf(m, "      Start TSF:  0x%08x\n", status->channel_start_tsf);
		seq_printf(m, "      End TSF:    0x%08x\n", status->channel_end_tsf);
		seq_printf(m, "      TX Traffic Index: %u\n", status->tx_traffic_index);
		seq_printf(m, "      RX Traffic Index: %u\n", status->rx_traffic_index);
	}

	kfree(report);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(wonder_channel_status_report);

static int wonder_channel_schedule_request_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_data *wondertap = &wonder->wondertap_data;
	struct channel_schedule_request *schedule;
	int i;

	mutex_lock(&wondertap->lock);
	schedule = &wondertap->cached_channel_schedule;

	if (schedule->channel_list_len == 0) {
		seq_puts(m, "Channel hopping schedule list is empty.\n");
		mutex_unlock(&wondertap->lock);
		return 0;
	}

	seq_printf(m, "Channel List Length: %u\n", schedule->channel_list_len);
	seq_printf(m, "Next Channel Index: %u\n", schedule->next_channel_index);
	seq_printf(m, "Dwell Time (TU): %u\n", schedule->dwell_time_tu);
	seq_printf(m, "Target Switch TSF: 0x%08x\n", schedule->target_switch_time_tsf);

	seq_puts(m, "\nChannel List:\n");
	if (schedule->channel_list) {
		for (i = 0; i < schedule->channel_list_len; i++) {
			seq_printf(m, "  [%d] Freq: %u MHz, BW: %u, Role: %u\n", i,
				   schedule->channel_list[i].freq,
				   schedule->channel_list[i].bandwidth,
				   schedule->channel_list[i].role);
		}
	}

	mutex_unlock(&wondertap->lock);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(wonder_channel_schedule_request);

static int wonder_station_query_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_data *wondertap = &wonder->wondertap_data;
	struct wondertap_station_info sta_info = {0};
	int ret;

	mutex_lock(&wondertap->lock);
	if (is_zero_ether_addr(wondertap->query_mac_addr)) {
		seq_puts(m, "Please write a valid MAC address first.\n");
		mutex_unlock(&wondertap->lock);
		return 0;
	}
	memcpy(sta_info.mac, wondertap->query_mac_addr, ETH_ALEN);
	mutex_unlock(&wondertap->lock);

	if (wondertap->wonder_ops && wondertap->wonder_ops->set_station_info) {
		ret = wondertap_set_station_info(wondertap,
							      WONDERTAP_STATION_STATE_QUERY,
									&sta_info);
		if (ret) {
			seq_printf(m, "Failed to query station info: %d\n", ret);
			return 0;
		}

		seq_printf(m, "Station MAC: %pM\n", sta_info.mac);
		seq_printf(m, "AID: %u\n", sta_info.aid);
		seq_printf(m, "Capability Mask: 0x%08x\n", sta_info.capability_mask);

		seq_printf(m, "    HT: %s, VHT: %s, HE: %s, HE_6G: %s\n",
			   (sta_info.capability_mask & BIT(WONDERTAP_STATION_CAP_HT)) ?
				   "Y" : "N",
			   (sta_info.capability_mask & BIT(WONDERTAP_STATION_CAP_VHT)) ?
				   "Y" : "N",
			   (sta_info.capability_mask & BIT(WONDERTAP_STATION_CAP_HE)) ?
				   "Y" : "N",
			   (sta_info.capability_mask & BIT(WONDERTAP_STATION_CAP_HE_6G)) ?
				   "Y" : "N");

		if (sta_info.capability_mask & BIT(WONDERTAP_STATION_CAP_HT))
			seq_hex_dump(m, "    HT_CAP: ", DUMP_PREFIX_NONE,
				     16, 1, &sta_info.ht_capa,
				     sizeof(struct ieee80211_ht_cap), false);

		if (sta_info.capability_mask & BIT(WONDERTAP_STATION_CAP_VHT))
			seq_hex_dump(m, "    VHT_CAP: ", DUMP_PREFIX_NONE,
				     16, 1, &sta_info.vht_capa,
				     sizeof(struct ieee80211_vht_cap), false);

		if ((sta_info.capability_mask & BIT(WONDERTAP_STATION_CAP_HE)) &&
		    sta_info.he_capa_len > 0)
			seq_hex_dump(m, "    HE_CAP: ", DUMP_PREFIX_NONE,
				     16, 1, &sta_info.he_capa, sta_info.he_capa_len, false);

		if (sta_info.capability_mask & BIT(WONDERTAP_STATION_CAP_HE_6G))
			seq_hex_dump(m, "    HE_6G_CAP: ", DUMP_PREFIX_NONE,
				     16, 1, &sta_info.he_6ghz_capa,
				     sizeof(struct ieee80211_he_6ghz_capa), false);
	} else {
		seq_puts(m, "set_station_info op is not implemented by vendor.\n");
	}

	return 0;
}

static ssize_t wonder_station_query_write(struct file *file, const char __user *user_buf,
					  size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct wonder_data *wonder = m->private;
	struct wondertap_data *wondertap = &wonder->wondertap_data;
	char buf[20];
	size_t len;

	len = min(count, sizeof(buf) - 1);
	if (copy_from_user(buf, user_buf, len))
		return -EFAULT;
	buf[len] = '\0';

	mutex_lock(&wondertap->lock);
	if (!mac_pton(buf, wondertap->query_mac_addr)) {
		pr_err("Invalid MAC address format. Expected xx:xx:xx:xx:xx:xx\n");
		mutex_unlock(&wondertap->lock);
		return -EINVAL;
	}
	mutex_unlock(&wondertap->lock);

	return count;
}

static int wonder_station_query_open(struct inode *inode, struct file *file)
{
	return single_open(file, wonder_station_query_show, inode->i_private);
}

static const struct file_operations wonder_station_query_fops = {
	.open = wonder_station_query_open,
	.read = seq_read,
	.write = wonder_station_query_write,
	.llseek = seq_lseek,
	.release = single_release,
};

static const char *wonder_ver_to_str(enum wondertap_ver ver)
{
	switch (ver) {
	case WONDER_VERSION_1_0: return "WONDER_VERSION_1_0";
	case WONDER_VERSION_1_1: return "WONDER_VERSION_1_1";
	case WONDER_VERSION_1_2: return "WONDER_VERSION_1_2";
	case WONDER_VERSION_1_3: return "WONDER_VERSION_1_3";
	case WONDER_VERSION_1_4: return "WONDER_VERSION_1_4";
	case WONDER_VERSION_1_4_1: return "WONDER_VERSION_1_4_1";
	case WONDER_VERSION_1_5: return "WONDER_VERSION_1_5";
	case WONDER_VERSION_1_5_1: return "WONDER_VERSION_1_5_1";
	case WONDER_VERSION_1_6_1: return "WONDER_VERSION_1_6_1 (or 1_6_2/1_6_3)";
	case WONDER_VERSION_1_6_4: return "WONDER_VERSION_1_6_4";
	case WONDER_VERSION_1_6_5: return "WONDER_VERSION_1_6_5";
	default: return "UNKNOWN_VERSION";
	}
}

static int wonder_version_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;
	struct wondertap_data *wondertap = &wonder->wondertap_data;

	mutex_lock(&wondertap->lock);
	seq_printf(m, "Wonder version: %s\n", wonder_ver_to_str(wondertap->ver));
	seq_printf(m, "WiFi version: %s\n", wonder_ver_to_str(wondertap->wifi_ver));
	mutex_unlock(&wondertap->lock);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(wonder_version);

static int wonder_stats_show(struct seq_file *m, void *v)
{
	struct wonder_data *wonder = m->private;

	seq_printf(m, "--- Wonder Driver Stats ---\n");
	seq_printf(m, "wonder_tx_entry_cnt:            %llu\n", wonder->stats.tx_entry_cnt);
	seq_printf(m, "wonder_tx_success_cnt:          %llu\n\n", wonder->stats.tx_success_cnt);

	seq_printf(m, "wonder_rx_entry_cnt:            %llu (receiv from wondertap0)\n",
		   wonder->stats.rx_entry_cnt);
	seq_printf(m, "wonder_rx_to_mac_cnt:           %llu (forward to mac80211)\n\n",
		   wonder->stats.rx_to_mac_cnt);

	return 0;
}

static ssize_t wonder_stats_write(struct file *file, const char __user *user_buf,
				  size_t count, loff_t *ppos)
{
	struct seq_file *m = file->private_data;
	struct wonder_data *wonder = m->private;

	/* Reset Wonder internal counters */
	wonder->stats.tx_entry_cnt = 0;
	wonder->stats.tx_success_cnt = 0;
	wonder->stats.rx_entry_cnt = 0;
	wonder->stats.rx_to_mac_cnt = 0;

	return count;
}

static int wonder_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, wonder_stats_show, inode->i_private);
}

static const struct file_operations wonder_stats_fops = {
	.open = wonder_stats_open,
	.read = seq_read,
	.write = wonder_stats_write,
	.llseek = seq_lseek,
	.release = single_release,
};

int wonder_debugfs_init(void *wonder)
{
	wonder_debugfs_root = debugfs_create_dir("wonder", NULL);
	if (!wonder_debugfs_root)
		return -ENODEV;

	debugfs_create_file("channel_status_report", 0644, wonder_debugfs_root,
			    wonder, &wonder_channel_status_report_fops);
	debugfs_create_file("channel_schedule_request", 0644, wonder_debugfs_root,
			    wonder, &wonder_channel_schedule_request_fops);
	debugfs_create_file("station_query", 0644, wonder_debugfs_root,
			    wonder, &wonder_station_query_fops);
	debugfs_create_file("version", 0444, wonder_debugfs_root,
			    wonder, &wonder_version_fops);
	debugfs_create_file("stats", 0644, wonder_debugfs_root,
			    wonder, &wonder_stats_fops);
	debugfs_create_bool("amsdu_enable", 0644, wonder_debugfs_root,
			    &((struct wonder_data *)wonder)->amsdu_enable);
	debugfs_create_bool("ampdu_enable", 0644, wonder_debugfs_root,
			    &((struct wonder_data *)wonder)->ampdu_enable);
	debugfs_create_bool("channel_hopping_enable", 0644, wonder_debugfs_root,
			    &((struct wonder_data *)wonder)->channel_hopping_enable);
	debugfs_create_bool("ra_enable", 0644, wonder_debugfs_root,
			    &((struct wonder_data *)wonder)->ra_enable);
	debugfs_create_u32("amsdu_threshold", 0644, wonder_debugfs_root,
			    &((struct wonder_data *)wonder)->amsdu_threshold);
	debugfs_create_u32("amsdu_delay", 0644, wonder_debugfs_root,
			    &((struct wonder_data *)wonder)->amsdu_delay);
	debugfs_create_bool("syna_support_enable", 0644, wonder_debugfs_root,
			    &((struct wonder_data *)wonder)->syna_support_enable);
	return 0;
}

void wonder_debugfs_exit(void)
{
	debugfs_remove_recursive(wonder_debugfs_root);
}