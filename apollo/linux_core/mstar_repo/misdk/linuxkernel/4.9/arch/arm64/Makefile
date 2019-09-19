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

LDFLAGS_vmlinux	:=-p --no-undefined -X
CPPFLAGS_vmlinux.lds = -DTEXT_OFFSET=$(TEXT_OFFSET)
GZFLAGS		:=-9

LIBGCC 		:= $(shell $(CC) $(KBUILD_CFLAGS) -print-libgcc-file-name)

ifeq ($(CONFIG_RELOCATABLE), y)
# Pass --no-apply-dynamic-relocs to restore pre-binutils-2.27 behaviour
# for relative relocs, since this leads to better Image compression
# with the relocation offsets always being zero.
LDFLAGS_vmlinux		+= -pie -shared -Bsymbolic \
			$(call ld-option, --no-apply-dynamic-relocs)
endif

ifeq ($(CONFIG_ARM64_ERRATUM_843419),y)
  ifeq ($(call ld-option, --fix-cortex-a53-843419),)
$(warning ld does not support --fix-cortex-a53-843419; kernel may be susceptible to erratum)
  else
    ifeq ($(call gold-ifversion, -lt, 114000000, y), y)
$(warning This version of GNU gold may generate incorrect code with --fix-cortex-a53-843419;\
	see https://sourceware.org/bugzilla/show_bug.cgi?id=21491)
    endif
LDFLAGS_vmlinux	+= --fix-cortex-a53-843419
  endif
else
  ifeq ($(ld-name),gold)
# Pass --no-fix-cortex-a53-843419 to ensure the erratum fix is disabled
LDFLAGS	+= --no-fix-cortex-a53-843419
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

ifeq ($(cc-name),clang)
# This is a workaround for https://bugs.llvm.org/show_bug.cgi?id=30792.
# TODO: revert when this is fixed in LLVM.
KBUILD_CFLAGS	+= -mno-implicit-float
else
KBUILD_CFLAGS	+= -mgeneral-regs-only
endif
KBUILD_CFLAGS	+= $(lseinstr)
KBUILD_CFLAGS	+= -fno-asynchronous-unwind-tables
KBUILD_CFLAGS	+= $(call cc-option, -mpc-relative-literal-loads)
KBUILD_CFLAGS	+= -fno-pic
KBUILD_AFLAGS	+= $(lseinstr)

ifeq ($(CONFIG_CPU_BIG_ENDIAN), y)
KBUILD_CPPFLAGS	+= -mbig-endian
AS		+= -EB
LD		+= -EB
UTS_MACHINE	:= aarch64_be
else
machine-$(CONFIG_MSTAR_MOONEY)       += ../../../../mstar2/hal/mooney/cpu/arm64
machine-$(CONFIG_MSTAR_MANHATTAN)   += ../../../../mstar2/hal/manhattan/cpu/arm64
machine-$(CONFIG_MSTAR_MASERATI)    += ../../../../mstar2/hal/maserati/cpu/arm64
machine-$(CONFIG_MSTAR_MAXIM)    += ../../../../mstar2/hal/maxim/cpu/arm64
machine-$(CONFIG_MSTAR_MAINZ)    += ../../../../mstar2/hal/mainz/cpu/arm64
machine-$(CONFIG_MSTAR_C2P)    += ../../../../mstar2/hal/c2p/cpu/arm64
machine-$(CONFIG_MSTAR_M5621)   += ../../../../mstar2/hal/m5621/cpu/arm64
machine-$(CONFIG_MSTAR_M7621)   += ../../../../mstar2/hal/m7621/cpu/arm64
machine-$(CONFIG_MSTAR_M7221)   += ../../../../mstar2/hal/m7221/cpu/arm64
machine-$(CONFIG_MSTAR_M7622)   += ../../../../mstar2/hal/m7622/cpu/arm64
machine-$(CONFIG_MSTAR_M7322)   += ../../../../mstar2/hal/m7322/cpu/arm64
machine-$(CONFIG_MSTAR_K7U)   += ../../../../mstar2/hal/k7u/cpu/arm64
machine-$(CONFIG_MSTAR_K6)   += ../../../../mstar2/hal/k6/cpu/arm64

ifeq ($(CONFIG_MP_MSTAR_STR_BASE),y)
mstar_chip = $(subst ",,$(CONFIG_MSTAR_CHIP_NAME))
ifneq ($(wildcard drivers/mstar2/hal/$(mstar_chip)/cpu/arm64/mstar_strmisc_chip.h),)
KBUILD_CPPFLAGS += -DMSTAR_STRMISC_CHIP
endif
endif

KBUILD_CPPFLAGS += -Iarch/arm64/include
KBUILD_CPPFLAGS += -Idrivers/mstar2

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
AS		+= -EL
LD		+= -EL
UTS_MACHINE	:= aarch64
else
KBUILD_CPPFLAGS	+= -mlittle-endian -Wl,-fix-cortex-a53-843419
AS		+= -EL -fix-cortex-a53-843419
LD		+= -EL -fix-cortex-a53-843419
UTS_MACHINE	:= aarch64
endif

endif

CHECKFLAGS	+= -D__aarch64__

ifeq ($(CONFIG_ARM64_MODULE_CMODEL_LARGE), y)
KBUILD_CFLAGS_MODULE	+= -mcmodel=large
ifeq ($(CONFIG_LTO_CLANG), y)
# Code model is not stored in LLVM IR, so we need to pass it also to LLVMgold
LDFLAGS		+= -plugin-opt=-code-model=large
endif
endif

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

ifeq ($(cc-name),clang)
KBUILD_CFLAGS += $(call cc-disable-warning, asm-operand-widths)
endif

# KASAN_SHADOW_OFFSET = VA_START + (1 << (VA_BITS - 3)) - (1 << 61)
# in 32-bit arithmetic
KASAN_SHADOW_OFFSET := $(shell printf "0x%08x00000000\n" $$(( \
			(0xffffffff & (-1 << ($(CONFIG_ARM64_VA_BITS) - 32))) \
			+ (1 << ($(CONFIG_ARM64_VA_BITS) - 32 - 3)) \
			- (1 << (64 - 32 - 3)) )) )

export	TEXT_OFFSET GZFLAGS

core-y		+= arch/arm64/kernel/ arch/arm64/mm/
drivers-$(CONFIG_OPROFILE)      += arch/arm64/oprofile/
core-$(CONFIG_NET) += arch/arm64/net/
core-$(CONFIG_KVM) += arch/arm64/kvm/
core-$(CONFIG_XEN) += arch/arm64/xen/
core-$(CONFIG_CRYPTO) += arch/arm64/crypto/
libs-y		:= arch/arm64/lib/ $(libs-y)
libs-y		+= $(LIBGCC)
core-$(CONFIG_EFI_STUB) += $(objtree)/drivers/firmware/efi/libstub/lib.a

# Default target when executing plain make
ifeq ($(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE),y)
KBUILD_IMAGE	:= $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_KERNEL_IMAGE_NAME))
else
KBUILD_IMAGE	:= Image.gz
endif

