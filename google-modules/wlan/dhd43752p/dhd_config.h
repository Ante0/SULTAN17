/*
 * DHD CONFIG INI
 *
 * Copyright (C) 2021, Broadcom.
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
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id$
 */

#include <dhd.h>

typedef struct dhd_config {
	bool nmode_disable;
	bool vhtmode_disable;
	bool he_disable;
	int gmode;
	bool gmode_set;
	int scan_band_prefer;
} dhd_config_t;

int dhd_conf_attach(dhd_pub_t *dhd);
void dhd_conf_detach(dhd_pub_t *dhd);
void dhd_conf_dependency_set(dhd_pub_t *dhd);
void dhd_conf_dependency_dft(dhd_pub_t *dhd);
int dhd_preinit_config(dhd_pub_t *dhd, int ifidx);
int dhd_preinit_proc(dhd_pub_t *dhd, int ifidx, char *name, char *value);
void dhd_conf_11mode_set(dhd_pub_t *dhd);
