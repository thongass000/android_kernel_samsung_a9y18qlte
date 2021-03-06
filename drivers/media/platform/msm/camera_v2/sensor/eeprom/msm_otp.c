/* Copyright (c) 2011-2014, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/lzo.h>
#include "msm_sd.h"
#include "msm_cci.h"
#include "msm_otp.h"
#include <media/msm_cam_sensor.h>
#ifdef CONFIG_S5K5E9YX
#include "s5k5e9yx_otp.h"
#endif
#ifdef CONFIG_S5K5E3YX
#include "s5k5e3yx_otp.h"
#endif

#define MSM_OTP_DEBUG 1

#undef CDBG
#ifdef MSM_OTP_DEBUG
#define CDBG(fmt, args...) pr_err(fmt, ##args)
#else
#define CDBG(fmt, args...) pr_debug(fmt, ##args)
#endif

#undef OTP_MMAP_DEBUG
#ifdef CONFIG_COMPAT
static struct v4l2_file_operations msm_otp_v4l2_subdev_fops;
#endif

#if defined(CONFIG_GET_FRONT_MODULE_ID_OTP)
extern uint8_t front_module_id[S5K5E9_OTP_MODULE_ID_SIZE + 1];
#endif
#if defined(CONFIG_GET_FRONT_SENSOR_ID)
extern uint8_t front_sensor_id[S5K5E9_OTP_SENSOR_ID_SIZE + 1];
#endif
#if defined(CONFIG_SEC_A9Y18QLTE_PROJECT)
extern uint8_t rear2_module_id[S5K5E9_OTP_MODULE_ID_SIZE + 1];
#endif

uint8_t *map_data = NULL;

struct msm_camera_i2c_reg_setting load_otp_setfile = {
#ifdef CONFIG_S5K5E9YX
	load_s5k5e9yx_otp_setfile_reg, sizeof(load_s5k5e9yx_otp_setfile_reg)/sizeof(struct msm_camera_i2c_reg_array), MSM_CAMERA_I2C_WORD_ADDR, MSM_CAMERA_I2C_BYTE_DATA, 50
#endif
#ifdef CONFIG_S5K5E3YX
	load_s5k5e3yx_otp_setfile_reg, sizeof(load_s5k5e3yx_otp_setfile_reg)/sizeof(struct msm_camera_i2c_reg_array), MSM_CAMERA_I2C_WORD_ADDR, MSM_CAMERA_I2C_BYTE_DATA, 10
#endif
};

struct msm_camera_i2c_reg_setting init_read_otp = {
#ifdef CONFIG_S5K5E9YX
	init_read_s5k5e9yx_otp_reg, sizeof(init_read_s5k5e9yx_otp_reg)/sizeof(struct msm_camera_i2c_reg_array), MSM_CAMERA_I2C_WORD_ADDR, MSM_CAMERA_I2C_BYTE_DATA, 10
#endif
#ifdef CONFIG_S5K5E3YX
	init_read_s5k5e3yx_otp_reg, sizeof(init_read_s5k5e3yx_otp_reg)/sizeof(struct msm_camera_i2c_reg_array), MSM_CAMERA_I2C_WORD_ADDR, MSM_CAMERA_I2C_BYTE_DATA, 10
#endif
};

struct msm_camera_i2c_reg_setting finish_read_otp = {
#ifdef CONFIG_S5K5E9YX
	finish_read_s5k5e9yx_otp_reg, sizeof(finish_read_s5k5e9yx_otp_reg)/sizeof(struct msm_camera_i2c_reg_array), MSM_CAMERA_I2C_WORD_ADDR, MSM_CAMERA_I2C_BYTE_DATA, 10
#endif
#ifdef CONFIG_S5K5E3YX
	finish_read_s5k5e3yx_otp_reg, sizeof(finish_read_s5k5e3yx_otp_reg)/sizeof(struct msm_camera_i2c_reg_array), MSM_CAMERA_I2C_WORD_ADDR, MSM_CAMERA_I2C_BYTE_DATA, 10
#endif
};

struct msm_camera_i2c_reg_setting init_write_otp = {
#ifdef CONFIG_S5K5E9YX
	init_write_s5k5e9yx_otp_reg, sizeof(init_write_s5k5e9yx_otp_reg)/sizeof(struct msm_camera_i2c_reg_array), MSM_CAMERA_I2C_WORD_ADDR, MSM_CAMERA_I2C_BYTE_DATA, 10
#endif
#ifdef CONFIG_S5K5E3YX
	init_write_s5k5e3yx_otp_reg, sizeof(init_write_s5k5e3yx_otp_reg)/sizeof(struct msm_camera_i2c_reg_array), MSM_CAMERA_I2C_WORD_ADDR, MSM_CAMERA_I2C_BYTE_DATA, 10
#endif
};

struct msm_camera_i2c_reg_setting finish_write_otp = {
#ifdef CONFIG_S5K5E9YX
	finish_write_s5k5e9yx_otp_reg, sizeof(finish_write_s5k5e9yx_otp_reg)/sizeof(struct msm_camera_i2c_reg_array), MSM_CAMERA_I2C_WORD_ADDR, MSM_CAMERA_I2C_BYTE_DATA, 10
#endif
#ifdef CONFIG_S5K5E3YX
	finish_write_s5k5e3yx_otp_reg, sizeof(finish_write_s5k5e3yx_otp_reg)/sizeof(struct msm_camera_i2c_reg_array), MSM_CAMERA_I2C_WORD_ADDR, MSM_CAMERA_I2C_BYTE_DATA, 10
#endif
};

DEFINE_MSM_MUTEX(msm_otp_mutex);
static int msm_otp_match_id(struct msm_otp_ctrl_t *e_ctrl);
static int read_otp_memory(struct msm_otp_ctrl_t *e_ctrl,
					struct msm_eeprom_memory_block_t *block);
static int msm_otp_get_dt_data(struct msm_otp_ctrl_t *e_ctrl);

/**
  * msm_otp_verify_sum - verify crc32 checksum
  * @mem:	data buffer
  * @size:	size of data buffer
  * @sum:	expected checksum
  *
  * Returns 0 if checksum match, -EINVAL otherwise.
  */
static int msm_otp_verify_sum(const char *mem, uint32_t size, uint32_t sum)
{
	uint32_t crc = ~0;

	/* check overflow */
	if (size > crc - sizeof(uint32_t))
		return -EINVAL;

	crc = crc32_le(crc, mem, size);
	if (~crc != sum) {
		pr_err("%s: expect 0x%x, result 0x%x\n", __func__, sum, ~crc);
		pr_err("Check otp or interface");
		return -EINVAL;
	}
	CDBG("%s: checksum pass 0x%x\n", __func__, sum);

	return 0;
}

/**
  * msm_otp_match_crc - verify multiple regions using crc
  * @data:	data block to be verified
  *
  * Iterates through all regions stored in @data.  Regions with odd index
  * are treated as data, and its next region is treated as checksum.  Thus
  * regions of even index must have valid_size of 4 or 0 (skip verification).
  * Returns a bitmask of verified regions, starting from LSB.  1 indicates
  * a checksum match, while 0 indicates checksum mismatch or not verified.
  */
static uint32_t msm_otp_match_crc(struct msm_eeprom_memory_block_t *data)
{
	int j, rc;
	uint32_t *sum;
	uint32_t ret = 0;
	uint8_t *memptr, *memptr_crc;
	struct msm_eeprom_memory_map_t *map;

	if (!data) {
		pr_err("%s data is NULL\n", __func__);
		return -EINVAL;
	}
	map = data->map;

	for (j = 0; j + 1 < data->num_map; j += 2) {
		memptr = data->mapdata + map[j].mem.addr;
		memptr_crc = data->mapdata + map[j+1].mem.addr;
		
		/* empty table or no checksum */
		if (!map[j].mem.valid_size || !map[j+1].mem.valid_size) {
			continue;
		}

		if (map[j+1].mem.valid_size != sizeof(uint32_t)) {
			pr_err("%s: malformatted data mapping\n", __func__);
			return -EINVAL;
		}
		
		sum = (uint32_t *) (memptr_crc);
		rc = msm_otp_verify_sum(memptr, map[j].mem.valid_size, *sum);
		if (!rc) {
			ret |= 1 << (j/2);
		}
	}

	return ret;
}

static int msm_otp_get_cmm_data(struct msm_otp_ctrl_t *e_ctrl,
				struct msm_eeprom_cfg_data *cdata)
{
	int rc = 0;
	struct msm_eeprom_cmm_t *cmm_data = &e_ctrl->eboard_info->cmm_data;
	
	cdata->cfg.get_cmm_data.cmm_support = cmm_data->cmm_support;
	cdata->cfg.get_cmm_data.cmm_compression = cmm_data->cmm_compression;
	cdata->cfg.get_cmm_data.cmm_size = cmm_data->cmm_size;

	return rc;
}

/**
  * msm_otp_power_up() - power up otp if it's not on
  * @e_ctrl:	control struct
  * @down:	output to indicate whether power down is needed later
  *
  * This function powers up OTP only if it's not already on.  If power
  * up is performed here, @down will be set to true.  Caller should power
  * down OTP after transaction if @down is true.
  */
static int msm_otp_power_up(struct msm_otp_ctrl_t *e_ctrl, bool *down)
{
	int rc = 0;

	if (e_ctrl->otp_device_type == MSM_CAMERA_SPI_DEVICE)
		rc = msm_otp_match_id(e_ctrl);
	
	pr_warn("%s : E", __func__);
	
	if (rc < 0) {
		if (down)
			*down = true;
		
		rc = msm_camera_power_up (&e_ctrl->eboard_info->power_info, 
							e_ctrl->otp_device_type, &e_ctrl->i2c_client,
							false, SUB_DEVICE_TYPE_EEPROM);
	} else {
		if (down)
			*down = false;
	}
	
	return rc;
}

/**
  * msm_otp_power_down() - power down otp
  * @e_ctrl:	control struct
  * @down:	indicate whether kernel powered up otp before
  *
  * This function powers down OTP only if it's powered on by calling
  * msm_otp_power_up() before.  If @down is false, no action will be
  * taken.  Otherwise, otp will be powered down.
  */
static int msm_otp_power_down(struct msm_otp_ctrl_t *e_ctrl, bool down)
{
	int rc = 0;

	pr_warn("%s : E", __func__);

	if (down) {
		return rc = msm_camera_power_down (&e_ctrl->eboard_info->power_info,
									e_ctrl->otp_device_type, &e_ctrl->i2c_client,
									false, SUB_DEVICE_TYPE_EEPROM);
	} else {
		return 0;
	}
}

static int otp_config_read_cal_data(struct msm_otp_ctrl_t *e_ctrl,
				struct msm_eeprom_cfg_data *cdata)
{
	int rc;

	/* check range */
	if ((cdata->cfg.read_data.num_bytes > e_ctrl->cal_data.num_data) ||
			(cdata->cfg.read_data.addr > e_ctrl->cal_data.num_data)) {
		pr_err("%s: Invalid size or addr. exp %u, req %u, addr %x\n", __func__,
			e_ctrl->cal_data.num_data, cdata->cfg.read_data.num_bytes,
			cdata->cfg.read_data.addr);
		return -EINVAL;
	}

