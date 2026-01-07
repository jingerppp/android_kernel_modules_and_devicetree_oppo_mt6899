// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * leds-aw22xxx.c   aw22xxx led module
 *
 * Copyright (c) 2017 Shanghai Awinic Technology Co., Ltd. All Rights Reserved
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#define AW22XXX_TAG "<aw22xxx> "
#define pr_fmt(fmt) "%s "fmt, AW22XXX_TAG

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/debugfs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/leds.h>
#include <linux/printk.h>

#include "leds-aw22xxx.h"
#include "leds-aw22xxx-reg.h"

#ifdef OPLUS_FEATURE_MISC_BASIC
#include <linux/regulator/consumer.h>
#endif
/******************************************************
 *
 * Marco
 *
 ******************************************************/
#define AW22XXX_I2C_NAME "aw22xxx_led"

#define AW22XXX_DRIVER_VERSION "v1.7.0"

#define AW_I2C_RETRIES 2
#define AW_I2C_RETRY_DELAY 1
#define AW_READ_CHIPID_RETRIES 2
#define AW_READ_CHIPID_RETRY_DELAY 1

#define FW_ACTION_HOTPLUG 1

/******************************************************
 *
 * aw22xxx led parameter
 *
 ******************************************************/
#define AW22XXX_CFG_NAME_MAX        64
static char *aw22xxx_fw_name = "aw22xxx_fw.bin";
static char aw22xxx_cfg_name[][AW22XXX_CFG_NAME_MAX] = {
	{"aw22xxx_cfg_led_off.bin"},
	{"aw22xxx_cfg_led_on.bin"},
	{"aw22xxx_cfg_led_breath.bin"},
	{"aw22xxx_cfg_led_blink.bin"},
	{"aw22xxx_cfg_led_skyline.bin"},
	{"aw22xxx_cfg_led_flower.bin"},
	{"aw22xxx_cfg_audio_skyline.bin"},
	{"aw22xxx_cfg_audio_flower.bin"},
};

#define AW22XXX_IMAX_NAME_MAX       32
static char aw22xxx_imax_name[][AW22XXX_IMAX_NAME_MAX] = {
	{"AW22XXX_IMAX_2mA"},
	{"AW22XXX_IMAX_3mA"},
	{"AW22XXX_IMAX_4mA"},
	{"AW22XXX_IMAX_6mA"},
	{"AW22XXX_IMAX_9mA"},
	{"AW22XXX_IMAX_10mA"},
	{"AW22XXX_IMAX_15mA"},
	{"AW22XXX_IMAX_20mA"},
	{"AW22XXX_IMAX_30mA"},
	{"AW22XXX_IMAX_40mA"},
	{"AW22XXX_IMAX_45mA"},
	{"AW22XXX_IMAX_60mA"},
	{"AW22XXX_IMAX_75mA"},
};
static char aw22xxx_imax_code[] = {
	AW22XXX_IMAX_2mA,
	AW22XXX_IMAX_3mA,
	AW22XXX_IMAX_4mA,
	AW22XXX_IMAX_6mA,
	AW22XXX_IMAX_9mA,
	AW22XXX_IMAX_10mA,
	AW22XXX_IMAX_15mA,
	AW22XXX_IMAX_20mA,
	AW22XXX_IMAX_30mA,
	AW22XXX_IMAX_40mA,
	AW22XXX_IMAX_45mA,
	AW22XXX_IMAX_60mA,
	AW22XXX_IMAX_75mA,
};
/******************************************************
 *
 * aw22xxx i2c write/read
 *
 ******************************************************/
static int
aw22xxx_i2c_write(struct aw22xxx *aw22xxx, unsigned char reg_addr, unsigned char reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_write_byte_data(aw22xxx->i2c, reg_addr, reg_data);
		if (ret < 0)
			pr_err("%s: i2c_write cnt=%d error=%d\n", __func__, cnt, ret);
		else
			break;
		cnt++;
		usleep_range(2000, 3000);
	}

	return ret;
}

static int
aw22xxx_i2c_read(struct aw22xxx *aw22xxx, unsigned char reg_addr, unsigned char *reg_data)
{
	int ret = -1;
	unsigned char cnt = 0;

	while (cnt < AW_I2C_RETRIES) {
		ret = i2c_smbus_read_byte_data(aw22xxx->i2c, reg_addr);
		if (ret < 0) {
			pr_err("%s: i2c_read cnt=%d error=%d\n", __func__, cnt, ret);
		} else {
			*reg_data = ret;
			break;
		}
		cnt++;
		usleep_range(2000, 3000);
	}

	return ret;
}

static int aw22xxx_i2c_write_bits(struct aw22xxx *aw22xxx,
		unsigned char reg_addr, unsigned char mask, unsigned char reg_data)
{
	unsigned char reg_val = 0;

	aw22xxx_i2c_read(aw22xxx, reg_addr, &reg_val);
	reg_val &= mask;
	reg_val |= (reg_data & (~mask));
	aw22xxx_i2c_write(aw22xxx, reg_addr, reg_val);

	return 0;
}

#ifdef AW22XXX_FLASH_I2C_WRITES
static int aw22xxx_i2c_writes(struct aw22xxx *aw22xxx,
		unsigned char reg_addr, unsigned char *buf, unsigned int len)
{
	int ret = -1;
	unsigned char *data;

	data = kmalloc(len+1, GFP_KERNEL);
	if (data == NULL)
		return  -ENOMEM;

	data[0] = reg_addr;
	memcpy(&data[1], buf, len);

	ret = i2c_master_send(aw22xxx->i2c, data, len+1);
	if (ret < 0)
		pr_err("%s: i2c master send error\n", __func__);

	kfree(data);

	return ret;
}
#endif

/*****************************************************
 *
 * aw22xxx led cfg
 *
 *****************************************************/
static int aw22xxx_reg_page_cfg(struct aw22xxx *aw22xxx, unsigned char page)
{
	aw22xxx_i2c_write(aw22xxx, REG_PAGE, page);
	return 0;
}

static int aw22xxx_sw_reset(struct aw22xxx *aw22xxx)
{
	aw22xxx_i2c_write(aw22xxx, REG_SRST, AW22XXX_SRSTW);
	usleep_range(2000, 3000);
	return 0;
}

static int aw22xxx_chip_enable(struct aw22xxx *aw22xxx, bool flag)
{
	if (flag) {
		aw22xxx_i2c_write_bits(aw22xxx, REG_GCR,
				BIT_GCR_CHIPEN_MASK, BIT_GCR_CHIPEN_ENABLE);
	} else {
		aw22xxx_i2c_write_bits(aw22xxx, REG_GCR,
				BIT_GCR_CHIPEN_MASK, BIT_GCR_CHIPEN_DISABLE);
	}
	usleep_range(2000, 3000);
	return 0;
}

static int aw22xxx_mcu_reset(struct aw22xxx *aw22xxx, bool flag)
{
	if (flag) {
		aw22xxx_i2c_write_bits(aw22xxx, REG_MCUCTR,
				BIT_MCUCTR_MCU_RESET_MASK, BIT_MCUCTR_MCU_RESET_ENABLE);
	} else {
		aw22xxx_i2c_write_bits(aw22xxx, REG_MCUCTR,
				BIT_MCUCTR_MCU_RESET_MASK, BIT_MCUCTR_MCU_RESET_DISABLE);
	}
	return 0;
}

static int aw22xxx_mcu_enable(struct aw22xxx *aw22xxx, bool flag)
{
	if (flag) {
		aw22xxx_i2c_write_bits(aw22xxx, REG_MCUCTR,
				BIT_MCUCTR_MCU_WORK_MASK, BIT_MCUCTR_MCU_WORK_ENABLE);
	} else {
		aw22xxx_i2c_write_bits(aw22xxx, REG_MCUCTR,
				BIT_MCUCTR_MCU_WORK_MASK, BIT_MCUCTR_MCU_WORK_DISABLE);
	}
	return 0;
}

