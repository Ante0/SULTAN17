// SPDX-License-Identifier: GPL-2.0-only
/* cma.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2021 Google LLC
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include <soc/google/meminfo.h>
#include "../../../../dma-buf/heaps/samsung/samsung-dma-heap.h"

/*****************************************************************************/
/*                       Modified Code Section                               */
/*****************************************************************************/
/*
 * This part of code is vendor hook functions, which modify or extend the
 * original functions.
 */

static LIST_HEAD(meminfo_list);
static DEFINE_MUTEX(meminfo_lock);

void rvh_meminfo_proc_show(void *data, struct seq_file *m)
{
	struct meminfo *meminfo;
	struct sysinfo i;
	int lru;
	long long misc_kb = 0;
	unsigned long pages[NR_LRU_LISTS];
	unsigned long sreclaimable, sunreclaim;
	unsigned long known_pages = 0;
	unsigned long others_kb = 0;
	unsigned long stack_kb = 0;
	char name[16];

	si_meminfo(&i);

        for (lru = LRU_BASE; lru < NR_LRU_LISTS; lru++)
                pages[lru] = global_node_page_state(NR_LRU_BASE + lru);

        sreclaimable = global_node_page_state_pages(NR_SLAB_RECLAIMABLE_B);
        sunreclaim = global_node_page_state_pages(NR_SLAB_UNRECLAIMABLE_B);

	mutex_lock(&meminfo_lock);
	list_for_each_entry(meminfo, &meminfo_list, list) {
		unsigned long size = meminfo->size_kb(meminfo->private);
		others_kb += size;
		snprintf(name, sizeof(name), "%s:", meminfo->name);
		seq_printf(m, "%-16s%8lu kB\n", name, size);
	}
	mutex_unlock(&meminfo_lock);

	known_pages = i.freeram + pages[LRU_ACTIVE_ANON] + pages[LRU_INACTIVE_ANON] +
		      pages[LRU_ACTIVE_FILE] + pages[LRU_INACTIVE_FILE] +
		      pages[LRU_UNEVICTABLE] + sreclaimable + sunreclaim +
		      global_node_page_state(NR_PAGETABLE) +
		      vmalloc_nr_pages() + pcpu_nr_pages();

	/*
	 * When CONFIG_VMAP_STACK is enabled, the kernel allocates thread stacks via calls to
	 * vmalloc(). So, the kernel thread stacks are already accounted for as part of the
	 * call to vmalloc_nr_pages() above. Therefore, they should not be considered again
	 * to avoid double counting.
	 */
	if (!IS_ENABLED(CONFIG_VMAP_STACK))
		stack_kb = global_node_page_state(NR_KERNEL_STACK_KB);

	misc_kb = (i.totalram << (PAGE_SHIFT - 10)) - ((known_pages << (PAGE_SHIFT - 10)) +
			        stack_kb + others_kb);

	seq_printf(m, "Misc:           %8lld kB\n", misc_kb < 0 ? 0 : misc_kb);
}

/*
 * Get memory information from registered items in meminfo_list,
 * return size would be kB, this API does not use any lock in
 * it's implementation. It is the caller's directive to ensure
 * concurrency safety.
 */
static unsigned long __get_meminfo_item_size(const char *name)
{
	struct meminfo *meminfo;
	unsigned long size = 0;
	/*
	 * We don't hold meminfo_lock intentionally here because
	 * this function is called in irq context on crash and
	 * it's unlikely to be race with unregister_meminfo.
	 */
	list_for_each_entry(meminfo, &meminfo_list, list) {
		if (!strcmp(name, meminfo->name)) {
			size = meminfo->size_kb(meminfo->private);
			break;
		}
	}
	return size;
}

/*
 * Get memory dump information of pixel device, should be careful
 * adding dump information since get_meminfo_item_size_no_lock
 * could be executed in hardirq context, so items for querying
 * should be atomic operation.
 */
void dump_pixel_meminfo(void)
{
	int i;
	unsigned long tmp_query = 0;
	static const char * const query_items[] = {"ION_heap", "ION_heap_pool", "Gpu"};

	for (i = 0; i < ARRAY_SIZE(query_items); i++) {
		/*
		 * Use the no-lock API as this is designed for hardlockup
		 * dumps and executes only once.
		 */
		tmp_query = __get_meminfo_item_size(query_items[i]);
		pr_info("%s %lu kB", query_items[i], tmp_query);
	}
}
EXPORT_SYMBOL_GPL(dump_pixel_meminfo);

void register_meminfo(struct meminfo *info)
{
	mutex_lock(&meminfo_lock);
	list_add(&info->list, &meminfo_list);
	mutex_unlock(&meminfo_lock);
}
EXPORT_SYMBOL_GPL(register_meminfo);

void unregister_meminfo(struct meminfo *info)
{
	mutex_lock(&meminfo_lock);
	list_del(&info->list);
	mutex_unlock(&meminfo_lock);
}
EXPORT_SYMBOL_GPL(unregister_meminfo);
