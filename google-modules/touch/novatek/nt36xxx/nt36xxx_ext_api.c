// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 - 2021 Novatek, Inc.
 *
 * $Revision: 83893 $
 * $Date: 2021-08-11 10:52:25 +0800 (週一, 21 六月 2021) $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include "nt36xxx.h"

#if NVT_TOUCH_EXT_API
#define PLAYBACK_RAWDATA_ADDR             0x26238
#define GET_CALIBRATION_ADDR              0x2B31A
#define GET_GRIP_LEVEL_ADDR               0x2B31B
#define GET_HM_TOUCH_TH_ADDR              0x2B31C
#define DTTW_TOUCH_AREA_MIN_ADDR          0x2B36A
#define DTTW_TOUCH_AREA_MAX_ADDR          0x2B36C
#define DTTW_CONTACT_DURATION_MIN_ADDR    0x2B36E
#define DTTW_CONTACT_DURATION_MAX_ADDR    0x2B370
#define DTTW_TAP_OFFSET_ADDR              0x2B372
#define DTTW_TAP_GAP_DURATION_MIN_ADDR    0x2B374
#define DTTW_TAP_GAP_DURATION_MAX_ADDR    0x2B376
#define DTTW_MOTION_TOLERANCE_ADDR        0x2B378
#define DTTW_DETECTION_WINDOW_EDGE_ADDR   0x2B37A
#define GET_MODE_HISTORY_ADDR             0x2B32A
#define TOUCH_CMD_STATUS_ADDR             0x2FE5C
#define PLAYBACK_DIFFDATA_ADDR            0x373E8
#define TOUCH_MODE_ADDR                   0x38D33

#define RAWDATA_UNIFORMITY_LIMIT          200
#define INPUT_TYPE_RAWDATA                1
#define INPUT_TYPE_DIFFDATA               2
#define SYNC_FREQ                         "120Hz"
#define LAST_ROUND_POS                    12288

#define PALM_MODE_CMD_TEST_BIT            BIT(0)
#define HIGH_SENSI_MODE_CMD_TEST_BIT      BIT(1)
#define HOLSTER_MODE_CMD_TEST_BIT         BIT(3)
#define TOUCH_IDLE_MODE_CMD_TEST_BIT      BIT(4)
#define ER_MODE_CMD_TEST_BIT              (BIT(5) | BIT(6))
#define CONT_REPORT_MODE_CMD_TEST_BIT     BIT(9)
#define NOISE_MODE_CMD_TEST_BIT           BIT(10)
#define WATER_MODE_CMD_TEST_BIT           BIT(11)
#define DTTW_MODE_CMD_TEST_BIT            BIT(12)
#define GRIP_LEVEL_CMD_TEST_BIT           BIT(13)
#define SET_CANCEL_CMD_TEST_BIT           BIT(14)
#define PLAYBACK_MODE_CMD_TEST_BIT        BIT(15)

#define TOUCH_HEATMAP_TH_LVL_SCALE 4
#define TOUCH_HEATMAP_TH_MIN 32
#define TOUCH_HEATMAP_TH_MAX 92
#define TOUCH_HEATMAP_TH_BASE TOUCH_HEATMAP_TH_MIN

uint32_t cc_uniformity_spi_buf_size;
uint32_t rawdata_uniformity_spi_buf_size;
uint32_t playback_spi_buf_size;
uint8_t *cc_uniformity_spi_buf;
uint8_t *rawdata_uniformity_spi_buf;
uint8_t *playback_spi_buf;
uint8_t *playback_spi_buf_offset;
uint32_t playback_addr;
uint8_t  playback_enabled;
uint8_t  grip_level;

ssize_t nvt_check_api_cmd_result(uint16_t cmd_test_bit, uint16_t pattern)
{
	uint8_t spi_buf[3] = {0}, shift = 0;
	uint16_t result;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	spi_buf[0] = TOUCH_CMD_STATUS_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 3);
	result = ((uint16_t)(spi_buf[2] << 8) | spi_buf[1]) & cmd_test_bit;
	while ((cmd_test_bit & 1) == 0) {
		cmd_test_bit = cmd_test_bit >> 1;
		shift += 1;
	}
	if (result != pattern << shift)
		return -EINVAL;
	return 0;
}

ssize_t nvt_get_api_status(uint16_t cmd_get_bit)
{
	uint8_t spi_buf[3] = {0}, shift = 0;
	uint16_t status;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	spi_buf[0] = TOUCH_CMD_STATUS_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 3);
	status = ((uint16_t)(spi_buf[2] << 8) | spi_buf[1]) & cmd_get_bit;
	while ((cmd_get_bit & 1) == 0) {
		cmd_get_bit = cmd_get_bit >> 1;
		shift += 1;
	}
	status = status >> shift;
	return status;
}

