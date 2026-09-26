#include "runtime/entry.h"
#include "render/resource/render_resources.h"
#include "graphics/resource/sampler.h"
#include "assets/editor_assets.h"
#include "file_watch_config.h"
#include "assets/material_editing.h"
#include "render/shader_reload.h"
#include "render/render_stats.h"
#include "render/render_diagnostics.h"
#include "common/file_io.h"
#include "scene/scene_file_dialog.h"
#include "scene/editor_request_policy.h"
#include "ui/dialogs.h"
#include "scene/command_history.h"
#include "scene/scene_editor.h"
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
#include "core/window.h"
#include "diagnostics/logger.h"
#include "diagnostics/profiler.h"
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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <imgui.h>
#include <spdlog/sinks/callback_sink.h>

namespace {
    class Editor final: public Comet::Application {
    public:
        explicit Editor(Comet::Project project)
            : Application({.cache_directory = project.paths().cache(),
                  .log_directory = project.paths().logs(),
                  .output_mode = Comet::OutputMode::Sdr,
                  .scene_output = Comet::Config::Render::SceneOutput::Offscreen,
                  .window_title = "Comet Editor"}),
              m_project(std::move(project)) {}

        Comet::Result<void, Comet::Error> on_init() override {
            LOG_INFO("Editor initializing...");

            auto& engine = get_engine();
            auto& renderer = engine.get_renderer();
            auto& render_context = renderer.get_render_context();

            auto ui = CometEditor::ImGuiContext::create(engine.get_window(), render_context,
                m_project.paths().editor_state() / "imgui.ini");
            if(!ui)
                return Comet::Result<void, Comet::Error>::failure(ui.error().as_error());
            m_imgui_context = std::move(ui).value();

            m_console_panel = std::make_shared<CometEditor::ConsolePanel>();
            setup_log_redirect();

            auto translations = CometEditor::Ui::load_translations();
            if(translations) {
                m_translations = std::move(translations).value();
            } else {
                LOG_WARN("{}; using English editor text", translations.error());
                m_ui_language = CometEditor::Ui::Language::English;
            }

            const std::filesystem::path shader_root(COMET_BUILTIN_SHADER_DIRECTORY);
            const auto editor_config =
                std::filesystem::path(COMET_CONFIG_DIRECTORY) / "editor.yaml";
            auto quiet_period = CometEditor::DEFAULT_FILE_WATCH_QUIET_PERIOD;
            auto configured_quiet_period = CometEditor::load_file_watch_quiet_period(editor_config);
            if(configured_quiet_period)
                quiet_period = configured_quiet_period.value();
            else
                LOG_ERROR(
                    "{}; using default file-watch quiet period", configured_quiet_period.error());
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
                engine.get_task_scheduler(), std::move(shader_requests), shader_root, quiet_period);
            if(!m_material_shader_reload->uses_native_notifications())
                LOG_WARN("Built-in shader monitor is using periodic fallback checks");
            auto shortcuts = CometEditor::EditorShortcuts::load(editor_config);
            if(shortcuts)
                m_shortcuts = std::move(shortcuts).value();
            else
                LOG_ERROR("{}; using default editor shortcuts", shortcuts.error());

