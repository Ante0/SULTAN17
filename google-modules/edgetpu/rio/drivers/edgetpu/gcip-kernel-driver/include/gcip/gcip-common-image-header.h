/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Common authenticated image format for Google SoCs
 *
 * Copyright (C) 2022 Google LLC
 */

#ifndef __GCIP_COMMON_IMAGE_HEADER_H__
#define __GCIP_COMMON_IMAGE_HEADER_H__

#include <linux/types.h>

#include "gcip-image-config.h"

#define GCIP_FW_LEGACY_HEADER_SIZE (0x1000)
#define GCIP_RDO_FORMAT_HEADER_SIZE GCIP_FW_LEGACY_HEADER_SIZE
#define GCIP_FW_PQ_ENABLED_HEADER_SIZE (0x5000)
#define GCIP_FW_MAX_HEADER_SIZE GCIP_FW_PQ_ENABLED_HEADER_SIZE

#define GCIP_RDO_IMAGE_FORMAT_VERSION (0x1)
#define GCIP_PQ_IMAGE_FORMAT_VERSION (0x2)

struct gcip_common_image_sub_header {
	uint32_t magic;
	uint32_t rollbackInfo;
	uint32_t delegate_rollback_info;
	uint32_t length;
	uint8_t flags[40];
	uint8_t body_hash[64];
	uint8_t chip_id[20];
	uint8_t auth_config[256];
	struct gcip_image_config image_config;
};

struct rdo_image_format_header {
	uint8_t root_sig[512];
	uint8_t root_pub[512];
	uint8_t delegate_pub[512];
	uint8_t delegate_policy[96];
	uint8_t delegate_sig[512];
	struct gcip_common_image_sub_header sub_header;
};

struct pq_enabled_image_format_header {
	uint8_t root_sig[512];
	uint8_t pq_root_sig[7856];
	uint8_t root_pub[512];
	uint8_t pq_root_pub[60];
	uint8_t delegate_pub[512];
	uint8_t pq_delegate_pub[60];
	uint8_t delegate_policy[96];
	uint8_t delegate_sig[512];
	uint8_t pq_delegate_sig[7856];
	struct gcip_common_image_sub_header sub_header;
};

struct gcip_common_image_header {
	uint32_t format_version;
	union {
		struct rdo_image_format_header rdo_header;
		struct pq_enabled_image_format_header pq_header;
	};
};

struct gcip_common_image_legacy_sub_header_common {
	uint32_t magic;
	uint32_t generation;
	uint32_t rollback_info;
	uint32_t length;
	uint8_t flags[16];
};

struct gcip_common_image_legacy_sub_header_gen1 {
	uint8_t body_hash[32];
	uint8_t chip_id[32];
	uint8_t auth_config[256];
	struct gcip_image_config image_config;
};

struct gcip_common_image_legacy_sub_header_gen2 {
	uint8_t body_hash[64];
	uint8_t chip_id[32];
	uint8_t auth_config[256];
	struct gcip_image_config image_config;
};

struct gcip_common_image_legacy_header {
	uint8_t sig[512];
	uint8_t pub[512];
	struct {
		struct gcip_common_image_legacy_sub_header_common common;
		union {
			struct gcip_common_image_legacy_sub_header_gen1 gen1;
			struct gcip_common_image_legacy_sub_header_gen2 gen2;
		};
	};
};

/*
 * Returns the image config field from a common image header or NULL if the header has an invalid
 * magic or generation identifier. Before calling this function, the IP driver should make sure
 * that the minimum size of the fw_data should be greater than GCIP_FW_MAX_HEADER_SIZE.
 */
static inline const struct gcip_image_config *
gcip_common_image_get_config_from_hdr(const void *fw_data, uint32_t magic)
{
	const struct gcip_common_image_header *hdr = fw_data;
	const struct gcip_common_image_legacy_header *legacy_hdr = fw_data;

	if (hdr->format_version == GCIP_RDO_IMAGE_FORMAT_VERSION &&
	    hdr->rdo_header.sub_header.magic == magic) {
		return &hdr->rdo_header.sub_header.image_config;
	} else if (hdr->format_version == GCIP_PQ_IMAGE_FORMAT_VERSION &&
		   hdr->pq_header.sub_header.magic == magic) {
		return &hdr->pq_header.sub_header.image_config;
	} else if (legacy_hdr->common.magic == magic) {
		switch (legacy_hdr->common.generation) {
		case 1:
			return &legacy_hdr->gen1.image_config;
		case 2:
			return &legacy_hdr->gen2.image_config;
		default:
			return NULL;
		}
	}

	return NULL;
}

/*
 * Checks if the magic field in the common image header matches the given magic value. Before
 * calling this function, the IP driver should make sure that the minimum size of the fw_data
 * should be greater than GCIP_FW_MAX_HEADER_SIZE.
 */
static inline bool gcip_common_image_check_magic(const void *fw_data, uint32_t magic)
{
	const struct gcip_image_config *image_config =
		gcip_common_image_get_config_from_hdr(fw_data, magic);

	return image_config != NULL;
}

/*
 * Returns the firmware image header size. Returns zero if the provided image header is invalid.
 * Before calling this function, the IP driver should make sure that the minimum size of the
 * fw_data should be greater than GCIP_FW_MAX_HEADER_SIZE.
 */
static inline size_t gcip_common_get_fw_header_size(const void *fw_data, uint32_t magic)
{
	const struct gcip_common_image_header *hdr = fw_data;
	const struct gcip_common_image_legacy_header *legacy_hdr = fw_data;

	if (hdr->format_version == GCIP_RDO_IMAGE_FORMAT_VERSION &&
	    hdr->rdo_header.sub_header.magic == magic)
		return GCIP_RDO_FORMAT_HEADER_SIZE;
	else if (hdr->format_version == GCIP_PQ_IMAGE_FORMAT_VERSION &&
		 hdr->pq_header.sub_header.magic == magic)
		return GCIP_FW_PQ_ENABLED_HEADER_SIZE;
	else if (legacy_hdr->common.magic == magic)
		return GCIP_FW_LEGACY_HEADER_SIZE;
	else
		return 0;
}

#endif /* __GCIP_COMMON_IMAGE_HEADER_H__ */
