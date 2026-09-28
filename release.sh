#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CALLER_DIRECTORY="$PWD"
cd "$ROOT_DIR"

echo "配置 app-release（Release）..."
cmake --preset app-release
cmake --build --preset app-release --parallel

EXEC="$ROOT_DIR/build-release/app/app"
if [[ "$(uname -s)" == "Darwin" ]]; then
    EXEC="$ROOT_DIR/build-release/app/Comet.app/Contents/MacOS/Comet"
fi
if [ -x "$EXEC" ]; then
    echo "运行 Release App: $EXEC"
    cd "$CALLER_DIRECTORY"
    if [[ $# -eq 0 ]]; then
        "$ROOT_DIR/build-release/tools/asset/comet_prepare_project" "$ROOT_DIR/demo"
    elif [[ $# -eq 1 && "$1" != "--help" ]]; then
        "$ROOT_DIR/build-release/tools/asset/comet_prepare_project" "$1"
    fi
    exec "$EXEC" "$@"
else
    echo "Release App executable not found: $EXEC"
    exit 1
fi
