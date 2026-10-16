#include "game_app.h"
#include "gameplay/checkpoint_store.h"
#include "gameplay/game_session.h"
#include "config/player_settings_owner.h"
#include "runtime/application.h"
#include "config/config_loader.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"
#include "asset/asset_manager.h"
#include "core/project.h"
#include "common/executable_path.h"
#include "core/window.h"
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
        GameApp(Comet::Project project, Comet::PlayerSettingsOwner::Startup settings,
            std::filesystem::path local_data)
            : Application({.cache_directory = local_data / "cache",
                  .log_directory = local_data / "logs",
                  .window_title = project.name(),
                  .display_settings = settings.display.settings(),
                  .quality_settings = settings.quality.settings()}),
              m_project(std::move(project)), m_player_settings(m_project, std::move(settings)) {}

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
            m_player_settings.bind_app(engine);
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
            auto references = m_asset_manager->prepare_scene(*scene, m_components);
            if(!references)
                return Init::failure(references.error());
            LOG_INFO("App project '{}', startup scene '{}'", m_project.paths().root().string(),
                m_project.startup_scene().generic_string());
            m_pending_scene = std::move(scene);
            m_pending_references = std::move(references).value();
            engine.get_window().set_title(m_project.name() + " | Loading assets");
            if(auto applied = m_player_settings.activate_audio(); !applied)
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
            auto services = m_player_settings.ui_services();
            services.session_get =
                [this](std::string_view key) -> std::optional<Comet::ParameterValue> {
                const auto& runtime = get_engine().get_scene_runtime();
                if(!runtime.is_active())
                    return std::nullopt;
                return runtime.get_session().get_value(key);
            };
            services.emit = [this](std::string_view event) {
                auto& engine = get_engine();
                auto* scene = engine.get_scene();
                return engine.get_scene_runtime().is_active() && scene && scene->emit_event(event);
            };
            auto ui = Comet::Ui::ProjectUi::create(engine.get_window(), engine.get_renderer(),
                *m_project.ui(), game_ui_options(m_project), std::move(services));
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
            if(auto expired = m_player_settings.expire_display(); !expired)
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
            if(auto saved = m_player_settings.save_window_state(); !saved)
                LOG_WARN("Cannot save game window state: {}", saved.error());
            get_engine().get_renderer().set_overlay({});
            if(m_ui)
                m_ui->deactivate();
            m_player_settings.reset_input();
            m_ui.reset();
            m_asset_manager.reset();
            m_pending_scene.reset();
            m_pending_transition.reset();
            m_game.end();
        }

    private:
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
            if(m_ui)
                m_ui->deactivate();
            m_player_settings.reset_input();
            if(auto configured = m_player_settings.configure_input(); !configured)
                return configured;
            const auto values =
                m_pending_transition ? m_pending_transition->values : Comet::ParameterMap{};
            if(auto started = get_engine().start_scene_runtime(m_pending_state, values,
                   m_pending_transition ? m_pending_transition->checkpoint : std::string{});
                !started)
                return started;
            if(auto prepared = get_engine().prepare_scene_rendering(); !prepared)
                return prepared;
            if(m_pending_transition) {
                m_game.accept(std::move(*m_pending_transition));
                m_pending_transition.reset();
            }
            Comet::AssetManager::ResidencyRelease released;
            auto demands =
                m_asset_manager->collect_scene_references(*get_engine().get_scene(), m_components);
            if(demands) {
                demands.value().insert(demands.value().end(), m_pending_references.begin(),
                    m_pending_references.end());
                released = m_asset_manager->release_unused(demands.value());
            } else {
                LOG_WARN(
                    "Keeping asset residency because active scene demands could not be collected: {}",
                    demands.error().message);
            }
            m_pending_references.clear();
            const auto resident = m_asset_manager->residency();
            LOG_INFO(
                "Asset residency: {} registered, {} CPU models; released {} registered and {} model cache entries",
                resident.registry_assets, resident.model_cache_entries, released.registry_assets,
                released.model_cache_entries);
            return Activation::success();
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
            auto references = m_asset_manager->prepare_scene(*candidate.scene, m_components);
            if(!references) {
                if(is_device_lost(references.error()))
                    return Requested::failure(references.error());
                LOG_ERROR("Cannot prepare scene: {}", references.error().message);
                return Requested::success();
            }
            if(auto reverted = m_player_settings.revert_display(); !reverted)
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
        Comet::PlayerSettingsOwner m_player_settings;
        Comet::Input::Gate m_input_gate;
        std::unique_ptr<Comet::Ui::ProjectUi> m_ui;
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
        auto settings = Comet::PlayerSettingsOwner::load_startup(project.value());
        if(!settings)
            return RunResult::failure({settings.error()});
        auto local_data = project.value().paths().local_data();
        if(std::filesystem::exists(project.value().paths().root() / Comet::ProductIndex::FILENAME))
            local_data = settings.value().display.path().parent_path();
        GameApp app(std::move(project).value(), std::move(settings).value(), std::move(local_data));
        return app.run(std::move(config).value());
    }
}
