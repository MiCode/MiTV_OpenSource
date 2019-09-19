/*
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

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "ion.h"
#include "ion_priv.h"
#include "compat_ion.h"

#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
#include <asm/cacheflush.h>
#include <asm/outercache.h>
#endif

union ion_ioctl_arg {
#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
	struct ion_cache_flush_data cache_flush;
#endif
	struct ion_fd_data fd;
	struct ion_allocation_data allocation;
	struct ion_handle_data handle;
	struct ion_custom_data custom;
	struct ion_user_data bus_addr_info; // for keeping bus_addr to msos layer, msos layer will change to miu/offset
	struct ion_cust_allocation_data cust_allocation;
	struct ion_heap_query query;
};

#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
static int ion_cache_flush(unsigned long start, unsigned long end)
{
	struct mm_struct *mm = current->active_mm;
	struct vm_area_struct *vma;
	int ret=0;

	if (end < start)
		return -EINVAL;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, start);
	if (vma && vma->vm_start < end) {
		if (start < vma->vm_start)
			start = vma->vm_start;
		if (end > vma->vm_end)
			end = vma->vm_end;

		dmac_flush_range((void*)(start& PAGE_MASK), (void*)PAGE_ALIGN(end));
		outer_flush_range(start, end);
#ifndef CONFIG_OUTER_CACHE
		{
			extern void Chip_Flush_Miu_Pipe(void);
			Chip_Flush_Miu_Pipe();
		}
#endif
		up_read(&mm->mmap_sem);
		return ret;
	}

	up_read(&mm->mmap_sem);
	return -EINVAL;
}
#endif

static int validate_ioctl_arg(unsigned int cmd, union ion_ioctl_arg *arg)
{
	int ret = 0;

	switch (cmd) {
	case ION_IOC_HEAP_QUERY:
		ret = arg->query.reserved0 != 0;
		ret |= arg->query.reserved1 != 0;
		ret |= arg->query.reserved2 != 0;
		break;
	default:
		break;
	}

	return ret ? -EINVAL : 0;
}

/* fix up the cases where the ioctl direction bits are incorrect */
static unsigned int ion_ioctl_dir(unsigned int cmd)
{
	switch (cmd) {
	case ION_IOC_SYNC:
	case ION_IOC_FREE:
	case ION_IOC_CUSTOM:
		return _IOC_WRITE;
	default:
		return _IOC_DIR(cmd);
	}
}

long ion_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ion_client *client = filp->private_data;
	struct ion_device *dev = client->dev;
	struct ion_handle *cleanup_handle = NULL;
	int ret = 0;
	unsigned int dir;
	union ion_ioctl_arg data;

	dir = ion_ioctl_dir(cmd);

	if (_IOC_SIZE(cmd) > sizeof(data))
		return -EINVAL;

	/*
	 * The copy_from_user is unconditional here for both read and write
	 * to do the validate. If there is no write for the ioctl, the
	 * buffer is cleared
	 */
	if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
		return -EFAULT;

	ret = validate_ioctl_arg(cmd, &data);
	if (ret) {
		pr_warn_once("%s: ioctl validate failed\n", __func__);
		return ret;
	}

	if (!(dir & _IOC_WRITE))
		memset(&data, 0, sizeof(data));

	switch (cmd) {
	case ION_IOC_ALLOC:
	{
		struct ion_handle *handle;

		handle = ion_alloc(client, data.allocation.len,
						data.allocation.align,
						data.allocation.heap_id_mask,
						data.allocation.flags);
		if (IS_ERR(handle))
			return PTR_ERR(handle);

		data.allocation.handle = handle->id;

		cleanup_handle = handle;
		break;
	}
	case ION_IOC_FREE:
	{
		struct ion_handle *handle;

		mutex_lock(&client->lock);
		handle = ion_handle_get_by_id_nolock(client, data.handle.handle);
		if (IS_ERR(handle)) {
			mutex_unlock(&client->lock);
			return PTR_ERR(handle);
		}
		ion_free_nolock(client, handle);
		ion_handle_put_nolock(handle);
		mutex_unlock(&client->lock);
		break;
	}
	case ION_IOC_SHARE:
	case ION_IOC_MAP:
	{
		struct ion_handle *handle;

		handle = ion_handle_get_by_id(client, data.handle.handle);
		if (IS_ERR(handle))
			return PTR_ERR(handle);
		data.fd.fd = ion_share_dma_buf_fd(client, handle);
		ion_handle_put(handle);
		if (data.fd.fd < 0)
			ret = data.fd.fd;
		break;
	}
	case ION_IOC_IMPORT:
	{
		struct ion_handle *handle;

		handle = ion_import_dma_buf_fd(client, data.fd.fd);
		if (IS_ERR(handle))
			ret = PTR_ERR(handle);
		else
			data.handle.handle = handle->id;
		break;
	}
	case ION_IOC_SYNC:
	{
		ret = ion_sync_for_device(client, data.fd.fd);
		break;
	}
	case ION_IOC_CUSTOM:
	{
		if (!dev->custom_ioctl)
			return -ENOTTY;
		ret = dev->custom_ioctl(client, data.custom.cmd,
						data.custom.arg);
		break;
	}
	case ION_IOC_GET_CMA_BUFFER_INFO:
	{
		/* add this to keep bus_addr info to msos layer */
		struct ion_handle *handle;
		struct ion_heap *heap;

		ion_phys_addr_t bus_addr;
		size_t buffer_len;

		handle = ion_handle_get_by_id(client, data.bus_addr_info.handle);
		if (IS_ERR(handle))
			return PTR_ERR(handle);

		heap = handle->buffer->heap;
		heap->ops->phys(heap, handle->buffer, &bus_addr, &buffer_len);
		data.bus_addr_info.bus_addr = (unsigned long)bus_addr;
		ion_handle_put(handle);
		break;
	}
	case ION_IOC_CUST_ALLOC:
	{
		struct ion_handle *handle;

		handle = ion_cust_alloc(client, data.cust_allocation.start,
					data.cust_allocation.len,
					data.cust_allocation.align,
					data.cust_allocation.heap_id_mask,
					data.cust_allocation.flags,
					&data.cust_allocation.miu,
					&data.cust_allocation.miu_offset);

		if (IS_ERR(handle))
			return PTR_ERR(handle);

		data.cust_allocation.handle = handle->id;

		cleanup_handle = handle;
		break;
	}
#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
	case ION_IOC_CACHE_FLUSH:
	{
		ion_cache_flush(data.cache_flush.start, data.cache_flush.start+data.cache_flush.len);
		break;
	}
#endif
	case ION_IOC_HEAP_QUERY:
		ret = ion_query_heaps(client, &data.query);
		break;
	default:
		return -ENOTTY;
	}

	if (dir & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd))) {
			if (cleanup_handle)
				ion_free(client, cleanup_handle);
			return -EFAULT;
		}
	}
	return ret;
}
