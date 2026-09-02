/*
 * Contiguous Memory Allocator
 *
 * Copyright (c) 2010-2011 by Samsung Electronics.
 * Copyright IBM Corporation, 2013
 * Copyright LG Electronics Inc., 2014
 * Written by:
 *	Marek Szyprowski <m.szyprowski@samsung.com>
 *	Michal Nazarewicz <mina86@mina86.com>
 *	Aneesh Kumar K.V <aneesh.kumar@linux.vnet.ibm.com>
 *	Joonsoo Kim <iamjoonsoo.kim@lge.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License or (at your optional) any later version of the license.
 */

#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
/* for using pr_info */
#define pr_fmt(fmt) "\033[31mFunction = %s, Line = %d, cma: \033[m" fmt , __PRETTY_FUNCTION__, __LINE__
#else
#define pr_fmt(fmt) "cma: " fmt
#endif

#ifdef CONFIG_CMA_DEBUG
#ifndef DEBUG
#  define DEBUG
#endif
#endif
#define CREATE_TRACE_POINTS

#include <linux/memblock.h>
#include <linux/err.h>
#include <linux/mm.h>
#include <linux/mutex.h>
#include <linux/sizes.h>
#include <linux/slab.h>
#include <linux/log2.h>
#include <linux/cma.h>
#include <linux/highmem.h>
#include <linux/io.h>
#include <linux/kmemleak.h>
#include <trace/events/cma.h>

#include "cma.h"

#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
#include <linux/dma-contiguous.h>
#include <linux/buffer_head.h>
#endif

#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
extern signed long long Show_Diff_Time(char *caller, ktime_t start_time, bool print);
#endif

struct cma cma_areas[MAX_CMA_AREAS];
unsigned cma_area_count;
static DEFINE_MUTEX(cma_mutex);

phys_addr_t cma_get_base(const struct cma *cma)
{
	return PFN_PHYS(cma->base_pfn);
}
EXPORT_SYMBOL(cma_get_base);

unsigned long cma_get_size(const struct cma *cma)
{
	return cma->count << PAGE_SHIFT;
}
EXPORT_SYMBOL(cma_get_size);

const char *cma_get_name(const struct cma *cma)
{
	return cma->name ? cma->name : "(undefined)";
}
EXPORT_SYMBOL_GPL(cma_get_name);

static unsigned long cma_bitmap_aligned_mask(const struct cma *cma,
					     unsigned int align_order)
{
	if (align_order <= cma->order_per_bit)
		return 0;
	return (1UL << (align_order - cma->order_per_bit)) - 1;
}

/*
 * Find the offset of the base PFN from the specified align_order.
 * The value returned is represented in order_per_bits.
 */
static unsigned long cma_bitmap_aligned_offset(const struct cma *cma,
					       unsigned int align_order)
{
	return (cma->base_pfn & ((1UL << align_order) - 1))
		>> cma->order_per_bit;
}

static unsigned long cma_bitmap_pages_to_bits(const struct cma *cma,
					      unsigned long pages)
{
	return ALIGN(pages, 1UL << cma->order_per_bit) >> cma->order_per_bit;
}

static void cma_clear_bitmap(struct cma *cma, unsigned long pfn,
			     unsigned int count)
{
	unsigned long bitmap_no, bitmap_count;

	bitmap_no = (pfn - cma->base_pfn) >> cma->order_per_bit;
	bitmap_count = cma_bitmap_pages_to_bits(cma, count);

	mutex_lock(&cma->lock);
	bitmap_clear(cma->bitmap, bitmap_no, bitmap_count);
	mutex_unlock(&cma->lock);
}

static int __init cma_activate_area(struct cma *cma)
{
	int bitmap_size = BITS_TO_LONGS(cma_bitmap_maxno(cma)) * sizeof(long);
	unsigned long base_pfn = cma->base_pfn, pfn = base_pfn;
	unsigned i = cma->count >> pageblock_order;
	struct zone *zone;

	cma->bitmap = kzalloc(bitmap_size, GFP_KERNEL);

	if (!cma->bitmap) {
		cma->count = 0;
		return -ENOMEM;
	}

	WARN_ON_ONCE(!pfn_valid(pfn));
	zone = page_zone(pfn_to_page(pfn));
	printk("\033[35mFunction = %s, cma_start pfn: 0x%X, length: 0x%X, is in zone %s\033[m\n", __PRETTY_FUNCTION__, (unsigned int)pfn, (unsigned int)cma->count, zone->name);

	do {
		unsigned j;

		base_pfn = pfn;
		for (j = pageblock_nr_pages; j; --j, pfn++) {
			WARN_ON_ONCE(!pfn_valid(pfn));
			/*
			 * alloc_contig_range requires the pfn range
			 * specified to be in the same zone. Make this
			 * simple by forcing the entire CMA resv range
			 * to be in the same zone.
			 */
			if (page_zone(pfn_to_page(pfn)) != zone)
				goto not_in_zone;
		}
		init_cma_reserved_pageblock(pfn_to_page(base_pfn));
	} while (--i);

	mutex_init(&cma->lock);

#ifdef CONFIG_CMA_DEBUGFS
	INIT_HLIST_HEAD(&cma->mem_head);
	spin_lock_init(&cma->mem_head_lock);
#endif

#ifdef CONFIG_MP_CMA_PATCH_CMA_AGGRESSIVE_ALLOC
	adjust_managed_cma_page_count(zone, cma->count);
#endif

	return 0;

not_in_zone:
	pr_err("CMA area %s could not be activated\n", cma->name);
	kfree(cma->bitmap);
	cma->count = 0;
	return -EINVAL;
}

