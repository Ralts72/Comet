#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include <memory>

namespace Comet {
    class Texture;
    class RenderResourceFactory;
    struct EnvironmentData;

    // 背景与光照资源整组发布，在途帧保留整组资源。
    struct COMET_API Environment {
        std::shared_ptr<Texture> background;
        std::shared_ptr<Texture> irradiance;
        std::shared_ptr<Texture> specular;
        std::shared_ptr<Texture> brdf;

        [[nodiscard]] static Result<std::shared_ptr<Environment>, GraphicsError> try_create(
            RenderResourceFactory& resources, const EnvironmentData& data);
    };
}
