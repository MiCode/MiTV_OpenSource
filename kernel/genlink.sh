#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# *****************************************************************************
#
#  This file is provided under a dual license.  When you use or
#  distribute this software, you may choose to be licensed under
#  version 2 of the GNU General Public License ("GPLv2 License")
#  or BSD License.
#
#  GPLv2 License
#
#  Copyright(C) 2019 MediaTek Inc.
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of version 2 of the GNU General Public License as
#  published by the Free Software Foundation.
#
#  This program is distributed in the hope that it will be useful, but
#  WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
#  See http://www.gnu.org/licenses/gpl-2.0.html for more details.
#
#  BSD LICENSE
#
#  Copyright(C) 2019 MediaTek Inc.
#  All rights reserved.
#
#  Redistribution and use in source and binary forms, with or without
#  modification, are permitted provided that the following conditions
#  are met:
#
#   * Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
#   * Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in
#     the documentation and/or other materials provided with the
#     distribution.
#   * Neither the name of the copyright holder nor the names of its
#     contributors may be used to endorse or promote products derived
#     from this software without specific prior written permission.
#
#  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
#  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
#  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
#  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
#  HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
#  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
#  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
#  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
#  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
#  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# ****************************************************************************/
 if [ "${NCT5_BUILD}" = "true" ]; then
    # Create symlinks
    #BUILD_LOC on Android Top
    MST_DRVROOT=${KERNELDIR}
    KERNEL_PATH_MSTAR=${KERNEL_ROOT}
    # Create symlinks
    ln -sfn ${KERNEL_PATH_MSTAR}/mstar2 ${MST_DRVROOT}/drivers/mstar2
    ln -sfn ${KERNEL_PATH_MSTAR}/mstar3party ${MST_DRVROOT}/drivers/mstar3party
    ln -sfn ${KERNEL_PATH_MSTAR}/mstar2/hal/${CHIP}/cpu/arm ${MST_DRVROOT}/arch/arm/mach-${CHIP}
    ln -sfn ${KERNEL_PATH_MSTAR}/mstar2/hal/common ${MST_DRVROOT}/arch/arm/mach-common
    ln -sfn ${KERNEL_PATH_MSTAR}/mstar2/drv/cpu/arm64 ${MST_DRVROOT}/arch/arm64/arm-boards

    if [ ! -d ${MST_DRVROOT}/drivers/mtk_misc ]; then
        ln -sfn ${MST_BUILD_ENTRY}/patch/kernel/mtk_misc ${MST_DRVROOT}/drivers/mtk_misc
        sed -i '$a\obj-y			+= mtk_misc/' ${MST_DRVROOT}/drivers/Makefile
        sed -i '/endmenu/i source "drivers/mtk_misc/Kconfig"' ${MST_DRVROOT}/drivers/Kconfig
    fi
    echo "NCT5:genlink.sh (KERNEL_PATH_MSTAR,MST_DRVROOT,CHIP)=(${KERNEL_PATH_MSTAR},${MST_DRVROOT},${CHIP}) "
else
    if [ $# -eq 1 ] && [ -d $1 ]
    then
        KERN_LOC=`realpath $1`
    else
        KERN_LOC=`pwd`
    fi

    echo "$KERN_LOC"
    if [ -e ${KERN_LOC}/mstar2 ]; then
        MST_DRVROOT=${KERN_LOC}
    elif [ -e ${KERN_LOC}/../mstar2 ]; then
        MST_DRVROOT=${KERN_LOC}/..
    else
        echo -e "err: mstar2 not found\n"
    fi
    # Create symlinks
    ls ${MST_DRVROOT}/mstar2/hal/ | xargs -I {} ln -sfn ${MST_DRVROOT}/mstar2/hal/{}/cpu/arm ${KERN_LOC}/arch/arm/mach-{}
    ln -sfn ${MST_DRVROOT}/mstar2/drv/cpu/arm64 ./arch/arm64/arm-boards
    ln -sfn ${MST_DRVROOT}/mstar2 ./drivers/mstar2
    ln -sfn ${MST_DRVROOT}/mstar2/Kconfig ./arch/mips/Kconfig_kdrv
    ln -sfn ${MST_DRVROOT}/mstar3party ./drivers/mstar3party
    true
fi
