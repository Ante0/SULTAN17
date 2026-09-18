/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * EdgeTPU support for buffers backed by dma-buf.
 *
 * Copyright (C) 2020-2026 Google LLC
 */
#ifndef __EDGETPU_DMABUF_H__
#define __EDGETPU_DMABUF_H__

#include "edgetpu-device-group.h"
#include "edgetpu-internal.h"
#include "edgetpu.h"

/**
 * edgetpu_map_dmabuf() - Maps a dma-buf to a device group.
 * @group: The device group to map the buffer to.
 * @arg: Pointer to the map dma-buf ioctl argument.
 * @limited: Set to true if being called on behalf of a limited interface.
 *
 * The arg->device_address will be set as the mapped TPU VA on success.
 *
 * Return: 0 on success or a negative errno on error.
 */
int edgetpu_map_dmabuf(struct edgetpu_device_group *group, struct edgetpu_map_dmabuf_ioctl *arg,
		       bool limited);

/**
 * edgetpu_unmap_dmabuf() - Unmap the dma-buf backed buffer from a device group.
 * @group: The device group to unmap the buffer from.
 * @tpu_addr: The TPU virtual address of the mapping to unmap.
 * @limited: Set to true if being called on behalf of a limited interface.
 *
 * If @limited == true, only mappings created with @limited == true will be unmapped. Otherwise the
 * unmap will fail and return -EINVAL.
 *
 * Return: 0 on success or a negative errno on error.
 */
int edgetpu_unmap_dmabuf(struct edgetpu_device_group *group, tpu_addr_t tpu_addr, bool limited);

#endif /* __EDGETPU_DMABUF_H__ */
