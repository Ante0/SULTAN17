// SPDX-License-Identifier: GPL-2.0-only
/*
 * Edge TPU ML accelerator telemetry: logging and tracing.
 *
 * Copyright (C) 2019-2025 Google LLC
 */

#include <linux/minmax.h>
#include <linux/mm_types.h>
#include <linux/types.h>

#include <gcip/gcip-alloc-helper.h>
#include <gcip/gcip-memory.h>
#include <gcip/gcip-telemetry.h>

#include "edgetpu-internal.h"
#include "edgetpu-iremap-pool.h"
#include "edgetpu-mmu.h"
#include "edgetpu-telemetry.h"
#include "edgetpu.h"

static void set_telemetry_mem(struct edgetpu_dev *etdev)
{
	struct gcip_telemetry *tel_log = etdev->telemetry_log;
	struct gcip_telemetry *tel_trace = etdev->telemetry_trace;
	int i, offset = 0;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		tel_log[i].memory.virt_addr = edgetpu_firmware_shared_data_vaddr(etdev) + offset;
		tel_log[i].memory.dma_addr = edgetpu_firmware_shared_data_daddr(etdev) + offset;
		tel_log[i].memory.host_addr = 0;
		tel_log[i].memory.phys_addr = edgetpu_firmware_shared_data_paddr(etdev) + offset;
		tel_log[i].memory.sgt = NULL;
		tel_log[i].memory.size = etdev->log_buffer_size;
		offset += etdev->log_buffer_size;
		tel_trace[i].memory.virt_addr = edgetpu_firmware_shared_data_vaddr(etdev) + offset;
		tel_trace[i].memory.dma_addr = edgetpu_firmware_shared_data_daddr(etdev) + offset;
		tel_trace[i].memory.host_addr = 0;
		tel_trace[i].memory.phys_addr = edgetpu_firmware_shared_data_paddr(etdev) + offset;
		tel_trace[i].memory.sgt = NULL;
		tel_trace[i].memory.size = etdev->trace_buffer_size;
		offset += etdev->trace_buffer_size;
	}
}

int edgetpu_telemetry_init(struct edgetpu_dev *etdev)
{
	int ret, i;
	size_t sz;

	sz = sizeof(*etdev->telemetry_log) * etdev->num_telemetry_buffers;
	etdev->telemetry_log = devm_krealloc(etdev->dev, etdev->telemetry_log, sz,
					     __GFP_ZERO | GFP_KERNEL);
	if (!etdev->telemetry_log)
		return -ENOMEM;

	sz = sizeof(*etdev->telemetry_trace) * etdev->num_telemetry_buffers;
	etdev->telemetry_trace = devm_krealloc(etdev->dev, etdev->telemetry_trace, sz,
					       __GFP_ZERO | GFP_KERNEL);
	if (!etdev->telemetry_trace)
		return -ENOMEM;

	set_telemetry_mem(etdev);

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		ret = gcip_telemetry_init(&etdev->telemetry_log[i], GCIP_TELEMETRY_TYPE_LOG,
					  etdev->dev);
		if (ret)
			break;

		ret = gcip_telemetry_init(&etdev->telemetry_trace[i], GCIP_TELEMETRY_TYPE_TRACE,
					  etdev->dev);
		if (ret) {
			gcip_telemetry_exit(&etdev->telemetry_log[i]);
			break;
		}
	}

	if (ret)
		while (i--) {
			gcip_telemetry_exit(&etdev->telemetry_trace[i]);
			gcip_telemetry_exit(&etdev->telemetry_log[i]);
		}

	return ret;
}

static void edgetpu_telemetry_hwtrace_setup_fw(struct edgetpu_dev *etdev)
{
	int ret;

	if (!edgetpu_telemetry_mapped(&etdev->telemetry_hwtrace))
		return;

	ret = gcip_telemetry_kci(&etdev->telemetry_hwtrace, edgetpu_kci_map_hwtrace_buffer,
				 etdev->etkci->kci);
	if (ret)
		etdev_warn(etdev, "failed to send hwtrace setup info to fw: %d\n", ret);
}

int edgetpu_telemetry_hwtrace_init(struct edgetpu_dev *etdev, size_t buffer_size)
{
	int ret;

	etdev->telemetry_hwtrace.memory.sgt =
		gcip_alloc_noncontiguous(etdev->dev, buffer_size, GFP_KERNEL);
	if (!etdev->telemetry_hwtrace.memory.sgt)
		return -ENOMEM;

        ret = edgetpu_mmu_map_iova_sgt(etdev, EDGETPU_TELEMETRY_HWTRACE_IOVA,
				       etdev->telemetry_hwtrace.memory.sgt, DMA_BIDIRECTIONAL,
				       EDGETPU_MMU_COHERENT, edgetpu_mmu_default_domain(etdev));
        if (ret)
		goto err_free_sgt;

	etdev->telemetry_hwtrace.memory.virt_addr =
		gcip_noncontiguous_sgt_to_mem(etdev->telemetry_hwtrace.memory.sgt);
	etdev->telemetry_hwtrace.memory.dma_addr =
		sg_dma_address(etdev->telemetry_hwtrace.memory.sgt->sgl);
	etdev->telemetry_hwtrace.memory.host_addr = 0;
	etdev->telemetry_hwtrace.memory.phys_addr = 0; /* Not used for HWTRACE */
	etdev->telemetry_hwtrace.memory.size = buffer_size;
	ret = gcip_telemetry_init(&etdev->telemetry_hwtrace, GCIP_TELEMETRY_TYPE_HWTRACE,
				  etdev->dev);
	if (ret)
		goto err_unmap_sgt;

	edgetpu_telemetry_hwtrace_setup_fw(etdev);
	return ret;

err_unmap_sgt:
	edgetpu_mmu_unmap_iova_sgt(etdev,etdev->telemetry_hwtrace.memory.dma_addr,
				   etdev->telemetry_hwtrace.memory.sgt,
				   DMA_BIDIRECTIONAL, edgetpu_mmu_default_domain(etdev));
	etdev->telemetry_hwtrace.memory.dma_addr = 0;
err_free_sgt:
	gcip_free_noncontiguous(etdev->telemetry_hwtrace.memory.sgt);
	etdev->telemetry_hwtrace.memory.sgt = NULL;
	return ret;
}

