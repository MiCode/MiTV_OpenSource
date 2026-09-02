/* SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause */
/******************************************************************************
 *
 * This file is provided under a dual license.  When you use or
 * distribute this software, you may choose to be licensed under
 * version 2 of the GNU General Public License ("GPLv2 License")
 * or BSD License.
 *
 * GPLv2 License
 *
 * Copyright(C) 2019 MediaTek Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See http://www.gnu.org/licenses/gpl-2.0.html for more details.
 *
 * BSD LICENSE
 *
 * Copyright(C) 2019 MediaTek Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

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

#include "input-compat.h"
#include "input_misc.h"

#define EVDEV_MISC_DEBUG
#ifdef EVDEV_MISC_DEBUG
#define evdev_misc_debug(fmt, args...) printk("[%s] " fmt, __FUNCTION__, ##args)
#else
#define evdev_misc_debug(fmt, args...) do {} while(0)
#endif

struct kobject *kobj_rf;
extern unsigned long invert_blk_key;
extern unsigned long en_blk_key;

extern struct list_head block_keyList;

struct ir_key_map *pkey_map;
extern struct list_head key_mapList;

static void free_list_block_key(void)
{
	struct  list_head *pos, *q;
	list_for_each_safe(pos, q, &block_keyList) {
		struct block_key *blkey = NULL;
		blkey = list_entry(pos, struct block_key, list);
		list_del_init(pos);
		kfree(blkey);
	}
}

static void free_list_switch_key(void)
{
	struct  list_head *pos, *q;
	list_for_each_safe(pos, q, &key_mapList)
	{
		struct ir_key_map *swkey = NULL;
		swkey = list_entry(pos, struct ir_key_map, list);
		list_del_init(pos);
		kfree(swkey);
	}
}

/***************ev_misc sysfs Fuctions **********************/
#if defined(CONFIG_CC_IS_CLANG) && defined(CONFIG_MSTAR_CHIP)
static ssize_t block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf);
static ssize_t invert_block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t invert_block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf);
static ssize_t enable_block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t enable_block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf);
static ssize_t key_switch_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, const char *buf, size_t count);
static ssize_t key_switch_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf);
#else
static ssize_t block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count);
static ssize_t block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count);
static ssize_t invert_block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count);
static ssize_t invert_block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count);
static ssize_t enable_block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count);
static ssize_t enable_block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count);
static ssize_t key_switch_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count);
static ssize_t key_switch_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count);
#endif

struct kobj_attribute ev_misc[] = { __ATTR(block_key, 0660, block_key_sysfs_show, block_key_sysfs_store),
									__ATTR(invert_block_key, 0660, invert_block_key_sysfs_show, invert_block_key_sysfs_store),
									__ATTR(enable_block_key, 0660, enable_block_key_sysfs_show, enable_block_key_sysfs_store),
									__ATTR(switch_key, 0660, key_switch_sysfs_show, key_switch_sysfs_store),
								};
