/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * GCIP support of DMA fences.
 *
 * Copyright (C) 2023 Google LLC
 */

#ifndef __GCIP_DMA_FENCE_H__
#define __GCIP_DMA_FENCE_H__

#include <linux/device.h>
#include <linux/dma-fence.h>
#include <linux/seq_file.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#define GCIP_DMA_FENCE_NAME_LENGTH 64

#define to_gcip_fence(fence) container_of(fence, struct gcip_dma_fence, fence)

/**
 * struct gcip_dma_fence_manager - GCIP DMA fence manager
 * @fence_list: The list of all fence objects for debugging.
 * @fence_list_lock: Protects the list headed by @fence_list_head.
 * @dev: The device pointer for logging.
 * @driver_name: The driver name of this fence manager.
 * @name: The name of the fence manager.
 */
struct gcip_dma_fence_manager {
	struct list_head fence_list;
	spinlock_t fence_list_lock;
	struct device *dev;
	char driver_name[GCIP_DMA_FENCE_NAME_LENGTH];
	char name[GCIP_DMA_FENCE_NAME_LENGTH];
};

/**
 * struct gcip_dma_fence - GCIP DMA fence
 * @fence: The underlying dma_fence object.
 * @mgr: The manager used to init this object.
 * @timeline_name: The timeline name of this fence.
 * @lock: Protects @fence.
 * @list_node: The list node for the fence manager's list.
 */
struct gcip_dma_fence {
	struct dma_fence fence;
	struct gcip_dma_fence_manager *mgr;
	char timeline_name[GCIP_DMA_FENCE_NAME_LENGTH];
	spinlock_t lock;
	struct list_head list_node;
};

/**
 * gcip_dma_fence_manager_create() - Allocates and returns a GCIP DMA fence manager.
 * @dev: The device for logging and managed memory allocation.
 * @driver_name: The driver name of this fence manager.
 * @name: The name of the fence manager.
 *
 * Return: A pointer to the fence manager on success, or the pointer to a negative errno otherwise.
 */
struct gcip_dma_fence_manager *gcip_dma_fence_manager_create(struct device *dev, char *driver_name,
							     const char *name);

/**
 * gcip_dma_fence_manager_destroy() - Destroys the fence manager.
 * @mgr: The fence manager to destroy.
 *
 * The remaining fences in the manager will be signaled with -ECANCELED and removed from the list.
 */
void gcip_dma_fence_manager_destroy(struct gcip_dma_fence_manager *mgr);

/**
 * gcip_dma_fence_manager_show() - Prints data of all fences in @mgr to the sequence file @s.
 * @mgr: The fence manager to show.
 * @s: The sequence file to print to.
 */
void gcip_dma_fence_manager_show(struct gcip_dma_fence_manager *mgr, struct seq_file *s);

/**
 * gcip_dma_fence_create() - Allocates and initializes a GCIP DMA fence.
 * @mgr: The fence manager to use for the fence.
 * @seqno: The sequence number to initialize the fence with.
 * @timeline_name: A null-terminated string with length less than GCIP_DMA_FENCE_NAME_LENGTH.
 *
 * This function does:
 *  1. Allocate memory for the GCIP DMA fence object.
 *  2. Initialize the GCIP DMA fence object.
 *
 * @mgr can be NULL, which means the fence will not be managed by any fence manager.
 *
 * The content of @timeline_name will be copied so it's fine to release this pointer afterwards.
 *
 * The returned fence must be released with gcip_dma_fence_put().
 *
 * Return: A pointer to the GCIP DMA fence on success, or a negative errno pointer otherwise.
 */
struct gcip_dma_fence *gcip_dma_fence_create(struct gcip_dma_fence_manager *mgr, u64 seqno,
					     const char *timeline_name);

/**
 * gcip_dma_fence_install_fd() - Installs a file descriptor for the fence.
 * @gfence: The GCIP DMA fence to install.
 *
 * The reference count of the fence will be increased by one, which is held by the sync_file.
 *
 * Return: The file descriptor on success, or a negative errno otherwise.
 */
int gcip_dma_fence_install_fd(struct gcip_dma_fence *gfence);

/**
 * gcip_dma_fence_get() - Increments the reference count of the GCIP DMA fence.
 * @gfence: The fence to get.
 */
