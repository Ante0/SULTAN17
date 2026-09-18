// SPDX-License-Identifier: GPL-2.0-only
/*
 * Compatibility layer to find mailboxes in the device tree and allocate edgetpu_mailboxes for them.
 *
 * Copyright (C) 2025 Google LLC
 */

#include <linux/of_platform.h>
#include <linux/platform_device.h>

#include "edgetpu-config.h"
#include "edgetpu-dt-mailbox-adapter.h"
#include "edgetpu-mailbox.h"

/* Mailbox index for kernel control interface */
#define KERNEL_MAILBOX_INDEX 0

/* Mailbox index for in-kernel virtual inference interface, if enabled */
#define IKV_MAILBOX_INDEX 1

/* Mailbox index for Inter-IP Fence signaling mailbox, if enabled */
#define IIF_MAILBOX_INDEX 2

static const char *const dt_reg_names[] = { "kci_mailbox", "vii_mailbox", "iif_mailbox" };

void edgetpu_dt_mailbox_adapter_init_regs_offset_from_top(struct edgetpu_dev *etdev)
{
	struct platform_device *pdev = to_platform_device(etdev->dev);
	struct resource *reg_resource =
		platform_get_resource_byname(pdev, IORESOURCE_MEM, dt_reg_names[0]);

	/*
	 * If mailboxes are specified in the device-tree, etdev->regs starts at the first ext
	 * mailbox instead of the start of TPU_TOP. CSR accesses will need to subtract the offset
	 * of the first ext mailbox to get the address it's actually trying to reach.
	 */
	if (reg_resource)
		etdev->regs_offset_from_top =
			edgetpu_mailbox_get_context_csr_base(EDGETPU_EXT_MAILBOX_START);
	else
		etdev->regs_offset_from_top = 0;
}

/*
 * Helper function to allocate and initialize a specific mailbox based on its index.
 */
static struct edgetpu_mailbox *dedicated_mailbox(struct edgetpu_dev *etdev, uint idx)
{
	static void __iomem *csr_bases[] = { 0, 0, 0 };
	static int irqs[] = { 0, 0, 0 };
	struct platform_device *pdev;
	struct resource *reg_resource;
	void __iomem *csr_base;
	int irq;
	int ret = 0;

	if (csr_bases[idx]) {
		csr_base = csr_bases[idx];
		irq = irqs[idx];
		goto alloc_mailbox;
	}

	pdev = to_platform_device(etdev->dev);

	reg_resource = platform_get_resource_byname(pdev, IORESOURCE_MEM, dt_reg_names[idx]);
	if (reg_resource) {
		csr_base = devm_ioremap_resource(etdev->dev, reg_resource);
		if (IS_ERR(csr_base)) {
			ret = PTR_ERR(csr_base);
			etdev_err(etdev, "failed to map %s: %d", dt_reg_names[idx], ret);
			return ERR_PTR(ret);
		}

		irq = platform_get_irq_byname(pdev, dt_reg_names[idx]);

		/*
		 * Since the ioremapping of the registers matches the lifecycle of the driver, they
		 * cannot be remapped if the mailbox is ever released and re-initialized. Instead,
		 * save the base kernel virtual address of the mapping and the IRQ number so they
		 * can be re-used if necessary.
		 */
		csr_bases[idx] = csr_base;
		irqs[idx] = irq;
	} else {
		/*
		 * If no reg entry for the mailbox is found, assume the platform is still including
		 * all mailboxes in the TPU_TOP reg entry.
		 */
		csr_base = etdev->regs.mem + edgetpu_mailbox_get_context_csr_base(idx);
		irq = platform_get_irq(to_platform_device(etdev->dev), idx);
	}

	if (!irq) {
		etdev_err(etdev, "no irq found for mailbox %u\n", idx);
		return ERR_PTR(-EINVAL);
	}

alloc_mailbox:
	return edgetpu_mailbox_alloc(etdev, csr_base, irq, idx);
}

struct edgetpu_mailbox *edgetpu_mailbox_kci(struct edgetpu_dev *etdev)
{
	return dedicated_mailbox(etdev, KERNEL_MAILBOX_INDEX);
}

struct edgetpu_mailbox *edgetpu_mailbox_ikv(struct edgetpu_dev *etdev)
{
	return dedicated_mailbox(etdev, IKV_MAILBOX_INDEX);
}

struct edgetpu_mailbox *edgetpu_mailbox_iif(struct edgetpu_dev *etdev)
{
	struct edgetpu_mailbox *mailbox;

	if (!EDGETPU_USE_IIF_MAILBOX)
		return ERR_PTR(-ENXIO);

	mailbox = dedicated_mailbox(etdev, IIF_MAILBOX_INDEX);

	/*
	 * -ENXIO is reserved only for indicating that a platform does not support an IIF
	 * mailbox. If some internal function call returned -ENXIO, it is overwritten here
	 * and a warning is logged for debugging the internal failure.
	 */
	if (PTR_ERR(mailbox) == -ENXIO) {
		etdev_warn(etdev, "overwriting IIF mailbox creation error -ENXIO with -ENODEV.");
		return ERR_PTR(-ENODEV);
	}

	return mailbox;
}

void __iomem *edgetpu_mailbox_get_ext_csr_base(struct edgetpu_dev *etdev, uint idx)
{
	if (idx < EDGETPU_EXT_MAILBOX_START ||
	    idx >= (EDGETPU_EXT_MAILBOX_START + EDGETPU_NUM_EXT_MAILBOXES))
		return ERR_PTR(-EINVAL);

	/*
	 * If dedicated mailboxes are in the device-tree, then etdev->regs starts at the external
	 * mailboxes, rather than the TPU TOP, and etdev->regs_offset_from_top is set to that
	 * offset.
	 */
	return etdev->regs.mem - etdev->regs_offset_from_top +
		edgetpu_mailbox_get_context_csr_base(idx);
}