static int aw22xxx_led_task0_cfg(struct aw22xxx *aw22xxx, unsigned char task)
{
	aw22xxx_i2c_write(aw22xxx, REG_TASK0, task);
	return 0;
}

static int aw22xxx_led_task1_cfg(struct aw22xxx *aw22xxx, unsigned char task)
{
	aw22xxx_i2c_write(aw22xxx, REG_TASK1, task);
	return 0;
}

static int aw22xxx_imax_cfg(struct aw22xxx *aw22xxx, unsigned char imax)
{
	if (imax > 0x0f)
		imax = 0x0f;

	aw22xxx_reg_page_cfg(aw22xxx, AW22XXX_REG_PAGE0);
	aw22xxx_i2c_write(aw22xxx, REG_IMAX, imax);

	return 0;
}

static int aw22xxx_dbgctr_cfg(struct aw22xxx *aw22xxx, unsigned char cfg)
{
	if (cfg >= (AW22XXX_DBGCTR_MAX-1))
		cfg = AW22XXX_DBGCTR_NORMAL;

	aw22xxx_i2c_write(aw22xxx, REG_DBGCTR, cfg);

	return 0;
}

static int aw22xxx_addr_cfg(struct aw22xxx *aw22xxx, unsigned int addr)
{
	aw22xxx_i2c_write(aw22xxx, REG_ADDR1, (unsigned char)((addr>>0)&0xff));
	aw22xxx_i2c_write(aw22xxx, REG_ADDR2, (unsigned char)((addr>>8)&0xff));

	return 0;
}

static int aw22xxx_data_cfg(struct aw22xxx *aw22xxx, unsigned int data)
{
	aw22xxx_i2c_write(aw22xxx, REG_DATA, data);

	return 0;
}

static int aw22xxx_led_display(struct aw22xxx *aw22xxx)
{
	aw22xxx_addr_cfg(aw22xxx, 0x00e1);
	aw22xxx_dbgctr_cfg(aw22xxx, AW22XXX_DBGCTR_SFR);
	aw22xxx_data_cfg(aw22xxx, 0x3d);
	aw22xxx_dbgctr_cfg(aw22xxx, AW22XXX_DBGCTR_NORMAL);
	return 0;
}

static int aw22xxx_led_off(struct aw22xxx *aw22xxx)
{
	aw22xxx_led_task0_cfg(aw22xxx, 0xff);
	aw22xxx_mcu_reset(aw22xxx, true);
	return 0;
}

static void aw22xxx_brightness_work(struct work_struct *work)
{
	struct aw22xxx *aw22xxx = container_of(work, struct aw22xxx, brightness_work);

	aw22xxx_led_off(aw22xxx);
	aw22xxx_chip_enable(aw22xxx, false);
	if (aw22xxx->cdev.brightness) {
		aw22xxx_chip_enable(aw22xxx, true);
		aw22xxx_mcu_enable(aw22xxx, true);

		aw22xxx_imax_cfg(aw22xxx, (unsigned char)aw22xxx->imax);
		aw22xxx_led_display(aw22xxx);

		aw22xxx_led_task0_cfg(aw22xxx, 0x82);
		aw22xxx_mcu_reset(aw22xxx, false);
	}
}

static void aw22xxx_set_brightness(struct led_classdev *cdev, enum led_brightness brightness)
{
	struct aw22xxx *aw22xxx = container_of(cdev, struct aw22xxx, cdev);

	aw22xxx->cdev.brightness = brightness;

	schedule_work(&aw22xxx->brightness_work);
}

static void aw22xxx_task_work(struct work_struct *work)
{
	struct aw22xxx *aw22xxx = container_of(work, struct aw22xxx, task_work);

	aw22xxx_led_off(aw22xxx);
	aw22xxx_chip_enable(aw22xxx, false);
	if (aw22xxx->task0) {
		aw22xxx_chip_enable(aw22xxx, true);
		aw22xxx_mcu_enable(aw22xxx, true);

		aw22xxx_imax_cfg(aw22xxx, (unsigned char)aw22xxx->imax);
		aw22xxx_led_display(aw22xxx);

		aw22xxx_led_task0_cfg(aw22xxx, aw22xxx->task0);
		aw22xxx_led_task1_cfg(aw22xxx, aw22xxx->task1);
		aw22xxx_mcu_reset(aw22xxx, false);
	}
}

static int aw22xxx_led_init(struct aw22xxx *aw22xxx)
{
	aw22xxx_sw_reset(aw22xxx);
	aw22xxx_chip_enable(aw22xxx, true);
	aw22xxx_imax_cfg(aw22xxx, aw22xxx_imax_code[aw22xxx->imax]);
	aw22xxx_chip_enable(aw22xxx, false);
#ifdef OPLUS_FEATURE_MISC_BASIC
	aw22xxx_i2c_write(aw22xxx, REG_PAGE, 0x00);
	aw22xxx_i2c_write(aw22xxx, REG_GCR, 0x01);
	aw22xxx_i2c_write(aw22xxx, REG_AUDCTR, 0x00);
	aw22xxx_i2c_write(aw22xxx, REG_IMAX, 0x03);
	aw22xxx_i2c_write(aw22xxx, REG_TASK0, 0x01);
	aw22xxx_i2c_write(aw22xxx, REG_MCUCTR, 0x01);
	aw22xxx_i2c_write(aw22xxx, REG_INTEN, 0x01);
	aw22xxx_i2c_write(aw22xxx, REG_MCUCTR, 0x03);
	aw22xxx_i2c_write(aw22xxx, REG_TASK0, 0x41);
#endif
	return 0;
}

/*****************************************************
 *
 * firmware/cfg update
 *
 *****************************************************/
