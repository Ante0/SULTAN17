// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 - 2021 Novatek, Inc.
 *
 * $Revision: 83893 $
 * $Date: 2021-06-21 10:52:25 +0800 (週一, 21 六月 2021) $
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
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include <linux/input/mt.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/power_supply.h>

#define NVT_VENDOR_ID		0x0603
#define NVT_PRODUCT_ID		0x7806
#define NVT_VERSION		0x0100

#define INFO_BUF_SIZE		(64 + 1)

#if defined(CONFIG_DRM_PANEL)
#include <drm/drm_panel.h>
#elif defined(CONFIG_DRM_MSM)
#include <linux/msm_drm_notify.h>
#endif

#if defined(CONFIG_FB)
#include <linux/notifier.h>
#include <linux/fb.h>
#elif defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

#include "nt36xxx.h"
#include "nt36xxx_ext_usi.h"
#if NVT_TOUCH_ESD_PROTECT
#include <linux/jiffies.h>
#endif /* #if NVT_TOUCH_ESD_PROTECT */

#if defined(NVT_TS_PANEL_BRIDGE)
#include <samsung/exynos_drm_connector.h>
static void nvt_ts_suspend_work(struct work_struct *work);
static void nvt_ts_resume_work(struct work_struct *work);
#endif
static const struct dev_pm_ops goog_pm_ops;

#if NVT_TOUCH_ESD_PROTECT
static struct delayed_work nvt_esd_check_work;
static struct workqueue_struct *nvt_esd_check_wq;
static unsigned long irq_timer;
uint8_t esd_check;
uint8_t esd_retry;
#endif /* #if NVT_TOUCH_ESD_PROTECT */

#if NVT_TOUCH_EXT_PROC
extern int32_t nvt_extra_proc_init(void);
extern void nvt_extra_proc_deinit(void);
#endif

#if NVT_TOUCH_MP
extern int32_t nvt_mp_proc_init(void);
extern void nvt_mp_proc_deinit(void);
#endif

struct nvt_ts_data *ts;

#if BOOT_UPDATE_FIRMWARE
struct workqueue_struct *nvt_fwu_wq;
extern void Boot_Update_Firmware(struct work_struct *work);
#endif

#if defined(CONFIG_FB)
#if defined(CONFIG_DRM_PANEL) && (defined(CONFIG_ARCH_QCOM) || defined(CONFIG_ARCH_MSM))
static int nvt_drm_panel_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data);
#elif defined(_MSM_DRM_NOTIFY_H_)
static int nvt_drm_notifier_callback(struct notifier_block *self,
				     unsigned long event, void *data);
#else
static int nvt_fb_notifier_callback(struct notifier_block *self,
				    unsigned long event, void *data);
#endif
#elif defined(CONFIG_HAS_EARLYSUSPEND)
static void nvt_ts_early_suspend(struct early_suspend *h);
static void nvt_ts_late_resume(struct early_suspend *h);
#endif

#if !SPI_FLASH
uint32_t ENG_RST_ADDR  = 0x7FFF80;
#endif // !SPI_FLASH
uint32_t SWRST_N8_ADDR; //read from dtsi
uint32_t SPI_RD_FAST_ADDR;	//read from dtsi

#if TOUCH_KEY_NUM > 0
const uint16_t touch_key_array[TOUCH_KEY_NUM] = {
	KEY_BACK,
	KEY_HOME,
	KEY_MENU
};
#endif

#if defined(CONFIG_SOC_GOOGLE)
const unsigned int gesture_keycode[GESTURE_ID_MAX] = {
	[GESTURE_SINGLE_TAP] = KEY_WAKEUP,
	[GESTURE_DOUBLE_TAP] = KEY_WAKEUP,
};
#else
const unsigned int gesture_keycode[GESTURE_ID_MAX] = {
	[GESTURE_WORD_C] = KEY_POWER,
	[GESTURE_WORD_W] = KEY_POWER,
	[GESTURE_SINGLE_TAP] = KEY_POWER,
	[GESTURE_DOUBLE_TAP] = KEY_POWER,
	[GESTURE_WORD_Z] = KEY_POWER,
	[GESTURE_WORD_M] = KEY_POWER,
	[GESTURE_WORD_O] = KEY_POWER,
	[GESTURE_WORD_e] = KEY_POWER,
	[GESTURE_WORD_S] = KEY_POWER,
	[GESTURE_SLIDE_UP] = KEY_POWER,
	[GESTURE_SLIDE_DOWN] = KEY_POWER,
	[GESTURE_SLIDE_LEFT] = KEY_POWER,
	[GESTURE_SLIDE_RIGHT] = KEY_POWER,
};
#endif

const char *gesture_string[GESTURE_ID_MAX] = {
	[GESTURE_WORD_C] = "Word-C",
	[GESTURE_WORD_W] = "Word-W",
	[GESTURE_SINGLE_TAP] = "Single Tap",
	[GESTURE_DOUBLE_TAP] = "Double Tap",
	[GESTURE_WORD_Z] = "Word-Z",
	[GESTURE_WORD_M] = "Word-M",
	[GESTURE_WORD_O] = "Word-O",
	[GESTURE_WORD_e] = "Word-e",
	[GESTURE_WORD_S] = "Word-S",
	[GESTURE_SLIDE_UP] = "Slide UP",
	[GESTURE_SLIDE_DOWN] = "Slide DOWN",
	[GESTURE_SLIDE_LEFT] = "Slide LEFT",
	[GESTURE_SLIDE_RIGHT] = "Slide UP",
};

#ifdef CONFIG_MTK_SPI
const struct mt_chip_conf spi_ctrdata = {
	.setuptime = 25,
	.holdtime = 25,
	.high_time = 5,	/* 10MHz (SPI_SPEED=100M / (high_time+low_time(10ns)))*/
	.low_time = 5,
	.cs_idletime = 2,
	.ulthgh_thrsh = 0,
	.cpol = 0,
	.cpha = 0,
	.rx_mlsb = 1,
	.tx_mlsb = 1,
	.tx_endian = 0,
	.rx_endian = 0,
	.com_mod = DMA_TRANSFER,
	.pause = 0,
	.finish_intr = 1,
	.deassert = 0,
	.ulthigh = 0,
	.tckdly = 0,
};
#endif

#ifdef CONFIG_SPI_MT65XX
const struct mtk_chip_config spi_ctrdata = {
	.rx_mlsb = 1,
	.tx_mlsb = 1,
	.cs_pol = 0,
};
#endif

const char *get_fw_name(void)
{
	if (ts && ts->fw_name)
		return ts->fw_name;
	else
		return BOOT_UPDATE_FIRMWARE_NAME;
}

const char *get_mp_fw_name(void)
{
	if (ts && ts->mp_fw_name)
		return ts->mp_fw_name;
	else
		return MP_UPDATE_FIRMWARE_NAME;
}

void nvt_set_heatmap_host_cmd(struct nvt_ts_data *ts, bool force_update)
{
	uint8_t cmd_type = 0;
	uint8_t cmd_buf[3] = {EVENT_MAP_HOST_CMD, 0x70, 0};

	if (!ts ||
		ts->heatmap_data_type == HEATMAP_DATA_TYPE_PEN_STRENGTH_COMP ||
		ts->selftest_in_process == true) {
		NVT_LOG("Skip to set heatmap mode!\n");
		return;
	}

	switch (ts->heatmap_data_type) {
	case HEATMAP_DATA_TYPE_TOUCH_RAWDATA:
		cmd_type = HEATMAP_HOST_CMD_TOUCH_RAWDATA;
		break;
	case HEATMAP_DATA_TYPE_TOUCH_BASELINE:
		cmd_type = HEATMAP_HOST_CMD_TOUCH_BASELINE;
		break;
	case HEATMAP_DATA_TYPE_TOUCH_STRENGTH:
		cmd_type = HEATMAP_HOST_CMD_TOUCH_STRENGTH;
		break;
	case HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP:
		cmd_type = HEATMAP_HOST_CMD_TOUCH_STRENGTH_COMP;
		break;
	case HEATMAP_DATA_TYPE_DISABLE:
		cmd_type = HEATMAP_HOST_CMD_DISABLE;
		break;
	default:
		NVT_ERR("unexpected heatmap type %d!", ts->heatmap_data_type);
		break;
	}

	if (force_update || (ts->heatmap_host_cmd != cmd_type)) {
		NVT_LOG("new host cmd(%#x) for heatmap type(%d)\n",
			cmd_type, ts->heatmap_data_type);
		cmd_buf[2] = cmd_type;
		CTP_SPI_WRITE(ts->client, cmd_buf, sizeof(cmd_buf));
		ts->heatmap_host_cmd = cmd_type;
	}
}