#if defined(CONFIG_CC_IS_CLANG) && defined(CONFIG_MSTAR_CHIP)
static ssize_t block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, const char *buf, size_t count)
#else
static ssize_t block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count)
#endif
{
	struct block_key *pblock_key;
	unsigned long scancode, keycode;
	struct  list_head *pos, *q;
	char *buf_tmp;
	buf_tmp = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf_tmp)
		return -ENOMEM;
	evdev_misc_debug("Sysfs - Write!!!\n");
	sscanf(buf, "%lx %lx", &scancode, &keycode);
	evdev_misc_debug("store scancode: 0x%02lx ,keycode: 0x%02lx \n", scancode, keycode);
	bool find_key = false;
	pblock_key = kzalloc(sizeof(*pblock_key), GFP_KERNEL);
	if (!pblock_key) {
		/* handling error... */
		printk("pblock_key kzalloc fail\n`");
		kfree(buf_tmp);
		return -ENOMEM;
	}

	list_for_each_safe(pos, q, &block_keyList) {
		struct block_key *blkey = NULL;
		blkey = list_entry(pos, struct block_key, list);
		evdev_misc_debug("blkey->scancode: 0x%02x,blkey->keycode: 0x%02x  \n", blkey->scancode,blkey->keycode);
		snprintf(buf_tmp,PAGE_SIZE, "%s0x%02x 0x%02x\n", buf, blkey->scancode,blkey->keycode);
#if defined(CONFIG_CC_IS_CLANG) && defined(CONFIG_MSTAR_CHIP)
                memcpy((void *)buf,buf_tmp,strlen(buf_tmp));
#else
                memcpy(buf,buf_tmp,strlen(buf_tmp));
#endif
		if(blkey->scancode==scancode && blkey->keycode == keycode)
		{
			find_key=true;
			evdev_misc_debug("del blkey->scancode: 0x%02x,blkey->keycode: 0x%02x  \n", blkey->scancode,blkey->keycode);
			list_del_init(&blkey->list);
			kfree(blkey);
			break;
		} else {
			find_key = false;
		}
	}
	kfree(buf_tmp);
	if (!find_key) {
		pblock_key->scancode = scancode;
		pblock_key->keycode = keycode;
		list_add_tail(&pblock_key->list, &block_keyList);
	}
	return count;
}
#if defined(CONFIG_CC_IS_CLANG) && defined(CONFIG_MSTAR_CHIP)
static ssize_t block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf)
#else
static ssize_t block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count)
#endif
{
	ssize_t ret = 0;
	struct  list_head *pos, *q;
	char *buf_tmp;
	buf_tmp = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf_tmp)
		return -ENOMEM;
	evdev_misc_debug("Sysfs - Read!!!\n");
	list_for_each_safe(pos, q, &block_keyList) {
		struct block_key *blkey = NULL;
		blkey = list_entry(pos, struct block_key, list);
		evdev_misc_debug("blkey->scancode: 0x%02x,blkey->keycode: 0x%02x  \n", blkey->scancode, blkey->keycode);
		snprintf(buf_tmp, PAGE_SIZE, "%s0x%02x 0x%02x\n", buf, blkey->scancode, blkey->keycode);
		memcpy(buf, buf_tmp, strlen(buf_tmp));
	}
	kfree(buf_tmp);
	ret = strlen(buf) + 1;
	return ret;
}
#if defined(CONFIG_CC_IS_CLANG) && defined(CONFIG_MSTAR_CHIP)
static ssize_t invert_block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, const char *buf, size_t count)
#else
static ssize_t invert_block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count)
#endif
{
	evdev_misc_debug("Sysfs - Write!!!\n");
	sscanf(buf, "%lx", &invert_blk_key);
	evdev_misc_debug("store invert_blk_key 0x%02lx \n", invert_blk_key);
	return count;
}
#if defined(CONFIG_CC_IS_CLANG) && defined(CONFIG_MSTAR_CHIP)
static ssize_t invert_block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf)
#else
static ssize_t invert_block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count)
#endif
{
	ssize_t ret = 0;
	evdev_misc_debug("Sysfs - Read!!!\n");
	sscanf(buf, "%lx", &invert_blk_key);
	evdev_misc_debug("store invert_blk_key 0x%02lx \n", invert_blk_key);
	snprintf(buf, sizeof invert_blk_key, "%lx\n", invert_blk_key);
	ret = strlen(buf) + 1;
	return ret;
}

#if defined(CONFIG_CC_IS_CLANG) && defined(CONFIG_MSTAR_CHIP)
static ssize_t enable_block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, const char *buf, size_t count)
#else
static ssize_t enable_block_key_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count)
#endif
{
	//evdev_misc_debug("%s:Sysfs - Write!!!\n",__func__);
	sscanf(buf, "%lx", &en_blk_key);
	evdev_misc_debug("store en_blk_key 0x%02lx \n", en_blk_key);
	return count;
}
#if defined(CONFIG_CC_IS_CLANG) && defined(CONFIG_MSTAR_CHIP)
static ssize_t enable_block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf)
#else
static ssize_t enable_block_key_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count)
#endif
{
	ssize_t ret = 0;
	//evdev_misc_debug("Sysfs - Read!!!\n");
	sscanf(buf, "%lx", &en_blk_key);
	evdev_misc_debug("store en_blk_key 0x%02lx \n", en_blk_key);
	snprintf(buf, sizeof en_blk_key, "%lx\n", en_blk_key);
	ret = strlen(buf) + 1;
	return ret;
}