static void aw22xxx_cfg_loaded(const struct firmware *cont, void *context)
{
	struct aw22xxx *aw22xxx = context;
	int i = 0;
	unsigned char page = 0;
	unsigned char reg_addr = 0;
	unsigned char reg_val = 0;
	unsigned int index = 0;


	if (!cont) {
		pr_err("%s: failed to read %s\n", __func__, aw22xxx_cfg_name[aw22xxx->effect]);
		release_firmware(cont);
		return;
	}

	pr_info("%s: loaded %s - size: %zu\n", __func__, aw22xxx_cfg_name[aw22xxx->effect],
					cont ? cont->size : 0);

	for (i = 0; i < cont->size; i += 2) {
		if (*(cont->data+i) == 0xff)
			page = *(cont->data+i+1);

		if (aw22xxx->cfg == 1) {
			aw22xxx_i2c_write(aw22xxx, *(cont->data+i), *(cont->data+i+1));
			pr_debug("%s: addr:0x%02x, data:0x%02x\n", __func__, *(cont->data+i), *(cont->data+i+1));
		} else if (aw22xxx->cfg == 2){
			if (page == AW22XXX_REG_PAGE1) {
				reg_addr = *(cont->data+i);
				if ((reg_addr < 0x2b) && (reg_addr > 0x0f)) {
					reg_addr -= 0x10;
					reg_val = (unsigned char)(((aw22xxx->rgb[reg_addr / 3]) >> (8 * (2 - reg_addr % 3))) & 0xff);
					aw22xxx_i2c_write(aw22xxx, *(cont->data+i), reg_val);
					pr_debug("%s: addr:0x%02x, data:0x%02x\n", __func__, *(cont->data+i), reg_val);
				} else {
					aw22xxx_i2c_write(aw22xxx, *(cont->data+i), *(cont->data+i+1));
					pr_debug("%s: addr:0x%02x, data:0x%02x\n", __func__, *(cont->data+i), *(cont->data+i+1));
				}
			} else {
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), *(cont->data+i+1));
				pr_debug("%s: addr:0x%02x, data:0x%02x\n", __func__, *(cont->data+i), *(cont->data+i+1));
			}
		} else if (aw22xxx->cfg == 3){
			if(i>=308 && (i-308)%12 == 0 && i < 380){
				index = (i-308)/12;
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), (aw22xxx->rgb[index]>>16)&0xff);
			}else if(i>=308 && (i-308)%12 == 4 && i < 380){
				index = (i-308)/12;
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), (aw22xxx->rgb[index]>>8)&0xff);
			}else if(i>=308 && (i-308)%12 == 8 && i < 380){
				index = (i-308)/12;
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), (aw22xxx->rgb[index]>>0)&0xff);
			}else{
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), *(cont->data+i+1));
			}
		} else if (aw22xxx->cfg == 4) {
			if(i>=560 && (i-560)%12 == 0 && i < 632){
				index = (i-560)/12;
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), (aw22xxx->rgb[index]>>16)&0xff);
			}else if(i>=560 && (i-560)%12 == 4 && i < 632){
				index = (i-560)/12;
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), (aw22xxx->rgb[index]>>8)&0xff);
			}else if(i>=560 && (i-560)%12 == 8 && i < 632){
				index = (i-560)/12;
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), (aw22xxx->rgb[index]>>0)&0xff);
			}else{
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), *(cont->data+i+1));
			}
		} else if (aw22xxx->cfg == 5) {
			if(i>=392 && (i-392)%12 == 0 && i < 464){
				index = (i-392)/12;
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), (aw22xxx->rgb[index]>>16)&0xff);
			}else if(i>=392 && (i-392)%12 == 4 && i < 464){
				index = (i-392)/12;
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), (aw22xxx->rgb[index]>>8)&0xff);
			}else if(i>=392 && (i-392)%12 == 8 && i < 464){
				index = (i-392)/12;
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), (aw22xxx->rgb[index]>>0)&0xff);
			}else{
				aw22xxx_i2c_write(aw22xxx, *(cont->data+i), *(cont->data+i+1));
			}
		}

		if (page == AW22XXX_REG_PAGE0) {
			reg_addr = *(cont->data+i);
			reg_val = *(cont->data+i+1);
			/* gcr chip enable delay */
			if ((reg_addr == REG_GCR) &&
					((reg_val&BIT_GCR_CHIPEN_ENABLE) == BIT_GCR_CHIPEN_ENABLE)) {
				usleep_range(2000, 3000);
			}
		}
	}

	release_firmware(cont);

	pr_info("%s: cfg update complete\n", __func__);
	mutex_unlock(&aw22xxx->cfg_lock);
}

static int aw22xxx_cfg_update(struct aw22xxx *aw22xxx)
{
	pr_info("aw22xxx_cfg_update\n");
	if (aw22xxx->effect < (sizeof(aw22xxx_cfg_name)/AW22XXX_CFG_NAME_MAX)) {
		pr_info("%s: cfg name=%s\n", __func__, aw22xxx_cfg_name[aw22xxx->effect]);
	} else {
		pr_err("%s: effect 0x%02x over max value\n", __func__, aw22xxx->effect);
		aw22xxx->effect = (sizeof(aw22xxx_cfg_name)/AW22XXX_CFG_NAME_MAX) - 1;
	}

	if (aw22xxx->fw_flags != AW22XXX_FLAG_FW_OK)
		return -2;

	mutex_lock(&aw22xxx->cfg_lock);

	return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				aw22xxx_cfg_name[aw22xxx->effect], aw22xxx->dev, GFP_KERNEL,
				aw22xxx, aw22xxx_cfg_loaded);
}

static int aw22xxx_container_update(struct aw22xxx *aw22xxx, struct aw22xxx_container *aw22xxx_fw)
{
	unsigned int i;
	unsigned char reg_val;
	unsigned int tmp_bist;
#ifdef AW22XXX_FLASH_I2C_WRITES
	unsigned int tmp_len;
#endif

	/* chip enable*/
	aw22xxx_reg_page_cfg(aw22xxx, AW22XXX_REG_PAGE0);
	aw22xxx_sw_reset(aw22xxx);
	aw22xxx_chip_enable(aw22xxx, true);
	aw22xxx_mcu_enable(aw22xxx, true);

	/* flash cfg */
	aw22xxx_i2c_write(aw22xxx, 0x80, 0xec);
	aw22xxx_i2c_write(aw22xxx, 0x35, 0x29);
	/*aw22xxx_i2c_write(aw22xxx, 0x37, 0xba);*/
	aw22xxx_i2c_write(aw22xxx, 0x38, aw22xxx_fw->key);

	/* flash erase*/
	aw22xxx_i2c_write(aw22xxx, 0x22, 0x00);
	aw22xxx_i2c_write(aw22xxx, 0x21, 0x00);
	aw22xxx_i2c_write(aw22xxx, 0x20, 0x03);
	aw22xxx_i2c_write(aw22xxx, 0x30, 0x03);
	aw22xxx_i2c_write(aw22xxx, 0x23, 0x00);
	usleep_range(4000, 4500);
	aw22xxx_i2c_write(aw22xxx, 0x30, 0x00);
	aw22xxx_i2c_write(aw22xxx, 0x22, 0x40);
	aw22xxx_i2c_write(aw22xxx, 0x21, 0x00);
	aw22xxx_i2c_write(aw22xxx, 0x30, 0x02);
	aw22xxx_i2c_write(aw22xxx, 0x23, 0x00);
	usleep_range(6000, 6500);
	aw22xxx_i2c_write(aw22xxx, 0x30, 0x00);
	aw22xxx_i2c_write(aw22xxx, 0x22, 0x42);
	aw22xxx_i2c_write(aw22xxx, 0x21, 0x00);
	aw22xxx_i2c_write(aw22xxx, 0x30, 0x02);
	aw22xxx_i2c_write(aw22xxx, 0x23, 0x00);
	usleep_range(6000, 6500);
	aw22xxx_i2c_write(aw22xxx, 0x30, 0x00);
	aw22xxx_i2c_write(aw22xxx, 0x22, 0x44);
	aw22xxx_i2c_write(aw22xxx, 0x21, 0x00);
	aw22xxx_i2c_write(aw22xxx, 0x30, 0x02);
	aw22xxx_i2c_write(aw22xxx, 0x23, 0x00);
	usleep_range(6000, 6500);
	aw22xxx_i2c_write(aw22xxx, 0x30, 0x00);
	aw22xxx_i2c_write(aw22xxx, 0x20, 0x00);

	#ifdef AW22XXX_FLASH_I2C_WRITES
	/* flash writes */
	aw22xxx_i2c_write(aw22xxx, 0x20, 0x03);
	for (i = 0; i < aw22xxx_fw->len; i += tmp_len) {
		aw22xxx_i2c_write(aw22xxx, 0x22, ((i>>8)&0xff));
		aw22xxx_i2c_write(aw22xxx, 0x21, ((i>>0)&0xff));
		aw22xxx_i2c_write(aw22xxx, 0x11, 0x01);
		aw22xxx_i2c_write(aw22xxx, 0x30, 0x04);
		if ((aw22xxx_fw->len - i) < MAX_FLASH_WRITE_BYTE_SIZE)
			tmp_len = aw22xxx_fw->len - i;
		else
			tmp_len = MAX_FLASH_WRITE_BYTE_SIZE;

		aw22xxx_i2c_writes(aw22xxx, 0x23, &aw22xxx_fw->data[i], tmp_len);
		aw22xxx_i2c_write(aw22xxx, 0x11, 0x00);
		aw22xxx_i2c_write(aw22xxx, 0x30, 0x00);
	}
	aw22xxx_i2c_write(aw22xxx, 0x20, 0x00);
	#else
	/* flash write */
	aw22xxx_i2c_write(aw22xxx, 0x20, 0x03);
	for (i = 0; i < aw22xxx_fw->len; i++) {
		aw22xxx_i2c_write(aw22xxx, 0x22, ((i>>8)&0xff));
		aw22xxx_i2c_write(aw22xxx, 0x21, ((i>>0)&0xff));
		aw22xxx_i2c_write(aw22xxx, 0x30, 0x04);
		aw22xxx_i2c_write(aw22xxx, 0x23, aw22xxx_fw->data[i]);
		aw22xxx_i2c_write(aw22xxx, 0x30, 0x00);
	}
	aw22xxx_i2c_write(aw22xxx, 0x20, 0x00);
	#endif

	/* bist check */
	aw22xxx_sw_reset(aw22xxx);
	aw22xxx_chip_enable(aw22xxx, true);
	aw22xxx_mcu_enable(aw22xxx, true);
	aw22xxx_i2c_write(aw22xxx, 0x22, (((aw22xxx_fw->len-1)>>8)&0xff));
	aw22xxx_i2c_write(aw22xxx, 0x21, (((aw22xxx_fw->len-1)>>0)&0xff));
	aw22xxx_i2c_write(aw22xxx, 0x24, 0x07);
	usleep_range(5000, 6500);
	aw22xxx_i2c_read(aw22xxx, 0x24, &reg_val);
	if (reg_val == 0x05) {
		aw22xxx_i2c_read(aw22xxx, 0x25, &reg_val);
		tmp_bist = reg_val;
		aw22xxx_i2c_read(aw22xxx, 0x26, &reg_val);
		tmp_bist |= (reg_val<<8);
		if (tmp_bist == aw22xxx_fw->bist) {
			pr_info("%s: bist check pass, bist=0x%04x\n", __func__, aw22xxx_fw->bist);
		} else {
			pr_err("%s: bist check fail, bist=0x%04x\n", __func__, aw22xxx_fw->bist);
			pr_err("%s: fw update failed, please reset phone\n", __func__);
			return -1;
		}
	} else {
		pr_err("%s: bist check is running, reg0x24=0x%02x\n", __func__, reg_val);
	}
	aw22xxx_i2c_write(aw22xxx, 0x24, 0x00);

	return 0;
}

