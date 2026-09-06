#include "core/engine.h"
#include "asset/registry.h"
#include "render/material.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    class FrameEditOrderTest: public ::testing::TestWithParam<bool> {};

    TEST_P(FrameEditOrderTest, PickConsumesSceneChangedDuringCurrentPreparation) {
        Config config;
        config.window.width = 320;
        config.window.height = 240;
        config.vulkan.msaa_samples = SampleCount::Count1;
        Engine engine(config);
        auto& renderer = engine.get_renderer();
        auto& resources = engine.get_resource_manager();
        const MeshData data{.vertices = {{.position = {-1, -1, 0}},
                                {.position = {1, -1, 0}}, {.position = {0, 1, 0}}},
            .indices = {0, 1, 2}};
        auto mesh = resources.try_create_mesh(data);
        auto texture = resources.try_create_texture(
            {.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(texture);
        auto material = std::make_shared<Material>("Test", "cube_texture");
        material->set_texture_property("u_Texture0", texture.value());
        material->set_texture_property("u_Texture1", texture.value());
        ASSERT_TRUE(
            engine.get_asset_registry().register_asset(AssetHandle(1), mesh.value()));
        ASSERT_TRUE(engine.get_asset_registry().register_asset(AssetHandle(2), material));
        auto make_scene = [](float x) {
            auto scene = std::make_unique<Scene>();
            auto camera = scene->create_entity("Camera");
            camera.get_component<TransformComponent>().translation.z = 3;
            camera.add_component<CameraComponent>().primary = true;
            auto object = scene->create_entity("Object");
            object.get_component<TransformComponent>().translation.x = x;
            object.add_component<MeshRendererComponent>(AssetHandle(1), AssetHandle(2));
            return scene;
        };
        engine.set_scene(make_scene(20));
        int preparations = 0;
        bool picked = false;
        renderer.set_overlay_callbacks(
            [&] {
                ++preparations;
                if(GetParam()) {
                    engine.set_scene(make_scene(0));
                } else {
                    engine.get_scene()
                        ->find_entity(EntityId(2))
                        .get_component<TransformComponent>()
                        .translation.x = 0;
                }
                const auto size =
                    renderer.get_scene_renderer().get_render_target().get_size();
                renderer.request_viewport_pick(size / 2u, size);
            },
            [&](CommandBuffer&) {
                glfwSetWindowShouldClose(engine.get_window().get(), GLFW_TRUE);
            });
        renderer.set_viewport_pick_callback([&](std::optional<ScenePickHit> hit) {
            picked = hit && hit->entity_id == EntityId(2);
        });
        int updates = 0;
        engine.register_update_callback([&](UpdateContext) {
            if(++updates > 5)
                glfwSetWindowShouldClose(engine.get_window().get(), GLFW_TRUE);
        });
        engine.on_update();
        renderer.set_overlay_callbacks({}, {});
        renderer.set_viewport_pick_callback({});
        EXPECT_EQ(preparations, 1);
        EXPECT_TRUE(picked);
    }

    INSTANTIATE_TEST_SUITE_P(MutateOrReplaceScene, FrameEditOrderTest, ::testing::Bool());
}
