/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Trace events for edgetpu
 *
 * Copyright (c) 2020 Google Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM edgetpu

#if !defined(_TRACE_EDGETPU_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_EDGETPU_H

#include <linux/stringify.h>
#include <linux/tracepoint.h>

#include "../../../edgetpu.h"
#include "../../../edgetpu-device-group.h"
#include "../../../edgetpu-internal.h"
#include "../../../gcip-kernel-driver/include/gcip/gcip-kci.h"

#define EDGETPU_TRACE_SYSTEM __stringify(TRACE_SYSTEM)

TRACE_EVENT(edgetpu_map_buffer_start,

	TP_PROTO(struct edgetpu_device_group *group, struct edgetpu_map_ioctl *ibuf),

	TP_ARGS(group, ibuf),

	TP_STRUCT__entry(
		__field(__u64, host_address)
		__field(__u64, size)
		__field(edgetpu_map_flag_t, flags)
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->host_address = ibuf->host_address;
		__entry->size = ibuf->size;
		__entry->flags = ibuf->flags;
		__entry->client_id = group->client->client_id;
	),

	TP_printk("host_address = 0x%llx, size = %llu, flags = 0x%x, client = %u",
		__entry->host_address, __entry->size, __entry->flags, __entry->client_id)
);

TRACE_EVENT(edgetpu_map_buffer_end,

	TP_PROTO(struct edgetpu_device_group *group, struct edgetpu_map_ioctl *ibuf),

	TP_ARGS(group, ibuf),

	TP_STRUCT__entry(
		__field(__u64, host_address)
		__field(__u64, size)
		__field(__u64, device_address)
		__field(edgetpu_map_flag_t, flags)
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->host_address = ibuf->host_address;
		__entry->size = ibuf->size;
		__entry->device_address = ibuf->device_address;
		__entry->flags = ibuf->flags;
		__entry->client_id = group->client->client_id;
	),

	TP_printk("host_address = 0x%llx, size = %llu, device_address = 0x%llx, flags = 0x%x, client = %u",
		__entry->host_address, __entry->size, __entry->device_address,
		__entry->flags, __entry->client_id)
);

TRACE_EVENT(edgetpu_unmap_buffer_start,

	TP_PROTO(struct edgetpu_device_group *group, struct edgetpu_map_ioctl *ibuf),

	TP_ARGS(group, ibuf),

	TP_STRUCT__entry(
		__field(__u64, device_address)
		__field(edgetpu_map_flag_t, flags)
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->device_address = ibuf->device_address;
		__entry->flags = ibuf->flags;
		__entry->client_id = group->client->client_id;
	),

	TP_printk("device_address = 0x%llx, flags = 0x%x, client = %u",
		__entry->device_address, __entry->flags, __entry->client_id)
);

TRACE_EVENT(edgetpu_unmap_buffer_end,

	TP_PROTO(struct edgetpu_device_group *group, struct edgetpu_map_ioctl *ibuf),

	TP_ARGS(group, ibuf),

	TP_STRUCT__entry(
		__field(__u64, device_address)
		__field(edgetpu_map_flag_t, flags)
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->device_address = ibuf->device_address;
		__entry->flags = ibuf->flags;
		__entry->client_id = group->client->client_id;
	),

	TP_printk("device_address = 0x%llx, flags = 0x%x, client = %u",
		__entry->device_address, __entry->flags, __entry->client_id)
);

TRACE_EVENT(edgetpu_map_dmabuf_start,

	TP_PROTO(struct edgetpu_device_group *group, struct edgetpu_map_dmabuf_ioctl *ibuf),

	TP_ARGS(group, ibuf),

	TP_STRUCT__entry(
		__field(__u64, offset)
		__field(__u64, size)
		__field(int, dmabuf_fd)
		__field(edgetpu_map_flag_t, flags)
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->offset = ibuf->offset;
		__entry->size = ibuf->size;
		__entry->dmabuf_fd = ibuf->dmabuf_fd;
		__entry->flags = ibuf->flags;
		__entry->client_id = group->client->client_id;
	),

	TP_printk("offset = 0x%llx, size = %llu, dmabuf_fd = %d, flags = 0x%x, client = %u",
		__entry->offset, __entry->size, __entry->dmabuf_fd,
		__entry->flags, __entry->client_id)
);

