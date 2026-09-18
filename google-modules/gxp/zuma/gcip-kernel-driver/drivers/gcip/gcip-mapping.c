// SPDX-License-Identifier: GPL-2.0-only
/*
 * Definitions of GCIP mapping structs and interfaces.
 *
 * Copyright (C) 2025 Google LLC
 */

#include <linux/atomic.h>
#include <linux/container_of.h>
#include <linux/dev_printk.h>
#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/dma-resv.h>
#include <linux/err.h>
#include <linux/gfp_types.h>
#include <linux/math.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/mmap_lock.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/scatterlist.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/types.h>
#include <linux/uaccess.h>

#include <gcip/gcip-iommu.h>
#include <gcip/gcip-mapping.h>

#define to_buffer_mapping(mapping) container_of(mapping, struct gcip_buffer_mapping, mapping)
#define to_dmabuf_mapping(mapping) container_of(mapping, struct gcip_dmabuf_mapping, mapping)

/**
 * struct gcip_buffer_mapping - Contains the information about buffer mapping.
 * @mapping: Stores the mapping information to the IOMMU domain.
 * @host_address: Start address of buffer in the virtual address space of the mapping process.
 * @owning_mm: The mm_struct to maintain pinned_vm.
 * @sync_lock: The mutex lock to make sure the mapping only be synced by one thread at a time.
 */
struct gcip_buffer_mapping {
	struct gcip_mapping mapping;
	u64 host_address;
	struct mm_struct *owning_mm;
	struct mutex sync_lock;
};

/* Contains the information about dma-buf mapping. */
struct gcip_dmabuf_mapping {
	/* Stores the mapping information to the IOMMU domain. */
	struct gcip_mapping mapping;

	/* Following fields store the mapping information to the default domain. */

	/* Scatter-gather table which contains the mapping information. */
	struct sg_table *sgt_default;
	/* Shared dma-buf object. */
	struct dma_buf *dma_buf;
	/* Device attachment of dma-buf. */
	struct dma_buf_attachment *dma_buf_attachment;
};

/**
 * gcip_get_gup_flags() - Checks the access mode of the given address with VMA.
 * @host_addr: The target host_addr for checking the access.
 * @dev: The device struct used to print messages.
 * @map_debug_flags: Pointer to initialized gcip_map_debug_flags, bits may be set for any findings
 *                   here to be noted
 *
 * Checks and returns the read/write permission of address @host_addr.
 * If the target address can not be found in current->mm, assuming it is RW.
 *
 * Return: The encoded gup_flags of target host_addr.
 */
static unsigned int gcip_get_gup_flags(u64 host_addr, struct device *dev,
				       enum gcip_map_debug_flags *map_debug_flags)
{
	struct vm_area_struct *vma;
	unsigned int gup_flags;
	vm_flags_t vm_flags;

	mmap_read_lock(current->mm);
	vma = vma_lookup(current->mm, host_addr & PAGE_MASK);
	if (vma)
		vm_flags = vma->vm_flags;
	mmap_read_unlock(current->mm);

	if (!vma) {
		dev_dbg(dev, "unable to find address in VMA, assuming buffer writable");
		gup_flags = FOLL_LONGTERM | FOLL_WRITE;
		*map_debug_flags |= GCIP_MAP_DEBUG_VMA_NF;
	} else if (vm_flags & VM_WRITE) {
		gup_flags = FOLL_LONGTERM | FOLL_WRITE;
	} else {
		gup_flags = FOLL_LONGTERM;
	}

	if (vma && is_cow_mapping(vm_flags) && (gup_flags & FOLL_WRITE)) {
		dev_dbg(dev, "%#llx maps copy-on-write (vm_flags %#lx)", host_addr, vm_flags);
		*map_debug_flags |= GCIP_MAP_DEBUG_COW;
	}

	return gup_flags;
}

/**
 * gcip_get_buffer_npages() - Calculates the number of pages from @host_address and @size.
 * @dev: The device pointer for printing debug message.
 * @host_address: The host address passed by user.
 * @size: The size passed by user.
 * @n_pg_ptr: The pointer used to output the number of pages.
 *
 * Return: Error code or 0 on success.
 */
static int gcip_get_buffer_npages(struct device *dev, u64 host_address, size_t size, uint *n_pg_ptr)
{
	ulong offset = offset_in_page(host_address);
	uint num_pages;

	if (unlikely(offset + size < offset)) {
		dev_dbg(dev, "Overflow: offset(%lu) + size(%lu) < offset(%lu)", offset, size,
			offset);
		return -EFAULT;
	}

	num_pages = DIV_ROUND_UP((size + offset), PAGE_SIZE);
	if (unlikely(num_pages * PAGE_SIZE < size + offset)) {
		dev_dbg(dev, "Overflow: num_pages(%u) * PAGE_SIZE(%lu) < size(%lu) + offset(%lu)",
			num_pages, PAGE_SIZE, offset, size);
		return -EFAULT;
	}

	*n_pg_ptr = num_pages;

	return 0;
}