void edgetpu_telemetry_exit(struct edgetpu_dev *etdev)
{
	int i;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		gcip_telemetry_exit(&etdev->telemetry_trace[i]);
		gcip_telemetry_exit(&etdev->telemetry_log[i]);
	}

	if (edgetpu_telemetry_mapped(&etdev->telemetry_hwtrace)) {
		gcip_telemetry_exit(&etdev->telemetry_hwtrace);
		edgetpu_mmu_unmap_iova_sgt(etdev, etdev->telemetry_hwtrace.memory.dma_addr,
					   etdev->telemetry_hwtrace.memory.sgt,
					   DMA_BIDIRECTIONAL, edgetpu_mmu_default_domain(etdev));
		etdev->telemetry_hwtrace.memory.dma_addr = 0;
		gcip_free_noncontiguous(etdev->telemetry_hwtrace.memory.sgt);
		etdev->telemetry_hwtrace.memory.sgt = NULL;
	}
}

int edgetpu_telemetry_kci(struct edgetpu_dev *etdev)
{
	int ret;

	/* Core 0 will notify other cores. */
	ret = gcip_telemetry_kci(&etdev->telemetry_log[0], edgetpu_kci_map_log_buffer,
				 etdev->etkci->kci);
	if (ret)
		return ret;

	ret = gcip_telemetry_kci(&etdev->telemetry_trace[0], edgetpu_kci_map_trace_buffer,
				 etdev->etkci->kci);
	if (ret)
		return ret;

	edgetpu_telemetry_hwtrace_setup_fw(etdev);
	return 0;
}

int edgetpu_telemetry_set_event(struct edgetpu_dev *etdev, struct gcip_telemetry *tel, u32 eventfd)
{
	int ret;
	int i;

	if (tel->type == GCIP_TELEMETRY_TYPE_HWTRACE)
		return gcip_telemetry_set_event(tel, eventfd);

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		ret = gcip_telemetry_set_event(&tel[i], eventfd);
		if (ret) {
			edgetpu_telemetry_unset_event(etdev, tel);
			return ret;
		}
	}

	return 0;
}

void edgetpu_telemetry_unset_event(struct edgetpu_dev *etdev, struct gcip_telemetry *tel)
{
	int i;

	if (tel->type == GCIP_TELEMETRY_TYPE_HWTRACE) {
		gcip_telemetry_unset_event(tel);
		return;
	}

	for (i = 0; i < etdev->num_telemetry_buffers; i++)
		gcip_telemetry_unset_event(&tel[i]);
}

void edgetpu_telemetry_irq_handler(struct edgetpu_dev *etdev)
{
	int i;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		gcip_telemetry_irq_handler(&etdev->telemetry_log[i]);
		gcip_telemetry_irq_handler(&etdev->telemetry_trace[i]);
	}

	if (edgetpu_telemetry_mapped(&etdev->telemetry_hwtrace))
		gcip_telemetry_irq_handler(&etdev->telemetry_hwtrace);
}

static void telemetry_mappings_show(struct gcip_telemetry *tel, struct seq_file *s)
{
	seq_printf(s, "  %pad %lu %s %#llx\n", &tel->memory.dma_addr,
		   DIV_ROUND_UP(tel->memory.size, PAGE_SIZE), tel->name, tel->memory.host_addr);
}

void edgetpu_telemetry_mappings_show(struct edgetpu_dev *etdev, struct seq_file *s)
{
	int i;

	for (i = 0; i < etdev->num_telemetry_buffers; i++) {
		telemetry_mappings_show(&etdev->telemetry_log[i], s);
		telemetry_mappings_show(&etdev->telemetry_trace[i], s);
	}

	if (edgetpu_telemetry_mapped(&etdev->telemetry_hwtrace))
		telemetry_mappings_show(&etdev->telemetry_hwtrace, s);
}

int edgetpu_mmap_telemetry_buffer(struct edgetpu_dev *etdev, struct gcip_telemetry *tel,
				  struct vm_area_struct *vma)
{
	int ret;

	ret = gcip_telemetry_mmap(tel, vma);
	if (ret)
		etdev_err(etdev, "Failed to mmap telemetry buffer: type=%d, ret=%d",
			  tel->type, ret);
	return ret;
}
