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

#define KMSG_COMPONENT "zram"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/device.h>
#include <linux/genhd.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/backing-dev.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/err.h>
#include <linux/idr.h>
#include <linux/sysfs.h>
#include <linux/debugfs.h>
#include <linux/cpuhotplug.h>
#include "zram_drv.h"
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
#include <linux/swap.h>
#endif
#include <linux/lzo.h>
#include <linux/crypto.h>
#ifdef CONFIG_MP_MZCCMDQ_HW
#include "mdrv_mzc_drv.h"
#include <linux/ktime.h>
#endif

#ifdef CONFIG_MP_MZCCMDQ_HW_SPLIT
extern DEFINE_PER_CPU(unsigned long, addr1);
extern DEFINE_PER_CPU(unsigned long, addr2);
extern DEFINE_PER_CPU(bool, dec_is_mzc);
#endif

#ifdef CONFIG_MP_ZRAM_PERFORMANCE
static atomic_t enter_idx;
static atomic_t leave_idx;
static atomic_t r_idx, w_idx;
inline unsigned long long timediff (struct timeval begin, struct timeval end)
{
	return ((end.tv_sec - begin.tv_sec) * 1000000 + (end.tv_usec - begin.tv_usec));
}
extern unsigned long long duration_enc, duration_dec;
#endif
#if defined(CONFIG_MP_MZCCMDQ_HW)
extern bool MZC_ready;
bool MZC_now = true;
#endif


static DEFINE_IDR(zram_index_idr);
/* idr index must be protected */
static DEFINE_MUTEX(zram_index_mutex);

static int zram_major;
#if defined(CONFIG_MP_MZCCMDQ_HYBRID_HW)
static const char *default_compressor = "mzc_hybrid";
#elif defined(CONFIG_MP_MZCCMDQ_HW)
static const char *default_compressor = "mzc";
#else
static const char *default_compressor = "lzo";
#endif
/* Module params (documentation at end) */
static unsigned int num_devices = 1;
/*
 * Pages that compress to sizes equals or greater than this are stored
 * uncompressed in memory.
 */
static size_t huge_class_size;


static void zram_free_page(struct zram *zram, size_t index);
static int zram_bvec_read(struct zram *zram, struct bio_vec *bvec,
				u32 index, int offset, struct bio *bio);


static int zram_slot_trylock(struct zram *zram, u32 index)
{
	return bit_spin_trylock(ZRAM_LOCK, &zram->table[index].flags);
}

static void zram_slot_lock(struct zram *zram, u32 index)
{
	bit_spin_lock(ZRAM_LOCK, &zram->table[index].flags);
}

static void zram_slot_unlock(struct zram *zram, u32 index)
{
	bit_spin_unlock(ZRAM_LOCK, &zram->table[index].flags);
}

static inline bool init_done(struct zram *zram)
{
	return zram->disksize;
}

static inline struct zram *dev_to_zram(struct device *dev)
{
	return (struct zram *)dev_to_disk(dev)->private_data;
}

static unsigned long zram_get_handle(struct zram *zram, u32 index)
{
	return zram->table[index].handle;
}

static void zram_set_handle(struct zram *zram, u32 index, unsigned long handle)
{
	zram->table[index].handle = handle;
}

/* flag operations require table entry bit_spin_lock() being held */
static bool zram_test_flag(struct zram *zram, u32 index,
			enum zram_pageflags flag)
{
	return zram->table[index].flags & BIT(flag);
}

static void zram_set_flag(struct zram *zram, u32 index,
			enum zram_pageflags flag)
{
	zram->table[index].flags |= BIT(flag);
}

static void zram_clear_flag(struct zram *zram, u32 index,
			enum zram_pageflags flag)
{
	zram->table[index].flags &= ~BIT(flag);
}

static inline void zram_set_element(struct zram *zram, u32 index,
			unsigned long element)
{
	zram->table[index].element = element;
}

static unsigned long zram_get_element(struct zram *zram, u32 index)
{
	return zram->table[index].element;
}

static size_t zram_get_obj_size(struct zram *zram, u32 index)
{
	return zram->table[index].flags & (BIT(ZRAM_FLAG_SHIFT) - 1);
}

static void zram_set_obj_size(struct zram *zram,
					u32 index, size_t size)
{
	unsigned long flags = zram->table[index].flags >> ZRAM_FLAG_SHIFT;

	zram->table[index].flags = (flags << ZRAM_FLAG_SHIFT) | size;
}


static inline bool zram_allocated(struct zram *zram, u32 index)
{
	return zram_get_obj_size(zram, index) ||
			zram_test_flag(zram, index, ZRAM_SAME) ||
			zram_test_flag(zram, index, ZRAM_WB);
}

#if PAGE_SIZE != 4096
static inline bool is_partial_io(struct bio_vec *bvec)
{
	return bvec->bv_len != PAGE_SIZE;
}
#else
static inline bool is_partial_io(struct bio_vec *bvec)
{
	return false;
}
#endif

#ifdef CONFIG_MP_ZSM
#include "zram_merge.h"
#endif

/*
 * Check if request is within bounds and aligned on zram logical blocks.
 */
static inline bool valid_io_request(struct zram *zram,
		sector_t start, unsigned int size)
{
	u64 end, bound;

	/* unaligned request */
	if (unlikely(start & (ZRAM_SECTOR_PER_LOGICAL_BLOCK - 1)))
		return false;
	if (unlikely(size & (ZRAM_LOGICAL_BLOCK_SIZE - 1)))
		return false;

	end = start + (size >> SECTOR_SHIFT);
	bound = zram->disksize >> SECTOR_SHIFT;
	/* out of range range */
	if (unlikely(start >= bound || end > bound || start > end))
		return false;

	/* I/O request is valid */
	return true;
}

static void update_position(u32 *index, int *offset, struct bio_vec *bvec)
{
	*index  += (*offset + bvec->bv_len) / PAGE_SIZE;
	*offset = (*offset + bvec->bv_len) % PAGE_SIZE;
}

static inline void update_used_max(struct zram *zram,
					const unsigned long pages)
{
	unsigned long old_max, cur_max;

	old_max = atomic_long_read(&zram->stats.max_used_pages);

	do {
		cur_max = old_max;
		if (pages > cur_max)
			old_max = atomic_long_cmpxchg(
				&zram->stats.max_used_pages, cur_max, pages);
	} while (old_max != cur_max);
}

static inline void zram_fill_page(void *ptr, unsigned long len,
					unsigned long value)
{
	WARN_ON_ONCE(!IS_ALIGNED(len, sizeof(unsigned long)));
	memset_l(ptr, value, len / sizeof(unsigned long));
}

static bool page_same_filled(void *ptr, unsigned long *element)
{
	unsigned int pos;
	unsigned long *page;
	unsigned long val;

	page = (unsigned long *)ptr;
	val = page[0];

	for (pos = 1; pos < PAGE_SIZE / sizeof(*page); pos++) {
		if (val != page[pos])
			return false;
	}

	*element = val;

	return true;
}

static ssize_t initstate_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u32 val;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	val = init_done(zram);
	up_read(&zram->init_lock);

	return scnprintf(buf, PAGE_SIZE, "%u\n", val);
}

static ssize_t disksize_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", zram->disksize);
}

static ssize_t mem_limit_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	u64 limit;
	char *tmp;
	struct zram *zram = dev_to_zram(dev);

	limit = memparse(buf, &tmp);
	if (buf == tmp) /* no chars parsed, invalid input */
		return -EINVAL;

	down_write(&zram->init_lock);
	zram->limit_pages = PAGE_ALIGN(limit) >> PAGE_SHIFT;
	up_write(&zram->init_lock);

	return len;
}

#ifdef CONFIG_MP_ZSM
static ssize_t zsm_on_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	return scnprintf(buf, PAGE_SIZE, "%d\n", zram->zsm_on);
}
#endif

static ssize_t mem_used_max_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int err;
	unsigned long val;
	struct zram *zram = dev_to_zram(dev);

	err = kstrtoul(buf, 10, &val);
	if (err || val != 0)
		return -EINVAL;

	down_read(&zram->init_lock);
	if (init_done(zram)) {
		atomic_long_set(&zram->stats.max_used_pages,
				zs_get_total_pages(zram->mem_pool));
	}
	up_read(&zram->init_lock);

	return len;
}

static ssize_t idle_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	unsigned long nr_pages = zram->disksize >> PAGE_SHIFT;
	int index;
	char mode_buf[8];
	ssize_t sz;
	sz = strscpy(mode_buf, buf, sizeof(mode_buf));
	if (sz <= 0)
		return -EINVAL;

	/* ignore trailing new line */
	if (mode_buf[sz - 1] == '\n')
		mode_buf[sz - 1] = 0x00;

	if (strcmp(mode_buf, "all"))
		return -EINVAL;

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return -EINVAL;
	}

	for (index = 0; index < nr_pages; index++) {
		/*
		 * Do not mark ZRAM_UNDER_WB slot as ZRAM_IDLE to close race.
		 * See the comment in writeback_store.
		 */
		zram_slot_lock(zram, index);
		if (zram_allocated(zram, index) &&
				!zram_test_flag(zram, index, ZRAM_UNDER_WB))
			zram_set_flag(zram, index, ZRAM_IDLE);
		zram_slot_unlock(zram, index);
	}

	up_read(&zram->init_lock);

	return len;
}

#ifdef CONFIG_MP_ZRAM_PERFORMANCE
static ssize_t collision_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	int i;

    for(i = 0; i < 10; i++)
        printk("%d task collision: %llu\n", i, atomic64_read(&zram->stats.collision[i]));
    printk("%6c %6d %6d %6d %6d %6d %6d %6d %6d %6d %6d\n", 'W', 0, 1, 2, 3, 4, 5, 6, 7, 8, 9);
    printk("===============================================\n");
    for(i = 0; i < 10; i++) {
        printk("R %4d | %6llu %6llu %6llu %6llu %6llu %6llu %6llu %6llu %6llu %6llu\n", i,
               atomic64_read(&zram->stats.rw_collision[i][0]),
               atomic64_read(&zram->stats.rw_collision[i][1]),
               atomic64_read(&zram->stats.rw_collision[i][2]),
               atomic64_read(&zram->stats.rw_collision[i][3]),
               atomic64_read(&zram->stats.rw_collision[i][4]),
               atomic64_read(&zram->stats.rw_collision[i][5]),
               atomic64_read(&zram->stats.rw_collision[i][6]),
               atomic64_read(&zram->stats.rw_collision[i][7]),
               atomic64_read(&zram->stats.rw_collision[i][8]),
               atomic64_read(&zram->stats.rw_collision[i][9])
              );
    }
    return 0;
}

static ssize_t all_performance_info_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
#ifdef CONFIG_MP_MZCCMDQ_HW
	if (MZC_now) {
		MDrv_ldec_cycle_info();
		MDrv_lenc_cycle_info();
	}