/**
 * gcip_pin_user_pages() - Try pin_user_pages_fast and try again with pin_user_pages if failed.
 * @dev: device for which the pages are being pinned, for logs.
 * @pages: The allocated pages to be pinned.
 * @start_addr: The starting user address, must be page-aligned.
 * @num_pages: Same as gcip_iommu_alloc_and_pin_user_pages.
 * @gup_flags: The gup_flags used to pin user pages.
 * @pin_user_pages_lock: Same as gcip_iommu_alloc_and_pin_user_pages.
 *
 * The function will try pin_user_pages for a few times.
 *
 * Return: @num_pages on success; 0 if only partial pages are pinned, or a negative error code on
 * failure.
 */
static int gcip_pin_user_pages(struct device *dev, struct page **pages, unsigned long start_addr,
			       uint num_pages, unsigned int gup_flags,
			       struct mutex *pin_user_pages_lock)
{
	int ret, i;
	struct vm_area_struct **vmas = NULL;
	int tried;

	/* Allocate our own vmas array non-contiguous. */
	vmas = kvmalloc((num_pages * sizeof(*vmas)), GFP_KERNEL | __GFP_NOWARN);
	if (!vmas)
		return -ENOMEM;
	/*
	 * pin_user_pages may fail due to temporary page reference counts held
	 * in various areas. Retry under lru_cache_disable to release additional
	 * reference counts from the LRU cache.
	 */
	for (tried = 0; tried < 5; tried++) {
		if (tried > 0)
			lru_cache_disable();
		if (pin_user_pages_lock)
			mutex_lock(pin_user_pages_lock);
		mmap_read_lock(current->mm);

		ret = pin_user_pages(start_addr, num_pages, gup_flags, pages, vmas);

		mmap_read_unlock(current->mm);
		if (pin_user_pages_lock)
			mutex_unlock(pin_user_pages_lock);

		if (tried > 0)
			lru_cache_enable();

		if (ret == num_pages)
			break;

		if (ret >= 0) {
			dev_err(dev, "Can only pin %u of %u pages requested", ret, num_pages);
			for (i = 0; i < ret; i++)
				unpin_user_page(pages[i]);
		}
		ret = 0;
	}
	if (tried > 0)
		dev_info(dev, "mapping required %d retries with LRU cache disabled", tried);
	kvfree(vmas);

	return ret;
}

/**
 * gcip_mapping_alloc_and_pin_user_pages() - Pins the user pages and returns an array of struct page
 *                                         pointers for the pinned pages.
 * @dev: The device pointer used to print messages.
 * @host_address: The requested host address.
 * @num_pages: The requested number of pages.
 * @gup_flags: The pointer gup_flags for pinning user pages.
 *             The flag FOLL_WRITE in gup_flags may be reomved if the user pages cannot be pinned
 *             with write access.
 * @pin_user_pages_lock: The lock to protect pin_user_page
 * @map_debug_flags: Pointer to initialized gcip_map_debug_flags, bits may be set for any findings
 *                   here to be noted
 *
 * This function tries to pin the user pages with `pin_user_page_fast` first and will try
 * `pin_user_page` on failure.
 * If both of above functions failed, it will retry with read-only mode.
 *
 * Return: Pinned user pages or error pointer on failure.
 */
static struct page **
gcip_mapping_alloc_and_pin_user_pages(struct device *dev, u64 host_address, uint num_pages,
				      unsigned int *gup_flags, struct mutex *pin_user_pages_lock,
				      enum gcip_map_debug_flags *map_debug_flags)
{
	unsigned long start_addr = host_address & PAGE_MASK;
	struct page **pages;
	int ret;

	/*
	 * "num_pages" is decided from user-space arguments, don't show warnings
	 * when facing malicious input.
	 */
	pages = kvmalloc_array(num_pages, sizeof(*pages), GFP_KERNEL | __GFP_NOWARN);
	if (!pages)
		return ERR_PTR(-ENOMEM);

	ret = gcip_pin_user_pages(dev, pages, start_addr, num_pages, *gup_flags,
				  pin_user_pages_lock);
	if (ret == num_pages)
		return pages;

	if (!(*gup_flags & FOLL_WRITE))
		goto err_free_pages;

	dev_warn_ratelimited(dev, "pin failed (ret=%d), assuming buffer is read-only", ret);
	*gup_flags &= ~FOLL_WRITE;
	*map_debug_flags |= GCIP_MAP_DEBUG_ASSUME_RDONLY;

	ret = gcip_pin_user_pages(dev, pages, start_addr, num_pages, *gup_flags,
				  pin_user_pages_lock);
	if (ret != num_pages)
		goto err_free_pages;

	atomic64_add(num_pages, &current->mm->pinned_vm);

	return pages;

err_free_pages:
	kvfree(pages);
	dev_err(dev, "Pin user pages failed: user_add=%#llx, num_pages=%u, %s, ret=%d\n",
		host_address, num_pages, ((*gup_flags & FOLL_WRITE) ? "writeable" : "read-only"),
		ret);

