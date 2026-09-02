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

#include <asm/dma-contiguous.h>
#include <asm/outercache.h>
#include <linux/bug.h>
#include <linux/cma.h>
#include <linux/device.h>
#include <linux/dma-contiguous.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/migrate.h>
#include <linux/page-isolation.h>
#include <linux/proc_fs.h>
#include <linux/slab.h>
#include <linux/swap.h>
#include <linux/memblock.h>
#include <internal.h>
#include <cma.h>
#include <linux/time.h>

#include "ion.h"
#include "ion_mstar_cma_heap.h"

static struct ion_heap **heaps;
#define IOMMU_DISCRETE_CMA_ALIGN (2<<20)
static const unsigned int orders_iommu_cma[] = {8, 4, 0};
#define NUM_ORDER_IOMMU_CMA ARRAY_SIZE(orders_iommu_cma)
#ifdef CONFIG_MP_MMA_UMA_WITH_NARROW
extern u64 mma_dma_zone_size;
#endif
static gfp_t high_order_iommu_cma_gfp_flags = (GFP_HIGHUSER | __GFP_NOWARN | __GFP_NORETRY) & ~__GFP_RECLAIM;
static gfp_t low_order_iommu_cma_gfp_flags = (GFP_HIGHUSER);

static int ion_mstar_cma_shrink_lock(struct ion_heap *heap);
static int get_cma_freebits(struct cma *cma);

static void ion_mstar_cma_clear_pages(struct page *page, unsigned long len)
{
	int pfn = page_to_pfn(page);

	CMA_BUG_ON(len % PAGE_SIZE, "size not aligned\n");
	while(len > 0){
		page = pfn_to_page(pfn);
		clear_highpage(page);
		pfn++;
		len -= PAGE_SIZE;
	}
}

unsigned long  get_free_bit_count(struct cma *cma, unsigned long start, unsigned long len)
{
	unsigned long count = 0;
	unsigned long offset_zero;
	unsigned long end = start+len;

	if (len == 0 || start >= cma->count)
		return 0;

	end = start + len;
	end = min(end, cma->count);

	offset_zero = start;

	while (offset_zero < cma->count){
		unsigned offset_bit;
		offset_zero = find_next_zero_bit(cma->bitmap, end, offset_zero);
		if(offset_zero>= end)
			break;
		offset_bit = find_next_bit(cma->bitmap, end, offset_zero+1);
		count += offset_bit - offset_zero;
		offset_zero = offset_bit + 1;
	}

	return count;
}

static int ion_mstar_cma_get_sgtable(struct device *dev, struct sg_table *sgt,
			       struct page *page, dma_addr_t handle, size_t size)
{
	int ret;

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (unlikely(ret))
		return ret;

	sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
	return 0;
}

static int cma_increase_pool_freelist_threshold(struct ion_mstar_cma_heap *mstar_cma_heap, unsigned long len)
{
	struct mstar_cma *mstar_cma= mstar_cma_heap->mstar_cma;
	unsigned long retlen = 0, nlen = CACHE_POOL_LEN;
	struct device *dev = mstar_cma_heap->dev;
	struct cma_cache_node *node;
	struct page *page;
	int pfn, totalfreebits;
	int ret = -ENOMEM;

	//No free bits in cma, return directly
	totalfreebits = get_cma_freebits(mstar_cma->cma);
	if(totalfreebits == 0)
		return -ENOMEM;

	/*
	 *Auto adjust cache size & cache node size
	 */
	if(nlen < len)
		nlen = PAGE_ALIGN(len);

	spin_lock(&mstar_cma->cache_lock);
	len += mstar_cma->cache_size;
	spin_unlock(&mstar_cma->cache_lock);
	if(len < CACHE_POOL_MAX)
		len = CACHE_POOL_MAX;
	else
		len = PAGE_ALIGN(len);

	spin_lock(&mstar_cma->cache_lock);
	while(mstar_cma->cache_size < len){
		spin_unlock(&mstar_cma->cache_lock);
		page = dma_alloc_from_contiguous_direct(dev, nlen>>PAGE_SHIFT, 0, &retlen);
		if(!page){
			if(ret == -ENOMEM)
				return ret;
			else
				return 0;
		}
		ret = 0;

		pfn = page_to_pfn(page);

		CMA_BUG_ON((page && !retlen) || (!page && retlen), "alloc out of cma area\n");
		CMA_BUG_ON(pfn < mstar_cma->cma->base_pfn, "alloc out of cma arean");
		CMA_BUG_ON(pfn >= mstar_cma->cma->base_pfn + mstar_cma->cma->count, "alloc out of cma area\n");

		node = kmalloc(sizeof(struct cma_cache_node), GFP_KERNEL);
		if(!node){
			printk("[%s] Alloc cache node fail\n", __func__);
			dma_release_from_contiguous(dev, page, retlen);
			return -ENOMEM;
		}
		INIT_LIST_HEAD(&node->list);
		node->len = retlen * PAGE_SIZE;
		node->page = page;

		spin_lock(&mstar_cma->cache_lock);
		list_add_tail(&node->list, &mstar_cma->cache_head);
		mstar_cma->cache_size += retlen * PAGE_SIZE;
		mstar_cma->cache_page_count += retlen;
	}
	spin_unlock(&mstar_cma->cache_lock);
	return 0;
}

//get one page
struct page* __mstar_get_discrete(struct ion_heap *heap)
{
	struct ion_mstar_cma_heap *mstar_heap = to_mstar_cma_heap(heap);
	struct mstar_cma *mstar_cma = mstar_heap->mstar_cma;
	struct page *page;
	struct cma_cache_node *node;
	int pfn;
	struct list_head *head;

retry:
	head = &mstar_cma->cache_head;
	spin_lock(&mstar_cma->cache_lock);

	CMA_BUG_ON(list_empty(&mstar_cma->cache_head) && mstar_cma->cache_size, "list empty but size is not 0\n");
	CMA_BUG_ON(!list_empty(&mstar_cma->cache_head) && !mstar_cma->cache_size, "list not empty but size is 0\n");
	CMA_BUG_ON(mstar_cma->cache_size != mstar_cma->cache_page_count * PAGE_SIZE, "cache size not match cache pages, %ld, %ld\n", mstar_cma->cache_page_count, mstar_cma->cache_size);

	if(unlikely(list_empty(head))){
		spin_unlock(&mstar_cma->cache_lock);
		cma_increase_pool_freelist_threshold(mstar_heap, 0);

		spin_lock(&mstar_cma->cache_lock);
		if(unlikely(list_empty(head))){
			spin_unlock(&mstar_cma->cache_lock);
			printk("\033[31mFunction = %s, Line = %d, mali cma no page\033[m\n", __PRETTY_FUNCTION__, __LINE__);
			page = alloc_page(GFP_HIGHUSER| __GFP_ZERO | ___GFP_RETRY_MAYFAIL | __GFP_NOWARN);
			return page;
		}
		else{
			spin_unlock(&mstar_cma->cache_lock);
			goto retry;
		}
	}
	else
	{
		BUG_ON(list_empty(&mstar_cma->cache_head) && mstar_cma->cache_size);

		node = list_first_entry(head, struct cma_cache_node, list);
		pfn = page_to_pfn(node->page);
		page = node->page;
		node->len -= PAGE_SIZE;
		if(node->len == 0){
			list_del(&node->list);
			kfree(node);
		}
		else
			node->page = pfn_to_page(pfn + 1);

		mstar_cma->cache_size -= PAGE_SIZE;
		mstar_cma->cache_page_count--;

		CMA_BUG_ON(mstar_cma->cache_size < 0 || mstar_cma->cache_page_count < 0, "cache < 0\n");
		CMA_BUG_ON(mstar_cma->cache_size % PAGE_SIZE, "cache size not aligned to page size\n");

		spin_unlock(&mstar_cma->cache_lock);
		return page;
	}
}