	if (!e_ctrl->cal_data.mapdata) {
		pr_err("%s : is NULL", __func__);
		return -EFAULT;
	}

	CDBG("%s:%d: OTP subdevid: %d mapdata %p", __func__, __LINE__,
		e_ctrl->subdev_id, e_ctrl->cal_data.mapdata);
	CDBG("%s:%d: OTP addr: %x num_bytes 0x%d", __func__, __LINE__,
		cdata->cfg.read_data.addr, cdata->cfg.read_data.num_bytes);
	rc = copy_to_user (cdata->cfg.read_data.dbuffer,
				e_ctrl->cal_data.mapdata + cdata->cfg.read_data.addr,
				cdata->cfg.read_data.num_bytes);

	return rc;
}

static int otp_config_read_data(struct msm_otp_ctrl_t *e_ctrl,
				struct msm_eeprom_cfg_data *cdata)
{
	char *buf;
	int rc = 0;

	buf = kmalloc(cdata->cfg.read_data.num_bytes, GFP_KERNEL);
	if (!buf) {
		pr_err("%s : buf is NULL", __func__);
		return -ENOMEM;
	}

	memcpy (buf, &map_data[(cdata->cfg.read_data.addr)],
			cdata->cfg.read_data.num_bytes);
	rc = copy_to_user(cdata->cfg.read_data.dbuffer, buf,
			cdata->cfg.read_data.num_bytes);
	kfree(buf);

	return rc;
}

static int otp_config_read_fw_version(struct msm_otp_ctrl_t *e_ctrl,
				struct msm_eeprom_cfg_data *cdata)
{
	char *buf;
	int rc = 0;

	buf = kmalloc(cdata->cfg.read_data.num_bytes, GFP_KERNEL);
	if (!buf) {
		pr_err("%s : buf is NULL", __func__);
		return -ENOMEM;
	}

	memset(buf, 0, cdata->cfg.read_data.num_bytes);

	if (e_ctrl->cal_data.num_data < (cdata->cfg.read_data.addr + cdata->cfg.read_data.num_bytes)) {
		pr_err("%s : requested data size was mismatched! addr:size[0x%x:%d]\n", __func__,
			cdata->cfg.read_data.addr, cdata->cfg.read_data.num_bytes);

		rc = -ENOMEM;
		goto FREE;
	}

	memcpy (buf, &map_data[cdata->cfg.read_data.addr], cdata->cfg.read_data.num_bytes);
	rc = copy_to_user(cdata->cfg.read_data.dbuffer, buf, cdata->cfg.read_data.num_bytes);

FREE:
	kfree(buf);
	return rc;
}

static int otp_config_read_compressed_data(struct msm_otp_ctrl_t *e_ctrl,
				struct msm_eeprom_cfg_data *cdata)
{
	int rc = 0;
	uint8_t *buf_comp = NULL;
	uint8_t *buf_decomp = NULL;
	size_t decomp_size;
	uint16_t data;
	int i;

	pr_err("%s: address (0x%x) comp_size (%d) after decomp (%d)", __func__,
		cdata->cfg.read_data.addr, cdata->cfg.read_data.comp_size, cdata->cfg.read_data.num_bytes);

	buf_comp = kmalloc(cdata->cfg.read_data.comp_size, GFP_KERNEL);
	buf_decomp = kmalloc(cdata->cfg.read_data.num_bytes, GFP_KERNEL);
	if (!buf_decomp || !buf_comp) {
		pr_err("%s: kmalloc fail", __func__);
		rc = -ENOMEM;
		goto FREE;
	}

	rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
			&(e_ctrl->i2c_client), &init_read_otp);
	if (rc < 0) {
		pr_err("%s:(%d) init_read_otp failed\n", __func__, __LINE__);
		goto POWER_DOWN;
	}

	for (i = 0; i<cdata->cfg.read_data.comp_size; i++) {
		rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_read(
				&(e_ctrl->i2c_client), cdata->cfg.read_data.addr+i,
				&data, MSM_CAMERA_I2C_BYTE_DATA);
		if (rc < 0) {
			pr_err("%s:(%d) read failed\n", __func__, __LINE__);
			goto POWER_DOWN;
		}

		buf_comp[i] = data;
	}

	rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
			&(e_ctrl->i2c_client), &finish_read_otp);
	if (rc < 0) {
		pr_err("%s:(%d) finish_read_otp failed\n", __func__, __LINE__);
		goto POWER_DOWN;
	}

	pr_err ("%s: crc = 0x%08X\n", __func__,
		*(uint32_t*)&buf_comp[cdata->cfg.read_data.comp_size - 4]);

	//  compressed data(buf_comp) contains uncompressed crc32 value.
	rc = msm_otp_verify_sum(buf_comp, cdata->cfg.read_data.comp_size - 4,
		*(uint32_t*)&buf_comp[cdata->cfg.read_data.comp_size-4]);
	if (rc < 0) {
		pr_err("%s: crc check error, rc %d\n", __func__, rc);
		goto POWER_DOWN;
	}

	decomp_size = cdata->cfg.read_data.num_bytes;
	rc = lzo1x_decompress_safe(buf_comp, cdata->cfg.read_data.comp_size - 4,
			buf_decomp, &decomp_size);
	if (rc != LZO_E_OK) {
		pr_err("%s: decompression failed %d", __func__, rc);
		goto POWER_DOWN;
	}

	rc = copy_to_user(cdata->cfg.read_data.dbuffer, buf_decomp, decomp_size);
	if (rc < 0) {
		pr_err("%s: failed to copy to user\n", __func__);
		goto POWER_DOWN;
	}

POWER_DOWN:
FREE:
	if (buf_comp)
		kfree(buf_comp);

	if (buf_decomp)
		kfree(buf_decomp);

	return rc;
}

static int otp_config_write_data(struct msm_otp_ctrl_t *e_ctrl,
				struct msm_eeprom_cfg_data *cdata)
{
	int rc = 0;
	char *buf = NULL;
	bool down;
	void *work_mem = NULL;
	uint8_t *compressed_buf = NULL;
	size_t compressed_size = 0;
	uint32_t crc = ~0;
	int i;

	pr_warn("%s: compress ? %d size %d", __func__,
		cdata->cfg.write_data.compress, cdata->cfg.write_data.num_bytes);

	buf = kmalloc(cdata->cfg.write_data.num_bytes, GFP_KERNEL);
	if (!buf) {
		pr_err("%s: allocation failed 1", __func__);
		return -ENOMEM;
	}

	rc = copy_from_user(buf, cdata->cfg.write_data.dbuffer,
			cdata->cfg.write_data.num_bytes);
	if (rc < 0) {
		pr_err("%s: failed to copy write data\n", __func__);
		goto FREE;
	}

	/* compress */
	if (cdata->cfg.write_data.compress) {
		compressed_buf = kmalloc(cdata->cfg.write_data.num_bytes +
			cdata->cfg.write_data.num_bytes / 16 + 64 + 3, GFP_KERNEL);

		if (!compressed_buf) {
			pr_err("%s: allocation failed 2", __func__);
			rc = -ENOMEM;
			goto FREE;
		}

		work_mem = kmalloc(LZO1X_1_MEM_COMPRESS, GFP_KERNEL);
		if (!work_mem) {
			pr_err("%s: allocation failed 3", __func__);
			rc = -ENOMEM;
			goto FREE;
		}

		if (lzo1x_1_compress(buf, cdata->cfg.write_data.num_bytes,
				compressed_buf, &compressed_size, work_mem) != LZO_E_OK) {
			pr_err("%s: compression failed", __func__);
			goto FREE;
		}

		crc = crc32_le(crc, compressed_buf, compressed_size);
		crc = ~crc;

		pr_err("%s: compressed size %d, crc=0x%0X \n", __func__, (uint32_t)compressed_size, crc);
		*cdata->cfg.write_data.write_size = compressed_size + 4;  //  include CRC size
	}

	rc = msm_otp_power_up (e_ctrl, &down);
	if (rc < 0) {
		pr_err("%s: failed to power on otp\n", __func__);
		goto FREE;
	}

	if (cdata->cfg.write_data.compress) {
		rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
				&(e_ctrl->i2c_client), &init_write_otp);
		if (rc < 0) {
			pr_err("%s:(%d) init_write_otp failed\n", __func__, __LINE__);
			goto POWER_DOWN;
		}

		for (i = 0; i < compressed_size; i++) {
			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
					&(e_ctrl->i2c_client), cdata->cfg.write_data.addr + i,
					compressed_buf[i], MSM_CAMERA_I2C_BYTE_DATA);
			if (rc < 0) {
				pr_err("%s:(%d) write failed\n", __func__, __LINE__);
				goto POWER_DOWN;
			}
		}

		rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
				&(e_ctrl->i2c_client), &init_write_otp);
		if (rc < 0) {
			pr_err("%s:(%d) init_write_otp failed\n", __func__, __LINE__);
			goto POWER_DOWN;
		}

		//  write CRC32 for compressed data
		for (i = 0; i < 4; i++) {
			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
					&(e_ctrl->i2c_client),
					cdata->cfg.write_data.addr+compressed_size + i,
					buf[i], MSM_CAMERA_I2C_BYTE_DATA);
			if (rc < 0) {
				pr_err("%s:(%d) write failed\n", __func__, __LINE__);
				goto POWER_DOWN;
			}
		}

		rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
				&(e_ctrl->i2c_client), &finish_write_otp);
		if (rc < 0) {
			pr_err("%s:(%d) finish_write_otp failed\n", __func__, __LINE__);
			goto POWER_DOWN;
		}
	} else {
		rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
				&(e_ctrl->i2c_client), &init_write_otp);
		if (rc < 0) {
			pr_err("%s:(%d) init_write_otp failed\n", __func__, __LINE__);
			goto POWER_DOWN;
		}

		for (i = 0; i < cdata->cfg.write_data.num_bytes; i++) {
			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
					&(e_ctrl->i2c_client), cdata->cfg.write_data.addr + i,
					buf[i], MSM_CAMERA_I2C_BYTE_DATA);
			if (rc < 0) {
				pr_err("%s:(%d) write failed\n", __func__, __LINE__);
				goto POWER_DOWN;
			}
		}

		rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
				&(e_ctrl->i2c_client), &finish_write_otp);
		if (rc < 0) {
			pr_err("%s:(%d) finish_write_otp failed\n", __func__, __LINE__);
			goto POWER_DOWN;
		}
	}

	if (rc < 0) {
		pr_err("%s: failed to write data, rc %d\n", __func__, rc);
		goto POWER_DOWN;
	}

	CDBG("%s: done", __func__);

POWER_DOWN:
	msm_otp_power_down(e_ctrl, down);

FREE:
	if (buf)
		kfree(buf);

	if (compressed_buf)
		kfree(compressed_buf);

	if (work_mem)
		kfree(work_mem);

	return rc;
}

static int otp_config_erase(struct msm_otp_ctrl_t *e_ctrl,
				struct msm_eeprom_cfg_data *cdata)
{
	int rc = 0;

	return rc;
}

