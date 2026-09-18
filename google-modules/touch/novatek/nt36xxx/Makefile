#
# Makefile for the Novatek NT36xxx touchscreen driver.
#

# Each configuration option enables a list of files.

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

KBUILD_OPTIONS	+= CONFIG_TOUCHSCREEN_NT36xxx_HOSTDL_SPI=m
EXTRA_CFLAGS	+= -DDYNAMIC_DEBUG_MODULE
EXTRA_CFLAGS	+= -DNVT_TS_PANEL_BRIDGE

EXTRA_SYMBOLS	+= $(COMMON_OUT_DIR)/private/google-modules/touch/common/Module.symvers
EXTRA_SYMBOLS	+= $(COMMON_OUT_DIR)/private/google-modules/touch/common/usi/Module.symvers

include $(ROOT_DIR)/private/google-modules/soc/gs/Makefile.include

modules modules_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) \
	$(KBUILD_OPTIONS) \
	EXTRA_CFLAGS="$(EXTRA_CFLAGS)" \
	KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" \
	$(@)

