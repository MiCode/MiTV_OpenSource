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

///////////////////////////////////////////////////////////////////////////////////////////////////
///
/// @file   mstar_ion.c
/// @brief  mstar ion driver
/// @author MStar Semiconductor Inc.
///
///////////////////////////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
//  Include files
//-------------------------------------------------------------------------------------------------
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/memblock.h>
#include <linux/dma-contiguous.h>
#include <linux/cma.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include "../ion.h"
#include "mdrv_types.h"
#include "mdrv_system.h"
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/dma-mapping.h>
#include <linux/highmem.h>
#include <linux/of.h>

#include "../ion_mstar_cma_heap.h"

#ifdef CONFIG_MP_MMA_CMA_ENABLE
#include "../ion_mstar_iommu_cma.h"
#endif
#include <cma.h>
static int num_heaps;
static struct ion_device *idev;
static struct ion_heap **heaps;

// to remove later
struct ion_platform_heap *heaps_info;

//-------------------------------------------------------------------------------------------------
// Macros
//-------------------------------------------------------------------------------------------------
#define ION_COMPAT_STR  "mstar,ion-mstar"
#define ION_SYSTEM_HEAP_NAME  "ion_system_heap"

#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
/*extern struct CMA_BootArgs_Config cma_config[MAX_CMA_AREAS];
extern struct device mstar_cma_device[MAX_CMA_AREAS];
extern int mstar_driver_boot_cma_buffer_num;
#else
int mstar_driver_boot_cma_buffer_num = 0;*/
#endif

static struct ion_heap* s_mali_current_heap = NULL;
static struct ion_heap* mali_heaps[3] = {NULL};		//mali heaps: [0] mali miu 0 heap
static int s_current_mali_alloc_strategy = -1;
int mali_alloc_strategy = 0;

/**
 * These heaps are listed in the order they will be allocated.
 * Don't swap the order unless you know what you are doing!
 */
#define MSTAR_DEFAULT_HEAP_NUM 1
static struct ion_platform_heap mstar_default_heaps[MSTAR_DEFAULT_HEAP_NUM] = {
		/*
		 * type means the heap_type of ion, which will decide the alloc/free methods, map[user/kernel], ...
		 * id is used to specify different usage, each id will have a unique heap, we can alloc buffer with a heap_id
		 * cma heap is added by boot args, don't add cma id here manually
		 */

		{
			.id    = ION_SYSTEM_HEAP_ID,
			.type   = ION_HEAP_TYPE_SYSTEM,
			.name   = ION_SYSTEM_HEAP_NAME,
		},
};

static struct ion_platform_data ion_pdata;

static struct platform_device ion_dev = {
	.name = "ion-mstar",
	.id = 1,
	.dev = { .platform_data = &ion_pdata },
};

static struct platform_device *common_ion_devices[] __initdata = {
	&ion_dev,
};

//for kernel mali driver usage
void mali_attch_heap(void)
{
	int i;

	for (i = 0; i < num_heaps; i++) {
		struct ion_platform_heap *heap_data = &ion_pdata.heaps[i];

		if(heap_data->id == ION_MALI_MIU0_HEAP_ID)
			mali_heaps[0] = heaps[i];
		else if(heap_data->id == ION_MALI_MIU1_HEAP_ID)
			mali_heaps[1] = heaps[i];
		else if(heap_data->id == ION_MALI_MIU2_HEAP_ID)
			mali_heaps[2] = heaps[i];
	}
}

struct ion_heap *find_heap_by_page(struct page* page)
{
	int i;

	for (i = 0; i < 3; i++) {
		if(mali_heaps[i] && in_cma_range(mali_heaps[i],page))
			return mali_heaps[i];
	}
	return NULL;
}

struct page* mali_alloc_page(void)
{
	struct page *page;

	if(s_current_mali_alloc_strategy != mali_alloc_strategy){
		BUG_ON(mali_alloc_strategy < 0 || mali_alloc_strategy >= 3);
		s_mali_current_heap = mali_heaps[mali_alloc_strategy];
		s_current_mali_alloc_strategy = mali_alloc_strategy;
	}

	if(s_mali_current_heap){
		page = __mstar_get_discrete(s_mali_current_heap);
		if(page)
			clear_highpage(page);//printk("alloc page pfn=%x \n ",page_to_pfn(page));
		else
			printk("alloc null page \n");
	}
	else
		page = alloc_page(GFP_HIGHUSER | __GFP_ZERO | ___GFP_RETRY_MAYFAIL | __GFP_NOWARN);

