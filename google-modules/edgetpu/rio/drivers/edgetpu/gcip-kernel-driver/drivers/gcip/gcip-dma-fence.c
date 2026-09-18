// SPDX-License-Identifier: GPL-2.0-only
/*
 * GCIP support of DMA fences.
 *
 * Copyright (C) 2023-2026 Google LLC
 */

#include <linux/bitops.h>
#include <linux/device.h>
#include <linux/dma-fence.h>
#include <linux/dma-fence-unwrap.h>
#include <linux/err.h>
#include <linux/file.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/sync_file.h>
#include <linux/time.h>

#include <gcip/gcip-dma-fence.h>

#define to_gfence(fence) container_of(fence, struct gcip_dma_fence, fence)

int gcip_signal_dma_fence_with_status(struct dma_fence *fence, int error, bool ignore_signaled)
{
	unsigned long flags;
	int ret;
	struct dma_fence *cur;
	struct dma_fence_unwrap iter;

	if (error > 0)
		error = -error;
	if (unlikely(error < -MAX_ERRNO))
		return -EINVAL;

	/* If not ignoring signaled, only return busy if ALL fences are already signaled. */
	ret = ignore_signaled ? 0 : -EBUSY;

	/*
	 * If @fence is a dma_fence_array, iterate over each fence in the array and signal it.
	 * This loop will be executed exactly once for cur == @fence, if @fence is not an array.
	 */
	dma_fence_unwrap_for_each(cur, &iter, fence) {
		spin_lock_irqsave(cur->lock, flags);
		/* don't signal fence twice */
		if (unlikely(test_bit(DMA_FENCE_FLAG_SIGNALED_BIT, &cur->flags)))
			goto cont_unlock;
		if (error)
			dma_fence_set_error(cur, error);
		dma_fence_signal_locked(cur);
		/* Success as long as at least one fence was not signaled. */
		ret = 0;
cont_unlock:
		spin_unlock_irqrestore(cur->lock, flags);
	}

	return ret;
}

static const char *sync_status_str(int status)
{
	if (status < 0)
		return "error";
	if (status > 0)
		return "signaled";
	return "active";
}

struct gcip_dma_fence_manager *gcip_dma_fence_manager_create(struct device *dev, char *driver_name,
							     const char *name)
{
	struct gcip_dma_fence_manager *mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);

	if (!mgr)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&mgr->fence_list);
	spin_lock_init(&mgr->fence_list_lock);
	strscpy(mgr->driver_name, driver_name, GCIP_DMA_FENCE_NAME_LENGTH);
	strscpy(mgr->name, name, GCIP_DMA_FENCE_NAME_LENGTH);
	mgr->dev = dev;

	return mgr;
}

void gcip_dma_fence_manager_destroy(struct gcip_dma_fence_manager *mgr)
{
	struct gcip_dma_fence *gfence;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&mgr->fence_list_lock, flags);

	while (!list_empty(&mgr->fence_list)) {
		gfence = list_first_entry(&mgr->fence_list, struct gcip_dma_fence, list_node);

		/* The list node is still safe to be deleted in gcip_dma_fence_exit(). */
		list_del_init(&gfence->list_node);

		/* Nullify manager pointer with RCU to ensure release path knows mgr is gone. */
		rcu_assign_pointer(gfence->mgr, NULL);

		/* If it fails, we can skip it because the fence is already in its release path. */
		if (!dma_fence_get_rcu(&gfence->fence))
			continue;

		/* Drop and re-acquire the lock to safely signal and put the reference. */
		spin_unlock_irqrestore(&mgr->fence_list_lock, flags);

		ret = gcip_dma_fenceptr_signal(gfence, -ECANCELED, true);
		if (ret) {
			char buf[128];

			gcip_dma_fence_scnprintf(buf, sizeof(buf), gfence);
			dev_warn(mgr->dev, "Failed to signal fence(%s): %d", buf, ret);
		}

		/*
		 * It is safe to trigger gcip_dma_fence_exit() because:
		 * 1. The gfence->list_node is initialized.
		 * 2. The gfence->mgr is NULL.
		 */
		dma_fence_put(&gfence->fence);

		spin_lock_irqsave(&mgr->fence_list_lock, flags);
	}

	spin_unlock_irqrestore(&mgr->fence_list_lock, flags);

	/* Wait for any gcip_dma_fence_exit() calls that already read the mgr pointer to finish */
	synchronize_rcu();

	kfree(mgr);
}

static void gcip_dma_fence_manager_add_fence(struct gcip_dma_fence_manager *mgr,
					     struct gcip_dma_fence *gfence)
{
	unsigned long flags;

	spin_lock_irqsave(&mgr->fence_list_lock, flags);
	list_add_tail(&gfence->list_node, &mgr->fence_list);
	spin_unlock_irqrestore(&mgr->fence_list_lock, flags);
}

static void gcip_dma_fence_manager_del_fence(struct gcip_dma_fence_manager *mgr,
					     struct gcip_dma_fence *gfence)
{
	unsigned long flags;

