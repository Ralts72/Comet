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

    // 有界离屏任务；GPU 完成后取回像素，宿主持有期间 Renderer 须存活。
    class COMET_API ScenePreview {
    public:
        enum class Output { Display, LinearHdr };
        ScenePreview(Renderer& renderer, Math::Vec2u size);
        ~ScenePreview();
        [[nodiscard]] bool busy() const;
        [[nodiscard]] Result<void, GraphicsError> submit(std::shared_ptr<const RenderWorld> world,
            RenderCamera camera, Output output = Output::Display);
        // 六面共用同一份世界与 Shader 快照；排除局部反射、粒子、雾和后处理。
        // 返回单 mip float16 cubemap，面顺序为 +X/-X/+Y/-Y/+Z/-Z。
        [[nodiscard]] Result<void, GraphicsError> submit_cubemap(
            std::shared_ptr<const RenderWorld> world, Math::Vec3 position, float near_clip = 0.05f,
            float far_clip = 1000.0f);
        [[nodiscard]] Result<std::optional<TextureData>, GraphicsError> poll();

    private:
        struct State;
        Result<void, GraphicsError> start(std::shared_ptr<const RenderWorld> world,
            std::vector<RenderCamera> cameras, Output output, bool cubemap);
        Renderer& m_renderer;
        Math::Vec2u m_size;
        std::unique_ptr<State> m_state;
    };
}