/*******************************************************
Description:
	Novatek touchscreen pinctrl enable/disable function.
*******************************************************/
static int nvt_pinctrl_configure(struct nvt_ts_data *ts, bool enable)
{
	struct pinctrl_state *state;

	if (IS_ERR_OR_NULL(ts->pinctrl)) {
		NVT_ERR("Invalid pinctrl!\n");
		return -EINVAL;
	}

	NVT_LOGI("%s\n", enable ? "ACTIVE" : "SUSPEND");

	if (enable) {
		state = pinctrl_lookup_state(ts->pinctrl, "ts_active");
		if (IS_ERR(state))
			NVT_ERR("Could not get ts_active pinstate!\n");
	} else {
		state = pinctrl_lookup_state(ts->pinctrl, "ts_suspend");
		if (IS_ERR(state))
			NVT_ERR("Could not get ts_suspend pinstate!\n");
	}

	if (!IS_ERR_OR_NULL(state))
		return pinctrl_select_state(ts->pinctrl, state);

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen irq enable/disable function.

return:
	n.a.
*******************************************************/
void nvt_irq_enable(bool enable)
{
	struct irq_desc *desc;

	if (enable) {
		if (!ts->irq_enabled) {
			enable_irq(ts->client->irq);
			ts->irq_enabled = true;
		}
	} else {
		if (ts->irq_enabled) {
			disable_irq_nosync(ts->client->irq);
			ts->irq_enabled = false;
		}
	}

	desc = irq_to_desc(ts->client->irq);
	NVT_LOG("enable=%d, desc->depth=%d\n", enable, desc->depth);
}

/*******************************************************
Description:
	Novatek touchscreen spi read/write core function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static inline int32_t spi_read_write(struct spi_device *client,
				     uint8_t *buf, size_t len, NVT_SPI_RW rw)
{
	struct spi_message m;
	struct spi_transfer t = {
		.len    = len,
	};

	memset(ts->xbuf, 0, len + DUMMY_BYTES);
	memcpy(ts->xbuf, buf, len);

	switch (rw) {
	case NVTREAD:
		t.tx_buf = ts->xbuf;
		t.rx_buf = ts->rbuf;
		t.len    = (len + DUMMY_BYTES);
		break;

	case NVTWRITE:
		t.tx_buf = ts->xbuf;
		break;
	}

	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(client, &m);
}

/*******************************************************
Description:
	Novatek touchscreen spi read function.

return:
	Executive outcomes. 2---succeed. -5---I/O error
*******************************************************/
int32_t CTP_SPI_READ(struct spi_device *client, uint8_t *buf, uint16_t len)
{
	int32_t ret = -1;
	int32_t retries = 0;

	mutex_lock(&ts->xbuf_lock);

	buf[0] = SPI_READ_MASK(buf[0]);

	while (retries < 5) {
		ret = spi_read_write(client, buf, len, NVTREAD);
		if (ret == 0)
			break;
		retries++;
		if (ret == -ESHUTDOWN) {
			NVT_LOGI("wait spi_controller_resume complete!\n");
			usleep_range(10 * 1000, 10 * 1000 + 1);
		}
	}

	if (unlikely(retries == 5)) {
		NVT_ERR("read error, ret=%d\n", ret);
		ret = -EIO;
	} else {
		memcpy((buf+1), (ts->rbuf+2), (len-1));
	}

	mutex_unlock(&ts->xbuf_lock);

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen spi write function.

return:
	Executive outcomes. 1---succeed. -5---I/O error
*******************************************************/
int32_t CTP_SPI_WRITE(struct spi_device *client, uint8_t *buf,
		      uint16_t len)
{
	int32_t ret = -1;
	int32_t retries = 0;

	mutex_lock(&ts->xbuf_lock);

	buf[0] = SPI_WRITE_MASK(buf[0]);

	while (retries < 5) {
		ret = spi_read_write(client, buf, len, NVTWRITE);
		if (ret == 0)
			break;
		retries++;
	}

	if (unlikely(retries == 5)) {
		NVT_ERR("error, ret=%d\n", ret);
		ret = -EIO;
	}

	mutex_unlock(&ts->xbuf_lock);

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen set index/page/addr address.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
int32_t nvt_set_page(uint32_t addr)
{
	uint8_t buf[4] = {0};

	buf[0] = 0xFF;	//set index/page/addr command
	buf[1] = (addr >> 15) & 0xFF;
	buf[2] = (addr >> 7) & 0xFF;

	return CTP_SPI_WRITE(ts->client, buf, 3);
}

/*******************************************************
Description:
	Novatek touchscreen write data to specify address.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
int32_t nvt_write_addr(uint32_t addr, uint8_t data)
{
	int32_t ret = 0;
	uint8_t buf[4] = {0};

	//---set xdata index---
	buf[0] = 0xFF;	//set index/page/addr command
	buf[1] = (addr >> 15) & 0xFF;
	buf[2] = (addr >> 7) & 0xFF;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret) {
		NVT_ERR("set page 0x%06X failed, ret = %d\n", addr, ret);
		return ret;
	}

	//---write data to index---
	buf[0] = addr & (0x7F);
	buf[1] = data;
	ret = CTP_SPI_WRITE(ts->client, buf, 2);
	if (ret) {
		NVT_ERR("write data to 0x%06X failed, ret = %d\n", addr, ret);
		return ret;
	}

	return ret;
}

#if !SPI_FLASH
/*******************************************************
Description:
	Novatek touchscreen enable hw bld crc function.

return:
	N/A.
*******************************************************/
void nvt_bld_crc_enable(void)
{
	uint8_t buf[4] = {0};

	//---set xdata index to BLD_CRC_EN_ADDR---
	nvt_set_page(ts->mmap->BLD_CRC_EN_ADDR);

	//---read data from index---
	buf[0] = ts->mmap->BLD_CRC_EN_ADDR & (0x7F);
	buf[1] = 0xFF;
	CTP_SPI_READ(ts->client, buf, 2);

	//---write data to index---
	buf[0] = ts->mmap->BLD_CRC_EN_ADDR & (0x7F);
	buf[1] = buf[1] | (0x01 << 7);
	CTP_SPI_WRITE(ts->client, buf, 2);
}

/*******************************************************
Description:
	Novatek touchscreen clear status & enable fw crc function.

return:
	N/A.
*******************************************************/
void nvt_fw_crc_enable(void)
{
	uint8_t clear_cmd[7] = {0};
	uint8_t crc_cmd[3] = {0};
	uint16_t clear_cmd_size = 0;
	uint16_t crc_cmd_size = 0;

#if defined(CONFIG_SOC_GOOGLE)
	if (nvt_ts_check_tid(ts, tid_nt36523n)) {
		clear_cmd_size = 7;
		crc_cmd_size = 3;
	} else {
		clear_cmd_size = 2;
		crc_cmd_size = 2;
	}
#else
	clear_cmd_size = 2;
	crc_cmd_size = 2;
#endif

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	//---clear fw reset status---
	clear_cmd[0] = EVENT_MAP_RESET_COMPLETE & (0x7F);
	CTP_SPI_WRITE(ts->client, clear_cmd, clear_cmd_size);

	//---enable fw crc---
	crc_cmd[0] = EVENT_MAP_HOST_CMD & (0x7F);
	crc_cmd[1] = 0xAE; //enable fw crc command
	CTP_SPI_WRITE(ts->client, crc_cmd, crc_cmd_size);
}

/*******************************************************
Description:
	Novatek touchscreen set boot ready function.

return:
	N/A.
*******************************************************/
void nvt_boot_ready(void)
{
	//---write BOOT_RDY status cmds---
	nvt_write_addr(ts->mmap->BOOT_RDY_ADDR, 1);

	mdelay(5);

	if (!ts->hw_crc) {
		//---write BOOT_RDY status cmds---
		nvt_write_addr(ts->mmap->BOOT_RDY_ADDR, 0);

		//---write POR_CD cmds---
		nvt_write_addr(ts->mmap->POR_CD_ADDR, 0xA0);
	}
}

/*******************************************************
Description:
	Novatek touchscreen enable auto copy mode function.

return:
	N/A.
*******************************************************/
void nvt_tx_auto_copy_mode(void)
{
	//---write TX_AUTO_COPY_EN cmds---
	nvt_write_addr(ts->mmap->TX_AUTO_COPY_EN, 0x69);

	NVT_LOG("tx auto copy mode enable\n");
}

/*******************************************************
Description:
	Novatek touchscreen check spi dma tx info function.

return:
	N/A.
*******************************************************/
int32_t nvt_check_spi_dma_tx_info(void)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 200;

	for (i = 0; i < retry; i++) {
		//---set xdata index to EVENT BUF ADDR---
		nvt_set_page(ts->mmap->SPI_DMA_TX_INFO);

		//---read fw status---
		buf[0] = ts->mmap->SPI_DMA_TX_INFO & 0x7F;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(1000, 1000);
	}

	if (i >= retry) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -EPERM;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen eng reset cmd
    function.

return:
	n.a.
*******************************************************/
void nvt_eng_reset(void)
{
	//---eng reset cmds to ENG_RST_ADDR---
	nvt_write_addr(ENG_RST_ADDR, 0x5A);

	mdelay(1);	//wait tMCU_Idle2TP_REX_Hi after TP_RST
}

/*******************************************************
Description:
	Novatek touchscreen reset MCU
    function.

return:
	n.a.
*******************************************************/
void nvt_sw_reset(void)
{
	//---software reset cmds to SWRST_N8_ADDR---
	nvt_write_addr(SWRST_N8_ADDR, 0x55);

	msleep(10);
}
#endif // !SPI_FLASH
/*******************************************************
Description:
	Novatek touchscreen reset MCU then into idle mode
    function.

return:
	n.a.
*******************************************************/
void nvt_sw_reset_idle(void)
{
	//---MCU idle cmds to SWRST_N8_ADDR---
	nvt_write_addr(SWRST_N8_ADDR, 0xAA);

	msleep(15);
}

#if SPI_FLASH
/*******************************************************
Description:
	Novatek touchscreen reset MCU then into idle mode
    function.

return:
	n.a.
*******************************************************/
void nvt_sw_reset_idle_no_delay(void)
{
	//---MCU idle cmds to SWRST_N8_ADDR---
	nvt_write_addr(SWRST_N8_ADDR, 0xAA);
}

/*******************************************************
Description:
	Novatek touchscreen write value to specific register.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
int32_t nvt_write_reg(nvt_ts_reg_t reg, uint8_t val)
{
	int32_t ret = 0;
	uint32_t addr = 0;
	uint8_t mask = 0;
	uint8_t shift = 0;
	uint8_t buf[8] = {0};
	uint8_t temp = 0;

	addr = reg.addr;
	mask = reg.mask;
	/* get shift */
	temp = reg.mask;
	shift = 0;
	while (1) {
		if ((temp >> shift) & 0x01)
			break;
		if (shift == 8) {
			NVT_ERR("mask all bits zero!\n");
			break;
		}
		shift++;
	}
	/* read the byte including this register */
	nvt_set_page(addr);
	buf[0] = addr & 0xFF;
	buf[1] = 0x00;
	ret = CTP_SPI_READ(ts->client, buf, 2);
	if (ret < 0) {
		NVT_ERR("CTP_SPI_READ failed!(%d)\n", ret);
		goto nvt_write_register_exit;
	}
	/* set register's value in its field of the byte */
	temp = buf[1] & (~mask);
	temp |= ((val << shift) & mask);
	/* write back the whole byte including this register */
	buf[0] = addr & 0xFF;
	buf[1] = temp;
	ret = CTP_SPI_WRITE(ts->client, buf, 2);
	if (ret < 0) {
		NVT_ERR("CTP_SPI_WRITE failed!(%d)\n", ret);
		goto nvt_write_register_exit;
	}

nvt_write_register_exit:
	return ret;
}
#endif // SPI_FLASH

/*******************************************************
Description:
	Novatek touchscreen reset MCU (boot) function.

return:
	n.a.
*******************************************************/
void nvt_bootloader_reset(void)
{
	//---reset cmds to SWRST_N8_ADDR---
	nvt_write_addr(SWRST_N8_ADDR, 0x69);

#if SPI_FLASH
	mdelay(35);	//wait tBRST2FR after Bootload RST
#else
	mdelay(5);	//wait tBRST2FR after Bootload RST
#endif // SPI_FLASH
	if (SPI_RD_FAST_ADDR) {
		/* disable SPI_RD_FAST */
		nvt_write_addr(SPI_RD_FAST_ADDR, 0x00);
	}
}

/*******************************************************
Description:
	Novatek touchscreen clear FW status function.

return:
	Executive outcomes. 0---succeed. -1---fail.
*******************************************************/
int32_t nvt_clear_fw_status(void)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 20;

	for (i = 0; i < retry; i++) {
		//---set xdata index to EVENT BUF ADDR---
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR |
			     EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

		//---clear fw status---
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_WRITE(ts->client, buf, 2);

		//---read fw status---
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(10000, 10000);
	}

	if (i >= retry) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -EPERM;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen check FW status function.

return:
	Executive outcomes. 0---succeed. -1---failed.
*******************************************************/
int32_t nvt_check_fw_status(void)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 50;

	usleep_range(20000, 20000);

	for (i = 0; i < retry; i++) {
		//---set xdata index to EVENT BUF ADDR---
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR |
			     EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

		//---read fw status---
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_READ(ts->client, buf, 2);

		if ((buf[1] & 0xF0) == 0xA0)
			break;

		usleep_range(10000, 10000);
	}

	if (i >= retry) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -EPERM;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen check FW reset state function.

return:
	Executive outcomes. 0---succeed. -1---failed.
*******************************************************/
int32_t nvt_check_fw_reset_state(RST_COMPLETE_STATE check_reset_state)
{
	uint8_t buf[8] = {0};
	int32_t ret = 0;
	int32_t retry = 0;
#if SPI_FLASH
	int32_t retry_max = 100;
#else
	int32_t retry_max = (check_reset_state == RESET_STATE_INIT) ? 10 : 50;
#endif

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE);

	while (1) {
		//---read reset state---
		buf[0] = EVENT_MAP_RESET_COMPLETE;
		buf[1] = 0x00;
		CTP_SPI_READ(ts->client, buf, 6);

		if ((buf[1] >= check_reset_state) && (buf[1] <= RESET_STATE_MAX)) {
			ret = 0;
			break;
		}

		retry++;
		if (unlikely(retry > retry_max)) {
			NVT_ERR("error, retry=%d, buf[1]=0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
				retry, buf[1], buf[2], buf[3], buf[4], buf[5]);
			ret = -1;
			break;
		}

		usleep_range(10000, 10000);
	}

#if SPI_FLASH
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (ts && ts->gti && ts->selftest_in_process == false) {
		struct gti_fw_status_data fw_status = {0};

		goog_notify_fw_status_changed(ts->gti, GTI_FW_STATUS_RESET,
			&fw_status);
	}
#endif
#endif

	return ret;
}

#if SPI_FLASH
/*******************************************************
Description:
	Novatek touchscreen clear reset state function.

return:
	Executive outcomes. 0---success. -1---fail.
*******************************************************/
void nvt_clear_fw_reset_state(void)
{
	uint8_t buf[2] = {0};

	nvt_sw_reset_idle();

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE);
	//---clear fw reset state---
	buf[0] = EVENT_MAP_RESET_COMPLETE;
	buf[1] = 0x00;
	CTP_SPI_WRITE(ts->client, buf, 2);
}
#endif // SPI_FLASH
/*******************************************************
Description:
	Novatek touchscreen get firmware related information
	function.

return:
	Executive outcomes. 0---success. -1---fail.
*******************************************************/
int32_t nvt_get_fw_info(void)
{
	uint8_t buf[64] = {0};
	uint32_t retry_count = 0;
	int32_t ret = 0;

info_retry:
	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_FWINFO);

	//---read fw info---
	buf[0] = EVENT_MAP_FWINFO;
	CTP_SPI_READ(ts->client, buf, 39);
	if ((buf[1] + buf[2]) != 0xFF) {
		NVT_ERR("FW info is broken! fw_ver=0x%02X, ~fw_ver=0x%02X\n", buf[1],
			buf[2]);
		if (retry_count < 3) {
			retry_count++;
			NVT_ERR("retry_count=%d\n", retry_count);
			goto info_retry;
		} else {
			ts->fw_ver = 0;
			ts->touch_width = TOUCH_DEFAULT_MAX_WIDTH;
			ts->touch_height = TOUCH_DEFAULT_MAX_HEIGHT;
			ts->abs_x_max = ts->touch_width - 1;
			ts->abs_y_max = ts->touch_height - 1;
			ts->max_button_num = TOUCH_KEY_NUM;
			NVT_ERR("Set default fw_ver=0x%02X, abs_max(x,y)=%d %d, max_button=%d!\n",
				ts->fw_ver, ts->abs_x_max, ts->abs_y_max, ts->max_button_num);
			ret = -1;
			goto out;
		}
	}
	ts->fw_ver = buf[1];
	ts->x_num = buf[3];
	ts->y_num = buf[4];
	ts->touch_width = (uint16_t)((buf[5] << 8) | buf[6]);
	ts->touch_height = (uint16_t)((buf[7] << 8) | buf[8]);
	ts->abs_x_max = ts->touch_width - 1;
	ts->abs_y_max = ts->touch_height - 1;
	ts->max_button_num = buf[11];
	ts->nvt_pid = (uint16_t)((buf[36] << 8) | buf[35]);
	if (ts->pen_support) {
		ts->x_gang_num = buf[37];
		ts->y_gang_num = buf[38];
	}
	ts->res_scale = ts->touch_width / ts->display_width;
	NVT_LOG("fw_ver=0x%02X, fw_type=0x%02X, PID=0x%04X, Touch W/H=(%d, %d), Pen W/H=(%d, %d)\n",
		ts->fw_ver, buf[14], ts->nvt_pid, ts->touch_width, ts->touch_height,
		ts->pen_width, ts->pen_height);

	/* Allocate buffer for heatmap(delta) data. */
	if (!ts->heatmap_out_buf) {
		ts->heatmap_out_buf_size = ts->x_num * ts->y_num * 2;
		ts->heatmap_out_buf = devm_kzalloc(&ts->client->dev,
				ts->heatmap_out_buf_size, GFP_KERNEL);
		if (!ts->heatmap_out_buf) {
			NVT_ERR("failed to alloc heatmap_out_buf!\n");
			return -ENOMEM;
		}
	}

	/* Allocate buffer for SPI heatmap(delta) data. */
	if (!ts->heatmap_spi_buf) {
		ts->heatmap_data_type = HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP; // keep the default
		ts->heatmap_host_cmd_addr = HEATMAP_TOUCH_ADDR;
		/* Need one stuffing byte for I/O transfer. */
		ts->heatmap_spi_buf_size = ts->x_num * ts->y_num * 2 + 1;
		ts->heatmap_spi_buf = devm_kzalloc(&ts->client->dev,
				ts->heatmap_spi_buf_size, GFP_KERNEL);
		if (!ts->heatmap_spi_buf) {
			NVT_ERR("failed to alloc heatmap_spi_buf!\n");
			return -ENOMEM;
		}
	}

	/* Allocate buffer for SPI extra(non-delta) data. */
	if (!ts->extra_spi_buf) {
		/* Need one stuffing byte for I/O transfer. */
		ts->extra_spi_buf_size = ts->x_num * ts->y_num * 2 + 1;
		ts->extra_spi_buf = devm_kzalloc(&ts->client->dev,
				ts->extra_spi_buf_size, GFP_KERNEL);
		if (!ts->extra_spi_buf) {
			NVT_ERR("failed to alloc extra_spi_buf!\n");
			return -ENOMEM;
		}
	}

	/* Initialize heatmap_host_cmd. */
	ts->heatmap_host_cmd = HEATMAP_HOST_CMD_DISABLE;
	nvt_set_heatmap_host_cmd(ts, true);