static void aw22xxx_fw_loaded(const struct firmware *cont, void *context)
{
	struct aw22xxx *aw22xxx = context;
	struct aw22xxx_container *aw22xxx_fw;
	int i = 0;
	int ret = -1;
	char tmp_buf[32] = {0};
	unsigned int shift = 0;
	unsigned short check_sum = 0;
	unsigned char reg_val = 0;
	unsigned int tmp_bist = 0;

	if (!cont) {
		pr_err("%s: failed to read %s\n", __func__, aw22xxx_fw_name);
		release_firmware(cont);
		return;
	}

	pr_info("%s: loaded %s - size: %zu\n", __func__, aw22xxx_fw_name,
					cont ? cont->size : 0);

	/* check sum */
	for (i = 2; i < cont->size; i++)
		check_sum += cont->data[i];

	if (check_sum != (unsigned short)((cont->data[0]<<8)|(cont->data[1]))) {
		pr_err("%s: check sum err: check_sum=0x%04x\n", __func__, check_sum);
		release_firmware(cont);
		return;
	}

	/* get fw info */
	aw22xxx_fw = kzalloc(cont->size + 4*sizeof(unsigned int), GFP_KERNEL);
	if (!aw22xxx_fw) {
		release_firmware(cont);
		pr_err("%s: Error allocating memory\n", __func__);
		return;
	}
	shift += 2;

	pr_info("%s: fw chip_id : 0x%02x\n", __func__, cont->data[0+shift]);
	shift += 1;

	memcpy(tmp_buf, &cont->data[0+shift], 16);
	pr_info("%s: fw customer: %s\n", __func__, tmp_buf);
	shift += 16;

	memcpy(tmp_buf, &cont->data[0+shift], 8);
	pr_info("%s: fw project: %s\n", __func__, tmp_buf);
	shift += 8;

	aw22xxx_fw->version = (cont->data[0+shift]<<24) | (cont->data[1+shift]<<16) |
			(cont->data[2+shift]<<8) | (cont->data[3+shift]<<0);
	pr_info("%s: fw version : 0x%04x\n", __func__, aw22xxx_fw->version);
	shift += 4;

	// reserved
	shift += 3;

	aw22xxx_fw->bist = (cont->data[0+shift]<<8) | (cont->data[1+shift]<<0);
	pr_info("%s: fw bist : 0x%04x\n", __func__, aw22xxx_fw->bist);
	shift += 2;

	aw22xxx_fw->key = cont->data[0+shift];
	pr_info("%s: fw key : 0x%04x\n", __func__, aw22xxx_fw->key);
	shift += 1;

	// reserved
	shift += 1;

	aw22xxx_fw->len = (cont->data[0+shift]<<8) | (cont->data[1+shift]<<0);
	pr_info("%s: fw len : 0x%04x\n", __func__, aw22xxx_fw->len);
	shift += 2;

	memcpy(aw22xxx_fw->data, &cont->data[shift], aw22xxx_fw->len);
	release_firmware(cont);

	/* check version */
	/*aw22xxx_get_fw_version(aw22xxx); */

	/* bist check */
	aw22xxx_sw_reset(aw22xxx);
	aw22xxx_chip_enable(aw22xxx, true);
	aw22xxx_mcu_enable(aw22xxx, true);
	aw22xxx_i2c_write(aw22xxx, 0x22, (((aw22xxx_fw->len-1)>>8)&0xff));
	aw22xxx_i2c_write(aw22xxx, 0x21, (((aw22xxx_fw->len-1)>>0)&0xff));
	aw22xxx_i2c_write(aw22xxx, 0x24, 0x07);
	usleep_range(5000, 6500);
	aw22xxx_i2c_read(aw22xxx, 0x24, &reg_val);
	if (reg_val == 0x05) {
		aw22xxx_i2c_read(aw22xxx, 0x25, &reg_val);
		tmp_bist = reg_val;
		aw22xxx_i2c_read(aw22xxx, 0x26, &reg_val);
		tmp_bist |= (reg_val<<8);
		if (tmp_bist == aw22xxx_fw->bist) {
			pr_info("%s: bist check pass, bist=0x%04x\n", __func__, aw22xxx_fw->bist);
			if (aw22xxx->fw_update == 0) {
				kfree(aw22xxx_fw);
				aw22xxx_fw = NULL;
				aw22xxx_i2c_write(aw22xxx, 0x24, 0x00);
				aw22xxx_led_init(aw22xxx);
				aw22xxx->fw_flags = AW22XXX_FLAG_FW_OK;
			} else {
				pr_info("%s: fw version: 0x%04x, force update fw\n",
					__func__, aw22xxx_fw->version);
			}
		} else {
			pr_info("%s: bist check fail, fw bist=0x%04x, flash bist=0x%04x\n",
					__func__, aw22xxx_fw->bist, tmp_bist);
			pr_info("%s: find new fw: 0x%04x, need update\n",
					__func__, aw22xxx_fw->version);
		}
	} else {
		pr_err("%s: bist check is running, reg0x24=0x%02x\n", __func__, reg_val);
		pr_info("%s: fw need update\n", __func__);
	}
	aw22xxx_i2c_write(aw22xxx, 0x24, 0x00);

	/* fw update */
	if (aw22xxx_fw != NULL) {
		pr_info("%s update firmware start ....\n", __func__);
		ret = aw22xxx_container_update(aw22xxx, aw22xxx_fw);
		if (ret)
			aw22xxx->fw_flags = AW22XXX_FLAG_FW_FAIL;
		else
			aw22xxx->fw_flags = AW22XXX_FLAG_FW_OK;

		kfree(aw22xxx_fw);

		aw22xxx->fw_update = 0;

		aw22xxx_led_init(aw22xxx);
	}

}

