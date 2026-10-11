#pragma once

#include "viewport/viewport_panel.h"
#include "viewport/transform_gizmo.h"
#include "common/error.h"
#include "common/result.h"

#include <memory>

namespace Comet {
    class AssetRegistry;
    class Renderer;
    class Sampler;
}

namespace CometEditor::Ui {
    class ImGuiContext;
}

namespace CometEditor {
    class Viewport final {
    public:
        Viewport(EditorState& state, const Comet::SceneRuntime& runtime,
            SelectionService& selection, CommandHistory& history,
            const Comet::ComponentRegistry& components, PropertyEditTransaction& inspector_edit,
            const EditorShortcuts& shortcuts, Comet::Renderer& renderer,
            Comet::AssetRegistry& assets, Ui::ImGuiContext& ui,
            std::shared_ptr<Comet::Sampler> sampler, uint32_t scene_view);

        Viewport(const Viewport&) = delete;
        Viewport& operator=(const Viewport&) = delete;

        [[nodiscard]] SceneViewportPanel& panel() { return m_panel; }
        [[nodiscard]] GameViewportPanel& game_panel() { return m_game_panel; }
        void cancel_interaction();

        void update_texture();
        // 主线程在构建面板前消费上一帧结果；场景切换和失效实体不改变选择。
        void consume_pick(Comet::Scene* scene);
        // 面板命令处理后、SceneExtractor 提取前调用。
        Comet::Result<void, Comet::Error> update(Comet::Scene* scene);
        void submit_feedback(Comet::Scene* scene);

    private:
        void focus_selection(Comet::Scene* scene);
        void submit_selection_bounds(Comet::Scene* scene);

        EditorState& m_state;
        SelectionService& m_selection;
        Comet::Renderer& m_renderer;
        Comet::AssetRegistry& m_assets;
        Ui::ImGuiContext& m_ui;
        std::shared_ptr<Comet::Sampler> m_sampler;
        uint32_t m_scene_view;
        TransformGizmo m_gizmo;
        SceneViewportPanel m_panel;
        GameViewportPanel m_game_panel;
    };
}
