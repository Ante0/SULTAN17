// SPDX-License-Identifier: GPL-2.0-only
/* filemap.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2025 Google LLC
 */

#include <linux/mm.h>
#include <linux/swap.h>

/*
 * turn readahead off from page fault handler if free swap size
 * is less than free_swap_threshold_mb.
 */
static unsigned long free_swap_threshold_mb = 20;

static struct kobject pixel_filemap_kobj;
static bool async_readahead_adj_enabled;

#define PIXEL_FILEMAP_ATTR_RW(_name) \
	static struct kobj_attribute _name##_attr = __ATTR_RW(_name)

static ssize_t swap_free_threshold_mb_store(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    const char *buf, size_t len)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	free_swap_threshold_mb = val;
	return len;
}

static ssize_t swap_free_threshold_mb_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
	return sysfs_emit(buf, "%lu\n", free_swap_threshold_mb);
}
PIXEL_FILEMAP_ATTR_RW(swap_free_threshold_mb);

static ssize_t async_readahead_adj_enable_store(struct kobject *kobj,
					    struct kobj_attribute *attr,
					    const char *buf, size_t len)
{
	bool enable;

	if (kstrtobool(buf, &enable))
		return -EINVAL;

	async_readahead_adj_enabled = enable;
	return len;
}

static ssize_t async_readahead_adj_enable_show(struct kobject *kobj,
					   struct kobj_attribute *attr,
					   char *buf)
{
	return sysfs_emit(buf, "%d\n", async_readahead_adj_enabled);
}
PIXEL_FILEMAP_ATTR_RW(async_readahead_adj_enable);

static struct attribute *pixel_filemap_attrs[] = {
	&swap_free_threshold_mb_attr.attr,
	&async_readahead_adj_enable_attr.attr,
	NULL,
};

static const struct attribute_group pixel_filemap_attr_group = {
	.attrs = pixel_filemap_attrs,
};

static const struct attribute_group *pixel_filemap_attr_groups[] = {
	&pixel_filemap_attr_group,
	NULL,
};

static void pixel_filemap_kobj_release(struct kobject *obj)
{
	/* Never released the static objects */
}

static const struct kobj_type pixel_filemap_ktype = {
	.release = pixel_filemap_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = pixel_filemap_attr_groups,
};

static inline void vh_mmap_readahead_adj(bool *skip)
{
	if (unlikely(!async_readahead_adj_enabled))
		return;

	if ((get_nr_swap_pages() * PAGE_SIZE) >> 20 < free_swap_threshold_mb)
		*skip = true;
}

void vh_do_async_mmap_readahead(void *data, struct vm_fault *vmf,
				struct folio *folio, bool *skip)
{
	vh_mmap_readahead_adj(skip);
}

void vh_do_sync_mmap_readahead(void *data, struct vm_fault *vmf,
			       bool *skip)
{
	vh_mmap_readahead_adj(skip);
}

int pixel_mm_filemap_sysfs(struct kobject *parent)
{
	int err = kobject_init_and_add(&pixel_filemap_kobj,
				       &pixel_filemap_ktype,
				       parent, "filemap");
	if (err)
		kobject_put(&pixel_filemap_kobj);

	return err;
}
