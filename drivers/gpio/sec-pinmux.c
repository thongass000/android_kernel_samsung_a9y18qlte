/* Copyright (c) 2010,2013-2014, The Linux Foundation. All rights reserved.
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
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/sec-pinmux.h>
#ifdef CONFIG_SEC_PM_DEBUG
#include <linux/gpio.h>
#include <linux/debugfs.h>
#endif
#ifdef CONFIG_SEC_GPIO_DVS
#include <linux/errno.h>
#include <linux/secgpio_dvs.h>
#include <linux/platform_device.h>
#endif

static DEFINE_SPINLOCK(gpiomux_lock);

/******************************************************************************
 * Define value in accordance with the specification of each BB vendor.
 ******************************************************************************/
#if defined(CONFIG_ARCH_SDM660)
  #define AP_MSM_GPIO_COUNT	114
  #define AP_LPI_GPIO_COUNT	0 // 32
  #define LPI_GPIO_NAME		"15070000.lpi_pinctrl"
#else
  #define AP_MSM_GPIO_COUNT	114
  #define AP_LPI_GPIO_COUNT	0
#endif

#define AP_GPIO_COUNT   (AP_MSM_GPIO_COUNT+AP_LPI_GPIO_COUNT)

/* GP PIN TYPE REG MASKS */
#define GPIO_PULL_SHFT             0
#define GPIO_PULL_MASK             0x3
#define GPIO_DIR_SHFT              9
#define GPIO_DIR_MASK              1
#define GPIO_FUNC_SHFT             2
#define GPIO_FUNC_MASK             0xF

/* config translations */
#define GPIO_NO_PULL          0
#define GPIO_PULL_DOWN        1
#define GPIO_PULL_UP          3

/* GP pin type register offsets */
#ifdef CONFIG_ARM64
#define GPIO_CFG_REG(base, pin)		\
	(void __iomem *)(base + 0x0 + 0x1000 * (ulong)(pin))
#define GPIO_INOUT_REG(base, pin)	\
	(void __iomem *)(base + 0x4 + 0x1000 * (ulong)(pin))
#else
#define GPIO_CFG_REG(base, pin)		\
	(void __iomem *)(base + 0x0 + 0x1000 * (pin))
#define GPIO_INOUT_REG(base, pin)	\
	(void __iomem *)(base + 0x4 + 0x1000 * (pin))
#endif

#define GET_RESULT_GPIO(a, b, c)	\
	((a<<4 & 0xF0) | (b<<1 & 0xE) | (c & 0x1))

#define GET_GPIO_IO(value)	\
	(unsigned char)((0xF0 & (value)) >> 4)
#define GET_GPIO_PUPD(value)	\
	(unsigned char)((0xE & (value)) >> 1)
#define GET_GPIO_LH(value)	\
	(unsigned char)(0x1 & (value))


#ifdef CONFIG_PINCTRL_LPI
static int find_gpiochip_by_name(struct gpio_chip *chip, void *data)
{
	const char *name = data;

	return !strcmp(chip->label, name);
}
#endif

#ifdef CONFIG_SEC_GPIO_DVS
/****************************************************************/
/* Pre-defined variables. (DO NOT CHANGE THIS!!) */
static unsigned char checkgpiomap_result[GDVS_PHONE_STATUS_MAX][AP_GPIO_COUNT];
static struct gpiomap_result gpiomap_result = {
	.init = checkgpiomap_result[PHONE_INIT],
	.sleep = checkgpiomap_result[PHONE_SLEEP]
};
/****************************************************************/
#ifdef SECGPIO_SLEEP_DEBUGGING
static struct sleepdebug_gpiotable sleepdebug_table;
#endif