TRACE_EVENT(edgetpu_map_dmabuf_end,

	TP_PROTO(struct edgetpu_device_group *group, struct edgetpu_map_dmabuf_ioctl *ibuf),

	TP_ARGS(group, ibuf),

	TP_STRUCT__entry(
		__field(__u64, offset)
		__field(__u64, size)
		__field(__u64, device_address)
		__field(int, dmabuf_fd)
		__field(edgetpu_map_flag_t, flags)
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->offset = ibuf->offset;
		__entry->size = ibuf->size;
		__entry->device_address = ibuf->device_address;
		__entry->dmabuf_fd = ibuf->dmabuf_fd;
		__entry->flags = ibuf->flags;
		__entry->client_id = group->client->client_id;
	),

	TP_printk("offset = 0x%llx, size = %llu, device_address = 0x%llx, dmabuf_fd = %d, flags = 0x%x, client = %u",
		__entry->offset, __entry->size, __entry->device_address,
		__entry->dmabuf_fd, __entry->flags, __entry->client_id)
);

TRACE_EVENT(edgetpu_unmap_dmabuf_start,

	TP_PROTO(struct edgetpu_device_group *group, struct edgetpu_map_dmabuf_ioctl *ibuf),

	TP_ARGS(group, ibuf),

	TP_STRUCT__entry(
		__field(__u64, device_address)
		__field(int, dmabuf_fd)
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->device_address = ibuf->device_address;
		__entry->dmabuf_fd = ibuf->dmabuf_fd;
		__entry->client_id = group->client->client_id;
	),

	TP_printk("device_address = 0x%llx, dmabuf_fd = %d, client = %u",
		__entry->device_address, __entry->dmabuf_fd, __entry->client_id)
);

TRACE_EVENT(edgetpu_unmap_dmabuf_end,

	TP_PROTO(struct edgetpu_device_group *group, struct edgetpu_map_dmabuf_ioctl *ibuf),

	TP_ARGS(group, ibuf),

	TP_STRUCT__entry(
		__field(__u64, device_address)
		__field(int, dmabuf_fd)
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->device_address = ibuf->device_address;
		__entry->dmabuf_fd = ibuf->dmabuf_fd;
		__entry->client_id = group->client->client_id;
	),

	TP_printk("device_address = 0x%llx, dmabuf_fd = %d, client = %u",
		__entry->device_address, __entry->dmabuf_fd, __entry->client_id)
);

TRACE_EVENT(edgetpu_acquire_wakelock_start,

	TP_PROTO(struct edgetpu_client *client, u32 flags),

	TP_ARGS(client, flags),

	TP_STRUCT__entry(
		__field(int, client_id)
		__field(u32, flags)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
		__entry->flags = flags;
	),

	TP_printk("client = %u flags = %u", __entry->client_id, __entry->flags)
);

TRACE_EVENT(edgetpu_acquire_wakelock_end,

	TP_PROTO(struct edgetpu_client *client, int count, int ret),

	TP_ARGS(client, count, ret),

	TP_STRUCT__entry(
		__field(int, client_id)
		__field(int, count)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
		__entry->count = count;
		__entry->ret = ret;
	),

	TP_printk("client = %u req_count = %d, ret = %d",
		  __entry->client_id, __entry->count, __entry->ret)
);

TRACE_EVENT(edgetpu_release_wakelock_start,

	TP_PROTO(struct edgetpu_client *client),

	TP_ARGS(client),

	TP_STRUCT__entry(
		__field(int, client_id)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
	),

	TP_printk("client = %u", __entry->client_id)
);

TRACE_EVENT(edgetpu_release_wakelock_end,

	TP_PROTO(struct edgetpu_client *client, int count),

	TP_ARGS(client, count),

	TP_STRUCT__entry(
		__field(int, client_id)
		__field(int, count)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
		__entry->count = count;
	),

	TP_printk("client = %u, req_count = %d", __entry->client_id, __entry->count)
);

TRACE_EVENT(edgetpu_vii_command_start,

	TP_PROTO(struct edgetpu_client *client),

	TP_ARGS(client),

	TP_STRUCT__entry(
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
	),

	TP_printk("client = %u",
		  __entry->client_id)
);

TRACE_EVENT(edgetpu_vii_command_end,

	TP_PROTO(struct edgetpu_client *client, struct edgetpu_vii_command_ioctl *ibuf, int ret),

	TP_ARGS(client, ibuf, ret),

	TP_STRUCT__entry(
		__field(__u32, client_id)
		__field(__u64, seq)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
		__entry->seq = ibuf->command.seq;
		__entry->ret = ret;
	),

	TP_printk("client = %u, seq = %llu (ret = %d)",
		  __entry->client_id, __entry->seq, __entry->ret)

);

