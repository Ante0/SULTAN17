/* SPDX-License-Identifier: GPL-2.0-only */
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
#ifndef _LINUX_NVT_TOUCH_H
#define	_LINUX_NVT_TOUCH_H

#include <linux/delay.h>
#include <linux/input.h>
#include <linux/of.h>
#include <linux/spi/spi.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#if SPI_FLASH
#include <linux/firmware.h>
#endif // SPI_FLASH

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <trace/hooks/systrace.h>
#include "nt36xxx_mem_map.h"
#if SPI_FLASH
#include "nvt_flash_info.h"
#endif // SPI_FLASH

#ifdef CONFIG_MTK_SPI
/* Please copy mt_spi.h file under mtk spi driver folder */
#include "mt_spi.h"
#endif

#ifdef CONFIG_SPI_MT65XX
#include <linux/platform_data/spi-mt65xx.h>
#endif

#include <drm/drm_panel.h> /* struct drm_panel */
#include <drm/drm_bridge.h> /* struct drm_bridge */
#include <drm/drm_connector.h> /* struct drm_connector */

#include "nt36xxx_goog.h"
#include "goog_usi_stylus.h"

#define NVT_MP_DEBUG 0

#if defined(CONFIG_SOC_GOOGLE)
#undef CONFIG_FB
#undef CONFIG_HAS_EARLYSUSPEND
#undef CONFIG_ARCH_QCOM
#undef CONFIG_ARCH_MSM
#endif

//---GPIO number---
#define NVTTOUCH_RST_PIN 980
#define NVTTOUCH_INT_PIN 943


//---INT trigger mode---
//#define IRQ_TYPE_EDGE_RISING 1
//#define IRQ_TYPE_EDGE_FALLING 2
#define INT_TRIGGER_TYPE IRQ_TYPE_EDGE_RISING

#if SPI_FLASH
//---bus transfer length---
#define BUS_TRANSFER_LENGTH  256
#endif // SPI_FLASH

//---SPI driver info.---
#define NVT_SPI_NAME "NVT-ts"
#undef pr_fmt
#define pr_fmt(fmt) "gtd: NVT-ts: " fmt
#define NVT_DBG(fmt, args...)  pr_debug(fmt, ##args)
#define NVT_LOG(fmt, args...)  pr_info(fmt, ##args)
#define NVT_ERR(fmt, args...)  pr_err(fmt, ##args)
#define NVT_LOGD(fmt, args...) NVT_DBG("%s: "fmt, __func__, ##args)
#define NVT_LOGI(fmt, args...) NVT_LOG("%s: "fmt, __func__, ##args)
#define NVT_LOGE(fmt, args...) NVT_ERR("%s: "fmt, __func__, ##args)

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 0)
#define sysfs_emit(buf, fmt, ...) scnprintf(buf, PAGE_SIZE, fmt, ##__VA_ARGS__)
#define sysfs_emit_at(buf, at, fmt, ...) scnprintf(buf + at, PAGE_SIZE - at, fmt, ##__VA_ARGS__)
#endif

//---Input device info.---
#define NVT_TS_NAME "NVTCapacitiveTouchScreen"
#define NVT_PEN_NAME "NVTCapacitivePen"
#define NVT_PEN_BATTERY_NAME "nvt-pen-battery"

//---Touch info.---
#define TOUCH_RES_SCALE 1
#define DEFAULT_MAX_WIDTH 1600
#define DEFAULT_MAX_HEIGHT 2560
#define TOUCH_DEFAULT_MAX_WIDTH (DEFAULT_MAX_WIDTH * TOUCH_RES_SCALE)
#define TOUCH_DEFAULT_MAX_HEIGHT (DEFAULT_MAX_HEIGHT * TOUCH_RES_SCALE)
#define PEN_RES_SCALE 2
#define PEN_DEFAULT_MAX_WIDTH (DEFAULT_MAX_WIDTH * PEN_RES_SCALE)
#define PEN_DEFAULT_MAX_HEIGHT (DEFAULT_MAX_HEIGHT * PEN_RES_SCALE)
#define TOUCH_MAX_FINGER_NUM 10
#define TOUCH_KEY_NUM 0
#if TOUCH_KEY_NUM > 0
extern const uint16_t touch_key_array[TOUCH_KEY_NUM];
#endif
//#define TOUCH_FORCE_NUM 1000
#ifdef TOUCH_FORCE_NUM
#define MT_PRESSURE_MAX TOUCH_FORCE_NUM
#else
#define MT_PRESSURE_MAX 256
#endif
//---for Pen---
//#define PEN_DISTANCE_SUPPORT
#define PEN_PRESSURE_MAX (4095)
#define PEN_DISTANCE_MAX (1)
#define PEN_TILT_MIN (-60)
#define PEN_TILT_MAX (60)
#define PEN_BATTERY_MAX (100)
#define PEN_BATTERY_MIN (0)

