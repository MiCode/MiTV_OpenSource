# SPDX-License-Identifier: GPL-2.0
ifeq ($(CONFIG_MP_CMA_PATCH_CMA_MSTAR_DRIVER_BUFFER),y)
EXTRA_CFLAGS      +=  -Idrivers/mstar2/include
EXTRA_CFLAGS      +=  -Imm
endif

obj-$(CONFIG_HAS_DMA)			+= mapping.o
obj-$(CONFIG_DMA_CMA)			+= contiguous.o
obj-$(CONFIG_HAVE_GENERIC_DMA_COHERENT) += coherent.o removed.o
obj-$(CONFIG_DMA_DIRECT_OPS)		+= direct.o
obj-$(CONFIG_DMA_NONCOHERENT_OPS)	+= noncoherent.o
obj-$(CONFIG_DMA_VIRT_OPS)		+= virt.o
obj-$(CONFIG_DMA_API_DEBUG)		+= debug.o
obj-$(CONFIG_SWIOTLB)			+= swiotlb.o

