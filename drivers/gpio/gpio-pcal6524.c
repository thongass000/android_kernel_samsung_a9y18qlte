/*
 *  pcal6524.c - 16 bit I/O port
 *
 *  Copyright (C) 2005 Ben Gardner <bgardner@wabtec.com>
 *  Copyright (C) 2007 Marvell International Ltd.
 *  Copyright (C) 2013 NXP Semiconductors
 *
 *  Derived from drivers/i2c/chips/pca953x.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/regulator/consumer.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/sec_class.h>

#if defined(CONFIG_OF)
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#endif
#include <linux/i2c/pcal6524.h>

struct pcal6524_chip {
	struct i2c_client *client;
	struct gpio_chip gpio_chip;
	struct dentry	*dentry;
	struct mutex lock;
	struct pcal6524_platform_data *pdata;
	unsigned gpio_start;

	uint8_t reg_output[3];
	uint8_t reg_polarity[3];
	uint8_t reg_config[3];
	uint8_t reg_drive0[2];
	uint8_t reg_drive1[2];
	uint8_t reg_drive2[2];
	uint8_t reg_inputlatch[3];
	uint8_t reg_enpullupdown[3];
	uint8_t reg_selpullupdown[3];
//	uint8_t reg_intmask[3];
	uint8_t reg_outputconfig;
};

static struct pcal6524_chip *g_dev = NULL;

#ifdef CONFIG_SEC_PM
#define	BUF_SIZE	700
char *gpio_exp_buf;
#endif

/* read the 8-bit register from the PCAL6524
	 reg: register address
   val: the value read back from the PCAL6524
*/
static int pcal6524_read_reg(struct pcal6524_chip *chip, int reg, uint8_t *val)
{
	int ret = i2c_smbus_read_byte_data(chip->client, reg);

	if (ret < 0) {
		dev_err(&chip->client->dev,
				"failed reading register %d\n", ret);
		return ret;
	}

	*val = (uint8_t)ret;
	return 0;
}

/* write a 8-bit value to the PCAL6524
	 reg: register address
   val: the value read back from the PCAL6524
*/
static int pcal6524_write_reg(struct pcal6524_chip *chip, int reg, uint8_t val)
{
	int ret = i2c_smbus_write_byte_data(chip->client, reg, val);

	if (ret < 0) {
		dev_err(&chip->client->dev,
				"failed writing register %d\n", ret);
		return ret;
	}

	return 0;
}
/*
static void print_pcal6524_gpio_state(struct pcal6524_chip *dev)
{
	struct pcal6524_chip *chip = dev;
	struct pcal6524_chip chip_state;
	int i, drv_str;
	int quot = 0, rest = 0;
	uint8_t read_input[3];
	char buf[255];
	unsigned int size;

	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		pcal6524_read_reg(chip, PCAL6524_INPUT + i, &read_input[i]);
		pcal6524_read_reg(chip, PCAL6524_DAT_OUT + i, &chip_state.reg_output[i]);
		pcal6524_read_reg(chip, PCAL6524_CONFIG + i, &chip_state.reg_config[i]);
		if (i < 2) {
			pcal6524_read_reg(chip, PCAL6524_DRIVE0 + i, &chip_state.reg_drive0[i]);
			pcal6524_read_reg(chip, PCAL6524_DRIVE1 + i, &chip_state.reg_drive1[i]);
			pcal6524_read_reg(chip, PCAL6524_DRIVE2 + i, &chip_state.reg_drive2[i]);
		}
		pcal6524_read_reg(chip, PCAL6524_EN_PULLUPDOWN + i, &chip_state.reg_enpullupdown[i]);
		pcal6524_read_reg(chip, PCAL6524_SEL_PULLUPDOWN + i, &chip_state.reg_selpullupdown[i]);
	}

	for (i = 0; i <= PCAL6524_MAX_GPIO; i++) {
		memset(buf, 0, sizeof(char) * 255);
		size = 0;
		size += sprintf(&buf[size], "Expander[3%02d]", i);
		quot = i / 8;
		rest = i % 8;

		if ((chip_state.reg_config[quot] >> rest) & 0x1)
			size += sprintf(&buf[size], " IN");
		else {
			if ((chip_state.reg_output[quot] >> rest) & 0x1)
				size += sprintf(&buf[size], " OUT_HIGH");
			else
				size += sprintf(&buf[size], " OUT_LOW");
		}

		if ((chip_state.reg_enpullupdown[quot] >> rest) & 0x1) {
			if ((chip_state.reg_selpullupdown[quot] >> rest) & 0x1)
				size += sprintf(&buf[size], " PULL_UP");
			else
				size += sprintf(&buf[size], " PULL_DOWN");
		} else
			size += sprintf(&buf[size], " PULL_NONE");

		if (quot == 2) {
			if (rest > 3)
				drv_str = (chip_state.reg_drive2[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive2[0] >> ((rest)*2)) & 0x3;
		} else if (quot == 1) {
			if (rest > 3)
				drv_str = (chip_state.reg_drive1[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive1[0] >> ((rest)*2)) & 0x3;
		} else {
			if (rest > 3)
				drv_str = (chip_state.reg_drive0[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive0[0] >> ((rest)*2)) & 0x3;
		}

		switch(drv_str) {
		case GPIO_CFG_6_25MA:
			size += sprintf(&buf[size], " DRV_6.25mA");
			break;
		case GPIO_CFG_12_5MA:
			size += sprintf(&buf[size], " DRV_12.5mA");
			break;
		case GPIO_CFG_18_75MA:
			size += sprintf(&buf[size], " DRV_18.75mA");
			break;
		case GPIO_CFG_25MA:
			size += sprintf(&buf[size], " DRV_25mA");
			break;
		}

		if ((read_input[quot] >> rest) & 0x1)
			size += sprintf(&buf[size], " VAL_HIGH\n");
		else
			size += sprintf(&buf[size], " VAL_LOW\n");

		pr_info("%s : %s\n", __func__, buf); 
	}

	return;
}
*/

