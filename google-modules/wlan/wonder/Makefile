obj-m += wonder.o
wonder-y := main.o mac80211.o wondertap.o nl80211_ven_cmd.o mac80211_txs.o ssr.o band_config.o

ifneq ($(CONFIG_DEBUG_FS),)
wonder-y += debugfs.o
endif

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

#EXTRA_CFLAGS += -I$(KERNEL_SRC)/../google-modules/wlan/wonder

ccflags-y := $(EXTRA_CFLAGS)

modules modules_install clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) W=1 \
    $(KBUILD_OPTIONS) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)

