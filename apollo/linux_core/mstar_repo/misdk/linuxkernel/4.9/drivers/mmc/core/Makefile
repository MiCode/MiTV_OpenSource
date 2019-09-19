#
# Makefile for the kernel mmc core.
#
CONFIG_MSTAR_CHIP_NAME := $(subst ",,$(CONFIG_MSTAR_CHIP_NAME))

# compatiblity with mstar
EXTRA_CFLAGS                += -Idrivers/mstar/mmc/host/eMMCDrv/inc/api/
EXTRA_CFLAGS                += -Idrivers/mstar/mmc/host/eMMCDrv/inc/common/
EXTRA_CFLAGS                += -Idrivers/mstar/mmc/host/eMMCDrv/inc/config/

# compatiblity with mstar2
EXTRA_CFLAGS                += -Idrivers/mstar2/drv/mmc/inc/api/
EXTRA_CFLAGS                += -Idrivers/mstar2/drv/mmc/inc/common/
EXTRA_CFLAGS                += -Idrivers/mstar2/drv/mmc/inc/config/
EXTRA_CFLAGS                += -Idrivers/mstar2/hal/$(CONFIG_MSTAR_CHIP_NAME)/mmc

obj-$(CONFIG_MMC)		+= mmc_core.o
mmc_core-y			:= core.o bus.o host.o \
				   mmc.o mmc_ops.o sd.o sd_ops.o \
				   sdio.o sdio_ops.o sdio_bus.o \
				   sdio_cis.o sdio_io.o sdio_irq.o \
				   quirks.o slot-gpio.o
mmc_core-$(CONFIG_OF)		+= pwrseq.o
obj-$(CONFIG_PWRSEQ_SIMPLE)	+= pwrseq_simple.o
obj-$(CONFIG_PWRSEQ_EMMC)	+= pwrseq_emmc.o
mmc_core-$(CONFIG_DEBUG_FS)	+= debugfs.o