static int32_t msm_otp_read_otp_data(struct msm_otp_ctrl_t *e_ctrl)
{
	int32_t rc = 0;

	CDBG("%s:%d Enter\n", __func__, __LINE__);

	/* check otp id */
	if (e_ctrl->otp_device_type == MSM_CAMERA_SPI_DEVICE) {
		rc = msm_otp_match_id(e_ctrl);
		if (rc < 0) {
			CDBG("%s: otp not matching %d\n", __func__, rc);
			return rc;
		}
	}

	/* read otp */
	if (e_ctrl->cal_data.map) {
		int i;
		int normal_crc_value = 0;

		normal_crc_value = 0;

		for (i = 0; i < e_ctrl->cal_data.num_map>>1; i ++)
			normal_crc_value |= (1 << i);

		CDBG("num_map = %d, Normal CRC value = 0x%X\n",
			e_ctrl->cal_data.num_map, normal_crc_value);

		/* power up otp for reading */
		rc = msm_camera_power_up (&e_ctrl->eboard_info->power_info,
				e_ctrl->otp_device_type, &e_ctrl->i2c_client,
				false, SUB_DEVICE_TYPE_EEPROM);
		if (rc < 0) {
			pr_err("failed rc %d\n", rc);
			return rc;
		}

		rc = read_otp_memory(e_ctrl, &e_ctrl->cal_data);
		if (rc < 0) {
			pr_err("%s: read cal data failed\n", __func__);
			goto POWER_DOWN;
		}

		e_ctrl->is_supported |= msm_otp_match_crc (&e_ctrl->cal_data);

		if (e_ctrl->is_supported != normal_crc_value) {
			pr_err("%s : any CRC value(s) are not matched.\n", __func__);
		} else {
			pr_err("%s : All CRC values are matched.\n", __func__);
		}
	}

	e_ctrl->is_supported = (e_ctrl->is_supported << 1) | 1;

	CDBG("%s:%d reloaded - is_supported : 0x%04X X\n",
		__func__, __LINE__, e_ctrl->is_supported);

POWER_DOWN:
	/* power down */
	if (msm_camera_power_down (&e_ctrl->eboard_info->power_info,
			e_ctrl->otp_device_type, &e_ctrl->i2c_client,
			false, SUB_DEVICE_TYPE_EEPROM) < 0) {
		pr_err("%s:%d power down failed\n", __func__, __LINE__);
	}
	pr_err("%s:%d Exit\n", __func__, __LINE__);

	return rc;
}

static int msm_otp_config(struct msm_otp_ctrl_t *e_ctrl, void __user *argp)
{
	struct msm_eeprom_cfg_data *cdata = (struct msm_eeprom_cfg_data *)argp;
	int rc = 0;

	CDBG("%s:%d: subdevid: %d, cfgtype: %d\n", __func__, __LINE__,
		e_ctrl->subdev_id, cdata->cfgtype);

	switch (cdata->cfgtype) {
	case CFG_EEPROM_GET_INFO:
		CDBG("%s E CFG_OTP_GET_INFO\n", __func__);
		cdata->is_supported = e_ctrl->is_supported;
		memcpy (cdata->cfg.eeprom_name,	e_ctrl->eboard_info->eeprom_name,
			sizeof(cdata->cfg.eeprom_name));
		break;

	case CFG_EEPROM_GET_CAL_DATA:
		CDBG("%s E CFG_OTP_GET_CAL_DATA\n", __func__);
		cdata->cfg.get_data.num_bytes = e_ctrl->cal_data.num_data;
		break;

	case CFG_EEPROM_READ_CAL_DATA:
		CDBG("%s E CFG_OTP_READ_CAL_DATA\n", __func__);
		rc = otp_config_read_cal_data(e_ctrl, cdata);
		break;

	case CFG_EEPROM_READ_DATA:
		CDBG("%s E CFG_OTP_READ_DATA\n", __func__);
		rc = otp_config_read_data(e_ctrl, cdata);
		break;

	case CFG_EEPROM_READ_COMPRESSED_DATA:
		CDBG("%s E CFG_OTP_READ_COMPRESSED_DATA\n", __func__);
		rc = otp_config_read_compressed_data(e_ctrl, cdata);
		if (rc < 0)
			pr_err("%s : otp_config_read_compressed_data failed", __func__);
		break;

	case CFG_EEPROM_WRITE_DATA:
		pr_warn("%s E CFG_OTP_WRITE_DATA\n", __func__);
		rc = otp_config_write_data(e_ctrl, cdata);
		break;

	case CFG_EEPROM_READ_DATA_FROM_HW:
		CDBG("%s E CFG_OTP_READ_DATA_FROM_HW\n", __func__);
		e_ctrl->is_supported = 0x01;

		pr_err ("kernel is_supported before : 0x%04X\n", e_ctrl->is_supported);
		rc = msm_otp_read_otp_data(e_ctrl);
		pr_err ("kernel is_supported after : 0x%04X\n", e_ctrl->is_supported);

		cdata->is_supported = e_ctrl->is_supported;
		if (rc < 0) {
			pr_err("%s:%d failed rc %d\n", __func__, __LINE__,  rc);
			break;
		}
		break;

	case CFG_EEPROM_GET_MM_INFO:
		CDBG("%s E CFG_OTP_GET_MM_INFO\n", __func__);
		rc = msm_otp_get_cmm_data(e_ctrl, cdata);
		break;

	case CFG_EEPROM_ERASE:
		CDBG("%s E CFG_OTP_ERASE\n", __func__);
		rc = otp_config_erase(e_ctrl, cdata);
		break;

	case CFG_EEPROM_POWER_ON:
		CDBG("%s E CFG_OTP_POWER_ON\n", __func__);
		rc = msm_otp_power_up(e_ctrl, NULL);
		if (rc < 0)
			pr_err("%s : msm_OTP_power_up failed", __func__);
		break;

	case CFG_EEPROM_POWER_OFF:
		CDBG("%s E CFG_OTP_POWER_OFF\n", __func__);
		rc = msm_otp_power_down(e_ctrl, true);
		if (rc < 0)
			pr_err("%s : msm_OTP_power_down failed", __func__);
		break;

	case CFG_EEPROM_GET_FW_VERSION_INFO:
		CDBG("%s E CFG_OTP_GET_FW_VERSION_INFO\n", __func__);
		rc = otp_config_read_fw_version(e_ctrl, cdata);
		break;

	default:
		break;
	}

	pr_err("%s X rc: %d\n", __func__, rc);
	return rc;
}

static int msm_otp_get_subdev_id (struct msm_otp_ctrl_t *e_ctrl, void *arg)
{
	uint32_t *subdev_id = (uint32_t *)arg;

	CDBG("%s E\n", __func__);

	if (!subdev_id) {
		pr_err("%s failed\n", __func__);
		return -EINVAL;
	}

	*subdev_id = e_ctrl->subdev_id;
	CDBG("subdev_id %d\n", *subdev_id);
	CDBG("%s X\n", __func__);

	return 0;
}

static long msm_otp_subdev_ioctl(struct v4l2_subdev *sd, unsigned int cmd, void *arg)
{
	struct msm_otp_ctrl_t *e_ctrl = v4l2_get_subdevdata(sd);
	void __user *argp = (void __user *)arg;

	CDBG("%s E\n", __func__);
	CDBG("%s:%d a_ctrl %p argp %p\n", __func__, __LINE__, e_ctrl, argp);

	switch (cmd) {
	case VIDIOC_MSM_SENSOR_GET_SUBDEV_ID:
		return msm_otp_get_subdev_id(e_ctrl, argp);

	case VIDIOC_MSM_EEPROM_CFG:
		return msm_otp_config(e_ctrl, argp);

	default:
		return -ENOIOCTLCMD;
	}

	pr_err("%s X\n", __func__);
}

static struct msm_camera_i2c_fn_t msm_otp_cci_func_tbl = {
	.i2c_read = msm_camera_cci_i2c_read,
	.i2c_read_seq = msm_camera_cci_i2c_read_seq,
	.i2c_write = msm_camera_cci_i2c_write,
	.i2c_write_seq = msm_camera_cci_i2c_write_seq,
	.i2c_write_table = msm_camera_cci_i2c_write_table,
	.i2c_write_seq_table = msm_camera_cci_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =	msm_camera_cci_i2c_write_table_w_microdelay,
	.i2c_util = msm_sensor_cci_i2c_util,
	.i2c_poll = msm_camera_cci_i2c_poll,
};

static struct msm_camera_i2c_fn_t msm_otp_qup_func_tbl = {
	.i2c_read = msm_camera_qup_i2c_read,
	.i2c_read_seq = msm_camera_qup_i2c_read_seq,
	.i2c_write = msm_camera_qup_i2c_write,
	.i2c_write_table = msm_camera_qup_i2c_write_table,
	.i2c_write_seq_table = msm_camera_qup_i2c_write_seq_table,
	.i2c_write_table_w_microdelay =	msm_camera_qup_i2c_write_table_w_microdelay,
};

static struct msm_camera_i2c_fn_t msm_otp_spi_func_tbl = {
	.i2c_read = msm_camera_spi_read,
	.i2c_read_seq = msm_camera_spi_read_seq,
};

static int msm_otp_open(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rc = 0;
	struct msm_otp_ctrl_t *e_ctrl =  v4l2_get_subdevdata(sd);

	CDBG("%s E\n", __func__);

	if (!e_ctrl) {
		pr_err("%s failed e_ctrl is NULL\n", __func__);
		return -EINVAL;
	}

	CDBG("%s X\n", __func__);
	return rc;
}

static int msm_otp_close(struct v4l2_subdev *sd, struct v4l2_subdev_fh *fh)
{
	int rc = 0;
	struct msm_otp_ctrl_t *e_ctrl =  v4l2_get_subdevdata(sd);

	CDBG("%s E\n", __func__);

	if (!e_ctrl) {
		pr_err("%s failed e_ctrl is NULL\n", __func__);
		return -EINVAL;
	}

	CDBG("%s X\n", __func__);
	return rc;
}

static const struct v4l2_subdev_internal_ops msm_otp_internal_ops = {
	.open = msm_otp_open,
	.close = msm_otp_close,
};

/**
  * read_otp_memory() - read map data into buffer
  * @e_ctrl:	otp control struct
  * @block:	block to be read
  *
  * This function iterates through blocks stored in block->map, reads each
  * region and concatenate them into the pre-allocated block->mapdata
  */
static int read_otp_memory(struct msm_otp_ctrl_t *e_ctrl,
				struct msm_eeprom_memory_block_t *block)
{
	int rc = 0;
	struct msm_eeprom_memory_map_t *emap = block->map;
	struct msm_eeprom_board_info *eb_info;
	uint8_t *memptr = block->mapdata;
	enum msm_camera_i2c_data_type data_type = MSM_CAMERA_I2C_BYTE_DATA;
	uint16_t OTP_Bank = 0;
	uint16_t start_addr, end_addr;
	uint8_t page;
	int i, j;
#ifdef CONFIG_S5K5E3YX
	uint16_t OTP_Data=0;
#endif
#ifdef CONFIG_S5K5E9YX
	int read_bytes = 0;
	int total_bytes_to_read = 0;
	int next_page_count = 0;
#endif

	if (!e_ctrl) {
		pr_err("%s e_ctrl is NULL", __func__);
		return -EINVAL;
	}

