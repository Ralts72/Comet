#pragma once

#include "asset/content_fwd.h"

#include "core/geometry.h"
#include "graphics/result.h"

#include <cstdint>
#include <memory>

namespace Comet {
    class RenderResourceFactory;

    struct COMET_API Irradiance {
        static constexpr int TEXELS_PER_PROBE = 41;

        Math::Vec3 origin{};
        Math::Vec3 spacing{1};
        Math::Vec3u dimensions{2};
        std::shared_ptr<Texture> probes;

        [[nodiscard]] uint32_t probe_count() const {
            return dimensions.x * dimensions.y * dimensions.z;
        }
        [[nodiscard]] BoundingBox bounds() const;
        [[nodiscard]] static Result<std::shared_ptr<Irradiance>, GraphicsError> try_create(
            RenderResourceFactory& resources, IrradianceData data);
    };
}
