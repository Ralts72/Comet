#include "core/engine.h"
#include "core/task_scheduler.h"
#include "config/config.h"
#include "core/window.h"
#include "render/renderer.h"
#include "support/scene_motion_system.h"

#include <gtest/gtest.h>
#include <GLFW/glfw3.h>
#include <type_traits>

namespace Comet::Tests {
    static_assert(
        std::is_same_v<decltype(std::declval<Engine&>().get_scene_runtime()), const SceneRuntime&>);

    TEST(EngineRunTest, DefaultSystemsInstallBeforeRuntimeStarts) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        ASSERT_TRUE(engine.add_default_scene_systems());
        engine.set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine.start_scene_runtime());
        EXPECT_FALSE(engine.add_default_scene_systems());
        ASSERT_TRUE(engine.stop_scene_runtime());
    }

    TEST(EngineRunTest, FrameContextRoutesInputWithoutCarryingItIntoTheNextFrame) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        auto calls = std::make_shared<RuntimeCalls>();
        engine.set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        int updates = 0;
        int ready_calls = 0;
        int draws = 0;
        Engine::FrameContext* current_frame = nullptr;
        engine.get_renderer().set_overlay_renderer([&](CommandBuffer&) {
            if(++draws == 3)
                engine.get_window().request_close();
        });
        const auto result = engine.run(
            [&](Engine::FrameContext& frame) {
                current_frame = &frame;
                ++updates;
                if(updates > 1)
                    EXPECT_TRUE(calls->input_focused);
                if(updates == 1) {
                    frame.runtime_input = frame.physical_input;
                    frame.runtime_input->focused = true;
                }
                return Result<void, Error>::success();
            },
            [&](Engine::FrameContext& frame) {
                EXPECT_EQ(&frame, current_frame);
                if(++ready_calls == 2) {
                    frame.runtime_input = frame.physical_input;
                    frame.runtime_input->focused = true;
                }
                return Result<void, Error>::success();
            });
        engine.get_renderer().set_overlay_renderer({});
        ASSERT_TRUE(result) << result.error().message;
        EXPECT_EQ(updates, 3);
        EXPECT_EQ(ready_calls, 3);
        EXPECT_EQ(calls->updates, 3);
        EXPECT_FALSE(calls->input_focused);
    }

    TEST(EngineRunTest, RuntimeLifecycleFollowsOwnedSceneAndShutdownIsFinal) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        EXPECT_FALSE(engine.start_scene_runtime());
        EXPECT_FALSE(engine.set_runtime_state(SceneRuntime::State::Paused));
        EXPECT_FALSE(engine.request_runtime_step());
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

    TEST(EngineRunTest, PausedRuntimeKeepsHostAndRenderingAliveWhileStepRunsOnce) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        auto calls = std::make_shared<RuntimeCalls>();
        engine.set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        ASSERT_TRUE(engine.set_runtime_state(SceneRuntime::State::Paused));
        int hosts = 0;
        int ui_frames = 0;
        int draws = 0;
        engine.get_renderer().set_overlay_renderer([&](CommandBuffer&) {
            ++draws;
            EXPECT_EQ(calls->starts, 1);
            EXPECT_EQ(calls->stops, 0);
            if(draws == 1) {
                EXPECT_EQ(calls->updates, 0);
                EXPECT_EQ(calls->fixed_updates, 0);
            } else if(draws <= 3) {
                EXPECT_EQ(calls->updates, 1);
                EXPECT_EQ(calls->fixed_updates, 1);
                EXPECT_EQ(engine.get_scene_runtime().get_state(), SceneRuntime::State::Paused);
            } else {
                EXPECT_EQ(calls->updates, 2);
                EXPECT_EQ(engine.get_scene_runtime().get_state(), SceneRuntime::State::Running);
                engine.get_window().request_close();
            }
        });
        const auto result = engine.run(
            [&](Engine::FrameContext&) {
                ++hosts;
                if(hosts > 8)
                    engine.get_window().request_close();
                if(hosts == 2)
                    return engine.request_runtime_step();
                if(hosts == 4)
                    return engine.set_runtime_state(SceneRuntime::State::Running);
                return Result<void, Error>::success();
            },
            [&](Engine::FrameContext&) {
                ++ui_frames;
                return Result<void, Error>::success();
            });
        engine.get_renderer().set_overlay_renderer({});
        ASSERT_TRUE(result);
        EXPECT_EQ(hosts, 4);
        EXPECT_EQ(ui_frames, 4);
        EXPECT_EQ(draws, 4);
        engine.prepare_shutdown();
        EXPECT_EQ(calls->stops, 1);
        EXPECT_FALSE(engine.set_runtime_state(SceneRuntime::State::Paused));
        EXPECT_FALSE(engine.request_runtime_step());
    }

    TEST(EngineRunTest, UpdateCanCloseBeforeRenderingAndRejectsReentry) {
        Config config;
        auto engine_result = Engine::create(config);
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        int updates = 0;
        int preparations = 0;
        EXPECT_TRUE(engine.run(
            [&](Engine::FrameContext& frame) {
                ++updates;
                EXPECT_EQ(frame.physical_input.serial, 1u);
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
            [&](Engine::FrameContext&) {
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
            [&](Engine::FrameContext& frame) {
                EXPECT_EQ(frame.physical_input.serial, static_cast<uint64_t>(updates + 1));
                if(++updates == 1) {
                    frame.runtime_input = frame.physical_input;
                    frame.runtime_input->focused = true;
                    focus(window, GLFW_TRUE);
                    key(window, GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
                    key(window, GLFW_KEY_SPACE, 0, GLFW_RELEASE, 0);
                    EXPECT_FALSE(frame.physical_input.key(Input::Key::Space).pressed);
                } else {
                    EXPECT_TRUE(frame.physical_input.key(Input::Key::Space).pressed);
                    EXPECT_TRUE(frame.physical_input.key(Input::Key::Space).released);
                    engine.get_window().request_close();
                }
                return Result<void, Error>::success();
            },
            [&](Engine::FrameContext&) {
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
        EXPECT_TRUE(calls->input_focused);
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

    TEST(EngineRunTest, RuntimeRecoveryCompletesAcquiredFrameBeforeReplacingScene) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created);
        auto& engine = *created.value();
        engine.set_scene(std::make_unique<Scene>());
        const auto original = engine.get_scene();
        auto calls = std::make_shared<RuntimeCalls>();
        calls->fail_update = true;
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        int draws = 0;
        int recoveries = 0;
        engine.get_renderer().set_overlay_renderer([&](CommandBuffer&) { ++draws; });
        const auto result = engine.run(
            [&](Engine::FrameContext&) {
                if(draws >= 2)
                    engine.get_window().request_close();
                return Result<void, Error>::success();
            },
            {},
            [&](const Error& error) {
                ++recoveries;
                EXPECT_EQ(error.message, "runtime update failed");
                EXPECT_EQ(draws, 1);
                EXPECT_FALSE(engine.get_renderer().get_frame_scheduler().is_frame_active());
                EXPECT_FALSE(engine.get_scene_runtime().is_active());
                EXPECT_EQ(calls->stops, 1);
                auto previous = engine.replace_scene(std::make_unique<Scene>());
                EXPECT_EQ(previous.get(), original);
                return Result<void, Error>::success();
            });
        EXPECT_TRUE(result);
        EXPECT_EQ(recoveries, 1);
        EXPECT_EQ(draws, 2);
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
        const auto result = engine.run({}, [&](Engine::FrameContext&) {
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

    TEST(EngineRunTest, RendererFailureAlsoStopsRuntimeAndTaskSubmission) {
        auto created = Engine::create(Config{});
        ASSERT_TRUE(created) << created.error();
        auto& engine = *created.value();
        auto calls = std::make_shared<RuntimeCalls>();
        engine.set_scene(std::make_unique<Scene>());
        ASSERT_TRUE(engine.add_system(std::make_unique<SceneMotionSystem>(calls)));
        ASSERT_TRUE(engine.start_scene_runtime());
        const auto result = engine.run({}, [&](Engine::FrameContext&) {
            engine.get_renderer().prepare_shutdown();
            return Result<void, Error>::success();
        });
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, "Renderer is shutting down");
        EXPECT_EQ(calls->stops, 1);
        EXPECT_FALSE(engine.get_scene_runtime().is_active());
        EXPECT_FALSE(engine.get_task_scheduler().try_submit([] {}));
        EXPECT_FALSE(engine.run());
    }

    TEST(EngineRunTest, CallbackFailureDoesNotLeaveLoopRunning) {
        Config config;
        auto engine_result = Engine::create(config);
        ASSERT_TRUE(engine_result) << engine_result.error().message;
        auto& engine = *engine_result.value();
        const auto failure = engine.run([](Engine::FrameContext&) {
            return Result<void, Error>::failure(
                GraphicsError{"update failed", vk::Result::eErrorDeviceLost}.as_error());
        });
        ASSERT_FALSE(failure);
        EXPECT_EQ(failure.error().message, "update failed");
        EXPECT_EQ(failure.error().code,
            (GraphicsError{"", vk::Result::eErrorDeviceLost}.as_error().code));
        int updates = 0;
        EXPECT_TRUE(engine.run([&](Engine::FrameContext&) {
            ++updates;
            engine.get_window().request_close();
            return Result<void, Error>::success();
        }));
        EXPECT_EQ(updates, 1);
    }

    TEST(EngineRunTest, FailedUpdateDiscardsPreviousFrameDiagnostics) {
        Config config;
        config.diagnostics.enable_render_diagnostics = true;
        auto created = Engine::create(config);
        ASSERT_TRUE(created) << created.error().message;
        auto& engine = *created.value();
        int updates = 0;
        int draws = 0;
        engine.get_renderer().set_overlay_renderer([&](CommandBuffer&) { ++draws; });

        const auto result = engine.run([&](Engine::FrameContext&) {
            if(++updates == 1)
                return Result<void, Error>::success();
            EXPECT_TRUE(engine.frame_diagnostics().current().has_value());
            return Result<void, Error>::failure({"update failed"});
        });

        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().message, "update failed");
        EXPECT_EQ(updates, 2);
        EXPECT_EQ(draws, 1);
        EXPECT_FALSE(engine.frame_diagnostics().current().has_value());
        engine.get_renderer().set_overlay_renderer({});
    }

}
