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

    TEST_F(InputTest, FocusLossVersionSurvivesSkippedFramesAndOnlyChangesOnLoss) {
        const auto initial = input.publish_frame();
        EXPECT_EQ(initial.interruption, 0u);
        input.focus_event(true);
        EXPECT_EQ(input.publish_frame().interruption, initial.interruption);

        input.focus_event(false);
        const auto lost = input.publish_frame();
        EXPECT_EQ(lost.interruption, initial.interruption + 1);
        input.focus_event(false);
        EXPECT_EQ(input.publish_frame().interruption, lost.interruption);
        input.focus_event(true);
        input.focus_event(true);
        input.publish_frame();
        const auto resumed = input.publish_frame();
        EXPECT_TRUE(resumed.focused);
        EXPECT_EQ(resumed.interruption, initial.interruption + 1);

        input.focus_event(false);
        input.focus_event(true);
        const auto coalesced = input.publish_frame();
        EXPECT_TRUE(coalesced.focused);
        EXPECT_EQ(coalesced.interruption, initial.interruption + 2);
        EXPECT_EQ(initial.interruption, 0u);
    }

    TEST_F(InputTest, GateDetectsSkippedFocusLossAndRequiresFreshPressAfterRegain) {
        Input::Gate gate;
        gate.read(input.publish_frame(), true);
        input.key_event(Input::Key::W, true);
        ASSERT_TRUE(gate.read(input.publish_frame(), true).key(Input::Key::W).down);

        input.focus_event(false);
        input.publish_frame();
        input.focus_event(true);
        input.key_event(Input::Key::W, true);
        input.cursor_event({100, 200});
        input.cursor_event({110, 220});
        input.scroll_event({0, 1});
        const auto resumed = input.publish_frame();
        ASSERT_TRUE(resumed.focused);
        ASSERT_TRUE(resumed.key(Input::Key::W).pressed);
        const auto interrupted = gate.read(resumed, true);
        EXPECT_FALSE(interrupted.focused);
        EXPECT_TRUE(interrupted.key(Input::Key::W).released);
        EXPECT_FALSE(interrupted.key(Input::Key::W).down);
        EXPECT_FALSE(interrupted.key(Input::Key::W).pressed);
        EXPECT_EQ(interrupted.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(interrupted.scroll, Math::Vec2(0));
        EXPECT_EQ(gate.read(resumed, true).serial, interrupted.serial);
        EXPECT_FALSE(gate.read(resumed, true).focused);

        const auto acquired = gate.read(input.publish_frame(), true);
        EXPECT_TRUE(acquired.focused);
        EXPECT_FALSE(acquired.key(Input::Key::W).down);
        EXPECT_FALSE(acquired.key(Input::Key::W).pressed);
        EXPECT_FALSE(gate.read(input.publish_frame(), true).key(Input::Key::W).down);
        input.key_event(Input::Key::W, false);
        EXPECT_FALSE(gate.read(input.publish_frame(), true).key(Input::Key::W).released);
        input.key_event(Input::Key::W, true);
        const auto pressed = gate.read(input.publish_frame(), true);
        EXPECT_TRUE(pressed.key(Input::Key::W).down);
        EXPECT_TRUE(pressed.key(Input::Key::W).pressed);
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

    TEST_F(InputTest, FirstConnectedGamepadUsesLowestSlotAndKeepsPublishedSnapshotsStable) {
        EXPECT_FALSE(input.publish_frame().first_connected_gamepad());
        Input::GamepadSample sample;
        input.gamepad_sample(7, sample);
        input.gamepad_sample(4, sample);
        const auto first = input.publish_frame();
        EXPECT_EQ(first.first_connected_gamepad(), 4u);
        input.gamepad_sample(2, sample);
        EXPECT_EQ(input.publish_frame().first_connected_gamepad(), 2u);
        input.gamepad_sample(2, std::nullopt);
        EXPECT_EQ(input.publish_frame().first_connected_gamepad(), 4u);
        input.gamepad_sample(4, std::nullopt);
        EXPECT_EQ(input.publish_frame().first_connected_gamepad(), 7u);
        input.gamepad_sample(7, std::nullopt);
        EXPECT_FALSE(input.publish_frame().first_connected_gamepad());
        EXPECT_EQ(first.first_connected_gamepad(), 4u);
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

    TEST_F(InputTest, PointerGateKeepsKeyboardAndGamepadAndRequiresFreshMousePress) {
        Input::Gate gate;
        Input::Gate downstream;
        Input::GamepadSample pad;
        input.gamepad_sample(0, pad);
        gate.read(input.publish_frame(), true, false);
        input.key_event(Input::Key::W, true);
        input.mouse_button_event(Input::MouseButton::Right, true);
        input.cursor_event({0, 0});
        input.cursor_event({10, 20});
        input.scroll_event({0, 1});
        pad.buttons[0] = true;
        pad.axes[0] = 0.8f;
        input.gamepad_sample(0, pad);
        const auto raw = input.publish_frame();
        EXPECT_TRUE(raw.pointer_enabled);
        auto routed = gate.read(raw, true, false);
        EXPECT_TRUE(routed.focused);
        EXPECT_FALSE(routed.pointer_enabled);
        EXPECT_TRUE(routed.key(Input::Key::W).pressed);
        EXPECT_TRUE(routed.gamepads[0].buttons[0].pressed);
        EXPECT_EQ(routed.gamepads[0].axes[0], 0.8f);
        EXPECT_FALSE(routed.mouse(Input::MouseButton::Right).down);
        EXPECT_EQ(routed.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(routed.scroll, Math::Vec2(0));
        const auto forwarded = downstream.read(routed, true, true);
        EXPECT_TRUE(forwarded.focused);
        EXPECT_FALSE(forwarded.pointer_enabled);
        EXPECT_EQ(downstream.read(routed, true, true).serial, forwarded.serial);

        routed = gate.read(raw, true, true);
        EXPECT_TRUE(routed.pointer_enabled);
        EXPECT_TRUE(routed.key(Input::Key::W).down);
        EXPECT_FALSE(routed.key(Input::Key::W).pressed);
        EXPECT_FALSE(routed.mouse(Input::MouseButton::Right).down);
        EXPECT_EQ(routed.cursor_delta, Math::Vec2(0));
        EXPECT_EQ(routed.scroll, Math::Vec2(0));
        EXPECT_EQ(gate.read(raw, true, true).serial, routed.serial);
        const auto restored = downstream.read(routed, true, true);
        EXPECT_TRUE(restored.pointer_enabled);
        EXPECT_FALSE(restored.mouse(Input::MouseButton::Right).down);
        EXPECT_EQ(restored.cursor_delta, Math::Vec2(0));
        input.mouse_button_event(Input::MouseButton::Right, false);
        gate.read(input.publish_frame(), true, true);
        input.mouse_button_event(Input::MouseButton::Right, true);
        input.cursor_event({15, 25});
        input.scroll_event({0, 2});
        const auto inside = input.publish_frame();
        routed = gate.read(inside, true, true);
        EXPECT_TRUE(routed.mouse(Input::MouseButton::Right).pressed);
        EXPECT_EQ(routed.cursor_delta, Math::Vec2(5, 5));
        EXPECT_EQ(routed.scroll, Math::Vec2(0, 2));
        routed = gate.read(inside, true, false);
        EXPECT_FALSE(routed.pointer_enabled);
        EXPECT_FALSE(downstream.read(routed, true, true).pointer_enabled);
        EXPECT_TRUE(routed.mouse(Input::MouseButton::Right).released);
        EXPECT_FALSE(routed.mouse(Input::MouseButton::Right).down);
        EXPECT_TRUE(routed.key(Input::Key::W).down);
        EXPECT_EQ(routed.scroll, Math::Vec2(0));
    }

    TEST_F(InputTest, GateSnapshotRevisesUnconsumedFallbackWithoutLosingOtherChannels) {
        Input::Gate gate;
        Input::GamepadSample pad;
        input.gamepad_sample(0, pad);
        gate.read(input.publish_frame(), true);
        input.key_event(Input::Key::K, true);
        input.mouse_button_event(Input::MouseButton::Right, true);
        pad.buttons[0] = true;
        input.gamepad_sample(0, pad);
        const auto raw = input.publish_frame();
        const auto before_ui = gate;
        const auto fallback = gate.read(raw, true);
        EXPECT_TRUE(fallback.mouse(Input::MouseButton::Right).pressed);

        gate = before_ui;
        const auto final = gate.read(raw, true, false);
        EXPECT_EQ(final.serial, fallback.serial);
        EXPECT_TRUE(final.key(Input::Key::K).pressed);
        EXPECT_TRUE(final.gamepads[0].buttons[0].pressed);
        EXPECT_FALSE(final.mouse(Input::MouseButton::Right).down);
        const auto deferred = gate.read(input.publish_frame(), true, false);
        EXPECT_FALSE(deferred.key(Input::Key::K).pressed);
        EXPECT_TRUE(deferred.key(Input::Key::K).down);

        const auto next_raw = input.publish_frame();
        const auto before_modal = gate;
        gate.read(next_raw, true);
        gate = before_modal;
        const auto modal = gate.read(next_raw, false);
        EXPECT_TRUE(modal.key(Input::Key::K).released);
        EXPECT_FALSE(modal.gamepads[0].buttons[0].down);
        const auto reacquired = gate.read(input.publish_frame(), true);
        EXPECT_FALSE(reacquired.key(Input::Key::K).down);
        EXPECT_FALSE(reacquired.mouse(Input::MouseButton::Right).down);
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
