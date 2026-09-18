// SPDX-License-Identifier: GPL-2.0-only
/*
 * Subsystem-crash test module
 * Copyright 2019 Google LLC
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/kthread.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/platform_data/sscoredump.h>

#define DEVICE_NAME "sscoredump-test"
#define MAX_OPEN     4
#define MAX_SEGS     32
#define MAX_SEG_SIZE 4096
#define MAX_THREADS  32
#define MAX_REPEATS  10

#define DEFAULT_SEGS 10

struct sscd_info_client {
	struct list_head       list;
	struct platform_device *pdev;
	char                   name[32];
	u32                    id;
};

struct sscd_info {
	char                 *name;
	struct miscdevice    miscdev;

	u32                  repeat_count;
	u32                  thread_count;
	struct sscd_segment  segs[MAX_SEGS];
	u16                  seg_count;
	u32                  seg_size_in_pages;

	u32                  start_test;
	atomic_t             sscd_threads;
	wait_queue_head_t    done_wait_q;

	atomic_t             in_test; /* 0 - no test active, 1 - in progress */
	struct list_head     in_test_clients; /* list of registered clients */
};

struct sscd_info_thread {
	struct task_struct *task;
	char               name[32];
	u32                id;
	struct sscd_info   *info;
};

static struct sscd_info test_info;

/*
 * allocate test segments
 */
static void sscd_allocate_segments(struct sscd_info *info)
{
	int i;

	/* free memory */
	for (i = 0; i < MAX_SEGS; i++) {
		if (info->segs[i].addr) {
			vfree((void *)info->segs[i].addr);
			memset(&info->segs[i], 0, sizeof(info->segs[i]));
		}
	}

	pr_info("allocate segments: segments %d size(pages/bytes) %d/%lu",
		info->seg_count,
		info->seg_size_in_pages,
		info->seg_size_in_pages * PAGE_SIZE);

	/* allocate memory */
	for (i = 0; i < info->seg_count; i++) {
		uint size = PAGE_SIZE * info->seg_size_in_pages;

		info->segs[i].addr = vmalloc(size);
		if (!info->segs[i].addr)
			break;
		info->segs[i].size = size;
		info->segs[i].paddr = info->segs[i].addr;
		info->segs[i].vaddr = info->segs[i].addr + 0x80000000;
		memset(info->segs[i].addr, 'A' + i, size);
	}
}

/*
 * free test segments
 */
static void sscd_free_segments(struct sscd_info *info)
{
	int i;

	for (i = 0; i < info->seg_count; i++)
		vfree(info->segs[i].addr);
}

static struct platform_device *sscd_register(const char *name)
{
	int ret;
	struct sscd_platform_data sscd_pdata = {0};
	struct platform_device *pdev = platform_device_alloc(name, -1);

	pdev->driver_override = SSCD_NAME;
	platform_device_add_data(pdev, &sscd_pdata, sizeof(sscd_pdata));
	ret = platform_device_add(pdev);

	pr_err("%s: pdev %pK ret %d", __func__, pdev, ret);
	return pdev;
}

static void sscd_unregister(struct platform_device *pdev)
{
	if (pdev) {
		pr_err("%s: pdev %pK", __func__, pdev);
		platform_device_put(pdev);
	}
}

/*
 * threads testing
 */
static int sscd_thread(void *thread_data)
{
	struct sscd_info_thread *thrd_info = thread_data;
	struct sscd_info *info = thrd_info->info;
	struct platform_device *pdev;

	pr_err("%s: starting thread...", thrd_info->name);

	pdev = sscd_register(thrd_info->name);
	if (pdev) {
		struct sscd_platform_data *pdata = dev_get_platdata(&pdev->dev);

		if (pdata) {
			pr_err("%s: invoke report nsegs(%d)",
			       thrd_info->name, info->seg_count);
			pdata->sscd_report(pdev, info->segs, info->seg_count,
					   SSCD_FLAGS_ELFARM64HDR, "sscd_thread");
			pr_err("%s: invoke unregister", thrd_info->name);
			sscd_unregister(pdev);
		}
	} else {
		pr_err("%s: unable to register client!!!!", thrd_info->name);
	}

	atomic_dec(&info->sscd_threads);
	wake_up(&info->done_wait_q);

	kfree(thrd_info);

	return 0;
}

