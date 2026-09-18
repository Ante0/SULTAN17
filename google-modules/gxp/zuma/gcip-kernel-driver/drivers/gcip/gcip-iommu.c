// SPDX-License-Identifier: GPL-2.0-only
/*
 * Manages GCIP IOMMU domains and allocates/maps IOVAs.
 *
 * Copyright (C) 2023-2025 Google LLC
 */

#include <linux/align.h>
#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/gfp_types.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/limits.h>
#include <linux/log2.h>
#include <linux/minmax.h>
#include <linux/of.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <gcip/gcip-config.h>
#include <gcip/gcip-iommu.h>
#include <gcip/gcip-mem-pool.h>

/* Restricted IOVA ceiling is for components with 32-bit DMA windows */
#define GCIP_RESTRICT_IOVA_CEILING UINT_MAX

/**
 * dma_info_to_prot - Translate DMA API directions and attributes to IOMMU API
 *                    page flags.
 * @dir: Direction of DMA transfer
 * @coherent: If true, create coherent mappings of the scatterlist.
 * @attrs: DMA attributes for the mapping
 *
 * See v5.15.94/source/drivers/iommu/dma-iommu.c#L418
 *
 * Return: corresponding IOMMU API page protection flags
 */
static int dma_info_to_prot(enum dma_data_direction dir, bool coherent, unsigned long attrs)
{
	int prot = coherent ? IOMMU_CACHE : 0;

	if (attrs & DMA_ATTR_PRIVILEGED)
		prot |= IOMMU_PRIV;

	switch (dir) {
	case DMA_BIDIRECTIONAL:
		return prot | IOMMU_READ | IOMMU_WRITE;
	case DMA_TO_DEVICE:
		return prot | IOMMU_READ;
	case DMA_FROM_DEVICE:
		return prot | IOMMU_WRITE;
	default:
		return 0;
	}
}

/*
 * Allocates an IOVA for the scatterlist and maps it to @domain.
 *
 * @domain: GCIP IOMMU domain which manages IOVA addresses.
 * @sgl: Scatterlist to be mapped.
 * @nents: The number of entries in @sgl.
 * @iova: Target IOVA to map @sgl. If it is 0, this function allocates an IOVA space.
 * @gcip_map_flags: Flags indicating mapping attributes, which should be encoded with
 *                  gcip_iommu_encode_gcip_map_flags().
 *
 * Returns the number of entries which are mapped to @domain. Returns 0 if it fails.
 */
static unsigned int gcip_iommu_domain_map_sg(struct gcip_iommu_domain *domain,
					     struct scatterlist *sgl, int nents, dma_addr_t iova,
					     u64 gcip_map_flags)
{
	enum dma_data_direction dir = GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags);
	bool coherent = GCIP_MAP_FLAGS_GET_DMA_COHERENT(gcip_map_flags);
	unsigned long attrs = GCIP_MAP_FLAGS_GET_DMA_ATTR(gcip_map_flags);
	int i, prot = dma_info_to_prot(dir, coherent, attrs);
	struct scatterlist *sg;
	size_t iova_len = 0;
	ssize_t map_size;
	int ret;
	bool allocated = false;

	/* Calculates how much IOVA space we need. */
	for_each_sg(sgl, sg, nents, i)
		iova_len += sg->length;

	if (!iova) {
		/* Allocates one continuous IOVA. */
		iova = gcip_iommu_alloc_iova(domain, iova_len, gcip_map_flags);
		if (!iova)
			return 0;
		allocated = true;
	}

	/*
	 * Maps scatterlist to the allocated IOVA.
	 *
	 * It will iterate each scatter list segment in order and map them to the IOMMU domain
	 * as amount of the size of each segment successively.
	 * Returns an error on failure or the total length of mapped segments on success.
	 */
#if GCIP_IOMMU_MAP_HAS_GFP
	map_size = iommu_map_sg(domain->domain, iova, sgl, nents, prot, GFP_KERNEL);
#else
	map_size = iommu_map_sg(domain->domain, iova, sgl, nents, prot);
