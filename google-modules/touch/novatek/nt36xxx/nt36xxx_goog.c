/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2021 Google LLC
 *    Author: Super Liu <supercjliu@google.com>
 */
#include "nt36xxx.h"
#include <linux/input/mt.h>
#include <samsung/exynos_drm_connector.h> /* to_exynos_connector_state() */

void nvt_heatmap_decode(
		const uint8_t *in, const uint32_t in_sz,
		const uint8_t *out, const uint32_t out_sz)
{
	const u16 ESCAPE_MASK = 0xF000;
	const u16 ESCAPE_BIT = 0x8000;
	const u16 *in_array = (u16 *)in;
	u16 *out_array = (u16 *)out;
	const int in_array_size = in_sz / 2;
	const int out_array_max_size = out_sz / 2;

	int i;
	int j;
	int out_array_size = 0;
	u16 prev_word = 0;
	u16 repetition = 0;

	if (!out || !out_sz) {
		NVT_ERR("invalid out pointer(%p) or size(%d)\n", out, out_sz);
		return;
	}

	if (!in || !in_sz) {
		NVT_ERR("invalid in pointer(%p) or size(%d)\n", in, in_sz);
		/* Zero out the output if any invalid input. */
		memset((void *)out, 0, out_sz);
		return;
	}

	for (i = 0; i < in_array_size; i++) {
		u16 curr_word = in_array[i];

		if ((curr_word & ESCAPE_MASK) == ESCAPE_BIT) {
			repetition = (curr_word & ~ESCAPE_MASK);
			if (out_array_size + repetition > out_array_max_size)
				break;
			for (j = 0; j < repetition; j++) {
				*out_array++ = prev_word;
				out_array_size++;
			}
		} else {
			if (out_array_size >= out_array_max_size)
				break;
			*out_array++ = curr_word;
			out_array_size++;
			prev_word = curr_word;
		}
	}

	if (i != in_array_size || out_array_size != out_array_max_size) {
		NVT_DBG("%d (in=%d, out=%d, rep=%d, out_max=%d).\n",
				i, in_array_size, out_array_size,
				repetition, out_array_max_size);
	}
}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
int nvt_get_channel_data(void *private_data,
			u32 type, u8 **ptr, u32 *size)
{
	int ret = 0;
	struct nvt_ts_data *ts = (struct nvt_ts_data *)private_data;
	uint8_t *spi_buf = NULL;
	uint32_t spi_buf_size = 0;
	uint32_t spi_read_size = 0;

	switch (ts->heatmap_data_type) {
	case HEATMAP_DATA_TYPE_TOUCH_RAWDATA:
	case HEATMAP_DATA_TYPE_TOUCH_BASELINE:
		spi_buf = ts->extra_spi_buf;
		spi_buf_size = ts->extra_spi_buf_size;
		spi_read_size = spi_buf_size;
		break;
	case HEATMAP_DATA_TYPE_TOUCH_STRENGTH:
		spi_buf = ts->heatmap_spi_buf;
		spi_buf_size = ts->heatmap_spi_buf_size;
		spi_read_size = spi_buf_size;
		break;
	case HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP:
		spi_buf = ts->heatmap_spi_buf;
		spi_buf_size = ts->heatmap_spi_buf_size;
		/* Need to read extra 1 byte for SPI header. */
		spi_read_size = ts->touch_heatmap_comp_len + 1;
		break;
	default:
		break;
	}

	if (ts->heatmap_data_type == HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP &&
		ts->touch_heatmap_comp_len == NVT_HEATMAP_COMP_NOT_READY_SIZE) {
		NVT_DBG("Heatmap compression is not ready!\n");
		return -ENODATA;
	}

	if (!spi_buf || !spi_buf_size || spi_read_size > spi_buf_size) {
		NVT_ERR("buffer is not ready for heatmap(%d) or invalid size(%d > %d)!\n",
			ts->heatmap_data_type, spi_read_size, spi_buf_size);
		return -ENODATA;
	}

	/* Only support mutual strength data currently. */
	if (type & TOUCH_SCAN_TYPE_MUTUAL) {
		if (type & TOUCH_DATA_TYPE_STRENGTH)
			nvt_set_heatmap_host_cmd(ts, false);
		else
			ret = -ENODATA;
	} else {
		ret = -ENODATA;
	}

	if (ret) {
		NVT_DBG("unsupported data request(type 0x%x)!\n", type);
		return ret;
	}

	if (spi_read_size) {
		/* Skip 1 byte header to the data start. */
		uint8_t *data = spi_buf + 1;
		uint32_t data_size = spi_read_size - 1;

		nvt_set_page(ts->heatmap_host_cmd_addr);
		spi_buf[0] = ts->heatmap_host_cmd_addr & 0x7F;
		CTP_SPI_READ(ts->client, spi_buf, spi_read_size);
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

		if (ts->heatmap_data_type == HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP) {
			nvt_heatmap_decode(data, data_size,
					ts->heatmap_out_buf, ts->heatmap_out_buf_size);
			*ptr = ts->heatmap_out_buf;
			*size = ts->heatmap_out_buf_size;
		} else {
			*ptr = data;
			*size = data_size;
		}
	} else {
		NVT_ERR("invalid size for SPI read(type: %d)!\n", ts->heatmap_data_type);
	}

	return ret;
}

