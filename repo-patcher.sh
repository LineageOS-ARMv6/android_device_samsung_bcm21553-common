#!/bin/sh

echo Patching each individual repo for regular building...

cd dalvik
git reset --hard && git clean -f -d
patch -p1 < ../device/samsung/bcm21553-common/patches/Fix-for-Dalvik-compile-error-on-CyanogenMod-9.patch
cd ../
cd frameworks/compile/slang
git reset --hard && git clean -f -d
patch -p1 < ../../../device/samsung/bcm21553-common/patches/Fix-build-for-slang.patch
cd ../../../
cd external/e2fsprogs
git reset --hard && git clean -f -d
patch -p1 < ../../device/samsung/bcm21553-common/patches/Allow-e2fsprogs-to-be-built-using-the-clang.patch
cd ../../
