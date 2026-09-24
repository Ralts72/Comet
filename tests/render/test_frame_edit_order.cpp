#include "core/engine.h"
#include "core/task_scheduler.h"
#include "config/config.h"
#include "asset/data/mesh_data.h"
#include "render/renderer.h"
#include "render/scene/scene_renderer.h"
#include "render/render_target.h"
#include "render/render_context.h"
#include "render/resource/render_resources.h"
#include "graphics/device.h"
#include "core/window.h"
#include "asset/registry.h"
#include "render/material/material.h"
#include "render/resource/mesh.h"
#include "render/resource/texture.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

#include <utility>

#include "support/scene_motion_system.h"

namespace Comet::Tests {
    class FrameEditOrderTest: public ::testing::TestWithParam<bool> {};

    TEST_P(FrameEditOrderTest, PickAndLinesConsumeSceneChangedDuringCurrentPreparation) {
        Config config;
        config.window.width = 320;
        config.window.height = 240;
        config.vulkan.msaa_samples = SampleCount::Count1;
        auto engine_result = Engine::create(config);
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        auto& renderer = engine.get_renderer();
        auto& resources = engine.get_render_resources();
        const MeshData data{.vertices = {{.position = {-1, -1, 0}}, {.position = {1, -1, 0}},
                                {.position = {0, 1, 0}}},
            .indices = {0, 1, 2}};
        auto mesh = resources.try_create_mesh(data);
        auto texture =
            resources.try_create_texture({.width = 1, .height = 1, .pixels = {255, 255, 255, 255}});
        ASSERT_TRUE(mesh);
        ASSERT_TRUE(texture);
        auto material = std::make_shared<Material>("Test", "pbr");
        material->set_texture_property("base_color_texture", texture.value());
        ASSERT_TRUE(engine.get_asset_registry().register_asset(AssetHandle(1), mesh.value()));
        ASSERT_TRUE(engine.get_asset_registry().register_asset(AssetHandle(2), material));
        auto make_scene = [](float x) {
            auto scene = std::make_unique<Scene>();
            auto camera = scene->create_entity("Camera");
            EXPECT_TRUE(camera.try_edit_transform([&](auto& value) { value.translation.z = 3; }));
            camera.add_component<CameraComponent>().primary = true;
            auto object = scene->create_entity("Object");
            EXPECT_TRUE(object.try_edit_transform([&](auto& value) { value.translation.x = x; }));
            object.add_component<MeshRendererComponent>(AssetHandle(1), AssetHandle(2));
            return scene;
        };
        engine.set_scene(make_scene(20));
        auto calls = std::make_shared<RuntimeCalls>();
        auto& runtime = engine.get_scene_runtime();
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
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
        const auto frame_ready = [&](Engine::FrameContext&) {
            ++preparations;
            if(GetParam()) {
                auto previous = engine.replace_scene(make_scene(10));
                EXPECT_FALSE(runtime.is_active());
                EXPECT_EQ(calls->stops, 1);
                EXPECT_EQ(previous->find_entity(EntityId(2))
                              .get_component<TransformComponent>()
                              .translation.x,
                    20);
                if(auto started = engine.start_scene_runtime(); !started)
                    return started;
            } else {
                EXPECT_TRUE(engine.get_scene()
                        ->find_entity(EntityId(2))
                        .try_edit_transform([](auto& value) { value.translation.x = 10; }));
            }
            const auto size = renderer.get_scene_renderer().get_render_target().get_size();
            renderer.request_viewport_pick(size / 2u, size);
            return Result<void, Error>::success();
        };
        renderer.set_overlay_renderer([&](CommandBuffer&) {
            // 结果回调提交的线段必须已在当前 scene pass 分配并录制。
            if(allocations_before_lines) {
                const auto& materials = renderer.get_scene_renderer().get_material_statistics();
                EXPECT_EQ(allocation_count(),
                    *allocations_before_lines + 1 + materials.material_bindings_created);
            }
            engine.get_window().request_close();
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
        EXPECT_TRUE(engine.run(
            [&](Engine::FrameContext&) {
                if(++updates > 5)
                    engine.get_window().request_close();
                return Result<void, Error>::success();
            },
            frame_ready));
        renderer.set_overlay_renderer({});
        renderer.set_viewport_pick_callback({});
        EXPECT_EQ(preparations, 1);
        EXPECT_EQ(calls->updates, 1);
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
        EXPECT_TRUE(picked);
        EXPECT_TRUE(allocations_before_lines.has_value());
    }

    INSTANTIATE_TEST_SUITE_P(MutateOrReplaceScene, FrameEditOrderTest, ::testing::Bool());
}