#if NVT_TOUCH_EXT_API
	/* Get DTTW initialized conf. */
	nvt_get_dttw_conf();
#endif

	ret = 0;
out:

	return ret;
}

/* customized gesture id */
#define DATA_PROTOCOL           30

/* function page definition */
#define FUNCPAGE_GESTURE         1

/*******************************************************
Description:
	Novatek touchscreen wake up gesture key report function.

return:
	n.a.
*******************************************************/
void nvt_ts_wakeup_gesture_report(uint8_t gesture_id, uint8_t *data)
{
	unsigned int keycode = 0;
	uint8_t func_type = data[2];
	uint8_t func_id = data[3];

	/* support fw special data protocol */
	if ((gesture_id == DATA_PROTOCOL) && (func_type == FUNCPAGE_GESTURE)) {
		gesture_id = func_id;
	} else if (gesture_id > DATA_PROTOCOL || gesture_id >= GESTURE_ID_MAX) {
		NVT_ERR("gesture_id %d is invalid, func_type %d, func_id %d\n", gesture_id,
			func_type, func_id);
		return;
	}

	if (gesture_id < GESTURE_ID_MAX)
		keycode = gesture_keycode[gesture_id];
	if (keycode) {
		NVT_LOG("Gesture: %s(%d) triggered and report keycode(%d).\n",
			gesture_string[gesture_id], gesture_id, keycode);
		input_report_key(ts->input_dev, keycode, 1);
		input_sync(ts->input_dev);
		input_report_key(ts->input_dev, keycode, 0);
		input_sync(ts->input_dev);
	} else {
		NVT_ERR("invalid gesture_id %d!\n", gesture_id);
	}
}

/*******************************************************
Description:
	Novatek touchscreen parse device tree function.

return:
	n.a.
*******************************************************/
#ifdef CONFIG_OF
static int32_t nvt_parse_dt(struct device *dev)
{
	struct device_node *np = dev->of_node;
	int32_t ret = 0;
	u16 values[2];

#if NVT_TOUCH_SUPPORT_HW_RST
	ts->reset_gpio = of_get_named_gpio_flags(np, "novatek,reset-gpio", 0,
			 &ts->reset_flags);
	NVT_LOG("novatek,reset-gpio=%d\n", ts->reset_gpio);
#endif
	ts->irq_gpio = of_get_named_gpio_flags(np, "novatek,irq-gpio", 0,
					       &ts->irq_flags);
	NVT_LOG("novatek,irq-gpio=%d\n", ts->irq_gpio);

	ts->pen_support = of_property_read_bool(np, "novatek,pen-support");
	NVT_LOG("novatek,pen-support=%d\n", ts->pen_support);

	if (ts->pen_support) {
		ts->pen_width = PEN_DEFAULT_MAX_WIDTH;
		ts->pen_height = PEN_DEFAULT_MAX_HEIGHT;
		of_property_read_u16(np, "novatek,pen-width", &ts->pen_width);
		of_property_read_u16(np, "novatek,pen-height", &ts->pen_height);
		ts->pen_abs_x_max = ts->pen_width - 1;
		ts->pen_abs_y_max = ts->pen_height - 1;
	}

	ret = of_property_read_u32(np, "novatek,swrst-n8-addr", &SWRST_N8_ADDR);
	if (ret) {
		NVT_ERR("error reading novatek,swrst-n8-addr. ret=%d\n", ret);
		return ret;
	} else {
		NVT_LOG("SWRST_N8_ADDR=0x%06X\n", SWRST_N8_ADDR);
	}

	ret = of_property_read_u32(np, "novatek,spi-rd-fast-addr",
				   &SPI_RD_FAST_ADDR);
	if (ret) {
		NVT_LOG("not support novatek,spi-rd-fast-addr\n");
		SPI_RD_FAST_ADDR = 0;
		ret = 0;
	} else {
		NVT_LOG("SPI_RD_FAST_ADDR=0x%06X\n", SPI_RD_FAST_ADDR);
	}

	if (of_property_read_u16_array(np, "goog,display-resolution",
		values, 2) == 0) {
		ts->display_width = values[0];
		ts->display_height = values[1];
	} else {
		ts->display_width = DEFAULT_MAX_WIDTH;
		ts->display_height = DEFAULT_MAX_HEIGHT;
	}
	ts->res_scale = TOUCH_RES_SCALE;

	return ret;
}

static void nvt_get_resolutions(struct device *dev, uint32_t *pos_x_res,
				uint32_t *pos_y_res, uint32_t *touch_major_res)
{
	struct device_node *np = dev->of_node;
	struct nvt_ts_data *ts = dev_get_drvdata(dev);

	/*
	 * Retrieve any available screen surface resolutions of
	 * ABS_MT_TOUCH_MAJOR and ABS_MT_POSITION_X/Y from DT.
	 */
	of_property_read_u32(np, "touchscreen-abs-mt-position-x-res",
			     pos_x_res);

	of_property_read_u32(np, "touchscreen-abs-mt-position-y-res",
			     pos_y_res);

	of_property_read_u32(np, "touchscreen-abs-mt-touch-major-res",
			     touch_major_res);

	if (ts->high_resolution_enabled) {
		*pos_x_res *= ts->res_scale;
		*pos_y_res *= ts->res_scale;
		*touch_major_res *= ts->res_scale;
	}

	NVT_LOG("pos-x-res=%d, pos-y-res=%d, touch-major-res=%d, res_scale=%d\n",
		*pos_x_res, *pos_y_res, *touch_major_res, ts->res_scale);
}
#else
static int32_t nvt_parse_dt(struct device *dev)
{
#if NVT_TOUCH_SUPPORT_HW_RST
	ts->reset_gpio = NVTTOUCH_RST_PIN;
#endif
	ts->irq_gpio = NVTTOUCH_INT_PIN;
	return 0;
}
#endif

/*******************************************************
Description:
	Novatek touchscreen config and request gpio

return:
	Executive outcomes. 0---succeed. not 0---failed.
*******************************************************/
static int nvt_gpio_config(struct nvt_ts_data *ts)
{
	int32_t ret = 0;

#if NVT_TOUCH_SUPPORT_HW_RST
	/* request RST-pin (Output/High) */
	if (gpio_is_valid(ts->reset_gpio)) {
		ret = gpio_request_one(ts->reset_gpio, GPIOF_OUT_INIT_LOW, "NVT-tp-rst");
		if (ret) {
			NVT_ERR("Failed to request NVT-tp-rst GPIO\n");
			goto err_request_reset_gpio;
		}
	}
#endif

	/* request INT-pin (Input) */
	if (gpio_is_valid(ts->irq_gpio)) {
		ret = gpio_request_one(ts->irq_gpio, GPIOF_IN, "NVT-int");
		if (ret) {
			NVT_ERR("Failed to request NVT-int GPIO\n");
			goto err_request_irq_gpio;
		}
	}

	return ret;

err_request_irq_gpio:
#if NVT_TOUCH_SUPPORT_HW_RST
	if (gpio_is_valid(ts->reset_gpio))
		gpio_free(ts->reset_gpio);
err_request_reset_gpio:
#endif
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen deconfig gpio

return:
	n.a.
*******************************************************/
static void nvt_gpio_deconfig(struct nvt_ts_data *ts)
{
	if (gpio_is_valid(ts->irq_gpio))
		gpio_free(ts->irq_gpio);
#if NVT_TOUCH_SUPPORT_HW_RST
	if (gpio_is_valid(ts->reset_gpio))
		gpio_free(ts->reset_gpio);
#endif
}

static uint8_t nvt_fw_recovery(uint8_t *point_data)
{
	uint8_t i = 0;
	uint8_t detected = true;

	/* check pattern */
	for (i = 1 ; i < 7 ; i++) {
		if (point_data[i] != 0x77) {
			detected = false;
			break;
		}
	}

	return detected;
}

#if NVT_TOUCH_ESD_PROTECT
void nvt_esd_check_enable(uint8_t enable)
{
	/* update interrupt timer */
	irq_timer = jiffies;
	/* clear esd_retry counter, if protect function is enabled */
	esd_retry = enable ? 0 : esd_retry;
	/* enable/disable esd check flag */
	esd_check = enable;
}

static void nvt_esd_check_func(struct work_struct *work)
{
	unsigned int timer = jiffies_to_msecs(jiffies - irq_timer);

	NVT_DBG("esd_check = %d (retry %d)\n", esd_check, esd_retry);

	if ((timer > NVT_TOUCH_ESD_CHECK_PERIOD) && esd_check) {
		mutex_lock(&ts->lock);
		NVT_ERR("do ESD recovery, timer = %d, retry = %d\n", timer, esd_retry);
#if SPI_FLASH
		/* do esd recovery, bootloader reset */
		nvt_bootloader_reset();
#else
		/* do esd recovery, reload fw */
		nvt_update_firmware(get_fw_name(), 1);
#endif // SPI_FLASH
		mutex_unlock(&ts->lock);
		/* update interrupt timer */
		irq_timer = jiffies;
		/* update esd_retry counter */
		esd_retry++;
	}

	if (ts->bTouchIsAwake)
		queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
				msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
}
#endif /* #if NVT_TOUCH_ESD_PROTECT */

#define PEN_DATA_LEN 14
#if CHECK_PEN_DATA_CHECKSUM
static int32_t nvt_ts_pen_data_checksum(uint8_t *buf, uint8_t length)
{
	uint8_t checksum = 0;
	int32_t i = 0;

	// Calculate checksum
	for (i = 0; i < length - 1; i++) {
		checksum += buf[i];
	}
	checksum = (~checksum + 1);

	// Compare ckecksum and dump fail data
	if (checksum != buf[length - 1]) {
		NVT_ERR("pen packet checksum not match. (buf[%d]=0x%02X, checksum=0x%02X)\n",
			length - 1, buf[length - 1], checksum);
		//--- dump pen buf ---
		for (i = 0; i < length; i++) {
			printk("%02X ", buf[i]);
		}
		printk("\n");

		return -EPERM;
	}

	return 0;
}
#endif // #if CHECK_PEN_DATA_CHECKSUM

#if NVT_TOUCH_WDT_RECOVERY
static uint8_t recovery_cnt;
static uint8_t nvt_wdt_fw_recovery(uint8_t *point_data)
{
	uint32_t recovery_cnt_max = 10;
	uint8_t recovery_enable = false;
	uint8_t i = 0;

	recovery_cnt++;

	/* check pattern */
	for (i = 1 ; i < 7 ; i++) {
		if ((point_data[i] != 0xFD) && (point_data[i] != 0xFE)) {
			recovery_cnt = 0;
			break;
		}
	}

	if (recovery_cnt > recovery_cnt_max) {
		recovery_enable = true;
		recovery_cnt = 0;
	}

	return recovery_enable;
}
#endif	/* #if NVT_TOUCH_WDT_RECOVERY */

void nvt_read_fw_history(uint32_t fw_history_addr)
{
	uint8_t i, j;
	uint8_t buf[65];
	char *str = ts->history_buf;
	char *log_str_tmp = str;
	int idx;
	int str_len = sizeof(ts->history_buf);

	if (fw_history_addr == 0)
		return;

	nvt_set_page(fw_history_addr);

	buf[0] = (uint8_t) (fw_history_addr & 0x7F);
	/* read 64 bytes history */
	CTP_SPI_READ(ts->client, buf, 64 + 1);

	/* print all data */
	NVT_LOG("fw history 0x%x:\n", fw_history_addr);
	memset(str, 0, str_len);
	idx = 0;
	for (j = 0; j < 4; j++) {
		log_str_tmp = str + idx;
		idx += scnprintf(str + idx, str_len - idx, "\t");
		for (i = 1; i <= 16; i++) {
			idx += scnprintf(str + idx, str_len - idx,
				"%02x", (uint8_t)buf[i + j * 16]);
			if (i % 8 == 0)
				idx += scnprintf(str + idx, str_len - idx, "    ");
			else
				idx += scnprintf(str + idx, str_len - idx, " ");
		}
		idx += scnprintf(str + idx, str_len - idx, "\n");
		NVT_LOG("%s", log_str_tmp);
	}
}


#if POINT_DATA_CHECKSUM
static int32_t nvt_ts_point_data_checksum(uint8_t *buf, uint8_t length)
{
	uint8_t checksum = 0;
	int32_t i = 0;

	// Generate checksum
	for (i = 0; i < length - 1; i++) {
		checksum += buf[i + 1];
	}
	checksum = (~checksum + 1);

	// Compare ckecksum and dump fail data
	if (checksum != buf[length]) {
		NVT_ERR("i2c/spi packet checksum not match. (point_data[%d]=0x%02X, checksum=0x%02X)\n",
			length, buf[length], checksum);

		for (i = 0; i < 10; i++) {
			NVT_LOG("%02X %02X %02X %02X %02X %02X\n",
				buf[1 + i*6], buf[2 + i*6], buf[3 + i*6],
				buf[4 + i*6], buf[5 + i*6], buf[6 + i*6]);
		}

		NVT_LOG("%02X %02X %02X %02X %02X\n", buf[61], buf[62], buf[63], buf[64],
			buf[65]);

		return -EPERM;
	}

	return 0;
}
#endif /* POINT_DATA_CHECKSUM */

static void process_usi_responses(uint16_t info_buf_flags, const uint8_t *info_buf)
{
	if (info_buf_flags & USI_NORMAL_PAIR_FLAG) {
		struct g_usi_pairing_info  pairing_info;
		u8 usi_version = 0x20;	/* USI version is always 2.0 */

		pairing_info.hash_id[0] = *(info_buf + USI_HASH_ID_OFFSET);
		pairing_info.hash_id[1] = *(info_buf + USI_HASH_ID_OFFSET + 1);
		pairing_info.session_id[0] = *(info_buf + USI_SESSION_ID_OFFSET);
		pairing_info.session_id[1] = *(info_buf + USI_SESSION_ID_OFFSET + 1);
		pairing_info.freq_sel = *(info_buf + USI_FREQ_SEED_OFFSET);
		pairing_info.additional_data = NULL;
		goog_usi_update_status(ts->g_usi_handle,
				       G_USI_UPDATE_NORMAL_PAIRING_DONE, &pairing_info);
		goog_usi_report_usi_version(ts->g_usi_handle, &usi_version);
	}

	if (info_buf_flags & USI_FAST_PAIR_FLAG)
		goog_usi_update_status(ts->g_usi_handle,
				       G_USI_UPDATE_FAST_PAIRING_DONE, NULL);

	/*
	 * Sometimes, pairing and other USI information comes at the same time.
	 * Pairing information should be sent before any other information so
	 * the following flags shouldn't be handled before handling XXXX_PAIR_FLAG
	 */

	if (info_buf_flags & USI_GID_FLAG) {
		goog_usi_report_gid(ts->g_usi_handle, info_buf + USI_GID_OFFSET);
		goog_usi_set_drvdata(ts->g_usi_handle,
				     (void *)(uintptr_t)*(info_buf + USI_PEN_MODEL_IDX_OFFSET));
	}

	if (info_buf_flags & USI_BATTERY_FLAG) {
		goog_usi_report_battery(ts->g_usi_handle, info_buf + USI_BATTERY_OFFSET);
	}

	if (info_buf_flags & USI_FW_VERSION_FLAG)
		goog_usi_report_fw_version(ts->g_usi_handle, info_buf + USI_FW_VERSION_OFFSET);

	if (info_buf_flags & USI_CAPABILITY_FLAG)
		goog_usi_report_capability(ts->g_usi_handle, info_buf + USI_CAPABILITY_OFFSET);
}

static irqreturn_t nvt_ts_isr(int irq, void *handle)
{
	struct nvt_ts_data *ts = (struct nvt_ts_data *)handle;

	ts->timestamp = ktime_get();
	pm_wakeup_event(&ts->client->dev, MSEC_PER_SEC);

	return IRQ_WAKE_THREAD;
}

/*
 * Please note any size changed need to check one by one
 * for where to be used.
 */
#define POINT_DATA_LEN 65 /* basic point data size */
#define POINT_DATA_LEN_EXTRA 29 /* only for high resolution case */
/*******************************************************
Description:
	Novatek touchscreen work function.

return:
	n.a.
*******************************************************/
static irqreturn_t nvt_ts_work_func(int irq, void *data)
{
	int32_t ret = -1;
	uint32_t position = 1;
	uint32_t input_x = 0;
	uint32_t input_y = 0;
	uint32_t input_w = 0;
	uint32_t input_p = 0;
	uint8_t input_id = 0;
	uint8_t input_status = 0;
	uint8_t press_id[TOUCH_MAX_FINGER_NUM] = {0};
	int32_t i = 0;
	int32_t finger_cnt = 0;
	struct g_usi_event usi_event;
	struct g_usi_event_leave usi_event_leave;
	uint8_t touch_freq_index;
	uint8_t pen_freq_index;
	uint8_t info_buf[INFO_BUF_SIZE] = {0};
	uint16_t info_buf_flags;
	char trace_tag[128];
	ktime_t pen_ktime;
	uint8_t *point_data = ts->point_data;
	uint16_t spi_read_len = POINT_DATA_LEN + 1;

	if (!ts->probe_done)
		return IRQ_HANDLED;

	if (ts->bTouchIsAwake == false && ts->irq_enabled == false) {
		u32 locks = goog_pm_wake_get_locks(ts->gti);

		NVT_LOG("Skipping stray interrupt, locks %#x wkg_option %#x!\n",
			locks, ts->wkg_option);
		return IRQ_HANDLED;
	}

	if (ts->wkg_option != WAKEUP_GESTURE_OFF && ts->bTouchIsAwake == false)
		pm_wakeup_event(&ts->input_dev->dev, 5 * MSEC_PER_SEC);
	else
		pm_wakeup_event(&ts->client->dev, MSEC_PER_SEC);

	mutex_lock(&ts->lock);

	if (ts->pen_support)
		spi_read_len += PEN_DATA_LEN;
	if (ts->high_resolution_enabled)
		spi_read_len += POINT_DATA_LEN_EXTRA;
	ret = CTP_SPI_READ(ts->client, point_data, spi_read_len);

	if (ret < 0) {
		NVT_ERR("CTP_SPI_READ failed.(%d)\n", ret);
		goto XFER_ERROR;
	}
	/*
		//--- dump SPI buf ---
		for (i = 0; i < 10; i++) {
			printk("%02X %02X %02X %02X %02X %02X  ",
				point_data[1+i*6], point_data[2+i*6], point_data[3+i*6],
				point_data[4+i*6], point_data[5+i*6], point_data[6+i*6]);
		}
		printk("\n");
	*/

#if NVT_TOUCH_WDT_RECOVERY
	/* ESD protect by WDT */
	if (nvt_wdt_fw_recovery(point_data)) {
		NVT_ERR("Recover for fw reset, %02X\n", point_data[1]);
		if (point_data[1] == 0xFE)
			nvt_sw_reset_idle();
		nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT0);
		nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT1);
		nvt_update_firmware(get_fw_name(), 1);
		goto XFER_ERROR;
	}
