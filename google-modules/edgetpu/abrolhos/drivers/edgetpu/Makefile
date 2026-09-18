# SPDX-License-Identifier: GPL-2.0
#
# Makefile for EdgeTPU framework and dependent drivers.
#

KBUILD_OPTIONS += CONFIG_ABROLHOS=m

include $(ROOT_DIR)/private/google-modules/soc/gs/Makefile.include
EXTRA_SYMBOLS	+= $(COMMON_OUT_DIR)/private/google-modules/power/mitigation/Module.symvers

modules modules_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 \
	$(KBUILD_OPTIONS) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
