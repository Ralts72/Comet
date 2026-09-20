#include "scene/scene_runtime.h"
#include "scene/systems/camera_controller.h"
#include "scene/scene.h"

#include <gtest/gtest.h>

#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace Comet::Tests {
    namespace {
        using UpdateResult = Result<void, Error>;
        struct Sample {
            double delta;
            double time;
            uint64_t index;
            Input::Frame input;
        };
        struct Calls {
            std::vector<std::string> order;
            std::vector<Sample> fixed;
            std::vector<Sample> updates;
        };
        class RecordingSystem final: public System {
        public:
            RecordingSystem(Calls& calls, std::string name) : calls(calls), name(std::move(name)) {}
            UpdateResult on_start(Scene& scene) override {
                calls.order.push_back("start " + name);
                if(start)
                    return start(scene);
                return UpdateResult::success();
            }
            UpdateResult fixed_update(Scene& scene, const Context& context) override {
                calls.order.push_back("fixed " + name);
                calls.fixed.push_back(
                    {context.delta_time, context.total_time, context.index, context.input});
                if(fixed)
                    return fixed(scene, context);
                return UpdateResult::success();
            }
            UpdateResult update(Scene& scene, const Context& context) override {
                calls.order.push_back("update " + name);
                calls.updates.push_back(
                    {context.delta_time, context.total_time, context.index, context.input});
                if(update_frame)
                    return update_frame(scene, context);
                return UpdateResult::success();
            }
            void on_stop(Scene& scene) noexcept override {
                calls.order.push_back("stop " + name);
                if(stop)
                    stop(scene);
            }
            std::function<UpdateResult(Scene&)> start;
            std::function<UpdateResult(Scene&, const Context&)> fixed;
            std::function<UpdateResult(Scene&, const Context&)> update_frame;
            std::function<void(Scene&)> stop;

        private:
            Calls& calls;
            std::string name;
        };
    }

    class SceneRuntimeTest: public testing::Test {
    protected:
        using State = SceneRuntime::State;
        Scene scene;
        Calls calls;
        SceneRuntime runtime;
        Input input;

        void SetUp() override {
            input.focus_event(true);
            ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.1}));
        }
        RecordingSystem* add(std::string name = "A") {
            auto system = std::make_unique<RecordingSystem>(calls, std::move(name));
            auto* observer = system.get();
            EXPECT_TRUE(runtime.add_system(std::move(system)));
            return observer;
        }
        void advance(double delta) {
            const auto& frame = input.publish_frame();
            ASSERT_TRUE(runtime.advance(delta, &frame));
        }
    };

    TEST_F(SceneRuntimeTest, OrdersPhasesAndStopsInReverseBeforeRestartingCleanly) {
        add("A");
        add("B");
        ASSERT_TRUE(runtime.start(scene));
        advance(0.21);
        EXPECT_EQ(calls.order, (std::vector<std::string>{"start A", "start B", "fixed A", "fixed B",
                                   "fixed A", "fixed B", "update A", "update B"}));
        EXPECT_EQ(runtime.get_timing().fixed_steps, 2u);
        EXPECT_EQ(runtime.get_timing().fixed_index, 2u);
        EXPECT_NEAR(runtime.get_timing().fixed_time, 0.2, 1e-9);
        EXPECT_NEAR(runtime.get_timing().interpolation, 0.1, 1e-9);
        EXPECT_DOUBLE_EQ(calls.updates.front().delta, 0.21);
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(calls.order[calls.order.size() - 2], "stop B");
        EXPECT_EQ(calls.order.back(), "stop A");
        const auto stopped_calls = calls.order.size();
        ASSERT_TRUE(runtime.stop());
        EXPECT_EQ(calls.order.size(), stopped_calls);
        ASSERT_TRUE(runtime.start(scene));
        advance(0.09);
        EXPECT_EQ(runtime.get_timing().fixed_index, 0u);
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.clear_systems());
    }

    TEST_F(SceneRuntimeTest, AccumulatesZeroStepInputAndConsumesEdgesOnlyOnTheFirstFixedStep) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::Space, true);
        input.cursor_event({0, 0});
        input.cursor_event({2, 3});
        input.scroll_event({0, 1});
        advance(0.03);
        EXPECT_TRUE(calls.fixed.empty());
        input.key_event(Input::Key::Space, false);
        input.cursor_event({5, 7});
        input.scroll_event({0, 2});
        advance(0.03);
        EXPECT_TRUE(calls.fixed.empty());
        advance(0.15);
        ASSERT_EQ(calls.fixed.size(), 2u);
        const auto& first = calls.fixed[0].input;
        EXPECT_TRUE(first.key(Input::Key::Space).pressed);
        EXPECT_TRUE(first.key(Input::Key::Space).released);
        EXPECT_FALSE(first.key(Input::Key::Space).down);
        EXPECT_EQ(first.cursor_delta, Math::Vec2(5, 7));
        EXPECT_EQ(first.scroll, Math::Vec2(0, 3));
        EXPECT_FALSE(calls.fixed[1].input.key(Input::Key::Space).pressed);
        EXPECT_FALSE(calls.fixed[1].input.key(Input::Key::Space).released);
        EXPECT_EQ(calls.fixed[1].input.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(calls.fixed[1].input.scroll, Math::Vec2(0));
        ASSERT_EQ(calls.updates.size(), 3u);
        EXPECT_TRUE(calls.updates[0].input.key(Input::Key::Space).pressed);
        EXPECT_TRUE(calls.updates[1].input.key(Input::Key::Space).released);
        EXPECT_FALSE(calls.updates[2].input.key(Input::Key::Space).pressed);
    }

    TEST_F(SceneRuntimeTest, DuplicateFramesDoNotReplayInputAndEachSystemSeesTheSameEdges) {
        add("A");
        add("B");
        ASSERT_TRUE(runtime.start(scene));
        input.mouse_button_event(Input::MouseButton::Right, true);
        input.scroll_event({0, 1});
        const auto frame = input.publish_frame();
        ASSERT_TRUE(runtime.advance(0.02, &frame));
        ASSERT_TRUE(runtime.advance(0.18, &frame));
        ASSERT_EQ(calls.fixed.size(), 4u);
        for(size_t index = 0; index < 2; ++index) {
            EXPECT_TRUE(calls.fixed[index].input.mouse(Input::MouseButton::Right).pressed);
            EXPECT_EQ(calls.fixed[index].input.scroll.y, 1);
            EXPECT_TRUE(calls.fixed[index + 2].input.mouse(Input::MouseButton::Right).down);
            EXPECT_FALSE(calls.fixed[index + 2].input.mouse(Input::MouseButton::Right).pressed);
            EXPECT_FALSE(calls.updates[index + 2].input.mouse(Input::MouseButton::Right).pressed);
            EXPECT_EQ(calls.updates[index + 2].input.scroll, Math::Vec2(0));
        }
        EXPECT_TRUE(frame.mouse(Input::MouseButton::Right).pressed);
    }

    TEST_F(SceneRuntimeTest, MissingInputAndFocusLossCancelPendingPressesButPreserveRelease) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 2});
        advance(0.04);
        ASSERT_TRUE(runtime.advance(0.02));
        ASSERT_TRUE(runtime.advance(0.04));
        ASSERT_EQ(calls.fixed.size(), 1u);
        EXPECT_FALSE(calls.fixed.back().input.focused);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::W).pressed);
        EXPECT_TRUE(calls.fixed.back().input.key(Input::Key::W).released);
        EXPECT_EQ(calls.fixed.back().input.scroll, Math::Vec2(0));
        EXPECT_TRUE(calls.updates[1].input.key(Input::Key::W).released);
        EXPECT_FALSE(calls.updates[2].input.key(Input::Key::W).released);
        EXPECT_EQ(runtime.get_timing().frame_index, 3u);

        input.key_event(Input::Key::W, false);
        input.key_event(Input::Key::Space, true);
        advance(0.04);
        input.focus_event(false);
        advance(0.06);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::Space).pressed);
        EXPECT_TRUE(calls.fixed.back().input.key(Input::Key::Space).released);
    }

    TEST_F(SceneRuntimeTest, DisconnectAndInputSourceRestartDoNotReplayStalePendingEdges) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        Input::GamepadSample pad;
        input.gamepad_sample(0, pad);
        advance(0);
        pad.buttons[0] = true;
        input.gamepad_sample(0, pad);
        advance(0.04);
        input.gamepad_sample(0, std::nullopt);
        advance(0.06);
        const auto& disconnected = calls.fixed.back().input.gamepads[0];
        EXPECT_FALSE(disconnected.connected);
        EXPECT_FALSE(disconnected.buttons[0].pressed);
        EXPECT_TRUE(disconnected.buttons[0].released);

        input.key_event(Input::Key::W, true);
        advance(0.04);
        Input replacement;
        replacement.focus_event(true);
        replacement.key_event(Input::Key::E, true);
        ASSERT_TRUE(runtime.advance(0.06, &replacement.publish_frame()));
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::W).pressed);
        EXPECT_TRUE(calls.fixed.back().input.key(Input::Key::E).pressed);
    }

    TEST_F(SceneRuntimeTest, BoundsCatchupAndReportsDiscardedTimeWithoutCarryingWholeSteps) {
        ASSERT_TRUE(runtime.set_settings(
            {.fixed_delta = 0.1, .max_frame_delta = 0.55, .max_fixed_steps = 2}));
        add();
        ASSERT_TRUE(runtime.start(scene));
        advance(2.0);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 2u);
        EXPECT_NEAR(runtime.get_timing().dropped_time, 1.75, 1e-9);
        EXPECT_NEAR(runtime.get_timing().interpolation, 0.5, 1e-9);
        EXPECT_DOUBLE_EQ(calls.updates.back().delta, 0.55);
        advance(0.05);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 1u);
        EXPECT_NEAR(runtime.get_timing().fixed_time, 0.3, 1e-9);
        EXPECT_NEAR(runtime.get_timing().interpolation, 0, 1e-9);
    }

    TEST_F(SceneRuntimeTest, EqualTimePartitionsGiveTheSameFixedSimulationWithoutOverload) {
        double position = 0;
        auto* system = add();
        system->fixed = [&](Scene&, const System::Context& context) {
            position += context.delta_time * 3;
            return UpdateResult::success();
        };
        ASSERT_TRUE(runtime.start(scene));
        for(int i = 0; i < 10; ++i)
            advance(0.1);
        const auto reference = position;
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        position = 0;
        for(int i = 0; i < 100; ++i)
            advance(0.01);
        EXPECT_DOUBLE_EQ(position, reference);
        EXPECT_EQ(runtime.get_timing().fixed_index, 10u);
    }

    TEST_F(SceneRuntimeTest, StartupAndUpdateFailuresPreserveErrorAndCleanUpPartialSystems) {
        const Error failure{"system failure", std::make_error_code(std::errc::io_error)};
        add("A");
        auto* failing = add("B");
        add("C");
        failing->start = [&](Scene&) { return UpdateResult::failure(failure); };
        auto result = runtime.start(scene);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, failure.code);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(
            calls.order, (std::vector<std::string>{"start A", "start B", "stop B", "stop A"}));
        failing->start = {};
        for(bool fixed : {true, false}) {
            calls.order.clear();
            failing->fixed = {};
            failing->update_frame = {};
            auto fail = [&](Scene&, const System::Context&) {
                return UpdateResult::failure(failure);
            };
            if(fixed)
                failing->fixed = fail;
            else
                failing->update_frame = fail;
            ASSERT_TRUE(runtime.start(scene));
            result = runtime.advance(0.1);
            ASSERT_FALSE(result);
            EXPECT_EQ(result.error().message, failure.message);
            EXPECT_EQ(result.error().code, failure.code);
            EXPECT_FALSE(runtime.is_active());
            EXPECT_EQ(calls.order[calls.order.size() - 3], "stop C");
            EXPECT_EQ(calls.order[calls.order.size() - 2], "stop B");
            EXPECT_EQ(calls.order.back(), "stop A");
        }
    }

    TEST_F(SceneRuntimeTest, InvalidSettingsAndReentryAreRejectedWithoutChangingTheActiveRun) {
        EXPECT_FALSE(runtime.set_state(State::Paused));
        EXPECT_FALSE(runtime.request_step());
        EXPECT_FALSE(runtime.set_settings({.fixed_delta = 0}));
        EXPECT_FALSE(runtime.set_settings({.max_fixed_steps = 0}));
        EXPECT_FALSE(
            runtime.set_settings({.max_frame_delta = std::numeric_limits<double>::infinity()}));
        EXPECT_FALSE(runtime.add_system(nullptr));
        auto* system = add();
        system->update_frame = [&](Scene& active, const System::Context&) {
            EXPECT_FALSE(runtime.start(active));
            EXPECT_FALSE(runtime.stop());
            EXPECT_FALSE(runtime.set_state(State::Paused));
            EXPECT_FALSE(runtime.request_step());
            EXPECT_FALSE(runtime.advance(0));
            EXPECT_FALSE(runtime.clear_systems());
            EXPECT_FALSE(runtime.set_settings({}));
            EXPECT_FALSE(runtime.add_system(std::make_unique<RecordingSystem>(calls, "nested")));
            return UpdateResult::success();
        };
        system->stop = [&](Scene& active) {
            EXPECT_EQ(&active, &scene);
            EXPECT_FALSE(runtime.stop());
        };
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_FALSE(runtime.request_step());
        EXPECT_FALSE(runtime.set_state(static_cast<State>(99)));
        EXPECT_FALSE(runtime.advance(-1));
        EXPECT_FALSE(runtime.advance(std::numeric_limits<double>::quiet_NaN()));
        advance(0.1);
        EXPECT_TRUE(runtime.is_active());
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
        ASSERT_TRUE(runtime.stop());
        system->stop = {};
    }

    TEST_F(SceneRuntimeTest, PauseFreezesSimulationAndResumeRebasesTimeAndInput) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::W, true);
        advance(0.04);
        ASSERT_TRUE(runtime.set_state(State::Paused));
        EXPECT_TRUE(runtime.is_active());
        const auto before = runtime.get_timing();
        input.key_event(Input::Key::W, false);
        input.key_event(Input::Key::Space, true);
        input.cursor_event({0, 0});
        input.cursor_event({9, 8});
        input.scroll_event({0, 4});
        advance(60);
        input.key_event(Input::Key::Space, false);
        advance(60);
        EXPECT_EQ(calls.order, (std::vector<std::string>{"start A", "update A"}));
        EXPECT_EQ(runtime.get_timing().frame_index, before.frame_index);
        EXPECT_EQ(runtime.get_timing().fixed_index, before.fixed_index);
        EXPECT_DOUBLE_EQ(runtime.get_timing().total_time, before.total_time);
        EXPECT_DOUBLE_EQ(runtime.get_timing().dropped_time, 0);
        EXPECT_DOUBLE_EQ(runtime.get_timing().interpolation, 0);

        ASSERT_TRUE(runtime.set_state(State::Running));
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 2});
        advance(0.04);
        EXPECT_TRUE(calls.fixed.empty());
        EXPECT_TRUE(calls.updates.back().input.key(Input::Key::W).down);
        EXPECT_FALSE(calls.updates.back().input.key(Input::Key::W).pressed);
        EXPECT_EQ(calls.updates.back().input.scroll, Math::Vec2(0));
        input.key_event(Input::Key::W, false);
        advance(0.06);
        ASSERT_EQ(calls.fixed.size(), 1u);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::W).pressed);
        EXPECT_TRUE(calls.fixed.back().input.key(Input::Key::W).released);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::Space).pressed);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::Space).released);
        EXPECT_EQ(calls.fixed.back().input.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(calls.fixed.back().input.scroll, Math::Vec2(0));
        EXPECT_NEAR(runtime.get_timing().total_time, 0.14, 1e-9);
    }

    TEST_F(SceneRuntimeTest, SingleStepRunsBothPhasesOnceAndCoalescesPendingRequests) {
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.1, .max_frame_delta = 0.01}));
        add("A");
        add("B");
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.set_state(State::Paused));
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 2});
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.request_step());
        advance(10);
        EXPECT_EQ(calls.order, (std::vector<std::string>{"start A", "start B", "fixed A", "fixed B",
                                   "update A", "update B"}));
        EXPECT_EQ(runtime.get_state(), State::Paused);
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 1u);
        EXPECT_EQ(runtime.get_timing().fixed_index, 1u);
        EXPECT_DOUBLE_EQ(runtime.get_timing().total_time, 0.1);
        EXPECT_DOUBLE_EQ(runtime.get_timing().fixed_time, 0.1);
        EXPECT_DOUBLE_EQ(runtime.get_timing().dropped_time, 0);
        for(const auto& sample : calls.fixed) {
            EXPECT_DOUBLE_EQ(sample.delta, 0.1);
            EXPECT_TRUE(sample.input.key(Input::Key::W).down);
            EXPECT_FALSE(sample.input.key(Input::Key::W).pressed);
            EXPECT_EQ(sample.input.scroll, Math::Vec2(0));
        }
        EXPECT_DOUBLE_EQ(calls.updates.front().delta, 0.1);
        advance(10);
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 0u);
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(runtime.get_timing().fixed_index, 2u);
        EXPECT_FALSE(calls.fixed.back().input.focused);
        EXPECT_FALSE(calls.fixed.back().input.key(Input::Key::W).down);
        EXPECT_EQ(calls.updates.size(), 4u);
    }

    TEST_F(SceneRuntimeTest, ResumeAndStopCancelPendingStepsAndRestartIsRunning) {
        add();
        ASSERT_TRUE(runtime.start(scene));
        advance(0.06);
        ASSERT_TRUE(runtime.set_state(State::Paused));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.set_state(State::Running));
        advance(0.04);
        EXPECT_TRUE(calls.fixed.empty());
        ASSERT_TRUE(runtime.set_state(State::Paused));
        ASSERT_TRUE(runtime.request_step());
        ASSERT_TRUE(runtime.stop());
        ASSERT_TRUE(runtime.start(scene));
        EXPECT_EQ(runtime.get_state(), State::Running);
        advance(0);
        EXPECT_TRUE(calls.fixed.empty());
        EXPECT_EQ(runtime.get_timing().frame_index, 1u);
    }

    TEST_F(SceneRuntimeTest, FailedSingleStepCleansUpAndCannotBeReplayed) {
        add("A");
        auto* failing = add("B");
        const Error failure{"step failed", std::make_error_code(std::errc::io_error)};
        failing->fixed = [&](Scene&, const System::Context&) {
            EXPECT_FALSE(runtime.set_state(State::Running));
            EXPECT_FALSE(runtime.request_step());
            return UpdateResult::failure(failure);
        };
        ASSERT_TRUE(runtime.start(scene));
        ASSERT_TRUE(runtime.set_state(State::Paused));
        ASSERT_TRUE(runtime.request_step());
        const auto result = runtime.advance(0);
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, failure.code);
        EXPECT_EQ(result.error().message, failure.message);
        EXPECT_FALSE(runtime.is_active());
        EXPECT_EQ(calls.order[calls.order.size() - 2], "stop B");
        EXPECT_EQ(calls.order.back(), "stop A");
        EXPECT_TRUE(calls.updates.empty());
        ASSERT_TRUE(runtime.advance(0));
        EXPECT_EQ(calls.fixed.size(), 2u);
    }

    TEST_F(SceneRuntimeTest, CameraRunsOncePerFrameNotOncePerFixedStep) {
        auto camera = scene.create_entity("Camera");
        camera.add_component<CameraComponent>().primary = true;
        camera.add_component<CameraControllerComponent>();
        ASSERT_TRUE(runtime.set_settings({.fixed_delta = 0.01}));
        ASSERT_TRUE(runtime.add_system(std::make_unique<CameraControllerSystem>()));
        ASSERT_TRUE(runtime.start(scene));
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 1});
        advance(0.05);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 5u);
        EXPECT_NEAR(camera.get_component<TransformComponent>().translation.z, -0.35f, 1e-6f);
        ASSERT_TRUE(runtime.advance(0.05));
        EXPECT_EQ(runtime.get_timing().fixed_steps, 5u);
        EXPECT_NEAR(camera.get_component<TransformComponent>().translation.z, -0.35f, 1e-6f);
        advance(10);
        EXPECT_NEAR(camera.get_component<TransformComponent>().translation.z, -1.10f, 1e-6f);
        EXPECT_DOUBLE_EQ(runtime.get_timing().total_time, 0.35);
    }
}