	eb_info = e_ctrl->eboard_info;
	rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
			&(e_ctrl->i2c_client), &load_otp_setfile);
	if (rc < 0) {
		pr_err("%s:(%d) load_otp_setfile failed\n", __func__, __LINE__);
		return rc;
	}

	rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
			&(e_ctrl->i2c_client), &init_read_otp);
	if (rc < 0) {
		pr_err("%s:(%d) init_read_otp failed\n", __func__, __LINE__);
		return rc;
	}

	rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_read(
#ifdef CONFIG_S5K5E9YX
		&(e_ctrl->i2c_client), S5K5E9_OTP_PAGE_START_REGISTER,
#else
		&(e_ctrl->i2c_client), START_ADDR_FOR_S5K5E3_OTP,
#endif
		&OTP_Bank, data_type);
	if (rc < 0) {
		pr_err("%s:(%d) read failed\n", __func__, __LINE__);
		return rc;
	}

	memptr[0] = OTP_Bank;
	pr_info("%s:%d read OTP_Bank: %d\n", __func__, __LINE__, OTP_Bank);

	switch (OTP_Bank) {
		// Refer to OTP document
#ifdef CONFIG_S5K5E9YX
		case 0:
		case 1:
			page = 41;
			break;

		case 3:
			page = 22;
			break;

		case 7:
			page = 27;
			break;

		case 0xF:
			page = 32;
			break;

		case 0x1F:
			page = 37;
			break;
#else
		case 0:
		case 1:
			page = 2;
			break;

		case 3:
			page = 3;
			break;

		case 7:
			page = 4;
			break;

		case 0xF:
			page = 5;
			break;
#endif
		default:
			pr_err("%s: Bank error : Bank(%d)\n", __func__, OTP_Bank);
			return -EINVAL;
	}

	pr_info("%s:%d read page: %d\n", __func__, __LINE__, page);

#ifdef CONFIG_S5K5E9YX
	init_read_otp.reg_setting[1].reg_data = page;
	init_write_otp.reg_setting[7].reg_data = page;
#endif
#ifdef CONFIG_S5K5E3YX
	init_read_otp.reg_setting[1].reg_data = page;
	init_write_otp.reg_setting[7].reg_data = page;
#endif

	block->mapdata[0] = page;

	for (j = 0; j < block->num_map; j++) {
		if (emap[j].mem.data_t == 0)
			continue;

		memptr = block->mapdata + emap[j].mem.addr;

		pr_err("%s: %d addr = 0x%X, size = %d\n", __func__, __LINE__,
			emap[j].mem.addr, emap[j].mem.valid_size);

		if (emap[j].saddr.addr) {
			eb_info->i2c_slaveaddr = emap[j].saddr.addr;
			e_ctrl->i2c_client.cci_client->sid = eb_info->i2c_slaveaddr >> 1;
			pr_err ("qcom,slave-addr = 0x%X\n", eb_info->i2c_slaveaddr);
		}

		if (emap[j].mem.valid_size) {
			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
					&(e_ctrl->i2c_client), 0x0A00,
					0x04, data_type);
			if (rc < 0) {
				pr_err("%s:(%d) write initial state failed\n", __func__, __LINE__);
				return rc;
			}

			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
				&(e_ctrl->i2c_client), 0x0A02, page, data_type);
			if (rc < 0) {
				pr_err("%s:(%d) write page failed\n", __func__, __LINE__);
				return rc;
			}

			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
				&(e_ctrl->i2c_client), 0x0A00, 0x01, data_type);
			if (rc < 0) {
				pr_err("%s:(%d) set read mode failed\n", __func__, __LINE__);
				return rc;
			}

#ifdef CONFIG_S5K5E9YX
			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_poll(
					&(e_ctrl->i2c_client), S5K5E9_OTP_ERROR_FLAG_REGISTER,
					0x01, MSM_CAMERA_I2C_BYTE_DATA, 1);
			if (rc < 0) {
				pr_err("%s:(%d) pool read byte failed\n", __func__, __LINE__);
				return rc;
			}

			start_addr = S5K5E9_OTP_PAGE_START_REGISTER + emap[j].mem.addr;

			while (start_addr > S5K5E9_OTP_PAGE_END_REGISTER) {
				start_addr -= S5K5E9_OTP_PAGE_SIZE;
			}
#endif
#ifdef CONFIG_S5K5E3YX
			start_addr = START_ADDR_FOR_S5K5E3_OTP + emap[j].mem.addr;
#endif
			end_addr = start_addr + emap[j].mem.valid_size;

			pr_err("%s: %d page %d start_addr = 0x%X\n",
				__func__, __LINE__, page, start_addr);

#ifdef CONFIG_S5K5E9YX
			total_bytes_to_read = emap[j].mem.valid_size;
			read_bytes = S5K5E9_OTP_PAGE_SIZE - emap[j].mem.addr;

			while (read_bytes < 0) {
				read_bytes += S5K5E9_OTP_PAGE_SIZE;
			}

			while (total_bytes_to_read > 0) {
				pr_err("%s: %d page_cnt [%d] read_bytes : %d \n",
					__func__, __LINE__, next_page_count, read_bytes);

				rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_read_seq(
						&(e_ctrl->i2c_client), start_addr, memptr, read_bytes);
				if (rc < 0) {
					pr_err("%s:(%d) read failed\n", __func__, __LINE__);
					return rc;
				}

				start_addr = S5K5E9_OTP_PAGE_START_REGISTER;
				total_bytes_to_read -= read_bytes;
				memptr += read_bytes;

				if (total_bytes_to_read < S5K5E9_OTP_PAGE_SIZE) {
					read_bytes = total_bytes_to_read;
				} else {
					read_bytes = S5K5E9_OTP_PAGE_SIZE;
				}

				if (total_bytes_to_read > 0) {
					rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
							&(e_ctrl->i2c_client), &finish_read_otp);
					if (rc < 0) {
						pr_err("%s:(%d) finish_read_otp failed\n", __func__, __LINE__);
						return rc;
					}

					next_page_count++;

					rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
							&(e_ctrl->i2c_client), 0x0A02, page + next_page_count,
							data_type);
					if (rc < 0) {
						pr_err("%s:(%d) write page failed\n", __func__, __LINE__);
						return rc;
					}

					rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write(
							&(e_ctrl->i2c_client), 0x0A00, 0x01, data_type);
					if (rc < 0) {
						pr_err("%s:(%d) set read mode failed\n", __func__, __LINE__);
						return rc;
					}
				}
			}

			page += next_page_count;
#else
			for (i = start_addr; i < end_addr; i++) {
				rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_read(
					&(e_ctrl->i2c_client), i, &OTP_Data, data_type);
				if (rc < 0) {
					pr_err("%s:(%d) read failed\n", __func__, __LINE__);
					return rc;
				}

				memptr[i - start_addr] = OTP_Data;
			}
#endif

			rc = e_ctrl->i2c_client.i2c_func_tbl->i2c_write_table(
					&(e_ctrl->i2c_client), &finish_read_otp);
			if (rc < 0) {
				pr_err("%s:(%d) finish_read_otp failed\n", __func__, __LINE__);
				return rc;
			}
		}
	}
#ifdef CONFIG_S5K5E9YX
	memptr = block->mapdata;
#if defined(CONFIG_GET_FRONT_MODULE_ID_OTP)
	memcpy(front_module_id, memptr + S5K5E9_OTP_MODULE_ID_OFFSET, S5K5E9_OTP_MODULE_ID_SIZE);
	front_module_id[S5K5E9_OTP_MODULE_ID_SIZE] = '\0';
#endif
#if defined(CONFIG_GET_FRONT_SENSOR_ID)
	memcpy(front_sensor_id, memptr + S5K5E9_OTP_SENSOR_ID_OFFSET, S5K5E9_OTP_SENSOR_ID_SIZE);
	front_sensor_id[S5K5E9_OTP_SENSOR_ID_SIZE] = '\0';
#endif
#if defined(CONFIG_SEC_A9Y18QLTE_PROJECT)
        /* update rear2 module id for 5M bokeh */
	memcpy(rear2_module_id, memptr + S5K5E9_OTP_MODULE_ID_OFFSET, S5K5E9_OTP_MODULE_ID_SIZE);
	rear2_module_id[S5K5E9_OTP_MODULE_ID_SIZE] = '\0';
#endif
#endif

#ifdef MSM_OTP_DEBUG
	memptr = block->mapdata;
	for (i = 0; i < block->num_data; i += 16) {
		pr_err("memptr[%03X]: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
			i, memptr[i],memptr[i+1],memptr[i+2],memptr[i+3],memptr[i+4],memptr[i+5],memptr[i+6],memptr[i+7],
			memptr[i+8],memptr[i+9],memptr[i+10],memptr[i+11],memptr[i+12],memptr[i+13],memptr[i+14],memptr[i+15]);
	}
#endif
	return rc;
}

/**
  * msm_otp_parse_memory_map() - parse memory map in device node
  * @of:	device node
  * @data:	memory block for output
  *
  * This functions parses @of to fill @data.  It allocates map itself, parses
  * the @of node, calculate total data length, and allocates required buffer.
  * It only fills the map, but does not perform actual reading.
  */
static int msm_otp_parse_memory_map(struct device_node *of,
				struct msm_eeprom_memory_block_t *data)
{
	int i, rc = 0;
	char property[PROPERTY_MAXSIZE];
	uint32_t count = 6;
	struct msm_eeprom_memory_map_t *map;
	uint32_t total_size = 0;

	snprintf(property, PROPERTY_MAXSIZE, "qcom,num-blocks");
	rc = of_property_read_u32(of, property, &data->num_map);
	if (rc < 0) {
		pr_err("%s failed rc %d\n", __func__, rc);
		return rc;
	}
	pr_err("%s: otp %s %d\n", __func__, property, data->num_map);

	map = kzalloc((sizeof(*map) * data->num_map), GFP_KERNEL);
	if (!map) {
		pr_err("%s failed line %d\n", __func__, __LINE__);
		return -ENOMEM;
	}
	data->map = map;

	for (i = 0; i < data->num_map; i++) {
		CDBG("%s, %d: i = %d\n", __func__, __LINE__, i);

		snprintf(property, PROPERTY_MAXSIZE, "qcom,page%d", i);
		rc = of_property_read_u32_array(of, property,
				(uint32_t *) &map[i].page, count);
		if (rc < 0) {
			pr_err("%s: failed %d\n", __func__, __LINE__);
			goto ERROR;
		}

		snprintf(property, PROPERTY_MAXSIZE, "qcom,poll%d", i);
		rc = of_property_read_u32_array(of, property,
				(uint32_t *) &map[i].poll, count);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR;
		}

