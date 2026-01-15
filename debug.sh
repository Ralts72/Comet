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

# Debug构建
BUILD_DIR="build-debug"
mkdir -p $BUILD_DIR
echo "构建Debug版本..."
cmake -S . -B $BUILD_DIR -G "$GENERATOR" -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build $BUILD_DIR -- $PARALLEL

# 运行Editor（Debug版本）
EXEC="$BUILD_DIR/editor/editor"
if [ -x "$EXEC" ]; then
    echo "运行Editor: $EXEC"
    "$EXEC"
else
    echo "Editor executable not found: $EXEC"
fi