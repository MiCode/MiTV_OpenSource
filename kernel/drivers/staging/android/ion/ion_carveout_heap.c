// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/staging/android/ion/ion_carveout_heap.c
 *
 * Copyright (C) 2011 Google, Inc.
 */
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/genalloc.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include "ion.h"

#define ION_CARVEOUT_ALLOCATE_FAIL	-1

struct ion_carveout_heap {
	struct ion_heap heap;
	struct gen_pool *pool;
	phys_addr_t base;
};

static phys_addr_t ion_carveout_allocate(struct ion_heap *heap,
					 unsigned long size)
{
	struct ion_carveout_heap *carveout_heap =
		container_of(heap, struct ion_carveout_heap, heap);
	unsigned long offset = gen_pool_alloc(carveout_heap->pool, size);

	if (!offset)
		return ION_CARVEOUT_ALLOCATE_FAIL;

	return offset;
}

static void ion_carveout_free(struct ion_heap *heap, phys_addr_t addr,
			      unsigned long size)
{
	struct ion_carveout_heap *carveout_heap =
		container_of(heap, struct ion_carveout_heap, heap);

	if (addr == ION_CARVEOUT_ALLOCATE_FAIL)
		return;
	gen_pool_free(carveout_heap->pool, addr, size);
}

static int ion_carveout_heap_allocate(struct ion_heap *heap,
				      struct ion_buffer *buffer,
				      unsigned long size,
				      unsigned long flags)
{
	struct sg_table *table;
	phys_addr_t paddr;
	int ret;

	table = kmalloc(sizeof(*table), GFP_KERNEL);
	if (!table)
		return -ENOMEM;
	ret = sg_alloc_table(table, 1, GFP_KERNEL);
	if (ret)
		goto err_free;

	paddr = ion_carveout_allocate(heap, size);
	if (paddr == ION_CARVEOUT_ALLOCATE_FAIL) {
		ret = -ENOMEM;
		goto err_free_table;
	}

	sg_set_page(table->sgl, pfn_to_page(PFN_DOWN(paddr)), size, 0);
	buffer->sg_table = table;

	return 0;

err_free_table:
	sg_free_table(table);
err_free:
	kfree(table);
	return ret;
}

static void ion_carveout_heap_free(struct ion_buffer *buffer)
{
	struct ion_heap *heap = buffer->heap;
	struct sg_table *table = buffer->sg_table;
	struct page *page = sg_page(table->sgl);
	phys_addr_t paddr = PFN_PHYS(page_to_pfn(page));

	ion_heap_buffer_zero(buffer);

	ion_carveout_free(heap, paddr, buffer->size);
	sg_free_table(table);
	kfree(table);
}

static struct ion_heap_ops carveout_heap_ops = {
	.allocate = ion_carveout_heap_allocate,
	.free = ion_carveout_heap_free,
	.map_user = ion_heap_map_user,
	.map_kernel = ion_heap_map_kernel,
	.unmap_kernel = ion_heap_unmap_kernel,
};

struct ion_heap *ion_carveout_heap_create(struct ion_platform_heap *heap_data)
{
	struct ion_carveout_heap *carveout_heap;
	int ret;

	struct page *page;
	size_t size;

	page = pfn_to_page(PFN_DOWN(heap_data->base));
	size = heap_data->size;

	ret = ion_heap_pages_zero(page, size, pgprot_writecombine(PAGE_KERNEL));
	if (ret)
		return ERR_PTR(ret);

	carveout_heap = kzalloc(sizeof(*carveout_heap), GFP_KERNEL);
	if (!carveout_heap)
		return ERR_PTR(-ENOMEM);

	carveout_heap->pool = gen_pool_create(PAGE_SHIFT, -1);
	if (!carveout_heap->pool) {
		kfree(carveout_heap);
		return ERR_PTR(-ENOMEM);
	}
	carveout_heap->base = heap_data->base;
	gen_pool_add(carveout_heap->pool, carveout_heap->base, heap_data->size,
		     -1);
	carveout_heap->heap.ops = &carveout_heap_ops;
	carveout_heap->heap.type = ION_HEAP_TYPE_CARVEOUT;
	carveout_heap->heap.flags = ION_HEAP_FLAG_DEFER_FREE;