            m_assets = std::make_unique<CometEditor::EditorAssets>(m_project.paths(),
                engine.get_asset_registry(), engine.get_render_resources(),
                engine.get_task_scheduler(), quiet_period, get_config().assets);
            auto initial_asset_scan = m_assets->refresh();
            m_property_editor_registry =
                CometEditor::create_property_editor_registry(m_assets->database());
            Comet::Engine* engine_ptr = &engine;
            const auto get_active_scene = [engine_ptr]() { return engine_ptr->get_scene(); };
            const auto activate_edit_scene = [this](std::unique_ptr<Comet::Scene> scene) {
                auto activated =
                    activate_candidate_scene(std::move(scene), CometEditor::EditorMode::Edit);
                if(!activated)
                    return Comet::Result<void, Comet::Error>::failure(activated.error());
                return Comet::Result<void, Comet::Error>::success();
            };
            m_scene_document = std::make_unique<CometEditor::SceneDocument>(m_scene_serializer,
                m_project.paths(), m_command_history, get_active_scene, activate_edit_scene);
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
                [this](std::unique_ptr<Comet::Scene> scene) {
                    return activate_candidate_scene(
                        std::move(scene), CometEditor::EditorMode::Play);
                },
                [this](std::unique_ptr<Comet::Scene> scene) {
                    return commit_scene(std::move(scene), CometEditor::EditorMode::Edit);
                },
                [engine_ptr] { return engine_ptr->start_scene_runtime(); });
            auto& scene = *engine.get_scene();
            if(auto configured = engine.set_input_actions(m_project.input_actions()); !configured)
                return configured;
            if(auto added = engine.add_default_scene_systems(); !added)
                return added;
            m_selection.emplace(scene);
            m_scene_editor = std::make_unique<CometEditor::SceneEditor>(m_editor_state,
                m_command_history, m_property_edit, m_component_registry, *m_selection, *m_assets);
            if(auto panels = setup_panels(std::move(initial_asset_scan)); !panels)
                return panels;
            auto material_layouts = renderer.get_material_layouts();
            m_inspector_panel->asset_inspector().set_material_layouts(material_layouts);
            m_project_panel->set_material_layouts(std::move(material_layouts));

            renderer.set_overlay({.render = [this](Comet::CommandBuffer& command_buffer) {
                m_imgui_context->render(command_buffer); },
                .release = [this] { m_imgui_context->release_swapchain_resources(); },
                .rebuild = [this](const Comet::SwapchainCompatibility& compatibility) {
                        return m_imgui_context->rebuild_swapchain_resources(compatibility); }});

            renderer.set_viewport_pick_callback(
                [this](const std::optional<Comet::ScenePickHit> hit) {
                    m_viewport->apply_pick(hit, get_engine().get_scene());
                });