static ssize_t test_threads_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);

	/* make sure single test is running */
	if (atomic_cmpxchg(&info->in_test, 0, 1) != 0)
		return -1;

	if (info->thread_count) {
		u32 i;

		init_waitqueue_head(&info->done_wait_q);
		atomic_set(&info->sscd_threads, info->thread_count);

		for (i = 0; i < info->thread_count; i++) {
			struct sscd_info_thread *thrd_info =
				kzalloc(sizeof(*thrd_info), GFP_KERNEL);

			if (!thrd_info) {
				atomic_dec(&info->sscd_threads);
				continue;
			}

			snprintf(thrd_info->name, sizeof(thrd_info->name),
				 "sscd_thread_%d", i);
			thrd_info->info = info;
			thrd_info->id = i;
			thrd_info->task = kthread_create(sscd_thread, thrd_info, thrd_info->name);
			if (IS_ERR_OR_NULL(thrd_info->task)) {
				atomic_dec(&info->sscd_threads);
				continue;
			}

			wake_up_process(thrd_info->task);
		}

		wait_event_interruptible(info->done_wait_q, atomic_read(&info->sscd_threads) == 0);

	} else {
		pr_err("skipped thread test");
	}

	atomic_set(&info->in_test, 0);

	return count;
}

/*
 * measure througput on first registered client
 */
static ssize_t test_throughput_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	int ret = 0;
	int rc = 0;
	u64 report_start;
	u64 report_end;
	struct sscd_info_client *client;
	struct sscd_platform_data *pdata;

	/* make sure single test is running */
	if (atomic_cmpxchg(&info->in_test, 0, 1) != 0) {
		pr_err("test in progress...");
		return -1;
	}

	if (list_empty(&info->in_test_clients)) {
		ret = scnprintf(buf, PAGE_SIZE, "no registered clients, abort...\n");
		goto out;
	}

	client = list_first_entry(&info->in_test_clients, struct sscd_info_client, list);
	pdata = dev_get_platdata(&client->pdev->dev);
	if (!pdata)
		goto out;

	report_start = jiffies;
	rc = pdata->sscd_report(client->pdev, info->segs, info->seg_count,
				SSCD_FLAGS_ELFARM64HDR, "throughput test");
	report_end = jiffies;

	ret = scnprintf(buf, PAGE_SIZE, "throughput: client(%s) ret(%d) size %lu time %d (msec)\n",
			client->name, rc, info->seg_count * info->seg_size_in_pages * PAGE_SIZE,
			jiffies_to_msecs(report_end - report_start));

out:
	atomic_set(&info->in_test, 0);

	return ret;
}

static ssize_t test_client_register_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	int ret;
	u32 client_count;
	u32 i = 0;

	/* make sure single test is running */
	if (atomic_cmpxchg(&info->in_test, 0, 1) != 0) {
		pr_err("test in progress...");
		return -1;
	}

	ret = kstrtou32(buf, 10, &client_count);
	if (ret || !client_count)
		goto out;

	for (i = 0; i < client_count; i++) {
		struct sscd_info_client *client =
			kzalloc(sizeof(struct sscd_info_client), GFP_KERNEL);

		if (!client)
			continue;

		INIT_LIST_HEAD(&client->list);
		get_random_bytes(&client->id, sizeof(client->id));
		scnprintf(client->name, sizeof(client->name),
			  "test-rand%08x", client->id);

		client->pdev = sscd_register(client->name);
		ret = client->pdev ? count : -1;
		if (!client->pdev) {
			kfree(client);
			continue;
		}

		list_add_tail(&client->list, &info->in_test_clients);
	}
