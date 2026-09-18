# SPDX-License-Identifier: GPL-2.0
#
# Makefile for WiFi performance tracker driver
#

obj-$(CONFIG_WLAN_PTRACKER) += wlan_ptracker.o

# common
wlan_ptracker-$(CONFIG_WLAN_PTRACKER) += main.o tp_monitor.o
wlan_ptracker-$(CONFIG_WLAN_PTRACKER) += notifier.o
wlan_ptracker-$(CONFIG_WLAN_PTRACKER) += scenes_fsm.o

# debugfs
wlan_ptracker-$(CONFIG_DEBUG_FS) += debugfs.o

# dynamic twt setup
wlan_ptracker-$(CONFIG_DYNAMIC_TWT_SETUP) += dynamic_twt_manager.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

ifeq ($(CONFIG_WLAN_PTRACKER),)
KBUILD_OPTIONS += CONFIG_WLAN_PTRACKER=m
KBUILD_OPTIONS += CONFIG_DYNAMIC_TWT_SETUP=y
endif

EXTRA_CFLAGS += -I$(KERNEL_SRC)/../google-modules/wlan/wlan_ptracker

ccflags-y := $(EXTRA_CFLAGS)

modules modules_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 \
	$(KBUILD_OPTIONS) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
