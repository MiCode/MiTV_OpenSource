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

#include <linux/export.h>
#include "ion_mstar_iommu_cma.h"
#ifdef CONFIG_MP_MMA_CMA_ENABLE
 struct heap_info{
    unsigned long base_phy;
    unsigned long size;
};

struct heap_info g_heap_info[4]={{0,0},{0,0},{0,0},{0,0}};

int ion_cma_heap_create_not_destroy_num = 0;

#define ION_CMA_BUG_ON(cond)  \
do { \
      if(cond) {\
        printk(KERN_ERR "ION_CMA_BUG_ON in %s @ %d \n", __FUNCTION__, __LINE__); \
      }\
      BUG_ON(cond); \
   } while(0)

bool delay_free_init_done = false;
static DEFINE_MUTEX(delay_free_lock);
static int delay_free_size_limite = 52428800; //50M
static int delay_free_time_limite = 3000*HZ/1000;//here default value 3000 means 3000ms,that is 3s.
extern int lowmem_minfree[6];
extern int lowmem_minfree_size;
static int delay_free_lowmem_minfree = 17 * 1024;
static uint32_t delay_free_last_force_free_jiffies = 0;
static uint32_t delay_free_evict_duration = 0;
LIST_HEAD(ion_cma_delay_list);

//FIXME
extern void miuprotect_dumpKRange(unsigned int miu);
extern int miuprotect_deleteKRange(unsigned long buffer_start_pa, unsigned long buffer_length);
extern int miuprotect_addKRange(unsigned long buffer_start_pa, unsigned long buffer_length);

enum cma_heap_flag{
    DESCRETE_CMA,
    CONTINUOUS_ONLY_CMA
};

struct mstar_cma_heap_private {
    struct device *cma_dev;
    enum cma_heap_flag flag;    //flag for cma type
};

/*struct mstar_cma{
    wait_queue_head_t cma_swap_wait;//page swap worker wait queue
    struct cma*     cma;
    struct mutex contig_rtylock;

    struct zone *zone;

    unsigned long fail_alloc_count;
};*/
int mma_get_heap_info(unsigned int miu,unsigned long *phy,unsigned long *size)
{
    if(phy == NULL || size == NULL || miu >4)
        return -1;

    *phy = g_heap_info[miu].base_phy;
    *size = g_heap_info[miu].size;
    return 0;
}

static int ion_mstar_iommu_cma_get_sgtable(struct device *dev, struct sg_table *sgt,
                             struct page *page, dma_addr_t handle, size_t size)
{
    int ret;

    ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
    if (unlikely(ret))
        return ret;

    sg_set_page(sgt->sgl, page, PAGE_ALIGN(size), 0);
    return 0;
}

static int buffer_node_info_get_sgtable(struct device *dev,struct ion_cma_buffer *buffer_node)
{
    buffer_node->info = kzalloc(sizeof(struct ion_cma_buffer_info), GFP_KERNEL);
    if (!buffer_node->info) {
        printk( "Can't allocate buffer info\n");
        return ION_CMA_ALLOCATE_FAILED;
    }
    buffer_node->info->page = buffer_node->page;
    buffer_node->info->table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
    if (!buffer_node->info->table) {
        printk("Fail to allocate sg table\n");
        kfree(buffer_node->info);
        return ION_CMA_ALLOCATE_FAILED;
    }
    buffer_node->info->handle = buffer_node->start_pa;
    if (ion_mstar_iommu_cma_get_sgtable(dev, buffer_node->info->table,
        buffer_node->info->page, buffer_node->info->handle, buffer_node->length))
    {
        kfree(buffer_node->info->table);
        kfree(buffer_node->info);
        return ION_CMA_ALLOCATE_FAILED;
    }
    return 0;
}

static void buffer_node_info_free_sgtable(struct ion_cma_buffer *buffer_node)
{
    sg_free_table(buffer_node->info->table);
    kfree(buffer_node->info->table);
    kfree(buffer_node->info);
}

static struct ion_cma_buffer * IONCMA_SplitBuffer( struct device *dev,
        unsigned long length, struct ion_cma_buffer *find,
        struct ion_cma_allocation_list * alloc_list, BUFF_OPS ops,bool is_secure)
{
    struct ion_cma_buffer *buffer_node = NULL;
    int ret = 0;

    ION_CMA_BUG_ON(!find || (length >  find->length));

    if(ops == CMA_ALLOC)
        ION_CMA_BUG_ON(find->freed == 0);
    else
        ION_CMA_BUG_ON(find->freed == 1);

    //at begginning of found buffer for un-secure
    if(length == find->length)
    {
           /* bufffer_node(start_pa, length)  =   find
            *  |------------------------------------|
            */
            buffer_node = find;
            if(ops == CMA_ALLOC)
            {
                buffer_node->freed = 0;
                alloc_list->freed_count--;
                alloc_list->using_count++;

               ret = buffer_node_info_get_sgtable(dev,buffer_node);
               if(ret)
                    return NULL;
            }
            else //cma free
            {
                alloc_list->freed_count++;
                alloc_list->using_count--;
                buffer_node->freed = 1;
                buffer_node_info_free_sgtable(buffer_node);
            }
    }
    else
    {
           /* freed node split
            *bufffer_node(start_pa, length)  find
            *  |----------------------------|--------|
            */
          //  printk("\033[1;32;40m %s,%d, split node \033[0m\n",__FUNCTION__,__LINE__);
            buffer_node = (struct ion_cma_buffer *)kzalloc(sizeof(struct ion_cma_buffer), GFP_KERNEL);

            ION_CMA_BUG_ON(!buffer_node);
            buffer_node->start_pa = find->start_pa; // buffer_noda specify the freed buffer
            buffer_node->length = length;
            buffer_node->page = find->page;

            INIT_LIST_HEAD(&buffer_node->list);

            find->start_pa = buffer_node->start_pa + buffer_node->length;   // adjust the freed buffer
            find->length -= buffer_node->length;
            find->page = buffer_node->page + (buffer_node->length >> PAGE_SHIFT);

            list_add(&buffer_node->list, find->list.prev); //insert new node before find node (list_add(source, to))

            if(ops == CMA_ALLOC)
            {
                buffer_node->freed = 0;
                alloc_list->using_count++;


                ret = buffer_node_info_get_sgtable(dev,buffer_node);
                if(ret)
                    return NULL;
            }
            else
            {
                printk("\033[1;32;40m %s,%d:should not be here!!!\033[0m\n",
                        __FUNCTION__,__LINE__);
            }
    }

    return buffer_node;
}

static struct ion_cma_buffer * _ion_alloc_from_freelist(struct device *dev,
                                    struct ion_cma_allocation_list *alloc_list,
                                    unsigned long length,bool is_secure)
{
    struct ion_cma_buffer *buffer_node, *find = NULL;


    if(alloc_list->freed_count <= 0)
        return NULL;

    //find the start address in free list
    list_for_each_entry(buffer_node, &alloc_list->list_head, list)
    {
        //not freed buffer
        if(buffer_node->freed == 0)
            continue;

        // check if this allocation_range is located at this freed buffer
        if( length <= buffer_node->length)
        {
            //if(buffer_node->cpu_addr != NULL)
            if(buffer_node->page != NULL)
            {

                find = buffer_node;
                break;
            }
        }
    }

    if(!find)
    {
        buffer_node = NULL;
    }
    else
    {
        buffer_node = IONCMA_SplitBuffer(dev,length, find, alloc_list, CMA_ALLOC,is_secure);    // split the free buffer to "ready_to_be_allocated buffer" and "free_buffer"
    }

    return buffer_node;
}