#ifdef CONFIG_SEC_PM
ssize_t get_gpio_exp(char *buf)
{
	struct pcal6524_chip chip_state;
	int i, drv_str;
	int quot = 0, rest = 0;
	uint8_t read_input[3];
	size_t size = 0;

	if (buf == NULL)
		buf = gpio_exp_buf;

	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		pcal6524_read_reg(g_dev, PCAL6524_INPUT + i, &read_input[i]);
		pcal6524_read_reg(g_dev, PCAL6524_DAT_OUT + i, &chip_state.reg_output[i]);
		pcal6524_read_reg(g_dev, PCAL6524_CONFIG + i, &chip_state.reg_config[i]);
		if (i < 2) {
			pcal6524_read_reg(g_dev, PCAL6524_DRIVE0 + i, &chip_state.reg_drive0[i]);
			pcal6524_read_reg(g_dev, PCAL6524_DRIVE1 + i, &chip_state.reg_drive1[i]);
			pcal6524_read_reg(g_dev, PCAL6524_DRIVE2 + i, &chip_state.reg_drive2[i]);
		}
		pcal6524_read_reg(g_dev, PCAL6524_EN_PULLUPDOWN + i, &chip_state.reg_enpullupdown[i]);
		pcal6524_read_reg(g_dev, PCAL6524_SEL_PULLUPDOWN + i, &chip_state.reg_selpullupdown[i]);
	}
	for (i = 0; i <= PCAL6524_MAX_GPIO; i++) {
		size += sprintf(&buf[size], "[%02d]", i);
		quot = i / 8;
		rest = i % 8;
		
		if ((chip_state.reg_config[quot] >> rest) & 0x1) {
			if ((read_input[quot] >> rest) & 0x1)
				size += sprintf(&buf[size], " IN_H");
			else
				size += sprintf(&buf[size], " IN_L");
		}
		else {
			if ((chip_state.reg_output[quot] >> rest) & 0x1)
				size += sprintf(&buf[size], " OUT_H");
			else
				size += sprintf(&buf[size], " OUT_L");
		}
		if ((chip_state.reg_enpullupdown[quot] >> rest) & 0x1) {
			if ((chip_state.reg_selpullupdown[quot] >> rest) & 0x1)
				size += sprintf(&buf[size], " PU");
			else
				size += sprintf(&buf[size], " PD");
		} else
			size += sprintf(&buf[size], " PN");

		if (quot == 2) {
			if (rest > 3)
				drv_str = (chip_state.reg_drive2[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive2[0] >> ((rest)*2)) & 0x3;
		} else if (quot == 1) {
			if (rest > 3)
				drv_str = (chip_state.reg_drive1[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive1[0] >> ((rest)*2)) & 0x3;
		} else {
			if (rest > 3)
				drv_str = (chip_state.reg_drive0[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive0[0] >> ((rest)*2)) & 0x3;
		}
		switch(drv_str) {
		case GPIO_CFG_6_25MA:
			size += sprintf(&buf[size], " DRV_6.25mA");
			break;
		case GPIO_CFG_12_5MA:
			size += sprintf(&buf[size], " DRV_12.5mA");
			break;
		case GPIO_CFG_18_75MA:
			size += sprintf(&buf[size], " DRV_18.75mA");
			break;
		case GPIO_CFG_25MA:
			size += sprintf(&buf[size], " DRV_25mA");
			break;
		}
		size += sprintf(&buf[size], "\n");
	}
	pr_info("[%s]\n%s\n", __func__, buf);

	return size;
}
EXPORT_SYMBOL(get_gpio_exp);
#endif

void pcal6524_sync_structure_with_register(struct pcal6524_chip *dev)
{
	struct pcal6524_chip *chip = dev;
	int i;

	pr_info(
"%s \n", __func__);
	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		pcal6524_read_reg(chip, PCAL6524_DAT_OUT + i, &chip->reg_output[i]);
		pcal6524_read_reg(chip, PCAL6524_CONFIG + i, &chip->reg_config[i]);
		if (i < 2) {
			pcal6524_read_reg(chip, PCAL6524_DRIVE0 + i, &chip->reg_drive0[i]);
			pcal6524_read_reg(chip, PCAL6524_DRIVE1 + i, &chip->reg_drive1[i]);
			pcal6524_read_reg(chip, PCAL6524_DRIVE2 + i, &chip->reg_drive2[i]);
		}
		pcal6524_read_reg(chip, PCAL6524_EN_PULLUPDOWN + i, &chip->reg_enpullupdown[i]);
		pcal6524_read_reg(chip, PCAL6524_SEL_PULLUPDOWN + i, &chip->reg_selpullupdown[i]);
	}	
	return;
}
/* read a port pin value (INPUT register) from the PCAL6524
   off: bit number (0..23)
   return: bit value 0 or 1
*/
static int pcal6524_gpio_get_value(struct gpio_chip *gc, unsigned off)
{
	int ret;
	int quot = off / 8;
	int rest = off % 8;
	uint8_t reg_val;

	struct pcal6524_chip *chip
		= container_of(gc, struct pcal6524_chip, gpio_chip);

	mutex_lock(&chip->lock);
	ret = pcal6524_read_reg(chip, PCAL6524_INPUT + quot, &reg_val);
	if (ret < 0) {
		/* NOTE:  diagnostic already emitted; that's all we should
		 * do unless gpio_*_value_cansleep() calls become different
		 * from their nonsleeping siblings (and report faults).
		 */
		mutex_unlock(&chip->lock);
		return ret;
	}

	mutex_unlock(&chip->lock);
	return (reg_val & (1u << rest)) ? 1 : 0;
}

/* write a port pin value (INPUT register) from the PCAL6524
   off: bit number (0..23)
   val: 0 or 1
   return: none
*/
static void pcal6524_gpio_set_value(struct gpio_chip *gc,
					unsigned off, int val)
{
	int quot = off / 8;
	int rest = off % 8;
	struct pcal6524_chip *chip
		= container_of(gc, struct pcal6524_chip, gpio_chip);

	mutex_lock(&chip->lock);
	if (val)
		chip->reg_output[quot] |= (1u << rest);
	else
		chip->reg_output[quot] &= ~(1u << rest);

	pcal6524_write_reg(chip, PCAL6524_DAT_OUT + quot, chip->reg_output[quot]);
	pr_info("%s : off =%d, val=%d\n",__func__ ,off , val);
	mutex_unlock(&chip->lock);
}

/* set the CONFIGURATION register of a port pin as an input
   off: bit number (0..23)
*/
static int pcal6524_gpio_direction_input(struct gpio_chip *gc, unsigned off)
{
	int ret;
	int quot = off / 8;
	int rest = off % 8;

	struct pcal6524_chip *chip
		= container_of(gc, struct pcal6524_chip, gpio_chip);

	pr_info("%s(off=[%d])\n", __func__, off);
	mutex_lock(&chip->lock);
	/* input set bit 1  */
	chip->reg_config[quot] |= (1u << rest);
	ret = pcal6524_write_reg(chip, PCAL6524_CONFIG + quot, chip->reg_config[quot]);
	mutex_unlock(&chip->lock);
	return ret;
}

/* set the DIRECTION (CONFIGURATION register) of a port pin as an output
   off: bit number (0..23)
   val = 1 or 0
   return: 0 if successful
*/
static int pcal6524_gpio_direction_output(struct gpio_chip *gc,
					unsigned off, int val)
{
	int ret;
	int quot = off / 8;
	int rest = off % 8;

	struct pcal6524_chip *chip
		= container_of(gc, struct pcal6524_chip, gpio_chip);

	pr_info("%s(off=[%d], val=[%d])\n", __func__, off, val);
	mutex_lock(&chip->lock);
	/* set output level */
	if (val)
		chip->reg_output[quot] |= (1u << rest);
	else
		chip->reg_output[quot] &= ~(1u << rest);
	ret = pcal6524_write_reg(chip, PCAL6524_DAT_OUT + quot, chip->reg_output[quot]);

	/* then direction */
	/* output set bit 0  */
	chip->reg_config[quot] &= ~(1u << rest);
	ret |= pcal6524_write_reg(chip, PCAL6524_CONFIG + quot, chip->reg_config[quot]);
	mutex_unlock(&chip->lock);

	return ret;
}

static int pcal6524_gpio_request(struct gpio_chip *gc, unsigned off)
{
	struct pcal6524_chip *chip
		= container_of(gc, struct pcal6524_chip, gpio_chip);

	pr_info("%s gpio %d \n", __func__, off);
	if (off >= chip->gpio_chip.ngpio) {
		pr_err("[%s] offset over max = [%d]\n", __func__, off);
		return 1;
	}
	/* to do*/
	return 0;
}

static void pcal6524_gpio_free(struct gpio_chip *gc, unsigned off)
{
	struct pcal6524_chip *chip
		= container_of(gc, struct pcal6524_chip, gpio_chip);

	pr_info("%s gpio %d \n", __func__, off);
	if (off >= chip->gpio_chip.ngpio) {
		pr_err("[%s] offset over max = [%d]\n", __func__, off);
	}
	/* to do*/
}

static int pcal6524_gpio_setup(struct pcal6524_chip *dev)
{
	int ret, i;
	pr_info("[%s] GPIO Expander Init setting\n", __func__);

	dev->reg_outputconfig = 0x00;
	ret = pcal6524_write_reg(dev, PCAL6524_OUTPUT_CONFIG, dev->reg_outputconfig);		/* push-pull */
	if (ret < 0) {
		pr_err("failed set output config\n");
		return ret;
	}

	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		ret = pcal6524_write_reg(dev, PCAL6524_DAT_OUT + i, dev->reg_output[i]);		/* 1 : output high, 0 : output low */
		if (ret < 0) {
			pr_err("failed set data out\n");
			return ret;
		}
	}
	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		ret = pcal6524_write_reg(dev, PCAL6524_CONFIG + i, dev->reg_config[i]);		/* 1 : input, 0 : output */
		if (ret < 0) {
			pr_err("failed set config\n");
			return ret;
		}
	}

	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		dev->reg_polarity[i] = 0x00;
		ret = pcal6524_write_reg(dev, PCAL6524_POLARITY + i, dev->reg_polarity[i]);
		if (ret < 0) {
			pr_err("failed set polarity\n");
			return ret;
		}
	}

	for (i = 0; i < 2; i++) {
		dev->reg_drive0[i] = 0x00;
		ret = pcal6524_write_reg(dev, PCAL6524_DRIVE0 + i, dev->reg_drive0[i]);		/* drive 0.25x */
		if (ret < 0) {
			pr_err("failed set drive0\n");
			return ret;
		}
	}
	for (i = 0; i < 2; i++) {
		dev->reg_drive1[i] = 0x00;
		ret = pcal6524_write_reg(dev, PCAL6524_DRIVE1 + i, dev->reg_drive1[i]);		/* drive 0.25x */
		if (ret < 0) {
			pr_err("failed set drive1\n");
			return ret;
		}
	}
	for (i = 0; i < 2; i++) {
		dev->reg_drive2[i] = 0x00;
		ret = pcal6524_write_reg(dev, PCAL6524_DRIVE2 + i, dev->reg_drive2[i]);		/* drive 0.25x */
		if (ret < 0) {
			pr_err("failed set drive1\n");
			return ret;
		}
	}

	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		dev->reg_inputlatch[i] = 0x00;
		ret = pcal6524_write_reg(dev, PCAL6524_INPUT_LATCH + i, dev->reg_inputlatch[i]);		/* not use latch */
		if (ret < 0) {
			pr_err("failed set input latch\n");
			return ret;
		}
	}

	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		ret = pcal6524_write_reg(dev, PCAL6524_SEL_PULLUPDOWN + i, dev->reg_selpullupdown[i]);	/* 1 : pull-up, 0 : pull-down */
		if (ret < 0) {
			pr_err("failed set select pull\n");
			return ret;
		}
	}
	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		ret = pcal6524_write_reg(dev, PCAL6524_EN_PULLUPDOWN + i, dev->reg_enpullupdown[i]);		/* 1 : enable, 0 : disable */
		if (ret < 0) {
			pr_err("failed set enable pullupdown\n");
			return ret;
		}
	}
