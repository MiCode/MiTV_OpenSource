#!/bin/sh
cd arch
cd ramdisk
tar xvf rootfs-arm64.tar.gz
cd ../../
chown -R $(whoami) arch/ramdisk/rootfs
chmod -R 777 arch/ramdisk/rootfs

#mkdir rootfs
#cp ramdisk.gz a.gz
#gunzip a.gz
#cd rootfs
#sudo cpio -ivd -I ../a
#rm -f ../a
#cd ../../..
#sudo chmod -R 777 ./arch/ramdisk/rootfs
#devexist=`ls ./arch/ramdisk/rootfs/dev | grep system`
#if [ "$devexist" == '' ] ; then
#    rm -rf ./arch/ramdisk/rootfs
#    printf '\E[1;31;40mrootfs extraction fail!!\nYou have to extract rootfs in root privilege!!\n\E[0m'
#else
#    printf 'rootfs extraction success\n'
#fi
