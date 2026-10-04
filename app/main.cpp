#include "runtime/entry.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"
#include "asset/asset_manager.h"
#include "core/project.h"
#include "core/window.h"
#include "input/player_input_settings.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"

#include <cmath>
#include <memory>
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
            return Init::success();
        }

        Comet::Result<void, Comet::Error> on_update(Comet::Engine::FrameContext& frame) override {
            if(frame.physical_input.key(Comet::Input::Key::Escape).pressed) {
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
            frame.runtime_input = m_input_gate.read(frame.physical_input, !m_pending_scene);
            return Comet::Result<void, Comet::Error>::success();
        }

        void on_shutdown() override {
            LOG_INFO("app shutdown");
            m_asset_manager.reset();
            m_pending_scene.reset();
            m_initial_scene.reset();
        }

    private:
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
            return get_engine().start_scene_runtime();
        }

        Comet::Project m_project;
        Comet::Input::Gate m_input_gate;
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
