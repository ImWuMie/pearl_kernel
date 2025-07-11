/*
 * Copyright (C) 2010 - 2018 Novatek, Inc.
 *
 * $Revision: 32206 $
 * $Date: 2018-08-10 19:23:04 +0800 (週五, 10 八月 2018) $
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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/uaccess.h>
#include <linux/input/mt.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/debugfs.h>
#include <linux/init.h>
#include <linux/notifier.h>
#include <linux/fb.h>
#include <linux/power_supply.h>
#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

#include "nt36xxx.h"
#ifndef NVT_SAVE_TESTDATA_IN_FILE
#include "nt36xxx_mp_ctrlram.h"
#endif
#if NVT_TOUCH_ESD_PROTECT
#include <linux/jiffies.h>
#endif /* #if NVT_TOUCH_ESD_PROTECT */
#if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)
#include "../xiaomi/xiaomi_touch.h"
#endif
#ifdef CONFIG_DRM_MEDIATEK_V2
#include "../../../gpu/drm/mediatek/mediatek_v2/mi_disp/mi_disp_notifier.h"
#include "../../../gpu/drm/mediatek/mediatek_v2/mtk_disp_recovery.h"
#endif
#if NVT_TOUCH_ESD_PROTECT
static struct delayed_work nvt_esd_check_work;
static struct workqueue_struct *nvt_esd_check_wq;
static unsigned long irq_timer = 0;
uint8_t esd_check = false;
uint8_t esd_retry = 0;
#endif /* #if NVT_TOUCH_ESD_PROTECT */

static char saved_cmdline[MAX_CMDLINE_PARAM_LEN] = {'\0'};
#if NVT_TOUCH_EXT_PROC
extern int32_t nvt_extra_proc_init(void);
extern void nvt_extra_proc_deinit(void);
#endif

#if NVT_TOUCH_MP
extern int32_t nvt_mp_proc_init(void);
extern void nvt_mp_proc_deinit(void);
#endif

struct nvt_ts_data *ts;
#if BOOT_UPDATE_FIRMWARE
static struct workqueue_struct *nvt_fwu_wq;
static struct workqueue_struct *nvt_lockdown_wq;
extern void Boot_Update_Firmware(struct work_struct *work);
#endif

#ifdef MI_DRM_NOTIFIER
static int nvt_drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data);
#else
static int nvt_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data);
#endif
#if defined(CONFIG_HAS_EARLYSUSPEND)
static void nvt_ts_early_suspend(struct early_suspend *h);
static void nvt_ts_late_resume(struct early_suspend *h);
#endif
static int32_t nvt_ts_suspend(struct device *dev);
static int32_t nvt_ts_resume(struct device *dev);
extern int get_lockdown_info_for_nvt(unsigned char *plockdowninfo);
static int32_t nvt_check_palm(uint8_t input_id, uint8_t *data);
static int nvt_write_ic_command(int mode, bool enable);
uint32_t ENG_RST_ADDR  = 0x7FFF80;
uint32_t SWRST_N8_ADDR = 0; /* read from dtsi */
uint32_t SPI_RD_FAST_ADDR = 0; /* read from dtsi */
#if IS_ENABLED(CONFIG_SPI_MT65XX)
const struct mtk_chip_config spi_ctrdata = {
#ifndef HAVE_PROC_OPS
	.rx_mlsb = 1,
	.tx_mlsb = 1,
	.deassert_mode = false,
#endif
	.sample_sel = 0,
	.cs_setuptime = 50,
	.cs_holdtime = 20,
	.cs_idletime = 0,
	.tick_delay = 0,
};
#endif  /*endif CONFIG_SPI_MT65XX*/

static ssize_t nvt_cg_color_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%c\n", ts->lockdown_info[2]);
}

static ssize_t nvt_cg_maker_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%c\n", ts->lockdown_info[6]);
}

static ssize_t nvt_display_maker_show(struct device *dev,
					struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%c\n", ts->lockdown_info[1]);
}

static ssize_t nvt_reg_debug_store(struct device *dev,struct device_attribute *attr,  const char *buf, size_t count)
{
	uint8_t i = 0;
	uint8_t tmp[65] = {0};
	char str[128] = {"\0"};
	int ret;
	char addr_buf[3];
	char data_buf[3];
	uint8_t cnt; /* for read is cnt(no big than 64, but alway return 64 byte),for write is first byte data to write */
	uint8_t data; /* second byte ,if read,this is ff, if write one byte this is fe */
	uint32_t addr;

    if (mutex_lock_interruptible(&ts->lock)) {
        return -ERESTARTSYS;
    }

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (sscanf(buf, "%02x %02x %02x %02x %02x", &addr_buf[0], &addr_buf[1], &addr_buf[2], &cnt, &data)) {
		addr = addr_buf[0] << 16 | addr_buf[1] << 8 | addr_buf[2];
		nvt_set_page(addr);
		if (0xff == data) {
			tmp[0] = (uint8_t) (addr & 0x7F);
			CTP_SPI_READ(ts->client, tmp, sizeof(tmp));
			for (i = 0; i < 4; i++) {
				snprintf(str, sizeof(str),
						"%02X %02X %02X %02X %02X %02X %02X %02X  "
						"%02X %02X %02X %02X %02X %02X %02X %02X\n",
						tmp[1 + i * 16], tmp[2 + i * 16], tmp[3 + i * 16], tmp[4 + i * 16],
						tmp[5 + i * 16], tmp[6 + i * 16], tmp[7 + i * 16], tmp[8 + i * 16],
						tmp[9 + i * 16], tmp[10 + i * 16], tmp[11 + i * 16], tmp[12 + i * 16],
						tmp[13 + i * 16], tmp[14 + i * 16], tmp[15 + i * 16], tmp[16 + i * 16]);
				NVT_LOG("%s", str);
			}
			nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
			mutex_unlock(&ts->lock);
			return count;
		} else if (0xfe == data) {
			data_buf[0] = (uint8_t) (addr & 0x7F);
			data_buf[1] = cnt;
			ret = CTP_SPI_WRITE(ts->client, data_buf, (sizeof(data_buf) / sizeof(data_buf[0]) - 1));
			if (ret) {
				NVT_ERR("write data to 0x%06X failed, ret = %d\n", addr, ret);
				nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
				mutex_unlock(&ts->lock);
				return -EINVAL;
			}
			nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
			mutex_unlock(&ts->lock);
			return count;
		} else {
			NVT_LOG("addr : %lu, data1 : %d, data2 : %d", addr, cnt, data);
			data_buf[0] = (uint8_t) (addr & 0x7F);
			data_buf[1] = cnt;
			data_buf[2] = data;
			ret = CTP_SPI_WRITE(ts->client, data_buf, sizeof(data_buf) / sizeof(data_buf[0]));
			if (ret) {
				NVT_ERR("write data to 0x%06X failed, ret = %d\n", addr, ret);
				nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
				mutex_unlock(&ts->lock);
				return -EINVAL;
			}
			nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
			mutex_unlock(&ts->lock);
			return count;
		}
	}

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
	mutex_unlock(&ts->lock);
	return -EINVAL;
}

static DEVICE_ATTR(cg_color, (S_IRUGO), nvt_cg_color_show, NULL);
static DEVICE_ATTR(cg_maker, (S_IRUGO), nvt_cg_maker_show, NULL);
static DEVICE_ATTR(display_maker, (S_IRUGO), nvt_display_maker_show, NULL);
static DEVICE_ATTR(nvt_reg_debug, 0644, NULL, nvt_reg_debug_store);

struct attribute *nvt_panel_attr[] = {
	&dev_attr_cg_color.attr,
	&dev_attr_cg_maker.attr,
	&dev_attr_display_maker.attr,
	&dev_attr_nvt_reg_debug.attr,
	NULL,
};

static uint8_t bTouchIsAwake = 0;
/*******************************************************
Description:
	Novatek touchscreen irq enable/disable function.

return:
	n.a.
*******************************************************/
static void nvt_irq_enable(bool enable)
{
	struct irq_desc *desc;

	if (enable) {
		if (!ts->irq_enabled) {
			enable_irq(ts->client->irq);
			ts->irq_enabled = true;
		}
	} else {
		if (ts->irq_enabled) {
			disable_irq(ts->client->irq);
			ts->irq_enabled = false;
		}
	}

	desc = irq_to_desc(ts->client->irq);
	NVT_LOG("enable=%d, desc->depth=%d\n", enable, desc->depth);
}