KBUILD_DTBS	:= dtbs

all:	$(KBUILD_IMAGE) $(KBUILD_DTBS)

boot := arch/arm64/boot

# FIXME: copied from arch/arm/Makefile
archprepare:
	$(Q)$(MAKE) $(build)=arch/arm/tools include/generated/mach-types.h

ifeq ($(CONFIG_MSTAR_ARM),y)
Image Image.gz uImage zuImage uImage-dtb: vmlinux
else
Image Image.gz uImage zuImage: vmlinux
endif
#	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@
	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $(boot)/$@

zinstall install uinstall: vmlinux
#	$(Q)$(MAKE) $(build)=$(boot) $@
	$(Q)$(MAKE) $(build)=$(boot) MACHINE=$(MACHINE) $@

%.dtb %.dtbo %.main_dtb: scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts $(boot)/dts/$@

PHONY += dtbs dtbs_install

dtbs: prepare scripts
	$(Q)$(MAKE) $(build)=$(boot)/dts dtbs
ifeq ($(vendor_overlay),true)
	$(info +++vendor_overlay info (CONFIG_MSTAR_CHIP_NAME,PRODUCT_BRAND,TARGET_PRODUCT,MODEL_NAME)=($(CONFIG_MSTAR_CHIP_NAME),$(PRODUCT_BRAND),$(TARGET_PRODUCT),$(MODEL_NAME)))
