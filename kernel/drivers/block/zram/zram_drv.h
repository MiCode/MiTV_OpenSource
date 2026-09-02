/*
 * Compressed RAM block device
 *
 * Copyright (C) 2008, 2009, 2010  Nitin Gupta
 *               2012, 2013 Minchan Kim
 *
 * This code is released using a dual license strategy: BSD/GPL
 * You can choose the licence that better fits your requirements.
 *
 * Released under the terms of 3-clause BSD License
 * Released under the terms of GNU General Public License Version 2.0
 *
 */

#ifndef _ZRAM_DRV_H_
#define _ZRAM_DRV_H_

#include <linux/rwsem.h>
#include <linux/zsmalloc.h>
#include <linux/crypto.h>
#ifdef CONFIG_MP_ZSM
#include "idx_rbtree.h"
#endif
#ifdef CONFIG_MP_ZSM_DEBUG
#include "linux/sched.h"
#define ZSM_DEBUG_HISTORY_CNT 10
#define ZSM_DEBUG_HISTROY_MSG_SIZE 16
#endif
#include "zcomp.h"

#define SECTORS_PER_PAGE_SHIFT	(PAGE_SHIFT - SECTOR_SHIFT)
#define SECTORS_PER_PAGE	(1 << SECTORS_PER_PAGE_SHIFT)
#define ZRAM_LOGICAL_BLOCK_SHIFT 12
#define ZRAM_LOGICAL_BLOCK_SIZE	(1 << ZRAM_LOGICAL_BLOCK_SHIFT)
#define ZRAM_SECTOR_PER_LOGICAL_BLOCK	\
	(1 << (ZRAM_LOGICAL_BLOCK_SHIFT - SECTOR_SHIFT))

/*
 * The lower ZRAM_FLAG_SHIFT bits of table.flags is for
 * object size (excluding header), the higher bits is for
 * zram_pageflags.
 *
 * zram is mainly used for memory efficiency so we want to keep memory
 * footprint small so we can squeeze size and flags into a field.
 * The lower ZRAM_FLAG_SHIFT bits is for object size (excluding header),
 * the higher bits is for zram_pageflags.
 */
#define ZRAM_FLAG_SHIFT 24
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
#define MAX_COLLISION 10
#endif
#ifdef CONFIG_MP_ZSM
/* Bits for 1024 MB, the maximum swap size of current ZSM design */
#define ZRAM_1024MB_PAGE_BITS   IDX_RB_1024MB_MAX_BITS
#endif
/* Flags for zram pages (table[page_no].flags) */
enum zram_pageflags {
	/* zram slot is locked */
	ZRAM_LOCK = ZRAM_FLAG_SHIFT,
	ZRAM_SAME,	/* Page consists the same element */
	ZRAM_WB,	/* page is stored on backing_device */
	ZRAM_UNDER_WB,	/* page is under writeback */
	ZRAM_HUGE,	/* Incompressible page */
	ZRAM_IDLE,	/* not accessed page since last idle marking */
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
    ZRAM_IS_MZC,
#endif

	__NR_ZRAM_PAGEFLAGS,
};

#ifdef CONFIG_MP_ZSM
/* This depends on PAGE_SIZE and zsm_select_tree_non4K(). */
#define ZSM_Compr_TREE_COUNT 16

enum zsm_flags {
    /* Use the rest space in same_checksum_link */
    ZRAM_FIRST_NODE = 0, /* page is the representative of same content */
    ZRAM_RB_NODE,
    ZRAM_ZSM_DONE_NODE,
	__NR_ZSM_FLAGS,
};

struct zsm_connect {
	u32 found_in_tree_idx;
	u32 found_in_list_idx;
	u32 parent_idx;
	int new_is_left;
};

/* 3 bytes */
typedef struct {
    u32 same_crc_next: ZRAM_1024MB_PAGE_BITS;
    u8 flags_current: __NR_ZSM_FLAGS;
} __attribute__((packed)) list_node_link;
#endif

/*-- Data structures */

/* Allocated for each disk page */
struct zram_table_entry {
	union {
		unsigned long handle;
		unsigned long element;
	};
	unsigned long flags;
#ifdef CONFIG_ZRAM_MEMORY_TRACKING
	ktime_t ac_time;
#endif
#ifdef CONFIG_MP_ZSM
    u32 checksum;
    list_node_link crc_link_node;
	struct idx_rb_node _idx_rb_node;
#endif
#ifdef CONFIG_MP_ZSM_DEBUG
    /* Record how many pages are as same as the page */
    int copy_count;
	/* history_* are used for flag_stack tracking for debugging */
	unsigned long history_zsm_flags[ZSM_DEBUG_HISTORY_CNT];
	unsigned long history_zram_flags[ZSM_DEBUG_HISTORY_CNT];
	pid_t history_pid[ZSM_DEBUG_HISTORY_CNT];
	unsigned long history_handle[ZSM_DEBUG_HISTORY_CNT];
	unsigned long history_len[ZSM_DEBUG_HISTORY_CNT];
	char history_msg[ZSM_DEBUG_HISTORY_CNT][ZSM_DEBUG_HISTROY_MSG_SIZE];
#endif
#ifdef CONFIG_MP_ZSM
	/* pad to 24/28 bytes for 32/64 bits system to make bit_spin_lock work */
} __attribute__((packed, aligned(sizeof(unsigned long))));
#else
};
#endif

