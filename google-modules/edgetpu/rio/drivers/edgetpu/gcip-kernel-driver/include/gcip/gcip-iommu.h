/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Manages GCIP IOMMU domains and allocates/maps IOVAs.
 *
 * One can replace allocating IOVAs via Linux DMA interface which will allocate and map them to
 * the default IOMMU domain with this framework. This framework will allocate and map IOVAs to the
 * specific IOMMU domain directly. This has following two advantages:
 *
 * - Can remove the mapping time by once as it maps to the target IOMMU domain directly.
 * - IOMMU domains don't have to share the total capacity.
 *
 * GCIP IOMMU domain is implemented by utilizing multiple kinds of IOVA space pool:
 * - struct iova_domain
 * - struct gcip_mem_pool
 *
 * Copyright (C) 2023 Google LLC
 */

#ifndef __GCIP_IOMMU_H__
#define __GCIP_IOMMU_H__

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/idr.h>
#include <linux/iommu.h>
#include <linux/iova.h>
#include <linux/scatterlist.h>
#include <linux/types.h>

#include <gcip/gcip-config.h>
#include <gcip/gcip-mem-pool.h>

/* Helpers to get/set @gcip_map_flags of the `gcip_iommu_domain_{map,unmap}_sg` functions. */

/* Bitfield sizes of gcip_map_flags. */
#define GCIP_MAP_FLAGS_DMA_DIRECTION_BIT_SIZE 2
#define GCIP_MAP_FLAGS_DMA_COHERENT_BIT_SIZE 1
#define GCIP_MAP_FLAGS_RESTRICT_IOVA_BIT_SIZE 1
#define GCIP_MAP_FLAGS_DMA_ATTR_BIT_SIZE 10
#define GCIP_MAP_FLAGS_MMIO_BIT_SIZE 1

/* Offsets of gcip_map_flags. */
#define GCIP_MAP_FLAGS_DMA_DIRECTION_OFFSET 0
#define GCIP_MAP_FLAGS_DMA_COHERENT_OFFSET \
	(GCIP_MAP_FLAGS_DMA_DIRECTION_OFFSET + GCIP_MAP_FLAGS_DMA_DIRECTION_BIT_SIZE)
#define GCIP_MAP_FLAGS_DMA_ATTR_OFFSET \
	(GCIP_MAP_FLAGS_DMA_COHERENT_OFFSET + GCIP_MAP_FLAGS_DMA_COHERENT_BIT_SIZE)
#define GCIP_MAP_FLAGS_RESTRICT_IOVA_OFFSET \
	(GCIP_MAP_FLAGS_DMA_ATTR_OFFSET + GCIP_MAP_FLAGS_DMA_ATTR_BIT_SIZE)
#define GCIP_MAP_FLAGS_MMIO_OFFSET \
	(GCIP_MAP_FLAGS_RESTRICT_IOVA_OFFSET + GCIP_MAP_FLAGS_RESTRICT_IOVA_BIT_SIZE)

