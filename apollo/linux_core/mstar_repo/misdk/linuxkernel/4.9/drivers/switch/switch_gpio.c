/*
 *  drivers/switch/switch_gpio.c
 *
 * Copyright (C) 2018 XiaoMi, Inc.
 * Copyright (C) 2008 Google, Inc.
 * Author: Mike Lockwood <lockwood@android.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/switch.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>

#define CONFIG_MSTAR_SWITCH_GPIO
#define GPIO_POLL
#define UNKNOWN_GPIO_PAD 999

#ifdef CONFIG_MSTAR_SWITCH_GPIO
#include <linux/delay.h>

#ifdef GPIO_POLL
#include <linux/kthread.h>
static struct task_struct *gpio_poll_tsk = NULL;
#endif

#endif

#define XIAOMI_AMP_ENABLE
#ifdef XIAOMI_AMP_ENABLE
#define XIAOMI_USE_MSTAR_API
#ifdef XIAOMI_USE_MSTAR_API
//#include "../../../mstar2/drv/gpio/mdrv_gpio.h"
extern void MDrv_GPIO_Set_High(unsigned char);
extern void MDrv_GPIO_Set_Low(unsigned char);
#endif
#endif

struct gpio_switch_data {
	struct switch_dev sdev;
	unsigned gpio;
	unsigned inverse;
	const char *name_on;
	const char *name_off;
	const char *state_on;
	const char *state_off;
	int irq;
	struct work_struct work;
#ifdef XIAOMI_AMP_ENABLE
	unsigned amp_en_gpio;
#if 0
	struct delayed_work amp_en_work;
#endif
#endif
};

#ifdef XIAOMI_AMP_ENABLE

#define XIAOMI_SYS_PERM (S_IRUGO|S_IWUSR|S_IWGRP)

/* TODO: need get private drv data from struct class? */
static unsigned gAmpEnGpio;

#if 0
static void amp_enable_work(struct work_struct *work)
{
	int state, state1, ret;
	struct delayed_work *dealyed_wk =
		container_of(work, struct delayed_work, work);
	struct gpio_switch_data *data =
		container_of(dealyed_wk, struct gpio_switch_data, amp_en_work);

	state  = gpio_get_value(data->gpio);
	state1 = gpio_get_value(data->amp_en_gpio);

	switch (state) {
	case 1:
		if (state1 != 1) {
		#ifdef XIAOMI_USE_MSTAR_API
			MDrv_GPIO_Set_High(data->amp_en_gpio);
		#else
			ret = gpio_direction_output(data->amp_en_gpio, 1);
		#endif
		}
		break;
	case 0:
	default:
		if (state1 != 0) {
		#ifdef XIAOMI_USE_MSTAR_API
			MDrv_GPIO_Set_Low(data->amp_en_gpio);
		#else
			ret = gpio_direction_output(data->amp_en_gpio, 0);
		#endif
		}
		break;
	}
}
#endif

static ssize_t amp_en_show(struct class *cla, struct class_attribute *attr, char *buf)
{
	int state;

	state = gpio_get_value(gAmpEnGpio);

	return sprintf(buf, "Headphone AMP(gpio:%d): %s\n", gAmpEnGpio, (state==0)?"disable":"enable");
}

static ssize_t amp_en_store(struct class *class, struct class_attribute *attr, const char *buf, size_t count)
{
	if (!strncmp(buf, "0", 1) || !strncmp(buf, "disable", 7)) {
	#ifdef XIAOMI_USE_MSTAR_API
		MDrv_GPIO_Set_Low(gAmpEnGpio);
	#else
		gpio_direction_output(gAmpEnGpio, 0);
	#endif
	} else if (!strncmp(buf, "1", 1) || !strncmp(buf, "enable", 6)) {
	#ifdef XIAOMI_USE_MSTAR_API
		MDrv_GPIO_Set_High(gAmpEnGpio);
	#else
		gpio_direction_output(gAmpEnGpio, 1);
	#endif
	} else {
		printk(KERN_ERR "xiaomi, unknown command!%d:%s\n",gAmpEnGpio, buf);
	}

	return count;
}

int amp_en_get_enable(void)
{
	return gpio_get_value(gAmpEnGpio);
}

EXPORT_SYMBOL(amp_en_get_enable);

void amp_en_set_enable(int enable)
{
	if (!enable) {
	#ifdef XIAOMI_USE_MSTAR_API
		MDrv_GPIO_Set_Low(gAmpEnGpio);
	#else
		gpio_direction_output(gAmpEnGpio, 0);
	#endif
	} else if (enable) {
	#ifdef XIAOMI_USE_MSTAR_API
		MDrv_GPIO_Set_High(gAmpEnGpio);
	#else
		gpio_direction_output(gAmpEnGpio, 1);
	#endif
	}
}

EXPORT_SYMBOL(amp_en_set_enable);

static struct class_attribute amp_en_attrs[] = {
	__ATTR(headphone_amp_en, XIAOMI_SYS_PERM, amp_en_show, amp_en_store),
	__ATTR_NULL
};

