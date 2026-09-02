/*
 * smaf-secure.h
 *
 * Copyright (C) Linaro SA 2015
 * Author: Benjamin Gaignard <benjamin.gaignard at linaro.org> for Linaro.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _SMAF_SECURE_H_
#define _SMAF_SECURE_H_

/**
 * struct smaf_secure
 * @create_context:	create a secure context for one dmabuf.
 * @destroy_context:	destroy secure context.
 * @grant_access:	check and provide access to memroy area for a specific
 *			device.
 * @revoke_access:	remove device access rights.
 * @allow_cpu_access:	return true if CPU can access to memory
 */
struct smaf_secure {
	void *(*create_context)(void);
	int (*destroy_context)(void *ctx);
	bool (*grant_access)(void *ctx,
			     struct device *dev,
			     size_t addr, size_t size,
			     enum dma_data_direction direction);
	void (*revoke_access)(void *ctx,
			      struct device *dev,
			      size_t addr, size_t size,
			      enum dma_data_direction direction);
	bool (*allow_cpu_access)(void *ctx, enum dma_data_direction direction);
};

/**
 * smaf_register_secure - register secure module helper
 * Secure module helper should be platform specific so only one can be
 * registered.
 *
 * @sec: secure module to be registered
 */
int smaf_register_secure(struct smaf_secure *sec);

/**
 * smaf_unregister_secure - unregister secure module helper
 */
void smaf_unregister_secure(struct smaf_secure *sec);

/**
 * smaf_is_secure - test is a dma_buf handle has been secured by SMAF
 * @dmabuf: dma_buf handle to be tested
 */
bool smaf_is_secure(struct dma_buf *dmabuf);

/**
 * smaf_set_secure - change dma_buf handle secure status
 * @dmabuf: dma_buf handle to be change
 * @secure: if true secure dma_buf handle
 */
int smaf_set_secure(struct dma_buf *dmabuf, bool secure);

/**
 * smaf_create_handle - create a smaf_handle with the give length and flags
 * do not allocate memory but provide smaf_handle->dmabuf that can be
 * shared between devices.
 *
 * @length: buffer size
 * @flags: handle flags
 */
struct smaf_handle *smaf_create_handle(size_t length, unsigned int flags);

#endif
