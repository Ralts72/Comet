#include "viewport/viewport.h"
#include "render/resource/vector_field.h"
#include "assets/terrain_editing.h"
#include "animation/retarget_preview.h"
#include "viewport/physics_overlay.h"

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
        void for_selected_bounds(Comet::Scene& scene, const SelectionService& selection,
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
                const auto visit_local = [&](const Comet::BoundingBox& local) {
                    if(const auto world =
                            Comet::transform_box(local, scene.get_world_matrix(entity)))
                        visit(*world);
                };
                if(entity.has_component<Comet::MeshRendererComponent>()) {
                    const auto mesh = assets.resolve<Comet::Mesh>(
                        entity.get_component<Comet::MeshRendererComponent>().mesh);
                    if(mesh)
                        visit_local(mesh->get_local_bounds());
                }
                if(entity.has_component<Comet::DecalComponent>()) {
                    const auto extent = entity.get_component<Comet::DecalComponent>().half_extents;
                    visit_local(Comet::BoundingBox{-extent, extent});
                }
                if(entity.has_component<Comet::IrradianceVolumeComponent>()) {
                    const auto center = Comet::Math::Vec3(scene.get_world_matrix(entity)[3]);
                    const auto extent =
                        entity.get_component<Comet::IrradianceVolumeComponent>().half_extents;
                    visit(Comet::BoundingBox{center - extent, center + extent});
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
        Ui::ImGuiContext& ui, Comet::PhysicsService::MeshSource meshes,
        EditorAssets* editable_assets)
        : m_state(state), m_selection(selection), m_renderer(renderer), m_assets(assets), m_ui(ui),
          m_gizmo(history, components),
          m_panel(state, runtime, selection, m_gizmo, inspector_edit,
              std::min(renderer.max_render_target_dimension(), std::uint32_t{4096}), shortcuts),
          m_colliders(std::move(meshes)) {
        if(editable_assets) {
            m_terrain = std::make_unique<TerrainEditing>(*editable_assets, history, components);
            m_panel.set_terrain_editing(m_terrain.get());
            m_retarget = std::make_unique<RetargetPreview>(*editable_assets);
        }
    }

    Viewport::~Viewport() = default;

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
            if(m_retarget)
                m_retarget->update();
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
        parameters.irradiance_debug = m_panel.irradiance_debug();
        if(m_panel.take_path_trace_restart())
            ++m_path_trace_generation;
        parameters.path_trace_settings.reset_generation = m_path_trace_generation;
        m_panel.set_path_trace_status(m_renderer.path_trace_samples(),
            parameters.path_trace_enabled ? m_renderer.path_trace_error() : std::string{});
        if(auto view = m_renderer.set_render_view(std::move(parameters)); !view)
            return Comet::Result<void, Comet::Error>::failure(view.error().as_error());
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
        auto image = m_renderer.capture_path_trace();
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
        for_selected_bounds(*scene, m_selection, m_assets, [&](const Comet::BoundingBox& world) {
            if(bounds)
                bounds->include(world);
            else
                bounds = world;
        });
        const auto resolution = m_panel.get_layout().image_resolution;
        if(bounds && resolution.x > 0 && resolution.y > 0)
            focus_editor_camera(
                m_state.camera, *bounds, static_cast<float>(resolution.x) / resolution.y);
    }

    void Viewport::append_selection_bounds(Comet::Scene& scene) {
        if(&m_selection.get_scene() != &scene)
            return;
        for_selected_bounds(scene, m_selection, m_assets, [&](const Comet::BoundingBox& world) {
            static_cast<void>(m_feedback_lines.add_box(
                world, Comet::Math::Mat4(1), Comet::Math::Vec4(1.0f, 0.65f, 0.1f, 1.0f)));
        });
        for(const auto uuid : m_selection.get_selected_entities()) {
            const auto entity = scene.find_entity(uuid);
            if(!entity)
                continue;
            const auto& world = scene.get_world_matrix(entity);
            if(entity.has_component<Comet::ParticleEmitterComponent>()) {
                const auto& emitter = entity.get_component<Comet::ParticleEmitterComponent>();
                const auto origin = Comet::Math::Vec3(world[3]);
                static_cast<void>(m_feedback_lines.add_line(
                    origin, origin + emitter.wind * .1f, {.2f, .7f, 1, 1}));
                if(emitter.vortex_strength != 0) {
                    static_cast<void>(m_feedback_lines.add_sphere(emitter.vortex_radius,
                        Comet::Math::translate(Comet::Math::Mat4(1), emitter.vortex_center),
                        {.4f, .7f, 1, 1}));
                    static_cast<void>(m_feedback_lines.add_line(emitter.vortex_center,
                        emitter.vortex_center
                            + glm::normalize(emitter.vortex_axis) * emitter.vortex_radius,
                        {.4f, .7f, 1, 1}));
                }
                if(const auto field = m_assets.resolve<Comet::VectorField>(emitter.vector_field)) {
                    const auto& data = field->data;
                    static_cast<void>(m_feedback_lines.add_box(data.bounds, {.1f, 1, .6f, 1}));
                    // 固定 5³ 可视采样，不随资产分辨率膨胀。
                    for(int z = 0; z < 5; ++z)
                        for(int y = 0; y < 5; ++y)
                            for(int x = 0; x < 5; ++x) {
                                const auto point =
                                    data.bounds.minimum
                                    + data.bounds.size() * Comet::Math::Vec3(x, y, z) * .25f;
                                auto direction =
                                    data.sample(point) * emitter.vector_field_strength * .1f;
                                const float length = glm::length(direction),
                                            limit = glm::length(data.bounds.size()) * .05f;
                                if(length > limit)
                                    direction *= limit / length;
                                static_cast<void>(m_feedback_lines.add_line(
                                    point, point + direction, {.1f, 1, .6f, 1}));
                            }
                }
            }
            if(entity.has_component<Comet::NavigationObstacleComponent>()) {
                const auto& obstacle = entity.get_component<Comet::NavigationObstacleComponent>();
                static_cast<void>(m_feedback_lines.add_box(
                    {-obstacle.half_extents, obstacle.half_extents}, world, {1, .3f, .1f, 1}));
            }
            if(entity.has_component<Comet::NavigationLinkComponent>()) {
                const auto& link = entity.get_component<Comet::NavigationLinkComponent>();
                const auto start = Comet::Math::Vec3(world[3]);
                const auto end = Comet::Math::Vec3(world * Comet::Math::Vec4(link.end, 1));
                static_cast<void>(m_feedback_lines.add_line(start, end, {0, 1, .6f, 1}));
                for(const auto point : {start, end}) {
                    const auto extent = Comet::Math::Vec3(link.radius, .05f, link.radius);
                    static_cast<void>(m_feedback_lines.add_box(
                        {point - extent, point + extent}, Comet::Math::Mat4(1), {0, 1, .6f, 1}));
                }
            }
        }
    }

    void Viewport::submit_feedback(Comet::Scene* scene) {
        m_feedback_lines.clear();
        if(!scene || !m_panel.is_visible()) {
            m_colliders.clear();
            m_renderer.cancel_viewport_pick();
            return;
        }
        if(m_panel.irradiance_debug() != Comet::IrradianceDebugMode::Off)
            scene->each<const Comet::IrradianceVolumeComponent>(
                [&](Comet::Entity entity, const Comet::IrradianceVolumeComponent& volume) {
                    const auto center = Comet::Math::Vec3(scene->get_world_matrix(entity)[3]);
                    const auto color = volume.enabled ? Comet::Math::Vec4(.1f, .8f, 1, 1)
                                                      : Comet::Math::Vec4(.4f, .4f, .4f, 1);
                    static_cast<void>(m_feedback_lines.add_box(
                        {center - volume.half_extents, center + volume.half_extents},
                        Comet::Math::Mat4(1), color));
                });
        if(m_state.mode == EditorMode::Edit) {
            if(const auto pixel = m_panel.take_pick_request())
                m_renderer.request_viewport_pick(
                    *pixel, m_panel.get_layout().image_resolution, scene->get_lifetime());
            append_selection_bounds(*scene);
            if(m_retarget)
                m_retarget->append(m_feedback_lines, m_state.camera.target);
            if(m_terrain && m_panel.terrain_tool())
                m_terrain->append(*scene, m_feedback_lines);
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
