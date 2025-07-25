/*
 * Copyright (C) 2010 - 2018 Novatek, Inc.
 *
 * Copyright (C) 2023 XiaoMi, Inc.
 *
 * $Revision: 52752 $
 * $Date: 2019-11-06 18:05:46 +0800 (週三, 06 十一月 2019) $
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
#include <linux/delay.h>
#include "nt36xxx.h"

/*
*#if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)
*#include "../xiaomi/xiaomi_touch.h"
*#endif
*/
extern int update_palm_sensor_value(int value);

#if NVT_TOUCH_EXT_PROC
#define NVT_FW_VERSION				"nvt_fw_version"
#define NVT_BASELINE				"nvt_baseline"
#define NVT_RAW						"nvt_raw"
#define NVT_DIFF					"nvt_diff"
#define NVT_XIAOMI_CONFIG_INFO		"nvt_xiaomi_config_info"
#define NVT_XIAOMI_LOCKDOWN_INFO	"tp_lockdown_info"

#define SPI_TANSFER_LENGTH		256

#define NORMAL_MODE				0x00
#define TEST_MODE_1				0x21
#define TEST_MODE_2				0x22
#define HANDSHAKING_HOST_READY	0xBB

static uint8_t xdata_tmp[2048] = {0};
static int32_t xdata[2048] = {0};

static struct proc_dir_entry *NVT_proc_fw_version_entry;
static struct proc_dir_entry *NVT_proc_baseline_entry;
static struct proc_dir_entry *NVT_proc_raw_entry;
static struct proc_dir_entry *NVT_proc_diff_entry;
static struct proc_dir_entry *NVT_proc_xiaomi_config_info_entry;
static struct proc_dir_entry *NVT_proc_xiaomi_lockdown_info_entry;


/* Xiaomi Config Info */
static uint8_t nvt_xiaomi_conf_info_fw_ver = 0;
static uint8_t nvt_xiaomi_conf_info_fae_id = 0;
static uint64_t nvt_xiaomi_conf_info_reservation = 0;

/*******************************************************
Description:
	Novatek touchscreen change mode function.

return:
	n.a.
*******************************************************/
void nvt_change_mode(uint8_t mode)
{
	uint8_t buf[8] = {0};

	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

	/* ---set mode--- */
	buf[0] = EVENT_MAP_HOST_CMD;
	buf[1] = mode;
	CTP_SPI_WRITE(ts->client, buf, 2);

	if (mode == NORMAL_MODE) {
		buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
		buf[1] = HANDSHAKING_HOST_READY;
		CTP_SPI_WRITE(ts->client, buf, 2);
		msleep(20);
	}
}

/*******************************************************
Description:
	Novatek touchscreen get firmware pipe function.

return:
	Executive outcomes. 0---pipe 0. 1---pipe 1.
*******************************************************/
uint8_t nvt_get_fw_pipe(void)
{
	uint8_t buf[8]= {0};

	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE);

	/* ---read fw status--- */
	buf[0] = EVENT_MAP_HANDSHAKING_or_SUB_CMD_BYTE;
	buf[1] = 0x00;
	CTP_SPI_READ(ts->client, buf, 2);
	return (buf[1] & 0x01);
}