static ssize_t nvt_get_mode_history_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint8_t spi_buf[65] = {0};
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(GET_MODE_HISTORY_ADDR);
	spi_buf[0] = GET_MODE_HISTORY_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 65);
	ret = sysfs_emit(buf, "%*ph\n", 64, &spi_buf[1]);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_palm_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint16_t cmd_get_bit = PALM_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%zd\n", nvt_get_api_status(cmd_get_bit));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_palm_mode_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = PALM_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Palm Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB3;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Palm Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB4;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_high_sensi_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint16_t cmd_get_bit = HIGH_SENSI_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%zd\n", nvt_get_api_status(cmd_get_bit));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_high_sensi_mode_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = HIGH_SENSI_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable High Sensitivity Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB1;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable High Sensitivity Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB2;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_touch_idle_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint32_t mode;
	uint8_t spi_buf[2] = {0};
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(TOUCH_MODE_ADDR);
	spi_buf[0] = TOUCH_MODE_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 2);
	mode = spi_buf[1];
	switch (mode) {
	case 0x3:
		NVT_LOG("normal active mode\n"); // Active mode
		ret = sysfs_emit(buf, "%s\n", "Normal_Active");
		break;
	case 0x4:
	case 0x6:
		NVT_LOG("normal idle mode\n"); // Idle mode
		ret = sysfs_emit(buf, "%s\n", "Normal_Idle");
		break;
	case 0xA:
		NVT_LOG("low power active mode\n"); // WKG mode
		ret = sysfs_emit(buf, "%s\n", "LowPower_Active");
		break;
	case 0x9:
	case 0xB:
		NVT_LOG("low power idle mode\n"); // FDM mode
		ret = sysfs_emit(buf, "%s\n", "LowPower_Idle");
		break;
	}
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_touch_idle_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = TOUCH_IDLE_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Normal/LowPower Idle Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB7;
		CTP_SPI_WRITE(ts->client, spi_buf, 2);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Normal/LowPower Idle Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xB8;
		CTP_SPI_WRITE(ts->client, spi_buf, 2);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_heatmap_data_type_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret = 0;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret += sysfs_emit(buf, "%d\n", ts->heatmap_data_type);
	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_heatmap_data_type_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	uint8_t type;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &type) || type >= HEATMAP_DATA_TYPE_UNSUPPORTED)
		return -EINVAL;

	if (type == HEATMAP_DATA_TYPE_PEN_STRENGTH_COMP) {
		/*
		 * TODO(b/219658467):
		 * Need to check with vendor for pen strength compression support
		 * in the future.
		 */
		NVT_ERR("heatmap does not support pen strength comp!\n");
		return -EINVAL;
	}

	if (!ts->heatmap_spi_buf) {
		ts->heatmap_spi_buf_size = ts->x_num * ts->y_num * 2 + 1;
		ts->heatmap_spi_buf = devm_kzalloc(&ts->client->dev,
				ts->heatmap_spi_buf_size, GFP_KERNEL);
		if (!ts->heatmap_spi_buf)
			return count;
	}

	NVT_LOG("switch type to %d.\n", type);
	if (type == HEATMAP_DATA_TYPE_PEN_STRENGTH_COMP) {
		ts->heatmap_host_cmd_addr = HEATMAP_PEN_ADDR;
	} else {
		ts->heatmap_host_cmd_addr = HEATMAP_TOUCH_ADDR;
		nvt_set_heatmap_host_cmd(ts, true);
	}

	ts->heatmap_data_type = type;
	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_heatmap_touch_threshold_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint8_t spi_buf[2] = {0};
	int32_t ret = 0;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(GET_HM_TOUCH_TH_ADDR);
	spi_buf[0] = GET_HM_TOUCH_TH_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, sizeof(spi_buf));
	ret += sysfs_emit(buf, "%d\n", spi_buf[1]);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_heatmap_touch_threshold_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {EVENT_MAP_HOST_CMD, 0x70, 0};
	uint8_t hm_touch_th = 0, hm_touch_th_lvl = 0;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &hm_touch_th) ||
		hm_touch_th < TOUCH_HEATMAP_TH_MIN ||
		hm_touch_th > TOUCH_HEATMAP_TH_MAX) {
		NVT_ERR("unsupported input(%d), should be from %d to %d!\n",
		hm_touch_th, TOUCH_HEATMAP_TH_MIN, TOUCH_HEATMAP_TH_MAX);
		return -EINVAL;
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	/*
	 * FW can't support precise value as threshold setting, but
	 * could support level(0~15) base adjustment that starts from
	 * the threshold(TOUCH_HEATMAP_TH_BASE: 32). And, for every
	 * level up will add 4 threshold correspondingly.
	 */
	hm_touch_th_lvl = ((hm_touch_th - TOUCH_HEATMAP_TH_BASE) /
			TOUCH_HEATMAP_TH_LVL_SCALE) & 0x0F;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	spi_buf[2] = 0x70 | hm_touch_th_lvl;
	CTP_SPI_WRITE(ts->client, spi_buf, sizeof(spi_buf));
	msleep(20);

	nvt_set_page(GET_HM_TOUCH_TH_ADDR);
	spi_buf[0] = GET_HM_TOUCH_TH_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 2);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	NVT_LOG("request %d as threshold, FW adjust to %d(lvl: %d) by design.\n",
		hm_touch_th, spi_buf[1], hm_touch_th_lvl);
	NVT_LOGD("--\n");

	return count;
}

static ssize_t nvt_cont_report_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint16_t cmd_get_bit = CONT_REPORT_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%zd\n", nvt_get_api_status(cmd_get_bit));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_cont_report_mode_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = CONT_REPORT_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable FW Continuously Report Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x01;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable FW Continuously Report Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x00;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_noise_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint16_t cmd_get_bit = NOISE_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%zd\n", nvt_get_api_status(cmd_get_bit));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_noise_mode_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = NOISE_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Noise Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x11;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Noise Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x10;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_water_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint16_t cmd_get_bit = WATER_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%zd\n", nvt_get_api_status(cmd_get_bit));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_water_mode_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = WATER_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Water Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x21;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Water Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x20;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_sw_reset_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	uint8_t mode;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode != CMD_ENABLE)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

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
	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");

	return count;
}

static ssize_t nvt_sensing_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Sensing Mode\n");
#if SPI_FLASH
		nvt_clear_fw_reset_state();
		nvt_bootloader_reset();
		ret = nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN);
		if (ret) {
			mutex_unlock(&ts->lock);
			NVT_ERR("check fw reset state failed!\n");
			return -EAGAIN;
		}
#else
		ret = nvt_update_firmware(get_fw_name(), 1);