#if 0
	dev->reg_intmask = 0xFFFF;
	ret = pcal6524_write_reg(dev, PCAL6524_INT_MASK,
			dev->reg_intmask);		/* not use int */
	if (ret < 0) {
		pr_err("failed set int mask\n");
		return ret;
	}
	ret = pcal6524_read_reg(dev, PCAL6524_INT_MASK,
			&read_val);
	if (ret < 0) {
		pr_err("failed read int mask\n");
		return ret;
	}
#endif

	return 0;
}

#ifdef CONFIG_OF
static int pcal6524_parse_dt(struct device *dev,
		struct pcal6524_platform_data *pdata)
{
	struct device_node *np = dev->of_node;
	struct pinctrl *reset_pinctrl;
	int ret, i, j;
	u32 pull_reg[3];

	ret = of_property_read_u32(np, "pcal6524,gpio_start", &pdata->gpio_start);
	if (ret < 0) {
		pr_err("[%s]: Unable to read pcal6524,gpio_start\n", __func__);
		return ret;
	}

	ret = of_property_read_u32(np, "pcal6524,ngpio", &pdata->ngpio);
	if (ret < 0) {
		pr_err("[%s]: Unable to read pcal6524,ngpio\n", __func__);
		return ret;
	}
	pdata->reset_gpio = of_get_named_gpio(np, "pcal6524,reset-gpio", 0);
	/* Get pinctrl if target uses pinctrl */
	reset_pinctrl = devm_pinctrl_get_select(dev, "expander_reset_setting");
	if (IS_ERR(reset_pinctrl)) {
		if (PTR_ERR(reset_pinctrl) == -EPROBE_DEFER)
			return -EPROBE_DEFER;

		pr_debug("Target does not use pinctrl\n");
		reset_pinctrl = NULL;
	}