	return &carveout_heap->heap;
}

#ifdef CONFIG_ZRAM_OVER_GENPOOL
static size_t ion_carveout_size(struct ion_heap *heap)
{
	struct ion_carveout_heap *carveout_heap =
		container_of(heap, struct ion_carveout_heap, heap);
	size_t size = gen_pool_avail(carveout_heap->pool);

	return size;
}

static struct ion_platform_heap mstar_default_heaps[1] = {
		{
			.id = (unsigned int)ION_HEAP_TYPE_CARVEOUT,
			.type = ION_HEAP_TYPE_CARVEOUT,
			.name = "ion_carveout_heap",
			.base = 0x0,
			.size = 0x0,
			.align = 0x0,
			.priv = NULL,
		},
};

struct ion_heap *carveout_heap = NULL;
unsigned long long carveout_start = 0;
unsigned long long carveout_end = 0;

#if 0 // compile fail, dont know why
#include <linux/of_reserved_mem.h>
static int __init ion_reserve_memory_to_camera(struct reserved_mem *mem)
{
	carveout_start = mem->base;
	carveout_end = carveout_start + mem->size;

	pr_info("%s: name:%s,base:%llx,size:0x%llx\n",
		__func__, mem->name,
		(unsigned long long)mem->base,
		(unsigned long long)mem->size);
	return 0;
}

RESERVEDMEM_OF_DECLARE(ion_camera_reserve, "mediatek,ion-carveout-heap", ion_reserve_memory_to_camera);
#endif

int ion_drv_create_carveout_heap(void)
{
	if (carveout_start == 0 && carveout_end == 0) {
		carveout_heap = NULL;
		printk("%s: carveout_start == carveout_end == 0, no zram_over_genpool\n", __func__);
		return 0;
	}

	mstar_default_heaps[0].base = carveout_start;
	mstar_default_heaps[0].size = carveout_end - carveout_start;
	carveout_heap = ion_carveout_heap_create(&(mstar_default_heaps[0]));

	if (carveout_heap < 0) {
		printk(KERN_ALERT "ion_carveout_heap_create fail\n");
		return 1;
	}
	return 0;
}
struct page* alloc_reserve_page(void)
{
	phys_addr_t paddr;

	if (carveout_heap == NULL) {
		printk_once(KERN_EMERG "%s: carveout_heap == NULL\n", __func__);
		return NULL;
	}

	paddr = ion_carveout_allocate(carveout_heap, PAGE_SIZE);
	if (paddr == ION_CARVEOUT_ALLOCATE_FAIL) {
		printk_once(KERN_EMERG "%s: ION_CARVEOUT_ALLOCATE_FAIL == NULL, free: 0x%x\n", __func__, ion_carveout_size(carveout_heap));
		return NULL;
	} else {
		if (paddr & (PAGE_SIZE-1)) {
			printk("%s: ion_carveout_allocate not PAGE alignment\n", __func__);
			panic("not page alignemnt");
		}
		if (!(paddr >= carveout_start && paddr < carveout_end)) {
			printk("%s: ion_carveout_allocate out-of-range\n", __func__);
			printk("0x%x, 0x%x, 0x%x\n", paddr, carveout_start, carveout_end);
			panic("out-of-range");
		}
		//pfn_to_page(PHYS_PFN(paddr));
		//trace_printk("0x%x, 0x%x, remaining: %d\n", paddr, phys_to_page(paddr), ion_carveout_size(carveout_heap));
		return phys_to_page(paddr);
	}
}

void free_reserve_page(const struct page *page)
{
	phys_addr_t paddr = page_to_phys(page);
	if (carveout_heap && paddr >= carveout_start && paddr < carveout_end) {
		ion_carveout_free(carveout_heap, page_to_phys(page), PAGE_SIZE);
	}
}

int is_reserve_page(const struct page *page)
{
	phys_addr_t paddr = page_to_phys(page);
	if (carveout_heap && paddr >= carveout_start && paddr < carveout_end) {
		return 1;
	} else {
		return 0;
	}
}

device_initcall(ion_drv_create_carveout_heap);
#endif