#endif /* #if NVT_TOUCH_WDT_RECOVERY */

	/* ESD protect by FW handshake */
	if (nvt_fw_recovery(point_data)) {
#if NVT_TOUCH_ESD_PROTECT
		nvt_esd_check_enable(true);
#endif /* #if NVT_TOUCH_ESD_PROTECT */
		goto XFER_ERROR;
	}

#if POINT_DATA_CHECKSUM
	if (POINT_DATA_LEN >= POINT_DATA_CHECKSUM_LEN) {
		ret = nvt_ts_point_data_checksum(point_data, POINT_DATA_CHECKSUM_LEN);
		if (ret) {
			goto XFER_ERROR;
		}
	}
#endif /* POINT_DATA_CHECKSUM */

	if (ts->wkg_option != WAKEUP_GESTURE_OFF && ts->bTouchIsAwake == false) {
		input_id = (uint8_t)(point_data[1] >> 3);
		nvt_ts_wakeup_gesture_report(input_id, point_data);
		mutex_unlock(&ts->lock);
		return IRQ_HANDLED;
	}

	finger_cnt = 0;

	goog_input_lock(ts->gti);
	goog_input_set_timestamp(ts->gti, ts->input_dev, ts->timestamp);

#if NVT_MT_CUSTOM
	switch (point_data[position] & 0x07) {
	case PALM_TOUCH:
		input_report_abs(ts->input_dev, ABS_MT_CUSTOM, PALM_TOUCH);
		break;
	case GRIP_TOUCH:
		input_report_abs(ts->input_dev, ABS_MT_CUSTOM, GRIP_TOUCH);
		break;
	default:
		input_report_abs(ts->input_dev, ABS_MT_CUSTOM, 0);
	}
#endif

	for (i = 0; i < ts->max_touch_num; i++) {
		position = 1 + 6 * i;
		input_id = (uint8_t)(point_data[position + 0] >> 3);
		input_status = (uint8_t)(point_data[position + 0] & 0x07);
		if ((input_id == 0) || (input_id > ts->max_touch_num))
			continue;

		if (((point_data[position] & 0x07) == 0x01) ||
		    ((point_data[position] & 0x07) == 0x02)) {	//finger down (enter & moving)
#if NVT_TOUCH_ESD_PROTECT
			/* update interrupt timer */
			irq_timer = jiffies;
#endif /* #if NVT_TOUCH_ESD_PROTECT */
			if (ts->high_resolution_enabled) {
				input_x = (uint32_t)(point_data[position + 1] << 8) +
					(uint32_t) (point_data[position + 2]);
				input_y = (uint32_t)(point_data[position + 3] << 8) +
					(uint32_t) (point_data[position + 4]);
			} else {
				input_x = (uint32_t)(point_data[position + 1] << 4) +
					(uint32_t) (point_data[position + 3] >> 4);
				input_y = (uint32_t)(point_data[position + 2] << 4) +
					(uint32_t) (point_data[position + 3] & 0x0F);
			}
			if ((input_x < 0) || (input_y < 0))
				continue;
			if ((input_x > ts->abs_x_max) || (input_y > ts->abs_y_max))
				continue;

			if (ts->high_resolution_enabled)
				input_w = (uint32_t)(point_data[position + 5]);
			else
				input_w = (uint32_t)(point_data[position + 4]);

			if (input_w == 0)
				input_w = 1;
#ifdef TOUCH_FORCE_NUM
			if (i < 2) {
				input_p = (uint32_t)(point_data[position + 5]) + (uint32_t)(
						  point_data[i + 63] << 8);
				if (input_p > TOUCH_FORCE_NUM)
					input_p = TOUCH_FORCE_NUM;
			} else {
				input_p = (uint32_t)(point_data[position + 5]);
			}
#else

			if (ts->high_resolution_enabled)
				input_p = (uint32_t)(point_data[i + 99]);
			else
				input_p = (uint32_t)(point_data[position + 5]);

#endif
			if (input_p == 0)
				input_p = 1;

			press_id[input_id - 1] = 1;

			if (ts->report_protocol == REPORT_PROTOCOL_B) {
				goog_input_mt_slot(ts->gti, ts->input_dev, input_id - 1);
				goog_input_mt_report_slot_state(ts->gti, ts->input_dev,
						MT_TOOL_FINGER, true);
			}

			if (ts->report_protocol == REPORT_PROTOCOL_A) {
				input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, input_id - 1);
				input_report_key(ts->input_dev, BTN_TOUCH, 1);
			}

			goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_POSITION_X, input_x);
			goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_POSITION_Y, input_y);
			goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_TOUCH_MAJOR, input_w);
			goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_PRESSURE, input_p);

			if (ts->report_protocol == REPORT_PROTOCOL_A)
				input_mt_sync(ts->input_dev);

			finger_cnt++;
		}
	}

	if (ts->report_protocol == REPORT_PROTOCOL_B) {
		for (i = 0; i < ts->max_touch_num; i++) {
			if (press_id[i] != 1) {
				goog_input_mt_slot(ts->gti, ts->input_dev, i);
				goog_input_report_abs(ts->gti, ts->input_dev,
						ABS_MT_TOUCH_MAJOR, 0);
				goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_PRESSURE, 0);
				goog_input_mt_report_slot_state(ts->gti, ts->input_dev,
					MT_TOOL_FINGER, false);
			}
		}
	}


	goog_input_report_key(ts->gti, ts->input_dev, BTN_TOUCH, (finger_cnt > 0));

	if (ts->report_protocol == REPORT_PROTOCOL_A && finger_cnt == 0) {
#ifdef ABS_MT_CUSTOM
		input_report_abs(ts->input_dev, ABS_MT_CUSTOM, 0);
#endif
		input_report_key(ts->input_dev, BTN_TOUCH, 0);
		input_mt_sync(ts->input_dev);
	}

#if TOUCH_KEY_NUM > 0
	if (point_data[61] == 0xF8) {
#if NVT_TOUCH_ESD_PROTECT
		/* update interrupt timer */
		irq_timer = jiffies;
#endif /* #if NVT_TOUCH_ESD_PROTECT */
		for (i = 0; i < ts->max_button_num; i++) {
			input_report_key(ts->input_dev, touch_key_array[i],
					 ((point_data[62] >> i) & 0x01));
		}
	} else {
		for (i = 0; i < ts->max_button_num; i++) {
			input_report_key(ts->input_dev, touch_key_array[i], 0);
		}
	}
