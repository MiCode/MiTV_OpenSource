// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2011 Google, Inc.
 */

#include <linux/kernel.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/uaccess.h>

#include "ion.h"

#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
#include <asm/cacheflush.h>
#include <asm/outercache.h>
#endif

union ion_ioctl_arg {
#if defined(CONFIG_MP_ION_PATCH_CACHE_FLUSH_MOD)
	struct ion_cache_flush_data cache_flush;
#endif
	struct ion_allocation_data allocation;
#if (MP_ION_PATCH_MSTAR == 1)
	struct ion_user_data bus_addr_info;
#endif
	struct ion_heap_query query;
};

#if (MP_ION_PATCH_CACHE_FLUSH_MOD==1)
static int ion_cache_flush(unsigned long start, unsigned long end)
{
	struct mm_struct *mm = current->active_mm;
	struct vm_area_struct *vma;
#ifdef CONFIG_CPU_SW_DOMAIN_PAN
	unsigned int __ua_flags;
#endif

	if (end < start)
		return -EINVAL;

	down_read(&mm->mmap_sem);
	vma = find_vma(mm, start);
	if (vma && vma->vm_start < end) {
		if (start < vma->vm_start)
			start = vma->vm_start;
		if (end > vma->vm_end)
			end = vma->vm_end;

#ifdef CONFIG_ARM64_SW_TTBR0_PAN
		uaccess_enable_not_uao();
#elif CONFIG_CPU_SW_DOMAIN_PAN
		__ua_flags = uaccess_save_and_enable();
#endif

		dmac_flush_range((void*)(start& PAGE_MASK), (void*)PAGE_ALIGN(end));
		outer_flush_range(start, end);

#ifdef CONFIG_ARM64_SW_TTBR0_PAN
		uaccess_disable_not_uao();
#elif CONFIG_CPU_SW_DOMAIN_PAN
		uaccess_restore(__ua_flags);
#endif

#ifndef CONFIG_OUTER_CACHE
		{
			extern void Chip_Flush_Miu_Pipe(void);
			Chip_Flush_Miu_Pipe();
		}
#endif
		up_read(&mm->mmap_sem);
		return 0;
	}

	up_read(&mm->mmap_sem);
	return -EINVAL;
}
#endif

static int validate_ioctl_arg(unsigned int cmd, union ion_ioctl_arg *arg)
{
	switch (cmd) {
	case ION_IOC_HEAP_QUERY:
		if (arg->query.reserved0 ||
		    arg->query.reserved1 ||
		    arg->query.reserved2)
			return -EINVAL;
		break;
	default:
		break;
	}

	return 0;
}

/* fix up the cases where the ioctl direction bits are incorrect */
static unsigned int ion_ioctl_dir(unsigned int cmd)
{
	switch (cmd) {
	default:
		return _IOC_DIR(cmd);
	}
}

long ion_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
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
		int fd;

#if (MP_ION_PATCH_MSTAR == 1)
		if(data.allocation.heap_id_mask == 0xFFFFFFFF)  // for codec2.0, the libion will use all heaps
			data.allocation.heap_id_mask = 1 << 0;  // system_heap id is 0
#endif

		fd = ion_alloc(data.allocation.len,
			       data.allocation.heap_id_mask,
			       data.allocation.flags);
		if (fd < 0)
			return fd;

		data.allocation.fd = fd;

		break;
	}
#if (MP_ION_PATCH_MSTAR == 1)
	case ION_IOC_GET_CMA_BUFFER_INFO:
	{
		/* use ION_IOC_ALLOC to get a share_fd,
		 * and use the share_fd to get bus_add and buffer_size
		 * this only works if ion_alloc to a mstar cma heap
		 */
		unsigned long bus_addr;

		bus_addr = ion_get_cma_buffer_info(data.bus_addr_info.fd);
		if(!bus_addr)
		{
			printk(KERN_EMERG "\033[35mFunction = %s, Line = %d, ion-ioctl: [Error] cannot get bus address info\033[m\n", __PRETTY_FUNCTION__, __LINE__);
			data.bus_addr_info.bus_addr = 0;
			return -EBADF;
		}

		data.bus_addr_info.bus_addr = bus_addr;
		break;
	}
#endif
#ifdef CONFIG_MP_ION_PATCH_CACHE_FLUSH_MOD
	case ION_IOC_CACHE_FLUSH:
	{
		ion_cache_flush((unsigned long)data.cache_flush.start, (unsigned long)(data.cache_flush.start+data.cache_flush.len));
		break;
	}
#endif
	case ION_IOC_HEAP_QUERY:
		ret = ion_query_heaps(&data.query);
		break;
	default:
		pr_warn("%s, cmd %x not support\n", __func__, cmd);
		return -ENOTTY;
	}

	if (dir & _IOC_READ) {
		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd)))
			return -EFAULT;
	}
	return ret;
}
