#!/bin/sh
set -xe
rm -rf _enet
git rm -rf --ignore-unmatch *.[ch]
git clone https://github.com/lsalzman/enet.git _enet
cd _enet; git rev-parse HEAD > ../git-revision; cd ..
mv _enet/*.c _enet/include/enet/*.h _enet/LICENSE .
git add *.[ch] LICENSE git-revision
rm -rf _enet
echo 'Make sure to update CMakeLists.txt.'
