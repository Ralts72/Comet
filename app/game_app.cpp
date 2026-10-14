#include "game_app.h"
#include "gameplay/checkpoint_store.h"
#include "gameplay/game_session.h"
#include "config/player_settings.h"
#include "config/display_settings_preview.h"
#include "runtime/application.h"
#include "config/config_loader.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"
#include "asset/asset_manager.h"
#include "core/project.h"
#include "common/executable_path.h"
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
#include <unordered_set>
#include <vector>

namespace {
    std::optional<std::filesystem::path> packaged_resources() {
        const auto resources = Comet::executable_resource_directory();
        if(resources.empty())
            return std::nullopt;
        if(std::filesystem::exists(resources / "game")
            || std::filesystem::exists(resources / "config.json"))
            return resources;
        return std::nullopt;
    }

    Comet::Ui::RmlContext::Options game_ui_options(const Comet::Project& project) {
        Comet::Ui::RmlContext::Options options;
        options.resource_root = project.paths().assets();
        const auto resources = Comet::executable_resource_directory();
        std::error_code error;
        if(!resources.empty() && std::filesystem::is_directory(resources / "fonts", error))
            options.font_directory = resources / "fonts";
        return options;
    }

    class GameApp final: public Comet::Application {
    public:
        GameApp(Comet::Project project, Comet::PlayerDisplaySettings display_settings,
            Comet::PlayerQualitySettings quality_settings,
            Comet::PlayerAudioSettings audio_settings, std::filesystem::path local_data)
            : Application({.cache_directory = local_data / "cache",
                  .log_directory = local_data / "logs",
                  .window_title = project.name(),
                  .display_settings = display_settings.settings(),
                  .quality_settings = quality_settings.settings()}),
              m_project(std::move(project)), m_display_settings(std::move(display_settings)),
              m_display_preview(m_display_settings),
              m_quality_settings(std::move(quality_settings)),
              m_audio_settings(std::move(audio_settings)) {}

