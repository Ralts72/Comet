#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CALLER_DIRECTORY="$PWD"
cd "$ROOT_DIR"

echo "配置 editor-dev（RelWithDebInfo）..."
cmake --preset editor-dev
cmake --build --preset editor-dev --parallel

EXEC="$ROOT_DIR/build-editor/editor/editor"
if [[ "$(uname -s)" == "Darwin" ]]; then
    EXEC="$ROOT_DIR/build-editor/editor/CometEditor.app/Contents/MacOS/CometEditor"
fi
if [ -x "$EXEC" ]; then
    echo "运行 Editor: $EXEC"
    cd "$CALLER_DIRECTORY"
    exec "$EXEC" "$@"
else
    echo "Editor executable not found: $EXEC"
    exit 1
fi