            LOG_INFO("Editor initialized");
            engine.get_window().confirm_close_requests(true);
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_update(Comet::Engine::FrameContext& frame) override {
            PROFILE_SCOPE("Editor::on_update");
            if(const auto language = m_menu_bar->take_language_request())
                m_ui_language = *language;
            process_diagnostics_requests();
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

            m_menu_bar->set_fps(frame.update.fps);
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_frame_ready(
            Comet::Engine::FrameContext& frame) override {
            m_viewport->update_texture();
            if(!m_imgui_context->begin_frame()) {
                m_viewport->panel().cancel_interaction();
                return Comet::Result<void, Comet::Error>::success();
            }
            {
                const Comet::ScopeExit end_ui([this] { m_imgui_context->end_frame(); });
                draw_editor_ui();
                frame.runtime_input = m_viewport->panel().route_runtime_input(frame.physical_input);
                if(auto viewport = m_viewport->update(get_engine().get_scene()); !viewport)
                    return viewport;
            }
            m_viewport->submit_feedback(get_engine().get_scene());
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_runtime_error(const Comet::Error& error) override {
            if(m_editor_state.mode != CometEditor::EditorMode::Play || !m_scene_session)
                return Comet::Result<void, Comet::Error>::failure(error);
            LOG_ERROR("Play stopped: {}", error.message);
            m_scene_session->request_mode(CometEditor::EditorMode::Edit);
            const auto restored = m_scene_session->apply_mode_request();
            if(!restored)
                return Comet::Result<void, Comet::Error>::failure(restored.error());
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_shutdown() override {
            get_engine().get_window().confirm_close_requests(false);
            LOG_INFO("Editor shutting down...");
            get_engine().get_renderer().set_overlay({});
            get_engine().get_renderer().set_viewport_pick_callback({});
            if(m_viewport)
                m_viewport->panel().cancel_interaction();
            if(m_inspector_panel)
                static_cast<void>(m_inspector_panel->finish_edit(true));
            else
                static_cast<void>(m_property_edit.cancel());
            m_command_history.bind_scene(nullptr);
            m_menu_bar.reset();
            m_render_stats.reset();
            m_project_panel.reset();
            m_hierarchy_panel.reset();
            m_inspector_panel.reset();
            m_viewport.reset();
            m_property_editor_registry = {};
            m_imgui_context.reset();
            m_scene_editor.reset();
            m_selection.reset();
            m_scene_session.reset();
            m_scene_document.reset();
            m_assets.reset();
            m_material_shader_reload.reset();
            m_console_panel.reset();
            return Comet::Result<void, Comet::Error>::success();
        }

    private:
        void process_diagnostics_requests() {
            auto& renderer = get_engine().get_renderer();
            if(const auto capture = m_render_stats->take_capture_request()) {
                if(auto enabled = renderer.get_diagnostics().set_enabled(*capture); !enabled)
                    LOG_ERROR("Cannot change render diagnostics: {}", enabled.error());
            }
            if(!m_render_stats->take_allocation_report_request())
                return;
            const auto report = renderer.get_diagnostics().build_allocation_report();
            if(!report) {
                LOG_ERROR("Cannot build allocation report: {}", report.error());
                return;
            }
            const auto path = m_project.paths().editor_state() / "diagnostics/gpu-allocations.json";
            if(auto saved = Comet::write_text_file_atomic(path, report.value()); !saved)
                LOG_ERROR("Cannot save allocation report: {}", saved.error());
            else
                LOG_INFO("Allocation report saved to {}", path.string());
        }

        Comet::Result<void, Comet::Error> update_material_shaders() {
            const auto compilation = m_material_shader_reload->update();
            if(!compilation)
                return Comet::Result<void, Comet::Error>::success();
            if(!compilation->succeeded) {
                LOG_ERROR("Material Shader compilation failed; previous version retained: {}",
                    compilation->diagnostics);
                return Comet::Result<void, Comet::Error>::success();
            }
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
            auto material_layouts = get_engine().get_renderer().get_material_layouts();
            m_inspector_panel->asset_inspector().set_material_layouts(material_layouts);
            m_project_panel->set_material_layouts(std::move(material_layouts));
            LOG_INFO(
                "Published material Shader revision {} ({} stages compiled): {} pipelines, {} material versions, {} bindings",
                compilation->revision, compilation->compiled_stages, result.value().pipelines,
                result.value().material_versions, result.value().material_bindings);
            LOG_INFO("Shader preparation: pipelines {:.2f} ms, candidate copies {:.2f} ms, "
                     "material CPU {:.2f} ms, material GPU {:.2f} ms",
                result.value().pipeline_preparation_ms, result.value().candidate_copy_ms,
                result.value().material_cpu_ms, result.value().material_gpu_ms);
            return Comet::Result<void, Comet::Error>::success();
        }

        bool finish_active_edit() {
            m_viewport->panel().cancel_interaction();
            if(m_inspector_panel->finish_edit())
                return true;
            LOG_ERROR("Cannot finish active property edit; editor request rejected");
            return false;
        }

        [[nodiscard]] std::filesystem::path current_saved_scene() const {
            if(m_scene_document->is_modified())
                return {};
            return m_scene_document->get_asset_relative_path();
        }

        void handle_scene_request(const CometEditor::SceneEditor::StructureRequest& request) {
            if(!m_scene_editor->can_edit(get_engine().get_scene(), request.generation)
                || !finish_active_edit())
                return;
            if(!m_scene_editor->execute(get_engine().get_scene(), request))
                LOG_WARN("Scene structure request was rejected or had no effect");
        }

        Comet::Result<void, Comet::Error> handle_command(
            const CometEditor::MenuBar::Command command) {
            if(m_editor_state.mode != CometEditor::EditorMode::Edit) {
                LOG_WARN("Scene commands are disabled in Play mode");
                return Comet::Result<void, Comet::Error>::success();
            }

            if(command != CometEditor::MenuBar::Command::CopyEntity && !finish_active_edit())
                return Comet::Result<void, Comet::Error>::success();

            switch(command) {
                case CometEditor::MenuBar::Command::Undo:
                    if(!m_scene_editor->undo(get_engine().get_scene()))
                        LOG_WARN("Cannot undo scene edit");
                    break;
                case CometEditor::MenuBar::Command::Redo:
                    if(!m_scene_editor->redo(get_engine().get_scene()))
                        LOG_WARN("Cannot redo scene edit");
                    break;
                case CometEditor::MenuBar::Command::CopyEntity:
                    if(const auto entity = m_selection->get_selected_entity(); entity) {
                        if(!m_scene_editor->copy_entity(get_engine().get_scene(), entity.get_uuid(),
                               m_command_history.generation()))
                            LOG_WARN("Cannot copy selected entity");
                    }
                    break;
                case CometEditor::MenuBar::Command::PasteEntity:
                    if(m_scene_editor->clipboard().has_content()
                        && !m_selection->get_selected_asset()) {
                        const CometEditor::SceneEditor::StructureRequest request{
                            CometEditor::SceneEditor::StructureRequest::Type::Paste, {}, {},
                            m_command_history.generation()};
                        if(!m_scene_editor->execute(get_engine().get_scene(), request))
                            LOG_WARN("Cannot paste entity into scene");
                    }
                    break;
                case CometEditor::MenuBar::Command::DeleteSelection:
                    if(m_selection->get_selected_asset()) {
                        m_project_panel->request_delete_selection();
                    } else if(const auto entity = m_selection->get_selected_entity(); entity) {
                        const CometEditor::SceneEditor::StructureRequest request{
                            CometEditor::SceneEditor::StructureRequest::Type::Delete,
                            entity.get_uuid(), {}, m_command_history.generation()};
                        if(!m_scene_editor->execute(get_engine().get_scene(), request))
                            LOG_WARN("Cannot delete selected entity");
                    }
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
                case CometEditor::MenuBar::Command::SetStartupScene: {
                    const auto scene = current_saved_scene();
                    if(scene.empty()) {
                        LOG_WARN("Save the current scene before setting it as the startup scene");
                        break;
                    }
                    const auto saved = m_project.save_startup_scene(scene);
                    if(!saved)
                        LOG_ERROR("Cannot set startup scene: {}", saved.error());
                    else
                        LOG_INFO("Startup scene set to '{}'", scene.generic_string());
                    break;
                }
                case CometEditor::MenuBar::Command::ClearStartupScene: {
                    const auto saved = m_project.save_startup_scene({});
                    if(!saved)
                        LOG_ERROR("Cannot clear startup scene: {}", saved.error());
                    else
                        LOG_INFO("Startup scene cleared");
                    break;
                }
            }
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<std::unique_ptr<Comet::Scene>, Comet::Error> activate_candidate_scene(
            std::unique_ptr<Comet::Scene> scene, CometEditor::EditorMode mode) {
            using Activation = Comet::Result<std::unique_ptr<Comet::Scene>, Comet::Error>;
            if(!scene)
                return Activation::failure({"Cannot activate an empty scene"});
            // 缺失引用保留供编辑器修复，不阻止安装候选场景。
            if(auto prepared = m_assets->prepare_scene(*scene, m_component_registry); !prepared)
                return Activation::failure(prepared.error());
            return Activation::success(commit_scene(std::move(scene), mode));
        }

        std::unique_ptr<Comet::Scene> commit_scene(
            std::unique_ptr<Comet::Scene> scene, CometEditor::EditorMode mode) {
            // 旧场景仍存活时结束交互；返回 owner 后才允许调用者销毁或保留它。
            if(m_inspector_panel)
                static_cast<void>(m_inspector_panel->finish_edit(true));
            else
                static_cast<void>(m_property_edit.cancel());
            if(m_viewport)
                m_viewport->panel().cancel_interaction();
            auto previous = get_engine().replace_scene(std::move(scene));
            auto* active = get_engine().get_scene();
            if(mode == CometEditor::EditorMode::Edit && m_command_history.get_scene() != active)
                m_command_history.bind_scene(active);
            if(m_selection) {
                m_selection->set_scene(*active);
                m_hierarchy_panel->reset_for_scene_change();
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

        Comet::Result<void, Comet::Error> setup_panels(Comet::AssetScanReport initial_asset_scan) {
            auto sampler =
                get_engine().get_render_resources().get_sampler_manager().get_nearest_clamp();
            if(!sampler)
                return Comet::Result<void, Comet::Error>::failure(sampler.error().as_error());
            m_menu_bar = std::make_unique<CometEditor::MenuBar>(
                m_editor_state, m_command_history, m_shortcuts);

            m_hierarchy_panel = std::make_unique<CometEditor::HierarchyPanel>(
                *m_selection, m_command_history, m_editor_state, m_scene_editor->clipboard());
            m_viewport = std::make_unique<CometEditor::Viewport>(m_editor_state,
                get_engine().get_scene_runtime(), *m_selection, m_command_history,
                m_component_registry, m_property_edit, m_shortcuts, get_engine().get_renderer(),
                get_engine().get_asset_registry(), *m_imgui_context, std::move(sampler).value());
            m_inspector_panel = std::make_unique<CometEditor::InspectorPanel>(m_editor_state,
                *m_selection, m_command_history, m_property_edit, m_component_registry,
                m_property_editor_registry, m_assets->database(), get_engine().get_asset_registry(),
                get_engine().get_renderer().get_material_programs());
            m_project_panel = std::make_unique<CometEditor::ProjectPanel>(m_assets->database(),
                m_project.paths().assets(), std::move(initial_asset_scan), *m_selection,
                m_command_history);
            m_menu_bar->register_panel(*m_hierarchy_panel);
            m_menu_bar->register_panel(m_viewport->panel());
            m_menu_bar->register_panel(*m_inspector_panel);
            m_menu_bar->register_panel(*m_project_panel);
            m_menu_bar->register_panel(*m_console_panel);
            m_render_stats = std::make_unique<CometEditor::RenderStatsPanel>(
                get_engine().frame_diagnostics(), get_engine().get_renderer().get_diagnostics());
            m_menu_bar->register_panel(*m_render_stats);
            return Comet::Result<void, Comet::Error>::success();
        }

        void draw_editor_ui() {
            const CometEditor::Ui::LanguageScope language(m_ui_language, &m_translations);
            constexpr ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;
            ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), dockspace_flags);

            ImGui::BeginDisabled(m_scene_document->has_pending_request());
            m_menu_bar->render(current_saved_scene(), m_project.startup_scene());
            m_hierarchy_panel->render();
            m_viewport->panel().render();
            m_inspector_panel->render();
            m_project_panel->render();
            ImGui::EndDisabled();
            m_console_panel->render();
            m_render_stats->render();
            m_scene_file_dialog.render();
            draw_unsaved_dialog();
            if(!m_scene_document->has_pending_request())
                m_menu_bar->collect_shortcuts();
        }

        Comet::Result<void, Comet::Error> handle_asset_assignment(
            const CometEditor::InspectorPanel::AssetAssignment& request) {
            if(!finish_active_edit())
                return Comet::Result<void, Comet::Error>::success();
            const auto& asset = request.asset;
            return m_scene_editor->assign_asset(get_engine().get_scene(), request.target,
                {asset.handle, asset.revision, asset.generation, asset.type});
        }

        Comet::Result<void, Comet::Error> handle_mesh_drop(
            const CometEditor::ViewportPanel::MeshDrop& request) {
            if(!finish_active_edit())
                return Comet::Result<void, Comet::Error>::success();
            const auto& asset = request.asset;
            return m_scene_editor->create_mesh(get_engine().get_scene(),
                {asset.handle, asset.revision, asset.generation, asset.type}, request.position);
        }

        Comet::Result<void, Comet::Error> process_editor_requests() {
            if(auto assets = process_asset_requests(); !assets)
                return assets;
            return process_scene_requests();
        }

        Comet::Result<void, Comet::Error> apply_asset_edit(const CometEditor::AssetEdit& edit) {
            if(std::holds_alternative<CometEditor::MaterialEdit>(edit.value))
                return CometEditor::apply_material_edit(
                    *m_assets, get_engine().get_renderer(), edit);
            return m_assets->apply_texture_edit(edit);
        }

        Comet::Result<void, Comet::Error> process_asset_requests() {
            if(const auto request = m_inspector_panel->asset_inspector().take_asset_read())
                m_inspector_panel->asset_inspector().complete_asset_read(
                    *request, m_assets->read_material(*request));
            if(const auto create = m_project_panel->take_create_material_request())
                m_project_panel->complete_create_material(
                    *create, m_assets->create_material(create->destination, create->data));
            if(const auto create = m_project_panel->take_create_script_request())
                m_project_panel->complete_create_script(
                    *create, m_assets->create_script(create->destination));
            if(const auto remove = m_project_panel->take_delete_request())
                m_project_panel->complete_delete(*remove, m_assets->remove(remove->handle));
            if(const auto move = m_project_panel->take_move_request())
                m_project_panel->complete_move(
                    *move, m_assets->move(move->handle, move->destination));
            if(m_project_panel->take_refresh_request())
                m_project_panel->update_scan_report(m_assets->refresh());
            if(const auto edit = m_inspector_panel->asset_inspector().take_asset_edit()) {
                const auto result = apply_asset_edit(*edit);
                std::string error;
                if(!result)
                    error = result.error().message;
                m_inspector_panel->asset_inspector().complete_asset_edit(
                    *edit, static_cast<bool>(result), std::move(error));
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
                if(directory) {
                    if(auto queued = m_assets->queue_import_files(drop.paths, *directory); !queued)
                        LOG_WARN("External file import not queued: {}", queued.error());
                } else {
                    LOG_WARN("Drop external files onto a Project folder or its empty area");
                }
            }
            if(const auto handle = m_project_panel->take_mesh_reimport_request())
                m_assets->request_mesh_reimport(*handle);
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> handle_scene_file_request(
            const CometEditor::SceneFileDialog::Request& request) {
            if(!finish_active_edit())
                return Comet::Result<void, Comet::Error>::success();
            if(request.action == CometEditor::SceneFileDialog::Action::Open) {
                m_scene_document->request({CometEditor::SceneDocument::Action::Open, request.path});
                m_scene_file_dialog.complete(Comet::Result<void, Comet::Error>::success());
                return Comet::Result<void, Comet::Error>::success();
            }
            const auto saved = m_scene_document->save(request.path);
            m_scene_file_dialog.complete(saved);
            if(!saved && is_device_lost(saved.error()))
                return saved;
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> apply_play_command(CometEditor::PlayCommand command) {
            using Command = CometEditor::PlayCommand;
            using State = Comet::SceneRuntime::State;
            using Result = Comet::Result<void, Comet::Error>;
            if(command == Command::Play || command == Command::Stop) {
                if(finish_active_edit()) {
                    auto mode = CometEditor::EditorMode::Edit;
                    if(command == Command::Play)
                        mode = CometEditor::EditorMode::Play;
                    m_scene_session->request_mode(mode);
                }
                return Result::success();
            }
            if(m_editor_state.mode != CometEditor::EditorMode::Play)
                return Result::success();
            if(command == Command::Pause)
                return get_engine().set_runtime_state(State::Paused);
            if(command == Command::Resume)
                return get_engine().set_runtime_state(State::Running);
            return get_engine().request_runtime_step();
        }

        Comet::Result<void, Comet::Error> process_scene_requests() {
            // 一次取走所有当帧请求；未选中的请求不留到新场景或新模式执行。
            const auto hierarchy_request = m_hierarchy_panel->take_request();
            const auto rename_request = m_hierarchy_panel->take_rename_request();
            const auto menu_command = m_menu_bar->take_command();
            const auto mesh_drop = m_viewport->panel().take_mesh_drop();
            const auto asset_assignment = m_inspector_panel->take_asset_assignment();
            const auto play_command = m_viewport->panel().take_play_command();
            const auto file_request = m_scene_file_dialog.take_request();
            const bool dialog_cancelled = m_scene_file_dialog.take_cancelled();
            if(dialog_cancelled)
                m_scene_document->decide(CometEditor::SceneDocument::Decision::Cancel);

            using Kind = CometEditor::SceneRequestKind;
            const auto selected = CometEditor::select_scene_request({
                .file_dialog = file_request.has_value(),
                .menu = menu_command.has_value(),
                .play = play_command.has_value(),
                .structure = hierarchy_request.has_value(),
                .rename = rename_request.has_value(),
                .mesh_drop = mesh_drop.has_value(),
                .asset_assignment = asset_assignment.has_value(),
                .document_pending = m_scene_document->has_pending_request(),
                .dialog_cancelled = dialog_cancelled,
            });
            switch(selected) {
                case Kind::None:
                    break;
                case Kind::FileDialog:
                    return handle_scene_file_request(*file_request);
                case Kind::Menu: {
                    auto result = handle_command(*menu_command);
                    if(!result && is_device_lost(result.error()))
                        return result;
                    break;
                }
                case Kind::Play: {
                    auto result = apply_play_command(*play_command);
                    if(!result) {
                        if(is_device_lost(result.error()))
                            return result;
                        LOG_WARN("Play command rejected: {}", result.error().message);
                    }
                    break;
                }
                case Kind::Structure:
                    handle_scene_request(*hierarchy_request);
                    break;
                case Kind::Rename:
                    if(finish_active_edit()
                        && !m_scene_editor->rename_entity(get_engine().get_scene(),
                            rename_request->entity, rename_request->name,
                            rename_request->generation))
                        LOG_WARN("Entity rename was rejected or had no effect");
                    break;
                case Kind::MeshDrop: {
                    auto result = handle_mesh_drop(*mesh_drop);
                    if(!result) {
                        if(is_device_lost(result.error()))
                            return result;
                        LOG_WARN("Mesh drop rejected: {}", result.error().message);
                    }
                    break;
                }
                case Kind::AssetAssignment: {
                    auto result = handle_asset_assignment(*asset_assignment);
                    if(!result) {
                        if(is_device_lost(result.error()))
                            return result;
                        LOG_WARN("Asset assignment rejected: {}", result.error().message);
                    }
                    break;
                }
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
        CometEditor::Ui::Language m_ui_language = CometEditor::Ui::Language::Chinese;
        CometEditor::Ui::Translations m_translations;
        std::unique_ptr<CometEditor::SceneEditor> m_scene_editor;
        std::unique_ptr<CometEditor::SceneDocument> m_scene_document;
        std::unique_ptr<CometEditor::EditorSceneSession> m_scene_session;
        CometEditor::SceneFileDialog m_scene_file_dialog;

        std::unique_ptr<CometEditor::MenuBar> m_menu_bar;
        std::unique_ptr<CometEditor::HierarchyPanel> m_hierarchy_panel;
        std::unique_ptr<CometEditor::Viewport> m_viewport;
        std::unique_ptr<CometEditor::InspectorPanel> m_inspector_panel;
        std::unique_ptr<CometEditor::ProjectPanel> m_project_panel;
        std::shared_ptr<CometEditor::ConsolePanel> m_console_panel;
        std::unique_ptr<CometEditor::RenderStatsPanel> m_render_stats;
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
