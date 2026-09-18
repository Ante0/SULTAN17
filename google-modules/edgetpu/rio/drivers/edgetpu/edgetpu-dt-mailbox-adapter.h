/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Compatibility layer to find mailboxes in the device tree and allocate edgetpu_mailboxes for them.
 *
 * Copyright (C) 2025 Google LLC
 */

#ifndef __EDGETPU_DT_MAILBOX_ADAPTER_H__
#define __EDGETPU_DT_MAILBOX_ADAPTER_H__

#include "edgetpu-internal.h"
#include "edgetpu-mailbox.h"

/*
 * Initialize @etdev's regs_offset_from_top field based on the device-tree.
 *
 * If the TPU's device-tree entry does not start its regs property at the beginning of TPU_TOP,
 * this function will set @etdev->regs_offset_from_top so that the edgetpu_dev_read/write
 * interfaces can still be called with offsets based on TPU_TOP.
 */
void edgetpu_dt_mailbox_adapter_init_regs_offset_from_top(struct edgetpu_dev *etdev);

/*
 * Allocates and initializes the mailbox for KCI.
 * The caller must release the mailbox with `edgetpu_mailbox_release()` when done with it.
 */
struct edgetpu_mailbox *edgetpu_mailbox_kci(struct edgetpu_dev *etdev);

/*
 * Allocates and initializes the mailbox for VII.
 * The caller must release the mailbox with `edgetpu_mailbox_release()` when done with it.
 */
struct edgetpu_mailbox *edgetpu_mailbox_ikv(struct edgetpu_dev *etdev);

/*
 * Allocates and initializes the mailbox for IIF.
 * The caller must release the mailbox with `edgetpu_mailbox_release()` when done with it.
 *
 * Returns -ENXIO if and only if the current platform does not support an IIF mailbox.
 * If an internal function returns -ENXIO, this function will log an error and overwrite that
 * value with -ENODEV.
 */
struct edgetpu_mailbox *edgetpu_mailbox_iif(struct edgetpu_dev *etdev);

/*
 * Returns a pointer to the base address of the CSRs associated with the @idx'th external mailbox.
 * @idx is index of the mailbox to fetch, relative to the KCI mailbox which is index 0.
 */
void __iomem *edgetpu_mailbox_get_ext_csr_base(struct edgetpu_dev *etdev, uint idx);

#endif /* __EDGETPU_DT_MAILBOX_ADAPTER_H__ */