/* Enable only when module have tp reset pin and connected to host */
#define NVT_TOUCH_SUPPORT_HW_RST 1

//---Customerized func.---
#define NVT_TOUCH_EXT_PROC 1
#define NVT_TOUCH_EXT_API 1
#define REPORT_PROTOCOL_A 1
#define REPORT_PROTOCOL_B 0
#define NVT_TOUCH_MP 1
#define BOOT_UPDATE_FIRMWARE 1
#if defined(CONFIG_SOC_GOOGLE)
#if SPI_FLASH
#define BOOT_UPDATE_FIRMWARE_MS_DELAY 0
#define UPDATE_FIRMWARE_TIMEOUT 6000
#else
#define BOOT_UPDATE_FIRMWARE_MS_DELAY 100
#define UPDATE_FIRMWARE_TIMEOUT 500
#endif
#else
#define BOOT_UPDATE_FIRMWARE_MS_DELAY 14000
#define UPDATE_FIRMWARE_TIMEOUT 500
#endif
#define BOOT_UPDATE_FIRMWARE_NAME "novatek_ts_fw.bin"
#define MP_UPDATE_FIRMWARE_NAME   "novatek_ts_mp.bin"
#define POINT_DATA_CHECKSUM 0
#define POINT_DATA_CHECKSUM_LEN 65
#define NVT_HEATMAP_COMP_NOT_READY_SIZE (0xFFF << 1)

#if SPI_FLASH
#if BOOT_UPDATE_FIRMWARE
#define SIZE_4KB 4096
#define FLASH_SECTOR_SIZE SIZE_4KB
#define FW_BIN_VER_OFFSET (fw_need_write_size - SIZE_4KB)
#define FW_BIN_VER_BAR_OFFSET (FW_BIN_VER_OFFSET + 1)
#define NVT_FLASH_END_FLAG_LEN 3
#define NVT_FLASH_END_FLAG_ADDR (fw_need_write_size - NVT_FLASH_END_FLAG_LEN)
#endif
#endif // SPI_FLASH

//---ESD Protect.---
#define NVT_TOUCH_ESD_CHECK_PERIOD 1500	/* ms */
#if SPI_FLASH
#define NVT_TOUCH_WDT_RECOVERY 0
#define NVT_TOUCH_ESD_PROTECT 0
#else
#define NVT_TOUCH_WDT_RECOVERY 1
#define NVT_TOUCH_ESD_PROTECT 1
#endif // SPI_FLASH

#define CHECK_PEN_DATA_CHECKSUM 0

// MP
#define NORMAL_MODE 0x00
#define TEST_MODE_2 0x22
#define MP_MODE_CC 0x41
#define ENTER_ENG_MODE 0x61
#define LEAVE_ENG_MODE 0x62
#define FREQ_HOP_DISABLE 0x66
#define FREQ_HOP_ENABLE 0x65

// NVT_MT_CUSTOM for Cancel Mode Finger Status
#define NVT_MT_CUSTOM 1
#if NVT_MT_CUSTOM
#define ABS_MT_CUSTOM 0x3e
#define GRIP_TOUCH 0x04
#define PALM_TOUCH 0x05
#endif

// HEATMAP
#if NVT_TOUCH_EXT_API
#define HEATMAP_TOUCH_ADDR 0x23200
#define HEATMAP_PEN_ADDR 0x2A50A
enum {
	HEATMAP_DATA_TYPE_DISABLE = 0,
	HEATMAP_DATA_TYPE_TOUCH_RAWDATA = 1,
	HEATMAP_DATA_TYPE_TOUCH_RAWDATA_UNCOMP = HEATMAP_DATA_TYPE_TOUCH_RAWDATA,
	HEATMAP_DATA_TYPE_TOUCH_BASELINE = 2,
	HEATMAP_DATA_TYPE_TOUCH_BASELINE_UNCOMP = HEATMAP_DATA_TYPE_TOUCH_BASELINE,
	HEATMAP_DATA_TYPE_TOUCH_STRENGTH = 3,
	HEATMAP_DATA_TYPE_TOUCH_STRENGTH_UNCOMP = HEATMAP_DATA_TYPE_TOUCH_STRENGTH,
	HEATMAP_DATA_TYPE_TOUCH_STRENGTH_COMP = 4,
	HEATMAP_DATA_TYPE_PEN_STRENGTH_COMP = 5,
	HEATMAP_DATA_TYPE_UNSUPPORTED,
};
#define HEATMAP_HOST_CMD_DISABLE             0x90
#define HEATMAP_HOST_CMD_TOUCH_STRENGTH      0x91
#define HEATMAP_HOST_CMD_TOUCH_STRENGTH_COMP 0x92
#define HEATMAP_HOST_CMD_TOUCH_RAWDATA       0x93
#define HEATMAP_HOST_CMD_TOUCH_BASELINE      0x94
#endif

