#include "runtime/entry.h"
#include "asset/registry.h"
#include "common/geometry_utils.h"
#include "render/material.h"
#include "render/renderer.h"
#include "scene/scene.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

namespace {
    constexpr Comet::AssetHandle DEMO_CUBE_MESH_HANDLE(1);
    constexpr Comet::AssetHandle DEMO_CUBE_MATERIAL_HANDLE(2);
}

class GameApp final: public Comet::Application {
public:
    void on_init() override {
        LOG_INFO("app init");

        auto* engine = get_engine();
        auto* resource_manager = engine->get_renderer()->get_resource_manager();
        auto* asset_registry = engine->get_asset_registry();

        auto [cube_vertices, cube_indices] =
            Comet::GeometryUtils::create_cube(-0.3f, 0.3f, -0.3f, 0.3f, -0.3f, 0.3f);
        auto cube_mesh = resource_manager->create_mesh(
            "demo_cube", cube_vertices, cube_indices);

        const std::string texture_path =
            std::string(PROJECT_ROOT_DIR) + "/engine/assets/textures/";
        auto texture0 = resource_manager->load_texture(texture_path + "awesomeface.png");
        auto texture1 = resource_manager->load_texture(texture_path + "R-C.jpeg");

        Comet::MaterialConfig material_config;
        auto material = resource_manager->get_material_manager()->create_material(
            "demo_material", material_config);
        material->set_property_texture("u_Texture0", texture0);
        material->set_property_texture("u_Texture1", texture1);

        const bool mesh_registered = asset_registry->register_asset(
            DEMO_CUBE_MESH_HANDLE, std::move(cube_mesh));
        const bool material_registered = asset_registry->register_asset(
            DEMO_CUBE_MATERIAL_HANDLE, std::move(material));
        if(!mesh_registered || !material_registered) {
            LOG_FATAL("Failed to register demo render assets");
        }

        auto scene = std::make_unique<Comet::Scene>();
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
        engine->set_scene(std::move(scene));
    }

    void on_update(Comet::UpdateContext context) override {
        Comet::Scene* scene = get_engine()->get_scene();
        if(!scene) {
            return;
        }

        for(std::size_t index = 0; index < m_cube_entity_ids.size(); ++index) {
            Comet::Entity cube = scene->find_entity(m_cube_entity_ids[index]);
            if(cube) {
                const float direction = index == 0 ? 1.0f : -1.0f;
                cube.get_component<Comet::TransformComponent>().rotation.y =
                    context.totalTime * 100.0f * direction;
            }
        }
    }

    void on_shutdown() override {
        LOG_INFO("app shutdown");
    }

private:
    std::array<Comet::EntityId, 2> m_cube_entity_ids = {
        Comet::INVALID_ENTITY_ID,
        Comet::INVALID_ENTITY_ID
    };
};

RUN_APP(GameApp)