#endif // SPI_FLASH
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Sensing Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x12;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		msleep(20);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, spi_buf, 3);
		ret = (spi_buf[1] == 0) ? 0 : -EINVAL;
		break;
	}
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_freq_hopping_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	uint8_t spi_buf[4] = {0}, mode;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > MODE_4 || (mode == 0))
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	if (nvt_switch_FreqHopEnDis(FREQ_HOP_DISABLE)) {
		mutex_unlock(&ts->lock);
		NVT_ERR("switch frequency hopping disable failed!\n");
		return -EAGAIN;
	}

	if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN)) {
		mutex_unlock(&ts->lock);
		NVT_ERR("check fw reset state failed!\n");
		return -EAGAIN;
	}

	switch (mode) {
	case MODE_1:
		NVT_LOG("Set Frequency Hopping to Mode 1\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x1B;
		spi_buf[2] = 0x01;
		spi_buf[3] = 0x01;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		break;
	case MODE_2:
		NVT_LOG("Set Frequency Hopping to Mode 2\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x1B;
		spi_buf[2] = 0x01;
		spi_buf[3] = 0x02;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		break;
	case MODE_3:
		NVT_LOG("Set Frequency Hopping to Mode 3\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x1B;
		spi_buf[2] = 0x01;
		spi_buf[3] = 0x03;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		break;
	case MODE_4:
		NVT_LOG("Set Frequency Hopping to Mode 4\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x1B;
		spi_buf[2] = 0x01;
		spi_buf[3] = 0x04;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		break;
	}

	msleep(50);
	spi_buf[1] = 0xFF;
	CTP_SPI_READ(ts->client, spi_buf, 2);
	mutex_unlock(&ts->lock);

	ret = (spi_buf[1] == 0) ? 0 : -EINVAL;
	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_grip_level_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint8_t spi_buf[2] = {0};
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(GET_GRIP_LEVEL_ADDR);
	spi_buf[0] = GET_GRIP_LEVEL_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 2);
	ret = sysfs_emit(buf, "%d\n", spi_buf[1]);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_grip_level_store(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = GRIP_LEVEL_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > MODE_4)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	switch (mode) {
	case CMD_DISABLE:
		NVT_LOG("Disable Grip Level\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x40;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		break;
	case MODE_1:
		NVT_LOG("Set Grip Level to Enable_weak\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x41;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		break;
	case MODE_2:
		NVT_LOG("Set Grip Level to Enable_Small\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x42;
		CTP_SPI_WRITE(ts->client, spi_buf, 4);
		break;
	case MODE_3:
		NVT_LOG("Set Grip Level to Enable_Medium\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x43;
		break;
	case MODE_4:
		NVT_LOG("Set Grip Level to Enable_Strong\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x44;
		break;
	}
	mutex_unlock(&ts->lock);

	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode > 0);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		grip_level = mode;
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_force_calibration_store(struct device *dev,
		struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode != CMD_ENABLE)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	NVT_LOG("Force Calibration\n");
	spi_buf[0] = EVENT_MAP_HOST_CMD;
	spi_buf[1] = 0x23;
	spi_buf[2] = 0x00;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	spi_buf[0] = EVENT_MAP_HOST_CMD;
	spi_buf[1] = 0xFF;
	CTP_SPI_READ(ts->client, spi_buf, 3);
	ret = (spi_buf[1] == 0) ? 0 : -EINVAL;

	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_get_calibration_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	int32_t ret;
	uint8_t spi_buf[2] = {0};

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(GET_CALIBRATION_ADDR);
	spi_buf[0] = GET_CALIBRATION_ADDR & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 2);
	ret = sysfs_emit(buf, "%d\n", spi_buf[1]);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_sync_freq_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");
	ret = sysfs_emit(buf, "%s\n", SYNC_FREQ);
	NVT_LOGD("--\n");

	return ret;
}

void cal_uniformity(uint8_t *arr, uint32_t size)
{
	uint8_t is_right_most, is_bottom;
	uint16_t res, i;

	for (i = 1; i < size; i += 2) {
		is_right_most = ((i + 1) % (ts->x_num  * 2) == 0);
		is_bottom = (size - i <= (ts->x_num * 2));
		if (!is_right_most && !is_bottom)
			res = ((abs(((uint16_t)arr[i + 1] << 8) + arr[i] \
				    - ((uint16_t)arr[i + 3] << 8) - arr[i + 2]) \
				+ abs(((uint16_t)arr[i + 1] << 8) + arr[i] \
				      - ((uint16_t)arr[i + 1 + (ts->x_num * 2)] << 8) - arr[i +
						      (ts->x_num * 2)])) / 2);
		else if (is_right_most && !is_bottom)
			res = abs(((uint16_t)arr[i + 1] << 8) + arr[i] \
				  - ((uint16_t)arr[i + 1 + (ts->x_num * 2)] << 8) - arr[i +
						  (ts->x_num * 2)]);
		else if (!is_right_most && is_bottom)
			res = abs(((uint16_t)arr[i + 1] << 8) + arr[i] \
				  - ((uint16_t)arr[i + 3] << 8) - arr[i + 2]);
		else
			res = 0;
		memcpy(&arr[i], &res, 2);
	}
}

static int32_t nvt_get_rawdata_uniformity(void)
{
	NVT_LOGD("++\n");

	if (!rawdata_uniformity_spi_buf) {
		rawdata_uniformity_spi_buf_size = ts->x_num * ts->y_num * 2 + 1;
		rawdata_uniformity_spi_buf = kzalloc(rawdata_uniformity_spi_buf_size,
						     GFP_KERNEL);
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0) {
		nvt_set_page(ts->mmap->RAW_PIPE0_ADDR);
		rawdata_uniformity_spi_buf[0] = ts->mmap->RAW_PIPE0_ADDR & 0x7F;
	} else {
		nvt_set_page(ts->mmap->RAW_PIPE1_ADDR);
		rawdata_uniformity_spi_buf[0] = ts->mmap->RAW_PIPE1_ADDR & 0x7F;
	}

	CTP_SPI_READ(ts->client, rawdata_uniformity_spi_buf,
		     rawdata_uniformity_spi_buf_size);

	nvt_change_mode(NORMAL_MODE);
	mutex_unlock(&ts->lock);

	cal_uniformity(rawdata_uniformity_spi_buf,
		       rawdata_uniformity_spi_buf_size);

	NVT_LOGD("--\n");
	return 0;
}

static int32_t nvt_get_cc_uniformity(void)
{
	NVT_LOGD("++\n");

	if (!cc_uniformity_spi_buf) {
		cc_uniformity_spi_buf_size = ts->x_num * ts->y_num * 2 + 1;
		cc_uniformity_spi_buf = kzalloc(cc_uniformity_spi_buf_size, GFP_KERNEL);
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;
#if !SPI_FLASH
	nvt_update_firmware(get_mp_fw_name(), 1);
#endif // !SPI_FLASH
	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		NVT_ERR("get fw info failed!\n");
		return -EAGAIN;
	}

#if SPI_FLASH
    if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN)) {
#else
    if (nvt_check_fw_reset_state(RESET_STATE_REK)) {
#endif // SPI_FLASH
		mutex_unlock(&ts->lock);
		NVT_ERR("check fw reset state failed!\n");
		return -EAGAIN;
	}

	if (nvt_switch_FreqHopEnDis(FREQ_HOP_DISABLE)) {
		mutex_unlock(&ts->lock);
		NVT_ERR("switch frequency hopping disable failed!\n");
		return -EAGAIN;
	}

	if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN)) {
		mutex_unlock(&ts->lock);
		NVT_ERR("check fw reset state failed!\n");
		return -EAGAIN;
	}

	msleep(100);

	//---Enter Test Mode---
	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		NVT_ERR("clear fw status failed!\n");
		return -EAGAIN;
	}

	nvt_change_mode(MP_MODE_CC);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		NVT_ERR("check fw status failed!\n");
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0) {
		nvt_set_page(ts->mmap->DIFF_PIPE1_ADDR);
		cc_uniformity_spi_buf[0] = ts->mmap->DIFF_PIPE1_ADDR & 0x7F;
	} else {
		nvt_set_page(ts->mmap->DIFF_PIPE0_ADDR);
		cc_uniformity_spi_buf[0] = ts->mmap->DIFF_PIPE0_ADDR & 0x7F;
	}

	CTP_SPI_READ(ts->client, cc_uniformity_spi_buf,
		     cc_uniformity_spi_buf_size);

	nvt_change_mode(NORMAL_MODE);
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
	mutex_unlock(&ts->lock);

	cal_uniformity(cc_uniformity_spi_buf, cc_uniformity_spi_buf_size);

	NVT_LOGD("--\n");
	return 0;
}

