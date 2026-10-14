#include "viewport/viewport.h"
#include "viewport/physics_overlay.h"

#include "asset/registry.h"
#include "diagnostics/logger.h"
#include "render/renderer.h"
#include "render/resource/mesh.h"
#include "scene/selection.h"
#include "scene/entity_tree.h"
#include "ui/imgui_context.h"

#include <algorithm>
#include <utility>

namespace CometEditor {
    namespace {
        template<typename Function>
        void for_selected_meshes(Comet::Scene& scene, const SelectionService& selection,
            const Comet::AssetRegistry& assets, Function&& visit) {
            if(&selection.get_scene() != &scene)
                return;
            const auto roots = Comet::EntityTrees::roots(scene, selection.get_selected_entities());
            if(!roots)
                return;
            std::vector<Comet::Entity> pending;
            for(const auto uuid : *roots)
                pending.push_back(scene.find_entity(uuid));
            for(std::size_t i = 0; i < pending.size(); ++i) {
                const auto entity = pending[i];
                if(entity.has_component<Comet::MeshRendererComponent>()) {
                    const auto mesh = assets.resolve<Comet::Mesh>(
                        entity.get_component<Comet::MeshRendererComponent>().mesh);
                    if(mesh)
                        visit(entity, *mesh);
                }
                const auto children = scene.get_children(entity);
                pending.insert(pending.end(), children.begin(), children.end());
            }
        }
    }

    Viewport::Viewport(EditorState& state, const Comet::SceneRuntime& runtime,
        SelectionService& selection, CommandHistory& history,
        const Comet::ComponentRegistry& components, PropertyEditTransaction& inspector_edit,
        const EditorShortcuts& shortcuts, Comet::Renderer& renderer, Comet::AssetRegistry& assets,
        Ui::ImGuiContext& ui, Comet::PhysicsService::MeshSource meshes)
        : m_state(state), m_selection(selection), m_renderer(renderer), m_assets(assets), m_ui(ui),
          m_gizmo(history, components),
          m_panel(state, runtime, selection, m_gizmo, inspector_edit,
              std::min(renderer.max_render_target_dimension(), std::uint32_t{4096}), shortcuts),
          m_colliders(std::move(meshes)) {}

    void Viewport::update_texture() {
        const auto frame = m_renderer.get_offscreen_frame();
        m_ui.set_viewport_image(frame.slot, frame.color_view);
        m_panel.set_texture_id(
            m_ui.get_viewport_texture_id(frame.slot), frame.size.x, frame.size.y);
    }

    void Viewport::cancel_interaction() {
        m_renderer.cancel_viewport_pick();
        m_panel.cancel_interaction();
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
        update_texture();
        m_panel.draw_gizmo();
        return Comet::Result<void, Comet::Error>::success();
    }

    void Viewport::focus_selection(Comet::Scene* scene) {
        if(!scene)
            return;
        std::optional<Comet::BoundingBox> bounds;
        for_selected_meshes(
            *scene, m_selection, m_assets, [&](Comet::Entity entity, const Comet::Mesh& mesh) {
                const auto world =
                    Comet::transform_box(mesh.get_local_bounds(), scene->get_world_matrix(entity));
                if(!world)
                    return;
                if(bounds)
                    bounds->include(*world);
                else
                    bounds = world;
            });
        const auto resolution = m_panel.get_layout().image_resolution;
        if(bounds && resolution.x > 0 && resolution.y > 0)
            focus_editor_camera(
                m_state.camera, *bounds, static_cast<float>(resolution.x) / resolution.y);
    }

    void Viewport::append_selection_bounds(Comet::Scene& scene) {
        for_selected_meshes(
            scene, m_selection, m_assets, [&](Comet::Entity entity, const Comet::Mesh& mesh) {
                static_cast<void>(m_feedback_lines.add_box(mesh.get_local_bounds(),
                    scene.get_world_matrix(entity), Comet::Math::Vec4(1.0f, 0.65f, 0.1f, 1.0f)));
            });
    }

    void Viewport::submit_feedback(Comet::Scene* scene) {
        m_feedback_lines.clear();
        if(!scene || !m_panel.is_visible()) {
            m_colliders.clear();
            m_renderer.cancel_viewport_pick();
            return;
        }
        if(m_state.mode == EditorMode::Edit) {
            if(const auto pixel = m_panel.take_pick_request())
                m_renderer.request_viewport_pick(
                    *pixel, m_panel.get_layout().image_resolution, scene->get_lifetime());
            append_selection_bounds(*scene);
        } else {
            m_renderer.cancel_viewport_pick();
        }
        if(m_panel.shows_colliders())
            m_colliders.append(*scene, m_feedback_lines);
        else
            m_colliders.clear();
        if(m_panel.shows_joints())
            append_joint_anchors(*scene, m_feedback_lines);
        m_renderer.submit_lines(m_feedback_lines);
    }

    void Viewport::consume_pick(Comet::Scene* scene) {
        const auto result = m_renderer.take_viewport_pick_result();
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
