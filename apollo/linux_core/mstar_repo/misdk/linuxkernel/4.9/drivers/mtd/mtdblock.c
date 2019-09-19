/*
 * Direct MTD block device access
 *
 * Copyright © 1999-2010 David Woodhouse <dwmw2@infradead.org>
 * Copyright © 2000-2003 Nicolas Pitre <nico@fluxnic.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/blktrans.h>
#include <linux/mutex.h>
#if defined(CONFIG_MSTAR_NAND) && (MP_NAND_MTD == 1)
#include <linux/mtd/nand.h>
#endif

struct mtdblk_dev {
	struct mtd_blktrans_dev mbd;
	int count;
	struct mutex cache_mutex;
	unsigned char *cache_data;
	unsigned long cache_offset;
	unsigned int cache_size;
	enum { STATE_EMPTY, STATE_CLEAN, STATE_DIRTY } cache_state;
#if (defined(CONFIG_MSTAR_NAND) || defined(CONFIG_MSTAR_SPI_NAND)) && (MP_NAND_MTD == 1)
	unsigned char	au8_BadBlkTbl[512];
#endif
};

/*
 * Cache stuff...
 *
 * Since typical flash erasable sectors are much larger than what Linux's
 * buffer cache can handle, we must implement read-modify-write on flash
 * sectors for each block write requests.  To avoid over-erasing flash sectors
 * and to speed things up, we locally cache a whole flash sector while it is
 * being written to until a different sector is required.
 */

static void erase_callback(struct erase_info *done)
{
	wait_queue_head_t *wait_q = (wait_queue_head_t *)done->priv;
	wake_up(wait_q);
}

static int erase_write (struct mtd_info *mtd, unsigned long pos,
			int len, const char *buf)
{
	struct erase_info erase;
	DECLARE_WAITQUEUE(wait, current);
	wait_queue_head_t wait_q;
	size_t retlen;
	int ret;

	/*
	 * First, let's erase the flash block.
	 */

	init_waitqueue_head(&wait_q);
	erase.mtd = mtd;
	erase.callback = erase_callback;
	erase.addr = pos;
	erase.len = len;
	erase.priv = (u_long)&wait_q;

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&wait_q, &wait);

	ret = mtd_erase(mtd, &erase);
	if (ret) {
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&wait_q, &wait);
		printk (KERN_WARNING "mtdblock: erase of region [0x%lx, 0x%x] "
				     "on \"%s\" failed\n",
			pos, len, mtd->name);
		return ret;
	}

	schedule();  /* Wait for erase to finish. */
	remove_wait_queue(&wait_q, &wait);

	/*
	 * Next, write the data to flash.
	 */

	ret = mtd_write(mtd, pos, len, &retlen, buf);
	if (ret)
		return ret;
	if (retlen != len)
		return -EIO;
	return 0;
}


static int write_cached_data (struct mtdblk_dev *mtdblk)
{
	struct mtd_info *mtd = mtdblk->mbd.mtd;
	int ret;

	if (mtdblk->cache_state != STATE_DIRTY)
		return 0;

	pr_debug("mtdblock: writing cached data for \"%s\" "
			"at 0x%lx, size 0x%x\n", mtd->name,
			mtdblk->cache_offset, mtdblk->cache_size);

	ret = erase_write (mtd, mtdblk->cache_offset,
			   mtdblk->cache_size, mtdblk->cache_data);
	if (ret)
		return ret;

	/*
	 * Here we could arguably set the cache state to STATE_CLEAN.
	 * However this could lead to inconsistency since we will not
	 * be notified if this content is altered on the flash by other
	 * means.  Let's declare it empty and leave buffering tasks to
	 * the buffer cache instead.
	 */
	mtdblk->cache_state = STATE_EMPTY;
	return 0;
}


