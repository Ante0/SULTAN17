# SPDX-License-Identifier: GPL-2.0
#
# Makefile for EdgeTPU framework and dependent drivers.
#

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
ROOT_DIR ?= $(KERNEL_SRC)/..
COMMON_OUT_DIR ?= $(OUT_DIR)/..
M ?= $(shell pwd)

EDGETPU_CHIP ?= RIO
KBUILD_OPTIONS	+= CONFIG_$(EDGETPU_CHIP)=m

KBUILD_OPTIONS	+= CONFIG_GCIP=y

-include $(ROOT_DIR)/private/google-modules/soc/gs/Makefile.include

ifneq ("$(wildcard $(COMMON_OUT_DIR)/private/google-modules/power/mitigation/Module.symvers)","")
EXTRA_SYMBOLS	+= $(COMMON_OUT_DIR)/private/google-modules/power/mitigation/Module.symvers
endif

ifneq ("$(wildcard $(COMMON_OUT_DIR)/private/google-modules/soc/gs/drivers/soc/google/gsa/Module.symvers)","")
EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/soc/gs/drivers/soc/google/gsa/Module.symvers
endif

ifneq ("$(wildcard $(COMMON_OUT_DIR)/private/google-modules/iif/Module.symvers)","")
EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/iif/Module.symvers
endif

ifneq ("$(wildcard $(COMMON_OUT_DIR)/private/google-modules/soc/rdo/Module.symvers)","")
EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/soc/rdo/Module.symvers
endif

GCIP_DIR=gcip-kernel-driver/drivers/gcip

modules modules_install compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M)/$(GCIP_DIR) W=1 gcip.o
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 $(KBUILD_OPTIONS) \
	KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M)/$(GCIP_DIR) W=1 $(@)
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 $(KBUILD_OPTIONS) $(@)
