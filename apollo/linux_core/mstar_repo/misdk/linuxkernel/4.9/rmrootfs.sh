#!/bin/sh
[ -e arch/ramdisk/rootfs ] && sudo chown $USER:users arch/ramdisk/rootfs -R && rm arch/ramdisk/rootfs -rf
