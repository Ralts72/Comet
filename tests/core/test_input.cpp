#include "core/input.h"

#include <gtest/gtest.h>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

namespace Comet::Tests {
    static_assert(std::is_trivially_copyable_v<Input::Frame>);

    class InputTest: public testing::Test {
    protected:
        Input input;
        void SetUp() override { input.focus_event(true); }
    };

    TEST_F(InputTest, GatesAreIndependentAndPublishStableConsumerSerials) {
        Input::Gate game;
        Input::Gate other(false);
        input.key_event(Input::Key::W, true);
        const auto source = input.publish_frame();
        const auto first = game.read(source, true);
        EXPECT_TRUE(first.key(Input::Key::W).pressed);
        EXPECT_FALSE(other.read(source, false).key(Input::Key::W).down);
        EXPECT_EQ(game.read(source, true).serial, first.serial);
        const auto disabled = game.read(source, false);
        EXPECT_GT(disabled.serial, first.serial);
        EXPECT_TRUE(disabled.key(Input::Key::W).released);
        EXPECT_FALSE(disabled.focused);
        EXPECT_EQ(game.read(source, false).serial, disabled.serial);
        EXPECT_FALSE(other.read(source, true).key(Input::Key::W).down);
        EXPECT_TRUE(source.key(Input::Key::W).pressed);
        EXPECT_TRUE(first.key(Input::Key::W).pressed);
        EXPECT_THROW(game.read(Input::Frame{}, true), std::invalid_argument);
    }

    TEST_F(InputTest, GateReacquisitionBlocksHeldButtonsUntilPhysicalRelease) {
        Input::Gate gate;
        input.key_event(Input::Key::W, true);
        input.mouse_button_event(Input::MouseButton::Left, true);
        gate.read(input.publish_frame(), true);
        const auto closed = gate.read(input.publish_frame(), false);
        EXPECT_TRUE(closed.key(Input::Key::W).released);
        EXPECT_TRUE(closed.mouse(Input::MouseButton::Left).released);
        EXPECT_FALSE(gate.read(input.publish_frame(), false).key(Input::Key::W).released);
        const auto reopened = gate.read(input.publish_frame(), true);
        EXPECT_TRUE(reopened.focused);
        EXPECT_FALSE(reopened.key(Input::Key::W).down);
        EXPECT_FALSE(reopened.mouse(Input::MouseButton::Left).down);
        EXPECT_FALSE(gate.read(input.publish_frame(), true).key(Input::Key::W).down);
        input.key_event(Input::Key::W, false);
        input.mouse_button_event(Input::MouseButton::Left, false);
        const auto released = gate.read(input.publish_frame(), true);
        EXPECT_FALSE(released.key(Input::Key::W).released);
        input.key_event(Input::Key::W, true);
        input.mouse_button_event(Input::MouseButton::Left, true);
        const auto pressed = gate.read(input.publish_frame(), true);
        EXPECT_TRUE(pressed.key(Input::Key::W).pressed);
        EXPECT_TRUE(pressed.mouse(Input::MouseButton::Left).pressed);
    }

