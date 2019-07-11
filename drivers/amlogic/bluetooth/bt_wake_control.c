/*
 * drivers/amlogic/bluetooth/bt_wake_control.c
 *
 * Copyright (C) 2017 Amlogic, Inc. All rights reserved.
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

#include <linux/module.h>	/* kernel module definitions */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/notifier.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <asm/irq.h>
#include <linux/ioport.h>
#include <linux/param.h>
#include <linux/bitops.h>
#include <linux/termios.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/serial_core.h>

#include <linux/amlogic/gpio-amlogic.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/amlogic/wakelock_android.h>
#include <linux/amlogic/cpu_version.h>
#include <linux/amlogic/iomap.h>
#include "../../gpio/gpiolib.h"

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h> /* event notifications */
#include "../../bluetooth/hci_uart.h"
#include "bt_wake_control.h"



#define VERSION		"1.0"

#define POLARITY_LOW 0
#define POLARITY_HIGH 1

struct bt_wake_control_info {
	unsigned int host_wake;
	unsigned int ext_wake;
	unsigned int bt_uart_enable;/*add by tankangxi*/
	unsigned int host_wake_irq;
	struct uart_port *uport;
	struct wake_lock wake_bt_lock;
	struct wake_lock wake_host_lock;
	int irq_polarity;
	int irq_trigger_type;
	int has_ext_wake;
};

/* 5 second timeout */
#define WAKE_TIMER_INTERVAL  1

static struct tasklet_struct hostwake_task;
static struct bt_wake_control_info *bwc;
static int bt_port_id = -1;
static int bt_ext_wake_polarity;

/* work function */
static void bt_wake_control_work(struct work_struct *work);

/* work queue */
DECLARE_DELAYED_WORK(bt_wake_control_workqueue, bt_wake_control_work);

/** Transmission timer */
static void wake_host_control_tx_timer_expire(unsigned long data);
static DEFINE_TIMER(host_tx_timer,
			 wake_host_control_tx_timer_expire, 0, 0);

/** receive timer */
static void wake_host_control_rx_timer_expire(unsigned long data);
static DEFINE_TIMER(host_rx_timer,
			wake_host_control_rx_timer_expire, 0, 0);

void btwake_control_wake_peer(struct uart_port *port)
{
	if (port->line != bt_port_id)
		return;
	/*pr_info("wake peer\n");*/
	mod_timer(&host_tx_timer, jiffies + (WAKE_TIMER_INTERVAL * HZ));
	if (bwc->has_ext_wake == 1)
		gpio_set_value(bwc->ext_wake, bt_ext_wake_polarity);
	wake_lock(&bwc->wake_bt_lock);
}
EXPORT_SYMBOL(btwake_control_wake_peer);

static void bt_wake_control_work(struct work_struct *work)
{
	pr_info("bt_wake_control_work\n");
	wake_lock(&bwc->wake_host_lock);
	mod_timer(&host_rx_timer, jiffies + (WAKE_TIMER_INTERVAL * HZ));
}

/**
 * @return 1 if the Host can go to sleep, 0 otherwise.
 */
static int bt_wake_control_can_sleep(void)
{
	if (gpio_get_value(bwc->host_wake) != bwc->irq_polarity) {
		/*pr_info("host wake pin is inactive,you can sleep\n");*/
		return 1;
	}

	return 0;
}

/**
 * Handles transmission timer expiration.
 * @param data Not used.
 */
static void wake_host_control_tx_timer_expire(unsigned long data)
{
	if (bwc->has_ext_wake == 1)
		gpio_set_value(bwc->ext_wake, !bt_ext_wake_polarity);
	wake_lock_timeout(&bwc->wake_bt_lock, HZ / 2);
}

/**
 * Handles transmission timer expiration.
 * @param data Not used.
 */
static void wake_host_control_rx_timer_expire(unsigned long data)
{
	/*pr_info("rx timeout host_wake_value:%d\n",*/
		/*gpio_get_value(bwc->host_wake));*/
	if (gpio_get_value(bwc->host_wake) == bwc->irq_polarity)
		mod_timer(&host_rx_timer, jiffies + (WAKE_TIMER_INTERVAL * HZ));
	else
		wake_lock_timeout(&bwc->wake_host_lock, HZ / 2);
}

/**
 * Schedules a tasklet to run when receiving an interrupt on the
 * <code>HOST_WAKE</code> GPIO pin.
 * @param irq Not used.
 * @param dev_id Not used.
 * zzl
 * static irqreturn_t bt_wake_control_hostwake_isr(int irq, void *dev_id)
 * {
 * tasklet_schedule(&hostwake_task);
 * return IRQ_HANDLED;
 * }
 */
