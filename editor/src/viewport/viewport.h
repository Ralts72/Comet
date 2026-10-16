#pragma once

#include "viewport/viewport_panel.h"
#include "viewport/transform_gizmo.h"
#include "viewport/physics_overlay.h"
#include "common/error.h"
#include "common/result.h"
#include "render/debug/line_draw_list.h"
#include "render/path_trace_scene.h"

namespace Comet {
    class AssetRegistry;
    class Renderer;
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
            Comet::PhysicsService::MeshSource meshes = {},
            Comet::PathTraceSceneBuilder::Images images = {});
        ~Viewport();

        Viewport(const Viewport&) = delete;
        Viewport& operator=(const Viewport&) = delete;

        [[nodiscard]] ViewportPanel& panel() { return m_panel; }
        void cancel_interaction();

        void update_texture();
        // 主线程在构建面板前消费上一帧结果；场景切换和失效实体不改变选择。
        void consume_pick(Comet::Scene* scene);
        // 面板命令处理后、SceneExtractor 提取前调用。
        Comet::Result<void, Comet::Error> update(Comet::Scene* scene);
        void submit_feedback(Comet::Scene* scene);

    private:
        void focus_selection(Comet::Scene* scene);
        void append_selection_bounds(Comet::Scene& scene);

        EditorState& m_state;
        SelectionService& m_selection;
        Comet::Renderer& m_renderer;
        Comet::AssetRegistry& m_assets;
        Ui::ImGuiContext& m_ui;
        TransformGizmo m_gizmo;
        ViewportPanel m_panel;
        Comet::LineDrawList m_feedback_lines;
        ColliderOverlay m_colliders;
        uint64_t m_path_trace_generation = 0;
    };
}
