// SPDX-License-Identifier: GPL-2.0
/*
 * Common support functions for Edge TPU ML accelerator host-side ops.
 *
 * Copyright (C) 2019 Google, Inc.
 */

#include <asm/current.h>
#include <asm/page.h>
#include <linux/atomic.h>
#include <linux/bits.h>
#include <linux/compiler.h>
#include <linux/cred.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/uidgid.h>

#include <gcip/gcip-firmware.h>

#include "edgetpu-config.h"
#include "edgetpu-debug.h"
#include "edgetpu-device-group.h"
#include "edgetpu-iif.h"
#include "edgetpu-ikv.h"
#include "edgetpu-internal.h"
#include "edgetpu-kci.h"
#include "edgetpu-mailbox.h"
#include "edgetpu-mmu.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-sw-watchdog.h"
#include "edgetpu-telemetry.h"
#include "edgetpu-usage-stats.h"
#include "edgetpu-wakelock.h"
#include "edgetpu.h"

/* Bits higher than VMA_TYPE_WIDTH are used to carry type specific data, e.g., core id. */
#define VMA_TYPE_WIDTH 16
#define VMA_TYPE(x) ((x) & (BIT_MASK(VMA_TYPE_WIDTH) - 1))
#define VMA_DATA_GET(x) ((x) >> VMA_TYPE_WIDTH)
#define VMA_DATA_SET(x, y) (VMA_TYPE(x) | ((y) << VMA_TYPE_WIDTH))

enum edgetpu_vma_type {
	VMA_INVALID,

	/* For VMA_LOG and VMA_TRACE, core id is stored in bits higher than VMA_TYPE_WIDTH. */
	VMA_LOG,
	VMA_TRACE,
	VMA_HWTRACE,
};

/* type that combines enum edgetpu_vma_type and data in higher bits. */
typedef u32 edgetpu_vma_flags_t;

/* structure to be set to vma->vm_private_data on mmap */
struct edgetpu_vma_private {
	struct edgetpu_client *client;
	edgetpu_vma_flags_t flag;
	/*
	 * vm_private_data is copied when a VMA is split, using this reference
	 * counter to know when should this object be freed.
	 */
	refcount_t count;
};

static atomic_t dev_count = ATOMIC_INIT(-1);

static atomic_t next_client_id = ATOMIC_INIT(0);

static edgetpu_vma_flags_t mmap_vma_flag(unsigned long pgoff)
{
	const unsigned long off = pgoff << PAGE_SHIFT;

	switch (off) {
	case EDGETPU_MMAP_LOG_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_LOG, 0);
	case EDGETPU_MMAP_TRACE_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_TRACE, 0);
#if EDGETPU_MAX_TELEMETRY_BUFFERS > 1
	case EDGETPU_MMAP_LOG1_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_LOG, 1);
	case EDGETPU_MMAP_TRACE1_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_TRACE, 1);
#endif /* EDGETPU_MAX_TELEMETRY_BUFFERS > 1 */
#if EDGETPU_MAX_TELEMETRY_BUFFERS > 2
	case EDGETPU_MMAP_LOG2_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_LOG, 2);
	case EDGETPU_MMAP_TRACE2_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_TRACE, 2);
#endif /* EDGETPU_MAX_TELEMETRY_BUFFERS > 2 */
#if EDGETPU_MAX_TELEMETRY_BUFFERS > 3
	case EDGETPU_MMAP_LOG3_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_LOG, 3);
	case EDGETPU_MMAP_TRACE3_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_TRACE, 3);
#endif /* EDGETPU_MAX_TELEMETRY_BUFFERS > 3 */
	case EDGETPU_MMAP_HWTRACE_BUFFER_OFFSET:
		return VMA_DATA_SET(VMA_HWTRACE, 0);
	default:
		return VMA_INVALID;
	}
}

/* Map exported LOG/TRACE/HWTRACE buffers into user space. */
int edgetpu_mmap(struct edgetpu_client *client, struct vm_area_struct *vma)
{
	struct edgetpu_dev *etdev = client->etdev;
	edgetpu_vma_flags_t flag;
	enum edgetpu_vma_type type;
	uint instance_id;

	etdev_dbg(etdev, "%s: mmap pgoff = %#lX\n", __func__, vma->vm_pgoff);

	flag = mmap_vma_flag(vma->vm_pgoff);
	type = VMA_TYPE(flag);
	instance_id = VMA_DATA_GET(flag);
	switch (type) {
	case VMA_LOG:
		if (instance_id >= etdev->num_telemetry_buffers)
			return -EINVAL;
		return edgetpu_mmap_telemetry_buffer(etdev, &etdev->telemetry_log[instance_id],
						     vma);
	case VMA_TRACE:
		if (instance_id >= etdev->num_telemetry_buffers)
			return -EINVAL;
		return edgetpu_mmap_telemetry_buffer(etdev, &etdev->telemetry_trace[instance_id],
						     vma);
	case VMA_HWTRACE:
		if (!edgetpu_telemetry_mapped(&etdev->telemetry_hwtrace))
			return -ENOENT;
		return edgetpu_mmap_telemetry_buffer(etdev, &etdev->telemetry_hwtrace, vma);
	case VMA_INVALID:
	default:
		return -EINVAL;
	}
}

