#pragma once

#include "asset/data/texture_data.h"
#include "common/export.h"
#include "common/result.h"
#include <string_view>

namespace Comet {
    // PNG 使用 SDR 显示变换与 sRGB，HDR 保留线性辐亮度；首行始终为画面顶部。
    [[nodiscard]] COMET_API Result<std::string> encode_image(
        const TextureData& image, std::string_view extension, float exposure = 1);
}