	ret = of_property_read_u32(np, "pcal6524,support_initialize", (u32 *)&pdata->support_init);
	if (ret < 0) {
		pr_err("[%s]: Unable to read pcal6524,support_init\n", __func__);
		pdata->support_init = 0;
	}

	pdata->vdd = devm_regulator_get(dev, "pcal6524,vdd");
	if (IS_ERR(pdata->vdd)) {
		pr_err("%s: cannot get pcal6524,vdd\n", __func__);
		ret = -ENOMEM;
		return ret;
	} else if (!regulator_get_voltage(pdata->vdd)) {
		ret = regulator_set_voltage(pdata->vdd, 1800000, 1800000);
		if (ret < 0) {
			pr_err("regulator set voltage failed, %d\n",
				ret);
			return ret;
		}
	}

	if (pdata->support_init) {
		ret = of_property_read_u32(np, "pcal6524,config", (u32 *)&pdata->init_config);
		if (ret < 0) {
			pr_err("[%s]: Unable to read pcal6524,support_init\n", __func__);
			pdata->init_config= 0x000000;
		}
		ret = of_property_read_u32(np, "pcal6524,data_out", (u32 *)&pdata->init_data_out);
		if (ret < 0) {
			pr_err("[%s]: Unable to read pcal6524,support_init\n", __func__);
			pdata->init_data_out = 0x000000;
		}
		ret = of_property_read_u32(np, "pcal6524,pull_reg_p0", &pull_reg[0]);
		if (ret < 0) {
			pr_err("[%s]: Unable to read pcal6524,pull_reg_p0\n", __func__);
		}
		ret = of_property_read_u32(np, "pcal6524,pull_reg_p1", &pull_reg[1]);
		if (ret < 0) {
			pr_err("[%s]: Unable to read pcal6524,pull_reg_p1\n", __func__);
		}
		ret = of_property_read_u32(np, "pcal6524,pull_reg_p2", &pull_reg[2]);
		if (ret < 0) {
			pr_err("[%s]: Unable to read pcal6524,pull_reg_p2\n", __func__);
		}
		pr_info("[%s] Pull reg P0[0x%04x] P1[0x%04x] P2[0x%04x]\n", __func__, pull_reg[0], pull_reg[1], pull_reg[2]);
		pdata->init_en_pull = 0x000000;
		pdata->init_sel_pull = 0x000000;
		for (j = 0; j < 3; j++){
			for (i = 0; i < 8; i++) {
				if (((pull_reg[j]>>(i*2))&0x3) == NO_PULL) {
					pdata->init_en_pull &= ~(1 << (i + (8 * j)));
					pdata->init_sel_pull &= ~(1 << (i + (8 * j)));
				}
				else if (((pull_reg[j]>>(i*2))&0x3) == PULL_DOWN) {
					pdata->init_en_pull |= (1 << (i + (8 * j)));
					pdata->init_sel_pull &= ~(1 << (i + (8 * j)));
				}
				else if (((pull_reg[j]>>(i*2))&0x3) == PULL_UP) {
					pdata->init_en_pull |= (1 << (i + (8 * j)));
					pdata->init_sel_pull |= (1 << (i + (8 * j)));
				}
			}
		}
	} else {
		pdata->init_config = 0x000000;
		pdata->init_data_out = 0x000000;
		pdata->init_en_pull = 0x000000;
		pdata->init_sel_pull = 0x000000;
	}