static ssize_t nvt_verify_calibration_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t i, ret = 0, max = 0;

	NVT_LOGD("++\n");

	if (nvt_get_rawdata_uniformity())
		return -EAGAIN;

	for (i = 1; i < rawdata_uniformity_spi_buf_size; i += 2) {
		if (((uint16_t)rawdata_uniformity_spi_buf[i + 1] << 8) +
		    rawdata_uniformity_spi_buf[i] > max)
			max = ((uint16_t)rawdata_uniformity_spi_buf[i + 1] << 8) +
			      rawdata_uniformity_spi_buf[i];
	}

	if (max > RAWDATA_UNIFORMITY_LIMIT)
		ret = sysfs_emit(buf, "%s\n", "Fail");
	else
		ret = sysfs_emit(buf, "%s\n", "Pass");

	NVT_LOG("max rawdata deviation = %d\n", max);

	NVT_LOGD("--\n");

	return ret;
}

static ssize_t nvt_cancel_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint16_t cmd_get_bit = SET_CANCEL_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%zd\n", nvt_get_api_status(cmd_get_bit));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_cancel_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0}, mode;
	uint16_t cmd_test_bit = SET_CANCEL_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > CMD_ENABLE)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_ENABLE:
		NVT_LOG("Enable Cancel Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x51;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case CMD_DISABLE:
		NVT_LOG("Disable Cancel Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x50;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	}
	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}

static ssize_t nvt_playback_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	uint16_t cmd_get_bit = PLAYBACK_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%zd\n", nvt_get_api_status(cmd_get_bit));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_playback_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[7] = {0}, mode;
	uint16_t cmd_test_bit = PLAYBACK_MODE_CMD_TEST_BIT;
	int32_t ret;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode) || mode > MODE_2)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	if (!playback_spi_buf) {
		playback_spi_buf_size = ts->x_num * ts->y_num * 2 + 1;
		playback_spi_buf = kzalloc(playback_spi_buf_size, GFP_KERNEL);
	}

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	switch (mode) {
	case CMD_DISABLE:
		NVT_LOG("Disable Playback Mode\n");
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x00;
		spi_buf[2] = 0xBB;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		msleep(20);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x60;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		break;
	case MODE_1:
		NVT_LOG("Playback Raw Data Mode\n");
		playback_addr = PLAYBACK_RAWDATA_ADDR;
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x61;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		msleep(20);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x25;
		spi_buf[2] = 0x00;
		spi_buf[3] = 0x00;
		spi_buf[4] = 0x00;
		spi_buf[5] = 0x01;
		spi_buf[6] = 0x00;
		CTP_SPI_WRITE(ts->client, spi_buf, 7);
		break;
	case MODE_2:
		NVT_LOG("Playback Diff Data Mode\n");
		playback_addr = PLAYBACK_DIFFDATA_ADDR;
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x62;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
		msleep(20);
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x25;
		spi_buf[2] = 0x00;
		spi_buf[3] = 0x00;
		spi_buf[4] = 0x00;
		spi_buf[5] = 0x01;
		spi_buf[6] = 0x00;
		CTP_SPI_WRITE(ts->client, spi_buf, 7);
		break;
	}

	msleep(20);
	ret = nvt_check_api_cmd_result(cmd_test_bit, mode > 0);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	}

	playback_enabled = (mode == CMD_DISABLE ? 0 : 1);
	NVT_LOGD("--\n");
	return count;
}

static uint16_t nvt_get_dttw_para(uint64_t dttw_addr)
{
	uint8_t spi_buf[3] = {0};

	nvt_set_page(dttw_addr);
	spi_buf[0] = dttw_addr & 0x7F;
	CTP_SPI_READ(ts->client, spi_buf, 3);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	return ((uint16_t)(spi_buf[2] << 8) + spi_buf[1]);
}

void nvt_set_dttw(bool check_result)
{
	uint8_t spi_buf[3] = {0};
	uint16_t cmd_test_bit = DTTW_MODE_CMD_TEST_BIT;
	int32_t ret = 0;

	if (ts->wkg_default != WAKEUP_GESTURE_DTTW)
		return;

	NVT_LOGD("++\n");
	if (ts->wkg_option == WAKEUP_GESTURE_DTTW) {
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x31;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
	} else {
		spi_buf[0] = EVENT_MAP_HOST_CMD;
		spi_buf[1] = 0x70;
		spi_buf[2] = 0x30;
		CTP_SPI_WRITE(ts->client, spi_buf, 3);
	}
	msleep(20);

	if (check_result) {
		ret = nvt_check_api_cmd_result(cmd_test_bit,
			(ts->wkg_option != WAKEUP_GESTURE_OFF) ? 1 : 0);
		if (ret) {
			NVT_ERR("DTTW conf: failed to setup, ret = %d.\n", ret);
			return;
		}
	}

	if (ts->wkg_option == WAKEUP_GESTURE_DTTW) {
		NVT_LOG("DTTW conf: area max/min %d %d, contact max/min %d %d.\n",
			ts->dttw_touch_area_max, ts->dttw_touch_area_min,
			ts->dttw_contact_duration_max, ts->dttw_contact_duration_min);
		NVT_LOG("DTTW conf: tap offset %d, gap max/min %d %d.\n",
			ts->dttw_tap_offset,
			ts->dttw_tap_gap_duration_max, ts->dttw_tap_gap_duration_min);
		NVT_LOG("DTTW conf: motion %d, edge %d.\n",
			ts->dttw_motion_tolerance, ts->dttw_detection_window_edge);
	} else {
		NVT_LOG("Gesture conf: off.\n");
	}

	NVT_LOGD("--\n");
}

