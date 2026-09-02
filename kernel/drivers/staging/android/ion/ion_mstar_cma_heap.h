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
/// @file   ion_mstar_cma_heap.h
/// @brief  mstar ion heap interface
/// @author MStar Semiconductor Inc.
///
///////////////////////////////////////////////////////////////////////////////////////////////////

#ifndef _LINUX_ION_MSATR_CMA_HEAP_H
#define _LINUX_ION_MSATR_CMA_HEAP_H
//-------------------------------------------------------------------------------------------------
//  Include Files
//-------------------------------------------------------------------------------------------------


//-------------------------------------------------------------------------------------------------
//  Macro and Define
//-------------------------------------------------------------------------------------------------
#define DEBUG

//-------------------------------------------------------------------------------------------------
//  Type and Structure
//-------------------------------------------------------------------------------------------------
enum cma_heap_flag{
	DESCRETE_CMA,
	CONTINUOUS_ONLY_CMA
};

#define CACHE_POOL_LEN 0x200000
#define CACHE_POOL_MIN 0x400000
#define CACHE_POOL_MAX 0x800000

struct cma_cache_node{
	struct list_head list;
	struct page *page;
	unsigned long len;
};

struct mstar_cma_heap_private {
	struct device *cma_dev;
	enum cma_heap_flag flag;	//flag for cma type
};

struct ion_mstar_cma_buffer_info {
	unsigned long flag;
	struct page* page;
	unsigned long size;
	dma_addr_t handle;
	struct sg_table *table;
};

struct mstar_cma {
	/* page swap worker wait queue */
	wait_queue_head_t cma_swap_wait;
	struct cma* cma;
	struct mutex contig_rtylock;

	struct list_head cache_head;
	spinlock_t cache_lock;
	unsigned long cache_size;
	unsigned long cache_page_count;
	unsigned long fail_alloc_count;
};

struct ion_mstar_cma_heap {
	struct ion_heap heap;
	struct device *dev;
	struct mstar_cma *mstar_cma;
};

#define ION_MSTAR_CMA_ALLOCATE_FAILED -1
#define MSTAR_CMA_HEAP_DEBUG 0
#define CMA_CONTIG_RTYCNT 1

#ifdef CONFIG_BUG
#define CMA_BUG_ON(condition, format...) ({						\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		__WARN_printf(format);					\
	if (unlikely(__ret_warn_on))					\
		BUG();					\
})
#else
#define CMA_BUG_ON(condition, format...)
#endif

#define to_mstar_cma_heap(x)	container_of(x, struct ion_mstar_cma_heap, heap)
#define to_mstar_cma(x)		container_of(x, struct mstar_cma, cma)

//-------------------------------------------------------------------------------------------------
//  Function and Variable
//-------------------------------------------------------------------------------------------------
struct page* __mstar_get_discrete(struct ion_heap *heap);
int __mstar_free_one_page(struct ion_heap *heap,struct page *page);
void get_system_heap_info(struct cma *system_cma, int *mali_heap_info);
void get_cma_heap_info(struct ion_heap *heap, int *mali_heap_info, char *name);
int in_cma_range(struct ion_heap* heap, struct page* page);
unsigned long  get_free_bit_count(struct cma *cma, unsigned long start, unsigned long len);
struct ion_heap *find_heap_by_page(struct page* page);
#endif  /* _LINUX_ION_MSATR_CMA_HEAP_H */
