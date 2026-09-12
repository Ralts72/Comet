#include "runtime/entry.h"
#include "render/render_context.h"
#include "render/resource/resource_manager.h"
#include "graphics/swapchain.h"
#include "assets/editor_assets.h"
#include "scene/scene_file_dialog.h"
#include "scene/command_history.h"
#include "scene/scene_commands.h"
#include "scene/editor_scene_session.h"
#include "editor_state.h"
#include "ui/imgui_context.h"
#include "inspector/property_editor_registry.h"
#include "scene/scene_document.h"
#include "ui/shortcuts.h"
#include "core/engine.h"
#include "core/project.h"
#include "render/renderer.h"
#include "render/scene/scene_renderer.h"
#include "core/window.h"
#include "diagnostics/logger.h"
#include "ui/menu_bar.h"
#include "ui/console.h"
#include "inspector/inspector.h"
#include "assets/project.h"
#include "viewport/viewport.h"
#include "scene/hierarchy.h"
#include "scene/selection.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"

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

            renderer.set_overlay_callbacks(
                [this]() {
                    m_viewport->update_texture();
                    m_imgui_context->update_frame();
                    m_viewport->submit_feedback(get_engine().get_scene());
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
                    m_viewport->apply_pick(hit, get_engine().get_scene());
                });

            LOG_INFO("Editor initialized");
        }

        void on_update(const Comet::UpdateContext context) override {
            if(auto report = m_assets->update())
                m_project_panel->update_scan_report(std::move(*report));
            apply_editor_mode_request();

            m_menu_bar->set_fps(context.fps);
        }

        void on_shutdown() override {
            LOG_INFO("Editor shutting down...");
            get_engine().get_renderer().set_overlay_callbacks({}, {});
            get_engine().get_renderer().set_viewport_pick_callback({});
            auto& scene_renderer = get_engine().get_renderer().get_scene_renderer();
            scene_renderer.set_swapchain_resource_callbacks({}, {});
            if(m_imgui_context)
                m_imgui_context->set_ui_callback({});
            if(m_viewport)
                m_viewport->panel().cancel_interaction();
            static_cast<void>(m_property_edit.cancel());
            m_command_history.bind_scene(nullptr);
            m_menu_bar.reset();
            m_project_panel.reset();
            m_hierarchy_panel.reset();
            m_inspector_panel.reset();
            m_viewport.reset();
            m_property_editor_registry = {};
            m_imgui_context.reset();
            m_selection.reset();
            m_scene_session.reset();
            m_scene_document.reset();
            m_assets.reset();
            m_console_panel.reset();
        }

    private:
        bool finish_active_edit() {
            m_viewport->panel().cancel_interaction();
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
            m_viewport = std::make_unique<CometEditor::Viewport>(m_editor_state,
                *m_selection, m_command_history, m_component_registry, m_property_edit,
                m_shortcuts, get_engine().get_renderer(),
                get_engine().get_asset_registry(), *m_imgui_context);
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
                std::move(initial_asset_scan), [this]() { return m_assets->refresh(); },
                [this](const Comet::AssetHandle handle,
                    const std::filesystem::path& destination) {
                    return m_assets->move(handle, destination);
                },
                *m_selection, m_command_history);
            m_menu_bar->register_panel(*m_hierarchy_panel);
            m_menu_bar->register_panel(m_viewport->panel());
            m_menu_bar->register_panel(*m_inspector_panel);
            m_menu_bar->register_panel(*m_project_panel);
            m_menu_bar->register_panel(*m_console_panel);
            m_imgui_context->set_ui_callback([this]() {
                draw_editor_ui();
                process_editor_requests();
                m_viewport->update(get_engine().get_scene());
            });
        }

        void draw_editor_ui() {
            constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockspace_flags);

            m_menu_bar->render();
            m_hierarchy_panel->render();
            m_viewport->panel().render();
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
                    m_project_panel->update_scan_report(
                        m_assets->import_files(drop.paths, *directory));
                else
                    LOG_WARN(
                        "Drop external files onto a Project folder or its empty area");
            }
            if(const auto handle = m_project_panel->take_mesh_reimport_request())
                m_assets->request_mesh_reimport(*handle);
            const auto hierarchy_request = m_hierarchy_panel->take_request();
            const auto menu_command = m_menu_bar->take_command();
            const auto mesh_drop = m_viewport->panel().take_mesh_drop();
            const auto asset_assignment = m_inspector_panel->take_asset_assignment();
            const auto mode = m_viewport->panel().take_mode_request();
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
        CometEditor::PropertyEditorRegistry m_property_editor_registry;
        Comet::SceneSerializer m_scene_serializer{m_component_registry};
        CometEditor::EditorState m_editor_state;
        CometEditor::EditorShortcuts m_shortcuts;
        std::unique_ptr<CometEditor::SceneDocument> m_scene_document;
        std::unique_ptr<CometEditor::EditorSceneSession> m_scene_session;
        CometEditor::SceneFileDialog m_scene_file_dialog;

        std::unique_ptr<CometEditor::MenuBar> m_menu_bar;
        std::unique_ptr<CometEditor::HierarchyPanel> m_hierarchy_panel;
        std::unique_ptr<CometEditor::Viewport> m_viewport;
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