static void msm_check_gpio_status(unsigned char phonestate)
{
	struct gpiomux_setting val;
	struct gpio_chip *gp = gpio_to_chip(0);
#ifdef CONFIG_PINCTRL_LPI
	struct gpio_chip *lgp = gpiochip_find(LPI_GPIO_NAME, find_gpiochip_by_name);
#endif
	u32 i;
	u8 temp_io = 0, temp_pdpu = 0, temp_lh = 0;
	pr_info("[dvs_%s] state : %s\n", __func__,
		(phonestate == PHONE_INIT) ? "init" : "sleep");

	for (i = 0; i < AP_GPIO_COUNT; i++) {
#ifdef CONFIG_PINCTRL_LPI
		if ( lgp && i >= AP_MSM_GPIO_COUNT ) {
			lpi_gp_get_cfg(lgp, (i-AP_MSM_GPIO_COUNT), &val);
			temp_lh = lpi_gp_get_value(lgp, (i-AP_MSM_GPIO_COUNT), val.dir);
		}
		else
#endif
		{
			msm_gp_get_cfg(gp, i, &val);
			temp_lh = msm_gp_get_value(gp, i, val.dir);
		}

		if (val.func == GPIOMUX_FUNC_GPIO) {
			if (val.dir == GPIOMUX_IN)
				temp_io = 0x01;	/* GPIO_IN */
			else if (val.dir == GPIOMUX_OUT_HIGH ||
					val.dir == GPIOMUX_OUT_LOW)
				temp_io = 0x02;	/* GPIO_OUT */
			else
				temp_io = 0xF;	/* not alloc. */
		} else
			temp_io = 0x0;		/* FUNC */

		switch(val.pull) {
		case GPIOMUX_PULL_NONE:
			temp_pdpu = 0x00;
			break;
		case GPIOMUX_PULL_DOWN:
			temp_pdpu = 0x01;
			break;
		case GPIOMUX_PULL_UP:
			temp_pdpu = 0x02;
			break;
		case GPIOMUX_PULL_KEEPER:
			temp_pdpu = 0x03;
			break;
		default:
			temp_pdpu = 0x07;
			break;
		}

		checkgpiomap_result[phonestate][i] =
			GET_RESULT_GPIO(temp_io, temp_pdpu, temp_lh);
	}

	pr_info("[dvs_%s]-\n", __func__);

	return;
}

#ifdef SECGPIO_SLEEP_DEBUGGING
/****************************************************************/
/* Define this function in accordance with the specification of each BB vendor */
void setgpio_for_sleepdebug(int gpionum, uint16_t  io_pupd_lh)
{
	unsigned char temp_io, temp_pupd, temp_lh;
	unsigned int temp_data;
	struct gpio_chip *gp = gpio_to_chip(0);

	pr_info("[dvs_%s] gpionum=%d, io_pupd_lh=0x%x\n",
		__func__, gpionum, io_pupd_lh);

	temp_io = GET_GPIO_IO(io_pupd_lh);
	temp_pupd = GET_GPIO_PUPD(io_pupd_lh);
	temp_lh = GET_GPIO_LH(io_pupd_lh);

	pr_info("[dvs_%s] io=%d, pupd=%d, lh=%d\n",
		__func__, temp_io, temp_pupd, temp_lh);

	/* in case of 'INPUT', set PD/PU */
	if (temp_io == GDVS_IO_IN) {
		/* 0x0:NP, 0x1:PD, 0x2:PU */
		if (temp_pupd == GDVS_PUPD_NP)
			temp_data = GPIO_DVS_CFG_PULL_NONE;
		else if (temp_pupd == GDVS_PUPD_PD)
			temp_data = GPIO_DVS_CFG_PULL_DOWN;
		else if (temp_pupd == GDVS_PUPD_PU)
			temp_data = GPIO_DVS_CFG_PULL_UP;
		else		/* It should be not runned */
			temp_data = GPIO_DVS_CFG_PULL_NONE;

		msm_set_gpio_status(gp, gpionum, temp_data, 0);
	}
	/* in case of 'OUTPUT', set L/H */

	else if (temp_io == GDVS_IO_OUT) {
		pr_info("[dvs_%s] %d gpio set %d\n",
			__func__, gpionum, temp_lh);
		temp_data = GPIO_DVS_CFG_OUTPUT;
		msm_set_gpio_status(gp, gpionum, temp_data, temp_lh);
	}
	else
	{
		pr_info("[dvs_%s] %d gpio set %d NOT VALID\n",
			__func__, gpionum, temp_lh);
	}
}
/****************************************************************/

