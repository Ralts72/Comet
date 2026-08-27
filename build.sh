#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

echo "配置 dev-debug（Debug: app + editor + tests）..."
cmake --preset dev-debug
cmake --build --preset dev-debug --parallel

echo "全部目标构建完成：$ROOT_DIR/build"