static int check_heap_alloc_list(struct ion_cma_heap *cma_heap)
{
    struct ion_cma_allocation_list buffer_list_secure =cma_heap->alloc_list_secure;
    struct ion_cma_allocation_list buffer_list_unsecure =cma_heap->alloc_list_unsecure;

    if(list_empty(&buffer_list_secure.list_head)
        || list_empty(&buffer_list_unsecure.list_head))
        return 0;//no need check

   if( (PHYSICAL_START_INIT == buffer_list_secure.min_start )
        || (PHYSICAL_END_INIT == buffer_list_secure.max_end )
        ||          (PHYSICAL_START_INIT == buffer_list_unsecure.min_start )
        || (PHYSICAL_END_INIT == buffer_list_unsecure.max_end )
    )
        return 0;//no need check

    if(buffer_list_unsecure.max_end >= buffer_list_secure.min_start)
        {
            printk("In same heap,address of unsecure buffer should be lower than that of secure buffer"
                "  buffer_list_unsecure[%lx, %lx],buffer_list_secure[%lx, %lx]"
                ,buffer_list_unsecure.min_start,buffer_list_unsecure.max_end
                ,buffer_list_secure.min_start,buffer_list_secure.max_end);
            return -1;
        }

    return 0;
}

static void ion_cma_clear_pages(struct page *page, unsigned long len)
{
    int pfn = page_to_pfn(page);

    BUG_ON((len % PAGE_SIZE) != 0);
    while(len > 0){
        page = pfn_to_page(pfn);
        clear_highpage(page);
        pfn++;
        len -= PAGE_SIZE;
    }
}

static int ion_mstar_iommu_cma_alloc_contiguous(struct ion_heap *heap,
                                              struct ion_buffer *buffer,
                                              unsigned long len)
{
    struct ion_cma_buffer_info* buffer_info;
    struct ion_cma_heap *ion_cma_heap = to_cma_heap(heap);
    struct device *dev = ion_cma_heap->dev;
    //struct ion_cma *ion_cma = ion_cma_heap->ion_cma;

    buffer_info = kzalloc(sizeof(struct ion_cma_buffer_info), GFP_KERNEL);
    if (!buffer_info) {
        printk("Can't allocate buffer info\n");
        return -1;
    }

    if (buffer->flags &ION_FLAG_CMA_ALLOC_SECURE)
    {
        buffer_info->page = dma_alloc_at_from_contiguous_from_high_to_low(dev,len>>PAGE_SHIFT,8,0);
    }
    else
    {
        buffer_info->page = dma_alloc_at_from_contiguous(dev,len>>PAGE_SHIFT,get_order(len),0);
    }
    buffer_info->size = len;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,31)

#else
    __dma_clear_buffer2(buffer_info->page, (len>>PAGE_SHIFT) << PAGE_SHIFT);
#endif

    if (!buffer_info->page) {
        printk("Fail to allocate buffer1\n");
        goto err;
    }

#if defined(CONFIG_ARM)
    buffer_info->handle = __pfn_to_phys(dev, page_to_pfn(buffer_info->page));                  // page to pfn to bus_addr
#else
    buffer_info->handle = (dma_addr_t)((page_to_pfn(buffer_info->page)) << PAGE_SHIFT);     // page to pfn to bus_addr
#endif

    buffer_info->table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
    if (!buffer_info->table) {
        printk("Fail to allocate sg table\n");
        goto free_mem;
    }

    if (ion_mstar_iommu_cma_get_sgtable(dev, buffer_info->table,
                                   buffer_info->page, buffer_info->handle, len))
    {
        dma_release_from_contiguous(dev, buffer_info->page, len >> PAGE_SHIFT);
        goto err1;
    }

    ion_cma_clear_pages(buffer_info->page, len);
    //ion_pages_sync_for_device(NULL, buffer_info->page, len, DMA_BIDIRECTIONAL);
    buffer_info->flag |= ION_FLAG_CONTIGUOUS;
    buffer->priv_virt = buffer_info;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,32)
    buffer->sg_table = buffer_info->table;
#endif
    dev_dbg(dev, "Allocate buffer %p\n", buffer);

    return 0;

err1:
    kfree(buffer_info->table);
free_mem:
    dma_release_from_contiguous(dev, buffer_info->page, len >> PAGE_SHIFT);
err:
    kfree(buffer_info);
    return -1;
}

static void list_sort_add(struct list_head *new_node,struct list_head *head)
{
    struct ion_cma_buffer *buffer_head = NULL,*buffer_new = NULL;
    struct ion_cma_buffer *buffer_pos = NULL,*buffer_next=NULL;
    struct list_head *pos = NULL,*next = NULL;

    if(list_empty (head))
    {
       // printk("%s,%d\n",__FUNCTION__,__LINE__);
        list_add_tail(new_node,head);
        return;
    }

    buffer_head = list_entry(head->next,struct ion_cma_buffer,list);
    buffer_new = list_entry(new_node,struct ion_cma_buffer,list);
    if(buffer_new->start_pa < buffer_head->start_pa)
    {
        list_add(new_node,head);
        return;
    }

    list_for_each(pos,head)
    {
        buffer_pos = list_entry(pos,struct ion_cma_buffer,list);
        if(buffer_pos->start_pa >=  buffer_new->start_pa+buffer_new->length)
        {
            list_add(new_node,pos->prev);
            return;
        }
    }

    list_add_tail(new_node,head);
}
struct ion_cma_heap_info* split_cache_buffer(struct ion_cma_heap *cma_heap,
                        cma_cache_buffer_range_node *range,unsigned long len)
{
	struct ion_cma_buffer_info *ret = NULL;
	struct device *dev = cma_heap->dev;

	ret = kzalloc(sizeof(struct ion_cma_buffer_info),GFP_KERNEL);
        if(!ret)
		return ret;
	ret->size = len;

	ret->page = range->range_info->page;
	ret->table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
	if (!ret->table) {
		printk("Fail to allocate sg table\n");

		kfree(ret);
		return NULL;
	}
	ret->handle = range->range_info->handle;

	if (ion_mstar_iommu_cma_get_sgtable(dev, ret->table,
		ret->page, ret->handle, len))
	{
		kfree(ret->table);
		kfree(ret);
		return NULL;
	}

	range->range_len -= len;
	range->range_info->page = range->range_info->page + (len >> PAGE_SHIFT);
#if defined(CONFIG_ARM)
	range->range_info->handle = __pfn_to_phys(dev, page_to_pfn(range->range_info->page));                  // page to pfn to bus_addr
#else
	range->range_info->handle = (dma_addr_t)((page_to_pfn(range->range_info->page)) << PAGE_SHIFT);     // page to pfn to bus_addr
#endif
	range->range_info->size -= len;

	sg_free_table(range->range_info->table);
	if (ion_mstar_iommu_cma_get_sgtable(dev, range->range_info->table,
		range->range_info->page, range->range_info->handle, len))
	{
		kfree(ret->table);
		kfree(ret);
		return NULL;
	}
    //printk("new handle %lx, size %lx range %lx ,szie %lx\n",ret->handle,ret->size,
    //              range->range_info->handle,range->range_info->size);
	return ret;
}
int merge_cache_buffer(struct device *dev,cma_cache_buffer_range_node *range_des,
                      struct ion_cma_buffer_info *info)
{
	if(range_des->range_info->handle +range_des->range_info->size
            == info->handle)
	{
		range_des->range_info->size += info->size;
	}
	else if(info->handle + info->size == range_des->range_info->handle)
	{
		range_des->range_info->handle = info->handle;
		range_des->range_info->page = info->page;
		range_des->range_info->size += info->size;
	}
	else
	{
		return -1;
	}
	range_des->range_len += info->size;
	sg_free_table(range_des->range_info->table);
	ion_mstar_iommu_cma_get_sgtable(dev, range_des->range_info->table,
		range_des->range_info->page, range_des->range_info->handle, range_des->range_len);
	sg_free_table(info->table);
	kfree(info->table);
	kfree(info);
	return 0;
}
static int get_buffer_from_cma_cache(struct ion_cma_heap *cma_heap,
                                    struct ion_buffer *buffer,unsigned long len)
{