int nvt_test_mode_read(struct nvt_ts_data *ts, struct gti_sensor_data_cmd *cmd)
{
	char trace_tag[128];
	int ret = 0;

	scnprintf(trace_tag, sizeof(trace_tag), "%s: type=%#x\n",
		__func__, cmd->type);
	ATRACE_BEGIN(trace_tag);

	NVT_DBG("++\n");
	if (mutex_lock_interruptible(&ts->lock)) {
		ret = -ERESTARTSYS;
		goto err_read;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		ret = -EAGAIN;
		goto err_read;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		ret = -EAGAIN;
		goto err_read;
	}

	if (nvt_get_fw_info()) {
		ret = -EAGAIN;
		goto err_read;
	}

	switch (cmd->type) {
	case GTI_SENSOR_DATA_TYPE_MS_RAW:
		if (nvt_get_fw_pipe() == 0)
			nvt_read_mdata(ts->mmap->RAW_PIPE0_ADDR, ts->mmap->RAW_BTN_PIPE0_ADDR);
		else
			nvt_read_mdata(ts->mmap->RAW_PIPE1_ADDR, ts->mmap->RAW_BTN_PIPE1_ADDR);
		break;
	case GTI_SENSOR_DATA_TYPE_MS_BASELINE:
		nvt_read_mdata(ts->mmap->BASELINE_ADDR, ts->mmap->BASELINE_BTN_ADDR);
		break;
	case GTI_SENSOR_DATA_TYPE_MS_DIFF:
		if (nvt_get_fw_pipe() == 0)
			nvt_read_mdata(ts->mmap->DIFF_PIPE0_ADDR, ts->mmap->DIFF_BTN_PIPE0_ADDR);
		else
			nvt_read_mdata(ts->mmap->DIFF_PIPE1_ADDR, ts->mmap->DIFF_BTN_PIPE1_ADDR);
		break;
	default:
		NVT_ERR("invalid type %#x.\n", cmd->type);
		ret = -ENODATA;
		break;
	}

err_read:
	nvt_change_mode(NORMAL_MODE);
	if (ret == -EAGAIN) {
		NVT_LOG("Reload FW to recover unexcepted return!");
#if SPI_FLASH
		nvt_clear_fw_reset_state();
		nvt_bootloader_reset();
		if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN)) {
			mutex_unlock(&ts->lock);
			NVT_ERR("check fw reset state failed!\n");
			return -EAGAIN;
		}
#else
		nvt_update_firmware(get_fw_name(), 1);
