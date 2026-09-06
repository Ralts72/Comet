#include "runtime/entry.h"
#include "asset/asset_manager.h"
#include "asset/registry.h"
#include "asset/source_monitor.h"
#include "src/camera_controller.h"
#include "src/command_history.h"
#include "src/scene_commands.h"
#include "src/editor_scene_session.h"
#include "src/editor_state.h"
#include "src/imgui_context.h"
#include "src/property_editor_registry.h"
#include "src/scene_document.h"
#include "src/shader_reload.h"
#include "core/engine.h"
#include "core/project_paths.h"
#include "render/renderer.h"
#include "render/resource/mesh.h"
#include "render/scene/scene_renderer.h"
#include "core/window.h"
#include "diagnostics/logger.h"
#include "menu_bar.h"
#include "src/panels/console.h"
#include "src/panels/inspector.h"
#include "src/panels/project.h"
#include "src/panels/view.h"
#include "src/panels/hierarchy.h"
#include "src/panels/render_stats.h"
#include "common/file_io.h"
#include "src/selection.h"
#include "src/transform_gizmo.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <imgui.h>
#include <spdlog/sinks/callback_sink.h>

namespace {
    constexpr std::size_t SCENE_PATH_CAPACITY = 1024;
    constexpr std::uint32_t EDITOR_VIEWPORT_MAX_RENDER_DIMENSION = 4096;
    const std::filesystem::path DEMO_MESH = "meshes/cube.gltf";
    const std::filesystem::path DEMO_MATERIAL = "materials/pbr.mat";

    struct EditorRenderAssets {
        Comet::AssetHandle mesh;
        Comet::AssetHandle material;
    };

    enum class SceneFileDialog { None, Open, Save };

    void log_asset_scan_issues(const Comet::AssetScanReport& report) {
        for(const Comet::AssetScanIssue& issue : report.issues) {
            LOG_WARN("Asset scan issue at '{}': {}", issue.path.generic_string(),
                issue.message);
        }
    }

    Comet::AssetHandle load_required_material(
        Comet::AssetManager& asset_manager, const std::filesystem::path& relative_path) {
        const Comet::AssetRecord* record =
            asset_manager.get_database().find(relative_path);
        if(!record) {
            LOG_FATAL("Required material asset '{}' is not indexed",
                relative_path.generic_string());
        }

        if(!asset_manager.load_material(record->handle)) {
            LOG_FATAL("Failed to load required material asset '{}'",
                relative_path.generic_string());
        }
        return record->handle;
    }

    Comet::AssetHandle load_required_mesh(
        Comet::AssetManager& asset_manager, const std::filesystem::path& relative_path) {
        const Comet::AssetRecord* record =
            asset_manager.get_database().find(relative_path);
        if(!record) {
            LOG_FATAL("Required mesh asset '{}' is not indexed",
                relative_path.generic_string());
        }

        if(!asset_manager.import_mesh(record->handle)
            || !asset_manager.load_mesh(record->handle)) {
            LOG_FATAL("Failed to load required mesh asset '{}'",
                relative_path.generic_string());
        }
        return record->handle;
    }

    std::unique_ptr<Comet::Scene> create_editor_scene(const EditorRenderAssets& assets) {
        auto scene = std::make_unique<Comet::Scene>();
        Comet::Entity main_camera = scene->create_entity("Main Camera");
        main_camera.get_component<Comet::TransformComponent>().translation.z = 3.0f;
        main_camera.add_component<Comet::CameraComponent>().primary = true;

        Comet::Entity cube = scene->create_entity("Editor Cube");
        auto& transform = cube.get_component<Comet::TransformComponent>();
        transform.rotation = Comet::Math::Vec3(-20.0f, 30.0f, 0.0f);
        cube.add_component<Comet::MeshRendererComponent>(assets.mesh, assets.material);
        auto light = scene->create_entity("Key Light");
        light.get_component<Comet::TransformComponent>().rotation = {-30, -35, 0};
        auto& light_component = light.add_component<Comet::LightComponent>();
        light_component.intensity = 4.0f;
        light_component.casts_shadow = true;
        auto ground = scene->create_entity("Ground");
        auto& ground_transform = ground.get_component<Comet::TransformComponent>();
        ground_transform.translation.y = -0.8f;
        ground_transform.scale = {4.0f, 0.1f, 4.0f};
        ground.add_component<Comet::MeshRendererComponent>(assets.mesh, assets.material);

        return scene;
    }

