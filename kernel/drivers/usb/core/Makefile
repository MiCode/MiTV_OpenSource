# SPDX-License-Identifier: GPL-2.0
#
# Makefile for USB Core files and filesystem
#

# mstar_porting_plat >>>
ifeq ($(CONFIG_ONEHAL_ARCH_SUPPORT),y)
ONEHAL_VERSION = $(subst \",,$(CONFIG_ONEHAL_VERSION))
ONEHAL_DIR = ver_$(ONEHAL_VERSION)
EXTRA_CFLAGS      += -Idrivers/mstar2/hal/$(CONFIG_MSTAR_CHIP_NAME)/usb/$(ONEHAL_DIR)
endif
EXTRA_CFLAGS      += -Idrivers/mstar2/hal/$(CONFIG_MSTAR_CHIP_NAME)/usb
# mstar_porting_plat <<<

usbcore-y := usb.o hub.o hcd.o urb.o message.o driver.o
usbcore-y += config.o file.o buffer.o sysfs.o endpoint.o
usbcore-y += devio.o notify.o generic.o quirks.o devices.o
usbcore-y += phy.o port.o
# mstar_porting_plat >>>
usbcore-$(CONFIG_MP_USB_MSTAR) += mstar-lib.o
# mstar_porting_plat <<<

usbcore-$(CONFIG_OF)		+= of.o
usbcore-$(CONFIG_USB_PCI)		+= hcd-pci.o
usbcore-$(CONFIG_ACPI)		+= usb-acpi.o

obj-$(CONFIG_USB)		+= usbcore.o

obj-$(CONFIG_USB_LEDS_TRIGGER_USBPORT)	+= ledtrig-usbport.o
