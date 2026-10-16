#pragma once

#include "common/export.h"
#include "core/geometry.h"
#include "graphics/result.h"

#include <memory>

namespace Comet {
    class Texture;
    class RenderResourceFactory;
    struct COMET_API IrradianceData;

    struct COMET_API Irradiance {
        static constexpr int TEXELS_PER_PROBE = 41;

        Math::Vec3 origin{};
        Math::Vec3 spacing{1};
        Math::Vec3u dimensions{2};
        std::shared_ptr<Texture> probes;

        [[nodiscard]] BoundingBox bounds() const;
        [[nodiscard]] static Result<std::shared_ptr<Irradiance>, GraphicsError> try_create(
            RenderResourceFactory& resources, IrradianceData data);
    };
}