	return page;
}
EXPORT_SYMBOL(mali_alloc_page);

void mali_free_page(struct page* page)
{
	int ret;
	struct ion_heap* heap;
	if(s_mali_current_heap){
		ret = __mstar_free_one_page(s_mali_current_heap,page);
		if(ret){	//current heap changed, get the heap the page alloc from
			heap = find_heap_by_page(page);
			if(heap)
				__mstar_free_one_page(heap,page);
			else
				__free_page(page);
		}
	}
	else{
		//case 1: alloc with current_heap !NULL, but when free, current_heap changed to NULL
		//case 2: alloc with current_heap NULL, free with current_heap NULL
		//can not judge which case unless do find_heap_by_page
		heap = find_heap_by_page(page);
		if(heap)
			__mstar_free_one_page(heap,page);
		else
			__free_page(page);
	}
}
EXPORT_SYMBOL(mali_free_page);

inline unsigned long get_mali_alloc_strategy(void)
{
	return mali_alloc_strategy;
}
EXPORT_SYMBOL(get_mali_alloc_strategy);


inline void set_mali_alloc_strategy(unsigned long mali)
{
	mali_alloc_strategy = mali;
}
EXPORT_SYMBOL(set_mali_alloc_strategy);

//for mali end

#ifdef CONFIG_MP_CMA_PATCH_CMA_DYNAMIC_STRATEGY
extern int lowmem_minfree[6];
extern unsigned long totalreserve_pages;
unsigned int lowmem_minfree_index = 3;   // the lowmem_minfree[lowmem_minfree_index] will be CMA_threshold_low of zone

unsigned int dcma_debug = 0;
unsigned int dcma_migration_bound_percentage = 90;
unsigned int dcma_migration_bound_value = 0;	// kB

static int early_dcma_debug_param(char *buf)
{
	if (!buf)
		return -EINVAL;

	if (strcmp(buf, "on") == 0)
		dcma_debug = 1;

	return 0;
}
early_param("dcma_debug", early_dcma_debug_param);

bool should_dcma_work(struct zone *checked_zone)
{
	unsigned long zone_available_cma_kbytes = 0;
	unsigned long zone_cma_free_kbytes = 0;

	zone_cma_free_kbytes = atomic_long_read(&checked_zone->vm_stat[NR_FREE_CMA_PAGES]) << (PAGE_SHIFT - 10);	// including default_cma buffer

	if(dcma_migration_bound_value)
	{
		if(zone_cma_free_kbytes >= dcma_migration_bound_value)
			return false;
		else
			return true;
	}
	else
	{
		zone_available_cma_kbytes = checked_zone->managed_cma_pages << (PAGE_SHIFT - 10);
		if(zone_cma_free_kbytes >= (((100 - dcma_migration_bound_percentage) * zone_available_cma_kbytes) / 100))	// cma_free_kbytes >= (1 - (dcma_migration_bound_percentage/100)) * available_cma_kbytes
			return false;
		else
			return true;
	}
}