static ssize_t nvt_dttw_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%d\n", ts->wkg_option);

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_dttw_mode_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t mode;

	NVT_LOGD("++\n");

	if (kstrtou8(buf, 10, &mode))
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	switch (mode) {
	case CMD_DISABLE:
		ts->wkg_option = WAKEUP_GESTURE_OFF;
		NVT_LOG("Disable Gesture.\n");
		break;
	case CMD_ENABLE:
		ts->wkg_default = WAKEUP_GESTURE_DEFAULT;
		ts->wkg_option = WAKEUP_GESTURE_DEFAULT;
		NVT_LOG("Enable Default Gesture(%d).\n", ts->wkg_option);
		break;
	default:
		if (mode >= WAKEUP_GESTURE_OFF || mode <= WAKEUP_GESTURE_DTTW) {
			ts->wkg_option = mode;
			ts->wkg_default = mode;
			NVT_LOG("Enable Gesture(%d) as default.\n", mode);
		}
		break;
	}

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_dttw_touch_area_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%d\n", nvt_get_dttw_para(DTTW_TOUCH_AREA_MAX_ADDR));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_dttw_touch_area_max_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0};
	uint16_t value;
	int32_t ret;

	NVT_LOGD("++\n");

	ret = kstrtou16(buf, 10, &value);
	if (ret) {
		NVT_ERR("invalid input, ret %d.\n", ret);
		return ret;
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(DTTW_TOUCH_AREA_MAX_ADDR);
	spi_buf[0] = DTTW_TOUCH_AREA_MAX_ADDR & 0x7F;
	spi_buf[1] = value & 0xFF;
	spi_buf[2] = value >> 8;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	ret = nvt_get_dttw_para(DTTW_TOUCH_AREA_MAX_ADDR) == value ? 0 : -EINVAL;
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	}

	ts->dttw_touch_area_max = value;
	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_dttw_touch_area_min_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%d\n", nvt_get_dttw_para(DTTW_TOUCH_AREA_MIN_ADDR));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_dttw_touch_area_min_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0};
	uint16_t value;
	int32_t ret;

	NVT_LOGD("++\n");

	ret = kstrtou16(buf, 10, &value);
	if (ret) {
		NVT_ERR("invalid input, ret %d.\n", ret);
		return ret;
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(DTTW_TOUCH_AREA_MIN_ADDR);
	spi_buf[0] = DTTW_TOUCH_AREA_MIN_ADDR & 0x7F;
	spi_buf[1] = value & 0xFF;
	spi_buf[2] = value >> 8;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	ret = nvt_get_dttw_para(DTTW_TOUCH_AREA_MIN_ADDR) == value ? 0 : -EINVAL;
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	}

	ts->dttw_touch_area_min = value;
	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_dttw_contact_duration_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%d\n", nvt_get_dttw_para(DTTW_CONTACT_DURATION_MAX_ADDR));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_dttw_contact_duration_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0};
	uint16_t value;
	int32_t ret;

	NVT_LOGD("++\n");

	ret = kstrtou16(buf, 10, &value);
	if (ret) {
		NVT_ERR("invalid input, ret %d.\n", ret);
		return ret;
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(DTTW_CONTACT_DURATION_MAX_ADDR);
	spi_buf[0] = DTTW_CONTACT_DURATION_MAX_ADDR & 0x7F;
	spi_buf[1] = value & 0xFF;
	spi_buf[2] = value >> 8;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	ret = nvt_get_dttw_para(DTTW_CONTACT_DURATION_MAX_ADDR) == value ? 0 : -EINVAL;
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	}

	ts->dttw_contact_duration_max = value;
	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_dttw_contact_duration_min_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%d\n", nvt_get_dttw_para(DTTW_CONTACT_DURATION_MIN_ADDR));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_dttw_contact_duration_min_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0};
	uint16_t value;
	int32_t ret;

	NVT_LOGD("++\n");

	ret = kstrtou16(buf, 10, &value);
	if (ret) {
		NVT_ERR("invalid input, ret %d.\n", ret);
		return ret;
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(DTTW_CONTACT_DURATION_MIN_ADDR);
	spi_buf[0] = DTTW_CONTACT_DURATION_MIN_ADDR & 0x7F;
	spi_buf[1] = value & 0xFF;
	spi_buf[2] = value >> 8;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	ret = nvt_get_dttw_para(DTTW_CONTACT_DURATION_MIN_ADDR) == value ? 0 : -EINVAL;
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	}

	ts->dttw_contact_duration_min = value;
	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_dttw_tap_offset_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%d\n", nvt_get_dttw_para(DTTW_TAP_OFFSET_ADDR));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_dttw_tap_offset_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0};
	uint16_t value;
	int32_t ret;

	NVT_LOGD("++\n");

	ret = kstrtou16(buf, 10, &value);
	if (ret) {
		NVT_ERR("invalid input, ret %d.\n", ret);
		return ret;
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(DTTW_TAP_OFFSET_ADDR);
	spi_buf[0] = DTTW_TAP_OFFSET_ADDR & 0x7F;
	spi_buf[1] = value & 0xFF;
	spi_buf[2] = value >> 8;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	ret = nvt_get_dttw_para(DTTW_TAP_OFFSET_ADDR) == value ? 0 : -EINVAL;
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	}

	ts->dttw_tap_offset = value;
	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_dttw_tap_gap_duration_max_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%d\n", nvt_get_dttw_para(DTTW_TAP_GAP_DURATION_MAX_ADDR));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_dttw_tap_gap_duration_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0};
	uint16_t value;
	int32_t ret;

	NVT_LOGD("++\n");

	ret = kstrtou16(buf, 10, &value);
	if (ret) {
		NVT_ERR("invalid input, ret %d.\n", ret);
		return ret;
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(DTTW_TAP_GAP_DURATION_MAX_ADDR);
	spi_buf[0] = DTTW_TAP_GAP_DURATION_MAX_ADDR & 0x7F;
	spi_buf[1] = value & 0xFF;
	spi_buf[2] = value >> 8;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	ret = nvt_get_dttw_para(DTTW_TAP_GAP_DURATION_MAX_ADDR) == value ? 0 : -EINVAL;
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	}

	ts->dttw_tap_gap_duration_max = value;
	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_dttw_tap_gap_duration_min_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%d\n", nvt_get_dttw_para(DTTW_TAP_GAP_DURATION_MIN_ADDR));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_dttw_tap_gap_duration_min_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0};
	uint16_t value;
	int32_t ret;

	NVT_LOGD("++\n");

	ret = kstrtou16(buf, 10, &value);
	if (ret) {
		NVT_ERR("invalid input, ret %d.\n", ret);
		return ret;
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(DTTW_TAP_GAP_DURATION_MIN_ADDR);
	spi_buf[0] = DTTW_TAP_GAP_DURATION_MIN_ADDR & 0x7F;
	spi_buf[1] = value & 0xFF;
	spi_buf[2] = value >> 8;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	ret = nvt_get_dttw_para(DTTW_TAP_GAP_DURATION_MIN_ADDR) == value ? 0 : -EINVAL;
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	}

	ts->dttw_tap_gap_duration_min = value;
	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_dttw_motion_tolerance_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%d\n", nvt_get_dttw_para(DTTW_MOTION_TOLERANCE_ADDR));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_dttw_motion_tolerance_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0};
	uint16_t value;
	int32_t ret;

	NVT_LOGD("++\n");

	ret = kstrtou16(buf, 10, &value);
	if (ret) {
		NVT_ERR("invalid input, ret %d.\n", ret);
		return ret;
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(DTTW_MOTION_TOLERANCE_ADDR);
	spi_buf[0] = DTTW_MOTION_TOLERANCE_ADDR & 0x7F;
	spi_buf[1] = value & 0xFF;
	spi_buf[2] = value >> 8;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	ret = nvt_get_dttw_para(DTTW_MOTION_TOLERANCE_ADDR) == value ? 0 : -EINVAL;
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	}

	ts->dttw_motion_tolerance = value;
	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_dttw_detection_window_edge_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "%d\n",
		nvt_get_dttw_para(DTTW_DETECTION_WINDOW_EDGE_ADDR));

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_dttw_detection_window_edge_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint8_t spi_buf[3] = {0};
	uint16_t value;
	int32_t ret;

	NVT_LOGD("++\n");

	ret = kstrtou16(buf, 10, &value);
	if (ret) {
		NVT_ERR("invalid input, ret %d.\n", ret);
		return ret;
	}

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_set_page(DTTW_DETECTION_WINDOW_EDGE_ADDR);
	spi_buf[0] = DTTW_DETECTION_WINDOW_EDGE_ADDR & 0x7F;
	spi_buf[1] = value & 0xFF;
	spi_buf[2] = value >> 8;
	CTP_SPI_WRITE(ts->client, spi_buf, 3);
	msleep(20);
	ret = nvt_get_dttw_para(DTTW_DETECTION_WINDOW_EDGE_ADDR) == value ? 0 : -EINVAL;
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	}

	ts->dttw_detection_window_edge = value;
	NVT_LOGD("--\n");
	return count;
}