static int do_cached_write (struct mtdblk_dev *mtdblk, unsigned long pos,
			    int len, const char *buf)
{
	struct mtd_info *mtd = mtdblk->mbd.mtd;
	unsigned int sect_size = mtdblk->cache_size;
	size_t retlen;
	int ret;

	pr_debug("mtdblock: write on \"%s\" at 0x%lx, size 0x%x\n",
		mtd->name, pos, len);

	if (!sect_size)
		return mtd_write(mtd, pos, len, &retlen, buf);

	while (len > 0) {
		unsigned long sect_start = (pos/sect_size)*sect_size;
		unsigned int offset = pos - sect_start;
		unsigned int size = sect_size - offset;
		if( size > len )
			size = len;

		if (size == sect_size) {
			/*
			 * We are covering a whole sector.  Thus there is no
			 * need to bother with the cache while it may still be
			 * useful for other partial writes.
			 */
			ret = erase_write (mtd, pos, size, buf);
			if (ret)
				return ret;
		} else {
			/* Partial sector: need to use the cache */

			if (mtdblk->cache_state == STATE_DIRTY &&
			    mtdblk->cache_offset != sect_start) {
				ret = write_cached_data(mtdblk);
				if (ret)
					return ret;
			}

			if (mtdblk->cache_state == STATE_EMPTY ||
			    mtdblk->cache_offset != sect_start) {
				/* fill the cache with the current sector */
				mtdblk->cache_state = STATE_EMPTY;
				ret = mtd_read(mtd, sect_start, sect_size,
					       &retlen, mtdblk->cache_data);
				if (ret)
					return ret;
				if (retlen != sect_size)
					return -EIO;

				mtdblk->cache_offset = sect_start;
				mtdblk->cache_size = sect_size;
				mtdblk->cache_state = STATE_CLEAN;
			}

			/* write data to our local cache */
			memcpy (mtdblk->cache_data + offset, buf, size);
			mtdblk->cache_state = STATE_DIRTY;
		}

		buf += size;
		pos += size;
		len -= size;
	}

	return 0;
}

#if defined(CONFIG_MSTAR_NAND) && (MP_NAND_MTD == 1)
extern int nand_CheckEmptyPageFalseAlarm(unsigned char *main, unsigned char *spare);
static int mtdblock_erase(struct mtd_info *mtd, unsigned long pos, int len)
{
	struct erase_info erase;
	DECLARE_WAITQUEUE(wait, current);
	wait_queue_head_t wait_q;
	int ret;

	/*
	 * First, let's erase the flash block.
	 */

	init_waitqueue_head(&wait_q);
	erase.mtd = mtd;
	erase.callback = erase_callback;
	erase.addr = pos;
	erase.len = len;
	erase.priv = (u_long)&wait_q;

	set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(&wait_q, &wait);

	ret = mtd_erase(mtd, &erase);
	if (ret) {
		set_current_state(TASK_RUNNING);
		remove_wait_queue(&wait_q, &wait);
		printk (KERN_WARNING "mtdblock: erase of region [0x%lx, 0x%x] "
				     "on \"%s\" failed\n",
			pos, len, mtd->name);
		return ret;
	}

	schedule();  /* Wait for erase to finish. */
	remove_wait_queue(&wait_q, &wait);

	return 0;
}