#endif
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
	printk("zram total number of hybrid hw times       %llu \n",atomic64_read(&zram->stats.num_hybrid_hw));
#endif
	printk("zram total number of kswapd write times    %llu \n",atomic64_read(&zram->stats.num_kswapd));
	printk("zram total number of write times           %llu \n",atomic64_read(&zram->stats.num_writes));
	printk("zram total write time                      %llu \n",atomic64_read(&zram->stats.write_time));
	printk("zram total number of read times            %llu \n",atomic64_read(&zram->stats.num_reads));
	printk("zram total read time                       %llu \n",atomic64_read(&zram->stats.read_time));
	printk("zram total number of compression times     %llu \n",atomic64_read(&zram->stats.num_compression));
	printk("zram total compression time                %llu \n",atomic64_read(&zram->stats.compression_time));
	printk("zram total compression time wo collision   %llu \n",atomic64_read(&zram->stats.compression_time_wo_collision));
	printk("zram total number of decompression times   %llu \n",atomic64_read(&zram->stats.num_decompression));
	printk("zram total decompression time              %llu \n",atomic64_read(&zram->stats.decompression_time));
	printk("zram total decompression time wo collision %llu \n",atomic64_read(&zram->stats.decompression_time_wo_collision));
	return 0;
}
static DEVICE_ATTR_RO(all_performance_info);
#endif

#ifdef CONFIG_ZRAM_WRITEBACK
static ssize_t writeback_limit_enable_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	u64 val;
	ssize_t ret = -EINVAL;

	if (kstrtoull(buf, 10, &val))
		return ret;

	down_read(&zram->init_lock);
	spin_lock(&zram->wb_limit_lock);
	zram->wb_limit_enable = val;
	spin_unlock(&zram->wb_limit_lock);
	up_read(&zram->init_lock);
	ret = len;

	return ret;
}

static ssize_t writeback_limit_enable_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	bool val;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	spin_lock(&zram->wb_limit_lock);
	val = zram->wb_limit_enable;
	spin_unlock(&zram->wb_limit_lock);
	up_read(&zram->init_lock);

	return scnprintf(buf, PAGE_SIZE, "%d\n", val);
}

static ssize_t writeback_limit_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	u64 val;
	ssize_t ret = -EINVAL;

	if (kstrtoull(buf, 10, &val))
		return ret;

	down_read(&zram->init_lock);
	spin_lock(&zram->wb_limit_lock);
	zram->bd_wb_limit = val;
	spin_unlock(&zram->wb_limit_lock);
	up_read(&zram->init_lock);
	ret = len;

	return ret;
}

static ssize_t writeback_limit_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	u64 val;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	spin_lock(&zram->wb_limit_lock);
	val = zram->bd_wb_limit;
	spin_unlock(&zram->wb_limit_lock);
	up_read(&zram->init_lock);

	return scnprintf(buf, PAGE_SIZE, "%llu\n", val);
}

static void reset_bdev(struct zram *zram)
{
	struct block_device *bdev;

	if (!zram->backing_dev)
		return;

	bdev = zram->bdev;
	if (zram->old_block_size)
		set_blocksize(bdev, zram->old_block_size);
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
	/* hope filp_close flush all of IO */
	filp_close(zram->backing_dev, NULL);
	zram->backing_dev = NULL;
	zram->old_block_size = 0;
	zram->bdev = NULL;
	zram->disk->queue->backing_dev_info->capabilities |=
				BDI_CAP_SYNCHRONOUS_IO;
	kvfree(zram->bitmap);
	zram->bitmap = NULL;
}

static ssize_t backing_dev_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct file *file;
	struct zram *zram = dev_to_zram(dev);
	char *p;
	ssize_t ret;

	down_read(&zram->init_lock);
	file = zram->backing_dev;
	if (!file) {
		memcpy(buf, "none\n", 5);
		up_read(&zram->init_lock);
		return 5;
	}

	p = file_path(file, buf, PAGE_SIZE - 1);
	if (IS_ERR(p)) {
		ret = PTR_ERR(p);
		goto out;
	}

	ret = strlen(p);
	memmove(buf, p, ret);
	buf[ret++] = '\n';
out:
	up_read(&zram->init_lock);
	return ret;
}

static ssize_t backing_dev_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	char *file_name;
	size_t sz;
	struct file *backing_dev = NULL;
	struct inode *inode;
	struct address_space *mapping;
	unsigned int bitmap_sz, old_block_size = 0;
	unsigned long nr_pages, *bitmap = NULL;
	struct block_device *bdev = NULL;
	int err;
	struct zram *zram = dev_to_zram(dev);

	file_name = kmalloc(PATH_MAX, GFP_KERNEL);
	if (!file_name)
		return -ENOMEM;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		pr_info("Can't setup backing device for initialized device\n");
		err = -EBUSY;
		goto out;
	}

	strlcpy(file_name, buf, PATH_MAX);
	/* ignore trailing newline */
	sz = strlen(file_name);
	if (sz > 0 && file_name[sz - 1] == '\n')
		file_name[sz - 1] = 0x00;

	backing_dev = filp_open(file_name, O_RDWR|O_LARGEFILE, 0);
	if (IS_ERR(backing_dev)) {
		err = PTR_ERR(backing_dev);
		backing_dev = NULL;
		goto out;
	}

	mapping = backing_dev->f_mapping;
	inode = mapping->host;

	/* Support only block device in this moment */
	if (!S_ISBLK(inode->i_mode)) {
		err = -ENOTBLK;
		goto out;
	}

	bdev = bdgrab(I_BDEV(inode));
	err = blkdev_get(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL, zram);
	if (err < 0) {
		bdev = NULL;
		goto out;
	}

	nr_pages = i_size_read(inode) >> PAGE_SHIFT;
	bitmap_sz = BITS_TO_LONGS(nr_pages) * sizeof(long);
	bitmap = kvzalloc(bitmap_sz, GFP_KERNEL);
	if (!bitmap) {
		err = -ENOMEM;
		goto out;
	}

	old_block_size = block_size(bdev);
	err = set_blocksize(bdev, PAGE_SIZE);
	if (err)
		goto out;

	reset_bdev(zram);

	zram->old_block_size = old_block_size;
	zram->bdev = bdev;
	zram->backing_dev = backing_dev;
	zram->bitmap = bitmap;
	zram->nr_pages = nr_pages;
	/*
	 * With writeback feature, zram does asynchronous IO so it's no longer
	 * synchronous device so let's remove synchronous io flag. Othewise,
	 * upper layer(e.g., swap) could wait IO completion rather than
	 * (submit and return), which will cause system sluggish.
	 * Furthermore, when the IO function returns(e.g., swap_readpage),
	 * upper layer expects IO was done so it could deallocate the page
	 * freely but in fact, IO is going on so finally could cause
	 * use-after-free when the IO is really done.
	 */
	zram->disk->queue->backing_dev_info->capabilities &=
			~BDI_CAP_SYNCHRONOUS_IO;
	up_write(&zram->init_lock);

	pr_info("setup backing device %s\n", file_name);
	kfree(file_name);

	return len;
out:
	if (bitmap)
		kvfree(bitmap);

	if (bdev)
		blkdev_put(bdev, FMODE_READ | FMODE_WRITE | FMODE_EXCL);

	if (backing_dev)
		filp_close(backing_dev, NULL);

	up_write(&zram->init_lock);

	kfree(file_name);

	return err;
}

static unsigned long alloc_block_bdev(struct zram *zram)
{
	unsigned long blk_idx = 1;
retry:
	/* skip 0 bit to confuse zram.handle = 0 */
	blk_idx = find_next_zero_bit(zram->bitmap, zram->nr_pages, blk_idx);
	if (blk_idx == zram->nr_pages)
		return 0;

	if (test_and_set_bit(blk_idx, zram->bitmap))
		goto retry;

	atomic64_inc(&zram->stats.bd_count);
	return blk_idx;
}

static void free_block_bdev(struct zram *zram, unsigned long blk_idx)
{
	int was_set;

	was_set = test_and_clear_bit(blk_idx, zram->bitmap);
	WARN_ON_ONCE(!was_set);
	atomic64_dec(&zram->stats.bd_count);
}

static void zram_page_end_io(struct bio *bio)
{
	struct page *page = bio_first_page_all(bio);

	page_endio(page, op_is_write(bio_op(bio)),
			blk_status_to_errno(bio->bi_status));
	bio_put(bio);
}

/*
 * Returns 1 if the submission is successful.
 */
static int read_from_bdev_async(struct zram *zram, struct bio_vec *bvec,
			unsigned long entry, struct bio *parent)
{
	struct bio *bio;

	bio = bio_alloc(GFP_ATOMIC, 1);
	if (!bio)
		return -ENOMEM;

	bio->bi_iter.bi_sector = entry * (PAGE_SIZE >> 9);
	bio_set_dev(bio, zram->bdev);
	if (!bio_add_page(bio, bvec->bv_page, bvec->bv_len, bvec->bv_offset)) {
		bio_put(bio);
		return -EIO;
	}

	if (!parent) {
		bio->bi_opf = REQ_OP_READ;
		bio->bi_end_io = zram_page_end_io;
	} else {
		bio->bi_opf = parent->bi_opf;
		bio_chain(bio, parent);
	}

	submit_bio(bio);
	return 1;
}

#define HUGE_WRITEBACK 1
#define IDLE_WRITEBACK 2

