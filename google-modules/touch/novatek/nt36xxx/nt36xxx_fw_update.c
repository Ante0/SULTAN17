// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 - 2021 Novatek, Inc.
 *
 * $Revision: 77624 $
 * $Date: 2021-02-05 10:03:05 +0800 (週五, 05 二月 2021) $
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/firmware.h>
#include <linux/gpio.h>

#include "nt36xxx.h"
#if SPI_FLASH

#if BOOT_UPDATE_FIRMWARE

const struct firmware *fw_entry;
size_t fw_need_write_size;

int32_t nvt_get_fw_need_write_size(const struct firmware *fw_entry)
{
	int32_t i = 0;
	int32_t total_sectors_to_check = 0;

	total_sectors_to_check = fw_entry->size / FLASH_SECTOR_SIZE;
	/* printk("total_sectors_to_check = %d\n", total_sectors_to_check); */

	for (i = total_sectors_to_check; i > 0; i--) {
		/* printk("current end flag address checked = 0x%X\n", i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN); */
		/* check if there is end flag "NVT" at the end of this sector */
		if (strncmp(&fw_entry->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN], "NVT", NVT_FLASH_END_FLAG_LEN) == 0) {
			fw_need_write_size = i * FLASH_SECTOR_SIZE;
			NVT_LOG("fw_need_write_size = %zu(0x%zx)\n", fw_need_write_size, fw_need_write_size);
			return 0;
		}
	}

	NVT_ERR("end flag \"NVT\" not found!\n");
	return -EINVAL;
}

/*******************************************************
Description:
	Novatek touchscreen request update firmware function.

return:
	Executive outcomes. 0---succeed. -1,-22---failed.
*******************************************************/
int32_t update_firmware_request(const char *filename)
{
	int32_t ret = 0;

	if (filename == NULL) {
		return -EINVAL;
	}

	NVT_LOG("filename is %s\n", filename);

	ret = request_firmware(&fw_entry, filename, &ts->client->dev);
	if (ret) {
		NVT_ERR("firmware load failed, ret=%d\n", ret);
		return ret;
	}

	// check FW need to write size
	if (nvt_get_fw_need_write_size(fw_entry)) {
		NVT_ERR("get fw need to write size fail!\n");
		return -EINVAL;
	}

	// check if FW version add FW version bar equals 0xFF
	if (*(fw_entry->data + FW_BIN_VER_OFFSET) + *(fw_entry->data + FW_BIN_VER_BAR_OFFSET) != 0xFF) {
		NVT_ERR("bin file FW_VER + FW_VER_BAR should be 0xFF!\n");
		NVT_ERR("FW_VER=0x%02X, FW_VER_BAR=0x%02X\n", *(fw_entry->data+FW_BIN_VER_OFFSET), *(fw_entry->data+FW_BIN_VER_BAR_OFFSET));
		return -EINVAL;
	}

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen release update firmware function.

return:
	n.a.
*******************************************************/
void update_firmware_release(void)
{
	if (fw_entry) {
		release_firmware(fw_entry);
	}
	fw_entry = NULL;
}

/*******************************************************
Description:
	Novatek touchscreen check firmware version function.

return:
	Executive outcomes. 0---need update. 1---need not
	update.
*******************************************************/
int32_t Check_FW_Ver(void)
{
	uint8_t buf[16] = {0};
	int32_t ret = 0;

	//write i2c index to EVENT BUF ADDR
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_FWINFO);
	if (ret < 0) {
		NVT_ERR("i2c write error!(%d)\n", ret);
		return ret;
	}

	//read Firmware Version
	buf[0] = EVENT_MAP_FWINFO;
	buf[1] = 0x00;
	buf[2] = 0x00;
	ret = CTP_SPI_READ(ts->client, buf, 3);
	if (ret < 0) {
		NVT_ERR("i2c read error!(%d)\n", ret);
		return ret;
	}

	NVT_LOG("IC FW Ver = 0x%02X, FW Ver Bar = 0x%02X\n", buf[1], buf[2]);
	NVT_LOG("Bin FW Ver = 0x%02X, FW ver Bar = 0x%02X\n",
			fw_entry->data[FW_BIN_VER_OFFSET], fw_entry->data[FW_BIN_VER_BAR_OFFSET]);

	// check IC FW_VER + FW_VER_BAR equals 0xFF or not, need to update if not
	if ((buf[1] + buf[2]) != 0xFF) {
		NVT_ERR("IC FW_VER + FW_VER_BAR not equals to 0xFF!\n");
		return 0;
	}

	// compare IC and binary FW version
	if (buf[1] == fw_entry->data[FW_BIN_VER_OFFSET]) {
		NVT_LOG("firmware version match\n");
		return 1;
	} else {
		return 0;
	}
}

int32_t Check_CheckSum(const struct firmware *fw_entry)
{
	return Check_CheckSum_GCM(fw_entry);
}

int32_t Update_Firmware(const struct firmware *fw_entry)
{
	return Update_Firmware_GCM(fw_entry);
}

int32_t nvt_check_flash_end_flag(void)
{
	return nvt_check_flash_end_flag_gcm();
}