TRACE_EVENT(edgetpu_vii_response_start,

	TP_PROTO(struct edgetpu_client *client),

	TP_ARGS(client),

	TP_STRUCT__entry(
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
	),

	TP_printk("client = %u",
		  __entry->client_id)
);

TRACE_EVENT(edgetpu_vii_response_end,

	TP_PROTO(struct edgetpu_client *client, struct edgetpu_vii_response_ioctl *ibuf, int ret),

	TP_ARGS(client, ibuf, ret),

	TP_STRUCT__entry(
		__field(__u32, client_id)
		__field(__u64, seq)
		__field(__u64, retval)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
		__entry->seq = ibuf->response.seq;
		__entry->retval = ibuf->response.retval;
		__entry->ret = ret;
	),

	TP_printk("client = %u, seq = %llu, retval = 0x%llx (ret = %d)",
		  __entry->client_id, __entry->seq, __entry->retval, __entry->ret)
);

TRACE_EVENT(edgetpu_vii_litebuf_command_start,

	TP_PROTO(struct edgetpu_client *client),

	TP_ARGS(client),

	TP_STRUCT__entry(
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
	),

	TP_printk("client = %u",
		  __entry->client_id)
);

TRACE_EVENT(edgetpu_vii_litebuf_command_end,

	TP_PROTO(struct edgetpu_client *client, struct edgetpu_vii_litebuf_command_ioctl *ibuf,
		 int ret),

	TP_ARGS(client, ibuf, ret),

	TP_STRUCT__entry(
		__field(__u32, client_id)
		__field(__u64, seq)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
		__entry->seq = ibuf->seq;
		__entry->ret = ret;
	),

	TP_printk("client = %u, seq = %llu (ret = %d)",
		  __entry->client_id, __entry->seq, __entry->ret)

);

TRACE_EVENT(edgetpu_vii_litebuf_response_start,

	TP_PROTO(struct edgetpu_client *client),

	TP_ARGS(client),

	TP_STRUCT__entry(
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
	),

	TP_printk("client = %u",
		  __entry->client_id)
);

TRACE_EVENT(edgetpu_vii_litebuf_response_end,

	TP_PROTO(struct edgetpu_client *client, struct edgetpu_vii_litebuf_response_ioctl *ibuf,
		 int ret),

	TP_ARGS(client, ibuf, ret),

	TP_STRUCT__entry(
		__field(__u32, client_id)
		__field(__u64, seq)
		__field(__u16, code)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
		__entry->seq = ibuf->seq;
		__entry->code = ibuf->code;
		__entry->ret = ret;
	),

	TP_printk("client = %u, seq = %llu, code = 0x%hx (ret = %d)",
		  __entry->client_id, __entry->seq, __entry->code, __entry->ret)
);

TRACE_EVENT(edgetpu_iif_unblocked_start,

	TP_PROTO(struct iif_fence *fence),

	TP_ARGS(fence),

	TP_STRUCT__entry(
		__field(int, id)
		__field(int, signal_error)
	),

	TP_fast_assign(
		__entry->id = fence->id;
		__entry->signal_error = fence->signal_error;
	),

	TP_printk("fence id = %d, signal_error = %d", __entry->id, __entry->signal_error)
);

TRACE_EVENT(edgetpu_iif_unblocked_end,

	TP_PROTO(u32 fence_id),

	TP_ARGS(fence_id),

	TP_STRUCT__entry(
		__field(u32, fence_id)
	),

	TP_fast_assign(
		__entry->fence_id = fence_id;
	),

	TP_printk("fence id = %u", __entry->fence_id)
);

TRACE_EVENT(edgetpu_client_create,

	TP_PROTO(struct edgetpu_client *client),

	TP_ARGS(client),

	TP_STRUCT__entry(
		__field(__u32, client_id)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
	),

	TP_printk("client = %u",
		  __entry->client_id)
);

TRACE_EVENT(edgetpu_client_group_create,

	TP_PROTO(struct edgetpu_client *client),

	TP_ARGS(client),

	TP_STRUCT__entry(
		__field(uint, client_id)
		__field(uint, vcid)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
		__entry->vcid = client->group->vcid;
	),

	TP_printk("client = %u vcid = %u",
		  __entry->client_id, __entry->vcid)
);

TRACE_EVENT(edgetpu_client_remove,

	TP_PROTO(struct edgetpu_client *client),

	TP_ARGS(client),

	TP_STRUCT__entry(
		__field(int, client_id)
		__field(uint, wakelock_count)
	),

	TP_fast_assign(
		__entry->client_id = client->client_id;
		__entry->wakelock_count = client->wakelock.req_count;
	),

	TP_printk("client = %u wake = %u",
		  __entry->client_id, __entry->wakelock_count)
);

