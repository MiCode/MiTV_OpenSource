/*
 * smaf.c
 *
 * Copyright (C) Linaro SA 2015
 * Author: Benjamin Gaignard <benjamin.gaignard at linaro.org> for Linaro.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/device.h>
#include <linux/dma-buf.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/ioctl.h>
#include <linux/list_sort.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#if defined(CONFIG_COMPAT)
#include <linux/compat.h>
#endif
#include <asm/cacheflush.h>
#include <asm/outercache.h>

#include "smaf.h"
#include "smaf-allocator.h"
#include "smaf-secure.h"

/**
 * struct smaf_device - smaf device node private data
 * @misc_dev:	the misc device
 * @head:	list of allocator
 * @lock:	list and secure pointer mutex
 * @secure:	pointer to secure functions helpers
 */
struct smaf_device {
	struct miscdevice misc_dev;
	struct list_head head;
	/* list and secure pointer lock*/
	struct mutex lock;
	struct smaf_secure *secure;
};

static struct smaf_device smaf_dev;

/**
 * smaf_allow_cpu_access return true if CPU can access to memory
 * if their is no secure module associated to SMAF assume that CPU can get
 * access to the memory.
 */
static bool smaf_allow_cpu_access(struct smaf_handle *handle,
				  unsigned long flags)
{
	if (!handle->is_secure)
		return true;

	if (!smaf_dev.secure)
		return true;

	if (!smaf_dev.secure->allow_cpu_access)
		return true;

	return smaf_dev.secure->allow_cpu_access(handle->secure_ctx, flags);
}

static int smaf_grant_access(struct smaf_handle *handle, struct device *dev,
			     dma_addr_t addr, size_t size,
			     enum dma_data_direction dir)
{
	if (!handle->is_secure)
		return 0;

	if (!smaf_dev.secure)
		return -EINVAL;

	if (!smaf_dev.secure->grant_access)
		return -EINVAL;

	return smaf_dev.secure->grant_access(handle->secure_ctx,
					     dev, addr, size, dir);
}

static void smaf_revoke_access(struct smaf_handle *handle, struct device *dev,
			       dma_addr_t addr, size_t size,
			       enum dma_data_direction dir)
{
	if (!handle->is_secure)
		return;

	if (!smaf_dev.secure)
		return;

	if (!smaf_dev.secure->revoke_access)
		return;

	smaf_dev.secure->revoke_access(handle->secure_ctx,
				       dev, addr, size, dir);
}

static int smaf_secure_handle(struct smaf_handle *handle)
{
	if (handle->is_secure)
		return 0;

	if (!smaf_dev.secure)
		return -EINVAL;

	if (!smaf_dev.secure->create_context)
		return -EINVAL;

	handle->secure_ctx = smaf_dev.secure->create_context();

	if (!handle->secure_ctx)
		return -EINVAL;

	handle->is_secure = true;
	return 0;
}

static int smaf_unsecure_handle(struct smaf_handle *handle)
{
	if (!handle->is_secure)
		return 0;

	if (!smaf_dev.secure)
		return -EINVAL;

	if (!smaf_dev.secure->destroy_context)
		return -EINVAL;

	if (smaf_dev.secure->destroy_context(handle->secure_ctx))
		return -EINVAL;

	handle->secure_ctx = NULL;
	handle->is_secure = false;
	return 0;
}

int smaf_register_secure(struct smaf_secure *s)
{
	if (smaf_dev.secure || !s)
		return -EINVAL;

	mutex_lock(&smaf_dev.lock);
	smaf_dev.secure = s;
	mutex_unlock(&smaf_dev.lock);

	return 0;
}
EXPORT_SYMBOL(smaf_register_secure);

void smaf_unregister_secure(struct smaf_secure *s)
{
	mutex_lock(&smaf_dev.lock);
	if (smaf_dev.secure == s)
		smaf_dev.secure = NULL;
	mutex_unlock(&smaf_dev.lock);
}
EXPORT_SYMBOL(smaf_unregister_secure);

