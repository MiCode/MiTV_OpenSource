#Add for Advanced OProfile

obj-$(CONFIG_ADVANCE_OPROFILE) += aop_oprofile.o
obj-$(CONFIG_ADVANCE_OPROFILE) += aop_report.o
obj-$(CONFIG_ADVANCE_OPROFILE) += aop_kernel.o
obj-$(CONFIG_ADVANCE_OPROFILE) += aop_report_symbol.o

ifeq ($(CONFIG_MEM_DBG),y)
EXTRA_CFLAGS := -DKDBG_MEM_DBG
endif