out:
	atomic_set(&info->in_test, 0);

	return ret;
}

static ssize_t _coredump(struct sscd_info *info, u64 flags)
{
	struct sscd_info_client *client;

	/* make sure single test is running */
	if (atomic_cmpxchg(&info->in_test, 0, 1) != 0) {
		pr_err("test in progress...");
		return -1;
	}

	list_for_each_entry(client, &info->in_test_clients, list) {
		struct sscd_platform_data *pdata = dev_get_platdata(&client->pdev->dev);

		pdata->sscd_report(client->pdev, info->segs, info->seg_count,
				   flags, "test_client_coredump");
	}

	atomic_set(&info->in_test, 0);

	return 0;
}

static ssize_t test_client_coredump_store(struct device *dev, struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	ssize_t status;

	status = _coredump(info, 0);

	return status ? status : count;
}

static ssize_t test_client_coredump_elf32_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	ssize_t status;

	status = _coredump(info, SSCD_FLAGS_ELFARM32HDR);

	return status ? status : count;
}

static ssize_t test_client_coredump_elf64_store(struct device *dev, struct device_attribute *attr,
						const char *buf, size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	ssize_t status;

	status = _coredump(info, SSCD_FLAGS_ELFARM64HDR);

	return status ? status : count;
}

static ssize_t test_client_info_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	struct sscd_info_client *client;
	int n = 0;

	/* make sure single test is running */
	if (atomic_cmpxchg(&info->in_test, 0, 1) != 0) {
		pr_err("test in progress...");
		return -1;
	}

	list_for_each_entry(client, &info->in_test_clients, list) {
		n += snprintf(buf + n, PAGE_SIZE - n, "%s\n", client->name);
	}

	atomic_set(&info->in_test, 0);

	return n;
}

/*
 * test_client_unregister_store() - unregister SSCD client
 * @arg "number" number of clients to unregister
 */
static ssize_t test_client_unregister_store(struct device *dev, struct device_attribute *attr,
					    const char *buf, size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	int ret;
	u32 client_count;
	u32 i = 0;

	/* make sure single test is running */
	if (atomic_cmpxchg(&info->in_test, 0, 1) != 0) {
		pr_err("test in progress...");
		return -1;
	}

	ret = kstrtou32(buf, 10, &client_count);
	if (ret || !client_count)
		goto out;

	for (i = 0; i < client_count; i++) {
		struct sscd_info_client *client;

		if (list_empty(&info->in_test_clients))
			break;

		client = list_last_entry(&info->in_test_clients,
					 struct sscd_info_client, list);
		list_del(&client->list);
		sscd_unregister(client->pdev);
		kfree(client);
	}
out:
	atomic_set(&info->in_test, 0);

	return count;
}

/*
 * configure number of threads
 */
static ssize_t cfg_thread_store(struct device *dev, struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	int ret;

	ret = kstrtou32(buf, 10, &info->thread_count);
	if (ret)
		return ret;

	info->thread_count = min_t(u32, info->thread_count, MAX_THREADS);

	return count;
}

static ssize_t cfg_thread_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);

	return snprintf(buf, PAGE_SIZE, "%d\n", info->thread_count);
}

/*
 * configure number of segments
 */
static ssize_t cfg_segment_count_store(struct device *dev, struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	int ret;

	ret = kstrtou16(buf, 10, &info->seg_count);
	if (ret)
		return ret;

	info->seg_count = min_t(u32, info->seg_count, MAX_SEGS);
	sscd_allocate_segments(info);

	return count;
}

static ssize_t cfg_segment_count_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);

	return snprintf(buf, PAGE_SIZE, "%d\n", info->seg_count);
}

/*
 * configure segment size
 */
