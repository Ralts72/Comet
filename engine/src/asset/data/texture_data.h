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
    };
}
