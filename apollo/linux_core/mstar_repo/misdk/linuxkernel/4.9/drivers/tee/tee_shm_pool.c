/*
 * Copyright (c) 2015, Linaro Limited
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
#include <linux/dma-buf.h>
#include <linux/genalloc.h>
#include <linux/slab.h>
#include <linux/tee_drv.h>
#include "tee_private.h"

static int pool_op_gen_alloc(struct tee_shm_pool_mgr *poolm,
			     struct tee_shm *shm, size_t size)
{
	unsigned long va;
	struct gen_pool *genpool = poolm->private_data;
	size_t s = roundup(size, 1 << genpool->min_alloc_order);

	va = gen_pool_alloc(genpool, s);
	if (!va)
		return -ENOMEM;

	memset((void *)va, 0, s);
	shm->kaddr = (void *)va;
	shm->paddr = gen_pool_virt_to_phys(genpool, va);
	shm->size = s;
	return 0;
}

static void pool_op_gen_free(struct tee_shm_pool_mgr *poolm,
			     struct tee_shm *shm)
{
	gen_pool_free(poolm->private_data, (unsigned long)shm->kaddr,
		      shm->size);
	shm->kaddr = NULL;
}

#if defined(CONFIG_TEE_3_2)
static void pool_op_gen_destroy_poolmgr(struct tee_shm_pool_mgr *poolm)
{
        gen_pool_destroy(poolm->private_data);
        kfree(poolm);
}
#endif

static const struct tee_shm_pool_mgr_ops pool_ops_generic = {
	.alloc = pool_op_gen_alloc,
	.free = pool_op_gen_free,
#if defined(CONFIG_TEE_3_2)
        .destroy_poolmgr = pool_op_gen_destroy_poolmgr,
#endif
};

#if !defined(CONFIG_TEE_3_2)
static void pool_res_mem_destroy(struct tee_shm_pool *pool)
{
	gen_pool_destroy(pool->private_mgr.private_data);
	gen_pool_destroy(pool->dma_buf_mgr.private_data);
}

static int pool_res_mem_mgr_init(struct tee_shm_pool_mgr *mgr,
				 struct tee_shm_pool_mem_info *info,
				 int min_alloc_order)
{
	size_t page_mask = PAGE_SIZE - 1;
	struct gen_pool *genpool = NULL;
	int rc;

	/*
	 * Start and end must be page aligned
	 */
	if ((info->vaddr & page_mask) || (info->paddr & page_mask) ||
	    (info->size & page_mask))
		return -EINVAL;

	genpool = gen_pool_create(min_alloc_order, -1);
	if (!genpool)
		return -ENOMEM;

	gen_pool_set_algo(genpool, gen_pool_best_fit, NULL);
	rc = gen_pool_add_virt(genpool, info->vaddr, info->paddr, info->size,
			       -1);
	if (rc) {
		gen_pool_destroy(genpool);
		return rc;
	}

	mgr->private_data = genpool;
	mgr->ops = &pool_ops_generic;
	return 0;
}
#endif

/**
 * tee_shm_pool_alloc_res_mem() - Create a shared memory pool from reserved
 * memory range
 * @priv_info:	Information for driver private shared memory pool
 * @dmabuf_info: Information for dma-buf shared memory pool
 *
 * Start and end of pools will must be page aligned.
 *
 * Allocation with the flag TEE_SHM_DMA_BUF set will use the range supplied
 * in @dmabuf, others will use the range provided by @priv.
 *
 * @returns pointer to a 'struct tee_shm_pool' or an ERR_PTR on failure.
 */
struct tee_shm_pool *
tee_shm_pool_alloc_res_mem(struct tee_shm_pool_mem_info *priv_info,
			   struct tee_shm_pool_mem_info *dmabuf_info)
{
#if !defined(CONFIG_TEE_3_2)
	struct tee_shm_pool *pool = NULL;
	int ret;

	pool = kzalloc(sizeof(*pool), GFP_KERNEL);
	if (!pool) {
		ret = -ENOMEM;
		goto err;
	}
#else
        struct tee_shm_pool_mgr *priv_mgr;
        struct tee_shm_pool_mgr *dmabuf_mgr;
        void *rc;
#endif
	/*
	 * Create the pool for driver private shared memory
	 */
#if !defined(CONFIG_TEE_3_2)
	ret = pool_res_mem_mgr_init(&pool->private_mgr, priv_info,
				    3 /* 8 byte aligned */);
	if (ret)
		goto err;
#else
        rc = tee_shm_pool_mgr_alloc_res_mem(priv_info->vaddr, priv_info->paddr,
                                            priv_info->size,
                                            3 /* 8 byte aligned */);
        if (IS_ERR(rc))
                return rc;
        priv_mgr = rc;
#endif
	/*
	 * Create the pool for dma_buf shared memory
	 */
#if !defined(CONFIG_TEE_3_2)
	ret = pool_res_mem_mgr_init(&pool->dma_buf_mgr, dmabuf_info,
				    PAGE_SHIFT);
	if (ret)
		goto err;
#else
        rc = tee_shm_pool_mgr_alloc_res_mem(dmabuf_info->vaddr,
                                            dmabuf_info->paddr,
                                            dmabuf_info->size, PAGE_SHIFT);
        if (IS_ERR(rc))
                goto err_free_priv_mgr;
        dmabuf_mgr = rc;