void show_dcma_info(void)
{
	struct zone *zone;
	unsigned long flags;
	unsigned long zone_available_cma_kbytes = 0;

	printk(KERN_EMERG "\033[31mdcma_debug is %d\033[m\n", dcma_debug);
	printk(KERN_EMERG "\033[31mdcma_migration_bound_value is %d\033[m\n", dcma_migration_bound_value);
	printk(KERN_EMERG "\033[31mdcma_migration_bound_percentage is %d\033[m\n\n", dcma_migration_bound_percentage);
	printk(KERN_EMERG "\033[31mlowmem_minfree[%d] = %u kB\033[m\n", lowmem_minfree_index, lowmem_minfree[lowmem_minfree_index] << (PAGE_SHIFT - 10));

	printk(KERN_EMERG "\033[31mtotalreserve_pages is %lu kB\033[m\n", totalreserve_pages << (PAGE_SHIFT - 10));
	printk(KERN_EMERG "\033[31mInactive(anon) is %lu kB\033[m\n", global_node_page_state(NR_INACTIVE_ANON) << (PAGE_SHIFT - 10));
	printk(KERN_EMERG "\033[31mInactive(file) is %lu kB\033[m\n", global_node_page_state(NR_INACTIVE_FILE) << (PAGE_SHIFT - 10));
	printk(KERN_EMERG "\033[31mSlabReclaimable is %lu kB\033[m\n", global_page_state(NR_SLAB_RECLAIMABLE) << (PAGE_SHIFT - 10));
	printk(KERN_EMERG "\033[31mtotal free is %lu kB\033[m\n", global_page_state(NR_FREE_PAGES) << (PAGE_SHIFT - 10));
	printk(KERN_EMERG "\033[31mtotal cma_free is %lu kB\033[m\n", global_page_state(NR_FREE_CMA_PAGES) << (PAGE_SHIFT - 10));
	//printk(KERN_EMERG "\033[31mif (zone_cma_free_kbytes >= dcma_migration_bound_percentage to value) ==> disable dcma\033[m\n");

	/* zone_page_state is for "a specified zone"; global_page_state is for "whole system"
	 * we use global_page_state(NR_FREE_CMA_PAGES) to get total cma free of whole syste
	 * we use zone_page_state(zone, NR_FREE_CMA_PAGES) to get total cma free of a zone
	 */
	for_each_populated_zone(zone)
	{
		spin_lock_irqsave(&zone->lock, flags);
		zone_available_cma_kbytes = zone->managed_cma_pages << (PAGE_SHIFT - 10);
		printk(KERN_EMERG "\033[31m\nzone: %s, %s-able dcma\033[m\n", zone->name, should_dcma_work(zone)? "En":"Dis");
		printk(KERN_EMERG "    total_CMA_size = %u kB\n", zone->total_CMA_size);
		printk(KERN_EMERG "    managed_cma_pages = %lu kB\n", zone->managed_cma_pages << (PAGE_SHIFT - 10));
		printk(KERN_EMERG "    CMA_threshold_low = %d kB\n    CMA_threshold_high = %d kB\n", zone->CMA_threshold_low, zone->CMA_threshold_high);

		printk(KERN_EMERG "\033[31m    zone_reserve_pages is %lu kB\033[m\n", zone->zone_reserve_pages << (PAGE_SHIFT - 10));
		printk(KERN_EMERG "\033[31m    zone_inactive_anon is %lu kB\033[m\n", atomic_long_read(&zone->vm_stat[NR_ZONE_INACTIVE_ANON]) << (PAGE_SHIFT - 10));
		printk(KERN_EMERG "\033[31m    zone_inactive_file is %lu kB\033[m\n", atomic_long_read(&zone->vm_stat[NR_ZONE_INACTIVE_FILE]) << (PAGE_SHIFT - 10));
		printk(KERN_EMERG "\033[31m    zone_slab_reclaimable is %lu kB\033[m\n", atomic_long_read(&zone->vm_stat[NR_SLAB_RECLAIMABLE]) << (PAGE_SHIFT - 10));
		printk(KERN_EMERG "\033[31m    zone_free_kbytes is %lu kB\033[m\n", atomic_long_read(&zone->vm_stat[NR_FREE_PAGES]) << (PAGE_SHIFT - 10));
		printk(KERN_EMERG "\033[31m    zone_cma_free_kbytes is %lu kB\033[m\n", atomic_long_read(&zone->vm_stat[NR_FREE_CMA_PAGES]) << (PAGE_SHIFT - 10));
		printk(KERN_EMERG "\033[31m    dcma_migration_bound_percentage to value is %lu kB\033[m\n", (((100 - dcma_migration_bound_percentage) * zone_available_cma_kbytes) / 100));
		spin_unlock_irqrestore(&zone->lock, flags);
	}
}



module_param(dcma_migration_bound_percentage, uint, 0644);
MODULE_PARM_DESC(dcma_migration_bound_percentage, "dcma boundary(percentage) for enabling/disabling dcma");
/* with higher dcma_migration_bound_percentage, more cma memory will be distributed by buddy system.
 * So that the later cma_alloc needs more time to do cma migration.
 * In ideal case, you will need to migrate dcma_migration_bound_percentage of allocated memory.
 * For example, if you allocate 100MB CMA, then you need to migrate "dcma_migration_bound_percentage * 100MB" memory.
 * If available_cma_kbytes - cma_free <= (dcma_migration_bound_percentage * available_cma_kbytes) / 100,
 * the distributed cma memory is still so low that we can still use cma for movable allocation ==> disable dcma
 */

module_param(dcma_migration_bound_value, uint, 0644);
MODULE_PARM_DESC(dcma_migration_bound_value, "dcma boundary(value) for enabling/disabling dcma");
/* If cma_free >= dcma_migration_bound_value,
 * which means the cma_free is still so low that we can still use cma for movable allocation ==> disable dcma
 */

module_param(dcma_debug, uint, 0644);
MODULE_PARM_DESC(dcma_debug, "Debug for dcma");
#endif
