#!/bin/sh

echo Resetting each repo in order to successfully repo sync...

cd dalvik
git reset --hard && git clean -f -d
cd ../
cd frameworks/compile/slang
git reset --hard && git clean -f -d
cd ../../../
cd external/e2fsprogs
git reset --hard && git clean -f -d
cd ../../
