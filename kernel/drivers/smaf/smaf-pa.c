#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <asm/cacheflush.h>
#include <asm/outercache.h>

#include "smaf-allocator.h"
#include "smaf.h"




struct smaf_pa_buffer_info {
	struct device *dev;
    	struct sg_table *sgt;
	size_t size;
       size_t start;
	void *vaddr;
	dma_addr_t paddr;
};

static char *smaf_pa_dev_list[] = {
	"vdec",
	NULL,
};

static struct sg_table *smaf_pa_map(struct dma_buf_attachment *attachment,
				     enum dma_data_direction direction)
{
	struct smaf_pa_buffer_info *info = attachment->dmabuf->priv;
	struct sg_table *sgt;
	int ret;

	sgt = kzalloc(sizeof(*sgt), GFP_KERNEL);
	if (!sgt)
		return ERR_PTR(-ENOMEM);

	ret = sg_alloc_table(sgt, 1, GFP_KERNEL);
	if (ret){
		printk("[%s][%d] malloc sg table failed\n", __FUNCTION__, __LINE__);
		goto error;
	}

    	info->sgt = sgt;
    	sg_set_page(sgt->sgl, 0x0, info->size, 0);
    	sg_dma_address(sgt->sgl) = info->paddr;
	return sgt;

error:
	kfree(sgt);
	return ERR_PTR(-ENOMEM);
}

static void smaf_pa_unmap(struct dma_buf_attachment *attachment,
			   struct sg_table *sgt,
			   enum dma_data_direction direction)
{
	/* do nothing */
}

static int smaf_pa_mmap(struct dma_buf *dmabuf, struct vm_area_struct *vma)
{
	struct smaf_pa_buffer_info *info = dmabuf->priv;
	int ret;

	if (info->size < vma->vm_end - vma->vm_start)
		return -EINVAL;

	vma->vm_flags |= VM_IO | VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP;

	ret = dma_mmap_attrs(info->dev, vma, info->vaddr, info->paddr,
			     info->size,DMA_ATTR_WRITE_COMBINE);

	return ret;
}

static void *smaf_pa_vmap(struct dma_buf *dmabuf)
{
	struct smaf_pa_buffer_info *info = dmabuf->priv;

	return info->vaddr;
}

static void *smaf_kmap_atomic(struct dma_buf *dmabuf, unsigned long offset)
{
	struct smaf_pa_buffer_info *info = dmabuf->priv;

	return (void *)info->vaddr + offset;
}

static void smaf_pa_release(struct dma_buf *dmabuf)
{
	struct smaf_pa_buffer_info *info = dmabuf->priv;

	if(info->vaddr);
		iounmap(info->vaddr);

	kfree(info);
}

static int smaf_pa_attach(struct dma_buf *dmabuf, struct device *dev,struct dma_buf_attachment *db_attachment)
{
       char *p = smaf_pa_dev_list[0];

       while(p) {
              if(strncmp(p, dev_name(dev), strlen(p)) == 0)
                     return 0;
              p++;
       }
	return -EINVAL;
}

static void smaf_pa_detach(struct dma_buf *dmabuf, struct dma_buf_attachment *db_attachment)
{
	return;
}

static int smaf_pa_begin_cpu_access(struct dma_buf *dmabuf, size_t start,
					size_t len,
					enum dma_data_direction direction)
{
	struct smaf_pa_buffer_info *info = dmabuf->priv;
	size_t end = start + len;

	if(!info->vaddr)
		return -EINVAL;

	if(end > (size_t)info->vaddr && start < (size_t)info->vaddr + info->size) {
		if(start < (size_t)info->vaddr)
			start = (size_t)info->vaddr;
		if(end > (size_t)info->vaddr + info->size)
			end = (size_t)info->vaddr + info->size;

		dmac_flush_range((void*)(start& PAGE_MASK), (void*)PAGE_ALIGN(end));
		outer_flush_all();
#ifndef CONFIG_OUTER_CACHE
		{
			extern void Chip_Flush_Miu_Pipe(void);
			Chip_Flush_Miu_Pipe();
		}
#endif
	}

	return -EINVAL;
}

static void smaf_pa_end_cpu_access(struct dma_buf *dmabuf, size_t start,
				       size_t len,
				       enum dma_data_direction direction)
{
	struct smaf_pa_buffer_info *info = dmabuf->priv;
	size_t end = start + len;

	if(!info->vaddr)
		return;

	if(end > (size_t)info->vaddr && start < (size_t)info->vaddr + info->size) {
		if(start < (size_t)info->vaddr)
			start =(size_t) info->vaddr;
		if(end > (size_t)info->vaddr + info->size)
			end = (size_t)info->vaddr + info->size;

		dmac_flush_range((void*)(start& PAGE_MASK), (void*)PAGE_ALIGN(end));
		outer_flush_all();
#ifndef CONFIG_OUTER_CACHE
		{
			extern void Chip_Flush_Miu_Pipe(void);
			Chip_Flush_Miu_Pipe();
		}
#endif
	}
}

static struct dma_buf_ops smaf_pa_ops = {
    	.attach = smaf_pa_attach,
	.detach = smaf_pa_detach,
	.begin_cpu_access = smaf_pa_begin_cpu_access,
	.end_cpu_access = smaf_pa_end_cpu_access,
	.map_dma_buf = smaf_pa_map,
	.unmap_dma_buf = smaf_pa_unmap,
	.mmap = smaf_pa_mmap,
	.release = smaf_pa_release,
	.map = smaf_kmap_atomic,
	.vmap = smaf_pa_vmap,
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
 * smaf_pa_match - return true if at least one device has been found
 */
static bool smaf_pa_match(struct dma_buf *dmabuf)
{
	return !!find_matching_device(dmabuf);
}

static struct dma_buf *smaf_pa_allocate(struct dma_buf *dmabuf,
					 size_t length, unsigned int flags)
{
	struct dma_buf_attachment *attach_obj;
	struct smaf_pa_buffer_info *info;
	struct dma_buf *pa_dmabuf;
	struct smaf_handle *handle;

	DEFINE_DMA_BUF_EXPORT_INFO(export);

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return NULL;

	handle = dmabuf->priv;

	BUG_ON(!handle);

	info->paddr = handle->start;
	info->size = round_up(length, PAGE_SIZE);
	info->dev = find_matching_device(dmabuf);

	info->vaddr = ioremap(info->paddr, info->size);
	if (!info->vaddr) {
//		ret = -ENOMEM;
		goto error;
	}

	export.ops = &smaf_pa_ops;
	export.size = info->size;
	export.flags = flags;
	export.priv = info;

//	pa_dmabuf = dma_buf_export(info, &smaf_pa_ops, info->size, flags);
	pa_dmabuf = dma_buf_export(&export);
	if (IS_ERR(pa_dmabuf))
		goto error;

	list_for_each_entry(attach_obj, &dmabuf->attachments, node) {
		dma_buf_attach(pa_dmabuf, attach_obj->dev);
	}

	return pa_dmabuf;

error:
	kfree(info);
	return NULL;
}

struct smaf_allocator smaf_pa = {
	.match = smaf_pa_match,
	.allocate = smaf_pa_allocate,
	.name = "smaf-pa",
	.ranking = 1,
};

static int __init smaf_pa_init(void)
{
	return smaf_register_allocator(&smaf_pa);
}
module_init(smaf_pa_init);

static void __exit smaf_pa_deinit(void)
{
	smaf_unregister_allocator(&smaf_pa);
}
module_exit(smaf_pa_deinit);

MODULE_DESCRIPTION("SMAF PA module");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Mstar Semiconductor");
