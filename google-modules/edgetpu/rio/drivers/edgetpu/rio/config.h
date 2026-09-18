/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Include all configuration files for Rio.
 *
 * Copyright (C) 2021-2025 Google LLC
 */

#ifndef __RIO_CONFIG_H__
#define __RIO_CONFIG_H__

#define DRIVER_NAME "rio"

#define EDGETPU_NUM_CORES 2

#define EDGETPU_NUM_SSMTS 2

#define EDGETPU_MAX_STREAM_ID 64

/* Max number of PASIDs that the IOMMU supports simultaneously */
#define EDGETPU_NUM_PASIDS 16
/* Max number of virtual context IDs that can be allocated for one device. */
#define EDGETPU_NUM_VCIDS 16

/* Number of TPU clusters for metrics handling. */
#define EDGETPU_TPU_CLUSTER_COUNT 3

/*
 * TZ Mailbox ID for secure workloads.  Must match firmware kTzMailboxId value for the chip,
 * but note firmware uses a zero-based index vs. kernel passing a one-based value here.
 * For this chip the value is not an actual mailbox index, but just an otherwise unused value
 * agreed upon with firmware for this purpose.
 */
#define EDGETPU_TZ_MAILBOX_ID 31

/* A special client ID for secure workloads pre-agreed with firmware (kTzRealmId). */
#define EDGETPU_EXT_TZ_CONTEXT_ID 0x40000000

#define EDGETPU_MMU_GRANULARITY_IS_PAGE 0

/* Rio platforms only support GSA enabled, cannot check CONFIG_GSA on 6.1 */
#define EDGETPU_HAS_GSA 1

#include "config-csrs.h"
#include "config-mailbox.h"
#include "config-pwr-state.h"

#endif /* __RIO_CONFIG_H__ */
