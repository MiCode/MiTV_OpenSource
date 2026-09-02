# SPDX-License-Identifier: GPL-2.0
#
# Makefile for the input core drivers.
#

# Each configuration option enables a list of files.

obj-$(CONFIG_INPUT)		+= input-core.o
input-core-y := input.o input-compat.o input-mt.o ff-core.o

obj-$(CONFIG_INPUT_FF_MEMLESS)	+= ff-memless.o
obj-$(CONFIG_INPUT_POLLDEV)	+= input-polldev.o
obj-$(CONFIG_INPUT_SPARSEKMAP)	+= sparse-keymap.o
obj-$(CONFIG_INPUT_MATRIXKMAP)	+= matrix-keymap.o

obj-$(CONFIG_INPUT_LEDS)	+= input-leds.o
obj-$(CONFIG_INPUT_MOUSEDEV)	+= mousedev.o
obj-$(CONFIG_INPUT_JOYDEV)	+= joydev.o
obj-$(CONFIG_INPUT_EVDEV)	+= evdev.o
obj-$(CONFIG_INPUT_EVBUG)	+= evbug.o
ifdef CONFIG_MSTAR_CHIP
obj-$(CONFIG_EVDEV_MISC)	+= evdev_misc.o
endif

obj-$(CONFIG_INPUT_KEYBOARD)	+= keyboard/
obj-$(CONFIG_INPUT_MOUSE)	+= mouse/
obj-$(CONFIG_INPUT_JOYSTICK)	+= joystick/
obj-$(CONFIG_INPUT_TABLET)	+= tablet/
obj-$(CONFIG_INPUT_TOUCHSCREEN)	+= touchscreen/
obj-$(CONFIG_INPUT_MISC)	+= misc/

obj-$(CONFIG_INPUT_APMPOWER)	+= apm-power.o

obj-$(CONFIG_RMI4_CORE)		+= rmi4/
ifdef CONFIG_MSTAR_DVFS
EXTRA_CFLAGS += -Idrivers/mstar2/drv/dvfs/include
EXTRA_CFLAGS += -Idrivers/mstar2/include
EXTRA_CFLAGS += -Idrivers/mstar2/hal/$(CONFIG_MSTAR_CHIP_NAME)/dvfs
endif

ifdef CONFIG_ARM_MTKTV_CPUFREQ
EXTRA_CFLAGS += -Idrivers/mstar2/drv/cpufreq/include
endif

ifdef CONFIG_MSTAR_CHIP
EXTRA_CFLAGS += -Idrivers/mstar2/drv/ir_mirc/ir_dynamic_config
endif
ifdef CONFIG_MP_QOS
EXTRA_CFLAGS += -Idrivers/mstar2/include
endif
