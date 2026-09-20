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
#include <GLFW/glfw3.h>

#include <type_traits>
#include <utility>

namespace Comet::Tests {
    static_assert(
        std::is_same_v<decltype(std::declval<Engine&>().get_scene_runtime()), const SceneRuntime&>);

    namespace {
        struct RuntimeCalls {
            int starts = 0;
            int updates = 0;
            int stops = 0;
            bool input_focused = false;
            bool fail_update = false;
            Scene* started_scene = nullptr;
            Scene* stopped_scene = nullptr;
        };
        class SceneMotionSystem final: public System {
        public:
            explicit SceneMotionSystem(std::shared_ptr<RuntimeCalls> calls)
                : m_calls(std::move(calls)) {}
            Result<void, Error> on_start(Scene& scene) override {
                ++m_calls->starts;
                m_calls->started_scene = &scene;
                return Result<void, Error>::success();
            }
            Result<void, Error> update(Scene& scene, const Context& context) override {
                ++m_calls->updates;
                m_calls->input_focused = context.input.focused;
                if(auto object = scene.find_entity(EntityId(2)))
                    object.get_component<TransformComponent>().translation.x = 0;
                if(m_calls->fail_update)
                    return Result<void, Error>::failure({"runtime update failed"});
                return Result<void, Error>::success();
            }
            void on_stop(Scene& scene) noexcept override {
                ++m_calls->stops;
                m_calls->stopped_scene = &scene;
            }

        private:
            std::shared_ptr<RuntimeCalls> m_calls;
        };
    }

    class FrameEditOrderTest: public ::testing::TestWithParam<bool> {};

