#Add of Architecture ARM
ifeq ($(CONFIG_MEM_DBG),y)
EXTRA_CFLAGS += -DKDBG_MEM_DBG
ifeq ($(CONFIG_MEM_CHK),y)
EXTRA_CFLAGS += -DKDBG_MEM_CHK
endif
endif
obj-$(CONFIG_KDEBUGD_TRACE)  += kdbg-trace.o
obj-$(CONFIG_KDEBUGD)  += kdbg-misc.o
