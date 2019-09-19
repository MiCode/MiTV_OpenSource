/*
 * drivers/staging/android/uapi/ion.h
 *
 * Copyright (C) 2011 Google, Inc.
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

#ifndef _UAPI_LINUX_ION_H
#define _UAPI_LINUX_ION_H

#include <linux/ioctl.h>
#include <linux/types.h>

typedef int ion_user_handle_t;

#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
/**
 * These are the only ids that should be used for Ion heap ids.
 * The ids listed are the order in which allocation will be attempted
 * if specified. Don't swap the order of heap ids unless you know what
 * you are doing!
 * Id's are spaced by purpose to allow new Id's to be inserted in-between (for
 * possible fallbacks)
 */
enum ion_heap_ids {
    INVALID_HEAP_ID = -1,

    //system heap
    ION_SYSTEM_HEAP_ID = 0,

    //system contig heap
    ION_SYSTEM_CONTIG_HEAP_ID = 1,

    //carveout heap
    ION_CARVEOUT_HEAP_ID = 2,

    //CHUNK heap
    ION_CHUNK_HEAP_ID = 3,

    //dma heap
    ION_DMA_HEAP_ID = 4,

    //CMA heap
    //mstar id start with 15, and it is created with bootarg in module init dynamically
    //don't add manually!!!!!!!
    ION_CMA_HEAP_ID_START = 15,
#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
	ION_MALI_FAKE_HEAP_ID = 15,	//fake memory heap
#endif

    //mstar cma heap: mali type
    ION_MALI_MIU0_HEAP_ID = 16,
    ION_MALI_MIU1_HEAP_ID = 17,
    ION_MALI_MIU2_HEAP_ID = 18,

    ION_CMA_MIU0_HEAP_ID = 25,
    ION_CMA_MIU1_HEAP_ID = 26,
    ION_CMA_MIU2_HEAP_ID = 27,
    ION_ZRAM_MIU0_HEAP_ID = 28,
    ION_ZRAM_MIU1_HEAP_ID = 29,
    ION_ZRAM_MIU2_HEAP_ID = 30,
    ION_HEAP_ID_RESERVED = 31 /** Bit reserved for ION_SECURE flag */
};
#endif

/**
 * enum ion_heap_types - list of all possible types of heaps
 * @ION_HEAP_TYPE_SYSTEM:	 memory allocated via vmalloc
 * @ION_HEAP_TYPE_SYSTEM_CONTIG: memory allocated via kmalloc
 * @ION_HEAP_TYPE_CARVEOUT:	 memory allocated from a prereserved
 *				 carveout heap, allocations are physically
 *				 contiguous
 * @ION_HEAP_TYPE_DMA:		 memory allocated via DMA API
 * @ION_NUM_HEAPS:		 helper for iterating over heaps, a bit mask
 *				 is used to identify the heaps, so only 32
 *				 total heap types are supported
 */
enum ion_heap_type {
	ION_HEAP_TYPE_SYSTEM,
	ION_HEAP_TYPE_SYSTEM_CONTIG,
	ION_HEAP_TYPE_CARVEOUT,
	ION_HEAP_TYPE_CHUNK,
	ION_HEAP_TYPE_DMA,
	ION_HEAP_TYPE_CUSTOM, /*
			       * must be last so device specific heaps always
			       * are at the end of this enum
			       */
#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
	ION_HEAP_TYPE_MSTAR_CMA,
#endif
	ION_NUM_HEAPS = 16,
};

#define ION_NUM_HEAP_IDS		(sizeof(unsigned int) * 8)

/**
 * allocation flags - the lower 16 bits are used by core ion, the upper 16
 * bits are reserved for use by the heaps themselves.
 */

/*
 * mappings of this buffer should be cached, ion will do cache maintenance
 * when the buffer is mapped for dma
 */
#define ION_FLAG_CACHED 1

/*
 * mappings of this buffer will created at mmap time, if this is set
 * caches must be managed manually
 */
#define ION_FLAG_CACHED_NEEDS_SYNC 2

#ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
/*alloc contiguous memory in cma heap*/
#define ION_FLAG_CONTIGUOUS (1<<16)

/*alloc contiguous memory with specified start address, only work with cma heap*/
#define ION_FLAG_STARTADDR  (1<<17)

/*alloc discrete pages in cma heap, especially for mali alloc page*/
#define ION_FLAG_DISCRETE   (1<<18)

/* alloc cleared memory */
#define ION_FLAG_ZERO_MEMORY   (1<<19)

#ifdef CONFIG_MP_ION_PATCH_FAKE_MEM
/* alloc fake memory */
#define ION_FLAG_FAKE_MEMORY   (1<<20)
#endif
#endif