static struct class amp_en_class = {
	.name = "xiaomi-headphone",
	.class_attrs = amp_en_attrs,
};

static int amp_en_create_class(void)
{
	int ret = 0;

	ret = class_register(&amp_en_class);
	if (ret < 0)
		printk(KERN_ERR "xiaomi, creat class amp en fail!\n");

	return 0;
}
#endif

static void gpio_switch_work(struct work_struct *work)
{
	int state;
	struct gpio_switch_data *data =
		container_of(work, struct gpio_switch_data, work);

	if (data->inverse != 0)
		state = !gpio_get_value(data->gpio);
	else
		state = gpio_get_value(data->gpio);

	// Send Headphone event for Margo
	if (state)
		state=2;

	switch_set_state(&data->sdev, state);
#ifdef XIAOMI_AMP_ENABLE
#if 0
	schedule_delayed_work(&data->amp_en_work, 500);
#endif
#endif
}

static irqreturn_t gpio_irq_handler(int irq, void *dev_id)
{
	struct gpio_switch_data *switch_data =
	    (struct gpio_switch_data *)dev_id;

	schedule_work(&switch_data->work);
	return IRQ_HANDLED;
}

#ifdef CONFIG_MSTAR_SWITCH_GPIO
#ifdef GPIO_POLL
static void gpio_poll(void* arg)
{
	struct gpio_switch_data *switch_data = (struct gpio_switch_data *)arg;

	int state;

	while(1)
	{
		if(kthread_should_stop())  // if kthread_stop() is called
			break;

		//schedule_timeout_interruptible(msecs_to_jiffies(100));       // replaced with msleep
		msleep(100);

		if (switch_data->inverse != 0)
			state = !gpio_get_value(switch_data->gpio);
		else
			state = gpio_get_value(switch_data->gpio);

		if (switch_data->sdev.state != state)
		{
			schedule_work(&switch_data->work);
		}
	}
}
#endif
#endif

static ssize_t switch_gpio_print_state(struct switch_dev *sdev, char *buf)
{
	struct gpio_switch_data	*switch_data =
		container_of(sdev, struct gpio_switch_data, sdev);
	const char *state;
	if (switch_get_state(sdev))
		state = switch_data->state_on;
	else
		state = switch_data->state_off;

	if (state)
		return sprintf(buf, "%s\n", state);
	return -1;
}

#if defined(CONFIG_OF)
static struct of_device_id mstar_gpio_switch_of_device_ids[] = {
                {.compatible = "mstar,switch-gpio"},
                {},
};
#endif

