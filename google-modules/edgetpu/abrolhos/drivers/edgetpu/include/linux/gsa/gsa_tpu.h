/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fallback header for systems without GSA support.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef __LINUX_GSA_TPU_H
#define __LINUX_GSA_TPU_H

/* This header file is a stub and shouldn't be used when compiling the driver
 * in the Pixel tree. Instead, we need to be using the header in the kernel
 * tree or SoC modules project, e.g private/google-modules/soc/gs. To avoid
 * instroducing conflicts when merging updates to this driver or re-introducing
 * this header from a merge, I'm insttead adding this build time error to make
 * sure we catch any changes that get this header included again.
 */
#error "We should not be using this header file!"

#include <linux/device.h>
#include <linux/types.h>

#if IS_ENABLED(CONFIG_EDGETPU_TEST)

int gsa_load_tpu_fw_image(struct device *gsa, dma_addr_t img_meta, phys_addr_t img_body);

#else /* IS_ENABLED(CONFIG_EDGETPU_TEST) */

static inline int gsa_load_tpu_fw_image(struct device *gsa, dma_addr_t img_meta,
					phys_addr_t img_body)
{
	return 0;
}

#endif /* IS_ENABLED(CONFIG_EDGETPU_TEST) */

static inline int gsa_unload_tpu_fw_image(struct device *gsa)
{
	return 0;
}

enum gsa_tpu_state {
	GSA_TPU_STATE_INACTIVE = 0,
	GSA_TPU_STATE_LOADED,
	GSA_TPU_STATE_RUNNING,
	GSA_TPU_STATE_SUSPENDED,
};

enum gsa_tpu_cmd {
	GSA_TPU_GET_STATE = 0,
	GSA_TPU_START,
	GSA_TPU_SUSPEND,
	GSA_TPU_RESUME,
	GSA_TPU_SHUTDOWN,
};

static inline int gsa_send_tpu_cmd(struct device *gsa, enum gsa_tpu_cmd cmd)
{
	return 0;
}

#endif /* __LINUX_GSA_TPU_H */
