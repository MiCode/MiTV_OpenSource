#
# Makefile for MMC/SD card drivers
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

obj-$(CONFIG_MMC_BLOCK)		+= mmc_block.o
mmc_block-objs			:= block.o queue.o
obj-$(CONFIG_MMC_TEST)		+= mmc_test.o

obj-$(CONFIG_SDIO_UART)		+= sdio_uart.o