#endif
	if (map_size < 0 || map_size < iova_len)
		goto err_free_iova;

	/*
	 * Fills out the mapping information. Each entry can be max UINT_MAX bytes, floored
	 * to the pool granule size.
	 */
	ret = 0;
	sg = sgl;
	while (iova_len) {
		size_t segment_len =
			min_t(size_t, iova_len, UINT_MAX & ~(domain->space.granule - 1));

		sg_dma_address(sg) = iova;
		sg_dma_len(sg) = segment_len;
		iova += segment_len;
		iova_len -= segment_len;
		ret++;
		sg = sg_next(sg);
	}

	/* Return # of sg entries filled out above. */
	return ret;

err_free_iova:
	if (allocated)
		gcip_iommu_free_iova(domain, iova, iova_len);
	return 0;
}

/*
 * Unmaps an IOVA which was mapped for the scatterlist.
 *
 * @domain: GCIP IOMMU domain which manages IOVA addresses.
 * @sgl: Scatterlist to be unmapped.
 * @nents: The number of sg elements.
 * @free_iova: Set to true if the IOVA space was allocated internally while mapping @sgl by the
 *             `gcip_iommu_domain_map_sg` function. (i.e., @iova argument of the function was 0.)
 */
static void gcip_iommu_domain_unmap_sg(struct gcip_iommu_domain *domain, struct scatterlist *sgl,
				       int nents, bool free_iova)
{
	dma_addr_t iova = sg_dma_address(sgl);
	size_t iova_len = 0;
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		uint s_len = sg_dma_len(sg);

		if (!s_len)
			break;
		iova_len += s_len;
	}

	iommu_unmap(domain->domain, iova, iova_len);
	if (free_iova)
		gcip_iommu_free_iova(domain, iova, iova_len);
}

static inline unsigned long gcip_iommu_to_pfn(dma_addr_t iova, size_t granule)
{
	return iova >> __ffs(granule);
}

static inline dma_addr_t gcip_iommu_from_pfn(unsigned long pfn, size_t granule)
{
	return (dma_addr_t)pfn << __ffs(granule);
}

static int gcip_iommu_domain_space_allocator_init_iovad(struct gcip_iommu_domain_space *space,
							struct device *dev)
{
	int ret;

	init_iova_domain(&space->iovad, space->granule,
			 max_t(unsigned long, 1, space->base_daddr >> ilog2(space->granule)));

	if (space->reserved_size) {
		unsigned long pfn_lo =
			gcip_iommu_to_pfn(space->reserved_base_daddr, space->granule);
		unsigned long pfn_hi = gcip_iommu_to_pfn(
			space->reserved_base_daddr + space->reserved_size, space->granule);

		reserve_iova(&space->iovad, pfn_lo, pfn_hi);
	}

	ret = iova_domain_init_rcaches(&space->iovad);
	if (ret)
		put_iova_domain(&space->iovad);

	return ret;
}

static void gcip_iommu_domain_space_allocator_exit_iovad(struct gcip_iommu_domain_space *space)
{
	put_iova_domain(&space->iovad);
}

static dma_addr_t gcip_iommu_domain_space_alloc_iovad(struct gcip_iommu_domain_space *space,
						      size_t size, bool restrict_iova)
{
	dma_addr_t last_daddr = restrict_iova ? space->last_daddr_restricted : space->last_daddr;
	dma_addr_t last_daddr_pfn = gcip_iommu_to_pfn(last_daddr, space->granule);
	dma_addr_t size_pfn = gcip_iommu_to_pfn(size, space->granule);
	unsigned long iova_pfn;

	iova_pfn = alloc_iova_fast(&space->iovad, size_pfn, last_daddr_pfn, true);

	return gcip_iommu_from_pfn(iova_pfn, space->granule);
}

static void gcip_iommu_domain_space_free_iovad(struct gcip_iommu_domain_space *space,
					       dma_addr_t iova, size_t size)
{
	dma_addr_t size_pfn = gcip_iommu_to_pfn(size, space->granule);
	dma_addr_t iova_pfn = gcip_iommu_to_pfn(iova, space->granule);

	free_iova_fast(&space->iovad, iova_pfn, size_pfn);
}