/* PEN */
#define PEN_HASH_SECTION_ID_ADDR 0x2B31D

/* FW History */
#define NVT_HISTORY_BUF_LEN		(65 * 4)

/* Gesture */
#define WAKEUP_GESTURE_OFF  0
#define WAKEUP_GESTURE_STTW 1
#define WAKEUP_GESTURE_DTTW 2
#define WAKEUP_GESTURE_DEFAULT WAKEUP_GESTURE_STTW

#if SPI_FLASH
/* PID */
#define TKI3_CSOT 0x780C
#define TKI3_BOE 0x780D
#endif // SPI_FLASH

enum gesture_id : u8 {
	GESTURE_WORD_C = 12,
	GESTURE_WORD_W = 13,
	GESTURE_SINGLE_TAP = 14,
	GESTURE_DOUBLE_TAP = 15,
	GESTURE_WORD_Z = 16,
	GESTURE_WORD_M = 17,
	GESTURE_WORD_O = 18,
	GESTURE_WORD_e = 19,
	GESTURE_WORD_S = 20,
	GESTURE_SLIDE_UP = 21,
	GESTURE_SLIDE_DOWN = 22,
	GESTURE_SLIDE_LEFT = 23,
	GESTURE_SLIDE_RIGHT = 24,
	GESTURE_ID_MAX,
};

struct nvt_ts_data {
	struct spi_device *client;
	struct input_dev *input_dev;
	struct delayed_work nvt_fwu_work;
	uint16_t addr;
	int8_t phys[32];
#if defined(CONFIG_FB) && !defined(CONFIG_SOC_GOOGLE)
#if defined(CONFIG_DRM_PANEL) && (defined(CONFIG_ARCH_QCOM) || defined(CONFIG_ARCH_MSM))
	struct notifier_block drm_panel_notif;
#elif defined(_MSM_DRM_NOTIFY_H_)
	struct notifier_block drm_notif;
#else
	struct notifier_block fb_notif;
#endif
#elif defined(CONFIG_HAS_EARLYSUSPEND)
	struct early_suspend early_suspend;
#endif
	uint8_t fw_ver;
	uint8_t x_num;
	uint8_t y_num;
	uint16_t touch_width;
	uint16_t touch_height;
	uint16_t abs_x_max;		/* abs report start from 0 to 'width-1' */
	uint16_t abs_y_max;		/* abs report start from 0 to 'height-1' */
	uint16_t pen_width;
	uint16_t pen_height;
	uint16_t pen_abs_x_max;		/* abs report start from 0 to 'width-1' */
	uint16_t pen_abs_y_max;		/* abs report start from 0 to 'height-1' */
	uint8_t max_touch_num;
	uint8_t max_button_num;
	uint8_t touch_freq_index;
	uint8_t pen_freq_index;
	uint32_t int_trigger_type;
	int32_t irq_gpio;
	uint32_t irq_flags;
	int32_t reset_gpio;
	uint32_t reset_flags;
	struct mutex lock;
#if defined(CONFIG_SOC_GOOGLE)
	const struct nvt_ts_trim_id_table *trim_table;
#endif
	const struct nvt_ts_mem_map *mmap;
	uint8_t hw_crc;
	uint16_t nvt_pid;
	uint8_t *rbuf;
	uint8_t *xbuf;
	char history_buf[NVT_HISTORY_BUF_LEN];
	struct mutex xbuf_lock;
	bool probe_done;
	bool irq_enabled;
	bool pen_support;
	uint8_t x_gang_num;
	uint8_t y_gang_num;
	int8_t pen_input_idx;
	int8_t pen_phys[32];
	int8_t pen_name[32];
#if SPI_FLASH
	uint8_t flash_mid;
	uint16_t flash_did; /* 2 bytes did read by 9Fh cmd */
	const flash_info_t *match_finfo;
	bool force_fw_update;
#endif // SPI_FLASH
	struct input_dev *pen_input_dev;
#ifdef CONFIG_MTK_SPI
	struct mt_chip_conf spi_ctrl;
#endif
#ifdef CONFIG_SPI_MT65XX
	struct mtk_chip_config spi_ctrl;
#endif
	uint8_t report_protocol;
	u8 wkg_option;
	u8 wkg_default;
	uint8_t bTouchIsAwake;
	uint8_t pen_format_id;
	g_usi_handle_t g_usi_handle;
#if NVT_TOUCH_EXT_API
	uint16_t dttw_touch_area_max;
	uint16_t dttw_touch_area_min;
	uint16_t dttw_contact_duration_max;
	uint16_t dttw_contact_duration_min;
	uint16_t dttw_tap_offset;
	uint16_t dttw_tap_gap_duration_max;
	uint16_t dttw_tap_gap_duration_min;
	uint16_t dttw_motion_tolerance;
	uint16_t dttw_detection_window_edge;
	uint8_t heatmap_data_type;
#endif

