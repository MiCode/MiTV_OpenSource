/*
 * smaf.h
 *
 * Copyright (C) Linaro SA 2015
 * Author: Benjamin Gaignard <benjamin.gaignard@linaro.org> for Linaro.
 * License terms:  GNU General Public License (GPL), version 2
 */

#ifndef _UAPI_SMAF_H_
#define _UAPI_SMAF_H_

#include <linux/ioctl.h>
#include <linux/types.h>
#if defined(CONFIG_COMPAT)
#include <linux/compat.h>
#endif

#define SMAF_INVALID_START_PA (~0)
#define ALLOCATOR_NAME_LENGTH 64

struct smaf_handle {
	struct dma_buf *dmabuf;
	struct smaf_allocator *allocator;
	struct dma_buf *db_alloc;
	size_t length;
	off_t start;
	unsigned int flags;
	int fd;
	bool is_secure;
	void *secure_ctx;
};


/**
 * struct smaf_create_data - allocation parameters
 * @length:	size of the allocation
 * @flags:	flags passed to allocator
 * @name:	name of the allocator to be selected, could be NULL
 * @fd:		returned file descriptor
 */
struct smaf_create_data {
	size_t length;
	unsigned int flags;
	char name[ALLOCATOR_NAME_LENGTH];
	int fd;
};

/**
 * struct smaf_create_pa_data - pa allocation parameters
 * @length:	size of the allocation
 * @start:       start phy address
 * @flags:	flags passed to allocator
 * @name:	name of the allocator to be selected, could be NULL
 * @fd:		returned file descriptor
 */
struct smaf_create_pa_data {
	size_t length;
	off_t start;
	unsigned int flags;
	char name[ALLOCATOR_NAME_LENGTH];
	int fd;
};

/**
 * struct smaf_secure_flag - set/get secure flag
 * @fd:		file descriptor
 * @secure:	secure flag value (set or get)
 */
struct smaf_secure_flag {
	int fd;
	int secure;
};

/**
 * struct smaf_sync_range - sync range
 * @start:	start virt address
 * @len:	       range length
 */
struct smaf_sync_range {
	size_t start;
	size_t len;
};

/**
 * struct smaf_buffer_info - get dma buffer info
 * @start:	start virt address
 * @lengt:  range length
 * @flags:  flags create buffer
 * @name:   allocator name
 * @secure: secure buffer indicator
 * @fd:     dma buffer fd
 */
struct smaf_buffer_info {
	size_t length;
	size_t start;
	unsigned int flags;
	char name[ALLOCATOR_NAME_LENGTH];
	int secure;
	int fd;
};

/*
 * compat ioctl struct
 */
#if defined(CONFIG_COMPAT)
struct compat_smaf_create_data {
	compat_size_t length;
	compat_uint_t flags;
	char name[ALLOCATOR_NAME_LENGTH];
	compat_int_t fd;
};

struct compat_smaf_create_pa_data {
	compat_size_t length;
	compat_off_t start;
	compat_uint_t flags;
	char name[ALLOCATOR_NAME_LENGTH];
	compat_int_t fd;
};

struct compat_smaf_secure_flag {
	compat_int_t fd;
	compat_int_t secure;
};

struct compat_smaf_sync_range {
	compat_size_t start;
	compat_size_t len;
};

struct compat_smaf_buffer_info {
	compat_size_t length;
	compat_size_t start;
	compat_uint_t flags;
	char name[ALLOCATOR_NAME_LENGTH];
	compat_int_t secure;
	compat_int_t fd;        //in
};
#endif

#define SMAF_IOC_MAGIC	'S'

#define SMAF_IOC_CREATE		 _IOWR(SMAF_IOC_MAGIC, 0, \
				       struct smaf_create_data)

#define SMAF_IOC_GET_SECURE_FLAG _IOWR(SMAF_IOC_MAGIC, 1, \
				       struct smaf_secure_flag)

#define SMAF_IOC_SET_SECURE_FLAG _IOWR(SMAF_IOC_MAGIC, 2, \
				       struct smaf_secure_flag)

#define SMAF_IOC_CREATE_PA             _IOWR(SMAF_IOC_MAGIC, 20, \
				       struct smaf_create_pa_data)

#define SMAF_IOC_SYNC_DEVICE           _IOWR(SMAF_IOC_MAGIC, 21, \
				       struct smaf_sync_range)

#define SMAF_IOC_SYNC_CPU          _IOWR(SMAF_IOC_MAGIC, 22, \
				       struct smaf_sync_range)

#define SMAF_IOC_GET_INFO          _IOWR(SMAF_IOC_MAGIC, 23, \
				       struct smaf_buffer_info)

/*compatible ioctl*/
#if defined(CONFIG_COMPAT)
#define COMPAT_SMAF_IOC_CREATE		 _IOWR(SMAF_IOC_MAGIC, 0, \
				       struct compat_smaf_create_data)

#define COMPAT_SMAF_IOC_GET_SECURE_FLAG _IOWR(SMAF_IOC_MAGIC, 1, \
				       struct compat_smaf_secure_flag)

#define COMPAT_SMAF_IOC_SET_SECURE_FLAG _IOWR(SMAF_IOC_MAGIC, 2, \
				       struct compat_smaf_secure_flag)

#define COMPAT_SMAF_IOC_CREATE_PA             _IOWR(SMAF_IOC_MAGIC, 20, \
				       struct compat_smaf_create_pa_data)

#define COMPAT_SMAF_IOC_SYNC_DEVICE           _IOWR(SMAF_IOC_MAGIC, 21, \
				       struct compat_smaf_sync_range)

#define COMPAT_SMAF_IOC_SYNC_CPU          _IOWR(SMAF_IOC_MAGIC, 22, \
				       struct compat_smaf_sync_range)

#define COMPAT_SMAF_IOC_GET_INFO          _IOWR(SMAF_IOC_MAGIC, 23, \
				       struct compat_smaf_buffer_info)
#endif
#endif