ifeq ($(T22_PCB_V20C),true)
	$(info *vendor_overlay info use T22_PCB_V20C)
	@mv $(boot)/dts/$(CONFIG_MSTAR_CHIP_NAME)_T22_PCB_V20C_overlay.dtbo $(boot)/dts/$(CONFIG_MSTAR_CHIP_NAME)_overlay_avb28.dtbo
else
ifeq ($(PRODUCT_BRAND),)
	$(info *vendor_overlay info PRODUCT_BRAND is empty)
else
ifneq ($(PRODUCT_BRAND),MediaTek)
	$(info ---vendor_overlay info not MediaTek (CONFIG_MSTAR_CHIP_NAME,PRODUCT_BRAND,TARGET_PRODUCT)=($(CONFIG_MSTAR_CHIP_NAME),$(PRODUCT_BRAND),$(TARGET_PRODUCT)))
	@mv $(boot)/dts/$(CONFIG_MSTAR_CHIP_NAME)_$(PRODUCT_BRAND)_$(MODEL_NAME)_overlay.dtbo $(boot)/dts/$(CONFIG_MSTAR_CHIP_NAME)_overlay_avb28.dtbo
endif
endif
endif
else
	$(info +++vendor info old flow)
ifneq ($(PRODUCT_BRAND),)
ifneq ($(PRODUCT_BRAND),MediaTek)
	$(info +++vendor info (CONFIG_MSTAR_CHIP_NAME,PRODUCT_BRAND,TARGET_PRODUCT)=($(CONFIG_MSTAR_CHIP_NAME),$(PRODUCT_BRAND),$(TARGET_PRODUCT)))
	@mv $(boot)/dts/$(CONFIG_MSTAR_CHIP_NAME)_$(PRODUCT_BRAND)_$(MODEL_NAME).main_dtb $(boot)/dts/$(CONFIG_MSTAR_CHIP_NAME)_an.main_dtb
endif
endif

ifeq ($(T22_PCB_V20C),true)
	$(info +++vendor info T22_V20C_PCB (CONFIG_MSTAR_CHIP_NAME,PRODUCT_BRAND,TARGET_PRODUCT)=($(CONFIG_MSTAR_CHIP_NAME),$(PRODUCT_BRAND),$(TARGET_PRODUCT)))
	@mv $(boot)/dts/$(CONFIG_MSTAR_CHIP_NAME)_an_T22_V20C_PCB.main_dtb $(boot)/dts/$(CONFIG_MSTAR_CHIP_NAME)_an.main_dtb
endif
endif

dtbs_install:
	$(Q)$(MAKE) $(dtbinst)=$(boot)/dts

Image-dtb Image.gz-dtb: vmlinux scripts dtbs
	$(Q)$(MAKE) $(build)=$(boot) $(boot)/$@

PHONY += vdso_install
vdso_install:
	$(Q)$(MAKE) $(build)=arch/arm64/kernel/vdso $@

# We use MRPROPER_FILES and CLEAN_FILES now
archclean:
	$(Q)$(MAKE) $(clean)=$(boot)
	$(Q)$(MAKE) $(clean)=$(boot)/dts

# We need to generate vdso-offsets.h before compiling certain files in kernel/.
# In order to do that, we should use the archprepare target, but we can't since
# asm-offsets.h is included in some files used to generate vdso-offsets.h, and
# asm-offsets.h is built in prepare0, for which archprepare is a dependency.
# Therefore we need to generate the header after prepare0 has been made, hence
# this hack.
prepare: vdso_prepare
vdso_prepare: prepare0
	$(Q)$(MAKE) $(build)=arch/arm64/kernel/vdso include/generated/vdso-offsets.h

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