#endif

	goog_input_sync(ts->gti, ts->input_dev);
	goog_input_unlock(ts->gti);

	/* Replace button status with heatmap compression length. */
	if (ts->heatmap_data_type == HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP) {
		ts->touch_heatmap_comp_len = (((point_data[62] & 0x0F) << 8) + point_data[61]) * 2;
		NVT_DBG("heatmap_comp_len: %d\n", ts->touch_heatmap_comp_len);
		if (ts->touch_heatmap_comp_len == 0)
			nvt_set_heatmap_host_cmd(ts, true);
	} else {
		ts->touch_heatmap_comp_len = 0;
	}

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	if (ts->heatmap_data_type && finger_cnt) {
		uint8_t *spi_buf = NULL;
		uint32_t spi_buf_size = 0;
		uint32_t spi_read_size = 0;

		/* Set heatmap type by host cmd. */
		nvt_set_heatmap_host_cmd(ts, true);

		switch (ts->heatmap_data_type) {
		case HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP:
			spi_buf = ts->heatmap_spi_buf;
			spi_buf_size = ts->heatmap_spi_buf_size;
			/* Extra 1 byte for SPI header. */
			spi_read_size = ts->touch_heatmap_comp_len + 1;
			break;
		case HEATMAP_DATA_TYPE_TOUCH_STRENGTH:
			spi_buf = ts->heatmap_spi_buf;
			spi_buf_size = ts->heatmap_spi_buf_size;
			spi_read_size = spi_buf_size;
			break;
		default:
			spi_buf = ts->extra_spi_buf;
			spi_buf_size = ts->extra_spi_buf_size;
			spi_read_size = spi_buf_size;
			break;
		}

		if (!spi_buf || !spi_buf_size || !spi_read_size ||
			spi_read_size > spi_buf_size) {
			NVT_ERR("buffer is not ready for heatmap(%d) or invalid size(%d > %d)!\n",
				ts->heatmap_data_type, spi_read_size, spi_buf_size);
		} else {
			nvt_set_page(ts->heatmap_host_cmd_addr);
			spi_buf[0] = ts->heatmap_host_cmd_addr & 0x7F;
			CTP_SPI_READ(ts->client, spi_buf, spi_read_size);
			nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
			if (ts->heatmap_data_type == HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP) {
				/* Skip 1 byte header to the data start. */
				nvt_heatmap_decode(spi_buf + 1, ts->touch_heatmap_comp_len,
						ts->heatmap_out_buf, ts->heatmap_out_buf_size);
			}
		}
	}
#endif	/* !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE) */

	if (ts->pen_support) {
		/*
		//--- dump pen buf ---
		printk("%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
		point_data[66], point_data[67], point_data[68], point_data[69], point_data[70],
		point_data[71], point_data[72], point_data[73], point_data[74], point_data[75],
		point_data[76], point_data[77], point_data[78], point_data[79]);
		*/
#if CHECK_PEN_DATA_CHECKSUM
		if (nvt_ts_pen_data_checksum(&point_data[66], PEN_DATA_LEN)) {
			// pen data packet checksum not match, skip it
			goto XFER_ERROR;
		}
#endif // #if CHECK_PEN_DATA_CHECKSUM

		// parse and handle pen report
		ts->pen_format_id = point_data[66];
		if (ts->pen_format_id != 0xFF) {
			if (ts->pen_format_id == 0x01) {
				pen_ktime = ktime_get();
				scnprintf(trace_tag, sizeof(trace_tag),
					"stylus-active: IN_TS=%lld TS=%lld DELTA=%lld ns.\n",
					ktime_to_ns(ts->timestamp), ktime_to_ns(pen_ktime),
					ktime_to_ns(ktime_sub(pen_ktime, ts->timestamp)));
				ATRACE_BEGIN(trace_tag);
				// report pen data
				usi_event.timestamp = ts->timestamp;
				usi_event.x = (uint32_t)(point_data[67] << 8) +
						(uint32_t)(point_data[68]);
				usi_event.y = (uint32_t)(point_data[69] << 8) +
						(uint32_t)(point_data[70]);
				usi_event.pressure = (uint32_t)(point_data[71] << 8) +
						(uint32_t)(point_data[72]);
				usi_event.tilt_x = (int32_t)(int8_t)point_data[73];
				usi_event.tilt_y = (int32_t)(int8_t)point_data[74];
#ifdef PEN_DISTANCE_SUPPORT
				usi_event.distance = (uint32_t)(point_data[75] << 8) +
						(uint32_t)(point_data[76]);
#endif
				usi_event.buttons = (uint32_t)point_data[77];
//				printk("x=%d,y=%d,p=%d,tx=%d,ty=%d,d=%d,b1=%d,b2=%d,b3=%d,bat=%d\n", pen_x, pen_y, pen_pressure,
//						pen_tilt_x, pen_tilt_y, pen_distance, pen_btn1, pen_btn2, pen_btn3, pen_battery);

				/* Snapshot some stylus context information for
				 * offload
				 */
				if (!ts->pen_active) {
					ts->pen_active = 1;
				}
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
				ts->pen_offload_coord.status = COORD_STATUS_PEN;
				ts->pen_offload_coord.x = usi_event.x;
				ts->pen_offload_coord.y = usi_event.y;
				ts->pen_offload_coord.pressure = usi_event.pressure;
#endif
				ts->pen_offload_coord_timestamp = usi_event.timestamp;

				goog_usi_send_event(ts->g_usi_handle, &usi_event);

				info_buf_flags = point_data[63] + (point_data[64] << 8);

				if (info_buf_flags) {
					nvt_set_page(ts->mmap->EB_INFO_ADDR);
					info_buf[0] = ts->mmap->EB_INFO_ADDR & 0x7F;
					CTP_SPI_READ(ts->client, info_buf, INFO_BUF_SIZE);
					nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

					process_usi_responses(info_buf_flags, info_buf);
				}

				ATRACE_END();
			} else if (ts->pen_format_id == 0xF0) {
				// report Pen ID
			} else {
				NVT_ERR("Unknown pen format id!\n");
				goto XFER_ERROR;
			}
		} else if (ts->pen_active) { // pen_format_id = 0xFF and a pen was reporting
			pen_ktime = ktime_get();
			scnprintf(trace_tag, sizeof(trace_tag),
				"stylus-inactive: IN_TS=%lld TS=%lld DELTA=%lld ns.\n",
				ktime_to_ns(ts->timestamp), ktime_to_ns(pen_ktime),
				ktime_to_ns(ktime_sub(pen_ktime, ts->timestamp)));
			ATRACE_BEGIN(trace_tag);

			/* Snapshot some stylus context information for offload */
			ts->pen_active = 0;
			ts->pen_offload_coord_timestamp = ts->timestamp;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
			memset(&ts->pen_offload_coord, 0,
			       sizeof(ts->pen_offload_coord));
#endif
			usi_event_leave.timestamp = ts->timestamp;
			goog_usi_send_event_leave(ts->g_usi_handle, &usi_event_leave);

			ATRACE_END();
		}
	} /* if (ts->pen_support) */

	/* Check any sensing freq hopping for touch or stylus. */
#if (TOUCH_KEY_NUM == 0)
	touch_freq_index = (point_data[62] & 0x70) >> 4;
	pen_freq_index = (point_data[62] & 0x80) >> 7;
	if (ts->touch_freq_index != touch_freq_index) {
		NVT_LOG("Touch freq hopping from %d to %d!\n",
			ts->touch_freq_index, touch_freq_index);
		ts->touch_freq_index = touch_freq_index;
	}
	if (ts->pen_freq_index != pen_freq_index) {
		NVT_LOG("Pen freq hopping from %d to %d!\n",
			ts->pen_freq_index, pen_freq_index);
		ts->pen_freq_index = pen_freq_index;
	}
#endif

XFER_ERROR:

	mutex_unlock(&ts->lock);
	return IRQ_HANDLED;
}

#if SPI_FLASH
/*******************************************************
Description:
	Novatek touchscreen check and stop crc reboot loop.

return:
	n.a.
*******************************************************/
void nvt_stop_crc_reboot(void)
{
	uint8_t buf[8] = {0};
	int32_t retry = 0;

	//read unexpected buffer to check CRC fail reboot is happening or not

	//---change SPI index to prevent geting 0xFF, but not 0xFC---
	nvt_set_page(CHIP_VER_TRIM_ADDR);

	//---read to check if buf is 0xFC which means IC is in CRC reboot ---
	buf[0] = CHIP_VER_TRIM_ADDR & 0xFF;
	CTP_SPI_READ(ts->client, buf, 4);

	if ((buf[1] == 0xFC) ||
		((buf[1] == 0xFF) && (buf[2] == 0xFF) && (buf[3] == 0xFF))) {

		//IC is in CRC fail reboot loop, needs to be stopped!
		for (retry = 5; retry > 0; retry--) {

			//---write spi cmds to reset idle : 1st---
			nvt_sw_reset_idle_no_delay();

			//---write spi cmds to reset idle : 2rd---
			nvt_sw_reset_idle_no_delay();
			msleep(1);

			//---clear CRC_ERR_FLAG---
			nvt_set_page(CRC_ERR_FLAG_ADDR);

			buf[0] = CRC_ERR_FLAG_ADDR & 0xFF;
			buf[1] = 0xA5;
			CTP_SPI_WRITE(ts->client, buf, 2);

			//---check CRC_ERR_FLAG---
			nvt_set_page(CRC_ERR_FLAG_ADDR);

			buf[0] = CRC_ERR_FLAG_ADDR & 0xFF;
			buf[1] = 0x00;
			CTP_SPI_READ(ts->client, buf, 2);

			if (buf[1] == 0xA5)
				break;
		}
		if (retry == 0)
			NVT_ERR("CRC auto reboot is not able to be stopped! buf[1]=0x%02X\n", buf[1]);
	}

	return;
}
#endif // SPI_FLASH

