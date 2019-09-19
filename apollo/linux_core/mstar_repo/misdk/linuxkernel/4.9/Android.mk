#<MStar Software>
#******************************************************************************
# MStar Software
# Copyright (c) 2010 - 2018 MStar Semiconductor, Inc. All rights reserved.
# All software, firmware and related documentation herein ("MStar Software") are
# intellectual property of MStar Semiconductor, Inc. ("MStar") and protected by
# law, including, but not limited to, copyright law and international treaties.
# Any use, modification, reproduction, retransmission, or republication of all
# or part of MStar Software is expressly prohibited, unless prior written
# permission has been granted by MStar.
#
# By accessing, browsing and/or using MStar Software, you acknowledge that you
# have read, understood, and agree, to be bound by below terms ("Terms") and to
# comply with all applicable laws and regulations:
#
# 1. MStar shall retain any and all right, ownership and interest to MStar
#    Software and any modification/derivatives thereof.
#    No right, ownership, or interest to MStar Software and any
#    modification/derivatives thereof is transferred to you under Terms.
#
# 2. You understand that MStar Software might include, incorporate or be
#    supplied together with third party's software and the use of MStar
#    Software may require additional licenses from third parties.
#    Therefore, you hereby agree it is your sole responsibility to separately
#    obtain any and all third party right and license necessary for your use of
#    such third party's software.
#
# 3. MStar Software and any modification/derivatives thereof shall be deemed as
#    MStar's confidential information and you agree to keep MStar's
#    confidential information in strictest confidence and not disclose to any
#    third party.
#
# 4. MStar Software is provided on an "AS IS" basis without warranties of any
#    kind. Any warranties are hereby expressly disclaimed by MStar, including
#    without limitation, any warranties of merchantability, non-infringement of
#    intellectual property rights, fitness for a particular purpose, error free
#    and in conformity with any international standard.  You agree to waive any
#    claim against MStar for any loss, damage, cost or expense that you may
#    incur related to your use of MStar Software.
#    In no event shall MStar be liable for any direct, indirect, incidental or
#    consequential damages, including without limitation, lost of profit or
#    revenues, lost or damage of data, and unauthorized system use.
#    You agree that this Section 4 shall still apply without being affected
#    even if MStar Software has been modified by MStar in accordance with your
#    request or instruction for your use, except otherwise agreed by both
#    parties in writing.
#
# 5. If requested, MStar may from time to time provide technical supports or
#    services in relation with MStar Software to you for your use of
#    MStar Software in conjunction with your or your customer's product
#    ("Services").
#    You understand and agree that, except otherwise agreed by both parties in
#    writing, Services are provided on an "AS IS" basis and the warranty
#    disclaimer set forth in Section 4 above shall apply.
#
# 6. Nothing contained herein shall be construed as by implication, estoppels
#    or otherwise:
#    (a) conferring any license or right to use MStar name, trademark, service
#        mark, symbol or any other identification;
#    (b) obligating MStar or any of its affiliates to furnish any person,
#        including without limitation, you and your customers, any assistance
#        of any kind whatsoever, or any information; or
#    (c) conferring any license or right under any intellectual property right.
#
# 7. These terms shall be governed by and construed in accordance with the laws
#    of Taiwan, R.O.C., excluding its conflict of law rules.
#    Any and all dispute arising out hereof or related hereto shall be finally
#    settled by arbitration referred to the Chinese Arbitration Association,
#    Taipei in accordance with the ROC Arbitration Law and the Arbitration
#    Rules of the Association by three (3) arbitrators appointed in accordance
#    with the said Rules.
#    The place of arbitration shall be in Taipei, Taiwan and the language shall
#    be English.
#    The arbitration award shall be final and binding to both parties.
#
#******************************************************************************
#<MStar Software>

ifeq ($(BUILD_WITH_KERNEL), true)


KERNEL_SRC_TOP := $(ANDROID_BUILD_TOP)/vendor/mstar/kernel/linaro
DOT_CONFIG := $(KERNEL_SRC_TOP)/.config
TOOLCHAIN := /tools/arm/MStar/linaro_aarch64_linux-2014.09_r20170413

ifneq ($(filter arm arm64, $(TARGET_ARCH)),)
    MSTAR_CONFIG := $(KERNEL_SRC_TOP)/arch/$(TARGET_ARCH)/configs/mstar_config
    KERNEL_TARGET_OUT := $(KERNEL_SRC_TOP)/arch/$(TARGET_ARCH)/boot/Image
else
    $(error Not a supported TARGET_ARCH: $(TARGET_ARCH))
endif