	const char *fw_name;
	const char *mp_fw_name;

	/*
	 * Time that the event was first received from
	 * the touch IC, acquired during hard interrupt,
	 * in CLOCK_MONOTONIC
	 */
	ktime_t timestamp;

	/*
	 * Used for event handler, suspend and resume work.
	 */
	struct pinctrl *pinctrl;
	struct drm_panel *active_panel;
	u32 initial_panel_index;

	struct completion bus_resumed;
	struct drm_bridge panel_bridge;
	struct drm_connector *connector;
	bool is_panel_lp_mode;
	struct delayed_work suspend_work;
	struct delayed_work resume_work;
	struct workqueue_struct *event_wq;

	struct mutex bus_mutex;
	ktime_t bugreport_ktime_start;
	u8 force_release_fw;

	/*
	 * Used for google touch interface.
	 */
	bool fw_update_in_process;
	bool selftest_in_process;
	struct goog_touch_interface *gti;
	uint8_t heatmap_host_cmd;
	uint32_t heatmap_host_cmd_addr;
	uint32_t heatmap_out_buf_size;
	uint8_t *heatmap_out_buf;
	uint32_t heatmap_spi_buf_size;
	uint8_t *heatmap_spi_buf;
	uint32_t extra_spi_buf_size;
	uint8_t *extra_spi_buf;
	uint32_t touch_heatmap_comp_len;

	/*
	 * Stylus context used by touch_offload
	 */
#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
	struct TouchOffloadCoord pen_offload_coord;
#endif
	ktime_t pen_offload_coord_timestamp;
	u8 pen_active;
#if SPI_FLASH
	struct completion fwu_done;
#endif // SPI_FLASH
	uint8_t mp_tvcl_mode;
	uint8_t mp_ibias_mode;

	union {
	struct {
		u32 selftest_failed_short : 1;
		u32 selftest_failed_open : 1;
		u32 selftest_failed_fw_raw : 1;
		u32 selftest_failed_fw_cc : 1;
		u32 selftest_failed_noise : 1;
		u32 selftest_failed_pen_raw : 1;
		u32 selftest_failed_pen_noise : 1;
		u32 selftest_failed_pen_detect : 1;
	};
	u32 selftest_failed_result;
	};
	unsigned long selftest_defect_x_line;
	unsigned long selftest_defect_y_line;

	bool high_resolution_enabled;
	u16 display_width; /* pixel */
	u16 display_height; /* pixel */
	u16 res_scale;
	uint8_t *point_data;
	u16 point_data_alloc_sz;

};

#if SPI_FLASH
typedef struct gcm_transfer {
	uint8_t flash_cmd;
	uint32_t flash_addr;
	uint16_t flash_checksum;
	uint8_t flash_addr_len;
	uint8_t pem_byte_len; /* performance enhanced mode / contineous read mode byte length*/
	uint8_t dummy_byte_len;
	uint8_t *tx_buf;
	uint16_t tx_len;
	uint8_t *rx_buf;
	uint16_t rx_len;
} gcm_xfer_t;
#endif // SPI_FLASH

typedef enum {
	RESET_STATE_INIT = 0xA0,// IC reset
	RESET_STATE_REK,		// ReK baseline
	RESET_STATE_REK_FINISH,	// baseline is ready
	RESET_STATE_NORMAL_RUN,	// normal run
	RESET_STATE_MAX  = 0xAF
} RST_COMPLETE_STATE;