#endif // SPI_FLASH
	}
	mutex_unlock(&ts->lock);
	NVT_DBG("--, ret(%d)\n", ret);

	ATRACE_END();
	return ret;
}


int nvt_callback(void *private_data,
		enum gti_cmd_type cmd_type, struct gti_union_cmd_data *cmd)
{
	int ret = -EOPNOTSUPP;
	struct nvt_ts_data *ts = (struct nvt_ts_data *)private_data;
	static bool sensing_enabled = true;
	static bool display_state_on = true;

	switch (cmd_type) {
	case GTI_CMD_PING:
		ret = -EOPNOTSUPP;
		break;

	case GTI_CMD_RESET:
#if SPI_FLASH
		nvt_clear_fw_reset_state();
		nvt_bootloader_reset();
		ret = nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN);
		if (ret) {
			mutex_unlock(&ts->lock);
			NVT_ERR("check fw reset state failed!\n");
			ret = -EAGAIN;
		}
#else
		ret = nvt_update_firmware(get_fw_name(), 1);
#endif // SPI_FLASH
		break;

	case GTI_CMD_SELFTEST: {
		char *buf = cmd->selftest_cmd.buffer;
		size_t size = sizeof(cmd->selftest_cmd.buffer);

		ret = nvt_selftest();
		if (ret) {
			NVT_LOGE("nvt_selftest abort, ret=%d!\n", ret);
		} else {
			if (ts->selftest_failed_result)
				cmd->selftest_cmd.result = GTI_SELFTEST_RESULT_FAIL;
			else
				cmd->selftest_cmd.result = GTI_SELFTEST_RESULT_PASS;
			nvt_print_selftest_result(buf, size);
		}
	}
		break;

	case GTI_CMD_GET_CONTEXT_DRIVER:
		cmd->context_driver_cmd.context_changed.screen_state = 1;
		cmd->context_driver_cmd.screen_state =
				ts->bTouchIsAwake ? 1 : 0;
#ifdef DYNAMIC_REFRESH_RATE
		cmd->context_driver_cmd.context_changed.display_refresh_rate = 1;
		cmd->context_driver_cmd.display_refresh_rate =
				ts->display_refresh_rate;
#endif
		/* Fixed touch report rate and no update event */
		cmd->context_driver_cmd.context_changed.touch_report_rate = 1;
		cmd->context_driver_cmd.touch_report_rate = 120;
		ret = 0;
		break;

	case GTI_CMD_GET_CONTEXT_STYLUS:
		cmd->context_stylus_cmd.contents.coords = 1;
		cmd->context_stylus_cmd.pen_offload_coord =
				ts->pen_offload_coord;

		cmd->context_stylus_cmd.contents.coords_timestamp = 1;
		cmd->context_stylus_cmd.pen_offload_coord_timestamp =
				ts->pen_offload_coord_timestamp;

		cmd->context_stylus_cmd.contents.pen_active = 1;
		cmd->context_stylus_cmd.pen_active = ts->pen_active;

		/* No useful pen-pairing information available in this driver */
		cmd->context_stylus_cmd.contents.pen_paired = 0;
		ret = 0;
		break;

	case GTI_CMD_GET_FW_VERSION: {
		int buf_idx = 0;
		char *buf = cmd->fw_version_cmd.buffer;
		size_t size = sizeof(cmd->fw_version_cmd.buffer);

		buf_idx += scnprintf(buf + buf_idx, size, "\n");
		buf_idx += scnprintf(buf + buf_idx, size,
			"fw_ver=0x%02X, x_num=%d, y_num=%d, button_num=%d\n",
			ts->fw_ver, ts->x_num, ts->y_num, ts->max_button_num);
		buf_idx += scnprintf(buf + buf_idx, size,
			"id= 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
			ts->trim_table->id[0], ts->trim_table->id[1],
			ts->trim_table->id[2], ts->trim_table->id[3],
			ts->trim_table->id[4], ts->trim_table->id[5]);
#if !SPI_FLASH
		buf_idx += scnprintf(buf + buf_idx, size, "mp_fw_name= %s\n", get_mp_fw_name());
#endif // !SPI_FLASH
		buf_idx += scnprintf(buf + buf_idx, size, "fw_name= %s\n", get_fw_name());
		ret = 0;
		NVT_LOG("GTI_CMD_GET_FW_VERSION.\n");
	}
		break;

