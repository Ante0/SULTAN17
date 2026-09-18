// SPDX-License-Identifier: GPL-2.0-only
/*
 * EdgeTPU support for dma-buf.
 *
 * Copyright (C) 2020-2026 Google LLC
 */

#include <linux/dma-buf.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/rwsem.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include <gcip/gcip-mapping.h>

#include "edgetpu-device-group.h"
#include "edgetpu-dmabuf.h"
#include "edgetpu-internal.h"
#include "edgetpu-mapping.h"
#include "edgetpu-mmu.h"
#include "edgetpu.h"

/*
 * Clean resources recorded in @dmap.
 *
 * Caller holds the lock of group (map->priv) and ensures the group is in the ready state.
 */
static void dmabuf_mapping_destroy(struct edgetpu_mapping *mapping)
{
	struct edgetpu_device_group *group = mapping->priv;

	gcip_mapping_unmap(mapping->gcip_mapping);
	edgetpu_device_group_put(group);
	kfree(mapping);
}

static void dmabuf_map_callback_show(struct edgetpu_mapping *map, struct seq_file *s)
{
	gcip_mapping_dmabuf_show(map->gcip_mapping, s);
}

/**
 * dmabuf_mapping_create() - Maps the DMA buffer and creates the corresponding mapping object.
 * @group: The group that the DMA buffer belongs to.
 * @fd: The file descriptor of the DMA buffer.
 * @flags: The flags used to map the DMA buffer.
 * @limited: Whether the mapping is created on behalf of a limited interface.
 *
 * Return: The pointer of the target mapping object or an error pointer on failure.
 */
static struct edgetpu_mapping *dmabuf_mapping_create(struct edgetpu_device_group *group, int fd,
						     edgetpu_map_flag_t flags, bool limited)
{
	struct edgetpu_mapping *mapping;
	struct edgetpu_iommu_domain *etdomain;
	struct dma_buf *dmabuf;
	int ret;
	u64 gcip_map_flags = edgetpu_mappings_encode_gcip_map_flags(flags, 0, false);

	dmabuf = dma_buf_get(fd);
	if (IS_ERR(dmabuf))
		return ERR_CAST(dmabuf);

	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		ret = -ENOMEM;
		goto err_dma_buf_put;
	}

	mapping->flags = flags;
	mapping->mmu_flags = map_to_mmu_flags(flags);
	mapping->priv = edgetpu_device_group_get(group);
	mapping->release = dmabuf_mapping_destroy;
	mapping->show = dmabuf_map_callback_show;
	mapping->mapped_by_limited = limited;

	down_read(&group->lock);
	mutex_lock(&group->mapping_lock);
	if (!edgetpu_device_group_is_ready(group)) {
		ret = edgetpu_group_errno(group);
		etdev_err(group->etdev, "client %s already errored: %d", group->client->name, ret);
		mutex_unlock(&group->mapping_lock);
		up_read(&group->lock);
		goto err_device_group_put;
	}
	etdomain = edgetpu_group_domain_locked(group);

	mapping->gcip_mapping = gcip_mapping_dmabuf_map(etdomain->gdomain, dmabuf, gcip_map_flags);
	mutex_unlock(&group->mapping_lock);
	up_read(&group->lock);
	if (IS_ERR(mapping->gcip_mapping)) {
		ret = PTR_ERR(mapping->gcip_mapping);
		edgetpu_device_group_log_map_error(group, dmabuf->size, flags, ret);
		goto err_device_group_put;
	}

	dma_buf_put(dmabuf);

	return mapping;

err_device_group_put:
	edgetpu_device_group_put(group);
	kfree(mapping);
err_dma_buf_put:
	dma_buf_put(dmabuf);
	return ERR_PTR(ret);
}

int edgetpu_map_dmabuf(struct edgetpu_device_group *group, struct edgetpu_map_dmabuf_ioctl *arg,
		       bool limited)
{
	struct edgetpu_mapping *mapping;
	int ret;

	/* Establishing new TPU mappings sets client to "not OK to trim" state. */
	group->client->trim_enabled = false;
	mapping = dmabuf_mapping_create(group, arg->dmabuf_fd, arg->flags, limited);
	if (IS_ERR(mapping))
		return PTR_ERR(mapping);

	/* Save address before add to mapping tree, after which another thread can free it. */
	arg->device_address = mapping->gcip_mapping->device_address;
	ret = edgetpu_mapping_add(&group->dmabuf_mappings, mapping);
	if (ret)
		goto err_destroy_mapping;

	return 0;

err_destroy_mapping:
	dmabuf_mapping_destroy(mapping);
	return ret;
}

int edgetpu_unmap_dmabuf(struct edgetpu_device_group *group, tpu_addr_t tpu_addr, bool limited)
{
	struct edgetpu_mapping_root *mappings = &group->dmabuf_mappings;
	struct edgetpu_mapping *map;

	edgetpu_mapping_lock(mappings);
	map = edgetpu_mapping_find_locked(mappings, tpu_addr, limited);
	if (!map) {
		edgetpu_mapping_unlock(mappings);
		etdev_err(group->etdev, "unmap client %s iova %pad not found",
			  group->client->name, &tpu_addr);
		return -EINVAL;
	}
	edgetpu_mapping_unlink(mappings, map);
	edgetpu_mapping_unlock(mappings);
	map->release(map);
	return 0;
}