static ssize_t nvt_fw_history_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
#if SPI_FLASH
	NVT_LOGD("++\n");
	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT0);
	nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT1);

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);
	NVT_LOGD("--\n");
	return 0;
#else
	int idx = 0;

	NVT_LOGD("++\n");
	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT0);
	idx += sysfs_emit_at(buf, idx, "0x%x:\n",
			ts->mmap->MMAP_HISTORY_EVENT0);
	idx += sysfs_emit_at(buf, idx, "%s", ts->history_buf);
	nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT1);
	idx += sysfs_emit_at(buf, idx, "0x%x:\n",
			ts->mmap->MMAP_HISTORY_EVENT1);
	idx += sysfs_emit_at(buf, idx, "%s", ts->history_buf);

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);
	NVT_LOGD("--\n");
	return idx;
#endif // SPI_FLASH
}

#if SPI_FLASH
static ssize_t nvt_mp_settings_mode_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret;

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = sysfs_emit(buf, "mp_tvcl_mode = %d, mp_ibias_mode = %d\n",
			ts->mp_tvcl_mode, ts->mp_ibias_mode);

	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");
	return ret;
}

static ssize_t nvt_mp_settings_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	uint8_t tvcl_mode, ibias_mode;
	int32_t ret = 0;

	NVT_LOGD("++\n");

	ret = sscanf(buf, "%hhu %hhu", &tvcl_mode, &ibias_mode);
	if (ret != 2) {
		NVT_ERR("must be two number\n");
		return -EINVAL;
	}

	if (tvcl_mode > MODE_4 || tvcl_mode < MODE_1)
		return -EINVAL;

	if (ibias_mode > MODE_2 || ibias_mode < MODE_1)
		return -EINVAL;

	if (mutex_lock_interruptible(&ts->lock))
		return -ERESTARTSYS;

	ret = nvt_mp_settings(tvcl_mode, ibias_mode);

	mutex_unlock(&ts->lock);

	if (ret) {
		NVT_ERR("failed, ret = %d\n", ret);
		return -EINVAL;
	} else {
		NVT_LOGD("--\n");
		return count;
	}
}
#endif // SPI_FLASH

static ssize_t nvt_selftest_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int32_t ret = 0;

	NVT_LOGD("++\n");

	ret = nvt_selftest();
	if (ret) {
		NVT_LOGE("abort(ret %d)!\n", ret);
		ret = sysfs_emit(buf, "selftest abort!\n");
		return ret;
	}

	ret = nvt_print_selftest_result(buf, PAGE_SIZE);

	NVT_LOGD("--\n");
	return ret;
}

#if defined(CONFIG_SOC_GOOGLE)
static DEVICE_ATTR_RW(force_touch_active);
static DEVICE_ATTR_RW(force_release_fw);
#endif
static DEVICE_ATTR_RO(nvt_get_mode_history);
static DEVICE_ATTR_RO(nvt_sync_freq);
static DEVICE_ATTR_RW(nvt_palm_mode);
static DEVICE_ATTR_RW(nvt_high_sensi_mode);
static DEVICE_ATTR_RW(nvt_cont_report_mode);
static DEVICE_ATTR_RW(nvt_noise_mode);
static DEVICE_ATTR_RW(nvt_water_mode);
static DEVICE_ATTR_WO(nvt_sw_reset);
static DEVICE_ATTR_WO(nvt_sensing);
static DEVICE_ATTR_WO(nvt_freq_hopping);
static DEVICE_ATTR_RW(nvt_touch_idle_mode);
static DEVICE_ATTR_WO(nvt_force_calibration);
static DEVICE_ATTR_RO(nvt_get_calibration);
static DEVICE_ATTR_RO(nvt_verify_calibration);
static DEVICE_ATTR_RW(nvt_heatmap_data_type);
static DEVICE_ATTR_RW(nvt_heatmap_touch_threshold);
static DEVICE_ATTR_RW(nvt_cancel_mode);
static DEVICE_ATTR_RW(nvt_grip_level);
static DEVICE_ATTR_RW(nvt_playback_mode);
static DEVICE_ATTR_RW(nvt_dttw_mode);
static DEVICE_ATTR_RW(nvt_dttw_touch_area_max);
static DEVICE_ATTR_RW(nvt_dttw_touch_area_min);
static DEVICE_ATTR_RW(nvt_dttw_contact_duration_max);
static DEVICE_ATTR_RW(nvt_dttw_contact_duration_min);
static DEVICE_ATTR_RW(nvt_dttw_tap_offset);
static DEVICE_ATTR_RW(nvt_dttw_tap_gap_duration_max);
static DEVICE_ATTR_RW(nvt_dttw_tap_gap_duration_min);
static DEVICE_ATTR_RW(nvt_dttw_motion_tolerance);
static DEVICE_ATTR_RW(nvt_dttw_detection_window_edge);
static DEVICE_ATTR_RO(nvt_fw_history);
#if SPI_FLASH
static DEVICE_ATTR_RW(nvt_mp_settings_mode);
#endif // SPI_FLASH
static DEVICE_ATTR_RO(nvt_selftest);

