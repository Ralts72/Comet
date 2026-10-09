#pragma once

#include "asset/asset_manager.h"
#include "assets/asset_edit.h"
#include "render/material/material_renderer.h"

#include <map>
#include <optional>

namespace Comet {
    class Renderer;
}

namespace CometEditor {
    class EditorAssets;

    // 一次手势拥有预览与回退版本；资产历史独立于场景寿命。
    class MaterialEditSession {
    public:
        MaterialEditSession(EditorAssets& assets, Comet::Renderer& renderer);
        [[nodiscard]] Comet::Result<void, Comet::Error> process(const AssetEdit& edit);
        [[nodiscard]] Comet::Result<void, Comet::Error> commit();
        [[nodiscard]] Comet::Result<void, Comet::Error> cancel();
        [[nodiscard]] bool active() const { return m_active.has_value(); }
        [[nodiscard]] bool can_undo(Comet::AssetHandle handle) const;
        [[nodiscard]] bool can_redo(Comet::AssetHandle handle) const;
        [[nodiscard]] Comet::Result<void, Comet::Error> undo(Comet::AssetHandle handle);
        [[nodiscard]] Comet::Result<void, Comet::Error> redo(Comet::AssetHandle handle);

    private:
        struct ActiveEdit {
            AssetEdit edit;
            std::shared_ptr<Comet::Material> original;
            Comet::AssetManager::MaterialUpdate candidate;
            Comet::MaterialRenderer::MaterialUpdate rollback;
        };
        struct History {
            History() { edits.reserve(128); }
            void record(const MaterialEdit& edit);
            std::vector<MaterialEdit> edits;
            std::size_t cursor = 0;
        };
        [[nodiscard]] Comet::Result<void, Comet::Error> preview(const AssetEdit& edit);
        [[nodiscard]] Comet::Result<void, Comet::Error> replay(
            Comet::AssetHandle handle, bool forward);

        EditorAssets& m_assets;
        Comet::Renderer& m_renderer;
        std::optional<ActiveEdit> m_active;
        std::map<Comet::AssetHandle, History> m_histories;
    };

}
