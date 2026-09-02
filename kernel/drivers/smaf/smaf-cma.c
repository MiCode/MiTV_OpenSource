/*
 * smaf-cma.c
 *
 * Copyright (C) Linaro SA 2015
 * Author: Benjamin Gaignard <benjamin.gaignard@linaro.org> for Linaro.
 * License terms:  GNU General Public License (GPL), version 2
 */

#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/slab.h>

#include "smaf-allocator.h"

struct smaf_cma_buffer_info {
	struct device *dev;
	struct sg_table *sgt;
	size_t size;
	void *vaddr;
	dma_addr_t paddr;
};

static char *smaf_cma_dev_list[] = {
	"vdec",
	NULL,
};

/**
 * find_matching_device - iterate over the attached devices to find one
 * with coherent_dma_mask correctly set to DMA_BIT_MASK(32).
 * Matching device (if any) will be used to aim CMA area.
 */
static struct device *find_matching_device(struct dma_buf *dmabuf)
{
	struct dma_buf_attachment *attach_obj;

	list_for_each_entry(attach_obj, &dmabuf->attachments, node) {
		if (attach_obj->dev->coherent_dma_mask == DMA_BIT_MASK(32))
			return attach_obj->dev;
	}

	return NULL;
}

/**
 * smaf_cma_match - return true if at least one device has been found
 */
static bool smaf_cma_match(struct dma_buf *dmabuf)
{
	return !!find_matching_device(dmabuf);
}

static void smaf_cma_release(struct dma_buf *dmabuf)
{
	struct smaf_cma_buffer_info *info = dmabuf->priv;
	DEFINE_DMA_ATTRS(attrs);

	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);

	dma_free_attrs(info->dev, info->size, info->vaddr, info->paddr, &attrs);

	kfree(info);
}

static struct sg_table *smaf_cma_map(struct dma_buf_attachment *attachment,
				     enum dma_data_direction direction)
{
	struct smaf_cma_buffer_info *info = attachment->dmabuf->priv;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return NULL;

	ret = dma_get_sgtable(info->dev, sgt, info->vaddr,
			      info->paddr, info->size);
	if (ret < 0)
		goto out;

	sg_dma_address(sgt->sgl) = info->paddr;
	info->sgt = sgt;
	return sgt;

out:
	kfree(sgt);
	return NULL;
}

static void smaf_cma_unmap(struct dma_buf_attachment *attachment,
			   struct sg_table *sgt,
			   enum dma_data_direction direction)
{
	/* do nothing */
}

static int smaf_cma_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct smaf_cma_buffer_info *info = dmabuf->priv;
	int ret;
	DEFINE_DMA_ATTRS(attrs);

	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);

	if (info->size < vma->vm_end - vma->vm_start)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;
	ret = dma_mmap_attrs(info->dev, vma, info->vaddr, info->paddr,
			     info->size, &attrs);

	return ret;
}

static void *smaf_cma_vmap(struct dma_buf *dmabuf)
{
	struct smaf_cma_buffer_info *info = dmabuf->priv;

	return info->vaddr;
}

static void *smaf_kmap_atomic(struct dma_buf *dmabuf, unsigned long offset)
{
	struct smaf_cma_buffer_info *info = dmabuf->priv;

	return (void *)info->vaddr + offset;
}

static int smaf_cma_attach(struct dma_buf *dmabuf, struct device *dev, struct dma_buf_attachment *db_attachment)
{
       char *p = smaf_cma_dev_list[0];

       while(p) {
              if(strncmp(p, dev_name(dev), strlen(p)) == 0)
                     return 0;
              p++;
       }
	return -EINVAL;
}

static void smaf_cma_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *db_attachment)
{
	return;
}

static dma_addr_t  smaf_cma_find_matching_sg(struct sg_table *sgt, size_t start,
					size_t len)
{
	struct scatterlist *sg;
	unsigned int count;
	size_t size = 0;
	dma_addr_t dma_handle = 0;

	/* Find a scatterlist that starts in "start" and has "len"
	* or return error */
	for_each_sg(sgt->sgl, sg, sgt->nents, count) {
		if ((size == start) && (len == sg_dma_len(sg))) {
			dma_handle = sg_dma_address(sg);
			break;
		}
		size += sg_dma_len(sg);
	}
	return dma_handle;
}