	return ERR_PTR(ret >= 0 ? -EFAULT : ret);
}

/**
 * gcip_mapping_buffer_sgt_create() - Pins user pages and creates a scatter-gather table.
 * @dev: The device pointer used to print messages.
 * @host_address: The requested host address.
 * @size: The size of the buffer.
 * @map_flags_ptr: The pointer to the mapping flags.
 * @debug_flags_ptr: The pointer to the debug flags.
 * @pin_user_pages_lock: The lock to protect pin_user_page.
 *
 * The @map_flags_ptr and @debug_flags_ptr are passed by pointer because they can be modified by
 * this function.
 *
 * Return: The pointer to the created sg_table, or the pointer to a negative errno otherwise.
 */
static struct sg_table *gcip_mapping_buffer_sgt_create(struct device *dev, u64 host_address,
						       size_t size, u64 *map_flags_ptr,
						       enum gcip_map_debug_flags *debug_flags_ptr,
						       struct mutex *pin_user_pages_lock)
{
	u64 gcip_map_flags = *map_flags_ptr;
	enum gcip_map_debug_flags map_debug_flags = *debug_flags_ptr;
	enum dma_data_direction orig_dir = GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags);
	uint num_pages;
	struct page **pages;
	int ret, i;
	struct sg_table *sgt;
	uint gup_flags;

	if (size == 0)
		return ERR_PTR(-EINVAL);

	if (!access_ok((const void *)host_address, size)) {
		dev_err(dev, "invalid address range in buffer map request");
		return ERR_PTR(-EFAULT);
	}

	gup_flags = gcip_get_gup_flags(host_address, dev, &map_debug_flags);

	ret = gcip_get_buffer_npages(dev, host_address, size, &num_pages);
	if (ret) {
		dev_err(dev, "Buffer size overflow: size=%#zx", size);
		return ERR_PTR(ret);
	}

	pages = gcip_mapping_alloc_and_pin_user_pages(dev, host_address, num_pages, &gup_flags,
						      pin_user_pages_lock, &map_debug_flags);
	if (IS_ERR(pages)) {
		dev_err(dev, "Failed to pin user pages (ret=%ld)\n", PTR_ERR(pages));
		return ERR_CAST(pages);
	}

	if (!(gup_flags & FOLL_WRITE) && orig_dir != DMA_TO_DEVICE) {
		gcip_map_flags &= ~GCIP_MAP_MASK_DMA_DIRECTION;
		gcip_map_flags |= ((u64)(DMA_TO_DEVICE) << GCIP_MAP_FLAGS_DMA_DIRECTION_OFFSET);
		map_debug_flags |= GCIP_MAP_DEBUG_OVRRD_RDDIR;
	}

	/* If mapping a writeable VMA read-only, clear CoW debug flag if set. */
	if (GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags) == DMA_TO_DEVICE)
		map_debug_flags &= ~GCIP_MAP_DEBUG_COW;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt) {
		ret = -ENOMEM;
		goto err_unpin_page;
	}

	ret = sg_alloc_table_from_pages(sgt, pages, num_pages, 0, num_pages * PAGE_SIZE,
					GFP_KERNEL);
	if (ret) {
		dev_err(dev, "Failed to alloc sgt for mapping (ret=%d)\n", ret);
		goto err_free_table;
	}

	*map_flags_ptr = gcip_map_flags;
	*debug_flags_ptr = map_debug_flags;
	kvfree(pages);

	return sgt;

err_free_table:
	/*
	 * The caller must call sg_free_table to clean up any leftover allocations if
	 * sg_alloc_table_from_pages returns non-zero values.
	 */
	sg_free_table(sgt);
	kfree(sgt);
err_unpin_page:
	for (i = 0; i < num_pages; i++)
		unpin_user_page(pages[i]);
	atomic64_sub(num_pages, &current->mm->pinned_vm);
	kvfree(pages);

	return ERR_PTR(ret);
}

/**
 * gcip_mapping_buffer_sgt_destroy() - Reverts gcip_mapping_buffer_sgt_create().
 * @sgt: The scatter-gather table to destroy.
 * @dir: The DMA direction of the mapping.
 * @mm: The mm_struct to maintain pinned_vm.
 *
 * If the @sgt has never been mapped, pass DMA_NONE for @dir to skip set_page_dirty_lock().
 */
static void gcip_mapping_buffer_sgt_destroy(struct sg_table *sgt, enum dma_data_direction dir,
					    struct mm_struct *mm)
{
	struct sg_page_iter sg_iter;
	struct page *page;
	unsigned long num_pages = 0;

	for_each_sg_page(sgt->sgl, &sg_iter, sgt->orig_nents, 0) {
		page = sg_page_iter_page(&sg_iter);
		if (dir == DMA_FROM_DEVICE || dir == DMA_BIDIRECTIONAL)
			set_page_dirty_lock(page);
		unpin_user_page(page);
		num_pages++;
	}

