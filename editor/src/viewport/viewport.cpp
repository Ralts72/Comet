#include "viewport/viewport.h"
#include "graphics/resource/sampler.h"

#include "asset/registry.h"
#include "diagnostics/logger.h"
#include "render/renderer.h"
#include "render/resource/mesh.h"
#include "scene/selection.h"
#include "ui/imgui_context.h"

#include <algorithm>
#include <utility>

namespace CometEditor {
    Viewport::Viewport(EditorState& state, const Comet::SceneRuntime& runtime,
        SelectionService& selection, CommandHistory& history,
        const Comet::ComponentRegistry& components, PropertyEditTransaction& inspector_edit,
        const EditorShortcuts& shortcuts, Comet::Renderer& renderer, Comet::AssetRegistry& assets,
        ImGuiContext& ui, std::shared_ptr<Comet::Sampler> sampler)
        : m_state(state), m_selection(selection), m_renderer(renderer), m_assets(assets), m_ui(ui),
          m_sampler(std::move(sampler)), m_gizmo(history, components),
          m_panel(state, runtime, selection, m_gizmo, inspector_edit,
              std::min(renderer.max_render_target_dimension(), std::uint32_t{4096}), shortcuts) {
        if(!m_sampler)
            LOG_FATAL("Viewport requires a prepared sampler");
    }

    void Viewport::update_texture() {
        const auto frame = m_renderer.get_offscreen_frame();
        m_ui.set_viewport_image(frame.slot, frame.color_view, m_sampler);
        m_panel.set_texture_id(
            m_ui.get_viewport_texture_id(frame.slot), frame.size.x, frame.size.y);
    }

    Comet::Result<void, Comet::Error> Viewport::update(Comet::Scene* scene) {
        if(m_state.mode == EditorMode::Edit) {
            if(const auto projection = m_panel.take_projection_request())
                m_state.camera.projection = *projection;
            if(const auto input = m_panel.take_camera_input())
                apply_editor_camera_input(m_state.camera, *input);
        }
        if(m_panel.take_focus_request() && m_state.mode == EditorMode::Edit)
            focus_selection(scene);
        if(auto view = m_renderer.set_render_view(make_render_view(
               m_state, m_panel.is_visible(), m_panel.get_requested_render_size()));
            !view)
            return Comet::Result<void, Comet::Error>::failure(view.error().as_error());
        m_panel.draw_gizmo();
        return Comet::Result<void, Comet::Error>::success();
    }

    void Viewport::focus_selection(Comet::Scene* scene) {
        const auto entity = m_selection.get_selected_entity();
        if(!scene || !scene->is_valid(entity)
            || !entity.has_component<Comet::MeshRendererComponent>())
            return;
        const auto mesh = m_assets.resolve<Comet::Mesh>(
            entity.get_component<Comet::MeshRendererComponent>().mesh);
        if(!mesh)
            return;
        const auto bounds =
            Comet::transform_box(mesh->get_local_bounds(), scene->get_world_matrix(entity));
        const auto resolution = m_panel.get_layout().image_resolution;
        if(bounds && resolution.x > 0 && resolution.y > 0)
            focus_editor_camera(
                m_state.camera, *bounds, static_cast<float>(resolution.x) / resolution.y);
    }

    void Viewport::submit_selection_bounds(Comet::Scene* scene) {
        if(m_state.mode != EditorMode::Edit || !m_panel.is_visible())
            return;
        const auto entity = m_selection.get_selected_entity();
        if(!scene || !scene->is_valid(entity)
            || !entity.has_component<Comet::MeshRendererComponent>())
            return;
        const auto mesh = m_assets.resolve<Comet::Mesh>(
            entity.get_component<Comet::MeshRendererComponent>().mesh);
        if(!mesh)
            return;
        Comet::LineDrawList lines;
        if(lines.add_box(mesh->get_local_bounds(), scene->get_world_matrix(entity),
               Comet::Math::Vec4(1.0f, 0.65f, 0.1f, 1.0f)))
            m_renderer.submit_lines(lines);
    }

    void Viewport::submit_feedback(Comet::Scene* scene) {
        if(m_state.mode != EditorMode::Edit || !m_panel.is_visible())
            return;
        if(const auto pixel = m_panel.take_pick_request()) {
            m_renderer.request_viewport_pick(*pixel, m_panel.get_layout().image_resolution);
            // 本帧有拾取时，由结果回调提交新选择的框，不先画旧选择。
            return;
        }
        submit_selection_bounds(scene);
    }

    void Viewport::apply_pick(const std::optional<Comet::ScenePickHit> hit, Comet::Scene* scene) {
        if(m_state.mode != EditorMode::Edit)
            return;
        if(hit)
            m_selection.select_entity(hit->entity_id);
        else
            m_selection.clear();
        submit_selection_bounds(scene);
    }
}
