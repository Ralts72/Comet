#pragma once

#include "graphics/enums.h"

#include <cstdint>
#include <vector>

namespace Comet {
    struct TextureData {
        int width = 0;
        int height = 0;
        Format format = Format::R8G8B8A8_UNORM;
        std::vector<std::uint8_t> pixels;
        // 像素按 mip 层级连续存储；每级立方体贴图的面顺序为 +X、-X、+Y、-Y、+Z、-Z。
        uint32_t mip_levels = 1;
        bool cubemap = false;
    };
}