static struct smaf_allocator *smaf_find_allocator(struct dma_buf *dmabuf)
{
	struct smaf_allocator *alloc;

	list_for_each_entry(alloc, &smaf_dev.head, list_node) {
		if (alloc->match(dmabuf))
			return alloc;
	}

	return NULL;
}

static struct smaf_allocator *smaf_get_first_allocator(struct dma_buf *dmabuf)
{
	/* the first allocator of the list is the preferred allocator */
	return list_first_entry(&smaf_dev.head, struct smaf_allocator,
			list_node);
}

static int smaf_allocator_compare(void *priv,
				  struct list_head *lh_a,
				  struct list_head *lh_b)
{
	struct smaf_allocator *a = list_entry(lh_a,
					      struct smaf_allocator, list_node);
	struct smaf_allocator *b = list_entry(lh_b,
					      struct smaf_allocator, list_node);
	int diff;

	diff = b->ranking - a->ranking;
	if (diff)
		return diff;

	return strcmp(a->name, b->name);
}

int smaf_register_allocator(struct smaf_allocator *alloc)
{
	BUG_ON(!alloc || !alloc->match || !alloc->allocate || !alloc->name);

	mutex_lock(&smaf_dev.lock);
	list_add(&alloc->list_node, &smaf_dev.head);
	list_sort(NULL, &smaf_dev.head, smaf_allocator_compare);
	mutex_unlock(&smaf_dev.lock);

	return 0;
}
EXPORT_SYMBOL(smaf_register_allocator);

void smaf_unregister_allocator(struct smaf_allocator *alloc)
{
	mutex_lock(&smaf_dev.lock);
	list_del(&alloc->list_node);
	mutex_unlock(&smaf_dev.lock);
}
EXPORT_SYMBOL(smaf_unregister_allocator);

static struct dma_buf_attachment *smaf_find_attachment(struct dma_buf *db_alloc,
						       struct device *dev)
{
	struct dma_buf_attachment *attach_obj;

	list_for_each_entry(attach_obj, &db_alloc->attachments, node) {
		if (attach_obj->dev == dev)
			return attach_obj;
	}

	return NULL;
}

static struct sg_table *smaf_map_dma_buf(struct dma_buf_attachment *attachment,
					 enum dma_data_direction direction)
{
	struct dma_buf_attachment *db_attachment;
	struct dma_buf *dmabuf = attachment->dmabuf;
	struct smaf_handle *handle = dmabuf->priv;
	struct sg_table *sgt;
	unsigned count_done, count;
	struct scatterlist *sg;

	if (handle->is_secure && !smaf_dev.secure)
		return NULL;

	/* try to find an allocator */
	if (!handle->allocator) {
		struct smaf_allocator *alloc;

		mutex_lock(&smaf_dev.lock);
		alloc = smaf_find_allocator(dmabuf);
		mutex_unlock(&smaf_dev.lock);

		/* still no allocator ? */
		if (!alloc)
			return NULL;

		handle->allocator = alloc;
	}

	if (!handle->db_alloc) {
		struct dma_buf *db_alloc;

		db_alloc = handle->allocator->allocate(dmabuf,
						       handle->length,
						       handle->flags);
		if (!db_alloc)
			return NULL;

		handle->db_alloc = db_alloc;
	}

	db_attachment = smaf_find_attachment(handle->db_alloc, attachment->dev);
	sgt = dma_buf_map_attachment(db_attachment, direction);

	if (!sgt)
		return NULL;

	if (!handle->is_secure)
		return sgt;

	/* now secure the data */
	for_each_sg(sgt->sgl, sg, sgt->nents, count_done) {
		if (smaf_grant_access(handle, db_attachment->dev,
				      sg_phys(sg), sg->length, direction))
			goto failed;
	}

	return sgt;

failed:
	for_each_sg(sgt->sgl, sg, count_done, count) {
		smaf_revoke_access(handle, db_attachment->dev,
				   sg_phys(sg), sg->length, direction);
	}

	sg_free_table(sgt);
	kfree(sgt);
	return NULL;
}