int edgetpu_get_state_errno_locked(struct edgetpu_dev *etdev)
{
	switch (etdev->state) {
	case ETDEV_STATE_BAD:
	case ETDEV_STATE_NOFW:
		return -EIO;
	case ETDEV_STATE_FWLOADING:
		return -EAGAIN;
	case ETDEV_STATE_SHUTDOWN:
		return -ESHUTDOWN;
	default:
		break;
	}
	return 0;
}

static struct gcip_fw_tracing *edgetpu_firmware_tracing_create(struct edgetpu_dev *etdev)
{
	const struct gcip_fw_tracing_args fw_tracing_args = {
		.dev = etdev->dev,
		.pm = edgetpu_gcip_pm(etdev),
		.dentry = edgetpu_fs_debugfs_dir(),
		.data = etdev,
		.set_level = edgetpu_kci_firmware_tracing_level,
	};

	return gcip_firmware_tracing_create(&fw_tracing_args);
}

static void edgetpu_firmware_tracing_destroy(struct gcip_fw_tracing *fw_tracing)
{
	gcip_firmware_tracing_destroy(fw_tracing);
}

int edgetpu_device_add(struct edgetpu_dev *etdev,
		       const struct edgetpu_mapped_resource *regs,
		       uint num_ifaces)
{
	struct edgetpu_mailbox_manager_desc mailbox_manager_desc = {
		.num_ext_mailbox = EDGETPU_NUM_EXT_MAILBOXES,
		.ext_mailbox_start = EDGETPU_EXT_MAILBOX_START,
	};
	uint ordinal_id;
	int ret;

	etdev->regs = *regs;

	etdev->etiface = devm_kzalloc(
		etdev->dev, sizeof(*etdev->etiface) * num_ifaces, GFP_KERNEL);

	if (!etdev->etiface) {
		dev_err(etdev->dev,
			"Failed to allocate memory for interfaces\n");
		return -ENOMEM;
	}

	ordinal_id = atomic_add_return(1, &dev_count);

	if (!ordinal_id)
		snprintf(etdev->dev_name, EDGETPU_DEVICE_NAME_MAX, "%s",
			 DRIVER_NAME);
	else
		snprintf(etdev->dev_name, EDGETPU_DEVICE_NAME_MAX,
			 "%s.%u", DRIVER_NAME, ordinal_id);

	mutex_init(&etdev->groups_lock);
	INIT_LIST_HEAD(&etdev->groups);
	etdev->n_groups = 0;
	etdev->group_create_lockout = false;
	mutex_init(&etdev->clients_lock);
	INIT_LIST_HEAD(&etdev->clients);
	mutex_init(&etdev->wakeup_sources_lock);
	INIT_LIST_HEAD(&etdev->wakeup_sources);
	etdev->vcid_pool = (1u << EDGETPU_NUM_VCIDS) - 1;
	mutex_init(&etdev->state_lock);
	etdev->state = ETDEV_STATE_NOFW;

	/* set_device_properties not enabled in production b/405471390 */
	if (IS_ENABLED(CONFIG_EDGETPU_TEST))
		mutex_init(&etdev->device_prop.lock);

	ret = edgetpu_soc_early_init(etdev);
	if (ret)
		return ret;

	edgetpu_fs_setup_debugfs(etdev);
	atomic_set(&etdev->eventlog.next_slot, 0);

	etdev->mailbox_manager =
		edgetpu_mailbox_create_mgr(etdev, &mailbox_manager_desc);
	if (IS_ERR(etdev->mailbox_manager)) {
		ret = PTR_ERR(etdev->mailbox_manager);
		dev_err(etdev->dev,
			"%s: edgetpu_mailbox_create_mgr returns %d\n",
			etdev->dev_name, ret);
		goto err_remove_debugfs;
	}

	/* Init PM in case the platform needs power up actions before MMU setup and such. */
	ret = edgetpu_pm_create(etdev);
	if (ret) {
		etdev_err(etdev, "Failed to initialize PM interface: %d", ret);
		goto remove_mboxes;
	}

	ret = edgetpu_mmu_attach(etdev);
	if (ret) {
		dev_err(etdev->dev, "failed to attach IOMMU: %d", ret);
		goto remove_pm;
	}
	etdev->max_concurrent_clients =
		min(edgetpu_mmu_get_max_attached_domains(etdev), EDGETPU_NUM_VCIDS);

	edgetpu_usage_stats_init(etdev);

	etdev->etkci = devm_kzalloc(etdev->dev, sizeof(*etdev->etkci), GFP_KERNEL);
	if (!etdev->etkci) {
		ret = -ENOMEM;
		goto remove_usage_stats;
	}

	etdev->etikv = devm_kzalloc(etdev->dev, sizeof(*etdev->etikv), GFP_KERNEL);
	if (!etdev->etikv) {
		ret = -ENOMEM;
		goto remove_usage_stats;
	}

	etdev->etiif = devm_kzalloc(etdev->dev, sizeof(*etdev->etiif), GFP_KERNEL);
	if (!etdev->etiif) {
		ret = -ENOMEM;
		goto remove_usage_stats;
	}

	ret = edgetpu_telemetry_init(etdev);
	if (ret)
		goto remove_usage_stats;

	ret = edgetpu_kci_init(etdev, etdev->etkci);
	if (ret) {
		etdev_err(etdev, "edgetpu_kci_init returns %d\n", ret);
		goto out_telemetry_exit;
	}

	ret = edgetpu_ikv_init(etdev, etdev->etikv);
	if (ret) {
		etdev_err(etdev, "edgetpu_ikv_init returns %d\n", ret);
		goto err_kci_release;
	}

	ret = edgetpu_iif_init(etdev, etdev->etiif);
	if (ret) {
		etdev_err(etdev, "edgetpu_iif_init returns %d\n", ret);
		goto err_ikv_release;
	}

	edgetpu_debug_init(etdev);
	etdev->fw_tracing = edgetpu_firmware_tracing_create(etdev);
	if (IS_ERR(etdev->fw_tracing))
		etdev->fw_tracing = NULL;

	/* No limit on DMA segment size */
	dma_set_max_seg_size(etdev->dev, UINT_MAX);

	etdev->is_first_open = true;
	mutex_init(&etdev->first_open_lock);

	return 0;

err_ikv_release:
	edgetpu_ikv_release(etdev, etdev->etikv);
err_kci_release:
	edgetpu_kci_release(etdev, etdev->etkci);
out_telemetry_exit:
	edgetpu_telemetry_exit(etdev);
remove_usage_stats:
	edgetpu_usage_stats_exit(etdev);
	edgetpu_mmu_detach(etdev);
remove_pm:
	edgetpu_pm_destroy(etdev);
remove_mboxes:
	edgetpu_mailbox_remove_ext_mailboxes(etdev->mailbox_manager, false);
err_remove_debugfs:
	debugfs_remove_recursive(etdev->d_entry);
	edgetpu_soc_exit(etdev);
	return ret;
}