	pr_info("[%s] initialize reg 0x%06x 0x%06x 0x%06x 0x%06x\n", __func__,
			pdata->init_config, pdata->init_data_out,
			pdata->init_en_pull, pdata->init_sel_pull);
	dev->platform_data = pdata;
	pr_info("[%s]gpio_start=[%d]ngpio=[%d]reset-gpio=[%d]\n",
			__func__, pdata->gpio_start, pdata->ngpio,
			pdata->reset_gpio);
	return 0;
}
#endif

static void pcal6524_power_ctrl(struct pcal6524_platform_data *pdata, char enable)
{
	int ret = 0;
	struct regulator *reg_power = pdata->vdd;

	if (enable) {
		if (regulator_is_enabled(reg_power))
			pr_err("%s: power regulator(1.8V) is enabled\n", __func__);
		else
			ret = regulator_enable(reg_power);
		if (ret) {
			pr_err("%s: power regulator enable failed, rc=%d\n",
					__func__, ret);
			return;
		}
		pr_info("%s: gpio expander 1.8V on is finished.\n", __func__);
	} else {
		if (regulator_is_enabled(reg_power))
			ret = regulator_disable(reg_power);
		else
			pr_err("%s: power regulator(1.8V) is disabled\n", __func__);
		if (ret) {
			pr_err("%s: disable power regulator failed, rc=%d\n",
					__func__, ret);
			return;
		}
		pr_info("%s: gpio expander 1.8V off is finished.\n", __func__);
	}
	pr_err("[pcal6524 gpio expander] %s enable(%d)\n", __func__, enable);
	return;
}

static int pcal6524_reset_chip(struct pcal6524_platform_data *pdata)
{
	int retval;
	int reset_gpio = pdata->reset_gpio;

	if (gpio_is_valid(reset_gpio)) {
		retval = gpio_request(reset_gpio, "pcal6524_reset_gpio");
		if (retval) {
			pr_err("[%s]: unable to request gpio [%d]\n",
					__func__, reset_gpio);
			return retval;
		}

		retval = gpio_direction_output(reset_gpio, 1);
		if (retval) {
			pr_err("[%s]: unable to set direction for gpio [%d]\n",
					__func__, reset_gpio);
			gpio_free(reset_gpio);
			return retval;
		}

		usleep_range(100, 100);
		gpio_set_value(reset_gpio, 0);
		usleep_range(100, 100);
		gpio_set_value(reset_gpio, 1);
		pr_info("[%s]: gpio expander reset.\n", __func__);

		gpio_free(reset_gpio);
		return 0;
	} else {
		pr_err("[%s]: gpio_is_valid fail\n", __func__);
		return -EIO;
	}
	return 0;
}

