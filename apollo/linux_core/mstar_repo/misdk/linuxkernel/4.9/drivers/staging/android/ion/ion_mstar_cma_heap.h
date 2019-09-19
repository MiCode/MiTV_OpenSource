////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2006-2007 MStar Semiconductor, Inc.
// All rights reserved.
//
// Unless otherwise stipulated in writing, any and all information contained
// herein regardless in any format shall remain the sole proprietary of
// MStar Semiconductor Inc. and be kept in strict confidence
// (¡§MStar Confidential Information¡¨) by the recipient.
// Any unauthorized act including without limitation unauthorized disclosure,
// copying, use, reproduction, sale, distribution, modification, disassembling,
// reverse engineering and compiling of the contents of MStar Confidential
// Information is unlawful and strictly prohibited. MStar hereby reserves the
// rights to any and all damages, losses, costs and expenses resulting therefrom.
//
////////////////////////////////////////////////////////////////////////////////

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
#define ION_CUST_ERR(fmt, args...) printk(KERN_ERR "error %s:%d " fmt,__FUNCTION__,__LINE__,## args)

#ifndef ION_CUST_DEBUG
#ifdef DEBUG
#define ION_CUST_DEBUG(fmt, args...) printk(KERN_ERR "%s:%d " fmt,__FUNCTION__,__LINE__,## args)
#else
#define ION_CUST_DEBUG(fmt, args...) do {} while(0)
#endif
#endif

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

struct mstar_cma{
	wait_queue_head_t cma_swap_wait;//page swap worker wait queue
	struct cma* 	cma;
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

#define CMA_BUG_ON(condition, format...) ({						\
	int __ret_warn_on = !!(condition);				\
	if (unlikely(__ret_warn_on))					\
		__WARN_printf(format);					\
	if (unlikely(__ret_warn_on))					\
		BUG();					\
})

#define to_mstar_cma_heap(x)          container_of(x, struct ion_mstar_cma_heap, heap)
#define to_mstar_cma(x)	container_of(x, struct mstar_cma, cma)

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
#endif	//_LINUX_ION_MSATR_CMA_HEAP_H
