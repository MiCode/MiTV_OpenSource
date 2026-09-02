/*
 * smaf-allocator.h
 *
 * Copyright (C) Linaro SA 2015
 * Author: Benjamin Gaignard <benjamin.gaignard at linaro.org> for Linaro.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _SMAF_ALLOCATOR_H_
#define _SMAF_ALLOCATOR_H_

#include <linux/dma-buf.h>
#include <linux/list.h>

/**
 * struct smaf_allocator - implement dma_buf_ops like functions
 *
 * @match: match function to check if allocator can accept the devices
 *	   attached to dmabuf
 * @allocate: allocate memory with the given length and flags
 *	      return a dma_buf handle
 * @name: allocator name
 * @ranking: allocator ranking (bigger is better)
 */
struct smaf_allocator {
	struct list_head list_node;
	bool (*match)(struct dma_buf *dmabuf);
	struct dma_buf *(*allocate)(struct dma_buf *dmabuf,
				    size_t length, unsigned int flags);
	const char *name;
	int ranking;
};

/**
 * smaf_register_allocator - register an allocator to be used by SMAF
 * @alloc: smaf_allocator structure
 */
int smaf_register_allocator(struct smaf_allocator *alloc);

/**
 * smaf_unregister_allocator - unregister alloctor
 * @alloc: smaf_allocator structure
 */
void smaf_unregister_allocator(struct smaf_allocator *alloc);

/**
 * smaf_select_allocator_by_name - select an allocator by it name
 * return 0 if the allocator has been found and selected.
 * @dmabuf: dma_buf buffer handler
 * @name: name of the allocator to be selected
 */
int smaf_select_allocator_by_name(struct dma_buf *dmabuf, char *name);

#endif