static struct attribute *nvt_api_attrs[] = {
#if defined(CONFIG_SOC_GOOGLE)
	&dev_attr_force_touch_active.attr,
	&dev_attr_force_release_fw.attr,
#endif
	&dev_attr_nvt_get_mode_history.attr,
	&dev_attr_nvt_sync_freq.attr,
	&dev_attr_nvt_palm_mode.attr,
	&dev_attr_nvt_high_sensi_mode.attr,
	&dev_attr_nvt_touch_idle_mode.attr,
	&dev_attr_nvt_cont_report_mode.attr,
	&dev_attr_nvt_noise_mode.attr,
	&dev_attr_nvt_water_mode.attr,
	&dev_attr_nvt_sw_reset.attr,
	&dev_attr_nvt_sensing.attr,
	&dev_attr_nvt_heatmap_data_type.attr,
	&dev_attr_nvt_heatmap_touch_threshold.attr,
	&dev_attr_nvt_freq_hopping.attr,
	&dev_attr_nvt_force_calibration.attr,
	&dev_attr_nvt_get_calibration.attr,
	&dev_attr_nvt_verify_calibration.attr,
	&dev_attr_nvt_cancel_mode.attr,
	&dev_attr_nvt_grip_level.attr,
	&dev_attr_nvt_playback_mode.attr,
	&dev_attr_nvt_dttw_mode.attr,
	&dev_attr_nvt_dttw_touch_area_max.attr,
	&dev_attr_nvt_dttw_touch_area_min.attr,
	&dev_attr_nvt_dttw_contact_duration_max.attr,
	&dev_attr_nvt_dttw_contact_duration_min.attr,
	&dev_attr_nvt_dttw_tap_offset.attr,
	&dev_attr_nvt_dttw_tap_gap_duration_max.attr,
	&dev_attr_nvt_dttw_tap_gap_duration_min.attr,
	&dev_attr_nvt_dttw_motion_tolerance.attr,
	&dev_attr_nvt_dttw_detection_window_edge.attr,
	&dev_attr_nvt_fw_history.attr,
#if SPI_FLASH
	&dev_attr_nvt_mp_settings_mode.attr,
#endif // SPI_FLASH
	&dev_attr_nvt_selftest.attr,
	NULL
};

static ssize_t nvt_playback_write_buf(struct file *data_file,
		struct kobject *kobj, struct bin_attribute *attributes,
		char *buf, loff_t pos, size_t count)
{
	uint8_t spi_buf[2] = {0};
	int16_t i, data_buf[1], retry = 500;

	if (!playback_enabled) {
		NVT_ERR("playback mode is not enabled\n");
		return -EINVAL;
	}

	playback_spi_buf_offset = playback_spi_buf + 1 + (pos / 4); //data start from 1

	for (i = 0; i < count; i += 8) {
		if (sscanf(buf + i, "%hd%*s", data_buf) > 0)
			memcpy(playback_spi_buf_offset + (i / 4), data_buf, 2);
	}
	if (pos == LAST_ROUND_POS) {
		mutex_lock(&ts->lock);
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
		for (i = 0; i < retry; i++) {
			spi_buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
			spi_buf[1] = 0x00;
			CTP_SPI_READ(ts->client, spi_buf, 2);
			if ((spi_buf[1] & 0xF0) == 0xA0)
				break;
			usleep_range(500, 500 + 1);
		}

		if (i == retry) {
			mutex_unlock(&ts->lock);
			return -EAGAIN;
		}

		nvt_set_page(playback_addr);
		playback_spi_buf[0] = playback_addr & 0x7F;
		CTP_SPI_WRITE(ts->client, playback_spi_buf, playback_spi_buf_size);
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
		spi_buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		spi_buf[1] = 0xBB;
		CTP_SPI_WRITE(ts->client, spi_buf, 2);
		mutex_unlock(&ts->lock);
	}

	return count;
}

static struct bin_attribute bin_attr_nvt_playback_write_buf = {
	.attr = {
		.name = "nvt_playback_write_buf",
		.mode = 0220,
	},
	.size = 0,
	.write = nvt_playback_write_buf,
};

static struct bin_attribute *nvt_api_bin_attrs[] = {
	&bin_attr_nvt_playback_write_buf,
	NULL,
};

static const struct attribute_group nvt_api_attribute_group = {
	.attrs = nvt_api_attrs,
	.bin_attrs = nvt_api_bin_attrs,
};

static struct proc_dir_entry *NVT_proc_heatmap_entry;
static struct proc_dir_entry *NVT_proc_cc_uniformity_entry;

static int32_t c_show_heatmap(struct seq_file *m, void *v)
{
	uint32_t count = 0;
	uint32_t start = 1;
	uint32_t i;
	uint8_t *buf = ts->heatmap_spi_buf;
	uint32_t buf_size = ts->heatmap_spi_buf_size;

	if (ts->heatmap_data_type == HEATMAP_DATA_TYPE_DISABLE)
		return 0;

	switch (ts->heatmap_data_type) {
	case HEATMAP_DATA_TYPE_PEN_STRENGTH_COMP:
		/*
		 * TODO(b/219658467):
		 * Need to check with vendor for pen diff compression support
		 * in the future.
		 */
		buf_size = 0;
		break;
	case HEATMAP_DATA_TYPE_TOUCH_STRENGTH:
		buf = ts->heatmap_spi_buf;
		buf_size = ts->heatmap_spi_buf_size;
		break;
	case HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP:
		start = 0;
		buf = ts->heatmap_out_buf;
		buf_size = ts->heatmap_out_buf_size;
		break;
	case HEATMAP_DATA_TYPE_TOUCH_RAWDATA:
	case HEATMAP_DATA_TYPE_TOUCH_BASELINE:
	default:
		buf = ts->extra_spi_buf;
		buf_size = ts->extra_spi_buf_size;
		break;
	}

	// Set size = 8 to align page with 4096 and fit in the data range for raw data
	for (i = start; i < buf_size; i += 2, count++) {
		seq_printf(m, "%7d",
			(int16_t)((buf[i + 1] << 8) + buf[i]));
		if ((count + 1) % ts->x_num == 0)
			seq_puts(m, "\n");
		else
			seq_puts(m, " ");
	}

	if (ts->heatmap_data_type == HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP) {
		seq_puts(m, "\n\nTouch Compressed data:\n");
		start = 1;
		buf = ts->heatmap_spi_buf;
		buf_size = ts->touch_heatmap_comp_len;
		for (i = start; i < buf_size; i += 2, count++) {
			seq_printf(m, "%5x",
				(u16)((buf[i + 1] << 8) + buf[i]));
			if ((count + 1) % ts->x_num == 0)
				seq_puts(m, "\n");
			else
				seq_puts(m, " ");
		}
	}

	seq_puts(m, "\n");
	return 0;
}