/*******************************************************
Description:
	Novatek touchscreen update firmware when booting
	function.

return:
	n.a.
*******************************************************/
void Boot_Update_Firmware(struct work_struct *work)
{
	int32_t ret = 0;

	// request bin file in "/etc/firmware"
	ret = update_firmware_request(get_fw_name());
	if (ret) {
		NVT_ERR("update_firmware_request failed. (%d)\n", ret);
		complete_all(&ts->fwu_done);
		return;
	}

	mutex_lock(&ts->lock);
	ts->fw_update_in_process = true;

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	ret = Check_CheckSum(fw_entry);

	if (ts->force_fw_update) {
		NVT_LOG("force fw update\n");
		Update_Firmware(fw_entry);
	} else if (ret < 0) {	// read firmware checksum failed
		NVT_ERR("read firmware checksum failed\n");
		Update_Firmware(fw_entry);
	} else if ((ret == 0) || (Check_FW_Ver() == 0)) {	// fw checksum not match || fw version not match
		NVT_LOG("firmware version not match\n");
		Update_Firmware(fw_entry);
	} else if (nvt_check_flash_end_flag()) {
		NVT_LOG("check flash end flag failed\n");
		Update_Firmware(fw_entry);
	} else {
		// Bootloader Reset
		nvt_clear_fw_reset_state();
		nvt_bootloader_reset();
		ret = nvt_check_fw_reset_state(RESET_STATE_INIT);
		if (ret) {
			NVT_LOG("check fw reset state failed\n");
			Update_Firmware(fw_entry);
		} else {
			NVT_LOG("do not need to update firmware\n");
		}
	}

	ts->fw_update_in_process = false;
	mutex_unlock(&ts->lock);
	complete_all(&ts->fwu_done);
	update_firmware_release();

}
#endif /* BOOT_UPDATE_FIRMWARE */

#else /* SPI_FLASH */

#if BOOT_UPDATE_FIRMWARE

#define SIZE_4KB 4096
#define FLASH_SECTOR_SIZE SIZE_4KB
#define FW_BIN_VER_OFFSET (fw_need_write_size - SIZE_4KB)
#define FW_BIN_VER_BAR_OFFSET (FW_BIN_VER_OFFSET + 1)
#define NVT_FLASH_END_FLAG_LEN 3
#define NVT_FLASH_END_FLAG_ADDR (fw_need_write_size - NVT_FLASH_END_FLAG_LEN)
#undef NVT_FW_UPDATE_PROFILING

static ktime_t start, end;
static bool is_mp_fw;
const struct firmware *mp_fw_entry;
const struct firmware *ts_fw_entry;
const struct firmware *fw_entry;
static size_t fw_need_write_size;
static uint8_t *fwbuf;
static uint8_t cascade_2nd_header_info;
static uint8_t spi_dma_div_cnt_val;
static uint32_t partition;

struct nvt_ts_bin_map {
	char name[12];
	uint32_t BIN_addr;
	uint32_t SRAM_addr;
	uint32_t size;
	uint32_t crc;
};

static struct nvt_ts_bin_map *bin_map;

static int32_t nvt_get_fw_need_write_size(const struct firmware *fw_entry)
{
	int32_t i = 0;
	int32_t total_sectors_to_check = 0;

	total_sectors_to_check = fw_entry->size / FLASH_SECTOR_SIZE;
	/* printk("total_sectors_to_check = %d\n", total_sectors_to_check); */

	for (i = total_sectors_to_check; i > 0; i--) {
		/* printk("current end flag address checked = 0x%X\n", i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN); */
		/* check if there is end flag "NVT" at the end of this sector */
		if (strncmp(&fw_entry->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN],
			"NVT", NVT_FLASH_END_FLAG_LEN) == 0) {
			fw_need_write_size = i * FLASH_SECTOR_SIZE;
			NVT_LOG("fw_need_write_size = %zu(0x%zx), NVT end flag\n",
				fw_need_write_size, fw_need_write_size);
			return 0;
		}

		/* check if there is end flag "MOD" at the end of this sector */
		if (strncmp(&fw_entry->data[i * FLASH_SECTOR_SIZE - NVT_FLASH_END_FLAG_LEN],
			"MOD", NVT_FLASH_END_FLAG_LEN) == 0) {
			fw_need_write_size = i * FLASH_SECTOR_SIZE;
			NVT_LOG("fw_need_write_size = %zu(0x%zx), MOD end flag\n",
				fw_need_write_size, fw_need_write_size);
			return 0;
		}
	}

	NVT_ERR("end flag \"NVT\" \"MOD\" not found!\n");
	return -EPERM;
}

