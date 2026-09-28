#pragma once

#include "common/export.h"
#include "graphics/result.h"
#include <memory>

namespace Comet {
    class Texture;
    class RenderResourceFactory;
    struct EnvironmentData;

    // 首次导入允许只显示临时背景；正式背景与光照仍整组发布。
    struct COMET_API Environment {
        std::shared_ptr<Texture> background;
        std::shared_ptr<Texture> irradiance;
        std::shared_ptr<Texture> specular;
        std::shared_ptr<Texture> brdf;

        [[nodiscard]] bool has_lighting() const { return irradiance && specular && brdf; }

        [[nodiscard]] static Result<std::shared_ptr<Environment>, GraphicsError> try_create(
            RenderResourceFactory& resources, const EnvironmentData& data);
    };
}