static int bt_wake_control_populate_dt_pinfo(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	int tmp, ret;
	const void *prop;
	const char *value;

	tmp = of_get_named_gpio(np, "bt_host_wake", 0);
	if (tmp < 0) {
		/*pr_info("couldn't find host_wake gpio\n");*/
		return -ENODEV;
	}
	bwc->host_wake = tmp;

	tmp = of_get_named_gpio(np, "bt_uart_enable", 0);
	if (tmp < 0) {
		/*pr_info("couldn't find bt_uart_enable gpio\n");*/
		return -ENODEV;
	}
	bwc->bt_uart_enable = tmp;
	pr_info("find bt_uart_enable gpio\n");
	tmp = of_get_named_gpio(np, "bt_ext_wake", 0);
	if (tmp < 0)
		bwc->has_ext_wake = 0;
	else
		bwc->has_ext_wake = 1;

	if (bwc->has_ext_wake)
		bwc->ext_wake = tmp;

	prop = of_get_property(np, "bt_port_id", NULL);
	if (prop) {
		bt_port_id = of_read_ulong(prop, 1);
		/*pr_info("bt port id is %d\n", bt_port_id);*/
	}

	/*pr_info("bt_host_wake %d, bt_ext_wake %d\n",*/
			/*bwc->host_wake,*/
			/*bwc->ext_wake);*/

	prop = of_get_property(np, "bt_ext_wake_polarity", NULL);
	if (prop)
		bt_ext_wake_polarity = of_read_ulong(prop, 1);

	ret = of_property_read_string(pdev->dev.of_node,
			"irq_trigger_type", &value);
	if (ret || NULL == value) {
		bwc->irq_trigger_type = GPIO_IRQ_FALLING;/*as default*/
		bwc->irq_polarity = POLARITY_LOW;
	} else if (strcmp(value, "GPIO_IRQ_HIGH") == 0) {
		bwc->irq_trigger_type = GPIO_IRQ_HIGH;
		bwc->irq_polarity = POLARITY_HIGH;
	} else if (strcmp(value, "GPIO_IRQ_LOW") == 0) {
		bwc->irq_trigger_type = GPIO_IRQ_LOW;
		bwc->irq_polarity = POLARITY_LOW;
	} else if (strcmp(value, "GPIO_IRQ_RISING") == 0) {
		bwc->irq_trigger_type = GPIO_IRQ_RISING;
		bwc->irq_polarity = POLARITY_HIGH;
	} else if (strcmp(value, "GPIO_IRQ_FALLING") == 0) {
		bwc->irq_trigger_type = GPIO_IRQ_FALLING;
		bwc->irq_polarity = POLARITY_LOW;
	} else {
		bwc->irq_trigger_type = GPIO_IRQ_FALLING;/*as default*/
		bwc->irq_polarity = POLARITY_LOW;
	}

	return 0;
}

static int bt_wake_control_populate_pinfo(struct platform_device *pdev)
{
	struct resource *res;

	res = platform_get_resource_byname(pdev, IORESOURCE_IO,
				"gpio_host_wake");
	if (!res) {
		/*pr_info("couldn't find host_wake gpio\n");*/
		return -ENODEV;
	}
	bwc->host_wake = res->start;

	res = platform_get_resource_byname(pdev, IORESOURCE_IO,
				"gpio_ext_wake");
	if (!res)
		bwc->has_ext_wake = 0;
	else
		bwc->has_ext_wake = 1;

	if (bwc->has_ext_wake)
		bwc->ext_wake = res->start;

	return 0;
}

static int bt_wake_control_probe(struct platform_device *pdev)
{
	/* struct resource *res; */
	/*zzl int ret, retval; */
	int ret;

	bwc = kzalloc(sizeof(struct bt_wake_control_info), GFP_KERNEL);
	if (!bwc)
		return -ENOMEM;

	if (pdev->dev.of_node) {
		ret = bt_wake_control_populate_dt_pinfo(pdev);
		if (ret < 0) {
			/*pr_info("couldn't populate info from dt\n");*/
			goto free_bwc;
		}
	} else {
		ret = bt_wake_control_populate_pinfo(pdev);
		if (ret < 0) {
			/*pr_info("couldn't populate info\n");*/
			goto free_bwc;
		}
	}

	/* configure host_wake as input */
	ret = gpio_request_one(bwc->host_wake, GPIOF_IN, "bt_host_wake");
	if (ret < 0) {
		/*pr_info("failed to configure input direction\n");*/
		goto free_bwc;
	}

	pr_info("BT WAKE: set to wake\n");
	if (bwc->has_ext_wake) {
		/* configure ext_wake as output mode*/
		if (bt_ext_wake_polarity)
			ret = gpio_request_one(bwc->ext_wake,
					GPIOF_OUT_INIT_HIGH, "bt_ext_wake");
		else
			ret = gpio_request_one(bwc->ext_wake,
					GPIOF_OUT_INIT_LOW, "bt_ext_wake");
		if (ret < 0) {
			/*pr_info("failed to config output\n");*/
			/*pr_info("for GPIO %d err %d\n",bwc->ext_wake, ret);*/
			goto free_bt_host_wake;
		}
	}

	if (bwc->bt_uart_enable) {
		/* configure bt_uart_enable as output mode*/
		pr_info("bt_uart_enable: set to enable uart\n");
		ret = gpio_request_one(bwc->bt_uart_enable,
				GPIOF_OUT_INIT_HIGH, "bt_uart_enable");

		if (ret < 0) {
			pr_info("failed to config uart_enable output\n");
			/*pr_info("	for GPIO %d err %d\n",*/
						/*bwc->uart_enable, ret);*/
			goto free_bt_host_wake;
		}
	}

/*
 * res =
 * platform_get_resource_byname(pdev,
 * IORESOURCE_IRQ, "host_wake");
 * if (!res) {
 * pr_info("couldn't find host_wake irq\n");
 * ret = -ENODEV;
 * goto free_bt_host_wake;
 * }
 * bwc->host_wake_irq = res->start;
 */

	wake_lock_init(&bwc->wake_bt_lock,
		WAKE_LOCK_SUSPEND, "hostwakebtcontrol");

	wake_lock_init(&bwc->wake_host_lock,
		WAKE_LOCK_SUSPEND, "btwakehostcontrol");

	bwc->host_wake_irq =
		irq_of_parse_and_map(pdev->dev.of_node, 0);
	if (bwc->host_wake_irq < 0) {
		pr_info("couldn't find host_wake irq\n");
		ret = -ENODEV;
		goto free_bt_ext_wake;
	}
/* zzl
 * gpio_for_irq(bwc->host_wake,
 * AML_GPIO_IRQ(bwc->host_wake_irq, FILTER_NUM7,
 * bwc->irq_trigger_type));
 * retval = request_irq(bwc->host_wake_irq, bt_wake_control_hostwake_isr,
 * IRQF_DISABLED,
 * "bluetooth hostwake", NULL);
 * if (retval  < 0) {
 * pr_info("Couldn't acquire BT_HOST_WAKE IRQ\n");
 * goto free_bt_ext_wake;
 * }
 */

	pr_info("bt wakeup control ok!\n");
	return 0;

free_bt_ext_wake:
	gpio_free(bwc->ext_wake);
free_bt_host_wake:
	gpio_free(bwc->host_wake);
free_bwc:
	kfree(bwc);
	return ret;
}

