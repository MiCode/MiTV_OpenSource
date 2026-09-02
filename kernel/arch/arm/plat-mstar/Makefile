
EXTRA_CFLAGS      +=  -Idrivers/mstar2/include
EXTRA_CFLAGS      +=  -Iarch/arm/mach-$(CONFIG_MSTAR_CHIP_NAME)
EXTRA_CFLAGS      +=  -Iarch/arm/mach-$(CONFIG_MSTAR_CHIP_NAME)/board

obj-y	:= memory.o
obj-$(CONFIG_PLAT_VERSATILE_CLCD) += clcd.o
obj-$(CONFIG_PLAT_VERSATILE_FPGA_IRQ) += fpga-irq.o
obj-$(CONFIG_PLAT_VERSATILE_LEDS) += leds.o
obj-$(CONFIG_PLAT_VERSATILE_SCHED_CLOCK) += sched-clock.o
obj-$(CONFIG_SMP) += headsmp.o platsmp.o
obj-$(CONFIG_HOTPLUG_CPU) += hotplug.o