#ifdef CONFIG_MP_MMA_ENABLE
#define ION_FLAG_SECURE   (1<<19)
#define ION_FLAG_MIU_SHIFT 20
#define ION_FLAG_MIUMASK  (3<<20)
#define ION_FLAG_ANYMIU   (0)
#define ION_FLAG_MIU0     (1<<20)
#define ION_FLAG_MIU1     (2<<20)
#define ION_FLAG_MIU2     (3<<20)
#define ION_FLAG_WND_SHIFT ION_FLAG_MIU_SHIFT
#define ION_FLAG_WNDMASK  ION_FLAG_MIUMASK
#define ION_FLAG_ANYDRAM   ION_FLAG_ANYMIU
#define ION_FLAG_WIDEDRAM  ION_FLAG_MIU0
#define ION_FLAG_NARROWDRAM ION_FLAG_MIU1
#define MAX_MIU_NR         (3)
#define ION_FLAG_DMAZONE    (1<<23)  // 1 DMA ZONE,0 HIGH/NORMAL/DMA ZONE;
/*ion flag for cma about alloc secure or un-secure*/
#define ION_FLAG_CMA_ALLOC_SECURE  ION_FLAG_SECURE

#define ION_FLAG_CMA_ALLOC_RESERVE (1<<22)
#endif

/**
 * DOC: Ion Userspace API
 *
 * create a client by opening /dev/ion
 * most operations handled via following ioctls
 *
 */

/**
 * struct ion_allocation_data - metadata passed from userspace for allocations
 * @len:		size of the allocation
 * @align:		required alignment of the allocation
 * @heap_id_mask:	mask of heap ids to allocate from
 * @flags:		flags passed to heap
 * @handle:		pointer that will be populated with a cookie to use to
 *			refer to this allocation
 *
 * Provided by userspace as an argument to the ioctl
 */
struct ion_allocation_data {
	size_t len;
	size_t align;
	unsigned int heap_id_mask;
	unsigned int flags;
	ion_user_handle_t handle;
};

#ifdef CONFIG_MP_ION_PATCH_CACHE_FLUSH_MOD
/**
 * struct ion_cache_flush_data - metadata passed from userspace for cacheflush
 * @start:       start address to flush
 * @len:        size to flush
 *
 * Provided by userspace as an argument to the ioctl
 */
struct ion_cache_flush_data {
	size_t start;
	size_t len;
};
#endif

/**
 * struct ion_fd_data - metadata passed to/from userspace for a handle/fd pair
 * @handle:	a handle
 * @fd:		a file descriptor representing that handle
 *
 * For ION_IOC_SHARE or ION_IOC_MAP userspace populates the handle field with
 * the handle returned from ion alloc, and the kernel returns the file
 * descriptor to share or map in the fd field.  For ION_IOC_IMPORT, userspace
 * provides the file descriptor and the kernel returns the handle.
 */
struct ion_fd_data {
	ion_user_handle_t handle;
	int fd;
};

/**
 * struct ion_handle_data - a handle passed to/from the kernel
 * @handle:	a handle
 */
struct ion_handle_data {
	ion_user_handle_t handle;
};

/**
 * struct ion_custom_data - metadata passed to/from userspace for a custom ioctl
 * @cmd:	the custom ioctl function to call
 * @arg:	additional data to pass to the custom ioctl, typically a user
 *		pointer to a predefined structure
 *
 * This works just like the regular cmd and arg fields of an ioctl.
 */
struct ion_custom_data {
	unsigned int cmd;
	unsigned long arg;
};

#define MAX_HEAP_NAME			32

/**
 * struct ion_heap_data - data about a heap
 * @name - first 32 characters of the heap name
 * @type - heap type
 * @heap_id - heap id for the heap
 */
struct ion_heap_data {
	char name[MAX_HEAP_NAME];
	__u32 type;
	__u32 heap_id;
	__u32 reserved0;
	__u32 reserved1;
	__u32 reserved2;
};

/**
 * struct ion_heap_query - collection of data about all heaps
 * @cnt - total number of heaps to be copied
 * @heaps - buffer to copy heap data
 */
struct ion_heap_query {
	__u32 cnt; /* Total number of heaps to be copied */
	__u32 reserved0; /* align to 64bits */
	__u64 heaps; /* buffer to be populated */
	__u32 reserved1;
	__u32 reserved2;
};

#if (MP_ION_PATCH_MSTAR==1)
/**
 * struct ion_cust_allocation_data - metadata passed from userspace for allocations
 * @start:       start address when flags with ION_FLAG_STARTADDR bit setting, this para only work for cma heap
 * @len:        size of the allocation
 * @align:  required alignment of the allocation
 * @heap_id_mask:   mask of heap ids to allocate from
 * @flags:      flags passed to heap
 * @handle:     pointer that will be populated with a cookie to use to
 *          refer to this allocation
 *
 * Provided by userspace as an argument to the ioctl
 */
struct ion_cust_allocation_data {
	size_t start;
	size_t len;
	size_t align;
	unsigned int heap_id_mask;
	unsigned int flags;
	ion_user_handle_t handle;
	unsigned long miu_offset;
	unsigned char miu;
};

