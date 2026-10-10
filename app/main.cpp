#include "config/player_display_settings.h"
#include "config/player_quality_settings.h"
#include "runtime/entry.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"
#include "asset/asset_manager.h"
#include "core/project.h"
#include "core/window.h"
#include "input/player_input_settings.h"
#include "ui/project_ui.h"
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

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
    std::filesystem::path executable_directory() {
        std::error_code error;
#if defined(__APPLE__)
        std::vector<char> path(1024);
        auto size = static_cast<std::uint32_t>(path.size());
        if(_NSGetExecutablePath(path.data(), &size) != 0) {
            path.resize(size);
            if(_NSGetExecutablePath(path.data(), &size) != 0)
                return {};
        }
        return std::filesystem::weakly_canonical(path.data(), error).parent_path();
#elif defined(_WIN32)
        std::vector<wchar_t> path(32768);
        const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        return size == 0 || size == path.size()
                   ? std::filesystem::path{}
                   : std::filesystem::path(std::wstring(path.data(), size)).parent_path();
#else
        const auto path = std::filesystem::read_symlink("/proc/self/exe", error);
        return error ? std::filesystem::path{} : path.parent_path();
#endif
    }

    Comet::Ui::RmlContext::Options game_ui_options(const Comet::Project& project) {
        Comet::Ui::RmlContext::Options options;
        options.resource_root = project.paths().assets();
        std::error_code error;
        const auto executable = executable_directory();
        if(!executable.empty()) {
            const std::filesystem::path candidates[]{
                executable / "resources", executable.parent_path() / "Resources"};
            for(const auto& candidate : candidates) {
                if(std::filesystem::is_directory(candidate / "fonts", error)) {
                    options.font_directory = candidate / "fonts";
                    break;
                }
            }
        }
        return options;
    }

    class GameApp final: public Comet::Application {
    public:
        GameApp(Comet::Project project, Comet::PlayerDisplaySettings display_settings,
            Comet::PlayerQualitySettings quality_settings)
            : Application({.cache_directory = project.paths().cache(),
                  .log_directory = project.paths().logs(),
                  .window_title = project.name(),
                  .display_settings = display_settings.settings(),
                  .quality_settings = quality_settings.settings()}),
              m_project(std::move(project)), m_display_settings(std::move(display_settings)),
              m_quality_settings(std::move(quality_settings)) {}

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
            if(!m_project.ui())
                return Init::success();
            auto ui = Comet::Ui::ProjectUi::create(engine.get_window(), engine.get_renderer(),
                *m_project.ui(), game_ui_options(m_project),
                {.input_actions = m_project.input_actions(),
                    .load_input = [this] { return load_player_input(); },
                    .apply_input =
                        [this](Comet::InputOverrides overrides) {
                            return apply_player_input(std::move(overrides));
                        },
                    .display_defaults = m_project.display_settings(),
                    .load_display =
                        [this] {
                            return Comet::Result<Comet::DisplaySettings>::success(
                                current_display_settings());
                        },
                    .apply_display =
                        [this](Comet::DisplaySettings settings) {
                            return m_display_settings.save_and_apply(
                                settings, [this](const auto& candidate) {
                                    auto& engine = get_engine();
                                    auto applied =
                                        engine.get_window().set_display_settings(candidate.mode,
                                            {static_cast<uint32_t>(candidate.width),
                                                static_cast<uint32_t>(candidate.height)});
                                    if(!applied)
                                        return applied;
                                    engine.get_renderer().set_vsync_enabled(candidate.vsync);
                                    return Comet::Result<void>::success();
                                });
                        },
                    .quality_defaults = m_project.quality_settings(),
                    .load_quality =
                        [this] {
                            return Comet::Result<Comet::QualitySettings>::success(
                                m_quality_settings.settings());
                        },
                    .apply_quality =
                        [this](Comet::QualitySettings settings) {
                            return m_quality_settings.save_and_apply(
                                settings, [this](const auto& candidate) {
                                    const auto applied =
                                        get_engine().get_renderer().request_quality_settings(
                                            candidate);
                                    if(!applied)
                                        return Comet::Result<void>::failure(
                                            applied.error().message);
                                    return Comet::Result<void>::success();
                                });
                        }});
            if(!ui)
                return Init::failure(ui.error());
            m_ui = std::move(ui).value();
            engine.get_renderer().set_overlay(
                {.render = [this](
                               Comet::OverlayRecordContext& frame) { return m_ui->render(frame); },
                    .release = [this] { m_ui->release_swapchain_resources(); },
                    .rebuild =
                        [this](const Comet::SwapchainCompatibility& compatibility) {
                            return m_ui->rebuild_swapchain_resources(compatibility);
                        }});
            return Init::success();
        }

        Comet::Result<void, Comet::Error> on_update(
            const Comet::Engine::FrameContext& frame) override {
            if((!m_ui || !m_ui->is_modal()) && !m_ui_blocked
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
            if(get_engine().take_runtime_restart_request()) {
                if(auto restarted = restart_scene(); !restarted)
                    return restarted;
            }
            if(auto activated = activate_pending_scene(); !activated)
                return activated;
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_frame_ready(
            const Comet::Engine::FrameContext& frame) override {
            if(m_ui) {
                const auto result = m_ui->frame(frame.physical_input,
                    {.fps = frame.update.fps,
                        .game_available = get_engine().get_scene_runtime().is_active(),
                        .vsync_active = get_engine().get_renderer().is_vsync_enabled()});
                if(!result)
                    return Comet::Result<void, Comet::Error>::failure(result.error());
                m_ui_blocked = result.value().blocked;
                m_ui_pointer_blocked = result.value().pointer_blocked;
            }
            return Comet::Result<void, Comet::Error>::success();
        }

        std::optional<Comet::Input::Frame> on_runtime_input(
            const Comet::Engine::FrameContext& frame) override {
            return m_input_gate.read(frame.physical_input,
                !m_pending_scene && (!m_ui || !m_ui->is_modal()) && !m_ui_blocked,
                !m_ui_pointer_blocked);
        }

        void on_shutdown() override {
            LOG_INFO("app shutdown");
            const auto display = current_display_settings();
            if(display != m_display_settings.settings()) {
                if(auto saved = m_display_settings.save(display); !saved)
                    LOG_WARN("Cannot save game window state: {}", saved.error());
            }
            get_engine().get_renderer().set_overlay({});
            if(m_ui)
                m_ui->deactivate();
            m_player_input_settings.reset();
            m_ui.reset();
            m_asset_manager.reset();
            m_pending_scene.reset();
            m_initial_scene.reset();
        }

    private:
        Comet::DisplaySettings current_display_settings() const {
            auto settings = m_display_settings.settings();
            const auto& window = get_engine().get_window();
            const auto size = window.get_restore_size();
            settings.width = static_cast<int>(size.x);
            settings.height = static_cast<int>(size.y);
            settings.mode = window.get_mode();
            return settings;
        }
        Comet::Result<void, Comet::Error> activate_pending_scene() {
            using Activation = Comet::Result<void, Comet::Error>;
            if(!m_pending_scene)
                return Activation::success();
            const auto ready = m_asset_manager->references_ready(
                m_pending_references, Comet::AssetManager::MissingAssetPolicy::FailRequired);
            if(!ready)
                return Activation::failure(ready.error());
            if(!ready.value())
                return Activation::success();
            get_engine().set_scene(std::move(m_pending_scene));
            m_pending_references.clear();
            return get_engine().start_scene_runtime();
        }

        Comet::Result<Comet::InputOverrides> load_player_input() {
            if(!m_player_input_settings) {
                auto loaded = Comet::PlayerInputSettings::load(m_project.id());
                if(!loaded)
                    return Comet::Result<Comet::InputOverrides>::failure(loaded.error());
                m_player_input_settings = std::move(loaded).value();
            }
            return Comet::Result<Comet::InputOverrides>::success(
                m_player_input_settings->overrides());
        }

        Comet::Result<void> apply_player_input(Comet::InputOverrides overrides) {
            if(!m_player_input_settings || !get_engine().get_scene_runtime().is_active())
                return Comet::Result<void>::failure("Player input settings require an active game");
            return m_player_input_settings->save_and_apply(m_project.input_actions(),
                std::move(overrides), [this](Comet::InputActions actions) {
                    return get_engine().rebind_input_actions(std::move(actions));
                });
        }

        Comet::Result<void, Comet::Error> configure_player_input() {
            auto overrides = load_player_input();
            if(!overrides) {
                LOG_WARN("Player input settings unavailable; using project defaults: {}",
                    overrides.error());
                return get_engine().set_input_actions(m_project.input_actions());
            }
            auto resolved = overrides.value().resolve(m_project.input_actions());
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
            if(m_ui)
                m_ui->deactivate();
            m_player_input_settings.reset();
            if(auto configured = configure_player_input(); !configured)
                return configured;
            return get_engine().start_scene_runtime();
        }

        Comet::Project m_project;
        Comet::PlayerDisplaySettings m_display_settings;
        Comet::PlayerQualitySettings m_quality_settings;
        Comet::Input::Gate m_input_gate;
        std::unique_ptr<Comet::Ui::ProjectUi> m_ui;
        std::optional<Comet::PlayerInputSettings> m_player_input_settings;
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
        auto display = Comet::PlayerDisplaySettings::load(
            project.value().id(), project.value().display_settings());
        if(!display)
            return Comet::Result<std::unique_ptr<Comet::Application>>::failure(display.error());
        auto quality = Comet::PlayerQualitySettings::load(
            project.value().id(), project.value().quality_settings());
        if(!quality)
            return Comet::Result<std::unique_ptr<Comet::Application>>::failure(quality.error());
        return Comet::Result<std::unique_ptr<Comet::Application>>::success(
            std::make_unique<GameApp>(std::move(project).value(), std::move(display).value(),
                std::move(quality).value()));
    }
}

RUN_APP(create_game_app, "[project-directory | project.json]")
