/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * The definition of gcip_memory.
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __GCIP_MEMORY_H__
#define __GCIP_MEMORY_H__

#include <linux/types.h>

/**
 * struct gcip_memory - The object to record the mapping of the memory.
 * @virt_addr: The kernel virtual address of the memory.
 * @phys_addr: The physical address of a contiguous memory area, or zero if @sgt is not NULL.
 * @system_coherent_dma_addr: The DMA address obtained from DMA-IOMMU coherent allocation APIs. This
 *                            buffer can also be optionally mapped to @dma_addr if needed.
 * @dma_addr: The device DMA address of the memory.
 * @host_addr: The host address of the memory, or zero if not mapped to a host process.
 * @size: The size of the memory.
 * @sgt: Pointer to scatter-gather table for the memory, or NULL for a contiguous memory area
 *       starting at @phys_addr.
 */
struct gcip_memory {
	void *virt_addr;
	phys_addr_t phys_addr;
	dma_addr_t system_coherent_dma_addr;
	dma_addr_t dma_addr;
	u64 host_addr;
	size_t size;
	struct sg_table *sgt;
};

#endif /* __GCIP_MEMORY_H__ */
