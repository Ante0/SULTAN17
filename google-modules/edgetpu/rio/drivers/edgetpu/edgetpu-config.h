/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Defines chipset dependent configuration.
 *
 * Copyright (C) 2019-2025 Google LLC
 */

#ifndef __EDGETPU_CONFIG_H__
#define __EDGETPU_CONFIG_H__


#if IS_ENABLED(CONFIG_RIO)

#include "rio/config.h"

#else /* unknown */

#error "Unknown EdgeTPU config"

#endif /* unknown */

#define EDGETPU_DEFAULT_FIRMWARE_NAME "google/edgetpu-" DRIVER_NAME ".fw"
#define EDGETPU_TEST_FIRMWARE_NAME "google/edgetpu-" DRIVER_NAME "-test.fw"

#ifndef EDGETPU_NUM_CORES
#define EDGETPU_NUM_CORES 1
#endif

#ifndef EDGETPU_MAX_TELEMETRY_BUFFERS
#define EDGETPU_MAX_TELEMETRY_BUFFERS EDGETPU_NUM_CORES
#endif

/* By default IOMMU domains can be modified while detached from a mailbox.*/
#ifndef HAS_DETACHABLE_IOMMU_DOMAINS
#define HAS_DETACHABLE_IOMMU_DOMAINS	1
#endif

#ifndef EDGETPU_HAS_GSA
#if IS_ENABLED(CONFIG_GSA) || IS_ENABLED(CONFIG_EDGETPU_TEST)
#define EDGETPU_HAS_GSA 1
#else
#define EDGETPU_HAS_GSA 0
#endif
#endif /* !defined(EDGETPU_HAS_GSA) */

#ifndef EDGETPU_ALLOW_NONSECURE_FW
#define EDGETPU_ALLOW_NONSECURE_FW 0
#endif

#ifndef EDGETPU_HAS_PQ_FW_AUTH
#define EDGETPU_HAS_PQ_FW_AUTH 0
#endif

/*
 * "Always on" can be enabled in a chip config.h file using this define; see comments for
 * field @always_on in edgetpu-pm.h. * The default is to not force "always on".
 */
#ifndef EDGETPU_FEATURE_ALWAYS_ON
#define EDGETPU_FEATURE_ALWAYS_ON 0
#endif

#ifndef EDGETPU_USE_LITEBUF_VII
#define EDGETPU_USE_LITEBUF_VII 0
#endif

#ifndef EDGETPU_HAS_FW_DEBUG
#define EDGETPU_HAS_FW_DEBUG 0
#endif

#ifndef EDGETPU_REPORT_PAGE_FAULT_ERRORS
#define EDGETPU_REPORT_PAGE_FAULT_ERRORS 0
#endif

#ifndef EDGETPU_USE_IIF_MAILBOX
#define EDGETPU_USE_IIF_MAILBOX 0
#endif

#ifndef EDGETPU_NUM_VII_CREDITS_PER_CLIENT
#define EDGETPU_NUM_VII_CREDITS_PER_CLIENT 8
#endif

#ifndef EDGETPU_CPU_CACHE_LINE_SIZE
#define EDGETPU_CPU_CACHE_LINE_SIZE 64
#endif

#ifndef EDGETPU_USE_HW_WDT
#define EDGETPU_USE_HW_WDT 0
#endif

#ifndef EDGETPU_POWER_ISLAND_COUNT
#define EDGETPU_POWER_ISLAND_COUNT 0
#endif

/* Whether the IOMMU granularity is same as PAGE_SIZE. */
#ifndef EDGETPU_MMU_GRANULARITY_IS_PAGE
#define EDGETPU_MMU_GRANULARITY_IS_PAGE 1
#endif

#endif /* __EDGETPU_CONFIG_H__ */