    class Editor final: public Comet::Application {
    public:
        void on_init() override {
            LOG_INFO("Editor initializing...");

            auto& engine = get_engine();
            auto& renderer = engine.get_renderer();
            auto& render_context = renderer.get_render_context();
            auto& scene_renderer = renderer.get_scene_renderer();

            const auto& swapchain = render_context.get_swapchain();
            renderer.enable_offscreen_rendering(
                Comet::Math::Vec2u(swapchain.get_width(), swapchain.get_height()));

            m_imgui_context =
                std::make_unique<CometEditor::ImGuiContext>(engine.get_window(),
                    render_context, m_project_paths.editor_state() / "imgui.ini");

            m_console_panel = std::make_shared<CometEditor::ConsolePanel>();
            setup_log_redirect();
            const std::filesystem::path shader_directory(COMET_ENGINE_SHADER_DIRECTORY);
            m_material_shader_reload = std::make_unique<CometEditor::ShaderReload>(
                engine.get_task_scheduler(),
                CometEditor::ShaderReload::Requests{
                    {"material_mesh", {.source = shader_directory / "material_mesh.vert",
                                          .stage = Comet::ShaderCompiler::Stage::Vertex}},
                    {"material_textured",
                        {.source = shader_directory / "material_textured.frag",
                            .stage = Comet::ShaderCompiler::Stage::Fragment}},
                    {"material_solid",
                        {.source = shader_directory / "material_solid.frag",
                            .stage = Comet::ShaderCompiler::Stage::Fragment}},
                    {"material_lit_vert",
                        {.source = shader_directory / "material_lit.vert",
                            .stage = Comet::ShaderCompiler::Stage::Vertex}},
                    {"material_lit_frag",
                        {.source = shader_directory / "material_lit.frag",
                            .stage = Comet::ShaderCompiler::Stage::Fragment}},
                    {"material_pbr_frag",
                        {.source = shader_directory / "material_pbr.frag",
                            .stage = Comet::ShaderCompiler::Stage::Fragment}}});
            m_debug_shader_reload =
                std::make_unique<CometEditor::ShaderReload>(engine.get_task_scheduler(),
                    CometEditor::ShaderReload::Requests{
                        {"debug_line_vert",
                            {.source = shader_directory / "debug_line.vert",
                                .stage = Comet::ShaderCompiler::Stage::Vertex}},
                        {"debug_line_frag",
                            {.source = shader_directory / "debug_line.frag",
                                .stage = Comet::ShaderCompiler::Stage::Fragment}}});

            m_asset_manager = std::make_unique<Comet::AssetManager>(m_project_paths,
                engine.get_asset_registry(), engine.get_resource_manager(),
                engine.get_task_scheduler());
            Comet::AssetScanReport initial_asset_scan = m_asset_manager->scan();
            log_asset_scan_issues(initial_asset_scan);
            m_asset_source_monitor =
                std::make_unique<Comet::AssetSourceMonitor>(m_project_paths.assets());
            handle_asset_source_poll(m_asset_source_monitor->poll());

            const EditorRenderAssets render_assets{
                .mesh = load_required_mesh(*m_asset_manager, DEMO_MESH),
                .material = load_required_material(*m_asset_manager, DEMO_MATERIAL)};
            m_placement_material = render_assets.material;
            engine.set_scene(create_editor_scene(render_assets));
            Comet::Engine* engine_ptr = &engine;
            const auto get_active_scene = [engine_ptr]() {
                return engine_ptr->get_scene();
            };
            const auto replace_active_scene = [this, engine_ptr](
                                                  std::unique_ptr<Comet::Scene> scene) {
                if(scene)
                    prepare_scene_assets(*scene);
                return engine_ptr->replace_scene(std::move(scene));
            };
            m_scene_document = std::make_unique<CometEditor::SceneDocument>(
                m_scene_serializer, get_active_scene, replace_active_scene);
            m_scene_session =
                std::make_unique<CometEditor::EditorSceneSession>(m_editor_state,
                    m_scene_serializer, get_active_scene, replace_active_scene);
            auto& scene = *engine.get_scene();
            m_command_history.bind_scene(&scene);
            m_selection.emplace(scene);
            setup_panels(scene, std::move(initial_asset_scan));

            initialize_viewport_textures(scene_renderer);

            renderer.set_overlay_callbacks(
                [this]() {
                    update_viewport_texture(
                        get_engine().get_renderer().get_scene_renderer());
                    m_imgui_context->update_frame();
                    submit_viewport_feedback();
                },
                [this](Comet::CommandBuffer& command_buffer) {
                    m_imgui_context->render(command_buffer);
                });

            // 注册交换链依赖资源的释放／重建钩子。
            scene_renderer.set_swapchain_resource_callbacks(
                [this]() { m_imgui_context->release_swapchain_resources(); },
                [this](const Comet::SwapchainCompatibility& compatibility) {
                    m_imgui_context->rebuild_swapchain_resources(compatibility);
                });

            renderer.set_viewport_pick_callback(
                [this](const std::optional<Comet::ScenePickHit> hit) {
                    if(!m_selection
                        || m_editor_state.mode != CometEditor::EditorMode::Edit) {
                        return;
                    }
                    if(hit) {
                        m_selection->select_entity(hit->entity_id);
                    } else {
                        m_selection->clear();
                    }
                    submit_selection_bounds();
                });

            LOG_INFO("Editor initialized");
        }