	atomic64_sub(num_pages, &mm->pinned_vm);
	sg_free_table(sgt);
	kfree(sgt);
}

/**
 * gcip_mapping_buffer_map_sgt_to_iova() - Prepare the sg_table of a buffer and map it to the domain
 *                                         at given IOVA.
 * @domain: The GCIP IOMMU domain the buffer should be mapped to.
 * @host_address: The starting virtual address of the user-space buffer.
 * @size: The size of the buffer in bytes.
 * @iova: The target IOVA to map the buffer to.
 * @map_flags_ptr: The pointer to the gcip_map_flags.
 * @debug_flags_ptr: The pointer to the gcip_map_debug_flags.
 * @pin_user_pages_lock: A mutex to protect the pin_user_pages calls.
 *
 * The @map_flags_ptr and @debug_flags_ptr are passed by pointer because they can be modified by
 * this function.
 *
 * If @iova is 0, a new IOVA will be allocated from the pool.
 *
 * Return: The pointer to the sg_table on success, or the pointer to a negative errno otherwise.
 */
static struct sg_table *
gcip_mapping_buffer_map_sgt_to_iova(struct gcip_iommu_domain *domain, u64 host_address, size_t size,
				    dma_addr_t iova, u64 *map_flags_ptr,
				    enum gcip_map_debug_flags *debug_flags_ptr,
				    struct mutex *pin_user_pages_lock)
{
	u64 gcip_map_flags = *map_flags_ptr;
	enum gcip_map_debug_flags map_debug_flags = *debug_flags_ptr;
	struct sg_table *sgt;
	int ret;

	sgt = gcip_mapping_buffer_sgt_create(domain->dev, host_address, size, &gcip_map_flags,
					     &map_debug_flags, pin_user_pages_lock);
	if (IS_ERR(sgt))
		return ERR_CAST(sgt);

	ret = gcip_iommu_domain_map_sgt_to_iova(domain, sgt, iova, &gcip_map_flags);
	if (!ret) {
		ret = -ENOSPC;
		dev_err(domain->dev, "Failed to map sgt to domain (ret=%d)\n", ret);
		goto err_destroy_sgt;
	}

	*map_flags_ptr = gcip_map_flags;
	*debug_flags_ptr = map_debug_flags;

	return sgt;

err_destroy_sgt:
	gcip_mapping_buffer_sgt_destroy(sgt, DMA_NONE, current->mm);

	return ERR_PTR(ret);
}

struct gcip_mapping *gcip_mapping_buffer_map_to_iova(struct gcip_iommu_domain *domain,
						     u64 host_address, size_t size, dma_addr_t iova,
						     u64 gcip_map_flags,
						     struct mutex *pin_user_pages_lock)
{
	struct gcip_buffer_mapping *buffer_mapping;
	struct gcip_mapping *mapping;
	struct sg_table *sgt;
	enum gcip_map_debug_flags map_debug_flags = 0;
	struct scatterlist *sl;
	int ret;
	int i;

	buffer_mapping = kzalloc(sizeof(*buffer_mapping), GFP_KERNEL);
	if (!buffer_mapping)
		return ERR_PTR(-ENOMEM);

	sgt = gcip_mapping_buffer_map_sgt_to_iova(domain, host_address, size, iova, &gcip_map_flags,
						  &map_debug_flags, pin_user_pages_lock);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_free_mapping;
	}

	buffer_mapping->host_address = host_address;
	mutex_init(&buffer_mapping->sync_lock);

	/* Grab a reference for owning_mm.  */
	mmgrab(current->mm);
	buffer_mapping->owning_mm = current->mm;

	mapping = &buffer_mapping->mapping;
	mapping->domain = domain;
	mapping->type = GCIP_MAPPING_TYPE_BUFFER;
	mapping->sgt = sgt;
	mapping->device_address = sg_dma_address(sgt->sgl) + offset_in_page(host_address);
	mapping->user_specified_daddr = iova;
	mapping->alloced_iova = iova ? 0 : sg_dma_address(sgt->sgl);
	mapping->gcip_map_flags = gcip_map_flags;
	mapping->dir = GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags);
	mapping->map_debug_flags = map_debug_flags;

	/* Calculate the mapped size. */
	mapping->size = 0;
	for_each_sg(sgt->sgl, sl, sgt->nents, i)
		mapping->size += sg_dma_len(sl);

	return mapping;

err_free_mapping:
	kfree(buffer_mapping);

	return ERR_PTR(ret);
}

struct gcip_mapping *gcip_mapping_buffer_map(struct gcip_iommu_domain *domain, u64 host_address,
					     size_t size, u64 gcip_map_flags,
					     struct mutex *pin_user_pages_lock)
{
	return gcip_mapping_buffer_map_to_iova(domain, host_address, size, 0, gcip_map_flags,
					       pin_user_pages_lock);
}

/*
 * For buffer unmap and trim operations, sync, unpin, and free the sgt.
 */
