/* SPDX-License-Identifier: GPL-2.0 */
/*
 * drivers/staging/android/uapi/ion.h
 *
 * Copyright (C) 2011 Google, Inc.
 */

#ifndef _UAPI_LINUX_ION_H
#define _UAPI_LINUX_ION_H

#include <linux/ioctl.h>
#include <linux/types.h>

typedef int ion_user_handle_t;

#define ION_FLAG_CACHED_NEEDS_SYNC 2
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
#ifdef CONFIG_MP_MMA_ENABLE
	ION_HEAP_TYPE_IOMMU_CARVEOUT,
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
#define ION_FLAG_FAKE_MEMORY   (1<<30)
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

/*alloc iommu cma in cma heap*/
#define ION_FLAG_IOMMU_CMA   (1<<24)
#define ION_FLAG_IOMMU_FLUSH   (1<<26)


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
 * @heap_id_mask:	mask of heap ids to allocate from
 * @flags:		flags passed to heap
 * @handle:		pointer that will be populated with a cookie to use to
 *			refer to this allocation
 *
 * Provided by userspace as an argument to the ioctl
 */
struct ion_allocation_data {
	__u64 len;
	__u32 heap_id_mask;
	__u32 flags;
	__u32 fd;
	__u32 unused;
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
	__u64 start;
	__u64 len;
};
#endif

#ifdef CONFIG_MP_ION_PATCH_MSTAR
/**
 * struct ion_user_data - for returning mapping infomation to user space
 * @handle:         to get the buffer we want
 * @bus_addr:       the start bus address of allocated buffer

 * This is currently for heap_type is ION_HEAP_TYPE_DMA
 */
struct ion_user_data {
	int fd;
	unsigned long long bus_addr;
};
#endif

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
 * DOC: ION_IOC_HEAP_QUERY - information about available heaps
 *
 * Takes an ion_heap_query structure and populates information about
 * available Ion heaps.
 */
#define ION_IOC_HEAP_QUERY     _IOWR(ION_IOC_MAGIC, 8, \
					struct ion_heap_query)

#if (MP_ION_PATCH_MSTAR == 1)
/**
 * DOC: ION_IOC_GET_CMA_BUFFER_INFO - get buffer info
 *
 * Takes an ion_user_data struct and returns it with the bus_addr field
 * populated with the opaque handle for the allocation.
 */
#define ION_IOC_GET_CMA_BUFFER_INFO     _IOWR(ION_IOC_MAGIC, 11, \
					struct ion_user_data)
#endif

#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
/**
 * DOC: ION_IOC_CACHE_FLUSH -  flush cache from start address
 *
 * Takes an ion_cache_flush_data struct
 */
#define ION_IOC_CACHE_FLUSH     _IOW(ION_IOC_MAGIC, 10, \
                      struct ion_cache_flush_data)
#endif

#endif /* _UAPI_LINUX_ION_H */
