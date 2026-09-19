#include "runtime/entry.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"
#include "asset/asset_manager.h"
#include "core/project.h"
#include "core/window.h"
#include "scene/scene.h"
#include "scene/component_registry.h"
#include "scene/scene_serializer.h"

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace {
    class GameApp final: public Comet::Application {
    public:
        explicit GameApp(Comet::Project project, const bool rotate_demo)
            : Application(project.paths().cache()), m_project(std::move(project)) {
            if(rotate_demo)
                m_rotating_entity = Comet::EntityUuid::parse("672cd0cc-501f-419e-af5e-a883a0cd3d02")
                                        .value_or(Comet::INVALID_ENTITY_UUID);
        }

        Comet::Result<void, Comet::Error> on_init() override {
            using Init = Comet::Result<void, Comet::Error>;
            const auto components = Comet::create_scene_component_registry();
            auto scene = std::make_unique<Comet::Scene>();
            if(!m_project.startup_scene().empty()) {
                const auto path = m_project.paths().resolve_asset_path(m_project.startup_scene());
                if(!path)
                    return Init::failure({path.error()});
                auto loaded = Comet::SceneSerializer(components).load(path.value().string());
                if(!loaded)
                    return Init::failure({loaded.error()});
                scene = std::move(loaded).value();
            }

            auto& engine = get_engine();
            m_window_title = engine.get_window().get_title();
            m_asset_manager = std::make_unique<Comet::AssetManager>(m_project.paths(),
                engine.get_asset_registry(), engine.get_render_resources(),
                engine.get_task_scheduler());
            const auto scan = m_asset_manager->scan();
            for(const auto& issue : scan.issues)
                LOG_WARN(
                    "Asset scan issue at '{}': {}", issue.path.generic_string(), issue.message);

            // 开发期 app 在启动阶段补齐 Artifact，不把源模型导入放进运行帧。
            const auto references = components.collect_asset_references(*scene);
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
            LOG_INFO("App project '{}', startup scene '{}', demo rotation {}",
                m_project.paths().root().string(), m_project.startup_scene().generic_string(),
                static_cast<bool>(m_rotating_entity));
            engine.set_scene(std::move(scene));
            return Init::success();
        }

        Comet::Result<void, Comet::Error> on_update(Comet::UpdateContext context) override {
            const auto fps = static_cast<int>(std::round(context.fps));
            if(context.fps > 0.0f && fps != m_displayed_fps) {
                get_engine().get_window().set_title(
                    m_window_title + " | " + std::to_string(fps) + " FPS");
                m_displayed_fps = fps;
            }
            if(auto assets = m_asset_manager->process_completions(); !assets)
                return Comet::Result<void, Comet::Error>::failure(assets.error());
            auto* scene = get_engine().get_scene();
            if(scene && m_rotating_entity) {
                if(auto cube = scene->find_entity(m_rotating_entity))
                    cube.get_component<Comet::TransformComponent>().rotate(
                        {0.0f, context.delta_time * 100.0f, 0.0f});
            }
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_shutdown() override {
            LOG_INFO("app shutdown");
            m_asset_manager.reset();
            return Comet::Result<void, Comet::Error>::success();
        }

    private:
        Comet::Project m_project;
        Comet::EntityUuid m_rotating_entity = Comet::INVALID_ENTITY_UUID;
        std::unique_ptr<Comet::AssetManager> m_asset_manager;
        std::string m_window_title;
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
        std::error_code error;
        const bool is_demo = std::filesystem::equivalent(
            project.value().paths().root(), COMET_SAMPLE_PROJECT_DIRECTORY, error);
        return Comet::Result<std::unique_ptr<Comet::Application>>::success(
            std::make_unique<GameApp>(std::move(project).value(), is_demo && !error));
    }
}

RUN_APP(create_game_app, "[project-directory | project.json]")