static int ion_mstar_cma_alloc_discrete(struct ion_heap *heap,
														struct ion_buffer *buffer,
														unsigned long len)
{
	struct cma_cache_node *node, *tmp;
	struct ion_mstar_cma_buffer_info* buffer_info;
	struct ion_mstar_cma_heap * mstar_heap = to_mstar_cma_heap(heap);
	struct mstar_cma *mstar_cma = mstar_heap->mstar_cma;
	struct scatterlist *sg;
	struct list_head head, *cache_head = &mstar_cma->cache_head;
	int index = 0, ret;
	len = PAGE_ALIGN(len);

	INIT_LIST_HEAD(&head);

	buffer_info = kzalloc(sizeof(struct ion_mstar_cma_buffer_info), GFP_KERNEL);
	if (!buffer_info) {
		printk("[%s] Can't allocate buffer info\n", __FUNCTION__);
		return ION_MSTAR_CMA_ALLOCATE_FAILED;
	}

retry:
	spin_lock(&mstar_cma->cache_lock);

	CMA_BUG_ON(list_empty(&mstar_cma->cache_head) && mstar_cma->cache_size, "list empty but size is not 0\n");
	CMA_BUG_ON(!list_empty(&mstar_cma->cache_head) && !mstar_cma->cache_size, "list not empty but size is 0\n");
	CMA_BUG_ON(mstar_cma->cache_size != mstar_cma->cache_page_count * PAGE_SIZE, "cache size not match cache pages\n");

	list_for_each_entry_safe(node, tmp, cache_head, list){

		CMA_BUG_ON(!node->len, "cache node len is 0\n");

		if(len >= node->len){
			list_del(&node->list);
			list_add_tail(&node->list, &head);
			len -= node->len;
			mstar_cma->cache_size -= node->len;
			mstar_cma->cache_page_count -= node->len / PAGE_SIZE;
			CMA_BUG_ON(mstar_cma->cache_size < 0 || mstar_cma->cache_page_count < 0, "cache < 0\n");
		}
		else{
			struct cma_cache_node *nnode = kmalloc(sizeof(struct cma_cache_node), GFP_KERNEL);
			int pfn = page_to_pfn(node->page);
			if(!nnode){
				printk("%s: malloc cma cache node failed\n", __func__);
				spin_unlock(&mstar_cma->cache_lock);
				goto err3;
			}
			nnode->page = node->page;
			nnode->len = len;
			node->len -= len;
			node->page = pfn_to_page(pfn + len / PAGE_SIZE);
			mstar_cma->cache_size -= len;
			mstar_cma->cache_page_count -= len / PAGE_SIZE;
			CMA_BUG_ON(mstar_cma->cache_size < 0 || mstar_cma->cache_page_count < 0, "cache < 0\n");
			INIT_LIST_HEAD(&nnode->list);
			list_add_tail(&nnode->list, &head);
			len = 0;
		}
		index++;

		CMA_BUG_ON(mstar_cma->cache_size < 0 || mstar_cma->cache_page_count < 0, "cache < 0\n");
		if(len == 0)
			break;
	}
	spin_unlock(&mstar_cma->cache_lock);

	if(len){
		cma_increase_pool_freelist_threshold(mstar_heap, len);

		spin_lock(&mstar_cma->cache_lock);
		if(list_empty(&mstar_cma->cache_head)){
			spin_unlock(&mstar_cma->cache_lock);
			while(len){
				struct page *page;
				struct cma_cache_node *nnode;
				printk("\033[31mFunction = %s, Line = %d, mali cma no page\033[m\n", __PRETTY_FUNCTION__, __LINE__);
				page = alloc_page(GFP_HIGHUSER| __GFP_ZERO | ___GFP_RETRY_MAYFAIL | __GFP_NOWARN);
				if(!page){
					printk("[%s] alloc page fail, %d\n", __FUNCTION__, __LINE__);
					goto err3;
				}
				nnode = kmalloc(sizeof(struct cma_cache_node), GFP_KERNEL);
				if(!nnode){
					printk("[%s] malloc cma cache node faill, %d\n", __FUNCTION__, __LINE__);
					__free_page(page);
					goto err3;
				}
				nnode->len = PAGE_SIZE;
				nnode->page = page;
				INIT_LIST_HEAD(&nnode->list);
				list_add_tail(&nnode->list, &head);

				index++;
				len -= PAGE_SIZE;
			}
		}
		else{
			spin_unlock(&mstar_cma->cache_lock);
			goto retry;
		}
	}

	buffer_info->table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!buffer_info->table){
		printk("[%s] malloc buffer table failed, %d\n", __FUNCTION__, __LINE__);
		goto err3;
	}
	ret = sg_alloc_table(buffer_info->table, index, GFP_KERNEL);
	if (ret){
		printk("[%s] malloc sg table failed, %d\n", __FUNCTION__, __LINE__);
		goto err2;
	}

	sg = buffer_info->table->sgl;
	list_for_each_entry_safe(node, tmp, &head, list){
		struct page *page = node->page;
		sg_set_page(sg, page, node->len, 0);
		ion_mstar_cma_clear_pages(node->page, node->len);
//		ion_pages_sync_for_device(NULL, page, node->len, DMA_BIDIRECTIONAL);
		sg = sg_next(sg);
		list_del(&node->list);
		kfree(node);
	}
	buffer_info->flag |= ION_FLAG_DISCRETE;
	buffer->priv_virt = buffer_info;
	buffer->sg_table = buffer_info->table;
	return 0;

	sg_free_table(buffer_info->table);
err2:
	kfree(buffer_info->table);
err3:
	spin_lock(&mstar_cma->cache_lock);
	list_for_each_entry_safe(node, tmp, cache_head, list) {
		int pfn = page_to_pfn(node->page);
		struct page *page;
		for(index = 0; index < node->len / PAGE_SIZE; index++){
			page = pfn_to_page(pfn + index);
			__mstar_free_one_page(heap, page);
		}
		list_del(&node->list);
		kfree(node);
	}
	spin_unlock(&mstar_cma->cache_lock);
	kfree(buffer_info);
	return -ENOMEM;
}

#if 0
static int cma_swap_worker_thread(void *p)
{
	struct ion_mstar_cma_heap *mstar_cma_heap = (struct ion_mstar_cma_heap *)p;
	struct mstar_cma *mstar_cma = mstar_cma_heap->mstar_cma;
	set_freezable();
	while(1){
		DEFINE_WAIT(wait);
		try_to_freeze();

		spin_lock(&mstar_cma->cache_lock);

		CMA_BUG_ON(list_empty(&mstar_cma->cache_head) && mstar_cma->cache_size, "list empty but size is not 0\n");
		CMA_BUG_ON(!list_empty(&mstar_cma->cache_head) && !mstar_cma->cache_size, "list not empty but size is 0\n");
		CMA_BUG_ON(mstar_cma->cache_size != mstar_cma->cache_page_count * PAGE_SIZE, "cache size not match cache pages\n");

		if(mstar_cma->cache_size < CACHE_POOL_MIN){
			spin_unlock(&mstar_cma->cache_lock);
			cma_increase_pool_freelist_threshold(mstar_cma_heap, 0);
		}
		else
			spin_unlock(&mstar_cma->cache_lock);

		prepare_to_wait(&mstar_cma->cma_swap_wait, &wait, TASK_INTERRUPTIBLE);
		schedule_timeout(HZ * 20);
		finish_wait(&mstar_cma->cma_swap_wait, &wait);
	}
	return 0;
}
#endif

int __mstar_free_one_page(struct ion_heap *heap, struct page *page)
{
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(heap);
	struct mstar_cma *mstar_cma = mstar_cma_heap->mstar_cma;
	unsigned long pfn = page_to_pfn(page);

	CMA_BUG_ON(!heap || !page, "NULL heap or page pointer\n");
	CMA_BUG_ON(atomic_read(&page->_refcount)!= 1,"page_count=%d \n",page_count(page));

	if (!in_cma_range(heap, page)) {
		heap = find_heap_by_page(page);
		if (heap) {
			mstar_cma_heap = to_mstar_cma_heap(heap);
			mstar_cma = mstar_cma_heap->mstar_cma;
		}
	}

	if (heap) {
		mutex_lock(&mstar_cma->cma->lock);
		if (!test_and_clear_bit(pfn - mstar_cma->cma->base_pfn, mstar_cma->cma->bitmap))
			BUG();
		mutex_unlock(&mstar_cma->cma->lock);
#ifdef CONFIG_MP_CMA_PATCH_CMA_AGGRESSIVE_ALLOC
		adjust_managed_cma_page_count(page_zone(pfn_to_page(mstar_cma->cma->base_pfn)), 1);
#endif
	}

	CMA_BUG_ON(page_count(page) != 1,"page_count=%d\n",page_count(page));

	__free_page(page);
	return 0;
}

void __dma_clear_buffer2(struct page *page, size_t size)
{
	void *ptr;
	if (!page)
		return;
	ptr = page_address(page);
	if (ptr) {
		memset(ptr, 0, size);
		dmac_flush_range(ptr, ptr + size);
		outer_flush_range(__pa(ptr), __pa(ptr) + size);
	}
}

static int ion_mstar_cma_alloc_contiguous(struct ion_heap *heap,
		struct ion_buffer *buffer,
		unsigned long len,
		unsigned long flags)
{
	struct ion_mstar_cma_buffer_info* buffer_info;
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(heap);
	struct device *dev = mstar_cma_heap->dev;
	struct mstar_cma *mstar_cma = mstar_cma_heap->mstar_cma;
	int freepg, retry = 0;

	buffer_info = kzalloc(sizeof(struct ion_mstar_cma_buffer_info), GFP_KERNEL);
	if (!buffer_info) {
		dev_err(dev, "Can't allocate buffer info\n");
		return ION_MSTAR_CMA_ALLOCATE_FAILED;
	}

ALLOC:
    buffer_info->page = dma_alloc_from_contiguous(dev, len>>PAGE_SHIFT, get_order(len), 0);
    buffer_info->size = len;
    //__dma_clear_buffer2(buffer_info->page, (len>>PAGE_SHIFT) << PAGE_SHIFT);
	if (!buffer_info->page) {
		if (retry < CMA_CONTIG_RTYCNT)
		{
			// get lock in the first retry round
			if (!retry)
			{
				mutex_lock(&mstar_cma->contig_rtylock);
			}

			freepg = get_free_bit_count(mstar_cma->cma, 0, mstar_cma->cma->count)
					+ mstar_cma->cache_page_count;
			// number of free pages is qualified
			if (freepg >= len>>PAGE_SHIFT)
			{
				// we can shrink cma cache and try again
				retry++;
				freepg = ion_mstar_cma_shrink_lock(heap);
				printk("CMA contig alloc retry by shrinking %d pages!\n", freepg);

				goto ALLOC;
			}
			else
			{
				mutex_unlock(&mstar_cma->contig_rtylock);
				//printk("CMA contig alloc retry fail, lack of free pages! free:0x%x need:0x%lx retry:%d\n",
					//freepg, len>>PAGE_SHIFT, retry);
			}
		}
		else
		{
			mutex_unlock(&mstar_cma->contig_rtylock);
			printk("CMA contig alloc retry fail!\n");
		}

		dev_err(dev, "Fail to allocate buffer1\n");
		goto err;
	}

	if (retry && (retry <= CMA_CONTIG_RTYCNT))
	{
		mutex_unlock(&mstar_cma->contig_rtylock);
		printk("CMA contig alloc retry pass! retry %d\n", retry);
	}

#if defined(CONFIG_ARM)
	buffer_info->handle = (dma_addr_t)__pfn_to_phys(page_to_pfn(buffer_info->page));		// page to pfn to bus_addr
#else
	buffer_info->handle = (dma_addr_t)((page_to_pfn(buffer_info->page)) << PAGE_SHIFT); 	// page to pfn to bus_addr
#endif

	buffer_info->table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!buffer_info->table) {
		dev_err(dev, "Fail to allocate sg table\n");
		goto free_mem;
	}

	if (ion_mstar_cma_get_sgtable
	    (dev, buffer_info->table, buffer_info->page, buffer_info->handle, len)){
			dma_release_from_contiguous(dev, buffer_info->page, len >> PAGE_SHIFT);
			goto err1;
	}

	if (flags & ION_FLAG_ZERO_MEMORY)
	{
		ion_mstar_cma_clear_pages(buffer_info->page, len);
	}

	if (flags & (ION_FLAG_CACHED | ION_FLAG_ZERO_MEMORY))
	{
//		ion_pages_sync_for_device(NULL, buffer_info->page, len, DMA_BIDIRECTIONAL);
	}

	buffer_info->flag |= ION_FLAG_CONTIGUOUS;
	buffer->priv_virt = buffer_info;
	buffer->sg_table = buffer_info->table;
	dev_dbg(dev, "Allocate buffer %p\n", buffer);

	return 0;