#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
// Add for init fake cma area, this fake cma area is a copy of original cma, with different bitmap
// So, this fake cma area can do fake dispatch for mali
// and user do ion_ioctl will use same cma_id, so, we also need a cma_id(true) to cma(fake) mapping table
static int __init cma_activate_fake_area(struct cma *cma)
{
	int bitmap_size = BITS_TO_LONGS(cma_bitmap_maxno(cma)) * sizeof(long);

	cma->bitmap = kzalloc(bitmap_size, GFP_KERNEL);
	if (!cma->bitmap)
	{
		printk(KERN_EMERG "\033[31mFunction = %s, allocating a fake_cma bitmap is fail, return -ENOMEM\033[m\n\n", __PRETTY_FUNCTION__);
		cma->count = 0;
		return -ENOMEM;
	}
	WARN_ON_ONCE(pfn_valid(cma->base_pfn));

	mutex_init(&cma->lock);

#ifdef CONFIG_CMA_DEBUGFS
	INIT_HLIST_HEAD(&cma->mem_head);
	spin_lock_init(&cma->mem_head_lock);
#endif

	printk("\033[31mFunction = %s, successfully init a fake cma area, pfn: 0x%X, length: 0x%X\033[m\n", __PRETTY_FUNCTION__, (unsigned int)cma->base_pfn, (unsigned int)cma->count);
	return 0;
}
#endif

static int __init cma_init_reserved_areas(void)
{
	int i;
	int ret = 0;

	for (i = 0; i < cma_area_count; i++)
	{
#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
		if (cma_areas[i].flags & CMA_FAKEMEM)
		{
			ret = cma_activate_fake_area(&cma_areas[i]);
			if (ret)
				printk(KERN_ERR "\033[31mFunction = %s, Line = %d, doing fake cma_info error!!\033[m\n", __PRETTY_FUNCTION__, __LINE__);
		}
		else
#endif
			ret = cma_activate_area(&cma_areas[i]);

		if (ret)
			return ret;
	}

	return 0;
}
core_initcall(cma_init_reserved_areas);

/**
 * cma_init_reserved_mem() - create custom contiguous area from reserved memory
 * @base: Base address of the reserved area
 * @size: Size of the reserved area (in bytes),
 * @order_per_bit: Order of pages represented by one bit on bitmap.
 * @name: The name of the area. If this parameter is NULL, the name of
 *        the area will be set to "cmaN", where N is a running counter of
 *        used areas.
 * @res_cma: Pointer to store the created cma region.
 *
 * This function creates custom contiguous area from already reserved memory.
 */
int __init cma_init_reserved_mem(phys_addr_t base, phys_addr_t size,
				 unsigned int order_per_bit,
				 const char *name,
				 struct cma **res_cma)
{
	struct cma *cma;
	phys_addr_t alignment;

	/* Sanity checks */
	if (cma_area_count == ARRAY_SIZE(cma_areas)) {
		pr_err("Not enough slots for CMA reserved regions!\n");
		return -ENOSPC;
	}

#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
	if ( (!size || !memblock_is_region_reserved(base, size)) && (strncmp(name, "fakemem", 7) != 0) )
#else
	if (!size || !memblock_is_region_reserved(base, size))
#endif
		return -EINVAL;

	/* ensure minimal alignment required by mm core */
	alignment = PAGE_SIZE <<
			max_t(unsigned long, MAX_ORDER - 1, pageblock_order);

	/* alignment should be aligned with order_per_bit */
	if (!IS_ALIGNED(alignment >> PAGE_SHIFT, 1 << order_per_bit))
		return -EINVAL;

	if (ALIGN(base, alignment) != base || ALIGN(size, alignment) != size)
		return -EINVAL;

	/*
	 * Each reserved area must be initialised later, when more kernel
	 * subsystems (like slab allocator) are available.
	 */
	cma = &cma_areas[cma_area_count];
	if (name) {
		cma->name = name;
	} else {
		cma->name = kasprintf(GFP_KERNEL, "cma%d\n", cma_area_count);
		if (!cma->name)
			return -ENOMEM;
	}
	cma->base_pfn = PFN_DOWN(base);
	cma->count = size >> PAGE_SHIFT;
	cma->order_per_bit = order_per_bit;
#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
	cma->flags = 0;
#endif
	*res_cma = cma;
	cma_area_count++;
#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
	if((strncmp(name, "fakemem", 7) != 0))
#endif
		totalcma_pages += (size / PAGE_SIZE);

	return 0;
}

/**
 * cma_declare_contiguous() - reserve custom contiguous area
 * @base: Base address of the reserved area optional, use 0 for any
 * @size: Size of the reserved area (in bytes),
 * @limit: End address of the reserved memory (optional, 0 for any).
 * @alignment: Alignment for the CMA area, should be power of 2 or zero
 * @order_per_bit: Order of pages represented by one bit on bitmap.
 * @fixed: hint about where to place the reserved area
 * @name: The name of the area. See function cma_init_reserved_mem()
 * @res_cma: Pointer to store the created cma region.
 *
 * This function reserves memory from early allocator. It should be
 * called by arch specific code once the early allocator (memblock or bootmem)
 * has been activated and all other subsystems have already allocated/reserved
 * memory. This function allows to create custom reserved areas.
 *
 * If @fixed is true, reserve contiguous area at exactly @base.  If false,
 * reserve in range from @base to @limit.
 */