static ssize_t store_pcal6524_gpio_inout(struct device *dev,
		struct device_attribute *devattr,
		const char *buf, size_t count)
{
	int retval, off, val, gpio_pcal6524;
	char in_out, msg[13];
	struct pcal6524_chip *data = dev_get_drvdata(dev);

	retval = sscanf(buf, "%1c %3d %1d", &in_out, &off, &val);
	if (retval == 0) {
		dev_err(&data->client->dev, "[%s] fail to pcal6524 out.\n", __func__);
		return count;
	}
	if (!(in_out == 'i' || in_out == 'o')) {
		pr_err("[%s] wrong in_out value [%c]\n", __func__,  in_out);
		return count;
	}
	if ((off < 0) || (off > PCAL6524_MAX_GPIO)) {
		pr_err("[%s] wrong offset value [%d]\n", __func__, off);
		return count;
	}
	if (!(val == 0 || val == 1)) {
		pr_err("[%s] wrong val value [%d]\n", __func__, val);
		return count;
	}

	gpio_pcal6524 = data->gpio_start + off;
	snprintf(msg, sizeof(msg)/sizeof(char), "exp-gpio%d\n", off);
	if (gpio_is_valid(gpio_pcal6524)) {
		retval = gpio_request(gpio_pcal6524, msg);
		if (retval) {
			pr_err("[%s] unable to request gpio=[%d] err=[%d]\n",
					__func__, gpio_pcal6524, retval);
			return count;
		}

		if (in_out == 'i') {
			retval = gpio_direction_input(gpio_pcal6524);
			val = gpio_get_value(gpio_pcal6524);
		}
		else
			retval = gpio_direction_output(gpio_pcal6524, val);

		if (retval)
			pr_err("%s: unable to set direction for gpio [%d]\n",
						__func__, gpio_pcal6524);

		gpio_free(gpio_pcal6524);
	}

	pr_info("pcal6524 mode set to dir[%c], offset[%d], val[%d]\n", in_out, off, val);

	return count;
}

static ssize_t show_pcal6524_gpio_state(struct device *dev,
		struct device_attribute *devattr, char *buf)
{
	struct pcal6524_chip *chip = dev_get_drvdata(dev);
	struct pcal6524_chip chip_state;
	int i, drv_str;
	int quot = 0, rest = 0;
	uint8_t read_input[3];
	size_t size = 0;

	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		pcal6524_read_reg(chip, PCAL6524_INPUT + i, &read_input[i]);
		pcal6524_read_reg(chip, PCAL6524_DAT_OUT + i, &chip_state.reg_output[i]);
		pcal6524_read_reg(chip, PCAL6524_CONFIG + i, &chip_state.reg_config[i]);
		if (i < 2) {
			pcal6524_read_reg(chip, PCAL6524_DRIVE0 + i, &chip_state.reg_drive0[i]);
			pcal6524_read_reg(chip, PCAL6524_DRIVE1 + i, &chip_state.reg_drive1[i]);
			pcal6524_read_reg(chip, PCAL6524_DRIVE2 + i, &chip_state.reg_drive2[i]);
		}
		pcal6524_read_reg(chip, PCAL6524_EN_PULLUPDOWN + i, &chip_state.reg_enpullupdown[i]);
		pcal6524_read_reg(chip, PCAL6524_SEL_PULLUPDOWN + i, &chip_state.reg_selpullupdown[i]);
	}

	for (i = 0; i <= PCAL6524_MAX_GPIO; i++) {
		size += sprintf(&buf[size], "Expander[3%02d]", i);
		quot = i / 8;
		rest = i % 8;

		if ((chip_state.reg_config[quot] >> rest) & 0x1)
			size += sprintf(&buf[size], " IN");
		else {
			if ((chip_state.reg_output[quot] >> rest) & 0x1)
				size += sprintf(&buf[size], " OUT_HIGH");
			else
				size += sprintf(&buf[size], " OUT_LOW");
		}

		if ((chip_state.reg_enpullupdown[quot] >> rest) & 0x1) {
			if ((chip_state.reg_selpullupdown[quot] >> rest) & 0x1)
				size += sprintf(&buf[size], " PULL_UP");
			else
				size += sprintf(&buf[size], " PULL_DOWN");
		} else
			size += sprintf(&buf[size], " PULL_NONE");

		if (quot == 2) {
			if (rest > 3)
				drv_str = (chip_state.reg_drive2[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive2[0] >> ((rest)*2)) & 0x3;
		} else if (quot == 1) {
			if (rest > 3)
				drv_str = (chip_state.reg_drive1[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive1[0] >> ((rest)*2)) & 0x3;
		} else {
			if (rest > 3)
				drv_str = (chip_state.reg_drive0[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive0[0] >> ((rest)*2)) & 0x3;
		}

		switch(drv_str) {
		case GPIO_CFG_6_25MA:
			size += sprintf(&buf[size], " DRV_6.25mA");
			break;
		case GPIO_CFG_12_5MA:
			size += sprintf(&buf[size], " DRV_12.5mA");
			break;
		case GPIO_CFG_18_75MA:
			size += sprintf(&buf[size], " DRV_18.75mA");
			break;
		case GPIO_CFG_25MA:
			size += sprintf(&buf[size], " DRV_25mA");
			break;
		}

		if ((read_input[quot] >> rest) & 0x1)
			size += sprintf(&buf[size], " VAL_HIGH\n");
		else
			size += sprintf(&buf[size], " VAL_LOW\n");
	}

	return size;
}

static DEVICE_ATTR(expgpio, 0644,
		show_pcal6524_gpio_state, store_pcal6524_gpio_inout);

static int expander_show(struct seq_file *s, void *unused)
{
	struct pcal6524_chip chip_state;
	int i, drv_str;
	int quot = 0, rest = 0;
	uint8_t read_input[3];

	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		pcal6524_read_reg(g_dev, PCAL6524_INPUT + i, &read_input[i]);
		pcal6524_read_reg(g_dev, PCAL6524_DAT_OUT + i, &chip_state.reg_output[i]);
		pcal6524_read_reg(g_dev, PCAL6524_CONFIG + i, &chip_state.reg_config[i]);
		if (i < 2) {
			pcal6524_read_reg(g_dev, PCAL6524_DRIVE0 + i, &chip_state.reg_drive0[i]);
			pcal6524_read_reg(g_dev, PCAL6524_DRIVE1 + i, &chip_state.reg_drive1[i]);
			pcal6524_read_reg(g_dev, PCAL6524_DRIVE2 + i, &chip_state.reg_drive2[i]);
		}
		pcal6524_read_reg(g_dev, PCAL6524_EN_PULLUPDOWN + i, &chip_state.reg_enpullupdown[i]);
		pcal6524_read_reg(g_dev, PCAL6524_SEL_PULLUPDOWN + i, &chip_state.reg_selpullupdown[i]);
	}

	for (i = 0; i <= PCAL6524_MAX_GPIO; i++) {
		seq_printf(s, "Expander[3%02d]", i);
		quot = i / 8;
		rest = i % 8;

		if ((chip_state.reg_config[quot] >> rest) & 0x1)
			seq_printf(s, " IN");
		else {
			if ((chip_state.reg_output[quot] >> rest) & 0x1)
				seq_printf(s, " OUT_HIGH");
			else
				seq_printf(s, " OUT_LOW");
		}

		if ((chip_state.reg_enpullupdown[quot] >> rest) & 0x1) {
			if ((chip_state.reg_selpullupdown[quot] >> rest) & 0x1)
				seq_printf(s, " PULL_UP");
			else
				seq_printf(s, " PULL_DOWN");
		} else
			seq_printf(s, " PULL_NONE");

		if (quot == 2) {
			if (rest > 3)
				drv_str = (chip_state.reg_drive2[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive2[0] >> ((rest)*2)) & 0x3;
		} else if (quot == 1) {
			if (rest > 3)
				drv_str = (chip_state.reg_drive1[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive1[0] >> ((rest)*2)) & 0x3;
		} else {
			if (rest > 3)
				drv_str = (chip_state.reg_drive0[1] >> ((rest-4)*2)) & 0x3;
			else
				drv_str = (chip_state.reg_drive0[0] >> ((rest)*2)) & 0x3;
		}

		switch(drv_str) {
		case GPIO_CFG_6_25MA:
			seq_printf(s, " DRV_6.25mA");
			break;
		case GPIO_CFG_12_5MA:
			seq_printf(s, " DRV_12.5mA");
			break;
		case GPIO_CFG_18_75MA:
			seq_printf(s, " DRV_18.75mA");
			break;
		case GPIO_CFG_25MA:
			seq_printf(s, " DRV_25mA");
			break;
		}

		if ((read_input[quot] >> rest) & 0x1)
			seq_printf(s, " VAL_HIGH\n");
		else
			seq_printf(s, " VAL_LOW\n");
	}

	return 0;
}
static int expander_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, expander_show, NULL);
}

static const struct file_operations expander_operations = {
	.open		= expander_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int pcal6524_gpio_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	struct pcal6524_platform_data *pdata = NULL;
	struct pcal6524_chip *dev;
	struct gpio_chip *gc;
	struct dentry *debugfs_file;
	int ret, i;
	int retry;

	pr_info("[%s]\n", __func__);
	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "SMBUS Byte Data not Supported\n");
		return -EIO;
	}
