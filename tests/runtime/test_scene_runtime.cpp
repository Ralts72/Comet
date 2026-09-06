#include "runtime/scene_runtime.h"
#include "scene/scene.h"

#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>
#include <string>

namespace Comet::Tests {
    class SceneRuntimeTest: public ::testing::Test {
    protected:
        struct Sample {
            char phase;
            int system;
            uint64_t index;
            double delta;
            Input::Frame input;
        };
        struct Probe final: System {
            Probe(int id, std::vector<Sample>& samples) : id(id), samples(samples) {}
            void on_start(Scene&) override {
                samples.push_back({'s', id, 0, 0, {}});
                if(fail_start)
                    throw std::runtime_error("start failed");
            }
            void fixed_update(Scene&, const Context& context) override {
                samples.push_back(
                    {'f', id, context.index, context.delta_time, context.input});
                if(fail_update)
                    throw std::runtime_error("update failed");
            }
            void update(Scene&, const Context& context) override {
                samples.push_back(
                    {'u', id, context.index, context.delta_time, context.input});
            }
            void on_stop(Scene& scene) noexcept override {
                scene.create_entity("Stopped");
                samples.push_back({'x', id, 0, 0, {}});
            }
            int id;
            std::vector<Sample>& samples;
            bool fail_start = false;
            bool fail_update = false;
        };
        Scene scene;
        std::vector<Sample> samples;
        SceneRuntime runtime{
            {.fixed_delta = 0.01, .max_frame_delta = 0.1, .max_fixed_steps = 4}};
        Input input;

        Probe& add(int id) {
            auto probe = std::make_unique<Probe>(id, samples);
            auto& ref = *probe;
            runtime.add_system(std::move(probe));
            return ref;
        }
        void start() {
            input.focus_event(true);
            runtime.start(scene);
            samples.clear();
        }
    };

    TEST_F(SceneRuntimeTest, FixedPhasesPrecedeOrdinaryUpdateAndStopInReverseOrder) {
        add(1);
        add(2);
        start();
        runtime.advance(0.025, input.publish_frame());
        ASSERT_EQ(samples.size(), 6U);
        for(size_t i = 0; i < samples.size(); ++i) {
            EXPECT_EQ(samples[i].system, int(i % 2) + 1);
            EXPECT_EQ(samples[i].phase, i < 4 ? 'f' : 'u');
            EXPECT_EQ(samples[i].index, i < 4 ? i / 2 + 1 : 1);
            EXPECT_DOUBLE_EQ(samples[i].delta, i < 4 ? 0.01 : 0.025);
        }
        EXPECT_NEAR(runtime.get_timing().interpolation, 0.5, 1e-12);
        runtime.stop();
        EXPECT_EQ(samples[6].system, 2);
        EXPECT_EQ(samples[7].system, 1);
        EXPECT_EQ(scene.entity_count(), 2U);
        runtime.stop();
        EXPECT_EQ(samples.size(), 8U);
    }

