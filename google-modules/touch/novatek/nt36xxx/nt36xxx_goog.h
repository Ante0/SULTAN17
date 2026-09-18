/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright (c) 2021 Google LLC
 *    Author: Super Liu <supercjliu@google.com>
 */
#ifndef _NT36XXX_GOOG_H_
#define _NT36XXX_GOOG_H_

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
#include <goog_touch_interface.h>
#endif

#define NVT_SUSPEND_WORK_MS_DELAY	0
#define NVT_SUSPEND_POST_MS_DELAY	80
#define NVT_RESUME_WORK_MS_DELAY	0
#define NVT_FORCE_ACTIVE_MS_DELAY	500
#define NVT_PINCTRL_US_DELAY		(10*1000)

static u8 tid_nt36523n[NVT_ID_BYTE_MAX] = {0x17, 0xFF, 0xFF, 0x23, 0x65, 0x03};
struct nvt_ts_data; /* forward declaration */

#if defined(CONFIG_SOC_GOOGLE)
bool nvt_ts_check_tid(struct nvt_ts_data *ts, u8 *tid);
#endif

#if IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE)
int nvt_get_channel_data(void *private_data,
		u32 type, u8 **buffer, u32 *size);
int nvt_callback(void *private_data,
		enum gti_cmd_type cmd_type, struct gti_union_cmd_data *cmd);
ssize_t force_touch_active_show(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t force_touch_active_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);
ssize_t force_release_fw_show(struct device *dev,
				struct device_attribute *attr, char *buf);
ssize_t force_release_fw_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count);
int nvt_ts_pm_suspend(struct device *dev);
int nvt_ts_pm_resume(struct device *dev);

#else /* !IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE) */

enum gti_cmd_type : u32{
	GTI_CMD_NOP,
};

enum gti_vendor_dev_pm_state : u32 {
	GTI_VENDOR_DEV_RESUME = 0,
	GTI_VENDOR_DEV_SUSPEND,
};

struct gti_union_cmd_data {
	u32 nop_cmd;
};

struct goog_touch_interface {
	void *private_data;
};

struct gti_optional_configuration {
	u32 reserve;
};

static inline int nvt_get_channel_data(void *private_data,
		u32 type, u8 **buffer, u32 *size)
{
	return -ENODATA;
}

static inline int nvt_callback(void *private_data,
		enum gti_cmd_type cmd_type, struct gti_union_cmd_data *cmd)
{
	return -ENODATA;
}

static inline ssize_t force_touch_active_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return 0;
}
static inline ssize_t force_touch_active_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return 0;
}
static inline ssize_t force_release_fw_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return 0;
}
static inline ssize_t force_release_fw_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	return 0;
}

static inline int nvt_ts_pm_suspend(struct device *dev)
{
	return 0;
}

static inline int nvt_ts_pm_resume(struct device *dev)
{
	return 0;
}

static inline void goog_input_lock(struct goog_touch_interface *gti)
{
}

static inline void goog_input_unlock(struct goog_touch_interface *gti)
{
}

static inline void goog_input_set_timestamp(
	struct goog_touch_interface *gti,
	struct input_dev *dev, ktime_t timestamp)
{
	input_set_timestamp(dev, timestamp);
}

static inline void goog_input_mt_slot(
	struct goog_touch_interface *gti,
	struct input_dev *dev, int slot)
{
	input_mt_slot(dev, slot);
}

static inline void goog_input_mt_report_slot_state(
	struct goog_touch_interface *gti,
	struct input_dev *dev, unsigned int tool_type, bool active)
{
	input_mt_report_slot_state(dev, tool_type, active);
}

static inline void goog_input_report_abs(
	struct goog_touch_interface *gti,
	struct input_dev *dev, unsigned int code, int value)
{
	input_report_abs(dev, code, value);
}

static inline void goog_input_report_key(
	struct goog_touch_interface *gti,
	struct input_dev *dev, unsigned int code, int value)
{
	input_report_key(dev, code, value);
}

static inline void goog_input_sync(struct goog_touch_interface *gti, struct input_dev *dev)
{
	input_sync(dev);
}

static inline int goog_request_threaded_irq(struct goog_touch_interface *gti,
	unsigned int irq, irq_handler_t handler, irq_handler_t thread_fn,
	unsigned long irqflags, const char *devname, void *dev_id)
{
	return request_threaded_irq(irq, handler, thread_fn, irqflags, devname, dev_id);
}

static inline u32 goog_pm_wake_get_locks(struct goog_touch_interface *gti)
{
	return 0;
}

static inline int goog_pm_register_notification(struct goog_touch_interface *gti,
	const struct dev_pm_ops *ops)
{
	return 0;
}

static inline int goog_pm_unregister_notification(struct goog_touch_interface *gti)
{
	return 0;
}

static inline struct goog_touch_interface *goog_touch_interface_probe(
	void *private_data,
	struct device *dev,
	struct input_dev *input_dev,
	int (*default_handler)(void *private_data,
		enum gti_cmd_type cmd_type, struct gti_union_cmd_data *cmd),
	struct gti_optional_configuration *options)
{
	static struct goog_touch_interface gti[1];

	return gti;
}

static inline int goog_touch_interface_remove(struct goog_touch_interface *gti)
{
	return 0;
}

#endif /* IS_ENABLED(CONFIG_GOOG_TOUCH_INTERFACE) */

#endif /* _NT36XXX_GOOG_H_ */
