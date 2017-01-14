#!/bin/sh

set -e

unset CC
unset CXX
export PATH=$HOME/android-toolchain/bin:$PATH

mkdir build
cd build
cmake -DBUILD_SHARED_LIBS=ON -DBUILD_TARGET_PLATFORM=ANDROID -DCMAKE_BUILD_TYPE=Release -DANDROID_STANDALONE_TOOLCHAIN=$HOME/android-toolchain -DANDROID_ABI=x86_64 .. # TODO: -DBUILD_TESTING=ON -DBUILD_TESTS=ON CC="arm-linux-androideabi-gcc" CXX="arm-linux-androideabi-g++"
cmake --build . --config release -- -j 4
# TODO: cmake --build . --config release --target test