static void smaf_unmap_dma_buf(struct dma_buf_attachment *attachment,
			       struct sg_table *sgt,
			       enum dma_data_direction direction)
{
	struct dma_buf_attachment *db_attachment;
	struct dma_buf *dmabuf = attachment->dmabuf;
	struct smaf_handle *handle = dmabuf->priv;
	struct scatterlist *sg;
	unsigned count;

	if (!handle->db_alloc)
		return;

	db_attachment = smaf_find_attachment(handle->db_alloc, attachment->dev);
	if (!db_attachment)
		return;

	if (handle->is_secure) {
		for_each_sg(sgt->sgl, sg, sgt->nents, count) {
			smaf_revoke_access(handle, db_attachment->dev,
					   sg_phys(sg), sg->length, direction);
		}
	}

	dma_buf_unmap_attachment(db_attachment, sgt, direction);
}

static int smaf_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct smaf_handle *handle = dmabuf->priv;

	if (!smaf_allow_cpu_access(handle, vma->vm_flags))
		return -EINVAL;

	/* if no allocator attached, get the first allocator */
	if (!handle->allocator) {
		struct smaf_allocator *alloc;

		mutex_lock(&smaf_dev.lock);
		alloc = smaf_get_first_allocator(dmabuf);
		mutex_unlock(&smaf_dev.lock);

		/* still no allocator ? */
		if (!alloc)
			return -EINVAL;

		handle->allocator = alloc;
	}

	if (!handle->db_alloc) {
		struct dma_buf *db_alloc;

		db_alloc = handle->allocator->allocate(dmabuf,
						       handle->length,
						       handle->flags);
		if (!db_alloc)
			return -EINVAL;

		handle->db_alloc = db_alloc;
	}

	return dma_buf_mmap(handle->db_alloc, vma, 0);
}

static void smaf_dma_buf_release(struct dma_buf *dmabuf)
{
	struct smaf_handle *handle = dmabuf->priv;

	if (handle->db_alloc)
		dma_buf_put(handle->db_alloc);

	smaf_unsecure_handle(handle);

	kfree(handle);
}

static int smaf_dma_buf_begin_cpu_access(struct dma_buf *dmabuf, enum dma_data_direction direction)
{
	struct smaf_handle *handle = dmabuf->priv;

	if (!smaf_allow_cpu_access(handle, direction))
		return -EINVAL;

	if (!handle->db_alloc)
		return -EINVAL;

	return dma_buf_begin_cpu_access(handle->db_alloc,direction);
}

static void smaf_dma_buf_end_cpu_access(struct dma_buf *dmabuf,
					enum dma_data_direction direction)
{
	struct smaf_handle *handle = dmabuf->priv;

	if (handle->db_alloc)
		dma_buf_end_cpu_access(handle->db_alloc,direction);
}

static void *smaf_dma_buf_kmap(struct dma_buf *dmabuf, unsigned long offset)
{
	struct smaf_handle *handle = dmabuf->priv;

	if (!handle->db_alloc)
		return NULL;

	if (!smaf_allow_cpu_access(handle, DMA_BIDIRECTIONAL))
		return NULL;

	return dma_buf_kmap(handle->db_alloc, offset);
}

static void smaf_dma_buf_kunmap(struct dma_buf *dmabuf, unsigned long offset,
				void *ptr)
{
	struct smaf_handle *handle = dmabuf->priv;

	if (!handle->db_alloc)
		return;

	dma_buf_kunmap(handle->db_alloc, offset, ptr);
}

static void *smaf_dma_buf_vmap(struct dma_buf *dmabuf)
{
	struct smaf_handle *handle = dmabuf->priv;

	if (!handle->db_alloc)
		return NULL;

	if (!smaf_allow_cpu_access(handle, DMA_BIDIRECTIONAL))
		return NULL;

	return dma_buf_vmap(handle->db_alloc);
}

static void smaf_dma_buf_vunmap(struct dma_buf *dmabuf, void *vaddr)
{
	struct smaf_handle *handle = dmabuf->priv;

	if (!handle->db_alloc)
		return;

	dma_buf_vunmap(handle->db_alloc, vaddr);
}

