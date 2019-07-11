#!/bin/bash
echo $PWD
export PATH=$PWD/cross_compile_tool/bin:$PATH

echo $#
echo $0
echo $1

if [ $# -gt "2" ] ;then
   
	echo "Usage:./build clean/menuconfig"
elif [ "$1" = "clean" ];then 
	echo "make clean"
	make clean
elif [ "$1" = "menuconfig" ];then
	echo "make meuconfig"
	make menuconfig
else 
	echo "make"
	make -j8
fi

date=`date +%Y%m%d`

echo "build time:"$date
