/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MM_CMA_H__
#define __MM_CMA_H__

#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
#define CMA_FAKEMEM 0x01
#endif

struct cma {
	unsigned long   base_pfn;
	unsigned long   count;
	unsigned long   *bitmap;
	unsigned int order_per_bit; /* Order of pages represented by one bit */
	struct mutex    lock;
#ifdef CONFIG_CMA_DEBUGFS
	struct hlist_head mem_head;
	spinlock_t mem_head_lock;
#endif
	const char *name;
#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
	struct cma_measurement *cma_measurement_ptr;
#endif

#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
	unsigned int flags;				// cma area flags, example:CMA_FAKEMEM
#endif
};

#if defined(CONFIG_MP_CMA_PATCH_COUNT_TIMECOST)
# define CMA_HEAP_MEASUREMENT_LENG 96
#endif

#ifdef CONFIG_MP_CMA_PATCH_COUNT_TIMECOST
struct cma_measurement {
	const char *cma_heap_name;
	unsigned int cma_heap_id;
	struct mutex cma_measurement_lock;

	/* Measure Node Start */
	unsigned long total_alloc_size_kb;
	unsigned long total_alloc_time_cost_ms;

	unsigned long total_migration_size_kb;
	unsigned long total_migration_time_cost_ms;
	/* Measure Node End */

	/* Reset Node Start */
	unsigned long cma_measurement_reset;
	/* Reset Node End */
};
#endif

extern struct cma cma_areas[MAX_CMA_AREAS];
extern unsigned cma_area_count;

static inline unsigned long cma_bitmap_maxno(struct cma *cma)
{
	return cma->count >> cma->order_per_bit;
}

#endif
