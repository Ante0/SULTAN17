/*
 * Copyright (C) 2010 - 2022 Novatek, Inc.
 *
 * $Revision$
 * $Date$
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

#include <linux/slab.h>

#include "nt36xxx.h"
#if SPI_FLASH

#if BOOT_UPDATE_FIRMWARE


#define GCM_FLASH_ADDR_LEN 3

static uint8_t flash_prog_data_cmd = 0x02;
static uint8_t flash_read_data_cmd = 0x03;
static uint8_t flash_read_pem_byte_len;
static uint8_t flash_read_dummy_byte_len;

#endif /* BOOT_UPDATE_FIRMWARE */

#define FLASH_PAGE_SIZE 256

/*******************************************************
Description:
	Novatek touchscreen switch enter / leave gcm mode
	function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Switch_GCM(uint8_t enable)
{
	int32_t ret = 0;
	uint8_t buf[8] = {0};
	int32_t retry = 0;
	uint32_t GCM_CODE_ADDR = ts->mmap->GCM_CODE_ADDR;
	uint32_t GCM_FLAG_ADDR = ts->mmap->GCM_FLAG_ADDR;

	while (1) {
		ret = nvt_set_page(GCM_CODE_ADDR);
		if (ret < 0) {
			NVT_ERR("change I2C buffer index error!!(%d)\n", ret);
			goto Switch_GCM_exit;
		}
		buf[0] = GCM_CODE_ADDR & 0xFF;
		if (enable) {
			buf[1] = 0x55;
			buf[2] = 0xFF;
			buf[3] = 0xAA;
		} else { // disable
			buf[1] = 0xAA;
			buf[2] = 0x55;
			buf[3] = 0xFF;
		}
		ret = CTP_SPI_WRITE(ts->client, buf, 4);
		if (ret < 0) {
			NVT_ERR("Write Switch GCM error!!(%d)\n", ret);
			goto Switch_GCM_exit;
		}
		/* check gcm flag */
		buf[0] = GCM_FLAG_ADDR & 0xFF;
		buf[1] = 0;
		ret = CTP_SPI_READ(ts->client, buf, 2);
		if (ret < 0) {
			NVT_ERR("Read GCM flag error!!(%d)\n", ret);
			goto Switch_GCM_exit;
		}
		if (enable) {
			if ((buf[1] & 0x01) == 0x01) {
				ret = 0;
				break;
			}
		} else { // disable
			if ((buf[1] & 0x01) == 0x00) {
				ret = 0;
				break;
			}
		}
		retry++;
		if (unlikely(retry > 3)) {
			NVT_ERR("Switch GCM %d failed!!(0x%02X)\n", enable, buf[1]);
			ret = -1;
			goto Switch_GCM_exit;
		}
	}

