#include "runtime/entry.h"
#include "asset/asset_manager.h"
#include "core/project_paths.h"
#include "scene/scene.h"
#include "runtime/scene_runtime.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace {
    const std::filesystem::path DEMO_MESH = "meshes/cube.gltf";
    const std::filesystem::path DEMO_MATERIAL = "materials/pbr.mat";

    Comet::AssetHandle load_required_mesh(
        Comet::AssetManager& asset_manager, const std::filesystem::path& relative_path) {
        const Comet::AssetRecord* record =
            asset_manager.get_database().find(relative_path);
        if(!record) {
            LOG_FATAL("Required mesh asset '{}' is not indexed",
                relative_path.generic_string());
        }

        if(!asset_manager.import_mesh(record->handle)
            || !asset_manager.load_mesh(record->handle)) {
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

    class DemoMotionSystem final: public Comet::System {
    public:
        DemoMotionSystem(Comet::EntityId camera, std::array<Comet::EntityId, 2> cubes)
            : m_camera(camera), m_cubes(cubes) {}

        void fixed_update(Comet::Scene& scene, const Context& context) override {
            for(std::size_t index = 0; index < m_cubes.size(); ++index) {
                if(auto cube = scene.find_entity(m_cubes[index])) {
                    const float direction = index == 0 ? 1.0f : -1.0f;
                    cube.get_component<Comet::TransformComponent>().rotate(
                        {0, float(context.delta_time) * 100.0f * direction, 0});
                }
            }
        }

        void update(Comet::Scene& scene, const Context& context) override {
            const auto& input = context.input;
            if(!input.focused)
                return;
            using Key = Comet::Input::Key;
            Comet::Math::Vec3 direction{
                float(input.key(Key::D).down) - float(input.key(Key::A).down),
                float(input.key(Key::E).down) - float(input.key(Key::Q).down),
                float(input.key(Key::S).down) - float(input.key(Key::W).down)};
            const auto stick = [](float value) {
                const float magnitude = std::abs(value);
                if(magnitude <= 0.15f)
                    return 0.0f;
                return std::copysign((magnitude - 0.15f) / 0.85f, value);
            };
            for(const auto& pad : input.gamepads) {
                if(!pad.connected)
                    continue;
                using Axis = Comet::Input::GamepadAxis;
                direction.x += stick(pad.axis(Axis::LeftX));
                direction.z += stick(pad.axis(Axis::LeftY));
                direction.y += pad.axis(Axis::RightTrigger) - pad.axis(Axis::LeftTrigger);
                break;
            }
            if(Comet::Math::length(direction) > 1)
                direction = Comet::Math::normalize(direction);
            if(auto camera = scene.find_entity(m_camera)) {
                auto& transform = camera.get_component<Comet::TransformComponent>();
                const float speed = input.key(Key::LeftShift).down ? 6.0f : 3.0f;
                transform.translation += direction * speed * float(context.delta_time);
                transform.translation.z -= input.scroll.y * 0.2f;
            }
        }

    private:
        Comet::EntityId m_camera;
        std::array<Comet::EntityId, 2> m_cubes;
    };

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
            const auto camera_id = main_camera.get_id();
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

            const std::array cube_ids{first_cube.get_id(), second_cube.get_id()};
            auto light = scene->create_entity("Key Light");
            light.get_component<Comet::TransformComponent>().rotation = {-30, -35, 0};
            auto& light_component = light.add_component<Comet::LightComponent>();
            light_component.intensity = 4.0f;
            light_component.casts_shadow = true;
            auto ground = scene->create_entity("Ground");
            auto& ground_transform = ground.get_component<Comet::TransformComponent>();
            ground_transform.translation.y = -0.8f;
            ground_transform.scale = {4.0f, 0.1f, 4.0f};
            ground.add_component<Comet::MeshRendererComponent>(
                mesh_handle, material_handle);
            engine.set_scene(std::move(scene));
            auto& runtime = engine.get_scene_runtime();
            runtime.add_system(std::make_unique<DemoMotionSystem>(camera_id, cube_ids));
            runtime.start(*engine.get_scene());
        }

        void on_update(Comet::UpdateContext) override {
            m_asset_manager->process_completions();
            const auto& input = get_engine().get_input_frame();
            if(input.focused && input.key(Comet::Input::Key::Escape).pressed)
                get_engine().get_window().request_close();
        }

        void on_shutdown() override {
            LOG_INFO("app shutdown");
            get_engine().get_scene_runtime().stop();
            m_asset_manager.reset();
        }

    private:
        std::unique_ptr<Comet::AssetManager> m_asset_manager;
    };
}

RUN_APP(GameApp)