static void gcip_mapping_buffer_flush_sgt(struct gcip_buffer_mapping *buffer_mapping)
{
	struct gcip_mapping *mapping = &buffer_mapping->mapping;
	enum dma_data_direction dir = GCIP_MAP_FLAGS_GET_DMA_DIRECTION(mapping->gcip_map_flags);

	if (!mapping->sgt)
		return;

	gcip_iommu_domain_unmap_sgt_from_iova(mapping->domain, mapping->sgt,
					      mapping->gcip_map_flags);

	gcip_mapping_buffer_sgt_destroy(mapping->sgt, dir, buffer_mapping->owning_mm);
	mapping->sgt = NULL;
}

/**
 * gcip_mapping_buffer_unmap() - Reverts gcip_mapping_buffer_map.
 * @mapping: The target mapping that should be unmapped.
 */
static void gcip_mapping_buffer_unmap(struct gcip_mapping *mapping)
{
	struct gcip_buffer_mapping *buffer_mapping = to_buffer_mapping(mapping);

	gcip_mapping_buffer_flush_sgt(buffer_mapping);

	if (!mapping->user_specified_daddr)
		gcip_iommu_free_iova(mapping->domain, mapping->alloced_iova, mapping->size);

	mmdrop(buffer_mapping->owning_mm);
	mutex_destroy(&buffer_mapping->sync_lock);
	kfree(buffer_mapping);
}

int gcip_mapping_buffer_sync(struct gcip_mapping *mapping, struct device *dev, u64 offset, u64 size,
			     bool for_cpu)
{
	struct gcip_buffer_mapping *buffer_mapping;
	struct scatterlist *cur_sg, *start_sg = NULL, *end_sg = NULL;
	int nelems = 0, ret = 0, i;
	size_t cur_offset = 0;
	u64 start, end;
	unsigned int start_diff = 0, end_diff = 0;

	if (mapping->type != GCIP_MAPPING_TYPE_BUFFER)
		return -EINVAL;

	buffer_mapping = to_buffer_mapping(mapping);

	if (!mapping->sgt)
		return -EINVAL;

	/*
	 * Valid input requires:
	 * - size > 0 (offset + size != offset)
	 * - offset + size does not overflow (offset + size > offset)
	 * - the mapped range falls within [0 : mapping->size]
	 */
	if (offset + size <= offset || offset + size > mapping->size)
		return -EINVAL;

	/* Ensure only one sync at a time as the scatterlist will be modified. */
	mutex_lock(&buffer_mapping->sync_lock);

	/*
	 * Mappings are created at a PAGE_SIZE granularity, however other data which is not part of
	 * the mapped buffer may be present in the first and last pages of the buffer's
	 * scattergather list.
	 *
	 * To ensure only the intended data is actually synced, iterate through the scattergather
	 * list, to find the first and last `scatterlist`s which contain the range of the buffer to
	 * sync.
	 *
	 * After those links are found, change their offset/lengths so that `dma_map_sg_for_*()`
	 * will only sync the requested region.
	 */
	start = offset_in_page(buffer_mapping->host_address) + offset;
	end = start + size;

	for_each_sg(mapping->sgt->sgl, cur_sg, mapping->sgt->orig_nents, i) {
		/* Check if the scatterlist contains the start of the range to sync. */
		if (cur_offset <= start && start < cur_offset + cur_sg->length) {
			start_sg = cur_sg;
			start_diff = start - cur_offset;
		}

		if (start_sg)
			nelems++;

		/* Check if the scatterlist contains the end of the range to sync. */
		if (cur_offset < end && end <= cur_offset + cur_sg->length) {
			end_sg = cur_sg;
			end_diff = cur_offset + cur_sg->length - end;
			break;
		}

		cur_offset += cur_sg->length;
	}

	/* Make sure the valid start/end SGs were found. */
	if (!start_sg || !end_sg) {
		ret = -EINVAL;
		goto out_unlock;
	}

	start_sg->offset += start_diff;
	start_sg->dma_address += start_diff;
	start_sg->length -= start_diff;
	start_sg->dma_length -= start_diff;
	end_sg->length -= end_diff;
	end_sg->dma_length -= end_diff;

	if (for_cpu)
		dma_sync_sg_for_cpu(dev, start_sg, nelems, mapping->dir);
	else
		dma_sync_sg_for_device(dev, start_sg, nelems, mapping->dir);

	/* Revert the start and end scatterlist list. */
	end_sg->length += end_diff;
	end_sg->dma_length += end_diff;
	start_sg->offset -= start_diff;
	start_sg->dma_address -= start_diff;
	start_sg->length += start_diff;
	start_sg->dma_length += start_diff;

out_unlock:
	mutex_unlock(&buffer_mapping->sync_lock);

	return ret;
}