    cma_cache_buffer_range_node  *cache_buffer_range;
    if(buffer->flags & ION_FLAG_CMA_ALLOC_SECURE)//for secure
    {
        //try to alloc from secure cache
        mutex_lock(&cma_heap->secure_cache.lock);
        if(true == cma_heap->secure_cache.cache_set)
        {
            list_for_each_entry(cache_buffer_range, &cma_heap->secure_cache.list_head, list_node)
            {

                if(len == cache_buffer_range->range_len)
                {
                    buffer->priv_virt = cache_buffer_range->range_info;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,32)
                   buffer->sg_table = cache_buffer_range->range_info->table;
#endif
                    list_del(&cache_buffer_range->list_node);
                    kfree(cache_buffer_range);
                    mutex_unlock(&cma_heap->secure_cache.lock);
                    return 0;
                }
                else if(len < cache_buffer_range->range_len)
                {
                    buffer->priv_virt = split_cache_buffer(cma_heap,
                                                          cache_buffer_range,len);
                    if(!buffer->priv_virt)
                        return -1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,32)
                   buffer->sg_table = cache_buffer_range->range_info->table;
#endif
			mutex_unlock(&cma_heap->secure_cache.lock);
			return 0;
                }
            }
        }
        else
        {
            //if cache_set is false,means nothing about cma cache , and will do nothing,
        }
        mutex_unlock(&cma_heap->secure_cache.lock);
    }
    else //for unsecure
    {
        //try to alloc from secure cache
        mutex_lock(&cma_heap->unsecure_cache.lock);
        if(true == cma_heap->unsecure_cache.cache_set)
        {
            list_for_each_entry(cache_buffer_range, &cma_heap->unsecure_cache.list_head, list_node)
            {

                if(len == cache_buffer_range->range_len)
                {
                    buffer->priv_virt = cache_buffer_range->range_info;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,32)
                   buffer->sg_table = cache_buffer_range->range_info->table;
#endif
                    list_del(&cache_buffer_range->list_node);
                    kfree(cache_buffer_range);
                    mutex_unlock(&cma_heap->unsecure_cache.lock);
                    return 0;
                }
		if(len < cache_buffer_range->range_len)
		{
		 buffer->priv_virt = split_cache_buffer(cma_heap,
                                                          cache_buffer_range,len);
                    if(!buffer->priv_virt)
                        return -1;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,32)
                   buffer->sg_table = cache_buffer_range->range_info->table;
#endif
			mutex_unlock(&cma_heap->unsecure_cache.lock);
			return 0;

		}
            }
        }
        else
        {
            //if cache_set is false,means nothing about cma cache , and will do nothing,
        }
        mutex_unlock(&cma_heap->unsecure_cache.lock);
    }

    return -1;
}
static int get_buffer_from_delayfree(struct ion_heap *heap,
                                    struct ion_buffer *buffer,unsigned long len)
{
    struct delay_free_reserved *tmp;
    struct list_head *pos, *q;

    mutex_lock(&delay_free_lock);
    list_for_each_safe(pos, q, &ion_cma_delay_list)
    {
        tmp = list_entry(pos, struct delay_free_reserved, list);

        if((tmp->delay_free_time_out != 0)
            && (tmp->buffer->heap == heap)//same heap
            &&(tmp->delay_free_length == len)
            &&((tmp->flags &ION_FLAG_CMA_ALLOC_SECURE) == (buffer->flags &ION_FLAG_CMA_ALLOC_SECURE)))//secure flag should be equal
        {

            buffer->priv_virt = tmp->buffer->priv_virt;
            list_del(pos);
            kfree(tmp);
            mutex_unlock(&delay_free_lock);
            return 0;
        }
    }

    mutex_unlock(&delay_free_lock);
    return -1;
}
struct ion_cma_buffer_info* dump_ion_cma_info(struct device *dev,struct ion_cma_buffer_info *info)
{
    struct ion_cma_buffer_info *ret_info;
    ret_info = kzalloc(sizeof(struct ion_cma_buffer_info),GFP_KERNEL);
    if(!ret_info)
        return ret_info;

    ret_info->size = info->size;
    ret_info->page = info->page;
    ret_info->table = kmalloc(sizeof(struct sg_table), GFP_KERNEL);
    if (!ret_info->table) {
        printk("Fail to allocate sg table\n");
        kfree(ret_info);
        return NULL;
    }
    ret_info->handle = info->handle;

    if (ion_mstar_iommu_cma_get_sgtable(dev, ret_info->table,
        ret_info->page, ret_info->handle, ret_info->size))
    {
        kfree(ret_info->table);
        kfree(ret_info);
        return NULL;
    }

   return ret_info;
}
static void store_cma_buffer_cache(struct ion_cma_heap *cma_heap,
                                  struct ion_buffer *buffer,unsigned long len,
                                  struct ion_cma_buffer_info *info)
{

    cma_cache_buffer_range_node  *cache_buffer_range;

    if(buffer->flags & ION_FLAG_CMA_ALLOC_SECURE)
        {
               mutex_lock(&cma_heap->secure_cache.lock);    //this line should outside the below line about cache_set
               if((false == cma_heap->secure_cache.cache_set))
               {
                  cma_heap->secure_cache.ranges_info.handle = info->handle;
                  cma_heap->secure_cache.ranges_total_len = len;
                  cma_heap->secure_cache.cache_set = true;

                  cache_buffer_range = (cma_cache_buffer_range_node  *)kzalloc(sizeof(cma_cache_buffer_range_node),GFP_KERNEL);
                  ION_CMA_BUG_ON(!cache_buffer_range);
                  cache_buffer_range->range_info = dump_ion_cma_info(cma_heap->dev,info);
                  ION_CMA_BUG_ON(!cache_buffer_range->range_info);
                  cache_buffer_range->range_len = len;
                  INIT_LIST_HEAD(&cache_buffer_range->list_node);

                  list_add(&cache_buffer_range->list_node, &cma_heap->secure_cache.list_head);
                  mutex_unlock(&cma_heap->secure_cache.lock);
                  return ;
               }
             else
             {
                  printk("for each cma heap,can only set secure cma cache once");
                  ION_CMA_BUG_ON(1);
             }

             mutex_unlock(&cma_heap->secure_cache.lock);
        }
        else//unsecure
        {
            mutex_lock(&cma_heap->unsecure_cache.lock);
            if(false == cma_heap->unsecure_cache.cache_set)
            {
                cma_heap->unsecure_cache.ranges_info.handle = info->handle;
                cma_heap->unsecure_cache.ranges_total_len = len;
                cma_heap->unsecure_cache.cache_set = true;

                cache_buffer_range = (cma_cache_buffer_range_node *)kzalloc(sizeof(cma_cache_buffer_range_node),GFP_KERNEL);
                ION_CMA_BUG_ON(!cache_buffer_range);
                cache_buffer_range->range_info = dump_ion_cma_info(cma_heap->dev,info);
                ION_CMA_BUG_ON(!cache_buffer_range->range_info);
                cache_buffer_range->range_len = len;

                INIT_LIST_HEAD(&cache_buffer_range->list_node);
                list_add(&cache_buffer_range->list_node, &cma_heap->unsecure_cache.list_head);
                mutex_unlock(&cma_heap->unsecure_cache.lock);
                return ;
             }
             else
             {
                printk("for each cma heap,can only set unsecure cma cache once");
                ION_CMA_BUG_ON(1);
             }
             mutex_unlock(&cma_heap->unsecure_cache.lock);

           }

}