/*******************************************************
Description:
	Novatek touchscreen check chip version trim function.

return:
	Executive outcomes. 0---NVT IC. -1---not NVT IC.
*******************************************************/
static int8_t nvt_ts_check_chip_ver_trim(uint32_t chip_ver_trim_addr)
{
	uint8_t buf[8] = {0};
	int32_t retry = 0;
	int32_t list = 0;
	int32_t i = 0;
	int32_t found_nvt_chip = 0;
	int32_t ret = -1;

#if SPI_FLASH
	nvt_bootloader_reset(); // NOT in retry loop
#endif // SPI_FLASH

	//---Check for 5 times---
	for (retry = 5; retry > 0; retry--) {

#if SPI_FLASH
		nvt_sw_reset_idle();
#else
		nvt_bootloader_reset();
#endif // SPI_FLASH
		nvt_set_page(chip_ver_trim_addr);

		buf[0] = chip_ver_trim_addr & 0x7F;
		buf[1] = 0x00;
		buf[2] = 0x00;
		buf[3] = 0x00;
		buf[4] = 0x00;
		buf[5] = 0x00;
		buf[6] = 0x00;
		CTP_SPI_READ(ts->client, buf, 7);
		NVT_LOG("buf[1]=0x%02X, buf[2]=0x%02X, buf[3]=0x%02X, buf[4]=0x%02X, buf[5]=0x%02X, buf[6]=0x%02X\n",
			buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

#if SPI_FLASH
		//---Stop CRC check to prevent IC auto reboot---
		if ((buf[1] == 0xFC) ||
				((buf[1] == 0xFF) && (buf[2] == 0xFF) && (buf[3] == 0xFF))) {
			nvt_stop_crc_reboot();
			continue;
		}
#endif // SPI_FLASH

		// compare read chip id on supported list
		for (list = 0;
		     list < (sizeof(trim_id_table) / sizeof(struct nvt_ts_trim_id_table));
		     list++) {
			found_nvt_chip = 0;

			// compare each byte
			for (i = 0; i < NVT_ID_BYTE_MAX; i++) {
				if (trim_id_table[list].mask[i]) {
					if (buf[i + 1] != trim_id_table[list].id[i])
						break;
				}
			}

			if (i == NVT_ID_BYTE_MAX) {
				found_nvt_chip = 1;
			}

			if (found_nvt_chip) {
				NVT_LOG("This is NVT touch IC\n");
#if defined(CONFIG_SOC_GOOGLE)
				ts->trim_table = &trim_id_table[list];
#endif
				ts->mmap = trim_id_table[list].mmap;
				ts->hw_crc = trim_id_table[list].hwinfo->hw_crc;
				ret = 0;
				goto out;
			} else {
				ts->mmap = NULL;
				ret = -1;
			}
		}

		msleep(10);
	}

out:
	return ret;
}

#if defined(CONFIG_DRM_PANEL)
#if defined(CONFIG_SOC_GOOGLE)
static int nvt_ts_check_dt(struct nvt_ts_data *ts)
{
	int index;
	int ret = 0;
	struct of_phandle_args panelmap;
	struct drm_panel *panel = NULL;
	struct device *dev = &ts->client->dev;
	struct device_node *np = dev->of_node;
	const char *name;

	if (of_property_read_bool(np, "novatek,panel_map")) {
		for (index = 0 ;; index++) {
			ret = of_parse_phandle_with_fixed_args(np,
							       "novatek,panel_map",
							       1,
							       index,
							       &panelmap);
			if (ret)
				return -EPROBE_DEFER;

			panel = of_drm_find_panel(panelmap.np);
			of_node_put(panelmap.np);
			if (!IS_ERR_OR_NULL(panel)) {
				ts->active_panel = panel;
				ts->initial_panel_index = panelmap.args[0];
				break;
			}
		}
	}

	if (ts->active_panel) {
		name = NULL;
		of_property_read_string_index(np, "novatek,firmware_names",
				ts->initial_panel_index, &name);
		if (name)
			ts->fw_name = name;
		else
			ts->fw_name = BOOT_UPDATE_FIRMWARE_NAME;
		NVT_LOG("fw_name: %s.\n", ts->fw_name);

		name = NULL;
#if !SPI_FLASH
		of_property_read_string_index(np, "novatek,mp_firmware_names",
				ts->initial_panel_index, &name);
		if (name)
			ts->mp_fw_name = name;
		else
			ts->mp_fw_name = MP_UPDATE_FIRMWARE_NAME;
		NVT_LOG("mp_fw_name: %s.\n", ts->mp_fw_name);
#endif // !SPI_FLASH
	}

	return 0;
}
#elif defined(CONFIG_ARCH_QCOM) || defined(CONFIG_ARCH_MSM)
static int nvt_ts_check_dt(struct nvt_ts_data *ts)
{
	int i;
	int count;
	struct device_node *node;
	struct drm_panel *panel;
	struct device *dev = &ts->client->dev;
	struct device_node *np = dev->of_node;

	count = of_count_phandle_with_args(np, "panel", NULL);
	if (count <= 0)
		return 0;

	for (i = 0; i < count; i++) {
		node = of_parse_phandle(np, "panel", i);
		panel = of_drm_find_panel(node);
		of_node_put(node);
		if (!IS_ERR(panel)) {
			ts->active_panel = panel;
			return 0;
		}
	}

	return PTR_ERR(panel);
}
#endif
#endif

static int32_t nvt_usi_send_uplink(g_usi_handle_t handle, uint8_t beacon[5], uint8_t response[3])
{
	return nvt_get_usi_data_diag(beacon, response);
}

static struct g_usi_callbacks nvt_usi_cbs = {
	.g_usi_send_uplink = nvt_usi_send_uplink,
};

/*******************************************************
Description:
	Novatek touchscreen driver probe function.

return:
	Executive outcomes. 0---succeed. negative---failed
*******************************************************/
static int32_t nvt_ts_probe(struct spi_device *client)
{
	int32_t ret = 0;
#if (TOUCH_KEY_NUM || WAKEUP_GESTURE_DEFAULT)
	int32_t retry = 0;
#endif
#ifdef CONFIG_OF
	uint32_t mt_touch_major_res = 0, mt_pos_x_res = 0, mt_pos_y_res = 0;
#endif

	NVT_LOGI("start\n");

	ts = (struct nvt_ts_data *)kzalloc(sizeof(struct nvt_ts_data), GFP_KERNEL);
	if (ts == NULL) {
		NVT_ERR("failed to allocated memory for nvt ts data\n");
		return -ENOMEM;
	}

	ts->xbuf = (uint8_t *)kzalloc(NVT_XBUF_LEN, GFP_KERNEL);
	if (ts->xbuf == NULL) {
		NVT_ERR("kzalloc for xbuf failed!\n");
		ret = -ENOMEM;
		goto err_malloc_xbuf;
	}

	ts->rbuf = (uint8_t *)kzalloc(NVT_READ_LEN, GFP_KERNEL);
	if (ts->rbuf == NULL) {
		NVT_ERR("kzalloc for rbuf failed!\n");
		ret = -ENOMEM;
		goto err_malloc_rbuf;
	}

	ts->client = client;
	spi_set_drvdata(client, ts);

#if defined(CONFIG_DRM_PANEL)
	ret = nvt_ts_check_dt(ts);
	if (ret == -EPROBE_DEFER) {
		NVT_LOG("Defer probe because panel is not ready!\n");
		goto err_check_dt;
	}

	if (ret) {
		NVT_ERR("nvt_ts_check_dt: failed(%d)!\n", ret);
		ret = -EPROBE_DEFER;
		goto err_check_dt;
	}
#endif

	ts->pinctrl = devm_pinctrl_get(&client->dev);
	if (IS_ERR_OR_NULL(ts->pinctrl)) {
		NVT_ERR("Could not get pinctrl!\n");
	} else {
		nvt_pinctrl_configure(ts, true);
	}

	//---prepare for spi parameter---
	if (ts->client->master->flags & SPI_MASTER_HALF_DUPLEX) {
		NVT_ERR("Full duplex not supported by master\n");
		ret = -EIO;
		goto err_ckeck_full_duplex;
	}
#if defined(CONFIG_SOC_GOOGLE)
	ts->client->rt = true;
#endif
	ts->client->bits_per_word = 8;
	ts->client->mode = SPI_MODE_0;

	ret = spi_setup(ts->client);
	if (ret < 0) {
		NVT_ERR("Failed to perform SPI setup\n");
		goto err_spi_setup;
	}

#ifdef NVT_TS_PANEL_BRIDGE
	INIT_DELAYED_WORK(&ts->suspend_work, nvt_ts_suspend_work);
	INIT_DELAYED_WORK(&ts->resume_work, nvt_ts_resume_work);
#endif
	ts->event_wq = alloc_workqueue("nvt_event_wq", WQ_UNBOUND |
				       WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!ts->event_wq) {
		NVT_ERR("Cannot create work thread\n");
		ret = -ENOMEM;
		goto err_alloc_workqueue;
	}

	init_completion(&ts->bus_resumed);
	complete_all(&ts->bus_resumed);

#ifdef CONFIG_MTK_SPI
	/* old usage of MTK spi API */
	memcpy(&ts->spi_ctrl, &spi_ctrdata, sizeof(struct mt_chip_conf));
	ts->client->controller_data = (void *)&ts->spi_ctrl;
#endif

#ifdef CONFIG_SPI_MT65XX
	/* new usage of MTK spi API */
	memcpy(&ts->spi_ctrl, &spi_ctrdata, sizeof(struct mtk_chip_config));
	ts->client->controller_data = (void *)&ts->spi_ctrl;
#endif

	NVT_LOG("mode=%d, max_speed_hz=%d\n", ts->client->mode,
		ts->client->max_speed_hz);

	//---parse dts---
	ret = nvt_parse_dt(&client->dev);
	if (ret) {
		NVT_ERR("parse dt error\n");
		goto err_parse_dt;
	}

	//---request and config GPIOs---
	ret = nvt_gpio_config(ts);
	if (ret) {
		NVT_ERR("gpio config error!\n");
		goto err_gpio_config_failed;
	}

	mutex_init(&ts->lock);
	mutex_init(&ts->xbuf_lock);
	mutex_init(&ts->bus_mutex);

#if !SPI_FLASH
	//---eng reset before TP_RESX high
	nvt_eng_reset();
#endif // !SPI_FLASH

#if NVT_TOUCH_SUPPORT_HW_RST
	if (gpio_is_valid(ts->reset_gpio))
		gpio_set_value(ts->reset_gpio, 1);
#endif

	// need 10ms delay after POR(power on reset)
	msleep(10);

	//---check chip version trim---
	ret = nvt_ts_check_chip_ver_trim(CHIP_VER_TRIM_ADDR);
	if (ret) {
		NVT_LOG("try to check from old chip ver trim address\n");
		ret = nvt_ts_check_chip_ver_trim(CHIP_VER_TRIM_OLD_ADDR);
		if (ret) {
			NVT_ERR("chip is not identified\n");
			ret = -EPROBE_DEFER;
			goto err_chipvertrim_failed;
		}
	}

#if BOOT_UPDATE_FIRMWARE
	nvt_fwu_wq = alloc_workqueue("nvt_fwu_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!nvt_fwu_wq) {
		NVT_ERR("nvt_fwu_wq create workqueue failed\n");
		ret = -ENOMEM;
		goto err_create_nvt_fwu_wq_failed;
	}
	INIT_DELAYED_WORK(&ts->nvt_fwu_work, Boot_Update_Firmware);
	// please make sure boot update start after display reset(RESX) sequence
#if SPI_FLASH
	init_completion(&ts->fwu_done);
#endif
	queue_delayed_work(nvt_fwu_wq, &ts->nvt_fwu_work,
			msecs_to_jiffies(BOOT_UPDATE_FIRMWARE_MS_DELAY));
#endif

#if SPI_FLASH
	if (wait_for_completion_timeout(&ts->fwu_done,
		msecs_to_jiffies(UPDATE_FIRMWARE_TIMEOUT)) == 0) {
		complete_all(&ts->fwu_done);
		NVT_LOGE("nvt wait for fwu_done timeout\n");
	}
	nvt_clear_fw_reset_state();
	nvt_bootloader_reset();
	nvt_check_fw_reset_state(RESET_STATE_INIT);
	nvt_get_fw_info();
	// Setup default tvcl ibias mode. Mode value can later be altered by sysfs node
	switch (ts->nvt_pid) {
	case TKI3_BOE:
	case TKI3_CSOT:
		// b/374674968: set 0.3/max as default for self-test.
		ts->mp_tvcl_mode = MODE_1;
		ts->mp_ibias_mode = MODE_2;
		break;
	default:
		ts->mp_tvcl_mode = 0;
		ts->mp_ibias_mode = 0;
	}

	if (ts->mp_tvcl_mode || ts->mp_ibias_mode) {
		NVT_LOG("set default mp_tvcl_mode = %d, mp_ibias_mode = %d\n",
				ts->mp_tvcl_mode, ts->mp_ibias_mode);
	}
#else
	/*
	 * For hostdl project, the touch W/H will update later by nvt_get_fw_info()
	 * from nvt_update_firmware() during Boot_Update_Firmware() process.
	 */
	ts->touch_width = TOUCH_DEFAULT_MAX_WIDTH;
	ts->touch_height = TOUCH_DEFAULT_MAX_HEIGHT;
#endif // SPI_FLASH

	ts->abs_x_max = ts->touch_width - 1;
	ts->abs_y_max = ts->touch_height - 1;

	ts->point_data_alloc_sz = POINT_DATA_LEN + PEN_DATA_LEN + 1 + DUMMY_BYTES;
	if (ts->res_scale > 1) {
		NVT_LOG("High resolution reporting enabled.\n");
		ts->high_resolution_enabled = true;
	}
	ts->point_data_alloc_sz += POINT_DATA_LEN_EXTRA;
	ts->point_data_alloc_sz = ALIGN(ts->point_data_alloc_sz, 32);
	ts->point_data = devm_kzalloc(&ts->client->dev, ts->point_data_alloc_sz, GFP_KERNEL);
	if (ts->point_data == NULL) {
		NVT_ERR("allocate point_data failed\n");
		ret = -ENOMEM;
		goto err_input_dev_alloc_failed;
	}

	//---allocate input device---
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		NVT_ERR("allocate input device failed\n");
		ret = -ENOMEM;
		goto err_input_dev_alloc_failed;
	}

	ts->max_touch_num = TOUCH_MAX_FINGER_NUM;

#if TOUCH_KEY_NUM > 0
	ts->max_button_num = TOUCH_KEY_NUM;
#endif

	ts->int_trigger_type = INT_TRIGGER_TYPE;


	//---set input device info.---
	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(
					  EV_ABS);
	ts->input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);
	ts->input_dev->propbit[0] = BIT(INPUT_PROP_DIRECT);
	ts->report_protocol = REPORT_PROTOCOL_B;
#if WAKEUP_GESTURE_DEFAULT
	ts->wkg_default = WAKEUP_GESTURE_DEFAULT;
	ts->wkg_option = WAKEUP_GESTURE_DEFAULT;
#endif

	if (ts->report_protocol == REPORT_PROTOCOL_B)
		input_mt_init_slots(ts->input_dev, ts->max_touch_num, 0);

	input_set_abs_params(ts->input_dev, ABS_MT_PRESSURE, 0,
			MT_PRESSURE_MAX, 0, 0);

#if NVT_MT_CUSTOM
	input_set_abs_params(ts->input_dev, ABS_MT_CUSTOM, 0, 0x8, 0, 0);
#endif

#if TOUCH_MAX_FINGER_NUM > 1
	input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);    //area = 255
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max, 0, 0);
	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max, 0, 0);

#ifdef CONFIG_OF
	/* Get any available resolution from DT and apply them */
	nvt_get_resolutions(&client->dev, &mt_pos_x_res, &mt_pos_y_res,
			    &mt_touch_major_res);

	input_abs_set_res(ts->input_dev, ABS_MT_TOUCH_MAJOR, mt_touch_major_res);
	input_abs_set_res(ts->input_dev, ABS_MT_POSITION_X, mt_pos_x_res);
	input_abs_set_res(ts->input_dev, ABS_MT_POSITION_Y, mt_pos_y_res);
#endif

	if (ts->report_protocol == REPORT_PROTOCOL_A)
		input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, ts->max_touch_num, 0, 0);
#endif //TOUCH_MAX_FINGER_NUM > 1

#if TOUCH_KEY_NUM > 0
	for (retry = 0; retry < ts->max_button_num; retry++) {
		input_set_capability(ts->input_dev, EV_KEY, touch_key_array[retry]);
	}
#endif

#if WAKEUP_GESTURE_DEFAULT
	for (retry = 0;
	     retry < ARRAY_SIZE(gesture_keycode);
	     retry++) {
		if (gesture_keycode[retry])
			input_set_capability(ts->input_dev, EV_KEY, gesture_keycode[retry]);
	}
