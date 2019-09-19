#Add for KDEBUGD

EXTRA_CFLAGS += -Ikernel/kdebugd/include/kdebugd
EXTRA_CFLAGS      +=  -Ifs/proc

obj-$(CONFIG_KDEBUGD_LIFE_TEST)   += kdbg_key_test_player.o
obj-$(CONFIG_KDEBUGD)   += kdbg-core.o
obj-$(CONFIG_KDEBUGD)   += kdbg-func.o
obj-$(CONFIG_KDEBUGD) += sec_workq.o
ifeq ($(ARCH), arm)
    EXTRA_CFLAGS += -Ikernel/kdebugd/arch/arm/
    obj-y   += arch/arm/
else
ifeq ($(ARCH), arm64)
    EXTRA_CFLAGS += -Ikernel/kdebugd/arch/arm/
    obj-y   += arch/arm/
endif # ifeq ($(ARCH), arm64)
ifeq ($(ARCH), mips)
    EXTRA_CFLAGS += -Ikernel/kdebugd/arch/mips/
    obj-y   += arch/mips/
endif # ifeq ($(ARCH), mips)
endif #ifeq ($(ARCH), arm)

obj-$(CONFIG_KDEBUGD_TRACE)  += kdbg-trace.o
obj-$(CONFIG_KDEBUGD_COUNTER_MONITOR) += sec_usage_core.o
obj-$(CONFIG_KDEBUGD_COUNTER_MONITOR) += sec_cpuusage.o
obj-$(CONFIG_KDEBUGD_COUNTER_MONITOR) += sec_topthread.o
obj-$(CONFIG_KDEBUGD_COUNTER_MONITOR) += sec_memusage.o
obj-$(CONFIG_KDEBUGD_COUNTER_MONITOR) += sec_diskusage.o
obj-$(CONFIG_KDEBUGD_COUNTER_MONITOR) += sec_netusage.o
obj-$(CONFIG_KDEBUGD_PMU_EVENTS_COUNTERS) += sec_perfcounters.o
obj-$(CONFIG_KDEBUGD) += kdbg_util.o
obj-$(CONFIG_KDEBUGD_FUTEX) += kdbg-futex.o
obj-$(CONFIG_KDEBUGD_FTRACE) += trace/kdbg_ftrace_menu.o
obj-$(CONFIG_FTRACE_BENCHMARK) += trace/kdbg_ftrace_benchmark.o

ifeq ($(CONFIG_MEM_DBG),y)
EXTRA_CFLAGS += -DKDBG_MEM_DBG
ifeq ($(CONFIG_MEM_CHK),y)
EXTRA_CFLAGS += -DKDBG_MEM_CHK
endif
obj-$(CONFIG_MEM_DBG) += kdbg_util_mem.o
endif

#Add for ELF
obj-$(CONFIG_ELF_MODULE)   += elf/
#Add for AOP
obj-$(CONFIG_ADVANCE_OPROFILE)   += aop/