static ssize_t writeback_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	unsigned long nr_pages = zram->disksize >> PAGE_SHIFT;
	unsigned long index;
	struct bio bio;
	struct bio_vec bio_vec;
	struct page *page;
	ssize_t ret, sz;
	char mode_buf[8];
	int mode = -1;
	unsigned long blk_idx = 0;

	sz = strscpy(mode_buf, buf, sizeof(mode_buf));
	if (sz <= 0)
		return -EINVAL;

	/* ignore trailing newline */
	if (mode_buf[sz - 1] == '\n')
		mode_buf[sz - 1] = 0x00;

	if (!strcmp(mode_buf, "idle"))
		mode = IDLE_WRITEBACK;
	else if (!strcmp(mode_buf, "huge"))
		mode = HUGE_WRITEBACK;

	if (mode == -1)
		return -EINVAL;

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		ret = -EINVAL;
		goto release_init_lock;
	}

	if (!zram->backing_dev) {
		ret = -ENODEV;
		goto release_init_lock;
	}

	page = alloc_page(GFP_KERNEL);
	if (!page) {
		ret = -ENOMEM;
		goto release_init_lock;
	}

	for (index = 0; index < nr_pages; index++) {
		struct bio_vec bvec;

		bvec.bv_page = page;
		bvec.bv_len = PAGE_SIZE;
		bvec.bv_offset = 0;

		spin_lock(&zram->wb_limit_lock);
		if (zram->wb_limit_enable && !zram->bd_wb_limit) {
			spin_unlock(&zram->wb_limit_lock);
			ret = -EIO;
			break;
		}
		spin_unlock(&zram->wb_limit_lock);

		if (!blk_idx) {
			blk_idx = alloc_block_bdev(zram);
			if (!blk_idx) {
				ret = -ENOSPC;
				break;
			}
		}

		zram_slot_lock(zram, index);
		if (!zram_allocated(zram, index))
			goto next;

		if (zram_test_flag(zram, index, ZRAM_WB) ||
				zram_test_flag(zram, index, ZRAM_SAME) ||
				zram_test_flag(zram, index, ZRAM_UNDER_WB))
			goto next;

		if (mode == IDLE_WRITEBACK &&
			  !zram_test_flag(zram, index, ZRAM_IDLE))
			goto next;
		if (mode == HUGE_WRITEBACK &&
			  !zram_test_flag(zram, index, ZRAM_HUGE))
			goto next;
		/*
		 * Clearing ZRAM_UNDER_WB is duty of caller.
		 * IOW, zram_free_page never clear it.
		 */
		zram_set_flag(zram, index, ZRAM_UNDER_WB);
		/* Need for hugepage writeback racing */
		zram_set_flag(zram, index, ZRAM_IDLE);
		zram_slot_unlock(zram, index);
		if (zram_bvec_read(zram, &bvec, index, 0, NULL)) {
			zram_slot_lock(zram, index);
			zram_clear_flag(zram, index, ZRAM_UNDER_WB);
			zram_clear_flag(zram, index, ZRAM_IDLE);
			zram_slot_unlock(zram, index);
			continue;
		}

		bio_init(&bio, &bio_vec, 1);
		bio_set_dev(&bio, zram->bdev);
		bio.bi_iter.bi_sector = blk_idx * (PAGE_SIZE >> 9);
		bio.bi_opf = REQ_OP_WRITE | REQ_SYNC;

		bio_add_page(&bio, bvec.bv_page, bvec.bv_len,
				bvec.bv_offset);
		/*
		 * XXX: A single page IO would be inefficient for write
		 * but it would be not bad as starter.
		 */
		ret = submit_bio_wait(&bio);
		if (ret) {
			zram_slot_lock(zram, index);
			zram_clear_flag(zram, index, ZRAM_UNDER_WB);
			zram_clear_flag(zram, index, ZRAM_IDLE);
			zram_slot_unlock(zram, index);
			continue;
		}

		atomic64_inc(&zram->stats.bd_writes);
		/*
		 * We released zram_slot_lock so need to check if the slot was
		 * changed. If there is freeing for the slot, we can catch it
		 * easily by zram_allocated.
		 * A subtle case is the slot is freed/reallocated/marked as
		 * ZRAM_IDLE again. To close the race, idle_store doesn't
		 * mark ZRAM_IDLE once it found the slot was ZRAM_UNDER_WB.
		 * Thus, we could close the race by checking ZRAM_IDLE bit.
		 */
		zram_slot_lock(zram, index);
		if (!zram_allocated(zram, index) ||
			  !zram_test_flag(zram, index, ZRAM_IDLE)) {
			zram_clear_flag(zram, index, ZRAM_UNDER_WB);
			zram_clear_flag(zram, index, ZRAM_IDLE);
			goto next;
		}

		zram_free_page(zram, index);
		zram_clear_flag(zram, index, ZRAM_UNDER_WB);
		zram_set_flag(zram, index, ZRAM_WB);
		zram_set_element(zram, index, blk_idx);
		blk_idx = 0;
		atomic64_inc(&zram->stats.pages_stored);
		spin_lock(&zram->wb_limit_lock);
		if (zram->wb_limit_enable && zram->bd_wb_limit > 0)
			zram->bd_wb_limit -=  1UL << (PAGE_SHIFT - 12);
		spin_unlock(&zram->wb_limit_lock);
next:
		zram_slot_unlock(zram, index);
	}

	if (blk_idx)
		free_block_bdev(zram, blk_idx);
	ret = len;
	__free_page(page);
release_init_lock:
	up_read(&zram->init_lock);

	return ret;
}

struct zram_work {
	struct work_struct work;
	struct zram *zram;
	unsigned long entry;
	struct bio *bio;
	struct bio_vec bvec;
};

#if PAGE_SIZE != 4096
static void zram_sync_read(struct work_struct *work)
{
	struct zram_work *zw = container_of(work, struct zram_work, work);
	struct zram *zram = zw->zram;
	unsigned long entry = zw->entry;
	struct bio *bio = zw->bio;

	read_from_bdev_async(zram, &zw->bvec, entry, bio);
}

/*
 * Block layer want one ->make_request_fn to be active at a time
 * so if we use chained IO with parent IO in same context,
 * it's a deadlock. To avoid, it, it uses worker thread context.
 */
static int read_from_bdev_sync(struct zram *zram, struct bio_vec *bvec,
				unsigned long entry, struct bio *bio)
{
	struct zram_work work;

	work.bvec = *bvec;
	work.zram = zram;
	work.entry = entry;
	work.bio = bio;

	INIT_WORK_ONSTACK(&work.work, zram_sync_read);
	queue_work(system_unbound_wq, &work.work);
	flush_work(&work.work);
	destroy_work_on_stack(&work.work);

	return 1;
}
#else
static int read_from_bdev_sync(struct zram *zram, struct bio_vec *bvec,
				unsigned long entry, struct bio *bio)
{
	WARN_ON(1);
	return -EIO;
}
#endif

static int read_from_bdev(struct zram *zram, struct bio_vec *bvec,
			unsigned long entry, struct bio *parent, bool sync)
{
	atomic64_inc(&zram->stats.bd_reads);
	if (sync)
		return read_from_bdev_sync(zram, bvec, entry, parent);
	else
		return read_from_bdev_async(zram, bvec, entry, parent);
}
#else
static inline void reset_bdev(struct zram *zram) {};
static int read_from_bdev(struct zram *zram, struct bio_vec *bvec,
			unsigned long entry, struct bio *parent, bool sync)
{
	return -EIO;
}

static void free_block_bdev(struct zram *zram, unsigned long blk_idx) {};
#endif

#ifdef CONFIG_ZRAM_MEMORY_TRACKING

static struct dentry *zram_debugfs_root;

static void zram_debugfs_create(void)
{
	zram_debugfs_root = debugfs_create_dir("zram", NULL);
}

static void zram_debugfs_destroy(void)
{
	debugfs_remove_recursive(zram_debugfs_root);
}

static void zram_accessed(struct zram *zram, u32 index)
{
	zram_clear_flag(zram, index, ZRAM_IDLE);
	zram->table[index].ac_time = ktime_get_boottime();
}

static ssize_t read_block_state(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	char *kbuf;
	ssize_t index, written = 0;
	struct zram *zram = file->private_data;
	unsigned long nr_pages = zram->disksize >> PAGE_SHIFT;
	struct timespec64 ts;

	kbuf = kvmalloc(count, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		kvfree(kbuf);
		return -EINVAL;
	}

	for (index = *ppos; index < nr_pages; index++) {
		int copied;

		zram_slot_lock(zram, index);
		if (!zram_allocated(zram, index))
			goto next;

		ts = ktime_to_timespec64(zram->table[index].ac_time);
		copied = snprintf(kbuf + written, count,
			"%12zd %12lld.%06lu %c%c%c%c\n",
			index, (s64)ts.tv_sec,
			ts.tv_nsec / NSEC_PER_USEC,
			zram_test_flag(zram, index, ZRAM_SAME) ? 's' : '.',
			zram_test_flag(zram, index, ZRAM_WB) ? 'w' : '.',
			zram_test_flag(zram, index, ZRAM_HUGE) ? 'h' : '.',
			zram_test_flag(zram, index, ZRAM_IDLE) ? 'i' : '.');

		if (count < copied) {
			zram_slot_unlock(zram, index);
			break;
		}
		written += copied;
		count -= copied;
next:
		zram_slot_unlock(zram, index);
		*ppos += 1;
	}

	up_read(&zram->init_lock);
	if (copy_to_user(buf, kbuf, written))
		written = -EFAULT;
	kvfree(kbuf);

	return written;
}

static const struct file_operations proc_zram_block_state_op = {
	.open = simple_open,
	.read = read_block_state,
	.llseek = default_llseek,
};

static void zram_debugfs_register(struct zram *zram)
{
	if (!zram_debugfs_root)
		return;

	zram->debugfs_dir = debugfs_create_dir(zram->disk->disk_name,
						zram_debugfs_root);
	debugfs_create_file("block_state", 0400, zram->debugfs_dir,
				zram, &proc_zram_block_state_op);
}

static void zram_debugfs_unregister(struct zram *zram)
{
	debugfs_remove_recursive(zram->debugfs_dir);
}
#else
static void zram_debugfs_create(void) {};
static void zram_debugfs_destroy(void) {};
static void zram_accessed(struct zram *zram, u32 index)
{
	zram_clear_flag(zram, index, ZRAM_IDLE);
};
static void zram_debugfs_register(struct zram *zram) {};
static void zram_debugfs_unregister(struct zram *zram) {};
#endif

/*
 * We switched to per-cpu streams and this attr is not needed anymore.
 * However, we will keep it around for some time, because:
 * a) we may revert per-cpu streams in the future
 * b) it's visible to user space and we need to follow our 2 years
 *    retirement rule; but we already have a number of 'soon to be
 *    altered' attrs, so max_comp_streams need to wait for the next
 *    layoff cycle.
 */
static ssize_t max_comp_streams_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%d\n", num_online_cpus());
}

static ssize_t max_comp_streams_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	return len;
}

static ssize_t comp_algorithm_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	size_t sz;
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	sz = zcomp_available_show(zram->compressor, buf);
	up_read(&zram->init_lock);

	return sz;
}

static ssize_t comp_algorithm_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	char compressor[ARRAY_SIZE(zram->compressor)];
	size_t sz;

	strlcpy(compressor, buf, sizeof(compressor));
	/* ignore trailing newline */
	sz = strlen(compressor);
	if (sz > 0 && compressor[sz - 1] == '\n')
		compressor[sz - 1] = 0x00;

	if (!zcomp_available_algorithm(compressor))
		return -EINVAL;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		up_write(&zram->init_lock);
		pr_info("Can't change algorithm for initialized device\n");
		return -EBUSY;
	}

	strcpy(zram->compressor, compressor);
#if defined(CONFIG_MP_MZCCMDQ_HW)
	if (strstr(compressor,"mzc"))
		MZC_now = true;
	else
		MZC_now = false;
#endif
	up_write(&zram->init_lock);
	return len;
}

static ssize_t compact_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);

	down_read(&zram->init_lock);
	if (!init_done(zram)) {
		up_read(&zram->init_lock);
		return -EINVAL;
	}

	zs_compact(zram->mem_pool);
	up_read(&zram->init_lock);

	return len;
}