err1:
	kfree(buffer_info->table);
free_mem:
	dma_release_from_contiguous(dev, buffer_info->page, len >> PAGE_SHIFT);
err:
	kfree(buffer_info);
	return ION_MSTAR_CMA_ALLOCATE_FAILED;
}


static int ion_mstar_cma_alloc_from_addr(struct ion_heap *heap,
														struct ion_buffer *buffer,
														unsigned long start,
														unsigned long len,
														unsigned long flags)
{
	struct ion_mstar_cma_buffer_info* buffer_info;
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(heap);
	struct device *dev = mstar_cma_heap->dev;
	struct mstar_cma *mstar_cma = mstar_cma_heap->mstar_cma;
	int freepg, retry = 0;

	buffer_info = kzalloc(sizeof(struct ion_mstar_cma_buffer_info), GFP_KERNEL);
	if (!buffer_info) {
		dev_err(dev, "Can't allocate buffer info\n");
		return ION_MSTAR_CMA_ALLOCATE_FAILED;
	}

ALLOC:
	//buffer_info->page = dma_alloc_from_contiguous_addr(dev,start,len >> PAGE_SHIFT,1);
	buffer_info->page = dma_alloc_at_from_contiguous(dev, len >> PAGE_SHIFT, 1, start);
	buffer_info->size = len;

	if (!buffer_info->page) {
		if (retry < CMA_CONTIG_RTYCNT)
		{
			// get lock in the first retry round
			if (!retry)
			{
				mutex_lock(&mstar_cma->contig_rtylock);
			}

			freepg = get_free_bit_count(mstar_cma->cma, 0, mstar_cma->cma->count)
					+ mstar_cma->cache_page_count;
			// number of free pages is qualified
			if (freepg >= len>>PAGE_SHIFT)
			{
				// we can shrink cma cache and try again
				retry++;
				freepg = ion_mstar_cma_shrink_lock(heap);
				printk("CMA contig alloc retry with addr by shrinking %d pages!\n", freepg);

				goto ALLOC;
			}
			else
			{
				mutex_unlock(&mstar_cma->contig_rtylock);
				//printk("CMA contig alloc retry with addr fail, lack of free pages! free:0x%x need:0x%lx retry:%d\n",
					//freepg, len>>PAGE_SHIFT, retry);
			}
		}
		else
		{
			mutex_unlock(&mstar_cma->contig_rtylock);
			printk("CMA contig alloc retry with addr fail!\n");
		}

		dev_err(dev, "Fail to allocate buffer1\n");
		goto err;
	}

	if (retry && (retry <= CMA_CONTIG_RTYCNT))
	{
		mutex_unlock(&mstar_cma->contig_rtylock);
		printk("CMA contig alloc retry with addr pass! retry %d\n", retry);
	}

#if defined(CONFIG_ARM)
	buffer_info->handle = (dma_addr_t)__pfn_to_phys(page_to_pfn(buffer_info->page));		// page to pfn to bus_addr
#else
	buffer_info->handle = (dma_addr_t)((page_to_pfn(buffer_info->page)) << PAGE_SHIFT); 	// page to pfn to bus_addr
#endif

	buffer_info->table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!buffer_info->table) {
		dev_err(dev, "Fail to allocate sg table\n");
		goto free_mem;
	}

	if (ion_mstar_cma_get_sgtable
	    (dev, buffer_info->table, buffer_info->page, buffer_info->handle, len)){
			dma_release_from_contiguous(dev, buffer_info->page, len >> PAGE_SHIFT);
			goto err1;
	}

	if (flags & ION_FLAG_ZERO_MEMORY)
	{
		ion_mstar_cma_clear_pages(buffer_info->page, len);
	}

	if (flags & (ION_FLAG_CACHED | ION_FLAG_ZERO_MEMORY))
	{
//		ion_pages_sync_for_device(NULL, buffer_info->page, len, DMA_BIDIRECTIONAL);
        }

	buffer_info->flag |= ION_FLAG_CONTIGUOUS;
	buffer->priv_virt = buffer_info;
	buffer->sg_table = buffer_info->table;
	dev_dbg(dev, "Allocate buffer %p\n", buffer);
	return 0;

err1:
	kfree(buffer_info->table);
free_mem:
	dma_release_from_contiguous(dev, buffer_info->page, len >> PAGE_SHIFT);
err:
	kfree(buffer_info);
	return ION_MSTAR_CMA_ALLOCATE_FAILED;
}

#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
static int ion_mstar_cma_alloc_fake_memory(
	struct ion_heap *heap,
	struct ion_buffer *buffer,
	unsigned long len,
	unsigned long align,
	unsigned long flags)
{
	struct ion_mstar_cma_buffer_info* buffer_info;
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(heap);
	struct device *dev = mstar_cma_heap->dev;
	struct mstar_cma *mstar_cma = mstar_cma_heap->mstar_cma;
	int freepg, retry = 0;
	unsigned long retlen = 0;

	buffer_info = kzalloc(sizeof(struct ion_mstar_cma_buffer_info), GFP_KERNEL);
	if (!buffer_info) {
		dev_err(dev, "Can't allocate buffer info\n");
		return ION_MSTAR_CMA_ALLOCATE_FAILED;
	}

	//1.alloc phy space
	buffer_info->page = dma_alloc_from_fake_memory(dev, len >> PAGE_SHIFT, 1, &retlen);
	buffer_info->size = retlen;
	if (buffer_info->page == NULL || retlen < len) {
		dev_err(dev, "allocate NG: page=%px, len[%#lx,%#lx], alloc fake_memory failed.\n",
            buffer_info->page, retlen, len);
		goto err;
	}

#if defined(CONFIG_ARM)
	buffer_info->handle = (dma_addr_t)__pfn_to_phys(page_to_pfn(buffer_info->page));        // page to pfn to bus_addr
#else
	buffer_info->handle = (dma_addr_t)((page_to_pfn(buffer_info->page)) << PAGE_SHIFT);     // page to pfn to bus_addr
#endif
	printk("%s:%d page=%px, len[%#lx,%#lx], handle=%pad.\n", __func__, __LINE__,
		buffer_info->page, len, buffer_info->size, &buffer_info->handle);

	//2.alloc sg table.
	buffer_info->table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!buffer_info->table) {
		dev_err(dev, "Fail to allocate sg table\n");
		goto free_mem;
	}

	if (ion_mstar_cma_get_sgtable(dev, buffer_info->table, buffer_info->page, buffer_info->handle, len))
	{
		dma_release_from_fake_memory(dev, buffer_info->page, len >> PAGE_SHIFT);
		goto err1;
	}

	//3. init buffer info
	buffer_info->flag |= ION_FLAG_FAKE_MEMORY;
	buffer->priv_virt = buffer_info;
	//printk(KERN_EMERG "\033[31mFunction = %s, Line = %d, buffer->priv_virt->handle is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, ((struct ion_mstar_cma_buffer_info *)(buffer->priv_virt))->handle);
	//printk(KERN_EMERG "\033[31mFunction = %s, Line = %d, buffer->priv_virt->size is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, ((struct ion_mstar_cma_buffer_info *)(buffer->priv_virt))->size);
	//printk(KERN_EMERG "\033[31mFunction = %s, Line = %d, buffer->priv_virt->flag is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, ((struct ion_mstar_cma_buffer_info *)(buffer->priv_virt))->flag);
	buffer->sg_table = buffer_info->table;
	dev_dbg(dev, "Allocate buffer %p\n", buffer);
	return 0;

err1:
	kfree(buffer_info->table);
free_mem:
	dma_release_from_fake_memory(dev, buffer_info->page, len >> PAGE_SHIFT);
err:
	kfree(buffer_info);
	return ION_MSTAR_CMA_ALLOCATE_FAILED;
}
#endif

