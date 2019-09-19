#!/bin/bash

# This script is made for MStar kernel source code release
# Usage: see showUsage()


function showUsage() {
    echo "Usage: $0  toolchain  platform  config"
    echo "Example: $0 /tools/arm/MStar/linaro-aarch64_linux-2014.09_843419-patched/bin arm64 .config_maserati_SMP_arm64_andorid_emmc_nand"
}

function ShowBusy(){
    title=${1:-}
    S=( '/' '-' '\\' '|');
    N=${#S[@]};
    I=0;
    while read data;
    do
        I=$(((I+1)%($N)));
        echo -en "\r${title}...${S[I]}";
    done
    echo
}

function buildKernel() {
    toolchain="$1"
    platform="$2"
    config="$3"
    threads=${4:-1}
    export PATH=$toolchain:$PATH
    cp $config .config
    cp $config arch/$platform/configs/mstar_config
    ./genlink.sh
    make defconfig KBUILD_DEFCONFIG=mstar_config
    make clean
    make -j${threads}
}

function releaseKernel() {
    RELEASE_FOLDER="MStarKernel_$(date +%Y%m%d)"
    mkdir ${RELEASE_FOLDER}
    find ./  -mindepth 1 -maxdepth 1 -not -name .git \
                                  -a -not -name ${RELEASE_FOLDER} \
                                  -a -not -name "$(basename $0)" \
                                  -exec rsync -av \{} ${RELEASE_FOLDER}/ \; 2>&1 | ShowBusy "Copying"
    cd ${RELEASE_FOLDER}/mstar2
    rm -rf .git
    cd -
    cd ${RELEASE_FOLDER}/mstar3party
    rm -rf .git
    cd -
    tar -jcvf ${RELEASE_FOLDER}.tar.bz2 ${RELEASE_FOLDER}/ 2>&1 | ShowBusy "Compressing"
    echo -e "\e[1;31mKernel Source code for release: ${RELEASE_FOLDER}.tar.bz2\e[0m"
}

function removeSecrets() {
    find ./mstar2/hal/ -mindepth 1 -maxdepth 1 -not -name ${mstar_chip} -print -exec rm -rf \{} \;
    rm -rf kernel/kdebugd/
    rm -f arch/Kconfig.kdebug
    find arch/ -mindepth 2 -maxdepth 2 -name Kconfig -exec  sed -i 's/^.*Kconfig\.kdebug.*$//g' \{} \;
    sed -i 's/^.*KDEBUGD.*$//g' kernel/Makefile
}

function main() {
    toolchain="$1"
    platform="$2"
    config="$3"

    if [[ -z "$toolchain" ]]; then
        echo "toolchain not correct"
        showUsage
        return 1
    else
        echo -e "toolchain: \e[1;32m[$toolchain]\e[0m"
    fi

    if [[ -n "$platform" ]]; then
        echo -n "platform : $platform ... "

        case "$platform" in
            "arm")
                echo -e "\e[1;32m[supported]\e[0m"
                ;;
            "arm64")
                echo -e "\e[1;32m[supported]\e[0m"
                ;;
            *)
                echo -e "\e[1;31m[not supported]\e[0m"
                showUsage
                return 1
                ;;
        esac
    else
        echo "platform not correct"
        showUsage
    fi
    if [[ -z "$config" || ! -e "$config" ]]; then
        echo -e "config \e[1;31m\"$config\"\e[0m not found"
        return 1
    else
        echo -e "config   : \e[1;32m[$config]\e[0m"
    fi

    mstar_chip="$(grep -E "^\ *CONFIG_MSTAR_CHIP_NAME=\"" ${config} | awk -F\" '{print$2}')"
    if [[ -n "$mstar_chip" ]]; then
        # Phase 1: build kernel for necessary binary files
        # remove HAL code of other chips
        removeSecrets
        buildKernel ${toolchain} ${platform} ${config} 48
        echo -e "\e[1;31mKernel build completed.\e[0m"

        if [[ -d "mstar2/hal/$mstar_chip/xc" ]]; then
            DLC_BIN="$(find . -name mhal_dlc.o -print0)"
            if [[ -e "$DLC_BIN" ]]; then
	        cd mstar2/
#                git add ${DLC_BIN}
#                git commit -n -m "stage mhal_dlc.o"
                find -name '*.o' | grep /xc/ | xargs -i git add {}
                git commit -n -m "stage xc obj file"
                git clean -df
                git reset --hard
		cd ../
            else
                echo -e "\e[1;32mERROR:$DLC_BIN not found\e[0m"
                return 1
            fi

            # Phase 2: Clean and prepare source code release
            # remove HAL code of other chips
            removeSecrets

            #remove LGPL file
            #find . -name "mhal_dlc.c" -delete;
            find -name '*.c' | grep /xc/ | xargs rm -f
            STRIP=$(find ${toolchain} -name "*strip" -print0)
            if [[ ! -e $"STRIP" ]]; then
                find . -name mhal_dlc.o -exec $STRIP -g \{} \;
            else
                echo -e "\e[1;31mERROR:strip executable not found\e[0m"
                return 1
            fi

	else
	    echo "XC has been removed from Kernel"
	fi

        releaseKernel
	git reset --soft HEAD~1
	git reset .
    else
        echo "mstar_chip not defined"
    fi
}

if [[ $# -gt 2 ]]; then
    main $@
else
    showUsage
fi
