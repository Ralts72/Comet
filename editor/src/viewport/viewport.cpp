#include "viewport/viewport.h"
#include "viewport/physics_overlay.h"
#include "render/path_trace_denoise.h"

#include "asset/registry.h"
#include "asset/import/image_encoding.h"
#include "common/file_io.h"
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
        Ui::ImGuiContext& ui, Comet::TaskScheduler& scheduler,
        Comet::PhysicsService::MeshSource meshes, Comet::PathTraceSceneBuilder::Images images)
        : m_state(state), m_selection(selection), m_renderer(renderer), m_assets(assets), m_ui(ui),
          m_gizmo(history, components),
          m_panel(state, runtime, selection, m_gizmo, inspector_edit,
              std::min(renderer.max_render_target_dimension(), std::uint32_t{4096}), shortcuts),
          m_colliders(meshes), m_denoise(std::make_unique<PathTraceDenoise>(renderer, scheduler)) {
        renderer.set_path_trace_sources(
            std::move(meshes.read), std::move(meshes.revision), std::move(images));
    }

    Viewport::~Viewport() {
        m_renderer.set_path_trace_sources({});
    }

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
        auto parameters =
            make_render_view(m_state, m_panel.is_visible(), m_panel.get_requested_render_size());
        parameters.path_trace_enabled = scene && m_panel.is_visible()
                                        && m_panel.path_trace_enabled()
                                        && m_state.mode == EditorMode::Edit;
        parameters.path_trace_settings = m_panel.path_trace_settings();
        parameters.path_trace_settings.denoised_view = m_panel.path_trace_denoise_enabled();
        if(m_panel.take_path_trace_restart())
            ++m_path_trace_generation;
        parameters.path_trace_settings.reset_generation = m_path_trace_generation;
        m_panel.set_path_trace_status(m_renderer.path_trace_samples(),
            parameters.path_trace_enabled ? m_renderer.path_trace_error() : std::string{});
        if(auto view = m_renderer.set_render_view(std::move(parameters)); !view)
            return Comet::Result<void, Comet::Error>::failure(view.error().as_error());
        m_denoise->update(scene && m_state.mode == EditorMode::Edit && m_panel.is_visible()
                              && m_panel.path_trace_enabled()
                              && m_panel.path_trace_denoise_enabled(),
            m_panel.path_trace_settings().samples, scene ? scene->get_lifetime() : 0);
        m_panel.set_path_trace_denoise_status(
            m_denoise->busy(), m_denoise->image() != nullptr, m_denoise->status());
        update_texture();
        m_panel.draw_gizmo();
        return Comet::Result<void, Comet::Error>::success();
    }

    Comet::Result<void, Comet::Error> Viewport::export_path_trace(
        const std::filesystem::path& path) {
        using Exported = Comet::Result<void, Comet::Error>;
        if(m_state.mode != EditorMode::Edit || !m_panel.path_trace_enabled()
            || !m_renderer.path_trace_error().empty())
            return Exported::failure({"当前没有可导出的路径追踪预览"});
        const auto extension = path.extension().string();
        if(extension != ".png" && extension != ".hdr")
            return Exported::failure({"请选择 .png 或 .hdr 文件"});
        auto image = [&]() -> Comet::Result<Comet::TextureData, Comet::GraphicsError> {
            if(!m_panel.path_trace_export_denoised())
                return m_renderer.capture_path_trace();
            const auto* denoised = m_denoise->image();
            if(!denoised)
                return Comet::Result<Comet::TextureData, Comet::GraphicsError>::failure(
                    {"降噪结果已失效或尚未完成，请等待当前采样和降噪完成"});
            return Comet::Result<Comet::TextureData, Comet::GraphicsError>::success(*denoised);
        }();
        if(!image)
            return Exported::failure(image.error().as_error());
        auto encoded = Comet::encode_image(
            image.value(), extension, m_selection.get_scene().get_post_process().exposure);
        if(!encoded)
            return Exported::failure({encoded.error()});
        auto saved =
            Comet::write_binary_file_atomic(path, std::as_bytes(std::span(encoded.value())));
        if(!saved)
            return Exported::failure({saved.error()});
        return Exported::success();
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