#endif

	snprintf(ts->phys, sizeof(ts->phys), "input/ts");
	ts->input_dev->name = NVT_TS_NAME;
	ts->input_dev->uniq = ts->input_dev->name;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->dev.parent = &client->dev;
	ts->input_dev->id.bustype = BUS_SPI;
        ts->input_dev->id.vendor = NVT_VENDOR_ID;
        ts->input_dev->id.product = NVT_PRODUCT_ID;
        ts->input_dev->id.version = NVT_VERSION;

	//---register input device---
	ret = input_register_device(ts->input_dev);
	if (ret) {
		NVT_ERR("register input device (%s) failed. ret=%d\n", ts->input_dev->name,
			ret);
		goto err_input_register_device_failed;
	}

	if (ts->pen_support) {
		struct g_usi stylus;

		memset(&stylus, 0, sizeof(stylus));

		/* setup new USI controller */
		stylus.dev = &ts->client->dev;
		stylus.bustype = BUS_SPI;

		stylus.abs_x_max = ts->pen_abs_x_max;
		stylus.abs_y_max = ts->pen_abs_y_max;
		stylus.distance_max = 0;	/* distance is not supported */
		stylus.tilt_min = PEN_TILT_MIN;
		stylus.tilt_max = PEN_TILT_MAX;

		stylus.is_flex_beacon = true;	/* the controller works with the flex beacon */
		stylus.cbs = &nvt_usi_cbs;

		/* register USI controller */
		ts->g_usi_handle = goog_usi_register(&stylus);
		if (ts->g_usi_handle == G_USI_INVALID_HANDLE) {
			NVT_ERR("goog_usi_register failed.\n");
			goto err_goog_usi_register_failed;
		}
	} /* if (ts->pen_support) */

	/* probe google touch interface. */
	ts->gti = goog_touch_interface_probe(ts, &ts->client->dev,
					ts->input_dev, nvt_callback, NULL);
	if (ts->gti == NULL) {
		NVT_ERR("goog_touch_interface probe failed. ret=%d!\n", ret);
		goto err_goog_touch_interface;
	}

	ret = goog_pm_register_notification(ts->gti, &goog_pm_ops);
	if (ret) {
		NVT_ERR("pm register failed. ret=%d!\n", ret);
		goto err_goog_pm_register;
	}

	//---set int-pin & request irq---
	client->irq = gpio_to_irq(ts->irq_gpio);
	if (client->irq) {
		NVT_LOG("int_trigger_type=%d\n", ts->int_trigger_type);
		ts->irq_enabled = true;
		ret = goog_request_threaded_irq(ts->gti, client->irq,
				nvt_ts_isr, nvt_ts_work_func,
				ts->int_trigger_type | IRQF_ONESHOT, NVT_SPI_NAME, ts);
		if (ret != 0) {
			NVT_ERR("request irq failed. ret=%d\n", ret);
			goto err_int_request_failed;
		} else {
			nvt_irq_enable(false);
			NVT_LOG("request irq %d succeed\n", client->irq);
		}
	}

#if WAKEUP_GESTURE_DEFAULT
	device_init_wakeup(&ts->input_dev->dev, 1);
#endif

	NVT_LOG("NVT_TOUCH_ESD_PROTECT is %d\n", NVT_TOUCH_ESD_PROTECT);
#if NVT_TOUCH_ESD_PROTECT
	INIT_DELAYED_WORK(&nvt_esd_check_work, nvt_esd_check_func);
	nvt_esd_check_wq = alloc_workqueue("nvt_esd_check_wq", WQ_MEM_RECLAIM, 1);
	if (!nvt_esd_check_wq) {
		NVT_ERR("nvt_esd_check_wq create workqueue failed\n");
		ret = -ENOMEM;
		goto err_create_nvt_esd_check_wq_failed;
	}
	queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
			   msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));

	irq_timer = 0;
	esd_check = false;
	esd_retry = 0;
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	//---set device node---
#if NVT_TOUCH_EXT_PROC
	ret = nvt_extra_proc_init();
	if (ret != 0) {
		NVT_ERR("nvt extra proc init failed. ret=%d\n", ret);
		goto err_extra_proc_init_failed;
	}
#endif

#if NVT_TOUCH_EXT_API
	ret = nvt_extra_api_init();
	if (ret) {
		NVT_ERR("nvt extra api init failed. ret=%d\n", ret);
		goto err_extra_api_init_failed;
	}
#endif

#if NVT_TOUCH_MP
	ret = nvt_mp_proc_init();
	if (ret != 0) {
		NVT_ERR("nvt mp proc init failed. ret=%d\n", ret);
		goto err_mp_proc_init_failed;
	}
#endif

#if defined(CONFIG_FB)
#if defined(CONFIG_DRM_PANEL) && (defined(CONFIG_ARCH_QCOM) || defined(CONFIG_ARCH_MSM))
	ts->drm_panel_notif.notifier_call = nvt_drm_panel_notifier_callback;
	if (ts->active_panel) {
		ret = drm_panel_notifier_register(ts->active_panel, &ts->drm_panel_notif);
		if (ret < 0) {
			NVT_ERR("register drm_panel_notifier failed. ret=%d\n", ret);
			goto err_register_drm_panel_notif_failed;
		}
	}
#elif defined(_MSM_DRM_NOTIFY_H_)
	ts->drm_notif.notifier_call = nvt_drm_notifier_callback;
	ret = msm_drm_register_client(&ts->drm_notif);
	if (ret) {
		NVT_ERR("register drm_notifier failed. ret=%d\n", ret);
		goto err_register_drm_notif_failed;
	}
#else
	ts->fb_notif.notifier_call = nvt_fb_notifier_callback;
	ret = fb_register_client(&ts->fb_notif);
	if (ret) {
		NVT_ERR("register fb_notifier failed. ret=%d\n", ret);
		goto err_register_fb_notif_failed;
	}
#endif
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = nvt_ts_early_suspend;
	ts->early_suspend.resume = nvt_ts_late_resume;
	ret = register_early_suspend(&ts->early_suspend);
	if (ret) {
		NVT_ERR("register early suspend failed. ret=%d\n", ret);
		goto err_register_early_suspend_failed;
	}
#endif

#if NVT_TOUCH_WDT_RECOVERY
	recovery_cnt = 0;
#endif

#if defined(CONFIG_SOC_GOOGLE)
	if (device_init_wakeup(&ts->client->dev, true))
		NVT_ERR("failed to init wakeup dev!\n");
#endif

	ts->bTouchIsAwake = true;
	ts->pen_format_id = 0xFF;

	NVT_LOGD("end\n");

	nvt_irq_enable(true);
	ret = enable_irq_wake(ts->client->irq);
	if (ret)
		NVT_ERR("enable_irq_wake() failed! ret=%d\n", ret);

	ts->probe_done = true;
	return 0;

#if defined(CONFIG_FB)
#if defined(CONFIG_DRM_PANEL) && (defined(CONFIG_ARCH_QCOM) || defined(CONFIG_ARCH_MSM))
err_register_drm_panel_notif_failed:
#elif defined(_MSM_DRM_NOTIFY_H_)
err_register_drm_notif_failed:
#else
err_register_fb_notif_failed:
#endif
#elif defined(CONFIG_HAS_EARLYSUSPEND)
err_register_early_suspend_failed:
#endif
#if NVT_TOUCH_MP
	nvt_mp_proc_deinit();
err_mp_proc_init_failed:
#endif
#if NVT_TOUCH_EXT_API
	nvt_extra_api_deinit();
err_extra_api_init_failed:
#endif
#if NVT_TOUCH_EXT_PROC
	nvt_extra_proc_deinit();
err_extra_proc_init_failed:
#endif
#if NVT_TOUCH_ESD_PROTECT
	if (nvt_esd_check_wq) {
		cancel_delayed_work_sync(&nvt_esd_check_work);
		destroy_workqueue(nvt_esd_check_wq);
		nvt_esd_check_wq = NULL;
	}
err_create_nvt_esd_check_wq_failed:
#endif
#if BOOT_UPDATE_FIRMWARE
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
#endif
#if WAKEUP_GESTURE_DEFAULT
	device_init_wakeup(&ts->input_dev->dev, 0);
#endif
	free_irq(client->irq, ts);
err_int_request_failed:
	goog_pm_unregister_notification(ts->gti);
err_goog_pm_register:
	goog_touch_interface_remove(ts->gti);
err_goog_touch_interface:
	goog_usi_unregister(ts->g_usi_handle);
	ts->g_usi_handle = G_USI_INVALID_HANDLE;
err_goog_usi_register_failed:
	input_unregister_device(ts->input_dev);
err_input_register_device_failed:
	if (ts->input_dev) {
		input_free_device(ts->input_dev);
		ts->input_dev = NULL;
	}
err_input_dev_alloc_failed:
err_create_nvt_fwu_wq_failed:
err_chipvertrim_failed:
	mutex_destroy(&ts->bus_mutex);
	mutex_destroy(&ts->xbuf_lock);
	mutex_destroy(&ts->lock);
	nvt_gpio_deconfig(ts);
err_gpio_config_failed:
err_parse_dt:
	if (ts->event_wq)
		destroy_workqueue(ts->event_wq);
err_alloc_workqueue:
err_spi_setup:
err_ckeck_full_duplex:
err_check_dt:
	spi_set_drvdata(client, NULL);
	if (ts->rbuf) {
		kfree(ts->rbuf);
		ts->rbuf = NULL;
	}
err_malloc_rbuf:
	if (ts->xbuf) {
		kfree(ts->xbuf);
		ts->xbuf = NULL;
	}
err_malloc_xbuf:
	if (ts) {
		kfree(ts);
		ts = NULL;
	}
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen driver release function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static void nvt_ts_remove(struct spi_device *client)
{
	NVT_LOG("Removing driver...\n");

#if defined(CONFIG_FB)
#if defined(CONFIG_DRM_PANEL) && (defined(CONFIG_ARCH_QCOM) || defined(CONFIG_ARCH_MSM))
	if (ts->active_panel) {
		if (drm_panel_notifier_unregister(ts->active_panel, &ts->drm_panel_notif))
			NVT_ERR("Error occurred while unregistering drm_panel_notifier.\n");
	}
#elif defined(_MSM_DRM_NOTIFY_H_)
	if (msm_drm_unregister_client(&ts->drm_notif))
		NVT_ERR("Error occurred while unregistering drm_notifier.\n");
#else
	if (fb_unregister_client(&ts->fb_notif))
		NVT_ERR("Error occurred while unregistering fb_notifier.\n");
#endif
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ts->early_suspend);
#endif

#if NVT_TOUCH_MP
	nvt_mp_proc_deinit();
#endif
#if NVT_TOUCH_EXT_API
	nvt_extra_api_deinit();
#endif
#if NVT_TOUCH_EXT_PROC
	nvt_extra_proc_deinit();
#endif

#if NVT_TOUCH_ESD_PROTECT
	if (nvt_esd_check_wq) {
		cancel_delayed_work_sync(&nvt_esd_check_work);
		nvt_esd_check_enable(false);
		destroy_workqueue(nvt_esd_check_wq);
		nvt_esd_check_wq = NULL;
	}
#endif

#if BOOT_UPDATE_FIRMWARE
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
#endif
#if WAKEUP_GESTURE_DEFAULT
	device_init_wakeup(&ts->input_dev->dev, 0);
#endif

	nvt_irq_enable(false);
	free_irq(client->irq, ts);

	goog_pm_unregister_notification(ts->gti);
	goog_touch_interface_remove(ts->gti);

	mutex_destroy(&ts->bus_mutex);
	mutex_destroy(&ts->xbuf_lock);
	mutex_destroy(&ts->lock);

	nvt_gpio_deconfig(ts);

	if (ts->pen_support) {
		if (ts->g_usi_handle != G_USI_INVALID_HANDLE) {
			goog_usi_unregister(ts->g_usi_handle);
			ts->g_usi_handle = G_USI_INVALID_HANDLE;
		}
	}

	if (ts->input_dev) {
		input_unregister_device(ts->input_dev);
		ts->input_dev = NULL;
	}

	spi_set_drvdata(client, NULL);

	devm_kfree(&client->dev, ts->heatmap_out_buf);
	ts->heatmap_out_buf = NULL;
	ts->heatmap_out_buf_size = 0;
	devm_kfree(&client->dev, ts->heatmap_spi_buf);
	ts->heatmap_spi_buf = NULL;
	ts->heatmap_spi_buf_size = 0;
	devm_kfree(&client->dev, ts->extra_spi_buf);
	ts->extra_spi_buf = NULL;
	ts->extra_spi_buf_size = 0;

	if (ts->xbuf) {
		kfree(ts->xbuf);
		ts->xbuf = NULL;
	}

	if (ts) {
		kfree(ts);
		ts = NULL;
	}
}

static void nvt_ts_shutdown(struct spi_device *client)
{
	NVT_LOG("Shutdown driver...\n");

	nvt_irq_enable(false);

#if defined(CONFIG_FB)
#if defined(CONFIG_DRM_PANEL) && (defined(CONFIG_ARCH_QCOM) || defined(CONFIG_ARCH_MSM))
	if (ts->active_panel) {
		if (drm_panel_notifier_unregister(ts->active_panel, &ts->drm_panel_notif))
			NVT_ERR("Error occurred while unregistering drm_panel_notifier.\n");
	}
#elif defined(_MSM_DRM_NOTIFY_H_)
	if (msm_drm_unregister_client(&ts->drm_notif))
		NVT_ERR("Error occurred while unregistering drm_notifier.\n");
#else
	if (fb_unregister_client(&ts->fb_notif))
		NVT_ERR("Error occurred while unregistering fb_notifier.\n");
#endif
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ts->early_suspend);
#endif

#if NVT_TOUCH_MP
	nvt_mp_proc_deinit();
#endif
#if NVT_TOUCH_EXT_API
	nvt_extra_api_deinit();
#endif
#if NVT_TOUCH_EXT_PROC
	nvt_extra_proc_deinit();
#endif

#if NVT_TOUCH_ESD_PROTECT
	if (nvt_esd_check_wq) {
		cancel_delayed_work_sync(&nvt_esd_check_work);
		nvt_esd_check_enable(false);
		destroy_workqueue(nvt_esd_check_wq);
		nvt_esd_check_wq = NULL;
	}
#endif /* #if NVT_TOUCH_ESD_PROTECT */

#if BOOT_UPDATE_FIRMWARE
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
#endif
#if WAKEUP_GESTURE_DEFAULT
	device_init_wakeup(&ts->input_dev->dev, 0);
#endif
}

#ifdef NVT_TS_PANEL_BRIDGE
/*******************************************************
Description:
	Novatek touchscreen driver suspend function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
int nvt_ts_suspend(struct device *dev)
{
	uint8_t buf[4] = {0};
	uint32_t __maybe_unused i = 0;
	struct g_usi_event_leave usi_event_leave;

	if (!ts->bTouchIsAwake) {
		NVT_LOG("Touch is already suspend\n");
		return 0;
	}

#if NVT_TOUCH_ESD_PROTECT
	NVT_LOG("cancel delayed work sync on nvt_esd_check_work\n");
	cancel_delayed_work_sync(&nvt_esd_check_work);
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	mutex_lock(&ts->lock);

	NVT_LOGD("start\n");
	/* Initialize heatmap_host_cmd to force sending again after resume. */
	ts->heatmap_host_cmd = HEATMAP_HOST_CMD_DISABLE;

	if (ts->wkg_option == WAKEUP_GESTURE_OFF)
		nvt_irq_enable(false);

	reinit_completion(&ts->bus_resumed);
	ts->bTouchIsAwake = false;