static const struct gcip_iommu_domain_space_ops iovad_ops = {
	.allocator_init = gcip_iommu_domain_space_allocator_init_iovad,
	.allocator_exit = gcip_iommu_domain_space_allocator_exit_iovad,
	.alloc = gcip_iommu_domain_space_alloc_iovad,
	.free = gcip_iommu_domain_space_free_iovad,
};

static int gcip_iommu_domain_space_allocator_init_mempool(struct gcip_iommu_domain_space *space,
							  struct device *dev)
{
	size_t size = space->size;
	int ret;

	/*
	 * Use separate gen_pools for 32-bit vs. unrestricted IOVAs.  Must have a non-empty 32-bit
	 * space.
	 */
	if (space->base_daddr > UINT_MAX)
		return -EINVAL;
	if (space->base_daddr + size + 1 > UINT_MAX) {
		size = space->size - ((unsigned long long)UINT_MAX - space->base_daddr + 1);
		ret = gcip_mem_pool_init(&space->mem_pool.pool64, dev,
					 (unsigned long long)UINT_MAX + 1, size, space->granule);
		if (ret)
			return ret;

		space->mem_pool.pool64_valid = true;
		size = UINT_MAX - space->base_daddr + 1;
	}
	ret = gcip_mem_pool_init(&space->mem_pool.pool32, dev, space->base_daddr, size,
				 space->granule);
	if (ret) {
		if (space->mem_pool.pool64_valid)
			gcip_mem_pool_exit(&space->mem_pool.pool64);
		return ret;
	}

	if (space->space_type == GCIP_IOMMU_DOMAIN_TYPE_MEMPOOL_BEST_FIT) {
		gen_pool_set_algo(space->mem_pool.pool32.gen_pool, gen_pool_best_fit, NULL);
		if (space->mem_pool.pool64_valid)
			gen_pool_set_algo(space->mem_pool.pool64.gen_pool, gen_pool_best_fit, NULL);
	}

	if (space->reserved_size)
		dev_warn(dev, "gcip-reserved-map is not supported in mem_pool mode.");

	return 0;
}

static void gcip_iommu_domain_space_allocator_exit_mempool(struct gcip_iommu_domain_space *space)
{
	gcip_mem_pool_exit(&space->mem_pool.pool32);
	if (space->mem_pool.pool64_valid)
		gcip_mem_pool_exit(&space->mem_pool.pool64);
}

static dma_addr_t gcip_iommu_domain_space_alloc_mempool(struct gcip_iommu_domain_space *space,
							size_t size, bool restrict_iova)
{
	if (restrict_iova || !space->mem_pool.pool64_valid)
		return (dma_addr_t)gcip_mem_pool_alloc(&space->mem_pool.pool32, size);
	return (dma_addr_t)gcip_mem_pool_alloc(&space->mem_pool.pool64, size);
}

static void gcip_iommu_domain_space_free_mempool(struct gcip_iommu_domain_space *space,
						 dma_addr_t iova, size_t size)
{
	if (iova <= UINT_MAX)
		gcip_mem_pool_free(&space->mem_pool.pool32, iova, size);
	else
		gcip_mem_pool_free(&space->mem_pool.pool64, iova, size);
}

static const struct gcip_iommu_domain_space_ops mem_pool_ops = {
	.allocator_init = gcip_iommu_domain_space_allocator_init_mempool,
	.allocator_exit = gcip_iommu_domain_space_allocator_exit_mempool,
	.alloc = gcip_iommu_domain_space_alloc_mempool,
	.free = gcip_iommu_domain_space_free_mempool,
};

