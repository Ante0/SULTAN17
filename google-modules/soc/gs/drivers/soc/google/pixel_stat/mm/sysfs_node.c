// SPDX-License-Identifier: GPL-2.0-only
/* sysfs_node.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2020 Google LLC
 */

#include <linux/cpu.h>
#include <linux/kobject.h>
#include <linux/sysfs.h>
#include "cma.h"
#include "vmscan.h"
#include "compaction.h"

DEFINE_PER_CPU(unsigned long, pgalloc_costly_order);
DEFINE_PER_CPU(unsigned long, pgcache_miss);
DEFINE_PER_CPU(unsigned long, pgcache_hit);
DEFINE_PER_CPU(unsigned long, pgalloc_atomic_order_zero);
DEFINE_PER_CPU(unsigned long, pgalloc_atomic_highorder_noncostly);
DEFINE_PER_CPU(unsigned long, pgalloc_atomic_highorder_costly);

static ssize_t vmstat_show(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	int cpu;
	unsigned long pages = 0;
	unsigned long miss_count = 0;
	unsigned long hit_count = 0;
	unsigned long atomic_allocs = 0;
	unsigned long atomic_zero = 0;
	unsigned long atomic_noncostly = 0;
	unsigned long atomic_costly = 0;

	cpus_read_lock();
	for_each_online_cpu(cpu) {
		pages += per_cpu(pgalloc_costly_order, cpu);
		miss_count += per_cpu(pgcache_miss, cpu);
		hit_count += per_cpu(pgcache_hit, cpu);
		atomic_zero += per_cpu(pgalloc_atomic_order_zero, cpu);
		atomic_noncostly += per_cpu(pgalloc_atomic_highorder_noncostly, cpu);
		atomic_costly += per_cpu(pgalloc_atomic_highorder_costly, cpu);
	}
	cpus_read_unlock();

	atomic_allocs = atomic_zero + atomic_noncostly + atomic_costly;

	return sprintf(buf, "%s %lu\n%s %lu\n%s %lu\n%s %lu\n%s %lu\n%s %lu\n%s %lu\n",
			"pgalloc_costly_order", pages,
			"pgcache_miss", miss_count,
			"pgcache_hit", hit_count,
			"pgalloc_atomic", atomic_allocs,
			"pgalloc_atomic_order_zero", atomic_zero,
			"pgalloc_atomic_highorder_noncostly", atomic_noncostly,
			"pgalloc_atomic_highorder_costly", atomic_costly);
}

static struct kobj_attribute vmstat_attribute = __ATTR_RO(vmstat);

extern struct kobject *pixel_stat_kobj;
static struct kobject *pixel_stat_mm_kobj;
static struct attribute *attrs[] = {
	&vmstat_attribute.attr,
	NULL,
};
static struct attribute_group attr_group = {
	.attrs = attrs,
};

int pixel_mm_sysfs(void)
{
	int ret;

	if (pixel_stat_kobj)
		pixel_stat_mm_kobj = kobject_create_and_add("mm", pixel_stat_kobj);

	if (!pixel_stat_mm_kobj)
		return -ENOMEM;

	ret = sysfs_create_group(pixel_stat_mm_kobj, &attr_group);
	if (ret)
		goto put_mm_kobj;

	ret = create_vmscan_sysfs(pixel_stat_mm_kobj);
	if (ret)
		goto remove_stat_sysfs;

	ret = create_cma_sysfs(pixel_stat_mm_kobj);
	if (ret)
		goto remove_vmscan_sysfs;

#ifdef CONFIG_COMPACTION
	ret = compaction_sysfs(pixel_stat_mm_kobj);
	if (ret)
		goto remove_cma_sysfs;
#endif

	return ret;

remove_cma_sysfs:
	remove_cma_sysfs();
remove_vmscan_sysfs:
	remove_vmscan_sysfs();
remove_stat_sysfs:
	sysfs_remove_group(pixel_stat_mm_kobj, &attr_group);
put_mm_kobj:
	kobject_put(pixel_stat_mm_kobj);

	return ret;
}