		snprintf(property, PROPERTY_MAXSIZE, "qcom,mem%d", i);
		rc = of_property_read_u32_array(of, property,
				(uint32_t *) &map[i].mem, count);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR;
		}

		if (map[i].mem.data_t == 1) {
			data->num_data += map[i].mem.valid_size;
		}
	}

	CDBG("%s::%d valid size = %d\n", __func__,__LINE__, data->num_data);

	// if total-size is defined at dtsi file.
	// set num_data as total-size
	snprintf(property, PROPERTY_MAXSIZE, "qcom,total-size");
	rc = of_property_read_u32(of, property, &total_size);

	CDBG("%s::%d  %s %d\n", __func__,__LINE__,property, total_size);

	// if "qcom,total-size" propoerty exists.
	if (rc >= 0) {
		pr_err("%s::%d set num_data as total-size (num_map : %d, total : %d, valid : %d)\n",
			__func__,__LINE__, data->num_map, total_size, data->num_data);
		data->num_data = total_size;
	}

	data->mapdata = kzalloc(data->num_data, GFP_KERNEL);
	if (!data->mapdata) {
		pr_err("%s failed line %d\n", __func__, __LINE__);
		rc = -ENOMEM;
		goto ERROR;
	}

	return rc;

ERROR:
	kfree(data->map);
	memset(data, 0, sizeof(*data));

	return rc;
}

static struct msm_cam_clk_info cam_8960_clk_info[] = {
	[SENSOR_CAM_MCLK] = {"cam_clk", 24000000},
};

static struct msm_cam_clk_info cam_8974_clk_info[] = {
	[SENSOR_CAM_MCLK] = {"cam_src_clk", 24000000},
	[SENSOR_CAM_CLK] = {"cam_clk", 0},
};

static struct v4l2_subdev_core_ops msm_otp_subdev_core_ops = {
	.ioctl = msm_otp_subdev_ioctl,
};

static struct v4l2_subdev_ops msm_otp_subdev_ops = {
	.core = &msm_otp_subdev_core_ops,
};

static int msm_otp_i2c_probe(struct i2c_client *client,
				const struct i2c_device_id *id)
{
	int rc = 0;
	uint32_t temp = 0;
	struct msm_otp_ctrl_t *e_ctrl = NULL;
	struct msm_camera_power_ctrl_t *power_info = NULL;
	struct device_node *of_node = client->dev.of_node;
	int i = 0;
	int normal_crc_value = 0;

	pr_info("%s otp E\n", __func__);

	if (!of_node) {
		pr_err("%s of_node NULL\n", __func__);
		return -EINVAL;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s i2c_check_functionality failed\n", __func__);
		goto probe_failure;
	}

	e_ctrl = kzalloc(sizeof(*e_ctrl), GFP_KERNEL);
	if (!e_ctrl) {
		pr_err("%s:%d kzalloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	e_ctrl->eeprom_v4l2_subdev_ops = &msm_otp_subdev_ops;
	e_ctrl->otp_mutex = &msm_otp_mutex;

	CDBG("%s client = 0x%p\n", __func__, client);

	e_ctrl->eboard_info = kzalloc(sizeof(struct msm_eeprom_board_info), GFP_KERNEL);

	if (!e_ctrl->eboard_info) {
		pr_err("%s:%d board info NULL\n", __func__, __LINE__);
		rc = -EINVAL;
		goto memdata_free;
	}

	rc = of_property_read_u32(of_node, "qcom,slave-addr", &temp);
	if (rc < 0) {
		pr_err("%s failed rc %d\n", __func__, rc);
		goto board_free;
	}

	rc = of_property_read_u32(of_node, "cell-index", &e_ctrl->subdev_id);
	if (rc < 0) {
		pr_err("failed read, rc %d\n", rc);
		goto board_free;
	}
	pr_info("cell-index/subdev_id %d, rc %d\n", e_ctrl->subdev_id, rc);

	power_info = &e_ctrl->eboard_info->power_info;
	e_ctrl->eboard_info->i2c_slaveaddr = temp;
	e_ctrl->i2c_client.client = client;
	e_ctrl->is_supported = 0;

	pr_info("%s:%d e_ctrl->eboard_info->i2c_slaveaddr = %d\n",
		__func__, __LINE__ , e_ctrl->eboard_info->i2c_slaveaddr);

	/* Set device type as I2C */
	e_ctrl->otp_device_type = MSM_CAMERA_I2C_DEVICE;
	e_ctrl->i2c_client.i2c_func_tbl = &msm_otp_qup_func_tbl;
	e_ctrl->i2c_client.addr_type = MSM_CAMERA_I2C_WORD_ADDR;

	if (e_ctrl->eboard_info->i2c_slaveaddr != 0) {
		e_ctrl->i2c_client.client->addr = e_ctrl->eboard_info->i2c_slaveaddr;
	}

	power_info->clk_info = cam_8960_clk_info;
	power_info->clk_info_size = ARRAY_SIZE(cam_8960_clk_info);
	power_info->dev = &client->dev;

	rc = of_property_read_string(of_node, "qcom,eeprom-name",
			&e_ctrl->eboard_info->eeprom_name);
	pr_info("%s qcom,eeprom-name %s, rc %d\n", __func__,
		e_ctrl->eboard_info->eeprom_name, rc);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto board_free;
	}

	rc = msm_otp_get_dt_data(e_ctrl);
	if (rc)
		goto board_free;

	rc = msm_otp_parse_memory_map(of_node, &e_ctrl->cal_data);
	if (rc < 0)
		pr_err("%s: no cal memory map\n", __func__);

	if (e_ctrl->cal_data.mapdata)
		map_data = e_ctrl->cal_data.mapdata;

	rc = msm_camera_power_up(power_info, e_ctrl->otp_device_type,
			&e_ctrl->i2c_client, false, SUB_DEVICE_TYPE_EEPROM);
	if (rc) {
		pr_err("failed rc %d\n", rc);
		goto memdata_free;
	}

	normal_crc_value = 0;
	for (i = 0; i < e_ctrl->cal_data.num_map>>1; i ++)
		normal_crc_value |= (1 << i);

	CDBG("num_map = %d, Normal CRC value = 0x%X\n",
			e_ctrl->cal_data.num_map, normal_crc_value);

	rc = read_otp_memory(e_ctrl, &e_ctrl->cal_data);
	if (rc < 0) {
		pr_err("%s read_otp_memory failed\n", __func__);
		goto power_down;
	}

	e_ctrl->is_supported |= msm_otp_match_crc(&e_ctrl->cal_data);
	if (e_ctrl->is_supported != normal_crc_value) {
		pr_err("%s : any CRC value(s) are not matched.\n", __func__);
	} else {
		pr_err("%s : All CRC values are matched.\n", __func__);
	}

	rc = msm_camera_power_down(power_info, e_ctrl->otp_device_type,
			&e_ctrl->i2c_client, false, SUB_DEVICE_TYPE_EEPROM);
	if (rc) {
		pr_err("failed rc %d\n", rc);
		goto memdata_free;
	}

	if (0 > of_property_read_u32(of_node, "qcom,sensor-position", &temp)) {
		pr_err("%s:%d Fail position, Default sensor position\n", __func__, __LINE__);
		temp = 0;
	}

	pr_err("%s qcom,sensor-position %d\n", __func__, temp);

	/* Initialize sub device */
	v4l2_i2c_subdev_init(&e_ctrl->msm_sd.sd, e_ctrl->i2c_client.client,
		e_ctrl->eeprom_v4l2_subdev_ops);
	v4l2_set_subdevdata(&e_ctrl->msm_sd.sd, e_ctrl);
	e_ctrl->msm_sd.sd.internal_ops = &msm_otp_internal_ops;
	e_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	media_entity_init(&e_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	e_ctrl->msm_sd.sd.entity.flags = temp;
	e_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	e_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_EEPROM;
	msm_sd_register(&e_ctrl->msm_sd);
	e_ctrl->is_supported = (e_ctrl->is_supported << 1) | 1;

	pr_info("%s success result=%d is_supported=0x%XX\n", __func__,
		rc, e_ctrl->is_supported);

	return rc;

power_down:
	msm_camera_power_down(power_info, e_ctrl->otp_device_type,
		&e_ctrl->i2c_client, false, SUB_DEVICE_TYPE_EEPROM);

board_free:
	kfree(e_ctrl->eboard_info);

memdata_free:
	kfree(e_ctrl);

probe_failure:
	pr_err("%s failed! rc = %d\n", __func__, rc);
	return rc;
}

static int msm_otp_i2c_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct msm_otp_ctrl_t  *e_ctrl;

	if (!sd) {
		pr_err("%s: Subdevice is NULL\n", __func__);
		return 0;
	}

	e_ctrl = (struct msm_otp_ctrl_t *)v4l2_get_subdevdata(sd);
	if (!e_ctrl) {
		pr_err("%s: otp device is NULL\n", __func__);
		return 0;
	}

	kfree(e_ctrl->cal_data.mapdata);
	kfree(e_ctrl->cal_data.map);

	if (e_ctrl->eboard_info) {
		kfree(e_ctrl->eboard_info->power_info.gpio_conf);
		kfree(e_ctrl->eboard_info);
	}

	kfree(e_ctrl);
	return 0;
}

#define msm_otp_spi_parse_cmd(spic, str, name, out, size)		\
	{								\
		if (of_property_read_u32_array(				\
			spic->spi_master->dev.of_node,			\
			str, out, size)) {				\
			return -EFAULT;					\
		} else {						\
			spic->cmd_tbl.name.opcode = out[0];		\
			spic->cmd_tbl.name.addr_len = out[1];		\
			spic->cmd_tbl.name.dummy_len = out[2];		\
		}							\
	}

static int msm_otp_spi_parse_of(struct msm_camera_spi_client *spic)
{
	int rc = -EFAULT;
	uint32_t tmp[5];
	struct device_node *of = spic->spi_master->dev.of_node;

	msm_otp_spi_parse_cmd(spic, "qcom,spiop-read", read, tmp, 5);
	msm_otp_spi_parse_cmd(spic, "qcom,spiop-readseq", read_seq, tmp, 5);
	msm_otp_spi_parse_cmd(spic, "qcom,spiop-queryid", query_id, tmp, 5);
	msm_otp_spi_parse_cmd(spic, "qcom,spiop-pprog", page_program, tmp, 5);
	msm_otp_spi_parse_cmd(spic, "qcom,spiop-wenable", write_enable, tmp, 5);
	msm_otp_spi_parse_cmd(spic, "qcom,spiop-readst", read_status, tmp, 5);
	msm_otp_spi_parse_cmd(spic, "qcom,spiop-erase", erase, tmp, 5);

	rc = of_property_read_u32(of, "qcom,spi-busy-mask", tmp);
	if (rc < 0) {
		pr_err("%s: Failed to get busy mask\n", __func__);
		return rc;
	}
	spic->busy_mask = tmp[0];

	rc = of_property_read_u32(of, "qcom,spi-page-size", tmp);
	if (rc < 0) {
		pr_err("%s: Failed to get page size\n", __func__);
		return rc;
	}
	spic->page_size = tmp[0];

	rc = of_property_read_u32(of, "qcom,spi-erase-size", tmp);
	if (rc < 0) {
		pr_err("%s: Failed to get erase size\n", __func__);
		return rc;
	}
	spic->erase_size = tmp[0];

	rc = of_property_read_u32_array(of, "qcom,eeprom-id0", tmp, 2);
	if (rc < 0) {
		pr_err("%s: Failed to get otp id 0\n", __func__);
		return rc;
	}
	spic->mfr_id0 = tmp[0];
	spic->device_id0 = tmp[1];

	rc = of_property_read_u32_array(of, "qcom,eeprom-id1", tmp, 2);
	if (rc < 0) {
		pr_err("%s: Failed to get otp id 1\n", __func__);
		return rc;
	}
	spic->mfr_id1 = tmp[0];
	spic->device_id1 = tmp[1];
	
	return 0;
}

static int msm_otp_match_id(struct msm_otp_ctrl_t *e_ctrl)
{
	int rc;
	struct msm_camera_i2c_client *client = &e_ctrl->i2c_client;
	uint8_t id[2];

	rc = msm_camera_spi_query_id(client, 0, &id[0], 2);
	if (rc < 0)
		return rc;

	pr_info("%s: read 0x%02X%02X, check Fidelix 16M:0x%02X%02X, Winbond 8M:0x%02X%02X\n", __func__,
		id[0], id[1], client->spi_client->mfr_id0, client->spi_client->device_id0,
		client->spi_client->mfr_id1, client->spi_client->device_id1);

	if ((id[0] == client->spi_client->mfr_id0 && id[1] == client->spi_client->device_id0)
			|| (id[0] == client->spi_client->mfr_id1 && id[1] == client->spi_client->device_id1))
		return 0;

	return -ENODEV;
}

static int msm_otp_get_dt_data(struct msm_otp_ctrl_t *e_ctrl)
{
	int rc = 0, i = 0;
	struct msm_eeprom_board_info *eb_info;
	struct msm_camera_power_ctrl_t *power_info = &e_ctrl->eboard_info->power_info;
	struct device_node *of_node = NULL;
	struct msm_camera_gpio_conf *gconf = NULL;
	uint16_t gpio_array_size = 0;
	uint16_t *gpio_array = NULL;

	eb_info = e_ctrl->eboard_info;

	if (e_ctrl->otp_device_type == MSM_CAMERA_SPI_DEVICE)
		of_node = e_ctrl->i2c_client.spi_client->spi_master->dev.of_node;
	else if (e_ctrl->otp_device_type == MSM_CAMERA_PLATFORM_DEVICE)
		of_node = e_ctrl->pdev->dev.of_node;
	else if (e_ctrl->otp_device_type == MSM_CAMERA_I2C_DEVICE)
		of_node = e_ctrl->i2c_client.client->dev.of_node;

	rc = msm_camera_get_dt_vreg_data(of_node, &power_info->cam_vreg,
					&power_info->num_vreg);
	if (rc < 0) {
		pr_err("msm_otp_get_dt_data: %d\n", __LINE__);
		return rc;
	}
	pr_info("msm_camera_get_dt_power_setting_data : %d\n", __LINE__);

	rc = msm_camera_get_dt_power_setting_data(of_node,
			power_info->cam_vreg, power_info->num_vreg, power_info);
	if (rc < 0) {
		pr_err("msm_otp_get_dt_data: %d\n", __LINE__);
		goto ERROR1;
	}

	power_info->gpio_conf = kzalloc(sizeof(struct msm_camera_gpio_conf), GFP_KERNEL);
	if (!power_info->gpio_conf) {
		pr_err("msm_otp_get_dt_data: %d\n", __LINE__);
		rc = -ENOMEM;
		goto ERROR2;
	}

	gconf = power_info->gpio_conf;
	gpio_array_size = of_gpio_count(of_node);
	pr_info("%s gpio count %d\n", __func__, gpio_array_size);

	if (gpio_array_size) {
		gpio_array = kzalloc(sizeof(uint16_t) * gpio_array_size, GFP_KERNEL);
		if (!gpio_array) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR3;
		}

		for (i = 0; i < gpio_array_size; i++) {
			gpio_array[i] = of_get_gpio(of_node, i);
		}
		pr_info("msm_otp_get_dt_data: %d\n", __LINE__);

		rc = msm_camera_get_dt_gpio_req_tbl(of_node, gconf,
				gpio_array, gpio_array_size);

		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR4;
		}
		pr_info("msm_otp_get_dt_data: %d\n", __LINE__);

		rc = msm_camera_init_gpio_pin_tbl(of_node, gconf, gpio_array, gpio_array_size);
		if (rc < 0) {
			pr_err("%s failed %d\n", __func__, __LINE__);
			goto ERROR4;
		}
		pr_info("msm_otp_get_dt_data: %d\n", __LINE__);

		kfree(gpio_array);
	}
	pr_info("msm_otp_get_dt_data: %d\n", __LINE__);

	return rc;