/*******************************************************
Description:
	Novatek touchscreen spi read/write core function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static inline int32_t spi_read_write(struct spi_device *client, uint8_t *buf, size_t len , NVT_SPI_RW rw)
{
	struct spi_message m;
	struct spi_transfer t = {
		.len    = len,
	};
	memset(ts->xbuf, 0, len + DUMMY_BYTES);

	memcpy(ts->xbuf, buf, len);

	switch (rw) {
		case NVTREAD:
			t.tx_buf = ts->xbuf;
			t.rx_buf = ts->rbuf;
			t.len    = (len + DUMMY_BYTES);
			break;

		case NVTWRITE:
			t.tx_buf = ts->xbuf;
			break;
	}
	spi_message_init(&m);
	spi_message_add_tail(&t, &m);
	return spi_sync(client, &m);
}

/*******************************************************
Description:
	Novatek touchscreen spi read function.

return:
	Executive outcomes. 2---succeed. -5---I/O error
*******************************************************/
int32_t CTP_SPI_READ(struct spi_device *client, uint8_t *buf, uint16_t len)
{
	int32_t ret = -1;
	int32_t retries = 0;
	mutex_lock(&ts->xbuf_lock);

	buf[0] = SPI_READ_MASK(buf[0]);

	while (retries < 5) {
		ret = spi_read_write(client, buf, len, NVTREAD);
		if (ret == 0) break;
		retries++;
	}

	if (unlikely(retries == 5)) {
		NVT_ERR("read error, ret=%d\n", ret);
		ret = -EIO;
	} else {
		memcpy((buf+1), (ts->rbuf+2), (len-1));
	}

	mutex_unlock(&ts->xbuf_lock);
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen spi write function.

return:
	Executive outcomes. 1---succeed. -5---I/O error
*******************************************************/
int32_t CTP_SPI_WRITE(struct spi_device *client, uint8_t *buf, uint16_t len)
{
	int32_t ret = -1;
	int32_t retries = 0;
	mutex_lock(&ts->xbuf_lock);
	buf[0] = SPI_WRITE_MASK(buf[0]);
	while (retries < 3) {
		ret = spi_read_write(client, buf, len, NVTWRITE);
		if (ret == 0)	break;
		retries++;
	}

	if (unlikely(retries == 3)) {
		NVT_ERR("error, ret=%d\n", ret);
		ret = -EIO;
	}
	mutex_unlock(&ts->xbuf_lock);
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen set index/page/addr address.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
int32_t nvt_set_page(uint32_t addr)
{
	uint8_t buf[4] = {0};

	buf[0] = 0xFF;	/* set index/page/addr command */
	buf[1] = (addr >> 15) & 0xFF;
	buf[2] = (addr >> 7) & 0xFF;

	return CTP_SPI_WRITE(ts->client, buf, 3);
}

/*******************************************************
Description:
	Novatek touchscreen write data to specify address.

return:
	Executive outcomes. 0---succeed. -5---access fail.
*******************************************************/
int32_t nvt_write_addr(uint32_t addr, uint8_t data)
{
	int32_t ret = 0;
	uint8_t buf[4] = {0};
	/* NVT_LOG("nvt_write_addr enter\n");*/
	/* ---set xdata index--- */
	buf[0] = 0xFF;	/* set index/page/addr command */
	buf[1] = (addr >> 15) & 0xFF;
	buf[2] = (addr >> 7) & 0xFF;
	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret) {
		NVT_ERR("set page 0x%06X failed, ret = %d\n", addr, ret);
		return ret;
	}

	/* ---write data to index--- */
	buf[0] = addr & (0x7F);
	buf[1] = data;
	ret = CTP_SPI_WRITE(ts->client, buf, 2);
	if (ret) {
		NVT_ERR("write data to 0x%06X failed, ret = %d\n", addr, ret);
		return ret;
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen enable hw bld crc function.

return:
	N/A.
*******************************************************/
void nvt_bld_crc_enable(void)
{
	uint8_t buf[4] = {0};

	/* ---set xdata index to BLD_CRC_EN_ADDR--- */
	nvt_set_page(ts->mmap->BLD_CRC_EN_ADDR);

	/* ---read data from index--- */
	buf[0] = ts->mmap->BLD_CRC_EN_ADDR & (0x7F);
	buf[1] = 0xFF;
	CTP_SPI_READ(ts->client, buf, 2);

	/* ---write data to index--- */
	buf[0] = ts->mmap->BLD_CRC_EN_ADDR & (0x7F);
	buf[1] = buf[1] | (0x01 << 7);
	CTP_SPI_WRITE(ts->client, buf, 2);
}

/*******************************************************
Description:
	Novatek touchscreen clear status & enable fw crc function.

return:
	N/A.
*******************************************************/
void nvt_fw_crc_enable(void)
{
	uint8_t buf[4] = {0};

	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	/* ---clear fw reset status--- */
	buf[0] = EVENT_MAP_RESET_COMPLETE & (0x7F);
	buf[1] = 0x00;
	CTP_SPI_WRITE(ts->client, buf, 2);

	/* ---enable fw crc--- */
	buf[0] = EVENT_MAP_HOST_CMD & (0x7F);
	buf[1] = 0xAE;	/* enable fw crc command */
	CTP_SPI_WRITE(ts->client, buf, 2);
}

/*******************************************************
Description:
	Novatek touchscreen set boot ready function.

return:
	N/A.
*******************************************************/
void nvt_boot_ready(void)
{
	/* ---write BOOT_RDY status cmds--- */
	nvt_write_addr(ts->mmap->BOOT_RDY_ADDR, 1);

	mdelay(5);

	if (!ts->hw_crc) {
		/* ---write BOOT_RDY status cmds--- */
		nvt_write_addr(ts->mmap->BOOT_RDY_ADDR, 0);

		/* ---write POR_CD cmds--- */
		nvt_write_addr(ts->mmap->POR_CD_ADDR, 0xA0);
	}
}

/*******************************************************
Description:
	Novatek touchscreen eng reset cmd
	function.

return:
	n.a.
*******************************************************/
void nvt_eng_reset(void)
{
	/* ---eng reset cmds to ENG_RST_ADDR--- */
	NVT_LOG("%s \n", __func__);
	nvt_write_addr(ENG_RST_ADDR, 0x5A);
	NVT_LOG("%s leave\n", __func__);
	mdelay(1);/* wait tMCU_Idle2TP_REX_Hi after TP_RST */
}

/*******************************************************
Description:
	Novatek touchscreen reset MCU
	function.

return:
	n.a.
*******************************************************/
void nvt_sw_reset(void)
{
	/* ---software reset cmds to SWRST_N8_ADDR--- */
	nvt_write_addr(SWRST_N8_ADDR, 0x55);

	msleep(10);
}

/*******************************************************
Description:
	Novatek touchscreen reset MCU then into idle mode
	function.

return:
	n.a.
*******************************************************/
void nvt_sw_reset_idle(void)
{
	/* ---MCU idle cmds to SWRST_N8_ADDR--- */
	nvt_write_addr(SWRST_N8_ADDR, 0xAA);

	usleep_range(1000, 1000);
}

/*******************************************************
Description:
	Novatek touchscreen reset MCU (boot) function.

return:
	n.a.
*******************************************************/
void nvt_bootloader_reset(void)
{
	/* ---reset cmds to SWRST_N8_ADDR--- */
	nvt_write_addr(SWRST_N8_ADDR, 0x69);
	mdelay(5); /* wait tBRST2FR after Bootload RST */

	if (SPI_RD_FAST_ADDR) {
		/* disable SPI_RD_FAST */
		nvt_write_addr(SPI_RD_FAST_ADDR, 0x00);
	}

	NVT_LOG("end\n");
}

/*******************************************************
Description:
	Novatek touchscreen clear FW status function.

return:
	Executive outcomes. 0---succeed. -1---fail.
*******************************************************/
int32_t nvt_clear_fw_status(void)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 20;

	for (i = 0; i < retry; i++) {
		/* ---set xdata index to EVENT BUF ADDR--- */
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

		/* ---clear fw status--- */
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_WRITE(ts->client, buf, 2);

		/* ---read fw status--- */
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0xFF;
		CTP_SPI_READ(ts->client, buf, 2);

		if (buf[1] == 0x00)
			break;

		usleep_range(10000, 10000);
	}

	if (i >= retry) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen check FW status function.

return:
	Executive outcomes. 0---succeed. -1---failed.
*******************************************************/
int32_t nvt_check_fw_status(void)
{
	uint8_t buf[8] = {0};
	int32_t i = 0;
	const int32_t retry = 50;

	usleep_range(20000, 20000);

	for (i = 0; i < retry; i++) {
		/* ---set xdata index to EVENT BUF ADDR--- */
		nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

		/* ---read fw status--- */
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = 0x00;
		CTP_SPI_READ(ts->client, buf, 2);

		if ((buf[1] & 0xF0) == 0xA0)
			break;

		usleep_range(10000, 10000);
	}

	if (i >= retry) {
		NVT_ERR("failed, i=%d, buf[1]=0x%02X\n", i, buf[1]);
		return -1;
	} else {
		return 0;
	}
}

/*******************************************************
Description:
	Novatek touchscreen check FW reset state function.

return:
	Executive outcomes. 0---succeed. -1---failed.
*******************************************************/
int32_t nvt_check_fw_reset_state(RST_COMPLETE_STATE check_reset_state)
{
	uint8_t buf[8] = {0};
	int32_t ret = 0;
	int32_t retry = 0;
	int32_t retry_max = (check_reset_state == RESET_STATE_INIT) ? 10 : 50;

	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_RESET_COMPLETE);

	while (1) {
		/* ---read reset state--- */
		buf[0] = EVENT_MAP_RESET_COMPLETE;
		buf[1] = 0x00;
		CTP_SPI_READ(ts->client, buf, 6);

		if ((buf[1] >= check_reset_state) && (buf[1] <= RESET_STATE_MAX)) {
			ret = 0;
			break;
		}

		retry++;
		if(unlikely(retry > retry_max)) {
			NVT_ERR("error, retry=%d, buf[1]=0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X\n",
				retry, buf[1], buf[2], buf[3], buf[4], buf[5]);
			ret = -1;
			break;
		}

		usleep_range(10000, 10000);
	}

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen get novatek project id information
	function.

return:
	Executive outcomes. 0---success. -1---fail.
*******************************************************/
int32_t nvt_read_pid(void)
{
	uint8_t buf[4] = {0};
	int32_t ret = 0;

	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_PROJECTID);

	/* ---read project id--- */
	buf[0] = EVENT_MAP_PROJECTID;
	buf[1] = 0x00;
	buf[2] = 0x00;
	CTP_SPI_READ(ts->client, buf, 3);

	ts->nvt_pid = (buf[2] << 8) + buf[1];

	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

	NVT_LOG("PID=%04X\n", ts->nvt_pid);

	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen get firmware related information
	function.

return:
	Executive outcomes. 0---success. -1---fail.
*******************************************************/
int32_t nvt_get_fw_info(void)
{
	uint8_t buf[64] = {0};
	uint32_t retry_count = 0;
	int32_t ret = 0;

#if 0
	/* READ CHIP ID */
	/* ---set xdata index to 0x1F600-- */
	nvt_set_page(0x1F600);
	buf[0] = 0x4E;
	buf[1] = 0x00;
	buf[2] = 0x00;
	buf[3] = 0x00;
	buf[4] = 0x00;
	buf[5] = 0x00;
	buf[6] = 0x00;
	CTP_SPI_READ(ts->client, buf, 7);
	NVT_LOG("buf[1]=0x%02X, buf[2]=0x%02X, buf[3]=0x%02X, buf[4]=0x%02X, buf[5]=0x%02X, buf[6]=0x%02X\n",
		buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);
	memset(buf, 0, 64);
#endif
info_retry:
	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_FWINFO);

	/* ---read fw info--- */
	buf[0] = EVENT_MAP_FWINFO;
	CTP_SPI_READ(ts->client, buf, 17);
	ts->fw_ver = buf[1];
	ts->x_num = buf[3];
	ts->y_num = buf[4];
	ts->abs_x_max = (uint16_t)((buf[5] << 8) | buf[6]);
	ts->abs_y_max = (uint16_t)((buf[7] << 8) | buf[8]);
	ts->max_button_num = buf[11];

	/* ---clear x_num, y_num if fw info is broken--- */
	if ((buf[1] + buf[2]) != 0xFF) {
		NVT_ERR("FW info is broken! fw_ver=0x%02X, ~fw_ver=0x%02X\n", buf[1], buf[2]);
		ts->fw_ver = 0;
		ts->x_num = 18;
		ts->y_num = 32;
		ts->abs_x_max = TOUCH_DEFAULT_MAX_WIDTH;
		ts->abs_y_max = TOUCH_DEFAULT_MAX_HEIGHT;
		ts->max_button_num = TOUCH_KEY_NUM;

		if(retry_count < 3) {
			retry_count++;
			NVT_ERR("retry_count=%d\n", retry_count);
			goto info_retry;
		} else {
			NVT_ERR("Set default fw_ver=%d, x_num=%d, y_num=%d, "
					"abs_x_max=%d, abs_y_max=%d, max_button_num=%d!\n",
					ts->fw_ver, ts->x_num, ts->y_num,
					ts->abs_x_max, ts->abs_y_max, ts->max_button_num);
			ret = -1;
		}
	} else {
		ret = 0;
	}

	NVT_LOG("FW type is 0x%02X, fw_ver=%d\n", buf[14], ts->fw_ver);

	/* ---Get Novatek PID--- */
	nvt_read_pid();
	return ret;
}

/*******************************************************
  Create Device Node (Proc Entry)
*******************************************************/
#if NVT_TOUCH_PROC
static struct proc_dir_entry *NVT_proc_entry;
#define DEVICE_NAME	"NVTSPI"

/*******************************************************
Description:
	Novatek touchscreen /proc/NVTSPI read function.

return:
	Executive outcomes. 2---succeed. -5,-14---failed.
*******************************************************/
static ssize_t nvt_flash_read(struct file *file, char __user *buff, size_t count, loff_t *offp)
{
	uint8_t *str = NULL;
	int32_t ret = 0;
	int32_t retries = 0;
	int8_t spi_wr = 0;
	uint8_t *buf;

	if ((count > NVT_TRANSFER_LEN + 3) || (count < 3)) {
		NVT_ERR("invalid transfer len!\n");
		return -EFAULT;
	}

	/* allocate buffer for spi transfer */
	str = (uint8_t *)kzalloc((count), GFP_KERNEL);
	if(str == NULL) {
		NVT_ERR("kzalloc for buf failed!\n");
		ret = -ENOMEM;
		goto kzalloc_failed;
	}

	buf = (uint8_t *)kzalloc((count), GFP_KERNEL | GFP_DMA);
	if(buf == NULL) {
		NVT_ERR("kzalloc for buf failed!\n");
		ret = -ENOMEM;
		kfree(str);
		str = NULL;
		goto kzalloc_failed;
	}

	if (copy_from_user(str, buff, count)) {
		NVT_ERR("copy from user error\n");
		ret = -EFAULT;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	/*
	 * stop esd check work to avoid case that 0x77 report righ after here to enable esd check again
	 * finally lead to trigger esd recovery bootloader reset
	 */
	cancel_delayed_work_sync(&nvt_esd_check_work);
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	spi_wr = str[0] >> 7;
	if ((((str[0] & 0x7F) << 8) | str[1]) > count){
		ret = -EINVAL;
		goto out;
        }
	memcpy(buf, str+2, ((str[0] & 0x7F) << 8) | str[1]);

	if (spi_wr == NVTWRITE) { /* SPI write */
		while (retries < 20) {
			ret = CTP_SPI_WRITE(ts->client, buf, ((str[0] & 0x7F) << 8) | str[1]);
			if (!ret)
				break;
			else
				NVT_ERR("error, retries=%d, ret=%d\n", retries, ret);

			retries++;
		}

		if (unlikely(retries == 20)) {
			NVT_ERR("error, ret = %d\n", ret);
			ret = -EIO;
			goto out;
		}
	} else if (spi_wr == NVTREAD) { /* SPI read */
		while (retries < 20) {
			ret = CTP_SPI_READ(ts->client, buf, ((str[0] & 0x7F) << 8) | str[1]);
			if (!ret)
				break;
			else
				NVT_ERR("error, retries=%d, ret=%d\n", retries, ret);

			retries++;
		}

		memcpy(str+2, buf, ((str[0] & 0x7F) << 8) | str[1]);
		/* copy buff to user if spi transfer */
		if (retries < 20) {
			if (copy_to_user(buff, str, count)) {
				ret = -EFAULT;
				goto out;
			}
		}

		if (unlikely(retries == 20)) {
			NVT_ERR("error, ret = %d\n", ret);
			ret = -EIO;
			goto out;
		}
	} else {
		NVT_ERR("Call error, str[0]=%d\n", str[0]);
		ret = -EFAULT;
		goto out;
	}

out:
	kfree(str);
	kfree(buf);
kzalloc_failed:
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen /proc/NVTSPI open function.

return:
	Executive outcomes. 0---succeed. -12---failed.
*******************************************************/
static int32_t nvt_flash_open(struct inode *inode, struct file *file)
{
	struct nvt_flash_data *dev;

	dev = kmalloc(sizeof(struct nvt_flash_data), GFP_KERNEL);
	if (dev == NULL) {
		NVT_ERR("Failed to allocate memory for nvt flash data\n");
		return -ENOMEM;
	}

	rwlock_init(&dev->lock);
	file->private_data = dev;

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen /proc/NVTSPI close function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_flash_close(struct inode *inode, struct file *file)
{
	struct nvt_flash_data *dev = file->private_data;

	if (dev)
		kfree(dev);

	return 0;
}
#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_flash_fops = {
	.proc_open = nvt_flash_open,
	.proc_release =  nvt_flash_close,
	.proc_read = nvt_flash_read,
};
#else
static const struct file_operations nvt_flash_fops = {
	.owner = THIS_MODULE,
	.open = nvt_flash_open,
	.release = nvt_flash_close,
	.read = nvt_flash_read,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/NVTSPI initial function.

return:
	Executive outcomes. 0---succeed. -12---failed.
*******************************************************/
static int32_t nvt_flash_proc_init(void)
{
	NVT_proc_entry = proc_create(DEVICE_NAME, 0444, NULL,&nvt_flash_fops);
	if (NVT_proc_entry == NULL) {
		NVT_ERR("Failed!\n");
		return -ENOMEM;
	} else {
		NVT_LOG("Succeeded!\n");
	}

	NVT_LOG("============================================================\n");
	NVT_LOG("Create /proc/%s\n", DEVICE_NAME);
	NVT_LOG("============================================================\n");

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen /proc/NVTSPI deinitial function.

return:
	n.a.
*******************************************************/
static void nvt_flash_proc_deinit(void)
{
	if (NVT_proc_entry != NULL) {
		remove_proc_entry(DEVICE_NAME, NULL);
		NVT_proc_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", DEVICE_NAME);
	}
}
#endif

#if WAKEUP_GESTURE
#define GESTURE_WORD_C			12
#define GESTURE_WORD_W			13
#define GESTURE_WORD_V			14
#define GESTURE_DOUBLE_CLICK	15
#define GESTURE_WORD_Z			16
#define GESTURE_WORD_M			17
#define GESTURE_WORD_O			18
#define GESTURE_WORD_e			19
#define GESTURE_WORD_S			20
#define GESTURE_SLIDE_UP		21
#define GESTURE_SLIDE_DOWN		22
#define GESTURE_SLIDE_LEFT		23
#define GESTURE_SLIDE_RIGHT		24
/* customized gesture id */
#define DATA_PROTOCOL			30

/* function page definition */
#define FUNCPAGE_GESTURE		1

/*******************************************************
Description:
	Novatek touchscreen wake up gesture key report function.

return:
	n.a.
*******************************************************/
void nvt_ts_wakeup_gesture_report(uint8_t gesture_id, uint8_t *data)
{
	uint8_t func_type = data[2];
	uint8_t func_id = data[3];

	/* support fw specifal data protocol */
	if ((gesture_id == DATA_PROTOCOL) && (func_type == FUNCPAGE_GESTURE)) {
		gesture_id = func_id;
	} else if (gesture_id > DATA_PROTOCOL) {
		NVT_ERR("gesture_id %d is invalid, func_type=%d, func_id=%d\n", gesture_id, func_type, func_id);
		return;
	}

	NVT_LOG("gesture_id = %d\n", gesture_id);

	switch (gesture_id) {
		case GESTURE_DOUBLE_CLICK:
			NVT_LOG("Gesture : Double Click.\n");
			input_report_key(ts->input_dev, KEY_WAKEUP, 1);
			input_sync(ts->input_dev);
			input_report_key(ts->input_dev, KEY_WAKEUP, 0);
			input_sync(ts->input_dev);
			break;
		default:
			break;
	}
}
#endif

/*******************************************************
Description:
	Novatek touchscreen parse device tree function.

return:
	n.a.
*******************************************************/
#ifdef CONFIG_OF
static int32_t nvt_parse_dt(struct device *dev)
{
	struct nvt_config_info *config_info;
	struct device_node *temp, *np = dev->of_node;
	int32_t ret = 0;
	uint32_t temp_val;

#if NVT_TOUCH_SUPPORT_HW_RST
	ts->reset_gpio = of_get_named_gpio_flags(np, "novatek,reset-gpio", 0, &ts->reset_flags);
	NVT_LOG("novatek,reset-gpio=%d\n", ts->reset_gpio);
#endif
	ts->irq_gpio = of_get_named_gpio_flags(np, "novatek,irq-gpio", 0, &ts->irq_flags);
	NVT_LOG("novatek,irq-gpio=%d\n", ts->irq_gpio);

	ret = of_property_read_u32(np, "novatek,swrst-n8-addr", &SWRST_N8_ADDR);
	if (ret) {
		NVT_ERR("error reading novatek,swrst-n8-addr. ret=%d\n", ret);
		return ret;
	} else {
		NVT_LOG("SWRST_N8_ADDR=0x%06X\n", SWRST_N8_ADDR);
	}

	ret = of_property_read_u32(np, "novatek,spi-rd-fast-addr", &SPI_RD_FAST_ADDR);
	if (ret) {
		NVT_ERR("not support novatek,spi-rd-fast-addr\n");
		SPI_RD_FAST_ADDR = 0;
		ret = 0;
	} else {
		NVT_LOG("SPI_RD_FAST_ADDR=0x%06X\n", SPI_RD_FAST_ADDR);
	}

	ret = of_property_read_u32(np, "novatek,config-array-size", &ts->config_array_size);
	if (ret) {
		NVT_ERR("Unable to get array size\n");
		return ret;
	} else {
		NVT_LOG("config-array-size: %u\n", ts->config_array_size);
	}

	ret = of_property_read_u32(np, "spi-max-frequency", &ts->spi_max_freq);
	if (ret) {
		NVT_ERR("Unable to get spi freq\n");
		return ret;
	} else {
		NVT_LOG("spi-max-frequency: %u\n", ts->spi_max_freq);
	}
	ret = of_property_read_u32(np, "super-resolution-factor", &ts->super_resolution_factor);
	if (ret) {
		NVT_ERR("no support, use default factors 1.0  for super resolution\n");
		ts->super_resolution_factor = 1;
	}

#ifdef  SUPPORT_GAME_VERSION2
	ret = of_property_read_u32(np, "novatek,touch-follow-performance-def", &temp_val);
	if (ret < 0)
		return ret;
	else
		ts->touch_follow_performance_def = temp_val;

	ret = of_property_read_u32(np, "novatek,touch-tap-sensitivity-def", &temp_val);
	if (ret < 0)
		return ret;
	else
		ts->touch_tap_sensitivity_def = temp_val;

	ret = of_property_read_u32(np, "novatek,touch-aim-sensitivity-def", &temp_val);
	if (ret < 0)
		return ret;
	else
		ts->touch_aim_sensitivity_def = temp_val;

	ret = of_property_read_u32(np, "novatek,touch-tap-stability-def", &temp_val);
	if (ret < 0)
		return ret;
	else
		ts->touch_tap_stability_def = temp_val;

	if (of_find_property(np, "novatek,touch-expert-array", &temp_val)) {
		ret = of_property_read_u32_array(np, "novatek,touch-expert-array",
				ts->touch_expert_array, temp_val / sizeof(u32));
		if (ret < 0)
			return ret;
	}
#endif

	ts->config_array = devm_kzalloc(dev, ts->config_array_size * sizeof(struct nvt_config_info), GFP_KERNEL);
	if (!ts->config_array) {
		NVT_ERR("Unable to allocate memory\n");
		return -ENOMEM;
	}


	config_info = ts->config_array;
	for_each_child_of_node(np, temp) {
		if (config_info - ts->config_array >= ts->config_array_size) {
			NVT_LOG("parse %ld config down\n", config_info - ts->config_array);
			break;
		}

		ret = of_property_read_u32(temp, "novatek,tp-vendor", &temp_val);
		if (ret) {
			NVT_ERR("Unable to read tp vendor\n");
		} else {
			config_info->tp_vendor = (u8) temp_val;
			NVT_LOG("tp vendor: %u", config_info->tp_vendor);
		}

		ret = of_property_read_u32(temp, "novatek,display-maker", &temp_val);
		if (ret) {
			NVT_ERR("Unable to read tp hw version\n");
		} else {
			config_info->display_maker = (u8) temp_val;
			NVT_LOG("tp hw version: %u", config_info->display_maker);
		}

		/*
		ret = of_property_read_u32(temp, "novatek,glass-vendor", &temp_val);
		if (ret) {
			NVT_ERR("Unable to read tp hw version\n");
		} else {
			config_info->glass_vendor = (u8) temp_val;
			NVT_LOG("tp hw version: %u", config_info->glass_vendor);
		}*/

		ret = of_property_read_string(temp, "novatek,fw-name",
						&config_info->nvt_fw_name);
		if (ret && (ret != -EINVAL)) {
			NVT_ERR("Unable to read fw name\n");
		} else {
			NVT_LOG("fw_name: %s", config_info->nvt_fw_name);
		}

		ret = of_property_read_string(temp, "novatek,mp-name",
						&config_info->nvt_mp_name);
		if (ret && (ret != -EINVAL)) {
			NVT_ERR("Unable to read mp name\n");
		} else {
			NVT_LOG("mp_name: %s", config_info->nvt_mp_name);
		}

		/*
		ret = of_property_read_string(temp, "novatek,limit-name",
						 &config_info->nvt_limit_name);
		if (ret && (ret != -EINVAL)) {
			NVT_LOG("Unable to read limit name\n");
		} else {
			NVT_LOG("limit_name: %s", config_info->nvt_limit_name);
		}*/
		config_info++;
	}
	return ret;
}
#endif

static bool nvt_cmds_panel_info(void)
{
	bool panel_id = false;
	char display_node[37] = {'\0'};
	char *match = (char *) strnstr(saved_cmdline,
				"panel_name=",
				strlen(saved_cmdline));
	if (match) {
		memcpy(display_node, (match + strlen("panel_name=")),
			sizeof(display_node) - 1);
		NVT_LOG("%s: display_node is %s\n", __func__, display_node);
		if (!strncmp(display_node, "dsi_j20s_36_02_0a_video_display",
					strlen("dsi_j20s_36_02_0a_video_display"))) {
			panel_id = true;
		}
	}
	return panel_id;
}

static int nvt_get_panel_type(struct nvt_ts_data *ts_data)
{
	int i;
	u8 *lockdown = ts_data->lockdown_info;
	struct nvt_config_info *panel_list = ts->config_array;

	for (i = 0; i < ts->config_array_size; i++) {

		if (lockdown[0] == panel_list[i].tp_vendor) {
			if(lockdown[0] == 0x46 || lockdown[0] == 0x53) {
				break;
			}
			if (lockdown[7] == panel_list[i].glass_vendor) {
				break;
			}
		}
	}

	ts->panel_index = i;

	if (i >= ts->config_array_size) {
		NVT_ERR("mismatch panel type, use default fw");
		ts->panel_index = -EINVAL;
		return ts->panel_index;
	}

	NVT_LOG("match panle type, fw is [%s], mp is [%s]",
		panel_list[i].nvt_fw_name, panel_list[i].nvt_mp_name);
	return ts->panel_index;
}

bool is_lockdown_empty(u8 *lockdown)
{
	bool ret = true;
	int i;
	for (i = 0; i < NVT_LOCKDOWN_SIZE; i++) {
		if (lockdown[i] != 0) {
			ret = false;
			break;
		}
	}

	return ret;
}

void nvt_match_fw(void)
{
	NVT_LOG("start match fw name");
	if (is_lockdown_empty(ts->lockdown_info))
		flush_delayed_work(&ts->nvt_lockdown_work);
	if (nvt_get_panel_type(ts) < 0) {
		if (nvt_cmds_panel_info()) {
			NVT_LOG("%s: default panel is first\n", __func__);
			ts->fw_name = DEFAULT_BOOT_UPDATE_FIRMWARE_FIRST;
			ts->mp_name = DEFAULT_MP_UPDATE_FIRMWARE_FIRST;
		} else {
			NVT_LOG("%s: default panel is second\n", __func__);
			ts->fw_name = DEFAULT_BOOT_UPDATE_FIRMWARE_SECOND;
			ts->mp_name = DEFAULT_MP_UPDATE_FIRMWARE_SECOND;
		}
	} else {
		ts->fw_name = ts->config_array[ts->panel_index].nvt_fw_name;
		ts->mp_name = ts->config_array[ts->panel_index].nvt_mp_name;
	}
}

/*******************************************************
Description:
	Novatek touchscreen config and request gpio

return:
	Executive outcomes. 0---succeed. not 0---failed.
*******************************************************/
static int nvt_gpio_config(struct nvt_ts_data *ts)
{
	int32_t ret = 0;

#if NVT_TOUCH_SUPPORT_HW_RST
	/* request RST-pin (Output/High) */
	if (gpio_is_valid(ts->reset_gpio)) {
		ret = gpio_request_one(ts->reset_gpio, GPIOF_OUT_INIT_LOW, "NVT-tp-rst");
		if (ret) {
			NVT_ERR("Failed to request NVT-tp-rst GPIO\n");
			goto err_request_reset_gpio;
		}
	}
#endif

	/* request INT-pin (Input) */
	if (gpio_is_valid(ts->irq_gpio)) {
		ret = gpio_request_one(ts->irq_gpio, GPIOF_IN, "NVT-int");
		if (ret) {
			NVT_ERR("Failed to request NVT-int GPIO\n");
			goto err_request_irq_gpio;
		}
	}
	NVT_LOG("%s Exit\n", __func__);
	return ret;

err_request_irq_gpio:
#if NVT_TOUCH_SUPPORT_HW_RST
	gpio_free(ts->reset_gpio);
err_request_reset_gpio:
#endif
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen deconfig gpio

return:
	n.a.
*******************************************************/
static void nvt_gpio_deconfig(struct nvt_ts_data *ts)
{
	if (gpio_is_valid(ts->irq_gpio))
		gpio_free(ts->irq_gpio);
#if NVT_TOUCH_SUPPORT_HW_RST
	if (gpio_is_valid(ts->reset_gpio))
		gpio_free(ts->reset_gpio);
#endif
}

void nvt_set_dbgfw_status(bool enable)
{
	ts->fw_debug = enable;
}

bool nvt_get_dbgfw_status(void)
{
	return ts->fw_debug;
}

static uint8_t nvt_fw_recovery(uint8_t *point_data)
{
	uint8_t i = 0;
	uint8_t detected = true;

	/* check pattern */
	for (i=1 ; i<7 ; i++) {
		if (point_data[i] != 0x77) {
			detected = false;
			break;
		}
	}

	return detected;
}

#if NVT_TOUCH_ESD_PROTECT
void nvt_esd_check_enable(uint8_t enable)
{
	/* update interrupt timer */
	irq_timer = jiffies;
	/* clear esd_retry counter, if protect function is enabled */
	esd_retry = enable ? 0 : esd_retry;
	/* enable/disable esd check flag */
	esd_check = enable;
}

extern struct mtk_drm_esd_ctx *g_esd_ctx;
static void nvt_esd_check_func(struct work_struct *work)
{
	unsigned int timer = jiffies_to_msecs(jiffies - irq_timer);

	if ((timer > NVT_TOUCH_ESD_CHECK_PERIOD) && esd_check) {
		if (esd_retry < 2) {
			mutex_lock(&ts->lock);
			NVT_LOG("do ESD recovery, timer = %d, retry = %d\n", timer, esd_retry);
			/* do esd recovery, reload fw */
			if (nvt_get_dbgfw_status()) {
				if (nvt_update_firmware(DEFAULT_DEBUG_FW_NAME) < 0) {
					NVT_ERR("use built-in fw");
					nvt_update_firmware(ts->fw_name);
				}
			} else {
				nvt_update_firmware(ts->fw_name);
			}
			mutex_unlock(&ts->lock);
			/* update interrupt timer */
			irq_timer = jiffies;
			/* update esd_retry counter */
			esd_retry++;
		} else {
			NVT_ERR("esd_retry = %d, g_trigger_disp_esd_recovery true\n", esd_retry);
 #ifdef CONFIG_MI_DISP_ESD_CHECK 
			if (g_esd_ctx->panel_init) {
				atomic_set(&g_esd_ctx->ext_te_event, 1);
				wake_up_interruptible(&g_esd_ctx->ext_te_wq);
				nvt_esd_check_enable(false);
				esd_retry = 0;
			}
#endif
		}
	}

	queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
			msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
}
#endif /* #if NVT_TOUCH_ESD_PROTECT */

#if NVT_TOUCH_WDT_RECOVERY
static uint8_t recovery_cnt = 0;
static uint8_t nvt_wdt_fw_recovery(uint8_t *point_data)
{
	uint32_t recovery_cnt_max = 10;
	uint8_t recovery_enable = false;
	uint8_t i = 0;

	recovery_cnt++;

	/* check pattern */
	for (i=1 ; i<7 ; i++) {
		if ((point_data[i] != 0xFD) && (point_data[i] != 0xFE)) {
			recovery_cnt = 0;
			break;
		}
	}

	if (recovery_cnt > recovery_cnt_max){
		recovery_enable = true;
		recovery_cnt = 0;
	}

	return recovery_enable;
}

void nvt_read_fw_history(uint32_t fw_history_addr)
{
	uint8_t i = 0;
	uint8_t buf[65];
	char str[128];
	if (fw_history_addr == 0)
		return;
	nvt_set_page(fw_history_addr);
	buf[0] = (uint8_t) (fw_history_addr & 0x7F);
	CTP_SPI_READ(ts->client, buf, 65);
	NVT_LOG("fw history 0x%x: \n", fw_history_addr);
	for (i = 0; i < 4; i++) {
		snprintf(str, sizeof(str),
				"%02X %02X %02X %02X %02X %02X %02X %02X  "
				"%02X %02X %02X %02X %02X %02X %02X %02X\n",
				buf[1 + i * 16], buf[2 + i * 16], buf[3 + i * 16], buf[4 + i * 16],
				buf[5 + i * 16], buf[6 + i * 16], buf[7 + i * 16], buf[8 + i * 16],
				buf[9 + i * 16], buf[10 + i * 16], buf[11 + i * 16], buf[12 + i * 16],
				buf[13 + i * 16], buf[14 + i * 16], buf[15 + i * 16], buf[16 + i * 16]);
		NVT_LOG("%s", str);
	}

	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);

}
#endif /* #if NVT_TOUCH_WDT_RECOVERY */

#if POINT_DATA_CHECKSUM
static int32_t nvt_ts_point_data_checksum(uint8_t *buf, uint8_t length)
{
	uint8_t checksum = 0;
	int32_t i = 0;

	/* Generate checksum */
	for (i = 0; i < length - 1; i++) {
		checksum += buf[i + 1];
	}
	checksum = (~checksum + 1);

	/* Compare ckecksum and dump fail data */
	if (checksum != buf[length]) {
		NVT_ERR("i2c/spi packet checksum not match. (point_data[%d]=0x%02X, checksum=0x%02X)\n",
				length, buf[length], checksum);
		for (i = 0; i < 10; i++) {
			NVT_LOG("%02X %02X %02X %02X %02X %02X\n",
					buf[1 + i*6], buf[2 + i*6], buf[3 + i*6], buf[4 + i*6], buf[5 + i*6], buf[6 + i*6]);
		}
		NVT_LOG("%02X %02X %02X %02X %02X\n", buf[61], buf[62], buf[63], buf[64], buf[65]);
		return -1;
	}

	return 0;
}
#endif /* POINT_DATA_CHECKSUM */
/*******************************************************
Description:
	Novatek touchscreen work function.

return:
	n.a.
*******************************************************/
static irqreturn_t nvt_ts_work_func(int irq, void *data)
{
	int32_t ret = -1;
	uint8_t point_data[POINT_DATA_LEN + 1 + DUMMY_BYTES] = {0};
	uint32_t position = 0;
	uint32_t input_x = 0;
	uint32_t input_y = 0;
	uint32_t input_w = 0;
	uint32_t input_p = 0;
	uint8_t input_id = 0;
#if MT_PROTOCOL_B
	uint8_t press_id[TOUCH_MAX_FINGER_NUM] = {0};
#endif /* MT_PROTOCOL_B */
	int32_t i = 0;
	int32_t finger_cnt = 0;

#if WAKEUP_GESTURE
	if (bTouchIsAwake == 0) {
		pm_wakeup_event(&ts->input_dev->dev, 5000);
	}
#endif
	mutex_lock(&ts->lock);
	if (ts->dev_pm_suspend) {
		ret = wait_for_completion_timeout(&ts->dev_pm_suspend_completion, msecs_to_jiffies(500));
		if (!ret) {
			NVT_ERR("system(spi) can't finished resuming procedure, skip it\n");
			goto XFER_ERROR;
		}
	}

	ret = CTP_SPI_READ(ts->client, point_data, POINT_DATA_LEN + 1);
	if (ret < 0) {
		NVT_ERR("CTP_SPI_READ failed.(%d)\n", ret);
		goto XFER_ERROR;
	}
	/*
	//--- dump SPI buf ---
	for (i = 0; i < 10; i++) {
		printk("%02X %02X %02X %02X %02X %02X  ",
			point_data[1+i*6], point_data[2+i*6], point_data[3+i*6], point_data[4+i*6], point_data[5+i*6], point_data[6+i*6]);
	}
	printk("\n");*/

#if NVT_TOUCH_WDT_RECOVERY
	/* ESD protect by WDT */
	if (nvt_wdt_fw_recovery(point_data)) {
		NVT_ERR("Recover for fw reset, %02X\n", point_data[1]);
		if (point_data[1] == 0xFE) {
				nvt_sw_reset_idle();
		}
		nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT0);
		nvt_read_fw_history(ts->mmap->MMAP_HISTORY_EVENT1);
		if (nvt_get_dbgfw_status()) {
			if (nvt_update_firmware(DEFAULT_DEBUG_FW_NAME) < 0) {
				NVT_ERR("use built-in fw");
				nvt_update_firmware(ts->fw_name);
			}
		} else {
			nvt_update_firmware(ts->fw_name);
		}
		goto XFER_ERROR;
   }
#endif /* #if NVT_TOUCH_WDT_RECOVERY */

	/* ESD protect by FW handshake */
	if (nvt_fw_recovery(point_data)) {
#if NVT_TOUCH_ESD_PROTECT
		nvt_esd_check_enable(true);
#endif /* #if NVT_TOUCH_ESD_PROTECT */
		goto XFER_ERROR;
	}

#if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)
	input_id = (uint8_t)(point_data[1] >> 3);
	if (nvt_check_palm(input_id, point_data)) {
		goto XFER_ERROR; /* to skip point data parsing */
	}
#endif

#if POINT_DATA_CHECKSUM
	if (POINT_DATA_LEN >= POINT_DATA_CHECKSUM_LEN) {
		ret = nvt_ts_point_data_checksum(point_data, POINT_DATA_CHECKSUM_LEN);
		if (ret) {
			goto XFER_ERROR;
		}
	}
#endif /* POINT_DATA_CHECKSUM */

#if WAKEUP_GESTURE
	if (bTouchIsAwake == 0) {
		input_id = (uint8_t)(point_data[1] >> 3);
		nvt_ts_wakeup_gesture_report(input_id, point_data);
		mutex_unlock(&ts->lock);
		return IRQ_HANDLED;
	}
#endif

	finger_cnt = 0;

	for (i = 0; i < ts->max_touch_num; i++) {
		position = 1 + 6 * i;
		input_id = (uint8_t)(point_data[position + 0] >> 3);
		if ((input_id == 0) || (input_id > ts->max_touch_num))
			continue;

		if (((point_data[position] & 0x07) == 0x01) || ((point_data[position] & 0x07) == 0x02)) {	//finger down (enter & moving)
#if NVT_TOUCH_ESD_PROTECT
			/* update interrupt timer */
			irq_timer = jiffies;
#endif /* #if NVT_TOUCH_ESD_PROTECT */
			if (10 == ts->super_resolution_factor) {
				input_x = (uint32_t)(point_data[position + 1] << 8) + (uint32_t) (point_data[position + 2]);
				input_y = (uint32_t)(point_data[position + 3] << 8) + (uint32_t) (point_data[position + 4]);
				if ((input_x < 0) || (input_y < 0))
					continue;
				if ((input_x > ts->abs_x_max * NVT_SUPER_RESOLUTION_10S - 1) || (input_y > ts->abs_y_max * NVT_SUPER_RESOLUTION_10S - 1))
					continue;
				input_w = (uint32_t)(point_data[position + 5]);
				if (input_w == 0)
					input_w = 1;
				input_p = (uint32_t)(point_data[1 + 98 + i]);
				if (input_p == 0)
					input_p = 1;
			} else if ( 1 == ts->super_resolution_factor) {
				input_x = (uint32_t)(point_data[position + 1] << 4) + (uint32_t) (point_data[position + 3] >> 4);
				input_y = (uint32_t)(point_data[position + 2] << 4) + (uint32_t) (point_data[position + 3] & 0x0F);
				if ((input_x < 0) || (input_y < 0))
					continue;
				if ((input_x > ts->abs_x_max) || (input_y > ts->abs_y_max))
					continue;
				input_w = (uint32_t)(point_data[position + 4]);
				if (input_w == 0)
					input_w = 1;
				if (i < 2) {
					input_p = (uint32_t)(point_data[position + 5]) + (uint32_t)(point_data[i + 63] << 8);
					if (input_p > TOUCH_FORCE_NUM)
						input_p = TOUCH_FORCE_NUM;
				} else {
					input_p = (uint32_t)(point_data[position + 5]);
				}
				if (input_p == 0)
					input_p = 1;
			}

#if MT_PROTOCOL_B
			press_id[input_id - 1] = 1;
			input_mt_slot(ts->input_dev, input_id - 1);
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, true);
			input_report_key(ts->input_dev, BTN_TOUCH, 1);
			input_report_key(ts->input_dev, BTN_TOOL_FINGER, 1);

#else /* MT_PROTOCOL_B */
			input_report_abs(ts->input_dev, ABS_MT_TRACKING_ID, input_id - 1);
			input_report_key(ts->input_dev, BTN_TOUCH, 1);
#endif /* MT_PROTOCOL_B */

			input_report_abs(ts->input_dev, ABS_MT_POSITION_X, input_x);
			input_report_abs(ts->input_dev, ABS_MT_POSITION_Y, input_y);
			/*input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, input_w);*/
			/*input_report_abs(ts->input_dev, ABS_MT_PRESSURE, input_p);*/
#if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)
			last_touch_events_collect(input_id - 1, 1);
#endif

#if MT_PROTOCOL_B
#else /* MT_PROTOCOL_B */
			input_mt_sync(ts->input_dev);
#endif /* MT_PROTOCOL_B */

			set_bit(input_id - 1, ts->slot_map);
			finger_cnt++;
		}
	}

#if MT_PROTOCOL_B
	for (i = 0; i < ts->max_touch_num; i++) {
		if (press_id[i] != 1) {
			input_mt_slot(ts->input_dev, i);
			/*input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);*/
			input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, false);
			/*input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0); */
#if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)
			last_touch_events_collect(i, 0);
#endif
			if (finger_cnt == 0 && test_bit(i, ts->slot_map)) {
				input_report_key(ts->input_dev, BTN_TOUCH, 0);
				input_report_key(ts->input_dev, BTN_TOOL_FINGER, 0);
				NVT_ERR("finger leave\n");
			}
			clear_bit(i, ts->slot_map);
		}
	}
	/* input_report_key(ts->input_dev, BTN_TOUCH, (finger_cnt > 0)); */
#else /* MT_PROTOCOL_A */
	if (finger_cnt == 0) {
		input_report_key(ts->input_dev, BTN_TOUCH, 0);
		input_mt_sync(ts->input_dev);
	}
#endif /* MT_PROTOCOL_B */

	input_sync(ts->input_dev);

XFER_ERROR:
	mutex_unlock(&ts->lock);
	return IRQ_HANDLED;
}


/*******************************************************
Description:
	Novatek touchscreen check chip version trim function.

return:
	Executive outcomes. 0---NVT IC. -1---not NVT IC.
*******************************************************/
static int8_t nvt_ts_check_chip_ver_trim(uint32_t chip_ver_trim_addr)
{
	uint8_t buf[8] = {0};
	int32_t retry = 0;
	int32_t list = 0;
	int32_t i = 0;
	int32_t found_nvt_chip = 0;
	int32_t ret = -1;

	/* ---Check for 5 times--- */
	for (retry = 5; retry > 0; retry--) {

		nvt_bootloader_reset();
		/*---set xdata index to 0x3F004---*/
		nvt_set_page(chip_ver_trim_addr);

		buf[0] = chip_ver_trim_addr & 0x7F;
		buf[1] = 0x00;
		buf[2] = 0x00;
		buf[3] = 0x00;
		buf[4] = 0x00;
		buf[5] = 0x00;
		buf[6] = 0x00;
		ret = CTP_SPI_READ(ts->client, buf, 7);
		NVT_LOG("ret = 0x%02x, buf[0] = 0x%02x,buf[1]=0x%02X, buf[2]=0x%02X, buf[3]=0x%02X, buf[4]=0x%02X, buf[5]=0x%02X, buf[6]=0x%02X\n",
			ret,buf[0],buf[1], buf[2], buf[3], buf[4], buf[5], buf[6]);

		/* compare read chip id on supported list */
		for (list = 0; list < (sizeof(trim_id_table) / sizeof(struct nvt_ts_trim_id_table)); list++) {
			found_nvt_chip = 0;
			/* compare each byte */
			for (i = 0; i < NVT_ID_BYTE_MAX; i++) {
				if (trim_id_table[list].mask[i]) {
					if (buf[i + 1] != trim_id_table[list].id[i])
						break;
				}
			}

			if (i == NVT_ID_BYTE_MAX) {
				found_nvt_chip = 1;
			}

			if (found_nvt_chip) {
				NVT_LOG("This is NVT touch IC\n");
				ts->mmap = trim_id_table[list].mmap;
				ts->carrier_system = trim_id_table[list].hwinfo->carrier_system;
				ts->hw_crc = trim_id_table[list].hwinfo->hw_crc;
				ret = 0;
				goto out;
			} else {
				ts->mmap = NULL;
				ret = -1;
			}
		}

		msleep(10);
	}

out:
	return ret;
}

static int nvt_pinctrl_init(struct nvt_ts_data *nvt_data)
{
	int retval = 0;
	/* Get pinctrl if target uses pinctrl */
	nvt_data->ts_pinctrl = devm_pinctrl_get(&nvt_data->client->dev);
	NVT_LOG("%s Enter\n", __func__);
	if (IS_ERR_OR_NULL(nvt_data->ts_pinctrl)) {
		retval = PTR_ERR(nvt_data->ts_pinctrl);
		NVT_ERR("Target does not use pinctrl %d\n", retval);
		goto err_pinctrl_get;
	}

	nvt_data->pinctrl_state_active
		= pinctrl_lookup_state(nvt_data->ts_pinctrl, PINCTRL_STATE_ACTIVE);

	if (IS_ERR_OR_NULL(nvt_data->pinctrl_state_active)) {
		retval = PTR_ERR(nvt_data->pinctrl_state_active);
		NVT_ERR("Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_ACTIVE, retval);
		goto err_pinctrl_lookup;
	}

	nvt_data->pinctrl_state_suspend
		= pinctrl_lookup_state(nvt_data->ts_pinctrl, PINCTRL_STATE_SUSPEND);

	if (IS_ERR_OR_NULL(nvt_data->pinctrl_state_suspend)) {
		retval = PTR_ERR(nvt_data->pinctrl_state_suspend);
		NVT_ERR("Can not lookup %s pinstate %d\n",
			PINCTRL_STATE_SUSPEND, retval);
		goto err_pinctrl_lookup;
	}
	nvt_data->pinctrl_state_spimode
		= pinctrl_lookup_state(nvt_data->ts_pinctrl, "pmx_ts_spimode");

	if (IS_ERR_OR_NULL(nvt_data->pinctrl_state_spimode)) {
		retval = PTR_ERR(nvt_data->pinctrl_state_spimode);
		NVT_ERR("Can not lookup %s pinstate %d\n",
			"pmx_ts_spimode", retval);
		goto err_pinctrl_lookup;
	}

	return 0;
err_pinctrl_lookup:
	devm_pinctrl_put(nvt_data->ts_pinctrl);
err_pinctrl_get:
	nvt_data->pinctrl_state_active = NULL;
	nvt_data->pinctrl_state_suspend = NULL;
	nvt_data->pinctrl_state_spimode = NULL;
	nvt_data->ts_pinctrl = NULL;
	return retval;
}

static void nvt_switch_mode_work(struct work_struct *work)
{
	NVT_LOG("%s double click wakeup", ts->db_wakeup ? "ENABLE" : "DISABLE");
	if (ts->ic_state <= NVT_IC_SUSPEND_OUT && ts->ic_state != NVT_IC_INIT ) {
		ts->gesture_command_delayed = ts->db_wakeup;
		NVT_ERR("Panel off, don't set dbclick gesture flag util panel on");
		ts->db_wakeup = 0;
	} else  if (ts->ic_state >= NVT_IC_RESUME_IN){
		tp_enable_doubleclick(!!ts->db_wakeup);
	}
}

static void nvt_short_test_work(struct work_struct *work)
{
	ts->result_type = nvt_short_test();
	ts->selftest_done = true;
	wake_up_interruptible(&ts->selftest_wait_queue);
}

static void nvt_open_test_work(struct work_struct *work)
{
	ts->result_type = nvt_open_test();
	ts->selftest_done = true;
	wake_up_interruptible(&ts->selftest_wait_queue);
}

#if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)

static struct xiaomi_touch_interface xiaomi_touch_interfaces;

int32_t nvt_check_palm(uint8_t input_id, uint8_t *data)
{
	int32_t ret = 0;
	uint8_t func_type = data[2];
	uint8_t palm_state = data[3];

	if ((input_id == DATA_PROTOCOL) && (func_type == FUNCPAGE_PALM)) {
		ret = palm_state;
		if (palm_state == PACKET_PALM_ON) {
			NVT_LOG("get packet palm on event.\n");
			update_palm_sensor_value(1);
		} else if (palm_state == PACKET_PALM_OFF) {
			NVT_LOG("get packet palm off event.\n");
			update_palm_sensor_value(0);
		} else {
			NVT_ERR("invalid palm state %d!\n", palm_state);
			ret = -1;
		}
	} else {
		ret = 0;
	}

	return ret;
}

static int nvt_palm_sensor_write(int value)
{
	int ret = 0;
	NVT_LOG("enter %d %d\n", value, ts->palm_sensor_switch);
	ts->palm_sensor_switch = value;

	if (ts->dev_pm_suspend) {
		NVT_LOG("tp has dev_pm_suspend status\n");
		return 0;
	}

	if (bTouchIsAwake) {
		mutex_lock(&ts->lock);
#if NVT_TOUCH_ESD_PROTECT
		nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */
		ret = nvt_set_pocket_palm_switch(value);
		mutex_unlock(&ts->lock);
	}
	return ret;
}


static struct xiaomi_touch_interface xiaomi_touch_interfaces;
static void nvt_init_touchmode_data(void)
{
	int i;

	NVT_LOG("%s,ENTER\n", __func__);
	/* Touch Game Mode Switch */
	xiaomi_touch_interfaces.touch_mode[Touch_Game_Mode][GET_MAX_VALUE] = 1;
	xiaomi_touch_interfaces.touch_mode[Touch_Game_Mode][GET_MIN_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Game_Mode][GET_DEF_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Game_Mode][SET_CUR_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Game_Mode][GET_CUR_VALUE] = 0;

	/* Acitve Mode */
	xiaomi_touch_interfaces.touch_mode[Touch_Active_MODE][GET_MAX_VALUE] = 1;
	xiaomi_touch_interfaces.touch_mode[Touch_Active_MODE][GET_MIN_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Active_MODE][GET_DEF_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Active_MODE][SET_CUR_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Active_MODE][GET_CUR_VALUE] = 0;

#ifdef SUPPORT_GAME_VERSION2
	/* Tap sensitivity */
	xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][GET_MAX_VALUE] = 5;
	xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][GET_MIN_VALUE] = 1;
	xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][GET_DEF_VALUE] = ts->touch_tap_sensitivity_def;
	xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][SET_CUR_VALUE] = ts->touch_tap_sensitivity_def;
	xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][GET_CUR_VALUE] = ts->touch_tap_sensitivity_def;

	/* following performance */
	xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][GET_MAX_VALUE] = 5;
	xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][GET_MIN_VALUE] = 1;
	xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][GET_DEF_VALUE] = ts->touch_follow_performance_def;
	xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][SET_CUR_VALUE] = ts->touch_follow_performance_def;
	xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][GET_CUR_VALUE] = ts->touch_follow_performance_def;

	/* Aim sensitivity */
	xiaomi_touch_interfaces.touch_mode[Touch_Aim_Sensitivity][GET_DEF_VALUE] = ts->touch_aim_sensitivity_def;
	xiaomi_touch_interfaces.touch_mode[Touch_Aim_Sensitivity][GET_CUR_VALUE] = ts->touch_aim_sensitivity_def;
	xiaomi_touch_interfaces.touch_mode[Touch_Aim_Sensitivity][SET_CUR_VALUE] = ts->touch_aim_sensitivity_def;
	xiaomi_touch_interfaces.touch_mode[Touch_Aim_Sensitivity][GET_MAX_VALUE] = 5;
	xiaomi_touch_interfaces.touch_mode[Touch_Aim_Sensitivity][GET_MIN_VALUE] = 1;

	/* Tap stability */
	xiaomi_touch_interfaces.touch_mode[Touch_Tap_Stability][GET_DEF_VALUE] = ts->touch_tap_stability_def;
	xiaomi_touch_interfaces.touch_mode[Touch_Tap_Stability][GET_CUR_VALUE] = ts->touch_tap_stability_def;
	xiaomi_touch_interfaces.touch_mode[Touch_Tap_Stability][SET_CUR_VALUE] = ts->touch_tap_stability_def;
	xiaomi_touch_interfaces.touch_mode[Touch_Tap_Stability][GET_MAX_VALUE] = 5;
	xiaomi_touch_interfaces.touch_mode[Touch_Tap_Stability][GET_MIN_VALUE] = 1;

	/* Expert Mode */
	xiaomi_touch_interfaces.touch_mode[Touch_Expert_Mode][GET_DEF_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Expert_Mode][GET_CUR_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Expert_Mode][SET_CUR_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Expert_Mode][GET_MAX_VALUE] = 3;
	xiaomi_touch_interfaces.touch_mode[Touch_Expert_Mode][GET_MIN_VALUE] = 1;