# mainz
ifneq ($(filter bennet, $(TARGET_DEVICE)),)
    KERNEL_CONFIG := .config_mainz_SMP_arm64_andorid_emmc_nand_optee
    KERNEL_BIN_TARGET_OUT := $(KERNEL_SRC_TOP)/arch/$(TARGET_ARCH)/boot/dts/mainz_an.dtb

# maserati
else ifneq ($(filter plum, $(TARGET_DEVICE)),)
    KERNEL_CONFIG := .config_maserati_SMP_arm64_andorid_emmc_nand_optee
    KERNEL_BIN_TARGET_OUT := $(KERNEL_SRC_TOP)/arch/$(TARGET_ARCH)/boot/dts/maserati_an.dtb

# m7221
else ifneq ($(filter sugarcane, $(TARGET_DEVICE)),)
    KERNEL_CONFIG := .config_m7221_SMP_arm64_android_emmc_nand
    KERNEL_BIN_TARGET_OUT := $(KERNEL_SRC_TOP)/arch/$(TARGET_ARCH)/boot/dts/m7221_an.dtb

# maxim - china
else ifneq ($(filter synsepalum, $(TARGET_DEVICE)),)
    KERNEL_CONFIG := .config_maserati_SMP_arm64_andorid_emmc_nand_optee
    KERNEL_BIN_TARGET_OUT := $(KERNEL_SRC_TOP)/arch/$(TARGET_ARCH)/boot/dts/maxim_an.dtb

# maxim - sri
else ifneq ($(filter denali, $(TARGET_DEVICE)),)
    KERNEL_CONFIG := .config_maxim_SMP_arm64_andorid_emmc_nand_optee
    KERNEL_BIN_TARGET_OUT := $(KERNEL_SRC_TOP)/arch/$(TARGET_ARCH)/boot/dts/maxim_an.dtb

# k7u
else ifneq ($(filter lime, $(TARGET_DEVICE)),)
    KERNEL_CONFIG := .config_k7u_SMP_arm_andorid_emmc_nand_utopia2K
    KERNEL_BIN_TARGET_OUT := $(KERNEL_SRC_TOP)/arch/$(TARGET_ARCH)/boot/dts/k7u_an.dtb

# c2p
else ifneq ($(filter pineapple, $(TARGET_DEVICE)),)
    KERNEL_CONFIG := .config_c2p_SMP_arm64_android_emmc_nand_optee
    KERNEL_BIN_TARGET_OUT := $(KERNEL_SRC_TOP)/arch/$(TARGET_ARCH)/boot/dts/c2p_an.dtb

# k6
else ifneq ($(filter waxapple, $(TARGET_DEVICE)),)
    KERNEL_CONFIG := .config_k6_SMP_arm64_android_emmc_nand_cma_utopia2K
    KERNEL_BIN_TARGET_OUT := $(KERNEL_SRC_TOP)/arch/$(TARGET_ARCH)/boot/dts/k6_an.dtb

else
    $(error Not a supported TARGET_DEVICE: $(TARGET_DEVICE))

endif


KERNEL_BZIMAGE := $(PRODUCT_OUT)/kernel
KERNEL_DTB := $(PRODUCT_OUT)/images/prebuilts/dtb.bin

$(DOT_CONFIG): $(KERNEL_SRC_TOP)/$(KERNEL_CONFIG)
	cd $(KERNEL_SRC_TOP); \
	cp $(KERNEL_CONFIG) .config; cp $(KERNEL_CONFIG) $(MSTAR_CONFIG);

$(KERNEL_BZIMAGE): $(DOT_CONFIG)
	cd $(KERNEL_SRC_TOP); \
	export PATH=$(TOOLCHAIN)/bin:$(PATH); sh genlink.sh; \
	make defconfig KBUILD_DEFCONFIG=mstar_config; \
	make clean; make -j32;
	@cp -f $(KERNEL_TARGET_OUT) $@
	@cp -f $(KERNEL_BIN_TARGET_OUT) $(KERNEL_DTB)

kernel_clean: $(DOT_CONFIG)
	cd $(KERNEL_SRC_TOP); \
	export PATH=$(TOOLCHAIN)/bin:$(PATH); sh genlink.sh; \
	make defconfig KBUILD_DEFCONFIG=mstar_config; \
	make clean
	@rm -f $(KERNEL_BZIMAGE)
	@rm -f $(KERNEL_DTB)

$(PRODUCT_OUT)/boot.img: $(KERNEL_BZIMAGE)


endif #ifeq ($(BUILD_WITH_KERNEL), true)

# ==============================================================================
#include $(call all-subdir-makefiles)