/* ION CMA heap operations functions */
static int ion_cma_allocate(struct ion_heap *heap, struct ion_buffer *buffer,
                unsigned long len, unsigned long align,
                unsigned long flags)
{
    struct ion_cma_heap *cma_heap = to_cma_heap(heap);
    struct device *dev = cma_heap->dev;
    struct ion_cma_buffer_info *info;
    dma_addr_t need_miu_protect_start_pa;
    int ret=0;
    struct ion_cma_buffer *ion_cma_buffer = NULL;
    struct ion_cma_allocation_list *alloc_list = NULL;
    bool miu_protect_change = false;

    unsigned long length_buffer_hole = 0;
    unsigned long need_miu_protect_length = len;

    printk("%s:%d len=%lx \n",__FUNCTION__,__LINE__,len);

    if(buffer->flags &ION_FLAG_CMA_ALLOC_SECURE)
    {
        len = ALIGN(len,(1<<20)); //secure 1M
    }
    else
    {
#ifdef CONFIG_MSTAR_MIUSLITS
        len = ALIGN(len,(1<<20));
#else
        len = ALIGN(len,(1<<13));
#endif
    }

    dev_dbg(dev, "Request buffer allocation len %ld\n", len);

    if (buffer->flags & ION_FLAG_CACHED)
        return -EINVAL;

    if (align > PAGE_SIZE)
        return -EINVAL;

    if(!get_buffer_from_cma_cache(cma_heap,buffer,len))
        return 0;

    if(!get_buffer_from_delayfree(heap,buffer,len))
        return 0;

    mutex_lock(&cma_heap->lock);
    if((buffer->flags &ION_FLAG_CMA_ALLOC_SECURE)
        &&(cma_heap->alloc_list_secure.freed_count > 0))
    {
        ion_cma_buffer = _ion_alloc_from_freelist(dev,&cma_heap->alloc_list_secure,len,true);
    }
    else if( (!(buffer->flags &ION_FLAG_CMA_ALLOC_SECURE))
        &&(cma_heap->alloc_list_unsecure.freed_count > 0))
    {
         ion_cma_buffer = _ion_alloc_from_freelist(dev,&cma_heap->alloc_list_unsecure,len,false);
    }

    if(ion_cma_buffer)
    {
        buffer->priv_virt = ion_cma_buffer->info;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,32)
	buffer->sg_table = ion_cma_buffer->info->table;
#endif
        mutex_unlock(&cma_heap->lock);
        return 0;
    }

    mutex_unlock(&cma_heap->lock);

    ret = ion_mstar_iommu_cma_alloc_contiguous(heap, buffer, len);
    buffer->size = len;
    if(!ret)
    {
         info = buffer->priv_virt;
    }
    else
    {
        printk("ion_mstar_iommu_cma_alloc_contiguous fail ,len=%lu\n",len);
        return ION_CMA_ALLOCATE_FAILED;
    }

    need_miu_protect_length = len;
    need_miu_protect_start_pa = info->handle;
    if(buffer->flags & ION_FLAG_CMA_ALLOC_SECURE){
        alloc_list  = &(cma_heap->alloc_list_secure);
    }
    else
    {
       alloc_list = &(cma_heap->alloc_list_unsecure);
    }

    if(alloc_list->min_start != PHYSICAL_START_INIT && alloc_list->max_end !=PHYSICAL_END_INIT)
    {
            /*
                      *   new buffer       hole   min_start                                   max_end
                      * |xxxxxxxxx|---------|======================|
       */
       if ((need_miu_protect_start_pa+need_miu_protect_length) <= alloc_list->min_start)    // font hole
       {

           need_miu_protect_length = alloc_list->min_start - need_miu_protect_start_pa;    // adjust allocation length to cover the hole
       }
       else if(need_miu_protect_start_pa >= alloc_list->max_end)
       {
       /*
                 * min_start                                     max_end    hole        new buffer
                 * |======================|---------------|xxxxxxxxxxxxxx|
       */

           length_buffer_hole = need_miu_protect_start_pa - alloc_list->max_end; // hole_length
           need_miu_protect_length += length_buffer_hole;                        // adjust length to cover this hole
           need_miu_protect_start_pa = alloc_list->max_end;                      // adjust alloc_start_addr to hole_start_addr

       }
    }

    ion_cma_buffer = (struct ion_cma_buffer *)kzalloc(sizeof(struct ion_cma_buffer), GFP_KERNEL);
    ION_CMA_BUG_ON(!ion_cma_buffer);
    ion_cma_buffer->start_pa = info->handle;
    ion_cma_buffer->length = len;

    ion_cma_buffer->page = info->page;

    ion_cma_buffer->info = info;
    INIT_LIST_HEAD(&ion_cma_buffer->list);
    ion_cma_buffer->freed = 0;

    list_sort_add(&ion_cma_buffer->list, &alloc_list->list_head);

    alloc_list->using_count++;

// here to update max_end and min_start addr
    if(need_miu_protect_start_pa < alloc_list->min_start)
    {
        alloc_list->min_start = need_miu_protect_start_pa;
        miu_protect_change = true;
    }
    if((need_miu_protect_start_pa+need_miu_protect_length) > alloc_list->max_end)
    {
        alloc_list->max_end = need_miu_protect_start_pa+need_miu_protect_length;
        miu_protect_change = true;
    }
    //after update max_end and min_start addr,must check alloc list
    check_heap_alloc_list(cma_heap);

    if(miu_protect_change)
    {
        ret = miuprotect_deleteKRange(need_miu_protect_start_pa, need_miu_protect_length);
        if(ret)
        {
            printk("update kernal range fail\n");
            kfree(ion_cma_buffer);
            goto free_info;
        }
    }
    if(buffer->flags & ION_FLAG_CMA_ALLOC_RESERVE)
    {
        store_cma_buffer_cache(cma_heap,buffer,len,info);
    }

    return 0;
free_info:

    kfree(info->table);

    dma_release_from_contiguous(dev, info->page, len >> PAGE_SHIFT);
    kfree(info);

    return ION_CMA_ALLOCATE_FAILED;
}

static bool is_in_cma_force_free_period(void)
{
      if(jiffies-delay_free_last_force_free_jiffies<delay_free_evict_duration)
        return true;

      return false;
}

struct ion_cma_buffer *find_ion_cma_buffer(struct ion_cma_heap *cma_heap,unsigned long len,struct page* page/*void *cpu_addr*/,dma_addr_t handle,bool is_secure)
{
    struct ion_cma_allocation_list  alloc_list;

    struct ion_cma_buffer * buffer = NULL, *find = NULL;

    mutex_lock(&cma_heap->lock);
    if(is_secure)
    {
         alloc_list=cma_heap->alloc_list_secure;
    }
    else
    {
        alloc_list =cma_heap->alloc_list_unsecure;
    }

    if(list_empty(&alloc_list.list_head))
         goto FIND_ION_CMA_BUFF_DONE;

    list_for_each_entry(buffer, &alloc_list.list_head, list)
    {
        if((handle >= buffer->start_pa) && ((handle + len) <= (buffer->start_pa + buffer->length)))
        {
            find = buffer;
            break;
        }
    }

FIND_ION_CMA_BUFF_DONE:
    mutex_unlock(&cma_heap->lock);
    return find;
}

struct miu_range{
    unsigned long front_add;
    unsigned long front_length;
    unsigned long back_add;
    unsigned long back_length;
};

//before call this API, lock heap info mutex firstly
static void free_buffer_list(struct ion_cma_heap *cma_heap , struct miu_range *range,bool is_secure,struct list_head *release_list)
{
    struct ion_cma_allocation_list *buffer_list ;
    struct ion_cma_buffer *buffer = NULL, *next = NULL;
    struct ion_cma_buffer *front = NULL, *back = NULL,*tmp = NULL;
    bool min_max_change = false;

    struct list_head front_list,back_list,*pos = NULL,*n = NULL;
    INIT_LIST_HEAD(&front_list);
    INIT_LIST_HEAD(&back_list);
    if(is_secure)
    {
        buffer_list = &cma_heap->alloc_list_secure;
    }
    else
    {
        buffer_list = &cma_heap->alloc_list_unsecure;
    }


    if(list_empty(&buffer_list->list_head))
        return;

    list_for_each_entry_safe(buffer, next, &buffer_list->list_head, list)   // search front buffer
    {
        if(buffer->freed !=1)
            break;

        list_del(&buffer->list);
        list_add_tail(&buffer->list,&front_list);
        buffer_list->freed_count--;
    }

    list_for_each_prev_safe(pos, n, &buffer_list->list_head)    // search back buffer
    {
        buffer=list_entry(pos,struct ion_cma_buffer,list);
        if(buffer->freed !=1)
            break;

         list_del(&buffer->list);
         list_add_tail(&buffer->list,&back_list);
         buffer_list->freed_count--;
    }

   if(!list_empty(&front_list))
   {
      front= list_entry(front_list.next,struct ion_cma_buffer,list);
      back= list_entry(front_list.prev,struct ion_cma_buffer,list);
      range->front_add = front->start_pa;
      if(list_empty(&buffer_list->list_head))
          range->front_length = back->start_pa + back->length - front->start_pa;
      else{
          tmp = list_entry(buffer_list->list_head.next,struct ion_cma_buffer,list);
          range->front_length = tmp->start_pa - front->start_pa;
      }
      list_splice(&front_list,release_list);
   }

   if(!list_empty(&back_list))
   {
      front= list_entry(back_list.prev,struct ion_cma_buffer,list);
      back= list_entry(back_list.next,struct ion_cma_buffer,list);
      if(list_empty(&buffer_list->list_head))
          printk("%s,%d: err \n",__FUNCTION__,__LINE__);
      else{

          tmp = list_entry(buffer_list->list_head.prev,struct ion_cma_buffer,list);
          range->back_add = tmp->start_pa+ tmp->length;
          range->back_length = back->start_pa + back->length - range->back_add;
      }
      list_splice(&back_list,release_list);
   }

    if(!list_empty(release_list))
    {
        buffer = list_entry(buffer_list->list_head.next, struct ion_cma_buffer, list);
        buffer_list->min_start = buffer->start_pa;
        buffer = list_entry(buffer_list->list_head.prev, struct ion_cma_buffer, list);
        buffer_list->max_end = buffer->start_pa + buffer->length;

//after update max_end and min_start addr,must check alloc list
        check_heap_alloc_list(cma_heap);
    }

    if(list_empty(&buffer_list->list_head))
    {
        buffer_list->min_start = PHYSICAL_START_INIT;
        buffer_list->max_end = PHYSICAL_END_INIT;
        buffer_list->using_count = 0;
        buffer_list->freed_count = 0;
        printk("%s,%d free all buffer\n",__FUNCTION__,__LINE__);
    }
}