#if !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	/* release all touches */
	goog_input_lock(ts->gti);
	goog_input_set_timestamp(ts->gti, ts->input_dev, ktime_get());
	if (ts->report_protocol == REPORT_PROTOCOL_B) {
		for (i = 0; i < ts->max_touch_num; i++) {
			goog_input_mt_slot(ts->gti, ts->input_dev, i);
			goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);
			goog_input_report_abs(ts->gti, ts->input_dev, ABS_MT_PRESSURE, 0);
			goog_input_mt_report_slot_state(ts->gti, ts->input_dev, MT_TOOL_FINGER, 0);
		}
	}
	goog_input_report_key(ts->gti, ts->input_dev, BTN_TOUCH, 0);

	if (ts->report_protocol == REPORT_PROTOCOL_A)
		input_mt_sync(ts->input_dev);

	goog_input_sync(ts->gti, ts->input_dev);
	goog_input_unlock(ts->gti);
#endif

	/* release pen event */
	if (ts->pen_support) {
		usi_event_leave.timestamp = ktime_get();
		goog_usi_send_event_leave(ts->g_usi_handle, &usi_event_leave);

		ts->pen_active = 0;
		ts->pen_offload_coord_timestamp = ts->timestamp;
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
		memset(&ts->pen_offload_coord, 0,
				sizeof(ts->pen_offload_coord));
#endif
	}

#if WAKEUP_GESTURE_DEFAULT
	nvt_set_dttw(false);
#endif

	if (ts->wkg_option != WAKEUP_GESTURE_OFF) {
		//---write command to enter "wakeup gesture mode"---
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x13;
		CTP_SPI_WRITE(ts->client, buf, 2);
		NVT_LOG("Gesture mode enabled.\n");
	} else {
		//---write command to enter "deep sleep mode"---
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x11;
		CTP_SPI_WRITE(ts->client, buf, 2);
		NVT_LOG("Deep sleep enabled.\n");
	}

	mutex_unlock(&ts->lock);

#if defined(CONFIG_SOC_GOOGLE)
	if (ts->wkg_option == WAKEUP_GESTURE_OFF)
		nvt_pinctrl_configure(ts, false);
#else
	msleep(50);
#endif

	NVT_LOGD("end\n");

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen driver resume function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
int nvt_ts_resume(struct device *dev)
{
	if (ts->bTouchIsAwake) {
		NVT_LOG("Touch is already resume\n");
		return 0;
	}

	mutex_lock(&ts->lock);

	NVT_LOGD("start\n");
#if defined(CONFIG_SOC_GOOGLE)
	nvt_pinctrl_configure(ts, true);
	usleep_range(NVT_PINCTRL_US_DELAY, NVT_PINCTRL_US_DELAY + 1);
#endif

	// please make sure display reset(RESX) sequence and mipi dsi cmds sent before this
#if NVT_TOUCH_SUPPORT_HW_RST
	if (gpio_is_valid(ts->reset_gpio))
		gpio_set_value(ts->reset_gpio, 1);
#endif

#if SPI_FLASH
	/*
	 * clear before check, please call the nvt_ts_resume function 10ms right
	 * after the RESX to prevent bootload reset delaying the first touch.
	 */
	nvt_clear_fw_reset_state();
	nvt_bootloader_reset();
	if (nvt_check_fw_reset_state(RESET_STATE_REK)) {
		NVT_ERR("FW is not ready! Try to bootloader reset\n");
		nvt_bootloader_reset();
#else
	if (nvt_update_firmware(get_fw_name(), 0)) {
		NVT_ERR("download firmware failed, ignore check fw state\n");
	} else {
#endif // SPI_FLASH
		nvt_check_fw_reset_state(RESET_STATE_REK);
	}

	nvt_irq_enable(true);

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
	queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
			   msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	/* Restore fast-pairing configuration. */
	if (ts->pen_support) {
		uint8_t buf[7], fw_version[2];
		uint8_t pen_model_index = 0;
		uint16_t validity_flags = 0;
		uint32_t info_flags;
		struct g_usi_pairing_info pairing_info;
		void *data = (void *)0;

		if (goog_usi_get_pairing_info(ts->g_usi_handle, &pairing_info))
			memset(&pairing_info, 0, sizeof(pairing_info)); /* set 0 if error */

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x70;
		buf[2] = 0x81;
		buf[3] = pairing_info.hash_id[0];
		buf[4] = pairing_info.hash_id[1];
		buf[5] = pairing_info.session_id[0];
		buf[6] = pairing_info.session_id[1];
		CTP_SPI_WRITE(ts->client, buf, sizeof(buf));
		NVT_LOG("fast-pairing: hash_id: 0x%02X%02X, session_id: 0x%02X%02X\n",
				pairing_info.hash_id[1], pairing_info.hash_id[0],
				pairing_info.session_id[1], pairing_info.session_id[0]);

		msleep(20);

		goog_usi_get_drvdata(ts->g_usi_handle, &data);
		pen_model_index = (u8)(uintptr_t)data;
		if (goog_usi_get_fw_version(ts->g_usi_handle, fw_version)) {
			fw_version[0] = 0; /* set 0 if error */
			fw_version[1] = 0;
		}

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x70;
		buf[2] = 0x82;
		buf[3] = pairing_info.freq_sel;
		buf[4] = fw_version[0];
		buf[5] = fw_version[1];
		buf[6] = pen_model_index;
		CTP_SPI_WRITE(ts->client, buf, 7);
		NVT_LOG("Write pen_freq_seed = %02X, pen_fw_ver = 0x%02X%02X, model_idx = %d\n",
			pairing_info.freq_sel, fw_version[1], fw_version[0], pen_model_index);
		msleep(20);

		/*
		 * validity_flag is to show what data is available in the driver.
		 * the validity_flag is sent to the controller FW during the resume()
		 * so that the controller only asks stylus the data that the driver doesn't have.
		 */
		goog_usi_get_stylus_info_flags(ts->g_usi_handle, &info_flags);

		if (info_flags & G_USI_FW_VERSION_FLAG) {
			NVT_LOG(">> FW Version\n");
			validity_flags = 1; /* update TIC with the FW version is available */
		}

		if (info_flags & G_USI_CAPABILITY_FLAG) {
			NVT_LOG(">> Capability\n");
			validity_flags |= 2; /* update TIC with the Capability is available */
		}

		if (info_flags & G_USI_GID_FLAG) {
			NVT_LOG(">> GID\n");
			validity_flags |= 4; /* update TIC with the GID is available */
		}

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x70;
		buf[2] = 0x83;
		buf[3] = validity_flags & 0xFF;
		buf[4] = (validity_flags >> 8) & 0xFF;
		CTP_SPI_WRITE(ts->client, buf, 5);
		NVT_LOG("pen_valid_flag = %04X\n", validity_flags);
	}

	ts->bTouchIsAwake = true;
	complete_all(&ts->bus_resumed);
	mutex_unlock(&ts->lock);

	NVT_LOGD("end\n");

	return 0;
}

static void nvt_ts_suspend_work(struct work_struct *work)
{
	struct nvt_ts_data *ts = container_of(work, struct nvt_ts_data, suspend_work.work);

	nvt_ts_suspend(&ts->client->dev);
}

static void nvt_ts_resume_work(struct work_struct *work)
{
	struct nvt_ts_data *ts = container_of(work, struct nvt_ts_data, resume_work.work);

	nvt_ts_resume(&ts->client->dev);
}
#endif

#if defined(CONFIG_FB)
#if defined(CONFIG_DRM_PANEL) && (defined(CONFIG_ARCH_QCOM) || defined(CONFIG_ARCH_MSM))
static int nvt_drm_panel_notifier_callback(struct notifier_block *self,
		unsigned long event, void *data)
{
	struct drm_panel_notifier *evdata = data;
	int *blank;
	struct nvt_ts_data *ts =
		container_of(self, struct nvt_ts_data, drm_panel_notif);

	if (!evdata)
		return 0;

	if (!(event == DRM_PANEL_EARLY_EVENT_BLANK ||
	      event == DRM_PANEL_EVENT_BLANK)) {
		//NVT_LOG("event(%lu) not need to process\n", event);
		return 0;
	}

	if (evdata->data && ts) {
		blank = evdata->data;
		if (event == DRM_PANEL_EARLY_EVENT_BLANK) {
			if (*blank == DRM_PANEL_BLANK_POWERDOWN) {
				NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
				nvt_ts_suspend(&ts->client->dev);
			}
		} else if (event == DRM_PANEL_EVENT_BLANK) {
			if (*blank == DRM_PANEL_BLANK_UNBLANK) {
				NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
				nvt_ts_resume(&ts->client->dev);
			}
		}
	}

	return 0;
}
#elif defined(_MSM_DRM_NOTIFY_H_)
static int nvt_drm_notifier_callback(struct notifier_block *self,
				     unsigned long event, void *data)
{
	struct msm_drm_notifier *evdata = data;
	int *blank;
	struct nvt_ts_data *ts =
		container_of(self, struct nvt_ts_data, drm_notif);

	if (!evdata || (evdata->id != 0))
		return 0;

	if (evdata->data && ts) {
		blank = evdata->data;
		if (event == MSM_DRM_EARLY_EVENT_BLANK) {
			if (*blank == MSM_DRM_BLANK_POWERDOWN) {
				NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
				nvt_ts_suspend(&ts->client->dev);
			}
		} else if (event == MSM_DRM_EVENT_BLANK) {
			if (*blank == MSM_DRM_BLANK_UNBLANK) {
				NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
				nvt_ts_resume(&ts->client->dev);
			}
		}
	}

	return 0;
}
#else
static int nvt_fb_notifier_callback(struct notifier_block *self,
				    unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct nvt_ts_data *ts =
		container_of(self, struct nvt_ts_data, fb_notif);

#if SPI_FLASH
	if (evdata && evdata->data && event == FB_EARLY_EVENT_BLANK) {
#else
	if (evdata && evdata->data && event == FB_EVENT_BLANK) {
#endif // SPI_FLASH
		blank = evdata->data;
		if (*blank == FB_BLANK_POWERDOWN) {
			NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
			nvt_ts_suspend(&ts->client->dev);
		}
	} else if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK) {
			NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
			nvt_ts_resume(&ts->client->dev);
		}
	}

	return 0;
}
#endif
#elif defined(CONFIG_HAS_EARLYSUSPEND)
/*******************************************************
Description:
	Novatek touchscreen driver early suspend function.

return:
	n.a.
*******************************************************/
static void nvt_ts_early_suspend(struct early_suspend *h)
{
	nvt_ts_suspend(ts->client, PMSG_SUSPEND);
}

/*******************************************************
Description:
	Novatek touchscreen driver late resume function.

return:
	n.a.
*******************************************************/
static void nvt_ts_late_resume(struct early_suspend *h)
{
	nvt_ts_resume(ts->client);
}
#endif

static const struct spi_device_id nvt_ts_id[] = {
	{ NVT_SPI_NAME, 0 },
	{ }
};

#ifdef CONFIG_OF
static struct of_device_id nvt_match_table[] = {
	{ .compatible = "novatek,NVT-ts-spi",},
	{ },
};
MODULE_DEVICE_TABLE(of, nvt_match_table);
#endif

#if defined(CONFIG_PM) && defined(CONFIG_SOC_GOOGLE)
static const struct dev_pm_ops nvt_ts_dev_pm_ops = {
	.suspend = nvt_ts_pm_suspend,
	.resume = nvt_ts_pm_resume,
};
#endif
static const struct dev_pm_ops goog_pm_ops = {
	.suspend = nvt_ts_suspend,
	.resume = nvt_ts_resume,
};

static struct spi_driver nvt_spi_driver = {
	.probe		= nvt_ts_probe,
	.remove		= nvt_ts_remove,
	.shutdown	= nvt_ts_shutdown,
	.id_table	= nvt_ts_id,
	.driver = {
		.name	= NVT_SPI_NAME,
		.owner	= THIS_MODULE,
#ifdef CONFIG_OF
		.of_match_table = nvt_match_table,
#endif
#if defined(CONFIG_PM) && defined(CONFIG_SOC_GOOGLE)
		.pm = &nvt_ts_dev_pm_ops,
#endif
	},
};

/*******************************************************
Description:
	Driver Install function.

return:
	Executive Outcomes. 0---succeed. not 0---failed.
********************************************************/
static int32_t __init nvt_driver_init(void)
{
	int32_t ret = 0;

	//---add spi driver---
	ret = spi_register_driver(&nvt_spi_driver);
	if (ret) {
		NVT_ERR("failed to add spi driver");
		goto err_driver;
	}

	NVT_LOGI("finished\n");

err_driver:
	return ret;
}

/*******************************************************
Description:
	Driver uninstall function.

return:
	n.a.
********************************************************/
static void __exit nvt_driver_exit(void)
{
	spi_unregister_driver(&nvt_spi_driver);
}

#if defined(CONFIG_DRM_PANEL) && (defined(CONFIG_ARCH_QCOM) || defined(CONFIG_ARCH_MSM))
late_initcall(nvt_driver_init);
#else
module_init(nvt_driver_init);
#endif
module_exit(nvt_driver_exit);

MODULE_DESCRIPTION("Novatek Touchscreen Driver");
MODULE_LICENSE("GPL");
