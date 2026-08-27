#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

echo "配置 editor-dev（RelWithDebInfo）..."
cmake --preset editor-dev
cmake --build --preset editor-dev --parallel

EXEC="$ROOT_DIR/build-editor/editor/editor"
if [ -x "$EXEC" ]; then
    echo "运行 Editor: $EXEC"
    "$EXEC"
else
    echo "Editor executable not found: $EXEC"
    exit 1
fi
