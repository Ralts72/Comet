#include "viewport/viewport.h"

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
        Ui::ImGuiContext& ui, std::shared_ptr<Comet::Sampler> sampler, const uint32_t scene_view)
        : m_state(state), m_selection(selection), m_renderer(renderer), m_assets(assets), m_ui(ui),
          m_sampler(std::move(sampler)), m_scene_view(scene_view), m_gizmo(history, components),
          m_panel(state, selection, m_gizmo, inspector_edit,
              std::min(renderer.max_render_target_dimension(), std::uint32_t{4096}), shortcuts),
          m_game_panel(state, runtime,
              std::min(renderer.max_render_target_dimension(), std::uint32_t{4096})) {
        if(!m_sampler)
            LOG_FATAL("Viewport requires a prepared sampler");
    }

    void Viewport::update_texture() {
        const auto bind = [&](ViewportPanel& panel, const uint32_t id) {
            const auto frame = m_renderer.get_offscreen_frame(id);
            m_ui.set_viewport_image(frame.slot, frame.color_view, m_sampler, id);
            panel.set_texture_id(
                m_ui.get_viewport_texture_id(frame.slot, id), frame.size.x, frame.size.y);
        };
        bind(m_panel, m_scene_view);
        bind(m_game_panel, Comet::Renderer::DEFAULT_VIEW);
    }

    void Viewport::cancel_interaction() {
        m_renderer.cancel_viewport_pick(m_scene_view);
        m_panel.cancel_interaction();
        m_game_panel.cancel_interaction();
    }

    Comet::Result<void, Comet::Error> Viewport::update(Comet::Scene* scene) {
        if(const auto projection = m_panel.take_projection_request())
            m_state.camera.projection = *projection;
        if(const auto input = m_panel.take_camera_input())
            apply_editor_camera_input(m_state.camera, *input);
        if(m_panel.take_focus_request() && m_state.mode == EditorMode::Edit)
            focus_selection(scene);
        if(auto view = m_renderer.set_render_view(
               make_render_view(m_state, m_panel.is_visible(), m_panel.get_requested_render_size()),
               m_scene_view);
            !view)
            return Comet::Result<void, Comet::Error>::failure(view.error().as_error());
        if(auto view = m_renderer.set_render_view({.visible = m_game_panel.is_visible(),
               .render_size = m_game_panel.get_requested_render_size()});
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
            m_renderer.submit_lines(lines, m_scene_view);
    }

    void Viewport::submit_feedback(Comet::Scene* scene) {
        if(!scene || m_state.mode != EditorMode::Edit || !m_panel.is_visible()) {
            m_renderer.cancel_viewport_pick(m_scene_view);
            return;
        }
        if(const auto pixel = m_panel.take_pick_request()) {
            m_renderer.request_viewport_pick(
                *pixel, m_panel.get_layout().image_resolution, scene->get_lifetime(), m_scene_view);
        }
        submit_selection_bounds(scene);
    }

    void Viewport::consume_pick(Comet::Scene* scene) {
        const auto result = m_renderer.take_viewport_pick_result(m_scene_view);
        if(!result || !scene || result->scene_lifetime != scene->get_lifetime()
            || &m_selection.get_scene() != scene || m_state.mode != EditorMode::Edit
            || !m_panel.is_visible())
            return;
        if(result->hit && !scene->find_entity(result->hit->entity_id))
            return;
        if(result->hit)
            m_selection.select_entity(result->hit->entity_id);
        else
            m_selection.clear();
    }
}
