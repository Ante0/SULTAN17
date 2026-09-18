// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2010 - 2021 Novatek, Inc.
 *
 * $Revision: 83893 $
 * $Date: 2021-06-21 10:52:25 +0800 (週一, 21 六月 2021) $
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

#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>

#include "nt36xxx.h"
#include "nt36xxx_mp_ctrlram.h"

#if NVT_TOUCH_MP

static uint8_t *RecordResult_Short;
static uint8_t *RecordResult_Open;
static uint8_t *RecordResult_FW_Rawdata;
static uint8_t *RecordResult_FW_CC;
static uint8_t *RecordResult_FW_DiffMax;
static uint8_t *RecordResult_FW_DiffMin;
static uint8_t *RecordResult_FW_DiffP2P;
static uint8_t *RecordResult_PenTipX_Raw;
static uint8_t *RecordResult_PenTipY_Raw;
static uint8_t *RecordResult_PenRingX_Raw;
static uint8_t *RecordResult_PenRingY_Raw;
static uint8_t *RecordResult_PenTipX_DiffMax;
static uint8_t *RecordResult_PenTipX_DiffMin;
static uint8_t *RecordResult_PenTipX_DiffP2P;
static uint8_t *RecordResult_PenTipY_DiffMax;
static uint8_t *RecordResult_PenTipY_DiffMin;
static uint8_t *RecordResult_PenTipY_DiffP2P;
static uint8_t *RecordResult_PenRingX_DiffMax;
static uint8_t *RecordResult_PenRingX_DiffMin;
static uint8_t *RecordResult_PenRingX_DiffP2P;
static uint8_t *RecordResult_PenRingY_DiffMax;
static uint8_t *RecordResult_PenRingY_DiffMin;
static uint8_t *RecordResult_PenRingY_DiffP2P;
static uint8_t *RecordResult_Pen_Rx_Max;

static int32_t TestResult_Short;
static int32_t TestResult_Open;
static int32_t TestResult_FW_Rawdata;
static int32_t TestResult_FW_CC;
static int32_t TestResult_Noise;
static int32_t TestResult_FW_DiffMax;
static int32_t TestResult_FW_DiffMin;
static int32_t TestResult_FW_DiffP2P;
static int32_t TestResult_Pen_FW_Raw;
static int32_t TestResult_PenTipX_Raw;
static int32_t TestResult_PenTipY_Raw;
static int32_t TestResult_PenRingX_Raw;
static int32_t TestResult_PenRingY_Raw;
static int32_t TestResult_Pen_Noise;
static int32_t TestResult_PenTipX_DiffMax;
static int32_t TestResult_PenTipX_DiffMin;
static int32_t TestResult_PenTipX_DiffP2P;
static int32_t TestResult_PenTipY_DiffMax;
static int32_t TestResult_PenTipY_DiffMin;
static int32_t TestResult_PenTipY_DiffP2P;
static int32_t TestResult_PenRingX_DiffMax;
static int32_t TestResult_PenRingX_DiffMin;
static int32_t TestResult_PenRingX_DiffP2P;
static int32_t TestResult_PenRingY_DiffMax;
static int32_t TestResult_PenRingY_DiffMin;
static int32_t TestResult_PenRingY_DiffP2P;
static int32_t TestResult_Pen_Rx_Max;

static int32_t *RawData_Short;
static int32_t *RawData_Open;
static int32_t *RawData_Diff;
static int32_t *RawData_Diff_Min;
static int32_t *RawData_Diff_Max;
static int32_t *RawData_Diff_P2P;
static int32_t *RawData_FW_Rawdata;
static int32_t *RawData_FW_CC;
static int32_t *RawData_PenTipX_Raw;
static int32_t *RawData_PenTipY_Raw;
static int32_t *RawData_PenRingX_Raw;
static int32_t *RawData_PenRingY_Raw;
static int32_t *RawData_PenTipX_DiffMin;
static int32_t *RawData_PenTipX_DiffMax;
static int32_t *RawData_PenTipX_DiffP2P;
static int32_t *RawData_PenTipY_DiffMin;
static int32_t *RawData_PenTipY_DiffMax;
static int32_t *RawData_PenTipY_DiffP2P;
static int32_t *RawData_PenRingX_DiffMin;
static int32_t *RawData_PenRingX_DiffMax;
static int32_t *RawData_PenRingX_DiffP2P;
static int32_t *RawData_PenRingY_DiffMin;
static int32_t *RawData_PenRingY_DiffMax;
static int32_t *RawData_PenRingY_DiffP2P;
static int32_t *RawData_Pen_Rx_Max;

static struct proc_dir_entry *NVT_proc_selftest_entry;
static int8_t nvt_mp_test_result_printed;
static uint8_t fw_ver;

extern void nvt_read_mdata(uint32_t xdata_addr, uint32_t xdata_btn_addr);
extern void nvt_get_mdata(int32_t *buf, uint8_t *m_x_num,
			  uint8_t *m_y_num);
extern void nvt_read_get_num_mdata(uint32_t xdata_addr, int32_t *buffer,
				   uint32_t num);
int32_t nvt_mp_parse_dt(struct device_node *root,
			const char *node_compatible);

