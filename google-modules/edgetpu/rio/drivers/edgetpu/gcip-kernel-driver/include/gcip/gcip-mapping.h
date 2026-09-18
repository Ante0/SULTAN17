/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Declarations of GCIP mapping structs and interfaces.
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __GCIP_MAPPING_H__
#define __GCIP_MAPPING_H__

#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/types.h>

#include <gcip/gcip-iommu.h>

/**
 * enum gcip_mapping_type - Indicates the type of the gcip_mapping.
 * GCIP_MAPPING_TYPE_BUFFER: The mapping of a normal buffer that mapped to the domain directly.
 * GCIP_MAPPING_TYPE_DMABUF: The mapping of a DMA buffer that mapped to domain with 2 steps.
 */
enum gcip_mapping_type {
	GCIP_MAPPING_TYPE_BUFFER,
	GCIP_MAPPING_TYPE_DMABUF,
};

/* Operaters for `struct gcip_mapping`. */
struct gcip_mapping_ops {
	/*
	 * Called after the corresponding mapping of @data is unmapped and released. Since its
	 * `struct gcip_mapping` instance is released, it won't be passed to the callback.
	 *
	 * This callback is optional.
	 */
	void (*after_unmap)(void *data);
};

/**
 * struct gcip_mapping - Contains the information of sgt mapping to the domain.
 * @type: Type of the mapping.
 * @domain: IOMMU domain where the @sgt is mapped.
 * @device_address: Assigned device address.
 * @alloced_iova: Allocated IOVA.
 * @size: Size of mapped buffer.
 * @sgt: This pointer will be set to a new allocated Scatter-gather table which contains the mapping
 *       information to the given domain received from the custom IOVA allocator.
 *       If the given domain is the default domain, the pointer will be set to the sgt received from
 *       default allocator.
 *       If NULL then the mapping has no pages resident due to being trimmed.
 * @dir: The dma data direction may be adjusted due to the system or hardware limit.
 *       This value is the real one that was used for mapping and should be the same as the one
 *       encoded in gcip_map_flags.
 *       This field should be used in revert functions and dma sync functions.
 * @gcip_map_flags: The flags used to create the mapping, which can be encoded with
 *                  gcip_iommu_encode_gcip_map_flags() or `GCIP_MAP_FLAGS_DMA_*_TO_FLAGS` macros.
 * @map_debug_flags: debug flags for reporting and diagnosis purposes.
 * @user_specified_daddr: If true, its IOVA address was specified by the user from the `*_to_iova`
 *                        mapping functions and it won't free that when it's going to be unmapped.
 *                        It's user's responsibility to manage the IOVA region.
 * @ops: User defined operators.
 * @data: User defined data.
 */
struct gcip_mapping {
	enum gcip_mapping_type type;
	struct gcip_iommu_domain *domain;
	dma_addr_t device_address;
	dma_addr_t alloced_iova;
	size_t size;
	struct sg_table *sgt;
	enum dma_data_direction dir;
	u64 gcip_map_flags;
	enum gcip_map_debug_flags map_debug_flags;
	bool user_specified_daddr;
	const struct gcip_mapping_ops *ops;
	void *data;
};

/**
 * gcip_mapping_set_ops() - Sets the operators for the mapping.
 * @mapping: The pointer of the mapping instance.
 * @ops: The operators to set.
 */
static inline void gcip_mapping_set_ops(struct gcip_mapping *mapping,
					const struct gcip_mapping_ops *ops)
{
	mapping->ops = ops;
}

/**
 * gcip_mapping_set_data() - Sets the user data for the mapping.
 * @mapping: The pointer of the mapping instance.
 * @data: The user data to set.
 */
static inline void gcip_mapping_set_data(struct gcip_mapping *mapping, void *data)
{
	mapping->data = data;
}