typedef enum {
	EVENT_MAP_HOST_CMD                      = 0x50,
	EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE   = 0x51,
	EVENT_MAP_RESET_COMPLETE                = 0x60,
	EVENT_MAP_FWINFO                        = 0x78,
	EVENT_MAP_PROJECTID                     = 0x9A,
} SPI_EVENT_MAP;

//---SPI READ/WRITE---
#define SPI_WRITE_MASK(a)	(a | 0x80)
#define SPI_READ_MASK(a)	(a & 0x7F)

#define DUMMY_BYTES (1)
#define NVT_TRANSFER_LEN	(63*1024)
#define NVT_READ_LEN		(4*1024)
#define NVT_XBUF_LEN		(NVT_TRANSFER_LEN+1+DUMMY_BYTES)

typedef enum {
	NVTWRITE = 0,
	NVTREAD  = 1
} NVT_SPI_RW;

//---extern structures---
extern struct nvt_ts_data *ts;
#if SPI_FLASH
extern size_t fw_need_write_size;
#endif // SPI_FLASH

//---extern functions---
int32_t CTP_SPI_READ(struct spi_device *client, uint8_t *buf, uint16_t len);
int32_t CTP_SPI_WRITE(struct spi_device *client, uint8_t *buf, uint16_t len);
#if SPI_FLASH
int32_t Check_CheckSum_GCM(const struct firmware *fw_entry);
int32_t nvt_check_flash_end_flag_gcm(void);
int32_t nvt_write_reg(nvt_ts_reg_t reg, uint8_t val);
int32_t Update_Firmware_GCM(const struct firmware *fw_entry);
void nvt_stop_crc_reboot(void);
void nvt_clear_fw_reset_state(void);
#endif // SPI_FLASH
void nvt_bootloader_reset(void);
#if !SPI_FLASH
void nvt_eng_reset(void);
void nvt_sw_reset(void);
#endif // !SPI_FLASH
void nvt_sw_reset_idle(void);
#if !SPI_FLASH
void nvt_boot_ready(void);
void nvt_bld_crc_enable(void);
void nvt_fw_crc_enable(void);
void nvt_tx_auto_copy_mode(void);
#endif // !SPI_FLASH
int32_t nvt_check_fw_reset_state(RST_COMPLETE_STATE check_reset_state);
int32_t nvt_get_fw_info(void);
int32_t nvt_clear_fw_status(void);
int32_t nvt_check_fw_status(void);
#if !SPI_FLASH
int32_t nvt_check_spi_dma_tx_info(void);
#endif // !SPI_FLASH
int32_t nvt_set_page(uint32_t addr);
int32_t nvt_write_addr(uint32_t addr, uint8_t data);
extern void update_firmware_release(void);
#if !SPI_FLASH
extern int32_t nvt_update_firmware(const char *firmware_name, uint8_t full);
#endif // !SPI_FLASH
extern void nvt_change_mode(uint8_t mode);
extern void nvt_get_xdata_info(int32_t **ptr, int *size);
extern void nvt_read_mdata(uint32_t xdata_addr, uint32_t xdata_btn_addr);
extern int8_t nvt_switch_FreqHopEnDis(uint8_t FreqHopEnDis);
extern uint8_t nvt_get_fw_pipe(void);
extern void nvt_read_fw_history(uint32_t addr);
#if NVT_TOUCH_EXT_API
extern int32_t nvt_extra_api_init(void);
extern void nvt_extra_api_deinit(void);
extern void nvt_get_dttw_conf(void);
extern void nvt_set_dttw(bool check_result);
#endif

enum {
	CMD_DISABLE = 0,
	MODE_1,
	CMD_ENABLE = 1,
	MODE_2,
	MODE_3,
	MODE_4,
	MODE_5,
	MODE_6,
	MODE_7,
	MODE_8,
	MODE_9,
	MODE_10,
	MODE_11,
	MODE_12,
	MODE_13,
	MODE_14,
	MODE_15
};

#if NVT_TOUCH_ESD_PROTECT
extern void nvt_esd_check_enable(uint8_t enable);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

void nvt_irq_enable(bool enable);
inline const char *get_fw_name(void);
inline const char *get_mp_fw_name(void);
int nvt_ts_resume(struct device *dev);
int nvt_ts_suspend(struct device *dev);

void nvt_set_heatmap_host_cmd(struct nvt_ts_data *ts, bool force_update);
extern struct workqueue_struct *nvt_fwu_wq;
extern int32_t nvt_mp_settings(uint8_t tvcl_mode, uint8_t ibias_mode);
extern int32_t nvt_selftest(void);
extern int32_t nvt_print_selftest_result(char *buf, int size);
#endif /* _LINUX_NVT_TOUCH_H */