/*******************************************************
Description:
	Novatek touchscreen read meta data function.

return:
	n.a.
*******************************************************/
void nvt_read_mdata(uint32_t xdata_addr, uint32_t xdata_btn_addr)
{
	int32_t i = 0;
	int32_t j = 0;
	int32_t k = 0;
	uint8_t buf[SPI_TANSFER_LENGTH + 2] = {0};
	uint32_t head_addr = 0;
	int32_t dummy_len = 0;
	int32_t data_len = 0;
	int32_t residual_len = 0;

	/* ---set xdata sector address & length--- */
	head_addr = xdata_addr - (xdata_addr % XDATA_SECTOR_SIZE);
	dummy_len = xdata_addr - head_addr;
	data_len = ts->x_num * ts->y_num * 2;
	residual_len = (head_addr + dummy_len + data_len) % XDATA_SECTOR_SIZE;

	/* printk("head_addr=0x%05X, dummy_len=0x%05X, data_len=0x%05X, residual_len=0x%05X\n", head_addr, dummy_len, data_len, residual_len); */

	/* read xdata : step 1 */
	for (i = 0; i < ((dummy_len + data_len) / XDATA_SECTOR_SIZE); i++) {
		/* ---read xdata by SPI_TANSFER_LENGTH */
		for (j = 0; j < (XDATA_SECTOR_SIZE / SPI_TANSFER_LENGTH); j++) {
			/* ---change xdata index--- */
			nvt_set_page(head_addr + (XDATA_SECTOR_SIZE * i) + (SPI_TANSFER_LENGTH * j));

			/* ---read data--- */
			buf[0] = SPI_TANSFER_LENGTH * j;
			CTP_SPI_READ(ts->client, buf, SPI_TANSFER_LENGTH + 1);

			/* ---copy buf to xdata_tmp--- */
			for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
				xdata_tmp[XDATA_SECTOR_SIZE * i + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
				/* printk("0x%02X, 0x%04X\n", buf[k+1], (XDATA_SECTOR_SIZE*i + SPI_TANSFER_LENGTH*j + k)); */
			}
		}
		/* printk("addr=0x%05X\n", (head_addr+XDATA_SECTOR_SIZE*i)); */
	}

	/* read xdata : step2 */
	if (residual_len != 0) {
		/* ---read xdata by SPI_TANSFER_LENGTH */
		for (j = 0; j < (residual_len / SPI_TANSFER_LENGTH + 1); j++) {
			/* ---change xdata index--- */
			nvt_set_page(xdata_addr + data_len - residual_len + (SPI_TANSFER_LENGTH * j));

			/* ---read data--- */
			buf[0] = SPI_TANSFER_LENGTH * j;
			CTP_SPI_READ(ts->client, buf, SPI_TANSFER_LENGTH + 1);

			/* ---copy buf to xdata_tmp--- */
			for (k = 0; k < SPI_TANSFER_LENGTH; k++) {
				xdata_tmp[(dummy_len + data_len - residual_len) + SPI_TANSFER_LENGTH * j + k] = buf[k + 1];
				/* printk("0x%02X, 0x%04x\n", buf[k+1], ((dummy_len+data_len-residual_len) + SPI_TANSFER_LENGTH*j + k)); */
			}
		}
		/* printk("addr=0x%05X\n", (xdata_addr+data_len-residual_len)); */
	}

	/* ---remove dummy data and 2bytes-to-1data--- */
	for (i = 0; i < (data_len / 2); i++) {
		xdata[i] = (int16_t)(xdata_tmp[dummy_len + i * 2] + 256 * xdata_tmp[dummy_len + i * 2 + 1]);
	}


	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR);
}

/*******************************************************
Description:
	Novatek touchscreen get meta data function.

return:
	n.a.
*******************************************************/
void nvt_get_mdata(int32_t *buf, uint8_t *m_x_num, uint8_t *m_y_num)
{
	*m_x_num = ts->x_num;
	*m_y_num = ts->y_num;
	memcpy(buf, xdata, ((ts->x_num * ts->y_num + TOUCH_KEY_NUM) * sizeof(int32_t)));
}

/*******************************************************
Description:
	Novatek touchscreen firmware version show function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t c_fw_version_show(struct seq_file *m, void *v)
{
	seq_printf(m, "fw_ver=%d, x_num=%d, y_num=%d, button_num=%d\n", ts->fw_ver, ts->x_num, ts->y_num, ts->max_button_num);
	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen xdata sequence print show
	function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t c_show(struct seq_file *m, void *v)
{
	int32_t i = 0;
	int32_t j = 0;
	for (i = 0; i < ts->y_num; i++) {
		for (j = 0; j < ts->x_num; j++) {
			seq_printf(m, "%5d, ", xdata[i * ts->x_num + j]);
		}
		seq_puts(m, "\n");
	}


	seq_printf(m, "\n\n");
	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen xdata sequence print start
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
	Novatek touchscreen xdata sequence print next
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
	Novatek touchscreen xdata sequence print stop
	function.

return:
	n.a.
*******************************************************/
static void c_stop(struct seq_file *m, void *v)
{
	return;
}

