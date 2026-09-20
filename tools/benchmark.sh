#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CALLER_DIRECTORY="$PWD"

usage() {
    printf '%s\n' \
        '用法：tools/benchmark.sh [OUTPUT.csv OBJECTS WIDTH HEIGHT FRAMES BLOOM(0/1)]' \
        '不带参数：64 个物体、640×360 逻辑窗口、240 帧、Bloom 开启。' \
        '默认报告：仓库 build-release/reports/render-benchmark.csv。' \
        '自定义相对报告路径以调用时的工作目录为准。' \
        '复用 build-release，仅构建基准程序及其依赖；不启动 app/editor。'
}

if [[ $# -eq 1 && "$1" == "--help" ]]; then
    usage
    exit 0
fi

args=("$@")
if [[ $# -eq 0 ]]; then
    args=("$ROOT_DIR/build-release/reports/render-benchmark.csv" 64 640 360 240 1)
elif [[ $# -ne 6 ]]; then
    usage >&2
    exit 2
fi

cd "$ROOT_DIR"
echo "配置并构建 Release 渲染基准..."
cmake --preset app-release -DCOMET_BUILD_BENCHMARKS=ON
cmake --build --preset app-release --target render_benchmark --parallel

EXEC="$ROOT_DIR/build-release/tools/render_benchmark/render_benchmark"
echo "运行固定场景基准，报告：${args[0]}"
cd "$CALLER_DIRECTORY"
if [[ "$(uname -s)" == "Darwin" ]]; then
    exec /usr/bin/caffeinate -i -s "$EXEC" "${args[@]}" -NSAutomaticWindowAnimationsEnabled NO
fi
exec "$EXEC" "${args[@]}"