struct zram_stats {
	atomic64_t compr_data_size;	/* compressed size of pages stored */
	atomic64_t num_reads;	/* failed + successful */
	atomic64_t num_writes;	/* --do-- */
	atomic64_t failed_reads;	/* can happen when memory is too low */
	atomic64_t failed_writes;	/* can happen when memory is too low */
	atomic64_t invalid_io;	/* non-page-aligned I/O requests */
	atomic64_t notify_free;	/* no. of swap slot free notifications */
	atomic64_t same_pages;		/* no. of same element filled pages */
	atomic64_t huge_pages;		/* no. of huge pages */
	atomic64_t pages_stored;	/* no. of pages currently stored */
	atomic_long_t max_used_pages;	/* no. of maximum pages stored */
	atomic64_t writestall;		/* no. of write slow paths */
	atomic64_t miss_free;		/* no. of missed free */
#ifdef	CONFIG_ZRAM_WRITEBACK
	atomic64_t bd_count;		/* no. of pages in backing device */
	atomic64_t bd_reads;		/* no. of reads from backing device */
	atomic64_t bd_writes;		/* no. of writes from backing device */
#endif
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
	atomic64_t write_time;
	atomic64_t read_time;
	atomic64_t notify_free_time;
	atomic64_t num_compression;
	atomic64_t num_decompression;
	atomic64_t compression_time;
	atomic64_t compression_time_wo_collision;
	atomic64_t decompression_time;
	atomic64_t decompression_time_wo_collision;
	atomic64_t rw_collision[MAX_COLLISION][MAX_COLLISION];
	atomic64_t collision[MAX_COLLISION];
	atomic64_t num_kswapd;
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
	atomic64_t num_hybrid_hw;
#endif
#endif
#ifdef CONFIG_MP_ZSM_STAT
	atomic64_t real_nCompr_pg_cnt;
	atomic64_t real_Compr_pg_cnt;
    atomic64_t zsm_saved_nCompr_sz;     /* saved non compressed size*/
    atomic64_t zsm_saved_Compr_sz;      /* saved compressed size*/
    atomic64_t zsm_saved_nCompr_pg_cnt; /* no. of saved non compressed pages */
    atomic64_t zsm_saved_Compr_pg_cnt;  /* no. of saved compressed pages */
#endif
};

#ifdef CONFIG_MP_ZSM
typedef spinlock_t zsm_tree_lock;
#endif

struct zram {
	struct zram_table_entry *table;
	struct zs_pool *mem_pool;
	struct zcomp *comp;
	struct gendisk *disk;
	/* Prevent concurrent execution of device init */
	struct rw_semaphore init_lock;
	/*
	 * the number of pages zram can consume for storing compressed data
	 */
	unsigned long limit_pages;

	struct zram_stats stats;
	/*
	 * This is the limit on amount of *uncompressed* worth of data
	 * we can store in a disk.
	 */
	u64 disksize;	/* bytes */
	char compressor[CRYPTO_MAX_ALG_NAME];
	/*
	 * zram is claimed so open request will be failed
	 */
	bool claim; /* Protected by bdev->bd_mutex */
	struct file *backing_dev;
#ifdef CONFIG_ZRAM_WRITEBACK
	spinlock_t wb_limit_lock;
	bool wb_limit_enable;
	u64 bd_wb_limit;
	struct block_device *bdev;
	unsigned int old_block_size;
	unsigned long *bitmap;
	unsigned long nr_pages;
#endif
#ifdef CONFIG_ZRAM_MEMORY_TRACKING
	struct dentry *debugfs_dir;
#endif
#ifdef CONFIG_MP_ZSM
	bool zsm_on; /* zsm cannot be turned off after turned on */
	struct idx_rb_root tree_root_Compr[ZSM_Compr_TREE_COUNT];
	struct idx_rb_root tree_root_nCompr;
	zsm_tree_lock tree_lock_Compr[ZSM_Compr_TREE_COUNT];
	zsm_tree_lock tree_lock_nCompr;
#endif
};
#endif