static int aw22xxx_fw_update(struct aw22xxx *aw22xxx)
{
	aw22xxx->fw_flags = AW22XXX_FLAG_FW_UPDATE;

	return request_firmware_nowait(THIS_MODULE, FW_ACTION_HOTPLUG,
				aw22xxx_fw_name, aw22xxx->dev, GFP_KERNEL,
				aw22xxx, aw22xxx_fw_loaded);
}

#ifdef AWINIC_FW_UPDATE_DELAY
static enum hrtimer_restart aw22xxx_fw_timer_func(struct hrtimer *timer)
{
	struct aw22xxx *aw22xxx = container_of(timer, struct aw22xxx, fw_timer);

	schedule_work(&aw22xxx->fw_work);

	return HRTIMER_NORESTART;
}
#endif

static void aw22xxx_fw_work_routine(struct work_struct *work)
{
	struct aw22xxx *aw22xxx = container_of(work, struct aw22xxx, fw_work);

	aw22xxx_fw_update(aw22xxx);

}

static void aw22xxx_cfg_work_routine(struct work_struct *work)
{
	struct aw22xxx *aw22xxx = container_of(work, struct aw22xxx, cfg_work);

	aw22xxx_cfg_update(aw22xxx);

}

static int aw22xxx_fw_init(struct aw22xxx *aw22xxx)
{
#ifdef AWINIC_FW_UPDATE_DELAY
	int fw_timer_val = 10000;

	hrtimer_init(&aw22xxx->fw_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	aw22xxx->fw_timer.function = aw22xxx_fw_timer_func;
	INIT_WORK(&aw22xxx->fw_work, aw22xxx_fw_work_routine);
	INIT_WORK(&aw22xxx->cfg_work, aw22xxx_cfg_work_routine);
	hrtimer_start(&aw22xxx->fw_timer,
			ktime_set(fw_timer_val/1000, (fw_timer_val%1000)*1000000),
			HRTIMER_MODE_REL);
#else
	INIT_WORK(&aw22xxx->fw_work, aw22xxx_fw_work_routine);
	INIT_WORK(&aw22xxx->cfg_work, aw22xxx_cfg_work_routine);
	schedule_work(&aw22xxx->fw_work);
#endif
	return 0;
}


/******************************************************
 *
 * irq
 *
 ******************************************************/
static void aw22xxx_interrupt_clear(struct aw22xxx *aw22xxx)
{
	unsigned char reg_val;

	aw22xxx_i2c_read(aw22xxx, REG_INTST, &reg_val);
	pr_info("%s: reg INTST=0x%x\n", __func__, reg_val);
}

static void aw22xxx_interrupt_setup(struct aw22xxx *aw22xxx)
{
	aw22xxx_interrupt_clear(aw22xxx);

	aw22xxx_i2c_write_bits(aw22xxx, REG_INTEN,
			BIT_INTEN_FUNCMPE_MASK, BIT_INTEN_FUNCMPE_ENABLE);

	// enable wtd irq
	aw22xxx_i2c_write_bits(aw22xxx, REG_INTEN,
			BIT_INTEN_WTD_MASK, BIT_INTEN_WTD_ENABLE);
}

static irqreturn_t aw22xxx_irq(int irq, void *data)
{
	struct aw22xxx *aw22xxx = data;
	unsigned char reg_val = 0x00;

	aw22xxx_i2c_read(aw22xxx, REG_INTST, &reg_val);
	pr_info("%s: reg INTST=0x%x\n", __func__, reg_val);

	if (reg_val & BIT_INTST_FUNCMPE) {
		pr_info("%s: functions compelte!\n", __func__);
		aw22xxx_reg_page_cfg(aw22xxx, AW22XXX_REG_PAGE0);
		aw22xxx_mcu_reset(aw22xxx, true);
		aw22xxx_mcu_enable(aw22xxx, false);
		aw22xxx_chip_enable(aw22xxx, false);
		pr_info("%s: enter standby mode!\n", __func__);
	}
	if (reg_val & BIT_INTST_WTD) {
		pr_info("%s: get watch dog irq!\n", __func__);
		aw22xxx_reg_page_cfg(aw22xxx, AW22XXX_REG_PAGE0);
		aw22xxx_mcu_reset(aw22xxx, true);
		aw22xxx_mcu_reset(aw22xxx, false);
		aw22xxx_mcu_enable(aw22xxx, true);
		if (aw22xxx->effect)
			schedule_work(&aw22xxx->cfg_work);
	}

	return IRQ_HANDLED;
}

/*****************************************************
 *
 * device tree
 *
 *****************************************************/
static int aw22xxx_parse_dt(struct device *dev, struct aw22xxx *aw22xxx, struct device_node *np)
{
#ifdef OPLUS_FEATURE_MISC_BASIC
	int ret;
#endif

	aw22xxx->reset_gpio = of_get_named_gpio(np, "reset-gpio", 0);

	aw22xxx->irq_gpio = of_get_named_gpio(np, "irq-gpio", 0);

#ifdef OPLUS_FEATURE_MISC_BASIC
	ret = of_property_read_u32(np, "vbled_volt",
			&aw22xxx->vbled_volt);
	if (ret < 0) {
		pr_err("%s: vbled_volt is not set\n", __func__);
		aw22xxx->vbled_volt = 0;
	}
#endif

	return 0;
}

static int aw22xxx_hw_reset(struct aw22xxx *aw22xxx)
{
#ifdef OPLUS_FEATURE_MISC_BASIC
	int ret;
#endif

	if (aw22xxx && gpio_is_valid(aw22xxx->reset_gpio)) {
		gpio_set_value_cansleep(aw22xxx->reset_gpio, 0);
		usleep_range(1000, 1500);
		gpio_set_value_cansleep(aw22xxx->reset_gpio, 1);
		usleep_range(1000, 1500);
	} else {
		pr_err("%s: failed\n", __func__);
	}


#ifdef OPLUS_FEATURE_MISC_BASIC
	if (!IS_ERR_OR_NULL(aw22xxx->vbled)) {
		pr_err("Enable the Regulator vbled.\n");
		ret = regulator_enable(aw22xxx->vbled);
		if (ret) {
			pr_err("Regulator vbled enable failed ret = %d\n", ret);
		}
	}
#endif

	return 0;
}

static int aw22xxx_hw_off(struct aw22xxx *aw22xxx)
{
#ifdef OPLUS_FEATURE_MISC_BASIC
	int ret;
#endif

	if (aw22xxx && gpio_is_valid(aw22xxx->reset_gpio)) {
		gpio_set_value_cansleep(aw22xxx->reset_gpio, 0);
		usleep_range(1000, 1500);
	} else {
		pr_err("%s: failed\n", __func__);
	}


#ifdef OPLUS_FEATURE_MISC_BASIC
	if (!IS_ERR_OR_NULL(aw22xxx->vbled)) {
		pr_err("Disable the Regulator vbled.\n");
		ret = regulator_disable(aw22xxx->vbled);
		if (ret) {
			pr_err("Regulator vbled enable failed ret = %d\n", ret);
		}
	}
#endif


	return 0;
}

/*****************************************************
 *
 * check chip id
 *
 *****************************************************/
static int aw22xxx_read_chipid(struct aw22xxx *aw22xxx)
{
	int ret = -1;
	unsigned char cnt = 0;
	unsigned char reg_val = 0;

	aw22xxx_reg_page_cfg(aw22xxx, AW22XXX_REG_PAGE0);
	aw22xxx_sw_reset(aw22xxx);

	while (cnt < AW_READ_CHIPID_RETRIES) {
		ret = aw22xxx_i2c_read(aw22xxx, REG_SRST, &reg_val);
		if (ret < 0) {
			pr_err(
				"%s: failed to read register AW22XXX_REG_ID: %d\n",
				__func__, ret);
			return -EIO;
		}
		switch (reg_val) {
		case AW22XXX_SRSTR:
			pr_info("%s aw22xxx detected\n", __func__);
			//aw22xxx->flags |= AW22XXX_FLAG_SKIP_INTERRUPTS;
			aw22xxx_i2c_read(aw22xxx, REG_CHIPID, &reg_val);
			switch (reg_val) {
			case AW22118_CHIPID:
				aw22xxx->chipid = AW22118;
				pr_info("%s: chipid: aw22118\n", __func__);
				break;
			case AW22127_CHIPID:
				aw22xxx->chipid = AW22127;
				pr_info("%s: chipid: aw22127\n", __func__);
				break;
			default:
				pr_err("%s: unknown id=0x%02x\n", __func__, reg_val);
				break;
			}
			return 0;
		default:
			pr_info("%s unsupported device revision (0x%x)\n",
				__func__, reg_val);
			break;
		}
		cnt++;

		msleep(AW_READ_CHIPID_RETRY_DELAY);
	}

	return -EINVAL;
}


/******************************************************
 *
 * sys group attribute: reg
 *
 ******************************************************/
static ssize_t
reg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	unsigned int databuf[2] = {0, 0};

	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
#ifdef OPLUS_FEATURE_MISC_BASIC
		pr_info("%s: set reg value: 0x%x with value: 0x%x", __func__, databuf[0], databuf[1]);
#endif
		aw22xxx_i2c_write(aw22xxx, databuf[0], databuf[1]);
	}
	return count;
}