static int gpio_switch_probe(struct platform_device *pdev)
{
	struct gpio_switch_platform_data *pdata = pdev->dev.platform_data;
	struct gpio_switch_data *switch_data;
	int ret = 0;

	struct device_node *dn;
	const char *dn_switch_name;
	u32 dn_switch_gpio;
	u32 dn_switch_inverse;
#ifdef XIAOMI_AMP_ENABLE
	u32 dn_amp_en_gpio;
#endif

#if defined (CONFIG_OF)
	dn = pdev->dev.of_node;
	if (dn)
	{
		if (0 != of_property_read_string(dn, "switch-name", &dn_switch_name) ||
		    0 != of_property_read_u32(dn, "switch-gpio", &dn_switch_gpio) ||
		    0 != of_property_read_u32(dn, "switch-inverse", &dn_switch_inverse))
		{
			printk(KERN_ERR "[%s][%d] Parse dts error\n", __FUNCTION__, __LINE__);
			return -ENXIO;
		}
#ifdef XIAOMI_AMP_ENABLE
		if (0 != of_property_read_u32(dn, "amp-en-gpio", &dn_amp_en_gpio))
		{
			printk(KERN_ERR "[%s][%d] Parse dts error, xiaomi, can not find amp-en-gpio\n", __FUNCTION__, __LINE__);
		}
#endif
	}
	else
	{
		printk(KERN_ERR "[%s][%d] device node is null\n", __FUNCTION__, __LINE__);
		return -ENXIO;
	}
#else
	if (!pdata)
		return -EBUSY;
#endif

	switch_data = kzalloc(sizeof(struct gpio_switch_data), GFP_KERNEL);
	if (!switch_data)
		return -ENOMEM;

#if defined (CONFIG_OF)
	switch_data->sdev.name = dn_switch_name;
	switch_data->gpio = dn_switch_gpio;
	switch_data->inverse = dn_switch_inverse;
	switch_data->sdev.print_state = switch_gpio_print_state;
#ifdef XIAOMI_AMP_ENABLE
	switch_data->amp_en_gpio = dn_amp_en_gpio;
	gAmpEnGpio = switch_data->amp_en_gpio;
	amp_en_create_class();
#endif
#else
	switch_data->sdev.name = pdata->name;
	switch_data->gpio = pdata->gpio;
	switch_data->inverse = pdata->inverse;
	switch_data->name_on = pdata->name_on;
	switch_data->name_off = pdata->name_off;
	switch_data->state_on = pdata->state_on;
	switch_data->state_off = pdata->state_off;
	switch_data->sdev.print_state = switch_gpio_print_state;
#endif

	printk("[switch-gpio] name: %s, gpio: %d, inverse: %d\n", dn_switch_name, dn_switch_gpio, dn_switch_inverse);
	if (dn_switch_gpio == UNKNOWN_GPIO_PAD)
	{
		printk("[switch-gpio] Unknown GPIO Number, Please check gpio pad\n");
		return -EBUSY;
	}

	ret = switch_dev_register(&switch_data->sdev);
	if (ret < 0)
		goto err_switch_dev_register;

	ret = gpio_request(switch_data->gpio, pdev->name);
	if (ret < 0)
		goto err_request_gpio;

	ret = gpio_direction_input(switch_data->gpio);
	if (ret < 0)
		goto err_set_gpio_input;

	INIT_WORK(&switch_data->work, gpio_switch_work);

#ifdef XIAOMI_AMP_ENABLE
	ret = gpio_request(switch_data->amp_en_gpio, "hp_en");
	if (ret < 0) {
		printk(KERN_ERR "xiaomi, request hp enable pin err!\n");
	}

#ifdef XIAOMI_USE_MSTAR_API
	MDrv_GPIO_Set_Low(switch_data->amp_en_gpio);
#else
	ret = gpio_direction_output(switch_data->amp_en_gpio, 0);
	if (ret < 0) {
		printk(KERN_ERR "xiaomi, write hp en pin err!\n");
	}
#endif

#if 0
	INIT_DELAYED_WORK(&switch_data->amp_en_work, amp_enable_work);
#endif
#endif

#ifdef CONFIG_MSTAR_SWITCH_GPIO
#ifdef GPIO_POLL
	if(gpio_poll_tsk == NULL)
	{
		gpio_poll_tsk = kthread_create(gpio_poll, switch_data, "GPIO poll Task");
		if (IS_ERR(gpio_poll_tsk)) {
			printk("create kthread for GPIO poll Task fail\n");
			goto err_set_gpio_input;
		}else
			wake_up_process(gpio_poll_tsk);
	}
	else
	{
		printk("\033[35mFunction = %s, Line = %d, gpio_poll_tsk is already created\033[m\n", __PRETTY_FUNCTION__, __LINE__);
		goto err_set_gpio_input;
	}
#endif
#else
	switch_data->irq = gpio_to_irq(switch_data->gpio);
	if (switch_data->irq < 0) {
		ret = switch_data->irq;
		goto err_detect_irq_num_failed;
	}

	ret = request_irq(switch_data->irq, gpio_irq_handler,
			  IRQF_TRIGGER_LOW, pdev->name, switch_data);
	if (ret < 0)
		goto err_request_irq;
#endif

	/* Perform initial detection */
	gpio_switch_work(&switch_data->work);

	return 0;

#if !defined(CONFIG_MSTAR_SWITCH_GPIO)
err_request_irq:
err_detect_irq_num_failed:
#endif
err_set_gpio_input:
	gpio_free(switch_data->gpio);
err_request_gpio:
	switch_dev_unregister(&switch_data->sdev);
err_switch_dev_register:
	kfree(switch_data);

	return ret;
}

static int gpio_switch_remove(struct platform_device *pdev)
{
	struct gpio_switch_data *switch_data = platform_get_drvdata(pdev);

#ifdef CONFIG_MSTAR_SWITCH_GPIO
#ifdef GPIO_POLL
	kthread_stop(gpio_poll_tsk);
	gpio_poll_tsk = NULL;
#endif
#endif

	cancel_work_sync(&switch_data->work);
#ifdef XIAOMI_AMP_ENABLE
#if 0
	if (delayed_work_pending(&switch_data->amp_en_work))
		cancel_delayed_work_sync(&switch_data->amp_en_work);
#endif
#endif
	gpio_free(switch_data->gpio);
	switch_dev_unregister(&switch_data->sdev);
	kfree(switch_data);

	return 0;
}

static struct platform_driver gpio_switch_driver = {
	.probe		= gpio_switch_probe,
	.remove		= gpio_switch_remove,
	.driver		= {
		.of_match_table = of_match_ptr(mstar_gpio_switch_of_device_ids),
		.name	= "switch-gpio",
		.owner	= THIS_MODULE,
	},
};

static int __init gpio_switch_init(void)
{
	return platform_driver_register(&gpio_switch_driver);
}

static void __exit gpio_switch_exit(void)
{
	platform_driver_unregister(&gpio_switch_driver);
}

module_init(gpio_switch_init);
module_exit(gpio_switch_exit);

MODULE_AUTHOR("Mike Lockwood <lockwood@android.com>");
MODULE_DESCRIPTION("GPIO Switch driver");
MODULE_LICENSE("GPL");
