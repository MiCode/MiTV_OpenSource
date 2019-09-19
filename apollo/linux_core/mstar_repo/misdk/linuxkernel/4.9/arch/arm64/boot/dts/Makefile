dtb-$(CONFIG_ARCH_VEXPRESS) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_ramdisk).dtb
dtb-$(CONFIG_ARCH_VEXPRESS) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an).dtb

ifeq ($(vendor_overlay),true)
ifeq ($(CONFIG_MSTAR_CHIP_NAME), "m7322")
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an_avb).dtb
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an).main_dtb
ifeq ($(T22_PCB_V20C),true)
$(info *vendor_overlay info use T22_PCB_V20C)
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_T22_PCB_V20C_overlay).dtbo
else
ifeq ($(PRODUCT_BRAND),)
$(info *vendor_overlay info PRODUCT_BRAND is empty)
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_28).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_avb28).dtbo
else
$(info ---vendor_overlay info (CONFIG_MSTAR_CHIP_NAME,PRODUCT_BRAND,TARGET_PRODUCT)=($(CONFIG_MSTAR_CHIP_NAME),$(PRODUCT_BRAND),$(TARGET_PRODUCT)))
ifeq ($(PRODUCT_BRAND),MediaTek)
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_28).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_avb28).dtbo
else
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_$(PRODUCT_BRAND)_$(MODEL_NAME)_overlay).dtbo
endif
endif
endif
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_1).dtbo
endif
ifeq ($(CONFIG_MSTAR_CHIP_NAME), "m5621")
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an_avb).dtb
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an).main_dtb
ifeq ($(PRODUCT_BRAND),)
$(info *vendor_overlay info PRODUCT_BRAND is empty)
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_28).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_avb28).dtbo
else
$(info ---vendor_overlay info (CONFIG_MSTAR_CHIP_NAME,PRODUCT_BRAND,TARGET_PRODUCT)=($(CONFIG_MSTAR_CHIP_NAME),$(PRODUCT_BRAND),$(TARGET_PRODUCT)))
ifeq ($(PRODUCT_BRAND),MediaTek)
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_28).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_avb28).dtbo
else
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_$(PRODUCT_BRAND)_$(MODEL_NAME)_overlay).dtbo
endif
endif
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_1).dtbo
endif
else
ifeq ($(CONFIG_MSTAR_CHIP_NAME), "m7322")
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an_avb).dtb
ifeq ($(PRODUCT_BRAND),)
$(info *vendor info PRODUCT_BRAND is empty)
ifeq ($(T22_PCB_V20C),true)
$(info *vendor info use T22_V20C_PCB)
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an_T22_V20C_PCB).main_dtb
else
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an).main_dtb
endif
else
$(info ---vendor info (CONFIG_MSTAR_CHIP_NAME,PRODUCT_BRAND,TARGET_PRODUCT)=($(CONFIG_MSTAR_CHIP_NAME),$(PRODUCT_BRAND),$(TARGET_PRODUCT)))
ifeq ($(PRODUCT_BRAND),MediaTek)
ifeq ($(T22_PCB_V20C),true)
$(info ---vendor info use T22_V20C_PCB)
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an_T22_V20C_PCB).main_dtb
else
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an).main_dtb
endif
else
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_$(PRODUCT_BRAND)_$(MODEL_NAME)).main_dtb
endif
endif
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_1).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_avb).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_dto).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_28).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_avb28).dtbo
endif

ifeq ($(CONFIG_MSTAR_CHIP_NAME), "m5621")
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an_avb).dtb
ifeq ($(PRODUCT_BRAND),)
$(info *vendor info PRODUCT_BRAND is empty)
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an).main_dtb
else
$(info ---vendor info (CONFIG_MSTAR_CHIP_NAME,PRODUCT_BRAND,TARGET_PRODUCT)=($(CONFIG_MSTAR_CHIP_NAME),$(PRODUCT_BRAND),$(TARGET_PRODUCT)))
ifeq ($(PRODUCT_BRAND),MediaTek)
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_an).main_dtb
else
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_$(PRODUCT_BRAND)_$(MODEL_NAME)).main_dtb
endif
endif
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_1).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_avb).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_dto).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_28).dtbo
dtb-$(CONFIG_OF) += $(subst ",,$(CONFIG_MSTAR_CHIP_NAME)_overlay_avb28).dtbo
endif
endif
targets += dtbs

DTB_NAMES := $(subst $\",,$(CONFIG_BUILD_ARM64_APPENDED_DTB_IMAGE_NAMES))
ifneq ($(DTB_NAMES),)
DTB_LIST := $(addsuffix .dtb,$(DTB_NAMES))
else
DTB_LIST := $(dtb-y)
endif
targets += $(DTB_LIST)

dtbs: $(addprefix $(obj)/, $(DTB_LIST))

clean-files := dts/*.dtb *.dtb dts/*.dtbo *.dtbo dts/*.main_dtb *.main_dtb