int __init cma_declare_contiguous(phys_addr_t base,
			phys_addr_t size, phys_addr_t limit,
			phys_addr_t alignment, unsigned int order_per_bit,
			bool fixed, const char *name, struct cma **res_cma)
{
	phys_addr_t memblock_end = memblock_end_of_DRAM();
	phys_addr_t highmem_start;
	int ret = 0;

	/*
	 * We can't use __pa(high_memory) directly, since high_memory
	 * isn't a valid direct map VA, and DEBUG_VIRTUAL will (validly)
	 * complain. Find the boundary by adding one to the last valid
	 * address.
	 */
	highmem_start = __pa(high_memory - 1) + 1;
	pr_debug("%s(size %pa, base %pa, limit %pa alignment %pa)\n",
		__func__, &size, &base, &limit, &alignment);

	if (cma_area_count == ARRAY_SIZE(cma_areas)) {
		pr_err("Not enough slots for CMA reserved regions!\n");
		return -ENOSPC;
	}

	if (!size)
		return -EINVAL;

	if (alignment && !is_power_of_2(alignment))
		return -EINVAL;

	/*
	 * Sanitise input arguments.
	 * Pages both ends in CMA area could be merged into adjacent unmovable
	 * migratetype page by page allocator's buddy algorithm. In the case,
	 * you couldn't get a contiguous memory, which is not what we want.
	 */
	alignment = max(alignment,  (phys_addr_t)PAGE_SIZE <<
			  max_t(unsigned long, MAX_ORDER - 1, pageblock_order));
	if (fixed && base & (alignment - 1)) {
#ifdef CONFIG_MSTAR_CHIP
		pr_err("Region at %pa must be aligned to %pa bytes\n",
			&base, &alignment);
#else
		ret = -EINVAL;
		pr_err("Region at %pa must be aligned to %pa bytes\n",
			&base, &alignment);
		goto err;
#endif
	}
	base = ALIGN(base, alignment);
	size = ALIGN(size, alignment);
	limit &= ~(alignment - 1);

	if (!base)
		fixed = false;

	/* size should be aligned with order_per_bit */
	if (!IS_ALIGNED(size >> PAGE_SHIFT, 1 << order_per_bit))
		return -EINVAL;

	/*
	 * If allocating at a fixed base the request region must not cross the
	 * low/high memory boundary.
	 */
	if (fixed && base < highmem_start && base + size > highmem_start) {
		ret = -EINVAL;
		pr_err("Region at %pa defined on low/high memory boundary (%pa)\n",
			&base, &highmem_start);
		goto err;
	}

	/*
	 * If the limit is unspecified or above the memblock end, its effective
	 * value will be the memblock end. Set it explicitly to simplify further
	 * checks.
	 */
	if (limit == 0 || limit > memblock_end)
		limit = memblock_end;

	if (base + size > limit) {
#ifdef CONFIG_MSTAR_CHIP
		pr_err("Size (%pa) of region at %pa exceeds limit (%pa)\n",
			&size, &base, &limit);
#else
		ret = -EINVAL;
		pr_err("Size (%pa) of region at %pa exceeds limit (%pa)\n",
			&size, &base, &limit);
		goto err;
#endif
	}

	/* Reserve memory */
#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
	if(strncmp(name, "fakemem", 7) == 0)
	{
		printk("\033[31mFunction = %s, Line = %d, base_bus_addr %pa is not valid, fakemem passing reserving memory_block\033[m\n", __PRETTY_FUNCTION__, __LINE__, &base);
		goto pass_reserve;
	}
#endif

	if (fixed) {
		if (memblock_is_region_reserved(base, size) ||
		    memblock_reserve(base, size) < 0) {
			ret = -EBUSY;
			goto err;
		}
	} else {
		phys_addr_t addr = 0;

		/*
		 * All pages in the reserved area must come from the same zone.
		 * If the requested region crosses the low/high memory boundary,
		 * try allocating from high memory first and fall back to low
		 * memory in case of failure.
		 */
		if (base < highmem_start && limit > highmem_start) {
			addr = memblock_alloc_range(size, alignment,
						    highmem_start, limit,
						    MEMBLOCK_NONE);
			limit = highmem_start;
		}

		if (!addr) {
			addr = memblock_alloc_range(size, alignment, base,
						    limit,
						    MEMBLOCK_NONE);
			if (!addr) {
				ret = -ENOMEM;
				goto err;
			}
		}

		/*
		 * kmemleak scans/reads tracked objects for pointers to other
		 * objects but this address isn't mapped and accessible
		 */
		kmemleak_ignore_phys(addr);
		base = addr;
	}

#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
pass_reserve:
#endif
	ret = cma_init_reserved_mem(base, size, order_per_bit, name, res_cma);
	if (ret)
		goto free_mem;

#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
	pr_info("%s %ld MiB at %pa\n\n", (strncmp(name, "fakemem", 7) == 0)? "Not Reserved":"Reserved", (unsigned long)size / SZ_1M,
#else
	pr_info("Reserved %ld MiB at %pa\n", (unsigned long)size / SZ_1M,
#endif
		&base);
	return 0;

free_mem:
	memblock_free(base, size);
err:
	pr_err("Failed to reserve %ld MiB\n", (unsigned long)size / SZ_1M);
	return ret;
}

#ifdef CONFIG_CMA_DEBUG
static void cma_debug_show_areas(struct cma *cma)
{
	unsigned long next_zero_bit, next_set_bit, nr_zero;
	unsigned long start = 0;
	unsigned long nr_part, nr_total = 0;
	unsigned long nbits = cma_bitmap_maxno(cma);

	mutex_lock(&cma->lock);
	pr_info("number of available pages: ");
	for (;;) {
		next_zero_bit = find_next_zero_bit(cma->bitmap, nbits, start);
		if (next_zero_bit >= nbits)
			break;
		next_set_bit = find_next_bit(cma->bitmap, nbits, next_zero_bit);
		nr_zero = next_set_bit - next_zero_bit;
		nr_part = nr_zero << cma->order_per_bit;
		pr_cont("%s%lu@%lu", nr_total ? "+" : "", nr_part,
			next_zero_bit);
		nr_total += nr_part;
		start = next_zero_bit + nr_zero;
	}
	pr_cont("=> %lu free of %lu total pages\n", nr_total, cma->count);
	mutex_unlock(&cma->lock);
}
#else
static inline void cma_debug_show_areas(struct cma *cma) { }
#endif

#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
static int count_cma_area_free_page_num(struct cma *counted_cma)
{
	int count_cma_free_page_count       = 0;
	int count_cma_free_start			= 0;
	int count_cma_bitmap_start_zero		= 0;
	int count_cma_bitmap_end_zero		= 0;

	int debug = 0;

	printk(CMA_DEBUG "\033[32mcma_area having \033[m");
	for(;;)
	{
		count_cma_bitmap_start_zero = find_next_zero_bit(counted_cma->bitmap, counted_cma->count, count_cma_free_start);

		if(count_cma_bitmap_start_zero >= counted_cma->count)
			break;
		if(debug)
			printk(CMA_DEBUG "######### count_cma_bitmap_start_zero=%x \n",count_cma_bitmap_start_zero);
		count_cma_free_start = count_cma_bitmap_start_zero + 1;

		count_cma_bitmap_end_zero = find_next_bit(counted_cma->bitmap, counted_cma->count, count_cma_free_start);

		if(debug)
			printk(CMA_DEBUG "######### count_cma_bitmap_end_zero=%x \n",count_cma_bitmap_end_zero);

		if(count_cma_bitmap_end_zero >= counted_cma->count)
		{
			count_cma_free_page_count += (counted_cma->count - count_cma_bitmap_start_zero);
			break;
		}

		count_cma_free_page_count += (count_cma_bitmap_end_zero - count_cma_bitmap_start_zero);

		count_cma_free_start = count_cma_bitmap_end_zero + 1;

		if(count_cma_free_start >= counted_cma->count)
			break;
	}
	printk(CMA_DEBUG "\033[32m%d free pages\033[m\n", count_cma_free_page_count);

	return count_cma_free_page_count;
}

/*
 * bitmap_find_first_zero_area - find the first contiguous aligned zero area
 * @map: The address to base the search on
 * @size: The bitmap size in bits
 * @start: The bitnumber to start searching at
 * @nr:The max length we want, if nr < 0, the max length is not limited
 * @len: The length of the first zero area
 * @align_mask: Alignment mask for zero area
 */
static unsigned long bitmap_find_first_zero_area(unsigned long *map,
                     unsigned long size,
                     unsigned long start,
                     unsigned long nr,
                     unsigned long *len,
                     unsigned long align_mask)
{
    unsigned long index, end;

    index = find_next_zero_bit(map, size, start);

    /* Align allocation */
    index = __ALIGN_MASK(index, align_mask);

    if(nr < 0)
        end = size;
    else{
        end = index + nr;
        if(end > size)
            end = size;
    }
    end = find_next_bit(map, end, index);

    *len = end - index;

    return index;
}

struct page *dma_alloc_from_contiguous_direct(struct device *dev, int count,
                       unsigned int align, long *retlen)
{
    unsigned long mask, pfn, pageno, start = 0, len = 0;
    struct cma *cma = dev_get_cma_area(dev);
    struct page *page = NULL;
    int ret;

    if (!cma || !cma->count)
        return NULL;

    if (align > CONFIG_CMA_ALIGNMENT){
        // if size > 2^8 pages ==> align will be > 8
        align = CONFIG_CMA_ALIGNMENT;
    }

    //printk("\033[31m%s(alloc %d pages, align %d)\033[m\n", __func__, count, align);

    if (!count)
        return NULL;

    mask = (1 << align) - 1;

    if(count >= 30)
    {
        printk(CMA_DEBUG "\033[32m[%s] Before %s \033[m", current->comm, __PRETTY_FUNCTION__);
        count_cma_area_free_page_num(cma);
    }

    for (;;) {
        mutex_lock(&cma->lock);
        // bitmap_find_next_zero_area will return an index which points a zero-bit in bitmap, whose following "count" bits are all zero-bit(from pageno to pageno+count are all free pages)
        pageno = bitmap_find_first_zero_area(cma->bitmap, cma->count,
                            start, count, &len, mask);
        if (pageno >= cma->count){
            mutex_unlock(&cma->lock);
            break;
        }
        bitmap_set(cma->bitmap, pageno, len);
        mutex_unlock(&cma->lock);
        /*
        * It's safe to drop the lock here. We've marked this region for
        * our exclusive use. If the migration fails we will take the
        * lock again and unmark it.
        */

        pfn = cma->base_pfn + pageno;
        mutex_lock(&cma_mutex);
	/* FIXME: GFP_KERNEL */
        ret = alloc_contig_range(pfn, pfn + len, MIGRATE_CMA, GFP_KERNEL);
        mutex_unlock(&cma_mutex);
        if (ret == 0) {
            page = pfn_to_page(pfn);
#ifdef CONFIG_MP_CMA_PATCH_CMA_AGGRESSIVE_ALLOC
	    adjust_managed_cma_page_count(page_zone(page), -len);
#endif
            break;
        } else if (ret != -EBUSY) {
            printk(CMA_ERR "[xiaohui] %s: alloc_contig_range fail\n", __func__);
			cma_clear_bitmap(cma, pfn, len);
            break;
        }
		cma_clear_bitmap(cma, pfn, len);
        pr_info("%s(): memory range at %p is busy, retrying,%d, %d, %s\n",
             __func__, pfn_to_page(pfn),current->pid, current->tgid, current->comm);
        /* try again with a bit different memory target */
        start = pageno + mask + 1;
    }

    if(count >= 30)
    {
        printk(CMA_DEBUG "\033[32m[%s] After %s \033[m", current->comm, __PRETTY_FUNCTION__);
        count_cma_area_free_page_num(cma);
    }

    *retlen = len;
    return page;
}

struct page *dma_alloc_at_from_contiguous(struct device *dev, int count,
				       unsigned int align, phys_addr_t at_addr)
{
	unsigned long mask, pfn, pageno, start = 0;
	struct cma *cma = dev_get_cma_area(dev);
	struct page *page = NULL;
	int ret;
	unsigned long start_pfn = __phys_to_pfn(at_addr);

#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
	s64 cma_alloc_time_cost_ms = 0;
	struct cma_measurement *cma_measurement_data = NULL;
#endif

	if (!cma || !cma->count)
		return NULL;

	if (align > CONFIG_CMA_ALIGNMENT)
	{
		// if size > 2^8 pages ==> align will be > 8
		align = CONFIG_CMA_ALIGNMENT;
	}

	pr_debug("%s(cma %p, count %d, align %d)\n", __func__, (void *)cma,
		 count, align);

	if (!count)
		return NULL;

	mask = (1 << align) - 1;

	if (start_pfn && start_pfn < cma->base_pfn)
		return NULL;
	start = start_pfn ? start_pfn - cma->base_pfn : start;		// if having start_pfn, start = start_pfn - cma->base_pfn

	if(count >= 10)
	{
		printk(CMA_DEBUG "\033[31m%s(alloc %d pages, at_addr %pa, start pfn 0x%lX)\033[m\n", __func__, count, &at_addr, start_pfn);
		printk(CMA_DEBUG "\033[35mFunction = %s, Line = %d, find bit_map from 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, start);
		printk(CMA_DEBUG "\033[32m[%s] Before %s \033[m", current->comm, __PRETTY_FUNCTION__);
		count_cma_area_free_page_num(cma);
	}
	for (;;) {
		unsigned long timeout;
#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
		ktime_t start_time;
		cma_alloc_time_cost_ms = 0;
#endif
		mutex_lock(&cma->lock);
		timeout = jiffies + msecs_to_jiffies(1000);

		// bitmap_find_next_zero_area will return an index which points a zero-bit in bitmap, whose following "count" bits are all zero-bit(from pageno to pageno+count are all free pages)
		pageno = bitmap_find_next_zero_area(cma->bitmap, cma->count,
						    start, count, mask);
#ifdef CONFIG_MP_CMA_PATCH_FORCE_ALLOC_START_ADDR
		if (pageno >= cma->count || (start_pfn && start != pageno))
#else
		if (pageno >= cma->count || (start && start != pageno))
#endif
		{	// no such continuous area
			mutex_unlock(&cma->lock);
			break;
		}

		bitmap_set(cma->bitmap, pageno, count);
        /*
		 * It's safe to drop the lock here. We've marked this region for
		 * our exclusive use. If the migration fails we will take the
		 * lock again and unmark it.
		 */
		mutex_unlock(&cma->lock);

		pfn = cma->base_pfn + pageno;
retry:
        mutex_lock(&cma_mutex);
#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
		start_time = ktime_get_real();
#endif
		/* FIXME: GFP_KERNEL */
		ret = alloc_contig_range(pfn, pfn + count, MIGRATE_CMA, GFP_KERNEL);
#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
		if(cma != dma_contiguous_default_area)
		{
			cma_measurement_data = cma->cma_measurement_ptr;
			cma_alloc_time_cost_ms = Show_Diff_Time("alloc_contig_range", start_time, 0);

			mutex_lock(&cma_measurement_data->cma_measurement_lock);
			cma_measurement_data->total_migration_time_cost_ms += cma_alloc_time_cost_ms;
			mutex_unlock(&cma_measurement_data->cma_measurement_lock);
		}
#endif
		mutex_unlock(&cma_mutex);
		if (ret == 0) {
			page = pfn_to_page(pfn);
#ifdef CONFIG_MP_CMA_PATCH_CMA_AGGRESSIVE_ALLOC
			adjust_managed_cma_page_count(page_zone(page), -count);
#endif

			break;
#ifdef CONFIG_MP_CMA_PATCH_FORCE_ALLOC_START_ADDR
		} else if (start_pfn && time_before(jiffies, timeout)) {
#else
		} else if (start && time_before(jiffies, timeout)) {
#endif
			cond_resched();
			invalidate_bh_lrus();
			goto retry;
#ifdef CONFIG_MP_CMA_PATCH_FORCE_ALLOC_START_ADDR
		} else if (ret != -EBUSY || start_pfn) {
#else
		} else if (ret != -EBUSY || start) {
#endif
			printk(KERN_EMERG "\033[35mFunction = %s, Line = %d, cannot get cma_memory, ret is %d, start_pfn is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, ret, start_pfn);
			printk(KERN_EMERG "\033[35mFunction = %s, Line = %d, cannot get cma_memory, ret is %d, start_pfn is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, ret, start_pfn);
			cma_clear_bitmap(cma, pfn, count);
			break;
		}
		cma_clear_bitmap(cma, pfn, count);

		printk(CMA_DEBUG "\033[35mFunction = %s, Line = %d, start is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, start);
		printk(CMA_DEBUG "\033[35mFunction = %s, Line = %d, pageno is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, pageno);
		printk(CMA_DEBUG "\033[35mFunction = %s, Line = %d, pfn is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, pfn);

		pr_info("%s(): memory range at 0x%lX is busy, retrying\n",
			 __func__, pfn << PAGE_SHIFT);
		/* try again with a bit different memory target */

#ifdef CONFIG_MP_CMA_PATCH_FORCE_ALLOC_START_ADDR
		if(start_pfn)
		{
			printk(CMA_ERR "\033[35mFunction = %s, Line = %d, do not change start from 0x%lX to 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, start, (pageno + mask + 1));
		}
		else
		{
			printk(CMA_ERR "\033[35mFunction = %s, Line = %d, change start from 0x%lX to 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, start, (pageno + mask + 1));
			start = pageno + mask + 1;
		}
#else
		start = pageno + mask + 1;
#endif
	}
	pr_debug("%s(): returned %p\n", __func__, page);

	if(count >= 10)
	{
		printk(CMA_DEBUG "\033[32m[%s] After %s \033[m", current->comm, __PRETTY_FUNCTION__);
		count_cma_area_free_page_num(cma);
	}

	return page;
}

#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
struct page *dma_alloc_from_fake_memory(struct device *dev, int count,
                       unsigned int align, long *retlen)
{
	unsigned long mask, pfn, pageno, start = 0;
	struct cma *cma = dev_get_cma_area(dev);
	struct page *page = NULL;
	int ret;

	if (!cma || !cma->count)
		return NULL;

	if(!(cma->flags & CMA_FAKEMEM))
	{
		printk(KERN_EMERG "\033[31mFunction = %s, Line = %d, you are using real_cma_heap for fake cma allocation!!\033[m\n", __PRETTY_FUNCTION__, __LINE__);
		return false;
	}

	if (align > CONFIG_CMA_ALIGNMENT){
		// if size > 2^8 pages ==> align will be > 8
		align = CONFIG_CMA_ALIGNMENT;
	}

	if (!count) {
		printk("%s:%d error: count is zero.\n", __func__, __LINE__);
		return NULL;
	}

	mask = (1 << align) - 1;

	if(count >= 30)
	{
		printk(CMA_DEBUG "\033[32m[%s] Before %s \033[m", current->comm, __PRETTY_FUNCTION__);
		count_cma_area_free_page_num(cma);
	}

	for (;;) {
		mutex_lock(&cma->lock);
		pageno = bitmap_find_next_zero_area(cma->bitmap, cma->count,
							start, count, mask);

		if (pageno >= cma->count){
			mutex_unlock(&cma->lock);
			break;
		}

		bitmap_set(cma->bitmap, pageno, count);
		mutex_unlock(&cma->lock);

		pfn = cma->base_pfn + pageno;
		page = pfn_to_page(pfn);
		break;
	}

	if(count >= 30)
	{
		printk(CMA_DEBUG "\033[32m[%s] After %s \033[m", current->comm, __PRETTY_FUNCTION__);
		count_cma_area_free_page_num(cma);
	}

	*retlen = count << PAGE_SHIFT;
	return page;
}

bool dma_release_from_fake_memory(struct device *dev, const struct page *pages, unsigned int count)
{
	unsigned long pfn;
	struct cma *cma = dev_get_cma_area(dev);

	if (!cma || !pages)
		return false;

	if(!(cma->flags & CMA_FAKEMEM))
	{
		printk(KERN_EMERG "\033[31mFunction = %s, Line = %d, you are using real_cma_heap for fake cma free!!\033[m\n", __PRETTY_FUNCTION__, __LINE__);
		return false;
	}

	pr_debug("%s(page %p) count=%#x\n", __func__, (void *)pages, count);

	pfn = page_to_pfn(pages);

	if (pfn < cma->base_pfn || pfn >= cma->base_pfn + cma->count)
		return false;

	VM_BUG_ON(pfn + count > cma->base_pfn + cma->count);

	if(count >= 30)
	{
		printk(CMA_DEBUG "\033[32m[%s] Before %s \033[m", current->comm, __PRETTY_FUNCTION__);
		count_cma_area_free_page_num(cma);
	}
	cma_clear_bitmap(cma, pfn, count);
	if(count >= 30)
	{
		printk(CMA_DEBUG "\033[32m[%s] After %s \033[m", current->comm, __PRETTY_FUNCTION__);
		count_cma_area_free_page_num(cma);
	}

	return true;
}
#endif
#endif

#ifdef CONFIG_MP_MMA_CMA_ENABLE
struct page *dma_alloc_at_from_contiguous_from_high_to_low(struct device *dev, int count,
					unsigned int align, phys_addr_t at_addr)
{
	unsigned long mask, pfn, pageno, start = 0, cma_bitmap_end =0;
	struct cma *cma = dev_get_cma_area(dev);
	struct page *page = NULL;
	int ret;
	unsigned long start_pfn = __phys_to_pfn(at_addr);
#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
	struct cma_measurement *cma_measurement_data = NULL;
#endif

	if (!cma || !cma->count)
		return NULL;
	if (align > CONFIG_CMA_ALIGNMENT)
	{
		// if size > 2^8 pages ==> align will be > 8
		align = CONFIG_CMA_ALIGNMENT;
	}

	printk(KERN_ERR "%s(cma %p, alloc %d pages, align %d)\n", __func__, (void *)cma, count, align);

	if (!count)
		return NULL;

	mask = (1 << align) - 1;

	if (start_pfn && start_pfn < cma->base_pfn)
		return NULL;
	start = start_pfn ? start_pfn - cma->base_pfn : start;		// if having start_pfn, start = start_pfn - cma->base_pfn

	if(count >= 10)
	{
		printk(CMA_DEBUG "\033[31m%s(alloc %d pages, at_addr 0x%X, start pfn 0x%X)\033[m\n", __func__, count, (unsigned int)at_addr, (unsigned int)start_pfn);
		printk(CMA_DEBUG "\033[35mFunction = %s, Line = %d, find bit_map from 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, start);
		printk(CMA_DEBUG "\033[32m[%s] Before %s \033[m", current->comm, __PRETTY_FUNCTION__);
		count_cma_area_free_page_num(cma);
	}

	for (;;) {
		unsigned long timeout;
		mutex_lock(&cma->lock);
		timeout = jiffies + msecs_to_jiffies(1000);

#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
		ktime_t start_time;
		s64 cma_alloc_time_cost_ms = 0;
#endif

refind:
		// bitmap_find_next_zero_area will return an index which points a zero-bit in bitmap, whose following "count" bits are all zero-bit(from pageno to pageno+count are all free pages)
		if(!cma_bitmap_end)
                    cma_bitmap_end = cma->count;
		pageno = bitmap_find_next_zero_area_from_high_to_low(cma->bitmap, cma_bitmap_end,
							start, count, mask);
#ifdef CONFIG_MP_CMA_PATCH_FORCE_ALLOC_START_ADDR
		if (pageno >= cma->count || (start_pfn && start != pageno)){	// no such continuous area
#else
		if (pageno >= cma->count || (start && start != pageno)){	// no such continuous area
#endif

			mutex_unlock(&cma->lock);
			break;
		}

		bitmap_set(cma->bitmap, pageno, count);
		/*
		 * It's safe to drop the lock here. We've marked this region for
		 * our exclusive use. If the migration fails we will take the
		 * lock again and unmark it.
		 */

		mutex_unlock(&cma->lock);

		pfn = cma->base_pfn + pageno;
retry:
		mutex_lock(&cma_mutex);
#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
		start_time = ktime_get_real();
#endif
		ret = alloc_contig_range(pfn, pfn + count, MIGRATE_CMA);
#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
		if(cma != dma_contiguous_default_area)
		{
			cma_measurement_data = cma->cma_measurement_ptr;
			cma_alloc_time_cost_ms = Show_Diff_Time("alloc_contig_range", start_time, 0);

			mutex_lock(&cma_measurement_data->cma_measurement_lock);
			cma_measurement_data->total_migration_time_cost_ms += cma_alloc_time_cost_ms;
			mutex_unlock(&cma_measurement_data->cma_measurement_lock);
		}
#endif
		mutex_unlock(&cma_mutex);
		if (ret == 0) {
			page = pfn_to_page(pfn);
#ifdef CONFIG_MP_CMA_PATCH_CMA_AGGRESSIVE_ALLOC
			adjust_managed_cma_page_count(page_zone(page), -count);
#endif

			break;
#ifdef CONFIG_MP_CMA_PATCH_FORCE_ALLOC_START_ADDR
		} else if (start_pfn && time_before(jiffies, timeout)) {
#else
		} else if (start && time_before(jiffies, timeout)) {
#endif
			cond_resched();
			invalidate_bh_lrus();
			goto retry;
#ifdef CONFIG_MP_CMA_PATCH_FORCE_ALLOC_START_ADDR
		} else if (ret != -EBUSY || start_pfn) {
#else
		} else if (ret != -EBUSY || start) {
#endif
			printk(KERN_EMERG "\033[35mFunction = %s, Line = %d, cannot get cma_memory, ret is %d, start_pfn is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, ret, start_pfn);
			printk(KERN_EMERG "\033[35mFunction = %s, Line = %d, cannot get cma_memory, ret is %d, start_pfn is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, ret, start_pfn);
			cma_clear_bitmap(cma, pfn, count);
			break;
		}
		cma_clear_bitmap(cma, pfn, count);

		printk(CMA_DEBUG "\033[35mFunction = %s, Line = %d, start is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, start);
		printk(CMA_DEBUG "\033[35mFunction = %s, Line = %d, pageno is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, pageno);
		printk(CMA_DEBUG "\033[35mFunction = %s, Line = %d, pfn is 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, pfn);

		pr_info("%s(): memory range at 0x%lX is busy, retrying\n",
			 __func__, pfn << PAGE_SHIFT);
		/* try again with a bit different memory target */

#ifdef CONFIG_MP_CMA_PATCH_FORCE_ALLOC_START_ADDR
		if(start_pfn)
		{
			printk(CMA_DEBUG "\033[35mFunction = %s, Line = %d, do not change start from 0x%lX to 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, start, (pageno + mask + 1));
		}
		else
		{
			printk(CMA_DEBUG "\033[35mFunction = %s, Line = %d, change start from 0x%lX to 0x%lX\033[m\n", __PRETTY_FUNCTION__, __LINE__, start, (pageno + mask + 1));
			//start = pageno + mask + 1;
                        cma_bitmap_end = cma_bitmap_end -mask -1;
		}
#else
		//start = pageno + mask + 1;
                cma_bitmap_end = cma_bitmap_end -mask -1;
#endif
	}

	pr_debug("%s(): returned %p\n", __func__, page);


	if(count >= 10)
	{
		if(page)
			printk(CMA_DEBUG "\033[32mAlloc Bus_Addr at 0x%X\033[m\n", (unsigned int)(pfn << PAGE_SHIFT));

		printk(CMA_DEBUG "\033[32m[%s] After %s \033[m", current->comm, __PRETTY_FUNCTION__);
		count_cma_area_free_page_num(cma);
	}

	return page;
}
#endif


/**
 * cma_alloc() - allocate pages from contiguous area
 * @cma:   Contiguous memory region for which the allocation is performed.
 * @count: Requested number of pages.
 * @align: Requested alignment of pages (in PAGE_SIZE order).
 * @no_warn: Avoid printing message about failed allocation
 *
 * This function allocates part of contiguous memory on specific
 * contiguous memory area.
 */
struct page *cma_alloc(struct cma *cma, size_t count, unsigned int align,
		       bool no_warn)
{
	unsigned long mask, offset;
	unsigned long pfn = -1;
	unsigned long start = 0;
	unsigned long bitmap_maxno, bitmap_no, bitmap_count;
	size_t i;
	struct page *page = NULL;
	int ret = -ENOMEM;

	if (!cma || !cma->count)
		return NULL;

	pr_debug("%s(cma %p, count %zu, align %d)\n", __func__, (void *)cma,
		 count, align);

	if (!count)
		return NULL;

	mask = cma_bitmap_aligned_mask(cma, align);
	offset = cma_bitmap_aligned_offset(cma, align);
	bitmap_maxno = cma_bitmap_maxno(cma);
	bitmap_count = cma_bitmap_pages_to_bits(cma, count);

	if (bitmap_count > bitmap_maxno)
		return NULL;

	for (;;) {
		mutex_lock(&cma->lock);
		bitmap_no = bitmap_find_next_zero_area_off(cma->bitmap,
				bitmap_maxno, start, bitmap_count, mask,
				offset);
		if (bitmap_no >= bitmap_maxno) {
			mutex_unlock(&cma->lock);
			break;
		}
		bitmap_set(cma->bitmap, bitmap_no, bitmap_count);
		/*
		 * It's safe to drop the lock here. We've marked this region for
		 * our exclusive use. If the migration fails we will take the
		 * lock again and unmark it.
		 */
		mutex_unlock(&cma->lock);

		pfn = cma->base_pfn + (bitmap_no << cma->order_per_bit);
		mutex_lock(&cma_mutex);
		ret = alloc_contig_range(pfn, pfn + count, MIGRATE_CMA,
				     GFP_KERNEL | (no_warn ? __GFP_NOWARN : 0));
		mutex_unlock(&cma_mutex);
		if (ret == 0) {
			page = pfn_to_page(pfn);
#ifdef CONFIG_MP_CMA_PATCH_CMA_AGGRESSIVE_ALLOC
			adjust_managed_cma_page_count(page_zone(page), -count);
#endif

			break;
		}

		cma_clear_bitmap(cma, pfn, count);
		if (ret != -EBUSY)
			break;

		pr_debug("%s(): memory range at %p is busy, retrying\n",
			 __func__, pfn_to_page(pfn));
		/* try again with a bit different memory target */
		start = bitmap_no + mask + 1;
	}

	trace_cma_alloc(pfn, page, count, align);

	/*
	 * CMA can allocate multiple page blocks, which results in different
	 * blocks being marked with different tags. Reset the tags to ignore
	 * those page blocks.
	 */
	if (page) {
		for (i = 0; i < count; i++)
			page_kasan_tag_reset(page + i);
	}

	if (ret && !no_warn) {
		pr_err("%s: alloc failed, req-size: %zu pages, ret: %d\n",
			__func__, count, ret);
		cma_debug_show_areas(cma);
	}

	pr_debug("%s(): returned %p\n", __func__, page);
	return page;
}
EXPORT_SYMBOL_GPL(cma_alloc);

/**
 * cma_release() - release allocated pages
 * @cma:   Contiguous memory region for which the allocation is performed.
 * @pages: Allocated pages.
 * @count: Number of allocated pages.
 *
 * This function releases memory allocated by alloc_cma().
 * It returns false when provided pages do not belong to contiguous area and
 * true otherwise.
 */
bool cma_release(struct cma *cma, const struct page *pages, unsigned int count)
{
	unsigned long pfn;

	if (!cma || !pages)
		return false;

	pr_debug("%s(page %p)\n", __func__, (void *)pages);

	pfn = page_to_pfn(pages);

	if (pfn < cma->base_pfn || pfn >= cma->base_pfn + cma->count)
		return false;
	VM_BUG_ON(pfn + count > cma->base_pfn + cma->count);
	free_contig_range(pfn, count);
	cma_clear_bitmap(cma, pfn, count);
	trace_cma_release(pfn, pages, count);

#ifdef CONFIG_MP_CMA_PATCH_CMA_AGGRESSIVE_ALLOC
	adjust_managed_cma_page_count(page_zone(pages), count);
#endif

	return true;
}
EXPORT_SYMBOL_GPL(cma_release);

int cma_for_each_area(int (*it)(struct cma *cma, void *data), void *data)
{
	int i;

	for (i = 0; i < cma_area_count; i++) {
		int ret = it(&cma_areas[i], data);

		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cma_for_each_area);
