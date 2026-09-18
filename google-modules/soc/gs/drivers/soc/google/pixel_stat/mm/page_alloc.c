// SPDX-License-Identifier: GPL-2.0-only
/* page_alloc.c
 *
 * Android Vendor Hook Support
 *
 * Copyright 2020 Google LLC
 */

#include <linux/mm.h>

DECLARE_PER_CPU(unsigned long, pgalloc_costly_order);
DECLARE_PER_CPU(unsigned long, pgalloc_atomic_order_zero);
DECLARE_PER_CPU(unsigned long, pgalloc_atomic_highorder_noncostly);
DECLARE_PER_CPU(unsigned long, pgalloc_atomic_highorder_costly);

/*****************************************************************************/
/*                       Modified Code Section                               */
/*****************************************************************************/
/*
 * This part of code is vendor hook functions, which modify or extend the
 * original functions.
 */
void vh_rmqueue_mod(void *data, struct zone *preferred_zone,
		struct zone *zone, unsigned int order, gfp_t gfp_flags,
		unsigned int alloc_flags, int migratetype)
{
	if (order > PAGE_ALLOC_COSTLY_ORDER)
		this_cpu_add(pgalloc_costly_order, 1 << order);
}

void rvh_try_alloc_pages_gfp_mod(void *data, struct page **page, unsigned int order,
				 gfp_t gfp, enum zone_type highest_zoneidx)
{
	if (unlikely((gfp & GFP_ATOMIC) == GFP_ATOMIC)) {
		if (order == 0)
			this_cpu_inc(pgalloc_atomic_order_zero);
		else if (order <= PAGE_ALLOC_COSTLY_ORDER)
			this_cpu_inc(pgalloc_atomic_highorder_noncostly);
		else
			this_cpu_inc(pgalloc_atomic_highorder_costly);
	}
}