static ssize_t io_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;

	down_read(&zram->init_lock);
	ret = scnprintf(buf, PAGE_SIZE,
			"%8llu %8llu %8llu %8llu\n",
			(u64)atomic64_read(&zram->stats.failed_reads),
			(u64)atomic64_read(&zram->stats.failed_writes),
			(u64)atomic64_read(&zram->stats.invalid_io),
			(u64)atomic64_read(&zram->stats.notify_free));
	up_read(&zram->init_lock);

	return ret;
}

#ifdef CONFIG_MP_ZSM
static ssize_t zsm_idx_rb_verify_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;
	int ret_non4KB, ret_4KB;
	/* non4KB */
	int index;
	for (index = 0; index < ZSM_Compr_TREE_COUNT; index++) {
		zsm_lock_tree(&zram->tree_lock_Compr[index]);
		ret_non4KB = idx_rb_verify_rules(&zram->tree_root_Compr[index]);
		zsm_unlock_tree(&zram->tree_lock_Compr[index]);
	}
	/* 4KB */
	zsm_lock_tree(&zram->tree_lock_nCompr);
	ret_4KB = idx_rb_verify_rules(&zram->tree_root_nCompr);
	zsm_unlock_tree(&zram->tree_lock_nCompr);
	if (ret_non4KB == IDX_RB_VERIFY_SUCCESS &&
			ret_4KB == IDX_RB_VERIFY_SUCCESS) {
		ret = scnprintf(buf, PAGE_SIZE,"ZSM: IDX_RB_TREE Verification"
				" [Successfully] for all idx_rb_tree\n");
	} else {
		ret = scnprintf(buf, PAGE_SIZE, "ZSM: IDX_RB_TREE Verification"
				" [Failed]\n");
	}
	return ret;
}
#endif

#ifdef CONFIG_MP_ZSM_STAT
static ssize_t zsm_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	struct zs_pool_stats pool_stats;
	u64 mem_used = 0;
	ssize_t ret;
	/* zsm static statistics */
	u64 orig_page_cnt;
	u64 real_Compr_pg_cnt;
	u64 real_nCompr_pg_cnt;
	u64 real_total_pg_cnt;
	u64 real_total_compr_size;
	u64 real_Compr_size;
	u64 real_nCompr_size;
	u64 zsm_total_saved_page_cnt;
	u64 zsm_saved_Compr_pg_cnt;
	u64 zsm_saved_nCompr_pg_cnt;
	u64 zsm_saved_total_Compr_size;
    u64 zsm_saved_Compr_sz;
    u64 zsm_saved_nCompr_sz;
	u64 zram_same_cnt;

	memset(&pool_stats, 0x00, sizeof(struct zs_pool_stats));

	down_read(&zram->init_lock);
	if (init_done(zram)) {
		mem_used = zs_get_total_pages(zram->mem_pool);
		zs_pool_stats(zram->mem_pool, &pool_stats);
	}
	/* page count */
	zram_same_cnt = atomic64_read(&zram->stats.same_pages);
	orig_page_cnt = atomic64_read(&zram->stats.pages_stored);
	real_Compr_pg_cnt = atomic64_read(&zram->stats.real_Compr_pg_cnt);
	real_nCompr_pg_cnt = atomic64_read(&zram->stats.real_nCompr_pg_cnt);
	real_total_pg_cnt = real_Compr_pg_cnt + real_nCompr_pg_cnt;
    zsm_saved_Compr_pg_cnt = atomic64_read(
			&zram->stats.zsm_saved_Compr_pg_cnt);
    zsm_saved_nCompr_pg_cnt = 
		atomic64_read(&zram->stats.zsm_saved_nCompr_pg_cnt);
	zsm_total_saved_page_cnt = zsm_saved_Compr_pg_cnt +
		zsm_saved_nCompr_pg_cnt;
	/* size */
	real_total_compr_size = (u64)atomic64_read(&zram->stats.compr_data_size);
	real_nCompr_size = real_nCompr_pg_cnt * PAGE_SIZE;
	real_Compr_size = real_total_compr_size - real_nCompr_size;
    zsm_saved_Compr_sz = atomic64_read(&zram->stats.zsm_saved_Compr_sz);
    zsm_saved_nCompr_sz = atomic64_read(&zram->stats.zsm_saved_nCompr_sz);
	zsm_saved_total_Compr_size = zsm_saved_Compr_sz + zsm_saved_nCompr_sz;
	if ((real_total_pg_cnt + zsm_total_saved_page_cnt + zram_same_cnt) 
			!= orig_page_cnt) {
		pr_alert("------------------------------------------\n");
		pr_alert("[ZSM_STAT] stat (page count) is wrong.\n"
				"May be the numbers are not updated yet.\n");
		pr_alert("real_total_pg_cnt=%llu; zsm_total_saved_page_cnt=%llu; "
				"zram_same_cnt=%llu; orig_page_cnt=%llu;\n",
				real_total_pg_cnt, zsm_total_saved_page_cnt, zram_same_cnt,
				orig_page_cnt);
		pr_alert("------------------------------------------\n");
	}
	ret = scnprintf(buf, PAGE_SIZE,
            "ZRAM:\n"
			"%14s %14s %14s %14s %14s %14s\n"
			"Real stat(with ZSM):\n"
			"%14llu %14llu %14llu %14llu %14llu %14llu\n"
			"ZSM stat (SAVED):\n"
			"%14llu %14llu %14llu %14llu %14llu %14llu\n"
			"Original stat (without ZSM):\n"
			"%14llu %14llu %14llu %14llu %14llu %14llu\n"
			"ZRAM_SAME: %llu\n",
			/* labels */
			"total_PgCnt", "Compr_PgCnt", "nonCompr_PgCnt",
			"Compr_size", "Compr_size", "nonCompr_size",
			/* Real stat */
			real_total_pg_cnt, real_Compr_pg_cnt, real_nCompr_pg_cnt,
			real_total_compr_size, real_Compr_size, real_nCompr_size,
			/* ZSM stat */
			zsm_total_saved_page_cnt, zsm_saved_Compr_pg_cnt, 
			zsm_saved_nCompr_pg_cnt, zsm_saved_total_Compr_size,
			zsm_saved_Compr_sz, zsm_saved_nCompr_sz,
			/* Original stat*/
			orig_page_cnt,
			real_Compr_pg_cnt + zsm_saved_Compr_pg_cnt,
			real_nCompr_pg_cnt + zsm_saved_nCompr_pg_cnt,
			real_total_compr_size + zsm_saved_total_Compr_size,
			real_Compr_size + zsm_saved_Compr_sz,
			real_nCompr_size + zsm_saved_nCompr_sz,
			/* ZRAM_SAME */
			zram_same_cnt
			);
	up_read(&zram->init_lock);

	return ret;
}
#endif

static ssize_t mm_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	struct zs_pool_stats pool_stats;
	u64 orig_size, mem_used = 0;
	long max_used;
	ssize_t ret;

	memset(&pool_stats, 0x00, sizeof(struct zs_pool_stats));

	down_read(&zram->init_lock);
	if (init_done(zram)) {
		mem_used = zs_get_total_pages(zram->mem_pool);
		zs_pool_stats(zram->mem_pool, &pool_stats);
	}

	orig_size = atomic64_read(&zram->stats.pages_stored);
	max_used = atomic_long_read(&zram->stats.max_used_pages);

	ret = scnprintf(buf, PAGE_SIZE,
			"%8llu %8llu %8llu %8lu %8ld %8llu %8lu %8llu\n",
			orig_size << PAGE_SHIFT,
			(u64)atomic64_read(&zram->stats.compr_data_size),
			mem_used << PAGE_SHIFT,
			zram->limit_pages << PAGE_SHIFT,
			max_used << PAGE_SHIFT,
			(u64)atomic64_read(&zram->stats.same_pages),
			pool_stats.pages_compacted,
			(u64)atomic64_read(&zram->stats.huge_pages));
	up_read(&zram->init_lock);

	return ret;
}

#ifdef CONFIG_ZRAM_WRITEBACK
#define FOUR_K(x) ((x) * (1 << (PAGE_SHIFT - 12)))
static ssize_t bd_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;

	down_read(&zram->init_lock);
	ret = scnprintf(buf, PAGE_SIZE,
		"%8llu %8llu %8llu\n",
			FOUR_K((u64)atomic64_read(&zram->stats.bd_count)),
			FOUR_K((u64)atomic64_read(&zram->stats.bd_reads)),
			FOUR_K((u64)atomic64_read(&zram->stats.bd_writes)));
	up_read(&zram->init_lock);

	return ret;
}
#endif

static ssize_t debug_stat_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int version = 1;
	struct zram *zram = dev_to_zram(dev);
	ssize_t ret;

	down_read(&zram->init_lock);
	ret = scnprintf(buf, PAGE_SIZE,
			"version: %d\n%8llu %8llu\n",
			version,
			(u64)atomic64_read(&zram->stats.writestall),
			(u64)atomic64_read(&zram->stats.miss_free));
	up_read(&zram->init_lock);

	return ret;
}

static DEVICE_ATTR_RO(io_stat);
static DEVICE_ATTR_RO(mm_stat);
#ifdef CONFIG_ZRAM_WRITEBACK
static DEVICE_ATTR_RO(bd_stat);
#endif
static DEVICE_ATTR_RO(debug_stat);

static void zram_meta_free(struct zram *zram, u64 disksize)
{
	size_t num_pages = disksize >> PAGE_SHIFT;
	size_t index;

	/* Free all pages that are still in this zram device */
	for (index = 0; index < num_pages; index++)
		zram_free_page(zram, index);

	zs_destroy_pool(zram->mem_pool);
	vfree(zram->table);
}

static bool zram_meta_alloc(struct zram *zram, u64 disksize)
{
	size_t num_pages;

	num_pages = disksize >> PAGE_SHIFT;
	zram->table = vzalloc(array_size(num_pages, sizeof(*zram->table)));
	if (!zram->table)
		return false;

#ifdef CONFIG_MP_ZSM
	u32 idx;
    for (idx = 0; idx < ZSM_Compr_TREE_COUNT; idx++) {
		zsm_init_tree_lock(&zram->tree_lock_Compr[idx]);
	}
    zsm_init_tree_lock(&zram->tree_lock_nCompr);
	pr_err("[ZSM] entry size = %zu*%zu (B) = %zu (KB)\n",
			sizeof(struct zram_table_entry), num_pages,
			(sizeof(struct zram_table_entry)*num_pages) >> 10);
    if (num_pages > (idx_rb_last_avail_idx + 1)) {
        pr_err("[ZSM] indices left for links is not enough.\n");
        pr_alert("[ZSM] The maximum zram size with ZSM is %u pages\n",
				(u32)(idx_rb_last_avail_idx + 1));
        BUG_ON(1);
    }
    for (idx = 0; idx < num_pages; idx++) {
        zsm_set_crc_next(zram, idx, idx);
    } 
	/* 4k and non4k idx_rbtree share the same table */
	for (idx = 0; idx < ZSM_Compr_TREE_COUNT; idx++) {
		zram->tree_root_Compr[idx] = IDX_RB_ROOT(zram->table,
				struct zram_table_entry, _idx_rb_node);
	}
	zram->tree_root_nCompr = IDX_RB_ROOT(zram->table, struct zram_table_entry,
			_idx_rb_node);
#endif

	zram->mem_pool = zs_create_pool(zram->disk->disk_name);
	if (!zram->mem_pool) {
		vfree(zram->table);
		return false;
	}

	printk("[1]huge_class_size = %zu\n", huge_class_size);
	if (!huge_class_size)
		huge_class_size = zs_huge_class_size(zram->mem_pool);
	printk("[2]huge_class_size = %zu\n", huge_class_size);
	return true;
}

