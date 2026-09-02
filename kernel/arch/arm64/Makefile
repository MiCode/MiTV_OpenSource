#
# arch/arm64/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 1995-2001 by Russell King

LDFLAGS_vmlinux	:=--no-undefined -X
CPPFLAGS_vmlinux.lds = -DTEXT_OFFSET=$(TEXT_OFFSET)
GZFLAGS		:=-9

ifeq ($(CONFIG_RELOCATABLE), y)
# Pass --no-apply-dynamic-relocs to restore pre-binutils-2.27 behaviour
# for relative relocs, since this leads to better Image compression
# with the relocation offsets always being zero.
LDFLAGS_vmlinux		+= -shared -Bsymbolic -z notext -z norelro \
			$(call ld-option, --no-apply-dynamic-relocs)

LIBGCC 		:= $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)
endif

ifeq ($(CONFIG_ARM64_ERRATUM_843419),y)
  ifeq ($(call ld-option, --fix-cortex-a53-843419),)
$(warning ld does not support --fix-cortex-a53-843419; kernel may be susceptible to erratum)
  else
LDFLAGS_vmlinux	+= --fix-cortex-a53-843419
  endif
endif

KBUILD_DEFCONFIG := defconfig

# Check for binutils support for specific extensions
lseinstr := $(call as-instr,.arch_extension lse,-DCONFIG_AS_LSE=1)

ifeq ($(CONFIG_ARM64_LSE_ATOMICS), y)
  ifeq ($(lseinstr),)
$(warning LSE atomics not supported by binutils)
  endif
endif

ifeq ($(CONFIG_ARM64), y)
brokengasinst := $(call as-instr,1:\n.inst 0\n.rept . - 1b\n\nnop\n.endr\n,,-DCONFIG_BROKEN_GAS_INST=1)

  ifneq ($(brokengasinst),)
$(warning Detected assembler with broken .inst; disassembly will be unreliable)
  endif
endif

KBUILD_CFLAGS	+= -mgeneral-regs-only $(lseinstr) $(brokengasinst)
KBUILD_CFLAGS	+= -fno-asynchronous-unwind-tables
KBUILD_CFLAGS	+= $(call cc-disable-warning, psabi)
KBUILD_AFLAGS	+= $(lseinstr) $(brokengasinst)

KBUILD_CFLAGS	+= $(call cc-option,-mabi=lp64)
KBUILD_AFLAGS	+= $(call cc-option,-mabi=lp64)

ifeq ($(CONFIG_SHADOW_CALL_STACK), y)
KBUILD_CFLAGS	+= -ffixed-x18
endif

ifeq ($(CONFIG_CPU_BIG_ENDIAN), y)
KBUILD_CPPFLAGS	+= -mbig-endian
CHECKFLAGS	+= -D__AARCH64EB__
AS		+= -EB
# Prefer the baremetal ELF build target, but not all toolchains include
# it so fall back to the standard linux version if needed.
KBUILD_LDFLAGS	+= -EB $(call ld-option, -maarch64elfb, -maarch64linuxb)
UTS_MACHINE	:= aarch64_be
else
machine-$(CONFIG_MSTAR_MOONEY)		+= ../../../../mstar2/hal/mooney/cpu/arm64
machine-$(CONFIG_MSTAR_MANHATTAN)	+= ../../../../mstar2/hal/manhattan/cpu/arm64
machine-$(CONFIG_MSTAR_MASERATI)	+= ../../../../mstar2/hal/maserati/cpu/arm64
machine-$(CONFIG_MSTAR_MAXIM)		+= ../../../../mstar2/hal/maxim/cpu/arm64
machine-$(CONFIG_MSTAR_MAINZ)		+= ../../../../mstar2/hal/mainz/cpu/arm64
machine-$(CONFIG_MSTAR_M5621)		+= ../../../../mstar2/hal/m5621/cpu/arm64
machine-$(CONFIG_MSTAR_M7621)		+= ../../../../mstar2/hal/m7621/cpu/arm64
machine-$(CONFIG_MSTAR_M7221)		+= ../../../../mstar2/hal/m7221/cpu/arm64
machine-$(CONFIG_MSTAR_M7322)		+= ../../../../mstar2/hal/m7322/cpu/arm64
machine-$(CONFIG_MSTAR_M7332)		+= ../../../../mstar2/hal/m7332/cpu/arm64
machine-$(CONFIG_MSTAR_M7622)		+= ../../../../mstar2/hal/m7622/cpu/arm64
machine-$(CONFIG_MSTAR_M7632)		+= ../../../../mstar2/hal/m7632/cpu/arm64
machine-$(CONFIG_MSTAR_M7642)		+= ../../../../mstar2/hal/m7642/cpu/arm64
machine-$(CONFIG_MSTAR_MT5862)		+= ../../../../mstar2/hal/mt5862/cpu/arm64
machine-$(CONFIG_MSTAR_MT5867)		+= ../../../../mstar2/hal/mt5867/cpu/arm64
machine-$(CONFIG_MSTAR_MT5889)		+= ../../../../mstar2/hal/mt5889/cpu/arm64
machine-$(CONFIG_MSTAR_MT5872)          += ../../../../mstar2/hal/mt5872/cpu/arm64