	case GTI_CMD_GET_IRQ_MODE:
		cmd->irq_cmd.setting = (ts->irq_enabled) ?
				GTI_IRQ_MODE_ENABLE : GTI_IRQ_MODE_DISABLE;
		ret = 0;
		break;

	case GTI_CMD_GET_SENSING_MODE:
		cmd->sensing_cmd.setting = (sensing_enabled) ?
				GTI_SENSING_MODE_ENABLE : GTI_SENSING_MODE_DISABLE;
		ret = 0;
		break;

	case GTI_CMD_GET_SENSOR_DATA:
		if (cmd->sensor_data_cmd.type & TOUCH_SENSOR_DATA_READ_METHOD_INT) {
			ret = nvt_get_channel_data(ts, cmd->sensor_data_cmd.type,
				&cmd->sensor_data_cmd.buffer, &cmd->sensor_data_cmd.size);
		}
		break;

	case GTI_CMD_GET_SENSOR_DATA_MANUAL:
		if (display_state_on == false ||
			!(cmd->manual_sensor_data_cmd.type & TOUCH_SCAN_TYPE_MUTUAL)) {
			ret = -ENODATA;
		} else {
			int16_t *out = (int16_t *)ts->extra_spi_buf;
			int out_sz = ts->x_num * ts->y_num * sizeof(int16_t);
			int32_t *in = NULL;
			int in_sz = 0;

			nvt_get_xdata_info(&in, &in_sz);
			if (in && out &&
				out_sz <= in_sz &&
				out_sz <= ts->extra_spi_buf_size) {
				int i, j;
				int idx = 0;

				ret = nvt_test_mode_read(ts, &cmd->manual_sensor_data_cmd);
				if (!ret) {
					for (i = 0; i < ts->y_num; i++)
						for (j = 0; j < ts->x_num; j++)
							out[idx++] = (int16_t)in[i * ts->x_num + j];

					cmd->manual_sensor_data_cmd.buffer = ts->extra_spi_buf;
					cmd->manual_sensor_data_cmd.size = out_sz;
				}
			}
		}
		break;

	case GTI_CMD_SET_CONTINUOUS_REPORT: {
		#define CONTINUOUS_ENABLE  0x01
		#define CONTINUOUS_DISABLE 0x00
		uint8_t spi_buf[3] = {EVENT_MAP_HOST_CMD, 0x70, CONTINUOUS_DISABLE};
		uint8_t fw_cmd = CONTINUOUS_DISABLE;

		if (cmd->continuous_report_cmd.setting == GTI_CONTINUOUS_REPORT_ENABLE)
			fw_cmd = CONTINUOUS_ENABLE;
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
		spi_buf[2] = fw_cmd;
		CTP_SPI_WRITE(ts->client, spi_buf, sizeof(spi_buf));
		ret = 0;
		NVT_DBG("continuous report %s.\n",
				(fw_cmd == CONTINUOUS_ENABLE) ? "enable" : "disable");
	}
		break;

	case GTI_CMD_SET_IRQ_MODE:
		if (cmd->irq_cmd.setting == GTI_IRQ_MODE_DISABLE)
			nvt_irq_enable(false);
		else
			nvt_irq_enable(true);
		ret = 0;
		break;