/* Masks of gcip_map_flags. */
#define GCIP_MAP_MASK(ATTR) \
	((BIT_ULL(GCIP_MAP_FLAGS_##ATTR##_BIT_SIZE) - 1) << (GCIP_MAP_FLAGS_##ATTR##_OFFSET))
#define GCIP_MAP_MASK_DMA_DIRECTION GCIP_MAP_MASK(DMA_DIRECTION)
#define GCIP_MAP_MASK_DMA_COHERENT GCIP_MAP_MASK(DMA_COHERENT)
#define GCIP_MAP_MASK_DMA_ATTR GCIP_MAP_MASK(DMA_ATTR)
#define GCIP_MAP_MASK_RESTRICT_IOVA GCIP_MAP_MASK(RESTRICT_IOVA)
#define GCIP_MAP_MASK_MMIO GCIP_MAP_MASK(MMIO)

/* Get functions of gcip_map_flags. */
#define GCIP_MAP_FLAGS_GET_VALUE(ATTR, flags) \
	(((flags) & GCIP_MAP_MASK(ATTR)) >> (GCIP_MAP_FLAGS_##ATTR##_OFFSET))
#define GCIP_MAP_FLAGS_GET_DMA_DIRECTION(flags) GCIP_MAP_FLAGS_GET_VALUE(DMA_DIRECTION, flags)
#define GCIP_MAP_FLAGS_GET_DMA_COHERENT(flags) GCIP_MAP_FLAGS_GET_VALUE(DMA_COHERENT, flags)
#define GCIP_MAP_FLAGS_GET_DMA_ATTR(flags) GCIP_MAP_FLAGS_GET_VALUE(DMA_ATTR, flags)
#define GCIP_MAP_FLAGS_GET_RESTRICT_IOVA(flags) GCIP_MAP_FLAGS_GET_VALUE(RESTRICT_IOVA, flags)
#define GCIP_MAP_FLAGS_GET_MMIO(flags) GCIP_MAP_FLAGS_GET_VALUE(MMIO, flags)

/*
 * Bitfields of @gcip_map_flags:
 *   [1:0]   - DMA_DIRECTION:
 *               00 = DMA_BIDIRECTIONAL (host/device can write buffer)
 *               01 = DMA_TO_DEVICE     (host can write buffer)
 *               10 = DMA_FROM_DEVICE   (device can write buffer)
 *               (See [REDACTED]
 *   [2:2]   - Coherent Mapping:
 *               0 = Create non-coherent mappings of the buffer.
 *               1 = Create coherent mappings of the buffer.
 *   [12:3]  - DMA_ATTR:
 *               (See [REDACTED]
 *   [13:13] - RESTRICT_IOVA:
 *               Restrict the IOVA assignment to 32 bit address window.
 *   [14:14] - MMIO:
 *               Mapping is for device memory, use IOMMU_MMIO flag.
 *   [63:15] - RESERVED
 *               Set RESERVED bits to 0 to ensure backwards compatibility.
 *
 * One should use gcip_iommu_encode_gcip_map_flags to generate the gcip_map_flags.
 */

/**
 * enum gcip_map_debug_flags - Mapping status flags for debugging, noting various attributes of the
 *                             mapping used for diagnosis of access problems.
 * GCIP_MAP_DEBUG_COW: VMA is copy-on-write, writeable mappings may have made a copy of pages
 * GCIP_MAP_DEBUG_OVRRD_RDDIR: map direction override to read-only, writable page pin failed
 * GCIP_MAP_DEBUG_VMA_NF: VMA for host addr not found, so initially assumed writeable by default
 * GCIP_MAP_DEBUG_ASSUME_RDONLY: writable page pin failed, assuming read-only
 */
enum gcip_map_debug_flags {
	GCIP_MAP_DEBUG_COW = 0x1,
	GCIP_MAP_DEBUG_OVRRD_RDDIR = 0x2,
	GCIP_MAP_DEBUG_VMA_NF = 0x4,
	GCIP_MAP_DEBUG_ASSUME_RDONLY = 0x8,
};

/**
 * enum gcip_iommu_domain_type - Type of IOVA space pool that IOMMU domain will utilize.
 * @GCIP_IOMMU_DOMAIN_TYPE_IOVAD: Uses iova_domain (red-black tree based).
 * @GCIP_IOMMU_DOMAIN_TYPE_MEMPOOL_BEST_FIT: Uses gcip_mem_pool with best-fit algorithm.
 * @GCIP_IOMMU_DOMAIN_TYPE_MEMPOOL_FIRST_FIT: Uses gcip_mem_pool with first-fit algorithm.
 * @GCIP_IOMMU_DOMAIN_TYPE_UNMANAGED: Disabled the IOVA space management.
 *
 * @GCIP_IOMMU_DOMAIN_TYPE_UNMANAGED is typically used for the default domain which we don't need
 * to manage the IOVA space.
 */
enum gcip_iommu_domain_type {
	GCIP_IOMMU_DOMAIN_TYPE_IOVAD,
	GCIP_IOMMU_DOMAIN_TYPE_MEMPOOL_BEST_FIT,
	GCIP_IOMMU_DOMAIN_TYPE_MEMPOOL_FIRST_FIT,
	GCIP_IOMMU_DOMAIN_TYPE_UNMANAGED,
};

/* For GCIP_IOMMU_DOMAIN_TYPE_MEM_POOL, gen_pools for 32-bit and > 32-bit spaces. */
struct gcip_iommu_domain_iova_mem_pools {
	struct gcip_mem_pool pool32;
	struct gcip_mem_pool pool64;
	/* If true then pool64 is valid, else this is a 32-bit-only pool. */
	bool pool64_valid;
};

struct gcip_iommu_domain_space_ops;

/**
 * struct gcip_iommu_domain_space - IOVA space management a GCIP IOMMU domain.
 * @space_type: Type of the IOVA space pool.
 * @base_daddr: Base address of the IOVA space.
 * @size: Size of the IOVA space.
 * @reserved_base_daddr: Base address of the reserved IOVA space.
 * @reserved_size: Size of the reserved IOVA space.
 * @last_daddr: The last address of the IOVA space, equal to (base_daddr + size - 1).
 * @last_daddr_restricted: The last address of the IOVA space with 32-bit restriction.
 * @granule: Alignment of the IOVA space.
 * @iovad: IOVA space managed by iova_domain.
 * @mem_pool: IOVA space managed by gcip_mem_pool.
 * @ops: Operations for the IOVA space management.
 */
struct gcip_iommu_domain_space {
	enum gcip_iommu_domain_type space_type;
	dma_addr_t base_daddr;
	size_t size;
	dma_addr_t reserved_base_daddr;
	size_t reserved_size;
	dma_addr_t last_daddr;
	dma_addr_t last_daddr_restricted;
	size_t granule;
	union {
		struct iova_domain iovad;
		struct gcip_iommu_domain_iova_mem_pools mem_pool;
	};
	const struct gcip_iommu_domain_space_ops *ops;
};

/*
 * Wrapper of iommu_domain.
 * It has its own IOVA space pool based on iova_domain or gcip_mem_pool. One can choose one of them
 * when calling the `gcip_iommu_domain_create` function. See `enum gcip_iommu_domain_type`
 * for details.
 */
struct gcip_iommu_domain {
	struct device *dev;
	struct iommu_domain *domain;
	bool default_domain;
	struct gcip_iommu_domain_space space;
	ioasid_t pasid; /* Only valid if attached */
};

/*
 * Holds operators which will be set according to the @domain_type.
 * These callbacks will be filled automatically when a `struct gcip_iommu_domain` is allocated.
 */
struct gcip_iommu_domain_space_ops {
	/* Initializes the IOVA allocator. */
	int (*allocator_init)(struct gcip_iommu_domain_space *space, struct device *dev);
	/* Reverts the allocator_init(). */
	void (*allocator_exit)(struct gcip_iommu_domain_space *space);
	/* Allocates @size of IOVA space, optionally restricted to 32 bits, returns start IOVA. */
	dma_addr_t (*alloc)(struct gcip_iommu_domain_space *space, size_t size, bool restrict_iova);
	/* Releases @size of buffer which was allocated to @iova. */
	void (*free)(struct gcip_iommu_domain_space *space, dma_addr_t iova, size_t size);
};

/**
 * gcip_iommu_domain_map_sgt(): Maps the scatter-gather table to the target IOMMU domain.
 * @domain: The domain that the sgt will be mapped to.
 * @sgt: The scatter-gather table to be mapped.
 * @gcip_map_flags: The gcip flags used to map the @sgt.
 *
 * This function will allocate an IOVA space and map the scatter-gather table to the address of the
 * allocated space in the target IOMMU domain. @sgt->nents will be updated to the number of mapped
 * chunks. Also, @sgt will be synced for the device.
 *
 * Return: The number of the entries that are mapped successfully.
 */
unsigned int gcip_iommu_domain_map_sgt(struct gcip_iommu_domain *domain, struct sg_table *sgt,
				       u64 *gcip_map_flags);

/**
 * gcip_iommu_domain_unmap_sgt() - Unmaps the scatter-gather table from the given domain.
 * @domain: The domain that the sgt will be unmapped from.
 * @sgt: The scatter-gather table to be unmapped.
 * @gcip_map_flags: The gcip flags used to unmap the @sgt.
 *
 * The scatter-gather table will be unmapped from @domain and synced for cpu. Also, the IOVA space
 * which was allocated from the `gcip_iommu_domain_map_sgt` function will be released.
 */
void gcip_iommu_domain_unmap_sgt(struct gcip_iommu_domain *domain, struct sg_table *sgt,
				 u64 gcip_map_flags);

/**
 * gcip_iommu_domain_map_sgt_to_iova(): Maps the scatter-gather table with specified IOVA to the
 *                                      target domain.
 *
 * @domain: The domain that the sgt will be mapped to.
 * @sgt: The scatter-gather table to be mapped.
 * @iova: The specified device address.
 * @gcip_map_flags: The gcip flags used to map the @sgt.
 *
 * This function is almost identical to gcip_iommu_domain_map_sgt() except this function maps with
 * the specified device address instead of allocating one internally.
 *
 * Note the used device address is NOT reserved by the domain, it's caller's responsibility to
 * ensure @iova does not overlap with the domain's IOVA space.
 *
 * Return: The number of the entries that are mapped successfully.
 */
unsigned int gcip_iommu_domain_map_sgt_to_iova(struct gcip_iommu_domain *domain,
					       struct sg_table *sgt, dma_addr_t iova,
					       u64 *gcip_map_flags);
/**
 * gcip_iommu_domain_unmap_sgt_from_iova(): Reverts gcip_iommu_domain_map_sgt_to_iova().
 * @domain: The domain that the sgt will be unmapped from.
 * @sgt: The scatter-gather table to be unmapped.
 * @gcip_map_flags: The gcip flags used to unmap @sgt.
 *
 * There is no @iova parameter because it is recorded in @sgt as done by
 * gcip_iommu_domain_map_sgt_to_iova().
 */
void gcip_iommu_domain_unmap_sgt_from_iova(struct gcip_iommu_domain *domain, struct sg_table *sgt,
					   u64 gcip_map_flags);

/**
 * gcip_iommu_domain_create() - Creates a GCIP IOMMU domain.
 * @dev: Device to create the domain for.
 * @domain: The IOMMU domain to use.
 * @domain_type: Type of the IOVA space of the IOMMU domain.
 * @space_daddr: Base address of the IOVA space.
 * @space_size: Size of the IOVA space.
 * @reserved_daddr: Base address of the reserved IOVA space.
 * @reserved_size: Size of the reserved IOVA space.
 * @granule: Alignment of the IOVA space (Should be power of 2).
 *
 * If @domain_type is GCIP_IOMMU_DOMAIN_TYPE_UNMANAGED, it'll be created with unmanaged IOVA space.
 *
 * Return: The pointer to the created gcip_iommu_domain on success, or a negative errno otherwise.
 */
struct gcip_iommu_domain *gcip_iommu_domain_create(struct device *dev, struct iommu_domain *domain,
						   enum gcip_iommu_domain_type domain_type,
						   dma_addr_t space_daddr, size_t space_size,
						   dma_addr_t reserved_daddr, size_t reserved_size,
						   size_t granule);

/**
 * gcip_iommu_domain_destroy() - Reverts gcip_iommu_domain_create().
 * @gdomain: The GCIP IOMMU domain to be destroyed.
 */
void gcip_iommu_domain_destroy(struct gcip_iommu_domain *gdomain);

/**
 * gcip_iommu_get_domain_for_dev() - Gets a default GCIP domain.
 * @dev: The device to fetch the default IOMMU domain.
 *
 * Return: The pointer to the domain on success, or the pointer to a negative errno otherwise.
 */
struct gcip_iommu_domain *gcip_iommu_get_domain_for_dev(struct device *dev);

/**
 * gcip_iommu_get_domain_for_dev_from_pool() - Gets a default GCIP domain with IOVA management.
 * @dev: The device to fetch the default IOMMU domain.
 * @domain_type: Type of the IOVA space of the IOMMU domain.
 * @granule: Alignment of the IOVA space (Should be power of 2).
 *
 * If the IOVA space management is not needed, pass NULL to @pool to disable it.
 * With IOVA space management enabled, the domain supports to be passed to map/unmap interfaces.
 *
 * Return: The pointer to the domain on success, or the pointer to a negative errno otherwise.
 */
struct gcip_iommu_domain *
gcip_iommu_get_domain_for_dev_from_pool(struct device *dev, enum gcip_iommu_domain_type domain_type,
					size_t granule);

/**
 * gcip_iommu_encode_gcip_map_flags() - Encodes the gcip_map_flags from given arguments.
 * @dir: The DMA_DIRECTION used for mapping.
 * @coherent: Whether it is a coherent buffer or not.
 * @dma_attrs: The DMA attributes used for mapping.
 * @restrict_iova: Whether to restrict the IOVA assignment to 32 bit address window.
 * @mmio: Whether to use IOMMU_MMIO flag.
 *
 * If the direction is DMA_FROM_DEVICE(WO), it will be adjusted to DMA_BIDIRECTIONAL(RW).
 * If the direction is DMA_NONE, it will be adjusted to DMA_TO_DEVICE(RO).
 *
 * Return: The encoded gcip_map_flags.
 */
u64 gcip_iommu_encode_gcip_map_flags(enum dma_data_direction dir, bool coherent,
				     unsigned long dma_attrs, bool restrict_iova, bool mmio);

/**
 * gcip_iommu_map_flags_dma_rw() - Encodes gcip_map_flags with DMA_BIDIRECTIONAL and default values.
 *
 * Return: The encoded value.
 */
static inline u64 gcip_iommu_map_flags_dma_rw(void)
{
	return gcip_iommu_encode_gcip_map_flags(DMA_BIDIRECTIONAL, false, 0, false, false);
}

/**
 * gcip_iommu_map_flags_dma_rw() - Encodes gcip_map_flags with DMA_TO_DEVICE and default values.
 *
 * Return: The encoded value.
 */
static inline u64 gcip_iommu_map_flags_dma_ro(void)
{
	return gcip_iommu_encode_gcip_map_flags(DMA_TO_DEVICE, false, 0, false, false);
}

/**
 * gcip_iommu_alloc_iova() - Allocates IOVA with size @size.
 * @domain: The GCIP domain to allocate IOVA.
 * @size: Size in bytes.
 * @gcip_map_flags: The flags used to create the mapping, which should be encoded with
 *                  gcip_iommu_encode_gcip_map_flags().
 *
 * Return: The allocated IOVA. Returns 0 on failure.
 */
dma_addr_t gcip_iommu_alloc_iova(struct gcip_iommu_domain *domain, size_t size, u64 gcip_map_flags);

/**
 * gcip_iommu_free_iova() - Frees IOVA allocated by gcip_iommu_alloc_iova().
 * @domain: The GCIP domain @iova allocated from.
 * @iova: The IOVA returned by gcip_iommu_alloc_iova().
 * @size: Size in bytes.
 */
void gcip_iommu_free_iova(struct gcip_iommu_domain *domain, dma_addr_t iova, size_t size);

/**
 * gcip_iommu_map() - Maps the desired mappings to the domain.
 * @domain: The GCIP domain to be mapped to.
 * @iova: The device address.
 * @paddr: The target address to be mapped to.
 * @size: Map size in bytes.
 * @gcip_map_flags: The flags used to create the mapping, which should be encoded with
 *                  gcip_iommu_encode_gcip_map_flags().
 *
 * Return: 0 on success, otherwise a negative errno.
 */
int gcip_iommu_map(struct gcip_iommu_domain *domain, dma_addr_t iova, phys_addr_t paddr,
		   size_t size, u64 gcip_map_flags);
/* Reverts gcip_iommu_map(). */
void gcip_iommu_unmap(struct gcip_iommu_domain *domain, dma_addr_t iova, size_t size);

/**
 * gcip_iommu_get_space_config() - Gets the IOVA space configuration from the device tree.
 * @dev: The device to get the IOVA space configuration from.
 * @space_daddr: The base address of the IOVA space.
 * @space_size: The size of the IOVA space.
 * @reserved_daddr: The base address of the reserved IOVA space.
 * @reserved_size: The size of the reserved IOVA space.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_iommu_get_space_config(struct device *dev, dma_addr_t *space_daddr, size_t *space_size,
				dma_addr_t *reserved_daddr, size_t *reserved_size);

/**
 * gcip_iommu_get_space_size() - Gets the IOVA space size from the device tree.
 * @dev: The device to get the IOVA space size from.
 * @space_size_ptr: The pointer to the size of the IOVA space.
 *
 * The returned value is the total window size fetched from <gcip-dma-window>
 * The range may include IOVAs which may be reserved.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_iommu_get_space_size(struct device *dev, size_t  *space_size_ptr);

#endif /* __GCIP_IOMMU_H__ */
