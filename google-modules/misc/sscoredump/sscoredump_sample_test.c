// SPDX-License-Identifier: GPL-2.0-only
/*
 * Subsystem-coredump sample test driver
 *
 * Copyright 2019 Google LLC
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/platform_data/sscoredump.h>

#define DEVICE_NAME "sscoredump-sample-test"
#define MAX_SEGS     256

struct sscd_info {
	char                *name;

	struct sscd_segment segs[MAX_SEGS];
	u16                 seg_count;
};

static void sscd_sample_test_release(struct device *dev);

static struct sscd_info test_info;
static struct sscd_platform_data sscd_pdata;
static struct platform_device sscd_dev = {
	.name            = DEVICE_NAME,
	.driver_override = SSCD_NAME,
	.id              = -1,
	.dev             = {
		.platform_data = &sscd_pdata,
		.release       = sscd_sample_test_release,
	},
};

/* allocate test segments */
static void test_client_allocate_segments(struct sscd_info *info)
{
	int i;

	/* allocate memory */
	for (i = 0; i < info->seg_count; i++) {
		uint size = PAGE_SIZE;

		info->segs[i].addr = vmalloc(size);
		if (!info->segs[i].addr)
			break;
		info->segs[i].size = size;
		info->segs[i].paddr = info->segs[i].addr;
		info->segs[i].vaddr = info->segs[i].addr + 0x80000000;
		memset(info->segs[i].addr, 'A' + i, size);
	}
}

/* free test segments */
static void test_client_free_segments(struct sscd_info *info)
{
	int i;

	for (i = 0; i < info->seg_count; i++)
		vfree(info->segs[i].addr);
}

/* trigger coredump */
static ssize_t test_client_coredump_store(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf,
					  size_t count)
{
	struct sscd_info *info = &test_info;
	struct sscd_platform_data *pdata = dev_get_platdata(dev);

	if (pdata->sscd_report) {
		dev_info(dev, "report: %d segments", info->seg_count);
		pdata->sscd_report(&sscd_dev, info->segs, info->seg_count,
			SSCD_FLAGS_ELFARM64HDR, "sample_test_coredump");
	}

	return count;
}

static DEVICE_ATTR_WO(test_client_coredump);

static struct attribute *sscd_test_attrs[] = {
	&dev_attr_test_client_coredump.attr,
	NULL,
};

static struct attribute_group sscd_test_group = {
	.attrs	= sscd_test_attrs,
};

static void sscd_sample_test_release(struct device *dev)
{
}

static int sscd_sample_test_init(void)
{
	struct sscd_info *info = &test_info;

	info->name = DEVICE_NAME;
	info->seg_count = 5;
	test_client_allocate_segments(info);

	/*
	 * register SSCD platform device
	 */
	platform_device_register(&sscd_dev);
	return sysfs_create_group(&sscd_dev.dev.kobj, &sscd_test_group);
}

static void sscd_sample_test_exit(void)
{
	struct sscd_info *info = &test_info;

	sysfs_remove_group(&sscd_dev.dev.kobj, &sscd_test_group);
	platform_device_unregister(&sscd_dev);

	test_client_free_segments(info);
}

module_init(sscd_sample_test_init);
module_exit(sscd_sample_test_exit);

MODULE_DESCRIPTION("Subsystem coredump sample test driver");
MODULE_AUTHOR("Oleg Matcovschi");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1a");