void gcip_mapping_buffer_trim(struct gcip_mapping *mapping)
{
	struct gcip_buffer_mapping *buffer_mapping;

	if (mapping->type != GCIP_MAPPING_TYPE_BUFFER) {
		dev_err(mapping->domain->dev, "Only buffer mappings can be trimmed");
		return;
	}

	buffer_mapping = to_buffer_mapping(mapping);
	gcip_mapping_buffer_flush_sgt(buffer_mapping);
}

int gcip_mapping_buffer_remap(struct gcip_mapping *mapping, struct mutex *pin_user_pages_lock)
{
	struct gcip_buffer_mapping *buffer_mapping;
	struct sg_table *sgt;
	enum gcip_map_debug_flags map_debug_flags;

	if (mapping->type != GCIP_MAPPING_TYPE_BUFFER)
		return -EINVAL;

	if (mapping->sgt) {
		dev_err(mapping->domain->dev, "remap buffer iova %pad not trimmed",
			&mapping->device_address);
		return -EBUSY;
	}

	buffer_mapping = to_buffer_mapping(mapping);
	sgt = gcip_mapping_buffer_map_sgt_to_iova(mapping->domain, buffer_mapping->host_address,
						  mapping->size, mapping->device_address,
						  &mapping->gcip_map_flags, &map_debug_flags,
						  pin_user_pages_lock);
	if (IS_ERR(sgt))
		return PTR_ERR(sgt);

	mapping->sgt = sgt;
	mapping->map_debug_flags = map_debug_flags;

	return 0;
}

/**
 * gcip_mapping_dmabuf_sgt_create() - Attach and map dma-buf to the default domain.
 * @dev: The device to attach the dma-buf to.
 * @dmabuf: The dma_buf to attach and map.
 * @map_flags_ptr: The pointer to the mapping flags.
 * @attachment_ptr: Pointer to return the dma_buf_attachment.
 *
 * The @map_flags_ptr is passed by pointer because it can be modified by this function.
 *
 * Return: The sg_table of the mapped dma-buf, or the pointer to a negative errno otherwise.
 */
static struct sg_table *gcip_mapping_dmabuf_sgt_create(struct device *dev, struct dma_buf *dmabuf,
						       u64 *map_flags_ptr,
						       struct dma_buf_attachment **attachment_ptr)
{
	u64 gcip_map_flags = *map_flags_ptr;
	enum dma_data_direction dir = GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags);
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt_default;
	int ret;

	attachment = dma_buf_attach(dmabuf, dev);
	if (IS_ERR(attachment)) {
		dev_err(dev, "Failed to attach dma-buf (ret=%ld, name=%s)\n", PTR_ERR(attachment),
			dmabuf->name);
		return ERR_CAST(attachment);
	}

	attachment->dma_map_attrs |= GCIP_MAP_FLAGS_GET_DMA_ATTR(gcip_map_flags);

	/* Map the attachment into the default domain. */
	sgt_default = dma_buf_map_attachment(attachment, dir);
	if (IS_ERR(sgt_default)) {
		ret = PTR_ERR(sgt_default);
		dev_err(dev, "Failed to get sgt from attachment (ret=%d, name=%s, size=%lu)\n", ret,
			dmabuf->name, dmabuf->size);
		goto err_detach_dmabuf;
	}

	*map_flags_ptr = gcip_map_flags;
	*attachment_ptr = attachment;

	return sgt_default;

err_detach_dmabuf:
	dma_buf_detach(dmabuf, attachment);

	return ERR_PTR(ret);
}

/**
 * gcip_mapping_dmabuf_sgt_destroy() - Reverts gcip_mapping_dmabuf_sgt_create().
 * @sgt_default: The sg_table to unmap.
 * @dmabuf: The dma_buf to detach.
 * @attachment: The dma_buf_attachment to unmap and detach.
 * @dir: The DMA direction of the mapping.
 */
static void gcip_mapping_dmabuf_sgt_destroy(struct sg_table *sgt_default, struct dma_buf *dmabuf,
					    struct dma_buf_attachment *attachment,
					    enum dma_data_direction dir)
{
	dma_buf_unmap_attachment(attachment, sgt_default, dir);

	dma_buf_detach(dmabuf, attachment);
}

/**
 * copy_alloc_sg_table(): Allocates a new sgt and copies the data from the old one.
 * @sgt_src: The source sg_table whose data will be copied to the new one.
 *
 * We will only copy the page information to the new sg_table, so the new sg_table will have the
 * same orig_nents and page information as the old one.
 *
 * Return: The new allocated sg_table with data copied from sgt_src or an error pointer on failure.
 */
static struct sg_table *copy_alloc_sg_table(struct sg_table *sgt_src)
{
	struct sg_table *sgt_dst;
	struct scatterlist *sgl_src, *sgl_dst;
	int ret, i;

	sgt_dst = kzalloc(sizeof(*sgt_dst), GFP_KERNEL);
	if (!sgt_dst) {
		ret = -ENOMEM;
		goto err_alloc_sgt;
	}

	ret = sg_alloc_table(sgt_dst, sgt_src->orig_nents, GFP_KERNEL);
	if (ret)
		goto err_alloc_sgl;