ERROR4:
	pr_err("msm_otp_get_dt_data: %d\n", __LINE__);
	kfree(gpio_array);

ERROR3:
	pr_err("msm_otp_get_dt_data: %d\n", __LINE__);
	kfree(power_info->gpio_conf);

ERROR2:
	pr_err("msm_otp_get_dt_data: %d\n", __LINE__);
	kfree(power_info->cam_vreg);

ERROR1:
	pr_err("msm_otp_get_dt_data: %d\n", __LINE__);
	kfree(power_info->power_setting);

	return rc;
}

#ifdef CONFIG_COMPAT
static int msm_otp_config32(struct msm_otp_ctrl_t *e_ctrl, void __user *argp)
{
	struct msm_eeprom_cfg_data32 *cdata32 = (struct msm_eeprom_cfg_data32 *)argp;
	struct msm_eeprom_cfg_data cdata;
	int rc = 0;

	CDBG("%s:%d E: subdevid: %d\n", __func__, __LINE__, e_ctrl->subdev_id);
	cdata.cfgtype = cdata32->cfgtype;
	CDBG("%s:%d cfgtype = %d\n", __func__, __LINE__, cdata.cfgtype);

	switch (cdata.cfgtype) {
	case CFG_EEPROM_GET_INFO:
		CDBG("%s E CFG_EEPROM_GET_INFO: %d, %s\n", __func__,
			e_ctrl->is_supported, e_ctrl->eboard_info->eeprom_name);
		cdata32->is_supported = e_ctrl->is_supported;
		memcpy(cdata32->cfg.eeprom_name, e_ctrl->eboard_info->eeprom_name,
			sizeof(cdata32->cfg.eeprom_name));
		break;

	case CFG_EEPROM_GET_CAL_DATA:
		CDBG("%s E CFG_EEPROM_GET_CAL_DATA: %d\n", __func__, e_ctrl->cal_data.num_data);
		cdata32->cfg.get_data.num_bytes = e_ctrl->cal_data.num_data;
		break;

	case CFG_EEPROM_READ_CAL_DATA:
		CDBG("%s E CFG_EEPROM_READ_CAL_DATA\n", __func__);
		cdata.cfg.read_data.num_bytes = cdata32->cfg.read_data.num_bytes;
		cdata.cfg.read_data.addr = cdata32->cfg.read_data.addr;
		cdata.cfg.read_data.dbuffer = compat_ptr(cdata32->cfg.read_data.dbuffer);
		rc = otp_config_read_cal_data(e_ctrl, &cdata);
		break;

	case CFG_EEPROM_READ_DATA:
		CDBG("%s E CFG_EEPROM_READ_DATA\n", __func__);
		cdata.cfg.read_data.num_bytes = cdata32->cfg.read_data.num_bytes;
		cdata.cfg.read_data.addr = cdata32->cfg.read_data.addr;
		cdata.cfg.read_data.dbuffer = compat_ptr(cdata32->cfg.read_data.dbuffer);
		rc = otp_config_read_data(e_ctrl, &cdata);
		break;

	case CFG_EEPROM_READ_COMPRESSED_DATA:
		CDBG("%s E CFG_EEPROM_READ_COMPRESSED_DATA\n", __func__);
		cdata.cfg.read_data.num_bytes = cdata32->cfg.read_data.num_bytes;
		cdata.cfg.read_data.addr = cdata32->cfg.read_data.addr;
		cdata.cfg.read_data.comp_size = cdata32->cfg.read_data.comp_size;
		cdata.cfg.read_data.dbuffer = compat_ptr(cdata32->cfg.read_data.dbuffer);
		rc = otp_config_read_compressed_data(e_ctrl, &cdata);
		if (rc < 0)
			pr_err("%s : otp_config_read_compressed_data failed \n", __func__);
		break;

	case CFG_EEPROM_WRITE_DATA:
		pr_warn("%s E CFG_EEPROM_WRITE_DATA\n", __func__);
		cdata.cfg.write_data.num_bytes = cdata32->cfg.write_data.num_bytes;
		cdata.cfg.write_data.addr = cdata32->cfg.write_data.addr;
		cdata.cfg.write_data.compress = cdata32->cfg.write_data.compress;
		cdata.cfg.write_data.write_size = compat_ptr(cdata32->cfg.write_data.write_size);
		cdata.cfg.write_data.dbuffer = compat_ptr(cdata32->cfg.write_data.dbuffer);
		rc = otp_config_write_data(e_ctrl, &cdata);
		break;

	case CFG_EEPROM_READ_DATA_FROM_HW:
		e_ctrl->is_supported = 0x01;
		pr_err ("kernel is_supported before : 0x%04X\n", e_ctrl->is_supported);
		rc = msm_otp_read_otp_data(e_ctrl);
		pr_err ("kernel is_supported after : 0x%04X\n", e_ctrl->is_supported);
		cdata32->is_supported = e_ctrl->is_supported;
		if (rc < 0) {
			pr_err("%s:%d failed rc %d\n", __func__, __LINE__,  rc);
			break;
		}
		break;

	case CFG_EEPROM_GET_ERASESIZE:
		CDBG("%s E CFG_EEPROM_GET_ERASESIZE: %d\n",
			__func__, e_ctrl->i2c_client.spi_client->erase_size);
		cdata32->cfg.get_data.num_bytes = e_ctrl->i2c_client.spi_client->erase_size;
		break;

	case CFG_EEPROM_POWER_ON:
		rc = msm_otp_power_up(e_ctrl, NULL);
		if (rc < 0)
			pr_err("%s : msm_otp_power_up failed \n", __func__);
		break;

	case CFG_EEPROM_POWER_OFF:
		rc = msm_otp_power_down(e_ctrl, true);
		if (rc < 0)
			pr_err("%s : msm_otp_power_down failed \n", __func__);
		break;

	case CFG_EEPROM_GET_MM_INFO:
		CDBG("%s E CFG_EEPROM_GET_MM_INFO\n", __func__);
		rc = msm_otp_get_cmm_data(e_ctrl, &cdata);
		break;

	default:
		break;
	}

	CDBG("%s X rc: %d\n", __func__, rc);
	return rc;
}

static long msm_otp_subdev_ioctl32(struct v4l2_subdev *sd,
				unsigned int cmd, void *arg)
{
	struct msm_otp_ctrl_t *e_ctrl = v4l2_get_subdevdata(sd);
	void __user *argp = (void __user *)arg;

	CDBG("%s E\n", __func__);
	CDBG("%s:%d a_ctrl %pK argp %pK\n", __func__, __LINE__, e_ctrl, argp);

	switch (cmd) {
	case VIDIOC_MSM_SENSOR_GET_SUBDEV_ID:
		return msm_otp_get_subdev_id(e_ctrl, argp);

	case VIDIOC_MSM_EEPROM_CFG32:
		return msm_otp_config32(e_ctrl, argp);

	default:
		return -ENOIOCTLCMD;
	}

	CDBG("%s X\n", __func__);
}

