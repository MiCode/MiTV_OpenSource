#!/bin/sh

if [ $# -eq 1 ] && [ -d $1 ]
then
	KERN_LOC=`realpath $1`
else
	KERN_LOC=`pwd`
fi

#echo "$KERN_LOC"
if [ -e ${KERN_LOC}/mstar2 ]; then
	MST_DRVROOT=${KERN_LOC}
elif [ -e ${KERN_LOC}/../mstar2 ]; then
	MST_DRVROOT=${KERN_LOC}/..
else
	echo -e "err: mstar2 not found\n"
fi

# Create symlinks
ls ${MST_DRVROOT}/mstar2/hal/ | xargs -I {} unlink ${KERN_LOC}/arch/arm/mach-{} 2> /dev/null
unlink ./arch/arm64/arm-boards 2> /dev/null
unlink ./drivers/mstar2 2> /dev/null
unlink ./arch/mips/Kconfig_kdrv 2> /dev/null
unlink ./drivers/mstar3party 2> /dev/null
true