static ssize_t cfg_segment_size_store(struct device *dev, struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	int ret;

	ret = kstrtou32(buf, 10, &info->seg_size_in_pages);
	if (ret)
		return ret;

	info->seg_size_in_pages = min_t(u32, info->seg_size_in_pages, MAX_SEG_SIZE);
	sscd_allocate_segments(info);

	return count;
}

static ssize_t cfg_segment_size_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);

	return snprintf(buf, PAGE_SIZE, "%d\n", info->seg_size_in_pages);
}

/*
 * configure number of segments
 */
static ssize_t cfg_repeat_store(struct device *dev, struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);
	int ret;

	ret = kstrtou32(buf, 10, &info->repeat_count);
	if (ret)
		return ret;

	info->repeat_count = min_t(u32, info->repeat_count, MAX_REPEATS);

	return count;
}

static ssize_t cfg_repeat_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct miscdevice *miscdev = dev_get_drvdata(dev);
	struct sscd_info *info = container_of(miscdev, struct sscd_info, miscdev);

	return snprintf(buf, PAGE_SIZE, "%d\n", info->repeat_count);
}

static DEVICE_ATTR_RW(cfg_repeat);
static DEVICE_ATTR_RW(cfg_segment_count);
static DEVICE_ATTR_RW(cfg_segment_size);
static DEVICE_ATTR_RW(cfg_thread);
static DEVICE_ATTR_WO(test_client_coredump);
static DEVICE_ATTR_WO(test_client_coredump_elf32);
static DEVICE_ATTR_WO(test_client_coredump_elf64);
static DEVICE_ATTR_RO(test_client_info);
static DEVICE_ATTR_WO(test_client_register);
static DEVICE_ATTR_WO(test_client_unregister);
static DEVICE_ATTR_WO(test_threads);
static DEVICE_ATTR_RO(test_throughput);

static struct attribute *sscd_test_attrs[] = {
	&dev_attr_cfg_repeat.attr,
	&dev_attr_cfg_segment_count.attr,
	&dev_attr_cfg_segment_size.attr,
	&dev_attr_cfg_thread.attr,
	&dev_attr_test_client_coredump.attr,
	&dev_attr_test_client_coredump_elf32.attr,
	&dev_attr_test_client_coredump_elf64.attr,
	&dev_attr_test_client_info.attr,
	&dev_attr_test_client_register.attr,
	&dev_attr_test_client_unregister.attr,
	&dev_attr_test_throughput.attr,
	&dev_attr_test_threads.attr,
	NULL,
};
ATTRIBUTE_GROUPS(sscd_test);

static int sscd_test_init(void)
{
	int ret = 0;
	struct sscd_info *info = &test_info;

	info->seg_count = DEFAULT_SEGS;
	info->seg_size_in_pages = 1;
	sscd_allocate_segments(info);

	info->name = kstrdup(DEVICE_NAME, GFP_KERNEL);

	info->miscdev.minor	= MISC_DYNAMIC_MINOR;
	info->miscdev.name	= info->name;
	info->miscdev.groups	= sscd_test_groups;
	ret = misc_register(&info->miscdev);
	if (ret) {
		dev_err(info->miscdev.this_device,
			"failed to register misc device %d\n", ret);
		return ret;
	}
	info->thread_count = MAX_THREADS;
	info->repeat_count = MAX_REPEATS;
	atomic_set(&info->in_test, 0);
	INIT_LIST_HEAD(&info->in_test_clients);

	dev_info(info->miscdev.this_device,
		 "registered '%s' %d:%d,\n", info->name,
		 MISC_MAJOR, info->miscdev.minor);

	return ret;
}

static void sscd_test_exit(void)
{
	struct sscd_info *info = &test_info;

	kfree(info->name);
	sscd_free_segments(info);
	misc_deregister(&info->miscdev);
}

module_init(sscd_test_init);
module_exit(sscd_test_exit);

MODULE_DESCRIPTION("Subsystem coredump test driver");
MODULE_AUTHOR("Oleg Matcovschi");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("0.1a");