const struct seq_operations nvt_fw_version_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_fw_version_show
};

const struct seq_operations nvt_seq_ops = {
	.start  = c_start,
	.next   = c_next,
	.stop   = c_stop,
	.show   = c_show
};

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_fw_version open
	function.

return:
	n.a.
*******************************************************/
static int32_t nvt_fw_version_open(struct inode *inode, struct file *file)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("%s ++\n", __func__);

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	mutex_unlock(&ts->lock);

	NVT_LOG("%s --\n", __func__);

	return seq_open(file, &nvt_fw_version_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops nvt_fw_version_fops = {
	.proc_open =  nvt_fw_version_open,
	.proc_read =  seq_read, 
	.proc_lseek = seq_lseek,
	.proc_release = seq_release, 
};
#else
static const struct file_operations nvt_fw_version_fops = {
	.owner = THIS_MODULE,
	.open = nvt_fw_version_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_baseline open function.

return:
Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_baseline_open(struct inode *inode, struct file *file)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("%s ++\n", __func__);

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_read_mdata(ts->mmap->BASELINE_ADDR, ts->mmap->BASELINE_BTN_ADDR);

	nvt_change_mode(NORMAL_MODE);

	mutex_unlock(&ts->lock);

	NVT_LOG("%s --\n", __func__);

	return seq_open(file, &nvt_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const  struct proc_ops  nvt_baseline_fops= {
        .proc_open =  nvt_baseline_open,
        .proc_read =  seq_read,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
};
#else
static const struct file_operations nvt_baseline_fops = {
	.owner = THIS_MODULE,
	.open = nvt_baseline_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_raw open function.

return:
	Executive outcomes. 0---succeed.
*******************************************************/
static int32_t nvt_raw_open(struct inode *inode, struct file *file)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("%s ++\n", __func__);

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0)
		nvt_read_mdata(ts->mmap->RAW_PIPE0_ADDR, ts->mmap->RAW_BTN_PIPE0_ADDR);
	else
		nvt_read_mdata(ts->mmap->RAW_PIPE1_ADDR, ts->mmap->RAW_BTN_PIPE1_ADDR);

	nvt_change_mode(NORMAL_MODE);

	mutex_unlock(&ts->lock);

	NVT_LOG("%s --\n", __func__);

	return seq_open(file, &nvt_seq_ops);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  nvt_raw_fops= {
        .proc_open =  nvt_raw_open,
        .proc_read =  seq_read,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
};
#else
static const struct file_operations nvt_raw_fops = {
	.owner = THIS_MODULE,
	.open = nvt_raw_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

/*******************************************************
Description:
	Novatek touchscreen /proc/nvt_diff open function.

return:
	Executive outcomes. 0---succeed. negative---failed.
*******************************************************/
static int32_t nvt_diff_open(struct inode *inode, struct file *file)
{
	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("%s ++\n", __func__);

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	if (nvt_clear_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	nvt_change_mode(TEST_MODE_2);

	if (nvt_check_fw_status()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_info()) {
		mutex_unlock(&ts->lock);
		return -EAGAIN;
	}

	if (nvt_get_fw_pipe() == 0)
		nvt_read_mdata(ts->mmap->DIFF_PIPE0_ADDR, ts->mmap->DIFF_BTN_PIPE0_ADDR);
	else
		nvt_read_mdata(ts->mmap->DIFF_PIPE1_ADDR, ts->mmap->DIFF_BTN_PIPE1_ADDR);

	nvt_change_mode(NORMAL_MODE);

	mutex_unlock(&ts->lock);

	NVT_LOG("%s --\n", __func__);

	return seq_open(file, &nvt_seq_ops);
}
#ifdef HAVE_PROC_OPS                                                            
static const  struct proc_ops  nvt_diff_fops= {                                   
        .proc_open =  nvt_diff_open,                                              
        .proc_read =  seq_read,                                                  
        .proc_lseek = seq_lseek,                                                 
        .proc_release = seq_release,                                             
};                                                                              
#else  
static const struct file_operations nvt_diff_fops = {
	.owner = THIS_MODULE,
	.open = nvt_diff_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};
#endif

static int nvt_xiaomi_config_info_show(struct seq_file *m, void *v)
{
	seq_printf(m, "FW version/Config version, Debug version: 0x%02X\n", nvt_xiaomi_conf_info_fw_ver);
	seq_printf(m, "FAE ID: 0x%02X\n", nvt_xiaomi_conf_info_fae_id);
	seq_printf(m, "Reservation byte: 0x%012llX\n", nvt_xiaomi_conf_info_reservation);

	return 0;
}

static int32_t nvt_xiaomi_config_info_open(struct inode *inode, struct file *file)
{
	uint8_t buf[16] = {0};

	if (mutex_lock_interruptible(&ts->lock)) {
		return -ERESTARTSYS;
	}

	NVT_LOG("%s ++\n", __func__);

#if NVT_TOUCH_ESD_PROTECT
	nvt_esd_check_enable(false);
#endif /* #if NVT_TOUCH_ESD_PROTECT */

	/* ---set xdata index to EVENT BUF ADDR--- */
	nvt_set_page(ts->mmap->EVENT_BUF_ADDR | 0x9C);

	buf[0] = 0x9C;
	CTP_SPI_READ(ts->client, buf, 9);

	nvt_xiaomi_conf_info_fw_ver = buf[1];
	nvt_xiaomi_conf_info_fae_id = buf[2];
	nvt_xiaomi_conf_info_reservation = (((uint64_t)buf[3] << 40) | ((uint64_t)buf[4] << 32) | ((uint64_t)buf[5] << 24) | ((uint64_t)buf[6] << 16) | ((uint64_t)buf[7] << 8) | (uint64_t)buf[8]);

	mutex_unlock(&ts->lock);

	NVT_LOG("%s --\n", __func__);

	return single_open(file, nvt_xiaomi_config_info_show, NULL);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  nvt_xiaomi_config_info_fops= {
        .proc_open =  nvt_xiaomi_config_info_open,
        .proc_read =  seq_read,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
};
#else
static const struct file_operations nvt_xiaomi_config_info_fops = {
	.owner = THIS_MODULE,
	.open = nvt_xiaomi_config_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif

static int nvt_xiaomi_lockdown_info_show(struct seq_file *m, void *v)
{
	u8 *lk = ts->lockdown_info;

	seq_printf(m, "0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x,0x%02x\n",
					lk[0], lk[1], lk[2], lk[3], lk[4], lk[5], lk[6], lk[7]);
	return 0;
}



static int32_t nvt_xiaomi_lockdown_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, nvt_xiaomi_lockdown_info_show, NULL);
}

#ifdef HAVE_PROC_OPS
static const struct proc_ops  nvt_xiaomi_lockdown_info_fops= {
        .proc_open =  nvt_xiaomi_lockdown_info_open,
        .proc_read =  seq_read,
        .proc_lseek = seq_lseek,
        .proc_release = seq_release,
};
#else
static const struct file_operations nvt_xiaomi_lockdown_info_fops = {
	.owner = THIS_MODULE,
	.open = nvt_xiaomi_lockdown_info_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};
#endif
int32_t nvt_set_pocket_palm_switch(uint8_t pocket_palm_switch)
{
	uint8_t buf[8] = {0};
	int32_t ret = 0;

	NVT_LOG("set pocket palm switch: %d\n", pocket_palm_switch);

	msleep(35);

	/* ---set xdata index to EVENT BUF ADDR--- */
	ret = nvt_set_page(ts->mmap->EVENT_BUF_ADDR | EVENT_MAP_HOST_CMD);

	if (ret < 0) {
		NVT_ERR("Set event buffer index fail!\n");
		goto nvt_set_pocket_palm_switch_out;
	}

	buf[0] = EVENT_MAP_HOST_CMD;
	if (pocket_palm_switch == 0) {
		/* pocket palm disable */
		buf[1] = 0x74;
        #if IS_ENABLED(CONFIG_TOUCHSCREEN_XIAOMI_TOUCHFEATURE)
		update_palm_sensor_value(0);
        #endif
	} else if (pocket_palm_switch == 1) {
		/* pocket palm enable */
		buf[1] = 0x73;
	} else {
		NVT_ERR("Invalid value! pocket_palm_switch = %d\n", pocket_palm_switch);
		ret = -EINVAL;
		goto nvt_set_pocket_palm_switch_out;
	}
	ret = CTP_SPI_WRITE(ts->client, buf, 2);
	if (ret < 0) {
		NVT_ERR("Write pocket palm switch command fail!\n");
		goto nvt_set_pocket_palm_switch_out;
	}

nvt_set_pocket_palm_switch_out:
	NVT_LOG("%s --\n", __func__);
	return ret;
}

/*******************************************************
Description:
	Novatek touchscreen extra function proc. file node
	initial function.

return:
	Executive outcomes. 0---succeed. -12---failed.
*******************************************************/
int32_t nvt_extra_proc_init(void)
{
	NVT_proc_fw_version_entry = proc_create(NVT_FW_VERSION, 0444, NULL,&nvt_fw_version_fops);
	if (NVT_proc_fw_version_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_FW_VERSION);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_FW_VERSION);
	}

	NVT_proc_baseline_entry = proc_create(NVT_BASELINE, 0444, NULL,&nvt_baseline_fops);
	if (NVT_proc_baseline_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_BASELINE);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_BASELINE);
	}

	NVT_proc_raw_entry = proc_create(NVT_RAW, 0444, NULL,&nvt_raw_fops);
	if (NVT_proc_raw_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_RAW);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_RAW);
	}

	NVT_proc_diff_entry = proc_create(NVT_DIFF, 0444, NULL,&nvt_diff_fops);
	if (NVT_proc_diff_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_DIFF);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_DIFF);
	}

	NVT_proc_xiaomi_config_info_entry = proc_create(NVT_XIAOMI_CONFIG_INFO, 0444, NULL, &nvt_xiaomi_config_info_fops);
	if (NVT_proc_xiaomi_config_info_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_XIAOMI_CONFIG_INFO);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_XIAOMI_CONFIG_INFO);
	}

	NVT_proc_xiaomi_lockdown_info_entry = proc_create(NVT_XIAOMI_LOCKDOWN_INFO, 0444, NULL, &nvt_xiaomi_lockdown_info_fops);
	if (NVT_proc_xiaomi_lockdown_info_entry == NULL) {
		NVT_ERR("create proc/%s Failed!\n", NVT_XIAOMI_LOCKDOWN_INFO);
		return -ENOMEM;
	} else {
		NVT_LOG("create proc/%s Succeeded!\n", NVT_XIAOMI_LOCKDOWN_INFO);
	}

	return 0;
}

/*******************************************************
Description:
	Novatek touchscreen extra function proc. file node
	deinitial function.

return:
	n.a.
*******************************************************/
void nvt_extra_proc_deinit(void)
{
	if (NVT_proc_fw_version_entry != NULL) {
		remove_proc_entry(NVT_FW_VERSION, NULL);
		NVT_proc_fw_version_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_FW_VERSION);
	}

	if (NVT_proc_baseline_entry != NULL) {
		remove_proc_entry(NVT_BASELINE, NULL);
		NVT_proc_baseline_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_BASELINE);
	}

	if (NVT_proc_raw_entry != NULL) {
		remove_proc_entry(NVT_RAW, NULL);
		NVT_proc_raw_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_RAW);
	}

	if (NVT_proc_diff_entry != NULL) {
		remove_proc_entry(NVT_DIFF, NULL);
		NVT_proc_diff_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_DIFF);
	}

	if (NVT_proc_xiaomi_config_info_entry != NULL) {
		remove_proc_entry(NVT_XIAOMI_CONFIG_INFO, NULL);
		NVT_proc_xiaomi_config_info_entry = NULL;
		NVT_LOG("Removed /proc/%s\n", NVT_XIAOMI_CONFIG_INFO);
	}
}
#endif
