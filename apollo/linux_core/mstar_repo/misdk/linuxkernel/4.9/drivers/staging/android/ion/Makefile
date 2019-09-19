ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
EXTRA_CFLAGS      +=  -Idrivers/mstar2/include
EXTRA_CFLAGS      +=  -Imm
endif

obj-$(CONFIG_ION) +=	ion.o ion-ioctl.o ion_heap.o \
			ion_page_pool.o ion_system_heap.o \
			ion_carveout_heap.o ion_chunk_heap.o
ifdef CONFIG_MP_MMA_CMA_ENABLE
obj-$(CONFIG_ION) +=	ion_mstar_iommu_cma.o
else
obj-$(CONFIG_ION) +=	ion_cma_heap.o
endif

ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
obj-$(CONFIG_ION) += ion_mstar_cma_heap.o
endif

obj-$(CONFIG_ION_TEST) += ion_test.o

ifdef CONFIG_COMPAT
obj-$(CONFIG_ION) += compat_ion.o
endif

obj-$(CONFIG_ION_DUMMY) += ion_dummy_driver.o
obj-$(CONFIG_ION_TEGRA) += tegra/
obj-$(CONFIG_ION_HISI) += hisilicon/
obj-$(CONFIG_ION_OF) += ion_of.o
ifdef CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER
obj-$(CONFIG_MP_ION_PATCH_MSTAR) += mstar/
endif