struct gcip_dma_fence *gcip_dma_fence_get(struct gcip_dma_fence *gfence);

/**
 * gcip_dma_fence_put() - Decrements the reference count of the GCIP DMA fence.
 * @gfence: The fence to put.
 */
void gcip_dma_fence_put(struct gcip_dma_fence *gfence);

/*
 * Sets @status to the DMA fence status of DMA fence FD @fence.
 * @status is only set when this function returns 0.
 *
 * It is OK if @fence does not refer to a gcip_dma_fence.
 *
 * Returns 0 on success. Otherwise a negative errno.
 */
int gcip_dma_fence_status(int fence, int *status);

/**
 * gcip_signal_dma_fence_with_status() - Signals the given fence with error code.
 * @fence: The target fence to be signaled.
 * @error: The error code to set in the dma fence. Pass 0 for the success cases.
 * @ignore_signaled: Set to true to ignore the double-signaled error.
 */
int gcip_signal_dma_fence_with_status(struct dma_fence *fence, int error, bool ignore_signaled);

/*
 * Signals the fence error of DMA fence FD @fence.
 *
 * If the fence has been signaled,
 *  - if @ignore_signaled is true, this function does nothing.
 *  - otherwise, returns -EALREADY.
 *
 * It is OK if @fence does not refer to a gcip_dma_fence.
 *
 * Returns 0 on success. Otherwise a negative errno.
 */
int gcip_dma_fence_signal(int fence, int error, bool ignore_signaled);

/* Identical to gcip_dma_fence_signal except this function accepts gcip_dma_fence as the input. */
int gcip_dma_fenceptr_signal(struct gcip_dma_fence *gfence, int error, bool ignore_signaled);

/**
 * gcip_dma_fence_scnprintf() - Prints data of @gfence to the buffer @buf.
 * @buf: The buffer to print to.
 * @size: The size of the buffer.
 * @gfence: The fence to print.
 *
 * Return: The number of characters written into @buf not including the trailing '\0'.
 */
int gcip_dma_fence_scnprintf(char *buf, size_t size, struct gcip_dma_fence *gfence);

/* Prints data of @gfence to the sequence file @s. For debug purpose only. */
void gcip_dma_fence_show(struct gcip_dma_fence *gfence, struct seq_file *s);

/**
 * gcip_dma_fence_merge_fences() -  Merges an array of DMA fences with dma_fence_unwrap_merge().
 *
 * Creates a dma_fence_array from all of the provided fences and returns a dma_fence representing
 * that array. If any of the provided fences are also arrays, the resulting array will include
 * their component fences as well.
 *   - Signaling with `gcip_signal_dma_fence_with_status` will signal all component fences.
 *   - Waiting with `dma_fence_wait` will wait until all component fences have been signaled.
 *
 * The returned fence must be released with `dma_fence_put()`.
 *
 * Returns a pointer to the fence on success. Otherwise a negative errno as an ERR_PTR.
 */
struct dma_fence *gcip_dma_fence_merge_fences(int num_fences, struct dma_fence **fences);

/**
 * gcip_dma_fence_merge_fds() - Gets and merges an array of DMA fences from their FDs.
 *
 * Creates a dma_fence_array from all of the provided fences.
 * It is OK if @fence_fds do not refer to gcip_dma_fences.
 *
 * Returns a pointer to the fence on success. Otherwise a negative errno as an ERR_PTR.
 *  - If unable to allocate sufficient memory, returns ERR_PTR(-ENOMEM)
 *  - If any of @fence_fds are invalid, returns ERR_PTR(-ENOENT)
 */
struct dma_fence *gcip_dma_fence_merge_fds(int num_fences, int *fence_fds);

/**
 * gcip_dma_fence_array_disable_signaling() - Reverts dma_fence_array_enable_signaling() to make
 *                                            @fence don't wait for the signal of underlying fences.
 * @fence: The base fence of dma_fence_array.
 *
 * This function should be called when @fence is going to be destroyed before signaled.
 * @fence must be a dma_fence_array created and enabled by the caller itself, otherwise, the fence
 * array created by other IP may be affected.
 *
 * The callback functions will be removed from the underlying fences and their reference will be
 * put if removed successfully.
 */
void gcip_dma_fence_array_disable_signaling(struct dma_fence *fence);

#endif /* __GCIP_DMA_FENCE_H__ */