void edgetpu_device_remove(struct edgetpu_dev *etdev)
{
	int ret;

	ret = edgetpu_pm_get(etdev);
	edgetpu_firmware_tracing_destroy(etdev->fw_tracing);
	edgetpu_debug_exit(etdev);
	edgetpu_iif_release(etdev->etiif);
	edgetpu_ikv_release(etdev, etdev->etikv);
	edgetpu_kci_release(etdev, etdev->etkci);
	edgetpu_telemetry_exit(etdev);
	edgetpu_usage_stats_exit(etdev);
	edgetpu_mmu_detach(etdev);
	if (!ret)
		edgetpu_pm_put(etdev);
	edgetpu_pm_destroy(etdev);
	edgetpu_mailbox_remove_ext_mailboxes(etdev->mailbox_manager, false);
	debugfs_remove_recursive(etdev->d_entry);
	edgetpu_wakeup_source_destroy_all(etdev);
	edgetpu_soc_exit(etdev);
}

void edgetpu_client_update_name(struct edgetpu_client *client, pid_t task_id)
{
	struct task_struct *tsk;

	rcu_read_lock();
	tsk = find_task_by_vpid(task_id);
	if (tsk)
		snprintf(client->name, sizeof(client->name), "%s.%u", tsk->comm, client->client_id);
	else
		snprintf(client->name, sizeof(client->name), "?.%u", client->client_id);
	rcu_read_unlock();
}

void edgetpu_client_set_tgid(struct edgetpu_client *client, pid_t task_id)
{
	client->runtime_identified_client = true;
	client->tgid = task_id;
	edgetpu_client_update_name(client, task_id);
}

struct edgetpu_client *edgetpu_client_add(struct edgetpu_dev_iface *etiface)
{
	struct edgetpu_client *client;
	struct edgetpu_list_device_client *l = kmalloc(sizeof(*l), GFP_KERNEL);
	struct edgetpu_dev *etdev = etiface->etdev;

