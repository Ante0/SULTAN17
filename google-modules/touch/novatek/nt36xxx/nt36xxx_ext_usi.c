// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 - 2022 Novatek, Inc.
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include "nt36xxx.h"

#define HID_DIAGNOSTIC_RETURN_SIZE	3
#define DIAGNOSTIC_HOST_CMD		0x74
#define MAX_SPI_BUF_SIZE		10

int32_t nvt_get_usi_data_diag(uint8_t *beacon, uint8_t *response)
{
	uint8_t spi_buf[MAX_SPI_BUF_SIZE];
	int32_t retry = 10;

	mutex_lock(&ts->lock);
	// write the diag cmd inside get feature, host will send the buf same as set
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	spi_buf[0] = EVENT_MAP_HOST_CMD & (0x7F);
	spi_buf[1] = DIAGNOSTIC_HOST_CMD;
	// since we only have 5 bytes left to append host cmd
	// remap cmd to prevent overwriting the A0 check bits
	spi_buf[2] = beacon[0] & 1;
	spi_buf[3] = (beacon[0] >> 1) + ((beacon[1] & 1) << 7);
	spi_buf[4] = (beacon[1] >> 1) + ((beacon[2] & 1) << 7);
	spi_buf[5] = (beacon[2] >> 1) + ((beacon[3] & 1) << 7);
	spi_buf[6] = (beacon[3] >> 1) + ((beacon[4] & 1) << 7);
	CTP_SPI_WRITE(ts->client, spi_buf, 7);
	mutex_unlock(&ts->lock);

	while (retry) {
		mutex_lock(&ts->lock);
		CTP_SPI_READ(ts->client, spi_buf, 3);
		mutex_unlock(&ts->lock);
		if ((spi_buf[2] & 0xF0) == 0xA0)
			break;
		NVT_ERR("retry get usi data diag : %d\n", retry);
		msleep(20);
		retry--;
	}
	if (!retry) {
		NVT_ERR("Pen diagnostic failed\n");
		return -EAGAIN;
	}
	mutex_lock(&ts->lock);
	nvt_set_page(ts->mmap->HID_PEN_INFO_ADDR);
	spi_buf[0] = ts->mmap->HID_PEN_INFO_ADDR & (0x7F);
	CTP_SPI_READ(ts->client, spi_buf, HID_DIAGNOSTIC_RETURN_SIZE + 1);
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);

	response[0] = spi_buf[1];
	response[1] = spi_buf[2];
	response[2] = spi_buf[3];

	return 0;
}
