#!/bin/sh

if [ $# -eq 1 ] && [ -d $1 ]
then
    KERN_LOC=`realpath $1`
else
    KERN_LOC=`pwd`
fi


function copy_safe() {
    src=${1}
    dst=${2}
    [ -e ${src} ] && mv ${src} ${dst}
}

function link_safe() {
    src=${1}
    dst=${2}
    if [ -L ${dst} ]; then
        rm -rf ${dst}
    fi
    ln -s ${src} ${dst}
}

# Copy header files
copy_safe ./include/uapi/linux/netfilter_ipv4/ipt_ECN_U.h  ./include/uapi/linux/netfilter_ipv4/ipt_ECN.h
copy_safe ./include/uapi/linux/netfilter_ipv4/ipt_ecn_L.h  ./include/uapi/linux/netfilter_ipv4/ipt_ecn.h
copy_safe ./include/uapi/linux/netfilter_ipv4/ipt_TTL_U.h  ./include/uapi/linux/netfilter_ipv4/ipt_TTL.h
copy_safe ./include/uapi/linux/netfilter_ipv4/ipt_ttl_L.h  ./include/uapi/linux/netfilter_ipv4/ipt_ttl.h
copy_safe ./include/uapi/linux/netfilter_ipv6/ip6t_HL_U.h  ./include/uapi/linux/netfilter_ipv6/ip6t_HL.h
copy_safe ./include/uapi/linux/netfilter_ipv6/ip6t_hl_L.h  ./include/uapi/linux/netfilter_ipv6/ip6t_hl.h
copy_safe ./include/uapi/linux/netfilter/xt_CONNMARK_U.h ./include/uapi/linux/netfilter/xt_CONNMARK.h
copy_safe ./include/uapi/linux/netfilter/xt_connmark_L.h ./include/uapi/linux/netfilter/xt_connmark.h
copy_safe ./include/uapi/linux/netfilter/xt_DSCP_U.h ./include/uapi/linux/netfilter/xt_DSCP.h
copy_safe ./include/uapi/linux/netfilter/xt_dscp_L.h ./include/uapi/linux/netfilter/xt_dscp.h
copy_safe ./include/uapi/linux/netfilter/xt_MARK_U.h ./include/uapi/linux/netfilter/xt_MARK.h
copy_safe ./include/uapi/linux/netfilter/xt_mark_L.h ./include/uapi/linux/netfilter/xt_mark.h
copy_safe ./include/uapi/linux/netfilter/xt_RATEEST_U.h ./include/uapi/linux/netfilter/xt_RATEEST.h
copy_safe ./include/uapi/linux/netfilter/xt_rateest_L.h ./include/uapi/linux/netfilter/xt_rateest.h
copy_safe ./include/uapi/linux/netfilter/xt_TCPMSS_U.h ./include/uapi/linux/netfilter/xt_TCPMSS.h
copy_safe ./include/uapi/linux/netfilter/xt_tcpmss_L.h ./include/uapi/linux/netfilter/xt_tcpmss.h
copy_safe ./net/netfilter/xt_DSCP_U.c ./net/netfilter/xt_DSCP.c
copy_safe ./net/netfilter/xt_dscp_L.c ./net/netfilter/xt_dscp.c
copy_safe ./net/netfilter/xt_HL_U.c ./net/netfilter/xt_HL.c
copy_safe ./net/netfilter/xt_hl_L.c ./net/netfilter/xt_hl.c
copy_safe ./net/netfilter/xt_RATEEST_U.c ./net/netfilter/xt_RATEEST.c
copy_safe ./net/netfilter/xt_rateest_L.c ./net/netfilter/xt_rateest.c
copy_safe ./net/netfilter/xt_TCPMSS_U.c ./net/netfilter/xt_TCPMSS.c
copy_safe ./net/netfilter/xt_tcpmss_L.c ./net/netfilter/xt_tcpmss.c

# Create symlinks
link_safe ${KERN_LOC}/mstar2/drv/cpu/arm ./arch/arm/arm-boards
link_safe ${KERN_LOC}/mstar2/drv/cpu/arm64 ./arch/arm64/arm-boards
link_safe ../generated/autoconf.h ./include/linux/autoconf.h
link_safe ${KERN_LOC}/mstar2 ./drivers/mstar2
link_safe ${KERN_LOC}/mstar2/Kconfig ./arch/mips/Kconfig_kdrv
link_safe ${KERN_LOC}/mstar3party ./drivers/mstar3party
true