	case GTI_CMD_SET_SENSING_MODE:
		if (ts->fw_update_in_process == false) {
			NVT_LOGI("set sensing %s.\n", cmd->sensing_cmd.setting ? "ON" : "OFF");
			if (cmd->sensing_cmd.setting == GTI_SENSING_MODE_DISABLE) {
				uint8_t spi_buf[3] = {0};

				ret = 0;
				if (sensing_enabled) {
					spi_buf[0] = EVENT_MAP_HOST_CMD;
					spi_buf[1] = 0x12;
					CTP_SPI_WRITE(ts->client, spi_buf, 3);
					msleep(20);
					spi_buf[0] = EVENT_MAP_HOST_CMD;
					spi_buf[1] = 0xFF;
					CTP_SPI_READ(ts->client, spi_buf, 3);
					ret = (spi_buf[1] == 0) ? 0 : -EIO;
					sensing_enabled = false;
				}
			} else {
#if SPI_FLASH
				nvt_clear_fw_reset_state();
				nvt_bootloader_reset();
				if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN)) {
					mutex_unlock(&ts->lock);
					NVT_ERR("check fw reset state failed!\n");
					ret = -EAGAIN;
				}
#else
				ret = nvt_update_firmware(get_fw_name(), 1);
#endif // SPI_FLASH
				sensing_enabled = true;
			}
		} else {
			NVT_LOGD("skip GTI_CMD_SET_SENSING_MODE ops during fw update!\n");
		}
		break;

	case GTI_CMD_NOTIFY_DISPLAY_STATE:
		if (cmd->display_state_cmd.setting == GTI_DISPLAY_STATE_OFF) {
			/*
			 * Need to have post-delay for touch FW to complete before return
			 * to display driver after GTI scheduled the suspend workqueue.
			 */
			if (display_state_on)
				msleep(NVT_SUSPEND_POST_MS_DELAY);
			NVT_LOG("GTI_DISPLAY_STATE_OFF\n");
			display_state_on = false;
		} else if (cmd->display_state_cmd.setting == GTI_DISPLAY_STATE_ON) {
			u32 locks = goog_pm_wake_get_locks(ts->gti);

			/*
			 * If driver skipped to suspend to keep bus active, needs to reenable
			 * driver for touch functionality because display will power
			 * off during suspend.
			 */
			if (ts->bTouchIsAwake &&
				((locks & GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE) ||
				 (locks & GTI_PM_WAKELOCK_TYPE_BUGREPORT))) {
				NVT_LOG("reenable touch for locks %#x.", locks);
				nvt_ts_suspend(&ts->client->dev);
				nvt_ts_resume(&ts->client->dev);
				sensing_enabled = true;
			}
			NVT_LOG("GTI_DISPLAY_STATE_ON");
			display_state_on = true;
		} else {
			NVT_ERR("invalid setting %d!\n", cmd->display_state_cmd.setting);
		}
		break;

	case GTI_CMD_NOTIFY_DISPLAY_VREFRESH:
		ret = 0;
		break;

#if SPI_FLASH
	case GTI_CMD_SET_HEATMAP_ENABLED:
		nvt_set_heatmap_host_cmd(ts, true);
		ret = 0;
		break;
#endif

	default:
		NVT_DBG("unsupported request cmd_type %#x!\n", cmd_type);
		ret = -EOPNOTSUPP;
		break;
	}

	return ret;
}

#endif /* CONFIG_GOOG_TOUCH_INTERFACE */

#if defined(CONFIG_SOC_GOOGLE)
bool nvt_ts_check_tid(struct nvt_ts_data *ts, u8 *tid)
{
	if (tid == NULL || ts == NULL ||
	    ts->trim_table == NULL)
		return false;

	if (memcmp(ts->trim_table->id, tid, NVT_ID_BYTE_MAX))
		return false;
	else
		return true;
}

ssize_t force_touch_active_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int32_t ret = 0;

	NVT_LOGD("++\n");

	ret = sysfs_emit(buf, "%#x\n",
		goog_pm_wake_get_locks(ts->gti));

	NVT_LOGD("--\n");
	return ret;
}