//before call this API, lock heap info mutex firstly
static void ion_cma_free_buffer_list_and_release_special_buf_and_update_kernel_protect(struct device *dev ,struct ion_buffer *buffer,struct ion_cma_buffer_info *info, struct ion_cma_buffer **release_buf_front , struct ion_cma_buffer **release_buf_back)
{
    int ret = 0;
    struct ion_cma_heap *cma_heap = to_cma_heap(buffer->heap);
    struct ion_cma_buffer *front = NULL, *back = NULL,*release_buffer;
    struct list_head release_list;
    struct miu_range range={
            0,0,0,0
    };

    INIT_LIST_HEAD(&release_list);

    free_buffer_list(cma_heap,&range,((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)?true:false,&release_list);

    if(range.front_length)
    {
        //printk("\033[35mFunction = %s, Line = %d,lenth %lx release_buf_front\033[m\n", __PRETTY_FUNCTION__, __LINE__,range.front_length);

        ret = miuprotect_addKRange(range.front_add, range.front_length);

    }

    if(range.back_length)
    {
       // printk("\033[35mFunction = %s, Line = %d, lenth %lx release_buf_back\033[m\n",
       //                       __PRETTY_FUNCTION__, __LINE__,range.back_length);
        ret = miuprotect_addKRange(range.back_add, range.back_length);
    }

    if(!list_empty(&release_list))
    {
        list_for_each_entry(release_buffer, &release_list, list)
        {
           // printk("%s,%d release dma add %lx,lenth %lx\n",__FUNCTION__,__LINE__,
           //                        release_buffer->start_pa, release_buffer->length);
            dma_release_from_contiguous(dev, release_buffer->page,
                                         release_buffer->length >> PAGE_SHIFT);
            kfree(release_buffer);
        }
    }
}
void sort_merge_cache_buffer(struct device *dev,struct list_head *head,struct ion_cma_buffer_info *info)
{
	cma_cache_buffer_range_node  *cache_range,*buffer,*next;

	list_for_each_entry(cache_range,head,list_node)
	{
		if((cache_range->range_info->handle +cache_range->range_info->size
		== info->handle))
		{
			merge_cache_buffer(dev,cache_range,info);
			if(cache_range->list_node.next == head)
				return;
			next = list_entry(cache_range->list_node.next,cma_cache_buffer_range_node,list_node);
			if(!merge_cache_buffer(dev,next,cache_range->range_info))
			{
				list_del(&next->list_node);
				kfree(next);
			}
			return;
		}
		if(info->handle+info->size == cache_range->range_info->handle)
		{
			merge_cache_buffer(dev,cache_range,info);
			return;
		}
		if(info->handle < cache_range->range_info->handle)
		{
			buffer = (cma_cache_buffer_range_node  *)kzalloc(sizeof(cma_cache_buffer_range_node),GFP_KERNEL);
			ION_CMA_BUG_ON(!buffer);
			buffer->range_info = info;
			buffer->range_len = info->size;
			list_add(&buffer->list_node,cache_range->list_node.prev);
			return;
		}
	}
	buffer = (cma_cache_buffer_range_node  *)kzalloc(sizeof(cma_cache_buffer_range_node),GFP_KERNEL);
			ION_CMA_BUG_ON(!buffer);
			buffer->range_info = info;
			buffer->range_len = info->size;
    printk("%s,%d erro!!!\n",__FUNCTION__,__LINE__);
	list_add_tail(&buffer->list_node,head);
}
static void ion_cma_free(struct ion_buffer *buffer)
{
    struct ion_cma_heap *cma_heap = to_cma_heap(buffer->heap);
    struct device *dev = cma_heap->dev;
    struct ion_cma_buffer_info *info = buffer->priv_virt;
    cma_cache_buffer_range_node  *cache_buffer_range;
    struct delay_free_reserved *tmp;
    struct ion_cma_buffer * release_buf_front = NULL, * release_buf_back = NULL,*find = NULL;

    dev_dbg(dev, "Release buffer %p\n", buffer);
    if(buffer->flags&ION_FLAG_CMA_ALLOC_RESERVE)
    {
      printk("%s,%d do not free reserved(cahched) memory!!!\n",__FUNCTION__,__LINE__);
      return;
    }
    mutex_lock(&cma_heap->secure_cache.lock);

    if(cma_heap->secure_cache.cache_set&&(info->handle >= cma_heap->secure_cache.ranges_info.handle)
        &&(info->handle + buffer->size  <=
        cma_heap->secure_cache.ranges_info.handle + cma_heap->secure_cache.ranges_total_len))
    {
	if(buffer->flags & ION_FLAG_CMA_ALLOC_RESERVE)
        {
            printk("%s,%d do not free reserved(cahched) memory!!!\n",__FUNCTION__,__LINE__);
            mutex_unlock(&cma_heap->secure_cache.lock);
            return;
        }
        sort_merge_cache_buffer(dev,&cma_heap->secure_cache.list_head,info);
        mutex_unlock(&cma_heap->secure_cache.lock);
        return;
    }
    mutex_unlock(&cma_heap->secure_cache.lock);

    mutex_lock(&cma_heap->unsecure_cache.lock);
    if(cma_heap->unsecure_cache.cache_set&&(info->handle >= cma_heap->unsecure_cache.ranges_info.handle)
        &&(info->handle + buffer->size  <=
        cma_heap->unsecure_cache.ranges_info.handle + cma_heap->unsecure_cache.ranges_total_len))
    {

        if(buffer->flags & ION_FLAG_CMA_ALLOC_RESERVE)
        {
            printk("%s,%d do not free reserved(cahched) memory!!!\n",__FUNCTION__,__LINE__);
            mutex_unlock(&cma_heap->unsecure_cache.lock);
            return;
        }
        sort_merge_cache_buffer(dev,&cma_heap->unsecure_cache.list_head,info);
        mutex_unlock(&cma_heap->unsecure_cache.lock);
        return;
    }
    mutex_unlock(&cma_heap->unsecure_cache.lock);

    mutex_lock(&delay_free_lock);
    if( !is_in_cma_force_free_period())
    {

        if((buffer->size >= delay_free_size_limite )
            &&(delay_free_time_limite != 0))
        {
            tmp =  (struct delay_free_reserved *)kzalloc(sizeof(struct delay_free_reserved),GFP_KERNEL);
            ION_CMA_BUG_ON(!tmp );
            tmp->buffer = buffer;

            tmp->delay_free_length = buffer->size;
            tmp->delay_free_time_out = delay_free_time_limite;
            if((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)
            {
                tmp->flags = ION_FLAG_CMA_ALLOC_SECURE;
            }
            else
            {
                tmp->flags = 0;
            }

            list_add(&tmp->list,&ion_cma_delay_list);
            mutex_unlock(&delay_free_lock);
            return;

        }
    }
    mutex_unlock(&delay_free_lock);

    if((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)
    {
        find = find_ion_cma_buffer(cma_heap, buffer->size,info->page, info->handle,true);
    }
    else
    {
        find = find_ion_cma_buffer(cma_heap, buffer->size, info->page, info->handle,false);

    }

    if(!find)
    {
        printk(KERN_ERR "\033[35mFunction = %s, Line = %d, [Error] [%s] Strange ION CMA Pool Free\033[m\n", __PRETTY_FUNCTION__, __LINE__, current->comm);
        printk(KERN_WARNING "\033[35mFunction = %s, Line = %d,  want to release from 0x%lX to 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, info->handle, (info->handle+buffer->size));

    }
    ION_CMA_BUG_ON(!find);
    if(find->freed)
    {
        printk("buffer already freed [PA %lX length %lX] \n",info->handle, buffer->size);
        ION_CMA_BUG_ON(1);

    }

    mutex_lock(&cma_heap->lock);
    if((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)
            ION_CMA_BUG_ON(cma_heap->alloc_list_secure.using_count == 0);
    else
        ION_CMA_BUG_ON(cma_heap->alloc_list_unsecure.using_count == 0);

    IONCMA_SplitBuffer(dev,info->size, find,
         ((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)?(&cma_heap->alloc_list_secure):(&cma_heap->alloc_list_unsecure)
        , CMA_FREE,((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)?true:false);
    //be careful, update kernel protect, then free memory to kernel
    ion_cma_free_buffer_list_and_release_special_buf_and_update_kernel_protect(dev,buffer,info, &release_buf_front, &release_buf_back);

    mutex_unlock(&cma_heap->lock);

}

#if 0
static int ion_cma_phys(struct ion_heap *heap, struct ion_buffer *buffer,
            phys_addr_t *addr, size_t *len)
{
    struct ion_cma_heap *cma_heap = to_cma_heap(buffer->heap);
    struct device *dev = cma_heap->dev;
    struct ion_cma_buffer_info *info = buffer->priv_virt;

    dev_dbg(dev, "Return buffer %p physical address 0x%pa\n", buffer,
        &info->handle);

    *addr = info->handle;
    *len = buffer->size;

    return 0;
}


static struct sg_table *ion_cma_heap_map_dma(struct ion_heap *heap,
                         struct ion_buffer *buffer)
{
    struct ion_cma_buffer_info *info = buffer->priv_virt;

    return info->table;
}


static void ion_cma_heap_unmap_dma(struct ion_heap *heap,
                   struct ion_buffer *buffer)
{
    return;
}
#endif
#if 1

static int ion_cma_mmap(struct ion_heap *heap, struct ion_buffer *buffer,
            struct vm_area_struct *vma)
{
    struct ion_cma_buffer_info *info = buffer->priv_virt;

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
#else
static int ion_cma_mmap(struct ion_heap *mapper, struct ion_buffer *buffer,
            struct vm_area_struct *vma)
{

    struct ion_cma_heap *cma_heap = to_cma_heap(buffer->heap);
    struct device *dev = cma_heap->dev;
    struct ion_cma_buffer_info *info = buffer->priv_virt;

    // goes to arm_dma_mmap, we need to modify vam->vm_page_prot first!!
    return dma_mmap_coherent(dev, vma, info->cpu_addr, info->handle,
                 buffer->size);



}
#endif


#if 1
static void *ion_cma_map_kernel(struct ion_heap *heap,
                struct ion_buffer *buffer)
{
        void *p = NULL;

        p = ion_heap_map_kernel(heap,buffer);

         printk("%s,%lx\n",__FUNCTION__,p);
         return p;

}
#else
static void *ion_cma_map_kernel(struct ion_heap *heap,
                struct ion_buffer *buffer)
{
    struct ion_cma_buffer_info *info = buffer->priv_virt;
    /* kernel memory mapping has been done at allocation time */
    return info->cpu_addr;
}
#endif

#if 1
static void ion_cma_unmap_kernel(struct ion_heap *heap,
                    struct ion_buffer *buffer)
{
        return ion_heap_unmap_kernel(heap,buffer);
}
#else
static void ion_cma_unmap_kernel(struct ion_heap *heap,
                    struct ion_buffer *buffer)
{
}
#endif

static struct ion_heap_ops ion_cma_ops = {
    .allocate = ion_cma_allocate,
    .free = ion_cma_free,
   // .map_dma = ion_cma_heap_map_dma,
   // .unmap_dma = ion_cma_heap_unmap_dma,
  //  .phys = ion_cma_phys,
    .map_user = ion_cma_mmap,
    .map_kernel = ion_cma_map_kernel,
    .unmap_kernel = ion_cma_unmap_kernel,
};


static struct task_struct *ion_cma_delay_free_tsk;
#define ION_CMA_USER_ROOT_DIR "ion_cma_delay_free"
static struct proc_dir_entry *ion_cma_delay_free_root;

static int delay_free_size_open(struct inode *inode, struct file *file){
    return 0;
}

static ssize_t delay_free_size_write(struct file *file, const char __user *buffer,
                                    size_t count, loff_t *ppos){
    char local_buf[256];
    if(count>=256)
        return -EINVAL;

    if (copy_from_user(local_buf, buffer, count))
        return -EFAULT;
    local_buf[count] = 0;

    delay_free_size_limite = simple_strtol(local_buf,NULL,10);

    return count;
}

static ssize_t delay_free_size_read(struct file *file, char __user *buf, size_t size, loff_t *ppos){
    int len = 0;

    if(*ppos != 0)
        return 0;

    len = sprintf(buf+len, "Delay Free Size limite : %d byte\n", delay_free_size_limite);
    *ppos += len;

    return len;
}
static int delay_free_timeout_open(struct inode *inode, struct file *file){
    return 0;
}
static void force_free_all_cma_cache(void)
{
    struct list_head *pos, *q;
    struct delay_free_reserved *tmp;
    struct ion_buffer *buffer;
    struct ion_cma_buffer * release_buf_front = NULL, * release_buf_back = NULL,*find = NULL;
    struct ion_cma_heap *cma_heap;
    struct device *dev;
    struct ion_cma_buffer_info *info;

    mutex_lock(&delay_free_lock);
    list_for_each_safe(pos, q, &ion_cma_delay_list)
    {
        tmp = list_entry(pos, struct delay_free_reserved, list);
        printk("\033[0;32;31m [Ian] %s %d Shrink \033[m\n",__func__,__LINE__);


        printk(KERN_DEBUG "\033[0;32;31m [Delay Free] %s %d \033[m\n",__func__,__LINE__);


             buffer =  tmp->buffer;
        cma_heap = to_cma_heap(buffer->heap);
        dev = cma_heap->dev;
        info = buffer->priv_virt;


        if((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)
        {
            find = find_ion_cma_buffer(cma_heap, buffer->size, info->page, info->handle,true);
        }
        else
        {
            find = find_ion_cma_buffer(cma_heap, buffer->size, info->page, info->handle,false);

        }

        if(!find)
        {
            printk(KERN_ERR "\033[35mFunction = %s, Line = %d, [Error] [%s] Strange ION CMA Pool Free\033[m\n", __PRETTY_FUNCTION__, __LINE__, current->comm);
            printk(KERN_WARNING "\033[35mFunction = %s, Line = %d,  want to release from 0x%lX to 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, info->handle, (info->handle+buffer->size));

        }
        ION_CMA_BUG_ON(!find);
        if(find->freed)
        {
            printk("buffer already freed [PA %lX length %lX] \n",info->handle, buffer->size);
               ION_CMA_BUG_ON(1);
            return ;
        }

        mutex_lock(&cma_heap->lock);
        if((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)
                ION_CMA_BUG_ON(cma_heap->alloc_list_secure.using_count == 0);
        else
            ION_CMA_BUG_ON(cma_heap->alloc_list_unsecure.using_count == 0);

        IONCMA_SplitBuffer(dev,buffer->size, find,
            ((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)?(&cma_heap->alloc_list_secure):(&cma_heap->alloc_list_unsecure)
            , CMA_FREE,((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)?true:false);
        //be careful, update kernel protect, then free memory to kernel
        ion_cma_free_buffer_list_and_release_special_buf_and_update_kernel_protect(dev,buffer,info, &release_buf_front, &release_buf_back);


        mutex_unlock(&cma_heap->lock);

        list_del(pos);
        kfree(tmp);
    }
    mutex_unlock(&delay_free_lock);
}
static ssize_t delay_free_timeout_write(struct file *file, const char __user *buffer,
                                    size_t count, loff_t *ppos){
    char local_buf[256];
    struct list_head *pos, *q;
    struct delay_free_reserved *tmp;

    if(count>=256)
        return -EINVAL;
    if (copy_from_user(local_buf, buffer, count))
        return -EFAULT;
    local_buf[count] = 0;

    delay_free_time_limite = simple_strtol(&local_buf,NULL,10);

    if(delay_free_time_limite == 0)
        force_free_all_cma_cache();
    mutex_lock(&delay_free_lock);

    list_for_each_safe(pos, q, &ion_cma_delay_list){
        tmp = list_entry(pos, struct delay_free_reserved, list);
        if(tmp->delay_free_time_out > delay_free_time_limite){
            tmp->delay_free_time_out = delay_free_time_limite;
        }
    }
    mutex_unlock(&delay_free_lock);

    return count;
}

static ssize_t delay_free_timeout_read(struct file *file, char __user *buf, size_t size, loff_t *ppos){
    int len = 0;

    if(*ppos != 0)
        return 0;

    len = sprintf(buf+len, "Delay Free time out : %d sec\n", delay_free_time_limite);
    *ppos += len;

    return len;
}


static int delay_free_lowmem_minfree_open(struct inode *inode, struct file *file){
    return 0;
}

static ssize_t delay_free_lowmem_minfree_write(struct file *file, const char __user *buffer,
                                    size_t count, loff_t *ppos){
    char local_buf[256];

    if(count>=256)
        return -EINVAL;

    if (copy_from_user(local_buf, buffer, count))
        return -EFAULT;
    local_buf[count] = 0;

    delay_free_lowmem_minfree = simple_strtol(local_buf,NULL,10);


    return count;
}

static ssize_t delay_free_lowmem_minfree_read(struct file *file, char __user *buf, size_t size, loff_t *ppos){
    int len = 0;

    if(*ppos != 0)
        return 0;

    len = sprintf(buf+len, "Delay Free lowmem minfree : %d byte\n", delay_free_lowmem_minfree);
    *ppos += len;

    return len;
}

static int delay_free_force_free_open(struct inode *inode, struct file *file){
    return 0;
}
static ssize_t delay_free_force_free_read(struct file *file, char __user *buf, size_t size, loff_t *ppos){

   //Show_CMA_Pool_state();
   //to do...
   return 0;
}
static ssize_t delay_free_force_free_write(struct file *file, char __user *buf, size_t size, loff_t *ppos){
   // unsigned long free_size;
   unsigned long duration_in_ms;
   char local_buf[256];


   if(size>=256)
       return -EINVAL;

   if (copy_from_user(local_buf, buf, size))
       return -EFAULT;
   local_buf[size] = 0;
   duration_in_ms = simple_strtol(local_buf,NULL,10);

   if(duration_in_ms > 30000)
      duration_in_ms = 30000;

   delay_free_evict_duration = (duration_in_ms*HZ)/1000;
   delay_free_last_force_free_jiffies = jiffies;
   force_free_all_cma_cache();

   return size;
}

static const struct file_operations delay_free_fops = {
    .owner = THIS_MODULE,
    .write = delay_free_size_write,
    .read = delay_free_size_read,
    .open = delay_free_size_open,
    .llseek = seq_lseek,
};
static const struct file_operations delay_free_timeout_fops = {
    .owner = THIS_MODULE,
    .write = delay_free_timeout_write,
    .read = delay_free_timeout_read,
    .open = delay_free_timeout_open,
    .llseek = seq_lseek,
};
static const struct file_operations delay_free_lowmem_minfree_fops = {
    .owner = THIS_MODULE,
    .write = delay_free_lowmem_minfree_write,
    .read = delay_free_lowmem_minfree_read,
    .open = delay_free_lowmem_minfree_open,
    .llseek = seq_lseek,
};
static const struct file_operations delay_free_force_free_fops = {
    .owner = THIS_MODULE,
    .write = delay_free_force_free_write,
    .read = delay_free_force_free_read,
    .open = delay_free_force_free_open,
    .llseek = seq_lseek,
};
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,31)
static unsigned long delay_free_count_objects(struct shrinker *s, struct shrink_control *sc)
{
	return 0;
}
#endif

static int delay_free_shrink(struct shrinker *s, struct shrink_control *sc)
{

	int other_free = global_page_state(NR_FREE_PAGES) - totalreserve_pages;
	int other_file = global_page_state(NR_FILE_PAGES) -
						global_page_state(NR_SHMEM);
	int free_cma = 0;

#ifdef CONFIG_CMA
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,31)
	if (gfpflags_to_migratetype(sc->gfp_mask) != MIGRATE_MOVABLE)
#else
	if (allocflags_to_migratetype(sc->gfp_mask) != MIGRATE_MOVABLE)
#endif
		free_cma = global_page_state(NR_FREE_CMA_PAGES);
#endif

	if(delay_free_lowmem_minfree < lowmem_minfree[lowmem_minfree_size-1])
		delay_free_lowmem_minfree = lowmem_minfree[lowmem_minfree_size-1];

	if ((other_free - free_cma) < delay_free_lowmem_minfree && other_file < delay_free_lowmem_minfree) {
		force_free_all_cma_cache();

	}
	return 0;
}

static struct shrinker delay_free_shrinker = {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,18,31)
	.count_objects = delay_free_count_objects,
	.scan_objects = delay_free_shrink,
#else
	.shrink = delay_free_shrink,
#endif
	.seeks = DEFAULT_SEEKS * 16

};

static void ion_cma_delay_free(void* arg)
{
    struct list_head *pos, *q;
    struct delay_free_reserved *tmp;
    struct ion_buffer *buffer;
    struct ion_cma_buffer * release_buf_front = NULL, * release_buf_back = NULL,*find = NULL;
    while(1)
    {
        mutex_lock(&delay_free_lock);
        list_for_each_safe(pos, q, &ion_cma_delay_list)
        {
            tmp = list_entry(pos, struct delay_free_reserved, list);

            if((tmp->delay_free_time_out == 0) &&(tmp->buffer)&& (tmp->buffer->heap))
            {
                struct ion_cma_heap *cma_heap;
                struct device *dev;
                struct ion_cma_buffer_info *info;
                printk(KERN_DEBUG "\033[0;32;31m [Delay Free] %s %d \033[m\n",__func__,__LINE__);

                buffer =  tmp->buffer;
                cma_heap = to_cma_heap(buffer->heap);
                dev = cma_heap->dev;
                info = buffer->priv_virt;


                if((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)
                {
                    find = find_ion_cma_buffer(cma_heap, buffer->size,info->page, info->handle,true);
                }
                else
                {
                    find = find_ion_cma_buffer(cma_heap, buffer->size,info->page , info->handle,false);

                }

                if(!find)
                {
                    printk(KERN_ERR "\033[35mFunction = %s, Line = %d, [Error] [%s] Strange ION CMA Pool Free\033[m\n", __PRETTY_FUNCTION__, __LINE__, current->comm);
                    printk(KERN_WARNING "\033[35mFunction = %s, Line = %d,  want to release from 0x%lX to 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, info->handle, (info->handle+buffer->size));

                }
                ION_CMA_BUG_ON(!find);
                if(find->freed)
                {
                    printk("buffer already freed [PA %lX length %lX] \n",info->handle, buffer->size);
                    ION_CMA_BUG_ON(1);

                }

                mutex_lock(&cma_heap->lock);
                if((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)
                        ION_CMA_BUG_ON(cma_heap->alloc_list_secure.using_count == 0);
                else
                    ION_CMA_BUG_ON(cma_heap->alloc_list_unsecure.using_count == 0);

                IONCMA_SplitBuffer(dev,buffer->size, find,
                    ((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)?(&cma_heap->alloc_list_secure):(&cma_heap->alloc_list_unsecure)
                    , CMA_FREE,((buffer->flags & ION_FLAG_CMA_ALLOC_SECURE) !=0)?true:false);
                //be careful, update kernel protect, then free memory to kernel
                ion_cma_free_buffer_list_and_release_special_buf_and_update_kernel_protect(dev,buffer,info, &release_buf_front, &release_buf_back);


                mutex_unlock(&cma_heap->lock);



                list_del(pos);
                kfree(tmp);
            }
            else if(tmp->delay_free_time_out > 0){
                tmp->delay_free_time_out--;
            }

        }
        mutex_unlock(&delay_free_lock);
        msleep(1000);
    }
}

#if 1
static void Show_ION_CMA_state(void)
{
#if 1
//to do:
//add debug code for showing ion cma allocation


#endif
}

static int ion_cma_state_open(struct inode *inode, struct file *file)
{
    return 0;
}


static ssize_t ion_cma_state_read(struct file *file, char __user *buf, size_t size, loff_t *ppos)
{
    Show_ION_CMA_state();
    return 0;
}


static const struct file_operations ion_cma_state_fops = {
    .owner      = THIS_MODULE,
    .open       = ion_cma_state_open,
    .read       = ion_cma_state_read,
    .llseek     = seq_lseek,
};
#endif
int ion_cma_delay_free_init(void)
{
    int s32Ret;
    struct proc_dir_entry *size;
    struct proc_dir_entry *timeout;

    ion_cma_delay_free_tsk = kthread_create(ion_cma_delay_free, NULL, "Ion CMA Delay Free Task");
    if (IS_ERR(ion_cma_delay_free_tsk)) {
        printk("create kthread for delay free fail\n");
        s32Ret = PTR_ERR(ion_cma_delay_free_tsk);
        ion_cma_delay_free_tsk = NULL;
    }else
        wake_up_process(ion_cma_delay_free_tsk);

    ion_cma_delay_free_root = proc_mkdir(ION_CMA_USER_ROOT_DIR, NULL);
    if (NULL==ion_cma_delay_free_root)
    {
        printk(KERN_ALERT "Create dir /proc/%s error!\n",ION_CMA_USER_ROOT_DIR);
        return -1;
    }

    size = proc_create("cma_delay_free_size_limite", 0644, ion_cma_delay_free_root, &delay_free_fops);
    if (!size){
        printk(KERN_ALERT "Create dir /proc/%s/cma_delay_free_size error!\n",ION_CMA_USER_ROOT_DIR);
        return -ENOMEM;
    }

    timeout = proc_create("cma_delay_free_timeout", 0644, ion_cma_delay_free_root, &delay_free_timeout_fops);
    if (!timeout){
        printk(KERN_ALERT "Create dir /proc/%s/cma_delay_free_timeout error!\n",ION_CMA_USER_ROOT_DIR);
        return -ENOMEM;
    }

//  struct proc_dir_entry *delay_free_lowmem_minfree;
    timeout = proc_create("cma_delay_free_lowmem_minfree", 0644, ion_cma_delay_free_root, &delay_free_lowmem_minfree_fops);
    if (!timeout){
        printk(KERN_ALERT "Create dir /proc/%s/cma_delay_free_lowmem_minfree error!\n",ION_CMA_USER_ROOT_DIR);
        return -ENOMEM;
    }
    timeout = proc_create("cma_force_free_cache", 0644, ion_cma_delay_free_root, &delay_free_force_free_fops);
    if (!timeout){
        printk(KERN_ALERT "Create dir /proc/%s/cma_force_free_cache error!\n",ION_CMA_USER_ROOT_DIR);
        return -ENOMEM;
    }

    timeout = proc_create("cma_state", 0644, ion_cma_delay_free_root, &ion_cma_state_fops);
    if (!timeout){
        printk(KERN_ALERT "Create dir /proc/%s/cma_state error!\n",ION_CMA_USER_ROOT_DIR);
        return -ENOMEM;
    }


    register_shrinker(&delay_free_shrinker);

    return 0;
}
static inline void phy_to_MiuOffset(unsigned long phy_addr, unsigned int *miu, unsigned long *offset)
{
    *miu = 0xff;

    if(phy_addr >= ARM_MIU2_BUS_BASE)
    {
        *miu = 2;
        *offset = phy_addr - ARM_MIU2_BUS_BASE;
    }
    else if(phy_addr >= ARM_MIU1_BUS_BASE)
    {
        *miu = 1;
        *offset = phy_addr - ARM_MIU1_BUS_BASE;
    }
    else if(phy_addr >= ARM_MIU0_BUS_BASE)
    {
        *miu = 0;
        *offset = phy_addr - ARM_MIU0_BUS_BASE;
    }
    else
        printk(CMA_ERR "\033[35mFunction = %s, Line = %d, Error, Unknown MIU, for phy_addr is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, phy_addr);
}

struct ion_heap *ion_cma_heap_create(struct ion_platform_heap *data)
{
    struct ion_cma_heap *cma_heap;
    struct mstar_cma_heap_private *mcma_private;
    unsigned int index;
    unsigned long offset;
    cma_heap = kzalloc(sizeof(struct ion_cma_heap), GFP_KERNEL);
    if (!cma_heap)
    {
        printk(" debug %s:%d\n",__FUNCTION__,__LINE__);
        return ERR_PTR(-ENOMEM);
    }
    cma_heap->heap.ops = &ion_cma_ops;
    /* get device from private heaps data, later it will be
     * used to make the link with reserved CMA memory */
    mcma_private = (struct mstar_cma_heap_private *)data->priv;
    cma_heap->dev =(struct device *)( mcma_private->cma_dev);
    cma_heap->heap.type = ION_HEAP_TYPE_DMA;

    cma_heap->base = data ->base;
    cma_heap->size = data ->size;

    phy_to_MiuOffset(PFN_PHYS(cma_heap->dev->cma_area->base_pfn),&index,&offset);
    g_heap_info[index].base_phy =PFN_PHYS(cma_heap->dev->cma_area->base_pfn);
    g_heap_info[index].size = cma_heap->dev->cma_area->count << PAGE_SHIFT;

    cma_heap->alloc_list_unsecure.min_start = PHYSICAL_START_INIT;
    cma_heap->alloc_list_unsecure.max_end = PHYSICAL_END_INIT;
    cma_heap->alloc_list_unsecure.using_count = 0;
    cma_heap->alloc_list_unsecure.freed_count = 0;
    INIT_LIST_HEAD(&cma_heap->alloc_list_unsecure.list_head);
    cma_heap->alloc_list_secure.min_start = PHYSICAL_START_INIT;
    cma_heap->alloc_list_secure.max_end = PHYSICAL_END_INIT;
    cma_heap->alloc_list_secure.using_count = 0;
    cma_heap->alloc_list_secure.freed_count = 0;
    INIT_LIST_HEAD(&cma_heap->alloc_list_secure.list_head);
    mutex_init(&cma_heap->lock);

    if(false == delay_free_init_done)
    {
          ion_cma_delay_free_init();
        delay_free_init_done = true;
    }

    ion_cma_heap_create_not_destroy_num++;

    //cma cache related
    INIT_LIST_HEAD(&cma_heap->secure_cache.list_head);
    mutex_init(&cma_heap->secure_cache.lock);
    mutex_lock(&cma_heap->secure_cache.lock);
    cma_heap->secure_cache.cache_set = false;
    mutex_unlock(&cma_heap->secure_cache.lock);
    INIT_LIST_HEAD(&cma_heap->unsecure_cache.list_head);
    mutex_init(&cma_heap->unsecure_cache.lock);
    mutex_lock(&cma_heap->unsecure_cache.lock);
    cma_heap->unsecure_cache.cache_set = false;
    mutex_unlock(&cma_heap->unsecure_cache.lock);

    return &cma_heap->heap;
}

void ion_cma_heap_destroy(struct ion_heap *heap)
{
    struct ion_cma_heap *cma_heap = to_cma_heap(heap);
    kfree(cma_heap);
    ion_cma_heap_create_not_destroy_num--;
    if(0 == ion_cma_heap_create_not_destroy_num)
    {
        kthread_stop(ion_cma_delay_free_tsk);
        remove_proc_entry(ION_CMA_USER_ROOT_DIR, ion_cma_delay_free_root);
        unregister_shrinker(&delay_free_shrinker);
    }

}

#endif