	spin_lock_irqsave(&mgr->fence_list_lock, flags);
	list_del_init(&gfence->list_node);
	spin_unlock_irqrestore(&mgr->fence_list_lock, flags);
}

void gcip_dma_fence_manager_show(struct gcip_dma_fence_manager *mgr, struct seq_file *s)
{
	struct gcip_dma_fence *gfence;
	unsigned long flags;

	spin_lock_irqsave(&mgr->fence_list_lock, flags);
	list_for_each_entry(gfence, &mgr->fence_list, list_node)
		gcip_dma_fence_show(gfence, s);
	spin_unlock_irqrestore(&mgr->fence_list_lock, flags);
}

static const char *gcip_dma_fence_get_driver_name(struct dma_fence *fence)
{
	struct gcip_dma_fence *gfence = to_gfence(fence);

	return gfence->mgr ? gfence->mgr->driver_name : "";
}

static const char *gcip_dma_fence_get_timeline_name(struct dma_fence *fence)
{
	struct gcip_dma_fence *gfence = to_gfence(fence);

	return gfence->timeline_name;
}

static bool gcip_dma_fence_enable_signaling(struct dma_fence *fence)
{
	return true;
}

/**
 * gcip_dma_fence_exit() - Exits the DMA fence.
 * @gfence: The GCIP DMA fence to exit.
 */
static void gcip_dma_fence_exit(struct gcip_dma_fence *gfence)
{
	struct gcip_dma_fence_manager *mgr;

	/* Enter RCU read-side to ensure 'mgr' memory stays valid.  */
	rcu_read_lock();

	mgr = rcu_dereference(gfence->mgr);
	if (mgr)
		gcip_dma_fence_manager_del_fence(mgr, gfence);

	rcu_read_unlock();
}

/**
 * gcip_dma_fence_release() - Releases the DMA fence.
 * @fence: The DMA fence to release.
 *
 * This function is called when the reference count of the DMA fence is 0.
 */
static void gcip_dma_fence_release(struct dma_fence *fence)
{
	struct gcip_dma_fence *gfence = to_gfence(fence);

	gcip_dma_fence_exit(gfence);

	kfree_rcu(gfence, fence.rcu);
}

const struct dma_fence_ops gcip_dma_fence_ops = {
	.get_driver_name = gcip_dma_fence_get_driver_name,
	.get_timeline_name = gcip_dma_fence_get_timeline_name,
	.enable_signaling = gcip_dma_fence_enable_signaling,
	.release = gcip_dma_fence_release,
};

int gcip_dma_fence_install_fd(struct gcip_dma_fence *gfence)
{
	struct sync_file *sync_file;
	int fd;
	int ret;

	fd = get_unused_fd_flags(O_CLOEXEC);
	if (fd < 0)
		return fd;

	sync_file = sync_file_create(&gfence->fence);
	if (!sync_file) {
		ret = -ENOMEM;
		goto err_put_fd;
	}

	fd_install(fd, sync_file->file);

	return fd;

err_put_fd:
	put_unused_fd(fd);

	return ret;
}

struct gcip_dma_fence *gcip_dma_fence_create(struct gcip_dma_fence_manager *mgr, u64 seqno,
					     const char *timeline_name)
{
	struct gcip_dma_fence *gfence;
	u64 context = dma_fence_context_alloc(1);

	gfence = kzalloc(sizeof(*gfence), GFP_KERNEL);
	if (!gfence)
		return ERR_PTR(-ENOMEM);

	strscpy(gfence->timeline_name, timeline_name, GCIP_DMA_FENCE_NAME_LENGTH);
	spin_lock_init(&gfence->lock);
	INIT_LIST_HEAD(&gfence->list_node);
	dma_fence_init(&gfence->fence, &gcip_dma_fence_ops, &gfence->lock, context, seqno);

	if (mgr) {
		gfence->mgr = mgr;
		gcip_dma_fence_manager_add_fence(mgr, gfence);
	}

	return gfence;
}

struct gcip_dma_fence *gcip_dma_fence_get(struct gcip_dma_fence *gfence)
{
	dma_fence_get(&gfence->fence);

	return gfence;
}

void gcip_dma_fence_put(struct gcip_dma_fence *gfence)
{
	dma_fence_put(&gfence->fence);
}

int gcip_dma_fence_status(int fence, int *status)
{
	struct dma_fence *fencep;

	fencep = sync_file_get_fence(fence);
	if (!fencep)
		return -EBADF;
	*status = dma_fence_get_status(fencep);
	dma_fence_put(fencep);
	return 0;
}

int gcip_dma_fence_signal(int fence, int error, bool ignore_signaled)
{
	struct dma_fence *fencep;
	int ret;

	fencep = sync_file_get_fence(fence);
	if (!fencep)
		return -EBADF;
	ret = gcip_signal_dma_fence_with_status(fencep, error, ignore_signaled);
	dma_fence_put(fencep);
	return ret;
}

