#!/bin/bash
set -e

# 检测构建系统
if command -v ninja &> /dev/null; then
    GENERATOR="Ninja"
    PARALLEL="-j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
else
    GENERATOR="Unix Makefiles"
    PARALLEL="-j$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"
fi

# Release构建
BUILD_DIR="build-release"
mkdir -p $BUILD_DIR
echo "构建Release版本..."
cmake -S . -B $BUILD_DIR -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS_RELEASE="-O3 -DNDEBUG"
cmake --build $BUILD_DIR -- $PARALLEL

# 运行Release版本
EXEC="$BUILD_DIR/app/app"
if [ -x "$EXEC" ]; then
    echo "运行Release版本: $EXEC"
    "$EXEC"
else
    echo "Release executable not found: $EXEC"
fi
