#!/bin/bash
set -euo pipefail

if command -v ninja &> /dev/null; then
    GENERATOR="Ninja"
else
    GENERATOR="Unix Makefiles"
fi

BUILD_DIR="build-release"

echo "构建Release版本..."
cmake -S . -B "$BUILD_DIR" -G "$GENERATOR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCOMET_BUILD_APP=ON \
    -DCOMET_BUILD_EDITOR=OFF \
    -DCOMET_BUILD_TESTS=OFF
cmake --build "$BUILD_DIR" --parallel

EXEC="$BUILD_DIR/app/app"
if [ -x "$EXEC" ]; then
    echo "运行Release版本: $EXEC"
    "$EXEC"
else
    echo "Release executable not found: $EXEC"
fi
