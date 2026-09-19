#pragma once

#include "viewport/viewport_panel.h"
#include "viewport/transform_gizmo.h"
#include "render/scene/scene_picking.h"
#include "common/error.h"
#include "common/result.h"

#include <memory>

namespace Comet {
    class AssetRegistry;
    class Renderer;
    class Sampler;
}

namespace CometEditor {
    class ImGuiContext;

    class Viewport final {
    public:
        Viewport(EditorState& state, SelectionService& selection, CommandHistory& history,
            const Comet::ComponentRegistry& components, PropertyEditTransaction& inspector_edit,
            const EditorShortcuts& shortcuts, Comet::Renderer& renderer,
            Comet::AssetRegistry& assets, ImGuiContext& ui,
            std::shared_ptr<Comet::Sampler> sampler);

        Viewport(const Viewport&) = delete;
        Viewport& operator=(const Viewport&) = delete;

        [[nodiscard]] ViewportPanel& panel() { return m_panel; }

        void update_texture();
        // 面板命令处理后、SceneExtractor 提取前调用。
        Comet::Result<void, Comet::Error> update(Comet::Scene* scene);
        void submit_feedback(Comet::Scene* scene);
        void apply_pick(std::optional<Comet::ScenePickHit> hit, Comet::Scene* scene);

    private:
        void focus_selection(Comet::Scene* scene);
        void submit_selection_bounds(Comet::Scene* scene);

        EditorState& m_state;
        SelectionService& m_selection;
        Comet::Renderer& m_renderer;
        Comet::AssetRegistry& m_assets;
        ImGuiContext& m_ui;
        std::shared_ptr<Comet::Sampler> m_sampler;
        TransformGizmo m_gizmo;
        ViewportPanel m_panel;
    };
}