static int nand_ReadDisturbance_BigImg(struct mtd_info *mtd, uint32_t u32_from)
{
	struct mtd_oob_ops ops;

	u_int32_t u32_Err, u32_Row ,u32_phys_erase_shift , u32_page_shift;
	u_int16_t u16_BlkPageCnt ,u16_Mtd_Block_cnt, u16_BakupBlock_cnt, u16_i, u16_GoodBlock_Idx,u16_BlkIdx;
	uint8_t *pu8_PageDataBuf = (uint8_t*)kmalloc(mtd->writesize * sizeof(uint8_t), GFP_KERNEL);
	uint8_t *pu8_PageSpareBuf = (uint8_t*)kmalloc(mtd->oobsize * sizeof(uint8_t), GFP_KERNEL);
	int ret;

	u32_phys_erase_shift = ffs(mtd->erasesize) - 1;
	u32_page_shift       = ffs(mtd->writesize) - 1;
	u16_Mtd_Block_cnt    = mtd->size >> u32_phys_erase_shift;
	u16_BakupBlock_cnt   = ((u16_Mtd_Block_cnt*3)/100)+2;
	u16_BlkPageCnt       = mtd->erasesize/mtd->writesize;

	ops.mode = MTD_OPS_PLACE_OOB;
	ops.ooboffs = 0;
	ops.ooblen = mtd->oobsize;
	ops.oobbuf = pu8_PageSpareBuf;
	ops.datbuf = pu8_PageDataBuf;
	ops.len = mtd->writesize;

	memset(pu8_PageSpareBuf, 0xFF, mtd->oobsize);

	for(u16_GoodBlock_Idx = u16_Mtd_Block_cnt-1 ; u16_GoodBlock_Idx > (u16_Mtd_Block_cnt - u16_BakupBlock_cnt); u16_GoodBlock_Idx--)
	{
		u32_Err = mtd_block_isbad(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift);
		if(u32_Err != 0)//bad block
			continue;

		ret = mtd_read_oob(mtd, u16_GoodBlock_Idx<<u32_phys_erase_shift, &ops);

		if(nand_CheckEmptyPageFalseAlarm(pu8_PageDataBuf, pu8_PageSpareBuf) != 1)
		{
			kfree(pu8_PageDataBuf);
			kfree(pu8_PageSpareBuf);
			return 0;
		}

		ret = mtdblock_erase(mtd, u16_GoodBlock_Idx<<u32_phys_erase_shift ,mtd->erasesize);
		if(ret == 0)
		{
			break;
		}
		else
			mtd_block_markbad(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift);
	}

	if(u16_GoodBlock_Idx <= (u16_Mtd_Block_cnt - u16_BakupBlock_cnt))
	{
		printk("No Good Block to do ReadDisturbance\n");
		kfree(pu8_PageDataBuf);
		kfree(pu8_PageSpareBuf);
		return 0;
	}

	ops.mode = MTD_OPS_AUTO_OOB;
	ops.ooboffs = 0;
	ops.ooblen = mtd->oobavail;
	ops.oobbuf = pu8_PageSpareBuf;
	ops.datbuf = pu8_PageDataBuf;
	ops.len = mtd->writesize;

	memset(pu8_PageSpareBuf, 0xFF, mtd->oobsize);
	u16_BlkIdx = u32_from / mtd->erasesize;

	//Write Src Block to Free Good Block
	for(u16_i = 0; u16_i < u16_BlkPageCnt; u16_i ++)
	{
		u32_Row = (u16_BlkIdx*u16_BlkPageCnt + u16_i)<<u32_page_shift;
		ret = mtd_read_oob(mtd, u32_Row, &ops);

		if (ret!= 0 && ret != -EUCLEAN)
		{
			kfree(pu8_PageDataBuf);
			kfree(pu8_PageSpareBuf);
			return 0;
		}
		ops.oobbuf[0] = 0x36;
		ops.oobbuf[1] = 0x97;
		ops.oobbuf[2] = u16_BlkIdx & 0xFF;
		ops.oobbuf[3] = u16_BlkIdx >>8;

		u32_Row = (u16_GoodBlock_Idx*u16_BlkPageCnt + u16_i)<<u32_page_shift;
		ret = mtd_write_oob(mtd, u32_Row, &ops);
		if (ret!= 0)
		{
			kfree(pu8_PageDataBuf);
			kfree(pu8_PageSpareBuf);
			return 0;
		}
	}

	ret = mtdblock_erase(mtd,u16_BlkIdx<<u32_phys_erase_shift,mtd->erasesize);
	if(ret != 0)
	{
		//complicated need to do
		//mark bad and do shifting block
		kfree(pu8_PageDataBuf);
		kfree(pu8_PageSpareBuf);
		return ret;
	}
	//Write  back to Src Block
	for(u16_i = 0; u16_i < u16_BlkPageCnt; u16_i ++)
	{
		u32_Row = (u16_GoodBlock_Idx*u16_BlkPageCnt + u16_i)<<u32_page_shift;
		ret = mtd_read_oob(mtd, u32_Row, &ops);
		if (ret!= 0 && ret!= -EUCLEAN)
		{
			kfree(pu8_PageDataBuf);
			kfree(pu8_PageSpareBuf);
			return ret;
		}

		u32_Row = (u16_BlkIdx*u16_BlkPageCnt + u16_i)<<u32_page_shift;
		ret = mtd_write_oob(mtd, u32_Row, &ops);
		if (ret!= 0)
		{
			kfree(pu8_PageDataBuf);
			kfree(pu8_PageSpareBuf);
			return ret;
		}
	}

	ret = mtdblock_erase(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift,mtd->erasesize);
	if(ret != 0)
	{
	    mtd_block_markbad(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift);
	}

	kfree(pu8_PageDataBuf);
	kfree(pu8_PageSpareBuf);
	return 0;
}

