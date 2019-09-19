#!/bin/sh
cd arch
cd ramdisk
sudo tar jxvf rootfs_32_64_kernel.tar.bz2
cd ../../
sudo chown -R $(whoami) arch/ramdisk/rootfs
chmod -R 755 arch/ramdisk/rootfs