static int smaf_attach(struct dma_buf *dmabuf, struct device *dev,
		       struct dma_buf_attachment *attach)
{
	struct smaf_handle *handle = dmabuf->priv;
	struct dma_buf_attachment *db_attach;

	if (!handle->db_alloc)
		return 0;

	db_attach = dma_buf_attach(handle->db_alloc, dev);

	return IS_ERR(db_attach);
}

static void smaf_detach(struct dma_buf *dmabuf,
			struct dma_buf_attachment *attach)
{
	struct smaf_handle *handle = dmabuf->priv;
	struct dma_buf_attachment *db_attachment;

	if (!handle->db_alloc)
		return;

	db_attachment = smaf_find_attachment(handle->db_alloc, attach->dev);
	dma_buf_detach(handle->db_alloc, db_attachment);
}

static struct dma_buf_ops smaf_dma_buf_ops = {
	.attach = smaf_attach,
	.detach = smaf_detach,
	.map_dma_buf = smaf_map_dma_buf,
	.unmap_dma_buf = smaf_unmap_dma_buf,
	.mmap = smaf_mmap,
	.release = smaf_dma_buf_release,
	.begin_cpu_access = smaf_dma_buf_begin_cpu_access,
	.end_cpu_access = smaf_dma_buf_end_cpu_access,
	.map = smaf_dma_buf_kmap,
	.unmap = smaf_dma_buf_kunmap,
	.vmap = smaf_dma_buf_vmap,
	.vunmap = smaf_dma_buf_vunmap,
};

static bool is_smaf_dmabuf(struct dma_buf *dmabuf)
{
	return dmabuf->ops == &smaf_dma_buf_ops;
}

bool smaf_is_secure(struct dma_buf *dmabuf)
{
	struct smaf_handle *handle = dmabuf->priv;

	if (!is_smaf_dmabuf(dmabuf))
		return false;

	return handle->is_secure;
}
EXPORT_SYMBOL(smaf_is_secure);

int smaf_set_secure(struct dma_buf *dmabuf, bool secure)
{
	struct smaf_handle *handle = dmabuf->priv;
	int ret;

	if (!is_smaf_dmabuf(dmabuf))
		return -EINVAL;

	mutex_lock(&smaf_dev.lock);
	if (secure)
		ret = smaf_secure_handle(handle);
	else
		ret = smaf_unsecure_handle(handle);
	mutex_unlock(&smaf_dev.lock);

	return ret;
}
EXPORT_SYMBOL(smaf_set_secure);