static ssize_t reg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);
	ssize_t len = 0;
	unsigned int i = 0;
	unsigned char reg_val = 0;
	unsigned char reg_page = 0;

	aw22xxx_i2c_read(aw22xxx, REG_PAGE, &reg_page);
	for (i = 0; i < AW22XXX_REG_MAX; i++) {
		if (!reg_page) {
			if (!(aw22xxx_reg_access[i]&REG_RD_ACCESS))
				continue;
		}
		aw22xxx_i2c_read(aw22xxx, i, &reg_val);
		len += snprintf(buf+len, PAGE_SIZE-len, "reg:0x%02x=0x%02x\n", i, reg_val);
	}
	return len;
}
static ssize_t
hwen_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);
	unsigned int databuf[1] = { 0 };
	int ret = -1;

	ret = kstrtou32(buf, 0, &databuf[0]);
	if (ret < 0) {
		pr_err("%s: input data invalid!", __func__);
		return ret;
	}
#ifdef OPLUS_FEATURE_MISC_BASIC
	pr_info("%s: set hwen value: %u", __func__, databuf[0]);
#endif
	if (databuf[0] == 1) {
		aw22xxx_hw_reset(aw22xxx);
	} else {
		aw22xxx_hw_off(aw22xxx);
	}

	return count;
}

static ssize_t
hwen_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len, "hwen=%d\n", gpio_get_value(aw22xxx->reset_gpio));

	return len;
}

static ssize_t
fw_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);
	unsigned int databuf[1] = { 0 };
	int ret = -1;

	ret = kstrtou32(buf, 0, &databuf[0]);
	if (ret < 0) {
		pr_err("%s: input data invalid!", __func__);
		return ret;
	}

	aw22xxx->fw_update = databuf[0];
#ifdef OPLUS_FEATURE_MISC_BASIC
	pr_info("%s: set fw value: %u", __func__, aw22xxx->fw_update);
#endif
	if (databuf[0] == 1){
		schedule_work(&aw22xxx->fw_work);
	}
	return count;
}

static ssize_t
fw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;

	len += snprintf(buf+len, PAGE_SIZE-len, "firmware name = %s\n", aw22xxx_fw_name);

	return len;
}

static ssize_t
cfg_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);
	unsigned int databuf[1] = { 0 };
	int ret = -1;

	ret = kstrtou32(buf, 0, &databuf[0]);
	if (ret < 0) {
		pr_err("%s: input data invalid!", __func__);
		return ret;
	}
#ifdef OPLUS_FEATURE_MISC_BASIC
	pr_info("%s: set cfg value = %x\n", __func__, databuf[0]);
#endif
	aw22xxx->cfg = databuf[0];
	if (aw22xxx->cfg)
		schedule_work(&aw22xxx->cfg_work);


	return count;
}

static ssize_t
cfg_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	unsigned int i;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	for (i = 0; i < sizeof(aw22xxx_cfg_name) / AW22XXX_CFG_NAME_MAX; i++) {
		len += snprintf(buf + len, PAGE_SIZE - len,
				"cfg[%x] = %s\n", i, aw22xxx_cfg_name[i]);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "current cfg = %s\n",
			aw22xxx_cfg_name[aw22xxx->effect]);

	return len;
}

static ssize_t
effect_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int databuf[1] = { 0 };
	int ret = -1;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	ret = kstrtou32(buf, 0, &databuf[0]);
	if (ret < 0) {
		pr_err("%s: input data invalid!", __func__);
		return ret;
	}

	aw22xxx->effect = databuf[0];
	pr_info("%s: set effect value = %d\n", __func__, aw22xxx->effect);
	return len;
}

static ssize_t effect_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	len += snprintf(buf+len, PAGE_SIZE-len, "effect = 0x%02x\n", aw22xxx->effect);

	return len;
}

static ssize_t
imax_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int databuf[1] = { 0 };
	int ret = -1;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	ret = kstrtou32(buf, 0, &databuf[0]);
	if (ret < 0) {
		pr_err("%s: input data invalid!", __func__);
		return ret;
	}

	aw22xxx->imax = databuf[0];
#ifdef OPLUS_FEATURE_MISC_BASIC
	pr_info("%s: set imax value: %u, imax code: %u", __func__, aw22xxx->imax, aw22xxx_imax_code[aw22xxx->imax]);
#endif
	aw22xxx_imax_cfg(aw22xxx, aw22xxx_imax_code[aw22xxx->imax]);

	return len;
}

static ssize_t imax_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	unsigned int i;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	for (i = 0; i < sizeof(aw22xxx_imax_name) / AW22XXX_IMAX_NAME_MAX; i++)
		len += snprintf(buf+len, PAGE_SIZE-len, "imax[%x] = %s\n", i, aw22xxx_imax_name[i]);

	len += snprintf(buf+len, PAGE_SIZE-len, "current id = 0x%02x, imax = %s\n",
		aw22xxx->imax, aw22xxx_imax_name[aw22xxx->imax]);

	return len;
}

static ssize_t
rgb_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int databuf[2];
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);
	int i;

#ifdef OPLUS_FEATURE_MISC_BASIC
	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2) {
		pr_info("%s: set rgb[%x] = %x\n", __func__, databuf[0], databuf[1]);
		aw22xxx->rgb[databuf[0]] = databuf[1];
	} else if (sscanf(buf, "%x", &databuf[0]) == 1) {
		pr_info("%s: set rgb all = %x\n", __func__, databuf[0]);
		for (i = 0; i < AW22XXX_RGB_MAX; i++) {
			aw22xxx->rgb[i] = databuf[0];
		}
	} else {
		pr_err("%s: input data invalid!", __func__);
		return -EINVAL;
	}