static int32_t c_show_cc_uniformity(struct seq_file *m, void *v)
{
	uint32_t i;

	// Set size = 8 to align page with 4096 and fit in the data range for raw data
	for (i = 1; i < cc_uniformity_spi_buf_size; i += 2) {
		seq_printf(m, "%7d",
			(cc_uniformity_spi_buf[i + 1] << 8) + cc_uniformity_spi_buf[i]);
		if ((i + 1) % (ts->x_num * 2) == 0)
			seq_puts(m, "\n");
		else
			seq_puts(m, " ");
	}

	return 0;
}

static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void c_stop(struct seq_file *m, void *v)
{
	return;
}

const struct seq_operations nvt_heatmap_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_show_heatmap
};

const struct seq_operations nvt_cc_uniformity_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_show_cc_uniformity
};

static int32_t nvt_heatmap_open(struct inode *inode, struct file *file)
{
	if (!ts->heatmap_data_type) {
		NVT_ERR("heatmap is not enabled!\n");
		return -EINVAL;
	}

	return seq_open(file, &nvt_heatmap_seq_ops);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops nvt_heatmap_fops = {
	.proc_open = nvt_heatmap_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_heatmap_fops = {
	.owner = THIS_MODULE,
	.open = nvt_heatmap_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

static int32_t nvt_cc_uniformity_open(struct inode *inode,
				      struct file *file)
{
	NVT_LOGD("++\n");
	if (nvt_get_cc_uniformity())
		return -EAGAIN;

	NVT_LOGD("--\n");

	return seq_open(file, &nvt_cc_uniformity_seq_ops);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops nvt_cc_uniformity_fops = {
	.proc_open = nvt_cc_uniformity_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_cc_uniformity_fops = {
	.owner = THIS_MODULE,
	.open = nvt_cc_uniformity_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

#define NVT_TOUCH_SYSFS_LINK "nvt_touch"
int32_t nvt_extra_api_init(void)
{
	int32_t ret;

	NVT_LOGD("++\n");

	ret = sysfs_create_link(ts->input_dev->dev.kobj.parent,
				&ts->input_dev->dev.kobj, NVT_TOUCH_SYSFS_LINK);
	if (ret != 0) {
		NVT_ERR("sysfs create link %s failed. ret=%d", NVT_TOUCH_SYSFS_LINK, ret);
		goto exit_nvt_touch_sysfs_init;
	}

	ret = devm_device_add_group(&ts->input_dev->dev, &nvt_api_attribute_group);
	if (ret)
		NVT_ERR("create sysfs nvt_api_attribute_group failed: %d\n", ret);

	NVT_proc_heatmap_entry = proc_create("nvt_heatmap", 0440, NULL,
					     &nvt_heatmap_fops);
	if (NVT_proc_heatmap_entry == NULL)
		NVT_ERR("create /proc/nvt_heatmap Failed!\n");

	NVT_proc_cc_uniformity_entry = proc_create("nvt_cc_uniformity", 0440, NULL,
					     &nvt_cc_uniformity_fops);
	if (NVT_proc_cc_uniformity_entry == NULL)
		NVT_ERR("create /proc/nvt_cc_uniformity Failed!\n");

	NVT_LOGD("--\n");

exit_nvt_touch_sysfs_init:
	return ret;
}

void nvt_extra_api_deinit(void)
{
	NVT_LOGD("++\n");
	devm_device_remove_group(&ts->input_dev->dev, &nvt_api_attribute_group);
	sysfs_remove_link(ts->input_dev->dev.kobj.parent, NVT_TOUCH_SYSFS_LINK);
	devm_kfree(&ts->client->dev, ts->heatmap_spi_buf);
	ts->heatmap_spi_buf = NULL;
	kfree(cc_uniformity_spi_buf);
	cc_uniformity_spi_buf = NULL;
	kfree(rawdata_uniformity_spi_buf);
	rawdata_uniformity_spi_buf = NULL;
	NVT_LOGD("--\n");
}

void nvt_get_dttw_conf(void)
{
	if (!ts->dttw_touch_area_max)
		ts->dttw_touch_area_max = nvt_get_dttw_para(DTTW_TOUCH_AREA_MAX_ADDR);
	if (!ts->dttw_touch_area_min)
		ts->dttw_touch_area_min = nvt_get_dttw_para(DTTW_TOUCH_AREA_MIN_ADDR);
	if (!ts->dttw_contact_duration_max)
		ts->dttw_contact_duration_max = nvt_get_dttw_para(DTTW_CONTACT_DURATION_MAX_ADDR);
	if (!ts->dttw_contact_duration_min)
		ts->dttw_contact_duration_min = nvt_get_dttw_para(DTTW_CONTACT_DURATION_MIN_ADDR);
	if (!ts->dttw_tap_offset)
		ts->dttw_tap_offset = nvt_get_dttw_para(DTTW_TAP_OFFSET_ADDR);
	if (!ts->dttw_tap_gap_duration_max)
		ts->dttw_tap_gap_duration_max = nvt_get_dttw_para(DTTW_TAP_GAP_DURATION_MAX_ADDR);
	if (!ts->dttw_tap_gap_duration_min)
		ts->dttw_tap_gap_duration_min = nvt_get_dttw_para(DTTW_TAP_GAP_DURATION_MIN_ADDR);
	if (!ts->dttw_motion_tolerance)
		ts->dttw_motion_tolerance = nvt_get_dttw_para(DTTW_MOTION_TOLERANCE_ADDR);
	if (!ts->dttw_detection_window_edge)
		ts->dttw_detection_window_edge = nvt_get_dttw_para(DTTW_DETECTION_WINDOW_EDGE_ADDR);
}
#endif /* #if NVT_TOUCH_EXT_API */