#define show_print(ker_pf, m, str, str_args...) do {	\
	if (ker_pf && !nvt_mp_test_result_printed)	\
		pr_info(str, ##str_args);	\
	if (m)	\
		seq_printf(m, str, ##str_args);	\
} while (0)

#if SPI_FLASH
/*******************************************************
Description:
	Novatek touchscreen set mp settings mode.

return:
	Executive outcomes. 0---succeed. -EAGAIN---Fail
*******************************************************/
int32_t nvt_mp_settings(uint8_t tvcl_mode, uint8_t ibias_mode)
{
	uint8_t buf[5] = {0}, i = 0;
	const int32_t retry = 5;

	if (!tvcl_mode || !ibias_mode) {
		NVT_LOG("mp tvcl/ibias setting not supported, ignore\n");
		return 0;
	}

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0x4D;
	buf[2] = 0x00;
	buf[3] = tvcl_mode;
	buf[4] = ibias_mode;
	CTP_SPI_WRITE(ts->client, buf, 5);

	for (i = 0; i < retry; i++) {
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(10000, 10000);
	}

	if (i >= retry) {
		NVT_ERR("Failed to set tvcl_mode : %d ibias_mode : %d\n",
				tvcl_mode, ibias_mode);
	} else {
		ts->mp_tvcl_mode = tvcl_mode;
		ts->mp_ibias_mode = ibias_mode;
		NVT_LOG("Set tvcl_mode : %d ibias_mode : %d\n",
				tvcl_mode, ibias_mode);
	}

	return 0;
}
#endif // SPI_FLASH

/*******************************************************
Description:
	Novatek touchscreen allocate buffer for mp selftest.

return:
	Executive outcomes. 0---succeed. -12---Out of memory
*******************************************************/
static int nvt_mp_buffer_init(void)
{
	size_t RecordResult_BufSize = X_Y_DIMENSION_MAX + IC_KEY_CFG_SIZE;
	size_t RawData_BufSize = (X_Y_DIMENSION_MAX +
					IC_KEY_CFG_SIZE) * sizeof(int32_t);
	size_t Pen_RecordResult_BufSize = PEN_X_Y_DIMENSION_MAX;
	size_t Pen_RawData_BufSize = PEN_X_Y_DIMENSION_MAX * sizeof(int32_t);

	RecordResult_Short = (uint8_t *)kzalloc(RecordResult_BufSize, GFP_KERNEL);
	if (!RecordResult_Short) {
		NVT_ERR("kzalloc for RecordResult_Short failed!\n");
		return -ENOMEM;
	}

	RecordResult_Open = (uint8_t *)kzalloc(RecordResult_BufSize, GFP_KERNEL);
	if (!RecordResult_Open) {
		NVT_ERR("kzalloc for RecordResult_Open failed!\n");
		return -ENOMEM;
	}

	RecordResult_FW_Rawdata = (uint8_t *)kzalloc(RecordResult_BufSize,
				GFP_KERNEL);
	if (!RecordResult_FW_Rawdata) {
		NVT_ERR("kzalloc for RecordResult_FW_Rawdata failed!\n");
		return -ENOMEM;
	}

	RecordResult_FW_CC = (uint8_t *)kzalloc(RecordResult_BufSize, GFP_KERNEL);
	if (!RecordResult_FW_CC) {
		NVT_ERR("kzalloc for RecordResult_FW_CC failed!\n");
		return -ENOMEM;
	}

	RecordResult_FW_DiffMax = (uint8_t *)kzalloc(RecordResult_BufSize,
				  GFP_KERNEL);
	if (!RecordResult_FW_DiffMax) {
		NVT_ERR("kzalloc for RecordResult_FW_DiffMax failed!\n");
		return -ENOMEM;
	}

	RecordResult_FW_DiffMin = (uint8_t *)kzalloc(RecordResult_BufSize,
				  GFP_KERNEL);
	if (!RecordResult_FW_DiffMin) {
		NVT_ERR("kzalloc for RecordResult_FW_DiffMin failed!\n");
		return -ENOMEM;
	}

	RecordResult_FW_DiffP2P = (uint8_t *)kzalloc(RecordResult_BufSize,
				  GFP_KERNEL);
	if (!RecordResult_FW_DiffP2P) {
		NVT_ERR("kzalloc for RecordResult_FW_DiffP2P failed!\n");
		return -ENOMEM;
	}

	if (ts->pen_support) {
		RecordResult_PenTipX_Raw = (uint8_t *)kzalloc(Pen_RecordResult_BufSize,
					   GFP_KERNEL);
		if (!RecordResult_PenTipX_Raw) {
			NVT_ERR("kzalloc for RecordResult_PenTipX_Raw failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenTipY_Raw = (uint8_t *)kzalloc(Pen_RecordResult_BufSize,
					   GFP_KERNEL);
		if (!RecordResult_PenTipY_Raw) {
			NVT_ERR("kzalloc for RecordResult_PenTipY_Raw failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenRingX_Raw = (uint8_t *)kzalloc(Pen_RecordResult_BufSize,
					    GFP_KERNEL);
		if (!RecordResult_PenRingX_Raw) {
			NVT_ERR("kzalloc for RecordResult_PenRingX_Raw failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenRingY_Raw = (uint8_t *)kzalloc(Pen_RecordResult_BufSize,
					    GFP_KERNEL);
		if (!RecordResult_PenRingY_Raw) {
			NVT_ERR("kzalloc for RecordResult_PenRingY_Raw failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenTipX_DiffMax = (uint8_t *)kzalloc(Pen_RecordResult_BufSize,
					       GFP_KERNEL);
		if (!RecordResult_PenTipX_DiffMax) {
			NVT_ERR("kzalloc for RecordResult_PenTipX_DiffMax failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenTipX_DiffMin = (uint8_t *)kzalloc(Pen_RecordResult_BufSize,
					       GFP_KERNEL);
		if (!RecordResult_PenTipX_DiffMin) {
			NVT_ERR("kzalloc for RecordResult_PenTipX_DiffMin failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenTipX_DiffP2P = (uint8_t *)kzalloc(Pen_RecordResult_BufSize,
					       GFP_KERNEL);
		if (!RecordResult_PenTipX_DiffP2P) {
			NVT_ERR("kzalloc for RecordResult_PenTipX_DiffP2P failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenTipY_DiffMax = (uint8_t *)kzalloc(Pen_RecordResult_BufSize,
					       GFP_KERNEL);
		if (!RecordResult_PenTipY_DiffMax) {
			NVT_ERR("kzalloc for RecordResult_PenTipY_DiffMax failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenTipY_DiffMin = (uint8_t *)kzalloc(Pen_RecordResult_BufSize,
					       GFP_KERNEL);
		if (!RecordResult_PenTipY_DiffMin) {
			NVT_ERR("kzalloc for RecordResult_PenTipY_DiffMin failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenTipY_DiffP2P = (uint8_t *)kzalloc(Pen_RecordResult_BufSize,
					       GFP_KERNEL);
		if (!RecordResult_PenTipY_DiffP2P) {
			NVT_ERR("kzalloc for RecordResult_PenTipY_DiffP2P failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenRingX_DiffMax = (uint8_t *)kzalloc(
							Pen_RecordResult_BufSize, GFP_KERNEL);
		if (!RecordResult_PenRingX_DiffMax) {
			NVT_ERR("kzalloc for RecordResult_PenRingX_DiffMax failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenRingX_DiffMin = (uint8_t *)kzalloc(
							Pen_RecordResult_BufSize, GFP_KERNEL);
		if (!RecordResult_PenRingX_DiffMin) {
			NVT_ERR("kzalloc for RecordResult_PenRingX_DiffMin failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenRingX_DiffP2P = (uint8_t *)kzalloc(
							Pen_RecordResult_BufSize, GFP_KERNEL);
		if (!RecordResult_PenRingX_DiffP2P) {
			NVT_ERR("kzalloc for RecordResult_PenRingX_DiffP2P failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenRingY_DiffMax = (uint8_t *)kzalloc(
							Pen_RecordResult_BufSize, GFP_KERNEL);
		if (!RecordResult_PenRingY_DiffMax) {
			NVT_ERR("kzalloc for RecordResult_PenRingY_DiffMax failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenRingY_DiffMin = (uint8_t *)kzalloc(
							Pen_RecordResult_BufSize, GFP_KERNEL);
		if (!RecordResult_PenRingY_DiffMin) {
			NVT_ERR("kzalloc for RecordResult_PenRingY_DiffMin failed!\n");
			return -ENOMEM;
		}

		RecordResult_PenRingY_DiffP2P = (uint8_t *)kzalloc(
							Pen_RecordResult_BufSize, GFP_KERNEL);
		if (!RecordResult_PenRingY_DiffP2P) {
			NVT_ERR("kzalloc for RecordResult_PenRingY_DiffP2P failed!\n");
			return -ENOMEM;
		}

		RecordResult_Pen_Rx_Max = (uint8_t *)kzalloc(PEN_RX_MAX_BUFSIZE, GFP_KERNEL);
		if (!RecordResult_Pen_Rx_Max) {
			NVT_ERR("kzalloc for RecordResult_Pen_Rx_Max failed!\n");
			return -ENOMEM;
		}
	} /* if (ts->pen_support) */

	RawData_Short = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
	if (!RawData_Short) {
		NVT_ERR("kzalloc for RawData_Short failed!\n");
		return -ENOMEM;
	}

	RawData_Open = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
	if (!RawData_Open) {
		NVT_ERR("kzalloc for RawData_Open failed!\n");
		return -ENOMEM;
	}

	RawData_Diff = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
	if (!RawData_Diff) {
		NVT_ERR("kzalloc for RawData_Diff failed!\n");
		return -ENOMEM;
	}

	RawData_Diff_Min = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
	if (!RawData_Diff_Min) {
		NVT_ERR("kzalloc for RawData_Diff_Min failed!\n");
		return -ENOMEM;
	}

	RawData_Diff_Max = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
	if (!RawData_Diff_Max) {
		NVT_ERR("kzalloc for RawData_Diff_Max failed!\n");
		return -ENOMEM;
	}

	RawData_Diff_P2P = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
	if (!RawData_Diff_P2P) {
		NVT_ERR("kzalloc for RawData_Diff_P2P failed!\n");
		return -ENOMEM;
	}

	RawData_FW_Rawdata = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
	if (!RawData_FW_Rawdata) {
		NVT_ERR("kzalloc for RawData_FW_Rawdata failed!\n");
		return -ENOMEM;
	}

	RawData_FW_CC = (int32_t *)kzalloc(RawData_BufSize, GFP_KERNEL);
	if (!RawData_FW_CC) {
		NVT_ERR("kzalloc for RawData_FW_CC failed!\n");
		return -ENOMEM;
	}

	if (ts->pen_support) {
		RawData_PenTipX_Raw = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
		if (!RawData_PenTipX_Raw) {
			NVT_ERR("kzalloc for RawData_PenTipX_Raw failed!\n");
			return -ENOMEM;
		}

		RawData_PenTipY_Raw = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
		if (!RawData_PenTipY_Raw) {
			NVT_ERR("kzalloc for RawData_PenTipY_Raw failed!\n");
			return -ENOMEM;
		}

		RawData_PenRingX_Raw = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
		if (!RawData_PenRingX_Raw) {
			NVT_ERR("kzalloc for RawData_PenRingX_Raw failed!\n");
			return -ENOMEM;
		}

		RawData_PenRingY_Raw = (int32_t *)kzalloc(Pen_RawData_BufSize, GFP_KERNEL);
		if (!RawData_PenRingY_Raw) {
			NVT_ERR("kzalloc for RawData_PenRingY_Raw failed!\n");
			return -ENOMEM;
		}

		RawData_PenTipX_DiffMax = (int32_t *)kzalloc(Pen_RawData_BufSize,
					  GFP_KERNEL);
		if (!RawData_PenTipX_DiffMax) {
			NVT_ERR("kzalloc for RawData_PenTipX_DiffMax failed!\n");
			return -ENOMEM;
		}

		RawData_PenTipX_DiffMin = (int32_t *)kzalloc(Pen_RawData_BufSize,
					  GFP_KERNEL);
		if (!RawData_PenTipX_DiffMin) {
			NVT_ERR("kzalloc for RawData_PenTipX_DiffMin failed!\n");
			return -ENOMEM;
		}

		RawData_PenTipX_DiffP2P = (int32_t *)kzalloc(Pen_RawData_BufSize,
					  GFP_KERNEL);
		if (!RawData_PenTipX_DiffP2P) {
			NVT_ERR("kzalloc for RawData_PenTipX_DiffP2P failed!\n");
			return -ENOMEM;
		}

		RawData_PenTipY_DiffMax = (int32_t *)kzalloc(Pen_RawData_BufSize,
					  GFP_KERNEL);
		if (!RawData_PenTipY_DiffMax) {
			NVT_ERR("kzalloc for RawData_PenTipY_DiffMax failed!\n");
			return -ENOMEM;
		}

		RawData_PenTipY_DiffMin = (int32_t *)kzalloc(Pen_RawData_BufSize,
					  GFP_KERNEL);
		if (!RawData_PenTipY_DiffMin) {
			NVT_ERR("kzalloc for RawData_PenTipY_DiffMin failed!\n");
			return -ENOMEM;
		}

		RawData_PenTipY_DiffP2P = (int32_t *)kzalloc(Pen_RawData_BufSize,
					  GFP_KERNEL);
		if (!RawData_PenTipY_DiffP2P) {
			NVT_ERR("kzalloc for RawData_PenTipY_DiffP2P failed!\n");
			return -ENOMEM;
		}

		RawData_PenRingX_DiffMax = (int32_t *)kzalloc(Pen_RawData_BufSize,
					   GFP_KERNEL);
		if (!RawData_PenRingX_DiffMax) {
			NVT_ERR("kzalloc for RawData_PenRingX_DiffMax failed!\n");
			return -ENOMEM;
		}

		RawData_PenRingX_DiffMin = (int32_t *)kzalloc(Pen_RawData_BufSize,
					   GFP_KERNEL);
		if (!RawData_PenRingX_DiffMin) {
			NVT_ERR("kzalloc for RawData_PenRingX_DiffMin failed!\n");
			return -ENOMEM;
		}

		RawData_PenRingX_DiffP2P = (int32_t *)kzalloc(Pen_RawData_BufSize,
					   GFP_KERNEL);
		if (!RawData_PenRingX_DiffP2P) {
			NVT_ERR("kzalloc for RawData_PenRingX_DiffP2P failed!\n");
			return -ENOMEM;
		}

		RawData_PenRingY_DiffMax = (int32_t *)kzalloc(Pen_RawData_BufSize,
					   GFP_KERNEL);
		if (!RawData_PenRingY_DiffMax) {
			NVT_ERR("kzalloc for RawData_PenRingY_DiffMax failed!\n");
			return -ENOMEM;
		}

		RawData_PenRingY_DiffMin = (int32_t *)kzalloc(Pen_RawData_BufSize,
					   GFP_KERNEL);
		if (!RawData_PenRingY_DiffMin) {
			NVT_ERR("kzalloc for RawData_PenRingY_DiffMin failed!\n");
			return -ENOMEM;
		}

		RawData_PenRingY_DiffP2P = (int32_t *)kzalloc(Pen_RawData_BufSize,
					   GFP_KERNEL);
		if (!RawData_PenRingY_DiffP2P) {
			NVT_ERR("kzalloc for RawData_PenRingY_DiffP2P failed!\n");
			return -ENOMEM;
		}

		RawData_Pen_Rx_Max = (int32_t *)kzalloc(PEN_RX_MAX_BUFSIZE, GFP_KERNEL);
		if (!RawData_Pen_Rx_Max) {
			NVT_ERR("kzalloc for RawData_Pen_Rx_Max failed!\n");
			return -ENOMEM;
		}
	} /* if (ts->pen_support) */

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen free buffer for mp selftest.

return:
	n.a.
*******************************************************/
static void nvt_mp_buffer_deinit(void)
{
	if (RecordResult_Short) {
		kfree(RecordResult_Short);
		RecordResult_Short = NULL;
	}

	if (RecordResult_Open) {
		kfree(RecordResult_Open);
		RecordResult_Open = NULL;
	}

	if (RecordResult_FW_Rawdata) {
		kfree(RecordResult_FW_Rawdata);
		RecordResult_FW_Rawdata = NULL;
	}

	if (RecordResult_FW_CC) {
		kfree(RecordResult_FW_CC);
		RecordResult_FW_CC = NULL;
	}

	if (RecordResult_FW_DiffMax) {
		kfree(RecordResult_FW_DiffMax);
		RecordResult_FW_DiffMax = NULL;
	}

	if (RecordResult_FW_DiffMin) {
		kfree(RecordResult_FW_DiffMin);
		RecordResult_FW_DiffMin = NULL;
	}

	if (RecordResult_FW_DiffP2P) {
		kfree(RecordResult_FW_DiffP2P);
		RecordResult_FW_DiffP2P = NULL;
	}

	if (ts->pen_support) {
		if (RecordResult_PenTipX_Raw) {
			kfree(RecordResult_PenTipX_Raw);
			RecordResult_PenTipX_Raw = NULL;
		}

		if (RecordResult_PenTipY_Raw) {
			kfree(RecordResult_PenTipY_Raw);
			RecordResult_PenTipY_Raw = NULL;
		}

		if (RecordResult_PenRingX_Raw) {
			kfree(RecordResult_PenRingX_Raw);
			RecordResult_PenRingX_Raw = NULL;
		}

		if (RecordResult_PenRingY_Raw) {
			kfree(RecordResult_PenRingY_Raw);
			RecordResult_PenRingY_Raw = NULL;
		}

		if (RecordResult_PenTipX_DiffMax) {
			kfree(RecordResult_PenTipX_DiffMax);
			RecordResult_PenTipX_DiffMax = NULL;
		}

		if (RecordResult_PenTipX_DiffMin) {
			kfree(RecordResult_PenTipX_DiffMin);
			RecordResult_PenTipX_DiffMin = NULL;
		}

		if (RecordResult_PenTipX_DiffP2P) {
			kfree(RecordResult_PenTipX_DiffP2P);
			RecordResult_PenTipX_DiffP2P = NULL;
		}

		if (RecordResult_PenTipY_DiffMax) {
			kfree(RecordResult_PenTipY_DiffMax);
			RecordResult_PenTipY_DiffMax = NULL;
		}

		if (RecordResult_PenTipY_DiffMin) {
			kfree(RecordResult_PenTipY_DiffMin);
			RecordResult_PenTipY_DiffMin = NULL;
		}

		if (RecordResult_PenTipY_DiffP2P) {
			kfree(RecordResult_PenTipY_DiffP2P);
			RecordResult_PenTipY_DiffP2P = NULL;
		}

		if (RecordResult_PenRingX_DiffMax) {
			kfree(RecordResult_PenRingX_DiffMax);
			RecordResult_PenRingX_DiffMax = NULL;
		}

		if (RecordResult_PenRingX_DiffMin) {
			kfree(RecordResult_PenRingX_DiffMin);
			RecordResult_PenRingX_DiffMin = NULL;
		}

		if (RecordResult_PenRingX_DiffP2P) {
			kfree(RecordResult_PenRingX_DiffP2P);
			RecordResult_PenRingX_DiffP2P = NULL;
		}

		if (RecordResult_PenRingY_DiffMax) {
			kfree(RecordResult_PenRingY_DiffMax);
			RecordResult_PenRingY_DiffMax = NULL;
		}

		if (RecordResult_PenRingY_DiffMin) {
			kfree(RecordResult_PenRingY_DiffMin);
			RecordResult_PenRingY_DiffMin = NULL;
		}

		if (RecordResult_PenRingY_DiffP2P) {
			kfree(RecordResult_PenRingY_DiffP2P);
			RecordResult_PenRingY_DiffP2P = NULL;
		}

		if (RecordResult_Pen_Rx_Max) {
			kfree(RecordResult_Pen_Rx_Max);
			RecordResult_Pen_Rx_Max = NULL;
		}
	} /* if (ts->pen_support) */

	if (RawData_Short) {
		kfree(RawData_Short);
		RawData_Short = NULL;
	}

	if (RawData_Open) {
		kfree(RawData_Open);
		RawData_Open = NULL;
	}

	if (RawData_Diff) {
		kfree(RawData_Diff);
		RawData_Diff = NULL;
	}

	if (RawData_Diff_Min) {
		kfree(RawData_Diff_Min);
		RawData_Diff_Min = NULL;
	}

	if (RawData_Diff_Max) {
		kfree(RawData_Diff_Max);
		RawData_Diff_Max = NULL;
	}

	if (RawData_Diff_P2P) {
		kfree(RawData_Diff_P2P);
		RawData_Diff_P2P = NULL;
	}

	if (RawData_FW_Rawdata) {
		kfree(RawData_FW_Rawdata);
		RawData_FW_Rawdata = NULL;
	}

	if (RawData_FW_CC) {
		kfree(RawData_FW_CC);
		RawData_FW_CC = NULL;
	}

	if (ts->pen_support) {
		if (RawData_PenTipX_Raw) {
			kfree(RawData_PenTipX_Raw);
			RawData_PenTipX_Raw = NULL;
		}

		if (RawData_PenTipY_Raw) {
			kfree(RawData_PenTipY_Raw);
			RawData_PenTipY_Raw = NULL;
		}

		if (RawData_PenRingX_Raw) {
			kfree(RawData_PenRingX_Raw);
			RawData_PenRingX_Raw = NULL;
		}

		if (RawData_PenRingY_Raw) {
			kfree(RawData_PenRingY_Raw);
			RawData_PenRingY_Raw = NULL;
		}

		if (RawData_PenTipX_DiffMax) {
			kfree(RawData_PenTipX_DiffMax);
			RawData_PenTipX_DiffMax = NULL;
		}

		if (RawData_PenTipX_DiffMin) {
			kfree(RawData_PenTipX_DiffMin);
			RawData_PenTipX_DiffMin = NULL;
		}

		if (RawData_PenTipX_DiffP2P) {
			kfree(RawData_PenTipX_DiffP2P);
			RawData_PenTipX_DiffP2P = NULL;
		}

		if (RawData_PenTipY_DiffMax) {
			kfree(RawData_PenTipY_DiffMax);
			RawData_PenTipY_DiffMax = NULL;
		}

		if (RawData_PenTipY_DiffMin) {
			kfree(RawData_PenTipY_DiffMin);
			RawData_PenTipY_DiffMin = NULL;
		}

		if (RawData_PenTipY_DiffP2P) {
			kfree(RawData_PenTipY_DiffP2P);
			RawData_PenTipY_DiffP2P = NULL;
		}

		if (RawData_PenRingX_DiffMax) {
			kfree(RawData_PenRingX_DiffMax);
			RawData_PenRingX_DiffMax = NULL;
		}

		if (RawData_PenRingX_DiffMin) {
			kfree(RawData_PenRingX_DiffMin);
			RawData_PenRingX_DiffMin = NULL;
		}

		if (RawData_PenRingX_DiffP2P) {
			kfree(RawData_PenRingX_DiffP2P);
			RawData_PenRingX_DiffP2P = NULL;
		}

		if (RawData_PenRingY_DiffMax) {
			kfree(RawData_PenRingY_DiffMax);
			RawData_PenRingY_DiffMax = NULL;
		}

		if (RawData_PenRingY_DiffMin) {
			kfree(RawData_PenRingY_DiffMin);
			RawData_PenRingY_DiffMin = NULL;
		}

		if (RawData_PenRingY_DiffP2P) {
			kfree(RawData_PenRingY_DiffP2P);
			RawData_PenRingY_DiffP2P = NULL;
		}

		if (RawData_Pen_Rx_Max) {
			kfree(RawData_Pen_Rx_Max);
			RawData_Pen_Rx_Max = NULL;
		}
	} /* if (ts->pen_support) */
}

static void nvt_print_data_log_in_one_line(int32_t *data, int32_t data_num)
{
	char *tmp_log = NULL;
	int32_t i = 0, len = 0;

	if (nvt_mp_test_result_printed)
		return;

	tmp_log = (char *)kzalloc(data_num * 7 + 1, GFP_KERNEL);
	if (!tmp_log) {
		NVT_ERR("kzalloc for tmp_log failed!\n ");
		return;
	}

	for (i = 0; i < data_num; i++) {
		len += snprintf(tmp_log + i * 7, data_num * 7 + 1 - len, "%6d,", data[i]);
	}
	tmp_log[data_num * 7] = '\0';
	pr_info("%s", tmp_log);
	if (tmp_log) {
		kfree(tmp_log);
		tmp_log = NULL;
	}

	return;
}

static void nvt_print_result_log_in_one_line(uint8_t *result,
		int32_t result_num)
{
	char *tmp_log = NULL;
	int32_t i = 0, len = 0;

	if (nvt_mp_test_result_printed)
		return;

	tmp_log = (char *)kzalloc(result_num * 6 + 1, GFP_KERNEL);
	if (!tmp_log) {
		NVT_ERR("kzalloc for tmp_log failed!\n ");
		return;
	}

	for (i = 0; i < result_num; i++) {
		len += snprintf(tmp_log + i * 6, result_num * 6 + 1 - len, "0x%02X, ",
				result[i]);
	}
	tmp_log[result_num * 6] = '\0';
	pr_info("%s", tmp_log);
	if (tmp_log) {
		kfree(tmp_log);
		tmp_log = NULL;
	}

	return;
}

/*******************************************************
Description:
	Novatek touchscreen self-test criteria print function.

return:
	n.a.
*******************************************************/
static void nvt_print_lmt_array(int32_t *array, int32_t x_ch, int32_t y_ch)
{
	int32_t j = 0;

	for (j = 0; j < y_ch; j++) {
		nvt_print_data_log_in_one_line(array + j * x_ch, x_ch);
		pr_info("\n");
	}
#if TOUCH_KEY_NUM > 0
	nvt_print_data_log_in_one_line(array + y_ch * x_ch, Key_Channel);
	pr_info("\n");
#endif /* #if TOUCH_KEY_NUM > 0 */
}

static void nvt_print_criteria(void)
{
	/* force to suppress the logs for default criteria */
	return;

	NVT_LOGD("++\n");

	//---PS_Config_Lmt_Short_Rawdata---
	pr_info("PS_Config_Lmt_Short_Rawdata_P:\n");
	nvt_print_lmt_array(PS_Config_Lmt_Short_Rawdata_P, X_Channel, Y_Channel);
	pr_info("PS_Config_Lmt_Short_Rawdata_N:\n");
	nvt_print_lmt_array(PS_Config_Lmt_Short_Rawdata_N, X_Channel, Y_Channel);

	//---PS_Config_Lmt_Open_Rawdata---
	pr_info("PS_Config_Lmt_Open_Rawdata_P:\n");
	nvt_print_lmt_array(PS_Config_Lmt_Open_Rawdata_P, X_Channel, Y_Channel);
	pr_info("PS_Config_Lmt_Open_Rawdata_N:\n");
	nvt_print_lmt_array(PS_Config_Lmt_Open_Rawdata_N, X_Channel, Y_Channel);

	//---PS_Config_Lmt_FW_Rawdata---
	pr_info("PS_Config_Lmt_FW_Rawdata_P:\n");
	nvt_print_lmt_array(PS_Config_Lmt_FW_Rawdata_P, X_Channel, Y_Channel);
	pr_info("PS_Config_Lmt_FW_Rawdata_N:\n");
	nvt_print_lmt_array(PS_Config_Lmt_FW_Rawdata_N, X_Channel, Y_Channel);

	//---PS_Config_Lmt_FW_CC---
	pr_info("PS_Config_Lmt_FW_CC_P:\n");
	nvt_print_lmt_array(PS_Config_Lmt_FW_CC_P, X_Channel, Y_Channel);
	pr_info("PS_Config_Lmt_FW_CC_N:\n");
	nvt_print_lmt_array(PS_Config_Lmt_FW_CC_N, X_Channel, Y_Channel);

	//---PS_Config_Lmt_FW_Diff---
	pr_info("PS_Config_Lmt_FW_Diff_P:\n");
	nvt_print_lmt_array(PS_Config_Lmt_FW_Diff_P, X_Channel, Y_Channel);
	pr_info("PS_Config_Lmt_FW_Diff_N:\n");
	nvt_print_lmt_array(PS_Config_Lmt_FW_Diff_N, X_Channel, Y_Channel);

	if (ts->pen_support) {
		//---PS_Config_Lmt_PenTipX_FW_Raw---
		pr_info("PS_Config_Lmt_PenTipX_FW_Raw_P:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenTipX_FW_Raw_P, ts->x_num,
				    ts->y_gang_num);
		pr_info("PS_Config_Lmt_PenTipX_FW_Raw_N:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenTipX_FW_Raw_N, ts->x_num,
				    ts->y_gang_num);

		//---PS_Config_Lmt_PenTipY_FW_Raw---
		pr_info("PS_Config_Lmt_PenTipY_FW_Raw_P:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenTipY_FW_Raw_P, ts->x_gang_num,
				    ts->y_num);
		pr_info("PS_Config_Lmt_PenTipY_FW_Raw_N:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenTipY_FW_Raw_N, ts->x_gang_num,
				    ts->y_num);

		//---PS_Config_Lmt_PenRingX_FW_Raw---
		pr_info("PS_Config_Lmt_PenRingX_FW_Raw_P:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenRingX_FW_Raw_P, ts->x_num,
				    ts->y_gang_num);
		pr_info("PS_Config_Lmt_PenRingX_FW_Raw_N:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenRingX_FW_Raw_N, ts->x_num,
				    ts->y_gang_num);

		//---PS_Config_Lmt_PenRingY_FW_Raw---
		pr_info("PS_Config_Lmt_PenRingY_FW_Raw_P:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenRingY_FW_Raw_P, ts->x_gang_num,
				    ts->y_num);
		pr_info("PS_Config_Lmt_PenRingY_FW_Raw_N:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenRingY_FW_Raw_N, ts->x_gang_num,
				    ts->y_num);

		//---PS_Config_Lmt_PenTipX_FW_Diff---
		pr_info("PS_Config_Lmt_PenTipX_FW_Diff_P:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenTipX_FW_Diff_P, ts->x_num,
				    ts->y_gang_num);
		pr_info("PS_Config_Lmt_PenTipX_FW_Diff_N:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenTipX_FW_Diff_N, ts->x_num,
				    ts->y_gang_num);

		//---PS_Config_Lmt_PenTipY_FW_Diff---
		pr_info("PS_Config_Lmt_PenTipY_FW_Diff_P:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenTipY_FW_Diff_P, ts->x_gang_num,
				    ts->y_num);
		pr_info("PS_Config_Lmt_PenTipY_FW_Diff_N:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenTipY_FW_Diff_N, ts->x_gang_num,
				    ts->y_num);

		//---PS_Config_Lmt_PenRingX_FW_Diff---
		pr_info("PS_Config_Lmt_PenRingX_FW_Diff_P:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenRingX_FW_Diff_P, ts->x_num,
				    ts->y_gang_num);
		pr_info("PS_Config_Lmt_PenRingX_FW_Diff_N:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenRingX_FW_Diff_N, ts->x_num,
				    ts->y_gang_num);

		//---PS_Config_Lmt_PenRingY_FW_Diff---
		pr_info("PS_Config_Lmt_PenRingY_FW_Diff_P:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenRingY_FW_Diff_P, ts->x_gang_num,
				    ts->y_num);
		pr_info("PS_Config_Lmt_PenRingY_FW_Diff_N:\n");
		nvt_print_lmt_array(PS_Config_Lmt_PenRingY_FW_Diff_N, ts->x_gang_num,
				    ts->y_num);

		//---PS_Config_Lmt_Pen_Rx_Max---
		pr_info("PS_Config_Lmt_Pen_Rx_Max_P:\n");
		nvt_print_lmt_array(PS_Config_Lmt_Pen_Rx_Max_P,
				PEN_RX_MAX_X_LEN, PEN_RX_MAX_Y_LEN);
		pr_info("PS_Config_Lmt_Pen_Rx_Max_N:\n");
		nvt_print_lmt_array(PS_Config_Lmt_Pen_Rx_Max_N,
				PEN_RX_MAX_X_LEN, PEN_RX_MAX_Y_LEN);
	} /* if (ts->pen_support) */

	NVT_LOGD("--\n");
}

static void nvt_print_rawdata(int32_t *rawdata, uint8_t x_ch, uint8_t y_ch)
{
#if NVT_MP_DEBUG
	uint32_t y;

	if (nvt_mp_test_result_printed)
		return;

	pr_info("%s:++\n", __func__);

	for (y = 0; y < y_ch; y++) {
		nvt_print_data_log_in_one_line(rawdata + y * x_ch, x_ch);
		pr_info("\n");
	}

	pr_info("%s:--\n", __func__);
#endif
}

static int32_t nvt_polling_hand_shake_status(void)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 250;

	usleep_range(20000, 20000);

	for (i = 0; i < retry; i++) {
		//---set xdata index to EVENT BUF ADDR---
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR |
			     EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

		//---read fw status---
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_READ(ts->client, buf, 2);

		if ((buf[1] == 0xA0) || (buf[1] == 0xA1))
			break;

		usleep_range(10000, 10000);
	}

	if (i >= retry) {
		NVT_ERR("polling hand shake status failed, buf[1]=0x%02X\n", buf[1]);

		// Read back 5 bytes from offset EVENT_MAP_HOST_CMD for debug check
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x00;
		buf[2] = 0x00;
		buf[3] = 0x00;
		buf[4] = 0x00;
		buf[5] = 0x00;
		CTP_SPI_READ(ts->client, buf, 6);
		NVT_ERR("Read back 5 bytes from offset EVENT_MAP_HOST_CMD: 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
			buf[1], buf[2], buf[3], buf[4], buf[5]);

		return -EPERM;
	} else {
		return 0;
	}
}

int8_t nvt_switch_FreqHopEnDis(uint8_t FreqHopEnDis)
{
	uint8_t buf[8] = {0};
	uint8_t retry = 0;
	int8_t ret = 0;

	NVT_LOGD("++\n");

	for (retry = 0; retry < 20; retry++) {
		//---set xdata index to EVENT BUF ADDR---
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

		//---switch FreqHopEnDis---
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = FreqHopEnDis;
		CTP_SPI_WRITE(ts->client, buf, 2);

		msleep(35);

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);

		if (buf[1] == 0x00)
			break;
	}

	if (unlikely(retry == 20)) {
		NVT_ERR("switch FreqHopEnDis 0x%02X failed, buf[1]=0x%02X\n", FreqHopEnDis,
			buf[1]);
		ret = -1;
	}

	NVT_LOGD("--\n");

	return ret;
}

static int32_t nvt_read_baseline(int32_t *xdata)
{
	uint8_t x_num = 0;
	uint8_t y_num = 0;
	uint32_t x = 0;
	uint32_t y = 0;
	int32_t iArrayIndex = 0;
#if TOUCH_KEY_NUM > 0
	int32_t k = 0;
#endif /* #if TOUCH_KEY_NUM > 0 */

	NVT_LOGD("++\n");

	nvt_read_mdata(ts->mmap->BASELINE_ADDR, ts->mmap->BASELINE_BTN_ADDR);

	nvt_get_mdata(xdata, &x_num, &y_num);

	for (y = 0; y < y_num; y++) {
		for (x = 0; x < x_num; x++) {
			iArrayIndex = y * x_num + x;
			xdata[iArrayIndex] = (int16_t)xdata[iArrayIndex];
		}
	}
#if TOUCH_KEY_NUM > 0
	for (k = 0; k < Key_Channel; k++) {
		iArrayIndex = Y_Channel * X_Channel + k;
		xdata[iArrayIndex] = (int16_t)xdata[iArrayIndex];
	}
#endif /* #if TOUCH_KEY_NUM > 0 */

	pr_info("%s:\n", __func__);
	nvt_print_rawdata(xdata, X_Channel, Y_Channel);

	NVT_LOGD("--\n");

	return 0;
}

static int32_t nvt_read_CC(int32_t *xdata)
{
	uint8_t x_num = 0;
	uint8_t y_num = 0;
	uint32_t x = 0;
	uint32_t y = 0;
	int32_t iArrayIndex = 0;
#if TOUCH_KEY_NUM > 0
	int32_t k = 0;
#endif /* #if TOUCH_KEY_NUM > 0 */

	NVT_LOGD("++\n");

	if (nvt_get_fw_pipe() == 0)
		nvt_read_mdata(ts->mmap->DIFF_PIPE1_ADDR, ts->mmap->DIFF_BTN_PIPE1_ADDR);
	else
		nvt_read_mdata(ts->mmap->DIFF_PIPE0_ADDR, ts->mmap->DIFF_BTN_PIPE0_ADDR);

	nvt_get_mdata(xdata, &x_num, &y_num);

	for (y = 0; y < y_num; y++) {
		for (x = 0; x < x_num; x++) {
			iArrayIndex = y * x_num + x;
			xdata[iArrayIndex] = (int16_t)xdata[iArrayIndex];
		}
	}
#if TOUCH_KEY_NUM > 0
	for (k = 0; k < Key_Channel; k++) {
		iArrayIndex = Y_Channel * X_Channel + k;
		xdata[iArrayIndex] = (int16_t)xdata[iArrayIndex];
	}
#endif /* #if TOUCH_KEY_NUM > 0 */

	pr_info("%s:\n", __func__);
	nvt_print_rawdata(xdata, X_Channel, Y_Channel);

	NVT_LOGD("--\n");

	return 0;
}

static int32_t nvt_read_pen_baseline(void)
{
	NVT_LOGD("++\n");

	nvt_read_get_num_mdata(ts->mmap->PEN_2D_BL_TIP_X_ADDR, RawData_PenTipX_Raw,
			       ts->x_num * ts->y_gang_num);
	nvt_read_get_num_mdata(ts->mmap->PEN_2D_BL_TIP_Y_ADDR, RawData_PenTipY_Raw,
			       ts->x_gang_num * ts->y_num);
	nvt_read_get_num_mdata(ts->mmap->PEN_2D_BL_RING_X_ADDR,
			       RawData_PenRingX_Raw, ts->x_num * ts->y_gang_num);
	nvt_read_get_num_mdata(ts->mmap->PEN_2D_BL_RING_Y_ADDR,
			       RawData_PenRingY_Raw, ts->x_gang_num * ts->y_num);

	pr_info("%s:RawData_PenTipX_Raw\n", __func__);
	nvt_print_rawdata(RawData_PenTipX_Raw, ts->x_num, ts->y_gang_num);
	pr_info("%s:RawData_PenTipY_Raw\n", __func__);
	nvt_print_rawdata(RawData_PenTipY_Raw, ts->x_gang_num, ts->y_num);
	pr_info("%s:RawData_PenRingX_Raw\n", __func__);
	nvt_print_rawdata(RawData_PenRingX_Raw, ts->x_num, ts->y_gang_num);
	pr_info("%s:RawData_PenRingY_Raw\n", __func__);
	nvt_print_rawdata(RawData_PenRingY_Raw, ts->x_gang_num, ts->y_num);

	NVT_LOGD("--\n");

	return 0;
}

static void nvt_enable_noise_collect(int32_t frame_num)
{
	uint8_t buf[8] = {0};

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

	//---enable noise collect---
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0x47;
	buf[2] = 0xAA;
	buf[3] = frame_num;
	buf[4] = 0x00;
	CTP_SPI_WRITE(ts->client, buf, 5);
}

static int32_t nvt_read_fw_noise(int32_t *xdata)
{
	uint8_t x_num = 0;
	uint8_t y_num = 0;
	uint32_t x = 0;
	uint32_t y = 0;
	int32_t iArrayIndex = 0;
	int32_t frame_num = 0;
	uint32_t rawdata_diff_min_offset = 0;
#if TOUCH_KEY_NUM > 0
	int32_t k = 0;
#endif /* #if TOUCH_KEY_NUM > 0 */
	int32_t i = 0;

	NVT_LOGD("++\n");

	//---Enter Test Mode---
	if (nvt_clear_fw_status()) {
		return -EAGAIN;
	}

	frame_num = PS_Config_Diff_Test_Frame / 10;
	if (frame_num <= 0)
		frame_num = 1;
	pr_info("%s: frame_num=%d\n", __func__, frame_num);
	nvt_enable_noise_collect(frame_num);
	// need wait PS_Config_Diff_Test_Frame * 8.3ms
	msleep(frame_num * 83);

	if (nvt_polling_hand_shake_status()) {
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0)
		nvt_read_mdata(ts->mmap->DIFF_PIPE0_ADDR, ts->mmap->DIFF_BTN_PIPE0_ADDR);
	else
		nvt_read_mdata(ts->mmap->DIFF_PIPE1_ADDR, ts->mmap->DIFF_BTN_PIPE1_ADDR);

	nvt_get_mdata(xdata, &x_num, &y_num);

	for (y = 0; y < y_num; y++) {
		for (x = 0; x < x_num; x++) {
			iArrayIndex = y * x_num + x;
			RawData_Diff_Max[iArrayIndex] = (int8_t)((xdata[iArrayIndex] >> 8) & 0xFF);
			RawData_Diff_Min[iArrayIndex] = (int8_t)(xdata[iArrayIndex] & 0xFF);
			RawData_Diff_P2P[iArrayIndex] = RawData_Diff_Max[iArrayIndex] - RawData_Diff_Min[iArrayIndex];
		}
	}
#if TOUCH_KEY_NUM > 0
	for (k = 0; k < Key_Channel; k++) {
		iArrayIndex = Y_Channel * X_Channel + k;
		RawData_Diff_Max[iArrayIndex] = (int8_t)((xdata[iArrayIndex] >> 8) & 0xFF);
		RawData_Diff_Min[iArrayIndex] = (int8_t)(xdata[iArrayIndex] & 0xFF);
		RawData_Diff_P2P[iArrayIndex] = RawData_Diff_Max[iArrayIndex] - RawData_Diff_Min[iArrayIndex];
	}
#endif /* #if TOUCH_KEY_NUM > 0 */

	if (ts->pen_support) {
		// get pen noise data
		nvt_read_get_num_mdata(ts->mmap->PEN_2D_DIFF_TIP_X_ADDR,
				RawData_PenTipX_DiffMax, ts->x_num * ts->y_gang_num);
		nvt_read_get_num_mdata(ts->mmap->PEN_2D_RAW_TIP_X_ADDR,
				RawData_PenTipX_DiffMin, ts->x_num * ts->y_gang_num);
		nvt_read_get_num_mdata(ts->mmap->PEN_2D_DIFF_TIP_Y_ADDR,
				RawData_PenTipY_DiffMax, ts->x_gang_num * ts->y_num);
		nvt_read_get_num_mdata(ts->mmap->PEN_2D_RAW_TIP_Y_ADDR,
				RawData_PenTipY_DiffMin, ts->x_gang_num * ts->y_num);
		nvt_read_get_num_mdata(ts->mmap->PEN_2D_DIFF_RING_X_ADDR,
				RawData_PenRingX_DiffMax, ts->x_num * ts->y_gang_num);
		nvt_read_get_num_mdata(ts->mmap->PEN_2D_RAW_RING_X_ADDR,
				RawData_PenRingX_DiffMin, ts->x_num * ts->y_gang_num);
		nvt_read_get_num_mdata(ts->mmap->PEN_2D_DIFF_RING_Y_ADDR,
				RawData_PenRingY_DiffMax, ts->x_gang_num * ts->y_num);
		nvt_read_get_num_mdata(ts->mmap->PEN_2D_RAW_RING_Y_ADDR,
				RawData_PenRingY_DiffMin, ts->x_gang_num * ts->y_num);
		nvt_read_get_num_mdata(ts->mmap->PEN_RX_ADDR, RawData_Pen_Rx_Max,
				PEN_RX_MAX_BUFSIZE);
		for (i = 0; i < ts->x_num * ts->y_gang_num; i++) {
			RawData_PenTipX_DiffP2P[i] = RawData_PenTipX_DiffMax[i] - RawData_PenTipX_DiffMin[i];
			RawData_PenRingX_DiffP2P[i] = RawData_PenRingX_DiffMax[i] - RawData_PenRingX_DiffMin[i];
		}
		for (i = 0; i < ts->x_gang_num * ts->y_num; i++) {
			RawData_PenTipY_DiffP2P[i] = RawData_PenTipY_DiffMax[i] - RawData_PenTipY_DiffMin[i];
			RawData_PenRingY_DiffP2P[i] = RawData_PenRingY_DiffMax[i] - RawData_PenRingY_DiffMin[i];
		}
	} /* if (ts->pen_support) */

	//---Leave Test Mode---
	nvt_change_mode(NORMAL_MODE);

	pr_info("%s:RawData_Diff_Max:\n", __func__);
	nvt_print_rawdata(RawData_Diff_Max, X_Channel, Y_Channel);

#if TOUCH_KEY_NUM > 0
	rawdata_diff_min_offset = Y_Channel * X_Channel * 7 + Y_Channel * 2 +
				  Key_Channel * 7 + 2;
#else
	rawdata_diff_min_offset = Y_Channel * X_Channel * 7 + Y_Channel * 2;
#endif /* #if TOUCH_KEY_NUM > 0 */
	pr_info("%s:RawData_Diff_Min:\n", __func__);
	nvt_print_rawdata(RawData_Diff_Min, X_Channel, Y_Channel);

	if (ts->pen_support) {
		pr_info("%s:RawData_PenTipX_DiffMax:\n", __func__);
		nvt_print_rawdata(RawData_PenTipX_DiffMax, ts->x_num, ts->y_gang_num);
		pr_info("%s:RawData_PenTipX_DiffMin:\n", __func__);
		nvt_print_rawdata(RawData_PenTipX_DiffMin, ts->x_num, ts->y_gang_num);
		pr_info("%s:RawData_PenTipY_DiffMax:\n", __func__);
		nvt_print_rawdata(RawData_PenTipY_DiffMax, ts->x_gang_num, ts->y_num);
		pr_info("%s:RawData_PenTipY_DiffMin:\n", __func__);
		nvt_print_rawdata(RawData_PenTipY_DiffMin, ts->x_gang_num, ts->y_num);
		pr_info("%s:RawData_PenRingX_DiffMax:\n", __func__);
		nvt_print_rawdata(RawData_PenRingX_DiffMax, ts->x_num, ts->y_gang_num);
		pr_info("%s:RawData_PenRingX_DiffMin:\n", __func__);
		nvt_print_rawdata(RawData_PenRingX_DiffMin, ts->x_num, ts->y_gang_num);
		pr_info("%s:RawData_PenRingY_DiffMax:\n", __func__);
		nvt_print_rawdata(RawData_PenRingY_DiffMax, ts->x_gang_num, ts->y_num);
		pr_info("%s:RawData_PenRingY_DiffMin:\n", __func__);
		nvt_print_rawdata(RawData_PenRingY_DiffMin, ts->x_gang_num, ts->y_num);
		pr_info("%s:RawData_Pen_Rx_Max:\n", __func__);
		nvt_print_rawdata(RawData_Pen_Rx_Max, PEN_RX_MAX_X_LEN, PEN_RX_MAX_Y_LEN);
	} /* if (ts->pen_support) */

	NVT_LOGD("--\n");

	return 0;
}

static void nvt_enable_open_test(void)
{
	uint8_t buf[8] = {0};

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

	//---enable open test---
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0x45;
	buf[2] = 0xAA;
	buf[3] = 0x02;
	buf[4] = 0x00;
	CTP_SPI_WRITE(ts->client, buf, 5);
}

static void nvt_enable_short_test(void)
{
	uint8_t buf[8] = {0};

	//---set xdata index to EVENT BUF ADDR---
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

	//---enable short test---
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = 0x43;
	buf[2] = 0xAA;
	buf[3] = 0x02;
	buf[4] = 0x00;
	CTP_SPI_WRITE(ts->client, buf, 5);
}

static int32_t nvt_read_fw_open(int32_t *xdata)
{
	uint32_t raw_pipe_addr = 0;
	uint8_t *rawdata_buf = NULL;
	uint32_t x = 0;
	uint32_t y = 0;
	uint8_t buf[128] = {0};
#if TOUCH_KEY_NUM > 0
	uint32_t raw_btn_pipe_addr = 0;
	int32_t k = 0;
#endif /* #if TOUCH_KEY_NUM > 0 */

	NVT_LOGD("++\n");

	//---Enter Test Mode---
	if (nvt_clear_fw_status()) {
		return -EAGAIN;
	}

	nvt_enable_open_test();

	if (nvt_polling_hand_shake_status()) {
		return -EAGAIN;
	}

#if TOUCH_KEY_NUM > 0
	rawdata_buf = (uint8_t *)kzalloc((IC_X_CFG_SIZE * IC_Y_CFG_SIZE +
					  IC_KEY_CFG_SIZE) * 2, GFP_KERNEL);
#else
	rawdata_buf = (uint8_t *)kzalloc(IC_X_CFG_SIZE * IC_Y_CFG_SIZE * 2,
					 GFP_KERNEL);
#endif /* #if TOUCH_KEY_NUM > 0 */
	if (!rawdata_buf) {
		NVT_ERR("kzalloc for rawdata_buf failed!\n");
		return -ENOMEM;
	}

	if (nvt_get_fw_pipe() == 0)
		raw_pipe_addr = ts->mmap->RAW_PIPE0_ADDR;
	else
		raw_pipe_addr = ts->mmap->RAW_PIPE1_ADDR;

	for (y = 0; y < IC_Y_CFG_SIZE; y++) {
		//---change xdata index---
		nvt_set_page(raw_pipe_addr + y * IC_X_CFG_SIZE * 2);
		buf[0] = (uint8_t)((raw_pipe_addr + y * IC_X_CFG_SIZE * 2) & 0xFF);
		CTP_SPI_READ(ts->client, buf, IC_X_CFG_SIZE * 2 + 1);
		memcpy(rawdata_buf + y * IC_X_CFG_SIZE * 2, buf + 1, IC_X_CFG_SIZE * 2);
	}
#if TOUCH_KEY_NUM > 0
	if (nvt_get_fw_pipe() == 0)
		raw_btn_pipe_addr = ts->mmap->RAW_BTN_PIPE0_ADDR;
	else
		raw_btn_pipe_addr = ts->mmap->RAW_BTN_PIPE1_ADDR;

	//---change xdata index---
	nvt_set_page(raw_btn_pipe_addr);
	buf[0] = (uint8_t)(raw_btn_pipe_addr & 0xFF);
	CTP_SPI_READ(ts->client, buf, IC_KEY_CFG_SIZE * 2 + 1);
	memcpy(rawdata_buf + IC_Y_CFG_SIZE * IC_X_CFG_SIZE * 2, buf + 1,
	       IC_KEY_CFG_SIZE * 2);
#endif /* #if TOUCH_KEY_NUM > 0 */

	for (y = 0; y < IC_Y_CFG_SIZE; y++) {
		for (x = 0; x < IC_X_CFG_SIZE; x++) {
			if ((AIN_Y[y] != 0xFF) && (AIN_X[x] != 0xFF)) {
				xdata[AIN_Y[y] * X_Channel + AIN_X[x]] = (int16_t)
					((rawdata_buf[(y * IC_X_CFG_SIZE + x) * 2] +
					256 * rawdata_buf[(y * IC_X_CFG_SIZE + x) * 2 + 1]));
			}
		}
	}
#if TOUCH_KEY_NUM > 0
	for (k = 0; k < IC_KEY_CFG_SIZE; k++) {
		if (AIN_KEY[k] != 0xFF)
			xdata[Y_Channel * X_Channel + AIN_KEY[k]] = (int16_t)(
				rawdata_buf[(IC_Y_CFG_SIZE * IC_X_CFG_SIZE + k) * 2] +
				256 * rawdata_buf[(IC_Y_CFG_SIZE * IC_X_CFG_SIZE + k) * 2 + 1]);
	}
#endif /* #if TOUCH_KEY_NUM > 0 */

	if (rawdata_buf) {
		kfree(rawdata_buf);
		rawdata_buf = NULL;
	}

	//---Leave Test Mode---
	nvt_change_mode(NORMAL_MODE);


	pr_info("%s:RawData_Open\n", __func__);
	nvt_print_rawdata(xdata, X_Channel, Y_Channel);

	NVT_LOGD("--\n");

	return 0;
}

static int32_t nvt_read_fw_short(int32_t *xdata)
{
	uint32_t raw_pipe_addr = 0;
	uint8_t *rawdata_buf = NULL;
	uint32_t x = 0;
	uint32_t y = 0;
	uint8_t buf[128] = {0};
	int32_t iArrayIndex = 0;
#if TOUCH_KEY_NUM > 0
	uint32_t raw_btn_pipe_addr = 0;
	int32_t k = 0;
#endif /* #if TOUCH_KEY_NUM > 0 */

	NVT_LOGD("++\n");

	//---Enter Test Mode---
	if (nvt_clear_fw_status()) {
		return -EAGAIN;
	}

	nvt_enable_short_test();

	if (nvt_polling_hand_shake_status()) {
		return -EAGAIN;
	}

#if TOUCH_KEY_NUM > 0
	rawdata_buf = (uint8_t *)kzalloc((X_Channel * Y_Channel + Key_Channel) * 2,
					 GFP_KERNEL);
#else
	rawdata_buf = (uint8_t *)kzalloc(X_Channel * Y_Channel * 2, GFP_KERNEL);
#endif /* #if TOUCH_KEY_NUM > 0 */
	if (!rawdata_buf) {
		NVT_ERR("kzalloc for rawdata_buf failed!\n");
		return -ENOMEM;
	}

	if (nvt_get_fw_pipe() == 0)
		raw_pipe_addr = ts->mmap->RAW_PIPE0_ADDR;
	else
		raw_pipe_addr = ts->mmap->RAW_PIPE1_ADDR;

	for (y = 0; y < Y_Channel; y++) {
		//---change xdata index---
		nvt_set_page(raw_pipe_addr + y * X_Channel * 2);
		buf[0] = (uint8_t)((raw_pipe_addr + y * X_Channel * 2) & 0xFF);
		CTP_SPI_READ(ts->client, buf, X_Channel * 2 + 1);
		memcpy(rawdata_buf + y * X_Channel * 2, buf + 1, X_Channel * 2);
	}
#if TOUCH_KEY_NUM > 0
	if (nvt_get_fw_pipe() == 0)
		raw_btn_pipe_addr = ts->mmap->RAW_BTN_PIPE0_ADDR;
	else
		raw_btn_pipe_addr = ts->mmap->RAW_BTN_PIPE1_ADDR;

	//---change xdata index---
	nvt_set_page(raw_btn_pipe_addr);
	buf[0] = (uint8_t)(raw_btn_pipe_addr & 0xFF);
	CTP_SPI_READ(ts->client, buf, Key_Channel * 2 + 1);
	memcpy(rawdata_buf + Y_Channel * X_Channel * 2, buf + 1, Key_Channel * 2);
#endif /* #if TOUCH_KEY_NUM > 0 */

	for (y = 0; y < Y_Channel; y++) {
		for (x = 0; x < X_Channel; x++) {
			iArrayIndex = y * X_Channel + x;
			xdata[iArrayIndex] = (int16_t)(rawdata_buf[iArrayIndex * 2] + 256 *
						       rawdata_buf[iArrayIndex * 2 + 1]);
		}
	}
#if TOUCH_KEY_NUM > 0
	for (k = 0; k < Key_Channel; k++) {
		iArrayIndex = Y_Channel * X_Channel + k;
		xdata[iArrayIndex] = (int16_t)(rawdata_buf[iArrayIndex * 2] + 256 *
					       rawdata_buf[iArrayIndex * 2 + 1]);
	}
#endif /* #if TOUCH_KEY_NUM > 0 */

	if (rawdata_buf) {
		kfree(rawdata_buf);
		rawdata_buf = NULL;
	}

	//---Leave Test Mode---
	nvt_change_mode(NORMAL_MODE);

	pr_info("%s:RawData_Short\n", __func__);
	nvt_print_rawdata(xdata, X_Channel, Y_Channel);

	NVT_LOGD("--\n");

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen raw data test for each single point function.

return:
	Executive outcomes. 0---passed. negative---failed.
*******************************************************/
static int32_t RawDataTest_SinglePoint_Sub(
	int32_t rawdata[], uint8_t RecordResult[], uint8_t x_ch, uint8_t y_ch,
	int32_t Rawdata_Limit_Positive[], int32_t Rawdata_Limit_Negative[])
{
	int32_t i = 0;
	int32_t j = 0;
#if TOUCH_KEY_NUM > 0
	int32_t k = 0;
#endif /* #if TOUCH_KEY_NUM > 0 */
	int32_t iArrayIndex = 0;
	bool isPass = true;

	for (j = 0; j < y_ch; j++) {
		for (i = 0; i < x_ch; i++) {
			iArrayIndex = j * x_ch + i;

			RecordResult[iArrayIndex] = 0x00; // default value for PASS

			if (rawdata[iArrayIndex] > Rawdata_Limit_Positive[iArrayIndex])
				RecordResult[iArrayIndex] |= 0x01;

			if (rawdata[iArrayIndex] < Rawdata_Limit_Negative[iArrayIndex])
				RecordResult[iArrayIndex] |= 0x02;
		}
	}
#if TOUCH_KEY_NUM > 0
	for (k = 0; k < Key_Channel; k++) {
		iArrayIndex = y_ch * x_ch + k;

		RecordResult[iArrayIndex] = 0x00; // default value for PASS

		if (rawdata[iArrayIndex] > Rawdata_Limit_Positive[iArrayIndex])
			RecordResult[iArrayIndex] |= 0x01;

		if (rawdata[iArrayIndex] < Rawdata_Limit_Negative[iArrayIndex])
			RecordResult[iArrayIndex] |= 0x02;
	}
#endif /* #if TOUCH_KEY_NUM > 0 */

	//---Check RecordResult---
	for (j = 0; j < y_ch; j++) {
		for (i = 0; i < x_ch; i++) {
			if (RecordResult[j * x_ch + i] != 0) {
				isPass = false;
				break;
			}
		}
	}
#if TOUCH_KEY_NUM > 0
	for (k = 0; k < Key_Channel; k++) {
		iArrayIndex = y_ch * x_ch + k;
		if (RecordResult[iArrayIndex] != 0) {
			isPass = false;
			break;
		}
	}
#endif /* #if TOUCH_KEY_NUM > 0 */

	if (isPass == false) {
		return -EPERM; // FAIL
	} else {
		return 0; // PASS
	}
}

/*******************************************************
Description:
	Novatek touchscreen print self-test data function.

return:
	n.a.
*******************************************************/
void print_selftest_data(uint8_t ker_pf, struct seq_file *m, int32_t rawdata[],
	uint8_t x_len, uint8_t y_len)
{
	int32_t i, j, iArrayIndex;
	char buffer[512];
	uint16_t cnt = 0;

	for (i = 0; i < y_len; i++) {
		for (j = 0; j < x_len; j++) {
			iArrayIndex = i * x_len + j;
			if (m)
				seq_printf(m, "%5d", rawdata[iArrayIndex]);
			cnt += scnprintf(buffer + cnt, sizeof(buffer) - cnt, "%5d",
				rawdata[iArrayIndex]);
			if (j != x_len - 1) {
				if (m)
					seq_puts(m, " ");
				cnt += scnprintf(buffer + cnt, sizeof(buffer) - cnt, " ");
			} else {
				if (m)
					seq_puts(m, "\n");
				cnt += scnprintf(buffer + cnt, sizeof(buffer) - cnt, "\n");
			}
		}
		if (ker_pf && !nvt_mp_test_result_printed)
			pr_info("%s\n", buffer);
		memset(buffer, 0, sizeof(buffer));
		cnt = 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen print self-test result function.

return:
	n.a.
*******************************************************/
void print_selftest_result(uint8_t ker_pf, struct seq_file *m, int32_t TestResult,
			   uint8_t RecordResult[], int32_t rawdata[], uint8_t x_len, uint8_t y_len)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t iArrayIndex = 0;
#if TOUCH_KEY_NUM > 0
	int32_t k = 0;
#endif /* #if TOUCH_KEY_NUM > 0 */

	switch (TestResult) {
	case 0:
		show_print(ker_pf, m, " PASS!\n");
		break;

	case 1:
		show_print(ker_pf, m, " ERROR! Read Data FAIL!\n");
		break;

	case -1:
		show_print(ker_pf, m, " FAIL!\n");
		show_print(ker_pf, m, "RecordResult:\n");
		for (i = 0; i < y_len; i++) {
			for (j = 0; j < x_len; j++) {
				iArrayIndex = i * x_len + j;
				show_print(false, m, "0x%02X, ", RecordResult[iArrayIndex]);
			}
			nvt_print_result_log_in_one_line(RecordResult + i * x_len, x_len);
			show_print(ker_pf, m, "\n");
		}
#if TOUCH_KEY_NUM > 0
		for (k = 0; k < Key_Channel; k++) {
			iArrayIndex = y_len * x_len + k;
			show_print(false, m, "0x%02X, ", RecordResult[iArrayIndex]);
		}
		nvt_print_result_log_in_one_line(RecordResult + y_len * x_len, Key_Channel);
		show_print(ker_pf, m, "\n");
#endif /* #if TOUCH_KEY_NUM > 0 */
		show_print(ker_pf, m, "ReadData:\n");
		for (i = 0; i < y_len; i++) {
			for (j = 0; j < x_len; j++) {
				iArrayIndex = i * x_len + j;
				show_print(false, m, "%5d, ", rawdata[iArrayIndex]);
			}
			nvt_print_data_log_in_one_line(rawdata + i * x_len, x_len);
			show_print(ker_pf, m, "\n");
		}
#if TOUCH_KEY_NUM > 0
		for (k = 0; k < Key_Channel; k++) {
			iArrayIndex = y_len * x_len + k;
			show_print(false, m, "%5d, ", rawdata[iArrayIndex]);
		}
		nvt_print_data_log_in_one_line(rawdata + y_len * x_len, Key_Channel);
		show_print(ker_pf, m, "\n");
#endif /* #if TOUCH_KEY_NUM > 0 */
		break;
	}
	show_print(ker_pf, m, "\n");
}

static void nvt_check_defect_line(uint8_t result[], uint8_t x_ch, uint8_t y_ch)
{
	int32_t i, j;
	int32_t iArrayIndex = 0;

	for (j = 0; j < y_ch; j++) {
		for (i = 0; i < x_ch; i++) {
			iArrayIndex = j * x_ch + i;
			if (result[iArrayIndex]) {
				set_bit(i, &ts->selftest_defect_x_line);
				set_bit(j, &ts->selftest_defect_y_line);
				NVT_LOGI("(%d,%d) defect!\n", i, j);
			}
		}
	}
}

void show_selftest(uint8_t ker_pf_data, uint8_t ker_pf_result, struct seq_file *m)
{
	show_print(ker_pf_data, m, "\n***** Selftest Data *****\n");

	show_print(ker_pf_data, m, "\n[Short]\n\n");
	print_selftest_data(ker_pf_data, m, RawData_Short, X_Channel, Y_Channel);
	show_print(ker_pf_data, m, "\n[Open]\n\n");
	print_selftest_data(ker_pf_data, m, RawData_Open, X_Channel, Y_Channel);
	show_print(ker_pf_data, m, "\n[Rawdata]\n\n");
	print_selftest_data(ker_pf_data, m, RawData_FW_Rawdata, X_Channel, Y_Channel);
	show_print(ker_pf_data, m, "\n[CC]\n\n");
	print_selftest_data(ker_pf_data, m, RawData_FW_CC, X_Channel, Y_Channel);
	show_print(ker_pf_data, m, "\n[Noise]\n\n");
	print_selftest_data(ker_pf_data, m, RawData_Diff_Max, X_Channel, Y_Channel);
	print_selftest_data(ker_pf_data, m, RawData_Diff_Min, X_Channel, Y_Channel);
	show_print(ker_pf_data, m, "\n[Pen_Rawdata]\n\n");
	print_selftest_data(ker_pf_data, m, RawData_PenTipX_Raw, ts->x_num, ts->y_gang_num);
	print_selftest_data(ker_pf_data, m, RawData_PenTipY_Raw, ts->x_gang_num, ts->y_num);
	print_selftest_data(ker_pf_data, m, RawData_PenRingX_Raw, ts->x_num, ts->y_gang_num);
	print_selftest_data(ker_pf_data, m, RawData_PenRingY_Raw, ts->x_gang_num, ts->y_num);
	show_print(ker_pf_data, m, "\n[Pen_Noise]\n\n");
	print_selftest_data(ker_pf_data, m, RawData_PenTipX_DiffMax, ts->x_num, ts->y_gang_num);
	print_selftest_data(ker_pf_data, m, RawData_PenTipX_DiffMin, ts->x_num, ts->y_gang_num);
	print_selftest_data(ker_pf_data, m, RawData_PenTipY_DiffMax, ts->x_gang_num, ts->y_num);
	print_selftest_data(ker_pf_data, m, RawData_PenTipY_DiffMin, ts->x_gang_num, ts->y_num);
	print_selftest_data(ker_pf_data, m, RawData_PenRingX_DiffMax, ts->x_num, ts->y_gang_num);
	print_selftest_data(ker_pf_data, m, RawData_PenRingX_DiffMin, ts->x_num, ts->y_gang_num);
	print_selftest_data(ker_pf_data, m, RawData_PenRingY_DiffMax, ts->x_gang_num, ts->y_num);
	print_selftest_data(ker_pf_data, m, RawData_PenRingY_DiffMin, ts->x_gang_num, ts->y_num);
	show_print(ker_pf_data, m, "\n[Pen_Rx_Max]\n\n");
	print_selftest_data(ker_pf_data, m, RawData_Pen_Rx_Max, PEN_RX_MAX_X_LEN, PEN_RX_MAX_Y_LEN);

	show_print(ker_pf_result, m, "\n\n===== Test Result =====\n\n");

	show_print(ker_pf_result, m, "FW Version: 0x%02X\n\n", fw_ver);

	show_print(ker_pf_result, m, "Short Test");
	print_selftest_result(ker_pf_result, m, TestResult_Short, RecordResult_Short,
			      RawData_Short, X_Channel, Y_Channel);

	show_print(ker_pf_result, m, "Open Test");
	print_selftest_result(ker_pf_result, m, TestResult_Open, RecordResult_Open, RawData_Open,
			      X_Channel, Y_Channel);

	show_print(ker_pf_result, m, "FW Rawdata Test");
	print_selftest_result(ker_pf_result, m, TestResult_FW_Rawdata, RecordResult_FW_Rawdata,
			RawData_FW_Rawdata, X_Channel, Y_Channel);

	show_print(ker_pf_result, m, "FW CC Test");
	print_selftest_result(ker_pf_result, m, TestResult_FW_CC,
			RecordResult_FW_CC, RawData_FW_CC, X_Channel, Y_Channel);

	show_print(ker_pf_result, m, "Noise Test");
	if ((TestResult_Noise == 0) || (TestResult_Noise == 1)) {
		print_selftest_result(ker_pf_result, m, TestResult_FW_DiffMax,
			RecordResult_FW_DiffMax, RawData_Diff_Max, X_Channel, Y_Channel);
	} else { // TestResult_Noise is -1
		show_print(ker_pf_result, m, " FAIL!\n");
		if (TestResult_FW_DiffMax == -1) {
			show_print(ker_pf_result, m, "FW Diff Max");
			print_selftest_result(ker_pf_result, m, TestResult_FW_DiffMax,
				RecordResult_FW_DiffMax, RawData_Diff_Max, X_Channel, Y_Channel);
		}
		if (TestResult_FW_DiffMin == -1) {
			show_print(ker_pf_result, m, "FW Diff Min");
			print_selftest_result(ker_pf_result, m, TestResult_FW_DiffMin,
				RecordResult_FW_DiffMin, RawData_Diff_Min, X_Channel, Y_Channel);
		}
	}

	if (ts->pen_support) {
		show_print(ker_pf_result, m, "Pen FW Rawdata Test");
		if ((TestResult_Pen_FW_Raw == 0) || (TestResult_Pen_FW_Raw == 1)) {
			print_selftest_result(ker_pf_result, m, TestResult_Pen_FW_Raw,
				RecordResult_PenTipX_Raw,
				RawData_PenTipX_Raw, ts->x_num, ts->y_gang_num);
		} else { // TestResult_Pen_FW_Raw is -1
			show_print(ker_pf_result, m, " FAIL!\n");
			if (TestResult_PenTipX_Raw == -1) {
				show_print(ker_pf_result, m, "Pen Tip X Raw");
				print_selftest_result(ker_pf_result, m, TestResult_PenTipX_Raw,
					RecordResult_PenTipX_Raw,
					RawData_PenTipX_Raw, ts->x_num, ts->y_gang_num);
			}
			if (TestResult_PenTipY_Raw == -1) {
				show_print(ker_pf_result, m, "Pen Tip Y Raw");
				print_selftest_result(ker_pf_result, m, TestResult_PenTipY_Raw,
					RecordResult_PenTipY_Raw,
					RawData_PenTipY_Raw, ts->x_gang_num, ts->y_num);
			}
			if (TestResult_PenRingX_Raw == -1) {
				show_print(ker_pf_result, m, "Pen Ring X Raw");
				print_selftest_result(ker_pf_result, m, TestResult_PenRingX_Raw,
						      RecordResult_PenRingX_Raw,
						      RawData_PenRingX_Raw, ts->x_num,
						      ts->y_gang_num);
			}
			if (TestResult_PenRingY_Raw == -1) {
				show_print(ker_pf_result, m, "Pen Ring Y Raw");
				print_selftest_result(ker_pf_result, m, TestResult_PenRingY_Raw,
						      RecordResult_PenRingY_Raw,
						      RawData_PenRingY_Raw, ts->x_gang_num,
						      ts->y_num);
			}
		}

		show_print(ker_pf_result, m, "Pen Noise Test");
		if ((TestResult_Pen_Noise == 0) || (TestResult_Pen_Noise == 1)) {
			print_selftest_result(ker_pf_result, m, TestResult_Pen_Noise,
					      RecordResult_PenTipX_DiffMax,
					      RawData_PenTipX_DiffMax, ts->x_num,
					      ts->y_gang_num);
		} else { // TestResult_Pen_Noise is -1
			show_print(ker_pf_result, m, " FAIL!\n");
			if (TestResult_PenTipX_DiffMax == -1) {
				show_print(ker_pf_result, m, "Pen Tip X Diff Max");
				print_selftest_result(ker_pf_result, m, TestResult_PenTipX_DiffMax,
						      RecordResult_PenTipX_DiffMax,
						      RawData_PenTipX_DiffMax, ts->x_num,
						      ts->y_gang_num);
			}
			if (TestResult_PenTipX_DiffMin == -1) {
				show_print(ker_pf_result, m, "Pen Tip X Diff Min");
				print_selftest_result(ker_pf_result, m, TestResult_PenTipX_DiffMin,
						      RecordResult_PenTipX_DiffMin,
						      RawData_PenTipX_DiffMin, ts->x_num,
						      ts->y_gang_num);
			}
			if (TestResult_PenTipY_DiffMax == -1) {
				show_print(ker_pf_result, m, "Pen Tip Y Diff Max");
				print_selftest_result(ker_pf_result, m, TestResult_PenTipY_DiffMax,
						      RecordResult_PenTipY_DiffMax,
						      RawData_PenTipY_DiffMax, ts->x_gang_num,
						      ts->y_num);
			}
			if (TestResult_PenTipY_DiffMin == -1) {
				show_print(ker_pf_result, m, "Pen Tip Y Diff Min");
				print_selftest_result(ker_pf_result, m, TestResult_PenTipY_DiffMin,
						      RecordResult_PenTipY_DiffMin,
						      RawData_PenTipY_DiffMin, ts->x_gang_num,
						      ts->y_num);
			}
			if (TestResult_PenRingX_DiffMax == -1) {
				show_print(ker_pf_result, m, "Pen Ring X Diff Max");
				print_selftest_result(ker_pf_result, m, TestResult_PenRingX_DiffMax,
						      RecordResult_PenRingX_DiffMax,
						      RawData_PenRingX_DiffMax, ts->x_num,
						      ts->y_gang_num);
			}
			if (TestResult_PenRingX_DiffMin == -1) {
				show_print(ker_pf_result, m, "Pen Ring X Diff Min");
				print_selftest_result(ker_pf_result, m, TestResult_PenRingX_DiffMin,
						      RecordResult_PenRingX_DiffMin,
						      RawData_PenRingX_DiffMin, ts->x_num,
						      ts->y_gang_num);
			}
			if (TestResult_PenRingY_DiffMax == -1) {
				show_print(ker_pf_result, m, "Pen Ring Y Diff Max");
				print_selftest_result(ker_pf_result, m, TestResult_PenRingY_DiffMax,
						      RecordResult_PenRingY_DiffMax,
						      RawData_PenRingY_DiffMax, ts->x_gang_num,
						      ts->y_num);
			}
			if (TestResult_PenRingY_DiffMin == -1) {
				show_print(ker_pf_result, m, "Pen Ring Y Diff Min");
				print_selftest_result(ker_pf_result, m, TestResult_PenRingY_DiffMin,
						      RecordResult_PenRingY_DiffMin,
						      RawData_PenRingY_DiffMin, ts->x_gang_num,
						      ts->y_num);
			}
		}

		show_print(ker_pf_result, m, "Pen Detect Test");
		if (TestResult_Pen_Rx_Max == 0 || TestResult_Pen_Rx_Max == 1) {
			print_selftest_result(ker_pf_result, m, TestResult_Pen_Rx_Max,
				RecordResult_Pen_Rx_Max, RawData_Pen_Rx_Max,
				PEN_RX_MAX_X_LEN, PEN_RX_MAX_Y_LEN);
		} else {
			show_print(ker_pf_result, m, " FAIL!\n");
			show_print(ker_pf_result, m, "Pen Detect Rx Max");
			print_selftest_result(ker_pf_result, m, TestResult_Pen_Rx_Max,
				RecordResult_Pen_Rx_Max, RawData_Pen_Rx_Max,
				PEN_RX_MAX_X_LEN, PEN_RX_MAX_Y_LEN);
		}
	} /* if (ts->pen_support) */

	show_print(ker_pf_result, m, "Self-test TVCL/IBIAS: %u %u\n",
		ts->mp_tvcl_mode, ts->mp_ibias_mode);

	// to prevent c_show_test entering multiple times
	nvt_mp_test_result_printed = 1;

	/* Google Defect X/Y line result */
	if (ts->selftest_failed_result) {
		if (ts->selftest_failed_short)
			nvt_check_defect_line(RecordResult_Short, X_Channel, Y_Channel);
		if (ts->selftest_failed_open)
			nvt_check_defect_line(RecordResult_Open, X_Channel, Y_Channel);
		if (ts->selftest_failed_fw_raw)
			nvt_check_defect_line(RecordResult_FW_Rawdata, X_Channel, Y_Channel);
		if (ts->selftest_failed_fw_cc)
			nvt_check_defect_line(RecordResult_FW_CC, X_Channel, Y_Channel);
		if (ts->selftest_failed_noise) {
			if (TestResult_FW_DiffMax < 0) {
				nvt_check_defect_line(RecordResult_FW_DiffMax,
					X_Channel, Y_Channel);
			}
			if (TestResult_FW_DiffMin < 0) {
				nvt_check_defect_line(RecordResult_FW_DiffMin,
					X_Channel, Y_Channel);
			}
			if (TestResult_FW_DiffP2P < 0) {
				nvt_check_defect_line(RecordResult_FW_DiffP2P,
					X_Channel, Y_Channel);
			}
		}
	}
}

int32_t nvt_print_selftest_result(char *buf, int size)
{
	uint16_t ret = 0;

	NVT_LOGD("++\n");

	// kernel print data and result
	show_selftest(false, true, NULL);
	// print the result into buf
	ret += scnprintf(buf + ret, size - ret, "\n===== Test Result =====\n");
	ret += scnprintf(buf + ret, size - ret, "FW Version: 0x%02X\n", fw_ver);
	ret += scnprintf(buf + ret, size - ret, "\n");
	ret += scnprintf(buf + ret, size - ret, "Short Test %s\n",
		TestResult_Short ? "FAIL!" : "PASS!");
	ret += scnprintf(buf + ret, size - ret, "Open Test %s\n",
		TestResult_Open ? "FAIL!" : "PASS!");
	ret += scnprintf(buf + ret, size - ret, "FW Rawdata Test %s\n",
		TestResult_FW_Rawdata ? "FAIL!" : "PASS!");
	ret += scnprintf(buf + ret, size - ret, "FW CC Test %s\n",
		TestResult_FW_CC ? "FAIL!" : "PASS!");
	ret += scnprintf(buf + ret, size - ret, "Noise Test %s\n",
		TestResult_Noise ? "FAIL!" : "PASS!");
	ret += scnprintf(buf + ret, size - ret, "Pen FW Rawdata Test %s\n",
		TestResult_Pen_FW_Raw ? "FAIL!" : "PASS!");
	ret += scnprintf(buf + ret, size - ret, "Pen Noise Test %s\n",
		TestResult_Pen_Noise ? "FAIL!" : "PASS!");
	ret += scnprintf(buf + ret, size - ret, "Pen Detect Test %s\n",
		TestResult_Pen_Rx_Max ? "FAIL!" : "PASS!");
	ret += scnprintf(buf + ret, size - ret, "Self-test TVCL/IBIAS: %u %u\n",
		ts->mp_tvcl_mode, ts->mp_ibias_mode);
	ret += scnprintf(buf + ret, size - ret, "\n");

	NVT_LOGD("--\n");
	return ret;
}

/*******************************************************
 * Description:
 *  Novatek touchscreen self-test sequence print show
 *  function.
 *
 * return:
 *  Executive outcomes. 0---succeed.
 ******************************************************/
static int32_t c_show_selftest(struct seq_file *m, void *v)
{
	NVT_LOGD("++\n");
	// kernel print result + seq print
	show_selftest(false, true, m);
	NVT_LOGD("--\n");
	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen self-test sequence print start
	function.

return:
	Executive outcomes. 1---call next function.
	NULL---not call next function and sequence loop
	stop.
*******************************************************/
static void *c_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

/*******************************************************
Description:
	Novatek touchscreen self-test sequence print next
	function.

return:
	Executive outcomes. NULL---no next and call sequence
	stop function.
*******************************************************/
static void *c_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

/*******************************************************
Description:
	Novatek touchscreen self-test sequence print stop
	function.

return:
	n.a.
*******************************************************/
static void c_stop(struct seq_file *m, void *v)
{
	return;
}

const struct seq_operations nvt_selftest_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_show_selftest
};

int32_t nvt_selftest(void)
{
	int32_t ret = 0;
	bool show_fw_history = true;
	struct device_node *np = ts->client->dev.of_node;
	unsigned char mpcriteria[32] = {0};	//novatek-mp-criteria-default

	ts->selftest_failed_result = 0;
	ts->selftest_defect_x_line = 0;
	ts->selftest_defect_y_line = 0;
	TestResult_Short = 0;
	TestResult_Open = 0;
	TestResult_FW_Rawdata = 0;
	TestResult_FW_CC = 0;
	TestResult_Noise = 0;
	TestResult_FW_DiffMax = 0;
	TestResult_FW_DiffMin = 0;
	TestResult_FW_DiffP2P = 0;
	if (ts->pen_support) {
		TestResult_Pen_FW_Raw = 0;
		TestResult_PenTipX_Raw = 0;
		TestResult_PenTipY_Raw = 0;
		TestResult_PenRingX_Raw = 0;
		TestResult_PenRingY_Raw = 0;
		TestResult_Pen_Noise = 0;
		TestResult_PenTipX_DiffMax = 0;
		TestResult_PenTipX_DiffMin = 0;
		TestResult_PenTipX_DiffP2P = 0;
		TestResult_PenTipY_DiffMax = 0;
		TestResult_PenTipY_DiffMin = 0;
		TestResult_PenTipY_DiffP2P = 0;
		TestResult_PenRingX_DiffMax = 0;
		TestResult_PenRingX_DiffMin = 0;
		TestResult_PenRingX_DiffP2P = 0;
		TestResult_PenRingY_DiffMax = 0;
		TestResult_PenRingY_DiffMin = 0;
		TestResult_PenRingY_DiffP2P = 0;
	} /* if (ts->pen_support) */

	NVT_LOGD("++\n");

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	ts->selftest_in_process = true;
	nvt_mp_test_result_printed = 0;

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */


#if SPI_FLASH
	nvt_clear_fw_reset_state();
	nvt_bootloader_reset();
	if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN))
		NVT_LOGE("FW is not ready for self-test!\n");
#else
	//---Download MP FW---
	nvt_update_firmware(get_mp_fw_name(), 1);
#endif // !SPI_FLASH

	if (nvt_get_fw_info()) {
		NVT_ERR("get fw info failed!\n");
		goto failed_out;
	}

	fw_ver = ts->fw_ver;

	/* Parsing criteria from dts */
	if (of_property_read_bool(np, "novatek,mp-support-dt")) {
		/*
		 * Parsing Criteria by Novatek PID
		 * The string rule is "novatek-mp-criteria-<nvt_pid>"
		 * nvt_pid is 2 bytes (show hex).
		 *
		 * Ex. nvt_pid = 500A
		 *     mpcriteria = "novatek-mp-criteria-500A"
		 */
		scnprintf(mpcriteria, sizeof(mpcriteria),
			  "novatek-mp-criteria-%04X", ts->nvt_pid);

		if (nvt_mp_parse_dt(np, mpcriteria)) {
#if !SPI_FLASH
			//---Download Normal FW---
			nvt_update_firmware(get_fw_name(), 1);
#endif // !SPI_FLASH
			mutex_unlock(&ts->lock);
			NVT_ERR("mp parse device tree failed!\n");
			ts->selftest_in_process = false;
			return -EINVAL;
		}
	} else {
		NVT_LOG("Not found novatek,mp-support-dt, use default setting\n");
		//---Print Test Criteria---
		nvt_print_criteria();
	}

#if SPI_FLASH
	if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN)) {
#else
	if (nvt_check_fw_reset_state(RESET_STATE_REK)) {
#endif // SPI_FLASH
		NVT_ERR("check fw reset state failed!\n");
		goto failed_out;
	}

#if SPI_FLASH
	if (nvt_mp_settings(ts->mp_tvcl_mode, ts->mp_ibias_mode))
		goto failed_out;
#endif // SPI_FLASH
	if (nvt_switch_FreqHopEnDis(FREQ_HOP_DISABLE)) {
		NVT_ERR("switch frequency hopping disable failed!\n");
		goto failed_out;
	}

	if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN)) {
		NVT_ERR("check fw reset state failed!\n");
		goto failed_out;
	}

	msleep(100);

	//---Enter Test Mode---
	if (nvt_clear_fw_status()) {
		NVT_ERR("clear fw status failed!\n");
		goto failed_out;
	}

	nvt_change_mode(MP_MODE_CC);

	if (nvt_check_fw_status()) {
		NVT_ERR("check fw status failed!\n");
		goto failed_out;
	}

	//---FW Rawdata Test---
	if (nvt_read_baseline(RawData_FW_Rawdata) != 0) {
		TestResult_FW_Rawdata = 1;
	} else {
		TestResult_FW_Rawdata = RawDataTest_SinglePoint_Sub(
						RawData_FW_Rawdata,
						RecordResult_FW_Rawdata,
						X_Channel, Y_Channel,
						PS_Config_Lmt_FW_Rawdata_P,
						PS_Config_Lmt_FW_Rawdata_N);
		if (TestResult_FW_Rawdata < 0)
			ts->selftest_failed_fw_raw = true;
	}

	if (nvt_read_CC(RawData_FW_CC) != 0) {
		TestResult_FW_CC = 1;
	} else {
		TestResult_FW_CC = RawDataTest_SinglePoint_Sub(RawData_FW_CC,
				   RecordResult_FW_CC, X_Channel, Y_Channel,
				   PS_Config_Lmt_FW_CC_P, PS_Config_Lmt_FW_CC_N);
		if (TestResult_FW_CC < 0)
			ts->selftest_failed_fw_cc = true;
	}

	if (ts->pen_support) {
		//---Pen FW Rawdata Test---
		if (nvt_read_pen_baseline() != 0) {
			TestResult_Pen_FW_Raw = 1;
		} else {
			TestResult_PenTipX_Raw =
				RawDataTest_SinglePoint_Sub(RawData_PenTipX_Raw,
				RecordResult_PenTipX_Raw, ts->x_num, ts->y_gang_num,
				PS_Config_Lmt_PenTipX_FW_Raw_P, PS_Config_Lmt_PenTipX_FW_Raw_N);
			TestResult_PenTipY_Raw =
				RawDataTest_SinglePoint_Sub(RawData_PenTipY_Raw,
				RecordResult_PenTipY_Raw, ts->x_gang_num, ts->y_num,
				PS_Config_Lmt_PenTipY_FW_Raw_P, PS_Config_Lmt_PenTipY_FW_Raw_N);
			TestResult_PenRingX_Raw =
				RawDataTest_SinglePoint_Sub(RawData_PenRingX_Raw,
				RecordResult_PenRingX_Raw, ts->x_num, ts->y_gang_num,
				PS_Config_Lmt_PenRingX_FW_Raw_P, PS_Config_Lmt_PenRingX_FW_Raw_N);
			TestResult_PenRingY_Raw =
				RawDataTest_SinglePoint_Sub(RawData_PenRingY_Raw,
				RecordResult_PenRingY_Raw, ts->x_gang_num, ts->y_num,
				PS_Config_Lmt_PenRingY_FW_Raw_P, PS_Config_Lmt_PenRingY_FW_Raw_N);

			if ((TestResult_PenTipX_Raw == -1) || (TestResult_PenTipY_Raw == -1) ||
			    (TestResult_PenRingX_Raw == -1) || (TestResult_PenRingY_Raw == -1))
				TestResult_Pen_FW_Raw = -1;
			else
				TestResult_Pen_FW_Raw = 0;

			if (TestResult_Pen_FW_Raw < 0)
				ts->selftest_failed_pen_raw = true;
		}
	} /* if (ts->pen_support) */

	//---Leave Test Mode---
	nvt_change_mode(NORMAL_MODE);

	//---Noise Test---
	if (nvt_read_fw_noise(RawData_Diff) != 0) {
		TestResult_Noise = 1;	// 1: ERROR
		TestResult_FW_DiffMax = 1;
		TestResult_FW_DiffMin = 1;
		TestResult_FW_DiffP2P = 1;
		if (ts->pen_support) {
			TestResult_Pen_Noise = 1;
			TestResult_PenTipX_DiffMax = 1;
			TestResult_PenTipX_DiffMin = 1;
			TestResult_PenTipX_DiffP2P = 1;
			TestResult_PenTipY_DiffMax = 1;
			TestResult_PenTipY_DiffMin = 1;
			TestResult_PenTipY_DiffP2P = 1;
			TestResult_PenRingX_DiffMax = 1;
			TestResult_PenRingX_DiffMin = 1;
			TestResult_PenRingX_DiffP2P = 1;
			TestResult_PenRingY_DiffMax = 1;
			TestResult_PenRingY_DiffMin = 1;
			TestResult_PenRingY_DiffP2P = 1;
			TestResult_Pen_Rx_Max = 1;
		} /* if (ts->pen_support) */
	} else {
		TestResult_FW_DiffMax = RawDataTest_SinglePoint_Sub(RawData_Diff_Max,
					RecordResult_FW_DiffMax, X_Channel, Y_Channel,
					PS_Config_Lmt_FW_Diff_P, PS_Config_Lmt_FW_Diff_N);

		TestResult_FW_DiffMin = RawDataTest_SinglePoint_Sub(RawData_Diff_Min,
					RecordResult_FW_DiffMin, X_Channel, Y_Channel,
					PS_Config_Lmt_FW_Diff_P, PS_Config_Lmt_FW_Diff_N);

		TestResult_FW_DiffP2P = RawDataTest_SinglePoint_Sub(RawData_Diff_P2P,
					RecordResult_FW_DiffP2P, X_Channel, Y_Channel,
					PS_Config_Lmt_FW_Diff_P2P_P, PS_Config_Lmt_FW_Diff_P2P_N);

		if (TestResult_FW_DiffP2P == -1)
				NVT_ERR("[Touch P2P] Test Fail\n");

		if ((TestResult_FW_DiffMax == -1) || (TestResult_FW_DiffMin == -1)
				|| (TestResult_FW_DiffP2P == -1))
			TestResult_Noise = -1;
		else
			TestResult_Noise = 0;

		if (TestResult_Noise < 0)
			ts->selftest_failed_noise = true;

		if (ts->pen_support) {
			TestResult_PenTipX_DiffMax = RawDataTest_SinglePoint_Sub(
							     RawData_PenTipX_DiffMax,
							     RecordResult_PenTipX_DiffMax,
							     ts->x_num, ts->y_gang_num,
							     PS_Config_Lmt_PenTipX_FW_Diff_P,
							     PS_Config_Lmt_PenTipX_FW_Diff_N);

			TestResult_PenTipX_DiffMin = RawDataTest_SinglePoint_Sub(
							     RawData_PenTipX_DiffMin,
							     RecordResult_PenTipX_DiffMin,
							     ts->x_num, ts->y_gang_num,
							     PS_Config_Lmt_PenTipX_FW_Diff_P,
							     PS_Config_Lmt_PenTipX_FW_Diff_N);

			TestResult_PenTipX_DiffP2P = RawDataTest_SinglePoint_Sub(
							     RawData_PenTipX_DiffP2P,
							     RecordResult_PenTipX_DiffP2P,
							     ts->x_num, ts->y_gang_num,
							     PS_Config_Lmt_PenTipX_FW_Diff_P2P_P,
							     PS_Config_Lmt_PenTipX_FW_Diff_P2P_N);

			TestResult_PenTipY_DiffMax = RawDataTest_SinglePoint_Sub(
							     RawData_PenTipY_DiffMax,
							     RecordResult_PenTipY_DiffMax,
							     ts->x_gang_num, ts->y_num,
							     PS_Config_Lmt_PenTipY_FW_Diff_P,
							     PS_Config_Lmt_PenTipY_FW_Diff_N);

			TestResult_PenTipY_DiffMin = RawDataTest_SinglePoint_Sub(
							     RawData_PenTipY_DiffMin,
							     RecordResult_PenTipY_DiffMin,
							     ts->x_gang_num, ts->y_num,
							     PS_Config_Lmt_PenTipY_FW_Diff_P,
							     PS_Config_Lmt_PenTipY_FW_Diff_N);

			TestResult_PenTipY_DiffP2P = RawDataTest_SinglePoint_Sub(
							     RawData_PenTipY_DiffP2P,
							     RecordResult_PenTipY_DiffP2P,
							     ts->x_gang_num, ts->y_num,
							     PS_Config_Lmt_PenTipY_FW_Diff_P2P_P,
							     PS_Config_Lmt_PenTipY_FW_Diff_P2P_N);

			TestResult_PenRingX_DiffMax = RawDataTest_SinglePoint_Sub(
							      RawData_PenRingX_DiffMax,
							      RecordResult_PenRingX_DiffMax,
							      ts->x_num, ts->y_gang_num,
							      PS_Config_Lmt_PenRingX_FW_Diff_P,
							      PS_Config_Lmt_PenRingX_FW_Diff_N);

			TestResult_PenRingX_DiffMin = RawDataTest_SinglePoint_Sub(
							      RawData_PenRingX_DiffMin,
							      RecordResult_PenRingX_DiffMin,
							      ts->x_num, ts->y_gang_num,
							      PS_Config_Lmt_PenRingX_FW_Diff_P,
							      PS_Config_Lmt_PenRingX_FW_Diff_N);

			TestResult_PenRingX_DiffP2P = RawDataTest_SinglePoint_Sub(
							     RawData_PenRingX_DiffP2P,
							     RecordResult_PenRingX_DiffP2P,
							     ts->x_num, ts->y_gang_num,
							     PS_Config_Lmt_PenRingX_FW_Diff_P2P_P,
							     PS_Config_Lmt_PenRingX_FW_Diff_P2P_N);

			TestResult_PenRingY_DiffMax = RawDataTest_SinglePoint_Sub(
							      RawData_PenRingY_DiffMax,
							      RecordResult_PenRingY_DiffMax,
							      ts->x_gang_num, ts->y_num,
							      PS_Config_Lmt_PenRingY_FW_Diff_P,
							      PS_Config_Lmt_PenRingY_FW_Diff_N);

			TestResult_PenRingY_DiffMin = RawDataTest_SinglePoint_Sub(
							      RawData_PenRingY_DiffMin,
							      RecordResult_PenRingY_DiffMin,
							      ts->x_gang_num, ts->y_num,
							      PS_Config_Lmt_PenRingY_FW_Diff_P,
							      PS_Config_Lmt_PenRingY_FW_Diff_N);

			TestResult_PenRingY_DiffP2P = RawDataTest_SinglePoint_Sub(
							     RawData_PenRingY_DiffP2P,
							     RecordResult_PenRingY_DiffP2P,
							     ts->x_gang_num, ts->y_num,
							     PS_Config_Lmt_PenRingY_FW_Diff_P2P_P,
							     PS_Config_Lmt_PenRingY_FW_Diff_P2P_N);

			TestResult_Pen_Rx_Max = RawDataTest_SinglePoint_Sub(
					RawData_Pen_Rx_Max, RecordResult_Pen_Rx_Max,
					PEN_RX_MAX_X_LEN, PEN_RX_MAX_Y_LEN,
					PS_Config_Lmt_Pen_Rx_Max_P, PS_Config_Lmt_Pen_Rx_Max_N);

			if (TestResult_Pen_Rx_Max < 0)
				ts->selftest_failed_pen_detect = true;

			if (TestResult_PenTipX_DiffP2P == -1)
				NVT_ERR("[Pen P2P] TipX Test Fail\n");
			if (TestResult_PenTipY_DiffP2P == -1)
				NVT_ERR("[Pen P2P] TipY Test Fail\n");
			if (TestResult_PenRingX_DiffP2P == -1)
				NVT_ERR("[Pen P2P] RingX Test Fail\n");
			if (TestResult_PenRingY_DiffP2P == -1)
				NVT_ERR("[Pen P2P] RingY Test Fail\n");

			if ((TestResult_PenTipX_DiffMax == -1) ||
			    (TestResult_PenTipX_DiffMin == -1) ||
			    (TestResult_PenTipX_DiffP2P == -1) ||
			    (TestResult_PenTipY_DiffMax == -1) ||
			    (TestResult_PenTipY_DiffMin == -1) ||
			    (TestResult_PenTipY_DiffP2P == -1) ||
			    (TestResult_PenRingX_DiffMax == -1) ||
			    (TestResult_PenRingX_DiffMin == -1) ||
			    (TestResult_PenRingX_DiffP2P == -1) ||
			    (TestResult_PenRingY_DiffMax == -1) ||
			    (TestResult_PenRingY_DiffMin == -1) ||
			    (TestResult_PenRingY_DiffP2P == -1))
				TestResult_Pen_Noise = -1;
			else
				TestResult_Pen_Noise = 0;

			if (TestResult_Pen_Noise < 0)
				ts->selftest_failed_pen_noise = true;

		} /* if (ts->pen_support) */
	}

	//--Short Test---
	if (nvt_read_fw_short(RawData_Short) != 0) {
		TestResult_Short = 1; // 1:ERROR
	} else {
		//---Self Test Check --- // 0:PASS, -1:FAIL
		TestResult_Short = RawDataTest_SinglePoint_Sub(RawData_Short,
				   RecordResult_Short, X_Channel, Y_Channel,
				   PS_Config_Lmt_Short_Rawdata_P, PS_Config_Lmt_Short_Rawdata_N);
		if (TestResult_Short < 0)
			ts->selftest_failed_short = true;
	}

	//---Open Test---
	if (nvt_read_fw_open(RawData_Open) != 0) {
		TestResult_Open = 1;    // 1:ERROR
	} else {
		//---Self Test Check --- // 0:PASS, -1:FAIL
		TestResult_Open = RawDataTest_SinglePoint_Sub(RawData_Open,
				  RecordResult_Open, X_Channel, Y_Channel,
				  PS_Config_Lmt_Open_Rawdata_P, PS_Config_Lmt_Open_Rawdata_N);
		if (TestResult_Open < 0)
			ts->selftest_failed_open = true;
	}

	show_fw_history = false;
failed_out:
	if (show_fw_history) {
		ret = -EAGAIN;
		nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT0);
		nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT1);
	}

	ts->selftest_in_process = false;
#if SPI_FLASH
	//---Reset IC---
	nvt_clear_fw_reset_state();
	nvt_bootloader_reset();
	if (nvt_check_fw_reset_state(RESET_STATE_NORMAL_RUN))
		NVT_LOGE("FW is not ready after self-test done!\n");
#else
	//---Download Normal FW---
	nvt_update_firmware(get_fw_name(), 1);
#endif // SPI_FLASH
	mutex_unlock(&ts->lock);

	NVT_LOGD("--\n");

	return ret;
}

/*******************************************************
 * Description:
 *	Novatek touchscreen /proc/nvt_selftest open function.
 *
 * return:
 *	Executive outcomes. 0---succeed. negative---failed.
 ******************************************************/
int32_t nvt_selftest_open(struct inode *inode, struct file *file)
{
	int32_t ret = 0;

	ret = nvt_selftest();

	if (ret)
		return ret;

	return seq_open(file, &nvt_selftest_seq_ops);
}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0))
static const struct proc_ops nvt_selftest_fops = {
	.proc_open = nvt_selftest_open,
	.proc_read = seq_read,
	.proc_lseek = seq_lseek,
	.proc_release = seq_release,
};
#else
static const struct file_operations nvt_selftest_fops = {
	.owner = THIS_MODULE,
	.open = nvt_selftest_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

#ifdef CONFIG_OF
/*******************************************************
Description:
	Novatek touchscreen parse AIN setting for array type.

return:
	n.a.
*******************************************************/
int32_t nvt_mp_parse_ain(struct device_node *np, const char *name,
			 uint8_t *array, int32_t size)
{
	struct property *data;
	int32_t len, ret;
	int32_t tmp[50];
	int32_t i;

	data = of_find_property(np, name, &len);
	len /= sizeof(u32);
	if ((!data) || (!len) || (len != size)) {
		NVT_ERR("error find %s. len=%d\n", name, len);
		return -EPERM;
	} else {
		NVT_LOG("%s. len=%d\n", name, len);
		ret = of_property_read_u32_array(np, name, tmp, len);
		if (ret) {
			NVT_ERR("error reading %s. ret=%d\n", name, ret);
			return -EPERM;
		}

		for (i = 0; i < len; i++)
			array[i] = tmp[i];

#if NVT_MP_DEBUG
		pr_info("[NVT-ts] %s = ", name);
		nvt_print_result_log_in_one_line(array, len);
		pr_info("\n");
#endif
	}

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen parse criterion for u32 type.

return:
	n.a.
*******************************************************/
int32_t nvt_mp_parse_u32(struct device_node *np, const char *name,
			 int32_t *para)
{
	int32_t ret;

	ret = of_property_read_u32(np, name, para);
	if (ret) {
		NVT_ERR("error reading %s. ret=%d\n", name, ret);
		return -EPERM;
	} else {
		NVT_LOG("%s=%d\n", name, *para);
	}

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen parse criterion for array type.

return:
	n.a.
*******************************************************/
int32_t nvt_mp_parse_array(struct device_node *np, const char *name,
			   int32_t *array,
			   int32_t size)
{
	struct property *data;
	int32_t len, ret;
#if NVT_MP_DEBUG
	int32_t j = 0;
#endif

	data = of_find_property(np, name, &len);
	len /= sizeof(u32);
	if ((!data) || (!len) || (len < size)) {
		NVT_ERR("error find %s. len=%d\n", name, len);
		return -EPERM;
	} else {
		NVT_LOG("%s. len=%d\n", name, len);
		ret = of_property_read_u32_array(np, name, array, len);
		if (ret) {
			NVT_ERR("error reading %s. ret=%d\n", name, ret);
			return -EPERM;
		}

#if NVT_MP_DEBUG
		NVT_LOG("%s =\n", name);
		for (j = 0; j < Y_Channel; j++) {
			nvt_print_data_log_in_one_line(array + j * X_Channel, X_Channel);
			pr_info("\n");
		}
#if TOUCH_KEY_NUM > 0
		nvt_print_data_log_in_one_line(array + Y_Channel * X_Channel, Key_Channel);
		pr_info("\n");
#endif
#endif
	}

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen parse criterion for pen array type.

return:
	n.a.
*******************************************************/
int32_t nvt_mp_parse_pen_array(struct device_node *np, const char *name,
			       int32_t *array,
			       uint32_t x_num, uint32_t y_num)
{
	struct property *data;
	int32_t len, ret;
#if NVT_MP_DEBUG
	int32_t j = 0;
#endif
	uint32_t size;

	size = x_num * y_num;
	data = of_find_property(np, name, &len);
	len /= sizeof(u32);
	if ((!data) || (!len) || (len < size)) {
		NVT_ERR("error find %s. len=%d\n", name, len);
		return -EPERM;
	} else {
		NVT_LOG("%s. len=%d\n", name, len);
		ret = of_property_read_u32_array(np, name, array, len);
		if (ret) {
			NVT_ERR("error reading %s. ret=%d\n", name, ret);
			return -EPERM;
		}

#if NVT_MP_DEBUG
		NVT_LOG("%s =\n", name);
		for (j = 0; j < y_num; j++) {
			nvt_print_data_log_in_one_line(array + j * x_num, x_num);
			pr_info("\n");
		}
#endif
	}

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen parse device tree mp function.

return:
	n.a.
*******************************************************/
int32_t nvt_mp_parse_dt(struct device_node *root,
			const char *node_compatible)
{
	struct device_node *np = root;
	struct device_node *child = NULL;
	int32_t i = 0;
	char cc_limit_p[50];
	char cc_limit_n[50];
	char * const tvcl_str[] = {"", "_0p3", "_0p4", "_0p5", "_0p7"};
	char * const ibias_str[] = {"", "_Normal", "_Max"};

	NVT_LOG("Parse mp criteria for node %s\n", node_compatible);

	/* find each MP sub-nodes */
	for_each_child_of_node(root, child) {
		/* find the specified node */
		if (of_device_is_compatible(child, node_compatible)) {
			NVT_LOG("found child node %s\n", node_compatible);
			np = child;
			break;
		}
	}
	if (child == NULL) {
		NVT_ERR("Not found compatible node %s!\n", node_compatible);
		return -EPERM;
	}

	/* MP Config*/
	if (nvt_mp_parse_u32(np, "IC_X_CFG_SIZE", &IC_X_CFG_SIZE))
		return -EPERM;

	if (nvt_mp_parse_u32(np, "IC_Y_CFG_SIZE", &IC_Y_CFG_SIZE))
		return -EPERM;

#if TOUCH_KEY_NUM > 0
	if (nvt_mp_parse_u32(np, "IC_KEY_CFG_SIZE", &IC_KEY_CFG_SIZE))
		return -EPERM;
#endif

	if (nvt_mp_parse_u32(np, "X_Channel", &X_Channel))
		return -EPERM;

	if (nvt_mp_parse_u32(np, "Y_Channel", &Y_Channel))
		return -EPERM;

	if (nvt_mp_parse_ain(np, "AIN_X", AIN_X, IC_X_CFG_SIZE))
		return -EPERM;

	if (nvt_mp_parse_ain(np, "AIN_Y", AIN_Y, IC_Y_CFG_SIZE))
		return -EPERM;

#if TOUCH_KEY_NUM > 0
	if (nvt_mp_parse_ain(np, "AIN_KEY", AIN_KEY, IC_KEY_CFG_SIZE))
		return -EPERM;
#endif

	/* MP Criteria */
	if (nvt_mp_parse_array(np, "PS_Config_Lmt_Short_Rawdata_P",
			       PS_Config_Lmt_Short_Rawdata_P,
			       X_Channel * Y_Channel + Key_Channel))
		return -EPERM;

	if (nvt_mp_parse_array(np, "PS_Config_Lmt_Short_Rawdata_N",
			       PS_Config_Lmt_Short_Rawdata_N,
			       X_Channel * Y_Channel + Key_Channel))
		return -EPERM;

	if (nvt_mp_parse_array(np, "PS_Config_Lmt_Open_Rawdata_P",
			       PS_Config_Lmt_Open_Rawdata_P,
			       X_Channel * Y_Channel + Key_Channel))
		return -EPERM;

	if (nvt_mp_parse_array(np, "PS_Config_Lmt_Open_Rawdata_N",
			       PS_Config_Lmt_Open_Rawdata_N,
			       X_Channel * Y_Channel + Key_Channel))
		return -EPERM;

	if (nvt_mp_parse_array(np, "PS_Config_Lmt_FW_Rawdata_P",
			       PS_Config_Lmt_FW_Rawdata_P,
			       X_Channel * Y_Channel + Key_Channel))
		return -EPERM;

	if (nvt_mp_parse_array(np, "PS_Config_Lmt_FW_Rawdata_N",
			       PS_Config_Lmt_FW_Rawdata_N,
			       X_Channel * Y_Channel + Key_Channel))
		return -EPERM;

	if (ts->mp_tvcl_mode > MODE_4) {
		NVT_ERR("unsupported tvcl_mode %d\n", ts->mp_tvcl_mode);
		return -EINVAL;
	}

	if (ts->mp_ibias_mode > MODE_2) {
		NVT_ERR("unsupported ibias_mode %d\n", ts->mp_ibias_mode);
		return -EINVAL;
	}

	scnprintf(cc_limit_p, sizeof(cc_limit_p), "PS_Config_Lmt_FW_CC%s%s_P",
		tvcl_str[ts->mp_tvcl_mode], ibias_str[ts->mp_ibias_mode]);
	scnprintf(cc_limit_n, sizeof(cc_limit_n), "PS_Config_Lmt_FW_CC%s%s_N",
		tvcl_str[ts->mp_tvcl_mode], ibias_str[ts->mp_ibias_mode]);

	if (nvt_mp_parse_array(np, cc_limit_p, PS_Config_Lmt_FW_CC_P,
			       X_Channel * Y_Channel + Key_Channel))
		return -EPERM;

	if (nvt_mp_parse_array(np, cc_limit_n, PS_Config_Lmt_FW_CC_N,
			       X_Channel * Y_Channel + Key_Channel))
		return -EPERM;

	if (nvt_mp_parse_array(np, "PS_Config_Lmt_FW_Diff_P",
			       PS_Config_Lmt_FW_Diff_P,
			       X_Channel * Y_Channel + Key_Channel))
		return -EPERM;

	if (nvt_mp_parse_array(np, "PS_Config_Lmt_FW_Diff_N",
			       PS_Config_Lmt_FW_Diff_N,
			       X_Channel * Y_Channel + Key_Channel))
		return -EPERM;

	for (i = 0; i < X_Channel * Y_Channel + Key_Channel; i++) {
		PS_Config_Lmt_FW_Diff_P2P_N[i] = P2P_N_DEFAULT;
		PS_Config_Lmt_FW_Diff_P2P_P[i] = PS_Config_Lmt_FW_Diff_P[i] - PS_Config_Lmt_FW_Diff_N[i];
	}

	if (ts->pen_support) {
		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenTipX_FW_Raw_P",
					   PS_Config_Lmt_PenTipX_FW_Raw_P,
					   ts->x_num, ts->y_gang_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenTipX_FW_Raw_N",
					   PS_Config_Lmt_PenTipX_FW_Raw_N,
					   ts->x_num, ts->y_gang_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenTipY_FW_Raw_P",
					   PS_Config_Lmt_PenTipY_FW_Raw_P,
					   ts->x_gang_num, ts->y_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenTipY_FW_Raw_N",
					   PS_Config_Lmt_PenTipY_FW_Raw_N,
					   ts->x_gang_num, ts->y_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenRingX_FW_Raw_P",
					   PS_Config_Lmt_PenRingX_FW_Raw_P,
					   ts->x_num, ts->y_gang_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenRingX_FW_Raw_N",
					   PS_Config_Lmt_PenRingX_FW_Raw_N,
					   ts->x_num, ts->y_gang_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenRingY_FW_Raw_P",
					   PS_Config_Lmt_PenRingY_FW_Raw_P,
					   ts->x_gang_num, ts->y_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenRingY_FW_Raw_N",
					   PS_Config_Lmt_PenRingY_FW_Raw_N,
					   ts->x_gang_num, ts->y_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenTipX_FW_Diff_P",
					   PS_Config_Lmt_PenTipX_FW_Diff_P,
					   ts->x_num, ts->y_gang_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenTipX_FW_Diff_N",
					   PS_Config_Lmt_PenTipX_FW_Diff_N,
					   ts->x_num, ts->y_gang_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenTipY_FW_Diff_P",
					   PS_Config_Lmt_PenTipY_FW_Diff_P,
					   ts->x_gang_num, ts->y_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenTipY_FW_Diff_N",
					   PS_Config_Lmt_PenTipY_FW_Diff_N,
					   ts->x_gang_num, ts->y_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenRingX_FW_Diff_P",
					   PS_Config_Lmt_PenRingX_FW_Diff_P,
					   ts->x_num, ts->y_gang_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenRingX_FW_Diff_N",
					   PS_Config_Lmt_PenRingX_FW_Diff_N,
					   ts->x_num, ts->y_gang_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenRingY_FW_Diff_P",
					   PS_Config_Lmt_PenRingY_FW_Diff_P,
					   ts->x_gang_num, ts->y_num))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_PenRingY_FW_Diff_N",
					   PS_Config_Lmt_PenRingY_FW_Diff_N,
					   ts->x_gang_num, ts->y_num))
			return -EPERM;

		for (i = 0; i < ts->x_num * ts->y_gang_num; i++) {
			PS_Config_Lmt_PenTipX_FW_Diff_P2P_N[i] = P2P_N_DEFAULT;
			PS_Config_Lmt_PenTipX_FW_Diff_P2P_P[i] =
					PS_Config_Lmt_PenTipX_FW_Diff_P[i] -
					PS_Config_Lmt_PenTipX_FW_Diff_N[i];
			PS_Config_Lmt_PenRingX_FW_Diff_P2P_N[i] = P2P_N_DEFAULT;
			PS_Config_Lmt_PenRingX_FW_Diff_P2P_P[i] =
					PS_Config_Lmt_PenRingX_FW_Diff_P[i] -
					PS_Config_Lmt_PenRingX_FW_Diff_N[i];
		}
		for (i = 0; i < ts->x_gang_num * ts->y_num; i++) {
			PS_Config_Lmt_PenTipY_FW_Diff_P2P_N[i] = P2P_N_DEFAULT;
			PS_Config_Lmt_PenTipY_FW_Diff_P2P_P[i] =
					PS_Config_Lmt_PenTipY_FW_Diff_P[i] -
					PS_Config_Lmt_PenTipY_FW_Diff_N[i];
			PS_Config_Lmt_PenRingY_FW_Diff_P2P_N[i] = P2P_N_DEFAULT;
			PS_Config_Lmt_PenRingY_FW_Diff_P2P_P[i] =
					PS_Config_Lmt_PenRingY_FW_Diff_P[i] -
					PS_Config_Lmt_PenRingY_FW_Diff_N[i];
		}

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_Pen_Rx_Max_P",
				PS_Config_Lmt_Pen_Rx_Max_P, PEN_RX_MAX_X_LEN, PEN_RX_MAX_Y_LEN))
			return -EPERM;

		if (nvt_mp_parse_pen_array(np, "PS_Config_Lmt_Pen_Rx_Max_N",
				PS_Config_Lmt_Pen_Rx_Max_N, PEN_RX_MAX_X_LEN, PEN_RX_MAX_Y_LEN))
			return -EPERM;
	} /* if (ts->pen_support) */

	if (nvt_mp_parse_u32(np, "PS_Config_Diff_Test_Frame",
			     &PS_Config_Diff_Test_Frame))
		return -EPERM;

	NVT_LOG("Parse mp criteria done!\n");

	return 0;
}
#endif /* #ifdef CONFIG_OF */

/*******************************************************
Description:
	Novatek touchscreen MP function proc. file node
	initial function.

return:
	Executive outcomes. 0---succeed. -1---failed.
*******************************************************/
int32_t nvt_mp_proc_init(void)
{
	NVT_proc_selftest_entry = proc_create("nvt_selftest", 0444, NULL,
					      &nvt_selftest_fops);
	if (NVT_proc_selftest_entry == NULL) {
		NVT_ERR("create /proc/nvt_selftest Failed!\n");
		return -EPERM;
	} else {
		if (nvt_mp_buffer_init()) {
			NVT_ERR("Allocate mp memory failed\n");
			return -EPERM;
		} else {
			NVT_LOG("create /proc/nvt_selftest Succeeded!\n");
		}
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen MP function proc. file node
	deinitial function.

return:
	n.a.
*******************************************************/
void nvt_mp_proc_deinit(void)
{
	nvt_mp_buffer_deinit();

	if (NVT_proc_selftest_entry != NULL) {
		remove_proc_entry("nvt_selftest", NULL);
		NVT_proc_selftest_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", "nvt_selftest");
	}
}
#endif /* #if NVT_TOUCH_MP */
