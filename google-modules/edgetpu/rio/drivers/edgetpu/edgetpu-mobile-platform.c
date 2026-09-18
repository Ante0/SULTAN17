// SPDX-License-Identifier: GPL-2.0-only
/*
 * Common platform interfaces for mobile TPU chips.
 *
 * Copyright (C) 2021-2025 Google LLC
 */

#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include <gcip/gcip-iommu.h>

#include "edgetpu-config.h"
#include "edgetpu-devfreq.h"
#include "edgetpu-dt-mailbox-adapter.h"
#include "edgetpu-firmware.h"
#include "edgetpu-ikv.h"
#include "edgetpu-internal.h"
#include "edgetpu-mmu.h"
#include "edgetpu-mobile-platform.h"
#include "edgetpu-pm.h"
#include "edgetpu-soc.h"
#include "edgetpu-telemetry.h"
#include "edgetpu-thermal.h"

static struct edgetpu_dev *edgetpu_debug_pointer;

static int edgetpu_platform_setup_fw_region(struct edgetpu_dev *etdev)
{
	struct device *dev = etdev->dev;
	struct resource r;
	struct device_node *np;
	int ret;

	np = of_parse_phandle(dev->of_node, "memory-region", 0);
	if (!np) {
		dev_err(dev, "No memory region for firmware");
		return -ENODEV;
	}

	ret = of_address_to_resource(np, 0, &r);
	of_node_put(np);
	if (ret) {
		dev_err(dev, "No memory address assigned to firmware region");
		return ret;
	}

	if (resource_size(&r) < EDGETPU_MAX_FW_LIMIT) {
		dev_err(dev, "Memory region for firmware too small (%#x bytes needed, got %#llx)",
			EDGETPU_MAX_FW_LIMIT, resource_size(&r));
		return -ENOSPC;
	}

	ret = edgetpu_firmware_setup_fw_carveout(etdev, r.start, resource_size(&r));
	if (ret)
		dev_err(dev, "setup firmware carveout failed: %d", ret);
	return ret;
}

static void edgetpu_platform_cleanup_fw_region(struct edgetpu_dev *etdev)
{
	edgetpu_firmware_cleanup_fw_carveout(etdev);
}

static inline const char *get_driver_commit(void)
{
#if IS_ENABLED(CONFIG_MODULE_SCMVERSION)
	return THIS_MODULE->scmversion ?: "scmversion missing";
#elif defined(GIT_REPO_TAG)
	return GIT_REPO_TAG;
#else
	return "Unknown";
#endif
}

static void edgetpu_platform_init_emulation(struct edgetpu_dev *etdev)
{
	const char *emul_flavor = NULL;

	if (of_property_read_string(etdev->dev->of_node, "emulation",
				    &emul_flavor))
		return;
	if (!strcmp(emul_flavor, "slow-tpu"))
		etdev->emulation_slow_tpu = true;
}