	if (!l)
		return ERR_PTR(-ENOMEM);
	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		kfree(l);
		return ERR_PTR(-ENOMEM);
	}
	client->client_id = atomic_add_return(1, &next_client_id);
	client->etdev = etdev;
	edgetpu_wakelock_init(client);
	client->tgid = task_tgid_nr(current);
	edgetpu_client_update_name(client, client->tgid);
	client->etiface = etiface;
	mutex_init(&client->group_lock);
	/* equivalent to edgetpu_client_get() */
	refcount_set(&client->count, 1);
	client->perdie_events = 0;
	mutex_init(&client->limited_interface_lock);
	mutex_lock(&etdev->clients_lock);
	l->client = client;
	list_add_tail(&l->list, &etdev->clients);
	mutex_unlock(&etdev->clients_lock);
	edgetpu_eventlog_event(client->etdev, EVENTLOG_EVENT_CLIENT_CREATE, client);
	return client;
}

struct edgetpu_client *edgetpu_client_get(struct edgetpu_client *client)
{
	WARN_ON_ONCE(!refcount_inc_not_zero(&client->count));
	return client;
}

void edgetpu_client_put(struct edgetpu_client *client)
{
	if (!client)
		return;
	if (refcount_dec_and_test(&client->count))
		kfree(client);
}

void edgetpu_client_remove(struct edgetpu_client *client)
{
	struct edgetpu_dev *etdev = client->etdev;
	struct edgetpu_list_device_client *lc;
	uint wakelock_count;

	edgetpu_eventlog_event(client->etdev, EVENTLOG_EVENT_CLIENT_REMOVE, client);
	mutex_lock(&client->group_lock);
	/*
	 * Safe to read wakelock->req_count here since req_count is only modified during
	 * [acquire/release]_wakelock ioctl calls which cannot race with releasing client/fd.
	 */
	wakelock_count = client->wakelock.req_count;
	mutex_unlock(&client->group_lock);

	mutex_lock(&etdev->clients_lock);
	/* remove the client from the device list */
	for_each_list_device_client(etdev, lc) {
		if (lc->client == client) {
			list_del(&lc->list);
			kfree(lc);
			break;
		}
	}
	mutex_unlock(&etdev->clients_lock);
	if (client->group) {
		edgetpu_device_group_disband(client);
		edgetpu_client_put(client);
		edgetpu_device_group_put(client->group);
		client->group = NULL;
	}
	/* Cleanup external mailbox/secure client stuff. */
	edgetpu_ext_client_remove(client);

	/* Clean up all the per die event fds registered by the client */
	if (client->perdie_events &
	    BIT(perdie_event_id_to_num(EDGETPU_PERDIE_EVENT_LOGS_AVAILABLE)))
		edgetpu_telemetry_unset_event(etdev, etdev->telemetry_log);
	if (client->perdie_events &
	    BIT(perdie_event_id_to_num(EDGETPU_PERDIE_EVENT_TRACES_AVAILABLE)))
		edgetpu_telemetry_unset_event(etdev, etdev->telemetry_trace);
	if (client->perdie_events &
	    BIT(perdie_event_id_to_num(EDGETPU_PERDIE_EVENT_HWTRACES_AVAILABLE)))
		edgetpu_telemetry_unset_event(etdev, &etdev->telemetry_hwtrace);

	/* Releases each acquired wake lock for this client. */
	while (wakelock_count--)
		edgetpu_pm_put(etdev);
	edgetpu_wakelock_destroy(client);
	edgetpu_client_put(client);
}

void edgetpu_client_trim_enable(struct edgetpu_client *client, u32 enable)
{
	client->trim_enabled = enable;
}

void edgetpu_handle_firmware_crash(struct edgetpu_dev *etdev, enum gcip_fw_crash_type crash_type)
{
	if (crash_type == GCIP_FW_CRASH_UNRECOVERABLE_FAULT) {
		etdev_err(etdev, "firmware unrecoverable crash");
		etdev->firmware_crash_count++;
		edgetpu_fatal_error_notify(etdev, EDGETPU_ERROR_FW_CRASH);
		edgetpu_debug_dump(etdev, DUMP_REASON_UNRECOVERABLE_FAULT);
	} else {
		etdev_err(etdev, "firmware non-fatal crash event: %u",
			  crash_type);
		edgetpu_debug_dump(etdev, DUMP_REASON_NON_FATAL_CRASH);
	}
}

int __init edgetpu_init(void)
{
	int ret;

	ret = edgetpu_fs_init();
	if (ret)
		return ret;
	return 0;
}

void __exit edgetpu_exit(void)
{
	edgetpu_fs_exit();
}