static int ion_mstar_cma_alloc_iommu_cma(struct ion_heap *heap, struct ion_buffer *buffer, unsigned long len,
														unsigned long flags)
{
	struct cma_cache_node *node, *tmp;
	struct ion_mstar_cma_buffer_info *buffer_info;
	struct ion_mstar_cma_heap *mstar_heap = to_mstar_cma_heap(heap);
	struct device *dev = mstar_heap->dev;
	struct mstar_cma *mstar_cma = mstar_heap->mstar_cma;
	struct scatterlist *sg = NULL;
	struct page *page = NULL;
	struct list_head head;
	int  size = 0, index = 0, ret = 0;
	unsigned int max_order = orders_iommu_cma[0];
	gfp_t gfp_mask;

	len = PAGE_ALIGN(len);
	INIT_LIST_HEAD(&head);

	buffer_info = kzalloc(sizeof(struct ion_mstar_cma_buffer_info), GFP_KERNEL);
	if (!buffer_info) {
		printk("[%s] Can't allocate buffer info\n", __FUNCTION__);
		return ION_MSTAR_CMA_ALLOCATE_FAILED;
	}

	buffer_info->size = len;

	buffer_info->table = kzalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!buffer_info->table){
		printk( "%s::%s::%d::malloc buffer table failed\n", __FILE__, __FUNCTION__, __LINE__);
		goto err2;
	}

	while(len)
	{
		if(len >= IOMMU_DISCRETE_CMA_ALIGN)
			size = IOMMU_DISCRETE_CMA_ALIGN;
		else
			size = len;
		page = dma_alloc_from_contiguous(dev, size>>PAGE_SHIFT, get_order(IOMMU_DISCRETE_CMA_ALIGN), 1);
		if (!page) {
			break;
		}
		struct cma_cache_node *nnode;
		nnode = kmalloc(sizeof(struct cma_cache_node), GFP_KERNEL);
		if(!nnode){
			printk("%s::%s::%d::malloc cma cache node faill\n", __FILE__, __FUNCTION__, __LINE__);
			goto err1;
		}
		nnode->len = size;
		nnode->page = page;
		INIT_LIST_HEAD(&nnode->list);
		list_add_tail(&nnode->list, &head);
		len -=size;
		index++;
	}

	while(len){
		int i;
		for(i = 0; i < NUM_ORDER_IOMMU_CMA; i++){
			if (len < (PAGE_SIZE<<orders_iommu_cma[i])){
				continue;
			}
			if (max_order < orders_iommu_cma[i]){
				continue;
			}

			if(orders_iommu_cma[i] > 0)
				gfp_mask = high_order_iommu_cma_gfp_flags;
			else
				gfp_mask = low_order_iommu_cma_gfp_flags;

#if CONFIG_MP_MMA_UMA_WITH_NARROW
			if(flags & ION_FLAG_DMAZONE){
				gfp_mask &= ~__GFP_HIGHMEM;
				gfp_mask |= GFP_DMA;
			}
#endif
			page = alloc_pages(gfp_mask, orders_iommu_cma[i]);
			if(!page){
				continue;
			}
			max_order = orders_iommu_cma[i];
			break;
		}

		if(!page){
			printk("%s::%s::%d, alloc_pages fail!\n", __FILE__, __FUNCTION__, __LINE__);
			goto err1;
		}

		struct cma_cache_node *nnode;
		nnode = kmalloc(sizeof(struct cma_cache_node), GFP_KERNEL);
		if(!nnode){
			printk("%s::%s::%d::malloc cma cache node faill\n", __FILE__, __FUNCTION__, __LINE__);
			goto err1;
		}
		nnode->len = (PAGE_SIZE<<max_order);
		nnode->page = page;
		INIT_LIST_HEAD(&nnode->list);
		list_add_tail(&nnode->list, &head);
		len -= (PAGE_SIZE<<max_order);
		index++;
	}

	ret = sg_alloc_table(buffer_info->table, index, GFP_KERNEL);
	sg = buffer_info->table->sgl;
	if (ret){
		printk("%s::%s::%d::malloc sg table failed\n", __FILE__, __FUNCTION__, __LINE__);
		goto err1;
	}

	list_for_each_entry_safe(node, tmp, &head, list){
		sg_set_page(sg, node->page, node->len, 0);
		if(!(flags & ION_FLAG_IOMMU_FLUSH)){
			ion_mstar_cma_clear_pages(node->page, node->len);

		}
		sg = sg_next(sg);
		list_del(&node->list);
		kfree(node);
	}
	buffer_info->flag |= ION_FLAG_IOMMU_CMA;
	buffer->priv_virt = buffer_info;
	buffer->sg_table = buffer_info->table;

	return 0;

err1:
	kfree(buffer_info->table);
	if(index)
	{
		list_for_each_entry_safe(node, tmp, &head, list){
			if(in_cma_range(heap, node->page))
				dma_release_from_contiguous(dev, node->page, node->len >> PAGE_SHIFT);
			else
				__free_pages(node->page, get_order(node->len));
			list_del(&node->list);
			kfree(node);
		}
	}
err2:
	kfree(buffer_info);
	return ION_MSTAR_CMA_ALLOCATE_FAILED;
}


static int ion_mstar_cma_allocate(struct ion_heap *heap, struct ion_buffer *buffer,
			    unsigned long len,
			    unsigned long flags)

{
	int ret;

	if(flags & ION_FLAG_CONTIGUOUS)
	{
		ret = ion_mstar_cma_alloc_contiguous(heap, buffer, len, flags);
		buffer->size = len;
	}
	else if(flags & ION_FLAG_STARTADDR)
	{
		ret = ion_mstar_cma_alloc_from_addr(heap, buffer, buffer->start, len, flags);
		buffer->size = len;
	}
#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
	else if (flags & ION_FLAG_FAKE_MEMORY)
	{
		ret = ion_mstar_cma_alloc_fake_memory(heap, buffer, len, buffer->start, flags);
		buffer->size = len;
	}
#endif
	else if(flags & ION_FLAG_IOMMU_CMA){
		ret = ion_mstar_cma_alloc_iommu_cma(heap, buffer, len, flags);
	}
	else
		ret = ion_mstar_cma_alloc_discrete(heap, buffer, len);

	return ret;
}

static void ion_mstar_cma_free(struct ion_buffer *buffer)
{
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(buffer->heap);
	struct device *dev = mstar_cma_heap->dev;
	struct ion_mstar_cma_buffer_info *buffer_info = buffer->priv_virt;
	int i;

	dev_dbg(dev, "Release buffer %p\n", buffer);
	/* release memory */

	if(buffer_info->flag & ION_FLAG_DISCRETE)
	{
		struct sg_table *table = buffer_info->table;
		struct scatterlist *sg;

		for_each_sg(table->sgl, sg, table->nents, i){
			//CMA_BUG_ON(sg->length != PAGE_SIZE,"invalide sg length %d \n",sg->length);
			struct page *page = sg_page(sg);
			unsigned long i, pfn = page_to_pfn(page);
			for(i = 0; i < sg->length; i+= PAGE_SIZE){
				page = pfn_to_page(pfn);
				__mstar_free_one_page(buffer->heap,page);
				pfn++;
			}
		}
	}
	else if(buffer_info->flag & (ION_FLAG_CONTIGUOUS | ION_FLAG_STARTADDR))
	{
		dma_release_from_contiguous(dev, buffer_info->page, buffer_info->size >> PAGE_SHIFT);
	}
#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
	else if (buffer_info->flag & ION_FLAG_FAKE_MEMORY)
	{
		dma_release_from_fake_memory(dev, buffer_info->page, buffer_info->size >> PAGE_SHIFT);
	}
#endif
	else if(buffer_info->flag & ION_FLAG_IOMMU_CMA){
		struct sg_table *table = buffer_info->table;
		struct scatterlist *sg;

		for_each_sg(table->sgl, sg, table->nents, i){
			struct page *page = sg_page(sg);
			unsigned long size = sg->length;

			CMA_BUG_ON(!buffer->heap || !page, "NULL heap or page pointer\n");
			CMA_BUG_ON(atomic_read(&page->_refcount)!= 1,"page_count=%d \n",page_count(page));

			if(in_cma_range(buffer->heap, page))
				dma_release_from_contiguous(dev, page, size >> PAGE_SHIFT);
			else
				__free_pages(page, get_order(size));
		}
	}
	else
		CMA_BUG_ON(1,"BUG!!! \n");

	/* release sg table */
	sg_free_table(buffer_info->table);
	kfree(buffer_info->table);
	kfree(buffer_info);
}

/* return physical address in addr */
static int ion_mstar_cma_phys(struct ion_heap *heap, struct ion_buffer *buffer,
			phys_addr_t *addr, size_t *len)
{
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(buffer->heap);
	struct device *dev = mstar_cma_heap->dev;
	struct ion_mstar_cma_buffer_info *info = buffer->priv_virt;

	if(info->flag & (ION_FLAG_DISCRETE | ION_FLAG_IOMMU_CMA))
	{
		pr_err("not ION_FLAG_DISCRETE");
		return -ENODEV;
	}
#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
	else if(info->flag & (ION_FLAG_CONTIGUOUS | ION_FLAG_STARTADDR | ION_FLAG_FAKE_MEMORY))
#else
	else if(info->flag & (ION_FLAG_CONTIGUOUS | ION_FLAG_STARTADDR))
#endif
	{
		dev_dbg(dev, "Return buffer %p physical address 0x%pa\n", buffer,
			&info->handle);

		*addr = info->handle;
		*len = buffer->size;
	}
	else
		CMA_BUG_ON(1,"BUG!!! \n");
	return 0;
}

