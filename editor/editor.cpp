#include "runtime/entry.h"
#include "src/editor_assets.h"
#include "src/scene_file_dialog.h"
#include "asset/registry.h"
#include "src/camera_controller.h"
#include "src/command_history.h"
#include "src/scene_commands.h"
#include "src/editor_scene_session.h"
#include "src/editor_state.h"
#include "src/imgui_context.h"
#include "src/property_editor_registry.h"
#include "src/scene_document.h"
#include "src/shortcuts.h"
#include "core/engine.h"
#include "core/project.h"
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
#include "src/selection.h"
#include "src/transform_gizmo.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <utility>
#include <imgui.h>
#include <spdlog/sinks/callback_sink.h>

namespace {
    constexpr std::uint32_t EDITOR_VIEWPORT_MAX_RENDER_DIMENSION = 4096;

    class Editor final: public Comet::Application {
    public:
        explicit Editor(Comet::Project project) : m_project(std::move(project)) {}

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
                    render_context, m_project.paths().editor_state() / "imgui.ini");

            m_console_panel = std::make_shared<CometEditor::ConsolePanel>();
            setup_log_redirect();

            try {
                m_shortcuts = CometEditor::EditorShortcuts::load(
                    std::filesystem::path(COMET_CONFIG_DIRECTORY)
                    / "profiles/editor-dev.yaml");
            } catch(const std::exception& error) {
                LOG_ERROR("{}; using default editor shortcuts", error.what());
            }

            m_assets = std::make_unique<CometEditor::EditorAssets>(m_project.paths(),
                engine.get_asset_registry(), engine.get_resource_manager(),
                engine.get_task_scheduler());
            auto initial_asset_scan = m_assets->refresh();
            m_property_editor_registry =
                CometEditor::create_property_editor_registry(m_assets->database());
            Comet::Engine* engine_ptr = &engine;
            const auto get_active_scene = [engine_ptr]() {
                return engine_ptr->get_scene();
            };
            const auto replace_active_scene = [this, engine_ptr](
                                                  std::unique_ptr<Comet::Scene> scene) {
                if(scene) {
                    static_cast<void>(
                        m_assets->prepare_scene(*scene, m_component_registry));
                }
                return engine_ptr->replace_scene(std::move(scene));
            };
            m_scene_document =
                std::make_unique<CometEditor::SceneDocument>(m_scene_serializer,
                    m_project.paths(), get_active_scene, replace_active_scene);
            LOG_INFO("Opened project '{}' at '{}'", m_project.name(),
                m_project.paths().root().string());
            if(m_project.startup_scene().empty()) {
                if(!m_scene_document->create_new())
                    LOG_FATAL("Cannot create an empty editor scene");
            } else if(!m_scene_document->open(m_project.startup_scene().string())) {
                LOG_WARN(
                    "Default scene could not be opened; starting with an empty scene");
                if(!m_scene_document->create_new())
                    LOG_FATAL("Cannot create an empty editor scene");
            }
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
            if(auto report = m_assets->update())
                apply_asset_scan_report(std::move(*report));
            apply_editor_mode_request();