static int nand_ReadDisturbance_BigImgRestore(struct mtd_info *mtd)
{
	struct mtd_oob_ops ops;
	u_int32_t u32_Err, u32_Row ,u32_phys_erase_shift , u32_page_shift;
	u_int16_t u16_BlkPageCnt, u16_Mtd_Block_cnt, u16_BakupBlock_cnt, u16_i, u16_GoodBlock_Idx, u16_BlkIdx;
	uint8_t *pu8_PageDataBuf  = (uint8_t*)kmalloc(mtd->writesize * sizeof(uint8_t), GFP_KERNEL);
	uint8_t *pu8_PageSpareBuf = (uint8_t*)kmalloc(mtd->oobsize * sizeof(uint8_t), GFP_KERNEL);
	int ret;

	u32_phys_erase_shift = ffs(mtd->erasesize) - 1;
	u32_page_shift       = ffs(mtd->writesize) - 1;
	u16_Mtd_Block_cnt    = mtd->size >> u32_phys_erase_shift;
	u16_BakupBlock_cnt   = ((u16_Mtd_Block_cnt*3)/100)+2;
	u16_BlkPageCnt       = mtd->erasesize / mtd->writesize;

	//search last good block of read only partition
	for(u16_GoodBlock_Idx = u16_Mtd_Block_cnt-1 ; u16_GoodBlock_Idx > (u16_Mtd_Block_cnt - u16_BakupBlock_cnt); u16_GoodBlock_Idx--)
	{
		u32_Err = mtd_block_isbad(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift);
		if(u32_Err == 0)//good block
			break;
	}

	if(u16_GoodBlock_Idx <= (u16_Mtd_Block_cnt - u16_BakupBlock_cnt))
	{
		printk("No Good Block to do ReadDisturbance\n");
		kfree(pu8_PageDataBuf);
		kfree(pu8_PageSpareBuf);
		return 0;
	}

	ops.mode = MTD_OPS_AUTO_OOB;
	ops.ooboffs = 0;
	ops.ooblen = mtd->oobsize;
	ops.oobbuf = pu8_PageSpareBuf;
	ops.datbuf = pu8_PageDataBuf;
	ops.len = mtd->writesize;
	memset(pu8_PageSpareBuf, 0xFF, mtd->oobsize);

	u32_Row = u16_GoodBlock_Idx << u32_phys_erase_shift;
	ret = mtd_read_oob(mtd, u32_Row, &ops);

	//check block content
	// read first and last lsb page for checking ecc and empty
	if(ret != 0 && ret != -EUCLEAN)
	{
		printk("Read last good readonly block 0x%x first page fail\n", u16_GoodBlock_Idx);
		//erase block and skip process
		ret =  mtdblock_erase(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift,mtd->erasesize);
		if(ret != 0)
		{
		    printk("Erase last good readonly block 0x%x fail\n", u16_GoodBlock_Idx);
			mtd_block_markbad(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift);
		}
		kfree(pu8_PageDataBuf);
		kfree(pu8_PageSpareBuf);
		return 0;
	}

	if(!((pu8_PageSpareBuf[0] == 0x36) && (pu8_PageSpareBuf[1] == 0x97)))
	{
		kfree(pu8_PageDataBuf);
		kfree(pu8_PageSpareBuf);
		return 0;
	}


	//Read last page of last good read only block
	u32_Row = ((u16_GoodBlock_Idx+1)*u16_BlkPageCnt -1)<<u32_page_shift;
	ret = mtd_read_oob(mtd, u32_Row, &ops);

	if((ret != 0) && ret != -EUCLEAN)
	{
		printk("last page is empty or dummy data or Read fail\n");
		ret =  mtdblock_erase(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift,mtd->erasesize);
		if(ret != 0)
		{
			printk("Erase last good readonly block 0x%x fail\n", u16_GoodBlock_Idx);
			mtd_block_markbad(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift);
		}
		kfree(pu8_PageDataBuf);
		kfree(pu8_PageSpareBuf);
		return 0;
	}

	if(!((pu8_PageSpareBuf[0] == 0x36) && (pu8_PageSpareBuf[1] == 0x97)))
	{
		ret =  mtdblock_erase(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift,mtd->erasesize);
		if(ret != 0)
		{
			printk("Erase last good readonly block 0x%x fail\n", u16_GoodBlock_Idx);
			mtd_block_markbad(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift);
		}
		kfree(pu8_PageDataBuf);
		kfree(pu8_PageSpareBuf);
		return 0;
	}

	//source block idx read from last good block last page
	u16_BlkIdx = (pu8_PageSpareBuf[3]<<8) + pu8_PageSpareBuf[2];

	//erase source block
	ret = mtdblock_erase(mtd,u16_BlkIdx<<u32_phys_erase_shift,mtd->erasesize);
	if(ret != 0)
	{
		//mark bad and do shifting...
		printk("Erase source readonly block 0x%x fail\n", u16_BlkIdx);
		kfree(pu8_PageDataBuf);
		kfree(pu8_PageSpareBuf);
		return ret;
	}

	//restore data to source block
	for(u16_i = 0; u16_i < u16_BlkPageCnt ; u16_i ++)
	{
		u32_Row = (u16_GoodBlock_Idx*u16_BlkPageCnt + u16_i)<<u32_page_shift;
		ret = mtd_read_oob(mtd, u32_Row, &ops);
		if(ret != 0)
		{
			//fatal error
			printk("Read last good block fail, nand blobk 0x%x page 0x%x\n", u16_BlkIdx, u16_i);
			kfree(pu8_PageDataBuf);
			kfree(pu8_PageSpareBuf);
			return ret;
		}
		else
		{
			if((pu8_PageSpareBuf[0] == 0x36)&&(pu8_PageSpareBuf[1] == 0x97))
			{
				u32_Row = (u16_BlkIdx*u16_BlkPageCnt + u16_i)<<u32_page_shift;
				ret = mtd_write_oob(mtd, u32_Row, &ops);

				if(ret != 0)
				{
					printk("restoring data to readonly block 0x%x page 0x%x fail\n", u16_BlkIdx, u16_i);
					mtd_block_markbad(mtd,u16_BlkIdx<<u32_phys_erase_shift);
					kfree(pu8_PageDataBuf);
					kfree(pu8_PageSpareBuf);
					return ret;
				}
			}
			else
			{
				printk("Dummy data find at last good readonly block 0x%x page 0x%x\n", u16_BlkIdx, u16_i);
				kfree(pu8_PageDataBuf);
				kfree(pu8_PageSpareBuf);
				return 1;
			}
		}
	}

	//Erase Backup block
	ret = mtdblock_erase(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift,mtd->erasesize);
	if(ret != 0)
	{
		printk("Erase last good readonly block 0x%x fail\n", u16_GoodBlock_Idx);
		mtd_block_markbad(mtd,u16_GoodBlock_Idx<<u32_phys_erase_shift);
	}
	kfree(pu8_PageDataBuf);
	kfree(pu8_PageSpareBuf);
	return 0;
}
#endif

