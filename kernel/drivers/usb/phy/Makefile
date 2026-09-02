# SPDX-License-Identifier: GPL-2.0

#compatibulity with mstar
EXTRA_CFLAGS += -Idrivers/mstar/usb

#compatibility with mstar2
ifeq ($(CONFIG_ONEHAL_ARCH_SUPPORT),y)
ONEHAL_VERSION = $(subst \",,$(CONFIG_ONEHAL_VERSION))
ONEHAL_DIR = ver_$(ONEHAL_VERSION)
EXTRA_CFLAGS += -Idrivers/mstar2/hal/$(CONFIG_MSTAR_CHIP_NAME)/usb/$(ONEHAL_DIR)
endif
EXTRA_CFLAGS += -Idrivers/mstar2/hal/$(CONFIG_MSTAR_CHIP_NAME)/usb
EXTRA_CFLAGS += -Idrivers/mstar2/include
EXTRA_CFLAGS += -Idrivers/mstar2/drv/gpio
EXTRA_CFLAGS += -Idrivers/mstar2/hal/$(CONFIG_MSTAR_CHIP_NAME)/gpio

#
# Makefile for physical layer USB drivers
#
obj-$(CONFIG_USB_PHY)			+= phy.o
obj-$(CONFIG_OF)			+= of.o

# transceiver drivers, keep the list sorted

obj-$(CONFIG_AB8500_USB)		+= phy-ab8500-usb.o
obj-$(CONFIG_FSL_USB2_OTG)		+= phy-fsl-usb.o
obj-$(CONFIG_ISP1301_OMAP)		+= phy-isp1301-omap.o
obj-$(CONFIG_NOP_USB_XCEIV)		+= phy-generic.o
obj-$(CONFIG_TAHVO_USB)			+= phy-tahvo.o
obj-$(CONFIG_AM335X_CONTROL_USB)	+= phy-am335x-control.o
obj-$(CONFIG_AM335X_PHY_USB)		+= phy-am335x.o
obj-$(CONFIG_OMAP_OTG)			+= phy-omap-otg.o
obj-$(CONFIG_TWL6030_USB)		+= phy-twl6030-usb.o
obj-$(CONFIG_USB_TEGRA_PHY)		+= phy-tegra-usb.o
obj-$(CONFIG_USB_GPIO_VBUS)		+= phy-gpio-vbus-usb.o
obj-$(CONFIG_USB_ISP1301)		+= phy-isp1301.o
obj-$(CONFIG_USB_MV_OTG)		+= phy-mv-usb.o
obj-$(CONFIG_USB_MS_OTG)		+= phy-ms-usb.o
obj-$(CONFIG_USB_MXS_PHY)		+= phy-mxs-usb.o
obj-$(CONFIG_USB_ULPI)			+= phy-ulpi.o
obj-$(CONFIG_USB_ULPI_VIEWPORT)		+= phy-ulpi-viewport.o
obj-$(CONFIG_KEYSTONE_USB_PHY)		+= phy-keystone.o