static int edgetpu_mobile_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct edgetpu_mobile_platform_dev *etmdev;
	struct edgetpu_dev *etdev;
	struct resource *r;
	struct edgetpu_mapped_resource regs;
	int ret;
	struct edgetpu_iface_params iface_params[] = {
		/* Default interface  */
		{ .name = NULL },
		/* Common name for embedded SoC devices */
		{ .name = "edgetpu-soc" },
		/* Limited interface for map/unmap only */
		{ .name = "edgetpu-limited", .limited = true },
	};

	/* Ensure any drivers relied upon have already probed. */
	ret = edgetpu_soc_check_supplier_devices(dev);
	if (ret)
		return ret;

	etmdev = devm_kzalloc(dev, sizeof(*etmdev), GFP_KERNEL);
	if (!etmdev)
		return -ENOMEM;
	mutex_init(&etmdev->tz_mailbox_lock);
	etdev = &etmdev->edgetpu_dev;
	etdev->commit_hash = get_driver_commit();
	platform_set_drvdata(pdev, etdev);
	etdev->dev = dev;
	etdev->num_cores = EDGETPU_NUM_CORES;
	etdev->num_telemetry_buffers = EDGETPU_NUM_CORES;
	etdev->log_buffer_size = EDGETPU_TELEMETRY_LOG_BUFFER_SIZE;
	etdev->trace_buffer_size = EDGETPU_TELEMETRY_TRACE_BUFFER_SIZE;
	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR_OR_NULL(r)) {
		dev_err(dev, "failed to get memory resource");
		return -ENODEV;
	}

	/* Initialize any potential offset between `regs` and the base of TPU_TOP first. */
	edgetpu_dt_mailbox_adapter_init_regs_offset_from_top(etdev);
	regs.phys = r->start;
	regs.size = resource_size(r);
	regs.mem = devm_ioremap_resource(dev, r);
	if (IS_ERR(regs.mem)) {
		ret = PTR_ERR(regs.mem);
		dev_err(dev, "failed to map TPU TOP registers: %d", ret);
		return ret;
	}

	/* Use 36-bit DMA mask for any default DMA API paths except coherent. */
	ret = dma_set_mask(&pdev->dev, DMA_BIT_MASK(36));
	if (ret)
		dev_warn(dev, "dma_set_mask returned %d\n", ret);
	ret = dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		dev_warn(dev, "dma_set_coherent_mask returned %d\n", ret);

	edgetpu_platform_init_emulation(etdev);
	ret = edgetpu_platform_setup_fw_region(etdev);
	if (ret) {
		dev_err(dev, "setup fw regions failed: %d", ret);
		return ret;
	}

	ret = edgetpu_device_add(etdev, &regs, ARRAY_SIZE(iface_params));
	if (ret) {
		dev_err(dev, "edgetpu device add failed: %d", ret);
		goto out_cleanup_fw_region;
	}

	ret = edgetpu_soc_setup_irqs(etdev);
	if (ret) {
		dev_err(dev, "IRQ setup failed: %d", ret);
		goto out_remove_device;
	}

	ret = edgetpu_firmware_create(etdev);
	if (ret) {
		dev_err(dev, "initialize firmware downloader failed: %d", ret);
		goto out_remove_device;
	}

	ret = edgetpu_thermal_create(etdev);
	if (ret)
		etdev_warn(etdev, "Failed to create thermal device: %d", ret);

	ret = edgetpu_devfreq_create(etdev);
	if (ret)
		etdev_warn(etdev, "Failed to create devfreq interface: %d", ret);

	edgetpu_soc_post_power_on_init(etdev);

	/* Turn the device off unless a client request is already received. */
	edgetpu_pm_shutdown(etdev, false);

	/*
	 * edgetpu_fs_add() should be the very last line of probe(), since once it succeeds,
	 * user-space can interact with the device through devtmpfs.
	 */
	ret = edgetpu_fs_add(etdev, iface_params, ARRAY_SIZE(iface_params));
	if (ret) {
		dev_err(etdev->dev, "%s: edgetpu_fs_add returns %d\n", etdev->dev_name, ret);
		goto out_destroy_devfreq;
	}

	dev_info(dev, "%s edgetpu initialized. Build: %s", etdev->dev_name, etdev->commit_hash);

	edgetpu_debug_pointer = etdev;

	return 0;

out_destroy_devfreq:
	edgetpu_devfreq_destroy(etdev);
	edgetpu_thermal_destroy(etdev);
	edgetpu_firmware_destroy(etdev);
out_remove_device:
	edgetpu_device_remove(etdev);
out_cleanup_fw_region:
	edgetpu_platform_cleanup_fw_region(etdev);
	return ret;
}

static void edgetpu_mobile_platform_remove(struct platform_device *pdev)
{
	struct edgetpu_dev *etdev = platform_get_drvdata(pdev);

	edgetpu_fs_remove(etdev);
	edgetpu_devfreq_destroy(etdev);
	edgetpu_thermal_destroy(etdev);
	edgetpu_firmware_destroy(etdev);
	edgetpu_device_remove(etdev);
	edgetpu_platform_cleanup_fw_region(etdev);

	edgetpu_debug_pointer = NULL;
}