#else
	/* Tap sensitivity */
	xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][GET_MAX_VALUE] = 2;
	xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][GET_MIN_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][GET_DEF_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][SET_CUR_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][GET_CUR_VALUE] = 0;

	/* following performance */
	xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][GET_MAX_VALUE] = 2;
	xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][GET_MIN_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][GET_DEF_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][SET_CUR_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][GET_CUR_VALUE] = 0;
#endif

	/* Panel orientation */
	xiaomi_touch_interfaces.touch_mode[Touch_Panel_Orientation][GET_MAX_VALUE] = 3;
	xiaomi_touch_interfaces.touch_mode[Touch_Panel_Orientation][GET_MIN_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Panel_Orientation][GET_DEF_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Panel_Orientation][SET_CUR_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Panel_Orientation][GET_CUR_VALUE] = 0;

	/* Edge filter area */
	xiaomi_touch_interfaces.touch_mode[Touch_Edge_Filter][GET_MAX_VALUE] = 3;
	xiaomi_touch_interfaces.touch_mode[Touch_Edge_Filter][GET_MIN_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Edge_Filter][GET_DEF_VALUE] = 2;
	xiaomi_touch_interfaces.touch_mode[Touch_Edge_Filter][SET_CUR_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Edge_Filter][GET_CUR_VALUE] = 0;

	/* Resist RF interference */
	xiaomi_touch_interfaces.touch_mode[Touch_Resist_RF][GET_MAX_VALUE] = 1;
	xiaomi_touch_interfaces.touch_mode[Touch_Resist_RF][GET_MIN_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Resist_RF][GET_DEF_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Resist_RF][SET_CUR_VALUE] = 0;
	xiaomi_touch_interfaces.touch_mode[Touch_Resist_RF][GET_CUR_VALUE] = 0;

	for (i = 0; i < Touch_Mode_NUM; i++) {
		NVT_LOG("mode:%d, set cur:%d, get cur:%d, def:%d min:%d max:%d\n",
			i,
			xiaomi_touch_interfaces.touch_mode[i][SET_CUR_VALUE],
			xiaomi_touch_interfaces.touch_mode[i][GET_CUR_VALUE],
			xiaomi_touch_interfaces.touch_mode[i][GET_DEF_VALUE],
			xiaomi_touch_interfaces.touch_mode[i][GET_MIN_VALUE],
			xiaomi_touch_interfaces.touch_mode[i][GET_MAX_VALUE]);
	}

	return;
}

