#pragma once

#include "common/export.h"
#include "render/render_scene.h"

namespace Comet {
    class Scene;

    class COMET_API SceneExtractor {
    public:
        [[nodiscard]] static RenderScene extract(Scene& scene);
    };
}