#ifdef CONFIG_OF
	if (np) {
		pdata = devm_kzalloc(&client->dev,
				sizeof(struct pcal6524_platform_data),
				GFP_KERNEL);
		if (!pdata) {
			dev_err(&client->dev, "Failed to allocate memory\n");
			return -ENOMEM;
		}

		ret = pcal6524_parse_dt(&client->dev, pdata);
		if (ret) {
			pr_err("[%s] pcal6524 parse dt failed\n", __func__);
			return ret;
		}

	} else {
		pdata = client->dev.platform_data;
		pr_info("GPIO Expender failed to align dtsi %s",
				__func__);
	}
#else
	pdata = client->dev.platform_data;
#endif
	if (pdata == NULL) {
		dev_err(&client->dev, "missing platform data\n");
		return -ENODEV;
	}
	pcal6524_power_ctrl(pdata, POWER_ON);

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (dev == NULL) {
		dev_err(&client->dev, "failed to alloc memory\n");
		return -ENOMEM;
	}

	dev->client = client;
	dev->pdata = pdata;
	dev->gpio_start = pdata->gpio_start;

#ifdef CONFIG_SEC_PM
	gpio_exp_buf = kzalloc(sizeof(char) * BUF_SIZE, GFP_KERNEL);
#endif

	gc = &dev->gpio_chip;
	gc->direction_input  = pcal6524_gpio_direction_input;
	gc->direction_output = pcal6524_gpio_direction_output;
	gc->get = pcal6524_gpio_get_value;
	gc->set = pcal6524_gpio_set_value;
	gc->request = pcal6524_gpio_request;
	gc->free = pcal6524_gpio_free;
	gc->can_sleep = 0;
	/*Require dev to use the of_gpio api*/
	gc->dev = &client->dev;
	gc->base = pdata->gpio_start;
	gc->ngpio = pdata->ngpio;
	gc->label = client->name;
	gc->owner = THIS_MODULE;

	mutex_init(&dev->lock);

	for (i = 0; i < PCAL6524_PORT_CNT; i++) {
		dev->reg_config[i] = (pdata->init_config >> (8*i)) & 0xFF;
		dev->reg_output[i] = (pdata->init_data_out >> (8*i)) & 0xFF;
		dev->reg_enpullupdown[i] = (pdata->init_en_pull >> (8*i)) & 0xFF;
		dev->reg_selpullupdown[i] = (pdata->init_sel_pull >> (8*i)) & 0xFF;
	}

	retry = 0;
	if (0) { 
		while(0) { // do not reset
			ret = pcal6524_reset_chip(pdata);
			if (ret) {
				pr_err("[%s]reset control fail\n", __func__);
			} else {
				ret = pcal6524_gpio_setup(dev);
				if (ret) {
					dev_err(&client->dev,
						"expander setup i2c retry [%d]\n", retry);
				} else {
					pr_info("[%s]Expander setup success [%d]\n",
							__func__, retry);
					break;
				}
			}
			if (retry++ > 5) {
				dev_err(&client->dev,
						"Failed to expander retry[%d]\n", retry);
				panic("pcal6524 i2c fail, check HW!\n");
				goto err;

			}
			usleep_range(100, 200);
		}
	}else {
		pcal6524_sync_structure_with_register(dev);
	}

	ret = gpiochip_add(&dev->gpio_chip);
	if (ret)
		goto err;

	dev_info(&client->dev, "gpios %d..%d on a %s\n",
			gc->base, gc->base + gc->ngpio - 1,
			client->name);

	pcal6524_dev = sec_device_create(0, dev, "expander");
	if (IS_ERR(pcal6524_dev)) {
		dev_err(&client->dev,
				"Failed to create device for expander\n");
		ret = -ENODEV;
		goto err;
	}

	ret = sysfs_create_file(&pcal6524_dev->kobj, &dev_attr_expgpio.attr);
	if (ret) {
		dev_err(&client->dev,
				"Failed to create sysfs group for expander\n");
		goto err_destroy;
	}

	dev->dentry = debugfs_create_dir("expander", NULL);
	if (IS_ERR_OR_NULL(dev->dentry)) {
		dev_err(&client->dev,
				"Failed to create debugfs dir for expander\n");
		goto err_debug_dir;

	}
	debugfs_file = debugfs_create_file("gpio", S_IFREG | S_IRUGO,
			dev->dentry, NULL, &expander_operations);
	if (IS_ERR_OR_NULL(debugfs_file)) {
		dev_err(&client->dev,
				"Failed to create debugfs file for gpio\n");
		goto err_debug_file;
	}

	i2c_set_clientdata(client, dev);
	g_dev = dev;

	return 0;