static int do_cached_read (struct mtdblk_dev *mtdblk, unsigned long pos,
			   int len, char *buf)
{
	struct mtd_info *mtd = mtdblk->mbd.mtd;
	unsigned int sect_size = mtdblk->cache_size;
	size_t retlen;
	int ret;
#if defined(CONFIG_MSTAR_NAND) && (MP_NAND_MTD == 1)
	struct nand_chip *chip = mtd->priv;
#endif

	pr_debug("mtdblock: read on \"%s\" at 0x%lx, size 0x%x\n",
			mtd->name, pos, len);

	if (!sect_size)
		return mtd_read(mtd, pos, len, &retlen, buf);

	while (len > 0) {
		unsigned long sect_start = (pos/sect_size)*sect_size;
		unsigned int offset = pos - sect_start;
		unsigned int size = sect_size - offset;
		if (size > len)
			size = len;

		/*
		 * Check if the requested data is already cached
		 * Read the requested amount of data from our internal cache if it
		 * contains what we want, otherwise we read the data directly
		 * from flash.
		 */
		if (mtdblk->cache_state != STATE_EMPTY &&
		    mtdblk->cache_offset == sect_start) {
			memcpy (buf, mtdblk->cache_data + offset, size);
		} else {
			ret = mtd_read(mtd, pos, size, &retlen, buf);
			#if defined(CONFIG_MSTAR_NAND) && (MP_NAND_MTD == 1)
			if(mtd->type == MTD_NANDFLASH)
			{
				if (ret == -EUCLEAN)//ecc correct
			        {
					if(!(chip->options & NAND_IS_SPI))
					{
						ret = nand_ReadDisturbance_BigImg(mtd, pos);
						if(ret != 0)
							return ret;
					}
				}
			}
			else {
			if (ret)
				return ret;
			}
			#else
			if (ret)
				return ret;
			#endif
			if (retlen != size)
				return -EIO;
		}

		buf += size;
		pos += size;
		len -= size;
	}

	return 0;
}