static long msm_otp_subdev_do_ioctl32( struct file *file, unsigned int cmd, void *arg)
{
	struct video_device *vdev = video_devdata(file);
	struct v4l2_subdev *sd = vdev_to_v4l2_subdev(vdev);

	return msm_otp_subdev_ioctl32(sd, cmd, arg);
}

static long msm_otp_subdev_fops_ioctl32(struct file *file, unsigned int cmd, unsigned long arg)
{
	return video_usercopy(file, cmd, arg, msm_otp_subdev_do_ioctl32);
}
#endif

static int msm_otp_cmm_dts(struct msm_eeprom_board_info *eb_info,
				struct device_node *of_node)
{
	int rc = 0;
	struct msm_eeprom_cmm_t *cmm_data = &eb_info->cmm_data;

	cmm_data->cmm_support = of_property_read_bool(of_node, "qcom,cmm-data-support");
	if (!cmm_data->cmm_support)
		return -EINVAL;

	cmm_data->cmm_compression = of_property_read_bool(of_node, "qcom,cmm-data-compressed");
	if (!cmm_data->cmm_compression)
		CDBG("No MM compression data\n");

	rc = of_property_read_u32(of_node, "qcom,cmm-data-offset", &cmm_data->cmm_offset);
	if (rc < 0)
		CDBG("No MM offset data\n");

	rc = of_property_read_u32(of_node, "qcom,cmm-data-size", &cmm_data->cmm_size);
	if (rc < 0)
		CDBG("No MM size data\n");

	CDBG("cmm_support: cmm_compr %d, cmm_offset %d, cmm_size %d\n",
		cmm_data->cmm_compression, cmm_data->cmm_offset, cmm_data->cmm_size);

	return 0;
}

static int msm_otp_spi_setup(struct spi_device *spi)
{
	struct msm_otp_ctrl_t *e_ctrl = NULL;
	struct msm_camera_i2c_client *client = NULL;
	struct msm_camera_spi_client *spi_client;
	struct msm_eeprom_board_info *eb_info;
	struct msm_camera_power_ctrl_t *power_info = NULL;
	int rc = 0;
	uint32_t temp = 0;
	int i = 0;
	int normal_crc_value = 0;

	e_ctrl = kzalloc(sizeof(*e_ctrl), GFP_KERNEL);
	if (!e_ctrl) {
		pr_err("%s:%d kzalloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	e_ctrl->eeprom_v4l2_subdev_ops = &msm_otp_subdev_ops;
	e_ctrl->otp_mutex = &msm_otp_mutex;
	client = &e_ctrl->i2c_client;
	e_ctrl->is_supported = 0;

	spi_client = kzalloc(sizeof(*spi_client), GFP_KERNEL);
	if (!spi_client) {
		pr_err("%s:%d kzalloc failed\n", __func__, __LINE__);
		kfree(e_ctrl);
		return -ENOMEM;
	}

	rc = of_property_read_u32(spi->dev.of_node, "cell-index", &e_ctrl->subdev_id);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		goto spi_free;
	}

	CDBG("cell-index/subdev_id %d, rc %d\n", e_ctrl->subdev_id, rc);
	e_ctrl->otp_device_type = MSM_CAMERA_SPI_DEVICE;
	client->spi_client = spi_client;
	spi_client->spi_master = spi;
	client->i2c_func_tbl = &msm_otp_spi_func_tbl;
	client->addr_type = MSM_CAMERA_I2C_3B_ADDR;
	eb_info = kzalloc(sizeof(*eb_info), GFP_KERNEL);
	if (!eb_info)
		goto spi_free;

	e_ctrl->eboard_info = eb_info;
	rc = of_property_read_string(spi->dev.of_node, "qcom,eeprom-name",
			&eb_info->eeprom_name);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto board_free;
	}

	CDBG("%s qcom,eeprom-name %s, rc %d\n", __func__, eb_info->eeprom_name, rc);

	rc = msm_otp_cmm_dts(e_ctrl->eboard_info, spi->dev.of_node);
	if (rc < 0)
		CDBG("%s MM data miss:%d\n", __func__, __LINE__);

	power_info = &eb_info->power_info;
	power_info->clk_info = cam_8974_clk_info;
	power_info->clk_info_size = ARRAY_SIZE(cam_8974_clk_info);
	power_info->dev = &spi->dev;

	rc = msm_otp_get_dt_data(e_ctrl);
	if (rc < 0)
		goto board_free;

	/* set spi instruction info */
	spi_client->retry_delay = 1;
	spi_client->retries = 0;

	rc = msm_otp_spi_parse_of(spi_client);
	if (rc < 0) {
		dev_err(&spi->dev, "%s: Error parsing device properties\n", __func__);
		goto board_free;
	}

	/* prepare memory buffer */
	rc = msm_otp_parse_memory_map(spi->dev.of_node, &e_ctrl->cal_data);
	if (rc < 0)
		CDBG("%s: no cal memory map\n", __func__);

	/* power up otp for reading */
	rc = msm_camera_power_up(power_info, e_ctrl->otp_device_type,
			&e_ctrl->i2c_client, false, SUB_DEVICE_TYPE_EEPROM);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		goto caldata_free;
	}

	/* check otp id */
	rc = msm_otp_match_id(e_ctrl);
	if (rc < 0) {
		CDBG("%s: otp not matching %d\n", __func__, rc);
		goto power_down;
	}

	normal_crc_value = 0;
	for (i = 0; i < e_ctrl->cal_data.num_map>>1; i ++)
		normal_crc_value |= (1 << i);

	CDBG("num_map = %d, Normal CRC value = 0x%X\n",
		e_ctrl->cal_data.num_map, normal_crc_value);

	/* read otp */
	if (e_ctrl->cal_data.map) {
		rc = read_otp_memory(e_ctrl, &e_ctrl->cal_data);
		if (rc < 0) {
			pr_err("%s: read cal data failed\n", __func__);
			goto power_down;
		}

		e_ctrl->is_supported |= msm_otp_match_crc(&e_ctrl->cal_data);

		if (e_ctrl->is_supported != normal_crc_value) {
			pr_err("%s : any CRC value(s) are not matched.\n", __func__);
		} else {
			pr_err("%s : All CRC values are matched.\n", __func__);
		}
	}

	rc = msm_camera_power_down(power_info, e_ctrl->otp_device_type,
			&e_ctrl->i2c_client, false, SUB_DEVICE_TYPE_EEPROM);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		goto caldata_free;
	}

	if (0 > of_property_read_u32(spi->dev.of_node, "qcom,sensor-position", &temp)) {
		pr_err("%s:%d Fail position, Default sensor position\n", __func__, __LINE__);
		temp = 0;
	}

	/* initiazlie subdev */
	v4l2_spi_subdev_init(&e_ctrl->msm_sd.sd, e_ctrl->i2c_client.spi_client->spi_master,
		e_ctrl->eeprom_v4l2_subdev_ops);
	v4l2_set_subdevdata(&e_ctrl->msm_sd.sd, e_ctrl);
	e_ctrl->msm_sd.sd.internal_ops = &msm_otp_internal_ops;
	e_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	media_entity_init(&e_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	e_ctrl->msm_sd.sd.entity.flags = temp;
	e_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	e_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_EEPROM;
	msm_sd_register(&e_ctrl->msm_sd);
	e_ctrl->is_supported = (e_ctrl->is_supported << 1) | 1;

	CDBG("%s success result=%d supported=%x X\n", __func__, rc, e_ctrl->is_supported);

	return 0;

power_down:
	msm_camera_power_down(power_info, e_ctrl->otp_device_type,
		&e_ctrl->i2c_client, false, SUB_DEVICE_TYPE_EEPROM);

caldata_free:
	kfree(e_ctrl->cal_data.mapdata);
	kfree(e_ctrl->cal_data.map);

board_free:
	kfree(e_ctrl->eboard_info);

spi_free:
	kfree(spi_client);
	kfree(e_ctrl);
	return rc;
}

static int msm_otp_spi_probe(struct spi_device *spi)
{
	int irq, cs, cpha, cpol, cs_high;

	pr_err("%s\n", __func__);

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	spi_setup(spi);
	irq = spi->irq;
	cs = spi->chip_select;
	cpha = (spi->mode & SPI_CPHA) ? 1 : 0;
	cpol = (spi->mode & SPI_CPOL) ? 1 : 0;
	cs_high = (spi->mode & SPI_CS_HIGH) ? 1 : 0;

	pr_err("%s: irq[%d] cs[%x] CPHA[%x] CPOL[%x] CS_HIGH[%x]\n",
			__func__, irq, cs, cpha, cpol, cs_high);
	pr_err("%s: max_speed[%u]\n", __func__, spi->max_speed_hz);

	return msm_otp_spi_setup(spi);
}

static int msm_otp_spi_remove(struct spi_device *sdev)
{
	struct v4l2_subdev *sd = spi_get_drvdata(sdev);
	struct msm_otp_ctrl_t  *e_ctrl;

	if (!sd) {
		pr_err("%s: Subdevice is NULL\n", __func__);
		return 0;
	}

	e_ctrl = (struct msm_otp_ctrl_t *)v4l2_get_subdevdata(sd);
	if (!e_ctrl) {
		pr_err("%s: otp device is NULL\n", __func__);
		return 0;
	}

	kfree(e_ctrl->i2c_client.spi_client);
	kfree(e_ctrl->cal_data.mapdata);
	kfree(e_ctrl->cal_data.map);

	if (e_ctrl->eboard_info) {
		kfree(e_ctrl->eboard_info->power_info.gpio_conf);
		kfree(e_ctrl->eboard_info);
	}

	kfree(e_ctrl);
	return 0;
}