#if 0
static struct sg_table *ion_mstar_cma_heap_map_dma(struct ion_heap *heap,
					     struct ion_buffer *buffer)
{
	struct ion_mstar_cma_buffer_info *info = buffer->priv_virt;

	return info->table;
}
#endif

#if 0
static void ion_mstar_cma_heap_unmap_dma(struct ion_heap *heap,
				   struct ion_buffer *buffer)
{
	return;
}
#endif


static int ion_cma_heap_map_user(struct ion_heap *heap, struct ion_buffer *buffer,
		      struct vm_area_struct *vma)
{
	struct ion_mstar_cma_buffer_info *info = buffer->priv_virt;

	struct sg_table *table = info->table;
	unsigned long addr = vma->vm_start;
	unsigned long offset = vma->vm_pgoff * PAGE_SIZE;
	struct scatterlist *sg;
	int i;
	int ret;

	for_each_sg(table->sgl, sg, table->nents, i) {
		struct page *page = sg_page(sg);
		unsigned long remainder = vma->vm_end - addr;
		unsigned long len = sg->length;

		if (offset >= sg->length) {
			offset -= sg->length;
			continue;
		} else if (offset) {
			page += offset / PAGE_SIZE;
			len = sg->length - offset;
			offset = 0;
		}
		len = min(len, remainder);
		ret = remap_pfn_range(vma, addr, page_to_pfn(page), len,
				vma->vm_page_prot);
		if (ret)
			return ret;
		addr += len;
		if (addr >= vma->vm_end)
			return 0;
	}
	return 0;
}

static void *ion_mstar_cma_map_kernel(struct ion_heap *heap,
				struct ion_buffer *buffer)
{
	struct ion_mstar_cma_buffer_info *info = buffer->priv_virt;

	if(info->flag & (ION_FLAG_DISCRETE | ION_FLAG_IOMMU_CMA))
		return ion_heap_map_kernel(heap,buffer);
	else if(info->flag & (ION_FLAG_CONTIGUOUS | ION_FLAG_STARTADDR)){
		return ion_heap_map_kernel(heap,buffer);
	}
	else
		CMA_BUG_ON(1,"BUG!!! \n");

	return 0;
}

static void ion_mstar_cma_unmap_kernel(struct ion_heap *heap,
					struct ion_buffer *buffer)
{
	struct ion_mstar_cma_buffer_info *info = buffer->priv_virt;

	if(info->flag & (ION_FLAG_DISCRETE | ION_FLAG_IOMMU_CMA))
		return ion_heap_unmap_kernel(heap,buffer);
	else if(info->flag & (ION_FLAG_CONTIGUOUS | ION_FLAG_STARTADDR)){
		return ion_heap_unmap_kernel(heap,buffer);
	}
	else
		CMA_BUG_ON(1,"BUG!!! \n");
}

static int ion_mstar_cma_shrink(struct ion_heap *heap, gfp_t gfp_mask,
					int nr_to_scan)
{
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(heap);
	struct mstar_cma *mstar_cma = mstar_cma_heap->mstar_cma;
	unsigned long pfn, freed = 0, len = 0;
	struct page *page;
	struct cma_cache_node *node, *tmp;

	if (gfpflags_to_migratetype(gfp_mask) != MIGRATE_MOVABLE)
		return 0;

	if(!mutex_trylock(&mstar_cma->cma->lock))
		goto OUT_NO_LOCK;

	spin_lock(&mstar_cma->cache_lock);
	list_for_each_entry_safe(node, tmp, &mstar_cma->cache_head, list){
		pfn = page_to_pfn(node->page);
		len = 0;

		CMA_BUG_ON(pfn<mstar_cma->cma->base_pfn || pfn>=mstar_cma->cma->base_pfn+mstar_cma->cma->count, "pfn=%lx,cma[%lx,%lx] \n",pfn,mstar_cma->cma->base_pfn,mstar_cma->cma->count);

		/*
		 *Free entire node, so freed may be a little larger than @nr_to_scan
		 */
		while(len < node->len){
			if(!test_and_clear_bit(pfn - mstar_cma->cma->base_pfn, mstar_cma->cma->bitmap)){
				BUG();
			}
			page = pfn_to_page(pfn);
			__free_page(page);
			pfn++;
			len += PAGE_SIZE;
		}
		list_del(&node->list);
		mstar_cma->cache_size -= node->len;
		mstar_cma->cache_page_count -= node->len / PAGE_SIZE;
		freed += node->len / PAGE_SIZE;
		kfree(node);
		if(freed >= nr_to_scan){
			break;
		}
	}
	spin_unlock(&mstar_cma->cache_lock);
	mutex_unlock(&mstar_cma->cma->lock);

OUT_NO_LOCK:
	return mstar_cma->cache_page_count;
}

static int ion_mstar_cma_shrink_lock(struct ion_heap *heap)
{
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(heap);
	struct mstar_cma *mstar_cma = mstar_cma_heap->mstar_cma;
	unsigned long pfn, freed = 0, len = 0;
	struct page *page;
	struct cma_cache_node *node, *tmp;

	mutex_lock(&mstar_cma->cma->lock);
	spin_lock(&mstar_cma->cache_lock);

	list_for_each_entry_safe(node, tmp, &mstar_cma->cache_head, list){
		pfn = page_to_pfn(node->page);
		len = 0;

		CMA_BUG_ON(pfn<mstar_cma->cma->base_pfn || pfn>=mstar_cma->cma->base_pfn+mstar_cma->cma->count, "pfn=%lx,cma[%lx,%lx] \n",pfn,mstar_cma->cma->base_pfn,mstar_cma->cma->count);

		while(len < node->len){
			if(!test_and_clear_bit(pfn - mstar_cma->cma->base_pfn, mstar_cma->cma->bitmap)){
				BUG();
			}
			page = pfn_to_page(pfn);
			__free_page(page);
			pfn++;
			len += PAGE_SIZE;
		}
		list_del(&node->list);
		mstar_cma->cache_size -= node->len;
		mstar_cma->cache_page_count -= node->len / PAGE_SIZE;
		freed += node->len / PAGE_SIZE;
		kfree(node);
	}

	spin_unlock(&mstar_cma->cache_lock);
	mutex_unlock(&mstar_cma->cma->lock);

	return freed;
}

#ifdef CONFIG_MSTAR_CMAPOOL
struct device *ion_mstar_cma_get_dev(struct ion_heap *heap)
{
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(heap);
	return mstar_cma_heap->dev;
}
#endif

static struct ion_heap_ops ion_mstar_cma_ops = {
	.allocate = ion_mstar_cma_allocate,
	.free = ion_mstar_cma_free,
	//.map_dma = ion_mstar_cma_heap_map_dma,
	//.unmap_dma = ion_mstar_cma_heap_unmap_dma,
	.phys = ion_mstar_cma_phys,
	.map_user = ion_cma_heap_map_user,
	.map_kernel = ion_mstar_cma_map_kernel,
	.unmap_kernel = ion_mstar_cma_unmap_kernel,
	.shrink = ion_mstar_cma_shrink,
#ifdef CONFIG_MSTAR_CMAPOOL
	.get_dev = ion_mstar_cma_get_dev,
#endif
};

struct ion_heap *ion_mstar_cma_heap_create(struct ion_platform_heap *data)
{
	struct ion_mstar_cma_heap *mstar_cma_heap;
	struct cma *cma;
	//char name[64];
	struct mstar_cma_heap_private *mcma_private;

	mstar_cma_heap = kzalloc(sizeof(struct ion_mstar_cma_heap), GFP_KERNEL);

	if (!mstar_cma_heap)
		return ERR_PTR(-ENOMEM);

	mstar_cma_heap->heap.ops = &ion_mstar_cma_ops;
	/* get device from private heaps data, later it will be
	 * used to make the link with reserved CMA memory */

	mcma_private = (struct mstar_cma_heap_private *)data->priv;
	mstar_cma_heap->dev = (struct device*)(mcma_private->cma_dev);
	printk("\033[31mFunction = %s, dev = %s, cma = %p, cma from 0x%lX to 0x%lX\033[m\n",
		__PRETTY_FUNCTION__, mstar_cma_heap->dev->init_name, mstar_cma_heap->dev->cma_area,
		mstar_cma_heap->dev->cma_area->base_pfn, mstar_cma_heap->dev->cma_area->base_pfn+mstar_cma_heap->dev->cma_area->count);
	BUG_ON(!mstar_cma_heap->dev);
	mstar_cma_heap->heap.type = ION_HEAP_TYPE_MSTAR_CMA;

	mstar_cma_heap->mstar_cma = kzalloc(sizeof(struct mstar_cma),GFP_KERNEL);
	if(!mstar_cma_heap->mstar_cma)
		goto err;

	//initialize the mstar cma private data
	cma = dev_get_cma_area(mstar_cma_heap->dev);
	if (!cma)
		goto err;

	mstar_cma_heap->mstar_cma->cma = cma;
	init_waitqueue_head(&mstar_cma_heap->mstar_cma->cma_swap_wait);
	mutex_init(&mstar_cma_heap->mstar_cma->contig_rtylock);

	mstar_cma_heap->mstar_cma->cache_size = 0;
	mstar_cma_heap->mstar_cma->cache_page_count = 0;
	mstar_cma_heap->mstar_cma->fail_alloc_count = 0;
	INIT_LIST_HEAD(&mstar_cma_heap->mstar_cma->cache_head);
	spin_lock_init(&mstar_cma_heap->mstar_cma->cache_lock);