static int gcip_iommu_domain_space_init(struct gcip_iommu_domain_space *space, struct device *dev,
					enum gcip_iommu_domain_type space_type,
					dma_addr_t space_daddr, size_t space_size,
					dma_addr_t reserved_daddr, size_t reserved_size,
					size_t granule)
{
	if (!is_power_of_2(granule))
		return -EINVAL;

	space->space_type = space_type;
	space->base_daddr = space_daddr;
	space->last_daddr = space_daddr + space_size - 1;
	space->size = space_size;
	space->reserved_base_daddr = reserved_daddr;
	space->reserved_size = reserved_size;
	space->last_daddr_restricted =
		min_t(dma_addr_t, GCIP_RESTRICT_IOVA_CEILING, space->last_daddr);
	space->granule = granule;

	switch (space_type) {
	case GCIP_IOMMU_DOMAIN_TYPE_IOVAD:
		space->ops = &iovad_ops;
		break;
	case GCIP_IOMMU_DOMAIN_TYPE_MEMPOOL_BEST_FIT:
	case GCIP_IOMMU_DOMAIN_TYPE_MEMPOOL_FIRST_FIT:
		space->ops = &mem_pool_ops;
		break;
	case GCIP_IOMMU_DOMAIN_TYPE_UNMANAGED:
		space->ops = NULL;
		return 0;
	default:
		return -EINVAL;
	}

	return space->ops->allocator_init(space, dev);
}

static void gcip_iommu_domain_space_exit(struct gcip_iommu_domain_space *space)
{
	if (space->space_type == GCIP_IOMMU_DOMAIN_TYPE_UNMANAGED)
		return;

	space->ops->allocator_exit(space);
}

struct gcip_iommu_domain *gcip_iommu_domain_create(struct device *dev, struct iommu_domain *domain,
						   enum gcip_iommu_domain_type domain_type,
						   dma_addr_t space_daddr, size_t space_size,
						   dma_addr_t reserved_daddr, size_t reserved_size,
						   size_t granule)
{
	struct gcip_iommu_domain *gdomain;
	int ret;

	gdomain = devm_kzalloc(dev, sizeof(*gdomain), GFP_KERNEL);
	if (!gdomain)
		return ERR_PTR(-ENOMEM);

	gdomain->dev = dev;
	gdomain->domain = domain;
	gdomain->pasid = IOMMU_PASID_INVALID;


	ret = gcip_iommu_domain_space_init(&gdomain->space, dev, domain_type, space_daddr,
					   space_size, reserved_daddr, reserved_size, granule);
	if (ret)
		goto err_free_gdomain;

	return gdomain;

err_free_gdomain:
	devm_kfree(dev, gdomain);

	return ERR_PTR(ret);
}

void gcip_iommu_domain_destroy(struct gcip_iommu_domain *gdomain)
{
	gcip_iommu_domain_space_exit(&gdomain->space);
	devm_kfree(gdomain->dev, gdomain);
}

/**
 * get_window_config() - Retrieve base address and size from device tree.
 * @dev: The device struct to get the device tree.
 * @name: The name of the target window.
 * @n_addr: The required number of cells to read the value of @addr.
 * @n_size: The required number of cells to read the value of @size.
 * @addr: The pointer of the base address to output the value. Set to 0 on failure.
 * @size: The pointer of the size to output the value. Set to 0 on failure.
 *
 * Return: 0 on success, negative errno on failure.
 */
static int get_window_config(struct device *dev, char *name, int n_addr, int n_size,
			     dma_addr_t *addr, size_t *size)
{
	const __be32 *window;

	window = of_get_property(dev->of_node, name, NULL);
	if (!window) {
		*addr = *size = 0;
		return -ENODATA;
	}

	*addr = of_read_number(window, n_addr);
	*size = of_read_number(window + n_addr, n_size);

	return 0;
}

int gcip_iommu_get_space_config(struct device *dev, dma_addr_t *space_daddr, size_t *space_size,
				dma_addr_t *reserved_daddr, size_t *reserved_size)
{
	const __be32 *prop;
	u32 n_addr, n_size;
	int ret;

	prop = of_get_property(dev->of_node, "#dma-address-cells", NULL);
	n_addr = max_t(u32, 1, prop ? be32_to_cpup(prop) : of_n_addr_cells(dev->of_node));

	prop = of_get_property(dev->of_node, "#dma-size-cells", NULL);
	n_size = max_t(u32, 1, prop ? be32_to_cpup(prop) : of_n_size_cells(dev->of_node));

	ret = get_window_config(dev, "gcip-dma-window", n_addr, n_size, space_daddr, space_size);
	if (ret) {
		dev_err(dev, "Failed to find gcip-dma-window property");
		return ret;
	}

	ret = get_window_config(dev, "gcip-reserved-map", n_addr, n_size, reserved_daddr,
				reserved_size);
	if (ret) {
		dev_warn(dev, "Failed to find gcip-reserved-map property");
		*reserved_daddr = *reserved_size = 0;
	}

	return 0;
}

