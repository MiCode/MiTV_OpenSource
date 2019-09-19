obj-$(CONFIG_ELF_MODULE) += kdbg_elf_parser.o
obj-$(CONFIG_ELF_MODULE) += kdbg_elf_read_dir.o
obj-$(CONFIG_ELF_MODULE) += kdbg_elf_sym_func.o
obj-$(CONFIG_DWARF_MODULE) += kdbg_elf_addr2line.o
obj-$(CONFIG_DWARF_MODULE) += kdbg_elf_dwarf.o
#Add for ELF
obj-$(CONFIG_ELF_MODULE)   += dem_src/

ifeq ($(CONFIG_MEM_DBG),y)
	EXTRA_CFLAGS := -DKDBG_MEM_DBG
endif