/**
 * struct ion_user_data - for returning mapping infomation to user space
 * @handle:         to get the buffer we want
 * @bus_addr:       the start bus address of allocated buffer

 * This is currently for heap_type is ION_HEAP_TYPE_DMA
 */
struct ion_user_data {
	ion_user_handle_t handle;
	unsigned long bus_addr;
};

/**
 * struct ion_cust_import_data - for get newfd point to file of another process fd
 * @pid:    point to another process
 * @fd:     fd in another process
 */
struct ion_cust_import_data {
	pid_t pid;  // of another process
	int fd;     // fd in another process pid
	int newfd; // in current process
};
#endif
#define ION_IOC_MAGIC		'I'

/**
 * DOC: ION_IOC_ALLOC - allocate memory
 *
 * Takes an ion_allocation_data struct and returns it with the handle field
 * populated with the opaque handle for the allocation.
 */
#define ION_IOC_ALLOC		_IOWR(ION_IOC_MAGIC, 0, \
				      struct ion_allocation_data)

/**
 * DOC: ION_IOC_FREE - free memory
 *
 * Takes an ion_handle_data struct and frees the handle.
 */
#define ION_IOC_FREE		_IOWR(ION_IOC_MAGIC, 1, struct ion_handle_data)

/**
 * DOC: ION_IOC_MAP - get a file descriptor to mmap
 *
 * Takes an ion_fd_data struct with the handle field populated with a valid
 * opaque handle.  Returns the struct with the fd field set to a file
 * descriptor open in the current address space.  This file descriptor
 * can then be used as an argument to mmap.
 */
#define ION_IOC_MAP		_IOWR(ION_IOC_MAGIC, 2, struct ion_fd_data)

/**
 * DOC: ION_IOC_SHARE - creates a file descriptor to use to share an allocation
 *
 * Takes an ion_fd_data struct with the handle field populated with a valid
 * opaque handle.  Returns the struct with the fd field set to a file
 * descriptor open in the current address space.  This file descriptor
 * can then be passed to another process.  The corresponding opaque handle can
 * be retrieved via ION_IOC_IMPORT.
 */
#define ION_IOC_SHARE		_IOWR(ION_IOC_MAGIC, 4, struct ion_fd_data)

/**
 * DOC: ION_IOC_IMPORT - imports a shared file descriptor
 *
 * Takes an ion_fd_data struct with the fd field populated with a valid file
 * descriptor obtained from ION_IOC_SHARE and returns the struct with the handle
 * filed set to the corresponding opaque handle.
 */
#define ION_IOC_IMPORT		_IOWR(ION_IOC_MAGIC, 5, struct ion_fd_data)

/**
 * DOC: ION_IOC_SYNC - syncs a shared file descriptors to memory
 *
 * Deprecated in favor of using the dma_buf api's correctly (syncing
 * will happen automatically when the buffer is mapped to a device).
 * If necessary should be used after touching a cached buffer from the cpu,
 * this will make the buffer in memory coherent.
 */
#define ION_IOC_SYNC		_IOWR(ION_IOC_MAGIC, 7, struct ion_fd_data)

/**
 * DOC: ION_IOC_CUSTOM - call architecture specific ion ioctl
 *
 * Takes the argument of the architecture specific ioctl to call and
 * passes appropriate userdata for that ioctl
 */
#define ION_IOC_CUSTOM		_IOWR(ION_IOC_MAGIC, 6, struct ion_custom_data)

/**
 * DOC: ION_IOC_HEAP_QUERY - information about available heaps
 *
 * Takes an ion_heap_query structure and populates information about
 * available Ion heaps.
 */
#define ION_IOC_HEAP_QUERY     _IOWR(ION_IOC_MAGIC, 8, \
					struct ion_heap_query)

#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
/**
 * DOC: ION_IOC_CACHE_FLUSH -  flush cache from start address
 *
 * Takes an ion_cache_flush_data struct
 */
#define ION_IOC_CACHE_FLUSH     _IOW(ION_IOC_MAGIC, 10, \
                      struct ion_cache_flush_data)
#endif

#if (MP_ION_PATCH_MSTAR==1)
/**
 * DOC: ION_IOC_CUST_ALLOC -  allocate memory API with start address
 *
 * Takes an ion_cust_allocation_data struct and returns it with the handle field
 * populated with the opaque handle for the allocation.
 */
#define ION_IOC_CUST_ALLOC      _IOWR(ION_IOC_MAGIC, 9, \
					struct ion_cust_allocation_data)

/**
 * DOC: ION_IOC_GET_CMA_BUFFER_INFO - get buffer info
 *
 * Takes an ion_user_data struct and returns it with the bus_addr field
 * populated with the opaque handle for the allocation.
 */
#define ION_IOC_GET_CMA_BUFFER_INFO     _IOWR(ION_IOC_MAGIC, 11, \
					struct ion_user_data)
#endif
#endif /* _UAPI_LINUX_ION_H */
