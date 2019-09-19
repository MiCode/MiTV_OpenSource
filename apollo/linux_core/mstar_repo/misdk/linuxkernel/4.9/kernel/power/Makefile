
ccflags-$(CONFIG_PM_DEBUG)	:= -DDEBUG

CONFIG_MSTAR_CHIP_NAME := $(subst ",,$(CONFIG_MSTAR_CHIP_NAME))

KASAN_SANITIZE_snapshot.o	:= n
ifeq ($(CONFIG_MP_CMA_PATCH_MBOOT_STR_USE_CMA),y)
EXTRA_CFLAGS      +=  -Idrivers/mstar2/drv/cma_pool
EXTRA_CFLAGS      +=  -Idrivers/mstar2/include
endif

ifeq ($(CONFIG_MSTAR_PM),y)
EXTRA_CFLAGS      +=  -Idrivers/mstar2/include
endif

ifeq ($(CONFIG_MSTAR_FRC_R2),y)
EXTRA_CFLAGS      +=  -Idrivers/mstar2/include
endif

ifeq ($(CONFIG_MSTAR_IPAPOOL),y)
EXTRA_CFLAGS      +=  -Idrivers/mstar2/include
endif

EXTRA_CFLAGS      +=  -Idrivers/mstar2/include/sys/common
EXTRA_CFLAGS      +=  -Idrivers/mstar2/drv/miu
EXTRA_CFLAGS      +=  -Idrivers/mstar2/hal/$(CONFIG_MSTAR_CHIP_NAME)/miu

obj-y				+= qos.o
obj-$(CONFIG_PM)		+= main.o
obj-$(CONFIG_VT_CONSOLE_SLEEP)	+= console.o
obj-$(CONFIG_FREEZER)		+= process.o
obj-$(CONFIG_SUSPEND)		+= suspend.o
obj-$(CONFIG_PM_TEST_SUSPEND)	+= suspend_test.o
obj-$(CONFIG_HIBERNATION)	+= hibernate.o snapshot.o swap.o user.o
obj-$(CONFIG_PM_AUTOSLEEP)	+= autosleep.o
obj-$(CONFIG_PM_WAKELOCKS)	+= wakelock.o

obj-$(CONFIG_MAGIC_SYSRQ)	+= poweroff.o

obj-$(CONFIG_SUSPEND)	+= wakeup_reason.o
obj-$(CONFIG_MP_MSTAR_STR_BASE) +=mstar_strmisc.o

obj-$(CONFIG_MSTAR_FASTBOOT) += mstar_fastboot.o