    TEST_F(InputTest,
        GateSuppressesActivationMotionAndRoutesGamepadWithoutChoosingDeadzone) {
        Input::Gate gate(false);
        Input::GamepadSample pad;
        pad.buttons.back() = true;
        pad.axes[0] = 0.7f;
        input.gamepad_sample(Input::MAX_GAMEPADS - 1, pad);
        input.cursor_event({0, 0});
        input.cursor_event({100, 30});
        input.scroll_event({2, 3});
        const auto acquiring = gate.read(input.publish_frame(), true);
        EXPECT_FALSE(acquiring.gamepads.back().buttons.back().down);
        EXPECT_EQ(acquiring.gamepads.back().axes[0], 0);
        EXPECT_EQ(acquiring.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(acquiring.scroll, Math::Vec2(0));
        input.cursor_event({105, 33});
        input.scroll_event({0, 1});
        const auto active = gate.read(input.publish_frame(), true);
        EXPECT_EQ(active.gamepads.back().axes[0], 0.7f);
        EXPECT_FALSE(active.gamepads.back().buttons.back().down);
        EXPECT_EQ(active.cursor_delta, Math::Vec2(5, 3));
        EXPECT_EQ(active.scroll, Math::Vec2(0, 1));
        input.gamepad_sample(Input::MAX_GAMEPADS - 1, std::nullopt);
        gate.read(input.publish_frame(), true);
        pad.buttons.back() = false;
        input.gamepad_sample(Input::MAX_GAMEPADS - 1, pad);
        gate.read(input.publish_frame(), true);
        pad.buttons.back() = true;
        input.gamepad_sample(Input::MAX_GAMEPADS - 1, pad);
        EXPECT_TRUE(gate.read(input.publish_frame(), true)
                .gamepads.back()
                .buttons.back()
                .pressed);
    }

    TEST_F(InputTest, GateHonorsPlatformFocusAndFrameTransientResetPreservesLevels) {
        Input::Gate gate;
        input.key_event(Input::Key::Space, true);
        auto frame = input.publish_frame();
        gate.read(frame, true);
        frame.clear_transients();
        EXPECT_TRUE(frame.key(Input::Key::Space).down);
        EXPECT_FALSE(frame.key(Input::Key::Space).pressed);
        EXPECT_TRUE(frame.focused);
        input.focus_event(false);
        const auto unfocused = gate.read(input.publish_frame(), true);
        EXPECT_FALSE(unfocused.focused);
        EXPECT_TRUE(unfocused.key(Input::Key::Space).released);
        input.focus_event(true);
        input.key_event(Input::Key::Space, true);
        EXPECT_FALSE(gate.read(input.publish_frame(), true).key(Input::Key::Space).down);
    }

    TEST_F(InputTest, GateInterruptionPublishesReleaseBeforeReacquisition) {
        Input::Gate gate;
        input.key_event(Input::Key::W, true);
        const auto source = input.publish_frame();
        gate.read(source, true);
        gate.interrupt();
        const auto release = gate.read(source, true);
        EXPECT_TRUE(release.key(Input::Key::W).released);
        EXPECT_FALSE(release.focused);
        const auto regain = gate.read(source, true);
        EXPECT_TRUE(regain.focused);
        EXPECT_FALSE(regain.key(Input::Key::W).down);
        EXPECT_FALSE(regain.key(Input::Key::W).released);
        EXPECT_GT(regain.serial, release.serial);
    }

    TEST_F(InputTest, PublishesStableFramesAndKeepsHeldStateWithoutRepeatingPress) {
        input.key_event(Input::Key::W, true);
        EXPECT_FALSE(input.get_frame().key(Input::Key::W).down);
        const auto first = input.publish_frame();
        EXPECT_EQ(first.serial, 1u);
        EXPECT_TRUE(first.key(Input::Key::W).down);
        EXPECT_TRUE(first.key(Input::Key::W).pressed);
        input.key_event(Input::Key::W, true);
        const auto held = input.publish_frame();
        EXPECT_TRUE(held.key(Input::Key::W).down);
        EXPECT_FALSE(held.key(Input::Key::W).pressed);
        input.key_event(Input::Key::W, false);
        EXPECT_TRUE(input.get_frame().key(Input::Key::W).down);
        const auto released = input.publish_frame();
        EXPECT_FALSE(released.key(Input::Key::W).down);
        EXPECT_TRUE(released.key(Input::Key::W).released);
        EXPECT_TRUE(first.key(Input::Key::W).down);
        EXPECT_TRUE(first.key(Input::Key::W).pressed);
        EXPECT_FALSE(input.publish_frame().key(Input::Key::W).released);
    }

    TEST_F(InputTest, QuickTapPreservesBothEdgesWithoutAnUnboundedEventQueue) {
        input.key_event(Input::Key::Space, true);
        input.key_event(Input::Key::Space, false);
        input.mouse_button_event(Input::MouseButton::Left, true);
        input.mouse_button_event(Input::MouseButton::Left, false);
        const auto frame = input.publish_frame();
        EXPECT_FALSE(frame.key(Input::Key::Space).down);
        EXPECT_TRUE(frame.key(Input::Key::Space).pressed);
        EXPECT_TRUE(frame.key(Input::Key::Space).released);
        EXPECT_FALSE(frame.mouse(Input::MouseButton::Left).down);
        EXPECT_TRUE(frame.mouse(Input::MouseButton::Left).pressed);
        EXPECT_TRUE(frame.mouse(Input::MouseButton::Left).released);
    }

    TEST_F(InputTest, CursorAndScrollAccumulateAndResetAtPublication) {
        input.cursor_event({10, 20});
        input.cursor_event({13, 16});
        input.cursor_event({15, 18});
        input.scroll_event({0.5f, 2});
        input.scroll_event({-0.25f, 1});
        const auto frame = input.publish_frame();
        EXPECT_EQ(frame.cursor_position, Math::Vec2(15, 18));
        EXPECT_EQ(frame.cursor_delta, Math::Vec2(5, -2));
        EXPECT_EQ(frame.scroll, Math::Vec2(0.25f, 3));
        const auto next = input.publish_frame();
        EXPECT_EQ(next.cursor_position, frame.cursor_position);
        EXPECT_EQ(next.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(next.scroll, Math::Vec2(0));
    }

    TEST_F(InputTest, FocusLossReleasesAllControlsAndRegainDoesNotInventEdgesOrMotion) {
        input.gamepad_sample(0, std::nullopt);
        Input::GamepadSample sample;
        sample.buttons[0] = true;
        sample.axes[0] = 0.8f;
        input.gamepad_sample(0, sample);
        input.key_event(Input::Key::A, true);
        input.mouse_button_event(Input::MouseButton::Right, true);
        input.cursor_event({1, 2});
        input.publish_frame();
        input.focus_event(false);
        input.key_event(Input::Key::D, true);
        input.mouse_button_event(Input::MouseButton::Middle, true);
        input.cursor_event({100, 200});
        input.scroll_event({0, 5});
        input.gamepad_sample(0, sample);
        const auto lost = input.publish_frame();
        EXPECT_FALSE(lost.focused);
        EXPECT_TRUE(lost.key(Input::Key::A).released);
        EXPECT_FALSE(lost.key(Input::Key::D).down);
        EXPECT_TRUE(lost.mouse(Input::MouseButton::Right).released);
        EXPECT_FALSE(lost.mouse(Input::MouseButton::Middle).down);
        EXPECT_TRUE(lost.gamepads[0].connected);
        EXPECT_TRUE(lost.gamepads[0].button(Input::GamepadButton::South).released);
        EXPECT_FALSE(lost.gamepads[0].button(Input::GamepadButton::South).down);
        EXPECT_EQ(lost.gamepads[0].axis(Input::GamepadAxis::LeftX), 0);
        EXPECT_EQ(lost.scroll, Math::Vec2(0));
        EXPECT_EQ(lost.cursor_delta, Math::Vec2(0));
        input.focus_event(true);
        input.cursor_event({300, 400});
        input.gamepad_sample(0, sample);
        const auto regained = input.publish_frame();
        EXPECT_TRUE(regained.focused);
        EXPECT_EQ(regained.cursor_delta, Math::Vec2(0));
        EXPECT_TRUE(regained.gamepads[0].button(Input::GamepadButton::South).down);
        EXPECT_FALSE(regained.gamepads[0].button(Input::GamepadButton::South).pressed);
    }

    TEST_F(InputTest, GamepadSamplesNormalizeInvalidAxesAndReleaseOnDisconnect) {
        input.gamepad_sample(2, std::nullopt);
        Input::GamepadSample sample;
        sample.buttons[0] = true;
        sample.axes = {2, std::numeric_limits<float>::quiet_NaN(), -2, 0.25f, -1, 2};
        input.gamepad_sample(2, sample);
        const auto connected = input.publish_frame().gamepads[2];
        EXPECT_TRUE(connected.connected);
        EXPECT_TRUE(connected.button(Input::GamepadButton::South).pressed);
        EXPECT_EQ(connected.axes, (std::array<float, 6>{1, 0, -1, 0.25f, 0, 1}));
        input.gamepad_sample(2, sample);
        EXPECT_FALSE(input.publish_frame()
                .gamepads[2]
                .button(Input::GamepadButton::South)
                .pressed);
        input.gamepad_sample(2, std::nullopt);
        const auto disconnected = input.publish_frame().gamepads[2];
        EXPECT_FALSE(disconnected.connected);
        EXPECT_TRUE(disconnected.button(Input::GamepadButton::South).released);
        EXPECT_EQ(disconnected.axes, (std::array<float, 6>{}));
    }

    TEST_F(InputTest, IgnoresInvalidControlsAndNonFinitePointerData) {
        input.key_event(Input::Key::Unknown, true);
        input.key_event(static_cast<Input::Key>(-1), true);
        input.mouse_button_event(Input::MouseButton::Count, true);
        input.gamepad_sample(Input::MAX_GAMEPADS, Input::GamepadSample{});
        input.cursor_event({1, 2});
        input.cursor_event({std::numeric_limits<float>::infinity(), 0});
        input.scroll_event({std::numeric_limits<float>::quiet_NaN(), 1});
        const auto frame = input.publish_frame();
        EXPECT_EQ(frame.cursor_position, Math::Vec2(1, 2));
        EXPECT_EQ(frame.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(frame.scroll, Math::Vec2(0));
        for(const auto& key : frame.keys)
            EXPECT_FALSE(key.down);
        for(const auto& button : frame.mouse_buttons)
            EXPECT_FALSE(button.down);
    }

    TEST_F(InputTest, OwnedFramesCanBeConsumedRepeatedlyWithoutLiveInputAccess) {
        std::vector<Input::Frame> recorded;
        input.key_event(Input::Key::W, true);
        recorded.push_back(input.publish_frame());
        recorded.push_back(input.publish_frame());
        input.key_event(Input::Key::W, false);
        recorded.push_back(input.publish_frame());
        const auto consume = [&] {
            unsigned held_steps = 0;
            unsigned presses = 0;
            for(const auto& frame : recorded) {
                held_steps += frame.key(Input::Key::W).down;
                presses += frame.key(Input::Key::W).pressed;
            }
            return std::pair{held_steps, presses};
        };
        EXPECT_EQ(consume(), (std::pair{2u, 1u}));
        input.focus_event(false);
        input.publish_frame();
        EXPECT_EQ(consume(), (std::pair{2u, 1u}));
    }
}