static int nvt_touchfeature_cmd_xsfer(uint8_t *touchfeature)
{
	int ret;
	uint8_t buf[8] = {0};

	NVT_LOG("++\n");
	NVT_LOG("cmd xsfer:%02x, %02x", touchfeature[0], touchfeature[1]);
	/* ---set xdata index to EVENT BUF ADDR--- */
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	if (ret < 0) {
		NVT_ERR("Set event buffer index fail!\n");
		goto nvt_touchfeature_cmd_xsfer_out;
	}

	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = touchfeature[0];
	buf[2] = touchfeature[1];

	ret = CTP_SPI_WRITE(ts->client, buf, 3);
	if (ret < 0) {
		NVT_ERR("Write sensitivity switch command fail!\n");
		goto nvt_touchfeature_cmd_xsfer_out;
	}

nvt_touchfeature_cmd_xsfer_out:
	NVT_LOG("--\n");
	return ret;

}

static int nvt_touchfeature_set(uint8_t *touchfeature)
{
	int ret;
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	ret = nvt_touchfeature_cmd_xsfer(touchfeature);
	if (ret < 0)
		NVT_ERR("send cmd via SPI failed, errno:%d", ret);

	mutex_unlock(&ts->lock);
	msleep(35);
	return ret;
}


static int nvt_set_cur_value(int nvt_mode, int nvt_value)
{
	bool skip = false;
	uint8_t nvt_game_value[2] = {0};
	uint8_t temp_value = 0;
	uint8_t ret = 0;

	if (nvt_mode >= Touch_Mode_NUM || nvt_mode < 0) {
		NVT_ERR("%s, nvt mode is error:%d", __func__, nvt_mode);
		return -EINVAL;
	}

	if (nvt_mode == Touch_Doubletap_Mode && ts && nvt_value >= 0) {
		ts-> db_wakeup = nvt_value;
		schedule_work(&ts->switch_mode_work);
	}

	xiaomi_touch_interfaces.touch_mode[nvt_mode][SET_CUR_VALUE] = nvt_value;

	if (xiaomi_touch_interfaces.touch_mode[nvt_mode][SET_CUR_VALUE] >
			xiaomi_touch_interfaces.touch_mode[nvt_mode][GET_MAX_VALUE]) {

		xiaomi_touch_interfaces.touch_mode[nvt_mode][SET_CUR_VALUE] =
				xiaomi_touch_interfaces.touch_mode[nvt_mode][GET_MAX_VALUE];

	} else if (xiaomi_touch_interfaces.touch_mode[nvt_mode][SET_CUR_VALUE] <
			xiaomi_touch_interfaces.touch_mode[nvt_mode][GET_MIN_VALUE]) {

		xiaomi_touch_interfaces.touch_mode[nvt_mode][SET_CUR_VALUE] =
				xiaomi_touch_interfaces.touch_mode[nvt_mode][GET_MIN_VALUE];
	}

	switch (nvt_mode) {
	case Touch_Game_Mode:
			break;
	case Touch_Active_MODE:
			break;
#ifdef SUPPORT_GAME_VERSION2
	case Touch_UP_THRESHOLD:
			temp_value = xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][SET_CUR_VALUE];
			nvt_game_value[0] = 0x71;
			nvt_game_value[1] = temp_value - 1;
			break;
	case Touch_Tolerance:
			temp_value = xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][SET_CUR_VALUE];
			nvt_game_value[0] = 0x70;
			nvt_game_value[1] = temp_value - 1;
			break;
	case Touch_Aim_Sensitivity:
			temp_value = xiaomi_touch_interfaces.touch_mode[Touch_Aim_Sensitivity][SET_CUR_VALUE];
			nvt_game_value[0] = 0x79;
			nvt_game_value[1] = temp_value - 1;
			break;
	case Touch_Tap_Stability:
			temp_value = xiaomi_touch_interfaces.touch_mode[Touch_Tap_Stability][SET_CUR_VALUE];
			nvt_game_value[0] = 0x7A;
			nvt_game_value[1] = temp_value - 1;
			break;
	case Touch_Expert_Mode:
			if (!nvt_value)
				goto end;
			temp_value = xiaomi_touch_interfaces.touch_mode[Touch_Expert_Mode][SET_CUR_VALUE];
			nvt_game_value[0] = 0x70;
			nvt_game_value[1] = ts->touch_expert_array[(temp_value - 1) * 4] - 1;
			nvt_touchfeature_set(nvt_game_value);
			xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][SET_CUR_VALUE] =
						ts->touch_expert_array[(temp_value - 1) * 4];
			xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][GET_CUR_VALUE] =
                                                 xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][SET_CUR_VALUE];

			nvt_game_value[0] = 0x71;
			nvt_game_value[1] = ts->touch_expert_array[(temp_value - 1) * 4 + 1] - 1;
			nvt_touchfeature_set(nvt_game_value);
			xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][SET_CUR_VALUE] =
						ts->touch_expert_array[(temp_value - 1) * 4 + 1];
			xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][GET_CUR_VALUE] =
                                                 xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][SET_CUR_VALUE];

			nvt_game_value[0] = 0x79;
			nvt_game_value[1] = ts->touch_expert_array[(temp_value - 1) * 4 + 2] - 1;
			nvt_touchfeature_set(nvt_game_value);
			xiaomi_touch_interfaces.touch_mode[Touch_Aim_Sensitivity][SET_CUR_VALUE] =
						ts->touch_expert_array[(temp_value - 1) * 4 + 2];
			xiaomi_touch_interfaces.touch_mode[Touch_Aim_Sensitivity][GET_CUR_VALUE] =
                                                 xiaomi_touch_interfaces.touch_mode[Touch_Aim_Sensitivity][SET_CUR_VALUE];

			nvt_game_value[0] = 0x7A;
			nvt_game_value[1] = ts->touch_expert_array[(temp_value - 1) * 4 + 3] - 1;
			xiaomi_touch_interfaces.touch_mode[Touch_Tap_Stability][SET_CUR_VALUE] =
						ts->touch_expert_array[(temp_value - 1) * 4 + 3];
			xiaomi_touch_interfaces.touch_mode[Touch_Tap_Stability][GET_CUR_VALUE] =
                                                 xiaomi_touch_interfaces.touch_mode[Touch_Tap_Stability][SET_CUR_VALUE];
			break;