ifeq ($(CONFIG_MP_MSTAR_STR_BASE),y)
  mstar_chip = $(subst ",,$(CONFIG_MSTAR_CHIP_NAME))
  ifneq ($(wildcard drivers/mstar2/hal/$(mstar_chip)/cpu/arm64/mstar_strmisc_chip.h),)
    KBUILD_CPPFLAGS += -DMSTAR_STRMISC_CHIP
  endif
endif

KBUILD_CPPFLAGS += -Iarch/arm64/include
KBUILD_CPPFLAGS += -Idrivers/mstar2
ifeq ($(CONFIG_ONEHAL_ARCH_SUPPORT),y)
ONEHAL_VERSION= $(subst ",,$(CONFIG_ONEHAL_VERSION))
ONEHAL_DIR = ver_$(ONEHAL_VERSION)
KBUILD_CPPFLAGS	+=  -Idrivers/mstar2/hal/$(subst ",,$(CONFIG_MSTAR_CHIP_NAME))/cpu/arm64/$(ONEHAL_DIR)
KBUILD_CPPFLAGS	+=  -Idrivers/mstar2/hal/$(subst ",,$(CONFIG_MSTAR_CHIP_NAME))/miu/$(ONEHAL_DIR)
KBUILD_CPPFLAGS +=  -Idrivers/mstar2/hal/$(subst ",,$(CONFIG_MSTAR_CHIP_NAME))/gpio/$(ONEHAL_DIR)
endif


# Platform directory name.  This list is sorted alphanumerically
plat-$(CONFIG_PLAT_MSTAR)    += mstar

ifneq ($(CONFIG_MSTAR_CHIP),)
MACHINE  := arch/arm64/arm-boards/$(word 1,$(machine-y))/
machdirs := $(patsubst %,arch/arm64/arm-boards/%/,$(machine-y))
platdirs := $(patsubst %,arch/arm64/arm-boards/plat-%/,$(plat-y))
core-y += arch/arm64/arm-boards/generic/
core-y += $(machdirs)
#core-y += ../mstar2/hal/$(subst ",,$(CONFIG_MSTAR_CHIP_NAME))/cpu/arm64/

ifeq ($(KBUILD_SRC),)
KBUILD_CPPFLAGS += $(patsubst %,-I%/include,$(machdirs) $(platdirs))
KBUILD_CPPFLAGS += $(patsubst %,-I%,$(machdirs) $(platdirs))
else
KBUILD_CPPFLAGS += $(patsubst %,-I$(srctree)/%/include,$(machdirs) $(platdirs))
KBUILD_CPPFLAGS += $(patsubst %,-I$(srctree)/%,$(machdirs) $(platdirs))
endif
endif

ifeq ($(CONFIG_MSTAR_CHIP_NAME), $(filter $(CONFIG_MSTAR_CHIP_NAME), "kano" "maserati" "m7821" "m7322" "m7622"))
KBUILD_CPPFLAGS	+= -mlittle-endian
CHECKFLAGS	+= -D__AARCH64EL__
AS		+= -EL
# Same as above, prefer ELF but fall back to linux target if needed.
KBUILD_LDFLAGS	+= -EL $(call ld-option, -maarch64elf, -maarch64linux)
UTS_MACHINE	:= aarch64
else
KBUILD_CPPFLAGS	+= -mlittle-endian -Wl,-fix-cortex-a53-843419
AS		+= -EL -fix-cortex-a53-843419
UTS_MACHINE	:= aarch64
endif

endif

CHECKFLAGS	+= -D__aarch64__

ifeq ($(CONFIG_ARM64_MODULE_PLTS),y)
KBUILD_LDFLAGS_MODULE	+= -T $(srctree)/arch/arm64/kernel/module.lds
endif

# Default value
head-y		:= arch/arm64/kernel/head.o

# The byte offset of the kernel image in RAM from the start of RAM.
ifeq ($(CONFIG_ARM64_RANDOMIZE_TEXT_OFFSET), y)
TEXT_OFFSET := $(shell awk "BEGIN {srand(); printf \"0x%06x\n\", \
		 int(2 * 1024 * 1024 / (2 ^ $(CONFIG_ARM64_PAGE_SHIFT)) * \
		 rand()) * (2 ^ $(CONFIG_ARM64_PAGE_SHIFT))}")
else
TEXT_OFFSET := 0x00080000
endif

ifeq ($(CONFIG_KASAN_SW_TAGS), y)
KASAN_SHADOW_SCALE_SHIFT := 4
else
KASAN_SHADOW_SCALE_SHIFT := 3
endif

KBUILD_CFLAGS += -DKASAN_SHADOW_SCALE_SHIFT=$(KASAN_SHADOW_SCALE_SHIFT)
KBUILD_CPPFLAGS += -DKASAN_SHADOW_SCALE_SHIFT=$(KASAN_SHADOW_SCALE_SHIFT)
KBUILD_AFLAGS += -DKASAN_SHADOW_SCALE_SHIFT=$(KASAN_SHADOW_SCALE_SHIFT)

# KASAN_SHADOW_OFFSET = VA_START + (1 << (VA_BITS - KASAN_SHADOW_SCALE_SHIFT))
#				 - (1 << (64 - KASAN_SHADOW_SCALE_SHIFT))
# in 32-bit arithmetic
KASAN_SHADOW_OFFSET := $(shell printf "0x%08x00000000\n" $$(( \
	(0xffffffff & (-1 << ($(CONFIG_ARM64_VA_BITS) - 32))) \
	+ (1 << ($(CONFIG_ARM64_VA_BITS) - 32 - $(KASAN_SHADOW_SCALE_SHIFT))) \
	- (1 << (64 - 32 - $(KASAN_SHADOW_SCALE_SHIFT))) )) )

export	TEXT_OFFSET GZFLAGS

core-y		+= arch/arm64/kernel/ arch/arm64/mm/
core-$(CONFIG_NET) += arch/arm64/net/
core-$(CONFIG_KVM) += arch/arm64/kvm/
core-$(CONFIG_XEN) += arch/arm64/xen/
core-$(CONFIG_CRYPTO) += arch/arm64/crypto/
libs-y		:= arch/arm64/lib/ $(libs-y)
libs-y		+= $(LIBGCC)
core-$(CONFIG_EFI_STUB) += $(objtree)/drivers/firmware/efi/libstub/lib.a

# Default target when executing plain make
boot		:= arch/arm64/boot
ifeq ($(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE),y)
KBUILD_IMAGE	:= $(boot)/$(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_KERNEL_IMAGE_NAME))
else
KBUILD_IMAGE	:= $(boot)/Image.gz
endif
KBUILD_DTBS	:= dtbs

all:	Image.gz $(KBUILD_DTBS)


# FIXME: copied from arch/arm/Makefile
archprepare:
	$(Q)$(MAKE) $(build)=arch/arm/tools kapi
ifeq ($(CONFIG_MSTAR_ARM),y)
Image Image.gz uImage zuImage uImage-dtb: vmlinux
else
Image Image.gz uImage zuImage: vmlinux
endif
	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $(boot)/$@

zinstall install uinstall: vmlinux
	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $@

%.dtb %.dtbo %.main_dtb: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts $(boot)/dts/$@

ifeq ($(CONFIG_MSTAR_CHIP),y)
PHONY +=copy_all_dtb
copy_all_dtb:
	@$(foreach dir, $(wildcard $(boot)/dts/mediatek-dtv/*.dtb), $(shell cp $(dir) $(boot)/dts/))
	@$(foreach dir, $(wildcard $(boot)/dts/mediatek-dtv/*.dtbo), $(shell cp $(dir) $(boot)/dts/))
	@$(foreach dir, $(wildcard $(boot)/dts/mediatek-dtv/*.main_dtb), $(shell cp $(dir) $(boot)/dts/))
endif

PHONY += dtbs dtbs_install

dtbs: prepare scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts
ifneq ($(NCT5_BUILD),true)
ifeq ($(CONFIG_MSTAR_CHIP),y)
	@echo "Copy dtb to dts folder, temporary patch"
	@make copy_all_dtb
endif
endif

dtbs_install:
	$(Q)$(MAKE) $(dtbinst)=$(boot)/dts

PHONY += vdso_install
vdso_install:
	$(Q)$(MAKE) $(build)=arch/arm64/kernel/vdso $@

# We use MRPROPER_FILES and CLEAN_FILES now
archclean:
	$(Q)$(MAKE) $(clean)=$(boot)
	$(Q)$(MAKE) $(clean)=$(boot)/dts

ifeq ($(KBUILD_EXTMOD),)
# We need to generate vdso-offsets.h before compiling certain files in kernel/.
# In order to do that, we should use the archprepare target, but we can't since
# asm-offsets.h is included in some files used to generate vdso-offsets.h, and
# asm-offsets.h is built in prepare0, for which archprepare is a dependency.
# Therefore we need to generate the header after prepare0 has been made, hence
# this hack.
prepare: vdso_prepare
vdso_prepare: prepare0
	$(Q)$(MAKE) $(build)=arch/arm64/kernel/vdso include/generated/vdso-offsets.h
endif

define archhelp
  echo  '* Image.gz      - Compressed kernel image (arch/$(ARCH)/boot/Image.gz)'
  echo  '  Image         - Uncompressed kernel image (arch/$(ARCH)/boot/Image)'
  echo  '  uImage        - U-Boot wrapped Image'
  echo  '  zuImage       - U-Boot wrapped Image.gz'
  echo  '* dtbs          - Build device tree blobs for enabled boards'
  echo  '  dtbs_install  - Install dtbs to $(INSTALL_DTBS_PATH)'
  echo  '  install       - Install uncompressed kernel'
  echo  '  zinstall      - Install compressed kernel'
  echo  '  uinstall      - Install U-Boot wrapped uncompressed kernel'
  echo  '                  Install using (your) ~/bin/installkernel or'
  echo  '                  (distribution) /sbin/installkernel or'
  echo  '                  install to $$(INSTALL_PATH) and run lilo'
endef