#else
	if (sscanf(buf, "%x %x", &databuf[0], &databuf[1]) == 2)
		aw22xxx->rgb[databuf[0]] = databuf[1];
#endif

	return len;
}

static ssize_t rgb_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	unsigned int i;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	for (i = 0; i < AW22XXX_RGB_MAX; i++)
		len += snprintf(buf+len, PAGE_SIZE-len, "rgb[%x] = 0x%06x\n", i, aw22xxx->rgb[i]);

	return len;
}

static ssize_t
task0_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int databuf[1] = { 0 };
	int ret = -1;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	ret = kstrtou32(buf, 0, &databuf[0]);
	if (ret < 0) {
		pr_err("%s: input data invalid!", __func__);
		return ret;
	}

	aw22xxx->task0 = databuf[0];
#ifdef OPLUS_FEATURE_MISC_BASIC
	pr_info("%s: set task0 value: %u", __func__, aw22xxx->task0);
#endif
	schedule_work(&aw22xxx->task_work);

	return len;
}

static ssize_t task0_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	len += snprintf(buf+len, PAGE_SIZE-len, "task0 = 0x%02x\n", aw22xxx->task0);

	return len;
}

static ssize_t
task1_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	unsigned int databuf[1] = { 0 };
	int ret = -1;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	ret = kstrtou32(buf, 0, &databuf[0]);
	if (ret < 0) {
		pr_err("%s: input data invalid!", __func__);
		return ret;
	}

	aw22xxx->task1 = databuf[0];
#ifdef OPLUS_FEATURE_MISC_BASIC
	pr_info("%s: set task1 value: %u", __func__, aw22xxx->task1);
#endif
	return len;
}

static ssize_t task1_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	ssize_t len = 0;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);

	len += snprintf(buf+len, PAGE_SIZE-len, "task1 = 0x%02x\n", aw22xxx->task1);

	return len;
}

static ssize_t
signal_led_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t len)
{
	int rgb_index, r_g_b, color, br;
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct aw22xxx *aw22xxx = container_of(led_cdev, struct aw22xxx, cdev);
	int ret;

	if (sscanf(buf, "%d %d %d %d", &rgb_index, &r_g_b, &color, &br) == 4) {
#ifdef OPLUS_FEATURE_MISC_BASIC
		pr_info("%s: set signal_led value: rgb_index = %d, r_g_b = %d, color = %d, br = %d",
			__func__, rgb_index, r_g_b, color, br);
#endif
		if (rgb_index < 0 || rgb_index > (AW22XXX_RGB_MAX - 1)) {
			pr_err("rgb_index error: 0 ~ 8");
			return len;
		}
		if (r_g_b < 0 || r_g_b > 2) {
			pr_err("r_g_b error: 0 ~ 2");
			return len;
		}
		if (color < 0 || color > CURRENT_MAX) {
			pr_err("color error: 0 ~ 0x%02X", CURRENT_MAX);
			return len;
		}
		if (br < 0 || br > PWM_MAX) {
			pr_err("br error: 0 ~ 0x%02X", PWM_MAX);
			return len;
		}
		ret = aw22xxx_i2c_write(aw22xxx, REG_PAGE, 0);
		if (ret < 0) {
			pr_err("i2c_write error");
			return ret;
		}
		ret = aw22xxx_i2c_write(aw22xxx, REG_AUDCTR, 0);
		if (ret < 0) {
			pr_err("i2c_write error");
			return ret;
		}
		ret = aw22xxx_i2c_write(aw22xxx, REG_TASK0,
				CURRENT_CMD_BASE + rgb_index * 3 + r_g_b);
		if (ret < 0) {
			pr_err("i2c_write error");
			return ret;
		}
		ret = aw22xxx_i2c_write(aw22xxx, REG_TASK1, color);
		if (ret < 0) {
			pr_err("i2c_write error");
			return ret;
		}
		ret = aw22xxx_i2c_write(aw22xxx, REG_MCUCTR, UPDATE_CMD);
		if (ret < 0) {
			pr_err("i2c_write error");
			return ret;
		}
		ret = aw22xxx_i2c_write(aw22xxx, REG_TASK0,
				PWM_CMD_BASE + rgb_index * 3 + r_g_b);
		if (ret < 0) {
			pr_err("i2c_write error");
			return ret;
		}
		ret = aw22xxx_i2c_write(aw22xxx, REG_TASK1, br);
		if (ret < 0) {
			pr_err("i2c_write error");
			return ret;
		}
		ret = aw22xxx_i2c_write(aw22xxx, REG_MCUCTR, UPDATE_CMD);
		if (ret < 0) {
			pr_err("i2c_write error");
			return ret;
		}
	}

	return len;
}
static DEVICE_ATTR_RW(reg);
static DEVICE_ATTR_RW(hwen);
static DEVICE_ATTR_RW(fw);
static DEVICE_ATTR_RW(cfg);
static DEVICE_ATTR_RW(effect);
static DEVICE_ATTR_RW(imax);
static DEVICE_ATTR_RW(rgb);
static DEVICE_ATTR_RW(task0);
static DEVICE_ATTR_RW(task1);
static DEVICE_ATTR_WO(signal_led);

static struct attribute *aw22xxx_attributes[] = {
	&dev_attr_reg.attr,
	&dev_attr_hwen.attr,
	&dev_attr_fw.attr,
	&dev_attr_cfg.attr,
	&dev_attr_effect.attr,
	&dev_attr_imax.attr,
	&dev_attr_rgb.attr,
	&dev_attr_task0.attr,
	&dev_attr_task1.attr,
	&dev_attr_signal_led.attr,
	NULL
};

static struct attribute_group aw22xxx_attribute_group = {
	.attrs = aw22xxx_attributes
};


/******************************************************
 *
 * led class dev
 *
 ******************************************************/
static int aw22xxx_parse_led_cdev(struct aw22xxx *aw22xxx, struct device_node *np)
{
	struct device_node *temp;
	int ret = -1;

	for_each_child_of_node(np, temp) {
		ret = of_property_read_string(temp, "aw22xxx,name", &aw22xxx->cdev.name);
		if (ret < 0) {
			pr_err("Failure reading led name, ret = %d\n", ret);
			goto free_pdata;
		}
		ret = of_property_read_u32(temp, "aw22xxx,imax", &aw22xxx->imax);
		if (ret < 0) {
			pr_err("Failure reading imax, ret = %d\n", ret);
			goto free_pdata;
		}
		ret = of_property_read_u32(temp, "aw22xxx,brightness", &aw22xxx->cdev.brightness);
		if (ret < 0) {
			pr_err("Failure reading brightness, ret = %d\n", ret);
			goto free_pdata;
		}
		ret = of_property_read_u32(temp, "aw22xxx,max_brightness",
			&aw22xxx->cdev.max_brightness);
		if (ret < 0) {
			pr_err("Failure reading max brightness, ret = %d\n", ret);
			goto free_pdata;
		}
	}

	INIT_WORK(&aw22xxx->brightness_work, aw22xxx_brightness_work);
	INIT_WORK(&aw22xxx->task_work, aw22xxx_task_work);

	aw22xxx->cdev.brightness_set = aw22xxx_set_brightness;
	ret = led_classdev_register(aw22xxx->dev, &aw22xxx->cdev);
	if (ret) {
		pr_err("unable to register led ret=%d\n", ret);
		goto free_pdata;
	}

	ret = sysfs_create_group(&aw22xxx->cdev.dev->kobj, &aw22xxx_attribute_group);
	if (ret) {
		pr_err("led sysfs ret: %d\n", ret);
		goto free_class;
	}

	return 0;

free_class:
	led_classdev_unregister(&aw22xxx->cdev);
free_pdata:
	return ret;
}