#else
	case Touch_UP_THRESHOLD:
			temp_value = xiaomi_touch_interfaces.touch_mode[Touch_UP_THRESHOLD][SET_CUR_VALUE];
			nvt_game_value[0] = 0x71;
			nvt_game_value[1] = temp_value - 1;
			break;
	case Touch_Tolerance:
			temp_value = xiaomi_touch_interfaces.touch_mode[Touch_Tolerance][SET_CUR_VALUE];
			nvt_game_value[0] = 0x70;
			nvt_game_value[1] = temp_value - 1;
			break;
#endif
	case Touch_Edge_Filter:
			/* filter 0,1,2,3 = default,1,2,3 level*/
			temp_value = xiaomi_touch_interfaces.touch_mode[Touch_Edge_Filter][SET_CUR_VALUE];
			nvt_game_value[0] = 0x72;
			nvt_game_value[1] = temp_value;
			break;
	case Touch_Panel_Orientation:
			/* 0,1,2,3 = 0, 90, 180,270 Counter-clockwise*/
			temp_value = xiaomi_touch_interfaces.touch_mode[Touch_Panel_Orientation][SET_CUR_VALUE];
			if (temp_value == 0 || temp_value == 2) {
				nvt_game_value[0] = 0xBA;
			} else if (temp_value == 1) {
				nvt_game_value[0] = 0xBC;
			} else if (temp_value == 3) {
				nvt_game_value[0] = 0xBB;
			}
			nvt_game_value[1] = 0;
			break;
	case Touch_Resist_RF:
			temp_value = xiaomi_touch_interfaces.touch_mode[Touch_Resist_RF][SET_CUR_VALUE];
			if (temp_value == 0) {
				nvt_game_value[0] = 0x76;
			} else if (temp_value == 1) {
				nvt_game_value[0] = 0x75;
			}
			nvt_game_value[1] = 0;
			break;
	default:
			/* Don't support */
			skip = true;
			break;
	};

	NVT_LOG("mode:%d, value:%d,temp_value:%d,game value:0x%x,0x%x", nvt_mode, nvt_value, temp_value, nvt_game_value[0], nvt_game_value[1]);

	if (!skip) {
		xiaomi_touch_interfaces.touch_mode[nvt_mode][GET_CUR_VALUE] =
						xiaomi_touch_interfaces.touch_mode[nvt_mode][SET_CUR_VALUE];

		ret = nvt_touchfeature_set(nvt_game_value);
		if (ret < 0) {
			NVT_ERR("change game mode fail");
		}
	} else {
		NVT_LOG("Cmd is not support,skip!");
	}
end:
	return 0;
}

static char nvt_touch_vendor_read(void)
{
	return '4';
}

static u8 nvt_panel_vendor_read(void)
{
	if (ts)
		return ts->lockdown_info[1];
	else
		return 0;
}

static u8 nvt_panel_color_read(void)
{
	if (ts)
		return ts->lockdown_info[2];
	else
		return 0;
}

static u8 nvt_panel_display_read(void)
{
	if (ts)
		return ts->lockdown_info[1];
	else
		return 0;
}

static int nvt_get_mode_value(int mode, int value_type)
{
	int value = -1;

	if (mode < Touch_Mode_NUM && mode >= 0)
		value = xiaomi_touch_interfaces.touch_mode[mode][value_type];
	else
		NVT_ERR("%s, don't support\n", __func__);

	return value;
}

static int nvt_get_mode_all(int mode, int *value)
{
	if (mode < Touch_Mode_NUM && mode >= 0) {
		value[0] = xiaomi_touch_interfaces.touch_mode[mode][GET_CUR_VALUE];
		value[1] = xiaomi_touch_interfaces.touch_mode[mode][GET_DEF_VALUE];
		value[2] = xiaomi_touch_interfaces.touch_mode[mode][GET_MIN_VALUE];
		value[3] = xiaomi_touch_interfaces.touch_mode[mode][GET_MAX_VALUE];
	} else {
		NVT_ERR("%s, don't support\n",  __func__);
	}
	NVT_LOG("%s, mode:%d, value:%d:%d:%d:%d\n", __func__, mode, value[0],
					value[1], value[2], value[3]);

	return 0;
}

