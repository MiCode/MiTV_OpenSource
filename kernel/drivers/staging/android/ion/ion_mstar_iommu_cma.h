/*
 * drivers/gpu/ion/ion_cma_heap.c
 *
 * Copyright (C) Linaro 2012
 * Author: <benjamin.gaignard@linaro.org> for ST-Ericsson.
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

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>

#include "ion.h"

#include <linux/version.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/proc_fs.h>
#include <linux/swap.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/seq_file.h>
#include <linux/dma-contiguous.h>
#include <linux/cma.h>
#include <cma.h>	// for struct cma


//#ifdef CONFIG_MP_MMA_CMA_ENABLE
#ifdef CONFIG_MSTAR_MIUPROTECT
#include "../../../mstar2/drv/miu_protect/mdrv_miu_protect.h"
#endif

#include <linux/highmem.h>
#include "mdrv_types.h"

#include "mdrv_system.h"

#define ION_CMA_ALLOCATE_FAILED -1

struct ion_cma_buffer_info {

    unsigned long flag;
    struct page* page;
    unsigned long size;
    dma_addr_t handle;
    struct sg_table *table;


};

struct delay_free_reserved {
    struct ion_buffer *buffer;
    //int delay_free_offset_in_heap;
    int delay_free_length;
    //filp_private *delay_free_pdev;//samson.huang develop stage "//" it,not delete
    int delay_free_time_out;
    unsigned long flags;
    struct list_head list;
};

typedef struct
{
    struct ion_cma_buffer_info *range_info;//info for each
    unsigned long range_len;//len of this range
    struct list_head list_node;
}cma_cache_buffer_range_node;

struct cma_cache_buffer_ranges
{
    struct ion_cma_buffer_info ranges_info;//info for reserved cma cache
    unsigned long ranges_total_len;//total len of cache ranges
    struct list_head list_head;
    bool cache_set;
    struct mutex lock;
};

#define PHYSICAL_START_INIT     UL(-1)
#define PHYSICAL_END_INIT       0

struct ion_cma_buffer
{
    //unsigned char miuBlockIndex;
    bool freed;
    #if 1
    struct page* page;
    #else
    void *cpu_addr;
    #endif

    unsigned long start_pa;
    unsigned long length;
    struct ion_cma_buffer_info *info;
    struct list_head list;
};

struct ion_cma_allocation_list
{

    unsigned long min_start;
    unsigned long max_end;
    unsigned long using_count;
    unsigned long freed_count;
    struct list_head list_head;
    struct mutex lock;
};



struct ion_cma_heap {
    struct ion_heap heap;
    struct device *dev;

    phys_addr_t base;
    size_t size;

    struct ion_cma_allocation_list alloc_list_unsecure;

    struct ion_cma_allocation_list alloc_list_secure;

    struct mutex lock;

    struct cma_cache_buffer_ranges unsecure_cache;//for un-secure buffer
    struct cma_cache_buffer_ranges secure_cache;//for secure buffer

    struct ion_cma *ion_cma;
};

#define to_cma_heap(x) container_of(x, struct ion_cma_heap, heap)

//-------------------------------------------------------------------------------------------------
// Type and Structure Declaration
//-------------------------------------------------------------------------------------------------
typedef enum
{
    CMA_ALLOC = 0,
    CMA_FREE
}BUFF_OPS;
//#endif
