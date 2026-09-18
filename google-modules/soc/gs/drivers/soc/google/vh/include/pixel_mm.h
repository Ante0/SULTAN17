/* SPDX-License-Identifier: GPL-2.0 */

#ifndef PIXEL_MM_H
#define PIXEL_MM_H

int pixel_mm_filemap_sysfs(struct kobject *parent);

void vh_do_async_mmap_readahead(void *data, struct vm_fault *vmf,
				struct folio *folio, bool *skip);

void vh_do_sync_mmap_readahead(void *data, struct vm_fault *vmf,
			       bool *skip);

#endif	/* PIXEL_MM_H */