static int nvt_reset_mode(int mode)
{
	int i = 0;

	NVT_LOG("nvt_reset_mode enter\n");

	if (mode < Touch_Report_Rate && mode > 0) {
		xiaomi_touch_interfaces.touch_mode[mode][SET_CUR_VALUE] =
			xiaomi_touch_interfaces.touch_mode[mode][GET_DEF_VALUE];
		nvt_set_cur_value(mode, xiaomi_touch_interfaces.touch_mode[mode][SET_CUR_VALUE]);
	} else if (mode == 0) {
		for (i = 0; i <= Touch_Report_Rate; i++) {
			if (i == Touch_Panel_Orientation) {
				xiaomi_touch_interfaces.touch_mode[i][SET_CUR_VALUE] =
				xiaomi_touch_interfaces.touch_mode[i][GET_CUR_VALUE];
			} else {
				xiaomi_touch_interfaces.touch_mode[i][SET_CUR_VALUE] =
				xiaomi_touch_interfaces.touch_mode[i][GET_DEF_VALUE];
			}
			nvt_set_cur_value(i, xiaomi_touch_interfaces.touch_mode[i][SET_CUR_VALUE]);
		}
	} else {
		NVT_ERR("%s, don't support\n",  __func__);
	}

	NVT_LOG("%s, mode:%d\n",  __func__, mode);

	return 0;
}
#endif

#ifdef CONFIG_TOUCHSCREEN_NVT_DEBUG_FS

/*static void tpdbg_shutdown(struct nvt_ts_data *ts_core, bool enable)
{
	mutex_lock(&ts->lock);
	if (enable) {
		if (nvt_write_addr(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD, 0x1A) < 0) {
			NVT_ERR("disable tp sensor failed!");
		}
	} else {
		if (nvt_write_addr(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD, 0x15) < 0) {
			NVT_ERR("enable tp sensor failed!");
		}
	}
	mutex_unlock(&ts->lock);
}*/

static void tpdbg_suspend(struct nvt_ts_data *ts_core, bool enable)
{
	if (enable) {
		uint8_t buf[4] = {0};
		NVT_LOG("start\n");
		mutex_lock(&ts->lock);

		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x12;
		CTP_SPI_WRITE(ts->client, buf, 2);

		mdelay(20);
		mutex_unlock(&ts->lock);

		NVT_LOG("end\n");
	}
	else
		nvt_ts_resume(&ts_core->client->dev);
}

static int tpdbg_open(struct inode *inode, struct file *file)
{
	file->private_data = inode->i_private;

	return 0;
}

static ssize_t tpdbg_read(struct file *file, char __user *buf, size_t size,
			loff_t *ppos)
{
	const char *str = "cmd support as below:\n \
				echo \"irq-disable\" or \"irq-enable\" to ctrl irq\n \
				echo \"tp-sd-en\" or \"tp-sd-off\" to ctrl panel on or off sensor\n \
				echo \"tp-suspend-en\" or \"tp-suspend-off\" to ctrl panel in or off suspend status\n \
				echo \"tp-idle-en\" or \"tp-idle-off\" to ctrl panel in or off doze status\n \
				echo \"tp-active-en\" or \"tp-active-off\" to ctrl panel in or off active status\n \
				echo \"fw-debug-on\" or \"fw-debug-off\" to on or off fw debug function\n";

	loff_t pos = *ppos;
	int len = strlen(str);

	if (pos < 0)
		return -EINVAL;
	if (pos >= len)
		return 0;

	if (copy_to_user(buf, str, len))
		return -EFAULT;

	*ppos = pos + len;

	return len;
}

static ssize_t tpdbg_write(struct file *file, const char __user *buf,
				size_t size, loff_t *ppos)
{
	struct nvt_ts_data *ts_core = file->private_data;
	char *cmd = kzalloc(size + 1, GFP_KERNEL);
	int ret = size;

	if (!cmd)
		return -ENOMEM;

	if (copy_from_user(cmd, buf, size)) {
		ret = -EFAULT;
		goto out;
	}

#if NVT_TOUCH_ESD_PROTECT
	cancel_delayed_work_sync(&nvt_esd_check_work);
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	cmd[size] = '\0';

	if (!strncmp(cmd, "irq-disable", 11))
		nvt_irq_enable(false);
	else if (!strncmp(cmd, "irq-enable", 10))
		nvt_irq_enable(true);
	else if (!strncmp(cmd, "tp-sd-en", 8))
		tpdbg_suspend(ts_core, true);
	else if (!strncmp(cmd, "tp-sd-off", 9))
		tpdbg_suspend(ts_core, false);
	else if (!strncmp(cmd, "tp-suspend-en", 13))
		tpdbg_suspend(ts_core, true);
	else if (!strncmp(cmd, "tp-suspend-off", 14))
		tpdbg_suspend(ts_core, false);
	else if (!strncmp(cmd, "tp-active-en", 12))
		nvt_write_ic_command(WRITE_IC_TP_ACTIVE, true);
	else if (!strncmp(cmd, "tp-active-off", 13))
		nvt_write_ic_command(WRITE_IC_TP_ACTIVE, false);
	else if (!strncmp(cmd, "tp-idle-en", 10))
		nvt_write_ic_command(WRITE_IC_TP_IDLE, true);
	else if (!strncmp(cmd, "tp-idle-off", 11))
		nvt_write_ic_command(WRITE_IC_TP_IDLE, false);
	else if (!strncmp(cmd, "fw-debug-on", 11))
		nvt_set_dbgfw_status(true);
	else if (!strncmp(cmd, "fw-debug-off", 12))
		nvt_set_dbgfw_status(false);
out:
	kfree(cmd);

	return ret;
}

static int tpdbg_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;

	return 0;
}

static ssize_t  nvt_touch_test_write(struct file *file, const char __user *buf,
		size_t count, loff_t *pos){
	int retval = -1;
	uint8_t cmd[8];
	if (copy_from_user(cmd, buf, count)) {
		retval = -EFAULT;
		goto out;
	}
	switch(cmd[0]) {
		case '0':
			ts->debug_flag = 0;
			break;
		case '1':
			ts->debug_flag = 1;
			break;
		case '2':
			ts->debug_flag = 2;
			break;
		default:
			NVT_LOG("%s invalid input cmd, set default value\n", __func__);
			ts->debug_flag = 2;
	}
	NVT_LOG("%s set touch boost debug flag to %d\n", __func__, ts->debug_flag);
	retval = count;
out:
	return retval;
}

static const struct file_operations nvt_touch_test_fops = {
	.owner = THIS_MODULE,
	.write = nvt_touch_test_write,
};

static const struct file_operations tpdbg_ops = {
	.owner = THIS_MODULE,
	.open = tpdbg_open,
	.read = tpdbg_read,
	.write = tpdbg_write,
	.release = tpdbg_release,
};
#endif

static void nvt_suspend_work(struct work_struct *work)
{
	struct nvt_ts_data *ts_core = container_of(work, struct nvt_ts_data, suspend_work);
	nvt_ts_suspend(&ts_core->client->dev);
}


static void nvt_resume_work(struct work_struct *work)
{
	struct nvt_ts_data *ts_core = container_of(work, struct nvt_ts_data, resume_work);
	nvt_ts_resume(&ts_core->client->dev);
}
static void get_lockdown_info(struct work_struct *work)
{
	int ret = 0;

	NVT_LOG("lkdown_readed = %d", ts->lkdown_readed);
	if (!ts->lkdown_readed) {
		ret = get_lockdown_info_for_nvt(ts->lockdown_info);
		if (ret < 0)
			NVT_ERR("can't get lockdown info");
		NVT_LOG("Lockdown:0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x\n",
			ts->lockdown_info[0], ts->lockdown_info[1], ts->lockdown_info[2], ts->lockdown_info[3],
			ts->lockdown_info[4], ts->lockdown_info[5], ts->lockdown_info[6], ts->lockdown_info[7]);
			ts->lkdown_readed = true;
		NVT_LOG("READ LOCKDOWN!!!");
	} else {
		NVT_LOG("use lockdown info that readed before");
		NVT_LOG("Lockdown:0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x\n",
			ts->lockdown_info[0], ts->lockdown_info[1], ts->lockdown_info[2], ts->lockdown_info[3],
			ts->lockdown_info[4], ts->lockdown_info[5], ts->lockdown_info[6], ts->lockdown_info[7]);
	}
}

static int nvt_write_ic_command(int mode, bool enable)
{
	int ret;
	uint8_t  data[3];
	data[0] = EVENT_MAP_HOST_CMD;
	switch(mode) {
	case WRITE_IC_CHARGER_STATE:
		if (enable)
			data[1] = 0x53;
		else 
			data[1] = 0x51;
		data[2] = 0xff;
		break;
	case WRITE_IC_TP_IDLE:
		data[1] = 0x77;
		if (enable)
			data[2] = 0x01;
		else
			data[2] = 0x00;
		break;
	case WRITE_IC_TP_ACTIVE:
		if (enable)
			data[1] = 0x61;
		else
			data[1] = 0x62;
		data[2] = 0xff;
		break;
	default: return -EINVAL;
	}
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);
	if (0xff != data[2])
		ret = CTP_SPI_WRITE(ts->client, data, sizeof(data) / sizeof(data[0]));
	else
		ret = CTP_SPI_WRITE(ts->client, data, (sizeof(data) / sizeof(data[0]) - 1));
	if (ret)
		NVT_LOG("write ic command failed");
	return ret;
}

static int nvt_ts_power_supply_event(struct notifier_block *nb, unsigned long event, void *ptr)
{
	struct nvt_ts_data *ts_data = container_of(nb, struct nvt_ts_data,  power_supply_notifier);
	if (ts_data)
		queue_work(ts_data->ts_workqueue, &ts_data->power_supply_work);
	return 0;
}

static void nvt_ts_power_supply_work(struct work_struct *work)
{
	int ret;
	struct nvt_ts_data *ts_data = container_of(work, struct nvt_ts_data, power_supply_work);
	union power_supply_propval cur_chgr = {0,};

	if (!ts_data->battery_psy) {
		NVT_ERR("battery psy is NULL, something error!!");
		return;
	}
	ret = power_supply_get_property(ts_data->battery_psy, POWER_SUPPLY_PROP_STATUS, &cur_chgr);
	if (ret < 0) {
		NVT_ERR("get psy property failed!!, skip charger mode handler");
		return;
	}

	switch (cur_chgr.intval) {
	case POWER_SUPPLY_STATUS_CHARGING:
		if (!ts_data->charger_mode) {
			if (ts->ic_state ==  NVT_IC_RESUME_OUT)
				ret = nvt_write_ic_command(WRITE_IC_CHARGER_STATE, true);
			ts_data->charger_mode = true;
			NVT_LOG("enter charger mode,ret = %d", ret);
		}
		break;
	case POWER_SUPPLY_STATUS_DISCHARGING:
	case POWER_SUPPLY_STATUS_NOT_CHARGING:
	if (ts_data->charger_mode) {
		if (ts->ic_state ==  NVT_IC_RESUME_OUT)
			ret = nvt_write_ic_command(WRITE_IC_CHARGER_STATE, false);
		ts_data->charger_mode = false;
		NVT_LOG("exit charger mode,ret = %d", ret);
		}
		break;
	default :
		NVT_ERR("unsupport charger state %d", cur_chgr.intval);
		break;
	}
}

/*******************************************************
Description:
	Novatek touchscreen driver probe function.

return:
	Executive outcomes. 0---succeed. negative---failed
*******************************************************/
static int32_t nvt_ts_probe(struct spi_device *client)
{
	int32_t ret = 0;
	struct attribute_group *attrs_p = NULL;

	NVT_LOG("start\n");

	ts = kzalloc(sizeof(struct nvt_ts_data), GFP_KERNEL);
	if (ts == NULL) {
		NVT_ERR("failed to allocated memory for nvt ts data\n");
		return -ENOMEM;
	}
	ts->xbuf = (uint8_t *)kzalloc((NVT_TRANSFER_LEN + 1 + DUMMY_BYTES), GFP_KERNEL);
	if(ts->xbuf == NULL) {
		NVT_ERR("kzalloc for xbuf failed!\n");
		if (ts) {
			kfree(ts);
			ts = NULL;
		}
		return -ENOMEM;
	}

	/* ---parse dts--- */
	ret = nvt_parse_dt(&client->dev);
	if (ret) {
		NVT_ERR("parse dt error\n");
		goto err_spi_setup;
	}
	ts->client = client;
	spi_set_drvdata(ts->client, ts);

	/* ---prepare for spi parameter--- */
	if (ts->client->master->flags & SPI_MASTER_HALF_DUPLEX) {
		NVT_ERR("Full duplex not supported by master\n");
		ret = -EIO;
		goto err_ckeck_full_duplex;
	}
	ts->client->bits_per_word = 8;
	ts->client->mode = SPI_MODE_0;
	ts->client->max_speed_hz = ts->spi_max_freq;
	ts->debug_flag = 2;

#if IS_ENABLED(CONFIG_SPI_MT65XX)
	/* new usage of MTK spi API */
	memcpy(&ts->spi_ctrl, &spi_ctrdata, sizeof(struct mtk_chip_config));
	ts->client->controller_data = (void *)&ts->spi_ctrl;
#endif
	ts->ts_workqueue = create_singlethread_workqueue("nvt_wq");
	if (!ts->ts_workqueue) {
		NVT_ERR("create nvt workqueue fail");
	}

	ts->selftest_wq = create_singlethread_workqueue("selftest_wq");
	if (!ts->selftest_wq) {
		ret = -ENOMEM;
		goto err_create_nvt_selftest_wq;
	}

	ret = spi_setup(ts->client);
	if (ret < 0) {
		NVT_ERR("Failed to perform SPI setup\n");
		goto err_spi_setup;
	}
	NVT_LOG("mode=%d, max_speed_hz=%d\n", ts->client->mode, ts->client->max_speed_hz);

	ret = nvt_pinctrl_init(ts);
	if (!ret && ts->ts_pinctrl) {
		ret = pinctrl_select_state(ts->ts_pinctrl, ts->pinctrl_state_active);
		if (ret < 0) {
			NVT_ERR("Failed to select %s pinstate %d\n",
				PINCTRL_STATE_ACTIVE, ret);
		}
		ret = pinctrl_select_state(ts->ts_pinctrl,ts->pinctrl_state_spimode);
		if (ret < 0) {
			NVT_ERR("Failed to select %s pinstate %d\n",
				"pmx_ts_spimode", ret);
		} else {
			NVT_ERR("success to select spimode");
		}
	} else {
		NVT_ERR("Failed to init pinctrl\n");
	}

	NVT_LOG("Request GPIO\n");
	/* ---request and config GPIOs--- */
	ret = nvt_gpio_config(ts);
	if (ret) {
		NVT_ERR("gpio config error!\n");
		goto err_gpio_config_failed;
	}

	mutex_init(&ts->lock);
	mutex_init(&ts->xbuf_lock);

	/* ---eng reset before TP_RESX high */

	nvt_eng_reset();
#if NVT_TOUCH_SUPPORT_HW_RST
	gpio_set_value(ts->reset_gpio, 1);
#endif
	NVT_LOG("gpio set complete\n");
	/* need 10ms delay after POR(power on reset) */
	msleep(10);

	/* ---check chip version trim--- */
	NVT_LOG("start check chip\n");
	ret = nvt_ts_check_chip_ver_trim(CHIP_VER_TRIM_ADDR);
	if (ret) {
		NVT_LOG("try to check from old chip ver trim address\n");
		ret = nvt_ts_check_chip_ver_trim(CHIP_VER_TRIM_OLD_ADDR);
		if (ret) {
			NVT_ERR("chip is not identified\n");
			ret = -EINVAL;
			goto err_chipvertrim_failed;
		}
	}
	NVT_LOG("finish check chip\n");

	ts->abs_x_max = TOUCH_DEFAULT_MAX_WIDTH;
	ts->abs_y_max = TOUCH_DEFAULT_MAX_HEIGHT;
	ts->input_dev = input_allocate_device();
	if (ts->input_dev == NULL) {
		NVT_ERR("allocate input device failed\n");
		ret = -ENOMEM;
		goto err_input_dev_alloc_failed;
	}

	ts->max_touch_num = TOUCH_MAX_FINGER_NUM;


	ts->int_trigger_type = INT_TRIGGER_TYPE;

	ts->input_dev->evbit[0] = BIT_MASK(EV_SYN) | BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS) ;
	__set_bit(BTN_TOUCH, ts->input_dev->keybit);
	__set_bit(BTN_TOOL_FINGER, ts->input_dev->keybit);
	ts->input_dev->propbit[0] = BIT(INPUT_PROP_DIRECT);

