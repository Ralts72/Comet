#include "runtime/entry.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"
#include "asset/asset_manager.h"
#include "core/project.h"
#include "core/window.h"
#include "input/player_input_settings.h"
#include "common/scope_exit.h"
#include "imgui_context.h"
#include "input_widgets.h"
#include "player_input_panel.h"
#include "render/renderer.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"

#include <cmath>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
    class GameApp final: public Comet::Application {
    public:
        explicit GameApp(Comet::Project project)
            : Application({.cache_directory = project.paths().cache(),
                  .log_directory = project.paths().logs(),
                  .window_title = project.name()}),
              m_project(std::move(project)) {}

        Comet::Result<void, Comet::Error> on_init() override {
            using Init = Comet::Result<void, Comet::Error>;
            const auto components = Comet::create_scene_component_registry();
            const auto path = m_project.paths().resolve_asset_path(m_project.startup_scene());
            if(!path)
                return Init::failure({path.error()});
            auto loaded = Comet::SceneSerializer(components).load(path.value().string());
            if(!loaded)
                return Init::failure({loaded.error()});
            auto scene = std::move(loaded).value();
            auto initial = Comet::SceneSerializer(components).clone(*scene);
            if(!initial)
                return Init::failure({initial.error()});
            m_initial_scene = std::move(initial).value();

            auto& engine = get_engine();
            m_asset_manager = std::make_unique<Comet::AssetManager>(m_project.paths(),
                engine.get_asset_registry(), engine.get_render_resources(),
                engine.get_task_scheduler(), get_config().assets);
            const auto scan = m_asset_manager->scan();
            for(const auto& issue : scan.issues)
                LOG_WARN(
                    "Asset scan issue at '{}': {}", issue.path.generic_string(), issue.message);

            // 开发期 app 在启动阶段补齐 Artifact，不把源模型导入放进运行帧。
            auto references = components.collect_asset_references(
                *scene, Comet::ComponentRegistry::ReferenceScope::Runtime);
            for(const auto& reference : references) {
                if(reference.type == Comet::AssetType::Mesh) {
                    if(auto imported = m_asset_manager->import_mesh(reference.handle); !imported)
                        return Init::failure(imported.error());
                }
            }
            if(auto prepared = m_asset_manager->prepare_references(
                   references, Comet::AssetManager::MissingAssetPolicy::FailRequired);
                !prepared)
                return Init::failure(prepared.error());
            LOG_INFO("App project '{}', startup scene '{}'", m_project.paths().root().string(),
                m_project.startup_scene().generic_string());
            m_pending_scene = std::move(scene);
            m_pending_references = references;
            engine.get_window().set_title(m_project.name() + " | Loading assets");
            if(auto configured = configure_player_input(); !configured)
                return configured;
            if(auto added = engine.add_default_scene_systems(); !added)
                return added;
            auto ui = CometUi::ImGuiContext::create(
                engine.get_window(), engine.get_renderer().get_render_context(), {});
            if(!ui)
                return Init::failure(ui.error().as_error());
            m_ui = std::move(ui).value();
            engine.get_renderer().set_overlay(
                {.render = [this](Comet::CommandBuffer& command) { m_ui->render(command); },
                    .release = [this] { m_ui->release_swapchain_resources(); },
                    .rebuild =
                        [this](const Comet::SwapchainCompatibility& compatibility) {
                            return m_ui->rebuild_swapchain_resources(compatibility);
                        }});
            return Init::success();
        }

        Comet::Result<void, Comet::Error> on_update(Comet::Engine::FrameContext& frame) override {
            if(!m_player_input_panel.is_open() && !m_ui_blocked
                && frame.physical_input.key(Comet::Input::Key::Escape).pressed) {
                get_engine().get_window().request_close();
                return Comet::Result<void, Comet::Error>::success();
            }
            const auto fps = static_cast<int>(std::round(frame.update.fps));
            if(!m_pending_scene && frame.update.fps > 0.0f && fps != m_displayed_fps) {
                get_engine().get_window().set_title(
                    m_project.name() + " | " + std::to_string(fps) + " FPS");
                m_displayed_fps = fps;
            }
            if(auto assets = m_asset_manager->process_completions(); !assets)
                return Comet::Result<void, Comet::Error>::failure(assets.error());
            if(auto* scene = get_engine().get_scene(); scene && scene->take_restart_request()) {
                if(auto restarted = restart_scene(); !restarted)
                    return restarted;
            }
            if(m_pending_scene) {
                auto ready = m_asset_manager->references_ready(
                    m_pending_references, Comet::AssetManager::MissingAssetPolicy::FailRequired);
                if(!ready)
                    return Comet::Result<void, Comet::Error>::failure(ready.error());
                if(ready.value()) {
                    get_engine().set_scene(std::move(m_pending_scene));
                    m_pending_references.clear();
                    if(auto started = get_engine().start_scene_runtime(); !started)
                        return started;
                }
            }
            // 延期帧没有 UI 回调，仍保持菜单对游戏输入的阻断。
            m_input_before_ui = m_input_gate;
            frame.runtime_input = m_input_gate.read(frame.physical_input,
                !m_pending_scene && !m_player_input_panel.is_open() && !m_ui_blocked,
                !m_ui_pointer_blocked);
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_frame_ready(
            Comet::Engine::FrameContext& frame) override {
            // Runtime 尚未消费 fallback；按最终 UI 授权重算，不消费同一物理帧两次。
            m_input_gate = m_input_before_ui;
            if(!m_ui->begin_frame()) {
                frame.runtime_input = m_input_gate.read(frame.physical_input, false);
                return Comet::Result<void, Comet::Error>::success();
            }
            const Comet::ScopeExit end_ui([this] { m_ui->end_frame(); });
            render_input_entry();
            m_ui_blocked = m_player_input_panel.render(frame.physical_input);
            if(auto requested = m_player_input_panel.take_request()) {
                const auto applied = apply_player_input(std::move(*requested));
                m_player_input_panel.complete(applied);
                if(!applied)
                    LOG_WARN("Cannot apply player input: {}", applied.error());
            }
            if(!m_player_input_panel.is_open())
                m_player_input_settings.reset();
            const bool close_error = frame.physical_input.focused
                                     && frame.physical_input.key(Comet::Input::Key::Escape).pressed;
            m_ui_blocked |= CometUi::render_player_input_error(m_input_error, close_error);
            m_ui_pointer_blocked = ImGui::GetIO().WantCaptureMouse;
            frame.runtime_input = m_input_gate.read(
                frame.physical_input, !m_pending_scene && !m_ui_blocked, !m_ui_pointer_blocked);
            return Comet::Result<void, Comet::Error>::success();
        }

        void on_shutdown() override {
            LOG_INFO("app shutdown");
            get_engine().get_renderer().set_overlay({});
            m_player_input_panel.close();
            m_player_input_settings.reset();
            m_ui.reset();
            m_asset_manager.reset();
            m_pending_scene.reset();
            m_initial_scene.reset();
        }

    private:
        void render_input_entry() {
            const auto* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(
                {viewport->WorkPos.x + viewport->WorkSize.x - 12, viewport->WorkPos.y + 12},
                ImGuiCond_Always, {1, 0});
            constexpr auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                                   | ImGuiWindowFlags_AlwaysAutoResize
                                   | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            ImGui::Begin("##Runtime Input", nullptr, flags);
            ImGui::BeginDisabled(!get_engine().get_scene_runtime().is_active());
            if(ImGui::Button("Input")) {
                if(auto opened = open_player_input(); !opened) {
                    m_input_error = opened.error();
                }
            }
            ImGui::EndDisabled();
            ImGui::End();
        }

        Comet::Result<void> open_player_input() {
            if(m_player_input_panel.is_open())
                return Comet::Result<void>::success();
            auto settings = Comet::PlayerInputSettings::load(m_project.id());
            if(!settings)
                return Comet::Result<void>::failure(settings.error());
            m_player_input_settings = std::move(settings).value();
            constexpr Comet::Input::Key reserved_keys[]{Comet::Input::Key::Escape};
            m_player_input_panel.open(
                m_project.input_actions(), m_player_input_settings->overrides(), reserved_keys);
            return Comet::Result<void>::success();
        }

        Comet::Result<void> apply_player_input(Comet::InputOverrides overrides) {
            if(!m_player_input_settings || !get_engine().get_scene_runtime().is_active())
                return Comet::Result<void>::failure("Player input settings require an active game");
            auto resolved = overrides.resolve(m_project.input_actions());
            if(!resolved)
                return Comet::Result<void>::failure(resolved.error());
            if(auto saved = m_player_input_settings->save(std::move(overrides)); !saved)
                return saved;
            if(auto applied =
                    get_engine().rebind_input_actions(std::move(resolved).value().actions);
                !applied)
                return Comet::Result<void>::failure(
                    "Player settings saved but not applied: " + applied.error().message);
            return Comet::Result<void>::success();
        }

        Comet::Result<void, Comet::Error> configure_player_input() {
            auto settings = Comet::PlayerInputSettings::load(m_project.id());
            if(!settings) {
                LOG_WARN("Player input settings unavailable; using project defaults: {}",
                    settings.error());
                return get_engine().set_input_actions(m_project.input_actions());
            }
            auto resolved = settings.value().overrides().resolve(m_project.input_actions());
            if(!resolved)
                return Comet::Result<void, Comet::Error>::failure({resolved.error()});
            for(const auto& issue : resolved.value().issues)
                LOG_WARN("Player input override ignored for action '{}', binding '{}': {}",
                    issue.action.to_string(), issue.binding.to_string(), issue.message);
            return get_engine().set_input_actions(std::move(resolved).value().actions);
        }

        Comet::Result<void, Comet::Error> restart_scene() {
            const auto components = Comet::create_scene_component_registry();
            auto candidate = Comet::SceneSerializer(components).clone(*m_initial_scene);
            if(!candidate) {
                LOG_ERROR("Cannot restart scene: {}", candidate.error());
                return Comet::Result<void, Comet::Error>::success();
            }
            get_engine().set_scene(std::move(candidate).value());
            m_player_input_panel.close();
            m_player_input_settings.reset();
            m_input_error.clear();
            if(auto configured = configure_player_input(); !configured)
                return configured;
            return get_engine().start_scene_runtime();
        }

        Comet::Project m_project;
        Comet::Input::Gate m_input_gate;
        Comet::Input::Gate m_input_before_ui;
        std::unique_ptr<CometUi::ImGuiContext> m_ui;
        CometUi::PlayerInputPanel m_player_input_panel;
        std::optional<Comet::PlayerInputSettings> m_player_input_settings;
        std::string m_input_error;
        bool m_ui_blocked = false;
        bool m_ui_pointer_blocked = false;
        std::unique_ptr<Comet::AssetManager> m_asset_manager;
        std::unique_ptr<Comet::Scene> m_pending_scene;
        std::unique_ptr<Comet::Scene> m_initial_scene;
        std::vector<Comet::AssetReference> m_pending_references;
        int m_displayed_fps = -1;
    };

    Comet::Result<std::unique_ptr<Comet::Application>> create_game_app(
        Comet::ApplicationArguments arguments) {
        if(arguments.size() > 1 || (!arguments.empty() && arguments.front().starts_with('-')))
            return Comet::Result<std::unique_ptr<Comet::Application>>::failure(
                "Expected a project directory or project.json");
        auto project = Comet::Project::load(
            arguments.empty() ? COMET_SAMPLE_PROJECT_DIRECTORY : arguments.front());
        if(!project)
            return Comet::Result<std::unique_ptr<Comet::Application>>::failure(project.error());
        return Comet::Result<std::unique_ptr<Comet::Application>>::success(
            std::make_unique<GameApp>(std::move(project).value()));
    }
}

RUN_APP(create_game_app, "[project-directory | project.json]")
