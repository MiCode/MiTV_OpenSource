filename=p4_changelist.h

GIT_COMMIT_ID="$(git rev-parse HEAD)"
test $? -ne 0 &&  GIT_COMMIT_ID=""
test -n "${GIT_COMMIT_ID}" && echo ${GIT_COMMIT_ID} > ${filename} || echo "MStarKrl" > ${filename}

version="$(cat ${filename} | cut -c 1-9)"
sed -i "s/#define\ \+KERN_CL\ \+.*/#define\ KERN_CL\ \"KERN-3.10.86.${version}\"/g" include/linux/release_version.h