            m_menu_bar->set_fps(context.fps);
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
            get_engine().get_renderer().set_overlay_callbacks({}, {});
            get_engine().get_renderer().set_viewport_pick_callback({});
            auto& scene_renderer = get_engine().get_renderer().get_scene_renderer();
            scene_renderer.set_swapchain_resource_callbacks({}, {});
            if(m_imgui_context)
                m_imgui_context->set_ui_callback({});
            if(m_viewport_panel)
                m_viewport_panel->cancel_interaction();
            static_cast<void>(m_property_edit.cancel());
            m_command_history.bind_scene(nullptr);
            m_menu_bar.reset();
            m_project_panel.reset();
            m_hierarchy_panel.reset();
            m_inspector_panel.reset();
            m_viewport_panel.reset();
            m_property_editor_registry = {};
            m_imgui_context.reset();
            m_selection.reset();
            m_scene_session.reset();
            m_scene_document.reset();
            m_assets.reset();
            m_console_panel.reset();
        }

    private:
        void apply_asset_scan_report(Comet::AssetScanReport report) {
            if(report.snapshot_updated)
                m_inspector_panel->invalidate_asset_cache();
            m_project_panel->update_scan_report(std::move(report));
        }

        bool finish_active_edit() {
            m_viewport_panel->cancel_interaction();
            if(m_property_edit.commit())
                return true;
            LOG_ERROR("Cannot finish active property edit; editor request rejected");
            return false;
        }

        void handle_scene_request(const CometEditor::HierarchyPanel::Request& request) {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit
                || request.generation != m_command_history.generation()
                || m_command_history.get_scene() != get_engine().get_scene())
                return;
            if(!finish_active_edit())
                return;
            using Type = CometEditor::HierarchyPanel::Request::Type;
            namespace Commands = CometEditor::SceneCommands;
            bool changed = false;
            switch(request.type) {
                case Type::Create:
                case Type::Duplicate: {
                    Comet::EntityUuid uuid;
                    if(request.type == Type::Create)
                        uuid = Commands::create_entity(m_command_history,
                            m_component_registry, "Entity", request.parent);
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

            if(!finish_active_edit())
                return;

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
                    m_scene_file_dialog.request(
                        CometEditor::SceneFileDialog::Action::Open, *m_scene_document,
                        m_project.paths().assets() / "scenes");
                    break;
                case CometEditor::MenuBar::Command::SaveScene:
                    if(m_scene_document->get_path().empty()) {
                        m_scene_file_dialog.request(
                            CometEditor::SceneFileDialog::Action::Save, *m_scene_document,
                            m_project.paths().assets() / "scenes");
                    } else {
                        static_cast<void>(
                            m_scene_document->save(m_scene_document->get_path()));
                    }
                    break;
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

        void setup_log_redirect() const {
            Comet::Logger::remove_console_sinks();

            // 弱引用避免延迟日志访问已销毁的面板。
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
            m_menu_bar = std::make_unique<CometEditor::MenuBar>(
                m_editor_state, m_command_history, m_shortcuts);

            m_hierarchy_panel = std::make_unique<CometEditor::HierarchyPanel>(
                scene, *m_selection, m_command_history, m_editor_state);
            const auto& render_context = get_engine().get_renderer().get_render_context();
            const std::uint32_t device_max_render_dimension =
                render_context.get_device().get_capability().max_image_dimension_2d;
            if(device_max_render_dimension == 0) {
                LOG_FATAL("Selected Vulkan device has no valid 2D image dimension limit");
            }
            const std::uint32_t max_render_dimension = std::min(
                device_max_render_dimension, EDITOR_VIEWPORT_MAX_RENDER_DIMENSION);
            m_viewport_panel = std::make_unique<CometEditor::ViewPanel>(m_editor_state,
                *m_selection, m_transform_gizmo, m_property_edit, max_render_dimension,
                m_shortcuts);
            m_inspector_panel = std::make_unique<CometEditor::InspectorPanel>(
                m_editor_state, *m_selection, m_command_history, m_property_edit,
                m_component_registry, m_property_editor_registry, m_assets->database(),
                m_project.paths().assets(),
                [this](const Comet::AssetHandle handle, const Comet::MaterialData& data) {
                    return m_assets->update_material(handle, data);
                },
                [this](const Comet::AssetHandle handle,
                    const Comet::TextureImportSettings settings) {
                    return m_assets->reimport_texture(handle, settings);
                });
            m_project_panel = std::make_unique<CometEditor::ProjectPanel>(
                m_assets->database(), m_project.paths().assets(),
                std::move(initial_asset_scan),
                [this]() {
                    auto report = m_assets->refresh();
                    apply_asset_scan_report(std::move(report));
                },
                [this](const Comet::AssetHandle handle,
                    const std::filesystem::path& destination) {
                    auto report = m_assets->move(handle, destination);
                    apply_asset_scan_report(report);
                    return report;
                },
                *m_selection, m_command_history);
            m_menu_bar->register_panel(*m_hierarchy_panel);
            m_menu_bar->register_panel(*m_viewport_panel);
            m_menu_bar->register_panel(*m_inspector_panel);
            m_menu_bar->register_panel(*m_project_panel);
            m_menu_bar->register_panel(*m_console_panel);
            m_imgui_context->set_ui_callback([this]() {
                draw_editor_ui();
                process_editor_requests();
                apply_viewport_camera_updates();
                apply_viewport_focus();
                update_viewport_state();
                m_viewport_panel->draw_gizmo();
            });
        }

        void draw_editor_ui() {
            constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockspace_flags);

            m_menu_bar->render();
            m_hierarchy_panel->render();
            m_viewport_panel->render();
            m_inspector_panel->render();
            m_project_panel->render();
            m_console_panel->render();
            if(m_scene_file_dialog.render(*m_scene_document))
                bind_active_scene();
            m_menu_bar->collect_shortcuts();
        }

        void handle_asset_assignment(
            const CometEditor::InspectorPanel::AssetAssignment& request) {
            auto* scene = get_engine().get_scene();
            if(!scene || request.asset.generation != m_command_history.generation()
                || (m_editor_state.mode == CometEditor::EditorMode::Edit
                    && m_command_history.get_scene() != scene))
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
            const auto current = property->copy_value(component->get_component(entity));
            if(!current || std::get<Comet::AssetHandle>(*current) == request.asset.handle)
                return;
            if(!finish_active_edit())
                return;
            if(!m_assets->load_reference(
                   request.asset.handle, request.asset.type, request.asset.revision)) {
                LOG_WARN("Cannot assign asset {}; previous reference is unchanged",
                    request.asset.handle.value());
                return;
            }
            if(m_editor_state.mode == CometEditor::EditorMode::Play) {
                if(!property->assign_value(
                       component->get_component(entity), request.asset.handle))
                    LOG_ERROR("Cannot update runtime asset reference");
                return;
            }
            if(!m_property_edit.apply(request.target, request.asset.handle))
                LOG_ERROR("Cannot commit asset reference");
        }

        void handle_mesh_drop(const CometEditor::ViewPanel::MeshDrop& request) {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit
                || request.asset.generation != m_command_history.generation()
                || !m_command_history.get_scene()
                || m_command_history.get_scene() != get_engine().get_scene())
                return;
            if(!request.asset.handle
                || !m_assets->load_reference(
                    request.asset.handle, Comet::AssetType::Mesh, request.asset.revision))
                return;
            if(!finish_active_edit())
                return;
            const auto* record = m_assets->database().find(request.asset.handle);
            const auto uuid = CometEditor::SceneCommands::create_mesh_entity(
                m_command_history, m_component_registry, record->path.stem().string(),
                request.asset.handle, {}, request.position);
            if(uuid)
                m_selection->select_entity(
                    m_command_history.get_scene()->find_entity(uuid).get_id());
            else
                LOG_ERROR("Cannot create entity for dropped mesh");
        }

        void process_editor_requests() {
            for(const auto& drop : get_engine().get_window().take_file_drops()) {
                // GLFW 与主 ImGui viewport 都使用逻辑坐标，不乘 Retina framebuffer scale。
                const auto origin = ImGui::GetMainViewport()->Pos;
                const auto directory = m_project_panel->file_drop_directory(
                    drop.position + Comet::Math::Vec2(origin.x, origin.y));
                if(directory)
                    apply_asset_scan_report(
                        m_assets->import_files(drop.paths, *directory));
                else
                    LOG_WARN(
                        "Drop external files onto a Project folder or its empty area");
            }
            if(const auto handle = m_project_panel->take_mesh_reimport_request())
                m_assets->request_mesh_reimport(*handle);
            const auto hierarchy_request = m_hierarchy_panel->take_request();
            const auto menu_command = m_menu_bar->take_command();
            const auto mesh_drop = m_viewport_panel->take_mesh_drop();
            const auto asset_assignment = m_inspector_panel->take_asset_assignment();
            const auto mode = m_viewport_panel->take_mode_request();
            // 菜单命令优先，避免同帧场景或历史切换后执行旧编辑请求。
            if(menu_command)
                handle_command(*menu_command);
            else if(hierarchy_request)
                handle_scene_request(*hierarchy_request);
            else if(mesh_drop && !mode)
                handle_mesh_drop(*mesh_drop);
            else if(asset_assignment && !mode)
                handle_asset_assignment(*asset_assignment);
            if(mode) {
                if(finish_active_edit())
                    m_scene_session->request_mode(*mode);
            }
            if(m_assets->take_reference_refresh_request()) {
                if(auto* scene = get_engine().get_scene())
                    static_cast<void>(
                        m_assets->prepare_scene(*scene, m_component_registry));
            }
        }

        Comet::Project m_project;
        std::unique_ptr<CometEditor::ImGuiContext> m_imgui_context;
        std::unique_ptr<CometEditor::EditorAssets> m_assets;
        std::optional<CometEditor::SelectionService> m_selection;
        Comet::ComponentRegistry m_component_registry =
            Comet::create_scene_component_registry();
        CometEditor::CommandHistory m_command_history;
        CometEditor::PropertyEditTransaction m_property_edit{
            m_command_history, m_component_registry};
        CometEditor::TransformGizmo m_transform_gizmo{
            m_command_history, m_component_registry};
        CometEditor::PropertyEditorRegistry m_property_editor_registry;
        Comet::SceneSerializer m_scene_serializer{m_component_registry};
        CometEditor::EditorState m_editor_state;
        CometEditor::EditorShortcuts m_shortcuts;
        std::unique_ptr<CometEditor::SceneDocument> m_scene_document;
        std::unique_ptr<CometEditor::EditorSceneSession> m_scene_session;
        CometEditor::SceneFileDialog m_scene_file_dialog;

        std::unique_ptr<CometEditor::MenuBar> m_menu_bar;
        std::unique_ptr<CometEditor::HierarchyPanel> m_hierarchy_panel;
        std::unique_ptr<CometEditor::ViewPanel> m_viewport_panel;
        std::unique_ptr<CometEditor::InspectorPanel> m_inspector_panel;
        std::unique_ptr<CometEditor::ProjectPanel> m_project_panel;
        std::shared_ptr<CometEditor::ConsolePanel> m_console_panel;
    };

    std::unique_ptr<Comet::Application> create_editor(
        Comet::ApplicationArguments arguments) {
        if(arguments.size() > 1
            || (!arguments.empty() && arguments.front().starts_with('-')))
            throw std::invalid_argument("Expected a project directory or project.json");
        auto project = Comet::Project::load(
            arguments.empty() ? COMET_SAMPLE_PROJECT_DIRECTORY : arguments.front());
        return std::make_unique<Editor>(std::move(project));
    }
}

RUN_APP(create_editor, "[project-directory | project.json]")
