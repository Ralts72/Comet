#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

echo "配置 app-release（Release）..."
cmake --preset app-release
cmake --build --preset app-release --parallel

EXEC="$ROOT_DIR/build-release/app/app"
if [ -x "$EXEC" ]; then
    echo "运行 Release App: $EXEC"
    "$EXEC"
else
    echo "Release App executable not found: $EXEC"
    exit 1
fi