int gcip_dma_fenceptr_signal(struct gcip_dma_fence *gfence, int error, bool ignore_signaled)
{
	return gcip_signal_dma_fence_with_status(&gfence->fence, error, ignore_signaled);
}

int gcip_dma_fence_scnprintf(char *buf, size_t size, struct gcip_dma_fence *gfence)
{
	struct gcip_dma_fence_manager *mgr;
	struct dma_fence *fence = &gfence->fence;
	int total = 0, written;

	spin_lock_irq(&gfence->lock);

	written = scnprintf(buf, size, "%s-%s %llu-%llu %s", fence->ops->get_driver_name(fence),
			    fence->ops->get_timeline_name(fence), fence->context, fence->seqno,
			    sync_status_str(dma_fence_get_status_locked(fence)));
	total += written;
	buf += written;
	size -= written;

	if (test_bit(DMA_FENCE_FLAG_TIMESTAMP_BIT, &fence->flags)) {
		struct timespec64 ts = ktime_to_timespec64(fence->timestamp);

		written = scnprintf(buf, size, " @%lld.%09ld", (s64)ts.tv_sec, ts.tv_nsec);
		total += written;
		buf += written;
		size -= written;
	}

	if (fence->error) {
		written = scnprintf(buf, size, " err=%d", fence->error);
		total += written;
		buf += written;
		size -= written;
	}

	spin_unlock_irq(&gfence->lock);

	rcu_read_lock();
	mgr = rcu_dereference(gfence->mgr);
	if (mgr && strlen(mgr->name)) {
		written = scnprintf(buf, size, " %s", mgr->name);
		total += written;
	}
	rcu_read_unlock();

	return total;
}

void gcip_dma_fence_show(struct gcip_dma_fence *gfence, struct seq_file *s)
{
	char buf[128];

	gcip_dma_fence_scnprintf(buf, sizeof(buf), gfence);
	seq_printf(s, "%s", buf);
}

struct dma_fence *gcip_dma_fence_merge_fences(int num_fences, struct dma_fence **fences)
{
	struct dma_fence *tmp;
	struct dma_fence *array;
	int i;
	int error;
	ktime_t timestamp;

	array = dma_fence_unwrap_merge(fences[0]);
	if (!array)
		return ERR_PTR(-ENOMEM);

	for (i = 1; i < num_fences; i++) {
		tmp = array;
		array = dma_fence_unwrap_merge(tmp, fences[i]);
		dma_fence_put(tmp);
		if (!array)
			return ERR_PTR(-ENOMEM);
	}

	/*
	 * When merging fences, any already signaled fences are thrown away, regardless of whether
	 * they were signaled with success or an error. Once the fences are merged however, the
	 * merged fence will inherit the first error from an internal fence.
	 *
	 * Re-iterate over the fences being merged and if any are already signaled with an error,
	 * pass that error on to the merged fence. This ensures waiters will not attempt to do work
	 * that should have been cancelled due to an in-fence error.
	 */
	for (i = 0; i < num_fences; i++) {
		error = dma_fence_get_status(fences[i]);
		if (error < 0) {
			timestamp = dma_fence_timestamp(fences[i]);
			dma_fence_put(array);
			array = dma_fence_allocate_private_stub(timestamp);
			array->error = error;
			break;
		}
	}

	return array;
}

struct dma_fence *gcip_dma_fence_merge_fds(int num_fences, int *fence_fds)
{
	struct dma_fence **fences;
	struct dma_fence *result;
	int i;

	if (!num_fences)
		return ERR_PTR(-EINVAL);

	fences = kcalloc(num_fences, sizeof(*fences), GFP_KERNEL);
	if (!fences)
		return ERR_PTR(-ENOMEM);

	for (i = 0; i < num_fences; i++) {
		fences[i] = sync_file_get_fence(fence_fds[i]);
		if (!fences[i]) {
			result = ERR_PTR(-ENOENT);
			goto out;
		}
	}

	result = gcip_dma_fence_merge_fences(num_fences, fences);

out:
	for (i = 0; i < num_fences; i++)
		dma_fence_put(fences[i]);
	kfree(fences);
	return result;
}

void gcip_dma_fence_array_disable_signaling(struct dma_fence *fence)
{
	struct dma_fence_array *array = container_of(fence, struct dma_fence_array, base);
	struct dma_fence_array_cb *cb = (void *)(&array[1]);
	unsigned long flags;
	int i;

	if (!dma_fence_is_array(fence))
		return;

	spin_lock_irqsave(fence->lock, flags);

	if (!test_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &fence->flags))
		goto out;

	for (i = 0; i < array->num_fences; ++i) {
		if (dma_fence_remove_callback(array->fences[i], &cb[i].cb))
			dma_fence_put(&array->base);
	}

	clear_bit(DMA_FENCE_FLAG_ENABLE_SIGNAL_BIT, &fence->flags);

out:
	spin_unlock_irqrestore(fence->lock, flags);
}