static int msm_otp_platform_probe(struct platform_device *pdev)
{
	int rc = 0;
	int retry_count = 0;
	uint32_t temp;
	int i = 0;
	int normal_crc_value = 0;
	struct msm_camera_cci_client *cci_client = NULL;
	struct msm_otp_ctrl_t *e_ctrl = NULL;
	struct msm_eeprom_board_info *eb_info = NULL;
	struct device_node *of_node = pdev->dev.of_node;
	struct msm_camera_power_ctrl_t *power_info = NULL;

	CDBG("%s:%d otp E\n", __func__, __LINE__);

	e_ctrl = kzalloc(sizeof(*e_ctrl), GFP_KERNEL);
	if (!e_ctrl) {
		pr_err("%s:%d kzalloc failed\n", __func__, __LINE__);
		return -ENOMEM;
	}

	e_ctrl->eeprom_v4l2_subdev_ops = &msm_otp_subdev_ops;
	e_ctrl->otp_mutex = &msm_otp_mutex;
	e_ctrl->is_supported = 0;

	if (!of_node) {
		pr_err("%s dev.of_node NULL\n", __func__);
		kfree(e_ctrl);
		return -EINVAL;
	}

	rc = of_property_read_u32(of_node, "cell-index", &pdev->id);
	if (rc < 0) {
		pr_err("failed rc %d\n", rc);
		kfree(e_ctrl);
		return rc;
	}
	CDBG("cell-index %d, rc %d\n", pdev->id, rc);

	e_ctrl->subdev_id = pdev->id;

	rc = of_property_read_u32(of_node, "qcom,cci-master", &e_ctrl->cci_master);
	if (rc < 0) {
		pr_err("%s failed rc %d\n", __func__, rc);
		kfree(e_ctrl);
		return rc;
	}
	CDBG("qcom,cci-master %d, rc %d\n", e_ctrl->cci_master, rc);

	rc = of_property_read_u32(of_node, "qcom,slave-addr", &temp);
	if (rc < 0) {
		pr_err("%s failed rc %d\n", __func__, rc);
		kfree(e_ctrl);
		return rc;
	}

	/* Set platform device handle */
	e_ctrl->pdev = pdev;

	/* Set device type as platform device */
	e_ctrl->otp_device_type = MSM_CAMERA_PLATFORM_DEVICE;
	e_ctrl->i2c_client.i2c_func_tbl = &msm_otp_cci_func_tbl;
	e_ctrl->i2c_client.addr_type = MSM_CAMERA_I2C_WORD_ADDR;
	e_ctrl->i2c_client.cci_client = kzalloc (sizeof (struct msm_camera_cci_client), GFP_KERNEL);

	if (!e_ctrl->i2c_client.cci_client) {
		pr_err("%s failed no memory\n", __func__);
		kfree(e_ctrl);
		return -ENOMEM;
	}

	e_ctrl->eboard_info = kzalloc (sizeof (	struct msm_eeprom_board_info), GFP_KERNEL);
	if (!e_ctrl->eboard_info) {
		pr_err("%s failed line %d\n", __func__, __LINE__);
		rc = -ENOMEM;
		goto cciclient_free;
	}

	eb_info = e_ctrl->eboard_info;
	power_info = &eb_info->power_info;
	eb_info->i2c_slaveaddr = temp;
	power_info->clk_info = cam_8974_clk_info;
	power_info->clk_info_size = ARRAY_SIZE(cam_8974_clk_info);
	power_info->dev = &pdev->dev;

	CDBG("qcom,slave-addr = 0x%X\n", eb_info->i2c_slaveaddr);

	cci_client = e_ctrl->i2c_client.cci_client;
	cci_client->cci_subdev = msm_cci_get_subdev();
	cci_client->cci_i2c_master = e_ctrl->cci_master;
	cci_client->sid = eb_info->i2c_slaveaddr >> 1;
	cci_client->retries = 3;
	cci_client->id_map = 0;
	cci_client->i2c_freq_mode = I2C_FAST_MODE;

	rc = of_property_read_string(of_node, "qcom,eeprom-name",	&eb_info->eeprom_name);
	if (rc < 0) {
		pr_err("%s failed %d\n", __func__, __LINE__);
		goto board_free;
	}
	CDBG("%s qcom,eeprom-name %s, rc %d\n", __func__, eb_info->eeprom_name, rc);

	rc = msm_otp_cmm_dts(e_ctrl->eboard_info, of_node);
	if (rc < 0)
		CDBG("%s MM data miss:%d\n", __func__, __LINE__);

	rc = msm_otp_get_dt_data(e_ctrl);
	if (rc)
		goto board_free;

	/* Get clocks information */
	rc = msm_camera_get_clk_info( e_ctrl->pdev, &power_info->clk_info,
		&power_info->clk_ptr, &power_info->clk_info_size);
	if (rc < 0) {
		pr_err("failed: msm_camera_i2c_dev_get_clk_info rc %d", rc);
		goto board_free;
	}

	rc = msm_otp_parse_memory_map(of_node, &e_ctrl->cal_data);
	if (rc < 0)
		goto board_free;

	if (e_ctrl->cal_data.mapdata)
		map_data = e_ctrl->cal_data.mapdata;

RETRY:
	rc = msm_camera_power_up(power_info, e_ctrl->otp_device_type,
		&e_ctrl->i2c_client, false, SUB_DEVICE_TYPE_EEPROM);
	if (rc) {
		pr_err("failed rc %d\n", rc);
		goto memdata_free;
	}

	if (e_ctrl->cal_data.map) {
		normal_crc_value = 0;

		for (i = 0; i < e_ctrl->cal_data.num_map >> 1; i++)
			normal_crc_value |= (1 << i);

		CDBG("num_map = %d, Normal CRC value = 0x%X\n",
			e_ctrl->cal_data.num_map, normal_crc_value);

		rc = read_otp_memory(e_ctrl, &e_ctrl->cal_data);
		if (rc < 0) {
			pr_err("%s read_otp_memory failed (%d)\n", __func__, retry_count);

			//Added retry code, sometimes, GPIO request is failed
			if (retry_count == 3) {
				goto power_down;
			} else {
				retry_count++;
				msm_camera_power_down(power_info, e_ctrl->otp_device_type,
					&e_ctrl->i2c_client, false, SUB_DEVICE_TYPE_EEPROM);

				goto RETRY;
			}
		}

		e_ctrl->is_supported |= msm_otp_match_crc(&e_ctrl->cal_data);
		if (e_ctrl->is_supported != normal_crc_value) {
			pr_err("%s : any CRC value(s) are not matched.\n", __func__);
		} else {
			pr_err("%s : All CRC values are matched.\n", __func__);
		}
	}

	rc = msm_camera_power_down(power_info, e_ctrl->otp_device_type,
			&e_ctrl->i2c_client, false, SUB_DEVICE_TYPE_EEPROM);
	if (rc) {
		pr_err("failed rc %d\n", rc);
		goto memdata_free;
	}

	if (0 > of_property_read_u32(of_node, "qcom,sensor-position", &temp)) {
		pr_err("%s:%d Fail position, Default sensor position\n", __func__, __LINE__);
		temp = 0;
	}
	pr_info("%s qcom,sensor-position %d\n", __func__, temp);

	v4l2_subdev_init(&e_ctrl->msm_sd.sd, e_ctrl->eeprom_v4l2_subdev_ops);
	v4l2_set_subdevdata(&e_ctrl->msm_sd.sd, e_ctrl);
	platform_set_drvdata(pdev, &e_ctrl->msm_sd.sd);
	e_ctrl->msm_sd.sd.internal_ops = &msm_otp_internal_ops;
	e_ctrl->msm_sd.sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	snprintf(e_ctrl->msm_sd.sd.name, ARRAY_SIZE(e_ctrl->msm_sd.sd.name), "msm_otp");
	media_entity_init(&e_ctrl->msm_sd.sd.entity, 0, NULL, 0);
	e_ctrl->msm_sd.sd.entity.flags = temp;
	e_ctrl->msm_sd.sd.entity.type = MEDIA_ENT_T_V4L2_SUBDEV;
	e_ctrl->msm_sd.sd.entity.group_id = MSM_CAMERA_SUBDEV_EEPROM;
	msm_sd_register(&e_ctrl->msm_sd);
	e_ctrl->is_supported = (e_ctrl->is_supported << 1) | 1;

	pr_err("%s Front is_supported=0x%02X\n", __func__, e_ctrl->is_supported);

#ifdef CONFIG_COMPAT
	msm_otp_v4l2_subdev_fops = v4l2_subdev_fops;
	msm_otp_v4l2_subdev_fops.compat_ioctl32 = msm_otp_subdev_fops_ioctl32;
	e_ctrl->msm_sd.sd.devnode->fops = &msm_otp_v4l2_subdev_fops;
#endif

	CDBG("%s X\n", __func__);
	return rc;

power_down:
	msm_camera_power_down(power_info, e_ctrl->otp_device_type,
		&e_ctrl->i2c_client, false, SUB_DEVICE_TYPE_EEPROM);

memdata_free:
	kfree(e_ctrl->cal_data.mapdata);
	kfree(e_ctrl->cal_data.map);

board_free:
	kfree(e_ctrl->eboard_info);

cciclient_free:
	kfree(e_ctrl->i2c_client.cci_client);
	kfree(e_ctrl);

	return rc;
}

static int msm_otp_platform_remove(struct platform_device *pdev)
{
	struct v4l2_subdev *sd = platform_get_drvdata(pdev);
	struct msm_otp_ctrl_t  *e_ctrl;

	if (!sd) {
		pr_err("%s: Subdevice is NULL\n", __func__);
		return 0;
	}

	e_ctrl = (struct msm_otp_ctrl_t *)v4l2_get_subdevdata(sd);
	if (!e_ctrl) {
		pr_err("%s: otp device is NULL\n", __func__);
		return 0;
	}

	kfree(e_ctrl->i2c_client.cci_client);
	kfree(e_ctrl->cal_data.mapdata);
	kfree(e_ctrl->cal_data.map);
	if (e_ctrl->eboard_info) {
		kfree(e_ctrl->eboard_info->power_info.gpio_conf);
		kfree(e_ctrl->eboard_info);
	}
	kfree(e_ctrl);

	return 0;
}

static const struct of_device_id msm_otp_dt_match[] = {
	{ .compatible = "qcom,otp" },
	{ }
};
MODULE_DEVICE_TABLE(of, msm_otp_dt_match);

static struct platform_driver msm_otp_platform_driver = {
	.driver = {
		.name = "qcom,otp",
		.owner = THIS_MODULE,
		.of_match_table = msm_otp_dt_match,
	},
	.probe = msm_otp_platform_probe,
	.remove = msm_otp_platform_remove,
};

static const struct of_device_id msm_otp_i2c_dt_match[] = {
	{ .compatible = "qcom,otp"},
	{ }
};
MODULE_DEVICE_TABLE(of, msm_otp_i2c_dt_match);

static const struct i2c_device_id msm_otp_i2c_id[] = {
	{ "qcom,otp", (kernel_ulong_t)NULL},
	{ }
};

static struct i2c_driver msm_otp_i2c_driver = {
	.id_table = msm_otp_i2c_id,
	.probe  = msm_otp_i2c_probe,
	.remove = msm_otp_i2c_remove,
	.driver = {
		.name = "qcom,otp",
		.owner = THIS_MODULE,
		.of_match_table = msm_otp_i2c_dt_match,
	},
};

static struct spi_driver msm_otp_spi_driver = {
	.driver = {
		.name = "qcom_otp",
		.owner = THIS_MODULE,
		.of_match_table = msm_otp_dt_match,
	},
	.probe = msm_otp_spi_probe,
	.remove = msm_otp_spi_remove,
};

static int __init msm_otp_init_module(void)
{
	int rc = 0;

	CDBG("%s:%d otp E\n", __func__, __LINE__);

	rc = platform_driver_register(&msm_otp_platform_driver);
	CDBG("%s:%d otp platform rc %d\n", __func__, __LINE__, rc);

	rc = i2c_add_driver(&msm_otp_i2c_driver);
	if (rc < 0)
		pr_err("%s:%d otp probe failed\n", __func__, __LINE__);
	else
		pr_info("%s:%d otp probe succeed\n", __func__, __LINE__);

	return rc;
}

static void __exit msm_otp_exit_module(void)
{
	platform_driver_unregister(&msm_otp_platform_driver);
	spi_unregister_driver(&msm_otp_spi_driver);
	i2c_del_driver(&msm_otp_i2c_driver);
}

module_init(msm_otp_init_module);
module_exit(msm_otp_exit_module);
MODULE_DESCRIPTION("MSM OTP driver");
MODULE_LICENSE("GPL v2");