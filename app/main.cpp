#include "runtime/entry.h"
#include "render/resource/render_resources.h"
#include "diagnostics/logger.h"
#include "asset/asset_manager.h"
#include "core/project_paths.h"
#include "scene/scene.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace {
    const std::filesystem::path DEMO_MESH = "meshes/cube.gltf";
    const std::filesystem::path DEMO_MATERIAL = "materials/demo.mat";

    Comet::Result<Comet::AssetHandle, Comet::Error> load_required_mesh(
        Comet::AssetManager& asset_manager, const std::filesystem::path& relative_path) {
        const Comet::AssetRecord* record = asset_manager.get_database().find(relative_path);
        if(!record) {
            return Comet::Result<Comet::AssetHandle, Comet::Error>::failure(
                {"Required mesh asset is not indexed: " + relative_path.generic_string()});
        }
        const auto handle = record->handle;
        if(auto imported = asset_manager.import_mesh(handle); !imported)
            return Comet::Result<Comet::AssetHandle, Comet::Error>::failure(imported.error());
        if(auto loaded = asset_manager.load_mesh(handle); !loaded)
            return Comet::Result<Comet::AssetHandle, Comet::Error>::failure(loaded.error());
        return Comet::Result<Comet::AssetHandle, Comet::Error>::success(handle);
    }

    Comet::Result<Comet::AssetHandle, Comet::Error> load_required_material(
        Comet::AssetManager& asset_manager, const std::filesystem::path& relative_path) {
        const Comet::AssetRecord* record = asset_manager.get_database().find(relative_path);
        if(!record) {
            return Comet::Result<Comet::AssetHandle, Comet::Error>::failure(
                {"Required material asset is not indexed: " + relative_path.generic_string()});
        }
        const auto handle = record->handle;
        if(auto loaded = asset_manager.load_material(handle); !loaded)
            return Comet::Result<Comet::AssetHandle, Comet::Error>::failure(loaded.error());
        return Comet::Result<Comet::AssetHandle, Comet::Error>::success(handle);
    }

    class GameApp final: public Comet::Application {
    public:
        GameApp() : Application(Comet::ProjectPaths(COMET_SAMPLE_PROJECT_DIRECTORY).cache()) {}

        Comet::Result<void, Comet::Error> on_init() override {
            LOG_INFO("app init");

            auto& engine = get_engine();
            auto& render_resources = engine.get_render_resources();
            auto& asset_registry = engine.get_asset_registry();

            m_asset_manager = std::make_unique<Comet::AssetManager>(
                Comet::ProjectPaths(COMET_SAMPLE_PROJECT_DIRECTORY), asset_registry,
                render_resources, engine.get_task_scheduler());
            const Comet::AssetScanReport scan_report = m_asset_manager->scan();
            for(const Comet::AssetScanIssue& issue : scan_report.issues) {
                LOG_WARN(
                    "Asset scan issue at '{}': {}", issue.path.generic_string(), issue.message);
            }

            const auto mesh = load_required_mesh(*m_asset_manager, DEMO_MESH);
            if(!mesh)
                return Comet::Result<void, Comet::Error>::failure(mesh.error());
            const auto material = load_required_material(*m_asset_manager, DEMO_MATERIAL);
            if(!material)
                return Comet::Result<void, Comet::Error>::failure(material.error());
            const auto mesh_handle = mesh.value();
            const auto material_handle = material.value();

            auto scene = std::make_unique<Comet::Scene>();
            Comet::Entity main_camera = scene->create_entity("Main Camera");
            main_camera.get_component<Comet::TransformComponent>().translation.z = 3.0f;
            main_camera.add_component<Comet::CameraComponent>().primary = true;

            Comet::Entity first_cube = scene->create_entity("Demo Cube A");
            auto& first_transform = first_cube.get_component<Comet::TransformComponent>();
            first_transform.translation.x = -0.5f;
            first_transform.rotation.x = -17.0f;
            first_transform.scale = Comet::Math::Vec3(0.6f);
            first_cube.add_component<Comet::MeshRendererComponent>(mesh_handle, material_handle);

            Comet::Entity second_cube = scene->create_entity("Demo Cube B");
            auto& second_transform = second_cube.get_component<Comet::TransformComponent>();
            second_transform.translation.x = 0.5f;
            second_transform.rotation.x = -17.0f;
            second_transform.scale = Comet::Math::Vec3(0.6f);
            second_cube.add_component<Comet::MeshRendererComponent>(mesh_handle, material_handle);

            m_cube_entity_ids = {first_cube.get_id(), second_cube.get_id()};
            engine.set_scene(std::move(scene));
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_update(Comet::UpdateContext context) override {
            if(auto assets = m_asset_manager->process_completions(); !assets)
                return Comet::Result<void, Comet::Error>::failure(assets.error());
            Comet::Scene* scene = get_engine().get_scene();
            if(!scene) {
                return Comet::Result<void, Comet::Error>::success();
            }

            for(std::size_t index = 0; index < m_cube_entity_ids.size(); ++index) {
                if(Comet::Entity cube = scene->find_entity(m_cube_entity_ids[index])) {
                    const float direction = index == 0 ? 1.0f : -1.0f;
                    auto& transform = cube.get_component<Comet::TransformComponent>();
                    transform.rotate(
                        Comet::Math::Vec3(0.0f, context.delta_time * 100.0f * direction, 0.0f));
                }
            }
            return Comet::Result<void, Comet::Error>::success();
        }

        Comet::Result<void, Comet::Error> on_shutdown() override {
            LOG_INFO("app shutdown");
            m_asset_manager.reset();
            return Comet::Result<void, Comet::Error>::success();
        }

    private:
        std::unique_ptr<Comet::AssetManager> m_asset_manager;
        std::array<Comet::EntityId, 2> m_cube_entity_ids = {
            Comet::INVALID_ENTITY_ID, Comet::INVALID_ENTITY_ID};
    };

    Comet::Result<std::unique_ptr<Comet::Application>> create_game_app(
        Comet::ApplicationArguments arguments) {
        if(!arguments.empty())
            return Comet::Result<std::unique_ptr<Comet::Application>>::failure(
                "This application does not accept command-line arguments");
        return Comet::Result<std::unique_ptr<Comet::Application>>::success(
            std::make_unique<GameApp>());
    }
}

RUN_APP(create_game_app)
