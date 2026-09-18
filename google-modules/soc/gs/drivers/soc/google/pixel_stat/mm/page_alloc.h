/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_PIXEL_PAGE_ALLOC_H__
#define __MM_PIXEL_PAGE_ALLOC_H__

void rvh_try_alloc_pages_gfp_mod(void *data, struct page **page, unsigned int order,
				 gfp_t gfp, enum zone_type highest_zoneidx);

#endif