/****************************************************************/
/* Define this function in accordance with the specification of each BB vendor */
static void undo_sleepgpio(void)
{
	int i;

	pr_info("[dvs_%s] ++\n", __func__);

	for (i = 0; i < sleepdebug_table.gpio_count; i++) {
		int gpio_num;
		gpio_num = sleepdebug_table.gpioinfo[i].gpio_num;
		/*
		 * << Caution >>
		 * If it's necessary,
		 * change the following function to another appropriate one
		 * or delete it
		 */
		setgpio_for_sleepdebug(gpio_num, gpiomap_result.sleep[gpio_num]);
	}

	pr_info("[dvs_%s] --\n", __func__);
	return;
}
/****************************************************************/
#endif

/********************* Fixed Code Area !***************************/
#ifdef SECGPIO_SLEEP_DEBUGGING
static void set_sleepgpio(void)
{
	int i;
	uint16_t set_data;

	pr_info("[dvs_%s] ++, cnt=%d\n",
		__func__, sleepdebug_table.gpio_count);

	for (i = 0; i < sleepdebug_table.gpio_count; i++) {
		int gpio_num;
		pr_info("[dvs_%s][%d] gpio_num(%d), io(%d), pupd(%d), lh(%d)\n",
			__func__,
			i, sleepdebug_table.gpioinfo[i].gpio_num,
			sleepdebug_table.gpioinfo[i].io,
			sleepdebug_table.gpioinfo[i].pupd,
			sleepdebug_table.gpioinfo[i].lh);

		gpio_num = sleepdebug_table.gpioinfo[i].gpio_num;

		// to prevent a human error caused by "don't care" value
		if( sleepdebug_table.gpioinfo[i].io == GDVS_IO_IN)
			sleepdebug_table.gpioinfo[i].lh =
				GET_GPIO_LH(gpiomap_result.sleep[gpio_num]);
		else if( sleepdebug_table.gpioinfo[i].io == GDVS_IO_OUT)
			sleepdebug_table.gpioinfo[i].pupd =
				GET_GPIO_PUPD(gpiomap_result.sleep[gpio_num]);

		set_data = GET_RESULT_GPIO(
			sleepdebug_table.gpioinfo[i].io,
			sleepdebug_table.gpioinfo[i].pupd,
			sleepdebug_table.gpioinfo[i].lh);

		setgpio_for_sleepdebug(gpio_num, set_data);
	}
	pr_info("[dvs_%s] --\n", __func__);
	return;
}
#endif

/****************************************************************/
/* Define appropriate variable in accordance with
	the specification of each BB vendor */
static struct gpio_dvs msm_gpio_dvs = {
	.result = &gpiomap_result,
	.check_gpio_status = msm_check_gpio_status,
	.count = AP_GPIO_COUNT,
	.check_init = false,
	.check_sleep = false,
#ifdef SECGPIO_SLEEP_DEBUGGING
	.sdebugtable = &sleepdebug_table,
	.set_sleepgpio = set_sleepgpio,
	.undo_sleepgpio = undo_sleepgpio,
#endif
};
/****************************************************************/
#endif

#ifdef CONFIG_SEC_PM_DEBUG
static const char * const gpiomux_drv_str[] = {
	"DRV_2mA",
	"DRV_4mA",
	"DRV_6mA",
	"DRV_8mA",
	"DRV_10mA",
	"DRV_12mA",
	"DRV_14mA",
	"DRV_16mA",
};

static const char * const gpiomux_func_str[] = {
	"GPIO",
	"Func_1",
	"Func_2",
	"Func_3",
	"Func_4",
	"Func_5",
	"Func_6",
	"Func_7",
	"Func_8",
	"Func_9",
	"Func_a",
	"Func_b",
	"Func_c",
	"Func_d",
	"Func_e",
	"Func_f",
};

static const char * const gpiomux_pull_str[] = {
	"PULL_NONE",
	"PULL_DOWN",
	"PULL_KEEPER",
	"PULL_UP",
};

static const char * const gpiomux_dir_str[] = {
	"IN",
	"OUT_HIGH",
	"OUT_LOW",
};

