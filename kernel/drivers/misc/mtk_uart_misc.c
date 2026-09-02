/*
 * mediatek uart_misc char device, giving access to switch uart source.
 *
 * Copyright (c) Mediatek
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/input/mt.h>
#include <linux/major.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/delay.h>

#define CHIPTOP_RIU                    (RIU_VIRT_BASE + 0x203C00)
#define UART_SEL                       (0x53)
#define UART1_DISABLE                  (0x00F0)
#define UART1_ENABLE                   (0xFF5F)
#define UART_REG(iobase, addr) *((volatile unsigned short*)(iobase + ((addr)<< 2)))

#define PM_SLP_RIU                     (RIU_VIRT_BASE + 0x1C00)
#define PM_SLP_UART                    (0x09)
#define PM_SLP_UART                    (0x09)
#define PM_SLP_UART_TX_ENABLE          (0x0400)
#define PM_SLP_UART_RX_ENABLE          (0x0800)
#define PM_SLP_HK51_UART_ENABLE        (0x1000)

#define PM_MISC_RIU                    (RIU_VIRT_BASE + 0x5C00)
#define PM_MISC_DBG_OW                 (0x1E)
#define PM_MISC_DBG_MASK               (0x00)
#define PM_MISC_DBG_OW_EN              (0x1)
#define PM_MISC_DBG_OW_VAL             (0x10)

#define uart_misc_DEBUG
#ifdef uart_misc_DEBUG
#define uart_misc_debug(fmt, args...) printk(KERN_EMERG"[%s] " fmt, __FUNCTION__, ##args)
#else
#define uart_misc_debug(fmt, args...) do {} while(0)
#endif

struct kobject *kobj_ref_misc;
static unsigned long uart_switch_number = 0;
static unsigned long hdmi2uart_enable = 0;
static unsigned long uart_tx_enable_number = 1;
static unsigned long uart_rx_enable_number = 1;
spinlock_t uart_misc_lock;

#if defined(CONFIG_ARM64)
extern ptrdiff_t mstar_pm_base;
#define RIU_VIRT_BASE  mstar_pm_base
#else
#define RIU_VIRT_BASE  0xFD000000
#endif
#define UART0 0x0
#define UART1 0x1
#define FUART 0x2
#define UART2 0x3
#define UART3 0x4

#define PIU_UART0  0x4
#define PIU_UART1  0x5
#define FAST_UART  0x7
#define PIU_UART2  0x6
#define PIU_UART3  0x3

#define UART_SEL0_MASK 0xFFF0
#define REG_UART_SEL0  0x10EA6
#define PIU_UART_SEL1  0x10EA6

unsigned int dev_ttyS0_tx_enable = 1;
static ssize_t uart0_dis_sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	uart_misc_debug("Sysfs uart0_dis - Write to %s (tx:0 disable, 1 enable)!!!\n", buf);
    //mdelay(1);
	sscanf(buf, "%x", &dev_ttyS0_tx_enable);
	uart_misc_debug("Sysfs dev_ttyS0_tx_enable 0x%x \n", dev_ttyS0_tx_enable);
	uart_misc_debug("Sysfs uart0_dis - Done!!!\n");

	return count;
}
static ssize_t uart0_dis_sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
#define SW_SZ 4
	ssize_t ret = 0;
	uart_misc_debug("Sysfs uart0_dis - Read!!!\n");
	uart_misc_debug("show dev_ttyS0_tx_enable %x \n", dev_ttyS0_tx_enable);
	snprintf(buf, SW_SZ, "%x\n", dev_ttyS0_tx_enable);
	ret = strlen(buf) + 1;
	return ret;
}

static ssize_t uart_switch_sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	uart_misc_debug("Sysfs uartswitch - Write!!!\n");
	sscanf(buf, "%lx", &uart_switch_number);
	uart_misc_debug("store uart_switch_number 0x%02lx!!! \n", uart_switch_number);
	if(uart_switch_number != 0)
	{
		//dev_ttyS0_tx_enable = 0;
	}
	else
	{
		//dev_ttyS0_tx_enable = 1;
	}
	//uart_misc_debug("Sysfs dev_ttyS0_tx_enable 0x%x\n", dev_ttyS0_tx_enable);
	uart_misc_debug("Sysfs Do uartswitch.\n");

	spin_lock(&uart_misc_lock);
	switch (uart_switch_number) {
	case UART0:
		UART_REG(CHIPTOP_RIU, UART_SEL) &= UART_SEL0_MASK;
		UART_REG(CHIPTOP_RIU, UART_SEL) |= PIU_UART0;
		break;
	case UART1:
		UART_REG(CHIPTOP_RIU, UART_SEL) &= UART_SEL0_MASK;
		UART_REG(CHIPTOP_RIU, UART_SEL) |= PIU_UART1;
		break;
	case UART2:
		UART_REG(CHIPTOP_RIU, UART_SEL) &= UART_SEL0_MASK;
		UART_REG(CHIPTOP_RIU, UART_SEL) |= PIU_UART2;
		break;
	case FUART:
		UART_REG(CHIPTOP_RIU, UART_SEL) &= UART_SEL0_MASK;
		UART_REG(CHIPTOP_RIU, UART_SEL) |= FAST_UART;
		break;
	default:
		UART_REG(CHIPTOP_RIU, UART_SEL) &= UART_SEL0_MASK;
		UART_REG(CHIPTOP_RIU, UART_SEL) |= PIU_UART0;
		break;
	}
	spin_unlock(&uart_misc_lock);
	return count;
}


static ssize_t uart_switch_sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
#define SW_SZ 4
	ssize_t ret = 0;
	uart_misc_debug("Sysfs uartswitch - Read!!!\n");
	unsigned int current_val = UART_REG(PM_SLP_RIU, PM_SLP_UART);
	uart_misc_debug("current_val:%X", current_val);
	uart_misc_debug("show uart_switch_number %lX \n", uart_switch_number);
	snprintf(buf, SW_SZ, "%lx\n", uart_switch_number);
	ret = strlen(buf) + 1;
	return ret;
}

static ssize_t hdmi_to_uart_sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	uart_misc_debug("Sysfs hdmi2uart - Write!!!\n");
	sscanf(buf, "%lx", &hdmi2uart_enable);
	uart_misc_debug("store hdmi2uart 0x%02lx \n", hdmi2uart_enable);
	spin_lock(&uart_misc_lock);
	if (hdmi2uart_enable) {
		UART_REG(PM_MISC_RIU, PM_MISC_DBG_OW) &= PM_MISC_DBG_MASK;
		UART_REG(PM_MISC_RIU, PM_MISC_DBG_OW) |= (PM_MISC_DBG_OW_EN|PM_MISC_DBG_OW_VAL);
	} else {
		UART_REG(PM_MISC_RIU, PM_MISC_DBG_OW) &= PM_MISC_DBG_MASK;
	}
	spin_unlock(&uart_misc_lock);
	uart_misc_debug("store hdmi2uart number result:%X \n", UART_REG(PM_MISC_RIU, PM_MISC_DBG_OW));

	return count;
}


static ssize_t hdmi_to_uart_sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
#define SW_SZ 4
	ssize_t ret = 0;
	uart_misc_debug("Sysfs hdmi2uart - Read!!!\n");
	unsigned int current_val = UART_REG(PM_MISC_RIU, PM_MISC_DBG_OW);
	uart_misc_debug("current_val:%X", current_val);
	uart_misc_debug("show hdmi2uart %lX \n", hdmi2uart_enable);
	snprintf(buf, SW_SZ, "%lx\n", hdmi2uart_enable);
	uart_misc_debug("store hdmi2uart number result:%X \n", UART_REG(PM_MISC_RIU, PM_MISC_DBG_OW));
	ret = strlen(buf) + 1;
	return ret;
}



static ssize_t uart_tx_enable_sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	uart_misc_debug("Sysfs uart tx enable - Write!!!\n");
	sscanf(buf, "%lx", &uart_tx_enable_number);
	uart_misc_debug("store uart_tx_enable_number 0x%02lx \n", uart_tx_enable_number);
	spin_lock(&uart_misc_lock);
	if (uart_tx_enable_number) {
		//UART_REG(PM_SLP_RIU, PM_SLP_UART) |= PM_SLP_UART_TX_ENABLE;
		UART_REG(PM_SLP_RIU, PM_SLP_UART) &= ~PM_SLP_HK51_UART_ENABLE;
	} else {
		//UART_REG(PM_SLP_RIU, PM_SLP_UART) &= ~PM_SLP_UART_TX_ENABLE;
		UART_REG(PM_SLP_RIU, PM_SLP_UART) |= PM_SLP_HK51_UART_ENABLE;
	}
	spin_unlock(&uart_misc_lock);
	uart_misc_debug("store uart_tx_enable_number result:%X \n", UART_REG(PM_SLP_RIU, PM_SLP_UART));

	return count;
}


static ssize_t uart_tx_enable_sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
#define SW_SZ 4
	ssize_t ret = 0;
	uart_misc_debug("Sysfs uart tx eanble - Read!!!\n");
	unsigned int current_val = UART_REG(PM_SLP_RIU, PM_SLP_UART);
	uart_misc_debug("current_val:%X", current_val);
	uart_misc_debug("show uart_tx_enable_number %lX \n", uart_tx_enable_number);
	snprintf(buf, SW_SZ, "%lx\n", uart_tx_enable_number);
	ret = strlen(buf) + 1;
	return ret;
}


static ssize_t uart_rx_enable_sysfs_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{
	uart_misc_debug("Sysfs uart rx enable - Write!!!\n");
	sscanf(buf, "%lx", &uart_rx_enable_number);
	uart_misc_debug("store uart_rx_enable_number 0x%02lx \n", uart_rx_enable_number);
	spin_lock(&uart_misc_lock);
	if (uart_rx_enable_number)
		UART_REG(PM_SLP_RIU, PM_SLP_UART) |= PM_SLP_UART_RX_ENABLE;
	else
		UART_REG(PM_SLP_RIU, PM_SLP_UART) &= ~PM_SLP_UART_RX_ENABLE;
	spin_unlock(&uart_misc_lock);
	uart_misc_debug("store uart_rx_enable_number result:%X \n", UART_REG(PM_SLP_RIU, PM_SLP_UART));

	return count;
}


static ssize_t uart_rx_enable_sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
#define SW_SZ 4
	ssize_t ret = 0;
	uart_misc_debug("Sysfs uart rx enable - Read!!!\n");
	unsigned int current_val = UART_REG(PM_SLP_RIU, PM_SLP_UART);
	uart_misc_debug("current_val:%X", current_val);
	uart_misc_debug("show uart_rx_enable_number %lX \n", uart_rx_enable_number);
	snprintf(buf, SW_SZ, "%lx\n", uart_rx_enable_number);
	ret = strlen(buf) + 1;
	return ret;
}

static struct kobj_attribute uart_switch[] = {  __ATTR(uart_switch, 0660, uart_switch_sysfs_show, uart_switch_sysfs_store),
												__ATTR(uart0_tx_dis, 0660, uart0_dis_sysfs_show, uart0_dis_sysfs_store),
												__ATTR(hdmi2uart, 0660,hdmi_to_uart_sysfs_show,hdmi_to_uart_sysfs_store),
												__ATTR(uart_tx_enable, 0660, uart_tx_enable_sysfs_show, uart_tx_enable_sysfs_store),
												__ATTR(uart_rx_enable, 0660, uart_rx_enable_sysfs_show, uart_rx_enable_sysfs_store),};

static int __init uart_misc_init(void)
{
	int i = 0;

	spin_lock_init(&uart_misc_lock);

	kobj_ref_misc = kobject_create_and_add("uart", kernel_kobj);
	for (i = 0; i < ARRAY_SIZE(uart_switch); i++)
		if (sysfs_create_file(kobj_ref_misc, &uart_switch[i].attr)) {
			uart_misc_debug("Cannot create sysfs file......\n");
			goto uart_misc_err;
		}

	return 0;

uart_misc_err:

	for (i = 0; i < ARRAY_SIZE(uart_switch); i++) {
		sysfs_remove_file(kernel_kobj, &uart_switch[i].attr);
	}

	return -ENOMEM;
}

static void __exit uart_misc_exit(void)
{
	int i = 0;
	uart_misc_debug("uart_misc driver remove , exit...\n");
	for (i = 0; i < ARRAY_SIZE(uart_switch); i++) {
		sysfs_remove_file(kernel_kobj, &uart_switch[i].attr);
	}
	kobject_put(kobj_ref_misc);
}
module_init(uart_misc_init);
module_exit(uart_misc_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mediatek");
MODULE_DESCRIPTION("Mtk uart misc driver ver 0.02");
