#pragma once

#include "asset/data/texture_data.h"
#include "render/scene/scene_preview.h"

#include <memory>
#include <optional>

namespace Comet {
    class Renderer;
    class AssetManager;
    class RenderResourceFactory;
    class Mesh;
    struct RenderWorld;
}

namespace CometEditor {
    // 单个离屏任务；完成后只返回像素，不创建游戏实体或占用主视口。
    class ThumbnailRenderer {
    public:
        ThumbnailRenderer(Comet::Renderer& renderer, Comet::RenderResourceFactory& factory);
        [[nodiscard]] Comet::Result<void, Comet::GraphicsError> submit(
            Comet::AssetManager& assets, Comet::AssetHandle handle);
        [[nodiscard]] Comet::Result<std::optional<Comet::TextureData>, Comet::GraphicsError> poll() {
            return m_preview.poll();
        }

    private:
        Comet::ScenePreview m_preview;
        Comet::RenderResourceFactory& m_factory;
        std::shared_ptr<Comet::Mesh> m_material_sphere;
    };
}