        void on_update(const Comet::UpdateContext context) override {
            if(auto shaders = m_material_shader_reload->update()) {
                try {
                    auto& renderer = get_engine().get_renderer();
                    const auto report =
                        renderer.get_scene_renderer().reload_material_shaders(
                            renderer.get_resource_manager(), *shaders);
                    for(auto& layout :
                        renderer.get_scene_renderer().get_material_layouts())
                        m_inspector_panel->set_material_layout(std::move(layout));
                    LOG_INFO(
                        "Material Shader reload published: {} pipelines, {} material versions, {} new bindings",
                        report.pipelines, report.material_versions,
                        report.material_bindings);
                } catch(const std::exception& error) {
                    LOG_ERROR("Material Shader reload kept previous GPU version: {}",
                        error.what());
                }
            }
            if(auto shaders = m_debug_shader_reload->update()) {
                try {
                    auto& renderer = get_engine().get_renderer();
                    if(renderer.get_scene_renderer().reload_debug_shaders(
                           renderer.get_resource_manager(), *shaders))
                        LOG_INFO("Debug Shader reload published at frame boundary");
                } catch(const std::exception& error) {
                    LOG_ERROR("Debug Shader reload kept previous GPU version: {}",
                        error.what());
                }
            }
            monitor_asset_sources();
            const auto published = m_asset_manager->process_completions();
            if(!published.empty()) {
                if(auto* scene = get_engine().get_scene())
                    prepare_scene_assets(*scene);
            }
            update_project_import_state();
            apply_editor_mode_request();

            m_menu_bar->set_fps(context.fps);
        }

        void prepare_scene_assets(Comet::Scene& scene) {
            std::size_t missing = 0;
            for(const auto& reference :
                m_component_registry.collect_asset_references(scene)) {
                if(!m_asset_manager->ensure_loaded(reference.handle, reference.type))
                    ++missing;
            }
            if(missing != 0)
                LOG_WARN(
                    "Scene has {} unresolved asset references; data is preserved for repair",
                    missing);
        }

        void update_project_import_state() {
            const auto handle = m_selection->get_selected_asset();
            const auto* record = m_asset_manager->get_database().find(handle);
            if(!record || record->type != Comet::AssetType::Mesh)
                return;
            using State = Comet::AssetManager::MeshImportState;
            if(m_asset_manager->get_mesh_import_state(handle) == State::Unknown)
                static_cast<void>(m_asset_manager->inspect_mesh(handle));
            m_project_panel->set_mesh_import_state(
                handle, m_asset_manager->get_mesh_import_state(handle));
        }

        void apply_viewport_camera_updates() {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit) {
                return;
            }

            if(const auto projection = m_viewport_panel->take_projection_request()) {
                m_editor_state.camera.projection = *projection;
            }
            const std::optional<CometEditor::EditorCameraInput> input =
                m_viewport_panel->take_camera_input();
            if(input) {
                CometEditor::apply_editor_camera_input(m_editor_state.camera, *input);
            }
        }

        void update_viewport_state() {
            get_engine().get_renderer().set_render_view(CometEditor::make_render_view(
                m_editor_state, m_viewport_panel->is_visible(),
                m_viewport_panel->get_requested_render_size()));
        }

        void apply_viewport_focus() {
            const bool requested = m_viewport_panel->take_focus_request();
            if(!requested || m_editor_state.mode != CometEditor::EditorMode::Edit
                || !m_selection) {
                return;
            }
            Comet::Entity entity = m_selection->get_selected_entity();
            if(!entity || !entity.has_component<Comet::MeshRendererComponent>()) {
                return;
            }
            const Comet::AssetHandle mesh_handle =
                entity.get_component<Comet::MeshRendererComponent>().mesh;
            const auto mesh =
                get_engine().get_asset_registry().resolve<Comet::Mesh>(mesh_handle);
            Comet::Scene* scene = get_engine().get_scene();
            if(!mesh || !scene) {
                return;
            }
            const auto world_bounds = Comet::transform_box(
                mesh->get_local_bounds(), scene->get_world_matrix(entity));
            const auto resolution = m_viewport_panel->get_layout().image_resolution;
            if(!world_bounds || resolution.x == 0 || resolution.y == 0) {
                return;
            }
            const float aspect = static_cast<float>(resolution.x) / resolution.y;
            CometEditor::focus_editor_camera(
                m_editor_state.camera, *world_bounds, aspect);
        }