static int bt_wake_control_remove(struct platform_device *pdev)
{
	/* free_irq(bwc->host_wake_irq, NULL); */
	gpio_free(bwc->host_wake);
	gpio_free(bwc->ext_wake);
	wake_lock_destroy(&bwc->wake_bt_lock);
	wake_lock_destroy(&bwc->wake_host_lock);
	kfree(bwc);
	bwc = NULL;
	return 0;
}

static void bt_wake_control_hostwake_task(unsigned long data)
{
	schedule_delayed_work(&bt_wake_control_workqueue, 0);
}

static int bt_wake_control_resume(struct platform_device *pdev)
{
	if (bwc->bt_uart_enable) {
		gpio_set_value(bwc->bt_uart_enable, 1);
		mdelay(30);
	}
	return 0;
}

static int bt_wake_control_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	if (bt_wake_control_can_sleep()) {
		if (bwc->bt_uart_enable)
		gpio_set_value(bwc->bt_uart_enable, 0);
		return 0;
	} else
		return -1;
}

static void bt_wake_control_shutdown(struct platform_device *pdev)
{
	/*pr_info("bt_wake_control_shutdown\n");*/
	if (bwc->bt_uart_enable)
		gpio_set_value(bwc->bt_uart_enable, 0);
}

static const struct of_device_id bt_wake_control_match_table[] = {
	{ .compatible = "amlogic, btwakecontrol" },
	{}
};

static struct platform_driver bt_wake_control_driver = {
	.probe = bt_wake_control_probe,
	.remove = bt_wake_control_remove,
	.suspend = bt_wake_control_suspend,
	.resume = bt_wake_control_resume,
	.shutdown = bt_wake_control_shutdown,
	.driver = {
		.name = "btwakecontrol",
		.owner = THIS_MODULE,
		.of_match_table = bt_wake_control_match_table,
	},
};


/**
 * Initializes the module.
 * @return On success, 0. On error, -1, and <code>errno</code> is set
 * appropriately.
 */
static int __init bt_wake_control_init(void)
{
	int retval;

	pr_info("bt_wake_control Mode Driver Ver %s\n", VERSION);

	/* Initialize timer */
	init_timer(&host_tx_timer);
	host_tx_timer.function = wake_host_control_tx_timer_expire;
	host_tx_timer.data = 0;

	init_timer(&host_rx_timer);
	host_rx_timer.function = wake_host_control_rx_timer_expire;
	host_rx_timer.data = 0;

	/* initialize host wake tasklet */
	tasklet_init(&hostwake_task, bt_wake_control_hostwake_task, 0);

	retval = platform_driver_register(&bt_wake_control_driver);
	if (retval)
		goto fail;

	return 0;

fail:
	return retval;
}

/**
 * Cleans up the module.
 */
static void __exit bt_wake_control_exit(void)
{
	del_timer(&host_tx_timer);
	del_timer(&host_rx_timer);
	platform_driver_unregister(&bt_wake_control_driver);
}

module_init(bt_wake_control_init);
module_exit(bt_wake_control_exit);

MODULE_DESCRIPTION("Bluetooth Wake Control Driver ver %s " VERSION);
MODULE_LICENSE("GPL");