/*
 * To protect concurrent access to the same index entry,
 * caller should hold this table index entry's bit_spinlock to
 * indicate this index entry is accessing.
 */
static void zram_free_page(struct zram *zram, size_t index)
{
#ifdef CONFIG_MP_ZSM_DEBUG
	zsm_record_history(zram, index, "Before Free.");
#endif
	unsigned long handle;

#ifdef CONFIG_ZRAM_MEMORY_TRACKING
	zram->table[index].ac_time = 0;
#endif
	if (zram_test_flag(zram, index, ZRAM_IDLE))
		zram_clear_flag(zram, index, ZRAM_IDLE);

	if (zram_test_flag(zram, index, ZRAM_HUGE)) {
		zram_clear_flag(zram, index, ZRAM_HUGE);
		atomic64_dec(&zram->stats.huge_pages);
	}

	if (zram_test_flag(zram, index, ZRAM_WB)) {
		zram_clear_flag(zram, index, ZRAM_WB);
		free_block_bdev(zram, zram_get_element(zram, index));
		goto out;
	}

	/*
	 * No memory is allocated for same element filled pages.
	 * Simply clear same page flag.
	 */
	if (zram_test_flag(zram, index, ZRAM_SAME)) {
		zram_clear_flag(zram, index, ZRAM_SAME);
		atomic64_dec(&zram->stats.same_pages);
		goto out;
	}

	handle = zram_get_handle(zram, index);
	if (!handle)
		return;

#ifdef CONFIG_MP_ZSM
	int ret = -1;
	if (zram->zsm_on) {
		ret = zsm_info_cleanup(zram, index, zram->tree_lock_Compr,
				zram->tree_root_Compr, &zram->tree_lock_nCompr,
				&zram->tree_root_nCompr);
	}
    if (ret == 0 || !zram->zsm_on) {
        zs_free(zram->mem_pool, handle);
		atomic64_sub(zram_get_obj_size(zram, index),
				&zram->stats.compr_data_size);
#ifdef CONFIG_MP_ZSM_STAT
		if (zram_get_obj_size(zram, index) == PAGE_SIZE) {
			atomic64_dec(&zram->stats.real_nCompr_pg_cnt);
		} else {
			atomic64_dec(&zram->stats.real_Compr_pg_cnt);
		}
#endif
    }
#else
	zs_free(zram->mem_pool, handle);

	atomic64_sub(zram_get_obj_size(zram, index),
			&zram->stats.compr_data_size);
#endif
out:
	atomic64_dec(&zram->stats.pages_stored);
	zram_set_handle(zram, index, 0);
	zram_set_obj_size(zram, index, 0);
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
	WARN_ON_ONCE(zram->table[index].flags &
		~(1UL << ZRAM_LOCK | 1UL << ZRAM_UNDER_WB | 1UL << ZRAM_IS_MZC));
#else
	WARN_ON_ONCE(zram->table[index].flags &
		~(1UL << ZRAM_LOCK | 1UL << ZRAM_UNDER_WB));
#endif
#ifdef CONFIG_MP_ZSM_DEBUG
	zsm_record_history(zram, index, "After Free-W/ H");
#endif
}

static int __zram_bvec_read(struct zram *zram, struct page *page, u32 index,
				struct bio *bio, bool partial_io)
{
	int ret;
	unsigned long handle;
	unsigned int size;
	void *src, *dst;
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
	struct timeval decompress_begin,decompress_end;
#endif
	zram_slot_lock(zram, index);
	if (zram_test_flag(zram, index, ZRAM_WB)) {
		struct bio_vec bvec;

		zram_slot_unlock(zram, index);

		bvec.bv_page = page;
		bvec.bv_len = PAGE_SIZE;
		bvec.bv_offset = 0;
		return read_from_bdev(zram, &bvec,
				zram_get_element(zram, index),
				bio, partial_io);
	}

	handle = zram_get_handle(zram, index);
	if (!handle || zram_test_flag(zram, index, ZRAM_SAME)) {
		unsigned long value;
		void *mem;

		value = handle ? zram_get_element(zram, index) : 0;
		mem = kmap_atomic(page);
		zram_fill_page(mem, PAGE_SIZE, value);
		kunmap_atomic(mem);
		zram_slot_unlock(zram, index);
		return 0;
	}

	size = zram_get_obj_size(zram, index);

#ifdef CONFIG_MP_MZCCMDQ_HW_SPLIT
	bool *is_mzc = &get_cpu_var(dec_is_mzc);
	*is_mzc = zram_test_flag(zram, index, ZRAM_IS_MZC);
	put_cpu_var(dec_is_mzc);
#endif
    src = zs_map_object(zram->mem_pool, handle, ZS_MM_RO);
	if (size == PAGE_SIZE) {
		dst = kmap_atomic(page);
		memcpy(dst, src, PAGE_SIZE);
		kunmap_atomic(dst);
		ret = 0;
	} else {
		struct zcomp_strm *zstrm = zcomp_stream_get(zram->comp);
		dst = kmap_atomic(page);
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
		atomic64_inc(&zram->stats.num_decompression);
		do_gettimeofday(&decompress_begin);
#endif
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
		zstrm->tfm->base.is_mzc = zram_test_flag(zram, index, ZRAM_IS_MZC);
#endif
#ifdef CONFIG_MP_MZCCMDQ_HW_SPLIT
		if (!src)
		{
			unsigned long  in_addr1 = get_cpu_var(addr1);
			unsigned long in_addr2 = get_cpu_var(addr2);
			zstrm->tfm->base.addr1 = in_addr1;
			zstrm->tfm->base.addr2 = in_addr2;
			put_cpu_var(addr1);
			put_cpu_var(addr2);

		}
#endif
    	ret = zcomp_decompress(zstrm, src, size, dst);
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
		do_gettimeofday(&decompress_end);
		atomic64_add(timediff(decompress_begin,decompress_end),&zram->stats.decompression_time);
#if defined(CONFIG_MP_MZCCMDQ_HYBRID_HW)
		if (MZC_now && zstrm->tfm->base.is_mzc)
			atomic64_add(duration_dec,&zram->stats.decompression_time_wo_collision);
		else
#elif defined(CONFIG_MP_MZCCMDQ_HW)
		if (MZC_now)
			atomic64_add(duration_dec,&zram->stats.decompression_time_wo_collision);
		else
#endif
		atomic64_add(timediff(decompress_begin,decompress_end),&zram->stats.decompression_time_wo_collision);
#endif
		kunmap_atomic(dst);
		zcomp_stream_put(zram->comp);
	}
	zs_unmap_object(zram->mem_pool, handle);
	zram_slot_unlock(zram, index);

	/* Should NEVER happen. Return bio error if it does. */
	if (unlikely(ret))
		pr_err("Decompression failed! err=%d, page=%u\n", ret, index);

	return ret;
}

static int zram_bvec_read(struct zram *zram, struct bio_vec *bvec,
				u32 index, int offset, struct bio *bio)
{
	int ret;
	struct page *page;

	page = bvec->bv_page;
	if (is_partial_io(bvec)) {
		/* Use a temporary buffer to decompress the page */
		page = alloc_page(GFP_NOIO|__GFP_HIGHMEM);
		if (!page)
			return -ENOMEM;
	}

	ret = __zram_bvec_read(zram, page, index, bio, is_partial_io(bvec));
	if (unlikely(ret))
		goto out;

	if (is_partial_io(bvec)) {
		void *dst = kmap_atomic(bvec->bv_page);
		void *src = kmap_atomic(page);

		memcpy(dst + bvec->bv_offset, src + offset, bvec->bv_len);
		kunmap_atomic(src);
		kunmap_atomic(dst);
	}
out:
	if (is_partial_io(bvec))
		__free_page(page);

	return ret;
}

