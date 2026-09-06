#include "core/engine.h"
#include "asset/registry.h"
#include "render/material.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "scene/scene.h"
#include "runtime/scene_runtime.h"

#include <gtest/gtest.h>

namespace Comet::Tests {
    class FrameEditOrderTest: public ::testing::TestWithParam<bool> {};

    TEST_P(FrameEditOrderTest, PickAndLinesConsumeSceneChangedDuringCurrentPreparation) {
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
        struct MoveSystem final: System {
            void update(Scene& scene, const Context&) override {
                scene.find_entity(EntityId(2))
                    .get_component<TransformComponent>()
                    .translation.x = 0;
            }
        };
        engine.set_scene(make_scene(20));
        auto& runtime = engine.get_scene_runtime();
        runtime.add_system(std::make_unique<MoveSystem>());
        runtime.start(*engine.get_scene());
        int preparations = 0;
        bool picked = false;
        std::optional<uint64_t> allocations_before_lines;
        const auto allocation_count = [&] {
            uint64_t count = 0;
            for(const auto& heap :
                renderer.get_render_context().get_device().query_memory_budget().heaps) {
                count += heap.allocation_count;
            }
            return count;
        };
        renderer.set_overlay_callbacks(
            [&] {
                ++preparations;
                if(GetParam()) {
                    engine.set_scene(make_scene(10));
                    EXPECT_FALSE(runtime.is_active());
                    runtime.start(*engine.get_scene());
                } else {
                    engine.get_scene()
                        ->find_entity(EntityId(2))
                        .get_component<TransformComponent>()
                        .translation.x = 10;
                }
                const auto size =
                    renderer.get_scene_renderer().get_render_target().get_size();
                renderer.request_viewport_pick(size / 2u, size);
            },
            [&](CommandBuffer&) {
                // 结果回调提交的线段必须已在当前 scene pass 分配并录制。
                if(allocations_before_lines) {
                    const auto& materials =
                        renderer.get_scene_renderer().get_material_statistics();
                    EXPECT_EQ(
                        allocation_count(), *allocations_before_lines + 1
                                                + materials.material_versions_created);
                }
                glfwSetWindowShouldClose(engine.get_window().get(), GLFW_TRUE);
            });
        renderer.set_viewport_pick_callback([&](std::optional<ScenePickHit> hit) {
            picked = hit && hit->entity_id == EntityId(2);
            if(hit) {
                auto& scene = *engine.get_scene();
                const auto entity = scene.find_entity(hit->entity_id);
                LineDrawList lines;
                EXPECT_TRUE(lines.add_box(
                    mesh.value()->get_local_bounds(), scene.get_world_matrix(entity)));
                EXPECT_EQ(lines.line_count(), 12U);
                // 场景修改/替换前对象在 x=20，本帧框应已回到原点附近。
                for(const auto& vertex : lines.vertices()) {
                    EXPECT_GE(vertex.position.x, -1.0f);
                    EXPECT_LE(vertex.position.x, 1.0f);
                }
                allocations_before_lines = allocation_count();
                renderer.submit_lines(lines);
            }
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
        EXPECT_TRUE(allocations_before_lines.has_value());
        EXPECT_EQ(runtime.get_timing().frame_index, 1U);
    }

    INSTANTIATE_TEST_SUITE_P(MutateOrReplaceScene, FrameEditOrderTest, ::testing::Bool());
}
