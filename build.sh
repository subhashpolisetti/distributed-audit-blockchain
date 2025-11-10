#!/bin/bash

set -e

echo "Cleaning previous build..."
rm -rf build

mkdir -p build
cd build

echo "Configuring with CMake..."
if [[ "$(uname)" == "Darwin" ]]; then
    echo "Using Apple Clang for macOS build..."
    export CC=/usr/bin/clang
    export CXX=/usr/bin/clang++
    cmake -DCMAKE_BUILD_TYPE=Release ..
else
    cmake ..
fi

echo "Building..."
if [[ "$(uname)" == "Darwin" ]]; then
    make -j$(sysctl -n hw.logicalcpu)
else
    make -j$(nproc)
fi

echo "Build completed successfully!"
echo "The executable is located at: $(pwd)/blockchain_node"
echo ""
echo "To run the node:"
echo "./blockchain_node --config ../config.json"
