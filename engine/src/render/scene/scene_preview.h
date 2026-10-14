#pragma once

#include "asset/data/texture_data.h"
#include "common/export.h"
#include "common/result.h"
#include "graphics/result.h"
#include "render/scene/render_scene.h"

#include <memory>
#include <optional>

namespace Comet {
    class Renderer;
    struct RenderWorld;

    // 有界离屏任务：提交不可变世界，GPU 完成后取回 SDR 像素；宿主持有期间 Renderer 须存活。
    class COMET_API ScenePreview {
    public:
        ScenePreview(Renderer& renderer, Math::Vec2u size);
        ~ScenePreview();
        [[nodiscard]] bool busy() const;
        [[nodiscard]] Result<void, GraphicsError> submit(
            std::shared_ptr<const RenderWorld> world, RenderCamera camera);
        [[nodiscard]] Result<std::optional<TextureData>, GraphicsError> poll();

    private:
        struct State;
        Renderer& m_renderer;
        Math::Vec2u m_size;
        std::unique_ptr<State> m_state;
    };
}
