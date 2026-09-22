#include "input/input.h"

#include <gtest/gtest.h>
#include <limits>
#include <type_traits>

namespace Comet::Tests {
    static_assert(std::is_trivially_copyable_v<Input::Frame>);

    class InputTest: public testing::Test {
    protected:
        Input input;
        void SetUp() override { input.focus_event(true); }
    };

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

    TEST_F(InputTest, SamplingInterruptionReleasesThenReacquiresEvenIfFramesWereSkipped) {
        Input::Gate gate;
        gate.read(input.publish_frame(), true);
        input.key_event(Input::Key::W, true);
        ASSERT_TRUE(gate.read(input.publish_frame(), true).key(Input::Key::W).down);
        input.cursor_event({0, 0});
        input.cursor_event({10, 10});
        input.scroll_event({1, 1});
        input.discard_pending();
        input.publish_frame();
        const auto resumed = input.publish_frame();
        EXPECT_EQ(resumed.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(resumed.scroll, Math::Vec2(0));
        const auto released = gate.read(resumed, true);
        EXPECT_TRUE(released.key(Input::Key::W).released);
        EXPECT_FALSE(released.key(Input::Key::W).down);
        EXPECT_EQ(gate.read(resumed, true).serial, released.serial);
        EXPECT_FALSE(gate.read(input.publish_frame(), true).key(Input::Key::W).down);
        input.key_event(Input::Key::W, false);
        gate.read(input.publish_frame(), true);
        input.key_event(Input::Key::W, true);
        EXPECT_TRUE(gate.read(input.publish_frame(), true).key(Input::Key::W).pressed);
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
        EXPECT_TRUE(connected.button(Input::GamepadButton::South).down);
        EXPECT_FALSE(connected.button(Input::GamepadButton::South).pressed);
        EXPECT_EQ(connected.axes, (std::array<float, 6>{1, 0, -1, 0.25f, 0, 1}));
        input.gamepad_sample(2, sample);
        EXPECT_FALSE(input.publish_frame().gamepads[2].button(Input::GamepadButton::South).pressed);
        input.gamepad_sample(2, std::nullopt);
        const auto disconnected = input.publish_frame().gamepads[2];
        EXPECT_FALSE(disconnected.connected);
        EXPECT_TRUE(disconnected.button(Input::GamepadButton::South).released);
        EXPECT_EQ(disconnected.axes, (std::array<float, 6>{}));
        input.gamepad_sample(2, sample);
        EXPECT_FALSE(input.publish_frame().gamepads[2].button(Input::GamepadButton::South).pressed);
        sample.buttons[0] = false;
        input.gamepad_sample(2, sample);
        EXPECT_TRUE(input.publish_frame().gamepads[2].button(Input::GamepadButton::South).released);
        sample.buttons[0] = true;
        input.gamepad_sample(2, sample);
        EXPECT_TRUE(input.publish_frame().gamepads[2].button(Input::GamepadButton::South).pressed);
    }

    TEST_F(InputTest, GateReleasesControlsAndRequiresFreshPressAfterAcquisition) {
        Input::Gate gate;
        input.key_event(Input::Key::W, true);
        input.mouse_button_event(Input::MouseButton::Right, true);
        input.scroll_event({0, 2});
        auto raw = input.publish_frame();
        EXPECT_FALSE(gate.read(raw, false).focused);
        const auto acquired = gate.read(raw, true);
        EXPECT_TRUE(acquired.focused);
        EXPECT_FALSE(acquired.key(Input::Key::W).down);
        EXPECT_FALSE(acquired.mouse(Input::MouseButton::Right).down);
        EXPECT_EQ(acquired.scroll, Math::Vec2(0));
        EXPECT_EQ(gate.read(raw, true).serial, acquired.serial);
        EXPECT_FALSE(gate.read(input.publish_frame(), true).key(Input::Key::W).down);
        input.key_event(Input::Key::W, false);
        EXPECT_FALSE(gate.read(input.publish_frame(), true).key(Input::Key::W).released);
        input.key_event(Input::Key::W, true);
        raw = input.publish_frame();
        EXPECT_TRUE(gate.read(raw, true).key(Input::Key::W).pressed);
        const auto closed = gate.read(raw, false);
        EXPECT_TRUE(closed.key(Input::Key::W).released);
        EXPECT_FALSE(closed.key(Input::Key::W).down);
        EXPECT_FALSE(gate.read(input.publish_frame(), false).key(Input::Key::W).released);
        EXPECT_TRUE(raw.key(Input::Key::W).down);
    }

    TEST_F(InputTest, GateHandlesGamepadsFocusAndNewInputSourcesWithoutReplayingMotion) {
        Input::Gate gate;
        Input::GamepadSample pad;
        pad.buttons[0] = true;
        pad.axes[0] = 0.8f;
        input.gamepad_sample(0, pad);
        EXPECT_EQ(gate.read(input.publish_frame(), true).gamepads[0].axes[0], 0);
        auto frame = gate.read(input.publish_frame(), true);
        EXPECT_EQ(frame.gamepads[0].axes[0], 0.8f);
        EXPECT_FALSE(frame.gamepads[0].buttons[0].down);
        pad.buttons[0] = false;
        input.gamepad_sample(0, pad);
        gate.read(input.publish_frame(), true);
        pad.buttons[0] = true;
        input.gamepad_sample(0, pad);
        EXPECT_TRUE(gate.read(input.publish_frame(), true).gamepads[0].buttons[0].pressed);
        input.focus_event(false);
        frame = gate.read(input.publish_frame(), true);
        EXPECT_FALSE(frame.focused);
        EXPECT_TRUE(frame.gamepads[0].buttons[0].released);
        EXPECT_EQ(frame.gamepads[0].axes[0], 0);
        Input replacement;
        replacement.focus_event(true);
        replacement.scroll_event({0, 10});
        EXPECT_EQ(gate.read(replacement.publish_frame(), true).scroll, Math::Vec2(0));
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

}
