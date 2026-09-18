// SPDX-License-Identifier: GPL-2.0-only
/*
 * GCIP IOMMU domain allocator.
 *
 * Copyright (C) 2022-2025 Google LLC
 */

 #include <linux/bitops.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/log2.h>
#include <linux/idr.h>
#include <linux/iommu.h>
#include <linux/of.h>
#include <linux/xarray.h>

#include <gcip/gcip-domain-pool.h>
#include <gcip/gcip-iommu.h>

#define GCIP_DOMAIN_POOL_MARK_AVAILABLE XA_MARK_0

int gcip_domain_pool_init(struct gcip_domain_pool *pool, struct device *dev, int size,
			  enum gcip_iommu_domain_type domain_type, size_t granule)
{
	struct gcip_iommu_domain *gdomain;
	struct iommu_domain *domain;
	dma_addr_t space_daddr, reserved_daddr;
	size_t space_size, reserved_size;
	u32 pasid_num_bits;
	int ret;
	int i;

	if (!size || !is_power_of_2(granule))
		return -EINVAL;

	ret = of_property_read_u32(dev->of_node, "pasid-num-bits", &pasid_num_bits);
	if (ret) {
		dev_err(dev, "Failed to fetch pasid-num-bits (%d)\n", ret);
		return ret;
	}

	ret = gcip_iommu_get_space_config(dev, &space_daddr, &space_size, &reserved_daddr,
					       &reserved_size);
	if (ret)
		return ret;

	pool->size = size;
	pool->dev = dev;
	pool->min_pasid = 1;
	pool->max_pasid = BIT(pasid_num_bits) - 1;

	ida_init(&pool->pasid_pool);
	xa_init(&pool->domain_xa);

	for (i = 0; i < size; i++) {
		domain = iommu_domain_alloc(dev->bus);
		if (!domain)
			domain = ERR_PTR(-ENOMEM);
		if (IS_ERR(domain)) {
			dev_err(pool->dev, "Failed to allocate iommu domain %d of %u\n", i + 1,
				size);
			ret = PTR_ERR(domain);
			goto err_free_domain;
		}

		gdomain = gcip_iommu_domain_create(dev, domain, domain_type, space_daddr,
						   space_size, reserved_daddr, reserved_size,
						   granule);
		if (IS_ERR(gdomain)) {
			iommu_domain_free(domain);
			ret = PTR_ERR(gdomain);
			goto err_free_domain;
		}

		xa_store(&pool->domain_xa, i, gdomain, GFP_KERNEL);
		xa_set_mark(&pool->domain_xa, i, GCIP_DOMAIN_POOL_MARK_AVAILABLE);
	}

	return 0;

err_free_domain:
	while (i--) {
		gdomain = xa_load(&pool->domain_xa, i);
		domain = gdomain->domain;
		gcip_iommu_domain_destroy(gdomain);
		iommu_domain_free(domain);
	}

	xa_destroy(&pool->domain_xa);
	ida_destroy(&pool->pasid_pool);

	return ret;
}

void gcip_domain_pool_exit(struct gcip_domain_pool *pool)
{
	struct gcip_iommu_domain *gdomain;
	struct iommu_domain *domain;
	int i;

	for (i = 0; i < pool->size; i++) {
		gdomain = xa_load(&pool->domain_xa, i);
		domain = gdomain->domain;
		gcip_iommu_domain_destroy(gdomain);
		iommu_domain_free(domain);
	}

	xa_destroy(&pool->domain_xa);
	ida_destroy(&pool->pasid_pool);
}

struct gcip_iommu_domain *gcip_domain_pool_alloc(struct gcip_domain_pool *pool)
{
	struct gcip_iommu_domain *gdomain;
	unsigned long id = 0;

	xa_lock(&pool->domain_xa);

	gdomain = xa_find(&pool->domain_xa, &id, pool->size - 1, GCIP_DOMAIN_POOL_MARK_AVAILABLE);

	if (gdomain)
		__xa_clear_mark(&pool->domain_xa, id, GCIP_DOMAIN_POOL_MARK_AVAILABLE);
	else
		gdomain = ERR_PTR(-ENOSPC);

	xa_unlock(&pool->domain_xa);

	return gdomain;
}

void gcip_domain_pool_free(struct gcip_domain_pool *pool, struct gcip_iommu_domain *gdomain)
{
	struct gcip_iommu_domain *cur;
	unsigned long id;

	xa_for_each(&pool->domain_xa, id, cur) {
		if (cur == gdomain) {
			xa_set_mark(&pool->domain_xa, id, GCIP_DOMAIN_POOL_MARK_AVAILABLE);
			return;
		}
	}

	dev_err(pool->dev, "Domain not found in pool\n");
}

int gcip_domain_pool_attach(struct gcip_domain_pool *pool, struct gcip_iommu_domain *gdomain)
{
	int ret;
	int pasid;

	if (gdomain->pasid != IOMMU_PASID_INVALID)
		/* Already attached. */
		return gdomain->pasid;

	pasid = ida_alloc_range(&pool->pasid_pool, pool->min_pasid, pool->max_pasid, GFP_KERNEL);
	if (pasid < 0)
		return pasid;

	ret = iommu_attach_device_pasid(gdomain->domain, pool->dev, pasid);
	if (ret) {
		ida_free(&pool->pasid_pool, pasid);
		return ret;
	}

	gdomain->pasid = pasid;

	return ret;
}

void gcip_domain_pool_detach(struct gcip_domain_pool *pool, struct gcip_iommu_domain *gdomain)
{
	if (gdomain->pasid == IOMMU_PASID_INVALID)
		return;

	iommu_detach_device_pasid(gdomain->domain, pool->dev, gdomain->pasid);
	ida_free(&pool->pasid_pool, gdomain->pasid);
	gdomain->pasid = IOMMU_PASID_INVALID;
}
