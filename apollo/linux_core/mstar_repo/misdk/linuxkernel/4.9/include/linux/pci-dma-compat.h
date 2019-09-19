/* include this file if the platform implements the dma_ DMA Mapping API
 * and wants to provide the pci_ DMA Mapping API in terms of it */

#ifndef _ASM_GENERIC_PCI_DMA_COMPAT_H
#define _ASM_GENERIC_PCI_DMA_COMPAT_H

#include <linux/dma-mapping.h>

/* This defines the direction arg to the DMA mapping routines. */
#define PCI_DMA_BIDIRECTIONAL	0
#define PCI_DMA_TODEVICE	1
#define PCI_DMA_FROMDEVICE	2
#define PCI_DMA_NONE		3

#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
#include <mstar/mstar_chip.h>

#define PA_TO_BA(x)\
	if(x > ARM_MIU2_BASE_ADDR)\
		x = x - ARM_MIU2_BASE_ADDR + ARM_MIU2_BUS_BASE;\
	else if(x > ARM_MIU1_BASE_ADDR)\
		x = x - ARM_MIU1_BASE_ADDR + ARM_MIU1_BUS_BASE;\
	else\
		x = x - ARM_MIU0_BASE_ADDR + ARM_MIU0_BUS_BASE;

#define BA_TO_PA(x)\
	if(x > ARM_MIU2_BUS_BASE)\
		x = x - ARM_MIU2_BUS_BASE + ARM_MIU2_BASE_ADDR;\
    else if(x > ARM_MIU1_BUS_BASE)\
        x = x - ARM_MIU1_BUS_BASE + ARM_MIU1_BASE_ADDR;\
    else\
        x = x - ARM_MIU0_BUS_BASE + ARM_MIU0_BASE_ADDR;
#endif
static inline void *
pci_alloc_consistent(struct pci_dev *hwdev, size_t size,
		     dma_addr_t *dma_handle)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	void *va;
	va = dma_alloc_coherent(hwdev == NULL ? NULL : &hwdev->dev, size, dma_handle, GFP_ATOMIC);
	BA_TO_PA(*dma_handle);
		return va;
#else
	return dma_alloc_coherent(hwdev == NULL ? NULL : &hwdev->dev, size, dma_handle, GFP_ATOMIC);
#endif
}

static inline void *
pci_zalloc_consistent(struct pci_dev *hwdev, size_t size,
		      dma_addr_t *dma_handle)
{
	return dma_zalloc_coherent(hwdev == NULL ? NULL : &hwdev->dev,
				   size, dma_handle, GFP_ATOMIC);
}

static inline void
pci_free_consistent(struct pci_dev *hwdev, size_t size,
		    void *vaddr, dma_addr_t dma_handle)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	PA_TO_BA(dma_handle);
	dma_free_coherent(hwdev == NULL ? NULL : &hwdev->dev, size, vaddr, dma_handle);
#else
	dma_free_coherent(hwdev == NULL ? NULL : &hwdev->dev, size, vaddr, dma_handle);
#endif
}

static inline dma_addr_t
pci_map_single(struct pci_dev *hwdev, void *ptr, size_t size, int direction)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	dma_addr_t dma_handle;
	dma_handle = dma_map_single(hwdev == NULL ? NULL : &hwdev->dev, ptr, size, (enum dma_data_direction)direction);
	BA_TO_PA(dma_handle);
	return dma_handle;
#else
	return dma_map_single(hwdev == NULL ? NULL : &hwdev->dev, ptr, size, (enum dma_data_direction)direction);
#endif
}

static inline void
pci_unmap_single(struct pci_dev *hwdev, dma_addr_t dma_addr,
		 size_t size, int direction)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	PA_TO_BA(dma_addr);
	dma_unmap_single(hwdev == NULL ? NULL : &hwdev->dev, dma_addr, size, (enum dma_data_direction)direction);
#else
	dma_unmap_single(hwdev == NULL ? NULL : &hwdev->dev, dma_addr, size, (enum dma_data_direction)direction);
#endif
}

static inline dma_addr_t
pci_map_page(struct pci_dev *hwdev, struct page *page,
	     unsigned long offset, size_t size, int direction)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	dma_addr_t dma_handle;
	dma_handle = dma_map_page(hwdev == NULL ? NULL : &hwdev->dev, page, offset, size, (enum dma_data_direction)direction);
	BA_TO_PA(dma_handle);
	return dma_handle;
#else
	return dma_map_page(hwdev == NULL ? NULL : &hwdev->dev, page, offset, size, (enum dma_data_direction)direction);
#endif
}

static inline void
pci_unmap_page(struct pci_dev *hwdev, dma_addr_t dma_address,
	       size_t size, int direction)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	PA_TO_BA(dma_address);
	dma_unmap_page(hwdev == NULL ? NULL : &hwdev->dev, dma_address, size, (enum dma_data_direction)direction);