	sgl_dst = sgt_dst->sgl;
	for_each_sg(sgt_src->sgl, sgl_src, sgt_src->orig_nents, i) {
		sg_set_page(sgl_dst, sg_page(sgl_src), sgl_src->length, 0);
		sgl_dst = sg_next(sgl_dst);
	}

	return sgt_dst;

err_alloc_sgl:
	kfree(sgt_dst);
err_alloc_sgt:
	return ERR_PTR(ret);
}

/**
 * gcip_mapping_dmabuf_map_sgt_to_iova() - Prepare the sg_table of a dmabuf and map it to the
 *                                         domain at given IOVA.
 * @domain: The desired IOMMU domain where the sgt should be mapped.
 * @dmabuf: The shared dma-buf object.
 * @iova: The target IOVA to map @sgt.
 * @map_flags_ptr: The pointer to the gcip_map_flags.
 * @attach_ptr: The pointer to return the device attachment of @dmabuf.
 * @sgt_default_ptr: The pointer to return the default sg_table.
 *
 * The @gcip_map_flags is passed by pointer because it is possible to be modified by this function.
 *
 * If @iova is 0, a new IOVA will be allocated from the pool.
 *
 * Return: The pointer to the sg_table on success, or the pointer to a negative errno otherwise.
 */
static struct sg_table *gcip_mapping_dmabuf_map_sgt_to_iova(struct gcip_iommu_domain *domain,
							    struct dma_buf *dmabuf, dma_addr_t iova,
							    u64 *map_flags_ptr,
							    struct dma_buf_attachment **attach_ptr,
							    struct sg_table **sgt_default_ptr)
{
	struct device *dev = domain->dev;
	u64 gcip_map_flags = *map_flags_ptr;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt_default, *sgt_ret;
	int nents_mapped;
	int ret;

	sgt_default =
		gcip_mapping_dmabuf_sgt_create(domain->dev, dmabuf, &gcip_map_flags, &attachment);
	if (IS_ERR(sgt_default)) {
		ret = PTR_ERR(sgt_default);
		dev_err(dev, "Failed to create sg_table for dmabuf (%d)", ret);
		return ERR_CAST(sgt_default);
	}

	if (domain->default_domain) {
		sgt_ret = sgt_default;
		goto out;
	}

	sgt_ret = copy_alloc_sg_table(sgt_default);
	if (IS_ERR(sgt_ret)) {
		ret = PTR_ERR(sgt_ret);
		dev_err(domain->dev, "Failed to copy sg_table (ret=%d)\n", ret);
		goto err_destroy_sgt;
	}

	/*
	 * If DMA_ATTR_PRIVILEGED is set in the attrs provided by the heap exporter then this
	 * mapping is from the system-uncached heap. That attr signalled the SMMU driver to remove
	 * shareability/IOMMU_CACHE from the default domain mapping (if it was requested), since
	 * this is invalid (and tends to hit bogus AP cache lines). Also turn off coherency/
	 * IOMMU_CACHE in our per-context mapping.
	 */
	if (attachment->dma_map_attrs & DMA_ATTR_PRIVILEGED)
		gcip_map_flags &= ~BIT(GCIP_MAP_FLAGS_DMA_COHERENT_OFFSET);

	nents_mapped = gcip_iommu_domain_map_sgt_to_iova(domain, sgt_ret, iova, &gcip_map_flags);
	if (!nents_mapped) {
		ret = -ENOSPC;
		dev_err(domain->dev, "Failed to map dmabuf to IOMMU domain (ret=%d)\n", ret);
		goto err_free_sgt_ret;
	}

out:
	*attach_ptr = attachment;
	*sgt_default_ptr = sgt_default;
	*map_flags_ptr = gcip_map_flags;

	return sgt_ret;

err_free_sgt_ret:
	sg_free_table(sgt_ret);
	kfree(sgt_ret);
err_destroy_sgt:
	gcip_mapping_dmabuf_sgt_destroy(sgt_default, dmabuf, attachment,
					GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags));

	return ERR_PTR(ret);
}

struct gcip_mapping *gcip_mapping_dmabuf_map_to_iova(struct gcip_iommu_domain *domain,
						     struct dma_buf *dmabuf, dma_addr_t iova,
						     u64 gcip_map_flags)
{
	struct gcip_dmabuf_mapping *dmabuf_mapping;
	struct gcip_mapping *mapping;
	struct dma_buf_attachment *attachment;
	struct sg_table *sgt, *sgt_default;
	int ret;

	dmabuf_mapping = kzalloc(sizeof(*dmabuf_mapping), GFP_KERNEL);
	if (!dmabuf_mapping)
		return ERR_PTR(-ENOMEM);

	sgt = gcip_mapping_dmabuf_map_sgt_to_iova(domain, dmabuf, iova, &gcip_map_flags,
						  &attachment, &sgt_default);
	if (IS_ERR(sgt)) {
		ret = PTR_ERR(sgt);
		goto err_free_mapping;
	}

	get_dma_buf(dmabuf);
	dmabuf_mapping->dma_buf = dmabuf;
	dmabuf_mapping->dma_buf_attachment = attachment;
	dmabuf_mapping->sgt_default = sgt_default;

	mapping = &dmabuf_mapping->mapping;
	mapping->domain = domain;
	mapping->size = dmabuf->size;
	mapping->type = GCIP_MAPPING_TYPE_DMABUF;
	mapping->user_specified_daddr = iova;
	mapping->sgt = sgt;
	mapping->device_address = sg_dma_address(sgt->sgl);
	mapping->gcip_map_flags = gcip_map_flags;
	mapping->dir = GCIP_MAP_FLAGS_GET_DMA_DIRECTION(gcip_map_flags);

	return mapping;

err_free_mapping:
	kfree(dmabuf_mapping);

	return ERR_PTR(ret);
}

