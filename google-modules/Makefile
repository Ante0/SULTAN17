# SPDX-License-Identifier: GPL-2.0

# Set KERNEL_SRC to google-modules so that the include paths are set correctly
# for in-tree compilation, since the modules assume that `google-modules` exists
# in the same directory as the kernel tree (one level up), whereas we've got
# `google-modules` inside the kernel tree. This way, when the modules include
# with `-I$(KERNEL_SRC)/../google-modules/SOME_MODULE`, inclusion works as-is.
export KERNEL_SRC := $(srctree)/google-modules
export ROOT_DIR := $(srctree)

obj-y += amplifiers/cs35l41/
obj-y += amplifiers/cs40l26/
obj-y += amplifiers/snd_soc_wm_adsp/
obj-y += aoc/
obj-y += aoc/alsa/
obj-y += aoc/usb/
obj-y += bluetooth/broadcom/
obj-y += bms/
obj-y += bms/misc/
obj-y += display/common/gs_drm/
obj-y += display/common/gs_panel/
obj-y += display/samsung/
obj-y += gps/broadcom/bcm47765/
obj-y += gpu/mali_kbase/
obj-y += gpu/mali_pixel/
obj-y += lwis/
obj-y += nfc/
obj-y += power/mitigation/
obj-y += power/reset/
obj-y += soc/gs/
obj-y += touch/common/
obj-y += touch/goodix/
obj-y += trusty/
obj-y += video/gchips/

ifdef CONFIG_SOC_ZUMA
# zuma and zumapro
obj-y += edgetpu/rio/drivers/edgetpu/
obj-y += gxp/zuma/
obj-y += hdcp/samsung/
obj-y += uwb/qorvo/qm35/qm35s/
endif

ifdef CONFIG_SOC_ZUMAPRO
# zumapro
obj-y += display/panels/caimito/
obj-y += fingerprint/fpc/
obj-y += fingerprint/qcom/qfs4008/
obj-y += radio/samsung/s5400/
obj-y += sensors/hall_sensor/
obj-y += touch/synaptics/syna_gtd/
obj-y += wlan/bcm4390/
else
# gs201 and zuma
obj-y += fingerprint/goodix/fps_touch_handler/
obj-y += radio/samsung/s5300/
ifdef CONFIG_SOC_GS201
# gs201
obj-y += amplifiers/cs35l45/
obj-y += amplifiers/drv2624/
obj-y += bluetooth/qcom/
obj-y += edgetpu/janeiro/drivers/edgetpu/
obj-y += gxp/gs201/
obj-y += gxp/gs201/gcip-kernel-driver/drivers/gcip/
obj-y += touch/focaltech/ft3658/
obj-y += touch/synaptics/syna_c10/
obj-y += uwb/qorvo/dw3000/kernel/
obj-y += wlan/bcm4389/
obj-y += wlan/wcn6740/cnss2/
obj-y += wlan/wcn6740/wlan/qcacld-3.0/
else ifdef CONFIG_SOC_ZUMA
# zuma
obj-y += touch/fts/ftm5/
obj-y += wlan/bcm4398/
endif
endif
