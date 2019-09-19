#
# arch/arm64/boot/Makefile
#
# This file is included by the global makefile so that you can add your own
# architecture-specific flags and dependencies.
#
# This file is subject to the terms and conditions of the GNU General Public
# License.  See the file "COPYING" in the main directory of this archive
# for more details.
#
# Copyright (C) 2012, ARM Ltd.
# Author: Will Deacon <will.deacon@arm.com>
#
# Based on the ia64 boot/Makefile.
#

include $(srctree)/arch/arm64/boot/dts/Makefile
ifneq ($(MACHINE),)
include $(srctree)/$(MACHINE)/Makefile.boot
endif

ZRELADDR    := $(zreladdr-y)

export ZRELADDR

OBJCOPYFLAGS_Image :=-O binary -R .note -R .note.gnu.build-id -R .comment -S

targets := Image Image.gz uImage

DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif
DTB_OBJS := $(addprefix $(obj)/dts/,$(DTB_LIST))

ifeq ($(CONFIG_MSTAR_ARM),y)
ifeq ($(vendor_overlay),true)
$(info =+=vendor info use _an.main_dts)
MAIN_DTB_OBJS := $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an).main_dtb
else
ifeq ($(PRODUCT_BRAND),)
$(info =+=vendor info PRODUCT_BRAND is empty)
ifeq ($(T22_PCB_V20C),true)
$(info =+=vendor info _an_T22_V20C_PCB is empty)
MAIN_DTB_OBJS := $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an_T22_V20C_PCB).main_dtb
else
MAIN_DTB_OBJS := $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an).main_dtb
endif
else
$(info =-=vendor info (CONFIG_MSTAR_CHIP_NAME,PRODUCT_BRAND,TARGET_PRODUCT)=($(CONFIG_MSTAR_CHIP_NAME),$(PRODUCT_BRAND),$(TARGET_PRODUCT)))
$(info =-=vendor info (MODEL_NAME)=($(MODEL_NAME)))
ifeq ($(PRODUCT_BRAND), MediaTek)
ifeq ($(T22_PCB_V20C),true)
$(info =-=vendor info _an_T22_V20C_PCB is empty)
MAIN_DTB_OBJS := $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an_T22_V20C_PCB).main_dtb
else
MAIN_DTB_OBJS := $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an).main_dtb
endif
else
MAIN_DTB_OBJS := $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_$(PRODUCT_BRAND)_$(MODEL_NAME)).main_dtb
endif
endif
endif
MAIN_DTB := $(addprefix $(obj)/dts/,$(MAIN_DTB_OBJS))
endif

$(obj)/Image: vmlinux FORCE
	$(call if_changed,objcopy)
	@echo '  Image $@ is ready'

$(obj)/Image.bz2: $(obj)/Image FORCE
	$(call if_changed,bzip2)

$(obj)/Image-dtb: $(obj)/Image $(DTB_OBJS) FORCE
	$(call if_changed,cat)

$(obj)/Image.gz: $(obj)/Image FORCE
	$(call if_changed,gzip)
	@echo '  Image $@ is ready'

$(obj)/Image.lz4: $(obj)/Image FORCE
	$(call if_changed,lz4)

$(obj)/Image.lzma: $(obj)/Image FORCE
	$(call if_changed,lzma)

$(obj)/Image.lzo: $(obj)/Image FORCE
	$(call if_changed,lzo)

$(obj)/Image.gz-dtb: $(obj)/Image.gz $(DTB_OBJS) FORCE
	$(call if_changed,cat)

ifneq ($(LOADADDR),)
  UIMAGE_LOADADDR=$(LOADADDR)
else
  ifeq ($(CONFIG_ZBOOT_ROM),y)
    UIMAGE_LOADADDR=$(CONFIG_ZBOOT_ROM_TEXT)
  else
    UIMAGE_LOADADDR=$(ZRELADDR)
  endif
endif

$(obj)/uImage: $(obj)/Image FORCE
	$(call if_changed,uimage)
	@echo '  Image $@ is ready'

ifeq ($(CONFIG_MSTAR_ARM),y)
$(obj)/uImage-dtb: $(obj)/uImage $(MAIN_DTB) FORCE
	$(call if_changed,cat)
	@echo ' uImage $@ is ready'
endif

quiet_cmd_zuimage = ZUIMAGE $@
      cmd_zuimage = $(CONFIG_SHELL) $(MKIMAGE) -A arm -O linux -T kernel \
	                -C gzip -a $(UIMAGE_LOADADDR) -e $(UIMAGE_LOADADDR) \
					-n 'Linux-$(KERNELRELEASE)' -d $< $@

$(obj)/zuImage: $(obj)/Image.gz FORCE
	$(call if_changed,zuimage)
	@echo '  zuImage $@ is ready'


install: $(obj)/Image
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/Image System.map "$(INSTALL_PATH)"

zinstall:
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/Image.gz System.map "$(INSTALL_PATH)"

uinstall: $(obj)/uImage
	$(CONFIG_SHELL) $(srctree)/$(src)/install.sh $(KERNELRELEASE) \
	$(obj)/uImage System.map "$(INSTALL_PATH)"