/******************************************************
 *
 * i2c driver
 *
 ******************************************************/
static int aw22xxx_i2c_probe(struct i2c_client *i2c)
{
	struct aw22xxx *aw22xxx;
	struct device_node *np = i2c->dev.of_node;
	int ret;
	int irq_flags;


	if (!i2c_check_functionality(i2c->adapter, I2C_FUNC_I2C)) {
		pr_err("check_functionality failed\n");
		return -EIO;
	}

	aw22xxx = devm_kzalloc(&i2c->dev, sizeof(struct aw22xxx), GFP_KERNEL);
	if (aw22xxx == NULL)
		return -ENOMEM;
    memset(aw22xxx, 0, sizeof(struct aw22xxx));
	aw22xxx->dev = &i2c->dev;
	aw22xxx->i2c = i2c;

	i2c_set_clientdata(i2c, aw22xxx);

	mutex_init(&aw22xxx->cfg_lock);

	/* aw22xxx rst & int */
	if (np) {
		ret = aw22xxx_parse_dt(&i2c->dev, aw22xxx, np);
		if (ret) {
			pr_err("%s: failed to parse device tree node\n", __func__);
			goto err_parse_dt;
		}
	} else {
		aw22xxx->reset_gpio = -1;
		aw22xxx->irq_gpio = -1;
	}
	pr_info("aw22xxx->reset_gpio:%d,irq:%d\n",aw22xxx->reset_gpio,aw22xxx->irq_gpio);
	if (gpio_is_valid(aw22xxx->reset_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, aw22xxx->reset_gpio,
			GPIOF_OUT_INIT_LOW, "aw22xxx_rst");
		if (ret) {
			pr_err("%s: rst request failed\n", __func__);
			goto err_gpio_request;
		}
	}

	if (gpio_is_valid(aw22xxx->irq_gpio)) {
		ret = devm_gpio_request_one(&i2c->dev, aw22xxx->irq_gpio,
			GPIOF_DIR_IN, "aw22xxx_int");
		if (ret) {
			pr_err("%s: int request failed\n", __func__);
			goto err_gpio_request;
		}
	}

#ifdef OPLUS_FEATURE_MISC_BASIC
	/* vdd 3.3v*/
	aw22xxx->vbled = regulator_get(aw22xxx->dev, "vbled");
	if (!aw22xxx->vbled_volt || IS_ERR_OR_NULL(aw22xxx->vbled)) {
		pr_err("Regulator vdd3v3 get failed\n");
	} else {
		pr_err("Regulator vbled volt set %u \n", aw22xxx->vbled_volt);
		if (aw22xxx->vbled_volt) {
			ret = regulator_set_voltage(aw22xxx->vbled, aw22xxx->vbled_volt,
							aw22xxx->vbled_volt);
		} else {
			ret = regulator_set_voltage(aw22xxx->vbled, 3300000, 3300000);
		}
		if (ret) {
			pr_err("Regulator set_vtg failed vdd rc = %d\n", ret);
		}
	}
#endif

	/* hardware reset */
	aw22xxx_hw_reset(aw22xxx);

	/* aw22xxx chip id */
	ret = aw22xxx_read_chipid(aw22xxx);
	if (ret < 0) {
		pr_err("%s: aw22xxx_read_chipid failed ret=%d\n", __func__, ret);
		goto err_id;
	}

	/* aw22xxx irq */
	if (gpio_is_valid(aw22xxx->irq_gpio) &&
		!(aw22xxx->flags & AW22XXX_FLAG_SKIP_INTERRUPTS)) {
		/* register irq handler */
		aw22xxx_interrupt_setup(aw22xxx);
		irq_flags = IRQF_TRIGGER_FALLING | IRQF_ONESHOT;
		ret = devm_request_threaded_irq(&i2c->dev,
					gpio_to_irq(aw22xxx->irq_gpio),
					NULL, aw22xxx_irq, irq_flags,
					"aw22xxx", aw22xxx);
		if (ret != 0) {
			pr_err("%s: failed to request IRQ %d: %d\n",
					__func__, gpio_to_irq(aw22xxx->irq_gpio), ret);
			goto err_irq;
		}
	} else {
		pr_info("%s skipping IRQ registration\n", __func__);
		/* disable feature support if gpio was invalid */
		aw22xxx->flags |= AW22XXX_FLAG_SKIP_INTERRUPTS;
	}

	dev_set_drvdata(&i2c->dev, aw22xxx);

	aw22xxx_parse_led_cdev(aw22xxx, np);
	if (ret < 0) {
		pr_err("%s error creating led class dev\n", __func__);
		goto err_sysfs;
	}

	aw22xxx_fw_init(aw22xxx);


	pr_info("%s probe completed successfully!\n", __func__);

	return 0;

err_sysfs:
	devm_free_irq(&i2c->dev, gpio_to_irq(aw22xxx->irq_gpio), aw22xxx);
err_irq:
err_id:
	if (gpio_is_valid(aw22xxx->reset_gpio))
		gpio_free(aw22xxx->reset_gpio);
	if (gpio_is_valid(aw22xxx->irq_gpio))
		gpio_free(aw22xxx->irq_gpio);
err_gpio_request:
err_parse_dt:
	devm_kfree(&i2c->dev, aw22xxx);
	aw22xxx = NULL;
	return ret;
}

static void aw22xxx_i2c_remove(struct i2c_client *i2c)
{
	struct aw22xxx *aw22xxx = i2c_get_clientdata(i2c);

	sysfs_remove_group(&aw22xxx->cdev.dev->kobj, &aw22xxx_attribute_group);
	led_classdev_unregister(&aw22xxx->cdev);

	devm_free_irq(&i2c->dev, gpio_to_irq(aw22xxx->irq_gpio), aw22xxx);

	if (gpio_is_valid(aw22xxx->reset_gpio))
		gpio_free(aw22xxx->reset_gpio);
	if (gpio_is_valid(aw22xxx->irq_gpio))
		gpio_free(aw22xxx->irq_gpio);

	devm_kfree(&i2c->dev, aw22xxx);
	aw22xxx = NULL;
}

static const struct i2c_device_id aw22xxx_i2c_id[] = {
	{ AW22XXX_I2C_NAME, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, aw22xxx_i2c_id);

static const struct of_device_id aw22xxx_dt_match[] = {
	{ .compatible = "awinic,aw22xxx_led" },
	{ },
};

static struct i2c_driver aw22xxx_i2c_driver = {
	.driver = {
		.name = AW22XXX_I2C_NAME,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(aw22xxx_dt_match),
	},
	.probe = aw22xxx_i2c_probe,
	.remove = aw22xxx_i2c_remove,
	.id_table = aw22xxx_i2c_id,
};


static int __init aw22xxx_i2c_init(void)
{
	int ret = 0;

	pr_info("aw22xxx driver version %s\n", AW22XXX_DRIVER_VERSION);

	ret = i2c_add_driver(&aw22xxx_i2c_driver);
	if (ret) {
		pr_err("fail to add aw22xxx device into i2c\n");
		return ret;
	}

	return 0;
}
module_init(aw22xxx_i2c_init);


static void __exit aw22xxx_i2c_exit(void)
{
	i2c_del_driver(&aw22xxx_i2c_driver);
}
module_exit(aw22xxx_i2c_exit);


MODULE_DESCRIPTION("AW22XXX LED Driver");
MODULE_LICENSE("GPL v2");