	//only descrete cma need worker thread
	if(mcma_private->flag == DESCRETE_CMA){
		//sprintf(name,"cma_%s",dev_name(mstar_cma_heap->dev));
		//mstar_cma_heap->mstar_cma->swap_worker = kthread_run(cma_swap_worker_thread, mstar_cma_heap, name);
		//CMA_BUG_ON(IS_ERR(mstar_cma_heap->mstar_cma->swap_worker),"create swap worker thread error! \n");
	}
	return &mstar_cma_heap->heap;

err:
	printk("\033[35mFunction = %s, Line = %d,error! \033[m\n", __PRETTY_FUNCTION__, __LINE__);

	kfree(mstar_cma_heap);
	return ERR_PTR(-ENOMEM);
}

void ion_mstar_cma_heap_destroy(struct ion_heap *heap)
{
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(heap);

	kfree(mstar_cma_heap->mstar_cma);
	kfree(mstar_cma_heap);
}

//helper function
static int get_cma_freebits(struct cma *cma)
{
	int total_free	= 0;
	int start	= 0;
	int pos		= 0;
	int next	= 0;

	while(1){
		pos = find_next_zero_bit(cma->bitmap, cma->count, start);

		if(pos >= cma->count)
			break;
		start = pos + 1;
		next = find_next_bit(cma->bitmap, cma->count, start);

		if(next >= cma->count){
			total_free += (cma->count - pos);
			break;
		}

		total_free += (next - pos);
		start = next + 1;

		if(start >= cma->count)
			break;
	}

	return total_free;
}

void get_system_heap_info(struct cma *system_cma, int *mali_heap_info)
{
	int freebits;

	mutex_lock(&system_cma->lock);
	freebits = get_cma_freebits(system_cma);
	mali_heap_info[0] = system_cma->count - freebits;	// alloc
	mali_heap_info[1] = 0;
	mali_heap_info[2] = 0;
	mali_heap_info[3] = freebits;						// free
	mutex_unlock(&system_cma->lock);
}

void get_cma_heap_info(struct ion_heap *heap, int *mali_heap_info, char *name)
{
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(heap);
	int freebits;

	spin_lock(&mstar_cma_heap->mstar_cma->cache_lock);

	freebits = get_cma_freebits(mstar_cma_heap->mstar_cma->cma);
	mali_heap_info[0] = mstar_cma_heap->mstar_cma->cma->count - freebits;
	mali_heap_info[1] = mstar_cma_heap->mstar_cma->cache_page_count;
	mali_heap_info[2] = mstar_cma_heap->mstar_cma->fail_alloc_count;
	mali_heap_info[3] = freebits;

	spin_unlock(&mstar_cma_heap->mstar_cma->cache_lock);
	strncpy(name, dev_name(mstar_cma_heap->dev), CMA_HEAP_NAME_LENG);
}

int in_cma_range(struct ion_heap* heap, struct page* page)
{
	struct ion_mstar_cma_heap *mstar_cma_heap = to_mstar_cma_heap(heap);
	struct mstar_cma *mstar_cma = mstar_cma_heap->mstar_cma;
	unsigned long pfn = page_to_pfn(page);

	if(pfn >= mstar_cma->cma->base_pfn && pfn < mstar_cma->cma->base_pfn + mstar_cma->cma->count){
		return 1;
	}

	return 0;
}

#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
extern struct CMA_BootArgs_Config cma_config[MAX_CMA_AREAS];
extern struct device mstar_cma_device[MAX_CMA_AREAS];
extern int mstar_driver_boot_cma_buffer_num;
struct ion_platform_heap *cma_heaps_info;
#else
int mstar_driver_boot_cma_buffer_num = 0;
#endif
#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
struct cma_measurement cma_measurement_data[ION_HEAP_ID_RESERVED];
static struct proc_dir_entry *proc_cma_measurement_root_dir;
#define MEASUREMENT_ROOT_DIR "cma_measurement"
#define MEASUREMENT_RESET_NODE "reset_cma_measurement_data"
#define MEASUREMENT_SHOW_NODE "show_cma_measurement_data"
#endif
#ifdef CONFIG_MP_CMA_PATCH_MBOOT_STR_USE_CMA
int mboot_str_heap_id = 0;
#endif
#ifdef CONFIG_MP_MMA_CMA_ENABLE
int ion_mma_cma_miu0_heap_id = 0;
int ion_mma_cma_miu1_heap_id = 0;
int ion_mma_cma_miu2_heap_id = 0;
#endif
int iommu_discrete_cma_miu0_heap_id = 0;
int iommu_discrete_cma_miu1_heap_id = 0;
int iommu_discrete_cma_miu2_heap_id = 0;
#ifdef CONFIG_PROC_FS
#define CMABITMAP_OUT_PATH "/mnt/usb/sda1/"
#define CMABITMAP_OUT_FILE "cmabitmap_"
#define CMARUNLIST_PROC_NAME	"cmarunlist_"
#define CMABITMAP_PROC_NAME	"cmabitmap_"

static int cmarunlist_proc_show(struct seq_file *m, void *v)
{
	int frst_bit = 0, last_bit = 0, this_bit = 0;
	struct cma *cma = (struct cma *)m->private;

	if(!cma)
		return 0;

	mutex_lock(&cma->lock);

	frst_bit = bitmap_find_next_zero_area(cma->bitmap, cma->count, 0, 1, 0);
	if (this_bit >= cma->count){
		goto out;
	}
	this_bit = frst_bit;
	last_bit = frst_bit;

	while(1){
		this_bit = bitmap_find_next_zero_area(cma->bitmap, cma->count, this_bit + 1, 1, 0);
		if (this_bit >= cma->count){
			printk("free pages: %d~%d, page num = %d\n", frst_bit, last_bit, last_bit - frst_bit + 1);
			break;
		}
		if(this_bit > last_bit + 1){
			printk("free pages: %d~%d, page num = %d\n", frst_bit, last_bit, last_bit - frst_bit + 1);
			frst_bit = this_bit;
		}
		last_bit = this_bit;
	}
out:
	mutex_unlock(&cma->lock);
	return 0;
}

static int cmarunlist_proc_open(struct inode *inode, struct file *file)
{
	int n = 0, ret = 0;
	unsigned long freebits = 0;
	struct ion_heap *heap = PDE_DATA(inode);
	struct ion_mstar_cma_heap *mheap = NULL;
	struct cma *cma;

	if (!heap) {
		pr_err("%s, heap not found\n", __func__);
		return 0;
	}

	mheap = to_mstar_cma_heap(heap);
	cma = mheap->mstar_cma->cma;
	if (cma) {
		freebits = get_free_bit_count(cma, 0, cma->count);
		printk("cma %s, total page %lu, free page %lu\n", heap->name, cma->count, freebits);
		ret = single_open(file, cmarunlist_proc_show, cma);
	}
	return ret;
}