static int mtdblock_readsect(struct mtd_blktrans_dev *dev,
			      unsigned long block, char *buf)
{
	struct mtdblk_dev *mtdblk = container_of(dev, struct mtdblk_dev, mbd);

	#if (defined(CONFIG_MSTAR_NAND) || defined(CONFIG_MSTAR_SPI_NAND)) && (MP_NAND_MTD == 1)
	struct mtd_info *mtd = mtdblk->mbd.mtd;
	u_int16_t u16_blkidx, u16_bad_blk_cnt , u16_blk_cnt;
	u_int16_t u16_blk_sec_cnt_shift, u16_blk_sec_cnt_mask;
	u_int16_t u16_check_blk_idx,u16_check_blk_cnt;
	u_int32_t u32_real_block,u32_phys_erase_shift;

	u32_phys_erase_shift = ffs(mtd->erasesize) -1;
	u16_blk_cnt = mtd->size >> u32_phys_erase_shift;
	u16_blk_sec_cnt_shift = ffs(mtd->erasesize >>9) - 1;
	u16_blk_sec_cnt_mask = (mtd->erasesize >>9) - 1;
	u16_blkidx = block >>u16_blk_sec_cnt_shift;
	u16_bad_blk_cnt = 0;
	u16_check_blk_cnt = 0;
	u16_check_blk_idx =0;
	if(mtd->type == MTD_NANDFLASH)
	{
		u32_phys_erase_shift = ffs(mtd->erasesize) - 1;
		while(u16_check_blk_cnt < (u16_blkidx+1))
		{
			 if (!(mtdblk->au8_BadBlkTbl[u16_check_blk_idx>> 3] & (1 << (u16_check_blk_idx & 0x7))))
			 {
		         u16_bad_blk_cnt++;
			 }
		     else
		     {
			     u16_check_blk_cnt++;
		     }
			 u16_check_blk_idx++;
		};

		if((u16_blkidx+u16_bad_blk_cnt) > u16_blk_cnt)
		{
		    printk("MTD Err: Too Many Bad Block!\n");
			return 1;
		}

		u32_real_block = ((u16_blkidx+u16_bad_blk_cnt) << u16_blk_sec_cnt_shift)+(block & u16_blk_sec_cnt_mask);

		return do_cached_read(mtdblk, u32_real_block<<9, 512, buf);
	}
	else
	{
		return do_cached_read(mtdblk, block<<9, 512, buf);
	}
	#else
	return do_cached_read(mtdblk, block<<9, 512, buf);
	#endif
}

static int mtdblock_writesect(struct mtd_blktrans_dev *dev,
			      unsigned long block, char *buf)
{
	struct mtdblk_dev *mtdblk = container_of(dev, struct mtdblk_dev, mbd);
	if (unlikely(!mtdblk->cache_data && mtdblk->cache_size)) {
		mtdblk->cache_data = vmalloc(mtdblk->mbd.mtd->erasesize);
		if (!mtdblk->cache_data)
			return -EINTR;
		/* -EINTR is not really correct, but it is the best match
		 * documented in man 2 write for all cases.  We could also
		 * return -EAGAIN sometimes, but why bother?
		 */
	}
	return do_cached_write(mtdblk, block<<9, 512, buf);
}

