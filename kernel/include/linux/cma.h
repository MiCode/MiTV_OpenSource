/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __CMA_H__
#define __CMA_H__

#include <linux/init.h>
#include <linux/types.h>

/*
 * There is always at least global CMA area and a few optional
 * areas configured in kernel .config.
 */
#ifdef CONFIG_CMA_AREAS
#define MAX_CMA_AREAS	(1 + CONFIG_CMA_AREAS)

#else
#define MAX_CMA_AREAS	(0)

#endif

#if defined(CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER)
#define CMA_HEAP_MIUOFFSET_NOCARE (-1UL)
#endif

#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
#define CMA_HEAP_NAME_LENG  32

struct CMA_BootArgs_Config {
	int miu;
	int heap_type;
	int pool_id;
	unsigned long start;  //for boot args this is miu offset
	unsigned long size;
	char name[CMA_HEAP_NAME_LENG];
};
#endif

#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
#include <linux/device.h>
extern struct page *dma_alloc_at_from_contiguous(struct device *dev, int count,
					unsigned int align, phys_addr_t at_addr);

extern struct page *dma_alloc_from_contiguous_direct(struct device *dev, int count,
					unsigned int align, long *retlen);

#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
extern struct page * dma_alloc_from_fake_memory(struct device *dev, int count,
					unsigned int align, long *retlen);
extern bool dma_release_from_fake_memory(struct device *dev, const struct page *pages, unsigned int count);
#endif
#ifdef CONFIG_MP_MMA_CMA_ENABLE
extern struct page *dma_alloc_at_from_contiguous_from_high_to_low(struct device *dev, int count,
				       unsigned int order, phys_addr_t at_addr);
#endif
#endif

struct cma;

extern unsigned long totalcma_pages;
extern phys_addr_t cma_get_base(const struct cma *cma);
extern unsigned long cma_get_size(const struct cma *cma);
extern const char *cma_get_name(const struct cma *cma);

extern int __init cma_declare_contiguous(phys_addr_t base,
			phys_addr_t size, phys_addr_t limit,
			phys_addr_t alignment, unsigned int order_per_bit,
			bool fixed, const char *name, struct cma **res_cma);
extern int cma_init_reserved_mem(phys_addr_t base, phys_addr_t size,
					unsigned int order_per_bit,
					const char *name,
					struct cma **res_cma);
extern struct page *cma_alloc(struct cma *cma, size_t count, unsigned int align,
			      bool no_warn);
extern bool cma_release(struct cma *cma, const struct page *pages, unsigned int count);

extern int cma_for_each_area(int (*it)(struct cma *cma, void *data), void *data);
#endif