#else
	dma_unmap_page(hwdev == NULL ? NULL : &hwdev->dev, dma_address, size, (enum dma_data_direction)direction);
#endif
}

static inline int
pci_map_sg(struct pci_dev *hwdev, struct scatterlist *sg,
	   int nents, int direction)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
		struct scatterlist *s;
		int i;

		for_each_sg(sg, s, nents, i)
		PA_TO_BA(s->dma_address);
	return dma_map_sg(hwdev == NULL ? NULL : &hwdev->dev, sg, nents, (enum dma_data_direction)direction);
#else
	return dma_map_sg(hwdev == NULL ? NULL : &hwdev->dev, sg, nents, (enum dma_data_direction)direction);
#endif
}

static inline void
pci_unmap_sg(struct pci_dev *hwdev, struct scatterlist *sg,
	     int nents, int direction)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nents, i)
		PA_TO_BA(s->dma_address);
	dma_unmap_sg(hwdev == NULL ? NULL : &hwdev->dev, sg, nents, (enum dma_data_direction)direction);
#else
	dma_unmap_sg(hwdev == NULL ? NULL : &hwdev->dev, sg, nents, (enum dma_data_direction)direction);
#endif
}

static inline void
pci_dma_sync_single_for_cpu(struct pci_dev *hwdev, dma_addr_t dma_handle,
		    size_t size, int direction)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	PA_TO_BA(dma_handle);
	dma_sync_single_for_cpu(hwdev == NULL ? NULL : &hwdev->dev, dma_handle, size, (enum dma_data_direction)direction);
#else
	dma_sync_single_for_cpu(hwdev == NULL ? NULL : &hwdev->dev, dma_handle, size, (enum dma_data_direction)direction);
#endif
}

static inline void
pci_dma_sync_single_for_device(struct pci_dev *hwdev, dma_addr_t dma_handle,
		    size_t size, int direction)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	PA_TO_BA(dma_handle);
	dma_sync_single_for_device(hwdev == NULL ? NULL : &hwdev->dev, dma_handle, size, (enum dma_data_direction)direction);
#else
	dma_sync_single_for_device(hwdev == NULL ? NULL : &hwdev->dev, dma_handle, size, (enum dma_data_direction)direction);
#endif
}

static inline void
pci_dma_sync_sg_for_cpu(struct pci_dev *hwdev, struct scatterlist *sg,
		int nelems, int direction)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nelems, i)
		PA_TO_BA(s->dma_address);
	dma_sync_sg_for_cpu(hwdev == NULL ? NULL : &hwdev->dev, sg, nelems, (enum dma_data_direction)direction);
#else
	dma_sync_sg_for_cpu(hwdev == NULL ? NULL : &hwdev->dev, sg, nelems, (enum dma_data_direction)direction);
#endif
}

static inline void
pci_dma_sync_sg_for_device(struct pci_dev *hwdev, struct scatterlist *sg,
		int nelems, int direction)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	struct scatterlist *s;
	int i;

	for_each_sg(sg, s, nelems, i)
		PA_TO_BA(s->dma_address);
	dma_sync_sg_for_device(hwdev == NULL ? NULL : &hwdev->dev, sg, nelems, (enum dma_data_direction)direction);
#else
	dma_sync_sg_for_device(hwdev == NULL ? NULL : &hwdev->dev, sg, nelems, (enum dma_data_direction)direction);
#endif
}

static inline int
pci_dma_mapping_error(struct pci_dev *pdev, dma_addr_t dma_addr)
{
#ifdef CONFIG_MP_PCI_PATCH_ADDR_TRANSLATE
	PA_TO_BA(dma_addr);
		return dma_mapping_error(&pdev->dev, dma_addr);
#else
	return dma_mapping_error(&pdev->dev, dma_addr);
#endif
}

#ifdef CONFIG_PCI
static inline int pci_set_dma_mask(struct pci_dev *dev, u64 mask)
{
	return dma_set_mask(&dev->dev, mask);
}

static inline int pci_set_consistent_dma_mask(struct pci_dev *dev, u64 mask)
{
	return dma_set_coherent_mask(&dev->dev, mask);
}

static inline int pci_set_dma_max_seg_size(struct pci_dev *dev,
					   unsigned int size)
{
	return dma_set_max_seg_size(&dev->dev, size);
}

static inline int pci_set_dma_seg_boundary(struct pci_dev *dev,
					   unsigned long mask)
{
	return dma_set_seg_boundary(&dev->dev, mask);
}
#else
static inline int pci_set_dma_mask(struct pci_dev *dev, u64 mask)
{ return -EIO; }
static inline int pci_set_consistent_dma_mask(struct pci_dev *dev, u64 mask)
{ return -EIO; }
static inline int pci_set_dma_max_seg_size(struct pci_dev *dev,
					   unsigned int size)
{ return -EIO; }
static inline int pci_set_dma_seg_boundary(struct pci_dev *dev,
					   unsigned long mask)
{ return -EIO; }
#endif

#endif
