#include "runtime/entry.h"
#include "asset/manager.h"
#include "src/editor_scene_session.h"
#include "src/editor_state.h"
#include "src/imgui_context.h"
#include "src/property_editor_registry.h"
#include "src/scene_document.h"
#include "core/engine.h"
#include "core/project_paths.h"
#include "render/renderer.h"
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
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"

#include <algorithm>
#include <array>
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
    const std::filesystem::path DEMO_MESH = "meshes/cube.gltf";
    const std::filesystem::path DEMO_MATERIAL = "materials/demo.mat";

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

        if(!asset_manager.load_mesh(record->handle)) {
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

            // 设置日志重定向
            setup_log_redirect();

            m_asset_manager = std::make_unique<Comet::AssetManager>(m_project_paths,
                engine.get_asset_registry(), engine.get_resource_manager(),
                engine.get_task_scheduler());
            m_asset_scan_report = m_asset_manager->scan();
            log_asset_scan_issues(m_asset_scan_report);

            const EditorRenderAssets render_assets{
                .mesh = load_required_mesh(*m_asset_manager, DEMO_MESH),
                .material = load_required_material(*m_asset_manager, DEMO_MATERIAL)};
            engine.set_scene(create_editor_scene(render_assets));
            Comet::Engine* engine_ptr = &engine;
            const auto get_active_scene = [engine_ptr]() {
                return engine_ptr->get_scene();
            };
            const auto replace_active_scene = [engine_ptr](
                                                  std::unique_ptr<Comet::Scene> scene) {
                return engine_ptr->replace_scene(std::move(scene));
            };
            m_scene_document = std::make_unique<CometEditor::SceneDocument>(
                m_scene_serializer, get_active_scene, replace_active_scene);
            m_scene_session =
                std::make_unique<CometEditor::EditorSceneSession>(m_editor_state,
                    m_scene_serializer, get_active_scene, replace_active_scene);
            auto& scene = *engine.get_scene();
            m_selection.emplace(scene);
            setup_panels(scene);

            m_imgui_context->set_viewport_images(
                scene_renderer.get_offscreen_color_views(),
                renderer.get_resource_manager()
                    .get_sampler_manager()
                    .get_nearest_clamp());

            // 注册 ImGui 渲染回调
            renderer.set_on_imgui_render([this](Comet::CommandBuffer& cmd) {
                update_viewport_texture(get_engine().get_renderer().get_scene_renderer());
                m_imgui_context->update_frame();
                m_imgui_context->render(cmd);
            });

            // 注册 Swapchain 重建回调
            scene_renderer.set_swapchain_recreate_callback(
                [this]() { m_imgui_context->recreate_swapchain(); });

            LOG_INFO("Editor initialized");
        }

        void on_update(const Comet::UpdateContext context) override {
            m_asset_manager->process_completions();
            apply_editor_mode_request();

            // 更新 FPS 显示
            m_menu_bar->set_fps(context.fps);

            update_viewport_state();
        }

        void update_viewport_state() {
            if(const auto resize_request = m_viewport_panel->take_resize_request()) {
                get_engine().get_renderer().resize_offscreen_target(*resize_request);
            }
        }

        void update_viewport_texture(Comet::SceneRenderer& scene_renderer) {
            auto& renderer = get_engine().get_renderer();
            m_imgui_context->set_viewport_images(
                scene_renderer.get_offscreen_color_views(),
                renderer.get_resource_manager()
                    .get_sampler_manager()
                    .get_nearest_clamp());

            const uint32_t frame_slot =
                scene_renderer.get_frame_scheduler().get_current_frame_slot_index();
            const ImTextureID texture_id =
                m_imgui_context->get_viewport_texture_id(frame_slot);
            const Comet::Math::Vec2u size = scene_renderer.get_render_target().get_size();
            m_viewport_panel->set_texture_id(texture_id, size.x, size.y);
        }

        void on_shutdown() override {
            LOG_INFO("Editor shutting down...");
            m_imgui_context.reset();
            m_project_panel.reset();
            m_hierarchy_panel.reset();
            m_inspector_panel.reset();
            m_selection.reset();
            m_scene_session.reset();
            m_scene_document.reset();
            m_asset_manager.reset();
        }

    private:
        void handle_file_command(const CometEditor::FileCommand command) {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit) {
                LOG_WARN("Scene file commands are disabled in Play mode");
                return;
            }

            switch(command) {
                case CometEditor::FileCommand::NewScene:
                    if(m_scene_document->create_new()) {
                        bind_active_scene();
                    }
                    break;
                case CometEditor::FileCommand::OpenScene:
                    request_scene_file_dialog(SceneFileDialog::Open);
                    break;
                case CometEditor::FileCommand::SaveScene:
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

        void setup_log_redirect() const {
            // 移除控制台输出，只保留文件输出和 GUI 输出
            Comet::Logger::remove_console_sinks();

            // 创建 callback sink，将日志发送到 ConsolePanel
            const auto gui_sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
                [this](const spdlog::details::log_msg& msg) {
                    const Comet::LogLevel level = Comet::log_level_from_spdlog(msg.level);
                    const std::string message(msg.payload.data(), msg.payload.size());
                    if(m_console_panel) {
                        m_console_panel->add_log(level, message);
                    }
                });

            // 将 GUI sink 添加到 logger
            Comet::Logger::add_custom_sink(gui_sink);
        }

        void setup_panels(Comet::Scene& scene) {
            // 创建菜单栏
            m_menu_bar = std::make_unique<CometEditor::MenuBar>(m_editor_state);
            m_menu_bar->set_file_command_callback(
                [this](const CometEditor::FileCommand command) {
                    handle_file_command(command);
                });
            m_menu_bar->set_editor_mode_callback(
                [this](const CometEditor::EditorMode mode) {
                    m_scene_session->request_mode(mode);
                });

            // 创建面板
            m_hierarchy_panel =
                std::make_unique<CometEditor::HierarchyPanel>(scene, *m_selection);
            m_viewport_panel = std::make_unique<CometEditor::ViewPanel>(m_editor_state);
            m_inspector_panel = std::make_unique<CometEditor::InspectorPanel>(
                *m_selection, m_component_registry, m_property_editor_registry,
                m_asset_manager->get_database(), m_project_paths.assets(),
                [this](const Comet::AssetHandle handle, const Comet::MaterialData& data) {
                    return static_cast<bool>(
                        m_asset_manager->update_material(handle, data));
                },
                [this](const Comet::AssetHandle handle,
                    const Comet::TextureImportSettings settings) {
                    return static_cast<bool>(
                        m_asset_manager->reimport_texture(handle, settings));
                });
            m_project_panel = std::make_unique<CometEditor::ProjectPanel>(
                m_asset_manager->get_database(), m_asset_scan_report,
                [this]() {
                    Comet::AssetScanReport report = m_asset_manager->scan();
                    if(report.snapshot_updated) {
                        m_inspector_panel->invalidate_asset_cache();
                    }
                    log_asset_scan_issues(report);
                    return report;
                },
                *m_selection);
            m_console_panel = std::make_unique<CometEditor::ConsolePanel>();

            // 设置菜单栏面板可见性回调
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

            // 设置 UI 回调
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
                render_scene_file_dialog();
            });
        }

        Comet::ProjectPaths m_project_paths{PROJECT_ROOT_DIR};
        std::unique_ptr<CometEditor::ImGuiContext> m_imgui_context;
        std::unique_ptr<Comet::AssetManager> m_asset_manager;
        Comet::AssetScanReport m_asset_scan_report;
        std::optional<CometEditor::SelectionService> m_selection;
        Comet::ComponentRegistry m_component_registry =
            Comet::create_scene_component_registry();
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
        std::unique_ptr<CometEditor::ConsolePanel> m_console_panel;
    };
}

RUN_APP(Editor)
