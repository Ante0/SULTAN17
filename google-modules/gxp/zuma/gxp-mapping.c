// SPDX-License-Identifier: GPL-2.0-only
/*
 * Records the mapped device addresses.
 *
 * Copyright (C) 2021 Google LLC
 */

#include <linux/atomic.h>
#include <linux/dma-mapping.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/mmap_lock.h>
#include <linux/moduleparam.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <gcip/gcip-iommu-reserve.h>
#include <gcip/gcip-mapping.h>

#include "gxp-client.h"
#include "gxp-debug-dump.h"
#include "gxp-dma.h"
#include "gxp-dmabuf.h"
#include "gxp-internal.h"
#include "gxp-mapping.h"

#include <trace/events/gxp.h>

#if IS_GXP_TEST
/* expose this variable to have unit tests set it dynamically */
bool gxp_log_iova;
#else
static bool gxp_log_iova;
#endif

module_param_named(log_iova, gxp_log_iova, bool, 0660);

void gxp_mapping_iova_log(struct gxp_client *client, struct gxp_mapping *map,
			  u8 mask)
{
	static bool is_first_log = true;
	struct device *dev = client->gxp->dev;
	const char *op = mask & GXP_IOVA_LOG_MAP ? "MAP" : "UNMAP";
	const char *buf_type = mask & GXP_IOVA_LOG_DMABUF ? "DMABUF" : "BUFFER";

	if (likely(!gxp_log_iova))
		return;

	if (is_first_log) {
		dev_info(
			dev,
			"iova_log_start: operation, buf_type, tgid, pid, host_address, device_address, size");
		is_first_log = false;
	}

	dev_info(dev, "iova_log: %s, %s, %d, %d, %#llx, %pad, %zu", op, buf_type, client->pid,
		 client->tgid, map->host_address, &map->gcip_mapping->device_address,
		 map->gcip_mapping->size);
}

/* Destructor for a mapping created with `gxp_mapping_create()` */
static void destroy_mapping(struct gxp_mapping *mapping)
{
	dma_addr_t device_address = mapping->gcip_mapping->device_address;
	size_t size = mapping->gcip_mapping->size;

	trace_gxp_mapping_destroy_start(device_address, size);

	mutex_destroy(&mapping->vlock);

	gcip_mapping_unmap(mapping->gcip_mapping);

	kfree(mapping);

	trace_gxp_mapping_destroy_end(device_address, size);
}

struct gxp_mapping *gxp_mapping_create(struct gxp_dev *gxp, struct gcip_iommu_reserve_manager *mgr,
				       struct gcip_iommu_domain *domain, u64 user_address,
				       size_t size, u32 flags, enum dma_data_direction dir,
				       dma_addr_t iova_hint)
{
	struct gxp_mapping *mapping;
	int ret;
	u64 gcip_map_flags = gxp_dma_encode_gcip_map_flags(flags, DMA_ATTR_SKIP_CPU_SYNC);

	trace_gxp_mapping_create_start(user_address, size);

	/* Initialize mapping book-keeping */
	mapping = kzalloc(sizeof(*mapping), GFP_KERNEL);
	if (!mapping) {
		ret = -ENOMEM;
		goto error_end_trace;
	}

	mapping->destructor = destroy_mapping;
	mapping->host_address = user_address;
	mapping->gxp = gxp;
	mapping->gxp_dma_flags = flags;

	if (!iova_hint)
		mapping->gcip_mapping = gcip_mapping_buffer_map(
			domain, user_address, size, gcip_map_flags, &gxp->pin_user_pages_lock);
	else
		mapping->gcip_mapping = gcip_iommu_reserve_map_buffer(mgr, user_address, size,
								      gcip_map_flags,
								      &gxp->pin_user_pages_lock,
								      iova_hint, mapping);
	if (IS_ERR(mapping->gcip_mapping)) {
		ret = PTR_ERR(mapping->gcip_mapping);
		dev_err(gxp->dev, "Failed to map user buffer (ret=%d)\n", ret);
		goto error_free_mapping;
	}

	if (dir != mapping->gcip_mapping->dir)
		dev_warn_ratelimited(
			gxp->dev,
			"Map buffer direction overridden to dir: %#x (iova = %#llx, size = %zx) ",
			mapping->gcip_mapping->dir, mapping->gcip_mapping->device_address,
			mapping->gcip_mapping->size);

	refcount_set(&mapping->refcount, 1);
	mutex_init(&mapping->vlock);

	trace_gxp_mapping_create_end(user_address, size, mapping->gcip_mapping->sgt->nents);

	return mapping;

error_free_mapping:
	kfree(mapping);
error_end_trace:
	trace_gxp_mapping_create_end(user_address, size, 0);

	return ERR_PTR(ret);
}

