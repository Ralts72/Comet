#include "runtime/entry.h"
#include "render/render_context.h"
#include "render/resource/render_resources.h"
#include "graphics/swapchain.h"
#include "graphics/resource/sampler.h"
#include "assets/editor_assets.h"
#include "render/shader_reload.h"
#include "scene/scene_file_dialog.h"
#include "ui/dialogs.h"
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
#include "common/scope_exit.h"
#include "render/renderer.h"
#include "render/scene/scene_renderer.h"
#include "core/window.h"
#include "diagnostics/logger.h"
#include "ui/menu_bar.h"
#include "ui/console.h"
#include "inspector/inspector.h"
#include "assets/project_panel.h"
#include "viewport/viewport.h"
#include "scene/hierarchy.h"
#include "scene/selection.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"

#include <exception>
#include <cstdint>
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
        explicit Editor(Comet::Project project)
            : Application(project.paths().cache(), Comet::OutputMode::Sdr),
              m_project(std::move(project)) {}

        Comet::Result<void, Comet::Error> on_init() override {
            LOG_INFO("Editor initializing...");

            auto& engine = get_engine();
            auto& renderer = engine.get_renderer();
            auto& render_context = renderer.get_render_context();
            auto& scene_renderer = renderer.get_scene_renderer();

            const auto& swapchain = render_context.get_swapchain();
            if(auto result = renderer.enable_offscreen_rendering(
                   Comet::Math::Vec2u(swapchain.get_width(), swapchain.get_height()));
                !result)
                return Comet::Result<void, Comet::Error>::failure(result.error().as_error());

            auto ui = CometEditor::ImGuiContext::create(engine.get_window(), render_context,
                m_project.paths().editor_state() / "imgui.ini");
            if(!ui)
                return Comet::Result<void, Comet::Error>::failure(ui.error().as_error());
            m_imgui_context = std::move(ui).value();

            m_console_panel = std::make_shared<CometEditor::ConsolePanel>();
            setup_log_redirect();

            const std::filesystem::path shader_root(COMET_BUILTIN_SHADER_DIRECTORY);
            CometEditor::ShaderReload::Requests shader_requests;
            for(const auto& program : Comet::builtin_material_shaders()) {
                const auto name = std::string(program.name);
                shader_requests.emplace(
                    name + ".vert", Comet::ShaderCompiler::Request{
                                        .source = shader_root / "material" / (name + ".vert"),
                                        .stage = Comet::ShaderStage::Vertex});
                shader_requests.emplace(
                    name + ".frag", Comet::ShaderCompiler::Request{
                                        .source = shader_root / "material" / (name + ".frag"),
                                        .stage = Comet::ShaderStage::Fragment});
            }
            m_material_shader_reload = std::make_unique<CometEditor::ShaderReload>(
                engine.get_task_scheduler(), std::move(shader_requests));
            auto shortcuts = CometEditor::EditorShortcuts::load(
                std::filesystem::path(COMET_CONFIG_DIRECTORY) / "profiles/editor-dev.yaml");
            if(shortcuts)
                m_shortcuts = std::move(shortcuts).value();
            else
                LOG_ERROR("{}; using default editor shortcuts", shortcuts.error());

            m_assets = std::make_unique<CometEditor::EditorAssets>(m_project.paths(),
                engine.get_asset_registry(), engine.get_render_resources(),
                engine.get_task_scheduler());
            auto initial_asset_scan = m_assets->refresh();
            m_property_editor_registry =
                CometEditor::create_property_editor_registry(m_assets->database());
            Comet::Engine* engine_ptr = &engine;
            const auto get_active_scene = [engine_ptr]() { return engine_ptr->get_scene(); };
            const auto replace_active_scene = [this](std::unique_ptr<Comet::Scene> scene) {
                return install_scene(std::move(scene), CometEditor::EditorMode::Edit);
            };
            const auto prepare_candidate = [this](Comet::Scene& scene) {
                // 缺失引用保留供编辑器修复，不阻止安装候选场景。
                if(auto prepared = m_assets->prepare_scene(scene, m_component_registry); !prepared)
                    return Comet::Result<void, Comet::Error>::failure(prepared.error());
                return Comet::Result<void, Comet::Error>::success();
            };
            m_scene_document =
                std::make_unique<CometEditor::SceneDocument>(m_scene_serializer, m_project.paths(),
                    m_command_history, get_active_scene, replace_active_scene, prepare_candidate);
            LOG_INFO(
                "Opened project '{}' at '{}'", m_project.name(), m_project.paths().root().string());
            if(m_project.startup_scene().empty()) {
                if(auto created = m_scene_document->create_new(); !created)
                    return created;
            } else if(auto opened = m_scene_document->open(m_project.startup_scene().string());
                !opened) {
                if(is_device_lost(opened.error()))
                    return opened;
                LOG_WARN("Default scene could not be opened; starting with an empty scene");
                if(auto created = m_scene_document->create_new(); !created)
                    return created;
            }
            m_scene_session = std::make_unique<CometEditor::EditorSceneSession>(
                m_editor_state, m_scene_serializer, get_active_scene,
                [this](std::unique_ptr<Comet::Scene> scene, CometEditor::EditorMode mode) {
                    return install_scene(std::move(scene), mode);
                },
                prepare_candidate);
            auto& scene = *engine.get_scene();
            m_selection.emplace(scene);
            if(auto panels = setup_panels(scene, std::move(initial_asset_scan)); !panels)
                return panels;
            m_inspector_panel->set_material_layouts(scene_renderer.get_material_layouts());

            renderer.set_overlay_renderer([this](Comet::CommandBuffer& command_buffer) {
                m_imgui_context->render(command_buffer);
            });

            renderer.set_swapchain_resource_callbacks(
                [this]() { m_imgui_context->release_swapchain_resources(); },
                [this](const Comet::SwapchainCompatibility& compatibility) {
                    return m_imgui_context->rebuild_swapchain_resources(compatibility);
                });

            renderer.set_viewport_pick_callback(
                [this](const std::optional<Comet::ScenePickHit> hit) {
                    m_viewport->apply_pick(hit, get_engine().get_scene());
                });

            LOG_INFO("Editor initialized");
            engine.get_window().confirm_close_requests(true);
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_update(const Comet::UpdateContext context) override {
            if(auto requests = process_editor_requests(); !requests)
                return requests;
            if(get_engine().get_window().take_close_request()) {
                m_scene_session->request_mode(CometEditor::EditorMode::Edit);
                if(m_editor_state.mode == CometEditor::EditorMode::Play) {
                    if(auto stopped = apply_editor_mode_request(); !stopped)
                        return stopped;
                }
                if(finish_active_edit())
                    m_scene_document->request({CometEditor::SceneDocument::Action::Close, {}});
            }
            if(auto action = execute_document_action(); !action)
                return action;
            if(get_engine().get_window().should_close())
                return Comet::Result<void, Comet::Error>::success();
            if(auto result = update_material_shaders(); !result)
                return result;
            auto assets = m_assets->update();
            if(!assets)
                return Comet::Result<void, Comet::Error>::failure(assets.error());
            if(assets.value())
                m_project_panel->update_scan_report(std::move(*assets.value()));
            if(auto mode = apply_editor_mode_request(); !mode)
                return mode;

            if(m_reference_history_state != m_command_history.state_id()) {
                m_assets->track_scene(*get_engine().get_scene(), m_component_registry);
                m_reference_history_state = m_command_history.state_id();
            }
            if(auto restored = m_assets->restore_references(); !restored)
                return Comet::Result<void, Comet::Error>::failure(restored.error());

            m_menu_bar->set_fps(context.fps);
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_frame_ready() override {
            m_viewport->update_texture();
            if(!m_imgui_context->begin_frame())
                return Comet::Result<void, Comet::Error>::success();
            {
                const Comet::ScopeExit end_ui([this] { m_imgui_context->end_frame(); });
                draw_editor_ui();
                if(auto viewport = m_viewport->update(get_engine().get_scene()); !viewport)
                    return viewport;
            }
            m_viewport->submit_feedback(get_engine().get_scene());
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_shutdown() override {
            get_engine().get_window().confirm_close_requests(false);
            LOG_INFO("Editor shutting down...");
            get_engine().get_renderer().set_overlay_renderer({});
            get_engine().get_renderer().set_viewport_pick_callback({});
            auto& renderer = get_engine().get_renderer();
            renderer.set_swapchain_resource_callbacks({}, {});
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
            m_material_shader_reload.reset();
            m_console_panel.reset();
            return Comet::Result<void, Comet::Error>::success();
        }

    private:
        Comet::Result<void, Comet::Error> update_material_shaders() {
            const auto compilation = m_material_shader_reload->update();
            if(!compilation)
                return Comet::Result<void, Comet::Error>::success();
            if(!compilation->succeeded) {
                LOG_ERROR("Material Shader compilation failed; previous version retained: {}",
                    compilation->diagnostics);
                return Comet::Result<void, Comet::Error>::success();
            }
            auto& scene_renderer = get_engine().get_renderer().get_scene_renderer();
            const auto& stages = compilation->stages;
            Comet::MaterialShaders shaders;
            for(const auto& program : Comet::builtin_material_shaders()) {
                const auto name = std::string(program.name);
                shaders.emplace(name, Comet::MaterialShaderProgram{stages.at(name + ".vert").words,
                                          stages.at(name + ".frag").words});
            }
            auto result = get_engine().get_renderer().reload_material_shaders(std::move(shaders));
            if(!result) {
                if(result.error().is_device_lost())
                    return Comet::Result<void, Comet::Error>::failure(result.error().as_error());
                if(result.error().is_out_of_memory()) {
                    if(!m_material_shader_reload->retry_delivery(compilation->revision)) {
                        LOG_ERROR(
                            "Material Shader publication retries exhausted; previous version retained, waiting for a new request: {}",
                            result.error().message);
                    } else if(m_reported_shader_retry != compilation->revision) {
                        LOG_WARN(
                            "Material Shader publication ran out of memory; previous version retained, retrying: {}",
                            result.error().message);
                        m_reported_shader_retry = compilation->revision;
                    }
                } else {
                    LOG_ERROR("Material Shader publication failed; previous version retained: {}",
                        result.error().message);
                }
                return Comet::Result<void, Comet::Error>::success();
            }
            m_reported_shader_retry = 0;
            if(!compilation->diagnostics.empty())
                LOG_WARN("{}", compilation->diagnostics);
            if(result.value().pipelines == 0)
                return Comet::Result<void, Comet::Error>::success();
            m_inspector_panel->set_material_layouts(scene_renderer.get_material_layouts());
            LOG_INFO(
                "Published material Shader revision {}: {} pipelines, {} material versions, {} bindings",
                compilation->revision, result.value().pipelines, result.value().material_versions,
                result.value().material_bindings);
            LOG_INFO("Shader preparation: pipelines {:.2f} ms, candidate copies {:.2f} ms, "
                     "material CPU {:.2f} ms, material GPU {:.2f} ms",
                result.value().pipeline_preparation_ms, result.value().candidate_copy_ms,
                result.value().material_cpu_ms, result.value().material_gpu_ms);
            return Comet::Result<void, Comet::Error>::success();
        }

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
                        uuid = Commands::create_entity(
                            m_command_history, m_component_registry, "Entity", request.parent);
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

        Comet::Result<void, Comet::Error> handle_command(
            const CometEditor::MenuBar::Command command) {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit) {
                LOG_WARN("Scene commands are disabled in Play mode");
                return Comet::Result<void, Comet::Error>::success();
            }

            if(!finish_active_edit())
                return Comet::Result<void, Comet::Error>::success();

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
                    m_scene_document->request({CometEditor::SceneDocument::Action::New, {}});
                    break;
                case CometEditor::MenuBar::Command::OpenScene:
                    m_scene_file_dialog.request(CometEditor::SceneFileDialog::Action::Open,
                        m_scene_document->get_path(), m_project.paths().assets() / "scenes");
                    break;
                case CometEditor::MenuBar::Command::SaveScene:
                    if(m_scene_document->get_path().empty()) {
                        m_scene_file_dialog.request(CometEditor::SceneFileDialog::Action::Save,
                            m_scene_document->get_path(), m_project.paths().assets() / "scenes");
                    } else {
                        return m_scene_document->save(m_scene_document->get_path());
                    }
                    break;
            }
            return Comet::Result<void, Comet::Error>::success();
        }

        std::unique_ptr<Comet::Scene> install_scene(
            std::unique_ptr<Comet::Scene> scene, CometEditor::EditorMode mode) {
            // 旧场景仍存活时结束交互；返回 owner 后才允许调用者销毁或保留它。
            if(m_viewport)
                m_viewport->panel().cancel_interaction();
            static_cast<void>(m_property_edit.cancel());
            auto previous = get_engine().replace_scene(std::move(scene));
            auto* active = get_engine().get_scene();
            if(mode == CometEditor::EditorMode::Edit && m_command_history.get_scene() != active)
                m_command_history.bind_scene(active);
            if(m_selection) {
                m_selection->set_scene(*active);
                m_hierarchy_panel->set_scene(*active);
            }
            m_assets->track_scene(*active, m_component_registry);
            m_reference_history_state = m_command_history.state_id();
            return previous;
        }

        Comet::Result<void, Comet::Error> apply_editor_mode_request() {
            if(!m_scene_session) {
                return Comet::Result<void, Comet::Error>::success();
            }

            const auto result = m_scene_session->apply_mode_request();
            if(!result) {
                if(is_device_lost(result.error()))
                    return Comet::Result<void, Comet::Error>::failure(result.error());
                LOG_ERROR("Cannot change editor mode: {}", result.error().message);
            }
            return Comet::Result<void, Comet::Error>::success();
        }

        void setup_log_redirect() const {
            Comet::Logger::remove_console_sinks();

            // 弱引用避免延迟日志访问已销毁的面板。
            const auto gui_sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
                [panel = std::weak_ptr(m_console_panel)](const spdlog::details::log_msg& msg) {
                    if(const auto console = panel.lock()) {
                        const auto level = Comet::log_level_from_spdlog(msg.level);
                        console->add_log(
                            level, std::string(msg.payload.data(), msg.payload.size()));
                    }
                });

            Comet::Logger::add_custom_sink(gui_sink);
        }

        Comet::Result<void, Comet::Error> setup_panels(
            Comet::Scene& scene, Comet::AssetScanReport initial_asset_scan) {
            auto sampler =
                get_engine().get_render_resources().get_sampler_manager().get_nearest_clamp();
            if(!sampler)
                return Comet::Result<void, Comet::Error>::failure(sampler.error().as_error());
            m_menu_bar = std::make_unique<CometEditor::MenuBar>(
                m_editor_state, m_command_history, m_shortcuts);

            m_hierarchy_panel = std::make_unique<CometEditor::HierarchyPanel>(
                scene, *m_selection, m_command_history, m_editor_state);
            m_viewport = std::make_unique<CometEditor::Viewport>(m_editor_state, *m_selection,
                m_command_history, m_component_registry, m_property_edit, m_shortcuts,
                get_engine().get_renderer(), get_engine().get_asset_registry(), *m_imgui_context,
                std::move(sampler).value());
            m_inspector_panel = std::make_unique<CometEditor::InspectorPanel>(m_editor_state,
                *m_selection, m_command_history, m_property_edit, m_component_registry,
                m_property_editor_registry, m_assets->database(), m_project.paths().assets());
            m_project_panel = std::make_unique<CometEditor::ProjectPanel>(m_assets->database(),
                m_project.paths().assets(), std::move(initial_asset_scan), *m_selection,
                m_command_history);
            m_menu_bar->register_panel(*m_hierarchy_panel);
            m_menu_bar->register_panel(m_viewport->panel());
            m_menu_bar->register_panel(*m_inspector_panel);
            m_menu_bar->register_panel(*m_project_panel);
            m_menu_bar->register_panel(*m_console_panel);
            return Comet::Result<void, Comet::Error>::success();
        }

        void draw_editor_ui() {
            constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockspace_flags);

            ImGui::BeginDisabled(m_scene_document->has_pending_request());
            m_menu_bar->render();
            m_hierarchy_panel->render();
            m_viewport->panel().render();
            m_inspector_panel->render();
            m_project_panel->render();
            ImGui::EndDisabled();
            m_console_panel->render();
            m_scene_file_dialog.render();
            draw_unsaved_dialog();
            if(!m_scene_document->has_pending_request())
                m_menu_bar->collect_shortcuts();
        }

        Comet::Result<void, Comet::Error> handle_asset_assignment(
            const CometEditor::InspectorPanel::AssetAssignment& request) {
            auto* scene = get_engine().get_scene();
            if(!scene || request.asset.generation != m_command_history.generation()
                || (m_editor_state.mode == CometEditor::EditorMode::Edit
                    && m_command_history.get_scene() != scene))
                return Comet::Result<void, Comet::Error>::success();
            auto entity = scene->find_entity(request.target.entity);
            const auto* component = m_component_registry.find_component(request.target.component);
            const auto* property =
                component ? component->find_property(request.target.property) : nullptr;
            if(!entity || !component || !component->has_component(entity) || !property
                || !property->editable || property->read_only
                || property->type != Comet::PropertyType::AssetHandle
                || property->asset_type != request.asset.type)
                return Comet::Result<void, Comet::Error>::success();
            const auto current =
                property->copy_value(component->get_component(std::as_const(entity)));
            if(!current || std::get<Comet::AssetHandle>(*current) == request.asset.handle)
                return Comet::Result<void, Comet::Error>::success();
            if(!finish_active_edit())
                return Comet::Result<void, Comet::Error>::success();
            if(auto loaded = m_assets->load_reference(
                   request.asset.handle, request.asset.type, request.asset.revision);
                !loaded)
                return loaded;
            if(m_editor_state.mode == CometEditor::EditorMode::Play) {
                if(!property->assign_value(component->get_component(entity), request.asset.handle))
                    LOG_ERROR("Cannot update runtime asset reference");
                m_assets->track_scene(*scene, m_component_registry);
                return Comet::Result<void, Comet::Error>::success();
            }
            if(!m_property_edit.apply(request.target, request.asset.handle))
                LOG_ERROR("Cannot commit asset reference");
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> handle_mesh_drop(
            const CometEditor::ViewportPanel::MeshDrop& request) {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit
                || request.asset.generation != m_command_history.generation()
                || !m_command_history.get_scene()
                || m_command_history.get_scene() != get_engine().get_scene())
                return Comet::Result<void, Comet::Error>::success();
            if(!request.asset.handle)
                return Comet::Result<void, Comet::Error>::success();
            if(auto loaded = m_assets->load_reference(
                   request.asset.handle, Comet::AssetType::Mesh, request.asset.revision);
                !loaded)
                return loaded;
            if(!finish_active_edit())
                return Comet::Result<void, Comet::Error>::success();
            const auto* record = m_assets->database().find(request.asset.handle);
            const auto uuid = CometEditor::SceneCommands::create_mesh_entity(m_command_history,
                m_component_registry, record->path.stem().string(), request.asset.handle, {},
                request.position);
            if(uuid)
                m_selection->select_entity(
                    m_command_history.get_scene()->find_entity(uuid).get_id());
            else
                LOG_ERROR("Cannot create entity for dropped mesh");
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> process_editor_requests() {
            if(const auto move = m_project_panel->take_move_request())
                m_project_panel->complete_move(
                    *move, m_assets->move(move->handle, move->destination));
            if(m_project_panel->take_refresh_request())
                m_project_panel->update_scan_report(m_assets->refresh());
            if(const auto edit = m_inspector_panel->take_asset_edit()) {
                const auto result = m_assets->apply_edit(*edit);
                m_inspector_panel->complete_asset_edit(*edit, static_cast<bool>(result));
                if(!result) {
                    if(is_device_lost(result.error()))
                        return result;
                    LOG_WARN("Asset edit rejected: {}", result.error().message);
                }
            }
            for(const auto& drop : get_engine().get_window().take_file_drops()) {
                // GLFW 与主 ImGui viewport 都使用逻辑坐标，不乘 Retina framebuffer scale。
                const auto origin = ImGui::GetMainViewport()->Pos;
                const auto directory = m_project_panel->file_drop_directory(
                    drop.position + Comet::Math::Vec2(origin.x, origin.y));
                if(directory)
                    m_project_panel->update_scan_report(
                        m_assets->import_files(drop.paths, *directory));
                else
                    LOG_WARN("Drop external files onto a Project folder or its empty area");
            }
            if(const auto handle = m_project_panel->take_mesh_reimport_request())
                m_assets->request_mesh_reimport(*handle);
            const auto hierarchy_request = m_hierarchy_panel->take_request();
            const auto menu_command = m_menu_bar->take_command();
            const auto mesh_drop = m_viewport->panel().take_mesh_drop();
            const auto asset_assignment = m_inspector_panel->take_asset_assignment();
            const auto mode = m_viewport->panel().take_mode_request();
            const auto file_request = m_scene_file_dialog.take_request();
            if(m_scene_file_dialog.take_cancelled()) {
                m_scene_document->decide(CometEditor::SceneDocument::Decision::Cancel);
            }
            // 弹窗提交、菜单命令优先，避免切换场景后执行旧编辑请求。
            if(file_request) {
                if(!finish_active_edit())
                    return Comet::Result<void, Comet::Error>::success();
                if(file_request->action == CometEditor::SceneFileDialog::Action::Open) {
                    m_scene_document->request(
                        {CometEditor::SceneDocument::Action::Open, file_request->path});
                    m_scene_file_dialog.complete(Comet::Result<void, Comet::Error>::success());
                } else {
                    const auto result = m_scene_document->save(file_request->path);
                    m_scene_file_dialog.complete(result);
                    if(!result && is_device_lost(result.error()))
                        return result;
                }
            } else if(menu_command) {
                if(auto command = handle_command(*menu_command);
                    !command && is_device_lost(command.error()))
                    return command;
            } else if(hierarchy_request)
                handle_scene_request(*hierarchy_request);
            else if(mesh_drop && !mode) {
                if(auto result = handle_mesh_drop(*mesh_drop); !result) {
                    if(is_device_lost(result.error()))
                        return result;
                    LOG_WARN("Mesh drop rejected: {}", result.error().message);
                }
            } else if(asset_assignment && !mode) {
                if(auto result = handle_asset_assignment(*asset_assignment); !result) {
                    if(is_device_lost(result.error()))
                        return result;
                    LOG_WARN("Asset assignment rejected: {}", result.error().message);
                }
            }
            if(mode && !file_request && !m_scene_document->has_pending_request()) {
                if(finish_active_edit())
                    m_scene_session->request_mode(*mode);
            }
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> execute_document_action() {
            const auto action = m_scene_document->take_ready_request();
            if(!action)
                return Comet::Result<void, Comet::Error>::success();
            if(action->action == CometEditor::SceneDocument::Action::Close) {
                get_engine().get_window().request_close();
                return Comet::Result<void, Comet::Error>::success();
            }
            auto result = Comet::Result<void, Comet::Error>::success();
            if(action->action == CometEditor::SceneDocument::Action::New)
                result = m_scene_document->create_new();
            else
                result = m_scene_document->open(action->path);
            if(!result && !is_device_lost(result.error())) {
                LOG_WARN("Scene operation rejected: {}", result.error().message);
                if(action->action == CometEditor::SceneDocument::Action::Open) {
                    m_scene_file_dialog.request(CometEditor::SceneFileDialog::Action::Open,
                        action->path, m_project.paths().assets() / "scenes");
                    m_scene_file_dialog.complete(result);
                }
                return Comet::Result<void, Comet::Error>::success();
            }
            return result;
        }

        void draw_unsaved_dialog() {
            const auto decision =
                CometEditor::draw_unsaved_scene_dialog(m_scene_document->needs_confirmation());
            if(!decision)
                return;
            m_scene_document->decide(*decision);
            if(*decision == CometEditor::SceneDocument::Decision::Save) {
                m_scene_file_dialog.request(CometEditor::SceneFileDialog::Action::Save,
                    m_scene_document->get_path(), m_project.paths().assets() / "scenes");
            }
        }

        std::uint64_t m_reference_history_state = 0;
        Comet::Project m_project;
        std::unique_ptr<CometEditor::ImGuiContext> m_imgui_context;
        std::unique_ptr<CometEditor::EditorAssets> m_assets;
        std::unique_ptr<CometEditor::ShaderReload> m_material_shader_reload;
        uint64_t m_reported_shader_retry = 0;
        std::optional<CometEditor::SelectionService> m_selection;
        Comet::ComponentRegistry m_component_registry = Comet::create_scene_component_registry();
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

    Comet::Result<std::unique_ptr<Comet::Application>> create_editor(
        Comet::ApplicationArguments arguments) {
        if(arguments.size() > 1 || (!arguments.empty() && arguments.front().starts_with('-')))
            return Comet::Result<std::unique_ptr<Comet::Application>>::failure(
                "Expected a project directory or project.json");
        auto project = Comet::Project::load(
            arguments.empty() ? COMET_SAMPLE_PROJECT_DIRECTORY : arguments.front());
        if(!project)
            return Comet::Result<std::unique_ptr<Comet::Application>>::failure(project.error());
        return Comet::Result<std::unique_ptr<Comet::Application>>::success(
            std::make_unique<Editor>(std::move(project).value()));
    }
}

RUN_APP(create_editor, "[project-directory | project.json]")