        Comet::Result<void, Comet::Error> on_init() override {
            using Init = Comet::Result<void, Comet::Error>;
            const auto& components = m_components;
            const auto path = m_project.paths().resolve_asset_path(m_project.startup_scene());
            if(!path)
                return Init::failure({path.error()});
            auto loaded = Comet::SceneSerializer(components).load(path.value().string());
            if(!loaded)
                return Init::failure({loaded.error()});
            m_game.begin(std::move(loaded).value(), m_project.startup_scene().generic_string());
            auto initial = m_game.prepare({});
            if(!initial)
                return Init::failure({initial.error()});
            auto scene = std::move(initial).value().scene;

            auto& engine = get_engine();
            m_asset_manager = std::make_unique<Comet::AssetManager>(m_project.paths(),
                engine.get_asset_registry(), engine.get_render_resource_factory(),
                engine.get_task_scheduler(), get_config().assets);
            const auto scan = m_asset_manager->scan();
            for(const auto& issue : scan.issues)
                LOG_WARN(
                    "Asset scan issue at '{}': {}", issue.path.generic_string(), issue.message);
            if(!scan.succeeded()
                && std::filesystem::exists(
                    m_project.paths().root() / Comet::ProductIndex::FILENAME))
                return Init::failure({"Cannot open project asset index"});

            auto save_path = Comet::CheckpointStore::default_path(m_project.id());
            if(!save_path)
                return Init::failure({save_path.error()});
            m_checkpoints.emplace(m_project.id(), std::move(save_path).value());
            configure_checkpoints();
            auto references = prepare_scene_assets(*scene);
            if(!references)
                return Init::failure(references.error());
            LOG_INFO("App project '{}', startup scene '{}'", m_project.paths().root().string(),
                m_project.startup_scene().generic_string());
            m_pending_scene = std::move(scene);
            m_pending_references = std::move(references).value();
            engine.get_window().set_title(m_project.name() + " | Loading assets");
            if(auto applied = engine.apply_audio_settings(m_audio_settings.settings()); !applied)
                return applied;
            if(auto added = engine.add_default_scene_systems(
                   {.read =
                           [this](Comet::AssetHandle handle) {
                               return m_asset_manager->read_mesh_data(handle);
                           },
                       .revision =
                           [this](Comet::AssetHandle handle) {
                               return m_asset_manager->mesh_data_revision(handle);
                           }});
                !added)
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
                            return m_display_preview.apply(settings, current_display_settings(),
                                [this](const auto& candidate) {
                                    return apply_display_settings(candidate);
                                });
                        },
                    .display_confirmation = [this] { return m_display_preview.remaining(); },
                    .confirm_display =
                        [this] {
                            if(get_engine().get_renderer().output_pending())
                                return Comet::Result<void>::failure(
                                    "Display output is still being applied");
                            return m_display_preview.confirm();
                        },
                    .revert_display =
                        [this] {
                            return m_display_preview.revert([this](const auto& previous) {
                                return apply_display_settings(previous);
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
                        },
                    .audio_defaults = m_project.audio_settings(),
                    .load_audio =
                        [this] {
                            return Comet::Result<Comet::AudioSettings>::success(
                                m_audio_settings.settings());
                        },
                    .active_audio = [this] { return get_engine().get_audio_settings(); },
                    .apply_audio =
                        [this](Comet::AudioSettings settings) {
                            return m_audio_settings.save_and_apply(
                                settings, [this](const auto& candidate) {
                                    const auto applied =
                                        get_engine().apply_audio_settings(candidate);
                                    if(!applied)
                                        return Comet::Result<void>::failure(
                                            applied.error().message);
                                    return Comet::Result<void>::success();
                                });
                        },
                    .session_get =
                        [this](std::string_view key) -> std::optional<Comet::ParameterValue> {
                        const auto& runtime = get_engine().get_scene_runtime();
                        if(!runtime.is_active())
                            return std::nullopt;
                        return runtime.get_session().get_value(key);
                    },
                    .emit =
                        [this](std::string_view event) {
                            auto& engine = get_engine();
                            auto* scene = engine.get_scene();
                            return engine.get_scene_runtime().is_active() && scene
                                   && scene->emit_event(event);
                        }});
            if(!ui)
                return Init::failure(ui.error());
            m_ui = std::move(ui).value();
            engine.get_renderer().set_overlay({.capture =
                                                   [this] {
                                                       Comet::OverlayDrawList draws;
                                                       if(auto frame = m_ui->take_draw_frame())
                                                           draws.push_back(std::move(frame));
                                                       return draws;
                                                   },
                .release = [this] { m_ui->release_swapchain_resources(); },
                .rebuild =
                    [this](const Comet::SwapchainCompatibility& compatibility) {
                        return m_ui->rebuild_swapchain_resources(compatibility);
                    },
                .shutdown = [this] { m_ui->shutdown_gpu(); }});
            return Init::success();
        }