/**
 * gcip_mapping_buffer_map_to_iova() - Maps the buffer to @domain at @iova.
 * @domain: The desired IOMMU domain where the buffer should be mapped.
 * @host_address: The starting address of the buffer.
 * @size: The size of the buffer.
 * @iova: The IOVA to map to.
 * @gcip_map_flags: The flags used to create the mapping, which can be encoded with
 *                  gcip_iommu_encode_gcip_map_flags() or `GCIP_MAP_FLAGS_DMA_*_TO_FLAGS` macros.
 * @pin_user_pages_lock: The lock for pinning user pages, or NULL if none.
 *
 * Following things are done in this function:
 * 1. Pin user pages.
 * 2. Allocate corresponding sg_table.
 * 3. Map the sg_table to the target domain.
 * 4. Create the desired mapping.
 *
 * The returned mapping should be unmapped by gcip_iommu_mapping_unmap().
 *
 * Note that the passed @iova won't be freed if it was non-zero when the returned mapping is going
 * to be unmapped. The life cycle of the given @iova must be managed by the user.
 *
 * Return: The mapping of the desired buffer with type GCIP_MAPPING_TYPE_BUFFER or an error pointer
 *         on failure.
 */
struct gcip_mapping *gcip_mapping_buffer_map_to_iova(struct gcip_iommu_domain *domain,
						     u64 host_address, size_t size, dma_addr_t iova,
						     u64 gcip_map_flags,
						     struct mutex *pin_user_pages_lock);

/**
 * gcip_mapping_map_buffer() - Maps the buffer to the target IOMMU domain.
 * @domain: The desired IOMMU domain where the buffer should be mapped.
 * @host_address: The starting address of the buffer.
 * @size: The size of the buffer.
 * @gcip_map_flags: The flags used to create the mapping, which can be encoded with
 *                  gcip_iommu_encode_gcip_map_flags() or `GCIP_MAP_FLAGS_DMA_*_TO_FLAGS` macros.
 * @pin_user_pages_lock: The lock for pinning user pages, or NULL if none.
 *
 * The returned mapping should be unmapped by gcip_iommu_mapping_unmap().
 *
 * This function basically works the same as the `gcip_mapping_map_buffer_to_iova` function but the
 * IOVA will be allocated internally.
 *
 * Return: The mapping of the desired buffer with type GCIP_MAPPING_TYPE_BUFFER or an error pointer
 *         on failure.
 */
struct gcip_mapping *gcip_mapping_buffer_map(struct gcip_iommu_domain *domain, u64 host_address,
					     size_t size, u64 gcip_map_flags,
					     struct mutex *pin_user_pages_lock);

/**
 * gcip_mapping_buffer_sync() - Sync a mapped buffer for either CPU or device.
 * @mapping: The pointer of the mapping instance to be synced.
 * @dev: The device that the mapping belongs to.
 * @offset: The offset, in bytes, into the mapped buffer where the region to be synced begins.
 * @size: The size, in bytes, of the region to be synced.
 * @for_cpu: True to sync for CPU access, false to sync for device access.
 *
 * This function only supports mappings with type GCIP_MAPPING_BUFFER.
 *
 * Return: 0 on success, or a negative errno otherwise.
 */
int gcip_mapping_buffer_sync(struct gcip_mapping *mapping, struct device *dev, u64 offset, u64 size,
			     bool for_cpu);

/**
 * gcip_mapping_buffer_trim() - Trims a buffer mapping, unpinning pages and unmapping from device,
 *                              but leaves the IOVA allocation and mapping metadata in place.
 * @mapping: The mapping instance to be trimmed.
 *
 * @mapping->sgt is set to NULL, indicating no pages currently mapped to TPU (or pinned).
 * The full mapping may be restored via gcip_mapping_remap().
 *
 * Only implemented for buffer, not dma-buf, mappings.
 */
void gcip_mapping_buffer_trim(struct gcip_mapping *mapping);

/**
 * gcip_mapping_buffer_remap() - Remaps a previously trimmed buffer mapping, re-pinning pages and
 *                               remapping to the device at the same IOVA as previous.
 * @mapping: The mapping instance to be remapped.
 * @pin_user_pages_lock: The lock for pinning user pages, or NULL if none.
 *
 * @mapping->sgt is set to the new scatter-gather list.
 *
 * Only implemented for buffer, not dma-buf, mappings.
 */
int gcip_mapping_buffer_remap(struct gcip_mapping *mapping, struct mutex *pin_user_pages_lock);