ssize_t force_touch_active_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 mode;
	int __maybe_unused ret;
	bool __maybe_unused active;
	u32 __maybe_unused lock = 0;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 0, &mode)) {
		NVT_ERR("invalid input!\n");
		return -EINVAL;
	}

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	switch (mode) {
	case 0x10:
		lock = GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE;
		active = false;
		break;
	case 0x11:
		lock = GTI_PM_WAKELOCK_TYPE_FORCE_ACTIVE;
		active = true;
		break;
	case 0x20:
		lock = GTI_PM_WAKELOCK_TYPE_BUGREPORT;
		active = false;
		ts->bugreport_ktime_start = 0;
		break;
	case 0x21:
		lock = GTI_PM_WAKELOCK_TYPE_BUGREPORT;
		active = true;
		ts->bugreport_ktime_start = ktime_get();
		break;
	}

	if (lock == 0) {
		NVT_ERR("invalid input %#x.\n", mode);
		return -EINVAL;
	}

	NVT_LOG("%s lock %#x\n",
		(active) ? "enable" : "disable", lock);

	if (active) {
		if (!ts->bTouchIsAwake) {
			input_report_key(ts->input_dev, KEY_WAKEUP, true);
			input_sync(ts->input_dev);
			input_report_key(ts->input_dev, KEY_WAKEUP, false);
			input_sync(ts->input_dev);
			NVT_LOG("KEY_WAKEUP triggered.\n");
		}
		pm_stay_awake(&ts->client->dev);
	} else {
		pm_relax(&ts->client->dev);
	}

	if (!ts->bTouchIsAwake)
		msleep(NVT_FORCE_ACTIVE_MS_DELAY);

	if (active)
		ret = goog_pm_wake_lock(ts->gti, lock, false);
	else
		ret = goog_pm_wake_unlock(ts->gti, lock);
	if (ret)
		NVT_ERR("failed to %s %#x(ret %d), current locks %#x!\n",
			(active) ? "lock" : "unlock",
			lock, ret, goog_pm_wake_get_locks(ts->gti));
#endif

	NVT_LOGD("--\n");
	return count;
}

ssize_t force_release_fw_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	ret = sysfs_emit(buf, "%d\n", ts->force_release_fw);

	NVT_LOGD("--\n");
	return ret;
}

ssize_t force_release_fw_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	u8 mode;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 0, &mode)) {
		NVT_ERR("invalid input!\n");
		return -EINVAL;
	}

	ts->force_release_fw = (mode) ? 1 : 0;
	if (ts->force_release_fw)
		update_firmware_release();

	NVT_LOGD("--\n");
	return count;
}

int nvt_ts_pm_suspend(struct device *dev)
{
	struct nvt_ts_data *ts = dev_get_drvdata(dev);
	u32 locks = 0;

	locks = goog_pm_wake_get_locks(ts->gti);
	NVT_LOGD("locks %#x\n", locks);

	if (ts->bTouchIsAwake) {
		NVT_ERR("can't suspend because touch bus is in use, locks %#x!\n",
			locks);
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		if (locks & GTI_PM_WAKELOCK_TYPE_BUGREPORT) {
			s64 delta_ms = ktime_ms_delta(ktime_get(),
							ts->bugreport_ktime_start);
			if (delta_ms > 30 * MSEC_PER_SEC) {
				goog_pm_wake_unlock(ts->gti, GTI_PM_WAKELOCK_TYPE_BUGREPORT);
				pm_relax(&ts->client->dev);
				ts->bugreport_ktime_start = 0;
				NVT_ERR("force release NVT_BUS_REF_BUGREPORT(delta: %lld)!\n",
					delta_ms);
			}
		}
#endif
		return -EBUSY;
	}

	return 0;
}

int nvt_ts_pm_resume(struct device *dev)
{
	return 0;
}

#endif /* defined(CONFIG_SOC_GOOGLE) */