int gcip_iommu_get_space_size(struct device *dev, size_t  *space_size_ptr)
{
	dma_addr_t space_daddr, reserved_daddr;
	size_t space_size, reserved_size;
	int ret;

	ret = gcip_iommu_get_space_config(dev, &space_daddr, &space_size, &reserved_daddr,
					   &reserved_size);
	if (ret)
		return ret;

	*space_size_ptr = space_size;

	return 0;
}

static inline void sync_sg_if_needed(struct device *dev, struct sg_table *sgt, u64 gcip_map_flags,
				     bool for_device)
{
	enum dma_data_direction dir = GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags);

	if (GCIP_MAP_FLAGS_GET_DMA_ATTR(gcip_map_flags) & DMA_ATTR_SKIP_CPU_SYNC)
		return;

	if (for_device)
		dma_sync_sg_for_device(dev, sgt->sgl, sgt->orig_nents, dir);
	else
		dma_sync_sg_for_cpu(dev, sgt->sgl, sgt->orig_nents, dir);
}

/* Maps @sgt to @iova. If @iova is 0, this function allocates an IOVA space internally. */
unsigned int gcip_iommu_domain_map_sgt_to_iova(struct gcip_iommu_domain *domain,
					       struct sg_table *sgt, dma_addr_t iova,
					       u64 *gcip_map_flags)
{
	struct scatterlist *sgl = sgt->sgl;
	uint orig_nents = sgt->orig_nents;
	uint nents_mapped;

	nents_mapped = gcip_iommu_domain_map_sg(domain, sgl, orig_nents, iova, *gcip_map_flags);

	sgt->nents = nents_mapped;

	sync_sg_if_needed(domain->dev, sgt, *gcip_map_flags, true);

	return nents_mapped;
}

unsigned int gcip_iommu_domain_map_sgt(struct gcip_iommu_domain *domain, struct sg_table *sgt,
				       u64 *gcip_map_flags)
{
	return gcip_iommu_domain_map_sgt_to_iova(domain, sgt, 0, gcip_map_flags);
}

/*
 * Unmaps @sgt from @domain. If @free_iova is true, the IOVA region which was allocated by the
 * `gcip_iommu_domain_map_sgt_to_iova` function will be freed.
 */
static void gcip_iommu_domain_unmap_sgt_free_iova(struct gcip_iommu_domain *domain,
						  struct sg_table *sgt, bool free_iova,
						  u64 gcip_map_flags)
{
	sync_sg_if_needed(domain->dev, sgt, gcip_map_flags, false);
	gcip_iommu_domain_unmap_sg(domain, sgt->sgl, sgt->orig_nents, free_iova);
}

void gcip_iommu_domain_unmap_sgt(struct gcip_iommu_domain *domain, struct sg_table *sgt,
				 u64 gcip_map_flags)
{
	return gcip_iommu_domain_unmap_sgt_free_iova(domain, sgt, true, gcip_map_flags);
}

void gcip_iommu_domain_unmap_sgt_from_iova(struct gcip_iommu_domain *domain, struct sg_table *sgt,
					   u64 gcip_map_flags)
{
	gcip_iommu_domain_unmap_sgt_free_iova(domain, sgt, false, gcip_map_flags);
}

struct gcip_iommu_domain *gcip_iommu_get_domain_for_dev(struct device *dev)
{
	return gcip_iommu_get_domain_for_dev_from_pool(dev, GCIP_IOMMU_DOMAIN_TYPE_UNMANAGED,
						       PAGE_SIZE);
}