#if defined(CONFIG_CC_IS_CLANG) && defined(CONFIG_MSTAR_CHIP)
static ssize_t key_switch_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, const char *buf, size_t count)
#else
static ssize_t key_switch_sysfs_store(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count)
#endif
{
	unsigned long scancode, keycode, keycode_new;
	bool find_key = false;
	struct  list_head *pos, *q;
	sscanf(buf, "%lx %lx %lx", &scancode, &keycode, &keycode_new);
	evdev_misc_debug("store scancode: 0x%02lx ,keycode: 0x%02lx ,keycode_new: 0x%02lx \n", scancode, keycode, keycode_new);
	list_for_each_safe(pos, q, &key_mapList) {
		struct ir_key_map *swkey = NULL;
		swkey = list_entry(pos, struct ir_key_map, list);
		evdev_misc_debug("swkey->scancode: 0x%02x,swkey->keycode: 0x%02x  ,swkey->keycode_new: 0x%02x\n",swkey->scancode,swkey->keycode,swkey->keycode_new);

		if (swkey->scancode == scancode && swkey->keycode == keycode && swkey->keycode_new == keycode_new) {
			find_key = true;
			evdev_misc_debug("del swkey->scancode: 0x%02x,swkey->keycode: 0x%02x ,swkey->keycode_new: 0x%02x \n", swkey->scancode, swkey->keycode, swkey->keycode_new);
			list_del_init(&swkey->list);
			kfree(swkey);
			break;
		} else {
			find_key = false;
		}
	}

	if (!find_key) {
		pkey_map = kzalloc(sizeof(*pkey_map), GFP_KERNEL);
		if (!pkey_map) {
			printk("pkey_map kzalloc fail\n");
			return -ENOMEM;
		}
		pkey_map->scancode = scancode;
		pkey_map->keycode = keycode;
		pkey_map->keycode_new = keycode_new;
		list_add_tail(&pkey_map->list, &key_mapList);
	}

	return count;
}
#if defined(CONFIG_CC_IS_CLANG) && defined(CONFIG_MSTAR_CHIP)
static ssize_t key_switch_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf)
#else
static ssize_t key_switch_sysfs_show(struct kobject *kobj,struct kobj_attribute *attr, char *buf, size_t count)
#endif
{
	ssize_t ret = 0;
	struct list_head *pos, *q;
	char *buf_tmp;
	buf_tmp = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf_tmp)
		return -ENOMEM;
	evdev_misc_debug("Sysfs - Read!!!\n");
	list_for_each_safe(pos, q, &key_mapList) {
		struct ir_key_map *swkey = NULL;
		swkey = list_entry(pos, struct ir_key_map, list);
		evdev_misc_debug("swkey->scancode: 0x%02x,swkey->keycode: 0x%02x ,swkey->keycode_new: 0x%02x\n", swkey->scancode,swkey->keycode,swkey->keycode_new);
		snprintf(buf_tmp,PAGE_SIZE, "%s0x%02x 0x%02x 0x%02x\n", buf, swkey->scancode,swkey->keycode,swkey->keycode_new);
		memcpy(buf,buf_tmp,strlen(buf_tmp));
	}
	kfree(buf_tmp);
	ret = strlen(buf) + 1;
	return ret;
}

static long evdev_misc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return 0;
}

static int evdev_misc_open(struct inode *inode, struct file *filp)
{
	int id = MINOR(inode->i_rdev);
	filp->private_data = (void *)id;
	return 0;
}

static int evdev_misc_close(struct inode *inode, struct file *filp)
{
	return 0;
}

static ssize_t evdev_misc_read(struct file *filp, char *buf, size_t size, loff_t *f_pos)
{
	return 0;
}

static ssize_t evdev_misc_write(struct file *filp, const char *buf, size_t size, loff_t *f_pos)
{
	size_t pos;
	uint8_t byte;
	for (pos = 0; pos < size; ++pos) {
		if (copy_from_user(&byte, buf + pos, 1) != 0) {
			break;
		}
	}
	return pos;
}


static struct file_operations evdev_misc_fops = {
	.owner                             = THIS_MODULE,
	.open                              = evdev_misc_open,
	.release                           = evdev_misc_close,
	.read                              = evdev_misc_read,
	.write                             = evdev_misc_write,
	.unlocked_ioctl                    = evdev_misc_ioctl,
};


static int evdev_misc_init(void)
{
	int i = 0;
	/*Creating a directory in /sys/kernel/ */
	kobj_rf = kobject_create_and_add("ev_misc", kernel_kobj);
	for (i = 0; i < ARRAY_SIZE(ev_misc); i++)
		if (sysfs_create_file(kobj_rf, &ev_misc[i].attr)) {
			evdev_misc_debug("Cannot create sysfs file......\n");
			goto err_sysfs;
		}
	return 0;

err_sysfs:
	kobject_put(kobj_rf);
	for (i = 0; i < ARRAY_SIZE(ev_misc); i++) {
		sysfs_remove_file(kernel_kobj, &ev_misc[i].attr);
	}
	return -1;
}

static void evdev_misc_exit(void)
{
	int i = 0;
	free_list_block_key();
	free_list_switch_key();
	kobject_put(kobj_rf);
	for (i = 0; i < ARRAY_SIZE(ev_misc); i++) {
		sysfs_remove_file(kernel_kobj, &ev_misc[i].attr);
	}
	evdev_misc_debug("evdev_misc driver remove , exit...\n");
}
module_init(evdev_misc_init);
module_exit(evdev_misc_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("MStar");
MODULE_DESCRIPTION("evdev misc driver ver 0.02");