static const char * const gpiomux_val_str[] = {
	"VAL_LOW",
	"VAL_HIGH",
};

static void gpiomux_debug_print(struct seq_file *m)
{
	unsigned long flags;
	struct gpiomux_setting set;
	struct gpio_chip *gp = gpio_to_chip(0);
	unsigned val = 0;
	unsigned gpio;
	unsigned begin = 0;
#ifdef CONFIG_PINCTRL_LPI
	struct gpio_chip *lgp = gpiochip_find(LPI_GPIO_NAME, find_gpiochip_by_name);
#endif

	spin_lock_irqsave(&gpiomux_lock, flags);

	for (gpio = begin; gpio < AP_GPIO_COUNT; ++gpio) {
#ifdef ENABLE_SENSORS_FPRINT_SECURE
		if (gpio >= CONFIG_SENSORS_FP_SPI_GPIO_START
				&& gpio <= CONFIG_SENSORS_FP_SPI_GPIO_END)
			continue;
#endif
#ifdef CONFIG_ESE_SECURE
		if (gpio >= CONFIG_ESE_SPI_GPIO_START
				&& gpio <= CONFIG_ESE_SPI_GPIO_END)
			continue;
#endif
#ifdef CONFIG_PINCTRL_LPI
		if ( lgp && gpio >= AP_MSM_GPIO_COUNT ) {
			lpi_gp_get_cfg(lgp, (gpio-AP_MSM_GPIO_COUNT), &set);
			val = lpi_gp_get_value(lgp, (gpio-AP_MSM_GPIO_COUNT), set.dir);
		}
		else 
#endif
		{
			msm_gp_get_cfg(gp, gpio, &set);
			val = msm_gp_get_value(gp, gpio, set.dir);
		}

		if (IS_ERR_OR_NULL(m)) {
			pr_info("GPIO[%u] \t%s \t%s \t%s \t%s \t%s\n",
				gpio,
				gpiomux_func_str[set.func],
				gpiomux_dir_str[set.dir],
				gpiomux_pull_str[set.pull],
				gpiomux_drv_str[set.drv],
				gpiomux_val_str[val]);
		} else {
			seq_printf(m, "GPIO[%u] \t%s \t%s \t%s \t%s \t%s\n",
				gpio,
				gpiomux_func_str[set.func],
				gpiomux_dir_str[set.dir],
				gpiomux_pull_str[set.pull],
				gpiomux_drv_str[set.drv],
				gpiomux_val_str[val]);
		}
	}

	spin_unlock_irqrestore(&gpiomux_lock, flags);
}

void msm_gpio_print_enabled(void)
{
	gpiomux_debug_print(NULL);
}

static int gpiomux_debug_showall(struct seq_file *m, void *unused)
{
	gpiomux_debug_print(m);
	return 0;
}

static int gpiomux_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, gpiomux_debug_showall, inode->i_private);
}

static const struct file_operations gpiomux_operations = {
	.open		= gpiomux_debug_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init msm_gpiomux_debug_init(void)
{
	(void) debugfs_create_file("gpiomux", S_IFREG | S_IRUGO,
				NULL, NULL, &gpiomux_operations);
	return 0;
}
late_initcall(msm_gpiomux_debug_init);
#endif

static int __init msm_gpiomux_init(void)
{
	return 0;
}
late_initcall(msm_gpiomux_init);

#ifdef CONFIG_SEC_GPIO_DVS
static struct platform_device secgpio_dvs_device = {
	.name	= "secgpio_dvs",
	.id	= -1,
	/****************************************************************
	 * Designate appropriate variable pointer
	 * in accordance with the specification of each BB vendor.
	 ***************************************************************/
	.dev.platform_data = &msm_gpio_dvs,
};

static struct platform_device *secgpio_dvs_devices[] __initdata = {
	&secgpio_dvs_device,
};

static int __init secgpio_dvs_device_init(void)
{
	return platform_add_devices(
		secgpio_dvs_devices, ARRAY_SIZE(secgpio_dvs_devices));
}
arch_initcall(secgpio_dvs_device_init);
#endif
