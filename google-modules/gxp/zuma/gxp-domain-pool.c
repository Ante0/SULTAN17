// SPDX-License-Identifier: GPL-2.0-only
/*
 * GXP IOMMU domain allocator.
 *
 * Copyright (C) 2022 Google LLC
 */

#include <linux/bits.h>
#include <linux/iommu.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>

#include <gcip/gcip-domain-pool.h>
#include <gcip/gcip-iommu.h>

#include "gxp-config.h"
#include "gxp-dma.h"
#include "gxp-domain-pool.h"

/*
 * See enum gcip_iommu_domain_type.
 * Default(0) = utilizing iova_domain
 */
static int gxp_gcip_iommu_domain_type;
module_param_named(gcip_iommu_domain_type, gxp_gcip_iommu_domain_type, int,
		   0660);

int gxp_domain_pool_init(struct gxp_dev *gxp, struct gcip_domain_pool *pool,
			 unsigned int num_domains)
{
	size_t granularity = GXP_MMU_GRANULARITY_IS_PAGE ? PAGE_SIZE : SZ_4K;

	return gcip_domain_pool_init(pool, gxp->dev, num_domains, gxp_gcip_iommu_domain_type,
				     granularity);
}

struct gcip_iommu_domain *
gxp_domain_pool_alloc(struct gcip_domain_pool *pool)
{
	struct gcip_iommu_domain *gdomain = gcip_domain_pool_alloc(pool);
	struct iommu_domain *domain = gdomain->domain;

	if (IS_ERR_OR_NULL(gdomain))
		return NULL;
	iommu_set_fault_handler(domain, gxp_iommu_fault_handler, NULL);

	return gdomain;
}

void gxp_domain_pool_free(struct gcip_domain_pool *pool,
			  struct gcip_iommu_domain *gdomain)
{
	gcip_domain_pool_free(pool, gdomain);
}

void gxp_domain_pool_destroy(struct gcip_domain_pool *pool)
{
	gcip_domain_pool_exit(pool);
}