        void submit_selection_bounds() {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit
                || !m_viewport_panel->is_visible() || !m_selection) {
                return;
            }
            const Comet::Entity entity = m_selection->get_selected_entity();
            Comet::Scene* scene = get_engine().get_scene();
            if(!scene || !scene->is_valid(entity)
                || !entity.has_component<Comet::MeshRendererComponent>()) {
                return;
            }
            const auto mesh = get_engine().get_asset_registry().resolve<Comet::Mesh>(
                entity.get_component<Comet::MeshRendererComponent>().mesh);
            if(!mesh) {
                return;
            }
            Comet::LineDrawList lines;
            if(lines.add_box(mesh->get_local_bounds(), scene->get_world_matrix(entity),
                   Comet::Math::Vec4(1.0f, 0.65f, 0.1f, 1.0f))) {
                get_engine().get_renderer().submit_lines(lines);
            }
        }

        void submit_viewport_feedback() {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit
                || !m_viewport_panel->is_visible()) {
                return;
            }
            if(const auto pixel = m_viewport_panel->take_pick_request()) {
                get_engine().get_renderer().request_viewport_pick(
                    *pixel, m_viewport_panel->get_layout().image_resolution);
                // 本帧有拾取时，由结果回调提交新选择的框，不先画旧选择。
                return;
            }
            submit_selection_bounds();
        }

        void update_viewport_texture(Comet::SceneRenderer& scene_renderer) {
            auto& renderer = get_engine().get_renderer();
            const uint32_t frame_slot =
                scene_renderer.get_frame_scheduler().get_current_frame_slot_index();
            m_imgui_context->set_viewport_image(frame_slot,
                scene_renderer.get_offscreen_color_view(frame_slot),
                renderer.get_resource_manager()
                    .get_sampler_manager()
                    .get_nearest_clamp());

            const ImTextureID texture_id =
                m_imgui_context->get_viewport_texture_id(frame_slot);
            const Comet::Math::Vec2u size = scene_renderer.get_render_target().get_size();
            m_viewport_panel->set_texture_id(texture_id, size.x, size.y);
        }

        void initialize_viewport_textures(Comet::SceneRenderer& scene_renderer) {
            auto sampler = get_engine()
                               .get_renderer()
                               .get_resource_manager()
                               .get_sampler_manager()
                               .get_nearest_clamp();
            const uint32_t frame_slot_count =
                scene_renderer.get_frame_scheduler().get_frame_slot_count();
            for(uint32_t frame_slot = 0; frame_slot < frame_slot_count; ++frame_slot) {
                m_imgui_context->set_viewport_image(frame_slot,
                    scene_renderer.get_offscreen_color_view(frame_slot), sampler);
            }
        }

        void on_shutdown() override {
            LOG_INFO("Editor shutting down...");
            m_material_shader_reload.reset();
            m_debug_shader_reload.reset();
            get_engine().get_renderer().set_overlay_callbacks({}, {});
            get_engine().get_renderer().set_viewport_pick_callback({});
            auto& scene_renderer = get_engine().get_renderer().get_scene_renderer();
            scene_renderer.set_swapchain_resource_callbacks({}, {});
            m_viewport_panel->cancel_interaction();
            static_cast<void>(m_property_edit.cancel());
            m_command_history.bind_scene(nullptr);
            m_imgui_context.reset();
            m_project_panel.reset();
            m_hierarchy_panel.reset();
            m_inspector_panel.reset();
            m_viewport_panel.reset();
            m_render_stats_panel.reset();
            m_menu_bar.reset();
            m_selection.reset();
            m_scene_session.reset();
            m_scene_document.reset();
            m_asset_source_monitor.reset();
            m_asset_manager.reset();
        }

    private:
        void handle_asset_source_poll(
            const Comet::AssetSourceMonitor::PollResult& result) {
            if(result.state == Comet::AssetSourceMonitor::PollState::NotPolled) {
                return;
            }
            if(result.state != Comet::AssetSourceMonitor::PollState::Failed) {
                m_asset_source_monitor_error.clear();
                return;
            }

            const std::string error =
                result.issue_path.generic_string() + ": " + result.message;
            if(error != m_asset_source_monitor_error) {
                LOG_WARN("Asset source monitor issue at '{}': {}",
                    result.issue_path.generic_string(), result.message);
                m_asset_source_monitor_error = error;
            }
        }

        void acknowledge_generated_metadata(const Comet::AssetScanReport& report) {
            if(report.generated_metadata == 0 || !m_asset_source_monitor) {
                return;
            }
            for(const Comet::AssetHandle handle : report.added_assets) {
                const Comet::AssetRecord* record =
                    m_asset_manager->get_database().find(handle);
                if(record) {
                    static_cast<void>(m_asset_source_monitor->acknowledge(
                        Comet::metadata_path(record->path)));
                }
            }
        }

        Comet::AssetScanReport refresh_project_assets(
            const bool source_state_already_observed) {
            if(!source_state_already_observed && m_asset_source_monitor) {
                handle_asset_source_poll(m_asset_source_monitor->poll_now());
            }

            Comet::AssetScanReport report = m_asset_manager->scan();
            if(report.snapshot_updated && m_inspector_panel) {
                m_inspector_panel->invalidate_asset_cache();
            }
            acknowledge_generated_metadata(report);
            log_asset_scan_issues(report);
            if(report.snapshot_updated) {
                if(auto* scene = get_engine().get_scene())
                    prepare_scene_assets(*scene);
            }
            return report;
        }

        Comet::AssetScanReport move_project_asset(
            const Comet::AssetHandle handle, const std::filesystem::path& destination) {
            const Comet::AssetRecord* old_record =
                m_asset_manager->get_database().find(handle);
            const std::filesystem::path old_path =
                old_record ? old_record->path : std::filesystem::path{};

            Comet::AssetScanReport report =
                m_asset_manager->move_asset(handle, destination);
            if(report.snapshot_updated) {
                const Comet::AssetRecord* new_record =
                    m_asset_manager->get_database().find(handle);
                const std::filesystem::path new_path =
                    new_record ? new_record->path : destination.lexically_normal();
                if(m_asset_source_monitor && !old_path.empty() && new_record) {
                    static_cast<void>(m_asset_source_monitor->acknowledge(old_path));
                    static_cast<void>(m_asset_source_monitor->acknowledge(
                        Comet::metadata_path(old_path)));
                    static_cast<void>(
                        m_asset_source_monitor->acknowledge(new_record->path));
                    static_cast<void>(m_asset_source_monitor->acknowledge(
                        Comet::metadata_path(new_record->path)));
                }
                if(m_inspector_panel) {
                    m_inspector_panel->invalidate_asset_cache();
                }
                LOG_INFO("Moved asset handle {} from '{}' to '{}'", handle.value(),
                    old_path.generic_string(), new_path.generic_string());
            }

            log_asset_scan_issues(report);
            return report;
        }

        void monitor_asset_sources() {
            if(!m_asset_source_monitor || !m_project_panel) {
                return;
            }

            const Comet::AssetSourceMonitor::PollResult result =
                m_asset_source_monitor->poll();
            handle_asset_source_poll(result);
            if(result.state != Comet::AssetSourceMonitor::PollState::Changed) {
                return;
            }

            m_project_panel->update_scan_report(refresh_project_assets(true));
        }

        bool update_material(
            const Comet::AssetHandle handle, const Comet::MaterialData& data) {
            const Comet::AssetRecord* record =
                m_asset_manager->get_database().find(handle);
            const std::filesystem::path relative_path =
                record ? record->path : std::filesystem::path{};
            const bool updated =
                static_cast<bool>(m_asset_manager->update_material(handle, data));
            if(updated && !relative_path.empty()) {
                static_cast<void>(m_asset_source_monitor->acknowledge(relative_path));
            }
            return updated;
        }

        bool reimport_texture(const Comet::AssetHandle handle,
            const Comet::TextureImportSettings settings) {
            const Comet::AssetRecord* record =
                m_asset_manager->get_database().find(handle);
            const std::filesystem::path relative_path =
                record ? record->path : std::filesystem::path{};
            const bool reimported =
                static_cast<bool>(m_asset_manager->reimport_texture(handle, settings));
            if(reimported && !relative_path.empty()) {
                static_cast<void>(m_asset_source_monitor->acknowledge(
                    Comet::metadata_path(relative_path)));
            }
            if(reimported) {
                if(auto* scene = get_engine().get_scene())
                    prepare_scene_assets(*scene);
            }
            return reimported;
        }

        void handle_asset_assignment(
            const CometEditor::InspectorPanel::AssetAssignment& request) {
            auto* scene = get_engine().get_scene();
            if(!scene || request.asset.generation != m_command_history.generation())
                return;
            auto entity = scene->find_entity(request.target.entity);
            const auto* component =
                m_component_registry.find_component(request.target.component);
            const auto* property =
                component ? component->find_property(request.target.property) : nullptr;
            if(!entity || !component || !component->has_component(entity) || !property
                || !property->editable || property->read_only
                || property->type != Comet::PropertyType::AssetHandle
                || property->asset_type != request.asset.type)
                return;
            m_viewport_panel->cancel_interaction();
            if(!m_property_edit.commit())
                return;
            const auto handle = request.asset.handle;
            if(handle && !m_asset_manager->ensure_loaded(handle, request.asset.type)) {
                LOG_WARN("Cannot assign asset {}; previous reference is unchanged",
                    handle.value());
                return;
            }
            if(m_editor_state.mode == CometEditor::EditorMode::Play) {
                if(!property->assign_value(component->get_component(entity), handle))
                    LOG_ERROR("Cannot update runtime asset reference");
                return;
            }
            if(m_command_history.get_scene() != scene
                || !m_property_edit.begin(request.target))
                return;
            if(!m_property_edit.preview(handle)) {
                static_cast<void>(m_property_edit.cancel());
                return;
            }
            if(!m_property_edit.commit())
                LOG_ERROR("Cannot commit asset reference");
        }

        void handle_mesh_drop(const CometEditor::ViewPanel::MeshDrop& request) {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit
                || request.asset.generation != m_command_history.generation())
                return;
            m_viewport_panel->cancel_interaction();
            if(!m_property_edit.commit())
                return;
            const auto& database = m_asset_manager->get_database();
            const auto* mesh = database.find(request.asset.handle);
            const auto* material = database.find(m_placement_material);
            if(!mesh || mesh->type != Comet::AssetType::Mesh || !material
                || material->type != Comet::AssetType::Material) {
                LOG_WARN("Mesh drop requires an indexed mesh and project demo material");
                return;
            }
            // 只消费已发布 Artifact；缺失时由 Project 的显式导入入口处理。
            if(!m_asset_manager->load_mesh(mesh->handle)
                || !m_asset_manager->load_material(material->handle)) {
                LOG_WARN("Cannot place mesh; import it in Project and check Log");
                return;
            }
            const auto uuid = CometEditor::SceneCommands::create_mesh_entity(
                m_command_history, m_component_registry, mesh->path.stem().string(),
                mesh->handle, material->handle, request.position);
            if(uuid)
                m_selection->select_entity(
                    m_command_history.get_scene()->find_entity(uuid).get_id());
            else
                LOG_WARN("Cannot create mesh entity");
        }

        void handle_scene_request(const CometEditor::HierarchyPanel::Request& request) {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit
                || request.generation != m_command_history.generation())
                return;
            m_viewport_panel->cancel_interaction();
            if(!m_property_edit.commit()) {
                LOG_ERROR("Cannot finish property edit before structure command");
                return;
            }
            using Type = CometEditor::HierarchyPanel::Request::Type;
            namespace Commands = CometEditor::SceneCommands;
            bool changed = false;
            switch(request.type) {
                case Type::Create:
                case Type::Duplicate: {
                    Comet::EntityUuid uuid;
                    if(request.type == Type::Create)
                        uuid = Commands::create_entity(
                            m_command_history, m_component_registry);
                    else
                        uuid = Commands::duplicate_entity(
                            m_command_history, m_component_registry, request.entity);
                    changed = static_cast<bool>(uuid);
                    if(changed)
                        m_selection->select_entity(
                            m_command_history.get_scene()->find_entity(uuid).get_id());
                    break;
                }
                case Type::Delete:
                    changed = Commands::delete_entity(
                        m_command_history, m_component_registry, request.entity);
                    if(changed)
                        m_selection->clear();
                    break;
                case Type::Reparent:
                    changed = Commands::reparent_entity(
                        m_command_history, request.entity, request.parent);
                    break;
            }
            if(!changed)
                LOG_WARN("Scene structure request was rejected or had no effect");
        }

        void handle_command(const CometEditor::MenuBar::Command command) {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit) {
                LOG_WARN("Scene commands are disabled in Play mode");
                return;
            }

            m_viewport_panel->cancel_interaction();
            if(!m_property_edit.commit()) {
                LOG_ERROR("Cannot finish property edit before scene command");
                return;
            }

            switch(command) {
                case CometEditor::MenuBar::Command::Undo:
                    if(!m_command_history.undo())
                        LOG_WARN("Cannot undo scene edit");
                    break;
                case CometEditor::MenuBar::Command::Redo:
                    if(!m_command_history.redo())
                        LOG_WARN("Cannot redo scene edit");
                    break;
                case CometEditor::MenuBar::Command::NewScene:
                    if(m_scene_document->create_new()) {
                        bind_active_scene();
                    }
                    break;
                case CometEditor::MenuBar::Command::OpenScene:
                    request_scene_file_dialog(SceneFileDialog::Open);
                    break;
                case CometEditor::MenuBar::Command::SaveScene:
                    if(m_scene_document->get_path().empty()) {
                        request_scene_file_dialog(SceneFileDialog::Save);
                    } else {
                        static_cast<void>(
                            m_scene_document->save(m_scene_document->get_path()));
                    }
                    break;
                default:;
            }
        }

        void bind_active_scene() {
            Comet::Scene* active_scene = get_engine().get_scene();
            if(active_scene == nullptr) {
                LOG_ERROR("Cannot bind editor panels without an active scene");
                return;
            }
            m_selection->set_scene(*active_scene);
            m_hierarchy_panel->set_scene(*active_scene);
            if(m_editor_state.mode == CometEditor::EditorMode::Edit) {
                m_command_history.bind_scene(active_scene);
            } else {
                m_command_history.bind_scene(nullptr);
            }
        }

        void apply_editor_mode_request() {
            if(!m_scene_session) {
                return;
            }

            try {
                if(m_scene_session->apply_mode_request()) {
                    bind_active_scene();
                }
            } catch(const std::exception& error) {
                LOG_ERROR("Failed to change editor mode: {}", error.what());
            }
        }

        void request_scene_file_dialog(const SceneFileDialog dialog) {
            m_scene_file_dialog = dialog;
            m_scene_file_dialog_open_requested = true;
            m_scene_document->clear_error();

            std::string initial_path = m_scene_document->get_path();
            if(dialog == SceneFileDialog::Save && initial_path.empty()) {
                initial_path = std::string(PROJECT_ROOT_DIR) + "/untitled.scene";
            } else if(dialog == SceneFileDialog::Open && initial_path.empty()) {
                initial_path = std::string(PROJECT_ROOT_DIR) + "/";
            }
            m_scene_path_buffer.fill('\0');
            std::copy_n(initial_path.data(),
                std::min(initial_path.size(), m_scene_path_buffer.size() - 1),
                m_scene_path_buffer.data());
        }

        void render_scene_file_dialog() {
            if(m_scene_file_dialog == SceneFileDialog::None) {
                return;
            }

            const bool is_open = m_scene_file_dialog == SceneFileDialog::Open;
            const char* title = is_open ? "Open Scene" : "Save Scene";
            if(m_scene_file_dialog_open_requested) {
                ImGui::OpenPopup(title);
                m_scene_file_dialog_open_requested = false;
            }

            if(!ImGui::BeginPopupModal(
                   title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
                return;
            }

            ImGui::SetNextItemWidth(560.0f);
            const bool submitted = ImGui::InputText("Path", m_scene_path_buffer.data(),
                m_scene_path_buffer.size(), ImGuiInputTextFlags_EnterReturnsTrue);

            const char* action = is_open ? "Open" : "Save";
            if((ImGui::Button(action, ImVec2(100.0f, 0.0f)) || submitted)) {
                const std::string path(m_scene_path_buffer.data());
                const bool succeeded =
                    is_open ? m_scene_document->open(path) : m_scene_document->save(path);
                if(succeeded) {
                    if(is_open) {
                        bind_active_scene();
                    }
                    ImGui::CloseCurrentPopup();
                    m_scene_file_dialog = SceneFileDialog::None;
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel", ImVec2(100.0f, 0.0f))) {
                ImGui::CloseCurrentPopup();
                m_scene_file_dialog = SceneFileDialog::None;
                m_scene_document->clear_error();
            }

            if(!m_scene_document->get_last_error().empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.25f, 0.2f, 1.0f));
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 560.0f);
                ImGui::TextWrapped("%s", m_scene_document->get_last_error().c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
            }
            ImGui::EndPopup();
        }

        void handle_render_diagnostics_requests() {
            auto& renderer = get_engine().get_renderer();
            if(const auto enabled = m_render_stats_panel->take_capture_request())
                renderer.get_scene_renderer().get_diagnostics().set_enabled(*enabled);
            if(m_render_stats_panel->take_allocation_report_request()) {
                try {
                    const auto path = m_project_paths.editor_state() / "diagnostics"
                                      / "gpu-allocations.json";
                    Comet::write_text_file_atomic(path, renderer.get_render_context()
                                                            .get_device()
                                                            .build_allocation_report());
                    LOG_INFO("Saved VMA allocation report to {}", path.string());
                } catch(const std::exception& error) {
                    LOG_ERROR("Failed to save VMA allocation report: {}", error.what());
                }
            }
        }

        void setup_log_redirect() const {
            // 日志改由编辑器面板展示，文件输出不受影响。
            Comet::Logger::remove_console_sinks();

            // 面板必须先创建；弱引用使延迟到达的日志不会访问已销毁的面板。
            const auto gui_sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
                [panel = std::weak_ptr(m_console_panel)](
                    const spdlog::details::log_msg& msg) {
                    if(const auto console = panel.lock()) {
                        const auto level = Comet::log_level_from_spdlog(msg.level);
                        console->add_log(
                            level, std::string(msg.payload.data(), msg.payload.size()));
                    }
                });

            Comet::Logger::add_custom_sink(gui_sink);
        }

        void setup_panels(
            Comet::Scene& scene, Comet::AssetScanReport initial_asset_scan) {
            m_menu_bar =
                std::make_unique<CometEditor::MenuBar>(m_editor_state, m_command_history);
            m_render_stats_panel =
                std::make_unique<CometEditor::RenderStatsPanel>(get_engine());

            m_hierarchy_panel = std::make_unique<CometEditor::HierarchyPanel>(
                scene, *m_selection, m_command_history);
            const auto& render_context = get_engine().get_renderer().get_render_context();
            const std::uint32_t device_max_render_dimension =
                render_context.get_device().get_capability().max_image_dimension_2d;
            if(device_max_render_dimension == 0) {
                LOG_FATAL("Selected Vulkan device has no valid 2D image dimension limit");
            }
            const std::uint32_t max_render_dimension = std::min(
                device_max_render_dimension, EDITOR_VIEWPORT_MAX_RENDER_DIMENSION);
            m_viewport_panel = std::make_unique<CometEditor::ViewPanel>(m_editor_state,
                *m_selection, m_transform_gizmo, m_property_edit, max_render_dimension);
            m_inspector_panel = std::make_unique<CometEditor::InspectorPanel>(
                *m_selection, m_command_history, m_property_edit, m_component_registry,
                m_property_editor_registry, m_asset_manager->get_database(),
                m_project_paths.assets(),
                [this](const Comet::AssetHandle handle, const Comet::MaterialData& data) {
                    return update_material(handle, data);
                },
                [this](const Comet::AssetHandle handle,
                    const Comet::TextureImportSettings settings) {
                    return reimport_texture(handle, settings);
                });
            for(auto& layout :
                get_engine().get_renderer().get_scene_renderer().get_material_layouts())
                m_inspector_panel->set_material_layout(std::move(layout));
            m_project_panel = std::make_unique<CometEditor::ProjectPanel>(
                m_asset_manager->get_database(), std::move(initial_asset_scan),
                [this]() { return refresh_project_assets(false); },
                [this](const Comet::AssetHandle handle,
                    const std::filesystem::path& destination) {
                    return move_project_asset(handle, destination);
                },
                *m_selection, m_command_history);
            m_menu_bar->set_panel_visibility_callback("Hierarchy",
                [this](const bool visible) { m_hierarchy_panel->set_visible(visible); });
            m_menu_bar->set_panel_visibility_callback("Viewport",
                [this](const bool visible) { m_viewport_panel->set_visible(visible); });
            m_menu_bar->set_panel_visibility_callback("Inspector",
                [this](const bool visible) { m_inspector_panel->set_visible(visible); });
            m_menu_bar->set_panel_visibility_callback("Project",
                [this](const bool visible) { m_project_panel->set_visible(visible); });
            m_menu_bar->set_panel_visibility_callback("Log",
                [this](const bool visible) { m_console_panel->set_visible(visible); });
            m_menu_bar->set_panel_visibility_callback(
                "Render Stats",
                [this](
                    const bool visible) { m_render_stats_panel->set_visible(visible); },
                false);

            m_imgui_context->set_ui_callback([this]() {
                constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
                ImGui::DockSpaceOverViewport(
                    0, ImGui::GetMainViewport(), dockspace_flags);

                m_menu_bar->render();
                m_hierarchy_panel->render();
                m_viewport_panel->render();
                m_inspector_panel->render();
                m_project_panel->render();
                m_console_panel->render();
                m_render_stats_panel->render();
                handle_render_diagnostics_requests();
                render_scene_file_dialog();
                m_menu_bar->collect_shortcuts();

                if(const auto handle = m_project_panel->take_mesh_import_request()) {
                    if(!m_asset_manager->import_mesh_async(*handle))
                        LOG_WARN("Mesh import request was not accepted for handle {}",
                            handle->value());
                    update_project_import_state();
                }

                if(const auto command = m_menu_bar->take_command()) {
                    handle_command(*command);
                }
                if(const auto request = m_hierarchy_panel->take_request())
                    handle_scene_request(*request);
                if(const auto request = m_viewport_panel->take_mesh_drop())
                    handle_mesh_drop(*request);
                if(const auto request = m_inspector_panel->take_asset_assignment())
                    handle_asset_assignment(*request);
                if(const auto mode = m_viewport_panel->take_mode_request()) {
                    m_viewport_panel->cancel_interaction();
                    if(m_property_edit.commit()) {
                        m_scene_session->request_mode(*mode);
                    } else {
                        LOG_ERROR("Cannot finish property edit before mode change");
                    }
                }
                apply_viewport_camera_updates();
                apply_viewport_focus();
                update_viewport_state();
                m_viewport_panel->draw_gizmo();
            });
        }

        Comet::ProjectPaths m_project_paths{PROJECT_ROOT_DIR};
        std::unique_ptr<CometEditor::ImGuiContext> m_imgui_context;
        std::unique_ptr<Comet::AssetManager> m_asset_manager;
        Comet::AssetHandle m_placement_material;
        std::unique_ptr<Comet::AssetSourceMonitor> m_asset_source_monitor;
        std::unique_ptr<CometEditor::ShaderReload> m_material_shader_reload;
        std::unique_ptr<CometEditor::ShaderReload> m_debug_shader_reload;
        std::string m_asset_source_monitor_error;
        std::optional<CometEditor::SelectionService> m_selection;
        Comet::ComponentRegistry m_component_registry =
            Comet::create_scene_component_registry();
        CometEditor::CommandHistory m_command_history;
        CometEditor::PropertyEditTransaction m_property_edit{
            m_command_history, m_component_registry};
        CometEditor::TransformGizmo m_transform_gizmo{
            m_command_history, m_component_registry};
        CometEditor::PropertyEditorRegistry m_property_editor_registry =
            CometEditor::create_property_editor_registry();
        Comet::SceneSerializer m_scene_serializer{m_component_registry};
        CometEditor::EditorState m_editor_state;
        std::unique_ptr<CometEditor::SceneDocument> m_scene_document;
        std::unique_ptr<CometEditor::EditorSceneSession> m_scene_session;
        std::array<char, SCENE_PATH_CAPACITY> m_scene_path_buffer{};
        SceneFileDialog m_scene_file_dialog = SceneFileDialog::None;
        bool m_scene_file_dialog_open_requested = false;

        std::unique_ptr<CometEditor::MenuBar> m_menu_bar;
        std::unique_ptr<CometEditor::HierarchyPanel> m_hierarchy_panel;
        std::unique_ptr<CometEditor::ViewPanel> m_viewport_panel;
        std::unique_ptr<CometEditor::InspectorPanel> m_inspector_panel;
        std::unique_ptr<CometEditor::ProjectPanel> m_project_panel;
        std::unique_ptr<CometEditor::RenderStatsPanel> m_render_stats_panel;
        std::shared_ptr<CometEditor::ConsolePanel> m_console_panel;
    };
}

RUN_APP(Editor)
