#pragma once

#include "asset/data/texture_data.h"
#include "common/export.h"
#include "common/result.h"
#include <string_view>

namespace Comet {
    // 原样编码单层 RGBA8，保留 alpha，不做曝光／色调映射。
    [[nodiscard]] COMET_API Result<std::string> encode_png_rgba(const TextureData& image);
    // PNG 使用 SDR 显示变换与 sRGB，HDR 保留线性辐亮度；首行始终为画面顶部。
    [[nodiscard]] COMET_API Result<std::string> encode_image(
        const TextureData& image, std::string_view extension, float exposure = 1);
}