static int mtdblock_open(struct mtd_blktrans_dev *mbd)
{
	struct mtdblk_dev *mtdblk = container_of(mbd, struct mtdblk_dev, mbd);
	struct mtd_info *mtd = mtdblk->mbd.mtd;
	#if (defined(CONFIG_MSTAR_NAND) || defined(CONFIG_MSTAR_SPI_NAND)) && (MP_NAND_MTD == 1)
	u_int16_t u16_blkidx;
	int ret;
	u_int32_t u32_phys_erase_shift;
	struct nand_chip *chip = mtd->priv;
	#endif

	pr_debug("mtdblock_open\n");

	if (mtdblk->count) {
		mtdblk->count++;
		return 0;
	}

	/* OK, it's not open. Create cache info for it */
	mtdblk->count = 1;
	mutex_init(&mtdblk->cache_mutex);
	mtdblk->cache_state = STATE_EMPTY;
	if (!(mbd->mtd->flags & MTD_NO_ERASE) && mbd->mtd->erasesize) {
		mtdblk->cache_size = mbd->mtd->erasesize;
		mtdblk->cache_data = NULL;
	}

	#if (defined(CONFIG_MSTAR_NAND) || defined(CONFIG_MSTAR_SPI_NAND)) && (MP_NAND_MTD == 1)
	if(mtd->type == MTD_NANDFLASH)
	{
		u32_phys_erase_shift = ffs(mtd->erasesize) - 1;
		memset(mtdblk->au8_BadBlkTbl,0xff,512);
		for(u16_blkidx=0;u16_blkidx< (mtd->size >> u32_phys_erase_shift);u16_blkidx++)
		{
		    ret = mtd_block_isbad(mtd,u16_blkidx<<u32_phys_erase_shift);
			if(ret != 0)//bad block
			{
	           mtdblk->au8_BadBlkTbl[u16_blkidx>> 3] &= ~(1 << (u16_blkidx & 0x7));
			}
		}
	}
	#if defined(CONFIG_MSTAR_NAND)
	if(mtd->type == MTD_NANDFLASH && !(chip->options & NAND_IS_SPI))
	{
		ret = nand_ReadDisturbance_BigImgRestore(mtd);
		if(ret != 0)
		    printk("nand_ReadDisturbance_BigImgRestore Fail\n");
	}
	#endif
	#endif
	pr_debug("ok\n");

	return 0;
}

static void mtdblock_release(struct mtd_blktrans_dev *mbd)
{
	struct mtdblk_dev *mtdblk = container_of(mbd, struct mtdblk_dev, mbd);

	pr_debug("mtdblock_release\n");

	mutex_lock(&mtdblk->cache_mutex);
	write_cached_data(mtdblk);
	mutex_unlock(&mtdblk->cache_mutex);

	if (!--mtdblk->count) {
		/*
		 * It was the last usage. Free the cache, but only sync if
		 * opened for writing.
		 */
		if (mbd->file_mode & FMODE_WRITE)
			mtd_sync(mbd->mtd);
		vfree(mtdblk->cache_data);
	}

	pr_debug("ok\n");
}

static int mtdblock_flush(struct mtd_blktrans_dev *dev)
{
	struct mtdblk_dev *mtdblk = container_of(dev, struct mtdblk_dev, mbd);

	mutex_lock(&mtdblk->cache_mutex);
	write_cached_data(mtdblk);
	mutex_unlock(&mtdblk->cache_mutex);
	mtd_sync(dev->mtd);
	return 0;
}

static void mtdblock_add_mtd(struct mtd_blktrans_ops *tr, struct mtd_info *mtd)
{
	struct mtdblk_dev *dev = kzalloc(sizeof(*dev), GFP_KERNEL);

	if (!dev)
		return;

	dev->mbd.mtd = mtd;
	dev->mbd.devnum = mtd->index;

	dev->mbd.size = mtd->size >> 9;
	dev->mbd.tr = tr;

	if (!(mtd->flags & MTD_WRITEABLE))
		dev->mbd.readonly = 1;

	if (add_mtd_blktrans_dev(&dev->mbd))
		kfree(dev);
}

static void mtdblock_remove_dev(struct mtd_blktrans_dev *dev)
{
	del_mtd_blktrans_dev(dev);
}

static struct mtd_blktrans_ops mtdblock_tr = {
	.name		= "mtdblock",
	.major		= MTD_BLOCK_MAJOR,
	.part_bits	= 0,
	.blksize 	= 512,
	.open		= mtdblock_open,
	.flush		= mtdblock_flush,
	.release	= mtdblock_release,
	.readsect	= mtdblock_readsect,
	.writesect	= mtdblock_writesect,
	.add_mtd	= mtdblock_add_mtd,
	.remove_dev	= mtdblock_remove_dev,
	.owner		= THIS_MODULE,
};

static int __init init_mtdblock(void)
{
	return register_mtd_blktrans(&mtdblock_tr);
}

static void __exit cleanup_mtdblock(void)
{
	deregister_mtd_blktrans(&mtdblock_tr);
}

module_init(init_mtdblock);
module_exit(cleanup_mtdblock);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Nicolas Pitre <nico@fluxnic.net> et al.");
MODULE_DESCRIPTION("Caching read/erase/writeback block device emulation access to MTD devices");