        Comet::Result<void, Comet::Error> on_update(
            const Comet::Engine::FrameContext& frame) override {
            if(auto expired = m_display_preview.expire(
                   [this](const auto& previous) { return apply_display_settings(previous); });
                !expired)
                return Comet::Result<void, Comet::Error>::failure({expired.error()});
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
            if(auto request = get_engine().take_checkpoint_request()) {
                if(auto saved = m_game.save_checkpoint(*request); !saved)
                    LOG_ERROR("Cannot save checkpoint: {}", saved.error());
                else
                    LOG_INFO("Saved checkpoint '{}'", request->marker);
            }
            if(!m_pending_scene)
                if(auto request = get_engine().take_runtime_scene_request()) {
                    if(auto transition = request_scene(*request); !transition)
                        return transition;
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
            if(!m_display_preview.is_pending() && display != m_display_settings.settings()) {
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
            m_pending_transition.reset();
            m_game.end();
        }

    private:
        Comet::Result<void> apply_display_settings(const Comet::DisplaySettings& settings) {
            auto& engine = get_engine();
            auto window = engine.get_window().set_display_settings(settings.mode,
                {static_cast<uint32_t>(settings.width), static_cast<uint32_t>(settings.height)});
            if(!window)
                return window;
            engine.get_renderer().set_vsync_enabled(settings.vsync);
            auto output = engine.get_renderer().request_output_settings(settings.output);
            if(!output)
                return Comet::Result<void>::failure(output.error().message);
            auto limit = engine.set_frame_rate_limit(settings.frame_rate_limit);
            if(!limit)
                return Comet::Result<void>::failure(limit.error().message);
            return Comet::Result<void>::success();
        }
        Comet::DisplaySettings current_display_settings() const {
            auto settings = m_display_preview.settings();
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
            if(m_ui)
                m_ui->deactivate();
            m_player_input_settings.reset();
            if(auto configured = configure_player_input(); !configured)
                return configured;
            const auto values =
                m_pending_transition ? m_pending_transition->values : Comet::ParameterMap{};
            if(auto started = get_engine().start_scene_runtime(m_pending_state, values,
                   m_pending_transition ? m_pending_transition->checkpoint : std::string{});
                !started)
                return started;
            if(m_pending_transition) {
                m_game.accept(std::move(*m_pending_transition));
                m_pending_transition.reset();
            }
            return Activation::success();
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

        void configure_checkpoints() {
            m_game.set_persistence(m_checkpoints->persistence(
                [this](std::string_view path) {
                    const auto* record =
                        m_asset_manager->get_database().find(std::filesystem::path(path));
                    return record && record->type == Comet::AssetType::Scene ? record->handle
                                                                             : Comet::AssetHandle{};
                },
                [this](Comet::AssetHandle scene) -> std::optional<std::string> {
                    const auto* record = m_asset_manager->get_database().find(scene);
                    if(!record || record->type != Comet::AssetType::Scene)
                        return std::nullopt;
                    return record->path.generic_string();
                }));
        }

        Comet::Result<std::vector<Comet::AssetReference>, Comet::Error> prepare_scene_assets(
            Comet::Scene& scene) {
            using Prepared = Comet::Result<std::vector<Comet::AssetReference>, Comet::Error>;
            // 开发 App 在关卡加载阶段导入，不把源模型工作放进运行帧。
            auto collected = m_asset_manager->collect_scene_references(scene, m_components);
            if(!collected)
                return Prepared::failure(collected.error());
            auto references = std::move(collected).value();
            if(!m_asset_manager->get_database().is_read_only()) {
                std::unordered_set<Comet::AssetHandle> imported_models;
                for(const auto& reference : references) {
                    const auto* record = m_asset_manager->get_database().find(reference.handle);
                    if(record && record->source_asset) {
                        const auto source = record->source_asset;
                        if(imported_models.insert(source).second)
                            if(auto imported = m_asset_manager->import_model(source); !imported)
                                return Prepared::failure(imported.error());
                    } else if(reference.type == Comet::AssetType::Mesh) {
                        if(auto imported = m_asset_manager->import_mesh(reference.handle);
                            !imported)
                            return Prepared::failure(imported.error());
                    }
                }
            }
            if(auto prepared = m_asset_manager->prepare_references(
                   references, Comet::AssetManager::MissingAssetPolicy::FailRequired);
                !prepared)
                return Prepared::failure(prepared.error());
            return Prepared::success(std::move(references));
        }

        Comet::Result<void, Comet::Error> request_scene(
            const Comet::RuntimeSession::SceneRequest& request) {
            using Requested = Comet::Result<void, Comet::Error>;
            auto prepared = m_game.prepare(request, [this](std::string_view relative) {
                auto path = m_project.paths().resolve_asset_path(relative);
                if(!path)
                    return Comet::Result<std::unique_ptr<Comet::Scene>>::failure(path.error());
                return m_scene_serializer.load(path.value().string());
            });
            if(!prepared) {
                LOG_ERROR("Cannot change scene: {}", prepared.error());
                return Requested::success();
            }
            auto candidate = std::move(prepared).value();
            auto references = prepare_scene_assets(*candidate.scene);
            if(!references) {
                if(is_device_lost(references.error()))
                    return Requested::failure(references.error());
                LOG_ERROR("Cannot prepare scene: {}", references.error().message);
                return Requested::success();
            }
            if(auto reverted = m_display_preview.revert(
                   [this](const auto& previous) { return apply_display_settings(previous); });
                !reverted)
                return Requested::failure({reverted.error()});
            m_pending_state = get_engine().get_scene_runtime().get_state();
            if(auto paused = get_engine().set_runtime_state(Comet::SceneRuntime::State::Paused);
                !paused)
                return paused;
            m_pending_scene = std::move(candidate.scene);
            m_pending_references = std::move(references).value();
            m_pending_transition = std::move(candidate);
            return Requested::success();
        }

        Comet::Project m_project;
        Comet::PlayerDisplaySettings m_display_settings;
        Comet::DisplaySettingsPreview m_display_preview;
        Comet::PlayerQualitySettings m_quality_settings;
        Comet::PlayerAudioSettings m_audio_settings;
        Comet::Input::Gate m_input_gate;
        std::unique_ptr<Comet::Ui::ProjectUi> m_ui;
        std::optional<Comet::PlayerInputSettings> m_player_input_settings;
        bool m_ui_blocked = false;
        bool m_ui_pointer_blocked = false;
        std::unique_ptr<Comet::AssetManager> m_asset_manager;
        std::unique_ptr<Comet::Scene> m_pending_scene;
        Comet::ComponentRegistry m_components = Comet::create_scene_component_registry();
        Comet::SceneSerializer m_scene_serializer{m_components};
        Comet::GameSession m_game{m_scene_serializer};
        std::optional<Comet::CheckpointStore> m_checkpoints;
        std::optional<Comet::GameSession::Transition> m_pending_transition;
        Comet::SceneRuntime::State m_pending_state = Comet::SceneRuntime::State::Running;
        std::vector<Comet::AssetReference> m_pending_references;
        int m_displayed_fps = -1;
    };

}

namespace CometApp {
    Comet::Result<void, Comet::Error> run_game(const Comet::LaunchOptions& options) {
        using RunResult = Comet::Result<void, Comet::Error>;
        const auto resources = packaged_resources();
        std::filesystem::path project_path = COMET_SAMPLE_PROJECT_DIRECTORY;
        if(resources)
            project_path = *resources / "game";
        if(options.project_path)
            project_path = *options.project_path;
        auto project = Comet::Project::load(project_path);
        if(!project)
            return RunResult::failure({project.error()});
        if(resources) {
            if(!std::filesystem::is_regular_file(
                   project.value().paths().root() / Comet::ProductIndex::FILENAME))
                return RunResult::failure({"Packaged application requires a product index"});
            if(project.value().ui()
                && (!std::filesystem::is_directory(*resources / "ui")
                    || !std::filesystem::is_directory(*resources / "fonts")))
                return RunResult::failure({"Packaged application UI resources are missing"});
        }
        const auto config_file = resources ? *resources / "config.json" : options.config_file;
        const auto profile = resources ? "app-release" : options.config_profile;
        auto config = Comet::ConfigLoader{}.load(config_file.string(), profile);
        if(!config)
            return RunResult::failure({config.error()});
        auto display = Comet::PlayerDisplaySettings::load(
            project.value().id(), project.value().display_settings());
        if(!display)
            return RunResult::failure({display.error()});
        auto quality = Comet::PlayerQualitySettings::load(
            project.value().id(), project.value().quality_settings());
        if(!quality)
            return RunResult::failure({quality.error()});
        auto audio = Comet::PlayerAudioSettings::load(
            project.value().id(), project.value().audio_settings());
        if(!audio)
            return RunResult::failure({audio.error()});
        auto local_data = project.value().paths().local_data();
        if(std::filesystem::exists(project.value().paths().root() / Comet::ProductIndex::FILENAME))
            local_data = display.value().path().parent_path();
        GameApp app(std::move(project).value(), std::move(display).value(),
            std::move(quality).value(), std::move(audio).value(), std::move(local_data));
        return app.run(std::move(config).value());
    }
}