    TEST(EngineRunTest, RuntimeLifecycleFollowsOwnedSceneAndShutdownIsFinal) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        EXPECT_FALSE(engine.start_scene_runtime());
        auto calls = std::make_shared<RuntimeCalls>();
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.set_runtime_settings({.fixed_delta = 0.02}));
        engine.set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine.start_scene_runtime());
        EXPECT_EQ(calls->started_scene, engine.get_scene());
        EXPECT_FALSE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        EXPECT_FALSE(engine.set_runtime_settings({}));

        auto previous = engine.replace_scene(std::make_unique<Scene>());
        EXPECT_FALSE(engine.get_scene_runtime().is_active());
        EXPECT_EQ(calls->stops, 1);
        EXPECT_EQ(calls->stopped_scene, previous.get());
        ASSERT_TRUE(engine.start_scene_runtime());
        EXPECT_EQ(calls->started_scene, engine.get_scene());
        EXPECT_EQ(calls->starts, 2);
        ASSERT_TRUE(engine.stop_scene_runtime());
        ASSERT_TRUE(engine.stop_scene_runtime());
        EXPECT_EQ(calls->stops, 2);
        ASSERT_TRUE(engine.start_scene_runtime());
        engine.prepare_shutdown();
        EXPECT_EQ(calls->stops, 3);
        EXPECT_EQ(calls->stopped_scene, engine.get_scene());
        EXPECT_FALSE(engine.start_scene_runtime());
        EXPECT_FALSE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        EXPECT_FALSE(engine.set_runtime_settings({}));
    }

    TEST(EngineRunTest, UpdateCanCloseBeforeRenderingAndRejectsReentry) {
        Config config;
        auto engine_result = Engine::create(config);
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        int updates = 0;
        int preparations = 0;
        EXPECT_TRUE(engine.run(
            [&](UpdateContext) {
                ++updates;
                EXPECT_EQ(engine.get_input_frame().serial, 1u);
                const auto nested = engine.run();
                EXPECT_FALSE(nested);
                if(!nested) {
                    EXPECT_EQ(nested.error().message, "Engine update loop is already running");
                    EXPECT_FALSE(nested.error().code);
                }
                EXPECT_FALSE(engine.run());
                engine.get_window().request_close();
                return Result<void, Error>::success();
            },
            [&] {
                ++preparations;
                return Result<void, Error>::success();
            }));
        EXPECT_EQ(updates, 1);
        EXPECT_EQ(preparations, 0);
    }

    TEST(EngineRunTest, DeferredFrameSkipsEditingAndDrawingButContinuesUpdates) {
        auto engine_result = Engine::create(Config{});
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        auto calls = std::make_shared<RuntimeCalls>();
        engine.set_scene(std::make_unique<Scene>());
        auto& runtime = engine.get_scene_runtime();
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        auto& renderer = engine.get_renderer();
        auto* window = engine.get_window().get();
        const auto key = glfwSetKeyCallback(window, nullptr);
        glfwSetKeyCallback(window, key);
        const auto focus = glfwSetWindowFocusCallback(window, nullptr);
        glfwSetWindowFocusCallback(window, focus);
        ASSERT_TRUE(key && focus);
        int updates = 0;
        int edits = 0;
        int draws = 0;
        int rebuilds = 0;
        renderer.set_swapchain_resource_callbacks([] {},
            [&](const SwapchainCompatibility&) {
                ++rebuilds;
                return Result<void, GraphicsError>::failure(
                    {"temporary UI allocation failure", vk::Result::eErrorOutOfDeviceMemory});
            });
        renderer.set_overlay_renderer([&](CommandBuffer&) { ++draws; });
        renderer.request_swapchain_recreation();
        const auto result = engine.run(
            [&](UpdateContext) {
                EXPECT_EQ(engine.get_input_frame().serial, static_cast<uint64_t>(updates + 1));
                if(++updates == 1) {
                    focus(window, GLFW_TRUE);
                    key(window, GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
                    key(window, GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
                    EXPECT_FALSE(engine.get_input_frame().key(Input::Key::Space).pressed);
                } else {
                    EXPECT_TRUE(engine.get_input_frame().key(Input::Key::Space).pressed);
                    EXPECT_TRUE(engine.get_input_frame().key(Input::Key::Space).released);
                    engine.get_window().request_close();
                }
                return Result<void, Error>::success();
            },
            [&] {
                ++edits;
                return Result<void, Error>::success();
            });
        renderer.set_overlay_renderer({});
        renderer.set_swapchain_resource_callbacks({}, {});
        EXPECT_TRUE(result);
        EXPECT_EQ(updates, 2);
        EXPECT_EQ(rebuilds, 1);
        EXPECT_EQ(edits, 0);
        EXPECT_EQ(draws, 0);
        EXPECT_EQ(calls->updates, 1);
        EXPECT_FALSE(calls->input_focused);
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
    }

    TEST(EngineRunTest, RuntimeFailureStopsSystemsBeforeReturningWithoutDrawing) {
        auto engine_result = Engine::create(Config{});
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        auto calls = std::make_shared<RuntimeCalls>();
        calls->fail_update = true;
        engine.set_scene(std::make_unique<Scene>());
        auto& runtime = engine.get_scene_runtime();
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        int draws = 0;
        engine.get_renderer().set_overlay_renderer([&](CommandBuffer&) { ++draws; });
        const auto result = engine.run();
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, "runtime update failed");
        EXPECT_EQ(calls->updates, 1);
        EXPECT_EQ(calls->stops, 1);
        EXPECT_EQ(draws, 0);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_FALSE(engine.run());
        engine.get_renderer().set_overlay_renderer({});
    }

    TEST(EngineRunTest, FrameReadyFailureStopsEngineWithoutDrawingOrReusingAcquiredFrame) {
        auto engine_result = Engine::create(Config{});
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        int edits = 0;
        int draws = 0;
        engine.get_renderer().set_overlay_renderer([&](CommandBuffer&) { ++draws; });
        const Error failure =
            GraphicsError{"asset creation failed", vk::Result::eErrorDeviceLost}.as_error();
        const auto result = engine.run({}, [&] {
            ++edits;
            return Result<void, Error>::failure(failure);
        });
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, failure.message);
        EXPECT_EQ(result.error().code, failure.code);
        EXPECT_EQ(edits, 1);
        EXPECT_EQ(draws, 0);
        EXPECT_FALSE(engine.get_task_scheduler().try_submit([] {}));
        EXPECT_FALSE(engine.get_renderer().prepare_frame());
        const auto restarted = engine.run();
        ASSERT_FALSE(restarted);
        EXPECT_EQ(restarted.error().message, "Engine is shutting down");
        engine.get_renderer().set_overlay_renderer({});
    }

    TEST(EngineRunTest, ShutdownPreparationDrainsWorkAndRejectsNewFrames) {
        auto engine_result = Engine::create(Config{});
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        bool completed = false;
        auto task = engine.get_task_scheduler().try_submit([&] { completed = true; });
        ASSERT_TRUE(task);
        engine.prepare_shutdown();
        engine.prepare_shutdown();
        EXPECT_TRUE(completed);
        EXPECT_FALSE(engine.get_task_scheduler().try_submit([] {}));
        const auto restarted = engine.run();
        ASSERT_FALSE(restarted);
        EXPECT_EQ(restarted.error().message, "Engine is shutting down");
        EXPECT_FALSE(engine.get_renderer().prepare_frame());
        EXPECT_FALSE(engine.get_renderer().render_frame({}));
    }

    TEST(EngineRunTest, CallbackFailureDoesNotLeaveLoopRunning) {
        Config config;
        auto engine_result = Engine::create(config);
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        const auto failure = engine.run([](UpdateContext) {
            return Result<void, Error>::failure(
                GraphicsError{"update failed", vk::Result::eErrorDeviceLost}.as_error());
        });
        ASSERT_FALSE(failure);
        EXPECT_EQ(failure.error().message, "update failed");
        EXPECT_EQ(failure.error().code,
            (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        int updates = 0;
        EXPECT_TRUE(engine.run([&](UpdateContext) {
            ++updates;
            engine.get_window().request_close();
            return Result<void, Error>::success();
        }));
        EXPECT_EQ(updates, 1);
    }

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
            camera.get_component<TransformComponent>().translation.z = 3;
            camera.add_component<CameraComponent>().primary = true;
            auto object = scene->create_entity("Object");
            object.get_component<TransformComponent>().translation.x = x;
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
        const auto frame_ready = [&] {
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
                engine.get_scene()
                    ->find_entity(EntityId(2))
                    .get_component<TransformComponent>()
                    .translation.x = 10;
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
            [&](UpdateContext) {
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