        rc = tee_shm_pool_alloc(priv_mgr, dmabuf_mgr);
        if (IS_ERR(rc))
                goto err_free_dmabuf_mgr;

        return rc;

 err_free_dmabuf_mgr:
        tee_shm_pool_mgr_destroy(dmabuf_mgr);
err_free_priv_mgr:
        tee_shm_pool_mgr_destroy(priv_mgr);

        return rc;
}
EXPORT_SYMBOL_GPL(tee_shm_pool_alloc_res_mem);

 struct tee_shm_pool_mgr *tee_shm_pool_mgr_alloc_res_mem(unsigned long vaddr,
                                                        phys_addr_t paddr,
                                                        size_t size,
                                                        int min_alloc_order)
{
        const size_t page_mask = PAGE_SIZE - 1;
        struct tee_shm_pool_mgr *mgr;
        int rc;

        /* Start and end must be page aligned */
        if (vaddr & page_mask || paddr & page_mask || size & page_mask)
                return ERR_PTR(-EINVAL);

        mgr = kzalloc(sizeof(*mgr), GFP_KERNEL);
        if (!mgr)
                return ERR_PTR(-ENOMEM);

        mgr->private_data = gen_pool_create(min_alloc_order, -1);
        if (!mgr->private_data) {
                rc = -ENOMEM;
                goto err;
        }
#endif
#if !defined(CONFIG_TEE_3_2)
	pool->destroy = pool_res_mem_destroy;
	return pool;
#else
        gen_pool_set_algo(mgr->private_data, gen_pool_best_fit, NULL);
        rc = gen_pool_add_virt(mgr->private_data, vaddr, paddr, size, -1);
        if (rc) {
                gen_pool_destroy(mgr->private_data);
                goto err;
        }

        mgr->ops = &pool_ops_generic;

        return mgr;
#endif
err:
#if !defined(CONFIG_TEE_3_2)
	if (ret == -ENOMEM)
		pr_err("%s: can't allocate memory for res_mem shared memory pool\n", __func__);
	if (pool && pool->private_mgr.private_data)
		gen_pool_destroy(pool->private_mgr.private_data);
	kfree(pool);
	return ERR_PTR(ret);
#else
        kfree(mgr);

        return ERR_PTR(rc);
#endif
}

#if !defined(CONFIG_TEE_3_2)
EXPORT_SYMBOL_GPL(tee_shm_pool_alloc_res_mem);
#else
EXPORT_SYMBOL_GPL(tee_shm_pool_mgr_alloc_res_mem);
#endif

#if defined(CONFIG_TEE_3_2)
static bool check_mgr_ops(struct tee_shm_pool_mgr *mgr)
{
        return mgr && mgr->ops && mgr->ops->alloc && mgr->ops->free &&
                mgr->ops->destroy_poolmgr;
}

struct tee_shm_pool *tee_shm_pool_alloc_3_2(struct tee_shm_pool_mgr *priv_mgr,
                                        struct tee_shm_pool_mgr *dmabuf_mgr)
{
        struct tee_shm_pool *pool;

        if (!check_mgr_ops(priv_mgr) || !check_mgr_ops(dmabuf_mgr))
                return ERR_PTR(-EINVAL);

        pool = kzalloc(sizeof(*pool), GFP_KERNEL);
        if (!pool)
                return ERR_PTR(-ENOMEM);

        pool->private_mgr = priv_mgr;
        pool->dma_buf_mgr = dmabuf_mgr;

        return pool;
}
/* EXPORT_SYMBOL_GPL(tee_shm_pool_alloc); */
#endif

/**
 * tee_shm_pool_free() - Free a shared memory pool
 * @pool:	The shared memory pool to free
 *
 * There must be no remaining shared memory allocated from this pool when
 * this function is called.
 */

#ifdef CONFIG_TEE_2_4
void tee_shm_pool_free_2_4(struct tee_shm_pool *pool)
#else
void tee_shm_pool_free(struct tee_shm_pool *pool)
#endif
{
#if !defined(CONFIG_TEE_3_2)
	pool->destroy(pool);
#else
        if (pool->private_mgr)
                tee_shm_pool_mgr_destroy(pool->private_mgr);
        if (pool->dma_buf_mgr)
                tee_shm_pool_mgr_destroy(pool->dma_buf_mgr);
#endif
	kfree(pool);
}
#ifndef CONFIG_TEE_2_4
EXPORT_SYMBOL_GPL(tee_shm_pool_free);
#endif