Switch_GCM_exit:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen initial and enter gcm mode function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Init_GCM(void)
{
	int32_t ret = 0;

	/* Enter GCM mode */
	ret = Switch_GCM(1);
	if (ret) {
		NVT_ERR("Enable GCM mode failed!!(%d)\n", ret);
		ret = -1;
		goto Init_GCM_exit;
	} else {
		NVT_LOG("Init OK\n");
		ret = 0;
	}

Init_GCM_exit:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen flash cmd gcm transfer function.
	function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t nvt_gcm_xfer(gcm_xfer_t *xfer)
{
	int32_t ret = 0;
	int32_t transfer_len = 0;
	uint8_t *buf = NULL;
	int32_t total_buf_size = 0;
	int32_t wait_cmd_issue_cnt = 0;
	uint32_t FLASH_CMD_ADDR = ts->mmap->FLASH_CMD_ADDR;
	uint32_t FLASH_CMD_ISSUE_ADDR = ts->mmap->FLASH_CMD_ISSUE_ADDR;
	uint32_t RW_FLASH_DATA_ADDR = ts->mmap->RW_FLASH_DATA_ADDR;
	int32_t write_len = 0;
	uint32_t tmp_addr = 0;
	int32_t tmp_len = 0;
	int32_t i;
//	int32_t j;

	if (BUS_TRANSFER_LENGTH <= FLASH_PAGE_SIZE)
		transfer_len = BUS_TRANSFER_LENGTH;
	else
		transfer_len = FLASH_PAGE_SIZE;

	total_buf_size = 64 + xfer->tx_len + xfer->rx_len;
	buf = (uint8_t *)kmalloc(total_buf_size, GFP_KERNEL);
	if (!buf) {
		NVT_ERR("alloc buf failed! total_buf_size=%d\n", total_buf_size);
		ret = -1;
		goto nvt_gcm_xfer_exit;
	}
	memset(buf, 0, total_buf_size);

	if ((xfer->tx_len > 0) && xfer->tx_buf) {
		for (i = 0; i < xfer->tx_len; i += transfer_len) {
			tmp_addr = RW_FLASH_DATA_ADDR + i;
			tmp_len = min(xfer->tx_len - i, transfer_len);
			nvt_set_page(tmp_addr);
			buf[0] = tmp_addr & 0xFF;
			memcpy(buf + 1, xfer->tx_buf + i, tmp_len);
#if 0
			printk("tx data of cmd 0x%02X = ", xfer->flash_cmd);
			for (j = 0; j < tmp_len; j++) {
				printk("%02X ", buf[1 + j]);
			}
			printk("\n");
#endif
			ret = CTP_SPI_WRITE(ts->client, buf, 1 + tmp_len);
			if (ret < 0) {
				NVT_ERR("Write tx data error!!(%d)\n", ret);
				goto nvt_gcm_xfer_exit;
			}
		}
	}

	ret = nvt_set_page(FLASH_CMD_ADDR);
	if (ret < 0) {
		NVT_ERR("change I2C buffer index error!!(%d)\n", ret);
		goto nvt_gcm_xfer_exit;
	}
	memset(buf, 0, total_buf_size);
	buf[0] = FLASH_CMD_ADDR & 0xFF;
	buf[1] = xfer->flash_cmd;
	if (xfer->flash_addr_len > 0) {
		buf[2] = xfer->flash_addr & 0xFF;
		buf[3] = (xfer->flash_addr >> 8) & 0xFF;
		buf[4] = (xfer->flash_addr >> 16) & 0xFF;
	} else {
		buf[2] = 0x00;
		buf[3] = 0x00;
		buf[4] = 0x00;
	}
	write_len = xfer->flash_addr_len + xfer->pem_byte_len + xfer->dummy_byte_len + xfer->tx_len;
	if (write_len > 0) {
		buf[6] = write_len & 0xFF;
		buf[7] = (write_len >> 8) & 0xFF;
	} else {
		buf[6] = 0x00;
		buf[7] = 0x00;
	}
	if (xfer->rx_len > 0) {
		buf[8] = xfer->rx_len & 0xFF;
		buf[9] = (xfer->rx_len >> 8) & 0xFF;
	} else {
		buf[8] = 0x00;
		buf[9] = 0x00;
	}
	buf[10] = xfer->flash_checksum & 0xFF;
	buf[11] = (xfer->flash_checksum >> 8) & 0xFF;
	buf[12] = 0xC2;
#if 0
	for (i = 0; i < 12; i++) {
		if (i == 0)
			printk("CMD:");
		if (i == 1)
			printk("ADDR:");
		if (i == 5)
			printk("WL:");
		if (i == 7)
			printk("RL:");
		if (i == 9)
			printk("CKSUM:");
		if (i == 11)
			printk("CMDISSUE:");
		printk("%02X ", buf[1 + i]);
	}
	printk("\n");
#endif
	ret = CTP_SPI_WRITE(ts->client, buf, 13);
	if (ret < 0) {
		NVT_ERR("Write Enter GCM error!!(%d)\n", ret);
		goto nvt_gcm_xfer_exit;
	}

	wait_cmd_issue_cnt = 0;
	while (1) {
		// check flash cmd issue complete
		buf[0] = FLASH_CMD_ISSUE_ADDR & 0xFF;
		buf[1] = 0xFF;
		ret = CTP_SPI_READ(ts->client, buf, 2);
		if (ret < 0) {
			NVT_ERR("Read FLASH_CMD_ISSUE_ADDR status error!!(%d)\n", ret);
			goto nvt_gcm_xfer_exit;
		}
		if (buf[1] == 0x00) {
			//NVT_LOG("wait_cmd_issue_cnt=%d\n", wait_cmd_issue_cnt);
			break;
		}
		wait_cmd_issue_cnt++;
		if (unlikely(wait_cmd_issue_cnt > 2000)) {
			NVT_ERR("write GCM cmd 0x%02X failed!!(0x%02X)\n", xfer->flash_cmd, buf[1]);
			ret = -1;
			goto nvt_gcm_xfer_exit;
		}
		mdelay(1);
	}

	if ((xfer->rx_len > 0) && xfer->rx_buf) {
		memset(buf + 1, 0, xfer->rx_len);
		for (i = 0; i < xfer->rx_len; i += transfer_len) {
			tmp_addr = RW_FLASH_DATA_ADDR + i;
			tmp_len = min(xfer->rx_len - i, transfer_len);
			nvt_set_page(tmp_addr);
			buf[0] = tmp_addr & 0xFF;
			ret = CTP_SPI_READ(ts->client, buf, 1 + tmp_len);
			if (ret < 0) {
				NVT_ERR("Read rx data fail error!!(%d)\n", ret);
				goto nvt_gcm_xfer_exit;
			}
			memcpy(xfer->rx_buf + i, buf + 1, tmp_len);
#if 0
			for (j = 0; j < tmp_len; j++) {
				printk("%02X ", buf[1 + j]);
			}
			printk("\n");
#endif
		}
	}

	ret = 0;
nvt_gcm_xfer_exit:
	if (buf) {
		kfree(buf);
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen resume from deep power down function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Resume_PD_GCM(void)
{
	int32_t ret = 0;
	gcm_xfer_t xfer;

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = 0xAB;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Resume PD failed!!(%d)\n", ret);
		ret = -1;
	} else {
		NVT_LOG("Resume PD OK\n");
		ret = 0;
	}
	usleep_range(10000, 10000);

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen find matched flash info item
	function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t nvt_find_match_flash_info(void)
{
	int32_t ret = 0;
	int32_t total_info_items = 0;
	int32_t i = 0;
	const struct flash_info *finfo;

	NVT_LOG("flash mid=0x%02X, did=0x%04X\n", ts->flash_mid, ts->flash_did);
	total_info_items = sizeof(flash_info_table) / sizeof(flash_info_table[0]);
	for (i = 0; i < total_info_items; i++) {
		if (flash_info_table[i].mid == ts->flash_mid) {
			/* mid of this flash info item match current flash's mid */
			if (flash_info_table[i].did == ts->flash_did) {
				/* specific mid and did of this flash info item match current flash's mid and did */
				break;
			} else if (flash_info_table[i].did == FLASH_DID_ALL) {
				/* mid of this flash info item match current flash's mid, and all did have same flash info */
				break;
			}
		} else if (flash_info_table[i].mid == FLASH_MFR_UNKNOWN) {
			/* reach the last item of flash_info_table, no flash info item matched */
			break;
		} else {
			/* mid of this flash info item not math current flash's mid */
			continue;
		}
	}
	ts->match_finfo = &flash_info_table[i];
	finfo = ts->match_finfo;
	NVT_LOG("matched flash info item %d:\n", i);
	NVT_LOG("\tmid=0x%02X, did=0x%04X\n", finfo->mid, finfo->did);
	NVT_LOG("\tqeb_pos=%d, qeb_order=%d\n", finfo->qeb_info.qeb_pos, finfo->qeb_info.qeb_order);
	NVT_LOG("\trd_method=%d, prog_method=%d\n", finfo->rd_method, finfo->prog_method);
	NVT_LOG("\twrsr_method=%d, rdsr1_cmd=0x%02X\n", finfo->wrsr_method, finfo->rdsr1_cmd);

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen read flash mid, did by 0x9F cmd
	function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Read_Flash_MID_DID_GCM(void)
{
	int32_t ret = 0;
	gcm_xfer_t xfer;
	uint8_t buf[3] = {0};

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = 0x9F;
	xfer.rx_buf = buf;
	xfer.rx_len = 3;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Read Flash MID DID GCM fail!!, ret = %d\n", ret);
		ret = -1;
		goto Read_Flash_MID_DID_GCM_exit;
	} else {
		ts->flash_mid = buf[0];
		ts->flash_did = (buf[1] << 8) | buf[2];
		NVT_LOG("Flash MID = 0x%02X, DID = 0x%04X\n", ts->flash_mid, ts->flash_did);
		nvt_find_match_flash_info();
		ret = 0;
	}

Read_Flash_MID_DID_GCM_exit:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen get flash information function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t nvt_get_flash_info(void)
{
	int32_t ret = 0;

	//---Stop CRC check to prevent IC auto reboot---
	nvt_stop_crc_reboot();

	ret = Init_GCM();
	if (ret < 0) {
		NVT_ERR("Init BootLoader error!!(%d)\n", ret);
		goto nvt_get_flash_info_exit;
	}

	ret = Resume_PD_GCM();
	if (ret < 0) {
		NVT_ERR("Resume PD error!!(%d)\n", ret);
		goto nvt_get_flash_info_exit;
	}

	ret = Read_Flash_MID_DID_GCM();
	if (ret < 0) {
		NVT_ERR("Read Flash ID error!!(%d)\n", ret);
		goto nvt_get_flash_info_exit;
	}

nvt_get_flash_info_exit:
	nvt_bootloader_reset();
	return ret;
}

#if BOOT_UPDATE_FIRMWARE
/*******************************************************
Description:
	Novatek touchscreen set read flash method function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t nvt_set_read_flash_method(void)
{
	int32_t ret = 0;
	FLASH_READ_METHOD_t rd_method;
	uint8_t bld_rd_io_sel = 0;
	uint8_t bld_rd_addr_sel = 0;

	rd_method = ts->match_finfo->rd_method;
	switch (rd_method) {
	case SISO_0x03:
		flash_read_data_cmd = 0x03;
		flash_read_pem_byte_len = 0;
		flash_read_dummy_byte_len = 0;
		bld_rd_io_sel = 0;
		bld_rd_addr_sel = 0;
		break;
	case SISO_0x0B:
		flash_read_data_cmd = 0x0B;
		flash_read_pem_byte_len = 0;
		flash_read_dummy_byte_len = 1;
		bld_rd_io_sel = 0;
		bld_rd_addr_sel = 0;
		break;
	case SIQO_0x6B:
		flash_read_data_cmd = 0x6B;
		flash_read_pem_byte_len = 0;
		flash_read_dummy_byte_len = 4;
		bld_rd_io_sel = 2;
		bld_rd_addr_sel = 0;
		break;
	case QIQO_0xEB:
		flash_read_data_cmd = 0xEB;
		flash_read_pem_byte_len = 1;
		flash_read_dummy_byte_len = 2;
		bld_rd_io_sel = 2;
		bld_rd_addr_sel = 1;
		break;
	default:
		NVT_ERR("flash read method %d not support!\n", rd_method);
		ret = -EINVAL;
		goto nvt_set_flash_read_method_exit;
		break;
	}
	NVT_LOG("rd_method=%d, flash_read_data_cmd=0x%02X, flash_read_pem_byte_len=%d, flash_read_dummy_byte_len=%d\n",
			rd_method, flash_read_data_cmd, flash_read_pem_byte_len, flash_read_dummy_byte_len);
	NVT_LOG("bld_rd_io_sel=%d, bld_rd_addr_sel=%d\n",
			bld_rd_io_sel, bld_rd_addr_sel);

	if (ts->mmap->BLD_RD_IO_SEL_REG.addr) {
		ret = nvt_write_reg(ts->mmap->BLD_RD_IO_SEL_REG, bld_rd_io_sel);
		if (ret < 0) {
			NVT_ERR("set BLD_RD_IO_SEL_REG failed!(%d)\n", ret);
			goto nvt_set_flash_read_method_exit;
		} else {
			/* NVT_LOG("set BLD_RD_IO_SEL_REG=%d done\n", bld_rd_io_sel); */
		}
	}
	if (ts->mmap->BLD_RD_ADDR_SEL_REG.addr) {
		ret = nvt_write_reg(ts->mmap->BLD_RD_ADDR_SEL_REG, bld_rd_addr_sel);
		if (ret < 0) {
			NVT_ERR("set BLD_RD_ADDR_SEL_REG failed!(%d)\n", ret);
			goto nvt_set_flash_read_method_exit;
		} else {
			/* NVT_LOG("set BLD_RD_ADDR_SEL_REG=%d done\n", bld_rd_addr_sel); */
		}
	}

nvt_set_flash_read_method_exit:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen set program flash method function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t nvt_set_prog_flash_method(void)
{
	int32_t ret = 0;
	FLASH_PROG_METHOD_t prog_method;
	uint8_t pp4io_en = 0;
	uint8_t q_wr_cmd = 0;
	uint8_t bld_rd_addr_sel = 0;

	prog_method = ts->match_finfo->prog_method;
	switch (prog_method) {
	case SPP_0x02:
		flash_prog_data_cmd = 0x02;
		pp4io_en = 0;
		q_wr_cmd = 0x00; /* not 0x02, must 0x00! */
		break;
	case QPP_0x32:
		flash_prog_data_cmd = 0x32;
		pp4io_en = 1;
		q_wr_cmd = 0x32;
		bld_rd_addr_sel = 0;
		break;
	case QPP_0x38:
		flash_prog_data_cmd = 0x38;
		pp4io_en = 1;
		q_wr_cmd = 0x38;
		bld_rd_addr_sel = 1;
		break;
	default:
		NVT_ERR("flash program method %d not support!\n", prog_method);
		ret = -EINVAL;
		goto nvt_set_prog_flash_method_exit;
		break;
	}
	NVT_LOG("prog_method=%d, flash_prog_data_cmd=0x%02X\n",
			prog_method, flash_prog_data_cmd);
	NVT_LOG("pp4io_en=%d, q_wr_cmd=0x%02X, bld_rd_addr_sel=0x%02X\n",
			pp4io_en, q_wr_cmd, bld_rd_addr_sel);

	if (ts->mmap->PP4IO_EN_REG.addr) {
		ret = nvt_write_reg(ts->mmap->PP4IO_EN_REG, pp4io_en);
		if (ret < 0) {
			NVT_ERR("set PP4IO_EN_REG failed!(%d)\n", ret);
			goto nvt_set_prog_flash_method_exit;
		} else {
			/* NVT_LOG("set PP4IO_EN_REG=%d done\n", pp4io_en); */
		}
	}
	if (ts->mmap->Q_WR_CMD_ADDR) {
		ret = nvt_write_addr(ts->mmap->Q_WR_CMD_ADDR, q_wr_cmd);
		if (ret < 0) {
			NVT_ERR("set Q_WR_CMD_ADDR failed!(%d)\n", ret);
			goto nvt_set_prog_flash_method_exit;
		} else {
			/* NVT_LOG("set Q_WR_CMD_ADDR=0x%02X done\n", q_wr_cmd); */
		}
	}
	if (pp4io_en) {
		if (ts->mmap->BLD_RD_ADDR_SEL_REG.addr) {
			ret = nvt_write_reg(ts->mmap->BLD_RD_ADDR_SEL_REG, bld_rd_addr_sel);
			if (ret < 0) {
				NVT_ERR("set BLD_RD_ADDR_SEL_REG failed!(%d)\n", ret);
				goto nvt_set_prog_flash_method_exit;
			} else {
				/* NVT_LOG("set BLD_RD_ADDR_SEL_REG=%d done\n", bld_rd_addr_sel); */
			}
		}
	}

nvt_set_prog_flash_method_exit:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen get checksum of written flash
	function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Get_Checksum_GCM(uint32_t flash_addr, uint16_t data_len, uint16_t *checksum)
{
	int32_t ret = 0;
	gcm_xfer_t xfer;
	uint32_t READ_FLASH_CHECKSUM_ADDR = ts->mmap->READ_FLASH_CHECKSUM_ADDR;
	uint8_t buf[8] = {0};

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = flash_read_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = GCM_FLASH_ADDR_LEN;
	xfer.pem_byte_len = flash_read_pem_byte_len;
	xfer.dummy_byte_len = flash_read_dummy_byte_len;
	xfer.rx_len = data_len;
	//NVT_LOG("%s: flash_addr=0x%04X, data_len=%d\n", __func__, flash_addr, data_len);
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Get Checksum GCM fail!!, ret = %d\n", ret);
		ret = -1;
		goto Get_Checksum_GCM_exit;
	}

	nvt_set_page(READ_FLASH_CHECKSUM_ADDR);
	buf[0] = READ_FLASH_CHECKSUM_ADDR & 0xFF;
	buf[1] = 0x00;
	buf[2] = 0x00;
	ret = CTP_SPI_READ(ts->client, buf, 3);
	if (ret < 0) {
		NVT_ERR("Get checksum error!!(%d)\n", ret);
		ret = -1;
		goto Get_Checksum_GCM_exit;
	}
	*checksum = (buf[2] << 8) | buf[1];

	/* NVT_LOG("Get Checksum OK.\n"); */
	ret = 0;

Get_Checksum_GCM_exit:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen flash write enable function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Write_Enable_GCM(void)
{
	int32_t ret = 0;
	gcm_xfer_t xfer;

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = 0x06;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Write Enable failed!!(%d)\n", ret);
		ret = -1;
	} else {
		/* NVT_LOG("Write Enable OK\n"); */
		ret = 0;
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen write flash status register byte
	(S7-S0) function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Write_Status_GCM(uint8_t status)
{
	int32_t ret = 0;
	FLASH_WRSR_METHOD_t wrsr_method;
	uint8_t sr1 = 0;
	gcm_xfer_t xfer;

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	wrsr_method = ts->match_finfo->wrsr_method;
	if (wrsr_method == WRSR_01H1BYTE) {
		xfer.flash_cmd = 0x01;
		xfer.flash_addr = status << 16;
		xfer.flash_addr_len = 1;
	} else if (wrsr_method == WRSR_01H2BYTE) {
		xfer.flash_cmd = ts->match_finfo->rdsr1_cmd;
		xfer.rx_len = 1;
		xfer.rx_buf = &sr1;
		ret = nvt_gcm_xfer(&xfer);
		if (ret) {
			NVT_ERR("Read Status Register-1 fail!!(%d)\n", ret);
			ret = -1;
			goto out;
		} else {
			//NVT_LOG("Read Status Register-1 OK. sr1=0x%02X\n", sr1);
		}

		memset(&xfer, 0, sizeof(gcm_xfer_t));
		xfer.flash_cmd = 0x01;
		xfer.flash_addr = (status << 16) | (sr1 << 8);
		xfer.flash_addr_len = 2;
	} else {
		NVT_ERR("Unknown or not support write status register method(%d)!\n", wrsr_method);
		goto out;
	}

	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Write Status GCM fail!!(%d)\n", ret);
		ret = -1;
	} else {
		//NVT_LOG("Write Status Register OK.\n");
		ret = 0;
	}

out:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen read flash status register byte
	(S7-S0) function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Read_Status_GCM(uint8_t *status)
{
	int32_t ret = 0;
	gcm_xfer_t xfer;

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = 0x05;
	xfer.rx_len = 1;
	xfer.rx_buf = status;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Read Status GCM fail!!(%d)\n", ret);
		ret = -1;
	} else {
		//NVT_LOG("Read Status Register OK. status=0x%02X\n", *status);
		ret = 0;
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen erase flash sector function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Sector_Erase_GCM(uint32_t flash_addr)
{
	int32_t ret = 0;
	gcm_xfer_t xfer;

	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = 0x20;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = GCM_FLASH_ADDR_LEN;
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Sector Erase GCM fail!!(%d)\n", ret);
		ret = -1;
	} else {
		//NVT_LOG("Sector Erase GCM OK.\n");
		ret = 0;
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen erase flash sectors function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Erase_Flash_GCM(void)
{
	int32_t ret = 0;
	int32_t count = 0;
	int32_t i = 0;
	uint32_t Flash_Address = 0;
	int32_t retry = 0;
	uint8_t status = 0;
	FLASH_MFR_t mid;
	const FLASH_QEB_INFO_t *qeb_info_p;

	mid = ts->match_finfo->mid;
	qeb_info_p = &ts->match_finfo->qeb_info;
	if ((mid != FLASH_MFR_UNKNOWN) && (qeb_info_p->qeb_pos != QEB_POS_UNKNOWN)) {
		/* check if QE bit is in Status Register 1st byte,
		 * if yes read it back and set back to its original QE bit value */
		if (qeb_info_p->qeb_pos == QEB_POS_SR_1B) {
			ret = Read_Status_GCM(&status);
			if (ret < 0) {
				NVT_ERR("Read Status Register error!!(%d)\n", ret);
				ret = -1;
				goto Erase_Flash_GCM_exit;
			}
			NVT_LOG("Read Status Register byte: 0x%02X\n", status);
			if ((qeb_info_p->qeb_order >= 0) && (qeb_info_p->qeb_order <= 7)) {
				status = 0x00 | (status & (0x01 << qeb_info_p->qeb_order));
			} else {
				NVT_ERR("Invalid qeb_order value: %d!\n", qeb_info_p->qeb_order);
				ret = -1;
				goto Erase_Flash_GCM_exit;
			}
		} else {
			// QE bit is not in SR 1st byte
			status = 0x00;
		}

		// Write Enable
		ret = Write_Enable_GCM();
		if (ret < 0) {
			NVT_ERR("Write Enable (for Write Status Register) error!!(%d)\n", ret);
			ret = -1;
			goto Erase_Flash_GCM_exit;
		}

		NVT_LOG("Write Status Register byte: 0x%02X\n", status);
		// Write Status Register
		ret = Write_Status_GCM(status);
		if (ret < 0) {
			NVT_ERR("Write Status Register error!!(%d)\n", ret);
			ret = -1;
			goto Erase_Flash_GCM_exit;
		}

		// Read Status
		retry = 0;
		while (1) {
			mdelay(5);
			ret = Read_Status_GCM(&status);
			if (ret < 0) {
				NVT_ERR("Read Status Register error!!(%d)\n", ret);
				ret = -1;
				goto Erase_Flash_GCM_exit;
			}
			if ((status & 0x03) == 0x00) {
				//NVT_ERR("%s:%d, retry=%d\n", __func__, __LINE__, retry);
				NVT_LOG("Read Status Register byte: 0x%02X\n", status);
				break;
			}
			retry++;
			if (unlikely(retry > 20)) {
				NVT_ERR("Read Status failed!!(0x%02X)\n", status);
				ret = -1;
				goto Erase_Flash_GCM_exit;
			}
		}
	} else {
		NVT_LOG("matched flash mid(%d) is FLASH_MFR_UNKNOWN or qeb_pos(%d) is QEB_POS_UNKNOWN\n",
				mid, qeb_info_p->qeb_pos);
		NVT_LOG("not write Status Register byte\n");
	}

	if (fw_need_write_size % FLASH_SECTOR_SIZE)
		count = fw_need_write_size / FLASH_SECTOR_SIZE + 1;
	else
		count = fw_need_write_size / FLASH_SECTOR_SIZE;

	for (i = 0; i < count; i++) {
		// Write Enable
		ret = Write_Enable_GCM();
		if (ret < 0) {
			NVT_ERR("Write Enable error!!(%d,%d)\n", ret, i);
			ret = -1;
			goto Erase_Flash_GCM_exit;
		}

		Flash_Address = i * FLASH_SECTOR_SIZE;

		// Sector Erase
		ret = Sector_Erase_GCM(Flash_Address);
		if (ret < 0) {
			NVT_ERR("Sector Erase error!!(%d,%d)\n", ret, i);
			ret = -1;
			goto Erase_Flash_GCM_exit;
		}
		mdelay(25);

		// Read Status
		retry = 0;
		while (1) {
			ret = Read_Status_GCM(&status);
			if (ret < 0) {
				NVT_ERR("Read Status Register error!!(%d)\n", ret);
				ret = -1;
				goto Erase_Flash_GCM_exit;
			}
			if ((status & 0x03) == 0x00) {
				//NVT_LOG("%s: retry=%d\n", __func__, retry);
				ret = 0;
				break;
			}
			retry++;
			if (unlikely(retry > 100)) {
				NVT_ERR("Wait Sector Erase timeout!!\n");
				ret = -1;
				goto Erase_Flash_GCM_exit;
			}
			mdelay(1);
		}
	}

	NVT_LOG("Erase OK\n");

Erase_Flash_GCM_exit:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen flash page program function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Page_Program_GCM(uint32_t flash_addr, uint16_t data_len, uint8_t *data)
{
	int32_t ret = 0;
	gcm_xfer_t xfer;
	uint16_t checksum = 0;
	int32_t i = 0;

	/* calculate checksum */
	checksum = (flash_addr & 0xFF);
	checksum += ((flash_addr >> 8) & 0xFF);
	checksum += ((flash_addr >> 16) & 0xFF);
	checksum += ((data_len + GCM_FLASH_ADDR_LEN) & 0xFF);
	checksum += (((data_len + GCM_FLASH_ADDR_LEN) >> 8) & 0xFF);
	for (i = 0; i < data_len; i++) {
		checksum += data[i];
	}
	checksum = ~checksum + 1;

	/* prepare gcm command transfer */
	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = flash_prog_data_cmd;
	xfer.flash_addr = flash_addr;
	xfer.flash_addr_len = GCM_FLASH_ADDR_LEN;
	xfer.tx_buf = data;
	xfer.tx_len = data_len;
	xfer.flash_checksum = checksum & 0xFFFF;
	//NVT_LOG("%s: flash_addr=0x%04X, data_len=%d\n", __func__, flash_addr, data_len);
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Page Program GCM fail!!(%d)\n", ret);
		ret = -1;
	} else {
		//NVT_LOG("Page Program GCM OK.\n");
		ret = 0;
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen write flash sectors function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Write_Flash_GCM(const struct firmware *fw_entry)
{
	int32_t ret = 0;
	uint8_t buf[64] = {0};
	uint32_t Flash_Address = 0;
	int32_t i = 0;
	int32_t count = 0;
	int32_t retry = 0;
	uint8_t status = 0;
	uint8_t page_program_retry = 0;
	uint32_t FLASH_CKSUM_STATUS_ADDR = ts->mmap->FLASH_CKSUM_STATUS_ADDR;
	int32_t percent = 0;
	int32_t previous_percent = -1;

	nvt_set_prog_flash_method();

	if (fw_need_write_size % FLASH_PAGE_SIZE)
		count = fw_need_write_size / FLASH_PAGE_SIZE + 1;
	else
		count = fw_need_write_size / FLASH_PAGE_SIZE;

	for (i = 0; i < count; i++) {
		Flash_Address = i * FLASH_PAGE_SIZE;
		page_program_retry = 0;

page_program_start:
		// Write Enable
		ret = Write_Enable_GCM();
		if (ret < 0) {
			NVT_ERR("Write Enable error!!(%d)\n", ret);
			ret = -1;
			goto Write_Flash_GCM_exit;
		}

		// Write Page : FLASH_PAGE_SIZE bytes
		// Page Program
		ret = Page_Program_GCM(Flash_Address, min(fw_need_write_size - Flash_Address, (size_t)FLASH_PAGE_SIZE), (uint8_t *)&(fw_entry->data[Flash_Address]));
		if (ret < 0) {
			NVT_ERR("Page Program error!!(%d), i=%d\n", ret, i);
			ret = -1;
			goto Write_Flash_GCM_exit;
		}

		// check flash checksum status
		nvt_set_page(FLASH_CKSUM_STATUS_ADDR);
		retry = 0;
		while (1) {
			buf[0] = FLASH_CKSUM_STATUS_ADDR & 0xFF;
			buf[1] = 0x00;
			ret = CTP_SPI_READ(ts->client, buf, 2);
			if (buf[1] == 0xAA) { /* checksum pass */
				//NVT_LOG("page_program_retry=%d, retry=%d\n", page_program_retry, retry);
				ret = 0;
				break;
			} else if (buf[1] == 0xEA) { /* checksum error */
				if (page_program_retry < 1) {
					page_program_retry++;
					goto page_program_start;
				} else {
					NVT_ERR("Check Flash Checksum Status error!!\n");
					ret = -1;
					goto Write_Flash_GCM_exit;
				}
			}
			retry++;
			if (unlikely(retry > 20)) {
				NVT_ERR("Check flash shecksum fail!!, buf[1]=0x%02X\n", buf[1]);
				ret = -1;
				goto Write_Flash_GCM_exit;
			}
			mdelay(1);
		}

		// Read Status
		retry = 0;
		while (1) {
			// Read Status
			ret = Read_Status_GCM(&status);
			if (ret < 0) {
				NVT_ERR("Read Status Register error!!(%d)\n", ret);
				ret = -1;
				goto Write_Flash_GCM_exit;
			}
			if ((status & 0x03) == 0x00) {
				//NVT_ERR("%s: retry=%d\n", __func__, retry);
				ret = 0;
				break;
			}
			retry++;
			if (unlikely(retry > 200)) {
				NVT_ERR("Wait Page Program timeout!!\n");
				ret = -1;
				goto Write_Flash_GCM_exit;
			}
			mdelay(1);
		}

		percent = ((i + 1) * 100) / count;
		if (((percent % 10) == 0) && (percent != previous_percent)) {
			NVT_LOG("Programming...%2d%%\n", percent);
			previous_percent = percent;
		}
	}

	NVT_LOG("Program OK\n");

Write_Flash_GCM_exit:
    return ret;
}

/*******************************************************
Description:
	Novatek touchscreen verify checksum of written flash
	function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Verify_Flash_GCM(const struct firmware *fw_entry)
{
	int32_t ret = 0;
	int32_t total_sector_need_check = 0;
	int32_t i = 0;
	int32_t j = 0;
	uint32_t flash_addr = 0;
	uint16_t data_len = 0;
	uint16_t write_checksum = 0;
	uint16_t read_checksum = 0;

	nvt_set_read_flash_method();

	total_sector_need_check = fw_need_write_size / SIZE_4KB;
	//NVT_LOG("total_sector_need_check=%d\n", total_sector_need_check);
	for (i = 0; i < total_sector_need_check; i++) {
		flash_addr = i * SIZE_4KB;
		data_len = SIZE_4KB;
		/* calculate write_checksum of each 4KB block */
		write_checksum = (flash_addr & 0xFF);
		write_checksum += ((flash_addr >> 8) & 0xFF);
		write_checksum += ((flash_addr >> 16) & 0xFF);
		write_checksum += ((data_len) & 0xFF);
		write_checksum += (((data_len) >> 8) & 0xFF);
		for (j = 0; j < data_len; j++) {
			write_checksum += fw_entry->data[flash_addr + j];
		}
		write_checksum = ~write_checksum + 1;

		ret = Get_Checksum_GCM(flash_addr, data_len, &read_checksum);
		if (ret < 0) {
			NVT_ERR("Get Checksum failed!!(%d,%d)\n", ret, i);
			ret = -1;
			goto Verify_Flash_GCM_exit;
		}
		//NVT_LOG("i=%d, write_checksum=0x%04X, read_checksum=0x%04X\n", i, write_checksum, read_checksum);
		if (write_checksum != read_checksum) {
			NVT_ERR("Verify Failed!!, i=%d, write_checksum=0x%04X, read_checksum=0x%04X\n",
					i, write_checksum, read_checksum);
			ret = -1;
			goto Verify_Flash_GCM_exit;
		}
	}

	NVT_LOG("Verify OK\n");

Verify_Flash_GCM_exit:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen check firmware checksum function.

return:
	Executive outcomes. 0---checksum not match.
	1---checksum match. -1--- checksum read failed.
*******************************************************/
int32_t Check_CheckSum_GCM(const struct firmware *fw_entry)
{
	int32_t ret = 0;
	int32_t i = 0;
	int32_t total_sector_need_check = 0;
	int32_t j = 0;
	uint32_t flash_addr = 0;
	uint16_t data_len = 0;
	uint16_t write_checksum = 0;
	uint16_t read_checksum = 0;

	nvt_sw_reset_idle();

	//---Stop CRC check to prevent IC auto reboot---
	nvt_stop_crc_reboot();

	ret = Init_GCM();
	if (ret < 0) {
		NVT_ERR("Init GCM error!!(%d)\n", ret);
		goto Check_CheckSum_exit;
	}

	ret = Resume_PD_GCM();
	if (ret < 0) {
		NVT_ERR("Resume PD error!!(%d)\n", ret);
		goto Check_CheckSum_exit;
	}

	ret = Read_Flash_MID_DID_GCM();
	if (ret < 0) {
		NVT_ERR("Read Flash ID error!!(%d)\n", ret);
		goto Check_CheckSum_exit;
	}

	nvt_set_read_flash_method();

	total_sector_need_check = fw_need_write_size / SIZE_4KB;
	//NVT_LOG("total_sector_need_check=%d\n", total_sector_need_check);
	for (i = 0; i < total_sector_need_check; i++) {
		flash_addr = i * SIZE_4KB;
		data_len = SIZE_4KB;
		/* calculate write_checksum of each 4KB block */
		write_checksum = (flash_addr & 0xFF);
		write_checksum += ((flash_addr >> 8) & 0xFF);
		write_checksum += ((flash_addr >> 16) & 0xFF);
		write_checksum += ((data_len) & 0xFF);
		write_checksum += (((data_len) >> 8) & 0xFF);
		for (j = 0; j < data_len; j++) {
			write_checksum += fw_entry->data[flash_addr + j];
		}
		write_checksum = ~write_checksum + 1;

		ret = Get_Checksum_GCM(flash_addr, data_len, &read_checksum);
		if (ret < 0) {
			NVT_ERR("Get Checksum failed!!(%d,%d)\n", ret, i);
			ret = -1;
			goto Check_CheckSum_exit;
		}
		//NVT_LOG("i=%d, write_checksum=0x%04X, read_checksum=0x%04X\n", i, write_checksum, read_checksum);
		if (write_checksum != read_checksum) {
			NVT_ERR("firmware checksum not match!!, i=%d, write_checksum=0x%04X, read_checksum=0x%04X\n",
					i, write_checksum, read_checksum);
			ret = 0;
			goto Check_CheckSum_exit;
		} else {
			/* firmware checksum match */
			ret = 1;
		}
	}

	NVT_LOG("firmware checksum match\n");
Check_CheckSum_exit:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen gcm update firmware function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
int32_t Update_Firmware_GCM(const struct firmware *fw_entry)
{
	int32_t ret = 0;
	int32_t retry = 0;

update_fw_retry:
	nvt_sw_reset_idle();

	//---Stop CRC check to prevent IC auto reboot---
	nvt_stop_crc_reboot();

	// Step 1 : initial gcm
	ret = Init_GCM();
	if (ret) {
		NVT_ERR("Init_GCM() failed! ret = %d\n", ret);
		goto Update_Firmware_exit;
	}

	// Step 2 : Resume PD
	ret = Resume_PD_GCM();
	if (ret) {
		NVT_ERR("Resume_PD_GCM() failed! ret = %d\n", ret);
		goto Update_Firmware_exit;
	}

	ret = Read_Flash_MID_DID_GCM();
	if (ret) {
		NVT_ERR("Read_Flash_MID_DID_GCM() failed! ret = %d\n", ret);
		goto Update_Firmware_exit;
	}

	// Step 3 : Erase
	ret = Erase_Flash_GCM();
	if (ret) {
		NVT_ERR("Erase_Flash_GCM() failed! ret = %d\n", ret);
		goto Update_Firmware_exit;
	}

	// Step 4 : Program
	ret = Write_Flash_GCM(fw_entry);
	if (ret) {
		NVT_ERR("Write_Flash_GCM() failed! ret = %d\n", ret);
		goto Update_Firmware_exit;
	}

	// Step 5 : Verify
	ret = Verify_Flash_GCM(fw_entry);
	if (ret) {
		NVT_ERR("Verify_Flash_GCM() failed! ret = %d\n", ret);
		goto Update_Firmware_exit;
	}

	//Step 6 : Bootloader Reset
	nvt_clear_fw_reset_state();
	nvt_bootloader_reset();
	nvt_check_fw_reset_state(RESET_STATE_INIT);
	nvt_get_fw_info();

Update_Firmware_exit:
	if (ret && retry < 1) {
		retry++;
		goto update_fw_retry;
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen check flash end flag function.

return:
	Executive outcomes. 0---succeed. 1,negative---failed.
*******************************************************/
int32_t nvt_check_flash_end_flag_gcm(void)
{
	uint8_t nvt_end_flag[NVT_FLASH_END_FLAG_LEN + 1] = {0};
	int32_t ret = 0;
	gcm_xfer_t xfer;

	nvt_sw_reset_idle();

	//---Stop CRC check to prevent IC auto reboot---
	nvt_stop_crc_reboot();

	// Step 1 : initial gcm
	ret = Init_GCM();
	if (ret) {
		return ret;
	}

	// Step 2 : Resume PD
	ret = Resume_PD_GCM();
	if (ret) {
		return ret;
	}

	nvt_set_read_flash_method();

	/* Step 3 : Flash Read Command GCM */
	memset(&xfer, 0, sizeof(gcm_xfer_t));
	xfer.flash_cmd = flash_read_data_cmd;
	xfer.flash_addr = NVT_FLASH_END_FLAG_ADDR;
	xfer.flash_addr_len = GCM_FLASH_ADDR_LEN;
	xfer.pem_byte_len = flash_read_pem_byte_len;
	xfer.dummy_byte_len = flash_read_dummy_byte_len;
	xfer.rx_buf = nvt_end_flag;
	xfer.rx_len = NVT_FLASH_END_FLAG_LEN;
	//NVT_LOG("%s: flash_addr=0x%04X, data_len=%d\n", __func__, flash_addr, data_len);
	ret = nvt_gcm_xfer(&xfer);
	if (ret) {
		NVT_ERR("Get \"NVT\" end flag fail!!, ret = %d\n", ret);
		ret = -1;
		return ret;
	} else {
	    //NVT_LOG("Get \"NVT\" end flag OK.\n");
		NVT_LOG("nvt_end_flag=%s (%02X %02X %02X)\n",
				nvt_end_flag, nvt_end_flag[0], nvt_end_flag[1], nvt_end_flag[2]);
		ret = 0;
	}

	if (strncmp(nvt_end_flag, "NVT", NVT_FLASH_END_FLAG_LEN) == 0) {
		return 0;
	} else {
		NVT_ERR("\"NVT\" end flag not found!\n");
		return 1;
	}
}

#endif /* BOOT_UPDATE_FIRMWARE */
#endif /* SPI_FLASH */
