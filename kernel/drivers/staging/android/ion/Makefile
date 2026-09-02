# SPDX-License-Identifier: GPL-2.0

ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
EXTRA_CFLAGS      +=  -Idrivers/mstar2/include
EXTRA_CFLAGS      +=  -Imm
endif

obj-$(CONFIG_ION) +=	ion-alloc.o
ion-alloc-objs += ion.o ion-ioctl.o ion_heap.o
ion-alloc-$(CONFIG_ION_SYSTEM_HEAP) += ion_system_heap.o ion_page_pool.o
ion-alloc-$(CONFIG_ION_CARVEOUT_HEAP) += ion_carveout_heap.o
ion-alloc-$(CONFIG_ION_CHUNK_HEAP) += ion_chunk_heap.o
ifdef CONFIG_MP_MMA_CMA_ENABLE
ion-alloc-$(CONFIG_ION_CMA_HEAP) += ion_mstar_iommu_cma.o
else
ion-alloc-$(CONFIG_ION_CMA_HEAP) += ion_cma_heap.o
endif

ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
obj-$(CONFIG_ION) += ion_mstar_cma_heap.o
ifdef CONFIG_MP_MMA_ENABLE
obj-$(CONFIG_ION) += ion_iommu_carveout_heap.o
endif
obj-$(CONFIG_MP_ION_PATCH_MSTAR) += mstar/
endif
