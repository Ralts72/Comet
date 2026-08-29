#include "runtime/entry.h"
#include "asset/manager.h"
#include "asset/registry.h"
#include "common/geometry_utils.h"
#include "core/project_paths.h"
#include "render/material.h"
#include "scene/scene.h"

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace {
    constexpr Comet::AssetHandle DEMO_CUBE_MESH_HANDLE(1);
    constexpr Comet::AssetHandle DEMO_CUBE_MATERIAL_HANDLE(2);
    const std::filesystem::path AWESOME_FACE_TEXTURE =
            "textures/awesomeface.png";
    const std::filesystem::path SECOND_DEMO_TEXTURE =
            "textures/R-C.jpeg";

    std::shared_ptr<Comet::Texture> load_required_texture(
        Comet::AssetManager& asset_manager,
        const std::filesystem::path& relative_path) {
        const Comet::AssetRecord* record =
                asset_manager.get_database().find(relative_path);
        if(!record) {
            LOG_FATAL(
                "Required texture asset '{}' is not indexed",
                relative_path.generic_string());
        }

        auto texture = asset_manager.load_texture(record->handle);
        if(!texture) {
            LOG_FATAL(
                "Failed to load required texture asset '{}'",
                relative_path.generic_string());
        }
        return texture;
    }

    class GameApp final: public Comet::Application {
    public:
        void on_init() override {
            LOG_INFO("app init");

            auto& engine = get_engine();
            auto& resource_manager = engine.get_resource_manager();
            auto& asset_registry = engine.get_asset_registry();

            m_asset_manager = std::make_unique<Comet::AssetManager>(
                Comet::ProjectPaths(PROJECT_ROOT_DIR),
                asset_registry,
                resource_manager);
            const Comet::AssetScanReport scan_report = m_asset_manager->scan();
            for(const Comet::AssetScanIssue& issue: scan_report.issues) {
                LOG_WARN(
                    "Asset scan issue at '{}': {}",
                    issue.path.generic_string(),
                    issue.message);
            }

            auto [cube_vertices, cube_indices] =
                    Comet::GeometryUtils::create_cube(-0.3f, 0.3f, -0.3f, 0.3f, -0.3f, 0.3f);
            auto cube_mesh = resource_manager.create_mesh(
                "demo_cube", cube_vertices, cube_indices);

            const auto texture0 = load_required_texture(
                *m_asset_manager, AWESOME_FACE_TEXTURE);
            const auto texture1 = load_required_texture(
                *m_asset_manager, SECOND_DEMO_TEXTURE);

            const Comet::MaterialConfig material_config;
            auto material = resource_manager.get_material_manager().create_material(
                "demo_material", material_config);
            material->set_property_texture("u_Texture0", texture0);
            material->set_property_texture("u_Texture1", texture1);

            const bool mesh_registered = asset_registry.register_asset(
                DEMO_CUBE_MESH_HANDLE, std::move(cube_mesh));
            const bool material_registered = asset_registry.register_asset(
                DEMO_CUBE_MATERIAL_HANDLE, std::move(material));
            if(!mesh_registered || !material_registered) {
                LOG_FATAL("Failed to register demo render assets");
            }

            auto scene = std::make_unique<Comet::Scene>();
            Comet::Entity main_camera = scene->create_entity("Main Camera");
            main_camera.get_component<Comet::TransformComponent>().translation.z = 3.0f;
            main_camera.add_component<Comet::CameraComponent>().primary = true;

            Comet::Entity first_cube = scene->create_entity("Demo Cube A");
            auto& first_transform = first_cube.get_component<Comet::TransformComponent>();
            first_transform.translation.x = -0.5f;
            first_transform.rotation.x = -17.0f;
            first_cube.add_component<Comet::MeshRendererComponent>(
                DEMO_CUBE_MESH_HANDLE, DEMO_CUBE_MATERIAL_HANDLE);

            Comet::Entity second_cube = scene->create_entity("Demo Cube B");
            auto& second_transform = second_cube.get_component<Comet::TransformComponent>();
            second_transform.translation.x = 0.5f;
            second_transform.rotation.x = -17.0f;
            second_cube.add_component<Comet::MeshRendererComponent>(
                DEMO_CUBE_MESH_HANDLE, DEMO_CUBE_MATERIAL_HANDLE);

            m_cube_entity_ids = {first_cube.get_id(), second_cube.get_id()};
            engine.set_scene(std::move(scene));
        }

        void on_update(Comet::UpdateContext context) override {
            Comet::Scene* scene = get_engine().get_scene();
            if(!scene) {
                return;
            }

            for(std::size_t index = 0; index < m_cube_entity_ids.size(); ++index) {
                if(Comet::Entity cube = scene->find_entity(m_cube_entity_ids[index])) {
                    const float direction = index == 0 ? 1.0f : -1.0f;
                    auto& transform = cube.get_component<Comet::TransformComponent>();
                    transform.rotate(Comet::Math::Vec3(
                        0.0f,
                        context.deltaTime * 100.0f * direction,
                        0.0f));
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
            Comet::INVALID_ENTITY_ID,
            Comet::INVALID_ENTITY_ID
        };
    };
}

RUN_APP(GameApp)
