#include "runtime/entry.h"
#include "asset/manager.h"
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

    class GameApp final: public Comet::Application {
    public:
        void on_init() override {
            LOG_INFO("app init");

            auto& engine = get_engine();
            auto& resource_manager = engine.get_resource_manager();
            auto& asset_registry = engine.get_asset_registry();

            m_asset_manager = std::make_unique<Comet::AssetManager>(
                Comet::ProjectPaths(PROJECT_ROOT_DIR), asset_registry, resource_manager,
                engine.get_task_scheduler());
            const Comet::AssetScanReport scan_report = m_asset_manager->scan();
            for(const Comet::AssetScanIssue& issue : scan_report.issues) {
                LOG_WARN("Asset scan issue at '{}': {}", issue.path.generic_string(),
                    issue.message);
            }

            const Comet::AssetHandle mesh_handle =
                load_required_mesh(*m_asset_manager, DEMO_MESH);
            const Comet::AssetHandle material_handle =
                load_required_material(*m_asset_manager, DEMO_MATERIAL);

            auto scene = std::make_unique<Comet::Scene>();
            Comet::Entity main_camera = scene->create_entity("Main Camera");
            main_camera.get_component<Comet::TransformComponent>().translation.z = 3.0f;
            main_camera.add_component<Comet::CameraComponent>().primary = true;

            Comet::Entity first_cube = scene->create_entity("Demo Cube A");
            auto& first_transform = first_cube.get_component<Comet::TransformComponent>();
            first_transform.translation.x = -0.5f;
            first_transform.rotation.x = -17.0f;
            first_transform.scale = Comet::Math::Vec3(0.6f);
            first_cube.add_component<Comet::MeshRendererComponent>(
                mesh_handle, material_handle);

            Comet::Entity second_cube = scene->create_entity("Demo Cube B");
            auto& second_transform =
                second_cube.get_component<Comet::TransformComponent>();
            second_transform.translation.x = 0.5f;
            second_transform.rotation.x = -17.0f;
            second_transform.scale = Comet::Math::Vec3(0.6f);
            second_cube.add_component<Comet::MeshRendererComponent>(
                mesh_handle, material_handle);

            m_cube_entity_ids = {first_cube.get_id(), second_cube.get_id()};
            engine.set_scene(std::move(scene));
        }

        void on_update(Comet::UpdateContext context) override {
            m_asset_manager->process_completions();
            Comet::Scene* scene = get_engine().get_scene();
            if(!scene) {
                return;
            }

            for(std::size_t index = 0; index < m_cube_entity_ids.size(); ++index) {
                if(Comet::Entity cube = scene->find_entity(m_cube_entity_ids[index])) {
                    const float direction = index == 0 ? 1.0f : -1.0f;
                    auto& transform = cube.get_component<Comet::TransformComponent>();
                    transform.rotate(Comet::Math::Vec3(
                        0.0f, context.deltaTime * 100.0f * direction, 0.0f));
                }
            }
        }

        void on_shutdown() override {
            LOG_INFO("app shutdown");
            m_asset_manager.reset();
        }

    private:
        std::unique_ptr<Comet::AssetManager> m_asset_manager;
        std::array<Comet::EntityId, 2> m_cube_entity_ids = {
            Comet::INVALID_ENTITY_ID, Comet::INVALID_ENTITY_ID};
    };
}

RUN_APP(GameApp)