/*******************************************************
Description:
	Novatek touchscreen init variable and allocate buffer
for download firmware function.

return:
	n.a.
*******************************************************/
static inline int32_t nvt_download_init(void)
{
	/* allocate buffer for transfer firmware */
	//NVT_LOG("NVT_TRANSFER_LEN = 0x%06X\n", NVT_TRANSFER_LEN);

	if (fwbuf == NULL) {
		fwbuf = (uint8_t *)devm_kzalloc(&ts->client->dev,
			(NVT_TRANSFER_LEN + 1 + DUMMY_BYTES), GFP_KERNEL);
		if (fwbuf == NULL) {
			NVT_ERR("kzalloc for fwbuf failed!\n");
			return -ENOMEM;
		}
	}

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen checksum function. Calculate bin
file checksum for comparison.

return:
	n.a.
*******************************************************/
static uint32_t CheckSum(const u8 *data, size_t len)
{
	uint32_t i = 0;
	uint32_t checksum = 0;

	for (i = 0 ; i < len+1 ; i++)
		checksum += data[i];

	checksum += len;
	checksum = ~checksum + 1;

	return checksum;
}

static uint32_t byte_to_word(const uint8_t *data)
{
	return data[0] + (data[1] << 8) + (data[2] << 16) + (data[3] << 24);
}

/*******************************************************
Description:
	Novatek touchscreen parsing bin header function.

return:
	n.a.
*******************************************************/
static int32_t nvt_bin_header_parser(const u8 *fwdata, size_t fwsize)
{
	uint32_t list = 0;
	uint32_t pos = 0x00;
	uint32_t end = 0x00;
	uint8_t info_sec_num = 0;
	uint8_t ovly_sec_num = 0;
	uint8_t ovly_info = 0;
	uint8_t find_bin_header = 0;
	uint8_t ilm_dlm_num = 2;

	/* Find the header size */
	end = fwdata[0] + (fwdata[1] << 8) + (fwdata[2] << 16) + (fwdata[3] << 24);

	/* check cascade next header */
	cascade_2nd_header_info = (fwdata[0x20] & 0x02) >> 1;
	NVT_LOG("cascade_2nd_header_info = %d\n", cascade_2nd_header_info);

	if (cascade_2nd_header_info) {
		pos = 0x30;	// info section start at 0x30 offset
		while (pos < (end / 2)) {
			info_sec_num++;
			pos += 0x10;	/* each header info is 16 bytes */
		}

		info_sec_num = info_sec_num + 1; //next header section
#if defined(CONFIG_SOC_GOOGLE)
		if (nvt_ts_check_tid(ts, tid_nt36523n))
			spi_dma_div_cnt_val = fwdata[0x29] & 0x01;
#endif
	} else {
		pos = 0x30;	// info section start at 0x30 offset
		while (pos < end) {
			info_sec_num++;
			pos += 0x10;	/* each header info is 16 bytes */
		}
	}

	/*
	 * Find the DLM OVLY section
	 * [0:3] Overlay Section Number
	 * [4]   Overlay Info
	 */
	ovly_info = (fwdata[0x28] & 0x10) >> 4;
	ovly_sec_num = (ovly_info) ? (fwdata[0x28] & 0x0F) : 0;

	/*
	 * calculate all partition number
	 * ilm_dlm_num (ILM & DLM) + ovly_sec_num + info_sec_num
	 */
	partition = ilm_dlm_num + ovly_sec_num + info_sec_num;
	NVT_LOG("ovly_info = %d, ilm_dlm_num = %d, ovly_sec_num = %d, info_sec_num = %d, partition = %d\n",
		ovly_info, ilm_dlm_num, ovly_sec_num, info_sec_num, partition);

	/* allocated memory for header info */
	bin_map = (struct nvt_ts_bin_map *)devm_kzalloc(&ts->client->dev,
		(partition + 1) * sizeof(struct nvt_ts_bin_map), GFP_KERNEL);
	if (bin_map == NULL) {
		NVT_ERR("kzalloc for bin_map failed!\n");
		return -ENOMEM;
	}

	for (list = 0; list < partition; list++) {
		/*
		 * [1] parsing ILM & DLM header info
		 * BIN_addr : SRAM_addr : size (12-bytes)
		 * crc located at 0x18 & 0x1C
		 */
		if (list < ilm_dlm_num) {
			bin_map[list].BIN_addr = byte_to_word(&fwdata[0 + list*12]);
			bin_map[list].SRAM_addr = byte_to_word(&fwdata[4 + list*12]);
			bin_map[list].size = byte_to_word(&fwdata[8 + list*12]);
			if (ts->hw_crc)
				bin_map[list].crc = byte_to_word(&fwdata[0x18 + list*4]);
			else { //ts->hw_crc
				if ((bin_map[list].BIN_addr + bin_map[list].size) < fwsize)
					bin_map[list].crc =
						CheckSum(&fwdata[bin_map[list].BIN_addr],
							bin_map[list].size);
				else {
					NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!\n",
						bin_map[list].BIN_addr,
						bin_map[list].BIN_addr + bin_map[list].size);
					return -EINVAL;
				}
			} //ts->hw_crc
			if (list == 0)
				snprintf(bin_map[list].name, sizeof(bin_map[list].name), "ILM");
			else if (list == 1)
				snprintf(bin_map[list].name, sizeof(bin_map[list].name), "DLM");
		}

		/*
		 * [2] parsing others header info
		 * SRAM_addr : size : BIN_addr : crc (16-bytes)
		 */
		if ((list >= ilm_dlm_num) && (list < (ilm_dlm_num + info_sec_num))) {
			if (find_bin_header == 0) {
				/* others partition located at 0x30 offset */
				pos = 0x30 + (0x10 * (list - ilm_dlm_num));
			} else if (find_bin_header && cascade_2nd_header_info) {
				/* cascade 2nd header info */
				pos = end - 0x10;
			}

			bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
			bin_map[list].size = byte_to_word(&fwdata[pos+4]);
			bin_map[list].BIN_addr = byte_to_word(&fwdata[pos+8]);
			if (ts->hw_crc)
				bin_map[list].crc = byte_to_word(&fwdata[pos+12]);
			else { //ts->hw_crc
				if ((bin_map[list].BIN_addr + bin_map[list].size) < fwsize)
					bin_map[list].crc =
						CheckSum(&fwdata[bin_map[list].BIN_addr],
							bin_map[list].size);
				else {
					NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!\n",
						bin_map[list].BIN_addr,
						bin_map[list].BIN_addr + bin_map[list].size);
					return -EINVAL;
				}
			} //ts->hw_crc
			/* detect header end to protect parser function */
			if ((bin_map[list].BIN_addr < end) && (bin_map[list].size != 0)) {
				snprintf(bin_map[list].name, sizeof(bin_map[list].name), "Header");
				find_bin_header = 1;
			} else {
				snprintf(bin_map[list].name, sizeof(bin_map[list].name), "Info-%d",
					 (list - ilm_dlm_num));
			}
		}

		/*
		 * [3] parsing overlay section header info
		 * SRAM_addr : size : BIN_addr : crc (16-bytes)
		 */
		if (list >= (ilm_dlm_num + info_sec_num)) {
			/* overlay info located at DLM (list = 1) start addr */
			pos = bin_map[1].BIN_addr + (0x10 * (list - ilm_dlm_num - info_sec_num));

			bin_map[list].SRAM_addr = byte_to_word(&fwdata[pos]);
			bin_map[list].size = byte_to_word(&fwdata[pos+4]);
			bin_map[list].BIN_addr = byte_to_word(&fwdata[pos+8]);
			if (ts->hw_crc)
				bin_map[list].crc = byte_to_word(&fwdata[pos+12]);
			else { //ts->hw_crc
				if ((bin_map[list].BIN_addr + bin_map[list].size) < fwsize)
					bin_map[list].crc =
						CheckSum(&fwdata[bin_map[list].BIN_addr],
							bin_map[list].size);
				else {
					NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!\n",
						bin_map[list].BIN_addr,
						bin_map[list].BIN_addr + bin_map[list].size);
					return -EINVAL;
				}
			} //ts->hw_crc
			snprintf(bin_map[list].name, sizeof(bin_map[list].name), "Overlay-%d",
				 (list - ilm_dlm_num - info_sec_num));
		}

		/* BIN size error detect */
		if ((bin_map[list].BIN_addr + bin_map[list].size) > fwsize) {
			NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!\n",
				bin_map[list].BIN_addr,
				bin_map[list].BIN_addr + bin_map[list].size);
			return -EINVAL;
		}