#if MT_PROTOCOL_B
	input_mt_init_slots(ts->input_dev, ts->max_touch_num, 0);
#endif

#if TOUCH_MAX_FINGER_NUM > 1
	/*input_set_abs_params(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);*/
	if (10 == ts->super_resolution_factor) {
        	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max * NVT_SUPER_RESOLUTION_10S - 1, 0, 0);
        	input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max * NVT_SUPER_RESOLUTION_10S - 1, 0, 0);
	} else if (1 == ts->super_resolution_factor) {
		input_set_abs_params(ts->input_dev, ABS_MT_POSITION_X, 0, ts->abs_x_max - 1, 0, 0);
		input_set_abs_params(ts->input_dev, ABS_MT_POSITION_Y, 0, ts->abs_y_max - 1, 0, 0);
	}
#if MT_PROTOCOL_B
	/* no need to set ABS_MT_TRACKING_ID, input_mt_init_slots() already set it */
#else
	input_set_abs_params(ts->input_dev, ABS_MT_TRACKING_ID, 0, ts->max_touch_num, 0, 0);
#endif
#endif

#if WAKEUP_GESTURE
	input_set_capability(ts->input_dev, EV_KEY, KEY_WAKEUP);
#endif

	sprintf(ts->phys, "input/ts");
	ts->input_dev->name = NVT_TS_NAME;
	ts->input_dev->phys = ts->phys;
	ts->input_dev->id.bustype = BUS_SPI;

	input_set_drvdata(ts->input_dev, ts);
	ret = input_register_device(ts->input_dev);
	if (ret) {
		NVT_ERR("register input device (%s) failed. ret=%d\n", ts->input_dev->name, ret);
		goto err_input_register_device_failed;
	}

	ts->client->irq = gpio_to_irq(ts->irq_gpio);
	if (ts->client->irq) {
		NVT_LOG("int_trigger_type=%d\n", ts->int_trigger_type);
		ts->irq_enabled = true;
		ret = request_threaded_irq(ts->client->irq, NULL, nvt_ts_work_func,
				ts->int_trigger_type | IRQF_ONESHOT, NVT_SPI_NAME, ts);
		if (ret != 0) {
			NVT_ERR("request irq failed. ret=%d\n", ret);
			goto err_int_request_failed;
		} else {
			nvt_irq_enable(false);
			NVT_LOG("request irq %d succeed\n", ts->client->irq);
		}
	}

	INIT_WORK(&ts->switch_mode_work, nvt_switch_mode_work);

	pm_stay_awake(&ts->client->dev);
	nvt_lockdown_wq = alloc_workqueue("nvt_lockdown_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!nvt_lockdown_wq) {
		NVT_ERR("nvt_fwu_wq create workqueue failed\n");
		ret = -ENOMEM;
		goto err_create_nvt_lockdown_wq_failed;
	}

	INIT_WORK(&ts->opentest_work, nvt_open_test_work);
	INIT_WORK(&ts->shorttest_work, nvt_short_test_work);
	INIT_DELAYED_WORK(&ts->nvt_lockdown_work, get_lockdown_info);
	init_waitqueue_head(&ts->selftest_wait_queue);
	/* please make sure boot update start after display reset(RESX) sequence*/
	queue_delayed_work(nvt_lockdown_wq, &ts->nvt_lockdown_work, msecs_to_jiffies(1000));

#if WAKEUP_GESTURE
	device_init_wakeup(&ts->input_dev->dev, 1);
#endif
	ts->charger_mode = false;
	ts->ic_state = NVT_IC_INIT;
	ts->dev_pm_suspend = false;
	ts->gesture_command_delayed = -1;
	init_completion(&ts->dev_pm_suspend_completion);

#if BOOT_UPDATE_FIRMWARE
	nvt_fwu_wq = alloc_workqueue("nvt_fwu_wq", WQ_UNBOUND | WQ_MEM_RECLAIM, 1);
	if (!nvt_fwu_wq) {
		NVT_ERR("nvt_fwu_wq create workqueue failed\n");
		ret = -ENOMEM;
		goto err_create_nvt_fwu_wq_failed;
	}
	INIT_DELAYED_WORK(&ts->nvt_fwu_work, Boot_Update_Firmware);
	/* please make sure boot update start after display reset(RESX) sequence */
	queue_delayed_work(nvt_fwu_wq, &ts->nvt_fwu_work, msecs_to_jiffies(10000));
#endif

	NVT_LOG("NVT_TOUCH_ESD_PROTECT is %d\n", NVT_TOUCH_ESD_PROTECT);
#if NVT_TOUCH_ESD_PROTECT
	INIT_DELAYED_WORK(&nvt_esd_check_work, nvt_esd_check_func);
	nvt_esd_check_wq = alloc_workqueue("nvt_esd_check_wq", WQ_MEM_RECLAIM, 1);
	if (!nvt_esd_check_wq) {
		NVT_ERR("nvt_esd_check_wq create workqueue failed\n");
		ret = -ENOMEM;
		goto err_create_nvt_esd_check_wq_failed;
	}
	queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
			msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
#endif /* #if NVT_TOUCH_ESD_PROTECT */

#if NVT_TOUCH_PROC
	ret = nvt_flash_proc_init();
	if (ret != 0) {
		NVT_ERR("nvt flash proc init failed. ret=%d\n", ret);
		goto err_flash_proc_init_failed;
	}
#endif

#if NVT_TOUCH_EXT_PROC
	ret = nvt_extra_proc_init();
	if (ret != 0) {
		NVT_ERR("nvt extra proc init failed. ret=%d\n", ret);
		goto err_extra_proc_init_failed;
	}
#endif

#if NVT_TOUCH_MP
	ret = nvt_mp_proc_init();
	if (ret != 0) {
		NVT_ERR("nvt mp proc init failed. ret=%d\n", ret);
		goto err_mp_proc_init_failed;
	}

#ifndef NVT_SAVE_TESTDATA_IN_FILE
	ret = nvt_test_data_proc_init(ts->client);
	if (ret < 0) {
		NVT_ERR("nvt test data interface init failed. ret=%d\n", ret);
		goto err_mp_proc_init_failed;
	}
#endif

#endif
	attrs_p = (struct attribute_group *)devm_kzalloc(&client->dev, sizeof(*attrs_p), GFP_KERNEL);
	if (!attrs_p) {
		NVT_ERR("no mem to alloc");
		goto err_mp_proc_init_failed;
	}
	ts->attrs = attrs_p;
	attrs_p->name = "panel_info";
	attrs_p->attrs = nvt_panel_attr;
	ret = sysfs_create_group(&client->dev.kobj, ts->attrs);

	ts->event_wq = alloc_workqueue("nvt-event-queue",
		WQ_UNBOUND | WQ_HIGHPRI | WQ_CPU_INTENSIVE, 1);
	if (!ts->event_wq) {
		NVT_ERR("Can not create work thread for suspend/resume!!");
		ret = -ENOMEM;
		goto err_alloc_work_thread_failed;
	}
	INIT_WORK(&ts->resume_work, nvt_resume_work);
	INIT_WORK(&ts->suspend_work, nvt_suspend_work);

#ifdef MI_DRM_NOTIFIER
	ts->drm_notif.notifier_call = nvt_drm_notifier_callback;
	ret = mi_disp_register_client(&ts->drm_notif);
	if(ret) {
		NVT_ERR("register drm_notifier failed. ret=%d\n", ret);
		goto err_register_drm_notif_failed;
	}
#else
	ts->fb_notif.notifier_call = nvt_fb_notifier_callback;
	ret = fb_register_client(&ts->fb_notif);
	if(ret) {
		NVT_ERR("register fb_notifier failed. ret=%d\n", ret);
		goto err_register_fb_notif_failed;
	}
#endif
#if defined(CONFIG_HAS_EARLYSUSPEND)
	ts->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	ts->early_suspend.suspend = nvt_ts_early_suspend;
	ts->early_suspend.resume = nvt_ts_late_resume;
	ret = register_early_suspend(&ts->early_suspend);
	if(ret) {
		NVT_ERR("register early suspend failed. ret=%d\n", ret);
		goto err_register_early_suspend_failed;
	}
#endif
	INIT_WORK(&ts->power_supply_work, nvt_ts_power_supply_work);
	ts->battery_psy = power_supply_get_by_name("battery");
	if (!ts->battery_psy) {
		mdelay(50);
		ts->battery_psy = power_supply_get_by_name("battery");
	}
	if  (!ts->battery_psy)
		NVT_ERR("get battery psy failed, don't register callback for charger mode");
	else {
		ts->power_supply_notifier.notifier_call = nvt_ts_power_supply_event;
		power_supply_reg_notifier(&ts->power_supply_notifier);
		NVT_LOG("register callback for charger mode successful");
	}


#ifdef CONFIG_TOUCHSCREEN_NVT_DEBUG_FS
	ts->debugfs = debugfs_create_dir("tp_debug", NULL);
	if (ts->debugfs) {
		debugfs_create_file("switch_state", 0660, ts->debugfs, ts, &tpdbg_ops);
		debugfs_create_file("touch_boost", 0660, ts->debugfs, ts, &nvt_touch_test_fops);
	}
#endif
#if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)
			xiaomi_touch_interfaces.touch_vendor_read = nvt_touch_vendor_read;
			xiaomi_touch_interfaces.panel_display_read = nvt_panel_display_read;
			xiaomi_touch_interfaces.panel_vendor_read = nvt_panel_vendor_read;
			xiaomi_touch_interfaces.panel_color_read = nvt_panel_color_read;
			xiaomi_touch_interfaces.getModeValue = nvt_get_mode_value;
			xiaomi_touch_interfaces.setModeValue = nvt_set_cur_value;
			xiaomi_touch_interfaces.resetMode = nvt_reset_mode;
			xiaomi_touch_interfaces.getModeAll = nvt_get_mode_all;
			xiaomi_touch_interfaces.palm_sensor_write = nvt_palm_sensor_write;
			nvt_init_touchmode_data();
			xiaomitouch_register_modedata(0, &xiaomi_touch_interfaces);
#endif

	bTouchIsAwake = 1;
	NVT_LOG("end\n");

	nvt_irq_enable(true);

	return 0;

#ifdef MI_DRM_NOTIFIER
	if (mi_disp_unregister_client(&ts->drm_notif))
		NVT_ERR("Error occurred while unregistering drm_notifier.\n");
err_register_drm_notif_failed:
#else
	if (fb_unregister_client(&ts->fb_notif))
		NVT_ERR("Error occurred while unregistering fb_notifier.\n");
err_register_fb_notif_failed:
#endif
#if defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ts->early_suspend);
err_register_early_suspend_failed:
#endif
	destroy_workqueue(ts->event_wq);
err_alloc_work_thread_failed:
#if NVT_TOUCH_MP
nvt_mp_proc_deinit();
err_mp_proc_init_failed:
#endif
#if NVT_TOUCH_EXT_PROC
nvt_extra_proc_deinit();
err_extra_proc_init_failed:
#endif
#if NVT_TOUCH_PROC
nvt_flash_proc_deinit();
err_flash_proc_init_failed:
#endif
sysfs_remove_group(&client->dev.kobj,ts->attrs);
#if NVT_TOUCH_ESD_PROTECT
	if (nvt_esd_check_wq) {
		cancel_delayed_work_sync(&nvt_esd_check_work);
		destroy_workqueue(nvt_esd_check_wq);
		nvt_esd_check_wq = NULL;
	}
err_create_nvt_esd_check_wq_failed:
#endif
#if BOOT_UPDATE_FIRMWARE
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
err_create_nvt_fwu_wq_failed:
	if (nvt_lockdown_wq) {
		cancel_delayed_work_sync(&ts->nvt_lockdown_work);
		destroy_workqueue(nvt_lockdown_wq);
		nvt_lockdown_wq = NULL;
	}
#endif
err_create_nvt_selftest_wq:
err_create_nvt_lockdown_wq_failed:
	pm_relax(&ts->client->dev);
#if WAKEUP_GESTURE
	device_init_wakeup(&ts->input_dev->dev, 0);
#endif
	free_irq(ts->client->irq, ts);
err_int_request_failed:
	input_unregister_device(ts->input_dev);
	ts->input_dev = NULL;
err_input_register_device_failed:
	if (ts->input_dev) {
		input_free_device(ts->input_dev);
		ts->input_dev = NULL;
	}
err_input_dev_alloc_failed:
err_chipvertrim_failed:
	mutex_destroy(&ts->xbuf_lock);
	mutex_destroy(&ts->lock);
	nvt_gpio_deconfig(ts);
err_gpio_config_failed:
err_spi_setup:
	if (ts->ts_workqueue)
		destroy_workqueue(ts->ts_workqueue);
	if (ts->selftest_wq) {
		flush_workqueue(ts->selftest_wq);
		destroy_workqueue(ts->selftest_wq);
		ts->selftest_wq = NULL;
	}