TRACE_EVENT(edgetpu_power_state,

	TP_PROTO(int state),

	TP_ARGS(state),

	TP_STRUCT__entry(
		__field(int, state)
	),

	TP_fast_assign(
		__entry->state = state;
	),

	TP_printk("state = %d",
		  __entry->state)
);

TRACE_EVENT(edgetpu_kci_command_start,

	TP_PROTO(struct gcip_kci_command_element *cmd),

	TP_ARGS(cmd),

	TP_STRUCT__entry(
		__field(u16, code)
	),

	TP_fast_assign(
		__entry->code = cmd->code;
	),

	TP_printk("code = %u",
		  __entry->code)
);

TRACE_EVENT(edgetpu_kci_command_end,

	TP_PROTO(struct gcip_kci_command_element *cmd, int ret),

	TP_ARGS(cmd, ret),

	TP_STRUCT__entry(
		__field(u16, code)
		__field(u64, seq)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->code = cmd->code;
		__entry->seq = cmd->seq;
		__entry->ret = ret;
	),

	TP_printk("code = %u seq = %llu ret = %d",
		  __entry->code, __entry->seq, __entry->ret)
);

TRACE_EVENT(edgetpu_rkci,

	TP_PROTO(struct gcip_kci_response_element *resp),

	TP_ARGS(resp),

	TP_STRUCT__entry(
		__field(u16, code)
		__field(u64, seq)
		__field(u32, value1)
		__field(u32, value2)
	),

	TP_fast_assign(
		__entry->code = resp->code;
		__entry->seq = resp->seq;
		__entry->value1 = resp->rkci_value1;
		__entry->value2 = resp->rkci_value2;
	),

	TP_printk("code = %u seq = %llu value1 = %u value2 = %u",
		  __entry->code, __entry->seq, __entry->value1, __entry->value2)
);

TRACE_EVENT(edgetpu_pm_runtime_get_sync_start,

	TP_PROTO(struct edgetpu_dev *etdev),

	TP_ARGS(etdev),

	TP_STRUCT__entry(
		__field(int, dev_state)
	),

	TP_fast_assign(
		__entry->dev_state = etdev->state;
	),

	TP_printk("state = %d", __entry->dev_state)
);

TRACE_EVENT(edgetpu_pm_runtime_get_sync_end,

	TP_PROTO(struct edgetpu_dev *etdev, int ret),

	TP_ARGS(etdev, ret),

	TP_STRUCT__entry(
		__field(int, dev_state)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->dev_state = etdev->state;
		__entry->ret = ret;
	),

	TP_printk("state = %d, ret = %d", __entry->dev_state, __entry->ret)
);

TRACE_EVENT(edgetpu_firmware_restart_start,

	TP_PROTO(struct edgetpu_dev *etdev, bool force_reset),

	TP_ARGS(etdev, force_reset),

	TP_STRUCT__entry(
		__field(int, dev_state)
		__field(bool, force_reset)
	),

	TP_fast_assign(
		__entry->dev_state = etdev->state;
		__entry->force_reset = force_reset;
	),

	TP_printk("state = %d, force_reset = %d", __entry->dev_state, __entry->force_reset)
);

TRACE_EVENT(edgetpu_firmware_restart_end,

	TP_PROTO(struct edgetpu_dev *etdev, int ret),

	TP_ARGS(etdev, ret),

	TP_STRUCT__entry(
		__field(int, dev_state)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->dev_state = etdev->state;
		__entry->ret = ret;
	),

	TP_printk("state = %d, ret = %d", __entry->dev_state, __entry->ret)
);

TRACE_EVENT(edgetpu_firmware_handshake_start,

	TP_PROTO(struct edgetpu_dev *etdev),

	TP_ARGS(etdev),

	TP_STRUCT__entry(
		__field(int, dev_state)
	),

	TP_fast_assign(
		__entry->dev_state = etdev->state;
	),

	TP_printk("state = %d", __entry->dev_state)
);

TRACE_EVENT(edgetpu_firmware_handshake_end,

	TP_PROTO(struct edgetpu_dev *etdev, int ret),

	TP_ARGS(etdev, ret),

	TP_STRUCT__entry(
		__field(int, dev_state)
		__field(int, ret)
	),

	TP_fast_assign(
		__entry->dev_state = etdev->state;
		__entry->ret = ret;
	),

	TP_printk("state = %d, ret = %d", __entry->dev_state, __entry->ret)
);

#endif /* _TRACE_EDGETPU_H */

/* This part must be outside protection */
#include <trace/define_trace.h>
