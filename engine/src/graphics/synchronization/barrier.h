#pragma once

#include "common/export.h"
#include "graphics/synchronization/resource_state.h"
#include "graphics/vk_common.h"

#include <optional>

namespace Comet::Graphics {
    [[nodiscard]] COMET_API std::optional<vk::ImageMemoryBarrier2>
    build_image_memory_barrier(
        vk::Image image,
        const ImageState& before,
        const ImageState& after);
}
