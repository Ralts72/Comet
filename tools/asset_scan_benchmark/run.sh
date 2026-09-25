#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CALLER_DIRECTORY="$PWD"

usage() {
    printf '%s\n' \
        '用法：tools/asset_scan_benchmark/run.sh [项目目录 [轮数]]' \
        '不带参数：测量仓库 demo 项目，运行 30 轮。' \
        '项目目录的相对路径以调用时的工作目录为准。' \
        '复用 build-release，仅构建资产扫描基准及其依赖。'
}

if [[ $# -eq 1 && "$1" == "--help" ]]; then
    usage
    exit 0
fi
if [[ $# -gt 2 ]]; then
    usage >&2
    exit 2
fi

if [[ $# -eq 0 ]]; then
    args=("$ROOT_DIR/demo" 30)
elif [[ $# -eq 1 ]]; then
    args=("$1" 30)
else
    args=("$1" "$2")
fi
cd "$ROOT_DIR"
echo '配置并构建 Release 资产扫描基准...' >&2
cmake --preset app-release -DCOMET_BUILD_BENCHMARKS=ON >&2
cmake --build --preset app-release --target asset_scan_benchmark --parallel >&2

cd "$CALLER_DIRECTORY"
exec "$ROOT_DIR/build-release/tools/asset_scan_benchmark/asset_scan_benchmark" "${args[@]}"
