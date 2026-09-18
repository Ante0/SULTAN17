#
# Copyright (C) 2026, Broadcom.
#
#      Unless you and Broadcom execute a separate written software license
# agreement governing use of this software, this software is licensed to you
# under the terms of the GNU General Public License version 2 (the "GPL"),
# available at http://www.broadcom.com/licenses/GPLv2.php, with the
# following added to such license:
#
#      As a special exception, the copyright holders of this software give you
# permission to link this software with independent modules, and to copy and
# distribute the resulting executable under terms of your choice, provided that
# you also meet, for each linked independent module, the terms and conditions of
# the license of that module.  An independent module is a module which is not
# derived from this software.  The special exception does not apply to any
# modifications of the software.
#
#
# <<Broadcom-WL-IPTag/Open:>>
#
# bcmdhd

# Path to the module source
M ?= $(shell pwd)

ifneq ($(KERNEL_SRC),)
  KBUILD_OPTIONS += BCMDHD_ROOT=$(shell cd $(KERNEL_SRC); readlink -e $(M))

  # For distinguishing 4390 projects. CONFIG_PCI_EXYNOS_GS is not enabled for next 4390 proj.
  CONFIG_PCI_EXYNOS_GS := $(shell grep -w CONFIG_PCI_EXYNOS_GS $(O)/.config | cut -d'=' -f2)
  ifneq ($(CONFIG_PCI_EXYNOS_GS), )
    -include $(ROOT_DIR)/private/google-modules/soc/gs/Makefile.include
  else
    KCFLAGS += -I$(ROOT_DIR)/private/google-modules/soc/rdo/include
    # For "include/linux/platform_data/sscoredump.h"
    KCFLAGS += -I$(ROOT_DIR)/private/google-modules/soc/gs/include
    ifneq ("$(wildcard $(COMMON_OUT_DIR)/private/google-modules/soc/rdo/Module.symvers)","")
      EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/soc/rdo/Module.symvers
    endif
    ifneq ("$(wildcard $(COMMON_OUT_DIR)/private/google-modules/soc/rdo/pci/google-pcie-v1_Module.symvers)","")
      EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/soc/rdo/pci/google-pcie-v1_Module.symvers
    endif
    ifneq ("$(wildcard $(COMMON_OUT_DIR)/private/google-modules/soc/rdo/pci/google-pcie-v2_Module.symvers)","")
      EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/soc/rdo/pci/google-pcie-v2_Module.symvers
    endif
  endif
  EXTRA_CFLAGS+="-Wno-missing-prototypes"
endif

NOA_SYMBOL_PATH := $(COMMON_OUT_DIR)/private/google-modules/noa/common/Module.symvers
ifneq ($(wildcard $(NOA_SYMBOL_PATH)),)
  EXTRA_SYMBOLS += $(NOA_SYMBOL_PATH)
endif

# Define and "all" target, which isn't really needed but doesn't hurt.
all: modules

modules modules_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) $(KBUILD_OPTIONS) \
		EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