static int smaf_cma_begin_cpu_access(struct dma_buf *dmabuf, size_t start,
					size_t len,
					enum dma_data_direction direction)
{
	dma_addr_t dma_handle;
	struct smaf_cma_buffer_info *info = dmabuf->priv;
	struct device *dev = info->dev;
	struct sg_table *sgt = info->sgt;

	dma_handle = smaf_cma_find_matching_sg(sgt, start, len);
	if (!dma_handle)
		return -EINVAL;

	dma_sync_single_range_for_cpu(dev, dma_handle, 0, len, direction);

	return 0;
}

static void smaf_cma_end_cpu_access(struct dma_buf *dmabuf, size_t start,
				       size_t len,
				       enum dma_data_direction direction)
{
	dma_addr_t dma_handle;
	struct smaf_cma_buffer_info *info = dmabuf->priv;
	struct device *dev = info->dev;
	struct sg_table *sgt = info->sgt;

	dma_handle = smaf_cma_find_matching_sg(sgt, start, len);
	if (!dma_handle)
		return;

	dma_sync_single_range_for_device(dev, dma_handle, 0, len, direction);
}

static struct dma_buf_ops smaf_cma_ops = {
	.attach = smaf_cma_attach,
	.detach = smaf_cma_detach,
	.begin_cpu_access = smaf_cma_begin_cpu_access,
	.end_cpu_access = smaf_cma_end_cpu_access,
	.map_dma_buf = smaf_cma_map,
	.unmap_dma_buf = smaf_cma_unmap,
	.mmap = smaf_cma_mmap,
	.release = smaf_cma_release,
	.kmap_atomic = smaf_kmap_atomic,
	.kmap = smaf_kmap_atomic,
	.vmap = smaf_cma_vmap,
};

static struct dma_buf *smaf_cma_allocate(struct dma_buf *dmabuf,
					 size_t length, unsigned int flags)
{
	struct dma_buf_attachment *attach_obj;
	struct smaf_cma_buffer_info *info;
	struct dma_buf *cma_dmabuf;
	int ret;

	//DEFINE_DMA_BUF_EXPORT_INFO(export);
	DEFINE_DMA_ATTRS(attrs);

	dma_set_attr(DMA_ATTR_WRITE_COMBINE, &attrs);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;

	info->size = round_up(length, PAGE_SIZE);
	info->dev = find_matching_device(dmabuf);

	info->vaddr = dma_alloc_attrs(info->dev, info->size, &info->paddr,
				      GFP_KERNEL | __GFP_NOWARN, &attrs);
	if (!info->vaddr) {
		ret = -ENOMEM;
		goto error;
	}

//	export.ops = &smaf_cma_ops;
//	export.size = info->size;
//	export.flags = flags;
//	export.priv = info;

       cma_dmabuf = dma_buf_export(info, &smaf_cma_ops, info->size, flags);
//	cma_dmabuf = dma_buf_export(&export);
	if (IS_ERR(cma_dmabuf))
		goto error;

	list_for_each_entry(attach_obj, &dmabuf->attachments, node) {
		dma_buf_attach(cma_dmabuf, attach_obj->dev);
	}

	return cma_dmabuf;

error:
	kfree(info);
	return NULL;
}

struct smaf_allocator smaf_cma = {
	.match = smaf_cma_match,
	.allocate = smaf_cma_allocate,
	.name = "smaf-cma",
	.ranking = 0,
};

static int __init smaf_cma_init(void)
{
	INIT_LIST_HEAD(&smaf_cma.list_node);
	return smaf_register_allocator(&smaf_cma);
}
module_init(smaf_cma_init);

static void __exit smaf_cma_deinit(void)
{
	smaf_unregister_allocator(&smaf_cma);
}
module_exit(smaf_cma_deinit);

MODULE_DESCRIPTION("SMAF CMA module");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Benjamin Gaignard <benjamin.gaignard@linaro.org>");
