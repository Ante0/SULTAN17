/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Fallback header for systems without Exynos ACPM support.
 *
 * Copyright (C) 2021 Google, Inc.
 */

#ifndef __ACPM_DVFS_H__
#define __ACPM_DVFS_H__

/* This header file is a stub and shouldn't be used when compiling the driver
 * in the Pixel tree. Instead, we need to be using the header in the kernel
 * tree or SoC modules project, e.g private/google-modules/soc/gs. To avoid
 * instroducing conflicts when merging updates to this driver or re-introducing
 * this header from a merge, I'm insttead adding this build time error to make
 * sure we catch any changes that get this header included again.
 */
#error "We should not be using this header file!"

static inline int exynos_acpm_set_init_freq(unsigned int dfs_id, unsigned long freq)
{
	return 0;
}

static inline int exynos_acpm_set_policy(unsigned int id, unsigned long policy)
{
	return 0;
}

#if IS_ENABLED(CONFIG_EDGETPU_TEST)

int exynos_acpm_set_rate(unsigned int id, unsigned long rate);
unsigned long exynos_acpm_get_rate(unsigned int id, unsigned long dbg_val);

#else /* IS_ENABLED(CONFIG_EDGETPU_TEST) */

static inline int exynos_acpm_set_rate(unsigned int id, unsigned long rate)
{
	return 0;
}

static inline unsigned long exynos_acpm_get_rate(unsigned int id, unsigned long dbg_val)
{
	return 0;
}

#endif /* IS_ENABLED(CONFIG_EDGETPU_TEST) */

#endif /* __ACPM_DVFS_H__ */
