#include "support/engine_fixture.h"
#include "core/engine.h"
#include "core/task_scheduler.h"
#include "config/config.h"
#include "asset/data/mesh_data.h"
#include "asset/data/texture_data.h"
#include "render/renderer.h"
#include "render/frame_scheduler.h"
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

#include <tuple>
#include <utility>

#include "support/scene_motion_system.h"

namespace Comet::Tests {
    class FrameEditOrderTest: public ::testing::TestWithParam<std::tuple<bool, bool>> {};

    TEST_P(FrameEditOrderTest, PickConsumesCapturedSceneAndFeedbackReturnsAtNextUpdate) {
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
            camera.add_component<CameraComponent>(CameraComponent{.primary = true});
            auto object = scene->create_entity("Object");
            EXPECT_TRUE(object.try_edit_transform([&](auto& value) { value.translation.x = x; }));
            object.add_component<MeshRendererComponent>(AssetHandle(1), AssetHandle(2));
            return scene;
        };
        engine.set_scene(make_scene(20));
        auto calls = std::make_shared<RuntimeCalls>();
        auto& runtime = engine.get_scene_runtime();
        const bool paused = std::get<1>(GetParam());
        const auto state = paused ? SceneRuntime::State::Paused : SceneRuntime::State::Running;
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime(state));
        int updates = 0;
        int preparations = 0;
        bool picked = false;
        bool first_feedback = false;
        std::optional<uint64_t> allocations_before_lines;
        const auto allocation_count = [&] {
            uint64_t count = 0;
            for(const auto& heap :
                renderer.get_render_context().get_device().query_memory_budget().heaps) {
                count += heap.allocation_count;
            }
            return count;
        };
        const auto frame_ready = [&](const Engine::FrameContext&) {
            ++preparations;
            if(preparations == 2) {
                EXPECT_TRUE(first_feedback);
                EXPECT_FALSE(picked);
                const auto entity = engine.get_scene()->find_entity(EntityId(2));
                LineDrawList lines;
                EXPECT_TRUE(lines.add_box(mesh.value()->get_local_bounds(),
                    engine.get_scene()->get_world_matrix(entity)));
                EXPECT_EQ(lines.line_count(), 12u);
                for(const auto& vertex : lines.vertices()) {
                    EXPECT_GE(vertex.position.x, -1.0f);
                    EXPECT_LE(vertex.position.x, 1.0f);
                }
                allocations_before_lines = allocation_count();
                renderer.submit_lines(lines);
                const auto size = renderer.get_scene_renderer().get_render_target().get_size();
                renderer.request_viewport_pick(size / 2u, size, engine.get_scene()->get_lifetime());
                return paused ? engine.request_runtime_step() : Result<void, Error>::success();
            }
            EXPECT_EQ(updates, 1);
            EXPECT_EQ(calls->updates, 0);
            EXPECT_TRUE(renderer.get_frame_scheduler().is_frame_active());
            if(std::get<0>(GetParam())) {
                auto previous = engine.replace_scene(make_scene(10));
                EXPECT_FALSE(runtime.is_active());
                EXPECT_EQ(calls->stops, 1);
                EXPECT_EQ(previous->find_entity(EntityId(2))
                              .get_component<TransformComponent>()
                              .translation.x,
                    20);
                if(auto started = engine.start_scene_runtime(state); !started)
                    return started;
            } else {
                EXPECT_TRUE(engine.get_scene()
                        ->find_entity(EntityId(2))
                        .try_edit_transform([](auto& value) { value.translation.x = 10; }));
            }
            const auto size = renderer.get_scene_renderer().get_render_target().get_size();
            renderer.request_viewport_pick(size / 2u, size, engine.get_scene()->get_lifetime());
            return paused ? engine.request_runtime_step() : Result<void, Error>::success();
        };
        Comet::Tests::set_test_overlay(renderer, {.render = [&](Comet::OverlayRecordContext&) {
            EXPECT_FALSE(renderer.take_viewport_pick_result());
            if(allocations_before_lines) {
                const auto& materials = renderer.get_scene_renderer().get_material_statistics();
                EXPECT_EQ(allocation_count(),
                    *allocations_before_lines + 1 + materials.material_bindings_created);
            }

            return Comet::Result<void, Comet::GraphicsError>::success();
        }});
        EXPECT_TRUE(engine.run({
            .update =
                [&](const Engine::FrameContext&) {
                    if(const auto result = renderer.take_viewport_pick_result()) {
                        EXPECT_EQ(calls->updates, preparations);
                        EXPECT_EQ(result->scene_lifetime, engine.get_scene()->get_lifetime());
                        if(preparations == 1) {
                            // 单步显示本次结果；持续运行仍绘制更新前的快照。
                            EXPECT_EQ(result->hit.has_value(), paused);
                            if(result->hit)
                                EXPECT_EQ(result->hit->entity_id, EntityId(2));
                            EXPECT_FLOAT_EQ(engine.get_scene()
                                                ->find_entity(EntityId(2))
                                                .get_component<TransformComponent>()
                                                .translation.x,
                                0);
                            first_feedback = true;
                        } else {
                            EXPECT_EQ(preparations, 2);
                            picked = result->hit && result->hit->entity_id == EntityId(2);
                            engine.get_window().request_close();
                        }
                        EXPECT_FALSE(renderer.take_viewport_pick_result());
                    }
                    if(++updates > 5)
                        engine.get_window().request_close();
                    return Result<void, Error>::success();
                },
            .frame_ready = frame_ready,
        }));
        renderer.set_overlay({});
        EXPECT_EQ(preparations, 2);
        EXPECT_EQ(calls->updates, 2);
        EXPECT_EQ(runtime.get_timing().frame_index, 2u);
        EXPECT_TRUE(picked);
        EXPECT_TRUE(allocations_before_lines.has_value());
    }

    INSTANTIATE_TEST_SUITE_P(MutateOrReplaceScene, FrameEditOrderTest,
        ::testing::Combine(::testing::Bool(), ::testing::Bool()));
}