static int __zram_bvec_write(struct zram *zram, struct bio_vec *bvec,
				u32 index, struct bio *bio)
{
	int ret = 0;
	unsigned long alloced_pages;
	unsigned long handle = 0;
	unsigned int comp_len = 0;
	void *src, *dst, *mem;
	struct zcomp_strm *zstrm;
	struct page *page = bvec->bv_page;
	unsigned long element = 0;
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
	int orig_algo = INITIAL_VALUE;
#endif
	enum zram_pageflags flags = 0;
#ifdef CONFIG_MP_ZSM
    u32 checksum = 0;
	zsm_tree_lock *tree_lock = NULL;
	struct idx_rb_root *tree_root = NULL;
	struct zsm_connect search_results;
	bool alloc_twice_succ = false;
#endif
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
	struct timeval compress_begin,compress_end;
#endif

	mem = kmap_atomic(page);
	if (page_same_filled(mem, &element)) {
		kunmap_atomic(mem);
#ifdef CONFIG_MP_ZSM
		zram_slot_lock(zram, index);
		/* remove ZSM info */
		zram_free_page(zram, index);
		zram_slot_unlock(zram, index);
#endif
		/* Free memory associated with this sector now. */
		flags = ZRAM_SAME;
		atomic64_inc(&zram->stats.same_pages);
		goto out;
	}
	kunmap_atomic(mem);

compress_again:
	zstrm = zcomp_stream_get(zram->comp);
	src = kmap_atomic(page);
	void *orig_src = src;
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
    atomic64_inc(&zram->stats.num_compression);
	do_gettimeofday(&compress_begin);
#endif
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
    if (MZC_now) {
	    if (!handle)
    	    zstrm->tfm->base.is_mzc = INITIAL_VALUE;
        else
    	    zstrm->tfm->base.is_mzc = orig_algo;
    }
    else
        zstrm->tfm->base.is_mzc = 0;
#endif
    ret = zcomp_compress(zstrm, src, &comp_len);
#ifdef CONFIG_MP_ZSM
	checksum = zstrm->tfm->base.crc32;
#endif
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW
	if (!handle)
		orig_algo = zstrm->tfm->base.is_mzc;
#endif
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
	do_gettimeofday(&compress_end);
	atomic64_add(timediff(compress_begin,compress_end),&zram->stats.compression_time);
#if defined(CONFIG_MP_MZCCMDQ_HYBRID_HW)
	if (MZC_now && zstrm->tfm->base.is_mzc) {
		atomic64_add(duration_enc,&zram->stats.compression_time_wo_collision);
		atomic64_inc(&zram->stats.num_hybrid_hw);
	} else
#elif defined(CONFIG_MP_MZCCMDQ_HW)
	if (MZC_now)
		atomic64_add(duration_enc,&zram->stats.compression_time_wo_collision);
	else
#endif
	atomic64_add(timediff(compress_begin,compress_end),&zram->stats.compression_time_wo_collision);
#endif
	kunmap_atomic(orig_src);
	if (unlikely(ret)) {
		zcomp_stream_put(zram->comp);
		pr_err("Compression failed! err=%d\n", ret);
		zs_free(zram->mem_pool, handle);
		return ret;
	}

	if (comp_len >= huge_class_size)
		comp_len = PAGE_SIZE;
#ifdef CONFIG_MP_ZSM
	if (!zram->zsm_on)
		goto zsm_skip_search;
	if (unlikely(handle)) {
		/* come from slow-path of zs_malloc() with handle */
		alloc_twice_succ = true;
		goto zsm_skip_search;
	}

	if (unlikely(comp_len >= huge_class_size)) {
		tree_root = &zram->tree_root_nCompr;
		tree_lock = &zram->tree_lock_nCompr;
	} else {
		tree_root = &zram->tree_root_Compr[zsm_select_compr_tree(comp_len)];
		tree_lock = &zram->tree_lock_Compr[zsm_select_compr_tree(comp_len)];
	}

	zram_slot_lock(zram, index);
	/* zsm move this zram_free_page beforehand to maintain the zsm info */
	zram_free_page(zram, index);
	zram_zsm_entry_init(zram, index, checksum, comp_len);
	zram_slot_unlock(zram, index);

	zsm_connect_init(&search_results);
	zsm_lock_tree(tree_lock);
	int zsm_search_ret;
	zsm_search_ret = search_page_in_zsm_info(bvec, zram, index, zstrm->buffer,
			checksum, comp_len, page, tree_root, &search_results,
			huge_class_size);
	if (unlikely(zsm_search_ret)) {
		/* found same page */
		insert_page_in_zsm_info(bvec, zram, index, orig_src, checksum, comp_len,
				page, tree_root, &search_results);
		zsm_unlock_tree(tree_lock);
		zcomp_stream_put(zram->comp);
		ret = 0;
		goto zsm_found_out;
	}
zsm_skip_search:
#endif
	/*
	 * handle allocation has 2 paths:
	 * a) fast path is executed with preemption disabled (for
	 *  per-cpu streams) and has __GFP_DIRECT_RECLAIM bit clear,
	 *  since we can't sleep;
	 * b) slow path enables preemption and attempts to allocate
	 *  the page with __GFP_DIRECT_RECLAIM bit set. we have to
	 *  put per-cpu compression stream and, thus, to re-do
	 *  the compression once handle is allocated.
	 *
	 * if we have a 'non-null' handle here then we are coming
	 * from the slow path and handle has already been allocated.
	 */
	if (!handle)
		handle = zs_malloc(zram->mem_pool, comp_len,
				__GFP_KSWAPD_RECLAIM |
				__GFP_NOWARN |
				__GFP_HIGHMEM |
				__GFP_MOVABLE);
	if (!handle) {
#ifdef CONFIG_MP_ZSM
		if (zram->zsm_on) {
			zsm_unlock_tree(tree_lock);
		}
#endif
		zcomp_stream_put(zram->comp);
		atomic64_inc(&zram->stats.writestall);
		handle = zs_malloc(zram->mem_pool, comp_len,
				GFP_NOIO | __GFP_HIGHMEM |
				__GFP_MOVABLE);
		if (handle)
			goto compress_again;
#ifdef CONFIG_MP_ZSM_DEBUG
		zsm_record_history(zram, index, "2 zsmalloc fail");
#endif
		return -ENOMEM;
	}

	alloced_pages = zs_get_total_pages(zram->mem_pool);
	update_used_max(zram, alloced_pages);

	if (zram->limit_pages && alloced_pages > zram->limit_pages) {
#ifdef CONFIG_MP_ZSM
		if (likely(zram->zsm_on && !alloc_twice_succ)) {
			zsm_unlock_tree(tree_lock);
		}
#endif
		zcomp_stream_put(zram->comp);
		zs_free(zram->mem_pool, handle);
		return -ENOMEM;
	}
#ifdef CONFIG_MP_ZSM
	if (zram->zsm_on && !alloc_twice_succ) {
		insert_page_in_zsm_info(bvec, zram, index, NULL, checksum, comp_len,
				page, tree_root, &search_results);
		zsm_unlock_tree(tree_lock);
	}
#endif
	dst = zs_map_object(zram->mem_pool, handle, ZS_MM_WO);

	src = zstrm->buffer;
	if (comp_len == PAGE_SIZE)
		src = kmap_atomic(page);
	memcpy(dst, src, comp_len);
	if (comp_len == PAGE_SIZE)
		kunmap_atomic(src);

	zcomp_stream_put(zram->comp);
	zs_unmap_object(zram->mem_pool, handle);
	atomic64_add(comp_len, &zram->stats.compr_data_size);
#ifdef CONFIG_MP_ZSM_STAT
	if (comp_len == PAGE_SIZE) {
		atomic64_inc(&zram->stats.real_nCompr_pg_cnt);
	} else {
		atomic64_inc(&zram->stats.real_Compr_pg_cnt);
	}
#endif
out:
	/*
	 * Free memory associated with this sector
	 * before overwriting unused sectors.
	 */
	zram_slot_lock(zram, index);
#ifdef CONFIG_MP_ZSM
	if (!zram->zsm_on)
#endif
	zram_free_page(zram, index);
#ifdef CONFIG_MP_MZCCMDQ_HYBRID_HW	
	if (orig_algo == MZC) {
		zram_set_flag(zram, index, ZRAM_IS_MZC);
	} else {
		zram_clear_flag(zram, index, ZRAM_IS_MZC);
	}
#endif
	if (comp_len == PAGE_SIZE) {
		zram_set_flag(zram, index, ZRAM_HUGE);
		atomic64_inc(&zram->stats.huge_pages);
	}

	if (flags) {
		zram_set_flag(zram, index, flags);
		zram_set_element(zram, index, element);
	}  else {
		zram_set_handle(zram, index, handle);
		zram_set_obj_size(zram, index, comp_len);
#ifdef CONFIG_MP_ZSM
		if (zram->zsm_on && !alloc_twice_succ) {
			zsm_lock_tree(tree_lock);
			zsm_set_link_flag(zram, index, ZRAM_ZSM_DONE_NODE);
			zsm_unlock_tree(tree_lock);
		}
#endif
	}
#ifdef CONFIG_MP_ZSM_DEBUG
	zsm_record_history(zram, index, "After Set.");
#endif
	zram_slot_unlock(zram, index);
#ifdef CONFIG_MP_ZSM
zsm_found_out:
#endif
	/* Update stats */
	atomic64_inc(&zram->stats.pages_stored);
	return ret;
}

static int zram_bvec_write(struct zram *zram, struct bio_vec *bvec,
				u32 index, int offset, struct bio *bio)
{
#ifdef CONFIG_MP_ZSM_DEBUG
	zsm_record_history(zram, index, "Before Write.");
#endif
	int ret;
	struct page *page = NULL;
	void *src;
	struct bio_vec vec;

	vec = *bvec;
	if (is_partial_io(bvec)) {
		void *dst;
		/*
		 * This is a partial IO. We need to read the full page
		 * before to write the changes.
		 */
		page = alloc_page(GFP_NOIO|__GFP_HIGHMEM);
		if (!page)
			return -ENOMEM;

		ret = __zram_bvec_read(zram, page, index, bio, true);
		if (ret)
			goto out;

		src = kmap_atomic(bvec->bv_page);
		dst = kmap_atomic(page);
		memcpy(dst + offset, src + bvec->bv_offset, bvec->bv_len);
		kunmap_atomic(dst);
		kunmap_atomic(src);

		vec.bv_page = page;
		vec.bv_len = PAGE_SIZE;
		vec.bv_offset = 0;
	}

	ret = __zram_bvec_write(zram, &vec, index, bio);
out:
	if (is_partial_io(bvec))
		__free_page(page);
#ifdef CONFIG_MP_ZSM_DEBUG
	zsm_record_history(zram, index, "After Write.");
#endif
	return ret;
}

/*
 * zram_bio_discard - handler on discard request
 * @index: physical block index in PAGE_SIZE units
 * @offset: byte offset within physical block
 */
static void zram_bio_discard(struct zram *zram, u32 index,
			     int offset, struct bio *bio)
{
	size_t n = bio->bi_iter.bi_size;

	/*
	 * zram manages data in physical block size units. Because logical block
	 * size isn't identical with physical block size on some arch, we
	 * could get a discard request pointing to a specific offset within a
	 * certain physical block.  Although we can handle this request by
	 * reading that physiclal block and decompressing and partially zeroing
	 * and re-compressing and then re-storing it, this isn't reasonable
	 * because our intent with a discard request is to save memory.  So
	 * skipping this logical block is appropriate here.
	 */
	if (offset) {
		if (n <= (PAGE_SIZE - offset))
			return;

		n -= (PAGE_SIZE - offset);
		index++;
	}

	while (n >= PAGE_SIZE) {
		zram_slot_lock(zram, index);
		zram_free_page(zram, index);
		zram_slot_unlock(zram, index);
		atomic64_inc(&zram->stats.notify_free);
		index++;
		n -= PAGE_SIZE;
	}
}

/*
 * Returns errno if it has some problem. Otherwise return 0 or 1.
 * Returns 0 if IO request was done synchronously
 * Returns 1 if IO request was successfully submitted.
 */
