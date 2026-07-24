#!/bin/bash
set -euo pipefail

if command -v ninja &> /dev/null; then
    GENERATOR="Ninja"
else
    GENERATOR="Unix Makefiles"
fi

BUILD_DIR="build"

echo "构建全部目标（Debug）..."
cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    -DCOMET_BUILD_APP=ON \
    -DCOMET_BUILD_EDITOR=ON \
    -DCOMET_BUILD_TESTS=ON
cmake --build "$BUILD_DIR" --parallel

echo "全部目标构建完成：$BUILD_DIR"