static const struct file_operations cmarunlist_proc_fops = {
	.open		= cmarunlist_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int cmabitmap_proc_show(struct seq_file *m, void *v)
{
	int ret;
	loff_t pos = 0;
	struct ion_heap *heap = (struct ion_heap *)m->private;
	struct ion_mstar_cma_heap *mheap = to_mstar_cma_heap(heap);
	struct cma *cma = mheap->mstar_cma->cma;
	struct file *filp = NULL;
	char filename[64];

	memset(filename, 0, 64);
	sprintf(filename, "%s%s%s", CMABITMAP_OUT_PATH, CMABITMAP_OUT_FILE, heap->name);
	filp =  filp_open(filename, O_RDWR | O_TRUNC | O_CREAT, 0777);
	if(!filp || IS_ERR(filp)){
		printk("%s, open file %s failed, error %ld\n", __func__, filename, PTR_ERR(filp));
		ret = PTR_ERR(filp);
		goto out;
	}
	set_fs(get_ds());
	ret = vfs_write(filp, (char __user *)cma->bitmap, cma->count / 8, &pos);

out:
	mutex_unlock(&cma->lock);
	if(!PTR_ERR(filp))
		filp_close(filp, NULL);
	return ret;
}

static int cmabitmap_proc_open(struct inode *inode, struct file *file)
{
	int n = 0, ret = 0;
	unsigned long freebits = 0;
	struct ion_heap *heap = PDE_DATA(inode);
	struct ion_mstar_cma_heap *mheap = NULL;
	struct cma *cma = NULL;

	if (!heap) {
		pr_err("%s, heap not found\n", __func__);
		return ret;
	}

	mheap = to_mstar_cma_heap(heap);
	cma = mheap->mstar_cma->cma;
	if(cma) {
		freebits = get_free_bit_count(cma, 0, cma->count);
		printk("cma %s, total page %lu, free page %lu\n", heap->name, cma->count, freebits);
		return single_open(file, cmabitmap_proc_show, heap);
	}
	return ret;
}

static const struct file_operations cmabitmap_proc_fops = {
	.open		= cmabitmap_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
#if 0
static ssize_t cma_measurement_data_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	int n = 0;
	struct ion_heap *heap = NULL;
	struct ion_mstar_cma_heap *mheap = NULL;
	struct cma_measurement *show_cma_measurement = NULL;
	char *filepath = NULL;

	char *filepath_buf = (char *)kmalloc(sizeof(char) * CMA_HEAP_MEASUREMENT_LENG, GFP_KERNEL);
	if(!filepath_buf)
		BUG_ON(1);
	char *checked_filepath = (char *)kmalloc(sizeof(char) * CMA_HEAP_MEASUREMENT_LENG, GFP_KERNEL);
	if(!checked_filepath)
		BUG_ON(1);
	//const char *filename = file->f_path.dentry->d_name.name;

	filepath = d_path(&file->f_path, filepath_buf, sizeof(char) * CMA_HEAP_MEASUREMENT_LENG);
	//printk("\033[35mFunction = %s, Line = %d, filename is %s\033[m\n", __PRETTY_FUNCTION__, __LINE__, filename);
	//printk("\033[35mFunction = %s, Line = %d, filepath is %s\033[m\n", __PRETTY_FUNCTION__, __LINE__, filepath);

	/* using filepath to get the heap_info */
	for(n = 0; n < MAX_CMA_AREAS; n++)
	{
		if(heaps[n])
		{
			sprintf(checked_filepath, "/proc/%s/%s/%s", MEASUREMENT_ROOT_DIR, heaps[n]->name, MEASUREMENT_SHOW_NODE);
			//printk("\033[35mFunction = %s, Line = %d, check %s\033[m\n", __PRETTY_FUNCTION__, __LINE__, checked_filepath);

			if(strcmp(checked_filepath, filepath) == 0)
			{
				heap = heaps[n];
				//printk("\033[35mFunction = %s, Line = %d, get heap: %s\033[m\n", __PRETTY_FUNCTION__, __LINE__, heap->name);
				break;
			}
		}
	}

	if(!heap)
		BUG_ON(1);

	mheap = to_mstar_cma_heap(heap);
	show_cma_measurement = mheap->mstar_cma->cma->cma_measurement_ptr;
	//show_cma_measurement = heap->cma_measurement_ptr;
	mutex_lock(&show_cma_measurement->cma_measurement_lock);
	printk("\033[31mthis show_cma_measurement is %s\033[m\n", show_cma_measurement->cma_heap_name);
	printk("\033[31mtotal_alloc_size_kb is %lu kb, total_alloc_time_cost_ms is %lu ms\033[m\n", show_cma_measurement->total_alloc_size_kb, show_cma_measurement->total_alloc_time_cost_ms);
	printk("\033[31mtotal_migration_size_kb is %lu kb, total_migration_time_cost_ms is %lu ms\033[m\n", show_cma_measurement->total_migration_size_kb, show_cma_measurement->total_migration_time_cost_ms);
	mutex_unlock(&show_cma_measurement->cma_measurement_lock);

	kfree(filepath_buf);
	kfree(checked_filepath);
	return 0;
}
#endif

static int cma_mesurement_show(struct seq_file *m, void *v)
{
	struct cma_measurement *me = (struct cma_measurement*)m->private;

	mutex_lock(&me->cma_measurement_lock);

	seq_printf(m, "this show_cma_measurement is %s\n", me->cma_heap_name);
	seq_printf(m, "total_alloc_size_kb is %lu kb, total_alloc_time_cost_ms is %lu ms\n",
			me->total_alloc_size_kb, me->total_alloc_time_cost_ms);
	seq_printf(m, "total_migration_size_kb is %lu kb, total_migration_time_cost_ms is %lu ms\n",
			me->total_migration_size_kb, me->total_migration_time_cost_ms);

	mutex_unlock(&me->cma_measurement_lock);

	return 0;
}

static int cma_measurement_data_open(struct inode *inode, struct file *file)
{
	int n = 0, ret = 0;
	unsigned long freebits = 0;
	struct ion_heap *heap = PDE_DATA(inode);
	struct ion_mstar_cma_heap *mheap = NULL;
	struct cma_measurement *show_cma_measurement = NULL;

	if (!heap) {
		pr_err("%s, heap not found\n", __func__);
		return 0;
	}

	mheap = to_mstar_cma_heap(heap);
	show_cma_measurement = mheap->mstar_cma->cma->cma_measurement_ptr;
	if (!show_cma_measurement) {
		pr_err("%s, invalid cma mesurement ptr\n", __func__);
		return 0;
	}

	return  single_open(file, cma_mesurement_show, show_cma_measurement);
}

static const struct file_operations show_cma_measurement_data_fops = {
	.owner		= THIS_MODULE,
	.open		= cma_measurement_data_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

ssize_t reset_cma_measurement_data_write(struct file *file, const char __user *buffer, size_t count, loff_t *ppos)
{
	int n = 0;
	struct ion_heap *heap = NULL;
	struct ion_mstar_cma_heap *mheap = NULL;
	struct cma_measurement *reset_cma_measurement = NULL;
	char *filepath = NULL;

	char *filepath_buf = (char *)kmalloc(sizeof(char) * CMA_HEAP_MEASUREMENT_LENG, GFP_KERNEL);
	if(!filepath_buf)
		BUG_ON(1);
	char *checked_filepath = (char *)kmalloc(sizeof(char) * CMA_HEAP_MEASUREMENT_LENG, GFP_KERNEL);
	if(!checked_filepath)
		BUG_ON(1);
	//const char *filename = file->f_path.dentry->d_name.name;

	filepath = d_path(&file->f_path, filepath_buf, sizeof(char) * CMA_HEAP_MEASUREMENT_LENG);

	/* using filepath to get the heap_info */
	for(n = 0; n < MAX_CMA_AREAS; n++)
	{
		if(heaps[n])
		{
			sprintf(checked_filepath, "/proc/%s/%s/%s", MEASUREMENT_ROOT_DIR, heaps[n]->name, MEASUREMENT_RESET_NODE);
			//printk("\033[35mFunction = %s, Line = %d, check %s\033[m\n", __PRETTY_FUNCTION__, __LINE__, checked_filepath);

			if(strcmp(checked_filepath, filepath) == 0)
			{
				heap = heaps[n];
				//printk("\033[35mFunction = %s, Line = %d, get heap: %s\033[m\n", __PRETTY_FUNCTION__, __LINE__, heap->name);
				break;
			}
		}
	}

	if(!heap)
		BUG_ON(1);

	mheap = to_mstar_cma_heap(heap);
	reset_cma_measurement = mheap->mstar_cma->cma->cma_measurement_ptr;
	//reset_cma_measurement = heap->cma_measurement_ptr;
	mutex_lock(&reset_cma_measurement->cma_measurement_lock);
	printk("\033[31mtotal_alloc_size_kb is %lu kb, total_alloc_time_cost_ms is %lu ms\033[m\n", reset_cma_measurement->total_alloc_size_kb, reset_cma_measurement->total_alloc_time_cost_ms);
	printk("\033[31mtotal_migration_size_kb is %lu kb, total_migration_time_cost_ms is %lu ms\033[m\n", reset_cma_measurement->total_migration_size_kb, reset_cma_measurement->total_migration_time_cost_ms);
	reset_cma_measurement->total_alloc_size_kb = 0;
	reset_cma_measurement->total_alloc_time_cost_ms = 0;
	reset_cma_measurement->total_migration_size_kb = 0;
	reset_cma_measurement->total_migration_time_cost_ms = 0;
	printk("\033[31mtotal_alloc_size_kb is %lu kb, total_alloc_time_cost_ms is %lu ms\033[m\n", reset_cma_measurement->total_alloc_size_kb, reset_cma_measurement->total_alloc_time_cost_ms);
	printk("\033[31mtotal_migration_size_kb is %lu kb, total_migration_time_cost_ms is %lu ms\033[m\n", reset_cma_measurement->total_migration_size_kb, reset_cma_measurement->total_migration_time_cost_ms);
	mutex_unlock(&reset_cma_measurement->cma_measurement_lock);

	kfree(filepath_buf);
	kfree(checked_filepath);
	return count;
}

static ssize_t reset_cma_measurement_data_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
	return 0;
}

static int reset_cma_measurement_data_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations reset_cma_measurement_data_fops = {
	.owner = THIS_MODULE,
	.write = reset_cma_measurement_data_write,
	.read = reset_cma_measurement_data_read,
	.open = reset_cma_measurement_data_open,
	.llseek = seq_lseek,
};
#endif
#endif

/*
 * used by cmainfo_proc_show
 * we use kb instead of page_cnt
 */
extern struct cma *dma_contiguous_default_area;
void get_cma_status(struct seq_file *m)
{
	int i;
	struct ion_heap *heap;
	int nrheap = mstar_driver_boot_cma_buffer_num;

	int default_cma_status[4] = {0};
	get_system_heap_info(dma_contiguous_default_area, default_cma_status);
	seq_printf(m,"%s (%dkb %dkb %dkb %dkb) ", " DEFAULT_CMA_BUFFER", default_cma_status[0]*4, default_cma_status[1]*4, default_cma_status[2]*4, default_cma_status[3]*4);

	for (i = 0; i < nrheap; i++)
	{
		int cma_status[4] = {0};
		char name[64] = {0};

		heap = heaps[i];
		if(heap->type != ION_HEAP_TYPE_MSTAR_CMA && heap->type != ION_HEAP_TYPE_SYSTEM)
			continue;

		if (heap->type == ION_HEAP_TYPE_MSTAR_CMA)
		{
			get_cma_heap_info(heap, cma_status, name);
			seq_printf(m,"%s (%dkb %dkb %dkb %dkb) ", name, cma_status[0]*4, cma_status[1]*4, cma_status[2]*4, cma_status[3]*4);
		}
		else
		{
			printk(KERN_EMERG "\033[35mFunction = %s, Line = %d, [Warning] mstar cma buffer %s, whose heap type is not ION_HEAP_TYPE_MSTAR_CMA\033[m\n", __PRETTY_FUNCTION__, __LINE__, heap->name);
		}
	}

	seq_printf(m,"\n");
}

#ifdef CONFIG_PROC_FS
static int cma_proc_init(struct ion_heap* heap, int hid)
{
#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
	struct proc_dir_entry *tm_dir;
	struct ion_mstar_cma_heap *mheap = NULL;
	struct cma *cma;
#endif
	char buf[64] = {0};

#ifdef CONFIG_MP_MMA_CMA_ENABLE
	if (heap->type != ION_HEAP_TYPE_MSTAR_CMA || heap->type != ION_HEAP_TYPE_DMA) {
#else
	if (heap->type != ION_HEAP_TYPE_MSTAR_CMA) {
#endif
		pr_err("%s, %d, invalid heap type for mstar cma\n", __func__, __LINE__);
		return -1;
	}

	/* create cma runlist proc file */
	snprintf(buf, 63, "%s%s", CMARUNLIST_PROC_NAME, heap->name);
	proc_create_data(buf, 0, NULL, &cmarunlist_proc_fops, heap);

	/* create cma bimap proc file */
	snprintf(buf, 63, "%s%s", CMABITMAP_PROC_NAME, heap->name);
	proc_create_data(buf, 0, NULL, &cmabitmap_proc_fops, heap);

#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
	tm_dir = proc_mkdir(heap->name, proc_cma_measurement_root_dir);
	if (!tm_dir)
		BUG_ON(1);

	proc_create_data(MEASUREMENT_SHOW_NODE, 0444, tm_dir, &show_cma_measurement_data_fops, heap);
	proc_create(MEASUREMENT_RESET_NODE, 0644, tm_dir, &reset_cma_measurement_data_fops);

	cma_measurement_data[hid].cma_heap_name = heap->name;
	cma_measurement_data[hid].cma_heap_id = heap->mst_hid;
	mutex_init(&cma_measurement_data[hid].cma_measurement_lock);

	pr_info("%s, %d, cma_measurement_data name: %s, id: %d\n\n", __func__, __LINE__,
			cma_measurement_data[hid].cma_heap_name,
			cma_measurement_data[hid].cma_heap_id);

#ifdef CONFIG_MP_MMA_CMA_ENABLE
	if (heap->type != ION_HEAP_TYPE_DMA) {
		// assign to corresponding cma
		mheap = to_mstar_cma_heap(heap);
		cma = mheap->mstar_cma->cma;
		cma->cma_measurement_ptr = &cma_measurement_data[hid];
	} else {
		iommu_heap =container_of(heap, struct ion_cma_heap, heap);
		cma = iommu_heap->dev->cma_area;
		cma->cma_measurement_ptr = &cma_measurement_data[hid];
	}
#else
	// assign to corresponding cma
	mheap = to_mstar_cma_heap(heap);
	cma = mheap->mstar_cma->cma;
	cma->cma_measurement_ptr = &cma_measurement_data[hid];
#endif
#endif
}
#endif

static int _ion_add_mstar_cma_heaps(void)
{
	int err, i;
	int nrcma = mstar_driver_boot_cma_buffer_num;

	pr_info("%s, %d, nums of heaps = %d\n", __func__, __LINE__, nrcma);

	heaps = kzalloc(sizeof(struct ion_heap *) * nrcma, GFP_KERNEL);

#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
	/*memset(cma_measurement_data, 0, sizeof(struct cma_measurement) * ION_HEAP_ID_RESERVED);*/

	proc_cma_measurement_root_dir = proc_mkdir(MEASUREMENT_ROOT_DIR, NULL);
	if (!proc_cma_measurement_root_dir)
		BUG_ON(1);
#endif

	/* create the heaps as specified in the mstar_default_heaps[] and parsed from bootargs */
	for (i = 0; i < nrcma; i++) {
		struct ion_platform_heap *heap_data = &cma_heaps_info[i];

#ifdef CONFIG_MP_CMA_PATCH_MBOOT_STR_USE_CMA
		/*if heap_name contains "_STR_MBOOT", then the heap is mboot co-buffer*/
		if (strstr(heap_data->name, "_STR_MBOOT") != NULL) {
			pr_info("%s, %d, STR_MBOOT co-buffer heap is %d\n", __func__, __LINE__, heap_data->id);
			mboot_str_heap_id = heap_data->id;
		}
#endif

#ifdef	CONFIG_MP_MMA_CMA_ENABLE
		if (strstr(heap_data->name, "ION_MMA_CMA0") != NULL) {
			pr_info("%s, %d, ion_mma_cma_miu0_heap_id is %d\n", __func__, __LINE__, heap_data->id);
			ion_mma_cma_miu0_heap_id = heap_data->id;
		} else if (strstr(heap_data->name, "ION_MMA_CMA1") != NULL) {
			pr_info("%s, %d, ion_mma_cma_miu1_heap_id is %d\n", __func__, __LINE__, heap_data->id);
			ion_mma_cma_miu1_heap_id = heap_data->id;
		} else if (strstr(heap_data->name, "ION_MMA_CMA2") != NULL) {
			pr_info("%s, %d, ion_mma_cma_miu2_heap_id is %d\n", __func__, __LINE__, heap_data->id);
			ion_mma_cma_miu2_heap_id = heap_data->id;
		}
#endif
		if (strstr(heap_data->name, "IOMMU_DISCRETE_CMA0") != NULL) {
			printk("\033[31mFunction = %s, Line = %d, iommu_discrete_cma_miu0_heap_id is %d\033[m\n", __PRETTY_FUNCTION__, __LINE__, heap_data->id);
			iommu_discrete_cma_miu0_heap_id = heap_data->id;
		}else if (strstr(heap_data->name, "IOMMU_DISCRETE_CMA1") != NULL) {
			printk("\033[31mFunction = %s, Line = %d, iommu_discrete_cma_miu1_heap_id is %d\033[m\n", __PRETTY_FUNCTION__, __LINE__, heap_data->id);
			iommu_discrete_cma_miu1_heap_id = heap_data->id;
		}else if (strstr(heap_data->name, "IOMMU_DISCRETE_CMA2") != NULL) {
			printk("\033[31mFunction = %s, Line = %d, iommu_discrete_cma_miu2_heap_id is %d\033[m\n", __PRETTY_FUNCTION__, __LINE__, heap_data->id);
			iommu_discrete_cma_miu2_heap_id = heap_data->id;
		}
		if (heap_data->type == ION_HEAP_TYPE_MSTAR_CMA)
			heaps[i] = ion_mstar_cma_heap_create(heap_data);
#if CONFIG_MP_MMA_CMA_ENABLE
		else if (heap_data->type == ION_HEAP_TYPE_DMA)
			heaps[i] = ion_cma_heap_create(heap_data);
#endif
		else {
			pr_err("%s, %d, err: type %d not support\n", __func__, __LINE__, heap_data->type);
			continue;
		}

		heaps[i]->name = heap_data->name;
		heaps[i]->mst_hid = heap_data->id;

		ion_device_add_heap(heaps[i]);
#ifdef CONFIG_PROC_FS
		cma_proc_init(heaps[i], heap_data->id);
#endif

	}

	return 0;
err:
	pr_err("%s, %d, mstar ion cma add heap fail!!\n", __func__, __LINE__);
	kfree(heaps);
	return err;
}

#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
static int ion_mstar_cma_setup(void)
{
	struct CMA_BootArgs_Config *heapconfig = NULL;
	struct mstar_cma_heap_private *mcma_private = NULL;
	int nrcma = mstar_driver_boot_cma_buffer_num;
	int i, j;

	if (nrcma == 0) {
		pr_alert("%s, no cma\n", __func__);
		return 0;
	}

	pr_info("\n%s, %d, nrcma =  %d\n", __func__, __LINE__, nrcma);

	cma_heaps_info = kzalloc(sizeof(struct ion_platform_heap) * nrcma, GFP_KERNEL);
	if (!cma_heaps_info) {
		pr_err("%s, no mem for cma_heaps_info\n", __func__);
		return -ENOMEM;
	}

	for (i = 0; i < nrcma; i++) {
		BUG_ON(cma_config[i].size == 0);
		BUG_ON(cma_config[i].pool_id < ION_CMA_HEAP_ID_START);
		BUG_ON(cma_config[i].pool_id >= ION_HEAP_ID_RESERVED);

		cma_heaps_info[i].id = cma_config[i].pool_id;
#ifdef CONFIG_MP_MMA_CMA_ENABLE
		if (strstr(cma_config[i].name, "ION_MMA_CMA"))
			cma_heaps_info[i].type= ION_HEAP_TYPE_DMA;
		else
			cma_heaps_info[i].type= ION_HEAP_TYPE_MSTAR_CMA;
#else
		cma_heaps_info[i].type= ION_HEAP_TYPE_MSTAR_CMA;
#endif
		cma_heaps_info[i].name = cma_config[i].name;

		mcma_private = kzalloc(sizeof(struct mstar_cma_heap_private), GFP_KERNEL);
		if (!mcma_private) {
			pr_err("%s, no mem for mcma private\n", __func__);
			return -ENOMEM;
		}

		mcma_private->cma_dev = &mstar_cma_device[i];

		//FIXME: judge by pool_id ?
		if ((cma_config[i].pool_id >= ION_MALI_MIU0_HEAP_ID)
				&& (cma_config[i].pool_id <= ION_MALI_MIU2_HEAP_ID))
			mcma_private->flag = DESCRETE_CMA;
		else  //VDEC XC DIP heap id
			mcma_private->flag = CONTINUOUS_ONLY_CMA;

		cma_heaps_info[i].priv = mcma_private;
	}

	return 0;
}
#endif

static int ion_add_mstar_cma_heaps(void)
{
#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
	ion_mstar_cma_setup();
	_ion_add_mstar_cma_heaps();
#endif
	return 0;
}
device_initcall(ion_add_mstar_cma_heaps);