/**
 * gcip_mapping_dmabuf_map_to_iova() - Maps the DMA buffer to @domain at @iova.
 * @domain: The desired IOMMU domain where the DMA buffer should be mapped.
 * @dmabuf: The dma_buf to map to @domain.
 * @iova: The IOVA to map to.
 * @gcip_map_flags: The flags used to create the mapping, which should be encoded with
 *                  gcip_iommu_encode_gcip_map_flags().
 *
 * The DMA buffer will be mapped to the default domain first to get a scatter-gather table.
 * The received sgt will be copied to a new sgt and the new one will be mapped to the target domain.
 * The IOVAs of those domains may be different and the mappings will be released at once by calling
 * `gcip_mapping_unmap`.
 *
 * Note that the passed @iova won't be freed if it was non-zero when the returned mapping is going
 * to be unmapped. The life cycle of the given @iova must be managed by the user.
 *
 * Return: The mapping of the desired DMA buffer with type GCIP_MAPPING_TYPE_DMABUF or an error
 *         pointer on failure.
 */
struct gcip_mapping *gcip_mapping_dmabuf_map_to_iova(struct gcip_iommu_domain *domain,
						     struct dma_buf *dmabuf, dma_addr_t iova,
						     u64 gcip_map_flags);

/**
 * gcip_mapping_dmabuf_map() - Maps the DMA buffer to the target IOMMU domain.
 * @domain: The desired IOMMU domain where the DMA buffer should be mapped.
 * @dmabuf: The dma_buf to map to @domain.
 * @gcip_map_flags: The flags used to create the mapping, which can be encoded with
 *                  gcip_iommu_encode_gcip_map_flags() or `GCIP_MAP_FLAGS_DMA_*_TO_FLAGS` macros.
 *
 * This function basically works the same as the `gcip_mapping_map_dmabuf_to_iova` function but the
 * IOVA will be allocated internally.
 *
 * Return: The mapping of the desired DMA buffer with type GCIP_MAPPING_TYPE_DMABUF or an error
 *         pointer on failure.
 */
struct gcip_mapping *gcip_mapping_dmabuf_map(struct gcip_iommu_domain *domain,
					     struct dma_buf *dmabuf, u64 gcip_map_flags);

/**
 * gcip_mapping_unmap() - Unmaps the mapping depends on its type.
 * @mapping: The pointer of the mapping instance to be unmapped.
 *
 * Reverting either gcip_mapping_map_dmabuf() or gcip_mapping_map_buffer().
 *
 * The @mapping->gcip_map_flags will be used for unmapping the buffer, it can be modified if
 * necessary such as adding DMA_ATTR_SKIP_CPU_SYNC flag.
 * In most scenarios the we should use the same flag which we used while mapping especially for
 * direction, coherent, and iova_restrict.
 */
void gcip_mapping_unmap(struct gcip_mapping *mapping);

/**
 * gcip_mapping_dmabuf_show() - Writes the dma-buf mapping information to the seq_file.
 * @mapping: The container of the mapping info.
 * @s: The seq_file that the mapping info should be written to.
 *
 * Following information will be written to the seq_file:
 * 1. Device addresses of the related domains.
 * 2. Number of pages.
 * 3. DMA data direction.
 * 4. The name of the dmabuf.
 */
void gcip_mapping_dmabuf_show(struct gcip_mapping *mapping, struct seq_file *s);

/**
 * gcip_mapping_dmabuf_hiorder_size() - Returns the number of bytes mapped by high-order (>=2MB)
 *                                      scatter-gather list segments for a dma-buf mapping.
 * @mapping: The container of the mapping info.
 */
size_t gcip_mapping_dmabuf_hiorder_size(struct gcip_mapping *mapping);

/**
 * gcip_iommu_mapping_unmap() - Unmaps the mapping depends on its type.
 * @mapping: The pointer of the mapping instance to be unmapped.
 *
 * Reverting either gcip_iommu_domain_map_dma_buf() or gcip_iommu_domain_map_buffer().
 *
 * The @mapping->gcip_map_flags will be used for unmapping the buffer, it can be modified if
 * necessary such as adding DMA_ATTR_SKIP_CPU_SYNC flag.
 * In most scenarios the we should use the same flag which we used while mapping especially for
 * direction, coherent, and iova_restrict.
 */
void gcip_iommu_mapping_unmap(struct gcip_mapping *mapping);

#endif /* __GCIP_MAPPING_H__ */
