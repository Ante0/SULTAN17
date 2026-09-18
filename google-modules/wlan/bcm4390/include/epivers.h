/*
 * Copyright (C) 2026, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 *
 * + epivers.py --key-comp-dir=../../../../components/dhd/src ../../../../build/dhd/android-u/gg_pixel24-gg/pcie@gg/source//include/epivers.h
 *
 * FILE-CSTYLED
 *
 */

#ifndef _epivers_h_
#define _epivers_h_

#define EPI_MAJOR_VERSION	103u

#define EPI_MINOR_VERSION	511u

#define EPI_RC_NUMBER		77u

#define EPI_INCREMENTAL_NUMBER	27u

#define EPI_VERSION		(103, 511, 77, 27)

#define EPI_VERSION_NUM		0x671ff4d1u

#define EPI_UNIQUE_NUM		0x671ff4d1bu

#define EPI_VERSION_DEV		(103.511.77)

/* Driver Version String, ASCII, 32 chars max */
#if defined(WLTEST)
#define EPI_VERSION_STR		"103.511.77.27 (g1f0de29 WLTEST)"
#elif (defined(BCMDBG_ASSERT) && \
	!defined(BCMDBG_ASSERT_DISABLED) && \
	!defined(ASSERT_FP_DISABLE))
#define EPI_VERSION_STR		"103.511.77.27 (g1f0de29 ASSRT)"
#else
#define EPI_VERSION_STR		"103.511.77.27 (g1f0de29)"
#endif /* BCMINTERNAL */

#endif /* _epivers_h_ */