struct gcip_iommu_domain *
gcip_iommu_get_domain_for_dev_from_pool(struct device *dev, enum gcip_iommu_domain_type domain_type,
					size_t granule)
{
	struct gcip_iommu_domain *gdomain;
	struct iommu_domain *domain;
	dma_addr_t space_daddr, reserved_daddr;
	size_t space_size, reserved_size;
	int ret;

	ret = gcip_iommu_get_space_config(dev, &space_daddr, &space_size, &reserved_daddr,
					       &reserved_size);
	if (ret)
		return ERR_PTR(ret);

	domain = iommu_get_domain_for_dev(dev);
	if (!domain)
		return ERR_PTR(-ENODEV);

	gdomain = gcip_iommu_domain_create(dev, domain, domain_type, space_daddr, space_size,
					   reserved_daddr, reserved_size, granule);
	if (IS_ERR(gdomain))
		return gdomain;

	gdomain->default_domain = true;
	gdomain->pasid = 0;

	return gdomain;
}

u64 gcip_iommu_encode_gcip_map_flags(enum dma_data_direction dir, bool coherent,
				     unsigned long dma_attrs, bool restrict_iova, bool mmio)
{
	if (dir == DMA_FROM_DEVICE)
		dir = DMA_BIDIRECTIONAL;
	else if (dir == DMA_NONE)
		dir = DMA_TO_DEVICE;

	return (dir << GCIP_MAP_FLAGS_DMA_DIRECTION_OFFSET) |
	       (coherent << GCIP_MAP_FLAGS_DMA_COHERENT_OFFSET) |
	       (dma_attrs << GCIP_MAP_FLAGS_DMA_ATTR_OFFSET) |
	       (restrict_iova << GCIP_MAP_FLAGS_RESTRICT_IOVA_OFFSET) |
	       (mmio << GCIP_MAP_FLAGS_MMIO_OFFSET);
}

dma_addr_t gcip_iommu_alloc_iova(struct gcip_iommu_domain *domain, size_t size, u64 gcip_map_flags)
{
	struct gcip_iommu_domain_space *space = &domain->space;
	bool restrict_iova = GCIP_MAP_FLAGS_GET_RESTRICT_IOVA(gcip_map_flags);
	dma_addr_t iova;

	iova = space->ops->alloc(space, ALIGN(size, space->granule), restrict_iova);
	if (!iova)
		dev_err(domain->dev, "%siova alloc size %zu failed",
			restrict_iova ? "32-bit " : "", size);
	return iova;
}

void gcip_iommu_free_iova(struct gcip_iommu_domain *domain, dma_addr_t iova, size_t size)
{
	struct gcip_iommu_domain_space *space = &domain->space;

	space->ops->free(space, iova, ALIGN(size, space->granule));
}

int gcip_iommu_map(struct gcip_iommu_domain *domain, dma_addr_t iova, phys_addr_t paddr,
		   size_t size, u64 gcip_map_flags)
{
	enum dma_data_direction dir = GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags);
	bool coherent = GCIP_MAP_FLAGS_GET_DMA_COHERENT(gcip_map_flags);
	bool mmio = GCIP_MAP_FLAGS_GET_MMIO(gcip_map_flags);
	unsigned long attrs = GCIP_MAP_FLAGS_GET_DMA_ATTR(gcip_map_flags);
	int prot = dma_info_to_prot(dir, coherent, attrs);

	if (mmio)
		prot |= IOMMU_MMIO;

#if GCIP_IOMMU_MAP_HAS_GFP
	return iommu_map(domain->domain, iova, paddr, size, prot, GFP_KERNEL);
#else
	return iommu_map(domain->domain, iova, paddr, size, prot);
#endif /* GCIP_IOMMU_MAP_HAS_GFP */
}

/* Reverts gcip_iommu_map(). */
void gcip_iommu_unmap(struct gcip_iommu_domain *domain, dma_addr_t iova, size_t size)
{
	size_t unmapped = iommu_unmap(domain->domain, iova, size);

	if (unlikely(unmapped != size))
		dev_warn(domain->dev, "Unmapping IOVA %pad, size (%#zx) only unmapped %#zx", &iova,
			 size, unmapped);
}
