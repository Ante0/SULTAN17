// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 - 2024 Novatek, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */
#ifndef _NT36XXX_EXT_USI_H_
#define _NT36XXX_EXT_USI_H_

int32_t nvt_get_usi_data_diag(uint8_t *beacon, uint8_t *response);

/* Flags for the responses of the USI read commands */
enum {
	USI_GID_FLAG            = 1U << 0,
	USI_BATTERY_FLAG        = 1U << 1,
	USI_CAPABILITY_FLAG     = 1U << 2,
	USI_FW_VERSION_FLAG     = 1U << 3,
	USI_CRC_FAIL_FLAG       = 1U << 4,
	USI_FAST_PAIR_FLAG      = 1U << 5,
	USI_NORMAL_PAIR_FLAG    = 1U << 6,
	USI_RESERVED1_FLAG      = 1U << 7,
	USI_RESERVED2_FLAG      = 1U << 8,
	USI_RESERVED3_FLAG      = 1U << 9,
	USI_RESERVED4_FLAG      = 1U << 10,
	USI_RESERVED5_FLAG      = 1U << 11,
	USI_HASH_ID_FLAG        = 1U << 12,
	USI_SESSION_ID_FLAG     = 1U << 13,
	USI_FREQ_SEED_FLAG      = 1U << 14,
	USI_INFO_FLAG           = 1U << 15,
};

enum {
	USI_GID_SIZE            = 12,
	USI_BATTERY_SIZE        = 2,
	USI_FW_VERSION_SIZE     = 2,
	USI_CAPABILITY_SIZE     = 12,
	USI_CRC_FAIL_SIZE       = 2,
	USI_FAST_PAIR_SIZE      = 2,
	USI_NORMAL_PAIR_SIZE    = 2,
	USI_PEN_MODEL_IDX_SIZE  = 1,
	USI_RESERVED1_SIZE      = 21,
	USI_HASH_ID_SIZE        = 2,
	USI_SESSION_ID_SIZE     = 2,
	USI_FREQ_SEED_SIZE      = 1,
	USI_RESERVED2_SIZE      = 1,
	USI_INFO_FLAG_SIZE      = 2,
};

/* location of the data in the response buffer */
enum {
	USI_GID_OFFSET           = 1,
	USI_BATTERY_OFFSET       = USI_GID_OFFSET + USI_GID_SIZE,
	USI_FW_VERSION_OFFSET    = USI_BATTERY_OFFSET + USI_BATTERY_SIZE,
	USI_CAPABILITY_OFFSET    = USI_FW_VERSION_OFFSET + USI_FW_VERSION_SIZE,
	USI_CRC_FAIL_OFFSET      = USI_CAPABILITY_OFFSET + USI_CAPABILITY_SIZE,
	USI_FAST_PAIR_OFFSET     = USI_CRC_FAIL_OFFSET + USI_CRC_FAIL_SIZE,
	USI_NORMAL_PAIR_OFFSET   = USI_FAST_PAIR_OFFSET + USI_FAST_PAIR_SIZE,
	USI_PEN_MODEL_IDX_OFFSET = USI_NORMAL_PAIR_OFFSET + USI_NORMAL_PAIR_SIZE,
	USI_RESERVED1_OFFSET     = USI_PEN_MODEL_IDX_OFFSET + USI_PEN_MODEL_IDX_SIZE,
	USI_HASH_ID_OFFSET       = USI_RESERVED1_OFFSET + USI_RESERVED1_SIZE,
	USI_SESSION_ID_OFFSET    = USI_HASH_ID_OFFSET + USI_HASH_ID_SIZE,
	USI_FREQ_SEED_OFFSET     = USI_SESSION_ID_OFFSET + USI_SESSION_ID_SIZE,
	USI_RESERVED2_OFFSET     = USI_FREQ_SEED_OFFSET + USI_FREQ_SEED_SIZE,
	USI_INFO_FLAG_OFFSET     = USI_RESERVED2_OFFSET + USI_RESERVED2_SIZE,
};

#endif  // _NT36XXX_EXT_USI_H_