static int zram_bvec_rw(struct zram *zram, struct bio_vec *bvec, u32 index,
			int offset, unsigned int op, struct bio *bio)
{
	unsigned long start_time = jiffies;
	struct request_queue *q = zram->disk->queue;
	int ret;
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
	struct timeval write_begin,write_end,read_begin,read_end;
	int enter, leave, read, write;
    atomic_inc(&enter_idx);
    if (op_is_write(op))
        atomic_inc(&w_idx);
    else
        atomic_inc(&r_idx);

    enter = atomic_read(&enter_idx);
    leave = atomic_read(&leave_idx);

    if ((enter - leave) < MAX_COLLISION) {
        atomic64_inc(&zram->stats.collision[enter-leave-1]);
    } else {
        printk("[ERR][ZRAM] enter=%d, leave=%d\n", enter, leave);
    }

    read = atomic_read(&r_idx);
    write = atomic_read(&w_idx);
    if (read < MAX_COLLISION && write < MAX_COLLISION) {
        atomic64_inc(&zram->stats.rw_collision[read][write]);
    } else {
        printk("[ERR][ZRAM] read=%d, write=%d\n", read, write);
    }
	if (current_is_kswapd())
		atomic64_inc(&zram->stats.num_kswapd);
#endif
	generic_start_io_acct(q, op, bvec->bv_len >> SECTOR_SHIFT,
			&zram->disk->part0);

	if (!op_is_write(op)) {
		atomic64_inc(&zram->stats.num_reads);
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
		do_gettimeofday(&read_begin);
#endif
		ret = zram_bvec_read(zram, bvec, index, offset, bio);
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
		do_gettimeofday(&read_end);
		atomic64_add(timediff(read_begin,read_end),&zram->stats.read_time);
#endif
		flush_dcache_page(bvec->bv_page);
	} else {
		atomic64_inc(&zram->stats.num_writes);
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
		do_gettimeofday(&write_begin);
#endif
		ret = zram_bvec_write(zram, bvec, index, offset, bio);
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
		do_gettimeofday(&write_end);
		atomic64_add(timediff(write_begin,write_end),&zram->stats.write_time);
#endif
	}

	generic_end_io_acct(q, op, &zram->disk->part0, start_time);
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
	if (op_is_write(op))
        atomic_dec(&w_idx);
    else
        atomic_dec(&r_idx);
	atomic_inc(&leave_idx);
#endif
	zram_slot_lock(zram, index);
	zram_accessed(zram, index);
	zram_slot_unlock(zram, index);

	if (unlikely(ret < 0)) {
		if (!op_is_write(op))
			atomic64_inc(&zram->stats.failed_reads);
		else
			atomic64_inc(&zram->stats.failed_writes);
	}
	return ret;
}

static void __zram_make_request(struct zram *zram, struct bio *bio)
{
	int offset;
	u32 index;
	struct bio_vec bvec;
	struct bvec_iter iter;

	index = bio->bi_iter.bi_sector >> SECTORS_PER_PAGE_SHIFT;
	offset = (bio->bi_iter.bi_sector &
		  (SECTORS_PER_PAGE - 1)) << SECTOR_SHIFT;

	switch (bio_op(bio)) {
	case REQ_OP_DISCARD:
	case REQ_OP_WRITE_ZEROES:
		zram_bio_discard(zram, index, offset, bio);
		bio_endio(bio);
		return;
	default:
		break;
	}

	bio_for_each_segment(bvec, bio, iter) {
		struct bio_vec bv = bvec;
		unsigned int unwritten = bvec.bv_len;

		do {
			bv.bv_len = min_t(unsigned int, PAGE_SIZE - offset,
							unwritten);
			if (zram_bvec_rw(zram, &bv, index, offset,
					 bio_op(bio), bio) < 0)
				goto out;

			bv.bv_offset += bv.bv_len;
			unwritten -= bv.bv_len;

			update_position(&index, &offset, &bv);
		} while (unwritten);
	}

	bio_endio(bio);
	return;

out:
	bio_io_error(bio);
}

/*
 * Handler function for all zram I/O requests.
 */
static blk_qc_t zram_make_request(struct request_queue *queue, struct bio *bio)
{
	struct zram *zram = queue->queuedata;

	if (!valid_io_request(zram, bio->bi_iter.bi_sector,
					bio->bi_iter.bi_size)) {
		atomic64_inc(&zram->stats.invalid_io);
		goto error;
	}

	__zram_make_request(zram, bio);
	return BLK_QC_T_NONE;

error:
	bio_io_error(bio);
	return BLK_QC_T_NONE;
}

static void zram_slot_free_notify(struct block_device *bdev,
				unsigned long index)
{
	struct zram *zram;

	zram = bdev->bd_disk->private_data;

	atomic64_inc(&zram->stats.notify_free);
	if (!zram_slot_trylock(zram, index)) {
		atomic64_inc(&zram->stats.miss_free);
		return;
	}
	zram_free_page(zram, index);
	zram_slot_unlock(zram, index);
}

static int zram_rw_page(struct block_device *bdev, sector_t sector,
		       struct page *page, unsigned int op)
{
	int offset, ret;
	u32 index;
	struct zram *zram;
	struct bio_vec bv;

	if (PageTransHuge(page))
		return -ENOTSUPP;
	zram = bdev->bd_disk->private_data;

	if (!valid_io_request(zram, sector, PAGE_SIZE)) {
		atomic64_inc(&zram->stats.invalid_io);
		ret = -EINVAL;
		goto out;
	}

	index = sector >> SECTORS_PER_PAGE_SHIFT;
	offset = (sector & (SECTORS_PER_PAGE - 1)) << SECTOR_SHIFT;

	bv.bv_page = page;
	bv.bv_len = PAGE_SIZE;
	bv.bv_offset = 0;

	ret = zram_bvec_rw(zram, &bv, index, offset, op, NULL);
out:
	/*
	 * If I/O fails, just return error(ie, non-zero) without
	 * calling page_endio.
	 * It causes resubmit the I/O with bio request by upper functions
	 * of rw_page(e.g., swap_readpage, __swap_writepage) and
	 * bio->bi_end_io does things to handle the error
	 * (e.g., SetPageError, set_page_dirty and extra works).
	 */
	if (unlikely(ret < 0))
		return ret;

	switch (ret) {
	case 0:
		page_endio(page, op_is_write(op), 0);
		break;
	case 1:
		ret = 0;
		break;
	default:
		WARN_ON(1);
	}
	return ret;
}

static void zram_reset_device(struct zram *zram)
{
	struct zcomp *comp;
	u64 disksize;

	down_write(&zram->init_lock);

	zram->limit_pages = 0;

	if (!init_done(zram)) {
		up_write(&zram->init_lock);
		return;
	}

	comp = zram->comp;
	disksize = zram->disksize;
	zram->disksize = 0;

	set_capacity(zram->disk, 0);
	part_stat_set_all(&zram->disk->part0, 0);

	up_write(&zram->init_lock);
	/* I/O operation under all of CPU are done so let's free */
	zram_meta_free(zram, disksize);
	memset(&zram->stats, 0, sizeof(zram->stats));
	zcomp_destroy(comp);
	reset_bdev(zram);
}

#ifdef CONFIG_MP_ZSM
static ssize_t zsm_on_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	struct zram *zram = dev_to_zram(dev);
	unsigned long long in;
	in = memparse(buf, NULL);
	if (!init_done(zram)) {
		if (in == 1) {
			zram->zsm_on = true;
		} else {
			zram->zsm_on = false;
		}
	} else {
		pr_alert("[ZRAM] ZSM cannot be enabled after ZRAM disksize is set\n");
	}
	return len;
}
#endif

static ssize_t disksize_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	u64 disksize;
	struct zcomp *comp;
	struct zram *zram = dev_to_zram(dev);
	int err;

	disksize = memparse(buf, NULL);
	if (!disksize)
		return -EINVAL;

	down_write(&zram->init_lock);
	if (init_done(zram)) {
		pr_info("Cannot change disksize for initialized device\n");
		err = -EBUSY;
		goto out_unlock;
	}

	disksize = PAGE_ALIGN(disksize);
	if (!zram_meta_alloc(zram, disksize)) {
		err = -ENOMEM;
		goto out_unlock;
	}

	comp = zcomp_create(zram->compressor);
	if (IS_ERR(comp)) {
		pr_err("Cannot initialise %s compressing backend\n",
				zram->compressor);
		err = PTR_ERR(comp);
		goto out_free_meta;
	}

	zram->comp = comp;
	zram->disksize = disksize;
	set_capacity(zram->disk, zram->disksize >> SECTOR_SHIFT);

	revalidate_disk(zram->disk);
#ifdef CONFIG_MP_ZSM
	zsm_validate_algo(zram);
#endif
	up_write(&zram->init_lock);

	return len;

out_free_meta:
	zram_meta_free(zram, disksize);
out_unlock:
	up_write(&zram->init_lock);
	return err;
}

static ssize_t reset_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t len)
{
	int ret;
	unsigned short do_reset;
	struct zram *zram;
	struct block_device *bdev;

	ret = kstrtou16(buf, 10, &do_reset);
	if (ret)
		return ret;

	if (!do_reset)
		return -EINVAL;

	zram = dev_to_zram(dev);
	bdev = bdget_disk(zram->disk, 0);
	if (!bdev)
		return -ENOMEM;

	mutex_lock(&bdev->bd_mutex);
	/* Do not reset an active device or claimed device */
	if (bdev->bd_openers || zram->claim) {
		mutex_unlock(&bdev->bd_mutex);
		bdput(bdev);
		return -EBUSY;
	}

	/* From now on, anyone can't open /dev/zram[0-9] */
	zram->claim = true;
	mutex_unlock(&bdev->bd_mutex);

	/* Make sure all the pending I/O are finished */
	fsync_bdev(bdev);
	zram_reset_device(zram);
	revalidate_disk(zram->disk);
	bdput(bdev);

	mutex_lock(&bdev->bd_mutex);
	zram->claim = false;
	mutex_unlock(&bdev->bd_mutex);

	return len;
}

static int zram_open(struct block_device *bdev, fmode_t mode)
{
	int ret = 0;
	struct zram *zram;

	WARN_ON(!mutex_is_locked(&bdev->bd_mutex));

	zram = bdev->bd_disk->private_data;
	/* zram was claimed to reset so open request fails */
	if (zram->claim)
		ret = -EBUSY;

	return ret;
}

static const struct block_device_operations zram_devops = {
	.open = zram_open,
	.swap_slot_free_notify = zram_slot_free_notify,
	.rw_page = zram_rw_page,
	.owner = THIS_MODULE
};

static DEVICE_ATTR_WO(compact);
static DEVICE_ATTR_RW(disksize);
static DEVICE_ATTR_RO(initstate);
static DEVICE_ATTR_WO(reset);
static DEVICE_ATTR_WO(mem_limit);
static DEVICE_ATTR_WO(mem_used_max);
static DEVICE_ATTR_WO(idle);
static DEVICE_ATTR_RW(max_comp_streams);
static DEVICE_ATTR_RW(comp_algorithm);
#ifdef CONFIG_ZRAM_WRITEBACK
static DEVICE_ATTR_RW(backing_dev);
static DEVICE_ATTR_WO(writeback);
static DEVICE_ATTR_RW(writeback_limit);
static DEVICE_ATTR_RW(writeback_limit_enable);
#endif
#ifdef CONFIG_MP_ZSM
static DEVICE_ATTR_RO(zsm_idx_rb_verify);
static DEVICE_ATTR_RW(zsm_on);
#endif
#ifdef CONFIG_MP_ZSM_STAT
static DEVICE_ATTR_RO(zsm_stat);
#endif