int smaf_select_allocator_by_name(struct dma_buf *dmabuf, char *name)
{
	struct smaf_handle *handle = dmabuf->priv;
	struct smaf_allocator *alloc;

	if (!is_smaf_dmabuf(dmabuf))
		return -EINVAL;

	if (handle->allocator)
		return -EINVAL;

	mutex_lock(&smaf_dev.lock);

	list_for_each_entry(alloc, &smaf_dev.head, list_node) {
		if (!strncmp(alloc->name, name, ALLOCATOR_NAME_LENGTH)) {
			handle->allocator = alloc;
			handle->db_alloc = NULL;
		}
	}

	mutex_unlock(&smaf_dev.lock);

	if (!handle->allocator)
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(smaf_select_allocator_by_name);

struct smaf_handle *smaf_create_handle(size_t length, unsigned int flags)
{
	struct smaf_handle *handle;

	DEFINE_DMA_BUF_EXPORT_INFO(info);

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return ERR_PTR(-ENOMEM);

	info.ops = &smaf_dma_buf_ops;
	info.size = length;
	info.flags = flags;
	info.priv = handle;

//	handle->dmabuf = dma_buf_export(handle, &smaf_dma_buf_ops, length, flags);
	handle->dmabuf = dma_buf_export(&info);
	if (IS_ERR(handle->dmabuf)) {
		kfree(handle);
		return NULL;
	}

	handle->length = length;
	handle->flags = flags;

	return handle;
}
EXPORT_SYMBOL(smaf_create_handle);

int smaf_get_info(struct smaf_buffer_info *info)
{
	struct smaf_handle *handle;
	struct dma_buf *dmabuf;
	int ret;

	if(!info)
		return -EINVAL;

	dmabuf = dma_buf_get(info->fd);
	if (!dmabuf)
		return -EINVAL;
	handle = dmabuf->priv;

	info->flags = handle->flags;
	info->start = handle->start;
	info->length = handle->length;
	info->secure = handle->is_secure;
	strcpy(info->name, handle->allocator->name);

	dma_buf_put(dmabuf);
	return 0;
}

/* flush cache includes write back & invalid */
static int smaf_sync_range(unsigned long start, unsigned long end)
{
	struct mm_struct *mm = current->active_mm;
	struct vm_area_struct *vma;
	int ret=0;

	if(!mm)
		return -EINVAL;

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
		outer_flush_all();
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

static long smaf_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case SMAF_IOC_CREATE:
	{
		struct smaf_create_data data;
		struct smaf_handle *handle;

		if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;

		handle = smaf_create_handle(data.length, data.flags);
		if (!handle)
			return -EINVAL;

		handle->start = SMAF_INVALID_START_PA;

		if (data.name[0]) {
			/* user force allocator selection */
			if (smaf_select_allocator_by_name(handle->dmabuf,
							  data.name)) {
				dma_buf_put(handle->dmabuf);
				return -EINVAL;
			}
		}

		handle->fd = dma_buf_fd(handle->dmabuf, data.flags);
		if (handle->fd < 0) {
			dma_buf_put(handle->dmabuf);
			return -EINVAL;
		}

		data.fd = handle->fd;
		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd))) {
			dma_buf_put(handle->dmabuf);
			return -EFAULT;
		}
		break;
	}
	case SMAF_IOC_CREATE_PA:
	{
		struct smaf_create_pa_data data;
		struct smaf_handle *handle;

		if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;

		handle = smaf_create_handle(data.length, data.flags);
		if (!handle)
			return -EINVAL;

              if(data.start == SMAF_INVALID_START_PA)
 			return -EINVAL;
		handle->start = data.start;
		if (data.name[0]) {
			/* user force allocator selection */
			if (smaf_select_allocator_by_name(handle->dmabuf,
							  data.name)) {
				dma_buf_put(handle->dmabuf);
				return -EINVAL;
			}
		}
		handle->fd = dma_buf_fd(handle->dmabuf, data.flags);
		if (handle->fd < 0) {
			dma_buf_put(handle->dmabuf);
			return -EINVAL;
		}
		data.fd = handle->fd;
		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd))) {
			dma_buf_put(handle->dmabuf);
			return -EFAULT;
		}

		break;
	}
	case SMAF_IOC_GET_SECURE_FLAG:
	{
		struct smaf_secure_flag data;
		struct dma_buf *dmabuf;

		if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;

		dmabuf = dma_buf_get(data.fd);
		if (!dmabuf)
			return -EINVAL;

		data.secure = smaf_is_secure(dmabuf);
		dma_buf_put(dmabuf);

		if (copy_to_user((void __user *)arg, &data, _IOC_SIZE(cmd)))
			return -EFAULT;
		break;
	}
	case SMAF_IOC_SET_SECURE_FLAG:
	{
		struct smaf_secure_flag data;
		struct dma_buf *dmabuf;
		int ret;

		if (!smaf_dev.secure)
			return -EINVAL;

		if (copy_from_user(&data, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;

		dmabuf = dma_buf_get(data.fd);
		if (!dmabuf)
			return -EINVAL;

		ret = smaf_set_secure(dmabuf, data.secure);

		dma_buf_put(dmabuf);

		if (ret)
			return -EINVAL;

		break;
	}
	case SMAF_IOC_SYNC_DEVICE:
	{
		struct smaf_sync_range range;

		if(copy_from_user(&range, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
              smaf_sync_range(range.start, range.start + range.len);

		return 0;
	}
	case SMAF_IOC_SYNC_CPU:
	{
		struct smaf_sync_range range;

		if(copy_from_user(&range, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;
              smaf_sync_range(range.start, range.start + range.len);

		return 0;
	}
	case SMAF_IOC_GET_INFO:
	{
		struct smaf_buffer_info info;

		if(copy_from_user(&info, (void __user *)arg, _IOC_SIZE(cmd)))
			return -EFAULT;

              smaf_get_info(&info);

		if (copy_to_user((void __user *)arg, &info, _IOC_SIZE(cmd)))
			return -EFAULT;

		return 0;
	}
	default:
              printk(KERN_ERR "invalid ioctl %x\n", cmd);
		return -EINVAL;
	}

	return 0;
}

#if defined(CONFIG_COMPAT)
static long compat_smaf_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	compat_int_t sint;
	compat_uint_t uint;
	compat_ulong_t ulong;
	compat_u64 u64;
	compat_size_t usize;
	compat_off_t uoff;
	long ret;
	int err = 0;

	if (!filp->f_op || !filp->f_op->unlocked_ioctl)
		return -ENOTTY;

	switch (cmd) {
	case COMPAT_SMAF_IOC_CREATE:
	{
		struct compat_smaf_create_data *data32;
		struct smaf_create_data *data;

		data32 = compat_ptr(arg);						// use 32-bit variable type to point the data(compat is used for 32-bit app and 64-bit kernel, the arg is 32-bit!!
		data = compat_alloc_user_space(sizeof(*data));	// alloc a memory in user space
		if (data == NULL)
			return -EFAULT;

		err = get_user(usize, &data32->length);
		err |= put_user(usize, &data->length);
		err |= get_user(uint, &data32->flags);
		err |= put_user(uint, &data->flags);
		copy_in_user(data->name, data32->name, ALLOCATOR_NAME_LENGTH);
		if(err)
			return err;
		ret = filp->f_op->unlocked_ioctl(filp, SMAF_IOC_CREATE, (unsigned long)data);
		if(ret < 0)
			return ret;
		copy_in_user(&data32->fd, &data->fd, sizeof(compat_uint_t));
		return 0;
	}
	case COMPAT_SMAF_IOC_GET_SECURE_FLAG:
	{
		struct smaf_secure_flag *data;
		struct compat_smaf_secure_flag *data32;

		data32 = compat_ptr(arg);						// use 32-bit variable type to point the data(compat is used for 32-bit app and 64-bit kernel, the arg is 32-bit!!
		data = compat_alloc_user_space(sizeof(*data));	// alloc a memory in user space
		err = get_user(uint, &data32->fd);
		err |= put_user(uint, &data->fd);
		err |= get_user(uint, &data32->secure);
		err |= put_user(uint, &data->secure);
		if(err)
			return err;
		return filp->f_op->unlocked_ioctl(filp, SMAF_IOC_GET_SECURE_FLAG, (unsigned long)data);
	}
	case COMPAT_SMAF_IOC_SET_SECURE_FLAG:
	{
		struct smaf_secure_flag *data;
		struct compat_smaf_secure_flag *data32;

		data32 = compat_ptr(arg);						// use 32-bit variable type to point the data(compat is used for 32-bit app and 64-bit kernel, the arg is 32-bit!!
		data = compat_alloc_user_space(sizeof(*data));	// alloc a memory in user space
		err = get_user(uint, &data32->fd);
		err |= put_user(uint, &data->fd);
		err |= get_user(uint, &data32->secure);
		err |= put_user(uint, &data->secure);
		if(err)
			return err;
		return filp->f_op->unlocked_ioctl(filp, SMAF_IOC_SET_SECURE_FLAG, (unsigned long)data);
	}
	case COMPAT_SMAF_IOC_CREATE_PA:
	{
		struct compat_smaf_create_pa_data *data32;
		struct smaf_create_pa_data *data;

		data32 = compat_ptr(arg);						// use 32-bit variable type to point the data(compat is used for 32-bit app and 64-bit kernel, the arg is 32-bit!!
		data = compat_alloc_user_space(sizeof(*data));	// alloc a memory in user space
		if (data == NULL)
			return -EFAULT;

		err = get_user(usize, &data32->length);
		err |= put_user(usize, &data->length);
		err |= get_user(uoff, &data32->start);
		err |= put_user(uoff, &data->start);
		err |= get_user(uint, &data32->flags);
		err |= put_user(uint, &data->flags);
		copy_in_user(data->name, data32->name, ALLOCATOR_NAME_LENGTH);
		if(err)
			return err;
		ret = filp->f_op->unlocked_ioctl(filp, SMAF_IOC_CREATE_PA, (unsigned long)data);
		if(ret < 0)
			return ret;
		copy_in_user(&data32->fd, &data->fd, sizeof(compat_uint_t));
		return 0;
	}
	case COMPAT_SMAF_IOC_SYNC_DEVICE:
	{
		struct compat_smaf_sync_range *data32;
		struct smaf_sync_range *data;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (data == NULL)
			return -EFAULT;

		err = get_user(usize, &data32->start);
		err |= put_user(usize, &data->start);
		err |= get_user(usize, &data32->len);
		err |= put_user(usize, &data->len);
		if(err)
			return err;
		return filp->f_op->unlocked_ioctl(filp, SMAF_IOC_SYNC_DEVICE, (unsigned long)data);
	}
	case COMPAT_SMAF_IOC_SYNC_CPU:
	{
		struct compat_smaf_sync_range *data32;
		struct smaf_sync_range *data;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (data == NULL)
			return -EFAULT;
		err = get_user(usize, &data32->start);
		err |= put_user(usize, &data->start);
		err |= get_user(usize, &data32->len);
		err |= put_user(usize, &data->len);
		if(err)
			return err;
		return filp->f_op->unlocked_ioctl(filp, SMAF_IOC_SYNC_CPU, (unsigned long)data);
	}
	case COMPAT_SMAF_IOC_GET_INFO:
	{
		struct compat_smaf_buffer_info *data32;
		struct smaf_buffer_info *data;

		data32 = compat_ptr(arg);
		data = compat_alloc_user_space(sizeof(*data));
		if (data == NULL)
			return -EFAULT;
		err = get_user(uint, &data32->fd);
		err |= put_user(uint, &data->fd);
		if(err)
			return err;
		ret = filp->f_op->unlocked_ioctl(filp, SMAF_IOC_GET_INFO, (unsigned long)data);
		if(ret < 0)
			return ret;

		copy_in_user(&data32->start, &data->start, sizeof(compat_size_t));
		copy_in_user(&data32->length, &data->length, sizeof(compat_size_t));
		copy_in_user(&data32->secure, &data->secure, sizeof(compat_int_t));
		copy_in_user(&data32->flags, &data->flags, sizeof(compat_uint_t));
		copy_in_user(&data32->name, &data->name, ALLOCATOR_NAME_LENGTH);
		return 0;
	}
	default:
		printk(KERN_ERR "invalid ioctl %x\n", cmd);
		return -EINVAL;
	}
	return 0;
}
#endif

static const struct file_operations smaf_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = smaf_ioctl,
#if defined(CONFIG_COMPAT)
	.compat_ioctl   = compat_smaf_ioctl,
#endif
};

static int __init smaf_init(void)
{
	int ret = 0;

	smaf_dev.misc_dev.minor = 70;
	smaf_dev.misc_dev.name  = "smaf";
	smaf_dev.misc_dev.fops  = &smaf_fops;

	/* register misc device */
	ret = misc_register(&smaf_dev.misc_dev);
	if (ret < 0)
    {
		return ret;
    }
	mutex_init(&smaf_dev.lock);
	INIT_LIST_HEAD(&smaf_dev.head);

	return ret;
}
module_init(smaf_init);

static void __exit smaf_deinit(void)
{
	misc_deregister(&smaf_dev.misc_dev);
}
module_exit(smaf_deinit);

MODULE_DESCRIPTION("Secure Memory Allocation Framework");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard at linaro.org>");