    TEST_F(SceneRuntimeTest, ZeroStepRetainsEdgesAndOnlyFirstFixedStepConsumesThem) {
        add(1);
        start();
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 2});
        runtime.advance(0.004, input.publish_frame());
        input.key_event(Input::Key::W, false);
        input.scroll_event({0, 3});
        runtime.advance(0.026, input.publish_frame());
        ASSERT_EQ(samples.size(), 5U);
        const auto& first = samples[1].input;
        EXPECT_TRUE(first.key(Input::Key::W).pressed);
        EXPECT_TRUE(first.key(Input::Key::W).released);
        EXPECT_FALSE(first.key(Input::Key::W).down);
        EXPECT_EQ(first.scroll.y, 5);
        for(size_t i : {2, 3}) {
            EXPECT_FALSE(samples[i].input.key(Input::Key::W).pressed);
            EXPECT_FALSE(samples[i].input.key(Input::Key::W).released);
            EXPECT_EQ(samples[i].input.scroll.y, 0);
        }
        EXPECT_FALSE(samples[4].input.key(Input::Key::W).pressed);
        EXPECT_TRUE(samples[4].input.key(Input::Key::W).released);
        EXPECT_EQ(samples[4].input.scroll.y, 3);
    }

    TEST_F(SceneRuntimeTest, ReusingPhysicalFrameDoesNotReplayEdgesButRetainsHeldState) {
        add(1);
        start();
        input.key_event(Input::Key::W, true);
        const auto frame = input.publish_frame();
        runtime.advance(0.01, frame);
        runtime.advance(0.01, frame);
        ASSERT_EQ(samples.size(), 4U);
        for(size_t i = 0; i < samples.size(); ++i) {
            EXPECT_TRUE(samples[i].input.key(Input::Key::W).down);
            EXPECT_EQ(samples[i].input.key(Input::Key::W).pressed, i < 2);
        }
    }

    TEST_F(SceneRuntimeTest, FocusLossAndDisconnectCancelPendingPresses) {
        add(1);
        start();
        Input::GamepadSample pad;
        input.gamepad_sample(0, pad);
        input.publish_frame();
        pad.buttons[0] = true;
        input.gamepad_sample(0, pad);
        input.key_event(Input::Key::W, true);
        input.scroll_event({0, 2});
        runtime.advance(0.002, input.publish_frame());
        input.gamepad_sample(0, std::nullopt);
        runtime.advance(0.002, input.publish_frame());
        input.focus_event(false);
        runtime.advance(0.006, input.publish_frame());
        ASSERT_EQ(samples.size(), 4U);
        const auto& fixed = samples[2].input;
        EXPECT_FALSE(fixed.focused);
        EXPECT_FALSE(fixed.key(Input::Key::W).pressed);
        EXPECT_TRUE(fixed.key(Input::Key::W).released);
        EXPECT_FALSE(fixed.gamepads[0].buttons[0].pressed);
        EXPECT_TRUE(fixed.gamepads[0].buttons[0].released);
        EXPECT_EQ(fixed.scroll.y, 0);
    }

    TEST_F(SceneRuntimeTest, LongFrameHasBoundedWorkAndReportsDroppedTime) {
        add(1);
        start();
        runtime.advance(0.507, input.publish_frame());
        const auto& timing = runtime.get_timing();
        EXPECT_EQ(timing.fixed_steps, 4U);
        EXPECT_EQ(timing.fixed_index, 4U);
        EXPECT_NEAR(timing.dropped_time, 0.467, 1e-12);
        EXPECT_DOUBLE_EQ(timing.total_time, 0.1);
        EXPECT_DOUBLE_EQ(timing.fixed_time, 0.04);
        EXPECT_NEAR(timing.interpolation, 0, 1e-12);
        runtime.advance(0, input.publish_frame());
        EXPECT_EQ(timing.fixed_steps, 0U);
        EXPECT_EQ(timing.frame_index, 2U);
    }

    TEST_F(SceneRuntimeTest, TimePartitionAndOwnedInputsProduceRepeatableFixedUpdates) {
        add(1);
        start();
        input.key_event(Input::Key::W, true);
        const auto frame = input.publish_frame();
        runtime.advance(0.03, frame);
        const auto original = samples;
        runtime.stop();
        runtime.start(scene);
        samples.clear();
        runtime.advance(0.004, frame);
        runtime.advance(0.006, frame);
        runtime.advance(0.02, frame);
        size_t index = 0;
        for(const auto& sample : samples) {
            if(sample.phase != 'f')
                continue;
            EXPECT_EQ(sample.index, original[index].index);
            EXPECT_DOUBLE_EQ(sample.delta, original[index].delta);
            EXPECT_EQ(sample.input.key(Input::Key::W).pressed,
                original[index].input.key(Input::Key::W).pressed);
            ++index;
        }
        EXPECT_EQ(index, 3U);
    }

    TEST_F(SceneRuntimeTest, StartupFailureStopsPartiallyStartedSystemsAndCanRestart) {
        add(1);
        auto& failing = add(2);
        failing.fail_start = true;
        add(3);
        EXPECT_THROW(runtime.start(scene), std::runtime_error);
        EXPECT_FALSE(runtime.is_active());
        ASSERT_EQ(samples.size(), 4U);
        EXPECT_EQ(samples[2].phase, 'x');
        EXPECT_EQ(samples[2].system, 2);
        EXPECT_EQ(samples[3].system, 1);
        failing.fail_start = false;
        start();
        EXPECT_TRUE(runtime.is_active());
        EXPECT_EQ(runtime.get_timing().fixed_index, 0U);
    }

    TEST_F(SceneRuntimeTest, UpdateFailureStopsRuntimeInsteadOfRetryingPartialStep) {
        add(1).fail_update = true;
        start();
        EXPECT_THROW(runtime.advance(0.01, input.publish_frame()), std::runtime_error);
        EXPECT_FALSE(runtime.is_active());
        ASSERT_EQ(samples.size(), 2U);
        EXPECT_EQ(samples.back().phase, 'x');
        runtime.advance(0.01, input.publish_frame());
        EXPECT_EQ(samples.size(), 2U);
    }

    TEST_F(SceneRuntimeTest, RejectsInvalidSettingsTimeSerialAndActiveGraphMutation) {
        EXPECT_THROW(SceneRuntime({.fixed_delta = 0}), std::invalid_argument);
        EXPECT_THROW(SceneRuntime({.max_frame_delta = INFINITY}), std::invalid_argument);
        EXPECT_THROW(SceneRuntime({.max_fixed_steps = 0}), std::invalid_argument);
        EXPECT_THROW(runtime.add_system(nullptr), std::invalid_argument);
        add(1);
        start();
        EXPECT_THROW(runtime.start(scene), std::logic_error);
        EXPECT_THROW(runtime.clear_systems(), std::logic_error);
        EXPECT_THROW(
            runtime.add_system(std::make_unique<Probe>(2, samples)), std::logic_error);
        EXPECT_THROW(runtime.advance(-1, input.publish_frame()), std::invalid_argument);
        EXPECT_THROW(runtime.advance(
                         std::numeric_limits<double>::quiet_NaN(), input.publish_frame()),
            std::invalid_argument);
        runtime.advance(0, input.publish_frame());
        EXPECT_THROW(runtime.advance(0, Input::Frame{}), std::invalid_argument);
        EXPECT_TRUE(runtime.is_active());
        EXPECT_EQ(runtime.get_timing().frame_index, 1U);
        runtime.stop();
        runtime.clear_systems();
    }

    TEST_F(SceneRuntimeTest, ExecutionRejectsReentryAndLifecycleChanges) {
        struct Reentry final: System {
            SceneRuntime& runtime;
            explicit Reentry(SceneRuntime& runtime) : runtime(runtime) {}
            void update(Scene& scene, const Context& context) override {
                EXPECT_THROW(runtime.advance(0, context.input), std::logic_error);
                EXPECT_THROW(runtime.start(scene), std::logic_error);
                EXPECT_THROW(runtime.stop(), std::logic_error);
                EXPECT_THROW(
                    runtime.set_state(SceneRuntime::State::Paused), std::logic_error);
                EXPECT_THROW(runtime.request_step(), std::logic_error);
                EXPECT_THROW(runtime.clear_systems(), std::logic_error);
            }
        };
        runtime.add_system(std::make_unique<Reentry>(runtime));
        start();
        runtime.advance(0, input.publish_frame());
        EXPECT_TRUE(runtime.is_active());
    }

    TEST_F(SceneRuntimeTest, PauseFreezesBothPhasesAndDropsTimeAndPendingEdges) {
        add(1);
        start();
        input.key_event(Input::Key::W, true);
        runtime.advance(0.004, input.publish_frame());
        runtime.set_state(SceneRuntime::State::Paused);
        const auto timing = runtime.get_timing();
        samples.clear();
        input.key_event(Input::Key::W, false);
        input.scroll_event({0, 20});
        runtime.advance(100, input.publish_frame());
        EXPECT_TRUE(samples.empty());
        EXPECT_EQ(runtime.get_timing().frame_index, timing.frame_index);
        EXPECT_EQ(runtime.get_timing().fixed_index, timing.fixed_index);
        EXPECT_DOUBLE_EQ(runtime.get_timing().total_time, timing.total_time);
        EXPECT_DOUBLE_EQ(runtime.get_timing().dropped_time, 0);
        runtime.set_state(SceneRuntime::State::Running);
        runtime.advance(0.01, input.publish_frame());
        ASSERT_EQ(samples.size(), 2U);
        EXPECT_FALSE(samples[0].input.key(Input::Key::W).pressed);
        EXPECT_FALSE(samples[0].input.key(Input::Key::W).released);
        EXPECT_EQ(samples[0].input.scroll.y, 0);
        EXPECT_EQ(runtime.get_timing().fixed_index, 1U);
    }

    TEST_F(
        SceneRuntimeTest, SingleStepRunsExactlyOneFixedAndOrdinaryUpdateAndStaysPaused) {
        add(1);
        start();
        runtime.set_state(SceneRuntime::State::Paused);
        input.key_event(Input::Key::W, true);
        runtime.request_step();
        runtime.request_step();
        runtime.advance(123, input.publish_frame());
        ASSERT_EQ(samples.size(), 2U);
        EXPECT_EQ(samples[0].phase, 'f');
        EXPECT_EQ(samples[1].phase, 'u');
        EXPECT_DOUBLE_EQ(samples[0].delta, 0.01);
        EXPECT_DOUBLE_EQ(samples[1].delta, 0.01);
        EXPECT_TRUE(samples[0].input.key(Input::Key::W).down);
        EXPECT_FALSE(samples[0].input.key(Input::Key::W).pressed);
        EXPECT_EQ(runtime.get_state(), SceneRuntime::State::Paused);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 1U);
        EXPECT_DOUBLE_EQ(runtime.get_timing().total_time, 0.01);
        runtime.advance(123, input.publish_frame());
        EXPECT_EQ(samples.size(), 2U);
        EXPECT_EQ(runtime.get_timing().fixed_steps, 0U);
        runtime.request_step();
        runtime.advance(0, input.publish_frame());
        EXPECT_EQ(runtime.get_timing().fixed_index, 2U);
        EXPECT_EQ(runtime.get_timing().frame_index, 2U);
    }

    TEST_F(SceneRuntimeTest, ResumeRebasesEvenWithoutAnInterveningPausedFrame) {
        add(1);
        start();
        input.key_event(Input::Key::W, true);
        const auto frame = input.publish_frame();
        runtime.advance(0.004, frame);
        runtime.set_state(SceneRuntime::State::Paused);
        runtime.set_state(SceneRuntime::State::Running);
        samples.clear();
        runtime.advance(0.01, frame);
        ASSERT_EQ(samples.size(), 2U);
        EXPECT_TRUE(samples[0].input.key(Input::Key::W).down);
        EXPECT_FALSE(samples[0].input.key(Input::Key::W).pressed);
        input.key_event(Input::Key::W, false);
        runtime.advance(0.01, input.publish_frame());
        EXPECT_TRUE(samples[2].input.key(Input::Key::W).released);
    }

    TEST_F(SceneRuntimeTest, RestartAndResumeCancelQueuedStepsAndValidateState) {
        EXPECT_THROW(runtime.request_step(), std::logic_error);
        EXPECT_THROW(runtime.set_state(SceneRuntime::State::Paused), std::logic_error);
        add(1);
        start();
        EXPECT_THROW(runtime.request_step(), std::logic_error);
        EXPECT_THROW(runtime.set_state(static_cast<SceneRuntime::State>(99)),
            std::invalid_argument);
        runtime.set_state(SceneRuntime::State::Paused);
        runtime.request_step();
        runtime.set_state(SceneRuntime::State::Running);
        runtime.advance(0, input.publish_frame());
        EXPECT_EQ(runtime.get_timing().fixed_index, 0U);
        runtime.set_state(SceneRuntime::State::Paused);
        runtime.request_step();
        runtime.stop();
        runtime.start(scene);
        EXPECT_EQ(runtime.get_state(), SceneRuntime::State::Running);
        runtime.advance(0, input.publish_frame());
        EXPECT_EQ(runtime.get_timing().fixed_index, 0U);
    }

    TEST_F(SceneRuntimeTest, ExplicitStepIsNotClampedByWallFrameLimit) {
        SceneRuntime limited({.fixed_delta = 0.02, .max_frame_delta = 0.001});
        limited.start(scene);
        limited.set_state(SceneRuntime::State::Paused);
        limited.request_step();
        limited.advance(0, input.publish_frame());
        EXPECT_EQ(limited.get_timing().fixed_steps, 1U);
        EXPECT_DOUBLE_EQ(limited.get_timing().total_time, 0.02);
        EXPECT_DOUBLE_EQ(limited.get_timing().dropped_time, 0);
    }
}