struct gcip_mapping *gcip_mapping_dmabuf_map(struct gcip_iommu_domain *domain,
					     struct dma_buf *dmabuf, u64 gcip_map_flags)
{
	return gcip_mapping_dmabuf_map_to_iova(domain, dmabuf, 0, gcip_map_flags);
}

/**
 * gcip_mapping_dmabuf_unmap() - Unmaps the dma buf mapping.
 * @mapping: The pointer of the mapping instance to be unmapped.
 *
 * Reverting gcip_mapping_map_dmabuf()
 */
static void gcip_mapping_dmabuf_unmap(struct gcip_mapping *mapping)
{
	struct gcip_dmabuf_mapping *dmabuf_mapping = to_dmabuf_mapping(mapping);

	if (!mapping->domain->default_domain) {
		if (mapping->user_specified_daddr)
			gcip_iommu_domain_unmap_sgt_from_iova(mapping->domain, mapping->sgt,
							      mapping->gcip_map_flags);
		else
			gcip_iommu_domain_unmap_sgt(mapping->domain, mapping->sgt,
						    mapping->gcip_map_flags);
		sg_free_table(mapping->sgt);
		kfree(mapping->sgt);
	}

	gcip_mapping_dmabuf_sgt_destroy(dmabuf_mapping->sgt_default, dmabuf_mapping->dma_buf,
					dmabuf_mapping->dma_buf_attachment, mapping->dir);
	dma_buf_put(dmabuf_mapping->dma_buf);
	kfree(dmabuf_mapping);
}

/* The helper function of gcip_mapping_dmabuf_map_show for printing multi-entry mappings. */
static void entry_show_dma_addrs(struct gcip_mapping *mapping, struct seq_file *s)
{
	struct sg_table *sgt = mapping->sgt;
	struct scatterlist *sg;
	uint i;

	if (sgt && sgt->nents > 1) {
		sg = sgt->sgl;
		seq_puts(s, " dma=[");
		for (i = 0; i < sgt->nents; i++) {
			if (i)
				seq_puts(s, ", ");
			seq_printf(s, "%pad", &sg_dma_address(sg));
			sg = sg_next(sg);
		}
		seq_puts(s, "]");
	}
	seq_puts(s, "\n");
}

void gcip_mapping_dmabuf_show(struct gcip_mapping *mapping, struct seq_file *s)
{
	static const char *dma_dir_tbl[4] = { "rw", "r", "w", "?" };
	struct gcip_dmabuf_mapping *dmabuf_mapping = to_dmabuf_mapping(mapping);

	seq_printf(s, "  %pad %lu %s %s %pad", &mapping->device_address,
		   DIV_ROUND_UP(mapping->size, PAGE_SIZE), dma_dir_tbl[mapping->dir],
		   dmabuf_mapping->dma_buf->exp_name,
		   &sg_dma_address(dmabuf_mapping->sgt_default->sgl));
	entry_show_dma_addrs(mapping, s);
}

size_t gcip_mapping_dmabuf_hiorder_size(struct gcip_mapping *mapping)
{
	struct gcip_dmabuf_mapping *dmabuf_mapping = to_dmabuf_mapping(mapping);
	struct scatterlist *sgl;
	int i;
	size_t ret = 0;

	for_each_sg(dmabuf_mapping->sgt_default->sgl, sgl, dmabuf_mapping->sgt_default->orig_nents,
		    i)
		if (sgl->length >= SZ_2M)
			ret += sgl->length;

	return ret;
}

void gcip_mapping_unmap(struct gcip_mapping *mapping)
{
	void *data = mapping->data;
	const struct gcip_mapping_ops *ops = mapping->ops;

	if (mapping->type == GCIP_MAPPING_TYPE_BUFFER)
		gcip_mapping_buffer_unmap(mapping);
	else if (mapping->type == GCIP_MAPPING_TYPE_DMABUF)
		gcip_mapping_dmabuf_unmap(mapping);

	/* From now on, @mapping is released and must not be accessed. */

	if (ops && ops->after_unmap)
		ops->after_unmap(data);
}

MODULE_IMPORT_NS(DMA_BUF);
