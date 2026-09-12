#pragma once

#include "viewport/view.h"
#include "viewport/transform_gizmo.h"
#include "render/scene/scene_picking.h"

namespace Comet {
    class AssetRegistry;
    class Renderer;
}

namespace CometEditor {
    class ImGuiContext;

    class Viewport final {
    public:
        Viewport(EditorState& state, SelectionService& selection, CommandHistory& history,
            const Comet::ComponentRegistry& components,
            PropertyEditTransaction& inspector_edit, const EditorShortcuts& shortcuts,
            Comet::Renderer& renderer, Comet::AssetRegistry& assets, ImGuiContext& ui);

        Viewport(const Viewport&) = delete;
        Viewport& operator=(const Viewport&) = delete;

        [[nodiscard]] ViewPanel& panel() { return m_panel; }

        void update_texture();
        // 面板命令处理后、SceneExtractor 提取前调用。
        void update(Comet::Scene* scene);
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
        TransformGizmo m_gizmo;
        ViewPanel m_panel;
    };
}