//		NVT_LOG("[%d][%s] SRAM (0x%08X), SIZE (0x%08X), BIN (0x%08X), CRC (0x%08X)\n",
//				list, bin_map[list].name,
//				bin_map[list].SRAM_addr, bin_map[list].size,  bin_map[list].BIN_addr, bin_map[list].crc);
	}

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen release update firmware function.

return:
	n.a.
*******************************************************/
void update_firmware_release(void)
{
	if (fw_entry) {
		NVT_LOG("Release %s FW.\n", (is_mp_fw) ? "MP" : "TS");
		release_firmware(fw_entry);
		fw_entry = NULL;
		if (is_mp_fw)
			mp_fw_entry = NULL;
		else
			ts_fw_entry = NULL;
	}

	if (!IS_ERR_OR_NULL(bin_map)) {
		NVT_LOG("Free bin_map.\n");
		devm_kfree(&ts->client->dev, bin_map);
		bin_map = NULL;
	}
}

/*******************************************************
Description:
	Novatek touchscreen request update firmware function.

return:
	Executive outcomes. 0---succeed. -1,-22---failed.
*******************************************************/
static int32_t update_firmware_request(const char *filename)
{
	uint8_t retry = 0;
	int32_t ret = 0;

	if (NULL == filename) {
		return -ENOENT;
	}

	while (1) {
		NVT_LOG("filename is %s\n", filename);

		/* Reuse fw_entry if possible. */
		if (is_mp_fw)
			fw_entry = mp_fw_entry;
		else
			fw_entry = ts_fw_entry;

		/* FW loading. */
		if (!fw_entry) {
			NVT_LOG("Request %s FW.\n", (is_mp_fw) ? "MP" : "TS");
			ret = request_firmware(&fw_entry, filename, &ts->client->dev);
			if (ret) {
				NVT_ERR("firmware load failed, ret=%d\n", ret);
				goto request_fail;
			}
			/* Store fw_entry to reuse later. */
			if (is_mp_fw)
				mp_fw_entry = fw_entry;
			else
				ts_fw_entry = fw_entry;
		}

		// check FW need to write size
		if (nvt_get_fw_need_write_size(fw_entry)) {
			NVT_ERR("get fw need to write size fail!\n");
			ret = -EINVAL;
			goto invalid;
		}

		// check if FW version add FW version bar equals 0xFF
		if (*(fw_entry->data + FW_BIN_VER_OFFSET) + *(fw_entry->data +
				FW_BIN_VER_BAR_OFFSET) != 0xFF) {
			NVT_ERR("bin file FW_VER + FW_VER_BAR should be 0xFF!\n");
			NVT_ERR("FW_VER=0x%02X, FW_VER_BAR=0x%02X\n",
				*(fw_entry->data+FW_BIN_VER_OFFSET),
				*(fw_entry->data+FW_BIN_VER_BAR_OFFSET));
			ret = -ENOEXEC;
			goto invalid;
		}

		/* Free bin_map to reallocate for new FW if exist. */
		if (!IS_ERR_OR_NULL(bin_map)) {
			NVT_LOG("Free bin_map for new FW.\n");
			devm_kfree(&ts->client->dev, bin_map);
			bin_map = NULL;
		}

		/* BIN Header Parser */
		ret = nvt_bin_header_parser(fw_entry->data, fw_entry->size);
		if (ret) {
			NVT_ERR("bin header parser failed\n");
			goto invalid;
		} else {
			break;
		}

invalid:
		update_firmware_release();

request_fail:
		retry++;
		if (unlikely(retry > 2)) {
			NVT_ERR("error, retry=%d\n", retry);
			break;
		}
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen write data to sram function.

- fwdata   : The buffer is written
- SRAM_addr: The sram destination address
- size     : Number of data bytes in @fwdata being written
- BIN_addr : The transferred data offset of @fwdata

return:
	Executive outcomes. 0---succeed. else---fail.
*******************************************************/
static int32_t nvt_write_sram(const u8 *fwdata,
			      uint32_t SRAM_addr, uint32_t size, uint32_t BIN_addr)
{
	int32_t ret = 0;
	uint32_t i = 0;
	uint16_t len = 0;
	int32_t count = 0;

	if (size % NVT_TRANSFER_LEN)
		count = (size / NVT_TRANSFER_LEN) + 1;
	else
		count = (size / NVT_TRANSFER_LEN);

	for (i = 0 ; i < count ; i++) {
		len = (size < NVT_TRANSFER_LEN) ? size : NVT_TRANSFER_LEN;

		//---set xdata index to start address of SRAM---
		ret = nvt_set_page(SRAM_addr);
		if (ret) {
			NVT_ERR("set page failed, ret = %d\n", ret);
			return ret;
		}

		//---write data into SRAM---
		fwbuf[0] = SRAM_addr & 0x7F;	//offset
		memcpy(fwbuf+1, &fwdata[BIN_addr], len);	//payload
		ret = CTP_SPI_WRITE(ts->client, fwbuf, len+1);
		if (ret) {
			NVT_ERR("write to sram failed, ret = %d\n", ret);
			return ret;
		}

		SRAM_addr += NVT_TRANSFER_LEN;
		BIN_addr += NVT_TRANSFER_LEN;
		size -= NVT_TRANSFER_LEN;
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen nvt_write_firmware function to write
firmware into each partition.

return:
	n.a.
*******************************************************/
static int32_t nvt_write_firmware(const u8 *fwdata, size_t fwsize, uint8_t full)
{
	uint32_t list = 0;
	char *name;
	uint32_t BIN_addr, SRAM_addr, size;
	int32_t ret = 0;

	NVT_LOG("full download = %d\n", full);

	memset(fwbuf, 0, (NVT_TRANSFER_LEN+1));

	for (list = 0; list < partition; list++) {
		/* initialize variable */
		SRAM_addr = bin_map[list].SRAM_addr;
		size = bin_map[list].size;
		BIN_addr = bin_map[list].BIN_addr;
		name = bin_map[list].name;
		/* ignore ilm (list-0) if not full download */
		if ((full == false) && (list == 0))
			continue;

//		NVT_LOG("[%d][%s] SRAM (0x%08X), SIZE (0x%08X), BIN (0x%08X)\n",
//				list, name, SRAM_addr, size, BIN_addr);

		/* Check data size */
		if ((BIN_addr + size) > fwsize) {
			NVT_ERR("access range (0x%08X to 0x%08X) is larger than bin size!\n",
				BIN_addr, BIN_addr + size);
			ret = -EINVAL;
			goto out;
		}

		/* ignore reserved partition (Reserved Partition size is zero) */
		if (!size)
			continue;
		else
			size = size + 1;

		/* write data to SRAM */
		ret = nvt_write_sram(fwdata, SRAM_addr, size, BIN_addr);
		if (ret) {
			NVT_ERR("sram program failed, ret = %d\n", ret);
			goto out;
		}
	}

out:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen check checksum function.
This function will compare file checksum and fw checksum.

return:
	n.a.
*******************************************************/
static int32_t nvt_check_fw_checksum(void)
{
	uint32_t fw_checksum = 0;
	uint32_t len = partition*4;
	uint32_t list = 0;
	int32_t ret = 0;

	memset(fwbuf, 0, (len+1));

	//---set xdata index to checksum---
	nvt_set_page(ts->mmap->R_ILM_CHECKSUM_ADDR);

	/* read checksum */
	fwbuf[0] = (ts->mmap->R_ILM_CHECKSUM_ADDR) & 0x7F;
	ret = CTP_SPI_READ(ts->client, fwbuf, len+1);
	if (ret) {
		NVT_ERR("Read fw checksum failed\n");
		return ret;
	}

	/*
	 * Compare each checksum from fw
	 * ILM + DLM + Overlay + Info
	 * ilm_dlm_num (ILM & DLM) + ovly_sec_num + info_sec_num
	 */
	for (list = 0; list < partition; list++) {
		fw_checksum = byte_to_word(&fwbuf[1+list*4]);

		/* ignore reserved partition (Reserved Partition size is zero) */
		if (!bin_map[list].size)
			continue;

		if (bin_map[list].crc != fw_checksum) {
			NVT_ERR("[%d] BIN_checksum=0x%08X, FW_checksum=0x%08X\n",
				list, bin_map[list].crc, fw_checksum);
			ret = -EIO;
		}
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen set bootload crc reg bank function.
This function will set hw crc reg before enable crc function.

return:
	n.a.
*******************************************************/
static void nvt_set_bld_crc_bank(uint32_t DES_ADDR, uint32_t SRAM_ADDR,
				 uint32_t LENGTH_ADDR, uint32_t size,
				 uint32_t G_CHECKSUM_ADDR, uint32_t crc)
{
	/* write destination address */
	nvt_set_page(DES_ADDR);
	fwbuf[0] = DES_ADDR & 0x7F;
	fwbuf[1] = (SRAM_ADDR) & 0xFF;
	fwbuf[2] = (SRAM_ADDR >> 8) & 0xFF;
	fwbuf[3] = (SRAM_ADDR >> 16) & 0xFF;
	CTP_SPI_WRITE(ts->client, fwbuf, 4);

	/* write length */
	//nvt_set_page(LENGTH_ADDR);
	fwbuf[0] = LENGTH_ADDR & 0x7F;
	fwbuf[1] = (size) & 0xFF;
	fwbuf[2] = (size >> 8) & 0xFF;
	fwbuf[3] = (size >> 16) & 0x01;
	if (ts->hw_crc == 1) {
		CTP_SPI_WRITE(ts->client, fwbuf, 3);
	} else if (ts->hw_crc > 1) {
		CTP_SPI_WRITE(ts->client, fwbuf, 4);
	}

	/* write golden dlm checksum */
	//nvt_set_page(G_CHECKSUM_ADDR);
	fwbuf[0] = G_CHECKSUM_ADDR & 0x7F;
	fwbuf[1] = (crc) & 0xFF;
	fwbuf[2] = (crc >> 8) & 0xFF;
	fwbuf[3] = (crc >> 16) & 0xFF;
	fwbuf[4] = (crc >> 24) & 0xFF;
	CTP_SPI_WRITE(ts->client, fwbuf, 5);

	return;
}

/*******************************************************
Description:
	Novatek touchscreen set BLD hw crc function.
This function will set ILM and DLM crc information to register.

return:
	n.a.
*******************************************************/
static void nvt_set_bld_hw_crc(void)
{
	/* [0] ILM */
	/* write register bank */
	nvt_set_bld_crc_bank(ts->mmap->ILM_DES_ADDR, bin_map[0].SRAM_addr,
			     ts->mmap->ILM_LENGTH_ADDR, bin_map[0].size,
			     ts->mmap->G_ILM_CHECKSUM_ADDR, bin_map[0].crc);

	/* [1] DLM */
	/* write register bank */
	nvt_set_bld_crc_bank(ts->mmap->DLM_DES_ADDR, bin_map[1].SRAM_addr,
			     ts->mmap->DLM_LENGTH_ADDR, bin_map[1].size,
			     ts->mmap->G_DLM_CHECKSUM_ADDR, bin_map[1].crc);
}

/*******************************************************
Description:
	Novatek touchscreen read BLD hw crc info function.
This function will check crc results from register.

return:
	n.a.
*******************************************************/
static void nvt_read_bld_hw_crc(void)
{
	uint8_t buf[8] = {0};
	uint32_t g_crc = 0, r_crc = 0;

	/* CRC Flag */
	nvt_set_page(ts->mmap->BLD_ILM_DLM_CRC_ADDR);
	buf[0] = ts->mmap->BLD_ILM_DLM_CRC_ADDR & 0x7F;
	buf[1] = 0x00;
	CTP_SPI_READ(ts->client, buf, 2);
	NVT_LOG("crc_done = %d, ilm_crc_flag = %d, dlm_crc_flag = %d\n",
		(buf[1] >> 2) & 0x01, (buf[1] >> 0) & 0x01, (buf[1] >> 1) & 0x01);

	/* ILM CRC */
	nvt_set_page(ts->mmap->G_ILM_CHECKSUM_ADDR);
	buf[0] = ts->mmap->G_ILM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(ts->client, buf, 5);
	g_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	nvt_set_page(ts->mmap->R_ILM_CHECKSUM_ADDR);
	buf[0] = ts->mmap->R_ILM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(ts->client, buf, 5);
	r_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	NVT_LOG("ilm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X\n",
		bin_map[0].crc, g_crc, r_crc);

	/* DLM CRC */
	nvt_set_page(ts->mmap->G_DLM_CHECKSUM_ADDR);
	buf[0] = ts->mmap->G_DLM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(ts->client, buf, 5);
	g_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	nvt_set_page(ts->mmap->R_DLM_CHECKSUM_ADDR);
	buf[0] = ts->mmap->R_DLM_CHECKSUM_ADDR & 0x7F;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	CTP_SPI_READ(ts->client, buf, 5);
	r_crc = buf[1] | (buf[2] << 8) | (buf[3] << 16) | (buf[4] << 24);

	NVT_LOG("dlm: bin crc = 0x%08X, golden = 0x%08X, result = 0x%08X\n",
		bin_map[1].crc, g_crc, r_crc);

	return;
}

/*******************************************************
 Description:
	Novatek touchscreen check DMA hw crc function.
This function will check hw crc result is pass or not.

return:
	n.a.
*******************************************************/
static int32_t nvt_check_dma_hw_crc(void)
{
	uint8_t retry = 0, crc_flag = 0, retry_max = 20;
	int32_t ret = 0;

	/* write register bank */
	nvt_set_bld_crc_bank(ts->mmap->BLD_DES_ADDR, bin_map[0].SRAM_addr,
			ts->mmap->BLD_LENGTH_ADDR, bin_map[0].size,
			ts->mmap->G_DLM_CHECKSUM_ADDR, bin_map[0].crc);

	/* bld dma crc enable */
	nvt_set_page(ts->mmap->DMA_CRC_EN_ADDR);
	fwbuf[0] = ts->mmap->DMA_CRC_EN_ADDR & 0x7F;
	fwbuf[1] = 0x01;	//enable
	CTP_SPI_WRITE(ts->client, fwbuf, 2);

	/* polling for dma crc check finish */
	while (1) {
		nvt_set_page(ts->mmap->DMA_CRC_FLAG_ADDR);
		fwbuf[0] = ts->mmap->DMA_CRC_FLAG_ADDR & 0x7F;
		fwbuf[1] = 0xFF;
		ret = CTP_SPI_READ(ts->client, fwbuf, 2);
		if (ret) {
			NVT_ERR("Read dma crc flag failed\n");
			return ret;
		}

		mdelay(1);

		/*
		 * [Bit-0] DMA_CRC_FLAG: 0-DMA CRC pass, 1-DMA CRC error
		 * [Bit-1] DMA_CRC_DONE: 0-CRC checking, 1-finish
		 */
		crc_flag = (fwbuf[1] & 0x03);
		if ((crc_flag == 0x02) || (crc_flag == 0x03))
			break;

		retry++;
		if (retry > retry_max)
			break;
	}

	if (crc_flag != 0x02) {
		NVT_ERR("[0][%s] dma crc error 0x%02X, retry %d\n",
				bin_map[0].name, crc_flag, retry);
		ret = -EIO;
	}

	return ret;
}

static void nvt_spi_dma_setup(void)
{
	uint8_t buf[33] = {0};

	if (ts->mmap->SPI_DMA_VAL_ADDR) {
		nvt_set_page(bin_map[1].SRAM_addr);
		buf[0] = bin_map[1].SRAM_addr & 0x7F;
		CTP_SPI_WRITE(ts->client, buf, 33);

		nvt_set_page(ts->mmap->SPI_DMA_VAL_ADDR);
		buf[0] = ts->mmap->SPI_DMA_VAL_ADDR & 0x7F;
		buf[1] = 0x35;
		buf[2] = 0x32;
		buf[3] = 0xAA;
		buf[4] = 0x00;
		CTP_SPI_WRITE(ts->client, buf, 5);

		NVT_LOG("set spi dma val finish\n");
	} else {
		NVT_ERR("spi dma val addr is NULL\n");
	}
}

/*******************************************************
Description:
	Novatek touchscreen Download_Firmware with HW CRC
function. It's complete download firmware flow.

return:
	Executive outcomes. 0---succeed. else---fail.
*******************************************************/
static int32_t nvt_download_firmware_hw_crc(uint8_t full)
{
	uint8_t retry = 0;
	int32_t ret = 0;

	start = ktime_get();

	while (1) {
		/* bootloader reset to reset MCU */
		nvt_bootloader_reset();
		/* check ilm crc if not doing full download */
		if (!full) {
			if (nvt_check_dma_hw_crc()) {
				NVT_ERR("ILM crc is not correct. Enable full download\n");
				full = true;
			}
		}

		/* set ilm & dlm reg bank */
		nvt_set_bld_hw_crc();

		/* Start to write firmware process */
		if (cascade_2nd_header_info) {
			/* for cascade */
#if defined(CONFIG_SOC_GOOGLE)
			if (nvt_ts_check_tid(ts, tid_nt36523n) && spi_dma_div_cnt_val)
				nvt_spi_dma_setup();
#endif
			nvt_tx_auto_copy_mode();

			ret = nvt_write_firmware(fw_entry->data, fw_entry->size, full);
			if (ret) {
				NVT_ERR("Write_Firmware failed. (%d)\n", ret);
				goto fail;
			}

			ret = nvt_check_spi_dma_tx_info();
			if (ret) {
				NVT_ERR("spi dma tx info failed. (%d)\n", ret);
				goto fail;
			}
		} else {
			ret = nvt_write_firmware(fw_entry->data, fw_entry->size, full);
			if (ret) {
				NVT_ERR("Write_Firmware failed. (%d)\n", ret);
				goto fail;
			}
		}

		/* enable hw bld crc function */
		nvt_bld_crc_enable();

		/* clear fw reset status & enable fw crc check */
		nvt_fw_crc_enable();

		/* Set Boot Ready Bit */
		nvt_boot_ready();

		ret = nvt_check_fw_reset_state(RESET_STATE_INIT);
		if (ret) {
			NVT_ERR("nvt_check_fw_reset_state failed. (%d)\n", ret);
			goto fail;
		} else {
			break;
		}

fail:
		full = true;
		retry++;
		if (unlikely(retry > 2)) {
			NVT_ERR("error, retry=%d\n", retry);
			nvt_read_bld_hw_crc();
			break;
		}
	}

	end = ktime_get();

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen Download_Firmware function. It's
complete download firmware flow.

return:
	n.a.
*******************************************************/
static int32_t nvt_download_firmware(uint8_t full)
{
	uint8_t retry = 0;
	int32_t ret = 0;

	start = ktime_get();

	while (1) {
		/*
		 * Send eng reset cmd before download FW
		 * Keep TP_RESX low when send eng reset cmd
		 */
#if NVT_TOUCH_SUPPORT_HW_RST
		if (gpio_is_valid(ts->reset_gpio)) {
			gpio_set_value(ts->reset_gpio, 0);
			mdelay(1);	//wait 1ms
		}
#endif
		nvt_eng_reset();
#if NVT_TOUCH_SUPPORT_HW_RST
		if (gpio_is_valid(ts->reset_gpio)) {
			gpio_set_value(ts->reset_gpio, 1);
			mdelay(10);	//wait tRT2BRST after TP_RST
		}
#endif
		nvt_bootloader_reset();

		/* clear fw reset status */
		nvt_write_addr(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE, 0x00);

		/* Start to write firmware process */
		ret = nvt_write_firmware(fw_entry->data, fw_entry->size, full);
		if (ret) {
			NVT_ERR("Write_Firmware failed. (%d)\n", ret);
			goto fail;
		}

		/* Set Boot Ready Bit */
		nvt_boot_ready();

		ret = nvt_check_fw_reset_state(RESET_STATE_INIT);
		if (ret) {
			NVT_ERR("nvt_check_fw_reset_state failed. (%d)\n", ret);
			goto fail;
		}

		/* check fw checksum result */
		ret = nvt_check_fw_checksum();
		if (ret) {
			NVT_ERR("firmware checksum not match, retry=%d\n", retry);
			goto fail;
		} else {
			break;
		}

fail:
		retry++;
		if (unlikely(retry > 2)) {
			NVT_ERR("error, retry=%d\n", retry);
			break;
		}
	}

	end = ktime_get();

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen update firmware main function.

return:
	n.a.
*******************************************************/
int32_t nvt_update_firmware(const char *firmware_name, uint8_t full)
{
	int32_t ret = 0;
#if defined(NVT_FW_UPDATE_PROFILING)
	ktime_t profile_start = ktime_get();
	ktime_t profile_end;
#endif

	ts->fw_update_in_process = true;
	if (strncmp(firmware_name, get_fw_name(),
		strlen(get_fw_name())) == 0) {
		is_mp_fw = false;
	} else {
		is_mp_fw = true;
	}

	/* request bin file in "/vendor/firmware" */
	ret = update_firmware_request(firmware_name);
	if (ret) {
		NVT_ERR("update_firmware_request failed. (%d)\n", ret);
		goto request_firmware_fail;
	}

#if defined(NVT_FW_UPDATE_PROFILING)
	profile_end = ktime_get();
	NVT_LOG("update_firmware_request() time us delta %lld\n",
		ktime_us_delta(profile_end, profile_start));
	profile_start = profile_end;
#endif

	/* initial buffer and variable */
	ret = nvt_download_init();
	if (ret) {
		NVT_ERR("Download Init failed. (%d)\n", ret);
		goto download_fail;
	}

#if defined(NVT_FW_UPDATE_PROFILING)
	profile_end = ktime_get();
	NVT_LOG("nvt_download_init() time us delta %lld\n",
		ktime_us_delta(profile_end, profile_start));
	profile_start = profile_end;
#endif

	/* download firmware process */
	if (ts->hw_crc)
		ret = nvt_download_firmware_hw_crc(full);
	else
		ret = nvt_download_firmware(full);
	if (ret) {
		NVT_ERR("Download Firmware failed. (%d)\n", ret);
		goto download_fail;
	}

#if defined(NVT_FW_UPDATE_PROFILING)
	profile_end = ktime_get();
	NVT_LOG("nvt_download_firmware/_hw_crc() time us delta %lld\n",
		ktime_us_delta(profile_end, profile_start));
	profile_start = profile_end;
#endif

	NVT_LOG("Update firmware success! <%ld us>\n",
		(long) ktime_us_delta(end, start));

	/* Get FW Info */
	ret = nvt_get_fw_info();
	if (ret) {
		NVT_ERR("nvt_get_fw_info failed. (%d)\n", ret);
	}

download_fail:
	if (ts->force_release_fw)
		update_firmware_release();
request_firmware_fail:

	ts->fw_update_in_process = false;
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen update firmware when booting
	function.

return:
	n.a.
*******************************************************/
void Boot_Update_Firmware(struct work_struct *work)
{
	mutex_lock(&ts->lock);
	nvt_update_firmware(get_fw_name(), 1);
	mutex_unlock(&ts->lock);
}
#endif /* BOOT_UPDATE_FIRMWARE */
#endif /* !SPI_FLASH*/