bool gxp_mapping_get(struct gxp_mapping *mapping)
{
	return refcount_inc_not_zero(&mapping->refcount);
}

void gxp_mapping_put(struct gxp_mapping *mapping)
{
	/* `refcount_dec_and_test()` returns true if the refcount drops to 0 */
	if (refcount_dec_and_test(&mapping->refcount))
		mapping->destructor(mapping);
}

int gxp_mapping_sync(struct gxp_mapping *mapping, u32 offset, u32 size, bool for_cpu)
{
	return gcip_mapping_buffer_sync(mapping->gcip_mapping, mapping->gxp->dev, offset, size,
					for_cpu);
}

void *gxp_mapping_vmap(struct gxp_mapping *mapping, bool is_dmabuf)
{
	struct sg_table *sgt;
	struct sg_page_iter sg_iter;
	struct page **pages;
	void *vaddr;
	int i = 0;
	u32 page_count = 0;

	if (!gxp_mapping_get(mapping))
		return ERR_PTR(-ENODEV);

	mutex_lock(&mapping->vlock);

	/* Check if user buffer has already been mapped to kernel */
	if (mapping->vmap_count) {
		vaddr = mapping->virtual_address;
		mapping->vmap_count++;
		goto out;
	}

	sgt = mapping->gcip_mapping->sgt;
	if (!sgt) {
		vaddr = ERR_PTR(-EINVAL);
		goto out;
	}

	for_each_sg_page(sgt->sgl, &sg_iter, sgt->orig_nents, 0)
		page_count++;

	pages = kvmalloc((page_count * sizeof(*pages)), GFP_KERNEL);
	if (!pages) {
		vaddr = ERR_PTR(-ENOMEM);
		goto out;
	}

	for_each_sg_page(sgt->sgl, &sg_iter, sgt->orig_nents, 0)
		pages[i++] = sg_page_iter_page(&sg_iter);

	vaddr = vmap(pages, page_count, VM_MAP, PAGE_KERNEL);
	kvfree(pages);
	if (vaddr == NULL) {
		dev_err(mapping->gxp->dev,
			"Failed to map user buffer to kernel");
		vaddr = ERR_PTR(-ENOMEM);
		goto out;
	}

	mapping->virtual_address = vaddr;
	mapping->page_count = page_count;
	mapping->vmap_count = 1;

	/* Hold a reference to the mapping so long as it is vmapped */
	gxp_mapping_get(mapping);

out:
	mutex_unlock(&mapping->vlock);

	gxp_mapping_put(mapping);

	return vaddr;
}

void gxp_mapping_vunmap(struct gxp_mapping *mapping)
{
	if (!gxp_mapping_get(mapping))
		return;

	mutex_lock(&mapping->vlock);

	/*
	 * Exit immediately if the mapping was never vmapped, or still has
	 * other users expecting it to be vmapped.
	 */
	if (!mapping->vmap_count || --mapping->vmap_count)
		goto out;

	vunmap(mapping->virtual_address);
	mapping->virtual_address = 0;
	mapping->page_count = 0;

	/* Release the reference from gxp_mapping_vmap() */
	gxp_mapping_put(mapping);

out:
	mutex_unlock(&mapping->vlock);

	gxp_mapping_put(mapping);
}