static struct attribute *zram_disk_attrs[] = {
	&dev_attr_disksize.attr,
	&dev_attr_initstate.attr,
	&dev_attr_reset.attr,
	&dev_attr_compact.attr,
	&dev_attr_mem_limit.attr,
	&dev_attr_mem_used_max.attr,
	&dev_attr_idle.attr,
	&dev_attr_max_comp_streams.attr,
	&dev_attr_comp_algorithm.attr,
#ifdef CONFIG_ZRAM_WRITEBACK
	&dev_attr_backing_dev.attr,
	&dev_attr_writeback.attr,
	&dev_attr_writeback_limit.attr,
	&dev_attr_writeback_limit_enable.attr,
#endif
	&dev_attr_io_stat.attr,
	&dev_attr_mm_stat.attr,
#ifdef CONFIG_ZRAM_WRITEBACK
	&dev_attr_bd_stat.attr,
#endif
	&dev_attr_debug_stat.attr,
#ifdef CONFIG_MP_ZSM
	&dev_attr_zsm_idx_rb_verify.attr,
	&dev_attr_zsm_on.attr,
#endif
#ifdef CONFIG_MP_ZSM_STAT
	&dev_attr_zsm_stat.attr,
#endif
#ifdef CONFIG_MP_ZRAM_PERFORMANCE
	&dev_attr_all_performance_info.attr,
#endif
	NULL,
};

static const struct attribute_group zram_disk_attr_group = {
	.attrs = zram_disk_attrs,
};

static const struct attribute_group *zram_disk_attr_groups[] = {
	&zram_disk_attr_group,
	NULL,
};

/*
 * Allocate and initialize new zram device. the function returns
 * '>= 0' device_id upon success, and negative value otherwise.
 */
static int zram_add(void)
{
	struct zram *zram;
	struct request_queue *queue;
	int ret, device_id;

	zram = kzalloc(sizeof(struct zram), GFP_KERNEL);
	if (!zram)
		return -ENOMEM;

	ret = idr_alloc(&zram_index_idr, zram, 0, 0, GFP_KERNEL);
	if (ret < 0)
		goto out_free_dev;
	device_id = ret;

	init_rwsem(&zram->init_lock);
#ifdef CONFIG_ZRAM_WRITEBACK
	spin_lock_init(&zram->wb_limit_lock);
#endif
#ifdef CONFIG_MP_ZSM
	if (PAGE_SIZE > 4096) {
		pr_err("[ZSM] The ZSM implementation is for 4KB page size, "
				"ZRAM_FLAG_SHIFT=%d and zram_table_entry "
				"need your attention.\n",
				ZRAM_FLAG_SHIFT);
		BUG_ON(1);
	}
	zram->zsm_on = true;
	pr_alert("[ZSM] ZRAM compiled with ZSM, and zsm_on=%d.\n", zram->zsm_on);
	pr_alert("[ZSM] Turn on ZSM by $echo 1 > /sys/block/zram0/zsm_on "
			"before change the ZRAM disksize.\n");
	pr_alert("[ZSM] Check ZSM on/off by $cat /sys/block/zram0/zsm_on\n");
#endif

	queue = blk_alloc_queue(GFP_KERNEL);
	if (!queue) {
		pr_err("Error allocating disk queue for device %d\n",
			device_id);
		ret = -ENOMEM;
		goto out_free_idr;
	}

	blk_queue_make_request(queue, zram_make_request);

	/* gendisk structure */
	zram->disk = alloc_disk(1);
	if (!zram->disk) {
		pr_err("Error allocating disk structure for device %d\n",
			device_id);
		ret = -ENOMEM;
		goto out_free_queue;
	}

	zram->disk->major = zram_major;
	zram->disk->first_minor = device_id;
	zram->disk->fops = &zram_devops;
	zram->disk->queue = queue;
	zram->disk->queue->queuedata = zram;
	zram->disk->private_data = zram;
	snprintf(zram->disk->disk_name, 16, "zram%d", device_id);

	/* Actual capacity set using syfs (/sys/block/zram<id>/disksize */
	set_capacity(zram->disk, 0);
	/* zram devices sort of resembles non-rotational disks */
	blk_queue_flag_set(QUEUE_FLAG_NONROT, zram->disk->queue);
	blk_queue_flag_clear(QUEUE_FLAG_ADD_RANDOM, zram->disk->queue);

	/*
	 * To ensure that we always get PAGE_SIZE aligned
	 * and n*PAGE_SIZED sized I/O requests.
	 */
	blk_queue_physical_block_size(zram->disk->queue, PAGE_SIZE);
	blk_queue_logical_block_size(zram->disk->queue,
					ZRAM_LOGICAL_BLOCK_SIZE);
	blk_queue_io_min(zram->disk->queue, PAGE_SIZE);
	blk_queue_io_opt(zram->disk->queue, PAGE_SIZE);
	zram->disk->queue->limits.discard_granularity = PAGE_SIZE;
	blk_queue_max_discard_sectors(zram->disk->queue, UINT_MAX);
	blk_queue_flag_set(QUEUE_FLAG_DISCARD, zram->disk->queue);

	/*
	 * zram_bio_discard() will clear all logical blocks if logical block
	 * size is identical with physical block size(PAGE_SIZE). But if it is
	 * different, we will skip discarding some parts of logical blocks in
	 * the part of the request range which isn't aligned to physical block
	 * size.  So we can't ensure that all discarded logical blocks are
	 * zeroed.
	 */
	if (ZRAM_LOGICAL_BLOCK_SIZE == PAGE_SIZE)
		blk_queue_max_write_zeroes_sectors(zram->disk->queue, UINT_MAX);

	zram->disk->queue->backing_dev_info->capabilities |=
			(BDI_CAP_STABLE_WRITES | BDI_CAP_SYNCHRONOUS_IO);
	disk_to_dev(zram->disk)->groups = zram_disk_attr_groups;
	add_disk(zram->disk);

	strlcpy(zram->compressor, default_compressor, sizeof(zram->compressor));

	zram_debugfs_register(zram);
	pr_info("Added device: %s\n", zram->disk->disk_name);
	return device_id;

out_free_queue:
	blk_cleanup_queue(queue);
out_free_idr:
	idr_remove(&zram_index_idr, device_id);
out_free_dev:
	kfree(zram);
	return ret;
}

static int zram_remove(struct zram *zram)
{
	struct block_device *bdev;

	bdev = bdget_disk(zram->disk, 0);
	if (!bdev)
		return -ENOMEM;

	mutex_lock(&bdev->bd_mutex);
	if (bdev->bd_openers || zram->claim) {
		mutex_unlock(&bdev->bd_mutex);
		bdput(bdev);
		return -EBUSY;
	}

	zram->claim = true;
	mutex_unlock(&bdev->bd_mutex);

	zram_debugfs_unregister(zram);
	/* Make sure all the pending I/O are finished */
	fsync_bdev(bdev);
	zram_reset_device(zram);
	bdput(bdev);

	pr_info("Removed device: %s\n", zram->disk->disk_name);

	del_gendisk(zram->disk);
	blk_cleanup_queue(zram->disk->queue);
	put_disk(zram->disk);
	kfree(zram);
	return 0;
}

/* zram-control sysfs attributes */

/*
 * NOTE: hot_add attribute is not the usual read-only sysfs attribute. In a
 * sense that reading from this file does alter the state of your system -- it
 * creates a new un-initialized zram device and returns back this device's
 * device_id (or an error code if it fails to create a new device).
 */
static ssize_t hot_add_show(struct class *class,
			struct class_attribute *attr,
			char *buf)
{
	int ret;

	mutex_lock(&zram_index_mutex);
	ret = zram_add();
	mutex_unlock(&zram_index_mutex);

	if (ret < 0)
		return ret;
	return scnprintf(buf, PAGE_SIZE, "%d\n", ret);
}
static struct class_attribute class_attr_hot_add =
	__ATTR(hot_add, 0400, hot_add_show, NULL);

static ssize_t hot_remove_store(struct class *class,
			struct class_attribute *attr,
			const char *buf,
			size_t count)
{
	struct zram *zram;
	int ret, dev_id;

	/* dev_id is gendisk->first_minor, which is `int' */
	ret = kstrtoint(buf, 10, &dev_id);
	if (ret)
		return ret;
	if (dev_id < 0)
		return -EINVAL;

	mutex_lock(&zram_index_mutex);

	zram = idr_find(&zram_index_idr, dev_id);
	if (zram) {
		ret = zram_remove(zram);
		if (!ret)
			idr_remove(&zram_index_idr, dev_id);
	} else {
		ret = -ENODEV;
	}

	mutex_unlock(&zram_index_mutex);
	return ret ? ret : count;
}
static CLASS_ATTR_WO(hot_remove);

static struct attribute *zram_control_class_attrs[] = {
	&class_attr_hot_add.attr,
	&class_attr_hot_remove.attr,
	NULL,
};
ATTRIBUTE_GROUPS(zram_control_class);

static struct class zram_control_class = {
	.name		= "zram-control",
	.owner		= THIS_MODULE,
	.class_groups	= zram_control_class_groups,
};

static int zram_remove_cb(int id, void *ptr, void *data)
{
	zram_remove(ptr);
	return 0;
}

static void destroy_devices(void)
{
	class_unregister(&zram_control_class);
	idr_for_each(&zram_index_idr, &zram_remove_cb, NULL);
	zram_debugfs_destroy();
	idr_destroy(&zram_index_idr);
	unregister_blkdev(zram_major, "zram");
	cpuhp_remove_multi_state(CPUHP_ZCOMP_PREPARE);
}

static int __init zram_init(void)
{
	int ret;

	ret = cpuhp_setup_state_multi(CPUHP_ZCOMP_PREPARE, "block/zram:prepare",
				      zcomp_cpu_up_prepare, zcomp_cpu_dead);
	if (ret < 0)
		return ret;

	ret = class_register(&zram_control_class);
	if (ret) {
		pr_err("Unable to register zram-control class\n");
		cpuhp_remove_multi_state(CPUHP_ZCOMP_PREPARE);
		return ret;
	}
	zram_debugfs_create();
	zram_major = register_blkdev(0, "zram");
	if (zram_major <= 0) {
		pr_err("Unable to get major number\n");
		class_unregister(&zram_control_class);
		cpuhp_remove_multi_state(CPUHP_ZCOMP_PREPARE);
		return -EBUSY;
	}

	while (num_devices != 0) {
		mutex_lock(&zram_index_mutex);
		ret = zram_add();
		mutex_unlock(&zram_index_mutex);
		if (ret < 0)
			goto out_error;
		num_devices--;
	}

	return 0;

out_error:
	destroy_devices();
	return ret;
}

static void __exit zram_exit(void)
{
	destroy_devices();
}

module_init(zram_init);
module_exit(zram_exit);

module_param(num_devices, uint, 0);
MODULE_PARM_DESC(num_devices, "Number of pre-created zram devices");

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Nitin Gupta <ngupta@vflare.org>");
MODULE_DESCRIPTION("Compressed RAM Block Device");
