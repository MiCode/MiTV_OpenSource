/*
 *  drivers/switch/switch_gpio.c
 *
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

#define MAX_GPIO 2
struct gpio_switch_data {
	struct switch_dev sdev;
	unsigned gpio[MAX_GPIO];
	unsigned inverse[MAX_GPIO];
	const char *name_on;
	const char *name_off;
	const char *state_on;
	const char *state_off;
	int irq;
	struct work_struct work;
	int index;
};

static int gpio_state = 0;
static void gpio_state_set(int state, int index)
{
// Send Headphone event for Margo
  if(index == 0)
  {
    if (state)
            gpio_state |= (1 << 1);
       else
         gpio_state &= ~(1 << 1);

    //printk("Headphone state change state %d gpio_state %d\n",state, gpio_state);
    }
    else if(index == 1) //send spdif event for margo
    {
      if(state)
        gpio_state |= (1 << 6);
      else
        gpio_state &= (~(1 << 6));
    //printk("send spdif state change state %d gpio_state %d\n",state,gpio_state );
    }
}

static void gpio_switch_work(struct work_struct *work)
{
	int state;
	struct gpio_switch_data *data =
		container_of(work, struct gpio_switch_data, work);

	if (data->inverse[data->index] != 0)
       state = !gpio_get_value(data->gpio[data->index]);
	else
       state = gpio_get_value(data->gpio[data->index]);


	gpio_state_set(state, data->index);
	printk("state %d ,data->index %d gpio_state %d\n",state, data->index, gpio_state);

	switch_set_state(&data->sdev, gpio_state);
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
static int gpio_poll(void* arg)
{
	struct gpio_switch_data *switch_data = (struct gpio_switch_data *)arg;

	int state;
	int i = 0;
	while(1)
	{
		if(kthread_should_stop())  // if kthread_stop() is called
			break;

		//schedule_timeout_interruptible(msecs_to_jiffies(100));       // replaced with msleep
		msleep(100);

		if(switch_data->gpio[i] != 9999)
		{
			if (switch_data->inverse[i] != 0)
				state = !gpio_get_value(switch_data->gpio[i]);
			else
				state = gpio_get_value(switch_data->gpio[i]);
			gpio_state_set(state, i);
		//if(gpio_state[i] != state)
			if (switch_data->sdev.state != gpio_state)
			{
				switch_data->index = i;
				//gpio_state[i] = state;
				schedule_work(&switch_data->work);
				printk("switch_data->sdev.state %d gpio_state[%d] %d inverse %d\n",switch_data->sdev.state, i ,gpio_state,switch_data->inverse[i]);
			}
	    }

		i++;
		if(i >= MAX_GPIO)
		{
			i = 0;
		}
	}

	return 0;
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
	int ret = 0, i = 0;

	struct device_node *dn;
	const char *dn_switch_name;
	u32 dn_switch_gpio;
	u32 dn_switch_inverse;
    u32 dn_switch_gpio2 = 9999;
	u32 dn_switch_inverse2;

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
		of_property_read_u32(dn, "switch-gpio2", &dn_switch_gpio2);
		of_property_read_u32(dn, "switch-inverse2", &dn_switch_inverse2);
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
	switch_data->gpio[0] = dn_switch_gpio;
	switch_data->inverse[0] = dn_switch_inverse;
	switch_data->sdev.print_state = switch_gpio_print_state;
	switch_data->gpio[1] = dn_switch_gpio2;
	switch_data->inverse[1] = dn_switch_inverse2;


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
    for(i = 0; i < MAX_GPIO; i ++)
    	printk("[switch-gpio] name: %s, gpio: %d, inverse: %d\n", dn_switch_name, switch_data->gpio[i], switch_data->inverse[i]);
	if (dn_switch_gpio == UNKNOWN_GPIO_PAD)
	{
		printk("[switch-gpio] Unknown GPIO Number, Please check gpio pad\n");
		return -EBUSY;
	}

	ret = switch_dev_register(&switch_data->sdev);
	if (ret < 0)
		goto err_switch_dev_register;

	ret = gpio_request(switch_data->gpio[0], pdev->name);
	if (ret < 0)
		goto err_request_gpio;

	ret = gpio_direction_input(switch_data->gpio[0]);
	if (ret < 0)
		goto err_set_gpio_input;

    if(switch_data->gpio[1] != 9999)
    {
    	ret = gpio_request(switch_data->gpio[1], "spdif");
    	if (ret >= 0)
    		gpio_direction_input(switch_data->gpio[1]);
    }

	INIT_WORK(&switch_data->work, gpio_switch_work);

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

err_request_irq:
err_detect_irq_num_failed:
err_set_gpio_input:
	gpio_free(switch_data->gpio[0]);
	if(switch_data->gpio[1] != 9999)
		gpio_free(switch_data->gpio[1]);
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
	gpio_free(switch_data->gpio[0]);
	if(switch_data->gpio[1] != 9999)
		gpio_free(switch_data->gpio[1]);
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
