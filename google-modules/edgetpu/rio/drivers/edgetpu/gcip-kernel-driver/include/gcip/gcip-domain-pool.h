/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GCIP IOMMU domain allocator.
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GCIP_DOMAIN_POOL_H__
#define __GCIP_DOMAIN_POOL_H__

#include <linux/device.h>
#include <linux/idr.h>
#include <linux/iommu.h>
#include <linux/types.h>
#include <linux/xarray.h>

#include <gcip/gcip-iommu.h>

/**
 * struct gcip_domain_pool - GCIP IOMMU domain pool.
 * @dev: Device to interact with the IOMMU interfaces.
 * @size: Number of domains to be pre-allocated.
 * @pasid_pool: PASID pool.
 * @min_pasid: Minimum PASID value.
 * @max_pasid: Maximum PASID value.
 * @domain_xa: Xarray of GCIP IOMMU domains.
 */
struct gcip_domain_pool {
	struct device *dev;
	size_t size;
	struct ida pasid_pool;
	ioasid_t min_pasid;
	ioasid_t max_pasid;
	struct xarray domain_xa;
};

/**
 * gcip_domain_pool_init() - Initializes an IOMMU domain pool.
 * @pool: IOMMU domain pool to be initialized.
 * @dev: Device used to retrieve the node properties from the device tree.
 * @size: Number of domains to be pre-allocated.
 * @domain_type: Type of the IOMMU domain.
 * @granule: IOMMU granule size (must be a power of 2).
 *
 * The IOVA space base address and size will be retrieved from the "gcip-dma-window" property in the
 * device tree.
 *
 * The reserved IOVA space will be retrieved from "gcip-reserved-dma-window" property in the device
 * tree.
 *
 * The number of PASIDs will be retrieved from "pasid-num-bits" property in the device tree.
 * The range of valid PASIDs is set to [1, BIT(pasid_num_bits) - 1].
 *
 * Return: 0 on success or a negative errno otherwise.
 */
int gcip_domain_pool_init(struct gcip_domain_pool *pool, struct device *dev, int size,
			  enum gcip_iommu_domain_type domain_type, size_t granule);

/**
 * gcip_domain_pool_exit() - Reverts gcip_domain_pool_init().
 * @pool: The IOMMU domain pool to be destroyed.
 */
void gcip_domain_pool_exit(struct gcip_domain_pool *pool);

/*
 * Allocates a domain from the pool
 * returns NULL on error.
 */
struct gcip_iommu_domain *gcip_domain_pool_alloc(struct gcip_domain_pool *pool);

/* Releases a domain from the pool. */
void gcip_domain_pool_free(struct gcip_domain_pool *pool, struct gcip_iommu_domain *gdomain);

/*
 * Attaches a GCIP IOMMU domain and sets the obtained PASID
 *
 * @pool: IOMMU domain pool @domain was allocated from
 * @domain: The GCIP IOMMU domain to attach
 *
 * On success, @domain->pasid will be set to obtained PASID
 *
 * Returns:
 * * 0 - Domain successfully attached with a PASID
 * * -ENOSYS - This device does not support attaching multiple domains
 * * other   - Failed to attach the domain or obtain a PASID for it
 */
int gcip_domain_pool_attach(struct gcip_domain_pool *pool, struct gcip_iommu_domain *gdomain);

/*
 * Detaches a GCIP IOMMU domain
 *
 * @pool: IOMMU domain pool @domain was allocated from and attached by
 * @domain: The GCIP IOMMU domain to detach
 */
void gcip_domain_pool_detach(struct gcip_domain_pool *pool, struct gcip_iommu_domain *gdomain);

/*
 * Returns the number of PASIDs can be used.
 *
 * @pool: IOMMU domain pool.
 */
static inline int gcip_iommu_domain_pool_get_num_pasid(struct gcip_domain_pool *pool)
{
	return pool->max_pasid - pool->min_pasid + 1;
}

#endif /* __GCIP_DOMAIN_POOL_H__ */
