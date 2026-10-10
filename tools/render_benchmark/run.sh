#!/bin/bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
CALLER_DIRECTORY="$PWD"

usage() {
    printf '%s\n' \
        '用法：tools/render_benchmark/run.sh [OUTPUT.csv OBJECTS WIDTH HEIGHT FRAMES BLOOM(0/1) [MATERIALS [WORKLOAD [MSAA ANISOTROPY RENDER_SCALE]]]]' \
        '不带参数：64 个物体、640×360 逻辑窗口、240 帧、Bloom 开启。' \
        'MATERIALS 默认 1，范围 1..256 且不能超过物体数。' \
        'WORKLOAD：static（默认）、moving、culling（3/4 物体屏外）、project-shader、physics-active、physics-sleeping；物理场景最多 512 个物体。' \
        '画质默认 4×MSAA、1×各向异性、100% 渲染比例；自定义时三个参数一起提供。' \
        'MSAA：1/2/4/8；ANISOTROPY：1..16；RENDER_SCALE：0.5..1。' \
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
elif [[ $# -lt 6 || ( $# -gt 8 && $# -ne 11 ) ]]; then
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