err_debug_file:
	debugfs_remove_recursive(dev->dentry);
err_debug_dir:
	sysfs_remove_file(&pcal6524_dev->kobj, &dev_attr_expgpio.attr);
err_destroy:
	sec_device_destroy(pcal6524_dev->devt);
err:
	pcal6524_power_ctrl(pdata, POWER_OFF);
	mutex_destroy(&dev->lock);
#ifdef CONFIG_SEC_PM
	kfree(gpio_exp_buf);
#endif
	kfree(dev);
	return ret;
}

static int pcal6524_gpio_remove(struct i2c_client *client)
{
	struct pcal6524_chip *dev = i2c_get_clientdata(client);

	pcal6524_power_ctrl(dev->pdata, POWER_OFF);
	gpiochip_remove(&dev->gpio_chip);
	if (!IS_ERR_OR_NULL(dev->dentry))
		debugfs_remove_recursive(dev->dentry);
	sysfs_remove_file(&pcal6524_dev->kobj, &dev_attr_expgpio.attr);
	mutex_destroy(&dev->lock);
#ifdef CONFIG_SEC_PM
	kfree(gpio_exp_buf);
#endif
	kfree(dev);
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id pcal6524_dt_ids[] = {
	{ .compatible = "pcal6524,gpio-expander",},
	{ /* sentinel */ },
};
#endif
static const struct i2c_device_id pcal6524_gpio_id[] = {
	{DRV_NAME, 0, },
	{ }
};
MODULE_DEVICE_TABLE(i2c, pcal6524_gpio_id);

static struct i2c_driver pcal6524_gpio_driver = {
	.driver = {
		   .name = DRV_NAME,
#ifdef CONFIG_OF
		   .of_match_table = of_match_ptr(pcal6524_dt_ids),
#endif
		   },
	.probe = pcal6524_gpio_probe,
	.remove = pcal6524_gpio_remove,
	.id_table = pcal6524_gpio_id,
};

static int __init pcal6524_gpio_init(void)
{
	return i2c_add_driver(&pcal6524_gpio_driver);
}

subsys_initcall(pcal6524_gpio_init);

static void __exit pcal6524_gpio_exit(void)
{
	i2c_del_driver(&pcal6524_gpio_driver);
}

module_exit(pcal6524_gpio_exit);

MODULE_DESCRIPTION("GPIO expander driver for PCAL6524");
MODULE_LICENSE("GPL");