err_ckeck_full_duplex:
	spi_set_drvdata(ts->client, NULL);
	if (ts->xbuf) {
		kfree(ts->xbuf);
		ts->xbuf = NULL;
	}
	if (ts) {
		kfree(ts);
		ts = NULL;
	}
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen driver release function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_ts_remove(struct spi_device *client)
{
	NVT_LOG("Removing driver...\n");

#ifdef MI_DRM_NOTIFIER
	if (mi_disp_unregister_client(&ts->drm_notif))
		NVT_ERR("Error occurred while unregistering drm_notifier.\n");
#else
	if (fb_unregister_client(&ts->fb_notif))
		NVT_ERR("Error occurred while unregistering fb_notifier.\n");
#endif
#if defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ts->early_suspend);
#endif
#ifndef NVT_SAVE_TESTDATA_IN_FILE
	nvt_test_data_proc_deinit();
#endif
#if NVT_TOUCH_MP
	nvt_mp_proc_deinit();
#endif
#if NVT_TOUCH_EXT_PROC
	nvt_extra_proc_deinit();
#endif
#if NVT_TOUCH_PROC
	nvt_flash_proc_deinit();
#endif
sysfs_remove_group(&client->dev.kobj,ts->attrs);
#if NVT_TOUCH_ESD_PROTECT
	if (nvt_esd_check_wq) {
		cancel_delayed_work_sync(&nvt_esd_check_work);
		nvt_esd_check_enable(false);
		destroy_workqueue(nvt_esd_check_wq);
		nvt_esd_check_wq = NULL;
	}
#endif

#if BOOT_UPDATE_FIRMWARE
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
#endif

#if WAKEUP_GESTURE
	device_init_wakeup(&ts->input_dev->dev, 0);
#endif

	nvt_irq_enable(false);
	free_irq(ts->client->irq, ts);

	mutex_destroy(&ts->xbuf_lock);
	mutex_destroy(&ts->lock);

	nvt_gpio_deconfig(ts);

	if (ts->input_dev) {
		input_unregister_device(ts->input_dev);
		ts->input_dev = NULL;
	}

	if (ts->ts_workqueue)
		destroy_workqueue(ts->ts_workqueue);
	if (ts->selftest_wq) {
		flush_workqueue(ts->selftest_wq);
		destroy_workqueue(ts->selftest_wq);
		ts->selftest_wq = NULL;
	}
	spi_set_drvdata(ts->client, NULL);

	if (ts) {
		kfree(ts);
		ts = NULL;
	}

	return 0;
}

static void nvt_ts_shutdown(struct spi_device *client)
{
	NVT_LOG("Shutdown driver...\n");

	nvt_irq_enable(false);

#ifdef MI_DRM_NOTIFIER
	if (mi_disp_unregister_client(&ts->drm_notif))
		NVT_ERR("Error occurred while unregistering drm_notifier.\n");
#else
	if (fb_unregister_client(&ts->fb_notif))
		NVT_ERR("Error occurred while unregistering fb_notifier.\n");
#endif
#if defined(CONFIG_HAS_EARLYSUSPEND)
	unregister_early_suspend(&ts->early_suspend);
#endif
#if NVT_TOUCH_MP
	nvt_mp_proc_deinit();
#endif
#if NVT_TOUCH_EXT_PROC
	nvt_extra_proc_deinit();
#endif
#if NVT_TOUCH_PROC
	nvt_flash_proc_deinit();
#endif

#if NVT_TOUCH_ESD_PROTECT
	if (nvt_esd_check_wq) {
		cancel_delayed_work_sync(&nvt_esd_check_work);
		nvt_esd_check_enable(false);
		destroy_workqueue(nvt_esd_check_wq);
		nvt_esd_check_wq = NULL;
	}
#endif /* #if NVT_TOUCH_ESD_PROTECT */

#if BOOT_UPDATE_FIRMWARE
	if (nvt_fwu_wq) {
		cancel_delayed_work_sync(&ts->nvt_fwu_work);
		destroy_workqueue(nvt_fwu_wq);
		nvt_fwu_wq = NULL;
	}
#endif

#if WAKEUP_GESTURE
	device_init_wakeup(&ts->input_dev->dev, 0);
#endif
}

/*******************************************************
Description:
	Novatek touchscreen driver suspend function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_ts_suspend(struct device *dev)
{
	uint8_t buf[4] = {0};
#if MT_PROTOCOL_B
	uint32_t i = 0;
#endif
	int ret = 0;
	if (!bTouchIsAwake) {
		NVT_LOG("Touch is already suspend\n");
		return 0;
	}
	pm_stay_awake(dev);
	ts->ic_state = NVT_IC_SUSPEND_IN;

	if (!ts->db_wakeup)
		nvt_irq_enable(false);	/*must before hold lock*/

#if NVT_TOUCH_ESD_PROTECT
	NVT_LOG("cancel delayed work sync\n");
	cancel_delayed_work_sync(&nvt_esd_check_work);
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	mutex_lock(&ts->lock);

	NVT_LOG("start\n");

	bTouchIsAwake = 0;

#if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)
	if (ts->palm_sensor_switch) {
		NVT_LOG("palm sensor on status, switch to off\n");
		update_palm_sensor_value(0);
		nvt_set_pocket_palm_switch(false);
	}
#endif
	mdelay(10);
	if (ts->db_wakeup) {
		/* ---write command to enter "wakeup gesture mode"--- */
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x13;
		CTP_SPI_WRITE(ts->client, buf, 2);

		enable_irq_wake(ts->client->irq);

		NVT_LOG("Enabled touch wakeup gesture\n");

	} else {
		/* ---write command to enter "deep sleep mode"--- */
		buf[0] = EVENT_MAP_HOST_CMD;
		buf[1] = 0x11;
		CTP_SPI_WRITE(ts->client, buf, 2);
		if (ts->ts_pinctrl) {
			ret = pinctrl_select_state(ts->ts_pinctrl, ts->pinctrl_state_suspend);

			if (ret < 0) {
				NVT_ERR("Failed to select %s pinstate %d\n",
					PINCTRL_STATE_SUSPEND, ret);
			}
		} else {
			NVT_ERR("Failed to init pinctrl\n");
		}
	}
	mdelay(10);
	mutex_unlock(&ts->lock);
	/* release all touches */
#if MT_PROTOCOL_B
	for (i = 0; i < ts->max_touch_num; i++) {
		input_mt_slot(ts->input_dev, i);
		/*input_report_abs(ts->input_dev, ABS_MT_TOUCH_MAJOR, 0);*/
		input_report_abs(ts->input_dev, ABS_MT_PRESSURE, 0);
		input_mt_report_slot_state(ts->input_dev, MT_TOOL_FINGER, 0);
#if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)
		last_touch_events_collect(i, 0);
#endif
	}
#endif
	input_report_key(ts->input_dev, BTN_TOOL_FINGER, 0);
	input_report_key(ts->input_dev, BTN_TOUCH, 0);
#if !MT_PROTOCOL_B
	input_mt_sync(ts->input_dev);
#endif
	input_sync(ts->input_dev);

	msleep(50);
	if (likely(ts->ic_state == NVT_IC_SUSPEND_IN))
		ts->ic_state = NVT_IC_SUSPEND_OUT;
	else
		NVT_ERR("IC state may error,caused by suspend/resume flow, please CHECK!!");
	NVT_LOG("end\n");
	pm_relax(dev);
	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen driver resume function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_ts_resume(struct device *dev)
{
	int ret;
	if (ts->dev_pm_suspend)
		pm_stay_awake(dev);
	if (!ts->db_wakeup) {
		if (ts->ts_pinctrl) {
			ret = pinctrl_select_state(ts->ts_pinctrl, ts->pinctrl_state_active);

			if (ret < 0) {
				NVT_ERR("Failed to select %s pinstate %d\n",
					PINCTRL_STATE_ACTIVE, ret);
			}
		} else {
			NVT_ERR("Failed to init pinctrl\n");
		}
	}
	if (bTouchIsAwake) {
		NVT_LOG("Touch is already resume\n");
#if NVT_TOUCH_WDT_RECOVERY
		mutex_lock(&ts->lock);
		if (nvt_get_dbgfw_status()) {
			ret = nvt_update_firmware(DEFAULT_DEBUG_FW_NAME);
		} else {
			ret = nvt_update_firmware(ts->fw_name);
		}
		mutex_unlock(&ts->lock);
#endif /* #if NVT_TOUCH_WDT_RECOVERY */
		goto Exit;
	}

	ts->ic_state = NVT_IC_RESUME_IN;

	mutex_lock(&ts->lock);
	NVT_LOG("start\n");

	/* please make sure display reset(RESX) sequence and mipi dsi cmds sent before this */
#if NVT_TOUCH_SUPPORT_HW_RST
	gpio_set_value(ts->reset_gpio, 1);
#endif
	if (nvt_get_dbgfw_status()) {
		ret = nvt_update_firmware(DEFAULT_DEBUG_FW_NAME);
		if (ret < 0) {
			NVT_ERR("use built-in fw");
			ret = nvt_update_firmware(ts->fw_name);
		}
	} else {
		ret = nvt_update_firmware(ts->fw_name);
	}
	if (ret)
		NVT_ERR("download firmware failed\n");
	nvt_check_fw_reset_state(RESET_STATE_REK);


	nvt_irq_enable(true);

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
	queue_delayed_work(nvt_esd_check_wq, &nvt_esd_check_work,
			msecs_to_jiffies(NVT_TOUCH_ESD_CHECK_PERIOD));
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	bTouchIsAwake = 1;
#if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)
	NVT_LOG("palm_sensor_switch=%d", ts->palm_sensor_switch);
	if (ts->palm_sensor_switch) {
		NVT_LOG("palm sensor on status, switch to on\n");
		nvt_set_pocket_palm_switch(true);
	}
#endif
	mutex_unlock(&ts->lock);
	tp_enable_doubleclick(!!ts->db_wakeup);/*if true, dbclick work until next suspend*/
	if (likely(ts->ic_state == NVT_IC_RESUME_IN))
		ts->ic_state = NVT_IC_RESUME_OUT;
	else
		NVT_ERR("IC state may error,caused by suspend/resume flow, please CHECK!!");

	if (ts->gesture_command_delayed >= 0){
		ts->db_wakeup = ts->gesture_command_delayed;
		ts->gesture_command_delayed = -1;
		NVT_LOG("execute delayed command, set double click wakeup %d\n", ts->db_wakeup);
		tp_enable_doubleclick(!!ts->db_wakeup);
	}
	ret = nvt_write_ic_command(WRITE_IC_CHARGER_STATE, ts->charger_mode);
	NVT_LOG("write charger mode :%d at resume, result:%d\n", ts->charger_mode, ret);
Exit:
	if (ts->dev_pm_suspend)
		pm_relax(dev);
	NVT_LOG("end\n");

	return 0;
}


#ifdef MI_DRM_NOTIFIER
static int nvt_drm_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct mi_disp_notifier *evdata = data;
	int *blank;
	struct nvt_ts_data *ts_data=
		container_of(self, struct nvt_ts_data, drm_notif);

	if (!evdata || (evdata->disp_id != 0))
		return 0;

	if (evdata->data && ts_data) {
		blank = evdata->data;
		if (event == MI_DISP_DPMS_EARLY_EVENT) {
			if (*blank == MI_DISP_DPMS_POWERDOWN) {
				NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
				flush_workqueue(ts_data->event_wq);
				nvt_ts_suspend(&ts_data->client->dev);
			}
		} else if (event == MI_DISP_DPMS_EVENT) {
			if (*blank == MI_DISP_DPMS_ON) {
				NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
				flush_workqueue(ts_data->event_wq);
				queue_work(ts_data->event_wq, &ts_data->resume_work);
			}
		}
	}
	return 0;
}
#else
static int nvt_fb_notifier_callback(struct notifier_block *self, unsigned long event, void *data)
{
	struct fb_event *evdata = data;
	int *blank;
	struct nvt_ts_data *ts =
		container_of(self, struct nvt_ts_data, fb_notif);

	if (evdata && evdata->data && event == FB_EARLY_EVENT_BLANK) {
		blank = evdata->data;
		if (*blank == FB_BLANK_POWERDOWN) {
			NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
			flush_workqueue(ts->event_wq);
			nvt_ts_suspend(&ts->client->dev);
		}
	} else if (evdata && evdata->data && event == FB_EVENT_BLANK) {
		blank = evdata->data;
		if (*blank == FB_BLANK_UNBLANK) {
			NVT_LOG("event=%lu, *blank=%d\n", event, *blank);
			flush_workqueue(ts->event_wq);
			queue_work(ts->event_wq, &ts->resume_work);
		}
	}

	return 0;
}
#endif

static int nvt_pm_suspend(struct device *dev)
{
	if (device_may_wakeup(dev) && ts->db_wakeup) {
		NVT_LOG("enable touch irq wake\n");
		enable_irq_wake(ts->client->irq);
	}
	ts->dev_pm_suspend = true;
	reinit_completion(&ts->dev_pm_suspend_completion);

	return 0;
}

static int nvt_pm_resume(struct device *dev)
{
	if (device_may_wakeup(dev) && ts->db_wakeup) {
		NVT_LOG("disable touch irq wake\n");
		disable_irq_wake(ts->client->irq);
	}
	ts->dev_pm_suspend = false;
	complete(&ts->dev_pm_suspend_completion);

	return 0;
}

static const struct dev_pm_ops nvt_dev_pm_ops = {
	.suspend = nvt_pm_suspend,
	.resume = nvt_pm_resume,
};

#if defined(CONFIG_HAS_EARLYSUSPEND)
/*******************************************************
Description:
	Novatek touchscreen driver early suspend function.

return:
	n.a.
*******************************************************/
static void nvt_ts_early_suspend(struct early_suspend *h)
{
	nvt_ts_suspend(ts->client, PMSG_SUSPEND);
}

/*******************************************************
Description:
	Novatek touchscreen driver late resume function.

return:
	n.a.
*******************************************************/
static void nvt_ts_late_resume(struct early_suspend *h)
{
	nvt_ts_resume(ts->client);
}
#endif

static const struct spi_device_id nvt_ts_id[] = {
	{ NVT_SPI_NAME, 0 },
	{ }
};


static struct of_device_id nvt_match_table[] = {
	{ .compatible = "novatek,NVT-ts-spi",},
	{ },
};


static struct spi_driver nvt_spi_driver = {
	.probe		= nvt_ts_probe,
	.remove		= nvt_ts_remove,
	.shutdown	= nvt_ts_shutdown,
	.id_table	= nvt_ts_id,
	.driver = {
		.name	= NVT_SPI_NAME,
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
		.pm = &nvt_dev_pm_ops,
#endif
		.of_match_table = nvt_match_table,
	},
};

static bool nvt_off_charger_mode(void)
{
	bool charger_mode = false;
	char charger_node[8] = {'\0'};
	char *chose = (char *) strnstr(saved_cmdline,
				"androidboot.mode=", strlen(saved_cmdline));
	if (chose) {
		memcpy(charger_node, (chose + strlen("androidboot.mode=")),
			sizeof(charger_node) - 1);
		NVT_LOG("%s: charger_node is %s\n", __func__, charger_node);
		if (!strncmp(charger_node, "charger", strlen("charger"))) {
			charger_mode = true;
		}
	}
	return charger_mode;
}

/*******************************************************
Description:
	Driver Install function.

return:
	Executive Outcomes. 0---succeed. not 0---failed.
********************************************************/
static int32_t __init nvt_driver_init(void)
{
	int32_t ret = 0;

	NVT_LOG("start\n");
	if (nvt_off_charger_mode()) {
		NVT_LOG("off_charger states, %s exit", __func__);
		return 0;
	}

	/* ---add spi driver--- */
	ret = spi_register_driver(&nvt_spi_driver);
	if (ret) {
		NVT_ERR("failed to add spi driver");
		goto err_driver;
	}

	NVT_LOG("finished\n");

err_driver:
	return ret;
}

/*******************************************************
Description:
	Driver uninstall function.

return:
	n.a.
********************************************************/
static void __exit nvt_driver_exit(void)
{
	spi_unregister_driver(&nvt_spi_driver);
}
late_initcall(nvt_driver_init);
module_exit(nvt_driver_exit);

MODULE_DESCRIPTION("Novatek Touchscreen Driver");
MODULE_LICENSE("GPL");
